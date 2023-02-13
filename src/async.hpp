#pragma once

#include <cstddef>
#include <coroutine>
#include <functional>
#include <utility>

struct suspend_maybe {
	bool should_suspend;
	bool await_ready() const noexcept;
	void await_suspend(std::coroutine_handle<> h) const noexcept;
	void await_resume() const noexcept;
};

template<typename RetType = void>
struct Async;

template<typename RetType>
struct BasePromise {
	std::coroutine_handle<> awaiter_h; // the handle of the function awaiting this task
	bool awaiter_destroyed = false;

	Async<RetType> get_return_object();
	std::suspend_never initial_suspend() noexcept;
	suspend_maybe final_suspend() noexcept;
	void unhandled_exception();
};

template<typename RetType>
struct Promise : BasePromise<RetType> {
	alignas(RetType) std::byte ret_buf[sizeof(RetType)];

	~Promise();
	RetType get_ret_val();
	void return_value(RetType);
};

template<>
struct Promise<void> : BasePromise<void> {
	void return_void() noexcept;
};

template<typename RetType>
struct Async {
	using result_type = RetType;
	using promise_type = Promise<RetType>;

	std::coroutine_handle<promise_type> task_h;

	Async(std::coroutine_handle<promise_type> p);
	~Async();

	Async(const Async&) = delete;
	const Async& operator=(const Async&) = delete;

	Async(Async&& o) noexcept;

	bool await_ready() const noexcept;
	void await_suspend(std::coroutine_handle<> h);
	RetType await_resume();
};

// allows the awaiter to be resumed by some event, no value is returned
struct Defer {
	std::function<void(std::coroutine_handle<>)> awaited;

	Defer(std::function<void(std::coroutine_handle<>)> awaited);

	Defer(const Defer&) = delete;
	const Defer& operator=(const Defer&) = delete;

	//Defer(Defer&& o) noexcept;

	bool await_ready() const noexcept;
	void await_suspend(std::coroutine_handle<> h);
	void await_resume();
};

auto discard(auto fn) {
	return [fn{std::move(fn)}] (auto&&... args) {
		fn(std::forward<decltype(args)>(args)...);
	};
}

template<typename R>
struct awaitify_data {
	alignas(R) std::byte ret_buf[sizeof(R)];
};

template<>
struct awaitify_data<void> { };

// very fragile
template<typename Result = void>
auto awaitify(auto fn) {
	using D = awaitify_data<Result>;
	struct awaitable : D {
		decltype(fn) cb;

		awaitable(decltype(fn) cb)
		: cb(std::move(cb)) { }

		bool await_ready() noexcept { return false; }
		void await_suspend(std::coroutine_handle<> h) {
			if constexpr (!std::is_same_v<Result, void>) {
				cb([this, h{std::move(h)}] (Result r) {
					new (reinterpret_cast<Result*>(&D::ret_buf[0])) Result (std::move(r));
					h.resume();
				});
			} else {
				cb([h{std::move(h)}] {
					h.resume();
				});
			}
		}

		Result await_resume() {
			if constexpr (!std::is_same_v<Result, void>) {
				return std::move(*reinterpret_cast<Result*>(&D::ret_buf[0]));
			}
		}
	};

	return awaitable{std::move(fn)};
}

// most likely bad design
template<typename T>
struct AwaiterProxy {
    T& obj;

    AwaiterProxy(T& obj) : obj(obj) { }

    bool await_ready() {
        return obj.await_ready();
    }

    auto await_suspend(std::coroutine_handle<> h) { return obj.await_suspend(std::move(h)); }
    auto await_resume() { return obj.await_resume(); }
};

#include "async.tpp" // IWYU pragma: keep
