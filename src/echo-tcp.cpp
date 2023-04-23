#include <arpa/inet.h>
#include <netinet/in.h>

#include <spdlog/spdlog.h>

#include "aiopp/ioqueue.hpp"
#include "aiopp/socket.hpp"

#include "spdlogger.hpp"

using namespace aiopp;

class Server {
public:
    Server(IoQueue& io, Fd listenSocket)
        : io_(io)
        , listenSocket_(std::move(listenSocket))
    {
    }

    void start() { accept(); }

private:
    class Session {
    public:
        Session(Server& server, Fd fd)
            : server_(server)
            , fd_(std::move(fd))
        {
        }

        void start(std::unique_ptr<Session> self) { receive(std::move(self)); }

    private:
        void close()
        {
            server_.io_.close(fd_, [](std::error_code) {});
        }

        void receive(std::unique_ptr<Session> self)
        {
            recvBuffer_.clear();
            recvBuffer_.append(2048, '\0');
            server_.io_.recv(fd_, recvBuffer_.data(), recvBuffer_.size(),
                [this, self = std::move(self)](std::error_code ec, int readBytes) mutable {
                    if (ec) {
                        spdlog::error("Error in recv: {}", ec.message());
                        close();
                        return;
                    }

                    if (readBytes == 0) {
                        close();
                        return;
                    }

                    recvBuffer_.resize(readBytes);
                    respond(std::move(self));
                });
        }

        void respond(std::unique_ptr<Session> self)
        {
            sendOffset_ = 0;
            sendResponse(std::move(self));
        }

        void sendResponse(std::unique_ptr<Session> self)
        {
            assert(sendOffset_ < recvBuffer_.size());
            server_.io_.send(fd_, recvBuffer_.data() + sendOffset_,
                recvBuffer_.size() - sendOffset_,
                [this, self = std::move(self)](std::error_code ec, int sentBytes) mutable {
                    if (ec) {
                        spdlog::error("Error in send: {}", ec.message());
                        close();
                        return;
                    }

                    if (sentBytes == 0) {
                        close();
                        return;
                    }

                    assert(sentBytes > 0);
                    if (sendOffset_ + sentBytes < recvBuffer_.size()) {
                        sendOffset_ += sentBytes;
                        sendResponse(std::move(self));
                        return;
                    }

                    receive(std::move(self));
                });
        }

        Server& server_;
        Fd fd_;
        std::string recvBuffer_;
        size_t sendOffset_;
    };

    void accept()
    {
        bool added = false;
        while (!added) {
            added = io_.accept(listenSocket_, nullptr, nullptr, [this](std::error_code ec, int fd) {
                handleAccept(ec, fd);
                accept();
            });
        }
    }

    void handleAccept(std::error_code ec, int fd)
    {
        if (ec) {
            spdlog::error("Error in accept: {}", ec.message());
            return;
        }

        auto session = std::make_unique<Session>(*this, fd);
        session->start(std::move(session));
    }

    IoQueue& io_;
    Fd listenSocket_;
};

int main()
{
    auto socket = createTcpListenSocket(IpAddress::parse("0.0.0.0").value(), 4242);
    if (socket == -1) {
        return 1;
    }

    setLogger(std::make_unique<SpdLogger>());
    IoQueue io;
    Server server(io, std::move(socket));
    server.start();
    io.run();
}
