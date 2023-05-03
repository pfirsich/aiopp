#pragma once

#include <sys/eventfd.h>

#include "aiopp/fd.hpp"
#include "aiopp/function.hpp"
#include "aiopp/result.hpp"
#include "aiopp/task.hpp"

namespace aiopp {
class IoQueue;

class EventFd {
public:
    enum class Flags : int {
        None = 0,
        Semaphore = EFD_SEMAPHORE,
    };

    EventFd(Flags flags = Flags::None);

    void read(IoQueue& io, Function<void(Result<uint64_t>)> callback) const;

    Task<Result<uint64_t>> read(IoQueue& io) const;

    void write(uint64_t v = 1) const;

    const Fd& fd() const { return fd_; }

private:
    Fd fd_;
};
}
