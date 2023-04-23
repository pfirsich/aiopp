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

    IoQueueAwaitable(aiopp::IoQueue& io, Method method, Args... args)
        : io_(io)
        , method_(method)
        , args_(std::make_tuple(args...))
    {
    }

    auto operator co_await()
    {
        struct Awaiter {
            aiopp::IoQueue& io;
            Method method;
            std::tuple<Args...> args;
            Result result = {};

            Awaiter(aiopp::IoQueue& io, Method method, std::tuple<Args...> args)
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
    aiopp::IoQueue& io_;
    Method method_;
    std::tuple<Args...> args_;
};

auto accept(aiopp::IoQueue& io, int fd, ::sockaddr_in* addr, ::socklen_t* addrLen)
{
    return IoQueueAwaitable<decltype(&aiopp::IoQueue::accept), int, ::sockaddr_in*, ::socklen_t*>(
        io, &aiopp::IoQueue::accept, fd, addr, addrLen);
}

auto recv(aiopp::IoQueue& io, int sock, void* buf, size_t len)
{
    using RecvType = bool (aiopp::IoQueue::*)(int, void*, size_t, aiopp::IoQueue::HandlerEcRes);
    return IoQueueAwaitable<RecvType, int, void*, size_t>(
        io, static_cast<RecvType>(&aiopp::IoQueue::recv), sock, buf, len);
}

auto send(aiopp::IoQueue& io, int sock, const void* buf, size_t len)
{
    using SendType
        = bool (aiopp::IoQueue::*)(int, const void*, size_t, aiopp::IoQueue::HandlerEcRes);
    return IoQueueAwaitable<SendType, int, const void*, size_t>(
        io, static_cast<SendType>(&aiopp::IoQueue::send), sock, buf, len);
}
}
