#include "aiopp/ioqueue.hpp"
#include "aiopp/socket.hpp"

#include "aiopp/basiccoroutine.hpp"
#include "aiopp/task.hpp"

#include "spdlogger.hpp"

using namespace aiopp;

const std::string& getResponse()
{
    static const std::string body = "This is a short string that serves as a response";
    static std::string response;
    if (response.empty()) {
        response.reserve(512);
        response.append("HTTP/1.1 200 OK\r\n");
        response.append("Server: aiopp coro\r\n");
        response.append("Content-Type: text/plain\r\n");
        response.append("Content-Length: ");
        response.append(std::to_string(body.size()));
        response.append("\r\n");
        response.append("\r\n");
        response.append(body);
    }
    return response;
}

Task<std::pair<std::error_code, bool>> sendAll(
    IoQueue& io, const Fd& socket, std::string_view buffer)
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

BasicCoroutine startSession(IoQueue& io, Fd socket)
{
    while (true) {
        std::string recvBuffer(1024, '\0');
        const auto receivedBytes = co_await io.recv(socket, recvBuffer.data(), recvBuffer.size());
        if (!receivedBytes) {
            spdlog::error("Error in receive: {}", receivedBytes.error().message());
            break;
        }

        if (*receivedBytes == 0) { // Connection closed
            break;
        }

        const auto [sendEc, eof] = co_await sendAll(io, socket, getResponse());
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
        startSession(io, Fd { *fd });
    }
}

int main()
{
    setLogger(std::make_unique<SpdLogger>());

    auto socket = createTcpListenSocket(IpAddress::parse("0.0.0.0").value(), 4242);
    if (socket == -1) {
        return 1;
    }

    IoQueue io(1024);
    serve(io, std::move(socket));
    io.run();
    return 0;
}
