#include "aiopp/eventfd.hpp"

#include <sys/eventfd.h>

#include <aiopp/ioqueue.hpp>

namespace aiopp {
EventFd::EventFd(Flags flags)
    : fd_(::eventfd(0, static_cast<int>(flags)))
{
    assert(fd_ != -1);
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
