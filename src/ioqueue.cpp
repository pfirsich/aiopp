#include "aiopp/ioqueue.hpp"

#include <time.h>

#include <sys/eventfd.h>
#include <unistd.h>

#include "aiopp/log.hpp"
#include "aiopp/util.hpp"

namespace aiopp {
namespace {
    struct PointerTags {
        enum class Type { Coroutine = 0, Callback = 1, Generic = 2 };
        Type type;
    };

    template <typename T>
    uint64_t tagPointer(T* ptr, PointerTags tags)
    {
        static_assert(std::alignment_of_v<T> >= 8);
        uint64_t userData = reinterpret_cast<uintptr_t>(ptr);
        userData |= static_cast<int>(tags.type);
        return userData;
    }

    std::pair<void*, PointerTags> untagPointer(uint64_t userData)
    {
        // The upper 16 bits are unused in x64 and since the types we are pointing to are aligned to
        // at least 8 bytes, the lowest 3 bits are always 0 and can be used for tags;
        const auto ptr = userData & (0x0000'ffff'ffff'fff0 | 0b11111000);
        const auto type = static_cast<PointerTags::Type>(userData & 0b11);
        return { reinterpret_cast<void*>(ptr), PointerTags { .type = type } };
    }
}

void IoQueue::setRelativeTimeout(Timespec* ts, uint64_t milliseconds)
{
    ts->tv_sec = milliseconds / 1000;
    ts->tv_nsec = (milliseconds % 1000) * 1000 * 1000;
}

void IoQueue::setAbsoluteTimeout(Timespec* ts, uint64_t milliseconds)
{
    ::timespec nowTs;
    ::clock_gettime(CLOCK_MONOTONIC, &nowTs);
    ts->tv_sec = nowTs.tv_sec + milliseconds / 1000;
    ts->tv_nsec = nowTs.tv_nsec + (milliseconds % 1000) * 1000 * 1000;
    ts->tv_sec += ts->tv_nsec / (1000 * 1000 * 1000);
    ts->tv_nsec = ts->tv_nsec % (1000 * 1000 * 1000);
}

IoQueue::IoQueue(size_t size, bool submissionQueuePolling)
{
    if (!ring_.init(size, submissionQueuePolling)) {
        getLogger().log(LogSeverity::Fatal, "Could not create io_uring: " + errnoToString(errno));
        std::exit(1);
    }
    if (!(ring_.getParams().features & IORING_FEAT_NODROP)) {
        getLogger().log(LogSeverity::Fatal, "io_uring does not support NODROP");
        std::exit(1);
    }
    if (!(ring_.getParams().features & IORING_FEAT_SUBMIT_STABLE)) {
        getLogger().log(LogSeverity::Fatal, "io_uring does not support SUBMIT_STABLE");
        std::exit(1);
    }
}

size_t IoQueue::getSize() const
{
    return ring_.getNumSqeEntries();
}

size_t IoQueue::getCapacity() const
{
    return ring_.getSqeCapacity();
}

IoQueue::OperationHandle IoQueue::accept(
    int fd, ::sockaddr_in* addr, socklen_t* addrlen, CompletionHandler cb)
{
    return addSqe(
        ring_.prepareAccept(fd, reinterpret_cast<sockaddr*>(addr), addrlen), std::move(cb));
}

IoQueue::OperationHandle IoQueue::acceptAwaiter(
    int fd, ::sockaddr_in* addr, socklen_t* addrlen, AwaiterBase* awaiter)
{
    return addSqe(ring_.prepareAccept(fd, reinterpret_cast<sockaddr*>(addr), addrlen), awaiter);
}

IoQueue::OperationHandle IoQueue::connect(
    int sockfd, const ::sockaddr* addr, socklen_t addrlen, CompletionHandler cb)
{
    return addSqe(ring_.prepareConnect(sockfd, addr, addrlen), std::move(cb));
}

IoQueue::OperationHandle IoQueue::send(
    int sockfd, const void* buf, size_t len, CompletionHandler cb)
{
    return addSqe(ring_.prepareSend(sockfd, buf, len), std::move(cb));
}

IoQueue::OperationHandle IoQueue::sendAwaiter(
    int sockfd, const void* buf, size_t len, AwaiterBase* awaiter)
{
    return addSqe(ring_.prepareSend(sockfd, buf, len), awaiter);
}

IoQueue::OperationHandle IoQueue::send(int sockfd, const void* buf, size_t len,
    IoQueue::Timespec* timeout, bool timeoutIsAbsolute, CompletionHandler cb)
{
    if (!timeout) {
        return send(sockfd, buf, len, std::move(cb));
    }
    return addSqe(ring_.prepareSend(sockfd, buf, len), timeout, timeoutIsAbsolute, std::move(cb));
}

IoQueue::OperationHandle IoQueue::recv(int sockfd, void* buf, size_t len, CompletionHandler cb)
{
    return addSqe(ring_.prepareRecv(sockfd, buf, len), std::move(cb));
}

IoQueue::OperationHandle IoQueue::recvAwaiter(
    int sockfd, void* buf, size_t len, AwaiterBase* awaiter)
{
    return addSqe(ring_.prepareRecv(sockfd, buf, len), awaiter);
}

IoQueue::OperationHandle IoQueue::recv(int sockfd, void* buf, size_t len,
    IoQueue::Timespec* timeout, bool timeoutIsAbsolute, CompletionHandler cb)
{
    if (!timeout) {
        return recv(sockfd, buf, len, std::move(cb));
    }
    return addSqe(ring_.prepareRecv(sockfd, buf, len), timeout, timeoutIsAbsolute, std::move(cb));
}

IoQueue::OperationHandle IoQueue::read(int fd, void* buf, size_t count, CompletionHandler cb)
{
    return addSqe(ring_.prepareRead(fd, buf, count), std::move(cb));
}

IoQueue::OperationHandle IoQueue::close(int fd, CompletionHandler cb)
{
    return addSqe(ring_.prepareClose(fd), std::move(cb));
}

IoQueue::OperationHandle IoQueue::closeAwaiter(int fd, AwaiterBase* awaiter)
{
    return addSqe(ring_.prepareClose(fd), awaiter);
}

IoQueue::OperationHandle IoQueue::shutdown(int fd, int how, CompletionHandler cb)
{
    return addSqe(ring_.prepareShutdown(fd, how), std::move(cb));
}

IoQueue::OperationHandle IoQueue::poll(int fd, short events, CompletionHandler cb)
{
    return addSqe(ring_.preparePollAdd(fd, events), std::move(cb));
}

IoQueue::OperationHandle IoQueue::recvmsg(
    int sockfd, ::msghdr* msg, int flags, CompletionHandler cb)
{
    return addSqe(ring_.prepareRecvmsg(sockfd, msg, flags), std::move(cb));
}

IoQueue::OperationHandle IoQueue::recvmsgAwaiter(
    int sockfd, ::msghdr* msg, int flags, AwaiterBase* awaiter)
{
    return addSqe(ring_.prepareRecvmsg(sockfd, msg, flags), awaiter);
}

IoQueue::OperationHandle IoQueue::sendmsg(
    int sockfd, const ::msghdr* msg, int flags, CompletionHandler cb)
{
    return addSqe(ring_.prepareSendmsg(sockfd, msg, flags), std::move(cb));
}

IoQueue::OperationHandle IoQueue::sendmsgAwaiter(
    int sockfd, const ::msghdr* msg, int flags, AwaiterBase* awaiter)
{
    return addSqe(ring_.prepareSendmsg(sockfd, msg, flags), awaiter);
}

namespace {
    struct MsgHdr {
        ::iovec iov;
        ::msghdr msg;

