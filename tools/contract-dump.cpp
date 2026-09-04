// contract-dump: receive the contract on a CAN interface and print it
// decoded, or measure cycle timing and counter continuity.
//
//   contract-dump <if>                      decoded lines until Ctrl-C
//   contract-dump <if> --stats=<seconds>    per-ID rate/jitter table, then exit
//       [--max-jitter-ms=<n>]                exit 1 if any ID's worst deviation exceeds n
//       [--expect-ids=0x400,0x410,...]       exit 1 if any listed ID was never seen
//       [--require-counter]                  exit 1 on any 0x400 counter skip
//   contract-dump <if> --raw                 hex only
#include "can_proxy_contract.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <map>
#include <net/if.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

static int64_t nowUs()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<int64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
}

static void printDecoded(uint32_t id, const uint8_t *d)
{
    char line[256];
    switch (id) {
    case CANPROXY_ID_STATUS: {
        canproxy_status_t s; canproxy_unpack_status(d, &s);
        static const char *st[] = { "starting", "no-vehicle", "degraded", "ok" };
        std::snprintf(line, sizeof line, "status    v%u.%u ctr=%3u state=%s caps=0x%05X",
                      s.contract_major, s.contract_minor, s.counter, st[s.proxy_state & 3], s.capabilities);
        break; }
    case CANPROXY_ID_IDENTITY: {
        canproxy_identity_t s; canproxy_unpack_identity(d, &s);
        static const char *dt[] = { "unknown", "ice", "bev", "hev", "phev", "fcev" };
        static const char *src[] = { "simulated", "emulator", "live", "replay" };
        std::snprintf(line, sizeof line, "identity  drivetrain=%s source=%s plugin=v%u.%u id=0x%08X",
                      s.drivetrain < 6 ? dt[s.drivetrain] : "?", s.source_kind < 4 ? src[s.source_kind] : "?",
                      s.plugin_major, s.plugin_minor, s.vehicle_id);
        break; }
    case CANPROXY_ID_MOTION: {
        canproxy_motion_t s; canproxy_unpack_motion(d, &s);
        static const char *gears = "PRNDL";
        char sp[16], rp[16];
        s.speed_valid ? std::snprintf(sp, sizeof sp, "%.2f", s.speed_kmh) : std::snprintf(sp, sizeof sp, "SNA");
        s.rpm_valid ? std::snprintf(rp, sizeof rp, "%u", s.rpm) : std::snprintf(rp, sizeof rp, "SNA");
        std::snprintf(line, sizeof line, "motion    speed=%s km/h rpm=%s gear=%c power=%s", sp, rp,
                      s.gear_valid && s.gear < 5 ? gears[s.gear] : '-',
                      !s.power_state_valid ? "SNA" : (const char *[]){ "off", "acc", "on", "ready" }[s.power_state & 3]);
        break; }
    case CANPROXY_ID_EDRIVE: {
        canproxy_edrive_t s; canproxy_unpack_edrive(d, &s);
        std::snprintf(line, sizeof line, "edrive    power=%s kW pack=%s V %s A",
                      s.motor_power_valid ? std::to_string(s.motor_power_kw).substr(0, 6).c_str() : "SNA",
                      s.pack_voltage_valid ? std::to_string(s.pack_voltage_v).substr(0, 5).c_str() : "SNA",
                      s.pack_current_valid ? std::to_string(s.pack_current_a).substr(0, 6).c_str() : "SNA");
        break; }
    case CANPROXY_ID_TELLTALES: {
        canproxy_telltales_t s; canproxy_unpack_telltales(d, &s);
        std::snprintf(line, sizeof line, "telltales 0x%08X", s.bits);
        break; }
    case CANPROXY_ID_ENERGY: {
        canproxy_energy_t s; canproxy_unpack_energy(d, &s);
        std::snprintf(line, sizeof line, "energy    soc=%s%% soh=%s%% range=%s km cons=%s Wh/km chg=%s",
                      s.soc_valid ? std::to_string(s.soc_pct).substr(0, 4).c_str() : "SNA",
                      s.soh_valid ? std::to_string(s.soh_pct).substr(0, 4).c_str() : "SNA",
                      s.range_valid ? std::to_string(s.range_km).c_str() : "SNA",
                      s.consumption_valid ? std::to_string(s.consumption_wh_km).c_str() : "SNA",
                      s.charging_state_valid ? std::to_string(s.charging_state).c_str() : "SNA");
        break; }
    case CANPROXY_ID_TRIP: {
        canproxy_trip_t s; canproxy_unpack_trip(d, &s);
        std::snprintf(line, sizeof line, "trip      odo=%s km ambient=%s C cabin=%s C",
                      s.odometer_valid ? std::to_string(s.odometer_km).substr(0, 8).c_str() : "SNA",
                      s.ambient_valid ? std::to_string(s.ambient_c).c_str() : "SNA",
                      s.cabin_valid ? std::to_string(s.cabin_c).c_str() : "SNA");
        break; }
    case CANPROXY_ID_THERMAL: {
        canproxy_thermal_t s; canproxy_unpack_thermal(d, &s);
        std::snprintf(line, sizeof line, "thermal   coolant=%s C fuel=%s%% aux=%s V",
                      s.coolant_valid ? std::to_string(s.coolant_c).c_str() : "SNA",
                      s.fuel_valid ? std::to_string(s.fuel_pct).substr(0, 4).c_str() : "SNA",
                      s.aux_battery_valid ? std::to_string(s.aux_battery_v).substr(0, 5).c_str() : "SNA");
        break; }
    case CANPROXY_ID_ASSIST: {
        canproxy_assist_t s; canproxy_unpack_assist(d, &s);
        std::snprintf(line, sizeof line, "assist    eco=%s limit=%s risk=%s lane=%s gap=%s m",
                      s.eco_score_valid ? std::to_string(s.eco_score).c_str() : "SNA",
                      s.speed_limit_valid ? std::to_string(s.speed_limit_kmh).c_str() : "SNA",
                      s.collision_risk_valid ? std::to_string(s.collision_risk).c_str() : "SNA",
                      s.lane_state_valid ? std::to_string(s.lane_state).c_str() : "SNA",
                      s.lead_gap_valid ? std::to_string(s.lead_gap_m).substr(0, 5).c_str() : "SNA");
        break; }
    default:
        std::snprintf(line, sizeof line, "0x%03X (not in contract)", id);
    }
    std::printf("%s\n", line);
}

