#include "../EmuProfile.h"
#include <cstdio>

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b) ((a) - (b) < 1e-6 && (b) - (a) < 1e-6)

int main()
{
    // Records as the emulator sends them (probe of --car=ev, defaults).
    emu::Pack pk;
    CHECK(emu::decodePack({ 0x0F, 0x28, 0x02, 0x26, 0xA0, 0xC2, 0x00, 0x00 }, pk));
    CHECK(NEAR(pk.voltageV, 388.0) && NEAR(pk.currentA, 55.0) && NEAR(pk.socPct, 80.0) && NEAR(pk.sohPct, 97.0) && pk.charging == 0);
    CHECK(emu::decodePack({ 0x0F, 0x28, 0xFF, 0x38, 0x14, 0xC2, 0x02, 0x00 }, pk));   // charging at -20 A, 10 %
    CHECK(NEAR(pk.currentA, -20.0) && NEAR(pk.socPct, 10.0) && pk.charging == 2);
    CHECK(!emu::decodePack({ 0x0F, 0x28, 0x02 }, pk));                                // short
    CHECK(!emu::decodePack({ 0x0F, 0x28, 0x02, 0x26, 0xA0, 0xC2, 0x09, 0x00 }, pk));  // bad enum

    emu::Range rg;
    CHECK(emu::decodeRange({ 0x01, 0x22, 0x00, 0xA5, 0x00, 0x01, 0x9C, 0xD7 }, rg));
    CHECK(rg.rangeKm == 290 && rg.consumptionWhKm == 165 && NEAR(rg.odometerKm, 10568.7));
    CHECK(emu::decodeRange({ 0x00, 0x00, 0xFF, 0xE7, 0x00, 0x00, 0x00, 0x00 }, rg));
    CHECK(rg.consumptionWhKm == -25);

    emu::Drive dr;
    CHECK(emu::decodeDrive({ 0x03, 0x03, 0x19, 0xC8, 0x00, 0xD5, 0x00, 0x00 }, dr));
    CHECK(dr.gear == 3 && dr.powerState == 3 && dr.motorRpm == 6600 && NEAR(dr.motorPowerKw, 21.3));
    CHECK(emu::decodeDrive({ 0x00, 0x00, 0x00, 0x00, 0xFE, 0x0C, 0x00, 0x00 }, dr));  // -50.0 kW regen
    CHECK(NEAR(dr.motorPowerKw, -50.0));
    CHECK(!emu::decodeDrive({ 0x07, 0x03, 0x19, 0xC8, 0x00, 0xD5 }, dr));           // gear out of range

    emu::Assist as;
    CHECK(emu::decodeAssist({ 0x4E, 0x32, 0x00, 0x03, 0x01, 0xA4, 0x00, 0x00 }, as));
    CHECK(as.ecoScore == 78 && as.speedLimitKmh == 50 && as.collisionRisk == 0 && as.laneState == 3 && NEAR(as.leadGapM, 42.0));
    CHECK(emu::decodeAssist({ 0x64, 0x00, 0x03, 0xF7, 0x00, 0x3C, 0x00, 0x00 }, as));   // upper lane bits masked
    CHECK(as.speedLimitKmh == 0 && as.collisionRisk == 3 && as.laneState == 7 && NEAR(as.leadGapM, 6.0));
    CHECK(!emu::decodeAssist({ 0x65, 0x32, 0x00, 0x03, 0x01, 0xA4 }, as));               // eco > 100
    CHECK(!emu::decodeAssist({ 0x4E, 0x32, 0x00 }, as));

    std::printf("emu_profile_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
