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
        const auto receivedBytes = co_await recvfrom(
            io, socket, receiveBuffer.data(), receiveBuffer.size(), 0, &clientAddr);
        if (!receivedBytes) {
            spdlog::error("Error in recvfrom: {}", receivedBytes.error().message());
            continue;
        }
        receiveBuffer.resize(*receivedBytes);

        const auto sentBytes = co_await sendto(
            io, socket, receiveBuffer.data(), receiveBuffer.size(), 0, &clientAddr);
        if (!sentBytes) {
            spdlog::error("Error in sendto: {}", sentBytes.error().message());
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
