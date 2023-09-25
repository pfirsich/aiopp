#include "aiopp/ioqueue_impl_iouring.hpp"

#include "aiopp/util.hpp"

namespace aiopp {
void setTimespec(Timespec& ts, std::chrono::milliseconds ms)
{
    ts.tv_sec = ms.count() / 1000;
    ts.tv_nsec = (ms.count() % 1000) * 1000 * 1000;
}

void setTimespec(Timespec& ts, std::chrono::time_point<std::chrono::steady_clock> tp)
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

IoQueueImpl::IoQueueImpl(IoQueue* parent, size_t size)
    : parent_(parent)
    , completers_(size)
{
}

bool IoQueueImpl::init(bool submissionQueuePolling)
{
    if (!ring_.init(completers_.capacity(), submissionQueuePolling)) {
        getLogger().log(LogSeverity::Fatal, "Could not create io_uring: " + errnoToString(errno));
        return false;
    }
    if (!(ring_.getParams().features & IORING_FEAT_NODROP)) {
        getLogger().log(LogSeverity::Fatal, "io_uring does not support NODROP");
        return false;
    }
    if (!(ring_.getParams().features & IORING_FEAT_SUBMIT_STABLE)) {
        getLogger().log(LogSeverity::Fatal, "io_uring does not support SUBMIT_STABLE");
        return false;
    }
    return true;
}

size_t IoQueueImpl::getSize() const
{
    return ring_.getNumSqeEntries();
}

size_t IoQueueImpl::getCapacity() const
{
    return ring_.getSqeCapacity();
}

OperationHandle IoQueueImpl::accept(int fd, ::sockaddr_in* addr, socklen_t* addrlen)
{
    return finalizeSqe(ring_.prepareAccept(fd, reinterpret_cast<sockaddr*>(addr), addrlen));
}

OperationHandle IoQueueImpl::connect(int sockfd, const ::sockaddr* addr, socklen_t addrlen)
{
    return finalizeSqe(ring_.prepareConnect(sockfd, addr, addrlen));
}

OperationHandle IoQueueImpl::send(int sockfd, const void* buf, size_t len)
{
    return finalizeSqe(ring_.prepareSend(sockfd, buf, len));
}

OperationHandle IoQueueImpl::recv(int sockfd, void* buf, size_t len)
{
    return finalizeSqe(ring_.prepareRecv(sockfd, buf, len));
}

OperationHandle IoQueueImpl::read(int fd, void* buf, size_t count)
{
    return finalizeSqe(ring_.prepareRead(fd, buf, count));
}

OperationHandle IoQueueImpl::close(int fd)
{
    return finalizeSqe(ring_.prepareClose(fd));
}

OperationHandle IoQueueImpl::shutdown(int fd, int how)
{
    return finalizeSqe(ring_.prepareShutdown(fd, how));
}

OperationHandle IoQueueImpl::poll(int fd, short events)
{
    return finalizeSqe(ring_.preparePollAdd(fd, events));
}

OperationHandle IoQueueImpl::recvmsg(int sockfd, ::msghdr* msg, int flags)
{
    return finalizeSqe(ring_.prepareRecvmsg(sockfd, msg, flags));
}

OperationHandle IoQueueImpl::sendmsg(int sockfd, const ::msghdr* msg, int flags)
{
    return finalizeSqe(ring_.prepareSendmsg(sockfd, msg, flags));
}

OperationHandle IoQueueImpl::timeout(Timespec* ts, uint32_t flags)
{
    return finalizeSqe(ring_.prepareTimeout(ts, 0, flags));
}

Task<IoResult> IoQueueImpl::timeout(Duration dur)
{
    Timespec ts;
    setTimespec(ts, dur);
    co_return co_await timeout(&ts, 0);
}

Task<IoResult> IoQueueImpl::timeout(TimePoint tp)
{
    Timespec ts;
    setTimespec(ts, tp);
    co_return co_await timeout(&ts, IORING_TIMEOUT_ABS);
}

OperationHandle IoQueueImpl::linkTimeout(Timespec* ts, int flags, OperationHandle op)
{
    assert(op);
    assert(lastSqe_ && lastSqe_->user_data == op.id);
    lastSqe_->flags |= IOSQE_IO_LINK;
    if (!finalizeSqe(ring_.prepareLinkTimeout(ts, flags), IoQueue::OpIdIgnore)) {
        return {};
    }
    return op;
}

Task<IoResult> IoQueueImpl::timeout(Timespec* ts, int flags, OperationHandle op)
{
    // TODO: Make this function check if op is lastSqe_ and if not, issue a regular timeout
    // operation that does cancel(op, false) when it completes with ETIME.
    // I think I need to await multiple operations (op and timeout) simultaneously for this, which
    // is why I put it off for now.
    co_return co_await linkTimeout(ts, flags, op);
}

Task<IoResult> IoQueueImpl::timeout(Duration dur, OperationHandle op)
{
    Timespec ts;
    setTimespec(ts, dur);
    co_return co_await timeout(&ts, 0, op);
}

Task<IoResult> IoQueueImpl::timeout(TimePoint tp, OperationHandle op)
{
    Timespec ts;
    setTimespec(ts, tp);
    co_return co_await timeout(&ts, IORING_TIMEOUT_ABS, op);
}

OperationHandle IoQueueImpl::cancel(OperationHandle operation, bool cancelHandler)
{
    assert(operation);

    if (cancelHandler) {
        const auto ptr = completers_.remove(operation.id);
        const auto completer = reinterpret_cast<Completer*>(ptr);
        delete completer;
    }

    return finalizeSqe(ring_.prepareAsyncCancel(operation.id), IoQueue::OpIdIgnore);
}

void IoQueueImpl::run()
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

        if (cqe->user_data != IoQueue::OpIdIgnore) {
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

OperationHandle IoQueueImpl::finalizeSqe(io_uring_sqe* sqe, uint64_t userData)
{
    if (!sqe) {
        getLogger().log(LogSeverity::Warning, "io_uring full");
        return {};
    }
    sqe->user_data = userData;
    lastSqe_ = sqe;
    return { parent_, sqe->user_data };
}

OperationHandle IoQueueImpl::finalizeSqe(io_uring_sqe* sqe)
{
    return finalizeSqe(sqe, parent_->getNextOpId());
}

void IoQueueImpl::setCompleter(OperationHandle operation, std::unique_ptr<Completer> completer)
{
    completers_.insert(operation.id, completer.release());
}
}
