#pragma once
#include "canproxy/plugin.h"

// One implementation, two plugins: emu-ev (battery-electric) and emu-hybrid
// (combustion plus battery). The difference is which J1979 PIDs are polled
// for the drivetrain and how rpm is sourced.
const canproxy_plugin *emuPluginDescriptor(bool hybrid);
