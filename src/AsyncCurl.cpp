#include "AsyncCurl.hpp"

#include <algorithm>
#include <memory>
#include <iostream>

#include <uWS.h>
#include <curl/curl.h>
#include <utils.hpp>

/* FIXME: Name resolves seem to be blocking (see: c-ares) */

void mc(CURLMcode code) {
	if (code != CURLM_OK && code != CURLM_BAD_SOCKET) {
		throw std::runtime_error(std::string("CURLM call failed: ") + curl_multi_strerror(code));
	}
}

void ec(CURLcode code) {
	if (code != CURLE_OK) {
		throw std::runtime_error(std::string("CURL call failed: ") + curl_easy_strerror(code));
	}
}

static struct CurlRaii {
	CurlRaii() { ec(curl_global_init(CURL_GLOBAL_DEFAULT)); }
	~CurlRaii() { curl_global_cleanup(); }
} autoCleanup;

static int noopWriter(char *, std::size_t s, std::size_t n, void *) { return s * n; }
static int noopReader(char *, std::size_t, std::size_t, void *) { return 0; }


inline int curlEvToUv(int curlEvents) {
	return (curlEvents & CURL_POLL_IN ? UV_READABLE : 0)
		| (curlEvents & CURL_POLL_OUT ? UV_WRITABLE : 0);
}

inline int uvEvToCurl(int uvEvents) {
	return (uvEvents & UV_READABLE ? CURL_CSELECT_IN : 0)
		| (uvEvents & UV_WRITABLE ? CURL_CSELECT_OUT : 0);
}

class CurlHandle {
	CURLM * multiHandle;
	CURL * easyHandle;

public:
	CurlHandle(CURLM *);
	virtual ~CurlHandle();

	CURL * getHandle();

protected:
	virtual bool finished(CURLcode result); /* Returns false if error occurred */
	void addToMultiHandle();

	friend AsyncCurl;
};

class CurlHttpHandle : public CurlHandle {
	std::string writeBuffer;
	std::function<void(AsyncCurl::Result)> onFinished;

public:
	CurlHttpHandle(CURLM *, std::string, std::unordered_map<std::string_view, std::string_view>,
		std::function<void(AsyncCurl::Result)>);

private:
	bool finished(CURLcode result);
	static sz_t writer(char *, sz_t, sz_t, std::string *);
};

class CurlSmtpHandle : public CurlHandle {
	std::string readBuffer;
	std::unique_ptr<curl_slist, void(*)(curl_slist *)> mailRcpts;
	sz_t amountSent;
	std::function<void(AsyncCurl::Result)> onFinished;

public:
	CurlSmtpHandle(CURLM *, const std::string& url, const std::string& from, const std::string& to,
		const std::string& subject, const std::string& message, const std::string& smtpSenderName, std::function<void(AsyncCurl::Result)>);

private:
	bool finished(CURLcode result);
	static sz_t reader(char *, sz_t, sz_t, CurlSmtpHandle *);
};

class CurlSocket : public uS::Poll {
	AsyncCurl * ah;
	void (*cb)(AsyncCurl *, CurlSocket *, int, int);

public:
	CurlSocket(uS::Loop * loop, AsyncCurl * ah, curl_socket_t fd)
	: Poll(loop, static_cast<int>(fd)),
	  ah(ah) { }

	void start(uS::Loop * loop, int events, void (*callback)(AsyncCurl *, CurlSocket *, int status, int events)) {
		cb = callback;
		Poll::setCb([] (Poll * p, int s, int e) {
			CurlSocket * cs = static_cast<CurlSocket *>(p);
			cs->cb(cs->ah, cs, s, uvEvToCurl(e));
		});

		Poll::start(loop, this, curlEvToUv(events));
	}

	void change(uS::Loop * loop, int events) {
		Poll::change(loop, this, curlEvToUv(events));
	}

	void close(uS::Loop * loop) {
		Poll::stop(loop);
		Poll::close(loop, [] (Poll * p) {
			delete static_cast<CurlSocket *>(p);
		});
	}
};

AsyncCurl::Result::Result(long httpResp, std::string data, const char * err)
: successful(err == nullptr),
  responseCode(httpResp),
  data(std::move(data)),
  errorString(err) { }

