// sim: a scripted drive cycle with no vehicle bus. Any drivetrain.
//
//   --plugin-arg drivetrain=ice|bev|hev|phev   (default bev)
//   --plugin-arg assist=0                       do not offer the assist signals
//   --plugin-arg link=0                         report "no vehicle" forever
//   --plugin-arg stall_after_ms=<n>             stop publishing after n ms (test hook)
//   --plugin-arg tick_ms=<n>                    update period (default 20)
//
// The cycle mirrors the cluster's demo simulator so the two are comparable
// on screen. Physics are deliberately simple; the point is plausible,
// continuous signals with honest validity, not a vehicle model.
#include "canproxy/plugin.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

struct Phase { int durMs; double spd0, spd1; double rpm0, rpm1; double tmp0, tmp1; uint32_t set, clr; int limit; };

constexpr uint32_t L(canproxy_lamp l) { return CANPROXY_LAMP_BIT(l); }

const Phase kPhases[] = {
    { 2000,   0,  40,  800, 2500, 70, 75, L(CANPROXY_LAMP_TURN_LEFT) | L(CANPROXY_LAMP_SEATBELT), 0, 50 },
    { 2000,  40,  80, 2500, 3500, 75, 80, 0, L(CANPROXY_LAMP_TURN_LEFT) | L(CANPROXY_LAMP_SEATBELT), 80 },
    { 3000,  80, 120, 2000, 3500, 80, 85, L(CANPROXY_LAMP_ENGINE), 0, 120 },
    { 8000, 120, 120, 2500, 2500, 85, 90, L(CANPROXY_LAMP_HIGH_BEAM), L(CANPROXY_LAMP_ENGINE), 120 },
    { 3000, 120, 160, 2500, 4500, 90, 90, L(CANPROXY_LAMP_TPMS), L(CANPROXY_LAMP_HIGH_BEAM), 0 },
    { 6000, 160, 160, 3200, 3200, 90, 90, L(CANPROXY_LAMP_ABS), L(CANPROXY_LAMP_TPMS), 0 },
    { 3000, 160, 200, 3200, 5500, 90, 92, L(CANPROXY_LAMP_TRACTION), L(CANPROXY_LAMP_ABS), 0 },
    { 4000, 200, 200, 4000, 4000, 92, 92, L(CANPROXY_LAMP_OIL) | L(CANPROXY_LAMP_BATTERY), L(CANPROXY_LAMP_TRACTION), 0 },
    { 4000, 200,  80, 4000, 1800, 92, 88, L(CANPROXY_LAMP_BRAKE), L(CANPROXY_LAMP_OIL) | L(CANPROXY_LAMP_BATTERY), 100 },
    { 3000,  80,  40, 1800, 1200, 88, 86, L(CANPROXY_LAMP_TURN_RIGHT) | L(CANPROXY_LAMP_DOOR), 0, 50 },
    { 3000,  40,   0, 1200,  800, 86, 85, 0, L(CANPROXY_LAMP_TURN_RIGHT) | L(CANPROXY_LAMP_DOOR), 50 },
    { 3000,   0,   0,  800,  800, 85, 80, 0, L(CANPROXY_LAMP_BRAKE), 50 },
};
constexpr int kPhaseCount = sizeof(kPhases) / sizeof(kPhases[0]);

struct Sim {
    canproxy_host host{};
    int drivetrain = CP_DRIVETRAIN_BEV;
    bool assist = true;
    bool link = true;
    long stallAfterMs = -1;
    int tickMs = 20;

    std::thread thread;
    std::atomic<bool> running{false};

    // evolving quantities
    int phase = 0;
    double phaseElapsedMs = 0;
    double lamps = 0;
    uint32_t lampBits = 0;
    double soc = 80.0, fuel = 75.0, odo = 10568.0;
    double powerFiltered = 0.0;
    double tripWh = 150.0, tripKm = 1.0;      // seeded so consumption starts at 150 Wh/km
    double lastSpeed = 0.0;
    double elapsedMs = 0;

    void logf(int level, const char *fmt, ...) __attribute__((format(printf, 3, 4)))
    {
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        if (host.log) host.log(host.ctx, level, buf);
    }

    bool electric() const { return drivetrain != CP_DRIVETRAIN_ICE; }
    bool combustion() const { return drivetrain == CP_DRIVETRAIN_ICE || drivetrain == CP_DRIVETRAIN_HEV || drivetrain == CP_DRIVETRAIN_PHEV; }

