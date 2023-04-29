#pragma once

#include <cassert>
#include <future>
#include <limits>
#include <system_error>
#include <thread>

#include <netinet/in.h>

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
