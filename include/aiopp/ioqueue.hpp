#pragma once

#include <cassert>
#include <future>
#include <limits>
#include <system_error>
#include <thread>

#include <netinet/in.h>

#include "aiopp/events.hpp"
#include "aiopp/function.hpp"
#include "aiopp/iouring.hpp"

namespace aiopp {
struct IoResult {
public:
    IoResult() = default;

    IoResult(int res)
        : result_(res)
    {
    }

    std::error_code error() const
    {
        assert(result_ < 0 && result_ != std::numeric_limits<int>::min());
        return std::make_error_code(static_cast<std::errc>(result_));
    }

    int result() const
    {
        assert(result_ >= 0);
        return result_;
    }

    int operator*() const { return result(); }

    explicit operator bool() const { return result_ >= 0; }

private:
    int result_ = std::numeric_limits<int>::min(); // not a valid errno
};

class IoQueue {
    static constexpr auto UserDataInvalid = 0;
    static constexpr auto UserDataIgnore = std::numeric_limits<uint64_t>::max();

public:
    using Timespec = IoURing::Timespec;
    using CompletionHandler = Function<void(IoResult)>;

    // These are both relative with respect to their arguments, but naming these is hard.
    static void setRelativeTimeout(Timespec* ts, uint64_t milliseconds);
    static void setAbsoluteTimeout(Timespec* ts, uint64_t milliseconds);

    IoQueue(size_t size = 1024, bool submissionQueuePolling = false);

    size_t getSize() const;

    size_t getCapacity() const;

    // TODO: Support cancellation by returning a RequestHandle wrapping an uint64_t containing the
    // SQE userData. Add an operator bool to replicate the old behaviour and add
    // cancel(RequestHandle), that generates an IORING_OP_ASYNC_CANCEL with the wrapped userData.

    bool accept(int fd, ::sockaddr_in* addr, socklen_t* addrlen, CompletionHandler cb);

    bool connect(int sockfd, const ::sockaddr* addr, socklen_t addrlen, CompletionHandler cb);

    // res argument is sent bytes
    bool send(int sockfd, const void* buf, size_t len, CompletionHandler cb);

    // timeout may be nullptr for convenience (which is equivalent to the function above)
    bool send(int sockfd, const void* buf, size_t len, Timespec* timeout, bool timeoutIsAbsolute,
        CompletionHandler cb);

    // res argument is received bytes
    bool recv(int sockfd, void* buf, size_t len, CompletionHandler cb);

    bool recv(int sockfd, void* buf, size_t len, Timespec* timeout, bool timeoutIsAbsolute,
        CompletionHandler cb);

    bool read(int fd, void* buf, size_t count, CompletionHandler cb);

    bool close(int fd, CompletionHandler cb);

    bool shutdown(int fd, int how, CompletionHandler cb);

    bool poll(int fd, short events, CompletionHandler cb);

    bool recvmsg(int sockfd, ::msghdr* msg, int flags, CompletionHandler cb);

    bool sendmsg(int sockfd, const ::msghdr* msg, int flags, CompletionHandler cb);

    // These functions are just convenience wrappers on top of recvmsg and sendmsg.
    // They need to wrap the callback and allocate a ::msghdr and ::iovec on the heap.
    // This is also why addrLen is not an in-out parameter, but just an in-parameter.
    // If you need it to be fast, use recvmsg and sendmsg.
    bool recvfrom(int sockfd, void* buf, size_t len, int flags, ::sockaddr* srcAddr,
        socklen_t addrLen, CompletionHandler cb);

    bool recvfrom(
        int sockfd, void* buf, size_t len, int flags, ::sockaddr_in* srcAddr, CompletionHandler cb)
    {
        return recvfrom(sockfd, buf, len, flags, reinterpret_cast<::sockaddr*>(srcAddr),
            sizeof(::sockaddr_in), std::move(cb));
    }

    bool sendto(int sockfd, const void* buf, size_t len, int flags, const ::sockaddr* destAddr,
        socklen_t addrLen, CompletionHandler cb);

    bool sendto(int sockfd, const void* buf, size_t len, int flags, const ::sockaddr_in* destAddr,
        CompletionHandler cb)
    {
        return sendto(sockfd, buf, len, flags, reinterpret_cast<const ::sockaddr*>(destAddr),
            sizeof(::sockaddr_in), std::move(cb));
    }

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

    size_t getNumOpsQueued() const { return numOpsQueued_; }

private:
    struct CallbackCompleter {
        CompletionHandler handler;
    };

    bool addSqe(io_uring_sqe* sqe, CompletionHandler cb);
    bool addSqe(io_uring_sqe* sqe, Timespec* timeout, bool timeoutIsAbsolute, CompletionHandler cb);

    IoURing ring_;
    size_t numOpsQueued_ = 0;
};
}
