/* Contract v1.1 unit tests: round-trips, SNA, clamping, golden wire bytes. */
#include "can_proxy_contract.h"
#include <stdio.h>
#include <string.h>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) g_pass++; else { g_fail++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_BYTES(d, ...) do { const uint8_t exp_[8] = { __VA_ARGS__ }; \
    if (memcmp(d, exp_, 8) == 0) g_pass++; else { g_fail++; unsigned i_; \
    fprintf(stderr, "FAIL %s:%d: bytes", __FILE__, __LINE__); \
    for (i_ = 0; i_ < 8; i_++) fprintf(stderr, " %02X", d[i_]); \
    fprintf(stderr, " expected"); \
    for (i_ = 0; i_ < 8; i_++) fprintf(stderr, " %02X", exp_[i_]); \
    fprintf(stderr, "\n"); } } while (0)
#define NEAR(a, b) ((a) - (b) < 1e-9 && (b) - (a) < 1e-9)

static void test_constants(void)
{
    static const uint32_t ids[] = { 0x400, 0x401, 0x410, 0x411, 0x420, 0x430, 0x431, 0x440, 0x450 };
    unsigned i, n = 0;
    CHECK(CANPROXY_CONTRACT_MAJOR == 1);
    CHECK(CANPROXY_CONTRACT_MINOR == 1);
    CHECK(CANPROXY_FRAME_DLC == 8);
    CHECK(sizeof(ids) / sizeof(ids[0]) == CANPROXY_FRAME_COUNT);
    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        CHECK(canproxy_is_contract_id(ids[i]));
        CHECK(canproxy_cycle_ms(ids[i]) > 0);
        /* three missed cycles rule: stale window is strictly more than 3 cycles */
        CHECK(canproxy_stale_ms(ids[i]) > 3 * canproxy_cycle_ms(ids[i]));
        CHECK(strcmp(canproxy_id_name(ids[i]), "unknown") != 0);
    }
    /* nothing else between first and last is a contract ID */
    for (i = CANPROXY_ID_FIRST; i <= CANPROXY_ID_LAST; i++)
        if (canproxy_is_contract_id(i)) n++;
    CHECK(n == CANPROXY_FRAME_COUNT);
    CHECK(!canproxy_is_contract_id(0x7DF));
    CHECK(!canproxy_is_contract_id(0x7E8));
    CHECK(canproxy_cycle_ms(0x7E8) == 0 && canproxy_stale_ms(0x7E8) == 0);
    /* documented numbers */
    CHECK(canproxy_cycle_ms(0x400) == 100 && canproxy_stale_ms(0x400) == 500);
    CHECK(canproxy_cycle_ms(0x401) == 1000 && canproxy_stale_ms(0x401) == 4000);
    CHECK(canproxy_cycle_ms(0x410) == 50 && canproxy_stale_ms(0x410) == 250);
    CHECK(canproxy_cycle_ms(0x411) == 50 && canproxy_stale_ms(0x411) == 250);
    CHECK(canproxy_cycle_ms(0x420) == 100 && canproxy_stale_ms(0x420) == 500);
    CHECK(canproxy_cycle_ms(0x430) == 500 && canproxy_stale_ms(0x430) == 2000);
    CHECK(canproxy_cycle_ms(0x431) == 1000 && canproxy_stale_ms(0x431) == 4000);
    CHECK(canproxy_cycle_ms(0x440) == 500 && canproxy_stale_ms(0x440) == 2000);
    CHECK(canproxy_cycle_ms(0x450) == 200 && canproxy_stale_ms(0x450) == 800);
    /* SNA encodings as documented */
    CHECK(CANPROXY_SNA_U8 == 0xFF);
    CHECK((uint8_t)CANPROXY_SNA_I8 == 0x80);
    CHECK(CANPROXY_SNA_U16 == 0xFFFF);
    CHECK((uint16_t)CANPROXY_SNA_I16 == 0x8000);
    CHECK(CANPROXY_SNA_U32 == 0xFFFFFFFFu);
}

