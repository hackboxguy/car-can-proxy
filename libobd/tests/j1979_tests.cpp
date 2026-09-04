#include "obd/J1979.h"
#include <cstdio>
#include <cstring>

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b) ((a) - (b) < 1e-6 && (b) - (a) < 1e-6)

using namespace obd;

int main()
{
    uint8_t f[8];
    buildRequest(kModeCurrentData, PID_SPEED, f);
    CHECK(f[0] == 0x02 && f[1] == 0x01 && f[2] == 0x0D && f[3] == 0 && f[7] == 0);
    CHECK(isResponseId(0x7E8) && isResponseId(0x7EF) && !isResponseId(0x7E7) && !isResponseId(0x7F0) && !isResponseId(0x7DF));
    CHECK(isSupportedPid(0x00) && isSupportedPid(0x20) && isSupportedPid(0xC0) && !isSupportedPid(0x0D));

    // Single-frame responses as the emulator and real ECUs send them.
    const uint8_t speed[8] = { 0x03, 0x41, 0x0D, 0x58, 0x00, 0x00, 0x00, 0x00 };
    auto r = parseSingleFrame(speed, 8);
    CHECK(r && r->mode == 0x41 && r->pid == 0x0D && r->len == 1 && r->data[0] == 0x58);
    CHECK(decode(PID_SPEED, r->data, r->len).value_or(-1) == 88.0);

    const uint8_t rpm[8] = { 0x04, 0x41, 0x0C, 0x1A, 0xF8, 0x00, 0x00, 0x00 };
    r = parseSingleFrame(rpm, 8);
    CHECK(r && r->len == 2 && NEAR(decode(PID_RPM, r->data, r->len).value_or(-1), (0x1A * 256 + 0xF8) / 4.0));

    const uint8_t odo[8] = { 0x06, 0x41, 0xA6, 0x00, 0x10, 0x1F, 0xA7, 0x00 };   // 0x00101FA7 / 10
    r = parseSingleFrame(odo, 8);
    CHECK(r && r->len == 4 && NEAR(decode(PID_ODOMETER, r->data, r->len).value_or(-1), 0x00101FA7 / 10.0));

    // Decode table by value.
    uint8_t a[4];
    a[0] = 75;  CHECK(decode(PID_COOLANT_TEMP, a, 1).value_or(0) == 35.0);
    a[0] = 0;   CHECK(decode(PID_AMBIENT_TEMP, a, 1).value_or(0) == -40.0);
    a[0] = 255; CHECK(NEAR(decode(PID_FUEL_LEVEL, a, 1).value_or(0), 100.0));
    a[0] = 0x34; a[1] = 0x1E; CHECK(NEAR(decode(PID_MODULE_VOLTAGE, a, 2).value_or(0), 13.342));
    a[0] = 0x05; a[1] = 0x40; CHECK(NEAR(decode(PID_MAF, a, 2).value_or(0), 13.44));
    a[0] = 128; CHECK(NEAR(decode(PID_ENGINE_LOAD, a, 1).value_or(0), 128 * 100.0 / 255.0));
    a[0] = 204; CHECK(NEAR(decode(PID_HYBRID_BATTERY, a, 1).value_or(0), 80.0));
    CHECK(!decode(PID_RPM, a, 1));                 // too short
    CHECK(!decode(0x99, a, 4));                     // unknown
    CHECK(payloadLength(PID_ODOMETER) == 4 && payloadLength(0x99) == 0);

    // Rejections: negative response, first frame, bad length, short dlc.
    const uint8_t neg[8] = { 0x03, 0x7F, 0x01, 0x12, 0, 0, 0, 0 };
    CHECK(!parseSingleFrame(neg, 8));
    const uint8_t ff[8] = { 0x10, 0x14, 0x49, 0x02, 0x01, 0x57, 0x50, 0x30 };
    CHECK(!parseSingleFrame(ff, 8));
    const uint8_t badlen[8] = { 0x09, 0x41, 0x0D, 0x58, 0, 0, 0, 0 };
    CHECK(!parseSingleFrame(badlen, 8));
    CHECK(!parseSingleFrame(speed, 2));
    const uint8_t tooShort[8] = { 0x01, 0x41, 0x0D, 0, 0, 0, 0, 0 };
    CHECK(!parseSingleFrame(tooShort, 8));

    // Supported-PID bitmaps: bit 7 of A is PID base+1, bit 0 of D is base+0x20.
    const uint8_t bm[4] = { 0x18, 0x18, 0x00, 0x01 };   // 04 05 0C 0D and next block
    auto pids = supportedPidsFrom(0x00, bm);
    CHECK(pids.size() == 5 && pids[0] == 0x04 && pids[1] == 0x05 && pids[2] == 0x0C && pids[3] == 0x0D && pids[4] == 0x20);
    CHECK(nextBlockAdvertised(bm));
    const uint8_t bm2[4] = { 0x00, 0x02, 0x00, 0x00 };  // 0x20 block: bit 1 of B = PID 0x2F
    pids = supportedPidsFrom(0x20, bm2);
    CHECK(pids.size() == 1 && pids[0] == 0x2F && !nextBlockAdvertised(bm2));
    const uint8_t bm3[4] = { 0x44, 0x00, 0x00, 0x00 };  // 0x40 block: 0x42 (bit 30) and 0x46 (bit 26)
    pids = supportedPidsFrom(0x40, bm3);
    CHECK(pids.size() == 2 && pids[0] == 0x42 && pids[1] == 0x46);
    const uint8_t bm4[4] = { 0x04, 0x00, 0x00, 0x00 };  // 0xA0 block: 0xA6
    pids = supportedPidsFrom(0xA0, bm4);
    CHECK(pids.size() == 1 && pids[0] == 0xA6);
    const uint8_t all[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    CHECK(supportedPidsFrom(0x00, all).size() == 32);

    CHECK(std::strcmp(pidName(PID_SPEED), "speed") == 0 && std::strcmp(pidName(0x20), "supported-pids") == 0);

    std::printf("j1979_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
