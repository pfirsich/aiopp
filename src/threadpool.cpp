#include "aiopp/threadpool.hpp"

#include <algorithm>

namespace aiopp {
ThreadPool::ThreadPool(size_t numThreads)
{
    running_.store(true);
    for (size_t i = 0; i < std::max(1ul, numThreads); ++i) {
        threads_.push_back(std::thread(&ThreadPool::workerFunc, this));
    }
}

ThreadPool::~ThreadPool()
{
    running_.store(false);
    tasksCv_.notify_all();
    for (auto& thread : threads_) {
        thread.join();
    }
}

void ThreadPool::push(Function<void()> task)
{
    {
        std::lock_guard lock(tasksMutex_);
        tasks_.push(std::move(task));
    }
    tasksCv_.notify_one();
}

void ThreadPool::workerFunc()
{
    while (running_.load()) {
        std::unique_lock lock(tasksMutex_);
        tasksCv_.wait(lock, [this] { return !tasks_.empty() || !running_.load(); });
        if (running_.load()) {
            auto task = std::move(tasks_.front());
            tasks_.pop();
            lock.unlock();
            task();
        }
    }
}

ThreadPool& getDefaultThreadPool()
{
    static ThreadPool threadPool;
    return threadPool;
}
}
