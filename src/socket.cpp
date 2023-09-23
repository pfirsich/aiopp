#include "aiopp/socket.hpp"

#include <cstring>

#include <arpa/inet.h>

#include "aiopp/log.hpp"
#include "aiopp/util.hpp"

namespace aiopp {
std::optional<IpAddress> IpAddress::parse(const std::string& str)
{
    ::in_addr addr;
    const auto res = ::inet_aton(str.c_str(), &addr);
    if (res == 0) {
        return std::nullopt;
    }
    return addr.s_addr;
}

IpAddress::IpAddress(uint32_t ipv4Addr)
    : ipv4(ipv4Addr)
{
}

std::string IpAddress::toString() const
{
    return ::inet_ntoa(::in_addr { ipv4 });
}

namespace {
    int getSocketType(SocketType type)
    {
        switch (type) {
        case SocketType::Tcp:
            return SOCK_STREAM;
        case SocketType::Udp:
            return SOCK_DGRAM;
        default:
            return 0;
        }
    }
}

Fd createSocket(SocketType type)
{
    Fd socket { ::socket(AF_INET, getSocketType(type), 0) };
    if (socket == -1) {
        getLogger().log(LogSeverity::Error, "Could not create socket: " + errnoToString(errno));
    }
    return socket;
}

Fd createSocket(SocketType type, const IpAddress& bindAddress, uint16_t bindPort, bool reuseAddr)
{
    auto socket = createSocket(type);
    if (socket == -1) {
        return socket;
    }

    if (reuseAddr) {
        const int reuse = 1;
        if (::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
            getLogger().log(LogSeverity::Error, "Could not set sockopt SO_REUSEADDR");
            return Fd {};
        }
    }

    if (!bind(socket, bindAddress, bindPort)) {
        return Fd {};
    }
    return socket;
}

bool bind(const Fd& socket, const IpAddress& address, uint16_t port)
{
    ::sockaddr_in sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = address.ipv4;
    sa.sin_port = htons(port);

    if (::bind(socket, reinterpret_cast<const ::sockaddr*>(&sa), sizeof(sa)) == -1) {
        getLogger().log(LogSeverity::Error, "Error binding socket to: " + errnoToString(errno));
        return false;
    }

    return true;
}

Fd createUdpSocket(const IpAddress& bindAddress, uint16_t bindPort)
{
    auto fd = createSocket(SocketType::Udp);
    if (!bind(fd, bindAddress, bindPort)) {
        return Fd {};
    }
    return fd;
}

Fd createTcpListenSocket(const IpAddress& address, uint16_t port, int backlog)
{
    auto socket = createSocket(SocketType::Tcp, address, port, true);
    if (socket == -1) {
        return socket;
    }

    if (::listen(socket, backlog) == -1) {
        getLogger().log(LogSeverity::Error, "Could not listen on socket: " + errnoToString(errno));
        return Fd {};
    }

    return socket;
}
}
