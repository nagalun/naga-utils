#include "utils.hpp"

#ifndef __WIN32
	#include <sys/types.h>
	#include <sys/stat.h>
	#include <unistd.h>
	#include <limits.h>
#else
	#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <charconv>
#include <random>
#include <locale>
#include <map>

#ifdef __GNUG__
	#define HAVE_CXA_DEMANGLE
	#include <memory>
	#include <cxxabi.h>
#endif

#include <SeededMt19937.hpp>

bool makeDir(const std::string& dir) {
#ifndef __WIN32
	return mkdir(dir.c_str(), 0755) == 0;
#else
	/* conversion from windows bool to c++ bool necessary? */
	return CreateDirectory(dir.c_str(), nullptr) == TRUE;
#endif
}

bool fileExists(const std::string& path) {
#ifndef __WIN32
	return access(path.c_str(), F_OK) != -1;
#else
	DWORD dwAttrib = GetFileAttributes(path.c_str());

	return dwAttrib != INVALID_FILE_ATTRIBUTES;
#endif
}

std::string_view getUsername() {
	// the variable is called USER on Linux, and USERNAME on Windows
	return getenv(
		"USER"
#ifdef __WIN32
		"NAME"
#endif
	);
}

std::string_view getHostname() {
	// some linux distros don't export HOSTNAME correctly, so gethostname is needed
#ifdef __WIN32
	return getenv("COMPUTERNAME");
#else
	static char hostname[HOST_NAME_MAX];
	return gethostname(&hostname[0], HOST_NAME_MAX) == 0 ? hostname : "";
#endif
}

bool processExists(int pid) {
#ifndef __WIN32
	return fileExists("/proc/" + std::to_string(pid));
#else
	HANDLE proc = OpenProcess(0, FALSE, pid);
	if (!proc) {
		return false;
	}

	LPDWORD ecode;
	if (!GetExitCodeProcess(proc, &ecode)) {
		return false;
	}

	CloseHandle(proc);
	return ecode == STILL_ACTIVE;
#endif
}


sz_t getUtf8StrLen(std::string_view str) {
	sz_t j = 0, i = 0, x = 1;
	while (i < str.size()) {
		if (x > 4) { /* Invalid unicode */
			return -1;
		}

		if ((str[i] & 0xC0) != 0x80) {
			j += x == 4 ? 2 : 1;
			x = 1;
		} else {
			x++;
		}
		i++;
	}
	if (x == 4) {
		j++;
	}
	return (j);
}

i64 jsDateNow() {
	namespace c = std::chrono;

	auto time = c::system_clock::now().time_since_epoch();
	return c::duration_cast<c::milliseconds>(time).count();
}

std::vector<std::string_view> tokenize(std::string_view str,
		char delimiter, bool trimEmpty) {
	sz_t pos, lastPos = 0, length = str.length();
	std::vector<std::string_view> tokens;
	while (lastPos < length + 1) {
		pos = str.find_first_of(delimiter, lastPos);
		if (pos == std::string::npos) {
			pos = length;
		}

		if (pos != lastPos || !trimEmpty) {
			tokens.emplace_back(str.data() + lastPos, (sz_t)pos - lastPos);
		}

		lastPos = pos + 1;
	}

	return tokens;
}

bool strStartsWith(std::string_view str, std::string_view prefix) {
	return prefix.size() <= str.size() && str.substr(0, prefix.size()) == prefix;
}

static SeededMt19937 rng;

u32 randUint32() {
	return rng();
}

std::string randomStr(sz_t size) { // WARNING: RNG not thread safe
	std::string str(size, '\0');

	std::generate(str.begin(), str.end(), [] {
		static const char alphanum[] =
			"0123456789?_@#$!"
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz";

		static std::uniform_int_distribution<sz_t> get(0, sizeof(alphanum) - 2);

		return alphanum[get(rng)];
	});

	return str;
}

// trim from start (in place)
void ltrim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [] (char ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
void rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [] (char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
void trim(std::string& s) {
    ltrim(s);
    rtrim(s);
}

void sanitize(std::string& s, bool keepNewlines) {
	s.erase(std::remove_if(s.begin(), s.end(), [keepNewlines] (unsigned char c) -> bool {
		if (c == '\n') {
			return !keepNewlines;
		}

		return std::iscntrl(c);
	}), s.end());
}


// could go in a different file
std::map<std::type_index, std::string> typeCache;
std::map<std::string, std::type_index> typeMap;

const std::string& demangle(std::type_index type) { // XXX: also not thread safe
	auto search = typeCache.find(type);

	if (search == typeCache.end()) {
#ifdef HAVE_CXA_DEMANGLE
		int s = -1;
		std::unique_ptr<char, void(*)(void*)> result(
			abi::__cxa_demangle(type.name(), NULL, NULL, &s),
			std::free
		);

		std::string name(s == 0 ? result.get() : type.name());
#else
		std::string name(type.name());
#endif
		search = typeCache.emplace(type, name).first;
		typeMap.emplace(name, type);
	}

	return search->second;
}

std::type_index strToType(const std::string& s) {
	return typeMap.at(s); // throws if not found
}

void urldecode(std::string& s) {
	sz_t pos = 0;
	while ((pos = s.find_first_of("%+", pos, 2)) != std::string::npos) {
		const char * p = s.c_str() + pos; // "%??..." or "+..."
		switch (p[0]) {
			case '+':
				s[pos] = ' ';
				break;

			case '%':
				// must always have at least two chars after the %
				if (s.size() - (pos + 1) < 2) {
					throw std::length_error("Wrong URL percent encoding");
				}

				// from first hex char to last + 1, replace % with the decoded value
				auto res = std::from_chars(p + 1, p + 3, *reinterpret_cast<u8 *>(&s[pos]), 16);
				if (res.ptr != p + 3) {
					throw std::logic_error("Invalid URL percent encoding");
				}

				// erase the two hex chars
				s.erase(pos + 1, 2);
				break;
		}
		
		pos++;
	}
}

void urldecode(std::vector<std::string>& v) {
	for (auto& s : v) {
		urldecode(s);
	}
}

std::string mkurldecoded(std::string s) {
	urldecode(s);
	return s;
}

std::string mkurldecoded_v(std::string_view sv) {
	std::string s(sv);
	urldecode(s);
	return s;
}
