#pragma once
#include "Publisher.h"
#include "canproxy/plugin.h"
#include <string>
#include <utility>
#include <vector>

namespace canproxy {

// Loads one plugin .so, checks its ABI, wires the host callbacks to a
// StateStore, and drives create/start/stop/destroy.
class PluginHost {
public:
    explicit PluginHost(StateStore &store);
    ~PluginHost();
    PluginHost(const PluginHost &) = delete;
    PluginHost &operator=(const PluginHost &) = delete;

    // Empty string on success, otherwise the error.
    std::string load(const std::string &path);
    std::string create(const std::string &vehicleIf,
                       const std::vector<std::pair<std::string, std::string>> &args);
    std::string start();
    void stop();
    void unload();

    const canproxy_plugin *descriptor() const { return m_plugin; }
    std::string name() const { return m_plugin ? m_plugin->name : ""; }

private:
    static void hostPublish(void *ctx, const canproxy_vehicle_state *s);
    static void hostSetLink(void *ctx, int present);
    static void hostLog(void *ctx, int level, const char *msg);

    StateStore &m_store;
    void *m_handle = nullptr;
    const canproxy_plugin *m_plugin = nullptr;
    void *m_instance = nullptr;
    bool m_started = false;
    canproxy_host m_host{};
};

} // namespace canproxy
