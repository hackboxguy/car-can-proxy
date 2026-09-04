// forza: the game's "Data Out" UDP telemetry as a vehicle. Point Forza
// Horizon / Motorsport at this host's IP and the port below, and the cluster
// shows the car you drive in the game. Source kind is "simulated" (badge SIM).
//
//   --plugin-arg port=<n>                 UDP port to listen on (default 1101)
//   --plugin-arg drivetrain=ice|bev|hev   what the game car is (default ice;
//                                         only decides the auto theme)
//   --plugin-arg timeout_ms=<n>           no packet for this long = no vehicle (default 1000)
//
// What is published: speed (from the Dash block, m/s -> km/h, or the sled's
// velocity vector), engine rpm, gear (P when the game is not in a race,
// R for the game's gear 0, D otherwise), power state (ready in a race),
// motor power (Dash Power, kW; negative while engine braking), fuel level,
// distance driven as odometer, and the parking-brake lamp from the
// handbrake. Nothing the game does not send is invented: no coolant, no
// battery, no range.
#include "ForzaTelemetry.h"
#include "canproxy/plugin.h"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Plugin {
    canproxy_host host{};
    int port = 1101;
    int drivetrain = CP_DRIVETRAIN_ICE;
    int64_t timeoutMs = 1000;
    int fd = -1;
    std::thread thread;
    std::atomic<bool> running{false};

    void logf(int level, const char *fmt, ...) __attribute__((format(printf, 3, 4)))
    {
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        if (host.log) host.log(host.ctx, level, buf);
    }

    void publish(const forza::Frame &f)
    {
        canproxy_vehicle_state s;
        std::memset(&s, 0, sizeof s);
        s.drivetrain = drivetrain;
        s.source = CP_SOURCE_SIMULATED;
        s.vehicle_id = canproxy_vehicle_id("forza", nullptr);

        uint32_t cap = CANPROXY_SIG_BIT(CANPROXY_SIG_SPEED) | CANPROXY_SIG_BIT(CANPROXY_SIG_RPM) |
                       CANPROXY_SIG_BIT(CANPROXY_SIG_GEAR) | CANPROXY_SIG_BIT(CANPROXY_SIG_POWER_STATE);
        if (f.hasDash)
            cap |= CANPROXY_SIG_BIT(CANPROXY_SIG_MOTOR_POWER) | CANPROXY_SIG_BIT(CANPROXY_SIG_FUEL_LEVEL) |
                   CANPROXY_SIG_BIT(CANPROXY_SIG_ODOMETER);
        s.capable = cap;
        s.valid = cap;

        s.speed_kmh = f.speedMps * 3.6;
        s.rpm = f.engineRpm;
        s.power_state = f.raceOn ? CP_POWER_READY : CP_POWER_ON;
        if (!f.raceOn) s.gear = CP_GEAR_P;
        else if (f.hasDash && f.gear == 0) s.gear = CP_GEAR_R;
        else s.gear = CP_GEAR_D;
        if (f.hasDash) {
            s.motor_power_kw = f.powerW / 1000.0;
            s.fuel_pct = f.fuel * 100.0;
            s.odometer_km = f.distanceM / 1000.0;
            if (f.handbrake) s.lamps |= CANPROXY_LAMP_BIT(CANPROXY_LAMP_PARK_BRAKE);
            if (f.fuel < 0.10f) s.lamps |= CANPROXY_LAMP_BIT(CANPROXY_LAMP_LOW_FUEL);
        }
        host.publish(host.ctx, &s);
    }

    void run()
    {
        bool linked = false;
        int64_t lastPacket = -1, lastPublish = 0;
        const char *lastFormat = "";
        uint8_t buf[512];
        while (running.load()) {
            struct pollfd pfd = { fd, POLLIN, 0 };
            const int rc = ::poll(&pfd, 1, 100);
            const int64_t now = nowMs();
            if (rc > 0) {
                const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
                forza::Frame f;
                if (n > 0 && forza::parse(buf, static_cast<size_t>(n), f)) {
                    if (!linked) {
                        host.set_link(host.ctx, 1);
                        linked = true;
                    }
                    if (std::strcmp(f.format, lastFormat) != 0) {
                        logf(CANPROXY_LOG_INFO, "telemetry format %s (%zd bytes)", f.format, n);
                        lastFormat = f.format;
                    }
                    lastPacket = now;
                    if (now - lastPublish >= 20) {     // Forza sends at 60 Hz; the contract's fastest frame is 50 ms
                        publish(f);
                        lastPublish = now;
                    }
                } else if (n > 0 && lastPacket < 0) {
                    logf(CANPROXY_LOG_WARN, "ignoring %zd-byte packet: not a known Forza telemetry size", n);
                    lastPacket = now;     // log once per silence
                }
            }
            if (linked && now - lastPacket > timeoutMs) {
                logf(CANPROXY_LOG_WARN, "no telemetry for %ld ms, vehicle gone", static_cast<long>(timeoutMs));
                host.set_link(host.ctx, 0);
                linked = false;
                lastFormat = "";
            }
        }
    }
};

void *create(const canproxy_plugin_args *args, const canproxy_host *host)
{
    auto *p = new Plugin;
    p->host = *host;
    if (const char *v = canproxy_arg(args, "port")) {
        p->port = std::atoi(v);
        if (p->port < 1 || p->port > 65535) { p->logf(CANPROXY_LOG_ERROR, "port out of range"); delete p; return nullptr; }
    }
    if (const char *d = canproxy_arg(args, "drivetrain")) {
        std::string v = d;
        if (v == "ice") p->drivetrain = CP_DRIVETRAIN_ICE;
        else if (v == "bev") p->drivetrain = CP_DRIVETRAIN_BEV;
        else if (v == "hev") p->drivetrain = CP_DRIVETRAIN_HEV;
        else { p->logf(CANPROXY_LOG_ERROR, "unknown drivetrain '%s' (ice|bev|hev)", d); delete p; return nullptr; }
    }
    if (const char *t = canproxy_arg(args, "timeout_ms")) {
        p->timeoutMs = std::atol(t);
        if (p->timeoutMs < 100) { p->logf(CANPROXY_LOG_ERROR, "timeout_ms must be >= 100"); delete p; return nullptr; }
    }
    return p;
}

int start(void *self)
{
    auto *p = static_cast<Plugin *>(self);
    if (p->running.exchange(true))
        return 0;
    p->fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (p->fd < 0) { p->logf(CANPROXY_LOG_ERROR, "socket: %s", std::strerror(errno)); p->running = false; return 1; }
    int reuse = 1;
    ::setsockopt(p->fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(p->port));
    if (::bind(p->fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof addr) < 0) {
        p->logf(CANPROXY_LOG_ERROR, "bind udp/%d: %s", p->port, std::strerror(errno));
        ::close(p->fd); p->fd = -1; p->running = false;
        return 1;
    }
    p->logf(CANPROXY_LOG_INFO, "listening for Forza Data Out on udp/%d; set the game's Data Out IP to this host", p->port);
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
    if (p->fd >= 0) { ::close(p->fd); p->fd = -1; }
}

void destroy(void *self) { delete static_cast<Plugin *>(self); }

const canproxy_plugin kPlugin = { CANPROXY_PLUGIN_ABI_VERSION, "forza", 1, 0, create, start, stop, destroy };

} // namespace

extern "C" const canproxy_plugin *canproxy_plugin_entry(void) { return &kPlugin; }
