// obd2-ice: a J1979 (OBD-II) poller for combustion vehicles. Written to the
// standard, not to the bench emulator: it discovers what the ECU advertises
// and polls only that, so a signal the car does not publish is never shown.
//
//   --plugin-arg source=live|emulator     identity source kind (default live)
//   --plugin-arg fast_ms=<n>              fast tier period (default 100)
//   --plugin-arg telltale_id=<hex|none>   broadcast lamp frame on the vehicle
//                                         bus; default 0x420 for source=emulator,
//                                         none for live
//   --plugin-arg record=<file>            candump-format log of the vehicle side
//                                         (the daemon's --record sets this)
//
// Poll tiers: speed and rpm every fast_ms; coolant every 5x; fuel, module
// voltage, ambient and odometer every 20x. A signal is valid while its last
// answer is younger than three of its own intervals; five unanswered
// requests in a row mean the vehicle is gone and discovery starts over.
//
// Plausibility: some devices (the first hardware OBD-II emulator this met
// on a CANable) advertise every PID and answer the ones they do not really
// have with zero payloads. A module voltage below 6 V and an ambient
// temperature of exactly -40 °C (raw 0x00) are the two such fillers that
// would otherwise reach a gauge as measurements. A PID answering one is
// dropped from the advertised set for the session (its capability bit
// clears, the gauge hides) and logged once. Nothing else is second-guessed.
#include "canproxy/plugin.h"
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

using Clock = std::chrono::steady_clock;

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

struct Plugin {
    canproxy_host host{};
    std::string vehicleIf;
    int source = CP_SOURCE_LIVE;
    int64_t fastMs = 100;
    long telltaleId = -1;
    std::string recordPath;

    std::thread thread;
    std::atomic<bool> running{false};

    obd::J1979Client client;
    obd::Poller poller;
    bool discovered = false;
    std::set<uint8_t> supported;
    std::map<uint8_t, double> values;
    uint32_t lamps = 0;
    int64_t lampsSeenMs = -1;
    std::set<uint8_t> implausibleLogged;

