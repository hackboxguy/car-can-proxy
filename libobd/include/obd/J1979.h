// J1979 (OBD-II) over classic CAN: frame building, single-frame response
// parsing, supported-PID bitmaps and the decode table for the PIDs this
// project uses. Pure functions; no sockets.
#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace obd {

constexpr uint32_t kRequestId = 0x7DF;          // functional (broadcast) request
constexpr uint32_t kResponseIdFirst = 0x7E8;    // ECU #0 ... #7
constexpr uint32_t kResponseIdLast = 0x7EF;
constexpr uint8_t kModeCurrentData = 0x01;
constexpr uint8_t kResponseOffset = 0x40;

// PIDs used by the plugins.
enum Pid : uint8_t {
    PID_SUPPORTED_00 = 0x00,
    PID_ENGINE_LOAD = 0x04,
    PID_COOLANT_TEMP = 0x05,
    PID_INTAKE_TEMP = 0x0B,
    PID_RPM = 0x0C,
    PID_SPEED = 0x0D,
    PID_MAF = 0x10,
    PID_SUPPORTED_20 = 0x20,
    PID_FUEL_LEVEL = 0x2F,
    PID_SUPPORTED_40 = 0x40,
    PID_MODULE_VOLTAGE = 0x42,
    PID_AMBIENT_TEMP = 0x46,
    PID_HYBRID_BATTERY = 0x5B,
    PID_SUPPORTED_60 = 0x60,
    PID_SUPPORTED_80 = 0x80,
    PID_SUPPORTED_A0 = 0xA0,
    PID_ODOMETER = 0xA6,
    PID_SUPPORTED_C0 = 0xC0,
};

inline bool isResponseId(uint32_t id) { return id >= kResponseIdFirst && id <= kResponseIdLast; }
inline bool isSupportedPid(uint8_t pid) { return (pid & 0x1F) == 0; }

// Fill an 8-byte mode-01 request for `pid`.
void buildRequest(uint8_t mode, uint8_t pid, uint8_t out[8]);

struct Response {
    uint8_t mode = 0;        // 0x41 for a mode-01 reply
    uint8_t pid = 0;
    uint8_t data[6] = {};    // payload A, B, C, D ... after the PID
    int len = 0;             // payload bytes actually present
};

// Parse a single-frame positive response. Returns nullopt for anything that
// is not one (negative response, multi-frame first frame, bad length).
std::optional<Response> parseSingleFrame(const uint8_t *frame, unsigned dlc);

// Expected payload bytes for a PID we know, 0 for unknown.
int payloadLength(uint8_t pid);

// PIDs advertised by a supported-PID bitmap reply (PID 0x00/0x20/...):
// A..D are the four payload bytes. The range is basePid+1 .. basePid+0x20.
std::vector<uint8_t> supportedPidsFrom(uint8_t basePid, const uint8_t abcd[4]);
// True if the bitmap says the next supported-PID block exists (last bit).
bool nextBlockAdvertised(const uint8_t abcd[4]);

// Physical-unit decode of a payload for a known PID. nullopt if the PID is
// unknown here or the payload is too short.
//   0x04 load %        0x05 coolant °C     0x0B intake °C     0x0C rpm
//   0x0D km/h          0x10 MAF g/s        0x2F fuel %        0x42 V
//   0x46 ambient °C    0x5B hybrid batt %  0xA6 odometer km
std::optional<double> decode(uint8_t pid, const uint8_t *payload, int len);

const char *pidName(uint8_t pid);

} // namespace obd
