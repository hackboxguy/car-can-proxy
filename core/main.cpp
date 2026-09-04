// can-proxyd: loads one vehicle plugin, publishes the contract on a CAN
// interface. See docs/prd.md §6.
#include "CanSocket.h"
#include "Config.h"
#include "Log.h"
#include "PluginHost.h"
#include "Publisher.h"
#include <atomic>
#include <csignal>
#include <cstdio>
#include <ctime>

using namespace canproxy;

static std::atomic<bool> g_stop{false};
static void onSignal(int) { g_stop = true; }

int main(int argc, char **argv)
{
    Config cfg;
    const std::string err = Config::parse(argc, argv, cfg);
    if (cfg.help) {
        std::fputs(Config::usage(argv[0]).c_str(), stdout);
        return 0;
    }
    if (!err.empty()) {
        std::fprintf(stderr, "%s\n\n%s", err.c_str(), Config::usage(argv[0]).c_str());
        return 2;
    }
    setLogLevel(cfg.logLevel);

    CanSocket bus;
    if (const std::string e = bus.open(cfg.contractIf); !e.empty()) {
        CANPROXY_LOG(Error, "contract", e);
        return 1;
    }
    CANPROXY_LOG(Info, "contract", "publishing v" + std::to_string(CANPROXY_CONTRACT_MAJOR) + "." +
                 std::to_string(CANPROXY_CONTRACT_MINOR) + " on " + cfg.contractIf);

    StateStore store;
    PluginHost host(store);
    const std::string path = cfg.resolvePluginPath();
    if (const std::string e = host.load(path); !e.empty()) { CANPROXY_LOG(Error, "plugin", e); return 1; }
    if (const std::string e = host.create(cfg.vehicleIf, cfg.pluginArgs); !e.empty()) { CANPROXY_LOG(Error, "plugin", e); return 1; }

    // The heartbeat starts before the plugin so a consumer sees "starting"
    // rather than nothing while the plugin brings up its source.
    Publisher pub(store,
                  [&bus](uint32_t id, const uint8_t d[8]) {
                      if (!bus.send(id, d, CANPROXY_FRAME_DLC))
                          CANPROXY_LOG(Warn, "contract", "send failed for 0x" + std::to_string(id));
                  },
                  monotonicNow,
                  static_cast<Nanos>(cfg.pluginTimeoutMs) * 1000000LL,
                  host.descriptor()->version_major, host.descriptor()->version_minor);
    pub.start();

    if (const std::string e = host.start(); !e.empty()) {
        CANPROXY_LOG(Error, "plugin", e);
        pub.stop();
        return 1;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    while (!g_stop.load()) {
        struct timespec ts = { 0, 100000000L };
        nanosleep(&ts, nullptr);
    }
    CANPROXY_LOG(Info, "main", "shutting down");
    host.stop();
    pub.stop();
    host.unload();
    return 0;
}
