// Decoders for the emulator's reference EV profile (docs/emulator-ev-profile.md).
// Pure functions over the record bytes of each DID.
#pragma once
#include "obd/Uds.h"
#include <cstdint>
#include <vector>

namespace emu {

constexpr uint32_t kBmsRequestId = 0x7E4;
constexpr uint32_t kBmsResponseId = 0x7EC;
constexpr uint16_t kDidPack = 0x0101;
constexpr uint16_t kDidRange = 0x0102;
constexpr uint16_t kDidDrive = 0x0103;
constexpr uint16_t kDidAssist = 0x0104;

struct Pack { double voltageV; double currentA; double socPct; double sohPct; int charging; };
struct Range { int rangeKm; int consumptionWhKm; double odometerKm; };
struct Drive { int gear; int powerState; int motorRpm; double motorPowerKw; };
struct Assist { int ecoScore; int speedLimitKmh; int collisionRisk; unsigned laneState; double leadGapM; };

inline bool decodePack(const std::vector<uint8_t> &d, Pack &out)
{
    if (d.size() < 7) return false;
    out.voltageV = obd::be16(&d[0]) / 10.0;
    out.currentA = obd::be16s(&d[2]) / 10.0;
    out.socPct = d[4] / 2.0;
    out.sohPct = d[5] / 2.0;
    out.charging = d[6];
    return out.charging <= 3;
}

inline bool decodeRange(const std::vector<uint8_t> &d, Range &out)
{
    if (d.size() < 8) return false;
    out.rangeKm = obd::be16(&d[0]);
    out.consumptionWhKm = obd::be16s(&d[2]);
    out.odometerKm = obd::be32(&d[4]) / 10.0;
    return true;
}

inline bool decodeDrive(const std::vector<uint8_t> &d, Drive &out)
{
    if (d.size() < 6) return false;
    out.gear = d[0];
    out.powerState = d[1];
    out.motorRpm = obd::be16(&d[2]);
    out.motorPowerKw = obd::be16s(&d[4]) / 10.0;
    return out.gear <= 4 && out.powerState <= 3;
}

inline bool decodeAssist(const std::vector<uint8_t> &d, Assist &out)
{
    if (d.size() < 6) return false;
    out.ecoScore = d[0];
    out.speedLimitKmh = d[1];
    out.collisionRisk = d[2];
    out.laneState = d[3] & 0x0F;
    out.leadGapM = obd::be16(&d[4]) / 10.0;
    return out.ecoScore <= 100 && out.collisionRisk <= 3;
}

} // namespace emu
