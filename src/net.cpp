#include "aiopp/net.hpp"

#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "aiopp/log.hpp"

namespace aiopp {
Task<std::vector<IpAddress>> resolve(IoQueue& io, ThreadPool& tp, const std::string& name)
{
    return wrapAsTask(io, tp, [name]() -> std::vector<IpAddress> {
        struct ::addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        std::vector<IpAddress> addrs;
        ::addrinfo* result;
        const auto res = ::getaddrinfo(name.c_str(), nullptr, &hints, &result);
        if (res != 0) {
            getLogger().log(LogSeverity::Error, std::string("getaddrinfo: ") + gai_strerror(res));
            return {};
        }
        for (::addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
            if (ai->ai_addrlen == sizeof(::sockaddr_in)) {
                addrs.push_back(
                    reinterpret_cast<const ::sockaddr_in*>(ai->ai_addr)->sin_addr.s_addr);
            }
        }
        ::freeaddrinfo(result);
        return addrs;
    });
}
}
