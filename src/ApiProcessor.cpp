#include "ApiProcessor.hpp"

#include <utils.hpp>

#include <nlohmann/json.hpp>
#include <uWS.h>

#include <chrono>
#include <iostream>

using Endpoint = ApiProcessor::Endpoint;

using TemplatedEndpointBuilder = ApiProcessor::TemplatedEndpointBuilder;

ApiProcessor::ApiProcessor(uWS::Hub& h) {
	// one connection can request multiple things before it closes
	h.onHttpRequest([this] (uWS::HttpResponse * res, uWS::HttpRequest req, char * data, sz_t len, sz_t rem) {
		//auto now(std::chrono::high_resolution_clock::now());
		ll::shared_ptr<Request> * rs = static_cast<ll::shared_ptr<Request> *>(res->getHttpSocket()->getUserData());

		if (!rs) {
			// we want to crash if this throws anyways, so no leak should be possible
			rs = new ll::shared_ptr<Request>(new Request(res, &req));
			res->getHttpSocket()->setUserData(rs);
		} else {
			res->hasHead = false;
			(*rs)->updateData(res, &req);
		}

		std::string_view body(data, len);

		std::vector<std::string> args;
		{
			auto urlh = req.getUrl();
			std::string_view url(urlh.value, urlh.valueLength);

			auto it = url.find('?'); // separate query params from url
			if (it != std::string_view::npos) {
				url.remove_suffix(url.size() - it);
			}

			auto argviews(tokenize(url, '/', true));
			args.reserve(argviews.size());
			for (auto& s : argviews) {
				args.emplace_back(s);
			}
		}

		try {
			urldecode(args);
		} catch (const std::exception& e) {
			(*rs)->writeStatus("400 Bad Request");
			(*rs)->end({
				{"reason", e.what()}
			});

			(*rs)->invalidateData();
			return;
		}

		exec(*rs, body, std::move(args));
		(*rs)->invalidateData();
		//std::cout << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - now).count() << std::endl;
	});

	h.onCancelledHttpRequest([] (uWS::HttpResponse * res) {
		// requests only get cancelled when the requester disconnects, right?
		if (auto * rs = static_cast<ll::shared_ptr<Request> *>(res->getHttpSocket()->getUserData())) {
			Request& rref = **rs;
			rref.cancel(std::move(*rs));
			delete rs;
			res->getHttpSocket()->setUserData(nullptr);
		}
	});

	h.onHttpDisconnection([] (uWS::HttpSocket<uWS::SERVER> * s) {
		// this library is retarded so the disconnection handler is called
		// before the cancelled requests handler, so i need to do hacky things
		// if i don't want to use freed memory (there's no request completion handler), cool!
		ll::shared_ptr<Request> * rs = static_cast<ll::shared_ptr<Request> *>(s->getUserData());
		if (!s->outstandingResponsesHead) {
			// seems like no cancelled request handler will be called
			delete rs; // note: deleting null is ok
		}
	});
}

TemplatedEndpointBuilder ApiProcessor::on(ApiProcessor::Method m) {
	return TemplatedEndpointBuilder(*this, m);
}

void ApiProcessor::add(ApiProcessor::Method m, std::unique_ptr<Endpoint> ep) {
	definedEndpoints[m].emplace_back(std::move(ep));
}

std::map<std::string_view, std::string_view, std::less<>> ApiProcessor::getPostParameters(std::string_view from) {
	std::map<std::string_view, std::string_view, std::less<>> result;
	auto vec = tokenize(from, '&', true);
	for (auto param : vec) {
		sz_t pos = param.find_first_of('=');
		if (pos != std::string_view::npos && pos + 1 < param.size()) {
			result.emplace(param.substr(0, pos), param.substr(pos + 1));
		}
	}

	return result;
}

void ApiProcessor::exec(ll::shared_ptr<Request> r, std::string_view body, std::vector<std::string> args) {
	int m = r->getData()->getMethod(); // lol

	if (m == ApiProcessor::Method::MINVALID) {
		r->writeStatus("400 Bad Request");
		r->end();
		return;
	}

	for (auto& ep : definedEndpoints[m]) {
		if (ep->verify(args)) {
			Request& rref = *r.get();
			try {
				ep->exec(std::move(r), body, std::move(args));
			} catch (const std::exception& e) {
				// The request wasn't freed yet. (1 ref left in socket userdata)
				if (!rref.isCompleted()) {
					rref.clearBufferedData(); // in case something was written
					rref.writeStatus("400 Bad Request");
					rref.end({
						{"reason", e.what()}
					});
				}
			}
			return;
		}
	}

	r->writeStatus("400 Bad Request");
	r->end();
}

