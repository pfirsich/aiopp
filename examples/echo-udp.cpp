#include "aiopp/ioqueue.hpp"
#include "aiopp/socket.hpp"

#include "spdlogger.hpp"

using namespace aiopp;

class Server {
public:
    Server(IoQueue& io, Fd&& socket)
        : io_(io)
        , socket_(std::move(socket))
        , receiveBuffer_(1024, 0)
        // msg_ can stay like this
        , msg_ {
            .msg_name = &clientAddr_,
            .msg_namelen = sizeof(clientAddr_),
            .msg_iov = &iov_,
            .msg_iovlen = 1,
            .msg_control = nullptr,
            .msg_controllen = 0,
            .msg_flags = 0,
        }
    {
    }

    void start() { receive(); }

private:
    void receive()
    {
        receiveBuffer_.resize(1024, 0);
        iov_ = ::iovec { receiveBuffer_.data(), receiveBuffer_.size() };
        io_.recvmsg(socket_, &msg_, 0, [this](std::error_code ec, int receivedBytes) {
            if (ec) {
                spdlog::error("Error in recvmsg: {}", ec.message());
                receive();
                return;
            }

            receiveBuffer_.resize(receivedBytes);
            respond();
        });
    }

    void respond()
    {
        iov_ = ::iovec { receiveBuffer_.data(), receiveBuffer_.size() };
        io_.sendmsg(socket_, &msg_, 0, [this](std::error_code ec, int /*sentBytes*/) {
            if (ec) {
                spdlog::error("Error in sendmsg: {}", ec.message());
            }
            receive();
        });
    }

    IoQueue& io_;
    Fd socket_;
    std::vector<uint8_t> receiveBuffer_;
    ::sockaddr_in clientAddr_;
    ::iovec iov_;
    ::msghdr msg_;
};

int main()
{
    setLogger(std::make_unique<SpdLogger>());

    auto socket = createSocket(SocketType::Udp, IpAddress::parse("0.0.0.0").value(), 4242);
    if (socket == -1) {
        return 1;
    }

    IoQueue io;
    Server server(io, std::move(socket));
    server.start();
    io.run();
    return 0;
}
