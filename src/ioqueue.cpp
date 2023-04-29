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

IoQueue::OperationHandle IoQueue::sendmsg(
    int sockfd, const ::msghdr* msg, int flags, CompletionHandler cb)
{
    return addSqe(ring_.prepareSendmsg(sockfd, msg, flags), std::move(cb));
}

IoQueue::OperationHandle IoQueue::cancel(OperationHandle operation, bool cancelHandler)
{
    if (cancelHandler) {
        reinterpret_cast<CallbackCompleter*>(operation.userData)->handler = nullptr;
    }
    auto sqe = ring_.prepareAsyncCancel(operation.userData);
    if (sqe) {
        sqe->user_data = UserDataIgnore;
        return { sqe->user_data };
    }
    return {};
}

namespace {
    struct MsgContext {
        ::msghdr msg;
        ::iovec iov;

        static std::unique_ptr<MsgContext> makeContext(
            void* buf, size_t len, ::sockaddr* addr, socklen_t addrLen)
        {
            auto context = std::make_unique<MsgContext>(MsgContext {
                ::msghdr {
                    .msg_name = addr,
                    .msg_namelen = addrLen,
                    .msg_iovlen = 1,
                    .msg_controllen = 0,
                    .msg_flags = 0,
                },
                ::iovec { buf, len },
            });
            context->msg.msg_iov = &context->iov;
            return context;
        }
    };
}

IoQueue::OperationHandle IoQueue::recvfrom(int sockfd, void* buf, size_t len, int flags,
    ::sockaddr* srcAddr, socklen_t addrLen, CompletionHandler cb)
{
    auto context = MsgContext::makeContext(buf, len, srcAddr, addrLen);
    return recvmsg(sockfd, &context->msg, flags,
        [context = std::move(context), cb = std::move(cb)](IoResult res) { cb(res); });
}

IoQueue::OperationHandle IoQueue::sendto(int sockfd, const void* buf, size_t len, int flags,
    const ::sockaddr* destAddr, socklen_t addrLen, CompletionHandler cb)
{
    auto context = MsgContext::makeContext(
        const_cast<void*>(buf), len, const_cast<::sockaddr*>(destAddr), addrLen);
    return sendmsg(sockfd, &context->msg, flags,
        [context = std::move(context), cb = std::move(cb)](IoResult res) { cb(res); });
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

        if (cqe->user_data != UserDataIgnore) {
            const auto completer = reinterpret_cast<CallbackCompleter*>(cqe->user_data);
            if (completer->handler) {
                completer->handler(IoResult(cqe->res));
            }
            delete completer;
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
    sqe->user_data = reinterpret_cast<uint64_t>(new CallbackCompleter { std::move(cb) });
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
