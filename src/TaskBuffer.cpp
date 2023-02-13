#include "TaskBuffer.hpp"

#ifndef __WIN32
	#include <pthread.h>
#endif

#include <cstring>
#include <chrono>
#include <iostream>

#include <explints.hpp>

TaskBuffer::TaskBuffer(nev::Loop& loop, std::size_t numWorkers) {
	execCaller = loop.async([this] (nev::Async&) {
		executeMainThreadTasks();
	});

	//shouldRun = ATOMIC_FLAG_INIT;
	shouldRun.clear();
	shouldRun.test_and_set();

	if (numWorkers < 1) {
		numWorkers = 1;
	}

	for (sz_t i = 0; i < numWorkers; i++) {
		workers.emplace_back([this] { executeTasks(); });
	}

	std::cout << workers.size() << " workers created!" << std::endl;
}

TaskBuffer::~TaskBuffer() {
	shouldRun.clear();
	cv.notify_all();
	for (auto& worker : workers) {
		worker.join();
	}
}

void TaskBuffer::prepareForDestruction() {
	// This function is needed because else the event loop doesn't stop
	// when the server closes
	executeMainThreadTasks();
	execCaller = nullptr;
}

void TaskBuffer::setWorkerThreadsSchedulingPriorityToLowestPossibleValueAllowedByTheOperatingSystem() {
#ifndef __WIN32
	sched_param param = { 0 };
	for (auto& worker : workers) {
		if (auto ret = pthread_setschedparam(worker.native_handle(), SCHED_IDLE, &param)) {
			std::cerr << "pthread_setschedparam failed (" << ret << "): " << std::strerror(ret) << std::endl;
		}
	}
#else
	std::cerr << __func__ << ": not supported for this platform" << std::endl;
#endif
}

void TaskBuffer::executeMainThreadTasks() {
	/* .empty() is not thread safe, but this is the only
	 * function where items are removed from the queue.
	 */
	if (mtTasks.empty()) {
		return; /* Avoid locking the mutex */
	}

	std::vector<std::function<void(TaskBuffer &)>> tasks;

	{
		std::lock_guard<std::mutex> lk(mtTaskLock);
		tasks.swap(mtTasks);
	}

	for (auto& func : tasks) {
		func(*this);
	}
}

void TaskBuffer::executeTasks() {
	std::unique_lock<std::mutex> uLock(cvLock);
	std::vector<std::function<void(TaskBuffer &)>> tasks;

	do {
		if (!asyncTasks.empty()) {
			{
				std::lock_guard<std::mutex> lk(taskLock);
				// this takes the whole vector, not just one function, but,
				// if functions are queuing up this fast the other threads should
				// be able to get work too while tasks execute
				// it also dances around the vectors's memory blocks so
				// no new allocations will be made eventually (faster emplace)
				tasks.swap(asyncTasks);
			}

			for (auto& func : tasks) {
				func(*this);
			}

			tasks.clear();
		} else {
			/* Only wait if the vector is empty */
			cv.wait_for(uLock, std::chrono::seconds(10));
		}

	} while (shouldRun.test_and_set());

	shouldRun.clear(); // notify another thread
	cv.notify_one(); // just in case it is waiting
}

void TaskBuffer::runInMainThread(std::function<void(TaskBuffer &)> func) {
	{
		std::lock_guard<std::mutex> lk(mtTaskLock);
		mtTasks.emplace_back(std::move(func));
	}

	execCaller->send();
}

void TaskBuffer::queue(std::function<void(TaskBuffer &)> func) {
	{
		std::lock_guard<std::mutex> lk(taskLock);
		asyncTasks.emplace_back(std::move(func));
	}

	cv.notify_one();
}

Defer TaskBuffer::switchToMain() {
	return Defer{[this](std::coroutine_handle<> h) {
		// queue resumption of this handle on the main thread
		runInMainThread([h{std::move(h)}](TaskBuffer&) { h.resume(); });
	}};
}

Defer TaskBuffer::switchToThread() {
	return Defer{[this](std::coroutine_handle<> h) {
		// queue resumption of this handle on some thread
		queue([h{std::move(h)}](TaskBuffer&) { h.resume(); });
	}};
}