Request::Request(uWS::HttpResponse * res, uWS::HttpRequest * req)
: HttpData(req),
  cancelHandler(nullptr),
  res(res),
  isProxied(false),
  ended(false) {
  	auto addr = res->getHttpSocket()->getAddress();
  	if (addr.family[0] == 'U') {
  		isProxied = true;
  	} else {
  		ip = Ip(addr.address);
  		isProxied = ip.isLocal();
  	}

	maybeUpdateIp();
}

Ip Request::getIp() const {
	return ip;
}

uWS::HttpResponse * Request::getResponse() {
	return res;
}

void Request::setCookie(std::string_view key, std::string_view value, std::vector<std::string_view> directives) {
	write("Set-Cookie: ", 12);
	bufferedData += key;
	bufferedData += '=';
	bufferedData += value;
	for (const auto& s : directives) {
		bufferedData += "; ";
		bufferedData += s;
	}

	write("\r\n", 2);
}

void Request::delCookie(std::string_view key, std::vector<std::string_view> directives) {
	directives.emplace_back("expires=Thu, 01 Jan 1970 00:00:00 GMT");
	setCookie(key, "", directives);
}

void Request::clearBufferedData() {
	bufferedData.clear();
}

void Request::writeStatus(std::string_view s) {
	write("HTTP/1.1 ", 9);
	write(s.data(), s.size());
	write("\r\n", 2);
}

void Request::writeHeader(std::string_view key, std::string_view value) {
	write(key.data(), key.size());
	write(": ", 2);
	write(value.data(), value.size());
	write("\r\n", 2);
}

void Request::end(const char * buf, sz_t size) {
	if (bufferedData.size()) {
		writeHeader("Content-Length", std::to_string(size));
		write("\r\n", 2);
	}

	writeAndEnd(buf, size);
}

void Request::end(nlohmann::json j) {
	std::string s(j.dump());
	if (!bufferedData.size()) {
		writeStatus("200 OK");
	}

	writeHeader("Content-Type", "application/json");
	end(s.data(), s.size());
}

void Request::end() {
	if (!bufferedData.size()) {
		res->end();
		ended = true;
	} else {
		writeAndEnd("Content-Length: 0\r\n\r\n", 21);
	}
}

bool Request::isCancelled() const {
	return res == nullptr;
}

bool Request::isCompleted() const {
	return isCancelled() || ended;
}

void Request::onCancel(std::function<void(ll::shared_ptr<Request>)> f) {
	cancelHandler = std::move(f);
}

void Request::write(const char * b, sz_t s) {
	bufferedData.append(b, s);
}

void Request::writeAndEnd(const char * b, sz_t s) {
	if (!bufferedData.size()) {
		res->end(b, s);
	} else {
		res->hasHead = true;
		bufferedData.append(b, s);
		res->end(bufferedData.data(), bufferedData.size());
	}

	ended = true;
}

void Request::cancel(ll::shared_ptr<Request> r) {
	// careful, this could be the last reference
	res = nullptr;
	if (cancelHandler) {
		cancelHandler(std::move(r));
	}
}

void Request::updateData(uWS::HttpResponse * res, uWS::HttpRequest * req) {
	this->res = res;
	HttpData::updateData(req);
	cancelHandler = nullptr;
	bufferedData.clear();
	ended = false;
	maybeUpdateIp();
}

void Request::invalidateData() {
	HttpData::invalidateData();
}

void Request::maybeUpdateIp() {
	if (isProxied) {
		if (auto realIp = getHeader("x-real-ip")) {
			// copy str and call const std::string& constructor
			ip = Ip(std::string(*realIp));
		}
	}
}


TemplatedEndpointBuilder::TemplatedEndpointBuilder(ApiProcessor& tc, ApiProcessor::Method m)
: targetClass(tc),
  method(m) { }

TemplatedEndpointBuilder& TemplatedEndpointBuilder::path(std::string s) {
	if (s.size() == 0) {
		throw std::runtime_error("Path sections can't be empty!");
	}

	varMarkers.emplace_back(std::move(s));
	return *this;
}

TemplatedEndpointBuilder& TemplatedEndpointBuilder::var() {
	varMarkers.emplace_back("");
	return *this;
}

Endpoint::Endpoint() { }
Endpoint::~Endpoint() { }

void Endpoint::exec(ll::shared_ptr<Request> req, std::string_view body, std::vector<std::string> arg) {
	// yeah this shouldn't happen
	req->writeStatus("501 Not Implemented");
	req->end();
}