static void test_byte_helpers(void)
{
    uint8_t b[4];
    canproxy_put_u16le(b, 0x1234);
    CHECK(b[0] == 0x34 && b[1] == 0x12);
    CHECK(canproxy_get_u16le(b) == 0x1234);
    canproxy_put_u32le(b, 0xDEADBEEFu);
    CHECK(b[0] == 0xEF && b[1] == 0xBE && b[2] == 0xAD && b[3] == 0xDE);
    CHECK(canproxy_get_u32le(b) == 0xDEADBEEFu);
    CHECK(canproxy_round(1.5) == 2 && canproxy_round(2.4999) == 2);
    CHECK(canproxy_round(-1.5) == -2 && canproxy_round(-0.4) == 0);
}

static void test_status(void)
{
    canproxy_status_t s, o;
    uint8_t d[8];
    memset(&s, 0, sizeof s);
    s.contract_major = CANPROXY_CONTRACT_MAJOR;
    s.contract_minor = CANPROXY_CONTRACT_MINOR;
    s.counter = 0xAB;
    s.proxy_state = CANPROXY_STATE_OK;
    s.capabilities = CANPROXY_CAP_SPEED | CANPROXY_CAP_ASSIST | 0x80000000u; /* reserved bit set */
    canproxy_pack_status(&s, d);
    CHECK_BYTES(d, 0x01, 0x01, 0xAB, 0x03, 0x01, 0x00, 0x02, 0x00);
    canproxy_unpack_status(d, &o);
    CHECK(o.contract_major == 1 && o.contract_minor == 1 && o.counter == 0xAB);
    CHECK(o.proxy_state == CANPROXY_STATE_OK);
    CHECK(o.capabilities == (CANPROXY_CAP_SPEED | CANPROXY_CAP_ASSIST)); /* reserved stripped */
    CHECK(canproxy_status_compatible(&o));
    o.contract_minor = 7;               /* future additive minor is fine */
    CHECK(canproxy_status_compatible(&o));
    o.contract_major = 2;
    CHECK(!canproxy_status_compatible(&o));
    /* v1.0 draft bits 0-12 keep their positions */
    CHECK(CANPROXY_CAP_SPEED == 1u && CANPROXY_CAP_RPM == 2u && CANPROXY_CAP_GEAR == 4u);
    CHECK(CANPROXY_CAP_MOTOR_POWER == (1u << 3) && CANPROXY_CAP_PACK_VI == (1u << 4));
    CHECK(CANPROXY_CAP_SOC == (1u << 5) && CANPROXY_CAP_RANGE == (1u << 6));
    CHECK(CANPROXY_CAP_CONSUMPTION == (1u << 7) && CANPROXY_CAP_ODOMETER == (1u << 8));
    CHECK(CANPROXY_CAP_AMBIENT_TEMP == (1u << 9) && CANPROXY_CAP_COOLANT_TEMP == (1u << 10));
    CHECK(CANPROXY_CAP_FUEL_LEVEL == (1u << 11) && CANPROXY_CAP_AUX_BATTERY == (1u << 12));
    CHECK(CANPROXY_CAP_ASSIST == (1u << 17));
    CHECK((CANPROXY_CAP_RESERVED_MASK & CANPROXY_CAP_ASSIST) == 0);
    CHECK((CANPROXY_CAP_RESERVED_MASK & (1u << 18)) != 0);
}

static void test_identity(void)
{
    canproxy_identity_t s, o;
    uint8_t d[8];
    s.drivetrain = CANPROXY_DRIVETRAIN_BEV;
    s.source_kind = CANPROXY_SOURCE_EMULATOR;
    s.plugin_major = 2;
    s.plugin_minor = 5;
    s.vehicle_id = 0x11223344u;
    canproxy_pack_identity(&s, d);
    CHECK_BYTES(d, 0x02, 0x01, 0x02, 0x05, 0x44, 0x33, 0x22, 0x11);
    canproxy_unpack_identity(d, &o);
    CHECK(memcmp(&s, &o, sizeof s) == 0);
    /* FNV-1a 32 known vectors */
    CHECK(canproxy_fnv1a32("") == 0x811C9DC5u);
    CHECK(canproxy_fnv1a32("a") == 0xE40C292Cu);
    CHECK(canproxy_fnv1a32("foobar") == 0xBF9CF968u);
    CHECK(canproxy_fnv1a32("obd2-ice/KNABX512ABT123456") != 0);
}

