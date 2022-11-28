#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_map>
#include <chrono>

#include <explints.hpp>
#include "Poll.hpp"

class TimedCallbacks {
public:
	struct TimerToken;

	struct TimerInfo {
		std::function<bool(void)> cb;
		std::chrono::steady_clock::duration interval;
		std::chrono::steady_clock::time_point next;
		TimerToken* token;
		bool paused = false;

		~TimerInfo();

		/* returns true if the timer should be deleted */
		bool maybeCall(std::chrono::steady_clock::time_point now);
	};

	struct TimerToken /*: nev::Timer*/ {
	private:
		using Iter = std::vector<TimerInfo>::iterator;
		Iter it;
		TimedCallbacks* tc;

		TimerToken(Iter, TimedCallbacks* tc);

	public:
		TimerToken(const TimerToken&) = delete;
		const TimerToken& operator=(const TimerToken&) = delete;

		TimerToken(TimerToken&&) noexcept;
		const TimerToken& operator=(TimerToken&&) noexcept;

		~TimerToken();

		std::nullptr_t operator=(std::nullptr_t) noexcept;
		operator bool() const noexcept;

		bool start();
		bool start(std::chrono::milliseconds interval);
		// bool start(std::function<void(Timer&)> cb, std::uint64_t timeout, std::uint64_t repeat = 0) override;
		bool again();
		bool stop();

		void setCb(std::function<bool(void)>);

		friend TimedCallbacks;
	};

private:
	nev::Loop& loop;
	std::unique_ptr<nev::Timer> mainTimer;
	std::vector<TimerInfo> runningTimers;

public:
	TimedCallbacks(nev::Loop& loop, std::chrono::milliseconds resolution = std::chrono::milliseconds(50));

	TimedCallbacks(const TimedCallbacks&) = delete;
	const TimedCallbacks& operator=(const TimedCallbacks&) = delete;

	void clearTimers();

	TimerToken timer(std::function<bool(void)> func, std::chrono::milliseconds timeout);

private:
	void fire();
	void stop(std::vector<TimerInfo>::iterator);
	void updateTokens(std::vector<TimerInfo>::iterator);
};
