#pragma once

#include <cstdint>
#include <functional>
#include <memory>

// generic event loop adapter
namespace nev {

struct Loop;
struct Poll;
struct Async;
struct Timer;

struct Loop {
	virtual ~Loop() = 0;

	virtual std::unique_ptr<Poll> poll(int fd) = 0;
	virtual std::unique_ptr<Async> async(std::function<void(Async&)> cb) = 0;
	virtual std::unique_ptr<Timer> timer() = 0;
};

struct Poll {
	enum Evt {
		READABLE = 1,
		WRITABLE = 2
	};

	virtual ~Poll() = 0;

	virtual bool start(int events, std::function<void(Poll&, int status, int events)> cb) = 0;
	virtual bool change(int events) = 0;
	virtual bool stop() = 0;
};

struct Async {
	virtual ~Async() = 0;

	virtual bool send() noexcept = 0; // must be async-signal safe
};

struct Timer {
	virtual ~Timer() = 0;

	virtual bool start(std::function<void(Timer&)> cb, std::uint64_t timeout, std::uint64_t repeat = 0) = 0;
	virtual bool again() = 0;
	virtual bool stop() = 0;
};

}