    // False for the two known "not really populated" answers; see the header
    // comment. Drops the PID from the session's advertised set.
    bool plausible(uint8_t pid, double v)
    {
        bool ok = true;
        if (pid == obd::PID_MODULE_VOLTAGE && v < 6.0) ok = false;
        if (pid == obd::PID_AMBIENT_TEMP && v <= -40.0) ok = false;
        if (!ok) {
            if (!implausibleLogged.count(pid)) {
                implausibleLogged.insert(pid);
                logf(CANPROXY_LOG_WARN, "PID 0x%02X (%s) answers %.1f, an unpopulated filler; dropping it for this session",
                     pid, obd::pidName(pid), v);
            }
            supported.erase(pid);
            poller.remove(pid);
            values.erase(pid);
        }
        return ok;
    }

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
        lamps = bits;                       // contract bit order == lamp order
        lampsSeenMs = nowMs();
    }

    // Discovery replaces `supported` only on success: a vehicle that goes
    // away keeps advertising what it could provide, so the cluster's layout
    // holds while the values go unknown.
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
        want(obd::PID_RPM, fastMs);
        want(obd::PID_COOLANT_TEMP, fastMs * 5);
        want(obd::PID_FUEL_LEVEL, fastMs * 20);
        want(obd::PID_MODULE_VOLTAGE, fastMs * 20);
        want(obd::PID_AMBIENT_TEMP, fastMs * 20);
        want(obd::PID_ODOMETER, fastMs * 20);

        std::string list;
        for (uint8_t pid : supported) {
            char b[8];
            std::snprintf(b, sizeof b, " %02X", pid);
            list += b;
        }
        logf(CANPROXY_LOG_INFO, "ECU 0x%03X advertises%s", client.primaryEcu(), list.c_str());
        return true;
    }

    void publish(int64_t now)
    {
        canproxy_vehicle_state s;
        std::memset(&s, 0, sizeof s);
        s.drivetrain = CP_DRIVETRAIN_ICE;
        s.source = source;
        s.vehicle_id = canproxy_vehicle_id("obd2-ice", nullptr);

        auto cap = [&](uint8_t pid, canproxy_signal sig, double *field, double scale = 1.0) {
            if (!supported.count(pid)) return;
            s.capable |= CANPROXY_SIG_BIT(sig);
            auto it = values.find(pid);
            if (it != values.end() && poller.fresh(pid, now)) {
                s.valid |= CANPROXY_SIG_BIT(sig);
                *field = it->second * scale;
            }
        };
        cap(obd::PID_SPEED, CANPROXY_SIG_SPEED, &s.speed_kmh);
        cap(obd::PID_RPM, CANPROXY_SIG_RPM, &s.rpm);
        cap(obd::PID_COOLANT_TEMP, CANPROXY_SIG_COOLANT_TEMP, &s.coolant_c);
        cap(obd::PID_FUEL_LEVEL, CANPROXY_SIG_FUEL_LEVEL, &s.fuel_pct);
        cap(obd::PID_MODULE_VOLTAGE, CANPROXY_SIG_AUX_BATTERY, &s.aux_battery_v);
        cap(obd::PID_AMBIENT_TEMP, CANPROXY_SIG_AMBIENT_TEMP, &s.ambient_c);
        cap(obd::PID_ODOMETER, CANPROXY_SIG_ODOMETER, &s.odometer_km);

        // Engine running is a fact the rpm carries; "on" because an ECU that
        // answers has ignition. Not invented, just read off.
        if (supported.count(obd::PID_RPM)) {
            s.capable |= CANPROXY_SIG_BIT(CANPROXY_SIG_POWER_STATE);
            if (s.valid & CANPROXY_SIG_BIT(CANPROXY_SIG_RPM)) {
                s.valid |= CANPROXY_SIG_BIT(CANPROXY_SIG_POWER_STATE);
                s.power_state = s.rpm > 0 ? CP_POWER_READY : CP_POWER_ON;
            }
        }
        if (lampsSeenMs >= 0 && now - lampsSeenMs <= 500)
            s.lamps = lamps;
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

            if (auto pid = poller.nextDue(now)) {
                poller.markRequested(*pid, now);
                double v = 0;
                if (client.query(*pid, static_cast<int>(fastMs), &v)) {
                    if (plausible(*pid, v)) {
                        values[*pid] = v;
                        poller.markAnswered(*pid, nowMs());
                    }
                } else if (poller.unansweredStreak() >= 5) {
                    logf(CANPROXY_LOG_WARN, "no answer from ECU 0x%03X, vehicle gone", client.primaryEcu());
                    discovered = false;
                    host.set_link(host.ctx, 0);
                    continue;
                }
            } else {
                // Idle until something is due, but keep draining broadcasts.
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

void *create(const canproxy_plugin_args *args, const canproxy_host *host)
{
    auto *p = new Plugin;
    p->host = *host;
    if (!args->vehicle_if) {
        p->logf(CANPROXY_LOG_ERROR, "obd2-ice needs --vehicle-if");
        delete p;
        return nullptr;
    }
    p->vehicleIf = args->vehicle_if;
    if (const char *s = canproxy_arg(args, "source")) {
        std::string v = s;
        if (v == "live") p->source = CP_SOURCE_LIVE;
        else if (v == "emulator") p->source = CP_SOURCE_EMULATOR;
        else if (v == "replay") p->source = CP_SOURCE_REPLAY;
        else { p->logf(CANPROXY_LOG_ERROR, "unknown source '%s' (live|emulator|replay)", s); delete p; return nullptr; }
    }
    if (const char *f = canproxy_arg(args, "fast_ms")) {
        p->fastMs = std::atol(f);
        if (p->fastMs < 20 || p->fastMs > 5000) { p->logf(CANPROXY_LOG_ERROR, "fast_ms out of range 20..5000"); delete p; return nullptr; }
    }
    p->telltaleId = (p->source == CP_SOURCE_LIVE) ? -1 : 0x420;
    if (const char *t = canproxy_arg(args, "telltale_id")) {
        if (std::string(t) == "none") p->telltaleId = -1;
        else p->telltaleId = std::strtol(t, nullptr, 0);
    }
    if (const char *r = canproxy_arg(args, "record"))
        p->recordPath = r;
    return p;
}

int start(void *self)
{
    auto *p = static_cast<Plugin *>(self);
    if (p->running.exchange(true))
        return 0;
    if (p->telltaleId >= 0)
        p->client.setBroadcastSink(p->telltaleId, [p](const obd::Frame &f) { p->onTelltales(f); });
    if (const std::string e = p->client.open(p->vehicleIf); !e.empty()) {
        p->logf(CANPROXY_LOG_ERROR, "%s", e.c_str());
        p->running = false;
        return 1;
    }
    if (!p->recordPath.empty()) {
        if (const std::string e = p->client.channel().startRecording(p->recordPath); !e.empty()) {
            p->logf(CANPROXY_LOG_ERROR, "%s", e.c_str());
            p->running = false;
            return 1;
        }
        p->logf(CANPROXY_LOG_INFO, "recording vehicle side to %s", p->recordPath.c_str());
    }
    p->logf(CANPROXY_LOG_INFO, "polling %s, fast tier %ld ms", p->vehicleIf.c_str(), static_cast<long>(p->fastMs));
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
}

void destroy(void *self) { delete static_cast<Plugin *>(self); }

const canproxy_plugin kPlugin = {
    CANPROXY_PLUGIN_ABI_VERSION, "obd2-ice", 1, 0, create, start, stop, destroy,
};

} // namespace

extern "C" const canproxy_plugin *canproxy_plugin_entry(void) { return &kPlugin; }
