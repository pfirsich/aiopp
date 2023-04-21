#include "log.hpp"

#include <unistd.h>

namespace aiopp {
namespace {
    int write(int fd, std::string_view str)
    {
        return ::write(fd, str.data(), str.size());
    }

    std::unique_ptr<LoggerBase>& loggerStorage()
    {
        static std::unique_ptr<LoggerBase> logger = std::make_unique<FdLogger>(STDERR_FILENO);
        return logger;
    }
}

std::string_view toString(LogSeverity severity)
{
    switch (severity) {
    case LogSeverity::Debug:
        return "DEBUG";
    case LogSeverity::Info:
        return "INFO";
    case LogSeverity::Warning:
        return "WARNING";
    case LogSeverity::Error:
        return "ERROR";
    case LogSeverity::Fatal:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

FdLogger::FdLogger(int fd)
    : fd_(fd)
{
}

void FdLogger::log(LogSeverity severity, const std::string& message)
{
    write(fd_, "[");
    write(fd_, toString(severity));
    write(fd_, "] ");
    write(fd_, message);
    if (message.size() > 0 && message.back() != '\n') {
        write(fd_, "\n");
    }
}

void setLogger(std::unique_ptr<LoggerBase> logger)
{
    loggerStorage() = std::move(logger);
}

LoggerBase& getLogger()
{
    return *loggerStorage();
}
}
