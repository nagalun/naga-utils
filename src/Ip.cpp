#include "Ip.hpp"

#ifdef __WIN32
	#include <Winsock2.h>
#else
	#include <arpa/inet.h>
#endif

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <algorithm>

Ip::Ip(const char * ip) {
	// prepares the mapped ipv4 prefix, in case the string is an ipv4 address
	address.fill(0);
	address[10] = address[11] = 0xFF;

	if (inet_pton(AF_INET, ip, address.data() + 12) != 1 && inet_pton(AF_INET6, ip, address.data()) != 1) {
		throw std::invalid_argument("Invalid IP address! (" + std::string(ip) + ")");
	}
}

Ip::Ip(const std::string& s) // can't be string_view because str must be null terminated
: Ip(s.c_str()) { }

Ip::Ip(u32 ip) // ipv4
: address{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF,
		u8(ip), u8(ip >> 8), u8(ip >> 16), u8(ip >> 24)} { }

Ip::Ip(std::array<u8, 16> ip)
: address(std::move(ip)) { }

Ip::Ip()
: address{0} { }

bool Ip::isLocal() const {
	static const Ip local("::1");
	return *this == local || (isIpv4() && (get4() & 0xFF) == 0x7F);
}

bool Ip::isIpv4() const {
	static constexpr std::array<u8, 12> prefix alignas(u64) {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
	return std::memcmp(prefix.data(), address.data(), prefix.size()) == 0;
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
		} b;
		u32 ip;
	};

	I4 ip4;
	ip4.b.a = address[12];
	ip4.b.b = address[13];
	ip4.b.c = address[14];
	ip4.b.d = address[15];

	return ip4.ip;
}

std::string_view Ip::toString() const {
	return isIpv4() ? toString4() : toString6();
}

std::string_view Ip::toString6() const {
	static thread_local std::array<char, INET6_ADDRSTRLEN> buf;
	buf.fill('\0');

	if (!inet_ntop(AF_INET6, address.data(), buf.data(), buf.size())) {
		throw std::runtime_error(std::string("Error in inet_ntop(AF_INET6): ") + std::strerror(errno));
	}

	return std::string_view(buf.data());
}

std::string_view Ip::toString4() const {
	static thread_local std::array<char, INET_ADDRSTRLEN> buf;
	buf.fill('\0');

	if (!inet_ntop(AF_INET, address.data() + 12, buf.data(), buf.size())) {
		throw std::runtime_error(std::string("Error in inet_ntop(AF_INET): ") + std::strerror(errno));
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

Ip Ip::fromBytes(const char * c, sz_t s) {
	std::array<u8, 16> arr{0};

	if (s != 16 && s != 4) {
		return Ip();
	}

	for (sz_t i = 16 - s, j = 0; j < s; i++, j++) {
		arr[i] = static_cast<u8>(c[j]);
	}

	return Ip(arr);
}

Ip Ip::fromBytes(std::string_view bytes) {
	return fromBytes(bytes.data(), bytes.size());
}


bool Ip::operator ==(const Ip& b) const {
	return address == b.address;
}

bool Ip::operator  <(const Ip& b) const {
	return address < b.address;
}
