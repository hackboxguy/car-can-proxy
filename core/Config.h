#pragma once
#include "Log.h"
#include <string>
#include <utility>
#include <vector>

namespace canproxy {

struct Config {
    std::string contractIf;                       // required
    std::string vehicleIf;                        // optional, passed to plugin
    std::string plugin;                           // name or path ending in .so
    std::string pluginDir;                        // where names resolve
    std::vector<std::pair<std::string, std::string>> pluginArgs;
    unsigned pluginTimeoutMs = 1000;              // silence before "degraded"
    std::string recordPath;                       // vehicle-side candump log
    LogLevel logLevel = LogLevel::Info;
    bool help = false;

    // Returns empty string on success, otherwise the error to print.
    static std::string parse(int argc, char **argv, Config &out);
    static std::string usage(const std::string &prog);
    std::string resolvePluginPath() const;
};

} // namespace canproxy
