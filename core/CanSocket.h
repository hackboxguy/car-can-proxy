#pragma once
#include <cstdint>
#include <string>

namespace canproxy {

// Minimal SocketCAN raw transmitter. Receive side is added when a consumer
// in this repository needs it (tools/contract-dump has its own).
class CanSocket {
public:
    CanSocket() = default;
    ~CanSocket();
    CanSocket(const CanSocket &) = delete;
    CanSocket &operator=(const CanSocket &) = delete;

    // Returns empty string on success, otherwise the error.
    std::string open(const std::string &interface);
    void close();
    bool isOpen() const { return m_fd >= 0; }

    // Classic CAN, 11-bit id, exactly `len` bytes (<= 8). False on failure.
    bool send(uint32_t id, const uint8_t *data, unsigned len);

private:
    int m_fd = -1;
};

} // namespace canproxy
