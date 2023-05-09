#include "aiopp/iouring.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>

#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace aiopp {
IoURing::~IoURing()
{
    io_uring_queue_exit(&ring_);
}

bool IoURing::init(size_t sqEntries, bool sqPoll)
{
    ring_.ring_fd = -1;
    std::memset(&params_, 0, sizeof(params_));
    if (sqPoll) {
        params_.flags = IORING_SETUP_SQPOLL;
    }
    const auto res = io_uring_queue_init_params(sqEntries, &ring_, &params_);
    if (res < 0) {
        errno = -res;
        std::perror("io_uring_queue_init");
        return false;
    }
    return true;
}

bool IoURing::isInitialized() const
{
    return ring_.ring_fd != -1;
}

const io_uring_params& IoURing::getParams() const
{
    return params_;
}

io_uring_cqe* IoURing::peekCqe()
{
    assert(ring_.ring_fd != -1);
    io_uring_cqe* cqe = nullptr;
    const auto res = io_uring_peek_cqe(&ring_, &cqe);
    if (res < 0) {
        return nullptr;
    }
    return cqe;
}

io_uring_cqe* IoURing::waitCqe(size_t num)
{
    assert(ring_.ring_fd != -1);
    io_uring_cqe* cqe = nullptr;
    const auto res = io_uring_wait_cqe_nr(&ring_, &cqe, num);
    if (res < 0) {
        return nullptr;
    }
    return cqe;
}

void IoURing::advanceCq(size_t num)
{
    assert(ring_.ring_fd != -1);
    io_uring_cq_advance(&ring_, num);
}

size_t IoURing::getNumSqeEntries() const
{
    return params_.sq_entries;
}

size_t IoURing::getSqeCapacity() const
{
    return params_.sq_entries - io_uring_sq_ready(&ring_);
}

io_uring_sqe* IoURing::getSqe()
{
    assert(ring_.ring_fd != -1);
    return io_uring_get_sqe(&ring_);
}

int IoURing::submitSqes(size_t waitCqes)
{
    assert(ring_.ring_fd != -1);
    return io_uring_submit_and_wait(&ring_, waitCqes);
}

io_uring_sqe* IoURing::prepare(uint8_t opcode, int fd, uint64_t off, const void* addr, uint32_t len)
{
    auto sqe = getSqe();
    if (!sqe) {
        return nullptr;
    }
    sqe->opcode = opcode;
    sqe->flags = 0;
    sqe->ioprio = 0;
    sqe->fd = fd;
    sqe->off = off;
    sqe->addr = reinterpret_cast<uint64_t>(addr);
    sqe->len = len;
    sqe->rw_flags = 0; // Init some union field with 0
    sqe->user_data = 0;
    sqe->buf_index = 0;
    sqe->personality = 0;
    sqe->splice_fd_in = 0;
    sqe->__pad2[0] = sqe->__pad2[1] = 0;
    return sqe;
}

io_uring_sqe* IoURing::prepareNop()
{
    return prepare(IORING_OP_NOP, -1, 0, nullptr, 0);
}

io_uring_sqe* IoURing::prepareReadv(int fd, const iovec* iov, int iovcnt, off_t offset)
{
    return prepare(IORING_OP_READV, fd, offset, iov, iovcnt);
}

io_uring_sqe* IoURing::prepareWritev(int fd, const iovec* iov, int iovcnt, off_t offset)
{
    return prepare(IORING_OP_WRITEV, fd, offset, iov, iovcnt);
}

io_uring_sqe* IoURing::prepareFsync(int fd, uint32_t flags)
{
    auto sqe = prepare(IORING_OP_FSYNC, fd, 0, nullptr, 0);
    if (sqe) {
        sqe->fsync_flags = flags;
    }
    return sqe;
}

io_uring_sqe* IoURing::preparePollAdd(int fd, short events, uint32_t flags)
{
    auto sqe = prepare(IORING_OP_POLL_ADD, fd, 0, nullptr, flags);
    if (sqe) {
        sqe->poll_events = static_cast<unsigned short>(events);
    }
    return sqe;
}

io_uring_sqe* IoURing::preparePollRemove(uint64_t userData)
{
    return prepare(IORING_OP_POLL_REMOVE, -1, 0, reinterpret_cast<void*>(userData), 0);
}

io_uring_sqe* IoURing::prepareSyncFileRange(
    int fd, off64_t offset, off64_t nbytes, unsigned int flags)
{
    auto sqe = prepare(IORING_OP_SYNC_FILE_RANGE, fd, offset, nullptr, nbytes);
    if (sqe) {
        sqe->sync_range_flags = flags;
    }
    return sqe;
}

io_uring_sqe* IoURing::prepareSendmsg(int sockfd, const msghdr* msg, int flags)
{
    auto sqe = prepare(IORING_OP_SENDMSG, sockfd, 0, msg, 1);
    if (sqe) {
        sqe->msg_flags = flags;
    }
    return sqe;
}

io_uring_sqe* IoURing::prepareRecvmsg(int sockfd, const msghdr* msg, int flags)
{
    auto sqe = prepare(IORING_OP_RECVMSG, sockfd, 0, msg, 1);
    if (sqe) {
        sqe->msg_flags = flags;
    }
    return sqe;
}

