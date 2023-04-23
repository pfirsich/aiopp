#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace aiopp {
enum class LogSeverity { Debug, Info, Warning, Error, Fatal };

std::string_view toString(LogSeverity severity);

class LoggerBase {
public:
    virtual ~LoggerBase() = default;
    virtual void log(LogSeverity severity, const std::string& message) = 0;
};

class FdLogger : public LoggerBase {
public:
    FdLogger(int fd);
    void log(LogSeverity severity, const std::string& message) override;

private:
    int fd_ = -1;
};

void setLogger(std::unique_ptr<LoggerBase> logger);
LoggerBase& getLogger();
}