static void test_motion(void)
{
    canproxy_motion_t s, o;
    uint8_t d[8];
    memset(&s, 0, sizeof s);
    /* all SNA */
    canproxy_pack_motion(&s, d);
    CHECK_BYTES(d, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00);
    canproxy_unpack_motion(d, &o);
    CHECK(!o.speed_valid && !o.rpm_valid && !o.gear_valid && !o.power_state_valid);
    /* golden: 123.45 km/h, 3000 rpm, D, ready */
    s.speed_kmh = 123.45; s.speed_valid = true;
    s.rpm = 3000; s.rpm_valid = true;
    s.gear = CANPROXY_GEAR_D; s.gear_valid = true;
    s.power_state = CANPROXY_POWER_READY; s.power_state_valid = true;
    canproxy_pack_motion(&s, d);
    CHECK_BYTES(d, 0x39, 0x30, 0xB8, 0x0B, 0x03, 0x03, 0x00, 0x00);
    canproxy_unpack_motion(d, &o);
    CHECK(o.speed_valid && NEAR(o.speed_kmh, 123.45));
    CHECK(o.rpm_valid && o.rpm == 3000);
    CHECK(o.gear_valid && o.gear == CANPROXY_GEAR_D);
    CHECK(o.power_state_valid && o.power_state == CANPROXY_POWER_READY);
    /* rounding and clamping never produce SNA */
    s.speed_kmh = 12.345;           /* 1234.5 -> 1235 */
    canproxy_pack_motion(&s, d);
    CHECK(canproxy_get_u16le(d) == 1235);
    s.speed_kmh = 9999.0;
    canproxy_pack_motion(&s, d);
    CHECK(canproxy_get_u16le(d) == 0xFFFE);
    s.speed_kmh = -5.0;
    canproxy_pack_motion(&s, d);
    CHECK(canproxy_get_u16le(d) == 0);
    s.rpm = 0xFFFF;
    canproxy_pack_motion(&s, d);
    CHECK(canproxy_get_u16le(&d[2]) == 0xFFFE);
}

static void test_edrive(void)
{
    canproxy_edrive_t s, o;
    uint8_t d[8];
    memset(&s, 0, sizeof s);
    canproxy_pack_edrive(&s, d);
    CHECK_BYTES(d, 0x00, 0x80, 0xFF, 0xFF, 0x00, 0x80, 0x00, 0x00);
    canproxy_unpack_edrive(d, &o);
    CHECK(!o.motor_power_valid && !o.pack_voltage_valid && !o.pack_current_valid);
    s.motor_power_kw = -12.3; s.motor_power_valid = true;   /* regen */
    s.pack_voltage_v = 385.6; s.pack_voltage_valid = true;
    s.pack_current_a = -31.9; s.pack_current_valid = true;  /* charging */
    canproxy_pack_edrive(&s, d);
    CHECK_BYTES(d, 0x85, 0xFF, 0x10, 0x0F, 0xC1, 0xFE, 0x00, 0x00);
    canproxy_unpack_edrive(d, &o);
    CHECK(o.motor_power_valid && NEAR(o.motor_power_kw, -12.3));
    CHECK(o.pack_voltage_valid && NEAR(o.pack_voltage_v, 385.6));
    CHECK(o.pack_current_valid && NEAR(o.pack_current_a, -31.9));
    s.motor_power_kw = -99999.0;   /* clamps to -3276.7, not SNA */
    canproxy_pack_edrive(&s, d);
    CHECK((int16_t)canproxy_get_u16le(d) == -32767);
    s.motor_power_kw = 99999.0;
    canproxy_pack_edrive(&s, d);
    CHECK((int16_t)canproxy_get_u16le(d) == 32767);
}

