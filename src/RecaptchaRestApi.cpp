#include "RecaptchaRestApi.hpp"

#include <algorithm>
#include <iostream>

#include <AsyncCurl.hpp>

#include <nlohmann/json.hpp>

RecaptchaRestApi::RecaptchaRestApi(AsyncCurl& ac, std::vector<std::string> allowedHostnames, std::string_view apiKey)
: ac(ac),
  apiKey(apiKey),
  allowedHostnames(std::move(allowedHostnames)) { }

void RecaptchaRestApi::check(Ip ip, std::string token, std::function<void(std::optional<bool>, nlohmann::json)> cb) {
	ac.httpGet("https://www.google.com/recaptcha/api/siteverify", {
		{"secret", apiKey},
		{"remoteip", ip.toString().data()}, // XXX: fix when json lib supports string views
		{"response", std::move(token)}
	}, [this, end{std::move(cb)}] (auto res) {
		if (!res.successful) {
			/* HTTP ERROR code check */
			std::cerr << "Error occurred when verifying captcha: " << res.errorString << ", " << res.data << std::endl;
			end(std::nullopt, nullptr);
			return;
		}

		bool verified = false;
		nlohmann::json response;
		
		try {
			response = nlohmann::json::parse(res.data);
			bool success = response["success"].get<bool>();
			std::string host;
			if (success) {
				host = response["hostname"].get<std::string>();
			}

			verified = success && std::find(allowedHostnames.begin(), allowedHostnames.end(), host) != allowedHostnames.end();
			/*if (!success) {
				failReason = "API rejected token";
				if (response["error-codes"].is_array()) {
					failReason += " " + response["error-codes"].dump();
				}
			} else if (success && !verified) {
				failReason = "Wrong hostname: '" + host + "'";
			}*/
		} catch (const nlohmann::json::exception& e) {
			std::cerr << "Exception when parsing json from google! (" << res.data << ")" << std::endl;
			std::cerr << "what(): " << e.what() << std::endl;

			end(std::nullopt, nullptr);
			return;
		}

		end(verified, std::move(response));
	});
}

// for v3 api keys
void RecaptchaRestApi::checkv3(std::string_view action, Ip ip, std::string token, std::function<void(std::optional<double>, nlohmann::json)> cb) {
	check(ip, std::move(token), [action{std::string(action)}, cb{std::move(cb)}] (auto ok, nlohmann::json res) {
		if (ok && *ok && res["action"].is_string() && res["score"].is_number()
				&& res["action"].get<std::string>() == action) {
			cb(res["score"].get<double>(), std::move(res));
			return;
		}

		cb(std::nullopt, std::move(res));
	});
}
