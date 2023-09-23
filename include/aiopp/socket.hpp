#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <sys/socket.h>

#include "aiopp/fd.hpp"

namespace aiopp {
enum class SocketType { Tcp, Udp };

struct IpAddress {
    static std::optional<IpAddress> parse(const std::string& str);

    IpAddress(uint32_t ipv4Addr);

    std::string toString() const;

    uint32_t ipv4; // in network byte order!
};

Fd createSocket(SocketType type);
Fd createSocket(
    SocketType type, const IpAddress& bindAddress, uint16_t bindPort, bool reuseAddr = false);

bool bind(const Fd& fd, const IpAddress& address, uint16_t port);

Fd createTcpListenSocket(const IpAddress& address, uint16_t port, int backlog = SOMAXCONN);
}
