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
            server_.io_.close(fd_).callback([](IoResult) {});
        }

        void receive(std::unique_ptr<Session> self)
        {
            recvBuffer_.clear();
            recvBuffer_.append(2048, '\0');
            server_.io_.recv(fd_, recvBuffer_.data(), recvBuffer_.size())
                .callback([this, self = std::move(self)](IoResult readBytes) mutable {
                    if (!readBytes) {
                        spdlog::error("Error in recv: {}", readBytes.error().message());
                        close();
                        return;
                    }

                    if (*readBytes == 0) {
                        close();
                        return;
                    }

                    recvBuffer_.resize(*readBytes);
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
            server_.io_
                .send(fd_, recvBuffer_.data() + sendOffset_, recvBuffer_.size() - sendOffset_)
                .callback([this, self = std::move(self)](IoResult sentBytes) mutable {
                    if (!sentBytes) {
                        spdlog::error("Error in send: {}", sentBytes.error().message());
                        close();
                        return;
                    }

                    if (*sentBytes == 0) {
                        close();
                        return;
                    }

                    assert(*sentBytes > 0);
                    if (sendOffset_ + *sentBytes < recvBuffer_.size()) {
                        sendOffset_ += *sentBytes;
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
            auto op = io_.accept(listenSocket_, nullptr, nullptr);
            added = op.valid();
            op.callback([this](IoResult result) {
                handleAccept(result);
                accept();
            });
        }
    }

    void handleAccept(IoResult fd)
    {
        if (!fd) {
            spdlog::error("Error in accept: {}", fd.error().message());
            return;
        }

        auto session = std::make_unique<Session>(*this, *fd);
        session->start(std::move(session));
    }

    IoQueue& io_;
    Fd listenSocket_;
};

int main()
{
    setLogger(std::make_unique<SpdLogger>());

    auto socket = createTcpListenSocket(IpAddressPort::parse("0.0.0.0:4242").value());
    if (socket == -1) {
        return 1;
    }

    IoQueue io;
    Server server(io, std::move(socket));
    server.start();
    io.run();
}
