#include "Mapping.h"
#include "test_support.h"
#include <cstring>

using namespace canproxy;

int main()
{
    // Every ABI signal maps to exactly one non-zero capability bit, and the
    // union of all signals is exactly the set of assigned capability bits.
    uint32_t all = 0;
    for (int i = 0; i < CANPROXY_SIG_COUNT; i++) {
        const uint32_t bit = capabilityBitFor(static_cast<canproxy_signal>(i));
        CHECK(bit != 0);
        CHECK((bit & (bit - 1)) == 0);
        CHECK((bit & CANPROXY_CAP_RESERVED_MASK) == 0);
        all |= bit;
    }
    CHECK(all == static_cast<uint32_t>(~CANPROXY_CAP_RESERVED_MASK));
    CHECK(capabilityBitFor(CANPROXY_SIG_PACK_VOLTAGE) == CANPROXY_CAP_PACK_VI);
    CHECK(capabilityBitFor(CANPROXY_SIG_PACK_CURRENT) == CANPROXY_CAP_PACK_VI);
    CHECK(capabilityBitFor(CANPROXY_SIG_ECO_SCORE) == CANPROXY_CAP_ASSIST);
    CHECK(capabilityBitFor(CANPROXY_SIG_LEAD_GAP) == CANPROXY_CAP_ASSIST);
    CHECK(capabilitiesFrom(CANPROXY_SIG_BIT(CANPROXY_SIG_SPEED) | CANPROXY_SIG_BIT(CANPROXY_SIG_SOC)) ==
          (CANPROXY_CAP_SPEED | CANPROXY_CAP_SOC));
    CHECK(capabilitiesFrom(0) == 0);

    // Lamps: one-to-one onto the assigned telltale bits, legacy bits in place.
    uint32_t tt = 0;
    for (int i = 0; i < CANPROXY_LAMP_COUNT; i++) {
        const uint32_t bit = telltaleBitFor(static_cast<canproxy_lamp>(i));
        CHECK(bit != 0);
        CHECK((bit & tt) == 0);      // no two lamps share a bit
        tt |= bit;
    }
    CHECK(tt == static_cast<uint32_t>(~CANPROXY_TT_RESERVED_MASK));
    CHECK(telltaleBitFor(CANPROXY_LAMP_ENGINE) == CANPROXY_TT_ENGINE);
    CHECK(telltaleBitFor(CANPROXY_LAMP_TURN_LEFT) == CANPROXY_TT_LEFT);
    CHECK(telltaleBitFor(CANPROXY_LAMP_TPMS) == CANPROXY_TT_TPMS);
    CHECK(telltaleBitFor(CANPROXY_LAMP_EV_READY) == CANPROXY_TT_EV_READY);
    CHECK(telltalesFrom(CANPROXY_LAMP_BIT(CANPROXY_LAMP_SEATBELT) | CANPROXY_LAMP_BIT(CANPROXY_LAMP_FOG)) ==
          (CANPROXY_TT_SEATBELT | CANPROXY_TT_FOG));

    // Enumerations: pinned value by value.
    CHECK(gearFrom(CP_GEAR_P) == CANPROXY_GEAR_P && gearFrom(CP_GEAR_R) == CANPROXY_GEAR_R);
    CHECK(gearFrom(CP_GEAR_N) == CANPROXY_GEAR_N && gearFrom(CP_GEAR_D) == CANPROXY_GEAR_D);
    CHECK(gearFrom(CP_GEAR_L) == CANPROXY_GEAR_L && gearFrom(99) == CANPROXY_SNA_U8);
    CHECK(powerStateFrom(CP_POWER_OFF) == CANPROXY_POWER_OFF && powerStateFrom(CP_POWER_READY) == CANPROXY_POWER_READY);
    CHECK(powerStateFrom(-1) == CANPROXY_SNA_U8);
    CHECK(chargingStateFrom(CP_CHARGING_DC) == CANPROXY_CHARGE_DC && chargingStateFrom(CP_CHARGING_COMPLETE) == CANPROXY_CHARGE_COMPLETE);
    CHECK(drivetrainFrom(CP_DRIVETRAIN_BEV) == CANPROXY_DRIVETRAIN_BEV && drivetrainFrom(CP_DRIVETRAIN_PHEV) == CANPROXY_DRIVETRAIN_PHEV);
    CHECK(drivetrainFrom(42) == CANPROXY_DRIVETRAIN_UNKNOWN);
    CHECK(sourceKindFrom(CP_SOURCE_LIVE) == CANPROXY_SOURCE_LIVE && sourceKindFrom(CP_SOURCE_REPLAY) == CANPROXY_SOURCE_REPLAY);
    CHECK(sourceKindFrom(CP_SOURCE_SIMULATED) == CANPROXY_SOURCE_SIMULATED);
    CHECK(collisionRiskFrom(CP_RISK_HIGH) == CANPROXY_RISK_HIGH && collisionRiskFrom(7) == CANPROXY_RISK_NONE);

    // framesFrom: capable + valid -> value; capable without valid -> SNA;
    // valid without capable -> SNA (a plugin cannot leak a value it did not
    // declare); live=false -> everything SNA but caps and identity intact.
    canproxy_vehicle_state s;
    std::memset(&s, 0, sizeof s);
    s.capable = CANPROXY_SIG_BIT(CANPROXY_SIG_SPEED) | CANPROXY_SIG_BIT(CANPROXY_SIG_SOC) | CANPROXY_SIG_BIT(CANPROXY_SIG_RPM);
    s.valid = CANPROXY_SIG_BIT(CANPROXY_SIG_SPEED) | CANPROXY_SIG_BIT(CANPROXY_SIG_FUEL_LEVEL);
    s.speed_kmh = 51.5; s.soc_pct = 70; s.rpm = 2000; s.fuel_pct = 50;
    s.drivetrain = CP_DRIVETRAIN_HEV; s.source = CP_SOURCE_EMULATOR; s.vehicle_id = 0xABCD;
    s.lamps = CANPROXY_LAMP_BIT(CANPROXY_LAMP_ABS);
    Frames f;
    framesFrom(s, true, CANPROXY_STATE_OK, 7, 3, 4, f);
    CHECK(f.status.contract_major == CANPROXY_CONTRACT_MAJOR && f.status.contract_minor == CANPROXY_CONTRACT_MINOR);
    CHECK(f.status.counter == 7 && f.status.proxy_state == CANPROXY_STATE_OK);
    CHECK(f.status.capabilities == (CANPROXY_CAP_SPEED | CANPROXY_CAP_SOC | CANPROXY_CAP_RPM));
    CHECK(f.identity.drivetrain == CANPROXY_DRIVETRAIN_HEV && f.identity.source_kind == CANPROXY_SOURCE_EMULATOR);
    CHECK(f.identity.plugin_major == 3 && f.identity.plugin_minor == 4 && f.identity.vehicle_id == 0xABCD);
    CHECK(f.motion.speed_valid && f.motion.speed_kmh == 51.5);
    CHECK(!f.motion.rpm_valid);                 // capable but not valid
    CHECK(!f.energy.soc_valid);
    CHECK(!f.thermal.fuel_valid);               // valid but not capable
    CHECK(f.telltales.bits == CANPROXY_TT_ABS);

    framesFrom(s, false, CANPROXY_STATE_NO_VEHICLE, 8, 3, 4, f);
    CHECK(!f.motion.speed_valid && !f.motion.rpm_valid && !f.energy.soc_valid);
    CHECK(f.telltales.bits == 0);
    CHECK(f.status.capabilities == (CANPROXY_CAP_SPEED | CANPROXY_CAP_SOC | CANPROXY_CAP_RPM));
    CHECK(f.status.proxy_state == CANPROXY_STATE_NO_VEHICLE);
    CHECK(f.identity.drivetrain == CANPROXY_DRIVETRAIN_HEV);

    // Range and rpm clamp; enum out of range -> SNA even when valid.
    s.capable = s.valid = CANPROXY_SIG_BIT(CANPROXY_SIG_RANGE) | CANPROXY_SIG_BIT(CANPROXY_SIG_RPM) | CANPROXY_SIG_BIT(CANPROXY_SIG_GEAR);
    s.range_km = 1e9; s.rpm = -5; s.gear = 77;
    framesFrom(s, true, CANPROXY_STATE_OK, 0, 0, 0, f);
    CHECK(f.energy.range_valid && f.energy.range_km == 65534);
    CHECK(f.motion.rpm_valid && f.motion.rpm == 0);
    CHECK(!f.motion.gear_valid);

    return REPORT("mapping_tests");
}
