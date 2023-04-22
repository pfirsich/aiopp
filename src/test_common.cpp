#include "test_common.hpp"

#include <cstring>

#include <spdlog/spdlog.h>

#include <netinet/in.h>
#include <sys/socket.h>

aiopp::Fd createListenSocket()
{
    aiopp::Fd sock_ { ::socket(AF_INET, SOCK_STREAM, 0) };
    if (sock_ == -1) {
        spdlog::error("Could not create socket: {}", errno);
        return aiopp::Fd {};
    }

    sockaddr_in addr;
    ::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(6942);

    const int reuse = 1;
    if (::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        spdlog::error("Could not set sockopt SO_REUSEADDR");
        return aiopp::Fd {};
    }

    if (::bind(sock_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == -1) {
        spdlog::error("Could not bind socket: {}", errno);
        return aiopp::Fd {};
    }

    if (::listen(sock_, SOMAXCONN) == -1) {
        spdlog::error("Could not listen on socket: {}", errno);
        return aiopp::Fd {};
    }

    return sock_;
}

namespace {
spdlog::level::level_enum mapSeverity(aiopp::LogSeverity severity)
{
    switch (severity) {
    case aiopp::LogSeverity::Debug:
        return spdlog::level::level_enum::debug;
    case aiopp::LogSeverity::Info:
        return spdlog::level::level_enum::info;
    case aiopp::LogSeverity::Warning:
        return spdlog::level::level_enum::warn;
    case aiopp::LogSeverity::Error:
        return spdlog::level::level_enum::err;
    case aiopp::LogSeverity::Fatal:
        return spdlog::level::level_enum::critical;
    default:
        std::abort();
    }
}
}

void SpdLogger::log(aiopp::LogSeverity severity, const std::string& message)
{
    spdlog::log(mapSeverity(severity), message);
}
