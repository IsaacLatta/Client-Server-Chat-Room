#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <thread>
#include <queue>
#include <functional>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <stdexcept>

// Beginning of ThreadPool
// ***Disclamer***
// Only Accepts Void Functions!
class ThreadPool
{
private:
	const int THREAD_COUNT = 10;
	bool stop;

	std::queue <std::function<void()>> tasks;
	std::vector <std::thread> threads;
	
	std::mutex task_mutex;
	std::condition_variable cv;

	// Gets tasks from queue
	void getTask()
	{
		while (!stop)
		{
			std::function <void()> task;

			{
				std::unique_lock < std::mutex> lock(task_mutex);
				cv.wait(lock, [this] {return !tasks.empty() || stop; }); // Wait until new task

				if (stop)
					return;

				task = std::move(tasks.front()); // Get task from queue
				tasks.pop(); // Remove task from queue	
			}
			task(); // Perform task
		}
	}

public:

	// Constructor
	ThreadPool()
	{
		stop = false;
		for (int i = 0; i < THREAD_COUNT; i++)
		{
			threads.emplace_back(std::thread(&ThreadPool::getTask, this));
		}
	}

	// Deconstructor
	~ThreadPool()
	{
		//Tell threads to stop
		{
			std::lock_guard <std::mutex> lock(task_mutex);
			stop = true;
		}
		cv.notify_all();

		//Wait for threads to join
		for (auto& thread : threads)
		{
			thread.join();
		}
	}
	// Check if threads are busy
	const bool busy()
	{
		std::lock_guard < std::mutex> lock(task_mutex);
		return !tasks.empty();
	}

	// Add task to queue with no parameters
	template <typename Func>
	void pushTask(Func&& func)
	{
		{
			std::lock_guard<std::mutex> lock(task_mutex);
			tasks.emplace(std::forward <Func> (func));
		}
		cv.notify_one();
	}
	
	// Adds task to queue with two parameters
	template <typename Func, typename Arg1, typename Arg2>
	void pushTask(Func&& func, Arg1&& arg1, Arg2&& arg2)
	{
		{
			std::unique_lock <std::mutex> lock(task_mutex);

			// Add task, forward parameters
			tasks.emplace
			(
				std::bind
				(
					std::forward<Func>(func),
					std::forward<Arg1>(arg1),
					std::forward<Arg2>(arg2)
				)
			);
		}
		cv.notify_one();
	}

	
	// Adds task to queue with one parameter
	template <typename Func, typename Arg1>
	void pushTask(Func&& func, Arg1&& arg1)
	{
		{
			std::unique_lock <std::mutex> lock(task_mutex);

			// Add task, forward parameters
			tasks.emplace
			(
				std::bind
				(
					std::forward<Func>(func),
					std::forward<Arg1>(arg1)
				)
			);
		}
		cv.notify_one();
	}
	
}; // End of ThreadPool
#endif 

