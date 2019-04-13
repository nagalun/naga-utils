#include "stringparser.hpp"

#include <stdexcept>
#include <type_traits>
#include <limits>
#include <charconv>
#include <explints.hpp>

template<typename T>
typename std::enable_if<!std::is_same<T, bool>::value && std::is_integral<T>::value, T>::type
fromString(std::string_view s, int base) {
	using L = std::numeric_limits<T>;

	if (s.size() == 0) {
		throw std::invalid_argument("Improperly formatted argument");
	}

	typename std::conditional<L::is_signed, i64, u64>::type n;
	auto res = std::from_chars(s.data(), s.data() + s.size(), n, base);

	if (res.ptr != s.data() + s.size() || res.ec == std::errc::invalid_argument || res.ec == std::errc::result_out_of_range) { // contains extra data, or too big
		throw std::invalid_argument("Improperly formatted argument");
	}

	if (n > L::max() || n < L::min()) {
		throw std::out_of_range("Value too big/small");
	}

	return n;
}

template<typename T>
typename std::enable_if<!std::is_same<T, bool>::value && std::is_integral<T>::value, T>::type
fromString(std::string_view s) {
	return fromString<T>(s, 10);
}

template<typename T>
typename std::enable_if<std::is_same<T, bool>::value, T>::type
fromString(std::string_view s) {
	if (s.size() != 0 && s.size() <= 5) {
		switch(s[0]) {
			case 't':
			case 'T':
				return true;

			case 'f':
			case 'F':
				return false;
		}
	}

	throw std::invalid_argument("Invalid bool");
}

template<typename T>
typename std::enable_if<std::is_same<T, std::string>::value, T>::type
fromString(std::string_view s) {
	return std::string(s.data(), s.size());
}

template<typename T>
typename std::enable_if<std::is_floating_point<T>::value, T>::type
fromString(std::string_view s) {
	if (s.size() == 0) {
		throw std::invalid_argument("Improperly formatted argument");
	}

	std::string str(s.data(), s.size()); // NOTE: can't guarantee s is null terminated, and from_chars is not implemented for floating point

	T n;
	sz_t proc;

	if constexpr (std::is_same<T, float>::value) {
		n = std::stof(str, &proc);
	} else if constexpr (std::is_same<T, double>::value) {
		n = std::stod(str, &proc);
	} else if constexpr (std::is_same<T, long double>::value) {
		n = std::stold(str, &proc);
	}

	if (proc != s.size()) { // contains extra data?
		throw std::invalid_argument("Improperly formatted argument");
	}

	/*auto res = std::from_chars(s.data(), s.data() + s.size(), n);

	if (s.size() == 0 || res.ptr != s.data() + s.size()) { // contains extra data, or too big
		throw std::invalid_argument("Improperly formatted argument");
	}*/

	return n;
}



// explicit instantiations
template u8 fromString<u8>(std::string_view, int);
template u16 fromString<u16>(std::string_view, int);
template u32 fromString<u32>(std::string_view, int);
template u64 fromString<u64>(std::string_view, int);

template i8 fromString<i8>(std::string_view, int);
template i16 fromString<i16>(std::string_view, int);
template i32 fromString<i32>(std::string_view, int);
template i64 fromString<i64>(std::string_view, int);

template u8 fromString<u8>(std::string_view);
template u16 fromString<u16>(std::string_view);
template u32 fromString<u32>(std::string_view);
template u64 fromString<u64>(std::string_view);

template i8 fromString<i8>(std::string_view);
template i16 fromString<i16>(std::string_view);
template i32 fromString<i32>(std::string_view);
template i64 fromString<i64>(std::string_view);

template bool fromString<bool>(std::string_view);
template std::string fromString<std::string>(std::string_view);
template float fromString<float>(std::string_view);
template double fromString<double>(std::string_view);
template long double fromString<long double>(std::string_view);
