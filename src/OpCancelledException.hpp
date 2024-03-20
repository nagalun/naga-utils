#pragma once

#include <exception>
#include <stop_token>

struct OpCancelledException : std::exception {
	using std::exception::exception;

	const char* what() const noexcept override;
	// throws if the token passed is cancelled
	static void check(const std::stop_token&);
	static void check(const std::stop_source&);
};
