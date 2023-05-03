#pragma once

#include <cassert>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>

#include <aiopp/eventfd.hpp>

namespace aiopp {
namespace detail {
    struct SharedBase {
        std::mutex mutex;
        std::condition_variable cv;
        EventFd eventFd;
    };

    template <typename T = void>
    struct SharedState : public SharedBase {
        std::optional<T> value;
    };

    template <>
    struct SharedState<void> : public SharedBase {
        bool ready = false;
    };
}

template <typename T = void>
class Future {
public:
    explicit Future(std::shared_ptr<detail::SharedState<T>> shared)
        : shared_(std::move(shared))
    {
    }

    ~Future() = default;

    // These are deleted mostly because we can only read from the eventfd once
    Future(const Future&) = delete;
    Future& operator=(Future&) = delete;

    Future(Future&&) = default;
    Future& operator=(Future&&) = default;

    bool ready() const
    {
        std::unique_lock lock(shared_->mutex);
        if constexpr (std::is_void_v<T>) {
            return shared_->ready;
        } else {
            return shared_->value.has_value();
        }
    }

    template <typename U = T>
    requires(std::is_void_v<U>) void get()
    {
        std::unique_lock lock(shared_->mutex);
        shared_->cv.wait(lock, [this] { return shared_->ready; });
    }

    template <typename U = T>
    requires(!std::is_void_v<U>) T get()
    {
        std::unique_lock lock(shared_->mutex);
        shared_->cv.wait(lock, [this] { return shared_->value.has_value(); });
        return std::move(shared_->value.value());
    }

    // Read buf for eventfd has to be associated with the operation, not with the future, because in
    // principle I could read from eventfd twice. That means that buffer belongs to operation.

    /*Task<T> wait(IoQueue& io)
    {
        const auto res = co_await io.wait(shared_->eventFd);
        assert(res);
        co_return get();
    }*/

    const EventFd& getEventFd() const { return shared_->eventFd; }

private:
    std::shared_ptr<detail::SharedState<T>> shared_;
};

template <typename T = void>
class Promise {
public:
    Promise()
        : shared_(std::make_shared<detail::SharedState<T>>())
    {
    }

    ~Promise() = default;

    Promise(const Promise&) = delete;
    Promise& operator=(Promise&) = delete;

    Promise(Promise&&) = default;
    Promise& operator=(Promise&&) = default;

    // Only call this once
    Future<T> getFuture() { return Future(shared_); }

    template <typename U = T>
    requires(std::is_void_v<U>) void set()
    {
        {
            std::unique_lock lock(shared_->mutex);
            assert(!shared_->ready);
            shared_->ready = true;
        }
        shared_->cv.notify_all();
    }

    template <typename U = T>
    requires(!std::is_void_v<U>) void set(T value)
    {
        {
            std::unique_lock lock(shared_->mutex);
            assert(!shared_->value.has_value());
            shared_->value = std::move(value);
        }
        shared_->cv.notify_all();
        shared_->eventFd.write();
    }

private:
    std::shared_ptr<detail::SharedState<T>> shared_;
};
}
