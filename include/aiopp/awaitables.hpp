#pragma once

#include <coroutine>

#include "aiopp/ioqueue.hpp"

namespace aiopp {
template <typename Method, typename... Args>
class IoQueueAwaitable {
public:
    IoQueueAwaitable(IoQueue& io, Method method, Args... args)
        : io_(io)
        , method_(method)
        , args_(std::make_tuple(args...))
    {
    }

    auto operator co_await()
    {
        struct Awaiter {
            IoQueueAwaitable& awaitable;
            IoResult result = {};

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> handle) noexcept
            {
                std::apply(
                    [this, handle](auto&&... args) {
                        (awaitable.io_.*awaitable.method_)(
                            std::forward<decltype(args)>(args)..., [this, handle](IoResult res) {
                                result = res;
                                handle.resume();
                            });
                    },
                    awaitable.args_);
            }

            IoResult await_resume() const noexcept { return result; }
        };
        return Awaiter { *this };
    }

private:
    IoQueue& io_;
    Method method_;
    std::tuple<Args...> args_;
};

auto accept(IoQueue& io, int fd, ::sockaddr_in* addr, ::socklen_t* addrLen)
{
    using AcceptType
        = bool (IoQueue::*)(int, ::sockaddr_in*, socklen_t*, IoQueue::CompletionHandler);
    return IoQueueAwaitable<AcceptType, int, ::sockaddr_in*, ::socklen_t*>(
        io, static_cast<AcceptType>(&IoQueue::accept), fd, addr, addrLen);
}

auto recv(IoQueue& io, int sock, void* buf, size_t len)
{
    using RecvType = bool (IoQueue::*)(int, void*, size_t, IoQueue::CompletionHandler);
    return IoQueueAwaitable<RecvType, int, void*, size_t>(
        io, static_cast<RecvType>(&IoQueue::recv), sock, buf, len);
}

auto send(IoQueue& io, int sock, const void* buf, size_t len)
{
    using SendType = bool (IoQueue::*)(int, const void*, size_t, IoQueue::CompletionHandler);
    return IoQueueAwaitable<SendType, int, const void*, size_t>(
        io, static_cast<SendType>(&IoQueue::send), sock, buf, len);
}

auto recvmsg(IoQueue& io, int sockfd, ::msghdr* msg, int flags)
{
    return IoQueueAwaitable<decltype(&IoQueue::recvmsg), int, ::msghdr*, int>(
        io, &IoQueue::recvmsg, sockfd, msg, flags);
}

auto sendmsg(IoQueue& io, int sockfd, const ::msghdr* msg, int flags)
{
    return IoQueueAwaitable<decltype(&IoQueue::sendmsg), int, const ::msghdr*, int>(
        io, &IoQueue::sendmsg, sockfd, msg, flags);
}

auto recvfrom(IoQueue& io, int sockfd, void* buf, size_t len, int flags, ::sockaddr_in* srcAddr)
{
    using RecvFromType
        = bool (IoQueue::*)(int, void*, size_t, int, ::sockaddr_in*, IoQueue::CompletionHandler);
    return IoQueueAwaitable<RecvFromType, int, void*, size_t, int, ::sockaddr_in*>(
        io, static_cast<RecvFromType>(&IoQueue::recvfrom), sockfd, buf, len, flags, srcAddr);
}

auto sendto(
    IoQueue& io, int sockfd, const void* buf, size_t len, int flags, const ::sockaddr_in* destAddr)
{
    using SendToType = bool (IoQueue::*)(
        int, const void*, size_t, int, const ::sockaddr_in*, IoQueue::CompletionHandler);
    return IoQueueAwaitable<SendToType, int, const void*, size_t, int, const ::sockaddr_in*>(
        io, &IoQueue::sendto, sockfd, buf, len, flags, destAddr);
}

auto close(IoQueue& io, int fd)
{
    using CloseType = bool (IoQueue::*)(int, IoQueue::CompletionHandler);
    return IoQueueAwaitable<CloseType, int>(io, &IoQueue::close, fd);
}
}
