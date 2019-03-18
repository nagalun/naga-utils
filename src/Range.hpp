#pragma once

template<typename T, T MIN, T MAX>
class Range {
	const T value;

public:
	using type = T;
	Range(T val);

	operator T() const;
};
