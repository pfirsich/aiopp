#include <cstring>
#include <vector>

#include "aiopp/basiccoroutine.hpp"
#include "aiopp/net.hpp"

#include <spdlog/spdlog.h>

using namespace aiopp;

// This example is not very interesting. It's just a chance to show Future and ThreadPool.

BasicCoroutine start(IoQueue& io)
{
    const auto res = co_await resolve(io, getDefaultThreadPool(), "theshoemaker.de");
    for (const auto& addr : res) {
        spdlog::info("addr: {}", addr.toString());
    }
}

int main()
{
    IoQueue io;
    start(io);
    io.run();
}
