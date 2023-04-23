#pragma once

#include "aiopp/fd.hpp"
#include "aiopp/log.hpp"

aiopp::Fd createListenSocket();

class SpdLogger : public aiopp::LoggerBase {
public:
    void log(aiopp::LogSeverity severity, const std::string& message) override;
};
