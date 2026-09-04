#include "../ForzaTelemetry.h"
#include <cstdio>
#include <cstring>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b) ((a) - (b) < 1e-3 && (b) - (a) < 1e-3)

static void putf(std::vector<uint8_t> &p, size_t off, float v) { std::memcpy(&p[off], &v, 4); }
static void puti(std::vector<uint8_t> &p, size_t off, int32_t v) { std::memcpy(&p[off], &v, 4); }

// Build a packet of the given size with the sled and (if present) dash fields set.
static std::vector<uint8_t> packet(size_t len, bool raceOn, float rpm, float speedMps, float powerW, float fuel, uint8_t gear, uint8_t handbrake)
{
    std::vector<uint8_t> p(len, 0);
    puti(p, 0, raceOn ? 1 : 0);
    puti(p, 4, 123456);
    putf(p, 8, 7500.0f);
    putf(p, 12, 800.0f);
    putf(p, 16, rpm);
    putf(p, 32, speedMps * 0.6f); putf(p, 36, 0.0f); putf(p, 40, speedMps * 0.8f);   // |v| = speedMps
    const int base = forza::dashOffset(len);
    if (base >= 0) {
        putf(p, base + 12, speedMps);
        putf(p, base + 16, powerW);
        putf(p, base + 20, 300.0f);
        putf(p, base + 44, fuel);
        putf(p, base + 48, 12345.0f);
        p[base + 71] = 200; p[base + 72] = 0; p[base + 74] = handbrake; p[base + 75] = gear;
        p[base + 76] = static_cast<uint8_t>(-30);
    }
    return p;
}

int main()
{
    forza::Frame f;
    for (size_t len : { size_t(311), size_t(324), size_t(331) }) {
        auto p = packet(len, true, 4321.0f, 27.5f, 155000.0f, 0.42f, 4, 0);
        CHECK(forza::parse(p.data(), p.size(), f));
        CHECK(f.raceOn && f.hasDash && f.timestampMs == 123456);
        CHECK(NEAR(f.engineRpm, 4321.0f) && NEAR(f.engineMaxRpm, 7500.0f));
        CHECK(NEAR(f.speedMps, 27.5f));
        CHECK(NEAR(f.powerW, 155000.0f) && NEAR(f.torqueNm, 300.0f));
        CHECK(NEAR(f.fuel, 0.42f) && NEAR(f.distanceM, 12345.0f));
        CHECK(f.gear == 4 && f.handbrake == 0 && f.accel == 200 && f.steer == -30);
    }
    CHECK(std::strcmp(f.format, "fm2023") == 0);

    // Sled-only: speed from the velocity vector, no dash fields.
    auto s = packet(232, true, 900.0f, 10.0f, 0, 0, 0, 0);
    CHECK(forza::parse(s.data(), s.size(), f));
    CHECK(!f.hasDash && NEAR(f.speedMps, 10.0f) && NEAR(f.engineRpm, 900.0f) && std::strcmp(f.format, "sled") == 0);

    // FH4/5 offsets as CarCluster reads them: speed at 256, gear at 319, handbrake at 318.
    auto h = packet(324, true, 1000.0f, 5.0f, 1000.0f, 0.5f, 0, 1);
    float sp; std::memcpy(&sp, &h[256], 4);
    CHECK(NEAR(sp, 5.0f) && h[319] == 0 && h[318] == 1);
    CHECK(forza::parse(h.data(), h.size(), f) && f.gear == 0 && f.handbrake == 1);

    // Menu / paused: raceOn false is still a valid frame.
    auto m = packet(324, false, 0.0f, 0.0f, 0.0f, 0.9f, 1, 0);
    CHECK(forza::parse(m.data(), m.size(), f) && !f.raceOn);

    // Rejections: unknown size, NaN rpm, fuel out of range.
    std::vector<uint8_t> junk(100, 0);
    CHECK(!forza::parse(junk.data(), junk.size(), f));
    auto bad = packet(324, true, 0.0f, 1.0f, 0.0f, 0.5f, 1, 0);
    putf(bad, 16, __builtin_nanf(""));
    CHECK(!forza::parse(bad.data(), bad.size(), f));
    auto bad2 = packet(311, true, 100.0f, 1.0f, 0.0f, 1.5f, 1, 0);
    CHECK(!forza::parse(bad2.data(), bad2.size(), f));
    CHECK(!forza::parse(nullptr, 324, f));

    std::printf("forza_parse_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
