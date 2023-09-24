#include "aiopp/ioqueue.hpp"
#include "aiopp/socket.hpp"

#include "aiopp/basiccoroutine.hpp"
#include "aiopp/task.hpp"

#include "spdlogger.hpp"

using namespace aiopp;

Task<std::pair<std::error_code, bool>> sendAll(
    IoQueue& io, const Fd& socket, const std::string& buffer)
{
    size_t offset = 0;
    while (offset < buffer.size()) {
        const auto sentBytes
            = co_await io.send(socket, buffer.data() + offset, buffer.size() - offset);
        if (!sentBytes) {
            spdlog::error("Error in send: {}", sentBytes.error().message());
            co_return std::make_pair(sentBytes.error(), false);
        }

        if (*sentBytes == 0) { // Connection closed
            co_return std::make_pair(std::error_code {}, true);
        }

        offset += *sentBytes;
    }
    co_return std::make_pair(std::error_code {}, false);
}

BasicCoroutine echo(IoQueue& io, Fd socket)
{
    while (true) {
        std::string recvBuffer(1024, '\0');
        const auto receivedBytes = co_await io.timeout(
            std::chrono::milliseconds(5000), io.recv(socket, recvBuffer.data(), recvBuffer.size()));
        if (!receivedBytes && receivedBytes.error() == std::errc::operation_canceled) {
            co_await sendAll(io, socket, "Session timed out. Bye!");
            break;
        }

        if (!receivedBytes) {
            spdlog::error("Error in receive: {}", receivedBytes.error().message());
            break;
        }

        if (*receivedBytes == 0) { // Connection closed
            break;
        }

        recvBuffer.resize(*receivedBytes);

        const auto [sendEc, eof] = co_await sendAll(io, socket, recvBuffer);
        if (sendEc || eof) {
            break;
        }
    }
    co_await io.close(socket.release());
}

BasicCoroutine serve(IoQueue& io, Fd&& listenSocket)
{
    while (true) {
        const auto fd = co_await io.accept(listenSocket, nullptr, nullptr);
        if (!fd) {
            spdlog::error("Error in accept: {}", fd.error().message());
            continue;
        }
        echo(io, Fd { *fd });
    }
}

int main()
{
    setLogger(std::make_unique<SpdLogger>());

    auto socket = createTcpListenSocket(IpAddressPort::parse("0.0.0.0:4242").value());
    if (socket == -1) {
        return 1;
    }

    IoQueue io;
    serve(io, std::move(socket));
    io.run();
    return 0;
}
