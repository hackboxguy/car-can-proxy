#include "Config.h"
#include <cstdlib>

namespace canproxy {

#ifndef CANPROXY_DEFAULT_PLUGIN_DIR
#define CANPROXY_DEFAULT_PLUGIN_DIR "/usr/local/lib/can-proxy/plugins"
#endif

std::string Config::usage(const std::string &prog)
{
    return "Usage: " + prog + " --contract-if=<if> --plugin=<name|/path.so> [options]\n"
           "\n"
           "  --contract-if=<if>        CAN interface to publish the contract on (e.g. vcan0)\n"
           "  --vehicle-if=<if>         CAN interface the plugin reads the vehicle from\n"
           "  --plugin=<name|path>      plugin name (resolved in --plugin-dir) or path to a .so\n"
           "  --plugin-dir=<dir>        default " CANPROXY_DEFAULT_PLUGIN_DIR "\n"
           "  --plugin-arg key=value    passed to the plugin, repeatable\n"
           "  --plugin-timeout-ms=<n>   plugin silence before signals go unknown (default 1000)\n"
           "  --log-level=<level>       error|warn|info|debug (default info)\n"
           "  --help\n";
}

static bool takeValue(const std::string &arg, const std::string &key, std::string &value,
                      int &i, int argc, char **argv)
{
    if (arg.compare(0, key.size() + 1, key + "=") == 0) {
        value = arg.substr(key.size() + 1);
        return true;
    }
    if (arg == key && i + 1 < argc) {
        value = argv[++i];
        return true;
    }
    return false;
}

std::string Config::parse(int argc, char **argv, Config &out)
{
    out.pluginDir = CANPROXY_DEFAULT_PLUGIN_DIR;
    if (const char *env = std::getenv("CANPROXY_PLUGIN_DIR"))
        out.pluginDir = env;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i], v;
        if (arg == "--help" || arg == "-h") { out.help = true; return ""; }
        if (takeValue(arg, "--contract-if", v, i, argc, argv)) { out.contractIf = v; continue; }
        if (takeValue(arg, "--vehicle-if", v, i, argc, argv))  { out.vehicleIf = v; continue; }
        if (takeValue(arg, "--plugin", v, i, argc, argv))      { out.plugin = v; continue; }
        if (takeValue(arg, "--plugin-dir", v, i, argc, argv))  { out.pluginDir = v; continue; }
        if (takeValue(arg, "--plugin-arg", v, i, argc, argv)) {
            auto eq = v.find('=');
            if (eq == std::string::npos || eq == 0)
                return "--plugin-arg expects key=value, got '" + v + "'";
            out.pluginArgs.emplace_back(v.substr(0, eq), v.substr(eq + 1));
            continue;
        }
        if (takeValue(arg, "--plugin-timeout-ms", v, i, argc, argv)) {
            char *end = nullptr;
            long n = std::strtol(v.c_str(), &end, 10);
            if (!end || *end || n <= 0) return "--plugin-timeout-ms expects a positive integer";
            out.pluginTimeoutMs = static_cast<unsigned>(n);
            continue;
        }
        if (takeValue(arg, "--log-level", v, i, argc, argv)) {
            if (!parseLogLevel(v, out.logLevel)) return "unknown log level '" + v + "'";
            continue;
        }
        return "unknown argument '" + arg + "'";
    }
    if (out.contractIf.empty()) return "--contract-if is required";
    if (out.plugin.empty()) return "--plugin is required";
    return "";
}

std::string Config::resolvePluginPath() const
{
    const bool isPath = plugin.find('/') != std::string::npos;
    const bool isSo = plugin.size() > 3 && plugin.compare(plugin.size() - 3, 3, ".so") == 0;
    if (isPath || isSo)
        return plugin;
    return pluginDir + "/" + plugin + ".so";
}

} // namespace canproxy
