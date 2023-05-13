#include "aiopp/ioqueue.hpp"

#include <time.h>

#include <sys/eventfd.h>
#include <unistd.h>

#include "aiopp/log.hpp"
#include "aiopp/util.hpp"

namespace aiopp {
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
    return setUserData(ring_.prepareAccept(fd, reinterpret_cast<sockaddr*>(addr), addrlen));
}

IoQueue::OperationHandle IoQueue::connect(int sockfd, const ::sockaddr* addr, socklen_t addrlen)
{
    return setUserData(ring_.prepareConnect(sockfd, addr, addrlen));
}

IoQueue::OperationHandle IoQueue::send(int sockfd, const void* buf, size_t len)
{
    return setUserData(ring_.prepareSend(sockfd, buf, len));
}

IoQueue::OperationHandle IoQueue::recv(int sockfd, void* buf, size_t len)
{
    return setUserData(ring_.prepareRecv(sockfd, buf, len));
}

IoQueue::OperationHandle IoQueue::read(int fd, void* buf, size_t count)
{
    return setUserData(ring_.prepareRead(fd, buf, count));
}

IoQueue::OperationHandle IoQueue::close(int fd)
{
    return setUserData(ring_.prepareClose(fd));
}

IoQueue::OperationHandle IoQueue::shutdown(int fd, int how)
{
    return setUserData(ring_.prepareShutdown(fd, how));
}

IoQueue::OperationHandle IoQueue::poll(int fd, short events)
{
    return setUserData(ring_.preparePollAdd(fd, events));
}

IoQueue::OperationHandle IoQueue::recvmsg(int sockfd, ::msghdr* msg, int flags)
{
    return setUserData(ring_.prepareRecvmsg(sockfd, msg, flags));
}

IoQueue::OperationHandle IoQueue::sendmsg(int sockfd, const ::msghdr* msg, int flags)
{
    return setUserData(ring_.prepareSendmsg(sockfd, msg, flags));
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

IoQueue::OperationHandle IoQueue::timeout(Timespec* ts, uint64_t count, uint32_t flags)
{
    return setUserData(ring_.prepareTimeout(ts, count, flags));
}

Task<void> IoQueue::timeout(Duration dur)
{
    auto ts = toTimespec(dur);
    co_await timeout(&ts, 0, 0);
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
        const auto ptr = completers_.remove(operation.userData);
        const auto completer = reinterpret_cast<Completer*>(ptr);
        delete completer;
    }

    auto sqe = ring_.prepareAsyncCancel(operation.userData);
    if (!sqe) {
        return {};
    }
    sqe->user_data = UserDataIgnore;
    return { this, sqe->user_data };
}

void IoQueue::run()
{
    while (completers_.size() > 0) {
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

IoQueue::OperationHandle IoQueue::setUserData(io_uring_sqe* sqe)
{
    // Just skip the magic values
    if (nextUserData_ == UserDataInvalid || nextUserData_ == UserDataIgnore) {
        nextUserData_ = 0;
    }
    const auto userData = nextUserData_;
    nextUserData_++;

    if (!sqe) {
        getLogger().log(LogSeverity::Warning, "io_uring full");
        return {};
    }
    sqe->user_data = userData;
    return { this, sqe->user_data };
}
}