        MsgHdr(void* buf, size_t len, void* addr, socklen_t addrLen)
            : iov { .iov_base = buf, .iov_len = len }
            , msg {
                .msg_name = addr,
                .msg_namelen = addrLen,
                .msg_iov = &iov,
                .msg_iovlen = 1,
                .msg_controllen = 0,
                .msg_flags = 0,
            }
        {
        }
    };
}

IoQueue::OperationHandle IoQueue::recvfrom(int sockfd, void* buf, size_t len, int flags,
    ::sockaddr* srcAddr, socklen_t addrLen, CompletionHandler cb)
{
    auto msgHdr = std::make_unique<MsgHdr>(buf, len, srcAddr, addrLen);
    return recvmsg(sockfd, &msgHdr->msg, flags, std::move(cb));
}

Task<IoResult> IoQueue::recvfrom(
    int sockfd, void* buf, size_t len, int flags, ::sockaddr_in* srcAddr)
{
    MsgHdr msgHdr(buf, len, srcAddr, sizeof(::sockaddr_in));
    co_return co_await recvmsg(sockfd, &msgHdr.msg, flags);
}

IoQueue::OperationHandle IoQueue::sendto(int sockfd, const void* buf, size_t len, int flags,
    const ::sockaddr* destAddr, socklen_t addrLen, CompletionHandler cb)
{
    auto msgHdr = std::make_unique<MsgHdr>(
        const_cast<void*>(buf), len, const_cast<::sockaddr*>(destAddr), addrLen);
    return sendmsg(sockfd, &msgHdr->msg, flags, std::move(cb));
}

Task<IoResult> IoQueue::sendto(
    int sockfd, const void* buf, size_t len, int flags, const ::sockaddr_in* destAddr)
{
    MsgHdr msgHdr(
        const_cast<void*>(buf), len, const_cast<::sockaddr_in*>(destAddr), sizeof(::sockaddr_in));
    co_return co_await sendmsg(sockfd, &msgHdr.msg, flags);
}

namespace {
    IoQueue::Timespec toTimespec(std::chrono::milliseconds dur)
    {
        IoQueue::Timespec ts;
        ts.tv_sec = dur.count() / 1000;
        ts.tv_nsec = (dur.count() % 1000) * 1000 * 1000;
        return ts;
    }

