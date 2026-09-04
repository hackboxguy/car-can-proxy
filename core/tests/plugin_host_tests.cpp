#include "PluginHost.h"
#include "test_support.h"
#include <cstdlib>
#include <string>

using namespace canproxy;

int main()
{
    const char *dir = std::getenv("STUB_DIR");
    if (!dir) { std::fprintf(stderr, "STUB_DIR not set\n"); return 2; }
    const std::string base = std::string(dir) + "/";

    StateStore store;
    {
        PluginHost h(store);
        CHECK(!h.load(base + "does_not_exist.so").empty());
        CHECK(h.load(base + "stub_plugin_noentry.so").find("no canproxy_plugin_entry") != std::string::npos);
        CHECK(h.load(base + "stub_plugin_badabi.so").find("ABI version") != std::string::npos);
        CHECK(h.descriptor() == nullptr);
    }
    {
        PluginHost h(store);
        CHECK(h.load(base + "stub_plugin.so").empty());
        CHECK(h.name() == "stub");
        CHECK(h.descriptor()->version_major == 9);
        CHECK(!h.start().empty());                     // not created yet
        CHECK(!h.create("", { { "fail_create", "1" } }).empty());
        CHECK(h.create("vcan9", {}).empty());
        CHECK(!h.create("vcan9", {}).empty());         // twice is an error
        auto before = store.snapshot();
        CHECK(!before.linkKnown && !before.everPublished);
        CHECK(h.start().empty());
        CHECK(h.start().empty());                      // idempotent
        auto after = store.snapshot();
        CHECK(after.linkKnown && after.linkPresent && after.everPublished);
        CHECK(after.state.speed_kmh == 42 && after.state.drivetrain == CP_DRIVETRAIN_ICE);
        h.stop();
        h.stop();
        h.unload();
        CHECK(h.descriptor() == nullptr);
    }
    return REPORT("plugin_host_tests");
}
