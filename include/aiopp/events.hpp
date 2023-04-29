#pragma once

#include <cstdint>
#include <functional>
#include <system_error>

#include "aiopp/fd.hpp"
#include "aiopp/ioqueue.hpp"
#include "aiopp/log.hpp"
#include "aiopp/mpscqueue.hpp"

namespace aiopp {
class EventFd {
public:
    EventFd(IoQueue& io);

    // I do not close fd_ asynchronously in ~EventFd here, because EventFd might be destroyed
    // from another thread (from which async IO operations are not allowed).

    // The read will complete once the counter stored in the eventfd is > 0.
    // Then it will read the current value and reset the counter to 0.
    bool read(Function<void(std::error_code, uint64_t)> cb);

    // This will increase the counter stored in the eventfd by `v`.
    // Note that this function writes SYNCHRONOUSLY, so it can be used from other threads, but it
    // also means that it will not be as fast and it might block (unlikely though). This means you
    // need to be careful about using it from the main thread, because it might block the IoQueue.
    void write(uint64_t v);

private:
    IoQueue& io_;
    Fd fd_;
    uint64_t readBuf_;
};

class NotifyHandle {
public:
    NotifyHandle(std::shared_ptr<EventFd> eventFd);

    // wait might fail, in which case this will return false
    explicit operator bool() const;

    // Note that all restrictions on EventFd::write apply here as well (writes synchronously, so
    // don't use from the main thread, but can be used from other threads).
    // Also this function must be called exactly once. If it is not called, the async read on
    // the eventfd will never terminate. If you call it more than once, there is no read queued
    // up, so this function will abort.
    void notify(uint64_t value = 1);

private:
    // We need shared ownership, because wait will issue an async read, which needs to have
    // ownership of this event fd as well.
    std::shared_ptr<EventFd> eventFd_;
};

// This will call a handler callback, when the NotifyHandle is notified.
// The value passed to NotifyHandle::notify will be passed to the handler cb.
NotifyHandle wait(IoQueue& io, Function<void(std::error_code, uint64_t)> cb);

template <typename Result>
bool async(IoQueue& io, Function<Result()> func, Function<void(std::error_code, Result&&)> cb)
{
    std::promise<Result> prom;
    auto handle = wait(
        io, [fut = prom.get_future(), cb = std::move(cb)](std::error_code ec, uint64_t) mutable {
            if (ec) {
                cb(ec, Result());
            } else {
                cb(std::error_code(), std::move(fut.get()));
            }
        });
    if (!handle) {
        return false;
    }

    // Simply detaching a thread is really not very clean, but it's easy and enough for my
    // current use cases.
    std::thread t(
        [func = std::move(func), prom = std::move(prom), handle = std::move(handle)]() mutable {
            prom.set_value(func());
            handle.notify();
        });
    t.detach();
    return true;
}

// This class provides a way send messages to the main thread where they can be handled
// asynchronously. It's main purpose is to provide a way to have other threads to IO through the IO
// Queue (e.g. the ACME client).
// For something like that use an Event class that contains some parameters and a promise and use an
// eventHandler that uses the parameters to start an asynchronous IO operation that fulfills the
// promise when it completes.
template <typename Event>
class EventListener {
public:
    // The class needs to be constructed from the main thread
    EventListener(IoQueue& io, Function<void(Event&& event)> eventHandler)
        : eventHandler_(std::move(eventHandler))
        , eventFd_(io)
    {
        pollQueue();
    }

    // This can be called from any thread!
    void emit(Event&& event)
    {
        queue_.produce(std::move(event));
        eventFd_.write(1);
    }

private:
    void pollQueue()
    {
        eventFd_.read([this](std::error_code ec, uint64_t) {
            if (ec) {
                getLogger().log(LogSeverity::Error, "Error reading eventfd: " + ec.message());
            } else {
                while (true) {
                    auto event = queue_.consume();
                    if (!event) {
                        break;
                    }
                    eventHandler_(std::move(*event));
                }
            }
            pollQueue();
        });
    }

    Function<void(Event&& event)> eventHandler_;
    MpscQueue<Event> queue_;
    EventFd eventFd_;
};
}
