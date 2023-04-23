#include "spdlogger.hpp"

namespace {
spdlog::level::level_enum mapSeverity(aiopp::LogSeverity severity)
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
}

void SpdLogger::log(aiopp::LogSeverity severity, const std::string& message)
{
    spdlog::log(mapSeverity(severity), message);
}
