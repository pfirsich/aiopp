#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "aiopp/function.hpp"
#include "aiopp/future.hpp"
#include "aiopp/ioqueue.hpp"

namespace aiopp {
class ThreadPool {
public:
    ThreadPool(size_t numThreads = std::thread::hardware_concurrency());

    ~ThreadPool();

    void push(Function<void()> task);

    template <typename Func, typename Result = std::invoke_result_t<Func>>
    Future<Result> submit(Func func)
    {
        Promise<Result> promise;
        auto future = promise.getFuture();
        push([promise = std::move(promise), func = Function<Result()>(std::move(func))]() mutable {
            if constexpr (std::is_void_v<Result>) {
                promise.set();
            } else {
                promise.set(func());
            }
        });
        return future;
    }

private:
    void workerFunc();

    std::atomic<bool> running_;
    std::vector<std::thread> threads_;
    std::condition_variable tasksCv_;
    std::mutex tasksMutex_;
    std::queue<Function<void(void)>> tasks_;
};

ThreadPool& getDefaultThreadPool();

// I think this does not belong here, because it brings the IoQueue include with it, which is not
// needed otherwise, but I don't want a separate file for only this thing. And you usually use it
// *with* a thread pool, so here we go.
template <typename Func, typename Result = std::invoke_result_t<Func>>
Task<Result> wrapAsTask(IoQueue& io, ThreadPool& tp, Func func)
{
    co_return co_await io.wait(tp.submit(std::move(func)));
}
}