    IoQueue::Timespec toTimespec(std::chrono::time_point<std::chrono::steady_clock> point)
    {
        const auto now = std::chrono::steady_clock::now();
        assert(now >= point);
        const auto delta = point - std::chrono::steady_clock::now();
        const auto deltaMs = std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
        ::timespec nowTs;
        ::clock_gettime(CLOCK_MONOTONIC, &nowTs);
        IoQueue::Timespec ts;
        ts.tv_sec = nowTs.tv_sec + deltaMs / 1000;
        ts.tv_nsec = nowTs.tv_nsec + (deltaMs % 1000) * 1000 * 1000;
        ts.tv_sec += ts.tv_nsec / (1000 * 1000 * 1000);
        ts.tv_nsec = ts.tv_nsec % (1000 * 1000 * 1000);
        return ts;
    }
}

IoQueue::OperationHandle IoQueue::timeout(
    Timespec* ts, uint64_t count, uint32_t flags, CompletionHandler cb)
{
    return addSqe(ring_.prepareTimeout(ts, count, flags), std::move(cb));
}

IoQueue::OperationHandle IoQueue::timeoutAwaiter(
    Timespec* ts, uint64_t count, uint32_t flags, AwaiterBase* awaiter)
{
    return addSqe(ring_.prepareTimeout(ts, count, flags), awaiter);
}

IoQueue::OperationHandle IoQueue::timeout(Duration dur, CompletionHandler cb)
{
    auto ts = std::make_unique<Timespec>(toTimespec(dur));
    return timeout(ts.get(), 0, 0, std::move(cb));
}

Task<void> IoQueue::timeout(Duration dur)
{
    auto ts = toTimespec(dur);
    co_await timeout(&ts, 0, 0);
}

IoQueue::OperationHandle IoQueue::timeout(TimePoint point, CompletionHandler cb)
{
    auto ts = std::make_unique<Timespec>(toTimespec(point));
    return timeout(ts.get(), 0, 0, std::move(cb));
}

Task<void> IoQueue::timeout(TimePoint point)
{
    auto ts = toTimespec(point);
    co_await timeout(&ts, 0, IORING_TIMEOUT_ABS);
}

IoQueue::OperationHandle IoQueue::cancel(OperationHandle operation, bool cancelHandler)
{
    assert(operation);
    if (cancelHandler) {
        const auto [ptr, tags] = untagPointer(operation.userData);
        if (tags.type == PointerTags::Type::Coroutine) {
            reinterpret_cast<CoroutineCompleter*>(ptr)->awaiter = nullptr;
        } else if (tags.type == PointerTags::Type::Callback) {
            reinterpret_cast<CallbackCompleter*>(ptr)->handler = nullptr;
        } else if (tags.type == PointerTags::Type::Generic) {
            reinterpret_cast<GenericCompleter*>(ptr)->clear();
        }
    }
    auto sqe = ring_.prepareAsyncCancel(operation.userData);
    if (sqe) {
        sqe->user_data = UserDataIgnore;
        return { sqe->user_data };
    }
    return {};
}

void IoQueue::run()
{
    while (numOpsQueued_ > 0) {
        const auto res = ring_.submitSqes(1);
        if (res < 0) {
            getLogger().log(LogSeverity::Error, "Error submitting SQEs: " + errnoToString(errno));
            continue;
        }
        const auto cqe = ring_.peekCqe();
        if (!cqe) {
            continue;
        }

        // It's possible the pointer tagging and the two special cases for coroutines and callbacks
        // are absolutely not necessary, but it's right here that I think they might be, because
        // they would not be inlined if they were virtual function calls. At the same time we will
        // call a function (type-erased) directly after, so maybe it makes no difference after all.
        // I will keep it until it bothers me too much and I find out for a fact that it makes no
        // difference.
        if (cqe->user_data != UserDataIgnore) {
            const auto [ptr, tags] = untagPointer(cqe->user_data);
            if (tags.type == PointerTags::Type::Coroutine) {
                const auto completer = reinterpret_cast<CoroutineCompleter*>(ptr);
                if (completer->awaiter) {
                    completer->awaiter->complete(IoResult(cqe->res));
                }
                delete completer;
            } else if (tags.type == PointerTags::Type::Callback) {
                const auto completer = reinterpret_cast<CallbackCompleter*>(ptr);
                if (completer->handler) {
                    completer->handler(IoResult(cqe->res));
                }
                delete completer;
            } else if (tags.type == PointerTags::Type::Generic) {
                const auto completer = reinterpret_cast<GenericCompleter*>(ptr);
                completer->complete(IoResult(cqe->res));
                delete completer;
            }
            numOpsQueued_--;
        }
        ring_.advanceCq();
    }
}

IoQueue::OperationHandle IoQueue::addSqe(io_uring_sqe* sqe, CompletionHandler cb)
{
    if (!sqe) {
        getLogger().log(LogSeverity::Warning, "io_uring full");
        return {};
    }
    numOpsQueued_++;
    sqe->user_data = tagPointer(
        new CallbackCompleter { std::move(cb) }, { .type = PointerTags::Type::Callback });
    return { sqe->user_data };
}

IoQueue::OperationHandle IoQueue::addSqe(io_uring_sqe* sqe, AwaiterBase* awaiter)
{
    if (!sqe) {
        getLogger().log(LogSeverity::Warning, "io_uring full");
        return {};
    }
    numOpsQueued_++;
    sqe->user_data
        = tagPointer(new CoroutineCompleter { awaiter }, { .type = PointerTags::Type::Coroutine });
    return { sqe->user_data };
}

IoQueue::OperationHandle IoQueue::addSqe(io_uring_sqe* sqe, GenericCompleter* completer)
{
    if (!sqe) {
        getLogger().log(LogSeverity::Warning, "io_uring full");
        return {};
    }
    numOpsQueued_++;
    sqe->user_data = tagPointer(completer, { .type = PointerTags::Type::Generic });
    return { sqe->user_data };
}

IoQueue::OperationHandle IoQueue::addSqe(
    io_uring_sqe* sqe, Timespec* timeout, bool timeoutIsAbsolute, CompletionHandler cb)
{
    if (!addSqe(sqe, std::move(cb))) {
        return {};
    }
    sqe->flags |= IOSQE_IO_LINK;
    // If the timeout does not fit into the SQ, that's fine. We don't want to undo the whole thing.
    // In the future the use of timeouts might be more critical and this should be reconsidered.
    auto timeoutSqe = ring_.prepareLinkTimeout(timeout, timeoutIsAbsolute ? IORING_TIMEOUT_ABS : 0);
    if (timeoutSqe) {
        timeoutSqe->user_data = UserDataIgnore;
    }
    return { sqe->user_data };
}
}
