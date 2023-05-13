#pragma once

#include <cassert>
#include <coroutine>
#include <future>
#include <limits>
#include <memory>
#include <system_error>
#include <thread>

#include <netinet/in.h>

#include "aiopp/basiccoroutine.hpp"
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
        uint64_t userData = UserDataInvalid;

        bool valid() const { return io && userData != UserDataInvalid; }
        explicit operator bool() const { return valid(); }

        void setCompleter(std::unique_ptr<Completer> completer)
        {
            io->setCompleter(*this, std::move(completer));
        }

        void cancel() { io->cancel(*this, true); }

        auto operator co_await()
        {
            assert(*this);
            return OperationAwaiter { *this };
        }

        template <typename Func>
        BasicCoroutine callback(Func func)
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
                operation.cancel();
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

    // These are both relative with respect to their arguments, but naming these is hard.
    static void setRelativeTimeout(Timespec* ts, uint64_t milliseconds);
    static void setAbsoluteTimeout(Timespec* ts, uint64_t milliseconds);

    // SQ Polling is disabled by default, because while it does reduce the amount of syscalls, it
    // barely increases throughput and noticably increases CPU usage when the queue is idling.
    IoQueue(size_t size = 1024, bool submissionQueuePolling = false);

    size_t getSize() const;

    size_t getCapacity() const;

    OperationHandle accept(int fd, ::sockaddr_in* addr, socklen_t* addrlen);

    OperationHandle connect(int sockfd, const ::sockaddr* addr, socklen_t addrlen);

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

    OperationHandle timeout(Timespec* ts, uint64_t count, uint32_t flags);

    // sleep accuracy on linux is a few ms anyways, so millis is fine.
    using Duration = std::chrono::milliseconds;

    Task<void> timeout(Duration dur);

    using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

    Task<void> timeout(TimePoint point);

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
    OperationHandle setUserData(io_uring_sqe* sqe);
    void setCompleter(OperationHandle operation, std::unique_ptr<Completer> completer);

    IoURing ring_;
    CompleterMap completers_;
    uint64_t nextUserData_ = 0;
};
}