static void test_telltales(void)
{
    canproxy_telltales_t s, o;
    uint8_t d[8];
    /* bits 0-11 must equal the pre-existing 16-bit reader's enum */
    CHECK(CANPROXY_TT_ENGINE == (1u << 0));
    CHECK(CANPROXY_TT_OIL == (1u << 1));
    CHECK(CANPROXY_TT_BATTERY == (1u << 2));
    CHECK(CANPROXY_TT_BRAKE == (1u << 3));
    CHECK(CANPROXY_TT_LEFT == (1u << 4));
    CHECK(CANPROXY_TT_RIGHT == (1u << 5));
    CHECK(CANPROXY_TT_HIGHBEAM == (1u << 6));
    CHECK(CANPROXY_TT_DOOR == (1u << 7));
    CHECK(CANPROXY_TT_SEATBELT == (1u << 8));
    CHECK(CANPROXY_TT_ABS == (1u << 9));
    CHECK(CANPROXY_TT_TRACTION == (1u << 10));
    CHECK(CANPROXY_TT_TPMS == (1u << 11));
    CHECK(CANPROXY_TT_HV_SYSTEM_FAULT == (1u << 19));
    CHECK((CANPROXY_TT_RESERVED_MASK & CANPROXY_TT_HV_SYSTEM_FAULT) == 0);
    CHECK((CANPROXY_TT_RESERVED_MASK & (1u << 20)) != 0);
    s.bits = CANPROXY_TT_LEFT | CANPROXY_TT_SEATBELT | CANPROXY_TT_EV_READY | CANPROXY_TT_PARK_BRAKE | 0x80000000u;
    canproxy_pack_telltales(&s, d);
    /* 0x00011110 -> 10 11 01 00, reserved bit 31 stripped, bytes 4-7 zero */
    CHECK_BYTES(d, 0x10, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00);
    /* a legacy reader taking bytes 0-1 sees exactly the legacy bits */
    CHECK(canproxy_get_u16le(d) == (CANPROXY_TT_LEFT | CANPROXY_TT_SEATBELT | CANPROXY_TT_EV_READY));
    CHECK((canproxy_get_u16le(d) & CANPROXY_TT_LEGACY_MASK) == canproxy_get_u16le(d));
    canproxy_unpack_telltales(d, &o);
    CHECK(o.bits == (CANPROXY_TT_LEFT | CANPROXY_TT_SEATBELT | CANPROXY_TT_EV_READY | CANPROXY_TT_PARK_BRAKE));
}

static void test_energy(void)
{
    canproxy_energy_t s, o;
    uint8_t d[8];
    memset(&s, 0, sizeof s);
    canproxy_pack_energy(&s, d);
    CHECK_BYTES(d, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x80, 0xFF, 0x00);
    canproxy_unpack_energy(d, &o);
    CHECK(!o.soc_valid && !o.soh_valid && !o.range_valid && !o.consumption_valid && !o.charging_state_valid);
    s.soc_pct = 80.5; s.soc_valid = true;
    s.soh_pct = 100.0; s.soh_valid = true;
    s.range_km = 368; s.range_valid = true;
    s.consumption_wh_km = -25; s.consumption_valid = true;  /* net regen */
    s.charging_state = CANPROXY_CHARGE_DC; s.charging_state_valid = true;
    canproxy_pack_energy(&s, d);
    CHECK_BYTES(d, 0xA1, 0xC8, 0x70, 0x01, 0xE7, 0xFF, 0x02, 0x00);
    canproxy_unpack_energy(d, &o);
    CHECK(o.soc_valid && NEAR(o.soc_pct, 80.5));
    CHECK(o.soh_valid && NEAR(o.soh_pct, 100.0));
    CHECK(o.range_valid && o.range_km == 368);
    CHECK(o.consumption_valid && o.consumption_wh_km == -25);
    CHECK(o.charging_state_valid && o.charging_state == CANPROXY_CHARGE_DC);
    s.soc_pct = 150.0;             /* clamps to 100 % = 200, never 0xFF */
    canproxy_pack_energy(&s, d);
    CHECK(d[0] == 200);
    s.range_km = 0xFFFF;
    s.consumption_wh_km = CANPROXY_SNA_I16;
    canproxy_pack_energy(&s, d);
    CHECK(canproxy_get_u16le(&d[2]) == 0xFFFE);
    CHECK((int16_t)canproxy_get_u16le(&d[4]) == -32767);
}

static void test_trip(void)
{
    canproxy_trip_t s, o;
    uint8_t d[8];
    memset(&s, 0, sizeof s);
    canproxy_pack_trip(&s, d);
    CHECK_BYTES(d, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x80, 0x00, 0x00);
    canproxy_unpack_trip(d, &o);
    CHECK(!o.odometer_valid && !o.ambient_valid && !o.cabin_valid);
    s.odometer_km = 123456.7; s.odometer_valid = true;   /* raw 1234567 = 0x12D687 */
    s.ambient_c = -40; s.ambient_valid = true;
    s.cabin_c = 22; s.cabin_valid = true;
    canproxy_pack_trip(&s, d);
    CHECK_BYTES(d, 0x87, 0xD6, 0x12, 0x00, 0xD8, 0x16, 0x00, 0x00);
    canproxy_unpack_trip(d, &o);
    CHECK(o.odometer_valid && NEAR(o.odometer_km, 123456.7));
    CHECK(o.ambient_valid && o.ambient_c == -40);
    CHECK(o.cabin_valid && o.cabin_c == 22);
    s.ambient_c = -128;           /* the SNA value as input must not become SNA */
    canproxy_pack_trip(&s, d);
    CHECK(d[4] == (uint8_t)-127);
    s.odometer_km = 1.0e12;       /* clamp, not wrap, not SNA */
    canproxy_pack_trip(&s, d);
    CHECK(canproxy_get_u32le(d) == 0xFFFFFFFEu);
}

