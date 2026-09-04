#include "obd/J1979.h"
#include <cstring>

namespace obd {

void buildRequest(uint8_t mode, uint8_t pid, uint8_t out[8])
{
    std::memset(out, 0, 8);
    out[0] = 0x02;
    out[1] = mode;
    out[2] = pid;
}

std::optional<Response> parseSingleFrame(const uint8_t *frame, unsigned dlc)
{
    if (!frame || dlc < 3)
        return std::nullopt;
    const uint8_t pci = frame[0];
    if ((pci & 0xF0) != 0x00)          // not a single frame (FF/CF/FC)
        return std::nullopt;
    const int len = pci & 0x0F;        // bytes after the PCI byte
    if (len < 2 || static_cast<unsigned>(len) + 1 > dlc || len > 7)
        return std::nullopt;
    Response r;
    r.mode = frame[1];
    if (r.mode == 0x7F)                // negative response
        return std::nullopt;
    r.pid = frame[2];
    r.len = len - 2;
    std::memcpy(r.data, &frame[3], static_cast<size_t>(r.len));
    return r;
}

int payloadLength(uint8_t pid)
{
    switch (pid) {
    case PID_ENGINE_LOAD:
    case PID_COOLANT_TEMP:
    case PID_INTAKE_TEMP:
    case PID_SPEED:
    case PID_FUEL_LEVEL:
    case PID_AMBIENT_TEMP:
    case PID_HYBRID_BATTERY:
        return 1;
    case PID_RPM:
    case PID_MAF:
    case PID_MODULE_VOLTAGE:
        return 2;
    case PID_ODOMETER:
    case PID_SUPPORTED_00: case PID_SUPPORTED_20: case PID_SUPPORTED_40:
    case PID_SUPPORTED_60: case PID_SUPPORTED_80: case PID_SUPPORTED_A0:
    case PID_SUPPORTED_C0:
        return 4;
    default:
        return 0;
    }
}

std::vector<uint8_t> supportedPidsFrom(uint8_t basePid, const uint8_t abcd[4])
{
    std::vector<uint8_t> pids;
    const uint32_t word = (uint32_t(abcd[0]) << 24) | (uint32_t(abcd[1]) << 16) |
                          (uint32_t(abcd[2]) << 8) | uint32_t(abcd[3]);
    for (int n = 1; n <= 32; n++)
        if (word & (1u << (32 - n)))
            pids.push_back(static_cast<uint8_t>(basePid + n));
    return pids;
}

bool nextBlockAdvertised(const uint8_t abcd[4])
{
    return (abcd[3] & 0x01) != 0;
}

std::optional<double> decode(uint8_t pid, const uint8_t *p, int len)
{
    if (!p || len < payloadLength(pid) || payloadLength(pid) == 0)
        return std::nullopt;
    switch (pid) {
    case PID_ENGINE_LOAD:    return p[0] * 100.0 / 255.0;
    case PID_COOLANT_TEMP:   return double(p[0]) - 40.0;
    case PID_INTAKE_TEMP:    return double(p[0]) - 40.0;
    case PID_RPM:            return (p[0] * 256.0 + p[1]) / 4.0;
    case PID_SPEED:          return double(p[0]);
    case PID_MAF:            return (p[0] * 256.0 + p[1]) / 100.0;
    case PID_FUEL_LEVEL:     return p[0] * 100.0 / 255.0;
    case PID_MODULE_VOLTAGE: return (p[0] * 256.0 + p[1]) / 1000.0;
    case PID_AMBIENT_TEMP:   return double(p[0]) - 40.0;
    case PID_HYBRID_BATTERY: return p[0] * 100.0 / 255.0;
    case PID_ODOMETER:       return ((uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                                     (uint32_t(p[2]) << 8) | p[3]) / 10.0;
    default:                 return std::nullopt;
    }
}

const char *pidName(uint8_t pid)
{
    switch (pid) {
    case PID_ENGINE_LOAD:    return "engine-load";
    case PID_COOLANT_TEMP:   return "coolant-temp";
    case PID_INTAKE_TEMP:    return "intake-temp";
    case PID_RPM:            return "rpm";
    case PID_SPEED:          return "speed";
    case PID_MAF:            return "maf";
    case PID_FUEL_LEVEL:     return "fuel-level";
    case PID_MODULE_VOLTAGE: return "module-voltage";
    case PID_AMBIENT_TEMP:   return "ambient-temp";
    case PID_HYBRID_BATTERY: return "hybrid-battery";
    case PID_ODOMETER:       return "odometer";
    default:                 return isSupportedPid(pid) ? "supported-pids" : "pid";
    }
}

} // namespace obd
