#include "obd/Uds.h"
#include <cstdio>

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main()
{
    auto req = obd::buildReadDid(0x0101);
    CHECK(req.size() == 3 && req[0] == 0x22 && req[1] == 0x01 && req[2] == 0x01);

    std::vector<uint8_t> pos = { 0x62, 0x01, 0x01, 0x0F, 0x28, 0xFF, 0x38, 0xA0, 0xC2, 0x00, 0x00 };
    auto r = obd::parseReadDidReply(pos, 0x0101);
    CHECK(r && r->positive && r->data.size() == 8 && r->data[0] == 0x0F);
    CHECK(obd::be16(&r->data[0]) == 0x0F28);
    CHECK(obd::be16s(&r->data[2]) == -200);
    CHECK(obd::be32(&pos[3]) == 0x0F28FF38u);
    CHECK(!obd::parseReadDidReply(pos, 0x0102));          // different DID echoed

    std::vector<uint8_t> neg = { 0x7F, 0x22, 0x31 };
    r = obd::parseReadDidReply(neg, 0x0101);
    CHECK(r && !r->positive && r->nrc == 0x31);

    std::vector<uint8_t> other = { 0x7F, 0x10, 0x12 };     // NRC for a different service
    CHECK(!obd::parseReadDidReply(other, 0x0101));
    std::vector<uint8_t> shortPdu = { 0x62, 0x01 };
    CHECK(!obd::parseReadDidReply(shortPdu, 0x0101));

    // Kernel support is a property of the host, not this library; just call it.
    (void)obd::IsoTpChannel::kernelSupportAvailable();

    std::printf("uds_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
