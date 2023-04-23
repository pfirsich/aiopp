#pragma once

namespace aiopp {
class Fd {
public:
    Fd();
    Fd(int fd);
    Fd(Fd&& other);
    Fd(const Fd& other) = delete;
    ~Fd();

    Fd& operator=(const Fd& other) = delete;
    Fd& operator=(Fd&& other);

    operator int() const;

    void close();
    void reset(int fd = -1); // close current fd and set new one
    int release(); // return the fd without closing

private:
    int fd_ = -1;
};

struct Pipe {
    Fd read;
    Fd write;

    Pipe();

    void close();
};
}
