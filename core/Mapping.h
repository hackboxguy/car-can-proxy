#pragma once
// The only place where the plugin ABI (canproxy/plugin.h) meets the wire
// contract (can_proxy_contract.h). Everything here is a pure function and
// every mapping is pinned by core/tests/mapping_tests.cpp.
#include "canproxy/plugin.h"
#include "can_proxy_contract.h"
#include <cstdint>

namespace canproxy {

// ABI signal index -> contract capability bit (0 if the signal has no bit of
// its own; pack voltage/current share one, the five assist signals share one).
uint32_t capabilityBitFor(canproxy_signal sig);
uint32_t capabilitiesFrom(uint32_t capableMask);

// ABI lamp index -> contract telltale bit.
uint32_t telltaleBitFor(canproxy_lamp lamp);
uint32_t telltalesFrom(uint32_t lampsMask);

// Enumerations are numerically identical by construction; these make the
// dependency explicit and let a test catch drift.
uint8_t gearFrom(int abiGear);
uint8_t powerStateFrom(int abiPowerState);
uint8_t chargingStateFrom(int abiCharging);
uint8_t drivetrainFrom(int abiDrivetrain);
uint8_t sourceKindFrom(int abiSource);
uint8_t collisionRiskFrom(int abiRisk);

// Fill contract frames from a vehicle state. `live` false means "publish
// everything as SNA" (no vehicle, plugin silent); capabilities and identity
// are still taken from the state so the consumer's layout holds.
struct Frames {
    canproxy_status_t    status;
    canproxy_identity_t  identity;
    canproxy_motion_t    motion;
    canproxy_edrive_t    edrive;
    canproxy_telltales_t telltales;
    canproxy_energy_t    energy;
    canproxy_trip_t      trip;
    canproxy_thermal_t   thermal;
    canproxy_assist_t    assist;
};

void framesFrom(const canproxy_vehicle_state &s, bool live, uint8_t proxyState, uint8_t counter,
                uint8_t pluginMajor, uint8_t pluginMinor, Frames &out);

} // namespace canproxy
