#pragma once
#include <string>

namespace canproxy {

enum class LogLevel { Error = 0, Warn, Info, Debug };

void setLogLevel(LogLevel level);
LogLevel logLevel();
bool parseLogLevel(const std::string &name, LogLevel &out);

// Single line to stderr, journald-friendly ("<level> tag: message").
void log(LogLevel level, const std::string &tag, const std::string &message);

} // namespace canproxy

#define CANPROXY_LOG(level, tag, msg) ::canproxy::log(::canproxy::LogLevel::level, tag, msg)