    uint32_t capable() const
    {
        uint32_t c = CANPROXY_SIG_BIT(CANPROXY_SIG_SPEED) | CANPROXY_SIG_BIT(CANPROXY_SIG_RPM) |
                     CANPROXY_SIG_BIT(CANPROXY_SIG_GEAR) | CANPROXY_SIG_BIT(CANPROXY_SIG_POWER_STATE) |
                     CANPROXY_SIG_BIT(CANPROXY_SIG_ODOMETER) | CANPROXY_SIG_BIT(CANPROXY_SIG_AMBIENT_TEMP) |
                     CANPROXY_SIG_BIT(CANPROXY_SIG_CABIN_TEMP) | CANPROXY_SIG_BIT(CANPROXY_SIG_COOLANT_TEMP) |
                     CANPROXY_SIG_BIT(CANPROXY_SIG_AUX_BATTERY);
        if (combustion())
            c |= CANPROXY_SIG_BIT(CANPROXY_SIG_FUEL_LEVEL);
        if (electric())
            c |= CANPROXY_SIG_BIT(CANPROXY_SIG_MOTOR_POWER) | CANPROXY_SIG_BIT(CANPROXY_SIG_PACK_VOLTAGE) |
                 CANPROXY_SIG_BIT(CANPROXY_SIG_PACK_CURRENT) | CANPROXY_SIG_BIT(CANPROXY_SIG_SOC) |
                 CANPROXY_SIG_BIT(CANPROXY_SIG_SOH) | CANPROXY_SIG_BIT(CANPROXY_SIG_RANGE) |
                 CANPROXY_SIG_BIT(CANPROXY_SIG_CONSUMPTION) | CANPROXY_SIG_BIT(CANPROXY_SIG_CHARGING_STATE);
        if (assist)
            c |= CANPROXY_SIG_BIT(CANPROXY_SIG_ECO_SCORE) | CANPROXY_SIG_BIT(CANPROXY_SIG_SPEED_LIMIT) |
                 CANPROXY_SIG_BIT(CANPROXY_SIG_COLLISION_RISK) | CANPROXY_SIG_BIT(CANPROXY_SIG_LANE_STATE) |
                 CANPROXY_SIG_BIT(CANPROXY_SIG_LEAD_GAP);
        return c;
    }

    void step(double dtMs, canproxy_vehicle_state &s)
    {
        const Phase &p = kPhases[phase];
        phaseElapsedMs += dtMs;
        elapsedMs += dtMs;
        if (phaseElapsedMs >= p.durMs) {
            phaseElapsedMs -= p.durMs;
            phase = (phase + 1) % kPhaseCount;
        }
        const Phase &q = kPhases[phase];
        const double t = phaseElapsedMs / q.durMs;
        if (phaseElapsedMs < dtMs + 1e-9) {          // entered this phase
            lampBits |= q.set;
            lampBits &= ~q.clr;
        }

        const double speed = q.spd0 + (q.spd1 - q.spd0) * t;
        const double dtH = dtMs / 3.6e6;
        const double accel = (speed - lastSpeed) / 3.6 / (dtMs / 1000.0);   // m/s²
        lastSpeed = speed;

        // tractive power: mass 1700 kg, drag, rolling resistance; regen on decel
        const double v = speed / 3.6;
        const double force = 1500.0 * accel + 0.5 * 1.2 * 0.29 * 2.3 * v * v + 0.012 * 1500.0 * 9.81 * (v > 0.1 ? 1 : 0);
        double power = force * v / 1000.0;                                     // kW
        if (power < 0) power *= 0.6;                                            // regen efficiency
        if (power < -60) power = -60;
        if (power > 120) power = 120;
        if (speed < 0.5) power = 0.3;                                           // auxiliaries
        powerFiltered += (power - powerFiltered) * 0.15;

        odo += speed * dtH;
        if (electric()) {
            soc -= powerFiltered * dtH / 64.0 * 100.0;                          // 64 kWh pack
            if (soc < 5) soc = 80;
            tripWh += powerFiltered * 1000.0 * dtH;                             // trip-average consumption
            tripKm += speed * dtH;
        }
        const double consumption = tripWh / tripKm;
        if (combustion()) {
            fuel -= (0.5 + (powerFiltered > 0 ? powerFiltered : 0) * 0.05) * dtMs / 60000.0;
            if (fuel < 3) fuel = 75;
        }

        std::memset(&s, 0, sizeof s);
        s.capable = capable();
        s.valid = s.capable;                                                    // sim always knows everything
        s.drivetrain = drivetrain;
        s.source = CP_SOURCE_SIMULATED;
        s.vehicle_id = canproxy_vehicle_id("sim", nullptr);

        s.speed_kmh = speed;
        s.rpm = electric() && drivetrain == CP_DRIVETRAIN_BEV ? speed * 75.0 : q.rpm0 + (q.rpm1 - q.rpm0) * t;
        s.gear = speed > 0.5 ? CP_GEAR_D : CP_GEAR_P;
        s.power_state = CP_POWER_READY;

        s.motor_power_kw = powerFiltered;
        s.pack_voltage_v = 340.0 + soc * 0.6;
        s.pack_current_a = powerFiltered * 1000.0 / s.pack_voltage_v;
        s.soc_pct = soc;
        s.soh_pct = 97.0;
        s.consumption_wh_km = consumption;
        s.range_km = soc / 100.0 * 64000.0 / (consumption > 80 ? consumption : 80);
        s.charging_state = CP_CHARGING_NONE;

        s.odometer_km = odo;
        s.ambient_c = 23.0;
        s.cabin_c = 21.5;
        s.coolant_c = electric() && !combustion() ? 35.0 + speed * 0.05 : q.tmp0 + (q.tmp1 - q.tmp0) * t;
        s.fuel_pct = fuel;
        s.aux_battery_v = 12.6 + (speed > 0.5 ? 1.6 : 0.0);

        uint32_t lamps = lampBits;
        if (drivetrain == CP_DRIVETRAIN_BEV) lamps |= L(CANPROXY_LAMP_EV_READY);
        if (combustion() && fuel < 12) lamps |= L(CANPROXY_LAMP_LOW_FUEL);
        if (electric() && soc < 15) lamps |= L(CANPROXY_LAMP_LOW_TRACTION_BATTERY);
        if (speed < 0.5 && phase == kPhaseCount - 1) lamps |= L(CANPROXY_LAMP_PARK_BRAKE);
        s.lamps = lamps;

        const double absP = std::fabs(powerFiltered);
        s.eco_score = static_cast<int>(100.0 - (absP > 100 ? 100 : absP));
        s.speed_limit_kmh = q.limit;
        s.collision_risk = (accel < -3.0) ? CP_RISK_HIGH : (accel < -1.5 ? CP_RISK_MEDIUM : (accel < -0.5 ? CP_RISK_LOW : CP_RISK_NONE));
        s.lane_state = speed > 30 ? (CP_LANE_LEFT_SEEN | CP_LANE_RIGHT_SEEN) : 0u;
        s.lead_gap_m = 25.0 + speed * 0.6;
    }

