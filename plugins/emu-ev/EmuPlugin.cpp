// emu-ev / emu-hybrid: J1979 over the raw channel for what the OBD port
// offers, UDS ReadDataByIdentifier over ISO-TP to the emulator's reference
// battery ECU for everything electric. See docs/emulator-ev-profile.md.
//
//   --plugin-arg source=emulator|replay|live   (default emulator)
//   --plugin-arg fast_ms=<n>                   fast tier (default 100)
//   --plugin-arg record=<file>                 raw-channel candump log
#include "EmuPlugin.h"
#include "EmuProfile.h"
#include "obd/IsoTp.h"
#include "obd/J1979Client.h"
#include "obd/Poller.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <thread>

namespace {

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Poller keys for the DIDs (the Poller is keyed by a byte).
constexpr uint8_t kKeyPack = 0xF1, kKeyRange = 0xF2, kKeyDrive = 0xF3, kKeyAssist = 0xF4;

struct Plugin {
    bool hybrid = false;
    canproxy_host host{};
    std::string vehicleIf;
    int source = CP_SOURCE_EMULATOR;
    int64_t fastMs = 100;
    std::string recordPath;

    std::thread thread;
    std::atomic<bool> running{false};

    obd::J1979Client client;
    obd::IsoTpChannel bms;
    obd::Poller poller;
    bool discovered = false;
    bool bmsPresent = false;
    bool assistPresent = false;
    std::set<uint8_t> supported;
    std::map<uint8_t, double> values;
    emu::Pack pack{};
    emu::Range range{};
    emu::Drive drive{};
    emu::Assist assist{};
    uint32_t lamps = 0;
    int64_t lampsSeenMs = -1;

    void logf(int level, const char *fmt, ...) __attribute__((format(printf, 3, 4)))
    {
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        if (host.log) host.log(host.ctx, level, buf);
    }

    void onTelltales(const obd::Frame &f)
    {
        if (f.dlc < 2) return;
        uint32_t bits = f.data[0] | (uint32_t(f.data[1]) << 8);
        if (f.dlc >= 4)
            bits |= (uint32_t(f.data[2]) << 16) | (uint32_t(f.data[3]) << 24);
        lamps = bits;
        lampsSeenMs = nowMs();
    }

    bool discover()
    {
        if (!client.discover(300))
            return false;
        supported = client.supported();
        poller.clear();
        values.clear();
        auto want = [&](uint8_t pid, int64_t interval) {
            if (supported.count(pid)) poller.add(pid, interval);
        };
        want(obd::PID_SPEED, fastMs);
        if (hybrid) {
            want(obd::PID_RPM, fastMs);
            want(obd::PID_COOLANT_TEMP, fastMs * 5);
            want(obd::PID_FUEL_LEVEL, fastMs * 20);
        }
        want(obd::PID_MODULE_VOLTAGE, fastMs * 20);
        want(obd::PID_AMBIENT_TEMP, fastMs * 20);
        want(obd::PID_ODOMETER, fastMs * 20);

        // The battery ECU: present if it answers the pack DID once.
        bmsPresent = false;
        if (bms.isOpen()) {
            auto r = obd::readDid(bms, emu::kDidPack, 300);
            bmsPresent = r && r->positive;
        }
        if (bmsPresent) {
            poller.add(kKeyDrive, fastMs);
            poller.add(kKeyPack, fastMs);
            poller.add(kKeyRange, fastMs * 5);
            // Driver-assist record: optional even on a vehicle with a battery ECU.
            auto a = obd::readDid(bms, emu::kDidAssist, 300);
            assistPresent = a && a->positive;
            if (assistPresent)
                poller.add(kKeyAssist, fastMs * 2);
        } else {
            assistPresent = false;
        }
        logf(CANPROXY_LOG_INFO, "ECU 0x%03X advertises %zu PIDs; battery ECU %s, driver assist %s",
             client.primaryEcu(), supported.size(), bmsPresent ? "present" : "absent",
             assistPresent ? "present" : "absent");
        return true;
    }

    bool pollDid(uint8_t key)
    {
        const uint16_t did = key == kKeyPack ? emu::kDidPack : key == kKeyRange ? emu::kDidRange
                           : key == kKeyAssist ? emu::kDidAssist : emu::kDidDrive;
        auto r = obd::readDid(bms, did, static_cast<int>(fastMs));
        if (!r || !r->positive)
            return false;
        switch (key) {
        case kKeyPack:  return emu::decodePack(r->data, pack);
        case kKeyRange:  return emu::decodeRange(r->data, range);
        case kKeyAssist: return emu::decodeAssist(r->data, assist);
        default:         return emu::decodeDrive(r->data, drive);
        }
    }

