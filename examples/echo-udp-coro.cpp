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
        const auto [recvEc, receivedBytes] = co_await recvfrom(
            io, socket, receiveBuffer.data(), receiveBuffer.size(), 0, &clientAddr);
        if (recvEc) {
            spdlog::error("Error in recvfrom: {}", recvEc.message());
            continue;
        }
        receiveBuffer.resize(receivedBytes);

        const auto [sendEc, sentBytes] = co_await sendto(
            io, socket, receiveBuffer.data(), receiveBuffer.size(), 0, &clientAddr);
        if (sendEc) {
            spdlog::error("Error in sendto: {}", sendEc.message());
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
