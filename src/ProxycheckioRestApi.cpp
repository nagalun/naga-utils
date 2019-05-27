#include "ProxycheckioRestApi.hpp"

#include <iostream>

#include <AsyncCurl.hpp>
#include <TimedCallbacks.hpp>

#include <nlohmann/json.hpp>

ProxycheckioRestApi::ProxycheckioRestApi(AsyncCurl& ac, TimedCallbacks& tc, std::string_view apiKey)
: ac(ac),
  apiKey(apiKey) {
	tc.startTimer([this] {
		auto now(std::chrono::steady_clock::now());

		for (auto it = cache.begin(); it != cache.end();) {
			std::chrono::hours expiryInterval(it->second.isProxy ? 12 : 24);

			if (now - it->second.checkTime > expiryInterval) {
				it = cache.erase(it);
			} else {
				++it;
			}
		}

		return true;
	}, 60 * 60 * 1000);
}

void ProxycheckioRestApi::check(Ip ip, std::function<void(std::optional<bool>, nlohmann::json)> cb) {
	std::optional<bool> result = cachedResult(ip);
	if (result) {
		cb(*result, nullptr);
		return;
	}

	std::string ipstr(ip.toString());

	ac.httpGet(std::string("http://proxycheck.io/v2/").append(ipstr), {
		{"key", apiKey},
	}, [this, ipstr, ip, end{std::move(cb)}] (auto res) {
		if (!res.successful) {
			std::cerr << "Error when checking for proxy: " << res.errorString << ", " << res.data << std::endl;
			end(std::nullopt, nullptr);
			return;
		}

		bool verified = false;
		bool isProxy = false;
		nlohmann::json j;

		try {
			j = nlohmann::json::parse(res.data);
			// std::string st(j["status"].get<std::string>());
			if (j["message"].is_string()) {
				std::cout << "Message from proxycheck.io: " << j["message"].get<std::string>() << std::endl;
			}

			if (j[ipstr].is_object()) {
				verified = true;
				isProxy = j[ipstr]["proxy"].get<std::string>() == "yes";
			}
		} catch (const nlohmann::json::parse_error& e) {
			std::cerr << "Exception when parsing json from proxycheck.io! (" << res.data << ")" << std::endl;
			std::cerr << "what(): " << e.what() << std::endl;
		}

		if (verified) {
			cache.emplace(ip, ProxycheckioRestApi::Info{isProxy, std::chrono::steady_clock::now()});
		}

		end(verified ? std::optional<bool>(isProxy) : std::nullopt, std::move(j));
	});
}

std::optional<bool> ProxycheckioRestApi::cachedResult(Ip ip) {
	auto search = cache.find(ip);
	return search == cache.end() ? std::nullopt : std::optional<bool>(search->second.isProxy);
}
