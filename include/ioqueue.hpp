#pragma once

#include <future>
#include <limits>
#include <system_error>
#include <thread>

#include <netinet/in.h>

#include "events.hpp"
#include "function.hpp"
#include "iouring.hpp"
#include "slotmap.hpp"

namespace aiopp {
class IoQueue {
private:
    using CompletionHandler = Function<void(const io_uring_cqe*)>;

    static constexpr auto Ignore = std::numeric_limits<uint64_t>::max();

public:
    struct Metrics {
        std::atomic<uint32_t> ioOperationsQueued;
    };

    using HandlerEc = Function<void(std::error_code ec)>;
    using HandlerEcRes = Function<void(std::error_code ec, int res)>;
    using Timespec = IoURing::Timespec;

    // These are both relative with respect to their arguments, but naming these is hard.
    static void setRelativeTimeout(Timespec* ts, uint64_t milliseconds);
    static void setAbsoluteTimeout(Timespec* ts, uint64_t milliseconds);

    IoQueue(size_t size = 1024, bool submissionQueuePolling = false);

    size_t getSize() const;

    size_t getCapacity() const;

    // TODO: Support cancellation by returning a RequestHandle wrapping an uint64_t containing the
    // SQE userData. Add an operator bool to replicate the old behaviour and add
    // cancel(RequestHandle), that generates an IORING_OP_ASYNC_CANCEL with the wrapped userData.

    // res argument is socket fd
    bool accept(int fd, sockaddr_in* addr, socklen_t* addrlen, HandlerEcRes cb);

    bool connect(int sockfd, const ::sockaddr* addr, socklen_t addrlen, HandlerEc cb);

    // res argument is sent bytes
    bool send(int sockfd, const void* buf, size_t len, HandlerEcRes cb);

    // timeout may be nullptr for convenience (which is equivalent to the function above)
    bool send(int sockfd, const void* buf, size_t len, Timespec* timeout, bool timeoutIsAbsolute,
        HandlerEcRes cb);

    // res argument is received bytes
    bool recv(int sockfd, void* buf, size_t len, HandlerEcRes cb);

    bool recv(int sockfd, void* buf, size_t len, Timespec* timeout, bool timeoutIsAbsolute,
        HandlerEcRes cb);

    bool read(int fd, void* buf, size_t count, HandlerEcRes cb);

    bool close(int fd, HandlerEc cb);

    bool shutdown(int fd, int how, HandlerEc cb);

    bool poll(int fd, short events, HandlerEcRes cb);

    bool recvmsg(int sockfd, ::msghdr* msg, int flags, HandlerEcRes cb);

    bool sendmsg(int sockfd, const ::msghdr* msg, int flags, HandlerEcRes cb);

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
    NotifyHandle wait(Function<void(std::error_code, uint64_t)> cb);

    template <typename Result>
    bool async(Function<Result()> func, Function<void(std::error_code, Result&&)> cb)
    {
        // Only a minimal amount of hair has been ripped out of my skull because of this.
        std::promise<Result> prom;
        auto handle = wait(
            [fut = prom.get_future(), cb = std::move(cb)](std::error_code ec, uint64_t) mutable {
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

    void run();

    const Metrics& getMetrics() const;

private:
    size_t addHandler(HandlerEc&& cb);
    size_t addHandler(HandlerEcRes&& cb);

    template <typename Callback>
    bool addSqe(io_uring_sqe* sqe, Callback cb);

    template <typename Callback>
    bool addSqe(io_uring_sqe* sqe, Timespec* timeout, bool timeoutIsAbsolute, Callback cb);

    IoURing ring_;
    SlotMap<CompletionHandler> completionHandlers_;
    Metrics metrics_;
};
}
