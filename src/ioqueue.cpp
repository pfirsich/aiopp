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
    : completionHandlers_(size)
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

bool IoQueue::accept(int fd, sockaddr_in* addr, socklen_t* addrlen, HandlerEcRes cb)
{
    return addSqe(
        ring_.prepareAccept(fd, reinterpret_cast<sockaddr*>(addr), addrlen), std::move(cb));
}

bool IoQueue::connect(int sockfd, const ::sockaddr* addr, socklen_t addrlen, HandlerEc cb)
{
    return addSqe(ring_.prepareConnect(sockfd, addr, addrlen), std::move(cb));
}

bool IoQueue::send(int sockfd, const void* buf, size_t len, HandlerEcRes cb)
{
    return addSqe(ring_.prepareSend(sockfd, buf, len), std::move(cb));
}

bool IoQueue::send(int sockfd, const void* buf, size_t len, IoQueue::Timespec* timeout,
    bool timeoutIsAbsolute, HandlerEcRes cb)
{
    if (!timeout) {
        return send(sockfd, buf, len, std::move(cb));
    }
    return addSqe(ring_.prepareSend(sockfd, buf, len), timeout, timeoutIsAbsolute, std::move(cb));
}

bool IoQueue::recv(int sockfd, void* buf, size_t len, HandlerEcRes cb)
{
    return addSqe(ring_.prepareRecv(sockfd, buf, len), std::move(cb));
}

bool IoQueue::recv(int sockfd, void* buf, size_t len, IoQueue::Timespec* timeout,
    bool timeoutIsAbsolute, HandlerEcRes cb)
{
    if (!timeout) {
        return recv(sockfd, buf, len, std::move(cb));
    }
    return addSqe(ring_.prepareRecv(sockfd, buf, len), timeout, timeoutIsAbsolute, std::move(cb));
}

bool IoQueue::read(int fd, void* buf, size_t count, HandlerEcRes cb)
{
    return addSqe(ring_.prepareRead(fd, buf, count), std::move(cb));
}

bool IoQueue::close(int fd, HandlerEc cb)
{
    return addSqe(ring_.prepareClose(fd), std::move(cb));
}

bool IoQueue::shutdown(int fd, int how, HandlerEc cb)
{
    return addSqe(ring_.prepareShutdown(fd, how), std::move(cb));
}

bool IoQueue::poll(int fd, short events, HandlerEcRes cb)
{
    return addSqe(ring_.preparePollAdd(fd, events), std::move(cb));
}

bool IoQueue::recvmsg(int sockfd, ::msghdr* msg, int flags, HandlerEcRes cb)
{
    return addSqe(ring_.prepareRecvmsg(sockfd, msg, flags), std::move(cb));
}

bool IoQueue::sendmsg(int sockfd, const ::msghdr* msg, int flags, HandlerEcRes cb)
{
    return addSqe(ring_.prepareSendmsg(sockfd, msg, flags), std::move(cb));
}

IoQueue::NotifyHandle::NotifyHandle(std::shared_ptr<EventFd> eventFd)
    : eventFd_(std::move(eventFd))
{
}

IoQueue::NotifyHandle::operator bool() const
{
    return eventFd_ != nullptr;
}

void IoQueue::NotifyHandle::notify(uint64_t value)
{
    assert(eventFd_);
    eventFd_->write(value);
    eventFd_.reset();
}

IoQueue::NotifyHandle IoQueue::wait(Function<void(std::error_code, uint64_t)> cb)
{
    auto eventFd = std::make_shared<EventFd>(*this);
    const auto res
        = eventFd->read([eventFd, cb = std::move(cb)](std::error_code ec, uint64_t value) {
              if (ec) {
                  cb(ec, 0);
              } else {
                  cb(std::error_code(), value);
              }
          });
    if (res) {
        return NotifyHandle { std::move(eventFd) };
    } else {
        return NotifyHandle { nullptr };
    }
}

void IoQueue::run()
{
    while (completionHandlers_.size() > 0) {
        const auto res = ring_.submitSqes(1);
        if (res < 0) {
            getLogger().log(LogSeverity::Error, "Error submitting SQEs: " + errnoToString(errno));
            continue;
        }
        const auto cqe = ring_.peekCqe();
        if (!cqe) {
            continue;
        }

        if (cqe->user_data != Ignore) {
            assert(completionHandlers_.contains(cqe->user_data));
            metrics_.ioOperationsQueued--;
            auto ch = std::move(completionHandlers_[cqe->user_data]);
            ch(cqe);
            completionHandlers_.remove(cqe->user_data);
        }
        ring_.advanceCq();
    }
}

size_t IoQueue::addHandler(HandlerEc&& cb)
{
    return completionHandlers_.emplace([cb = std::move(cb)](const io_uring_cqe* cqe) {
        if (cqe->res < 0) {
            cb(std::make_error_code(static_cast<std::errc>(-cqe->res)));
        } else {
            cb(std::error_code());
        }
    });
}

size_t IoQueue::addHandler(HandlerEcRes&& cb)
{
    return completionHandlers_.emplace([cb = std::move(cb)](const io_uring_cqe* cqe) {
        if (cqe->res < 0) {
            cb(std::make_error_code(static_cast<std::errc>(-cqe->res)), -1);
        } else {
            cb(std::error_code(), cqe->res);
        }
    });
}

template <typename Callback>
bool IoQueue::addSqe(io_uring_sqe* sqe, Callback cb)
{
    if (!sqe) {
        getLogger().log(LogSeverity::Warning, "io_uring full");
        return false;
    }
    metrics_.ioOperationsQueued++;
    sqe->user_data = addHandler(std::move(cb));
    return true;
}

template bool IoQueue::addSqe<IoQueue::HandlerEc>(io_uring_sqe* sqe, IoQueue::HandlerEc cb);
template bool IoQueue::addSqe<IoQueue::HandlerEcRes>(io_uring_sqe* sqe, IoQueue::HandlerEcRes cb);

template <typename Callback>
bool IoQueue::addSqe(io_uring_sqe* sqe, Timespec* timeout, bool timeoutIsAbsolute, Callback cb)
{
    if (!addSqe(sqe, std::move(cb))) {
        return false;
    }
    sqe->flags |= IOSQE_IO_LINK;
    // If the timeout does not fit into the SQ, that's fine. We don't want to undo the whole thing.
    // In the future the use of timeouts might be more critical and this should be reconsidered.
    auto timeoutSqe = ring_.prepareLinkTimeout(timeout, timeoutIsAbsolute ? IORING_TIMEOUT_ABS : 0);
    timeoutSqe->user_data = Ignore;
    return true;
}

template bool IoQueue::addSqe<IoQueue::HandlerEc>(
    io_uring_sqe* sqe, Timespec* timeout, bool timeoutIsAbsolute, IoQueue::HandlerEc cb);
template bool IoQueue::addSqe<IoQueue::HandlerEcRes>(
    io_uring_sqe* sqe, Timespec* timeout, bool timeoutIsAbsolute, IoQueue::HandlerEcRes cb);
}
