#include <iostream>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <spdlog/spdlog.h>

#include "ioqueue.hpp"

class Server {
public:
    Server(aiopp::IoQueue& io)
        : io_(io)
        , listenSocket_(::socket(AF_INET, SOCK_STREAM, 0))
    {
        if (listenSocket_ == -1) {
            spdlog::critical("Could not create listen socket: {}", errno);
            std::exit(1);
        }

        sockaddr_in addr;
        ::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(6942);

        const int reuse = 1;
        if (::setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
            spdlog::critical("Could not set sockopt SO_REUSEADDR");
            std::exit(1);
        }

        if (::bind(listenSocket_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == -1) {
            spdlog::critical("Could not bind socket: {}", errno);
            std::exit(1);
        }

        if (::listen(listenSocket_, SOMAXCONN) == -1) {
            spdlog::critical("Could not listen on socket: {}", errno);
            std::exit(1);
        }
    }

    void start()
    {
        accept();
    }

private:
    class Session {
    public:
        Session(Server& server, aiopp::Fd fd, std::string remoteAddr)
            : server_(server)
            , fd_(std::move(fd))
            , remoteAddr_(std::move(remoteAddr))
        {
        }

        void start(std::unique_ptr<Session> self)
        {
            readRequest(std::move(self));
        }

    private:
        void close()
        {
            server_.io_.close(fd_, [](std::error_code) {});
        }

        void readRequest(std::unique_ptr<Session> self)
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
                    processRequest(std::move(self), recvBuffer_);
                });
        }

        bool getKeepAlive(std::string_view request) const
        {
            return request.find("Connection: keep-alive") != std::string::npos;
        }

        void processRequest(std::unique_ptr<Session> self, std::string_view request)
        {
            const auto requestLine = request.substr(0, request.find(" HTTP/1.1\r\n"));
            if (requestLine == "GET /") {
                std::string response
                    = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
                    + std::to_string(remoteAddr_.size()) + "\r\n\r\n" + remoteAddr_;
                spdlog::info("{} GET /: 200", remoteAddr_);
                respond(std::move(self), std::move(response), getKeepAlive(request));
            } else {
                spdlog::info("{} {}: 400", remoteAddr_, requestLine);
                respond(std::move(self), "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n",
                    false);
            }
        }

        void respond(std::unique_ptr<Session> self, std::string response, bool keepAlive)
        {
            sendBuffer_ = std::move(response);
            sendOffset_ = 0;
            sendResponse(std::move(self), keepAlive);
        }

        void sendResponse(std::unique_ptr<Session> self, bool keepAlive)
        {
            assert(sendOffset_ < sendBuffer_.size());
            server_.io_.send(fd_, sendBuffer_.data() + sendOffset_,
                sendBuffer_.size() - sendOffset_,
                [this, self = std::move(self), keepAlive](
                    std::error_code ec, int sentBytes) mutable {
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
                    if (sendOffset_ + sentBytes < sendBuffer_.size()) {
                        sendOffset_ += sentBytes;
                        sendResponse(std::move(self), keepAlive);
                        return;
                    }

                    if (keepAlive) {
                        start(std::move(self));
                    } else {
                        close();
                    }
                });
        }

        Server& server_;
        aiopp::Fd fd_;
        std::string remoteAddr_;
        std::string recvBuffer_;
        std::string sendBuffer_;
        size_t sendOffset_;
    };

    void accept()
    {
        acceptAddrLen_ = sizeof(acceptAddr_);
        bool added = false;
        while (!added) {
            added = io_.accept(
                listenSocket_, &acceptAddr_, &acceptAddrLen_, [this](std::error_code ec, int fd) {
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

        auto session = std::make_unique<Session>(*this, fd, ::inet_ntoa(acceptAddr_.sin_addr));
        session->start(std::move(session));
    }

    aiopp::IoQueue& io_;
    aiopp::Fd listenSocket_;
    ::sockaddr_in acceptAddr_;
    ::socklen_t acceptAddrLen_;
};

class SpdLogger : public aiopp::LoggerBase {
public:
    void log(aiopp::LogSeverity severity, const std::string& message) override
    {
        spdlog::log(mapSeverity(severity), message);
    }

private:
    static spdlog::level::level_enum mapSeverity(aiopp::LogSeverity severity)
    {
        switch (severity) {
        case aiopp::LogSeverity::Debug:
            return spdlog::level::level_enum::debug;
        case aiopp::LogSeverity::Info:
            return spdlog::level::level_enum::info;
        case aiopp::LogSeverity::Warning:
            return spdlog::level::level_enum::warn;
        case aiopp::LogSeverity::Error:
            return spdlog::level::level_enum::err;
        case aiopp::LogSeverity::Fatal:
            return spdlog::level::level_enum::critical;
        default:
            std::abort();
        }
    }
};

int main()
{
    aiopp::setLogger(std::make_unique<SpdLogger>());
    aiopp::IoQueue io;
    Server server(io);
    server.start();
    io.run();
}
