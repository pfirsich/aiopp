#pragma once

#include <cassert>
#include <coroutine>
#include <future>
#include <limits>
#include <memory>
#include <system_error>
#include <thread>

#include <netinet/in.h>

#include "aiopp/completermap.hpp"
#include "aiopp/function.hpp"
#include "aiopp/future.hpp"
#include "aiopp/iouring.hpp"
#include "aiopp/log.hpp"
#include "aiopp/result.hpp"
#include "aiopp/task.hpp"

namespace aiopp {
struct IoResult {
public:
    IoResult() = default;

    IoResult(int res)
        : result_(res)
    {
    }

    std::error_code error() const { return std::make_error_code(static_cast<std::errc>(-result_)); }

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
    static constexpr uint64_t UserDataInvalid = std::numeric_limits<uint64_t>::max() - 1;
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
                    std::move(awaitable.args_));
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

    // SQ Polling is disabled by default, because while it does reduce the amount of syscalls, it
    // barely increases throughput and noticably increases CPU usage when the queue is idling.
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

    OperationHandle readAwaiter(int fd, void* buf, size_t count, AwaiterBase* awaiter);

    auto read(int fd, void* buf, size_t count)
    {
        return Awaitable(*this, &IoQueue::readAwaiter, fd, buf, count);
    }

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

    Task<IoResult> recvfrom(int sockfd, void* buf, size_t len, int flags, ::sockaddr_in* srcAddr);

    OperationHandle sendto(int sockfd, const void* buf, size_t len, int flags,
        const ::sockaddr* destAddr, socklen_t addrLen, CompletionHandler cb);

    OperationHandle sendto(int sockfd, const void* buf, size_t len, int flags,
        const ::sockaddr_in* destAddr, CompletionHandler cb)
    {
        return sendto(sockfd, buf, len, flags, reinterpret_cast<const ::sockaddr*>(destAddr),
            sizeof(::sockaddr_in), std::move(cb));
    }

    Task<IoResult> sendto(
        int sockfd, const void* buf, size_t len, int flags, const ::sockaddr_in* destAddr);

    OperationHandle timeout(Timespec* ts, uint64_t count, uint32_t flags, CompletionHandler cb);

    OperationHandle timeoutAwaiter(
        Timespec* ts, uint64_t count, uint32_t flags, AwaiterBase* awaiter);

    auto timeout(Timespec* ts, uint64_t count, uint32_t flags)
    {
        using TimeoutAwaiter
            = OperationHandle (IoQueue::*)(Timespec*, uint64_t, uint32_t, AwaiterBase*);
        return Awaitable(
            *this, static_cast<TimeoutAwaiter>(&IoQueue::timeoutAwaiter), ts, count, flags);
    }

    // sleep accuracy on linux is a few ms anyways, so millis is fine.
    using Duration = std::chrono::milliseconds;

    OperationHandle timeout(Duration dur, CompletionHandler cb);

    Task<void> timeout(Duration dur);

    using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

    OperationHandle timeout(TimePoint point, CompletionHandler cb);

    Task<void> timeout(TimePoint point);

    // The IoQueue has to take ownership of the future, because it will call .get() on it.
    template <typename T>
    OperationHandle wait(Future<T> future, Function<void(Result<T>)> cb)
    {
        // I don't use EventFd::read here, because that doesn't return an OperationHandle and it
        // doesn't return an OperationHandle, because then it would depend on IoQueue, but IoQueue
        // already depends on EventFd.
        auto readBuf = std::make_unique<uint64_t>();
        return read(future.getEventFd().fd(), readBuf.get(), sizeof(uint64_t),
            [future = std::move(future), cb = std::move(cb), readBuf = std::move(readBuf)](
                IoResult result) {
                if (!result) {
                    cb(error(result.error()));
                } else {
                    assert(*result == 8);
                    cb(future.get());
                }
            });
    }

    template <typename T>
    Task<T> wait(Future<T> future)
    {
        if (future.ready()) {
            co_return future.get();
        }
        const auto res = co_await future.getEventFd().read(*this);
        if (!res) {
            getLogger().log(
                LogSeverity::Fatal, "Error reading from eventfd: " + res.error().message());
            std::abort();
        }
        assert(*res == 1);
        co_return future.get();
    }

    // Note that the cancelation is asynchronous as well, so it is also possible that the operation
    // completes before the cancelation has been consumed. In this case the handler still might get
    // called with a successful result instead of ECANCELED. If cancelHandler is true the handler
    // will not be called again in either case, even if the async cancelation could not be submitted
    // at all.
    OperationHandle cancel(OperationHandle operation, bool cancelHandler);

    void run();

private:
    struct GenericCompleter {
        virtual ~GenericCompleter() = default;
        virtual void clear() = 0;
        virtual void complete(IoResult) = 0;
    };

    template <typename Storage>
    struct StorageCallbackCompleter final : public GenericCompleter {
        CompletionHandler handler;
        Storage storage;

        StorageCallbackCompleter(CompletionHandler handler)
            : handler(std::move(handler))
        {
        }

        void clear() override { handler = nullptr; }

        void complete(IoResult result) override
        {
            if (handler) {
                handler(result);
            }
        }
    };

    template <typename Storage>
    struct StorageCoroutineCompleter final : public GenericCompleter {
        AwaiterBase* awaiter;
        Storage storage;

        StorageCoroutineCompleter(AwaiterBase* awaiter)
            : awaiter(awaiter)
        {
        }

        void clear() override { awaiter = nullptr; }

        void complete(IoResult result) override
        {
            if (awaiter) {
                awaiter->complete(result);
            }
        }
    };

    struct CallbackCompleter {
        CompletionHandler handler;
    };

    struct CoroutineCompleter {
        AwaiterBase* awaiter;
    };

    uint64_t addCompleter(void* completer);

    OperationHandle addSqe(io_uring_sqe* sqe, CompletionHandler cb);
    OperationHandle addSqe(io_uring_sqe* sqe, AwaiterBase* awaiter);
    OperationHandle addSqe(io_uring_sqe* sqe, GenericCompleter* awaiter);

    OperationHandle addSqe(
        io_uring_sqe* sqe, Timespec* timeout, bool timeoutIsAbsolute, CompletionHandler cb);

    IoURing ring_;
    CompleterMap completers_;
    uint64_t nextUserData_ = 0;
};
}
