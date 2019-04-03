#include "Ip.hpp"

#ifdef __WIN32
	#include <Winsock2.h>
#else
	#include <arpa/inet.h>
#endif

#include <errno.h>
#include <string.h>
#include <stdexcept>

#include <nlohmann/json.hpp>

Ip::Ip(const char * ip) {
	// prepares the mapped ipv4 prefix, in case the string is an ipv4 address
	address.fill(0);
	address[10] = address[11] = 0xFF;

	if (inet_pton(AF_INET, ip, address.data() + 12) != 1 && inet_pton(AF_INET6, ip, address.data()) != 1) {
		throw std::invalid_argument("Invalid IP address! (" + std::string(ip) + ")");
	}
}

Ip::Ip(const std::string& s)
: Ip(s.c_str()) { }

Ip::Ip(u32 ip) // ipv4
: address{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF,
		u8(ip), u8(ip >> 8), u8(ip >> 16), u8(ip >> 24)} { }

Ip::Ip(std::array<u8, 16> ip)
: address(std::move(ip)) { } // can't really move arrays

Ip::Ip() { address.fill(0); }

bool Ip::isLocal() const {
	static const Ip local("::1");
	return *this == local || isIpv4() && (get4() & 0xFF) == 0x7F;
}

bool Ip::isIpv4() const {
	static const std::array<u8, 12> prefix{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
	for (int i = 0; i < prefix.size(); i++) { // should optimize?
		if (address[i] != prefix[i]) {
			return false;
		}
	}

	return true;
}

const std::array<u8, 16>& Ip::get() const {
	return address;
}

u32 Ip::get4() const {
	union I4 {
		struct {
			u8 a;
			u8 b;
			u8 c;
			u8 d;
		};
		u32 ip;
	};

	return (I4{address[12], address[13], address[14], address[15]}).ip;
}

std::string_view Ip::toString() const {
	return isIpv4() ? toString4() : toString6();
}

std::string_view Ip::toString6() const {
	static thread_local std::array<char, INET6_ADDRSTRLEN> buf;
	buf.fill('\0');

	if (!inet_ntop(AF_INET6, address.data(), buf.data(), buf.size())) {
		throw std::runtime_error(std::string("Error in inet_ntop(AF_INET6): ") + strerror(errno));
	}

	return std::string_view(buf.data());
}

std::string_view Ip::toString4() const {
	static thread_local std::array<char, INET_ADDRSTRLEN> buf;
	buf.fill('\0');

	if (!inet_ntop(AF_INET, address.data() + 12, buf.data(), buf.size())) {
		throw std::runtime_error(std::string("Error in inet_ntop(AF_INET): ") + strerror(errno));
	}

	return std::string_view(buf.data());
}

Ip Ip::fromString(const char * c, sz_t s) {
	return Ip(std::string(c, s).c_str());
}

bool Ip::operator ==(const Ip& b) const {
	return address == b.address;
}

bool Ip::operator  <(const Ip& b) const {
	return address < b.address;
}

void to_json(nlohmann::json& j, const Ip& ip) {
	j = ip.toString();
}

void from_json(const nlohmann::json& j, Ip& ip) {
	ip = Ip(j.get<std::string>());
}