CurlHandle::CurlHandle(CURLM * mHdl)
: multiHandle(mHdl),
  easyHandle(curl_easy_init()) {
	if (!easyHandle) {
		throw std::bad_alloc();
	}

	ec(curl_easy_setopt(easyHandle, CURLOPT_PRIVATE, this));
	ec(curl_easy_setopt(easyHandle, CURLOPT_WRITEFUNCTION, noopWriter));
	ec(curl_easy_setopt(easyHandle, CURLOPT_READFUNCTION, noopReader));

	//ec(curl_easy_setopt(easyHandle, CURLOPT_TIMEOUT, 60)); // idk if this caused any problems
	//ec(curl_easy_setopt(easyHandle, CURLOPT_TCP_FASTOPEN, 1)); // makes http requests fail?
	ec(curl_easy_setopt(easyHandle, CURLOPT_TCP_NODELAY, 0));
	ec(curl_easy_setopt(easyHandle, CURLOPT_PIPEWAIT, 1));
	//ec(curl_easy_setopt(easyHandle, CURLOPT_VERBOSE, 1));
}

CurlHandle::~CurlHandle() {
	if (multiHandle) {
		mc(curl_multi_remove_handle(multiHandle, easyHandle));
	}
	curl_easy_cleanup(easyHandle);
}

CURL * CurlHandle::getHandle() {
	return easyHandle;
}

bool CurlHandle::finished(CURLcode result) { return true; }

void CurlHandle::addToMultiHandle() {
	mc(curl_multi_add_handle(multiHandle, easyHandle));
}


CurlHttpHandle::CurlHttpHandle(CURLM * mHdl, std::string url, std::unordered_map<std::string_view, std::string_view> params, std::function<void(AsyncCurl::Result)> cb)
: CurlHandle(mHdl),
  onFinished(std::move(cb)) {
	bool first = true;
	for (const auto& param : params) {
		url += first ? '?' : '&';
		first = false;
		url += param.first; // should this be escaped as well?
		url += '=';
		url += AsyncCurl::urlEscape(param.second);
	}

	ec(curl_easy_setopt(getHandle(), CURLOPT_WRITEFUNCTION, &CurlHttpHandle::writer));
	ec(curl_easy_setopt(getHandle(), CURLOPT_WRITEDATA, &writeBuffer));
	ec(curl_easy_setopt(getHandle(), CURLOPT_URL, url.c_str()));

	addToMultiHandle();
}

bool CurlHttpHandle::finished(CURLcode result) {
	bool successful = result == CURLE_OK;
	long responseCode = -1;
	ec(curl_easy_getinfo(getHandle(), CURLINFO_RESPONSE_CODE, &responseCode));

	onFinished({responseCode, std::move(writeBuffer),
			successful ? nullptr : curl_easy_strerror(result)});

	return successful;
}

sz_t CurlHttpHandle::writer(char * data, std::size_t size, std::size_t nmemb, std::string * writerData) {
	// writerData will never be null
	writerData->append(data, size * nmemb);

	return size * nmemb;
}

CurlSmtpHandle::CurlSmtpHandle(CURLM * mHdl, const std::string& url, const std::string& from, const std::string& to,
		const std::string& subject, const std::string& message, const std::string& smtpSenderName, std::function<void(AsyncCurl::Result)> cb)
: CurlHandle(mHdl),
  mailRcpts(curl_slist_append(nullptr, to.c_str()), curl_slist_free_all),
  amountSent(0),
  onFinished(std::move(cb)) {
	CURL * curl = getHandle();
	ec(curl_easy_setopt(curl, CURLOPT_URL, url.c_str()));
	ec(curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from.c_str()));
	ec(curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, mailRcpts.get()));

	ec(curl_easy_setopt(curl, CURLOPT_READFUNCTION, &CurlSmtpHandle::reader));
	ec(curl_easy_setopt(curl, CURLOPT_READDATA, this));

	ec(curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L));

	readBuffer.reserve(from.size() + to.size() + subject.size() + message.size() + smtpSenderName.size() + 7 + 3 + 5 + 3 + 9 + 4 + 2);

	if (smtpSenderName.size() > 0) {
		readBuffer += "From: ";
		readBuffer += smtpSenderName;
		readBuffer += " <"; // 7
	} else {
		readBuffer += "From: <"; // 7
	}

	readBuffer += from;
	readBuffer += ">\r\n"; // 3

	readBuffer += "To: <"; // 5
	readBuffer += to;
	readBuffer += ">\r\n"; // 3

	readBuffer += "Subject: "; // 9
	readBuffer += subject;
	readBuffer += "\r\n\r\n"; // 4

	readBuffer += message;

	addToMultiHandle();
}

