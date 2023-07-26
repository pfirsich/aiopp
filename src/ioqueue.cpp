#include "aiopp/ioqueue.hpp"

#include <time.h>

#include <sys/eventfd.h>
#include <unistd.h>

#include "aiopp/log.hpp"
#include "aiopp/util.hpp"

namespace aiopp {
void IoQueue::setTimespec(Timespec& ts, std::chrono::milliseconds ms)
{
    ts.tv_sec = ms.count() / 1000;
    ts.tv_nsec = (ms.count() % 1000) * 1000 * 1000;
}

void IoQueue::setTimespec(Timespec& ts, std::chrono::time_point<std::chrono::steady_clock> tp)
{
    const auto now = std::chrono::steady_clock::now();
    assert(now >= tp);
    const auto delta = tp - now;
    const auto deltaMs = std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();

    ::timespec nowTs;
    ::clock_gettime(CLOCK_MONOTONIC, &nowTs);

    ts.tv_sec = nowTs.tv_sec + deltaMs / 1000;
    ts.tv_nsec = nowTs.tv_nsec + (deltaMs % 1000) * 1000 * 1000;
    ts.tv_sec += ts.tv_nsec / (1000 * 1000 * 1000);
    ts.tv_nsec = ts.tv_nsec % (1000 * 1000 * 1000);
}

IoQueue::Duration::Duration(std::chrono::milliseconds ms)
{
    set(ms);
}

void IoQueue::Duration::set(std::chrono::milliseconds ms)
{
    setTimespec(*this, ms);
}

IoQueue::TimePoint::TimePoint(std::chrono::time_point<std::chrono::steady_clock> tp)
{
    set(tp);
}

void IoQueue::TimePoint::set(std::chrono::time_point<std::chrono::steady_clock> tp)
{
    setTimespec(*this, tp);
}

IoQueue::IoQueue(size_t size, bool submissionQueuePolling)
    : completers_(size)
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

IoQueue::OperationHandle IoQueue::accept(int fd, ::sockaddr_in* addr, socklen_t* addrlen)
{
    return finalizeSqe(ring_.prepareAccept(fd, reinterpret_cast<sockaddr*>(addr), addrlen));
}

IoQueue::OperationHandle IoQueue::connect(int sockfd, const ::sockaddr* addr, socklen_t addrlen)
{
    return finalizeSqe(ring_.prepareConnect(sockfd, addr, addrlen));
}

IoQueue::OperationHandle IoQueue::send(int sockfd, const void* buf, size_t len)
{
    return finalizeSqe(ring_.prepareSend(sockfd, buf, len));
}

IoQueue::OperationHandle IoQueue::recv(int sockfd, void* buf, size_t len)
{
    return finalizeSqe(ring_.prepareRecv(sockfd, buf, len));
}

IoQueue::OperationHandle IoQueue::read(int fd, void* buf, size_t count)
{
    return finalizeSqe(ring_.prepareRead(fd, buf, count));
}

IoQueue::OperationHandle IoQueue::close(int fd)
{
    return finalizeSqe(ring_.prepareClose(fd));
}

IoQueue::OperationHandle IoQueue::shutdown(int fd, int how)
{
    return finalizeSqe(ring_.prepareShutdown(fd, how));
}

IoQueue::OperationHandle IoQueue::poll(int fd, short events)
{
    return finalizeSqe(ring_.preparePollAdd(fd, events));
}

IoQueue::OperationHandle IoQueue::recvmsg(int sockfd, ::msghdr* msg, int flags)
{
    return finalizeSqe(ring_.prepareRecvmsg(sockfd, msg, flags));
}

IoQueue::OperationHandle IoQueue::sendmsg(int sockfd, const ::msghdr* msg, int flags)
{
    return finalizeSqe(ring_.prepareSendmsg(sockfd, msg, flags));
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

Task<IoResult> IoQueue::recvfrom(
    int sockfd, void* buf, size_t len, int flags, ::sockaddr* srcAddr, socklen_t addrLen)
{
    MsgHdr msgHdr(buf, len, srcAddr, addrLen);
    co_return co_await recvmsg(sockfd, &msgHdr.msg, flags);
}

Task<IoResult> IoQueue::sendto(int sockfd, const void* buf, size_t len, int flags,
    const ::sockaddr* destAddr, socklen_t addrLen)
{
    MsgHdr msgHdr(const_cast<void*>(buf), len, const_cast<::sockaddr*>(destAddr), addrLen);
    co_return co_await sendmsg(sockfd, &msgHdr.msg, flags);
}

IoQueue::OperationHandle IoQueue::timeout(Timespec* ts, uint32_t flags)
{
    return finalizeSqe(ring_.prepareTimeout(ts, 0, flags));
}

Task<IoResult> IoQueue::timeout(Duration dur)
{
    co_return co_await timeout(&dur, 0);
}

Task<IoResult> IoQueue::timeout(TimePoint tp)
{
    co_return co_await timeout(&tp, IORING_TIMEOUT_ABS);
}

IoQueue::OperationHandle IoQueue::linkTimeout(Timespec* ts, int flags, OperationHandle op)
{
    assert(op);
    assert(lastSqe_ && lastSqe_->user_data == op.userData);
    lastSqe_->flags |= IOSQE_IO_LINK;
    if (!finalizeSqe(ring_.prepareLinkTimeout(ts, flags), UserDataIgnore)) {
        return {};
    }
    return op;
}

Task<IoResult> IoQueue::timeout(Timespec* ts, int flags, OperationHandle op)
{
    // TODO: Make this function check if op is lastSqe_ and if not, issue a regular timeout
    // operation that does cancel(op, false) when it completes with ETIME.
    // I think I need to await multiple operations (op and timeout) simultaneously for this, which
    // is why I put it off for now.
    co_return co_await linkTimeout(ts, flags, op);
}

Task<IoResult> IoQueue::timeout(Duration dur, OperationHandle op)
{
    co_return co_await timeout(&dur, 0, op);
}

Task<IoResult> IoQueue::timeout(TimePoint tp, OperationHandle op)
{
    co_return co_await timeout(&tp, IORING_TIMEOUT_ABS, op);
}

IoQueue::OperationHandle IoQueue::cancel(OperationHandle operation, bool cancelHandler)
{
    assert(operation);

    if (cancelHandler) {
        const auto ptr = completers_.remove(operation.userData);
        const auto completer = reinterpret_cast<Completer*>(ptr);
        delete completer;
    }

    return finalizeSqe(ring_.prepareAsyncCancel(operation.userData), UserDataIgnore);
}

void IoQueue::run()
{
    while (completers_.size() > 0) {
        lastSqe_ = nullptr;
        const auto res = ring_.submitSqes(1);
        if (res < 0) {
            getLogger().log(LogSeverity::Error, "Error submitting SQEs: " + errnoToString(errno));
            continue;
        }
        const auto cqe = ring_.peekCqe();
        if (!cqe) {
            continue;
        }

        if (cqe->user_data != UserDataIgnore) {
            const auto ptr = completers_.remove(cqe->user_data);
            if (ptr) {
                const auto completer = reinterpret_cast<Completer*>(ptr);
                completer->complete(cqe->res);
                delete completer;
            }
        }
        ring_.advanceCq();
    }
}

void IoQueue::setCompleter(OperationHandle operation, std::unique_ptr<Completer> completer)
{
    completers_.insert(operation.userData, completer.release());
}

IoQueue::OperationHandle IoQueue::finalizeSqe(io_uring_sqe* sqe, uint64_t userData)
{
    if (!sqe) {
        getLogger().log(LogSeverity::Warning, "io_uring full");
        return {};
    }
    sqe->user_data = userData;
    lastSqe_ = sqe;
    return { this, sqe->user_data };
}

IoQueue::OperationHandle IoQueue::finalizeSqe(io_uring_sqe* sqe)
{
    // Just skip the magic values
    if (nextUserData_ == UserDataInvalid || nextUserData_ == UserDataIgnore) {
        nextUserData_ = 0;
    }
    const auto userData = nextUserData_;
    nextUserData_++;
    return finalizeSqe(sqe, userData);
}
}
