#include "Log.h"
#include <atomic>
#include <cstdio>
#include <mutex>

namespace canproxy {

static std::atomic<int> g_level{static_cast<int>(LogLevel::Info)};
static std::mutex g_mutex;

void setLogLevel(LogLevel level) { g_level = static_cast<int>(level); }
LogLevel logLevel() { return static_cast<LogLevel>(g_level.load()); }

bool parseLogLevel(const std::string &name, LogLevel &out)
{
    if (name == "error") { out = LogLevel::Error; return true; }
    if (name == "warn")  { out = LogLevel::Warn;  return true; }
    if (name == "info")  { out = LogLevel::Info;  return true; }
    if (name == "debug") { out = LogLevel::Debug; return true; }
    return false;
}

void log(LogLevel level, const std::string &tag, const std::string &message)
{
    if (static_cast<int>(level) > g_level.load())
        return;
    static const char *names[] = { "error", "warn", "info", "debug" };
    std::lock_guard<std::mutex> lock(g_mutex);
    std::fprintf(stderr, "%s %s: %s\n", names[static_cast<int>(level)], tag.c_str(), message.c_str());
    std::fflush(stderr);
}

} // namespace canproxy
