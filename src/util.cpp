#include "aiopp/util.hpp"

#include <system_error>

namespace aiopp {
std::string errnoToString(int err)
{
    return std::make_error_code(static_cast<std::errc>(err)).message();
}

std::string hexDump(std::span<std::byte> data)
{
    static constexpr std::array<char, 16> hexChars { '0', '1', '2', '3', '4', '5', '6', '7', '8',
        '9', 'a', 'b', 'c', 'd', 'e', 'f' };
    std::string ret;
    ret.reserve(data.size() * 2);
    for (const auto byte : data) {
        const auto hi = (static_cast<uint8_t>(byte) & 0xF0) >> 4;
        const auto lo = (static_cast<uint8_t>(byte) & 0x0F);
        ret.push_back(hexChars[hi]);
        ret.push_back(hexChars[lo]);
    }
    return ret;
}
}
