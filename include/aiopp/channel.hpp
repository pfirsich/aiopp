#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

#include "aiopp/ioqueue.hpp"
#include "aiopp/log.hpp"

namespace aiopp {
template <typename Message>
class Channel {
public:
    Channel(IoQueue& io)
        : io_(io)
        , eventFd_(EventFd::Flags::Semaphore)
    {
    }

    void send(Message msg)
    {
        {
            std::unique_lock lock(mutex_);
            messages_.push(std::move(msg));
        }
        eventFd_.write();
    }

    void receive(Function<void(Message)> callback)
    {
        eventFd_.read(io_, [this, callback = std::move(callback)](Result<uint64_t> res) {
            if (!res) {
                getLogger().log(LogSeverity::Fatal,
                    "Error reading from eventfd in Channel: " + res.error().message());
                std::abort();
            }
            auto msg = pop();
            callback(std::move(msg));
        });
    }

    Task<Message> receive()
    {
        const auto res = co_await eventFd_.read(io_);
        if (!res) {
            getLogger().log(LogSeverity::Fatal,
                "Error reading from eventfd in Channel: " + res.error().message());
            std::abort();
        }
        co_return pop();
    }

private:
    Message pop()
    {
        std::unique_lock lock(mutex_);
        assert(!messages_.empty());
        auto msg = std::move(messages_.front());
        messages_.pop();
        return msg;
    }

    IoQueue& io_;
    std::mutex mutex_;
    std::queue<Message> messages_;
    EventFd eventFd_;
};
}
