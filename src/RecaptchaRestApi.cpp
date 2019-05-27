#include "RecaptchaRestApi.hpp"

#include <algorithm>
#include <iostream>

#include <AsyncCurl.hpp>

#include <nlohmann/json.hpp>

RecaptchaRestApi::RecaptchaRestApi(AsyncCurl& ac, std::vector<std::string> allowedHostnames, std::string_view apiKey)
: ac(ac),
  apiKey(apiKey),
  allowedHostnames(std::move(allowedHostnames)) { }

void RecaptchaRestApi::check(Ip ip, std::string_view token, std::function<void(std::optional<bool>, nlohmann::json)> cb) {
	ac.httpGet("https://www.google.com/recaptcha/api/siteverify", {
		{"secret", apiKey},
		{"remoteip", ip.toString().data()}, // XXX: fix when json lib supports string views
		{"response", std::string(token)}
	}, [this, end{std::move(cb)}] (auto res) {
		if (!res.successful) {
			/* HTTP ERROR code check */
			std::cerr << "Error occurred when verifying captcha: " << res.errorString << ", " << res.data << std::endl;
			end(std::nullopt, nullptr);
			return;
		}

		bool verified = false;
		//std::string failReason;
		nlohmann::json response;
		try {
			response = nlohmann::json::parse(res.data);
			bool success = response["success"].get<bool>();
			std::string host(response["hostname"].get<std::string>());
			verified = success && std::find(allowedHostnames.begin(), allowedHostnames.end(), host) != allowedHostnames.end();
			/*if (!success) {
				failReason = "API rejected token";
				if (response["error-codes"].is_array()) {
					failReason += " " + response["error-codes"].dump();
				}
			} else if (success && !verified) {
				failReason = "Wrong hostname: '" + host + "'";
			}*/
		} catch (const nlohmann::json::parse_error& e) {
			std::cerr << "Exception when parsing json from google! (" << res.data << ")" << std::endl;
			std::cerr << "what(): " << e.what() << std::endl;

			end(std::nullopt, nullptr);
			return;
		}

		end(verified, std::move(response));
	});
}
