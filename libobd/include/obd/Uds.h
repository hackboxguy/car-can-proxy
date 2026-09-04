// UDS (ISO 14229) ReadDataByIdentifier, read-only by construction: this
// library offers no write, no session control and no security access.
#pragma once
#include "obd/IsoTp.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace obd {

constexpr uint8_t kUdsReadDataByIdentifier = 0x22;
constexpr uint8_t kUdsNegativeResponse = 0x7F;

struct UdsReply {
    bool positive = false;
    uint8_t nrc = 0;               // negative response code when !positive
    std::vector<uint8_t> data;     // record data (after the echoed DID)
};

// Build the request PDU for a DID.
std::vector<uint8_t> buildReadDid(uint16_t did);

// Parse a reply PDU to a 0x22 request for `did`. nullopt when the PDU is
// neither a matching positive response nor a negative response to 0x22.
std::optional<UdsReply> parseReadDidReply(const std::vector<uint8_t> &pdu, uint16_t did);

// Convenience: one round trip. nullopt on transport failure.
std::optional<UdsReply> readDid(IsoTpChannel &ch, uint16_t did, int timeoutMs);

// Big-endian field helpers for record data.
inline uint16_t be16(const uint8_t *p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
inline int16_t  be16s(const uint8_t *p) { return static_cast<int16_t>(be16(p)); }
inline uint32_t be32(const uint8_t *p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

} // namespace obd
