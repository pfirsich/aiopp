#include "aiopp/fd.hpp"

#include <cassert>
#include <unistd.h>

namespace aiopp {
Fd::Fd()
    : fd_(-1)
{
}

Fd::Fd(int fd)
    : fd_(fd)
{
}

Fd::Fd(Fd&& other)
    : fd_(other.release())
{
}

Fd::~Fd()
{
    close();
}

Fd& Fd::operator=(Fd&& other)
{
    fd_ = other.release();
    return *this;
}

Fd::operator int() const
{
    return fd_;
}

void Fd::close()
{
    if (fd_ != -1)
        ::close(fd_);
    fd_ = -1;
}

void Fd::reset(int fd)
{
    close();
    fd_ = fd;
}

int Fd::release()
{
    const int fd = fd_;
    fd_ = -1;
    return fd;
}

Pipe::Pipe()
{
    int fds[2];
    assert(pipe(fds) != -1);
    read.reset(fds[0]);
    write.reset(fds[1]);
}

void Pipe::close()
{
    read.close();
    write.close();
}
}
