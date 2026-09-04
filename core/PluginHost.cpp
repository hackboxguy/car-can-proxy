#include "PluginHost.h"
#include "Log.h"
#include <dlfcn.h>
#include <vector>

namespace canproxy {

PluginHost::PluginHost(StateStore &store) : m_store(store)
{
    m_host.ctx = this;
    m_host.publish = &PluginHost::hostPublish;
    m_host.set_link = &PluginHost::hostSetLink;
    m_host.log = &PluginHost::hostLog;
}

PluginHost::~PluginHost() { unload(); }

std::string PluginHost::load(const std::string &path)
{
    unload();
    m_handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m_handle)
        return std::string("dlopen: ") + ::dlerror();
    auto entry = reinterpret_cast<canproxy_plugin_entry_fn>(::dlsym(m_handle, CANPROXY_PLUGIN_ENTRY_SYMBOL));
    if (!entry) {
        std::string err = path + ": no " CANPROXY_PLUGIN_ENTRY_SYMBOL " symbol";
        unload();
        return err;
    }
    const canproxy_plugin *p = entry();
    if (!p) { unload(); return path + ": entry returned NULL"; }
    if (p->abi_version != CANPROXY_PLUGIN_ABI_VERSION) {
        std::string err = path + ": ABI version " + std::to_string(p->abi_version) +
                          ", host expects " + std::to_string(CANPROXY_PLUGIN_ABI_VERSION);
        unload();
        return err;
    }
    if (!p->name || !p->create || !p->start || !p->stop || !p->destroy) {
        unload();
        return path + ": incomplete plugin descriptor";
    }
    m_plugin = p;
    CANPROXY_LOG(Info, "plugin", std::string("loaded ") + p->name + " v" +
                 std::to_string(p->version_major) + "." + std::to_string(p->version_minor) + " from " + path);
    return "";
}

std::string PluginHost::create(const std::string &vehicleIf,
                               const std::vector<std::pair<std::string, std::string>> &args)
{
    if (!m_plugin) return "no plugin loaded";
    if (m_instance) return "plugin already created";
    std::vector<canproxy_kv> kvs;
    kvs.reserve(args.size());
    for (const auto &a : args)
        kvs.push_back({ a.first.c_str(), a.second.c_str() });
    canproxy_plugin_args pa;
    pa.vehicle_if = vehicleIf.empty() ? nullptr : vehicleIf.c_str();
    pa.args = kvs.empty() ? nullptr : kvs.data();
    pa.nargs = kvs.size();
    m_instance = m_plugin->create(&pa, &m_host);
    if (!m_instance)
        return std::string(m_plugin->name) + ": create() failed (see plugin log)";
    return "";
}

std::string PluginHost::start()
{
    if (!m_instance) return "plugin not created";
    if (m_started) return "";
    int rc = m_plugin->start(m_instance);
    if (rc != 0)
        return std::string(m_plugin->name) + ": start() returned " + std::to_string(rc);
    m_started = true;
    return "";
}

void PluginHost::stop()
{
    if (m_instance && m_started) {
        m_plugin->stop(m_instance);
        m_started = false;
    }
}

void PluginHost::unload()
{
    stop();
    if (m_instance) {
        m_plugin->destroy(m_instance);
        m_instance = nullptr;
    }
    m_plugin = nullptr;
    if (m_handle) {
        ::dlclose(m_handle);
        m_handle = nullptr;
    }
}

void PluginHost::hostPublish(void *ctx, const canproxy_vehicle_state *s)
{
    if (!ctx || !s) return;
    static_cast<PluginHost *>(ctx)->m_store.publish(*s, monotonicNow());
}

void PluginHost::hostSetLink(void *ctx, int present)
{
    if (!ctx) return;
    static_cast<PluginHost *>(ctx)->m_store.setLink(present != 0, monotonicNow());
}

void PluginHost::hostLog(void *ctx, int level, const char *msg)
{
    auto *self = static_cast<PluginHost *>(ctx);
    LogLevel lv = LogLevel::Info;
    switch (level) {
    case CANPROXY_LOG_ERROR: lv = LogLevel::Error; break;
    case CANPROXY_LOG_WARN:  lv = LogLevel::Warn;  break;
    case CANPROXY_LOG_INFO:  lv = LogLevel::Info;  break;
    default:                 lv = LogLevel::Debug; break;
    }
    log(lv, self && self->m_plugin ? self->m_plugin->name : "plugin", msg ? msg : "");
}

} // namespace canproxy
