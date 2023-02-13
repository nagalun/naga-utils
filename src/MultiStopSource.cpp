#include "MultiStopSource.hpp"

#include <tuple>
#include <utility>

MultiStopSource::MultiStopSource(std::stop_token st) {
	if (st.stop_requested()) {
		main.request_stop();
	}

	connect(std::move(st));
}

std::stop_token MultiStopSource::getToken() {
	return main.get_token();
}

bool MultiStopSource::stopRequested() const noexcept {
	return main.stop_requested();
}

bool MultiStopSource::stopPossible() const noexcept {
	return main.stop_possible();
}

void MultiStopSource::connect(std::stop_token st) {
	if (st.stop_requested() || main.stop_requested() || !main.stop_possible()) {
		return;
	}

	std::stop_token st2{st};
	connections.emplace_back(std::piecewise_construct,
		std::forward_as_tuple(std::move(st)),
		std::forward_as_tuple(std::move(st2), [this] { cleanAndCheck(); }));
}

void MultiStopSource::invalidate() {
	main = std::stop_source{std::nostopstate};
	connections.clear();
}

void MultiStopSource::cleanAndCheck() {
	for (auto it = connections.begin(); it != connections.end();) {
		if (it->first.stop_requested()) {
			// erasing the currently executing callback is apparently ok
			it = connections.erase(it);
		} else {
			++it;
		}
	}

	// if no one else is locking this request, stop it!
	if (connections.empty()) {
		main.request_stop();
	}
}
