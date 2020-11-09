#include "Ip.hpp"

#ifdef __WIN32
	#include <Winsock2.h>
#else
	#include <arpa/inet.h>
#endif

#include <errno.h>
#include <string.h>
#include <stdexcept>
#include <algorithm>

#include <nlohmann/json.hpp>

Ip::Ip(const char * ip) {
	// prepares the mapped ipv4 prefix, in case the string is an ipv4 address
	address.fill(0);
	address[10] = address[11] = 0xFF;

	if (inet_pton(AF_INET, ip, address.data() + 12) != 1 && inet_pton(AF_INET6, ip, address.data()) != 1) {
		throw std::invalid_argument("Invalid IP address! (" + std::string(ip) + ")");
	}

	isIpv4Cache = calculateIsIpv4();
}

Ip::Ip(const std::string& s) // can't be string_view because str must be null terminated
: Ip(s.c_str()) { }

Ip::Ip(u32 ip) // ipv4
: address{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF,
		u8(ip), u8(ip >> 8), u8(ip >> 16), u8(ip >> 24)},
  isIpv4Cache(true) { }

Ip::Ip(std::array<u8, 16> ip)
: address(std::move(ip)), // can't really move arrays
  isIpv4Cache(calculateIsIpv4()) { }

Ip::Ip()
: isIpv4Cache(false) { address.fill(0); }

bool Ip::isLocal() const {
	static const Ip local("::1");
	return *this == local || (isIpv4() && (get4() & 0xFF) == 0x7F);
}

bool Ip::calculateIsIpv4() const {
	static const std::array<u8, 12> prefix{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
	for (sz_t i = 0; i < prefix.size(); i++) { // should optimize?
		if (address[i] != prefix[i]) {
			return false;
		}
	}

	return true;
}

bool Ip::isIpv4() const {
	return isIpv4Cache;
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

	I4 ip4;
	ip4.a = address[12];
	ip4.b = address[13];
	ip4.c = address[14];
	ip4.d = address[15];

	return ip4.ip;
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

std::vector<u8> Ip::getPgData() const {
	// family, mask, is_cidr, addr size, addr data...
	std::array<u8, 1 + 1 + 1 + 1 + 16> ret;

	ret[2] = false; // not CIDR data
	auto firstIt = address.begin();

	if (isIpv4()) {
		ret[0] = AF_INET;
		ret[1] = 32; // mask
		ret[3] = 4; // byte size

		firstIt += 12;
		std::fill(ret.begin() + 4 + 4, ret.end(), 0);
	} else {
		ret[0] = AF_INET + 1; // IPv6 constant defined here: https://github.com/postgres/postgres/blob/master/src/port/inet_net_ntop.c#L43
		ret[1] = 128;
		ret[3] = 16;
	}

	auto lastIt = std::copy(firstIt, address.end(), ret.begin() + 4);

	return std::vector<u8>(ret.begin(), lastIt);
}


Ip Ip::fromString(const char * c, sz_t s) {
	std::string str(c, s); // could not be null terminated
	auto pos = str.find('/');
	if (pos != std::string::npos) { // remove cidr
		str.erase(pos);
	}

	return Ip(str.c_str());
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
