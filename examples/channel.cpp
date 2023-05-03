#include <cstring>
#include <vector>

#include "aiopp/basiccoroutine.hpp"
#include "aiopp/channel.hpp"
#include "aiopp/threadpool.hpp"

#include <spdlog/spdlog.h>

using namespace aiopp;

ThreadPool& threadPool()
{
    static ThreadPool tp;
    return tp;
}

BasicCoroutine receiver(std::string id, IoQueue& io, Channel<std::string>& channel)
{
    while (true) {
        const auto msg = co_await channel.receive();
        spdlog::info("[{}] Message: {}", id, msg);
    }
}

BasicCoroutine sender(IoQueue& io, Channel<std::string>& channel)
{
    threadPool().push([&channel]() {
        while (true) {
            for (size_t i = 0; i < 3; ++i) {
                channel.send("Whattup");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    });

    while (true) {
        channel.send("Hello!");
        co_await io.timeout(std::chrono::milliseconds(1000));
    }
}

int main()
{
    IoQueue io;
    Channel<std::string> channel(io);
    receiver("1", io, channel);
    receiver("2", io, channel);
    sender(io, channel);
    io.run();
}
