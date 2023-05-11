#include <arpa/inet.h>
#include <netinet/in.h>

#include <spdlog/spdlog.h>

#include "aiopp/ioqueue.hpp"
#include "aiopp/socket.hpp"

#include "spdlogger.hpp"

using namespace aiopp;

const std::string& getResponse()
{
    static const std::string body = "This is a short string that serves as a response";
    static std::string response;
    if (response.empty()) {
        response.reserve(512);
        response.append("HTTP/1.1 200 OK\r\n");
        response.append("Server: aiopp coro\r\n");
        response.append("Content-Type: text/plain\r\n");
        response.append("Content-Length: ");
        response.append(std::to_string(body.size()));
        response.append("\r\n");
        response.append("\r\n");
        response.append(body);
    }
    return response;
}

class Server {
public:
    Server(IoQueue& io, Fd listenSocket)
        : io_(io)
        , listenSocket_(std::move(listenSocket))
        , acceptTimeout_(std::chrono::milliseconds(5000))
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
            server_.io_.close(fd_, [](IoResult) {});
        }

        void receive(std::unique_ptr<Session> self)
        {
            buffer_.clear();
            buffer_.append(2048, '\0');
            server_.io_.recv(fd_, buffer_.data(), buffer_.size(),
                [this, self = std::move(self)](IoResult readBytes) mutable {
                    if (!readBytes) {
                        spdlog::error("Error in recv: {}", readBytes.error().message());
                        close();
                        return;
                    }

                    if (*readBytes == 0) {
                        close();
                        return;
                    }

                    buffer_.resize(*readBytes);
                    respond(std::move(self));
                });
        }

        void respond(std::unique_ptr<Session> self)
        {
            sendOffset_ = 0;
            buffer_ = getResponse();
            sendResponse(std::move(self));
        }

        void sendResponse(std::unique_ptr<Session> self)
        {
            assert(sendOffset_ < buffer_.size());
            server_.io_.send(fd_, buffer_.data() + sendOffset_, buffer_.size() - sendOffset_,
                [this, self = std::move(self)](IoResult sentBytes) mutable {
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
                    if (sendOffset_ + *sentBytes < buffer_.size()) {
                        sendOffset_ += *sentBytes;
                        sendResponse(std::move(self));
                        return;
                    }

                    receive(std::move(self));
                });
        }

        Server& server_;
        Fd fd_;
        std::string buffer_;
        size_t sendOffset_;
    };

    void accept()
    {
        bool added = false;
        while (!added) {
            auto op = io_.accept(listenSocket_, nullptr, nullptr, [this](IoResult result) {
                if (result.error().value() == ECANCELED) {
                    spdlog::info("Accept timed out");
                    return;
                }
                handleAccept(result);
                accept();
            });
            added = op.valid();
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
    IoQueue::Duration acceptTimeout_;
};

int main()
{
    setLogger(std::make_unique<SpdLogger>());

    auto socket = createTcpListenSocket(IpAddress::parse("0.0.0.0").value(), 4242);
    if (socket == -1) {
        return 1;
    }

    IoQueue io;
    Server server(io, std::move(socket));
    server.start();
    io.run();
}