    void publish(int64_t now)
    {
        canproxy_vehicle_state s;
        std::memset(&s, 0, sizeof s);
        s.drivetrain = hybrid ? CP_DRIVETRAIN_HEV : CP_DRIVETRAIN_BEV;
        s.source = source;
        s.vehicle_id = canproxy_vehicle_id(hybrid ? "emu-hybrid" : "emu-ev", nullptr);

        auto capPid = [&](uint8_t pid, canproxy_signal sig, double *field) {
            if (!supported.count(pid)) return;
            s.capable |= CANPROXY_SIG_BIT(sig);
            auto it = values.find(pid);
            if (it != values.end() && poller.fresh(pid, now)) {
                s.valid |= CANPROXY_SIG_BIT(sig);
                *field = it->second;
            }
        };
        capPid(obd::PID_SPEED, CANPROXY_SIG_SPEED, &s.speed_kmh);
        capPid(obd::PID_MODULE_VOLTAGE, CANPROXY_SIG_AUX_BATTERY, &s.aux_battery_v);
        capPid(obd::PID_AMBIENT_TEMP, CANPROXY_SIG_AMBIENT_TEMP, &s.ambient_c);
        capPid(obd::PID_ODOMETER, CANPROXY_SIG_ODOMETER, &s.odometer_km);
        if (hybrid) {
            capPid(obd::PID_RPM, CANPROXY_SIG_RPM, &s.rpm);
            capPid(obd::PID_COOLANT_TEMP, CANPROXY_SIG_COOLANT_TEMP, &s.coolant_c);
            capPid(obd::PID_FUEL_LEVEL, CANPROXY_SIG_FUEL_LEVEL, &s.fuel_pct);
        }

        if (bmsPresent) {
            const uint32_t packSigs = CANPROXY_SIG_BIT(CANPROXY_SIG_PACK_VOLTAGE) | CANPROXY_SIG_BIT(CANPROXY_SIG_PACK_CURRENT) |
                                      CANPROXY_SIG_BIT(CANPROXY_SIG_SOC) | CANPROXY_SIG_BIT(CANPROXY_SIG_SOH) |
                                      CANPROXY_SIG_BIT(CANPROXY_SIG_CHARGING_STATE);
            const uint32_t rangeSigs = CANPROXY_SIG_BIT(CANPROXY_SIG_RANGE) | CANPROXY_SIG_BIT(CANPROXY_SIG_CONSUMPTION);
            uint32_t driveSigs = CANPROXY_SIG_BIT(CANPROXY_SIG_GEAR) | CANPROXY_SIG_BIT(CANPROXY_SIG_POWER_STATE) |
                                 CANPROXY_SIG_BIT(CANPROXY_SIG_MOTOR_POWER);
            if (!hybrid) driveSigs |= CANPROXY_SIG_BIT(CANPROXY_SIG_RPM);   // motor speed is "the" rpm on a BEV
            s.capable |= packSigs | rangeSigs | driveSigs;

            if (poller.fresh(kKeyPack, now)) {
                s.valid |= packSigs;
                s.pack_voltage_v = pack.voltageV;
                s.pack_current_a = pack.currentA;
                s.soc_pct = pack.socPct;
                s.soh_pct = pack.sohPct;
                s.charging_state = pack.charging;
            }
            if (poller.fresh(kKeyRange, now)) {
                s.valid |= rangeSigs;
                s.range_km = range.rangeKm;
                s.consumption_wh_km = range.consumptionWhKm;
            }
            if (poller.fresh(kKeyDrive, now)) {
                s.valid |= driveSigs;
                s.gear = drive.gear;
                s.power_state = drive.powerState;
                s.motor_power_kw = drive.motorPowerKw;
                if (!hybrid) s.rpm = drive.motorRpm;
            }
        }

        if (assistPresent) {
            const uint32_t assistSigs = CANPROXY_SIG_BIT(CANPROXY_SIG_ECO_SCORE) | CANPROXY_SIG_BIT(CANPROXY_SIG_SPEED_LIMIT) |
                                        CANPROXY_SIG_BIT(CANPROXY_SIG_COLLISION_RISK) | CANPROXY_SIG_BIT(CANPROXY_SIG_LANE_STATE) |
                                        CANPROXY_SIG_BIT(CANPROXY_SIG_LEAD_GAP);
            s.capable |= assistSigs;
            if (poller.fresh(kKeyAssist, now)) {
                s.valid |= assistSigs;
                s.eco_score = assist.ecoScore;
                s.speed_limit_kmh = assist.speedLimitKmh;
                s.collision_risk = assist.collisionRisk;
                s.lane_state = assist.laneState;
                s.lead_gap_m = assist.leadGapM;
            }
        }

        uint32_t l = (lampsSeenMs >= 0 && now - lampsSeenMs <= 500) ? lamps : 0;
        if (s.valid & CANPROXY_SIG_BIT(CANPROXY_SIG_POWER_STATE)) {
            if (!hybrid && s.power_state == CP_POWER_READY) l |= CANPROXY_LAMP_BIT(CANPROXY_LAMP_EV_READY);
        }
        if ((s.valid & CANPROXY_SIG_BIT(CANPROXY_SIG_CHARGING_STATE)) &&
                (s.charging_state == CP_CHARGING_AC || s.charging_state == CP_CHARGING_DC))
            l |= CANPROXY_LAMP_BIT(CANPROXY_LAMP_CHARGING);
        if ((s.valid & CANPROXY_SIG_BIT(CANPROXY_SIG_SOC)) && s.soc_pct < 10.0)
            l |= CANPROXY_LAMP_BIT(CANPROXY_LAMP_LOW_TRACTION_BATTERY);
        s.lamps = l;
        host.publish(host.ctx, &s);
    }

