// Minimal plugin for PluginHost tests. Publishes once on start, reports link.
#include "canproxy/plugin.h"
#include <cstring>

namespace {
struct Stub { canproxy_host host; int started = 0; };

void *create(const canproxy_plugin_args *args, const canproxy_host *host)
{
    if (canproxy_arg(args, "fail_create")) { host->log(host->ctx, CANPROXY_LOG_ERROR, "asked to fail"); return nullptr; }
    auto *s = new Stub; s->host = *host; return s;
}
int start(void *self)
{
    auto *s = static_cast<Stub *>(self);
    s->started++;
    s->host.set_link(s->host.ctx, 1);
    canproxy_vehicle_state st; std::memset(&st, 0, sizeof st);
    st.capable = st.valid = CANPROXY_SIG_BIT(CANPROXY_SIG_SPEED);
    st.speed_kmh = 42; st.drivetrain = CP_DRIVETRAIN_ICE; st.source = CP_SOURCE_EMULATOR;
    s->host.publish(s->host.ctx, &st);
    s->host.log(s->host.ctx, CANPROXY_LOG_INFO, "stub started");
    return 0;
}
void stop(void *) {}
void destroy(void *self) { delete static_cast<Stub *>(self); }

const canproxy_plugin kPlugin = {
#ifdef STUB_BAD_ABI
    CANPROXY_PLUGIN_ABI_VERSION + 100,
#else
    CANPROXY_PLUGIN_ABI_VERSION,
#endif
    "stub", 9, 9, create, start, stop, destroy,
};
}

#ifndef STUB_NO_ENTRY
extern "C" const canproxy_plugin *canproxy_plugin_entry(void) { return &kPlugin; }
#else
extern "C" const canproxy_plugin *not_the_entry(void) { return &kPlugin; }
#endif
