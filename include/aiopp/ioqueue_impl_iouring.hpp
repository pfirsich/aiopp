#pragma once

#include "completermap.hpp"
#include "ioqueue.hpp"
#include "iouring.hpp"

namespace aiopp {
// These 'using's are only fine, because this is not a public header!
using Duration = IoQueue::Duration;
using OperationHandle = IoQueue::OperationHandle;
using TimePoint = IoQueue::TimePoint;
using Completer = IoQueue::Completer;

using Timespec = IoURing::Timespec;
void setTimespec(Timespec& ts, Duration duration);
void setTimespec(Timespec& ts, TimePoint tp);

struct IoQueueImpl {
    IoQueue* parent_;
    IoURing ring_;
    CompleterMap completers_;
    io_uring_sqe* lastSqe_ = nullptr;

    IoQueueImpl(IoQueue* parent, size_t size);
    bool init(bool submissionQueuePolling = false);

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

    OperationHandle timeout(Timespec* ts, uint32_t flags);
    Task<IoResult> timeout(Duration dur);
    Task<IoResult> timeout(TimePoint tp);

    // The passed operation will return ECANCELED in case the timeout expired.
    OperationHandle linkTimeout(Timespec* ts, int flags, OperationHandle op);
    Task<IoResult> timeout(Timespec* ts, int flags, OperationHandle op);
    Task<IoResult> timeout(Duration dur, OperationHandle op);
    Task<IoResult> timeout(TimePoint tp, OperationHandle op);

    OperationHandle cancel(OperationHandle operation, bool cancelHandler);

    void run();

    OperationHandle finalizeSqe(io_uring_sqe* sqe, uint64_t userData);
    OperationHandle finalizeSqe(io_uring_sqe* sqe);
    void setCompleter(OperationHandle operation, std::unique_ptr<Completer> completer);
};
}