    void run()
    {
        int64_t nextPublish = 0;
        while (running.load()) {
            const int64_t now = nowMs();
            if (!discovered) {
                if (discover()) {
                    discovered = true;
                    host.set_link(host.ctx, 1);
                } else {
                    host.set_link(host.ctx, 0);
                    values.clear();
                    publish(now);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    continue;
                }
            }
            if (auto key = poller.nextDue(now)) {
                poller.markRequested(*key, now);
                bool ok;
                if (*key >= 0xF0) {
                    ok = pollDid(*key);
                } else {
                    double v = 0;
                    ok = client.query(*key, static_cast<int>(fastMs), &v);
                    if (ok) values[*key] = v;
                }
                if (ok) {
                    poller.markAnswered(*key, nowMs());
                } else if (poller.unansweredStreak() >= 8) {
                    logf(CANPROXY_LOG_WARN, "no answers, vehicle gone");
                    discovered = false;
                    host.set_link(host.ctx, 0);
                    continue;
                }
            } else {
                const int64_t wait = poller.nextDeadline(now) - now;
                client.idle(static_cast<int>(wait > 50 ? 50 : (wait < 1 ? 1 : wait)));
            }
            if (nowMs() >= nextPublish) {
                publish(nowMs());
                nextPublish = nowMs() + 50;
            }
        }
    }
};


void *createImpl(const canproxy_plugin_args *args, const canproxy_host *host, bool hybrid)
{
    auto *p = new Plugin;
    p->hybrid = hybrid;
    p->host = *host;
    if (!args->vehicle_if) {
        p->logf(CANPROXY_LOG_ERROR, "%s needs --vehicle-if", hybrid ? "emu-hybrid" : "emu-ev");
        delete p;
        return nullptr;
    }
    p->vehicleIf = args->vehicle_if;
    if (const char *s = canproxy_arg(args, "source")) {
        std::string v = s;
        if (v == "emulator") p->source = CP_SOURCE_EMULATOR;
        else if (v == "replay") p->source = CP_SOURCE_REPLAY;
        else if (v == "live") p->source = CP_SOURCE_LIVE;
        else { p->logf(CANPROXY_LOG_ERROR, "unknown source '%s'", s); delete p; return nullptr; }
    }
    if (const char *f = canproxy_arg(args, "fast_ms")) {
        p->fastMs = std::atol(f);
        if (p->fastMs < 20 || p->fastMs > 5000) { p->logf(CANPROXY_LOG_ERROR, "fast_ms out of range 20..5000"); delete p; return nullptr; }
    }
    if (const char *r = canproxy_arg(args, "record"))
        p->recordPath = r;
    return p;
}

void *createEv(const canproxy_plugin_args *a, const canproxy_host *h) { return createImpl(a, h, false); }
void *createHybrid(const canproxy_plugin_args *a, const canproxy_host *h) { return createImpl(a, h, true); }

int start(void *self)
{
    auto *p = static_cast<Plugin *>(self);
    if (p->running.exchange(true))
        return 0;
    p->client.setBroadcastSink(0x420, [p](const obd::Frame &f) { p->onTelltales(f); });
    if (const std::string e = p->client.open(p->vehicleIf); !e.empty()) {
        p->logf(CANPROXY_LOG_ERROR, "%s", e.c_str());
        p->running = false;
        return 1;
    }
    if (const std::string e = p->bms.open(p->vehicleIf, emu::kBmsRequestId, emu::kBmsResponseId); !e.empty())
        p->logf(CANPROXY_LOG_WARN, "battery ECU channel unavailable: %s", e.c_str());
    if (!p->recordPath.empty()) {
        if (const std::string e = p->client.channel().startRecording(p->recordPath); !e.empty()) {
            p->logf(CANPROXY_LOG_ERROR, "%s", e.c_str());
            p->running = false;
            return 1;
        }
        p->logf(CANPROXY_LOG_INFO, "recording raw vehicle side to %s (ISO-TP transfers are not captured)", p->recordPath.c_str());
    }
    p->thread = std::thread([p] { p->run(); });
    return 0;
}

void stop(void *self)
{
    auto *p = static_cast<Plugin *>(self);
    if (!p->running.exchange(false))
        return;
    if (p->thread.joinable())
        p->thread.join();
    p->client.channel().stopRecording();
    p->client.close();
    p->bms.close();
}

void destroy(void *self) { delete static_cast<Plugin *>(self); }

const canproxy_plugin kEv = { CANPROXY_PLUGIN_ABI_VERSION, "emu-ev", 1, 0, createEv, start, stop, destroy };
const canproxy_plugin kHybrid = { CANPROXY_PLUGIN_ABI_VERSION, "emu-hybrid", 1, 0, createHybrid, start, stop, destroy };

} // namespace

const canproxy_plugin *emuPluginDescriptor(bool hybrid) { return hybrid ? &kHybrid : &kEv; }
