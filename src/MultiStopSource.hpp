#pragma once

#include <list>
#include <utility>
#include <stop_token>
#include <functional>

// NOTE: not thread safe
class MultiStopSource {
	std::stop_source main;
	std::list<std::pair<std::stop_token, std::stop_callback<std::function<void(void)>>>> connections;

public:
	// it doesn't make sense to initialize without an initial connecting stop token because it would already be stopped
	MultiStopSource(std::stop_token st);

	std::stop_token getToken();
	bool stopRequested() const noexcept;
	bool stopPossible() const noexcept;
	void connect(std::stop_token st);

	// marks this stop source as expired, ie. not possible to stop anymore. doesn't call request_stop on main.
	void invalidate();

private:
	void cleanAndCheck();
};
