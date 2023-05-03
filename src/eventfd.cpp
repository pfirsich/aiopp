#include "aiopp/eventfd.hpp"

#include <sys/eventfd.h>

#include <aiopp/ioqueue.hpp>

namespace aiopp {
EventFd::EventFd(Flags flags)
    : fd_(::eventfd(0, static_cast<int>(flags)))
{
    assert(fd_ != -1);
}

void EventFd::read(IoQueue& io, Function<void(Result<uint64_t>)> callback) const
{
    auto readBuf = std::make_unique<uint64_t>();
    io.read(fd_, readBuf.get(), sizeof(uint64_t),
        [callback = std::move(callback), readBuf = std::move(readBuf)](IoResult result) {
            if (result) {
                callback(*readBuf);
            } else {
                callback(error(result.error()));
            }
        });
}

Task<Result<uint64_t>> EventFd::read(IoQueue& io) const
{
    uint64_t buf = 0;
    const auto res = co_await io.read(fd_, &buf, sizeof(buf));
    if (res) {
        co_return buf;
    } else {
        co_return error(res.error());
    }
}

void EventFd::write(uint64_t v) const
{
    const auto res = ::eventfd_write(fd_, v);
    assert(res == 0);
}
}
