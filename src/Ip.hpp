#pragma once

#include <string_view>
#include <array>
#include <vector>

#include <explints.hpp>

class Ip {
	alignas(u64) std::array<u8, 16> address;

public:
	Ip(const char *); // string -> bin conv.
	Ip(const std::string&);
	constexpr Ip(u32); // ipv4
	constexpr Ip(std::array<u8, 16>);
	constexpr Ip();
	//Ip(u8[16]);

	bool isLocal() const;
	bool isIpv4() const;
	const std::array<u8, 16>& get() const;
	u32 get4() const;
	std::string_view toString() const;
	std::string_view toString6() const;
	std::string_view toString4() const;

	std::vector<u8> getPgData() const;

	static Ip fromString(const char *, sz_t);

	bool operator ==(const Ip&) const;
	bool operator  <(const Ip&) const;
};

#if defined(INCLUDE_NLOHMANN_JSON_HPP_) || defined(INCLUDE_NLOHMANN_JSON_FWD_HPP_)
static inline void to_json(nlohmann::json& j, const Ip& ip) {
	j = ip.toString();
}

static inline void from_json(const nlohmann::json& j, Ip& ip) {
	ip = Ip(j.get<std::string>());
}
#endif