bool CurlSmtpHandle::finished(CURLcode result) {
	bool successful = result == CURLE_OK;
	long responseCode = -1;
	ec(curl_easy_getinfo(getHandle(), CURLINFO_RESPONSE_CODE, &responseCode));

	onFinished({responseCode, {}, successful ? nullptr : curl_easy_strerror(result)});

	return successful;
}

sz_t CurlSmtpHandle::reader(char * buf, std::size_t size, std::size_t nmemb, CurlSmtpHandle * d) {
	sz_t off = d->amountSent;
	sz_t toRead = std::min(size * nmemb, d->readBuffer.size() - off);

	std::copy_n(d->readBuffer.c_str() + off, toRead, buf);

	d->amountSent += toRead;
	return toRead;
}

AsyncCurl::AsyncCurl(uS::Loop * loop)
: localSmtpUrl("smtp://localhost/" + std::string(getDomainname())),
  loop(loop),
  timer(new uS::Timer(loop)),
  multiHandle(curl_multi_init()),
  handleCount(0),
  isTimerRunning(false) {
	if (!multiHandle) {
		throw std::bad_alloc();
	}

	timer->setData(this);

	mc(curl_multi_setopt(multiHandle, CURLMOPT_PIPELINING, CURLPIPE_HTTP1 | CURLPIPE_MULTIPLEX));
	mc(curl_multi_setopt(multiHandle, CURLMOPT_MAX_TOTAL_CONNECTIONS, 8));
	mc(curl_multi_setopt(multiHandle, CURLMOPT_MAX_HOST_CONNECTIONS, 1));
	mc(curl_multi_setopt(multiHandle, CURLMOPT_MAXCONNECTS, 8));

	mc(curl_multi_setopt(multiHandle, CURLMOPT_TIMERDATA, this));
	mc(curl_multi_setopt(multiHandle, CURLMOPT_TIMERFUNCTION, +[] (CURLM * multi, long tmo_ms, void * u) -> int {
		// notice the plus sign on the lambda definition, needed to get the pointer to it
		AsyncCurl * ah = static_cast<AsyncCurl *>(u);
		switch (tmo_ms) {
			case -1:
				ah->stopTimer();
				break;

			case 0:
				mc(curl_multi_socket_action(ah->multiHandle, CURL_SOCKET_TIMEOUT, 0, &ah->handleCount));
				break;

			default:
				ah->startTimer(tmo_ms);
				break;
		}

		return 0;
	}));

	mc(curl_multi_setopt(multiHandle, CURLMOPT_SOCKETDATA, this));
	mc(curl_multi_setopt(multiHandle, CURLMOPT_SOCKETFUNCTION, +[] (CURL * e, curl_socket_t s, int what, void * up, void * sp) -> int {
		// listen for event what on socket s of easy handle e, s priv data on sp
		AsyncCurl * ah = static_cast<AsyncCurl *>(up);
		CurlSocket * cs = static_cast<CurlSocket *>(sp);

		if (what == CURL_POLL_REMOVE) {
			if (cs) {
				cs->close(ah->loop); // also deletes when the event loop is ready
				mc(curl_multi_assign(ah->multiHandle, s, nullptr));
			}
		} else {
			if (!cs) {
				cs = new CurlSocket(ah->loop, ah, s);
				cs->start(ah->loop, what, +[] (AsyncCurl * ah, CurlSocket * cs, int status, int events) {
					mc(curl_multi_socket_action(ah->multiHandle, reinterpret_cast<curl_socket_t>(cs->getFd()), events, &ah->handleCount));
					ah->processCompleted();

					if (ah->handleCount == 0) {
						ah->stopTimer();
					}
				});

				mc(curl_multi_assign(ah->multiHandle, s, cs));
			} else {
				cs->change(ah->loop, what);
			}
		}

		return 0;
	}));
}

