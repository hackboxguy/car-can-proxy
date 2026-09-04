// Forza "Data Out" UDP telemetry: the Sled block (232 bytes) that every
// Forza title sends, optionally followed by the Dash block. Four packet
// sizes are known; all fields are little-endian.
//
//   232  Sled only (Forza Motorsport 7 "sled" setting)
//   311  Sled + Dash (Forza Motorsport 7 "dash")
//   324  Sled + 12 unknown bytes + Dash (Forza Horizon 4 / 5)
//   331  Sled + Dash + tire wear + track ordinal (Forza Motorsport 2023)
//
// Layout references: the CarCluster project (r00li/CarCluster,
// src/Games/ForzaHorizonGame.cpp) and the widely reproduced Turn 10 field
// list. Pure header; the plugin and its tests share it.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace forza {

struct Frame {
    bool raceOn = false;
    uint32_t timestampMs = 0;
    float engineMaxRpm = 0, engineIdleRpm = 0, engineRpm = 0;
    float speedMps = 0;          // dash Speed, or |velocity| from the sled
    bool hasDash = false;
    float powerW = 0, torqueNm = 0, boost = 0, fuel = 0, distanceM = 0;
    uint8_t accel = 0, brake = 0, clutch = 0, handbrake = 0, gear = 0;
    int8_t steer = 0;
    const char *format = "";
};

inline float f32(const uint8_t *p) { float v; std::memcpy(&v, p, 4); return v; }
inline int32_t s32(const uint8_t *p) { int32_t v; std::memcpy(&v, p, 4); return v; }
inline uint32_t u32(const uint8_t *p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

// Dash block offset for a packet size, or -1 when there is no dash block.
inline int dashOffset(size_t len)
{
    switch (len) {
    case 311: return 232;   // FM7 dash
    case 324: return 244;   // FH4/FH5
    case 331: return 232;   // FM 2023
    default:  return -1;
    }
}

inline bool parse(const uint8_t *d, size_t len, Frame &out)
{
    if (!d || (len != 232 && dashOffset(len) < 0))
        return false;
    out = Frame();
    out.raceOn = s32(d + 0) != 0;
    out.timestampMs = u32(d + 4);
    out.engineMaxRpm = f32(d + 8);
    out.engineIdleRpm = f32(d + 12);
    out.engineRpm = f32(d + 16);
    const float vx = f32(d + 32), vy = f32(d + 36), vz = f32(d + 40);
    out.speedMps = std::sqrt(vx * vx + vy * vy + vz * vz);
    const int base = dashOffset(len);
    if (base < 0) {
        out.format = "sled";
        return std::isfinite(out.engineRpm) && std::isfinite(out.speedMps);
    }
    const uint8_t *h = d + base;
    out.hasDash = true;
    out.speedMps = f32(h + 12);
    out.powerW = f32(h + 16);
    out.torqueNm = f32(h + 20);
    out.boost = f32(h + 40);
    out.fuel = f32(h + 44);
    out.distanceM = f32(h + 48);
    out.accel = h[71];
    out.brake = h[72];
    out.clutch = h[73];
    out.handbrake = h[74];
    out.gear = h[75];
    out.steer = static_cast<int8_t>(h[76]);
    out.format = len == 311 ? "fm7-dash" : len == 324 ? "fh4/fh5" : "fm2023";
    return std::isfinite(out.engineRpm) && std::isfinite(out.speedMps) && std::isfinite(out.powerW) &&
           std::isfinite(out.fuel) && out.fuel >= 0.0f && out.fuel <= 1.0f;
}

} // namespace forza
