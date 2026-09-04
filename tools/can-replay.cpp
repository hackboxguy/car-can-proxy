// can-replay: play a candump-format log back onto a CAN interface.
//
//   can-replay --timed <if> <log> [--loop]
//       Every frame at its recorded time offset, like canplayer.
//   can-replay --respond <if> <log> [--period-ms=<n>]
//       Act as the recorded vehicle: answer each OBD-II request (0x7DF) with
//       the last answer the log holds for that PID, and re-broadcast every
//       non-OBD frame on its recorded period. Deterministic for a poller.
//
// Log line format (candump -l / obd::CanChannel): "(sec.usec) iface ID#HEX".
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <map>
#include <net/if.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

struct Rec { double t; uint32_t id; uint8_t dlc; uint8_t data[8]; };

static bool parseLine(const std::string &line, Rec &r)
{
    unsigned long sec, usec;
    char iface[32], hex[64];
    unsigned id;
    if (std::sscanf(line.c_str(), "(%lu.%lu) %31s %x#%63s", &sec, &usec, iface, &id, hex) != 5)
        return false;
    r.t = sec + usec / 1e6;
    r.id = id;
    const size_t n = std::strlen(hex) / 2;
    r.dlc = static_cast<uint8_t>(n > 8 ? 8 : n);
    for (int i = 0; i < r.dlc; i++) {
        unsigned b;
        std::sscanf(hex + 2 * i, "%2x", &b);
        r.data[i] = static_cast<uint8_t>(b);
    }
    return true;
}

static int openCan(const char *ifname)
{
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) { std::perror("socket"); return -1; }
    struct ifreq ifr; std::memset(&ifr, 0, sizeof ifr);
    std::strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) { std::perror(ifname); close(fd); return -1; }
    struct sockaddr_can addr; std::memset(&addr, 0, sizeof addr);
    addr.can_family = AF_CAN; addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof addr) < 0) { std::perror("bind"); close(fd); return -1; }
    return fd;
}

static void sendRec(int fd, const Rec &r)
{
    struct can_frame f; std::memset(&f, 0, sizeof f);
    f.can_id = r.id; f.can_dlc = r.dlc; std::memcpy(f.data, r.data, 8);
    if (write(fd, &f, sizeof f) != sizeof f) std::perror("write");
}

static bool isObdRequest(uint32_t id) { return id == 0x7DF || (id >= 0x7E0 && id <= 0x7E7); }
static bool isObdResponse(uint32_t id) { return id >= 0x7E8 && id <= 0x7EF; }

int main(int argc, char **argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: can-replay --timed <if> <log> [--loop]\n"
                             "       can-replay --respond <if> <log> [--period-ms=<n>]\n");
        return 2;
    }
    const std::string mode = argv[1];
    const char *ifname = argv[2];
    std::ifstream in(argv[3]);
    if (!in) { std::perror(argv[3]); return 1; }
    std::vector<Rec> recs;
    std::string line;
    while (std::getline(in, line)) { Rec r; if (parseLine(line, r)) recs.push_back(r); }
    if (recs.empty()) { std::fprintf(stderr, "no frames in %s\n", argv[3]); return 1; }
    int fd = openCan(ifname);
    if (fd < 0) return 1;

    if (mode == "--timed") {
        const bool loop = argc > 4 && std::string(argv[4]) == "--loop";
        do {
            const auto start = std::chrono::steady_clock::now();
            const double t0 = recs.front().t;
            for (const Rec &r : recs) {
                const auto due = start + std::chrono::microseconds(static_cast<long>((r.t - t0) * 1e6));
                std::this_thread::sleep_until(due);
                sendRec(fd, r);
            }
        } while (loop);
        return 0;
    }

    if (mode == "--respond") {
        long periodOverride = -1;
        for (int i = 4; i < argc; i++)
            if (std::strncmp(argv[i], "--period-ms=", 12) == 0) periodOverride = std::atol(argv[i] + 12);

        // Pair each request with the next response carrying the same PID.
        std::map<uint8_t, Rec> answers;
        std::map<uint32_t, std::vector<double>> broadcastTimes;
        std::map<uint32_t, Rec> broadcastLast;
        for (size_t i = 0; i < recs.size(); i++) {
            const Rec &r = recs[i];
            if (isObdRequest(r.id) && r.dlc >= 3) {
                const uint8_t pid = r.data[2];
                for (size_t j = i + 1; j < recs.size() && j < i + 16; j++) {
                    const Rec &a = recs[j];
                    if (isObdResponse(a.id) && a.dlc >= 3 && a.data[2] == pid) { answers[pid] = a; break; }
                }
            } else if (!isObdResponse(r.id)) {
                broadcastTimes[r.id].push_back(r.t);
                broadcastLast[r.id] = r;
            }
        }
        std::fprintf(stderr, "can-replay: %zu answerable PIDs, %zu broadcast IDs\n", answers.size(), broadcastTimes.size());

        // Broadcast frames on their observed period.
        std::vector<std::thread> threads;
        for (auto &kv : broadcastTimes) {
            const uint32_t id = kv.first;
            const auto &ts = kv.second;
            long periodMs = periodOverride;
            if (periodMs < 0)
                periodMs = ts.size() > 1 ? static_cast<long>((ts.back() - ts.front()) / (ts.size() - 1) * 1000.0 + 0.5) : 100;
            if (periodMs < 1) periodMs = 1;
            Rec frame = broadcastLast[id];
            threads.emplace_back([fd, frame, periodMs] {
                for (;;) { sendRec(fd, frame); std::this_thread::sleep_for(std::chrono::milliseconds(periodMs)); }
            });
        }

        for (;;) {
            struct can_frame f;
            if (read(fd, &f, sizeof f) != sizeof f) { if (errno == EINTR) continue; break; }
            const uint32_t id = f.can_id & CAN_SFF_MASK;
            if (!isObdRequest(id) || f.can_dlc < 3) continue;
            auto it = answers.find(f.data[2]);
            if (it == answers.end()) continue;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            sendRec(fd, it->second);
        }
        return 0;
    }

    std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
    return 2;
}
