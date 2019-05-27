#pragma once

#include <string>
#include <string_view>
#include <functional>
#include <optional>
#include <chrono>

#include <Ip.hpp>

#include <nlohmann/json_fwd.hpp>

class AsyncCurl;
class TimedCallbacks;

class ProxycheckioRestApi {
	struct Info;

	AsyncCurl& ac;
	const std::string apiKey;
	std::map<Ip, Info> cache;

public:
	ProxycheckioRestApi(AsyncCurl& ac, TimedCallbacks& tc, std::string_view apiKey);

	// optional is empty only if the request failed for some reason
	void check(Ip ip, std::function<void(std::optional<bool>, nlohmann::json)>);
	std::optional<bool> cachedResult(Ip);
};

struct ProxycheckioRestApi::Info {
	bool isProxy;
	std::chrono::steady_clock::time_point checkTime;
};
