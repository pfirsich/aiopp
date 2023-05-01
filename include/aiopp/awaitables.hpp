#pragma once

#include <coroutine>

#include "aiopp/ioqueue.hpp"

namespace aiopp {
template <typename Method, typename... Args>
class CallbackAwaitable {
public:
    CallbackAwaitable(IoQueue& io, Method method, Args... args)
        : io_(io)
        , method_(method)
        , args_(std::make_tuple(args...))
    {
    }

    auto operator co_await()
    {
        struct Awaiter {
            CallbackAwaitable& awaitable;
            IoQueue::OperationHandle operation = {};
            IoResult result = {};

            ~Awaiter()
            {
                if (operation) {
                    awaitable.io_.cancel(operation, true);
                }
            }

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> handle) noexcept
            {
                std::apply(
                    [this, handle](auto&&... args) {
                        operation = (awaitable.io_.*awaitable.method_)(
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

auto recvfrom(IoQueue& io, int sockfd, void* buf, size_t len, int flags, ::sockaddr_in* srcAddr)
{
    using RecvFromType = IoQueue::OperationHandle (IoQueue::*)(
        int, void*, size_t, int, ::sockaddr_in*, IoQueue::CompletionHandler);
    return CallbackAwaitable(
        io, static_cast<RecvFromType>(&IoQueue::recvfrom), sockfd, buf, len, flags, srcAddr);
}

auto sendto(
    IoQueue& io, int sockfd, const void* buf, size_t len, int flags, const ::sockaddr_in* destAddr)
{
    using SendToType = IoQueue::OperationHandle (IoQueue::*)(
        int, const void*, size_t, int, const ::sockaddr_in*, IoQueue::CompletionHandler);
    return CallbackAwaitable(
        io, static_cast<SendToType>(&IoQueue::sendto), sockfd, buf, len, flags, destAddr);
}
}
