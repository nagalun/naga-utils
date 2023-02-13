#include "async.hpp"

bool suspend_maybe::await_ready() const noexcept {
	return !should_suspend;
}

void suspend_maybe::await_suspend(std::coroutine_handle<>) const noexcept { }

void suspend_maybe::await_resume() const noexcept { }

void Promise<void>::return_void() noexcept { }

Defer::Defer(std::function<void(std::coroutine_handle<>)> awaited)
: awaited(std::move(awaited)) { }

bool Defer::await_ready() const noexcept {
	return false;
}

void Defer::await_suspend(std::coroutine_handle<> h) {
	awaited(std::move(h));
}

void Defer::await_resume() { }
