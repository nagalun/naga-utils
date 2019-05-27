#pragma once

#include <string>
#include <string_view>
#include <functional>
#include <vector>
#include <optional>

#include <Ip.hpp>

#include <nlohmann/json_fwd.hpp>

class AsyncCurl;

class RecaptchaRestApi {
	AsyncCurl& ac;
	const std::string apiKey;
	std::vector<std::string> allowedHostnames;

public:
	RecaptchaRestApi(AsyncCurl& ac, std::vector<std::string> allowedHostnames, std::string_view apiKey);

	void check(Ip ip, std::string_view token, std::function<void(std::optional<bool>, nlohmann::json)>);
};
