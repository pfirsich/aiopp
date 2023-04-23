#include "aiopp/ioqueue.hpp"
#include "aiopp/socket.hpp"

#include "aiopp/awaitables.hpp"
#include "aiopp/basiccoroutine.hpp"

#include "spdlogger.hpp"

using namespace aiopp;

BasicCoroutine serve(IoQueue& io, const Fd& socket)
{
    while (true) {
        std::string receiveBuffer(1024, '\0');
        ::sockaddr_in clientAddr;
        ::iovec iov { receiveBuffer.data(), receiveBuffer.size() };
        ::msghdr msg {
            .msg_name = &clientAddr,
            .msg_namelen = sizeof(clientAddr),
            .msg_iov = &iov,
            .msg_iovlen = 1,
            .msg_control = nullptr,
            .msg_controllen = 0,
            .msg_flags = 0,
        };
        const auto [recvEc, receivedBytes] = co_await recvmsg(io, socket, &msg, 0);
        if (recvEc) {
            spdlog::error("Error in recvmsg: {}", recvEc.message());
            continue;
        }
        receiveBuffer.resize(receivedBytes);

        iov = ::iovec { receiveBuffer.data(), receiveBuffer.size() };
        const auto [sendEc, sentBytes] = co_await sendmsg(io, socket, &msg, 0);
        if (sendEc) {
            spdlog::error("Error in sendmsg: {}", sendEc.message());
            continue;
        }
    }
}

int main()
{
    setLogger(std::make_unique<SpdLogger>());

    auto socket = createSocket(SocketType::Udp, IpAddress::parse("0.0.0.0").value(), 4242);
    if (socket == -1) {
        return 1;
    }

    IoQueue io;
    serve(io, socket);
    io.run();
    return 0;
}
