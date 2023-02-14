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
	virtual ~Loop();

	virtual std::unique_ptr<Poll> poll(int fd, bool fallthrough = false) = 0;
	virtual std::unique_ptr<Async> async(std::function<void(Async&)> cb, bool fallthrough = false) = 0;
	virtual std::unique_ptr<Timer> timer(bool fallthrough = false) = 0;

	// returns a pointer to the native handle, e.g. uv_loop_t
	virtual void* handle() = 0;
};

struct Poll {
	enum Evt {
		READABLE = 1,
		WRITABLE = 2
	};

	virtual ~Poll();

	virtual bool start(int events, std::function<void(Poll&, int status, int events)> cb) = 0;
	virtual bool change(int events) = 0;
	virtual bool stop() = 0;
};

struct Async {
	virtual ~Async();

	virtual void change(std::function<void(Async&)> cb) = 0;
	virtual bool send() noexcept = 0; // must be async-signal safe
};

struct Timer {
	virtual ~Timer();

	virtual bool start(std::function<void(Timer&)> cb, std::uint64_t timeout, std::uint64_t repeat = 0) = 0;
	virtual bool again() = 0;
	virtual bool stop() = 0;
};

}
