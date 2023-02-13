#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>

#include "Poll.hpp"

class CurlHandle;

class AsyncCurl {
public:
	class Result;

private:
	std::string localSmtpUrl;
	std::string smtpSenderName;
	nev::Loop& loop;
	std::unique_ptr<nev::Timer> timer;
	void * multiHandle;
	int handleCount;
	std::unordered_set<CurlHandle *> pendingRequests;
	bool isTimerRunning;

public:
	AsyncCurl(nev::Loop&);
	~AsyncCurl();

	int activeHandles() const;
	int queuedRequests() const;

	void smtpSendMail(const std::string& url, const std::string& from, const std::string& to, const std::string& subject, const std::string& message, std::function<void(AsyncCurl::Result)>);
	void smtpSendMail(const std::string& url, const std::string& to, const std::string& subject, const std::string& message, std::function<void(AsyncCurl::Result)>);
	void smtpRelay(const std::string& to, const std::string& subject, const std::string& message, std::function<void(AsyncCurl::Result)>);
	void smtpSetRelayUrl(std::string);
	void smtpSetSenderName(std::string);

	void httpGet(std::string url, std::unordered_map<std::string_view, std::string_view> params, std::function<void(AsyncCurl::Result)>);
	void httpGet(std::string url, std::function<void(AsyncCurl::Result)>);
	/* No cancelRequest(), see: https://github.com/curl/curl/issues/2101 */

	static std::string urlEscape(std::string_view);

private:
	void update();
	void processCompleted();
	void startTimer(long);
	void stopTimer();
};

class AsyncCurl::Result {
public:
	const bool successful;
	const long responseCode;
	std::string data; // not const so it's possible to move it
	const char * errorString;

	Result(long, std::string, const char *);
};
