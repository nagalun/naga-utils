#include "TimedCallbacks.hpp"
#include <chrono>
#include <utility>

using namespace nev;

bool TimedCallbacks::TimerInfo::maybeCall(std::chrono::steady_clock::time_point now) {
	if (now >= next) {
		// drop backed up ticks or call sooner?
		//next = now + interval; // drop
		//next += interval; // reduce timeout
		next = std::max(next + interval, now - interval * 2); // queue at most 2 missed ticks
		if (cb && !paused) { // cb returns false if the timer should be stopped
			return !cb();
		}
	}

	return false;
}

TimedCallbacks::TimerInfo::~TimerInfo() {
	if (token) {
		*token = nullptr; // call operator=(std::nullptr)
	}
}

TimedCallbacks::TimerToken::TimerToken(Iter it, TimedCallbacks* tc)
: it(it),
  tc(tc) {
	it->token = this;
}

TimedCallbacks::TimerToken::operator bool() const noexcept {
	return tc;
}

std::nullptr_t TimedCallbacks::TimerToken::operator=(std::nullptr_t) noexcept {
	it = {};
	tc = nullptr;
	return nullptr;
}

bool TimedCallbacks::TimerToken::start() {
	if (tc) {
		it->paused = false;
	}

	return tc;
}

bool TimedCallbacks::TimerToken::start(std::chrono::milliseconds interval) {
	if (tc) {
		it->paused = false;
		it->interval = interval;
	}

	return tc;
}

bool TimedCallbacks::TimerToken::again() {
	if (tc) {
		auto now = std::chrono::steady_clock::now();
		it->paused = false;
		it->next = now + it->interval;
	}

	return tc;
}

bool TimedCallbacks::TimerToken::stop() {
	if (tc) {
		it->paused = true;
	}

	return tc;
}

void TimedCallbacks::TimerToken::setCb(std::function<bool(void)> cb) {
	if (tc) {
		it->cb = std::move(cb);
	}
}

TimedCallbacks::TimerToken::TimerToken(TimerToken&& tok) noexcept
: it(std::exchange(tok.it, {})),
  tc(std::exchange(tok.tc, nullptr)) {
	if (tc) {
		it->token = this;
	}
}

const TimedCallbacks::TimerToken& TimedCallbacks::TimerToken::operator=(TimerToken&& tok) noexcept {
	it = std::exchange(tok.it, {});
	tc = std::exchange(tok.tc, nullptr);
	if (tc) {
		it->token = this;
	}
	return *this;
}

TimedCallbacks::TimerToken::~TimerToken() {
	if (tc) {
		tc->stop(it);
		tc = nullptr;
		it = {};
	}
}

TimedCallbacks::TimedCallbacks(Loop& loop, std::chrono::milliseconds resolution)
: loop(loop) {
	mainTimer = loop.timer();
	mainTimer->start([this] (Timer&) {
		fire();
	}, resolution.count());
}

void TimedCallbacks::clearTimers() {
	runningTimers.clear();
}

TimedCallbacks::TimerToken TimedCallbacks::timer(std::function<bool(void)> func, std::chrono::milliseconds timeout) {
	auto now = std::chrono::steady_clock::now();
	bool update = runningTimers.size() == runningTimers.capacity();
	auto it = runningTimers.emplace(runningTimers.end(), TimerInfo{std::move(func), timeout, now + timeout, nullptr});

	if (update) {
		updateTokens(runningTimers.begin());
	}

	return {it, this};
}

void TimedCallbacks::fire() {
	auto now = std::chrono::steady_clock::now();
	for (auto it = runningTimers.begin(); it != runningTimers.end(); ++it) {
		TimerInfo& ti = *it;

		if (ti.maybeCall(now)) {
			it = runningTimers.erase(it);
			updateTokens(it);
		}
	}
}

void TimedCallbacks::stop(std::vector<TimerInfo>::iterator it) {
	updateTokens(runningTimers.erase(it));
}

void TimedCallbacks::updateTokens(std::vector<TimerInfo>::iterator from) {
	for (auto it = from; it != runningTimers.end(); ++it) {
		if (TimerToken* tok = it->token) {
			tok->it = it;
			tok->tc = this;
		}
	}
}
