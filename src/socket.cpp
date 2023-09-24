#include "aiopp/socket.hpp"

#include <charconv>
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

std::string IpAddress::toString() const
{
    return ::inet_ntoa(::in_addr { ipv4 });
}

std::optional<IpAddressPort> IpAddressPort::parse(const std::string& str)
{
    const auto portDelim = str.find(':');
    if (portDelim == std::string::npos) {
        // I could return an IpAddressPort with port = 0, but I think
        // I will catch more mistakes by failing here.
        return std::nullopt;
    }
    const auto ipStr = str.substr(0, portDelim);
    const auto ip = IpAddress::parse(ipStr);
    if (!ip) {
        return std::nullopt;
    }
    uint16_t port = 0;
    const auto portSv = std::string_view { str }.substr(portDelim + 1);
    const auto [ptr, ec] = std::from_chars(&portSv.front(), &portSv.back() + 1, port);
    if (ec != std::errc() || ptr != &portSv.back() + 1) {
        return std::nullopt;
    }
    return IpAddressPort(*ip, port);
}

std::string IpAddressPort::toString() const
{
    return address.toString() + ":" + std::to_string(port);
}

::sockaddr_in IpAddressPort::getSockAddr() const
{
    ::sockaddr_in sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = address.ipv4;
    sa.sin_port = htons(port);
    return sa;
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

Fd createSocket(SocketType type, const IpAddressPort& bindAddress, bool reuseAddr)
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

    if (!bind(socket, bindAddress)) {
        return Fd {};
    }
    return socket;
}

bool bind(const Fd& socket, const IpAddressPort& address)
{
    ::sockaddr_in sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = address.address.ipv4;
    sa.sin_port = htons(address.port);

    if (::bind(socket, reinterpret_cast<const ::sockaddr*>(&sa), sizeof(sa)) == -1) {
        getLogger().log(LogSeverity::Error,
            "Error binding socket to " + address.toString() + ": " + errnoToString(errno));
        return false;
    }

    return true;
}

Fd createUdpSocket(const IpAddressPort& bindAddress)
{
    auto fd = createSocket(SocketType::Udp);
    if (!bind(fd, bindAddress)) {
        return Fd {};
    }
    return fd;
}

Fd createTcpListenSocket(const IpAddressPort& listenAddress, int backlog)
{
    auto socket = createSocket(SocketType::Tcp, listenAddress, true);
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
