#include "aiopp/ioqueue.hpp"
#include "aiopp/socket.hpp"

#include "aiopp/awaitables.hpp"
#include "aiopp/basiccoroutine.hpp"
#include "aiopp/task.hpp"

#include "spdlogger.hpp"

using namespace aiopp;

Task<std::pair<std::error_code, bool>> sendAll(
    IoQueue& io, const Fd& socket, const std::string& buffer)
{
    size_t offset = 0;
    while (offset < buffer.size()) {
        const auto [ec, sentBytes]
            = co_await send(io, socket, buffer.data() + offset, buffer.size() - offset);
        if (ec) {
            spdlog::error("Error in send: {}", ec.message());
            co_return std::make_pair(ec, false);
        }

        if (sentBytes == 0) { // Connection closed
            co_return std::make_pair(ec, true);
        }

        offset += sentBytes;
    }
    co_return std::make_pair(std::error_code {}, false);
}

BasicCoroutine echo(IoQueue& io, Fd socket)
{
    while (true) {
        std::string recvBuffer(1024, '\0');
        const auto [ec, receivedBytes]
            = co_await recv(io, socket, recvBuffer.data(), recvBuffer.size());
        if (ec) {
            spdlog::error("Error in receive: {}", ec.message());
            break;
        }

        if (receivedBytes == 0) { // Connection closed
            break;
        }

        recvBuffer.resize(receivedBytes);

        const auto [sendEc, eof] = co_await sendAll(io, socket, recvBuffer);
        if (sendEc || eof) {
            break;
        }
    }
}

BasicCoroutine serve(IoQueue& io, Fd&& listenSocket)
{
    while (true) {
        const auto [ec, fd] = co_await accept(io, listenSocket, nullptr, nullptr);
        if (ec) {
            spdlog::error("Error in accept: {}", ec.message());
            continue;
        }
        echo(io, Fd { fd });
    }
}

int main()
{
    setLogger(std::make_unique<SpdLogger>());

    auto socket = createTcpListenSocket(IpAddress::parse("0.0.0.0").value(), 4242);
    if (socket == -1) {
        return 1;
    }

    IoQueue io;
    serve(io, std::move(socket));
    io.run();
    return 0;
}
