#pragma once

#include "fd.hpp"
#include "log.hpp"

aiopp::Fd createListenSocket();

class SpdLogger : public aiopp::LoggerBase {
public:
    void log(aiopp::LogSeverity severity, const std::string& message) override;
};