io_uring_sqe* IoURing::prepareTimeout(Timespec* ts, uint64_t count, uint32_t flags)
{
    auto sqe = prepare(IORING_OP_TIMEOUT, -1, count, ts, 1);
    if (sqe) {
        sqe->timeout_flags = flags;
    }
    return sqe;
}

io_uring_sqe* IoURing::prepareTimeoutRemove(uint64_t userData, uint32_t flags)
{
    auto sqe = prepare(IORING_OP_TIMEOUT_REMOVE, -1, 0, reinterpret_cast<void*>(userData), 0);
    if (sqe) {
        sqe->timeout_flags = flags;
    }
    return sqe;
}

io_uring_sqe* IoURing::prepareAccept(int sockfd, sockaddr* addr, socklen_t* addrlen, uint32_t flags)
{
    auto sqe = prepare(IORING_OP_ACCEPT, sockfd, 0, addr, 0);
    if (sqe) {
        sqe->addr2 = reinterpret_cast<uint64_t>(addrlen);
        sqe->accept_flags = flags;
    }
    return sqe;
}

io_uring_sqe* IoURing::prepareAsyncCancel(uint64_t userData)
{
    return prepare(IORING_OP_ASYNC_CANCEL, -1, 0, reinterpret_cast<void*>(userData), 0);
}

io_uring_sqe* IoURing::prepareLinkTimeout(Timespec* ts, uint32_t flags)
{
    auto sqe = prepare(IORING_OP_LINK_TIMEOUT, -1, 0, ts, 1);
    if (sqe) {
        sqe->timeout_flags = flags;
    }
    return sqe;
}

io_uring_sqe* IoURing::prepareConnect(int sockfd, const sockaddr* addr, socklen_t addrlen)
{
    return prepare(IORING_OP_CONNECT, sockfd, addrlen, addr, 0);
}

io_uring_sqe* IoURing::prepareOpenat(int dirfd, const char* pathname, int flags, mode_t mode)
{
    auto sqe = prepare(IORING_OP_OPENAT, dirfd, 0, pathname, mode);
    if (sqe) {
        sqe->open_flags = flags;
    }
    return sqe;
}

io_uring_sqe* IoURing::prepareClose(int fd)
{
    return prepare(IORING_OP_CLOSE, fd, 0, nullptr, 0);
}

io_uring_sqe* IoURing::prepareStatx(
    int dirfd, const char* pathname, int flags, unsigned int mask, struct statx* statxbuf)
{
    auto sqe
        = prepare(IORING_OP_STATX, dirfd, reinterpret_cast<uint64_t>(statxbuf), pathname, mask);
    if (sqe) {
        sqe->statx_flags = flags;
    }
    return sqe;
}

io_uring_sqe* IoURing::prepareRead(int fd, void* buf, size_t count, off_t offset)
{
    return prepare(IORING_OP_READ, fd, offset, buf, count);
}

io_uring_sqe* IoURing::prepareWrite(int fd, const void* buf, size_t count, off_t offset)
{
    return prepare(IORING_OP_WRITE, fd, offset, buf, count);
}

io_uring_sqe* IoURing::prepareSend(int sockfd, const void* buf, size_t len, int flags)
{
    auto sqe = prepare(IORING_OP_SEND, sockfd, 0, buf, len);
    if (sqe) {
        sqe->msg_flags = flags;
    }
    return sqe;
}

io_uring_sqe* IoURing::prepareRecv(int sockfd, void* buf, size_t len, int flags)
{
    auto sqe = prepare(IORING_OP_RECV, sockfd, 0, buf, len);
    if (sqe) {
        sqe->msg_flags = flags;
    }
    return sqe;
}

io_uring_sqe* IoURing::prepareOpenat2(int dirfd, const char* pathname, const open_how* how)
{
    return prepare(
        IORING_OP_OPENAT2, dirfd, reinterpret_cast<uint64_t>(how), pathname, sizeof(open_how));
}

io_uring_sqe* IoURing::prepareEpollCtl(int epfd, int op, int fd, epoll_event* event)
{
    return prepare(IORING_OP_EPOLL_CTL, epfd, reinterpret_cast<uint64_t>(event),
        reinterpret_cast<void*>(fd), op);
}

io_uring_sqe* IoURing::prepareShutdown(int fd, int how)
{
    return prepare(IORING_OP_SHUTDOWN, fd, 0, nullptr, how);
}

io_uring_sqe* IoURing::prepareRenameat(
    int olddirfd, const char* oldpath, int newdirfd, const char* newpath, int flags)
{
    auto sqe = prepare(
        IORING_OP_RENAMEAT, olddirfd, reinterpret_cast<uint64_t>(newpath), oldpath, newdirfd);
    if (sqe) {
        sqe->rename_flags = flags;
    }
    return sqe;
}

io_uring_sqe* IoURing::prepareUnlinkat(int dirfd, const char* pathname, int flags)
{
    auto sqe = prepare(IORING_OP_UNLINKAT, dirfd, 0, pathname, 0);
    if (sqe) {
        sqe->unlink_flags = flags;
    }
    return sqe;
}
}
