#pragma once

#include <optional>

#include "basiccoroutine.hpp"

namespace aiopp {
template <typename T>
concept Iterable = requires(T t)
{
    t.begin();
    t.end();
};

class WaitAll {
public:
    template <Iterable Container>
    WaitAll(Container&& container)
    {
        for (auto&& awaitable : container) {
            add(awaitable);
        }
    }

    template <typename... Args>
    WaitAll(Args&&... args)
    {
        (add(std::forward<Args>(args)), ...);
    }

    // This is kind of tricky and I am not 100% sure if this is correct or if there is a more
    // elegant way to do it, but there are two overloads of `add`, so we can take references to
    // lvalues and take ownership of temporaries.
    // That way you can do something like `WaitAll wait { task, anotherTask() }; co_await wait;`
    // where the first task will simply be referenced (the first overload) and the second
    // (temporary) task will be moved into the lambda coroutine, so that it will not get destroyed
    // at the end of the expression (before WaitAll is awaited).

    template <typename Awaitable>
    void add(Awaitable& awaitable)
    {
        [this](Awaitable& awaitable) -> BasicCoroutine {
            startAwaitable();
            co_await awaitable;
            awaitableCompleted();
        }(awaitable);
    }

    template <typename Awaitable>
    void add(Awaitable&& awaitable)
    {
        [this](std::decay_t<Awaitable> awaitable) -> BasicCoroutine {
            startAwaitable();
            co_await awaitable;
            awaitableCompleted();
        }(std::forward<Awaitable>(awaitable));
    }

    auto operator co_await() noexcept
    {
        struct Awaiter {
            WaitAll* waitAll;

            bool await_ready() noexcept { return waitAll->pending_ == 0; }

            void await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                waitAll->continuation_ = continuation;
            }

            void await_resume() noexcept { }
        };
        return Awaiter { this };
    }

private:
    void startAwaitable() { pending_++; }

    void awaitableCompleted()
    {
        pending_--;
        if (continuation_ && pending_ == 0) {
            continuation_.resume();
        }
    }

    size_t pending_ { 0 };
    std::coroutine_handle<> continuation_;
};

class WaitAny {
public:
    template <Iterable Container>
    WaitAny(Container&& container)
    {
        for (auto&& awaitable : container) {
            add(awaitable);
        }
    }

    template <typename... Args>
    WaitAny(Args&&... args)
    {
        (add(std::forward<Args>(args)), ...);
    }

    template <typename Awaitable>
    void add(Awaitable& awaitable)
    {
        [this](Awaitable& awaitable) -> BasicCoroutine {
            const auto idx = startAwaitable();
            co_await awaitable;
            awaitableCompleted(idx);
        }(awaitable);
    }

    template <typename Awaitable>
    void add(Awaitable&& awaitable)
    {
        [this](std::decay_t<Awaitable> awaitable) -> BasicCoroutine {
            const auto idx = startAwaitable();
            co_await awaitable;
            awaitableCompleted(idx);
        }(std::forward<Awaitable>(awaitable));
    }

    auto operator co_await() noexcept
    {
        struct Awaiter {
            WaitAny* waitAny;

            bool await_ready() noexcept { return waitAny->completed_.has_value(); }

            void await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                waitAny->continuation_ = continuation;
            }

            size_t await_resume() noexcept { return waitAny->completed_.value(); }
        };
        return Awaiter { this };
    }

private:
    size_t startAwaitable()
    {
        const auto idx = started_;
        started_++;
        return idx;
    }

    void awaitableCompleted(size_t idx)
    {
        if (!completed_) {
            completed_ = idx;
        }
        if (continuation_) {
            continuation_.resume();
        }
    }

    size_t started_ { 0 };
    std::optional<size_t> completed_;
    std::coroutine_handle<> continuation_;
};
}
