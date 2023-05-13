#pragma once

#include <coroutine>

#include "aiopp/basiccoroutine.hpp"

namespace aiopp {
template <typename Result = void>
class [[nodiscard]] Task {
public:
    struct FinalAwaiter {
        bool await_ready() const noexcept { return false; }

        template <typename P>
        auto await_suspend(std::coroutine_handle<P> handle) noexcept
        {
            return handle.promise().continuation;
        }

        void await_resume() const noexcept { }
    };

    struct Promise {
        std::coroutine_handle<> continuation;
        Result result;

        Task get_return_object()
        {
            return Task { std::coroutine_handle<Promise>::from_promise(*this) };
        }

        void unhandled_exception() noexcept { }

        void return_value(Result&& res) noexcept { result = std::move(res); }

        std::suspend_always initial_suspend() noexcept { return {}; }

        FinalAwaiter final_suspend() noexcept { return {}; }
    };
    using promise_type = Promise;

    Task() = default;

    ~Task()
    {
        if (handle_) {
            handle_.destroy();
        }
    }

    struct Awaiter {
        std::coroutine_handle<Promise> handle;

        bool await_ready() const noexcept { return !handle || handle.done(); }

        auto await_suspend(std::coroutine_handle<> calling) noexcept
        {
            handle.promise().continuation = calling;
            return handle;
        }

        // clang-format off
        template <typename T = Result>
        requires(std::is_same_v<T, void>)
        void await_resume() noexcept { }

        template <typename T = Result>
        requires(!std::is_same_v<T, void>)
        T await_resume() noexcept { return std::move(handle.promise().result); }
        // clang-format on
    };

    auto operator co_await() noexcept { return Awaiter { handle_ }; }

    template <typename Func>
    BasicCoroutine callback(Func func)
    {
        func(co_await *this);
    }

private:
    explicit Task(std::coroutine_handle<Promise> handle)
        : handle_(handle)
    {
    }

    std::coroutine_handle<Promise> handle_;
};

template <>
struct Task<void>::Promise {
    std::coroutine_handle<> continuation;

    Task get_return_object()
    {
        return Task { std::coroutine_handle<Promise>::from_promise(*this) };
    }

    void unhandled_exception() noexcept { }

    void return_void() noexcept { }

    std::suspend_always initial_suspend() noexcept { return {}; }
    FinalAwaiter final_suspend() noexcept { return {}; }
};
}
