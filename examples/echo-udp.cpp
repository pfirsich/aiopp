#include "aiopp/ioqueue.hpp"
#include "aiopp/socket.hpp"

#include "spdlogger.hpp"

using namespace aiopp;

class Server {
public:
    Server(IoQueue& io, Fd&& socket)
        : io_(io)
        , socket_(std::move(socket))
    {
    }

    void start() { receive(); }

private:
    void receive()
    {
        receiveBuffer_.resize(1024, 0);
        io_.recvfrom(socket_, receiveBuffer_.data(), receiveBuffer_.size(), 0, &clientAddr_,
            [this](IoResult receivedBytes) {
                if (!receivedBytes) {
                    spdlog::error("Error in recvfrom: {}", receivedBytes.error().message());
                    receive();
                    return;
                }

                receiveBuffer_.resize(*receivedBytes);
                respond();
            });
    }

    void respond()
    {
        io_.sendto(socket_, receiveBuffer_.data(), receiveBuffer_.size(), 0, &clientAddr_,
            [this](IoResult sentBytes) {
                if (!sentBytes) {
                    spdlog::error("Error in sendto: {}", sentBytes.error().message());
                }
                receive();
            });
    }

    IoQueue& io_;
    Fd socket_;
    std::vector<uint8_t> receiveBuffer_;
    ::sockaddr_in clientAddr_;
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
