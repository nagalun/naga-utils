#include "OpCancelledException.hpp"

const char* OpCancelledException::what() const noexcept {
	return "the operation was cancelled";
}

void OpCancelledException::check(const std::stop_token& st) {
	if (st.stop_requested()) {
		throw OpCancelledException{};
	}
}

void OpCancelledException::check(const std::stop_source& ss) {
	if (ss.stop_requested()) {
		throw OpCancelledException{};
	}
}
