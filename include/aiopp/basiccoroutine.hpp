#pragma once

#include <coroutine>

class BasicCoroutine {
public:
    struct Promise {
        BasicCoroutine get_return_object() { return BasicCoroutine {}; }

        void unhandled_exception() noexcept { }

        void return_void() noexcept { }

        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
    };
    using promise_type = Promise;
};

// With this you can turn a Task into an eager fire-and-forget coroutine, like BasicCoroutine
template <typename Awaitable>
BasicCoroutine fireAndForget(Awaitable awaitable)
{
    co_await awaitable;
}
