#pragma once

#include <coroutine>

#include "aiopp/ioqueue.hpp"

namespace aiopp {
template <typename Method, typename... Args>
class IoQueueAwaitable {
public:
    struct Result {
        std::error_code ec;
        int result;
    };

    IoQueueAwaitable(IoQueue& io, Method method, Args... args)
        : io_(io)
        , method_(method)
        , args_(std::make_tuple(args...))
    {
    }

    auto operator co_await()
    {
        struct Awaiter {
            IoQueue& io;
            Method method;
            std::tuple<Args...> args;
            Result result = {};

            Awaiter(IoQueue& io, Method method, std::tuple<Args...> args)
                : io(io)
                , method(method)
                , args(std::move(args))
            {
            }

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> handle) noexcept
            {
                std::apply(
                    [this, handle](auto&&... args) {
                        (io.*method)(std::forward<decltype(args)>(args)...,
                            [this, handle](std::error_code ec, int res) {
                                result = Result { ec, res };
                                handle.resume();
                            });
                    },
                    args);
            }

            Result await_resume() const noexcept { return result; }
        };
        return Awaiter { io_, method_, std::move(args_) };
    }

private:
    IoQueue& io_;
    Method method_;
    std::tuple<Args...> args_;
};

auto accept(IoQueue& io, int fd, ::sockaddr_in* addr, ::socklen_t* addrLen)
{
    return IoQueueAwaitable<decltype(&IoQueue::accept), int, ::sockaddr_in*, ::socklen_t*>(
        io, &IoQueue::accept, fd, addr, addrLen);
}

auto recv(IoQueue& io, int sock, void* buf, size_t len)
{
    using RecvType = bool (IoQueue::*)(int, void*, size_t, IoQueue::HandlerEcRes);
    return IoQueueAwaitable<RecvType, int, void*, size_t>(
        io, static_cast<RecvType>(&IoQueue::recv), sock, buf, len);
}

auto send(IoQueue& io, int sock, const void* buf, size_t len)
{
    using SendType = bool (IoQueue::*)(int, const void*, size_t, IoQueue::HandlerEcRes);
    return IoQueueAwaitable<SendType, int, const void*, size_t>(
        io, static_cast<SendType>(&IoQueue::send), sock, buf, len);
}
}
