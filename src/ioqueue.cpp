#include "aiopp/ioqueue.hpp"

#include <cstring>
#include <ctime>

#include <sys/eventfd.h>
#include <unistd.h>

#include "aiopp/log.hpp"
#include "aiopp/util.hpp"

#ifdef AIOPP_IOQUEUE_BACKEND_IOURING
#include "aiopp/ioqueue_impl_iouring.hpp"
#endif

namespace aiopp {
IoQueue::IoQueue(size_t size)
    : impl_(std::make_unique<IoQueueImpl>(this, size))
{
    if (!impl_->init()) {
        // Should have logged in init()
        std::exit(1);
    }
}

IoQueue::~IoQueue() = default;

size_t IoQueue::getSize() const
{
    return impl_->getSize();
}

size_t IoQueue::getCapacity() const
{
    return impl_->getCapacity();
}

IoQueue::OperationHandle IoQueue::accept(int fd, ::sockaddr_in* addr, socklen_t* addrlen)
{
    return impl_->accept(fd, addr, addrlen);
}

IoQueue::OperationHandle IoQueue::connect(int sockfd, const ::sockaddr* addr, socklen_t addrlen)
{
    return impl_->connect(sockfd, addr, addrlen);
}

IoQueue::OperationHandle IoQueue::connect(int sockfd, const ::sockaddr_in* addr)
{
    return connect(sockfd, reinterpret_cast<const sockaddr*>(addr), sizeof(::sockaddr_in));
}

Task<IoResult> IoQueue::connect(int sockfd, IpAddressPort addr)
{
    ::sockaddr_in sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = addr.address.ipv4;
    sa.sin_port = htons(addr.port);
    co_return co_await connect(sockfd, &sa);
}

IoQueue::OperationHandle IoQueue::send(int sockfd, const void* buf, size_t len)
{
    return impl_->send(sockfd, buf, len);
}

IoQueue::OperationHandle IoQueue::recv(int sockfd, void* buf, size_t len)
{
    return impl_->recv(sockfd, buf, len);
}

IoQueue::OperationHandle IoQueue::read(int fd, void* buf, size_t count)
{
    return impl_->read(fd, buf, count);
}

IoQueue::OperationHandle IoQueue::close(int fd)
{
    return impl_->close(fd);
}

IoQueue::OperationHandle IoQueue::shutdown(int fd, int how)
{
    return impl_->shutdown(fd, how);
}

IoQueue::OperationHandle IoQueue::poll(int fd, short events)
{
    return impl_->poll(fd, events);
}

IoQueue::OperationHandle IoQueue::recvmsg(int sockfd, ::msghdr* msg, int flags)
{
    return impl_->recvmsg(sockfd, msg, flags);
}

IoQueue::OperationHandle IoQueue::sendmsg(int sockfd, const ::msghdr* msg, int flags)
{
    return impl_->sendmsg(sockfd, msg, flags);
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
                .msg_control = nullptr,
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

Task<IoResult> IoQueue::timeout(Duration dur)
{
    return impl_->timeout(dur);
}
Task<IoResult> IoQueue::timeout(TimePoint tp)
{
    return impl_->timeout(tp);
}

Task<IoResult> IoQueue::timeout(Duration dur, OperationHandle op)
{
    return impl_->timeout(dur, op);
}
Task<IoResult> IoQueue::timeout(TimePoint tp, OperationHandle op)
{
    return impl_->timeout(tp, op);
}

IoQueue::OperationHandle IoQueue::cancel(OperationHandle operation, bool cancelHandler)
{
    return impl_->cancel(operation, cancelHandler);
}

void IoQueue::run()
{
    return impl_->run();
}

void IoQueue::setCompleter(OperationHandle operation, std::unique_ptr<Completer> completer)
{
    return impl_->setCompleter(operation, std::move(completer));
}

IoQueue::OperationId IoQueue::getNextOpId()
{
    // Just skip the magic values
    if (nextOpId_ == OpIdInvalid || nextOpId_ == OpIdIgnore) {
        nextOpId_ = 0;
    }
    const auto opId = nextOpId_;
    nextOpId_++;
    return opId;
}
}
