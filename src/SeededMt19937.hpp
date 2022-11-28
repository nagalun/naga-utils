#pragma once

#include <random>

class SeededMt19937 : public std::mt19937 {
public:
	SeededMt19937();
};
