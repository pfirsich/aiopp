#pragma once

#include <string>
#include <vector>

#include "aiopp/socket.hpp"
#include "aiopp/threadpool.hpp"

namespace aiopp {
Task<std::vector<IpAddress>> resolve(IoQueue& io, ThreadPool& tp, const std::string& name);
}
