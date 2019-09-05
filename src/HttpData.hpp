#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <fwd_uWS.h>

class HttpData {
	uWS::HttpRequest * req;

public:
	HttpData(uWS::HttpRequest *);

	uWS::HttpRequest * getData();

	std::string_view getUrl();
	std::optional<std::string_view> getQueryParam(std::string_view);
	std::optional<std::string> getDecodedQueryParam(std::string_view);
	std::optional<std::string_view> getHeader(std::string_view);

	std::optional<std::string_view> getCookie(std::string_view);

	void updateData(uWS::HttpRequest *);
	void invalidateData();
};