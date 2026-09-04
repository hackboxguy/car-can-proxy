// A raw SocketCAN channel for the vehicle side, with optional recording of
// everything sent and received in candump log format so a session can be
// replayed later (tools/can-replay).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace obd {

struct Frame {
    uint32_t id = 0;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
};

class CanChannel {
public:
    CanChannel() = default;
    ~CanChannel();
    CanChannel(const CanChannel &) = delete;
    CanChannel &operator=(const CanChannel &) = delete;

    // Receive only the given IDs (11-bit, exact match). Empty = everything.
    std::string open(const std::string &interface, const std::vector<uint32_t> &acceptIds);
    void close();
    bool isOpen() const { return m_fd >= 0; }

    // candump -l style log: "(sec.usec) iface ID#HEX". Empty string = success.
    std::string startRecording(const std::string &path);
    void stopRecording();

    bool send(const Frame &f);
    // true when a frame arrived within the timeout.
    bool receive(Frame &f, int timeoutMs);

private:
    void record(const Frame &f);
    int m_fd = -1;
    std::string m_interface;
    std::FILE *m_log = nullptr;
};

} // namespace obd