AsyncCurl::~AsyncCurl() {
	stopTimer();
	timer->close(); /* This deletes the timer */

	for (CurlHandle * ch : pendingRequests) {
		delete ch;
	}

	mc(curl_multi_cleanup(multiHandle));
}

int AsyncCurl::activeHandles() const {
	return handleCount;
}

int AsyncCurl::queuedRequests() const {
	return pendingRequests.size();
}

void AsyncCurl::smtpSendMail(const std::string& url, const std::string& from, const std::string& to,
		const std::string& subject, const std::string& message, std::function<void(AsyncCurl::Result)> onFinished) {
	pendingRequests.emplace(new CurlSmtpHandle(multiHandle, url,
		from, to, subject, message, smtpSenderName, std::move(onFinished)));
	//update();
}

void AsyncCurl::smtpSendMail(const std::string& url, const std::string& to, const std::string& subject,
		const std::string& message, std::function<void(AsyncCurl::Result)> onFinished) {
	static const std::string self = std::string(getUsername()) + "@" + std::string(getHostname());
	smtpSendMail(url, self, to, subject, message, std::move(onFinished));
}

void AsyncCurl::smtpRelay(const std::string& to, const std::string& subject, const std::string& message,
		std::function<void(AsyncCurl::Result)> onFinished) {
	smtpSendMail(localSmtpUrl, to, subject, message, std::move(onFinished));
}

void AsyncCurl::smtpSetRelayUrl(std::string newUrl) {
	localSmtpUrl = std::move(newUrl);
}

void AsyncCurl::smtpSetSenderName(std::string newName) {
	smtpSenderName = std::move(newName);
}



void AsyncCurl::httpGet(std::string url, std::unordered_map<std::string_view, std::string_view> params,
		std::function<void(AsyncCurl::Result)> onFinished) {
	pendingRequests.emplace(new CurlHttpHandle(multiHandle, std::move(url), std::move(params), std::move(onFinished)));
	//update();
}

void AsyncCurl::httpGet(std::string url, std::function<void(AsyncCurl::Result)> onFinished) {
	httpGet(std::move(url), {}, std::move(onFinished));
}

// lots of allocs can happen here, bad!
std::string AsyncCurl::urlEscape(std::string_view str) {
	static CurlHandle escHandle(nullptr);
	std::string result;
	if (char * escaped = curl_easy_escape(escHandle.getHandle(), str.data(), str.size())) {
		result = escaped;
		curl_free(escaped);
	} else {
		throw std::bad_alloc();
	}

	return result;
}


void AsyncCurl::update() {
	mc(curl_multi_socket_action(multiHandle, CURL_SOCKET_TIMEOUT, 0, &handleCount));
	processCompleted();
}

void AsyncCurl::processCompleted() {
	int queued;
	CurlHandle * hdl;

	while (CURLMsg * m = curl_multi_info_read(multiHandle, &queued)) {
		if (m->msg == CURLMSG_DONE) {
			ec(curl_easy_getinfo(m->easy_handle, CURLINFO_PRIVATE, &hdl));

			auto search = pendingRequests.find(hdl);

			if (search != pendingRequests.end()) {
				pendingRequests.erase(search);
			}

			hdl->finished(m->data.result);
			delete hdl;
		}
	}
}

void AsyncCurl::startTimer(long timeout) {
	if (isTimerRunning) {
		stopTimer();
	}

	if (!isTimerRunning) {
		timer->start(&AsyncCurl::timerCallback, timeout, timeout);
		isTimerRunning = true;
	}
}

void AsyncCurl::stopTimer() {
	if (isTimerRunning) {
		timer->stop();
		isTimerRunning = false;
	}
}

void AsyncCurl::timerCallback(uS::Timer * timer) {
	AsyncCurl * const http = static_cast<AsyncCurl *>(timer->getData());
	http->stopTimer();
	http->update();
}
