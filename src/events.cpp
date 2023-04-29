#include "aiopp/events.hpp"

#include <cassert>

#include <sys/eventfd.h>
#include <unistd.h>

#include "aiopp/ioqueue.hpp"

namespace aiopp {
EventFd::EventFd(IoQueue& io)
    : io_(io)
    , fd_(::eventfd(0, 0))
{
}

bool EventFd::read(Function<void(std::error_code, uint64_t)> cb)
{
    assert(fd_ != -1);
    return io_.read(
        fd_, &readBuf_, sizeof(readBuf_), [this, cb = std::move(cb)](IoResult readBytes) {
            if (!readBytes) {
                cb(readBytes.error(), 0);
                return;
            }
            // man 2 eventfd: Each successful read(2) returns an 8-byte integer.
            // The example does handle the case of res != 8, but I don't really know
            // what I am not sure what I should do in that case, so I assert for now.
            assert(*readBytes == sizeof(uint64_t));
            cb(std::error_code(), readBuf_);
        });
}

void EventFd::write(uint64_t v)
{
    assert(fd_ != -1);
    if (::write(fd_, &v, sizeof(uint64_t)) != sizeof(uint64_t)) {
        // We cannot call the read handler (can't reach it).
        // We cannot cancel or terminate the read somehow (no functionality like that yet).
        // If we close fd_, the read will be stuck forever (tried it out).
        // This is used for certificate reloading, so if this fails here, we will never update
        // the certificate, when we should. It's also used for expensive async operations while
        // handling HTTP requests and if we fail here those requests would hang forever. I think
        // the right thing to do here is exit.
        getLogger().log(LogSeverity::Fatal,
            "Error writing to eventfd: "
                + std::make_error_code(static_cast<std::errc>(errno)).message());
        std::exit(1);
    }
}
}
