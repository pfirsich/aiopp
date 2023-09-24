#pragma once

#include <span>
#include <string>

namespace aiopp {
std::string errnoToString(int err);
std::string hexDump(std::span<std::byte> data);
}
