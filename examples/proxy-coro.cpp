#include <span>

#include "spdlogger.hpp"

#include "aiopp/ioqueue.hpp"
#include "aiopp/socket.hpp"
#include "aiopp/task.hpp"
#include "aiopp/util.hpp"
#include "aiopp/wait.hpp"

using namespace aiopp;

struct Configuration {
    struct Upstream {
        std::string listenAddr;
        std::string upstreamAddr;
    };

    std::vector<Upstream> upstreams;
};

static const Configuration config
{
    .upstreams = {
        Configuration::Upstream {
            .listenAddr = "127.0.0.1:4242",
            .upstreamAddr = "127.0.0.1:4243",
        },
    },
};

Task<std::error_code> sendAll(IoQueue& io, const Fd& socket, std::span<const std::byte> buffer)
{
    size_t offset = 0;
    while (offset < buffer.size()) {
        const auto sentBytes
            = co_await io.send(socket, buffer.data() + offset, buffer.size() - offset);
        if (!sentBytes) {
            spdlog::error("Error in send: {}", sentBytes.error().message());
            co_return sentBytes.error();
        }
        offset += *sentBytes;
    }
    co_return {};
}

Task<void> echo(IoQueue& io, Fd& recvSocket, Fd& sendSocket)
{
    std::vector<std::byte> recvBuffer(8 * 1024);
    while (true) {
        const auto receivedBytes
            = co_await io.recv(recvSocket, recvBuffer.data(), recvBuffer.size());
        if (!receivedBytes) {
            spdlog::error("Error in receive: {}", receivedBytes.error().message());
            break;
        }

        if (*receivedBytes == 0) {
            spdlog::info("Connection closed");
            break;
        }

        const auto recvData = std::span { recvBuffer }.first(*receivedBytes);
        const auto sendRes = co_await sendAll(io, sendSocket, recvData);
        if (sendRes != std::error_code {}) {
            spdlog::info("Error in send: {}", sendRes.message());
            break;
        }
    }
    // We shutdown the sockets here to wake up the other `echo`, which will then detect a connection
    // closure as well.
    // And we do an async closure, so the destructor of this coroutine (and
    // clientSocket) does not hold up the event loop synchronously.
    co_await io.shutdown(recvSocket, SHUT_RDWR);
    co_await io.close(recvSocket.release());
    co_await io.shutdown(sendSocket, SHUT_RDWR);
    co_await io.close(sendSocket.release());
}

BasicCoroutine handleClient(IoQueue& io, Fd clientSocket, const IpAddressPort& upstreamAddr)
{
    auto upstreamSocket = createSocket(SocketType::Tcp);
    const auto sa = upstreamAddr.getSockAddr();
    const auto connRes
        = co_await io.timeout(std::chrono::milliseconds(2000), io.connect(upstreamSocket, &sa));

    if (!connRes) {
        // There is no good way to communicate the reason of the closure out-of-band to the client,
        // so for now we will not communicate anything.
        if (connRes.error() == std::errc::operation_canceled) {
            spdlog::error("Connect timed out after 2s");
        } else {
            spdlog::error("Error in connect: {}", connRes.error().message());
        }
        co_await io.close(clientSocket.release());
        co_await io.close(upstreamSocket.release());
        co_return;
    }

    spdlog::info("Connected to upstream at {}", upstreamAddr.toString());

    // This coroutine must outlive both `echo`s, because they reference the sockets, so we need to
    // wait for them.
    co_await WaitAll {
        echo(io, clientSocket, upstreamSocket),
        echo(io, upstreamSocket, clientSocket),
    };

    spdlog::info("Done handling client");
}

BasicCoroutine serve(IoQueue& io, Fd listenSocket, const IpAddressPort& upstreamAddr)
{
    while (true) {
        ::sockaddr_in sa;
        socklen_t sockLen = sizeof(sa);
        const auto fd = co_await io.accept(listenSocket, &sa, &sockLen);
        if (!fd) {
            spdlog::error("Error in accept: {}", fd.error().message());
            continue;
        }
        spdlog::info("Got connection from {}", IpAddressPort(sa).toString());
        handleClient(io, Fd { *fd }, upstreamAddr);
    }
}

int main(int, char**)
{
    setLogger(std::make_unique<SpdLogger>());

    IoQueue io;

    for (const auto& upstream : config.upstreams) {
        const auto listenAddr = IpAddressPort::parse(upstream.listenAddr);
        if (!listenAddr) {
            spdlog::critical("Invalid listen address '{}'", upstream.upstreamAddr);
            return 1;
        }

        const auto upstreamAddr = IpAddressPort::parse(upstream.upstreamAddr);
        if (!upstreamAddr) {
            spdlog::critical("Invalid upstream address '{}'", upstream.upstreamAddr);
            return 1;
        }

        auto socket = createTcpListenSocket(*listenAddr);
        if (socket == -1) {
            // Error details have already been logged
            spdlog::critical("Could not create listen socket");
            return 1;
        }

        serve(io, std::move(socket), *upstreamAddr);
    }

    io.run();

    return 0;
}
