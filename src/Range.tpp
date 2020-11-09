#include "Range.hpp"
#include <stdexcept>
#include <string>

template<typename T, T MIN, T MAX>
Range<T, MIN, MAX>::Range(T val)
: value(val) {
	if (val < MIN || val > MAX) {
		throw std::range_error("Value " + std::to_string(val) + " outside range (" + std::to_string(MIN) + "-" + std::to_string(MAX) + ")");
	}
}

template<typename T, T MIN, T MAX>
Range<T, MIN, MAX>::operator T() const {
	return value;
}
