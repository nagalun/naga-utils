#pragma once

#include <string_view>
#include <array>
#include <vector>

#include "explints.hpp"

class Ip {
	alignas(u64) std::array<u8, 16> address;

public:
	Ip(const char *); // string -> bin conv.
	Ip(const std::string&);
	Ip(u32); // ipv4
	Ip(std::array<u8, 16>);
	Ip();
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
	static Ip fromBytes(const char *, sz_t);
	static Ip fromBytes(std::string_view);

	bool operator ==(const Ip&) const;
	bool operator  <(const Ip&) const;
};
