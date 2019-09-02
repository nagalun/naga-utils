#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <typeindex>

#include <explints.hpp>

sz_t getUtf8StrLen(std::string_view);

bool makeDir(const std::string&);
bool fileExists(const std::string&);

std::string_view getUsername();
std::string_view getHostname();
bool processExists(int pid);

i64 jsDateNow();
std::vector<std::string_view> tokenize(std::string_view, char delimiter = ' ', bool trimEmpty = false);

bool strStartsWith(std::string_view str, std::string_view prefix, bool caseSensitive = true);

u32 randUint32();
std::string randomStr(sz_t size);

void rtrim(std::string&);
void ltrim(std::string&);
void trim(std::string&);

void rtrim_v(std::string_view&);
void ltrim_v(std::string_view&);
void trim_v(std::string_view&);

void sanitize(std::string&, bool keepNewlines = false);

const std::string& demangle(std::type_index);
std::type_index strToType(const std::string& s);

void urldecode(std::string&);
void urldecode(std::vector<std::string>&);
std::string mkurldecoded(std::string);
std::string mkurldecoded_v(std::string_view);

// interestingly enough this doesn't cause linker errors
constexpr u8 popc(u32 n) {
	n = (n & 0x55555555) + ((n >> 1) & 0x55555555);
	n = (n & 0x33333333) + ((n >> 2) & 0x33333333);
	n = (n & 0x0F0F0F0F) + ((n >> 4) & 0x0F0F0F0F);
	n = (n & 0x00FF00FF) + ((n >> 8) & 0x00FF00FF);
	n = (n & 0x0000FFFF) + ((n >> 16)& 0x0000FFFF);
	return n;
}

template<typename I>
std::string n2hexstr(I w, sz_t hex_len = sizeof(I) << 1) {
	static const char * digits = "0123456789ABCDEF";
	std::string rc(hex_len,'0');

	for (sz_t i = 0, j = (hex_len - 1) * 4; i < hex_len; ++i, j -= 4) {
		rc[i] = digits[(w >> j) & 0x0F];
	}

	return rc;
}
