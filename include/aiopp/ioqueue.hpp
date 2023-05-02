#pragma once

#include <cassert>
#include <coroutine>
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
        assert(result_ < 0);
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
    static constexpr uint64_t UserDataInvalid = 0;
    static constexpr uint64_t UserDataIgnore = std::numeric_limits<uint64_t>::max();

public:
    using Timespec = IoURing::Timespec;
    using CompletionHandler = Function<void(IoResult)>;

    struct OperationHandle {
        uint64_t userData = UserDataInvalid;

        bool valid() const { return userData != UserDataInvalid; }
        explicit operator bool() const { return valid(); }
    };

    struct AwaiterBase {
        std::coroutine_handle<> caller;
        OperationHandle operation;
        IoResult result;

        void complete(IoResult res)
        {
            result = res;
            operation = {}; // Now the operation has completed, we don't want to cancel it anymore.
            caller.resume();
        }
    };

    template <typename Method, typename... Args>
    class Awaitable {
    public:
        Awaitable(IoQueue& io, Method method, Args... args)
            : io_(io)
            , method_(method)
            , args_(std::make_tuple(args...))
        {
        }

        struct Awaiter : public AwaiterBase {
            Awaitable& awaitable;

            Awaiter(Awaitable& a)
                : awaitable(a)
            {
            }

            // We need to cancel, because it's possible the coroutine and therefore the Awaiter it
            // lives in is destroyed before the IO operation completes. We want to make sure we
            // don't call into a freed AwaiterBase, so we need to cancel the request and unregister.
            ~Awaiter()
            {
                if (operation) {
                    awaitable.io_.cancel(operation, true);
                }
            }

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> handle) noexcept
            {
                caller = handle;
                std::apply(
                    [this](auto&&... args) {
                        operation = (awaitable.io_.*awaitable.method_)(
                            std::forward<decltype(args)>(args)..., this);
                    },
                    awaitable.args_);
            }

            IoResult await_resume() const noexcept { return result; }
        };

        auto operator co_await() { return Awaiter { *this }; }

    private:
        IoQueue& io_;
        Method method_;
        std::tuple<Args...> args_;
    };

    // These are both relative with respect to their arguments, but naming these is hard.
    static void setRelativeTimeout(Timespec* ts, uint64_t milliseconds);
    static void setAbsoluteTimeout(Timespec* ts, uint64_t milliseconds);

    IoQueue(size_t size = 1024, bool submissionQueuePolling = false);

    size_t getSize() const;

    size_t getCapacity() const;

    OperationHandle accept(int fd, ::sockaddr_in* addr, socklen_t* addrlen, CompletionHandler cb);

    OperationHandle acceptAwaiter(
        int fd, ::sockaddr_in* addr, socklen_t* addrlen, AwaiterBase* awaiter);

    auto accept(int fd, ::sockaddr_in* addr, socklen_t* addrlen)
    {
        return Awaitable(*this, &IoQueue::acceptAwaiter, fd, addr, addrlen);
    }

    OperationHandle connect(
        int sockfd, const ::sockaddr* addr, socklen_t addrlen, CompletionHandler cb);

    // res argument is sent bytes
    OperationHandle send(int sockfd, const void* buf, size_t len, CompletionHandler cb);

    OperationHandle sendAwaiter(int sockfd, const void* buf, size_t len, AwaiterBase* awaiter);

    auto send(int sockfd, const void* buf, size_t len)
    {
        return Awaitable(*this, &IoQueue::sendAwaiter, sockfd, buf, len);
    }

    // timeout may be nullptr for convenience (which is equivalent to the function above)
    OperationHandle send(int sockfd, const void* buf, size_t len, Timespec* timeout,
        bool timeoutIsAbsolute, CompletionHandler cb);

    // res argument is received bytes
    OperationHandle recv(int sockfd, void* buf, size_t len, CompletionHandler cb);

    OperationHandle recvAwaiter(int sockfd, void* buf, size_t len, AwaiterBase* awaiter);

    auto recv(int sockfd, void* buf, size_t len)
    {
        return Awaitable(*this, &IoQueue::recvAwaiter, sockfd, buf, len);
    }

    OperationHandle recv(int sockfd, void* buf, size_t len, Timespec* timeout,
        bool timeoutIsAbsolute, CompletionHandler cb);

    OperationHandle read(int fd, void* buf, size_t count, CompletionHandler cb);

    OperationHandle close(int fd, CompletionHandler cb);

    OperationHandle closeAwaiter(int fd, AwaiterBase* awaiter);

    auto close(int fd) { return Awaitable(*this, &IoQueue::closeAwaiter, fd); }

    OperationHandle shutdown(int fd, int how, CompletionHandler cb);

    OperationHandle poll(int fd, short events, CompletionHandler cb);

    OperationHandle recvmsg(int sockfd, ::msghdr* msg, int flags, CompletionHandler cb);

    OperationHandle recvmsgAwaiter(int sockfd, ::msghdr* msg, int flags, AwaiterBase* awaiter);

    auto recvmsg(int sockfd, ::msghdr* msg, int flags)
    {
        return Awaitable(*this, &IoQueue::recvmsgAwaiter, sockfd, msg, flags);
    }

    OperationHandle sendmsg(int sockfd, const ::msghdr* msg, int flags, CompletionHandler cb);

    OperationHandle sendmsgAwaiter(
        int sockfd, const ::msghdr* msg, int flags, AwaiterBase* awaiter);

    auto sendmsg(int sockfd, ::msghdr* msg, int flags)
    {
        return Awaitable(*this, &IoQueue::sendmsgAwaiter, sockfd, msg, flags);
    }

    // These functions are just convenience wrappers on top of recvmsg and sendmsg.
    // They need to wrap the callback and allocate a ::msghdr and ::iovec on the heap.
    // This is also why addrLen is not an in-out parameter, but just an in-parameter.
    // If you need it to be fast, use recvmsg and sendmsg.
    OperationHandle recvfrom(int sockfd, void* buf, size_t len, int flags, ::sockaddr* srcAddr,
        socklen_t addrLen, CompletionHandler cb);

    OperationHandle recvfrom(
        int sockfd, void* buf, size_t len, int flags, ::sockaddr_in* srcAddr, CompletionHandler cb)
    {
        return recvfrom(sockfd, buf, len, flags, reinterpret_cast<::sockaddr*>(srcAddr),
            sizeof(::sockaddr_in), std::move(cb));
    }

    OperationHandle sendto(int sockfd, const void* buf, size_t len, int flags,
        const ::sockaddr* destAddr, socklen_t addrLen, CompletionHandler cb);

    OperationHandle sendto(int sockfd, const void* buf, size_t len, int flags,
        const ::sockaddr_in* destAddr, CompletionHandler cb)
    {
        return sendto(sockfd, buf, len, flags, reinterpret_cast<const ::sockaddr*>(destAddr),
            sizeof(::sockaddr_in), std::move(cb));
    }

    // Note that the cancelation is asynchronous as well, so it is also possible that the operation
    // completes before the cancelation has been consumed. In this case the handler still might get
    // called with a successful result instead of ECANCELED. If cancelHandler is true the handler
    // will not be called again in either case, even if the async cancelation could not be submitted
    // at all.
    OperationHandle cancel(OperationHandle operation, bool cancelHandler);

    void run();

    size_t getNumOpsQueued() const { return numOpsQueued_; }

private:
    struct CallbackCompleter {
        CompletionHandler handler;
    };

    struct CoroutineCompleter {
        AwaiterBase* awaiter;
    };

    OperationHandle addSqe(io_uring_sqe* sqe, CompletionHandler cb);
    OperationHandle addSqe(io_uring_sqe* sqe, AwaiterBase* awaiter);

    OperationHandle addSqe(
        io_uring_sqe* sqe, Timespec* timeout, bool timeoutIsAbsolute, CompletionHandler cb);

    IoURing ring_;
    size_t numOpsQueued_ = 0;
};
}
