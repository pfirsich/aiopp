#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <netinet/in.h>
#include <sys/socket.h>

#include "aiopp/fd.hpp"

namespace aiopp {
enum class SocketType { Tcp, Udp };

struct IpAddress {
    static std::optional<IpAddress> parse(const std::string& str);

    constexpr IpAddress() = default;

    constexpr IpAddress(uint32_t ipv4Addr)
        : ipv4(ipv4Addr)
    {
    }

    constexpr IpAddress(uint8_t oct0, uint8_t oct1, uint8_t oct2, uint8_t oct3)
        : IpAddress(oct0 | (oct1 << 8) | (oct2 << 16) | (oct3 << 24))
    {
    }

    std::string toString() const;

    uint32_t ipv4 = 0; // in network byte order!
};

struct IpAddressPort {
    static std::optional<IpAddressPort> parse(const std::string& str);

    constexpr IpAddressPort() = default;

    constexpr IpAddressPort(IpAddress addr, uint16_t port)
        : address(addr)
        , port(port)
    {
    }

    constexpr IpAddressPort(IpAddress addr)
        : IpAddressPort(addr, 0)
    {
    }

    std::string toString() const;

    ::sockaddr_in getSockAddr() const;

    IpAddress address;
    uint16_t port = 0;
};

Fd createSocket(SocketType type);
Fd createSocket(SocketType type, const IpAddressPort& bindAddress, bool reuseAddr = false);

bool bind(const Fd& fd, const IpAddressPort& address);

Fd createTcpListenSocket(const IpAddressPort& listenAddress, int backlog = SOMAXCONN);
}
