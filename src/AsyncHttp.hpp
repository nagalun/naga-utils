#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>

#include <fwd_uWS.h>
#include <explints.hpp>

class CurlSocket;
class CurlHandle;

class AsyncHttp {
public:
	class Result;

private:
	uS::Loop * loop;
	uS::Timer * timer;
	void * multiHandle;
	int handleCount;
	std::unordered_set<CurlHandle *> pendingRequests;
	bool isTimerRunning;

public:
	AsyncHttp(uS::Loop *);
	~AsyncHttp();

	int activeHandles() const;
	int queuedRequests() const;

	void smtpSendMail(const std::string& url, const std::string& from, const std::string& to, const std::string& subject, const std::string& message, std::function<void(AsyncHttp::Result)>);
	void smtpSendMail(const std::string& url, const std::string& to, const std::string& subject, const std::string& message, std::function<void(AsyncHttp::Result)>);

	void httpGet(std::string url, std::unordered_map<std::string_view, std::string_view> params, std::function<void(AsyncHttp::Result)>);
	void httpGet(std::string url, std::function<void(AsyncHttp::Result)>);
	/* No cancelRequest(), see: https://github.com/curl/curl/issues/2101 */

private:
	void update();
	void processCompleted();
	void startTimer(long);
	void stopTimer();
	static void timerCallback(uS::Timer *);
};

class AsyncHttp::Result {
public:
	const bool successful;
	const long responseCode;
	std::string data; // not const so it's possible to move it
	const char * errorString;

	Result(long, std::string, const char *);
};