static void test_thermal(void)
{
    canproxy_thermal_t s, o;
    uint8_t d[8];
    memset(&s, 0, sizeof s);
    canproxy_pack_thermal(&s, d);
    CHECK_BYTES(d, 0x80, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00);
    canproxy_unpack_thermal(d, &o);
    CHECK(!o.coolant_valid && !o.fuel_valid && !o.aux_battery_valid);
    s.coolant_c = 90; s.coolant_valid = true;
    s.fuel_pct = 37.5; s.fuel_valid = true;
    s.aux_battery_v = 12.64; s.aux_battery_valid = true;  /* 1264 = 0x04F0 */
    canproxy_pack_thermal(&s, d);
    CHECK_BYTES(d, 0x5A, 0x4B, 0xF0, 0x04, 0x00, 0x00, 0x00, 0x00);
    canproxy_unpack_thermal(d, &o);
    CHECK(o.coolant_valid && o.coolant_c == 90);
    CHECK(o.fuel_valid && NEAR(o.fuel_pct, 37.5));
    CHECK(o.aux_battery_valid && NEAR(o.aux_battery_v, 12.64));
}

static void test_assist(void)
{
    canproxy_assist_t s, o;
    uint8_t d[8];
    memset(&s, 0, sizeof s);
    canproxy_pack_assist(&s, d);
    CHECK_BYTES(d, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00);
    canproxy_unpack_assist(d, &o);
    CHECK(!o.eco_score_valid && !o.speed_limit_valid && !o.collision_risk_valid && !o.lane_state_valid && !o.lead_gap_valid);
    s.eco_score = 72; s.eco_score_valid = true;
    s.speed_limit_kmh = 50; s.speed_limit_valid = true;
    s.collision_risk = CANPROXY_RISK_MEDIUM; s.collision_risk_valid = true;
    s.lane_state = CANPROXY_LANE_LEFT_SEEN | CANPROXY_LANE_RIGHT_SEEN | 0xF0; s.lane_state_valid = true;
    s.lead_gap_m = 42.5; s.lead_gap_valid = true;   /* 425 = 0x01A9 */
    canproxy_pack_assist(&s, d);
    CHECK_BYTES(d, 0x48, 0x32, 0x02, 0x03, 0xA9, 0x01, 0x00, 0x00);
    canproxy_unpack_assist(d, &o);
    CHECK(o.eco_score_valid && o.eco_score == 72);
    CHECK(o.speed_limit_valid && o.speed_limit_kmh == 50);
    CHECK(o.collision_risk_valid && o.collision_risk == CANPROXY_RISK_MEDIUM);
    CHECK(o.lane_state_valid && o.lane_state == (CANPROXY_LANE_LEFT_SEEN | CANPROXY_LANE_RIGHT_SEEN));
    CHECK(o.lead_gap_valid && NEAR(o.lead_gap_m, 42.5));
    s.eco_score = 250;            /* clamp to 100 */
    s.speed_limit_kmh = 255;      /* 0xFF as input must not become SNA */
    canproxy_pack_assist(&s, d);
    CHECK(d[0] == 100 && d[1] == 0xFE);
    s.speed_limit_kmh = 0;        /* 0 = "no limit known" is a valid value */
    canproxy_pack_assist(&s, d);
    canproxy_unpack_assist(d, &o);
    CHECK(o.speed_limit_valid && o.speed_limit_kmh == 0);
}

int main(void)
{
    test_constants();
    test_byte_helpers();
    test_status();
    test_identity();
    test_motion();
    test_edrive();
    test_telltales();
    test_energy();
    test_trip();
    test_thermal();
    test_assist();
    printf("contract_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
