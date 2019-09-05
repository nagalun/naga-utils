#include "HttpData.hpp"

#include <algorithm>

#include <utils.hpp>
#include <ApiProcessor.hpp>

#include <uWS.h>

HttpData::HttpData(uWS::HttpRequest * req)
: req(req) { }

uWS::HttpRequest * HttpData::getData() {
	return req;
}

std::string_view HttpData::getUrl() {
	auto urlh = req->getUrl();
	return std::string_view(urlh.value, urlh.valueLength);
}

std::optional<std::string_view> HttpData::getQueryParam(std::string_view name) {
	if (req) {
		auto urlh = req->getUrl();
		std::string_view url(urlh.value, urlh.valueLength);

		auto s = url.find('?');
		if (s == std::string_view::npos) {
			return std::nullopt;
		}

		url.remove_prefix(std::min(s + 1, url.size()));

		auto params = ApiProcessor::getPostParameters(url);
		auto param = params.find(name);
		if (param != params.end()) {
			return param->second;
		}
	}

	return std::nullopt;
}

std::optional<std::string> HttpData::getDecodedQueryParam(std::string_view name) {
	auto q = getQueryParam(name);
	if (!q) {
		return std::nullopt;
	}

	std::string qc(*q);
	try {
		urldecode(qc);
	} catch (const std::exception& e) {
		return std::nullopt;
	}

	return qc;
}

std::optional<std::string_view> HttpData::getHeader(std::string_view name) {
	if (!req) {

	} else if (uWS::Header h = req->getHeader(name.data(), name.size())) {
		return std::string_view(h.value, h.valueLength);
	}

	return std::nullopt;
}

std::optional<std::string_view> HttpData::getCookie(std::string_view cname) {
	auto chead = getHeader("cookie");
	if (!chead) {
		return std::nullopt;
	}

	auto cookies(tokenize(*chead, ';', true));
	auto search = std::find_if(cookies.begin(), cookies.end(),
	[cname] (std::string_view s) {
		s.remove_prefix(std::min(s.find_first_not_of(' '), s.size()));
		return strStartsWith(s, cname);
	});

	if (search != cookies.end()) {
		auto s = search->find('=');

		if (s != std::string_view::npos) {
			search->remove_prefix(std::min(s + 1, search->size()));
			return *search;
		}
	}

	return std::nullopt;
}

void HttpData::updateData(uWS::HttpRequest * req) {
	this->req = req;
}

void HttpData::invalidateData() {
	req = nullptr;
}
