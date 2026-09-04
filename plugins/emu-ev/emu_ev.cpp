#include "EmuPlugin.h"
extern "C" const canproxy_plugin *canproxy_plugin_entry(void) { return emuPluginDescriptor(false); }