struct Stat { int count = 0; int64_t last = 0; double sumInterval = 0; double worstDev = 0; };

int main(int argc, char **argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: contract-dump <if> [--stats=<s>] [--max-jitter-ms=<n>] [--expect-ids=a,b] [--require-counter] [--raw]\n"); return 2; }
    const char *ifname = argv[1];
    int statsSec = 0; double maxJitterMs = -1; bool requireCounter = false, raw = false;
    std::vector<uint32_t> expect;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a.rfind("--stats=", 0) == 0) statsSec = std::atoi(a.c_str() + 8);
        else if (a.rfind("--max-jitter-ms=", 0) == 0) maxJitterMs = std::atof(a.c_str() + 16);
        else if (a.rfind("--expect-ids=", 0) == 0) {
            std::string list = a.substr(13); size_t pos = 0;
            while (pos <= list.size()) { size_t c = list.find(',', pos); if (c == std::string::npos) c = list.size();
                expect.push_back(static_cast<uint32_t>(std::strtoul(list.substr(pos, c - pos).c_str(), nullptr, 0))); pos = c + 1; }
        }
        else if (a == "--require-counter") requireCounter = true;
        else if (a == "--raw") raw = true;
        else { std::fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }

    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) { std::perror("socket"); return 1; }
    struct ifreq ifr; std::memset(&ifr, 0, sizeof ifr);
    std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) { std::perror(ifname); return 1; }
    struct sockaddr_can addr; std::memset(&addr, 0, sizeof addr);
    addr.can_family = AF_CAN; addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof addr) < 0) { std::perror("bind"); return 1; }
    struct timeval tv = { 1, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    std::map<uint32_t, Stat> stats;
    int counterSkips = 0, lastCounter = -1;
    const int64_t start = nowUs();
    const int64_t end = start + static_cast<int64_t>(statsSec) * 1000000;

    for (;;) {
        if (statsSec > 0 && nowUs() >= end) break;
        struct can_frame f;
        ssize_t n = read(fd, &f, sizeof f);
        if (n < 0) { if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue; std::perror("read"); return 1; }
        if (n != static_cast<ssize_t>(sizeof f)) continue;
        const uint32_t id = f.can_id & CAN_SFF_MASK;
        const int64_t t = nowUs();

        if (statsSec > 0) {
            Stat &s = stats[id];
            if (s.count > 0) {
                const double interval = (t - s.last) / 1000.0;
                s.sumInterval += interval;
                const double dev = std::abs(interval - canproxy_cycle_ms(id));
                if (dev > s.worstDev) s.worstDev = dev;
            }
            s.count++; s.last = t;
            if (id == CANPROXY_ID_STATUS) {
                if (lastCounter >= 0 && f.data[2] != static_cast<uint8_t>(lastCounter + 1)) counterSkips++;
                lastCounter = f.data[2];
            }
            continue;
        }
        std::printf("%10.3f  0x%03X  ", (t - start) / 1e6, id);
        if (raw || !canproxy_is_contract_id(id)) {
            for (int i = 0; i < f.can_dlc; i++) std::printf("%02X ", f.data[i]);
            std::printf("\n");
        } else {
            printDecoded(id, f.data);
        }
        std::fflush(stdout);
    }

    int rc = 0;
    std::printf("%-6s %-10s %6s %9s %9s %9s\n", "id", "name", "count", "cycle", "mean", "worstdev");
    for (auto &kv : stats) {
        const Stat &s = kv.second;
        const double mean = s.count > 1 ? s.sumInterval / (s.count - 1) : 0;
        std::printf("0x%03X  %-10s %6d %6u ms %7.2f ms %7.2f ms\n", kv.first, canproxy_id_name(kv.first),
                    s.count, canproxy_cycle_ms(kv.first), mean, s.worstDev);
        if (maxJitterMs >= 0 && s.worstDev > maxJitterMs) { std::printf("  ^ jitter over %.1f ms\n", maxJitterMs); rc = 1; }
    }
    for (uint32_t e : expect)
        if (!stats.count(e)) { std::printf("missing 0x%03X\n", e); rc = 1; }
    std::printf("status counter skips: %d\n", counterSkips);
    if (requireCounter && counterSkips) rc = 1;
    return rc;
}