    void run()
    {
        using clock = std::chrono::steady_clock;
        host.set_link(host.ctx, link ? 1 : 0);
        if (!link) {
            logf(CANPROXY_LOG_INFO, "link=0: reporting no vehicle");
            while (running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return;
        }
        auto next = clock::now();
        bool stalled = false;
        canproxy_vehicle_state s;
        while (running.load()) {
            next += std::chrono::milliseconds(tickMs);
            step(tickMs, s);
            if (stallAfterMs >= 0 && elapsedMs >= stallAfterMs) {
                if (!stalled) { logf(CANPROXY_LOG_WARN, "stall_after_ms reached: no more publishes"); stalled = true; }
            } else {
                host.publish(host.ctx, &s);
            }
            std::this_thread::sleep_until(next);
        }
    }
};

void *simCreate(const canproxy_plugin_args *args, const canproxy_host *host)
{
    auto *sim = new Sim;
    sim->host = *host;
    if (const char *d = canproxy_arg(args, "drivetrain")) {
        std::string v = d;
        if (v == "ice") sim->drivetrain = CP_DRIVETRAIN_ICE;
        else if (v == "bev") sim->drivetrain = CP_DRIVETRAIN_BEV;
        else if (v == "hev") sim->drivetrain = CP_DRIVETRAIN_HEV;
        else if (v == "phev") sim->drivetrain = CP_DRIVETRAIN_PHEV;
        else { sim->logf(CANPROXY_LOG_ERROR, "unknown drivetrain '%s' (ice|bev|hev|phev)", d); delete sim; return nullptr; }
    }
    if (const char *a = canproxy_arg(args, "assist")) sim->assist = std::atoi(a) != 0;
    if (const char *l = canproxy_arg(args, "link")) sim->link = std::atoi(l) != 0;
    if (const char *st = canproxy_arg(args, "stall_after_ms")) sim->stallAfterMs = std::atol(st);
    if (const char *t = canproxy_arg(args, "tick_ms")) {
        sim->tickMs = std::atoi(t);
        if (sim->tickMs < 5 || sim->tickMs > 1000) { sim->logf(CANPROXY_LOG_ERROR, "tick_ms out of range 5..1000"); delete sim; return nullptr; }
    }
    static const char *names[] = { "unknown", "ice", "bev", "hev", "phev", "fcev" };
    sim->logf(CANPROXY_LOG_INFO, "drivetrain=%s assist=%d tick=%dms", names[sim->drivetrain], sim->assist ? 1 : 0, sim->tickMs);
    return sim;
}

int simStart(void *self)
{
    auto *sim = static_cast<Sim *>(self);
    if (sim->running.exchange(true)) return 0;
    sim->thread = std::thread([sim] { sim->run(); });
    return 0;
}

void simStop(void *self)
{
    auto *sim = static_cast<Sim *>(self);
    if (!sim->running.exchange(false)) return;
    if (sim->thread.joinable()) sim->thread.join();
}

void simDestroy(void *self) { delete static_cast<Sim *>(self); }

const canproxy_plugin kPlugin = {
    CANPROXY_PLUGIN_ABI_VERSION, "sim", 1, 0, simCreate, simStart, simStop, simDestroy,
};

} // namespace

extern "C" const canproxy_plugin *canproxy_plugin_entry(void) { return &kPlugin; }
