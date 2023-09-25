#pragma once

#include <cassert>
#include <chrono>
#include <coroutine>
#include <future>
#include <limits>
#include <memory>
#include <system_error>
#include <thread>

#include <netinet/in.h>

#include "aiopp/basiccoroutine.hpp"
#include "aiopp/function.hpp"
#include "aiopp/future.hpp"
#include "aiopp/log.hpp"
#include "aiopp/result.hpp"
#include "aiopp/socket.hpp"
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

struct IoQueueImpl;

class IoQueue {
public:
    using OperationId = uint64_t;
    static constexpr OperationId OpIdInvalid = std::numeric_limits<OperationId>::max() - 1;
    static constexpr OperationId OpIdIgnore = std::numeric_limits<OperationId>::max();

    // sleep accuracy on linux is a few ms anyways, so I think millis is fine.
    using Duration = std::chrono::milliseconds;
    using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

    struct OperationAwaiter;

    struct CoroutineCompleter {
        OperationAwaiter* awaiter;

        CoroutineCompleter(OperationAwaiter* awaiter)
            : awaiter(awaiter)
        {
        }

        void complete(IoResult result)
        {
            awaiter->result = result;
            // Now the operation has completed, we don't want to cancel it anymore.
            awaiter->operation = {};
            awaiter->caller.resume();
        }
    };

    // If I ever need it, turn this into an abstract base class
    using Completer = CoroutineCompleter;

    struct OperationHandle {
        IoQueue* io = nullptr;
        OperationId id = OpIdInvalid;

        bool valid() const { return io && id != OpIdInvalid; }
        explicit operator bool() const { return valid(); }

        void setCompleter(std::unique_ptr<Completer> completer) const
        {
            io->setCompleter(*this, std::move(completer));
        }

        void cancel(bool cancelHandler) const { io->cancel(*this, cancelHandler); }

        auto operator co_await() const
        {
            assert(*this);
            return OperationAwaiter { *this };
        }

        template <typename Func>
        BasicCoroutine callback(Func func) const
        {
            func(co_await *this);
        }
    };

    struct OperationAwaiter {
        OperationHandle operation;
        std::coroutine_handle<> caller = {};
        IoResult result = {};

        ~OperationAwaiter()
        {
            if (operation) {
                operation.cancel(true);
            }
        }

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> handle) noexcept
        {
            assert(operation);
            caller = handle;
            operation.setCompleter(std::make_unique<CoroutineCompleter>(this));
        }

        IoResult await_resume() const noexcept { return result; }
    };

    IoQueue(size_t size = 1024);

    ~IoQueue(); // We only need this to destruct incomplete IoQueueImpl

    size_t getSize() const;

    size_t getCapacity() const;

    OperationHandle accept(int fd, ::sockaddr_in* addr, socklen_t* addrlen);

    OperationHandle connect(int sockfd, const ::sockaddr* addr, socklen_t addrlen);
    OperationHandle connect(int sockfd, const ::sockaddr_in* addr);
    Task<IoResult> connect(int sockfd, IpAddressPort addr);

    OperationHandle send(int sockfd, const void* buf, size_t len);

    OperationHandle recv(int sockfd, void* buf, size_t len);

    OperationHandle read(int fd, void* buf, size_t count);

    OperationHandle close(int fd);

    OperationHandle shutdown(int fd, int how);

    OperationHandle poll(int fd, short events);

    OperationHandle recvmsg(int sockfd, ::msghdr* msg, int flags);

    OperationHandle sendmsg(int sockfd, const ::msghdr* msg, int flags);

    // These functions are just convenience wrappers on top of recvmsg and sendmsg.
    // They need to wrap the callback and allocate a ::msghdr and ::iovec on the heap.
    // This is also why addrLen is not an in-out parameter, but just an in-parameter.
    // If you need it to be fast, use recvmsg and sendmsg.
    Task<IoResult> recvfrom(
        int sockfd, void* buf, size_t len, int flags, ::sockaddr* srcAddr, socklen_t addrLen);

    template <typename SockAddr>
    Task<IoResult> recvfrom(int sockfd, void* buf, size_t len, int flags, SockAddr* srcAddr)
    {
        return recvfrom(
            sockfd, buf, len, flags, reinterpret_cast<::sockaddr*>(srcAddr), sizeof(SockAddr));
    }

    Task<IoResult> sendto(int sockfd, const void* buf, size_t len, int flags,
        const ::sockaddr* destAddr, socklen_t addrLen);

    template <typename SockAddr>
    Task<IoResult> sendto(
        int sockfd, const void* buf, size_t len, int flags, const SockAddr* destAddr)
    {
        return sendto(sockfd, buf, len, flags, reinterpret_cast<const ::sockaddr*>(destAddr),
            sizeof(SockAddr));
    }

    Task<IoResult> timeout(Duration dur);
    Task<IoResult> timeout(TimePoint tp);

    // The passed operation will return ECANCELED in case the timeout expired.
    Task<IoResult> timeout(Duration dur, OperationHandle op);
    Task<IoResult> timeout(TimePoint tp, OperationHandle op);

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

    // Note that the cancelation can be asynchronous as well, so it is also possible that the
    // operation to be canceled completes successfuly before the cancelation has been consumed. If
    // cancelHandler is true then the handler will be disabled asynchronously as part of this
    // function, and the handler will not be called in either case.
    OperationHandle cancel(OperationHandle operation, bool cancelHandler);

    void run();

private:
    friend struct IoQueueImpl;

    void setCompleter(OperationHandle operation, std::unique_ptr<Completer> completer);
    OperationId getNextOpId();

    OperationId nextOpId_ = 0;
    std::unique_ptr<IoQueueImpl> impl_;
};
}
