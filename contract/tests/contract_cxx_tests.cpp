// The header must compile cleanly as C++ (a Qt consumer vendors it) and
// behave identically. One round-trip per frame is enough to prove linkage.
#include "can_proxy_contract.h"
#include <cstdio>
#include <cstring>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { ++fails; std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main()
{
    uint8_t d[CANPROXY_FRAME_DLC];

    canproxy_status_t st = {}, st2 = {};
    st.contract_major = CANPROXY_CONTRACT_MAJOR;
    st.contract_minor = CANPROXY_CONTRACT_MINOR;
    st.proxy_state = CANPROXY_STATE_NO_VEHICLE;
    st.capabilities = CANPROXY_CAP_SPEED | CANPROXY_CAP_FUEL_LEVEL;
    canproxy_pack_status(&st, d);
    canproxy_unpack_status(d, &st2);
    CHECK(std::memcmp(&st, &st2, sizeof st) == 0);
    CHECK(canproxy_status_compatible(&st2));

    canproxy_motion_t m = {}, m2 = {};
    m.speed_kmh = 88.8; m.speed_valid = true;
    canproxy_pack_motion(&m, d);
    canproxy_unpack_motion(d, &m2);
    CHECK(m2.speed_valid && m2.speed_kmh > 88.79 && m2.speed_kmh < 88.81);
    CHECK(!m2.rpm_valid && !m2.gear_valid && !m2.power_state_valid);

    canproxy_edrive_t e = {}, e2 = {};
    e.motor_power_kw = -7.5; e.motor_power_valid = true;
    canproxy_pack_edrive(&e, d);
    canproxy_unpack_edrive(d, &e2);
    CHECK(e2.motor_power_valid && e2.motor_power_kw < -7.49 && e2.motor_power_kw > -7.51);

    canproxy_telltales_t t = {}, t2 = {};
    t.bits = CANPROXY_TT_ABS | CANPROXY_TT_LOW_FUEL;
    canproxy_pack_telltales(&t, d);
    canproxy_unpack_telltales(d, &t2);
    CHECK(t2.bits == t.bits);

    canproxy_energy_t en = {}, en2 = {};
    en.soc_pct = 55.5; en.soc_valid = true;
    canproxy_pack_energy(&en, d);
    canproxy_unpack_energy(d, &en2);
    CHECK(en2.soc_valid && en2.soc_pct == 55.5);

    canproxy_trip_t tr = {}, tr2 = {};
    tr.ambient_c = -3; tr.ambient_valid = true;
    canproxy_pack_trip(&tr, d);
    canproxy_unpack_trip(d, &tr2);
    CHECK(tr2.ambient_valid && tr2.ambient_c == -3 && !tr2.odometer_valid);

    canproxy_thermal_t th = {}, th2 = {};
    th.aux_battery_v = 13.8; th.aux_battery_valid = true;
    canproxy_pack_thermal(&th, d);
    canproxy_unpack_thermal(d, &th2);
    CHECK(th2.aux_battery_valid && th2.aux_battery_v > 13.79 && th2.aux_battery_v < 13.81);

    canproxy_assist_t a = {}, a2 = {};
    a.collision_risk = CANPROXY_RISK_HIGH; a.collision_risk_valid = true;
    canproxy_pack_assist(&a, d);
    canproxy_unpack_assist(d, &a2);
    CHECK(a2.collision_risk_valid && a2.collision_risk == CANPROXY_RISK_HIGH);

    canproxy_identity_t id = {}, id2 = {};
    id.drivetrain = CANPROXY_DRIVETRAIN_HEV;
    id.vehicle_id = canproxy_fnv1a32("sim");
    canproxy_pack_identity(&id, d);
    canproxy_unpack_identity(d, &id2);
    CHECK(std::memcmp(&id, &id2, sizeof id) == 0);

    std::printf("contract_cxx_tests: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
