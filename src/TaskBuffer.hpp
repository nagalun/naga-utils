#pragma once

#include <functional>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>

#include "Poll.hpp"
#include "async.hpp"

class TaskBuffer {
	std::unique_ptr<nev::Async> execCaller;
	std::vector<std::thread> workers; // spawns one thread per core
	std::atomic_flag shouldRun;
	std::condition_variable cv;
	std::mutex cvLock;
	std::mutex mtTaskLock; /* For main thread tasks */
	std::mutex taskLock; /* For async tasks */
	std::vector<std::function<void(TaskBuffer &)>> mtTasks; /* Functions to be run in the main thread */
	std::vector<std::function<void(TaskBuffer &)>> asyncTasks; /* Expensive functions (run on another thread) */

public:
	TaskBuffer(nev::Loop&, std::size_t numWorkers = std::thread::hardware_concurrency());

	TaskBuffer(const TaskBuffer&) = delete;
	const TaskBuffer& operator=(const TaskBuffer&) = delete;

	~TaskBuffer();

	void prepareForDestruction();
	void setWorkerThreadsSchedulingPriorityToLowestPossibleValueAllowedByTheOperatingSystem();

	/* Thread safe */
	void runInMainThread(std::function<void(TaskBuffer &)>);
	void queue(std::function<void(TaskBuffer &)>);

	/* also thread safe, coro equivalent to functions above */
	Defer switchToMain();
	Defer switchToThread();

private:
	void executeMainThreadTasks();
	void executeTasks();
};
