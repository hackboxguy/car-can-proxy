#include "obd/Uds.h"

namespace obd {

std::vector<uint8_t> buildReadDid(uint16_t did)
{
    return { kUdsReadDataByIdentifier, static_cast<uint8_t>(did >> 8), static_cast<uint8_t>(did & 0xFF) };
}

std::optional<UdsReply> parseReadDidReply(const std::vector<uint8_t> &pdu, uint16_t did)
{
    if (pdu.size() >= 3 && pdu[0] == kUdsNegativeResponse && pdu[1] == kUdsReadDataByIdentifier) {
        UdsReply r;
        r.positive = false;
        r.nrc = pdu[2];
        return r;
    }
    if (pdu.size() >= 3 && pdu[0] == kUdsReadDataByIdentifier + 0x40 &&
            pdu[1] == (did >> 8) && pdu[2] == (did & 0xFF)) {
        UdsReply r;
        r.positive = true;
        r.data.assign(pdu.begin() + 3, pdu.end());
        return r;
    }
    return std::nullopt;
}

std::optional<UdsReply> readDid(IsoTpChannel &ch, uint16_t did, int timeoutMs)
{
    std::vector<uint8_t> resp;
    if (!ch.transact(buildReadDid(did), resp, timeoutMs))
        return std::nullopt;
    return parseReadDidReply(resp, did);
}

} // namespace obd
