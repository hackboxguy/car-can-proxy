// The "how to talk" half of an OBD-II poller: functional requests on the
// raw channel, first-responder-wins ECU stickiness, supported-PID discovery
// and an optional broadcast frame sniffed while waiting. Plugins decide what
// to ask and what it means.
#pragma once
#include "obd/CanChannel.h"
#include "obd/J1979.h"
#include <functional>
#include <set>
#include <string>

namespace obd {

class J1979Client {
public:
    // Frames on this ID seen while waiting are handed to the callback.
    void setBroadcastSink(long id, std::function<void(const Frame &)> sink);

    std::string open(const std::string &interface);
    void close();
    CanChannel &channel() { return m_ch; }

    // Walk the supported-PID bitmap chain from the first ECU that answers.
    // Returns false if nothing answers PID 0x00 within timeoutMs.
    bool discover(int timeoutMs);
    const std::set<uint8_t> &supported() const { return m_supported; }
    uint32_t primaryEcu() const { return m_primaryEcu; }
    void forgetEcu() { m_primaryEcu = 0; }

    // One mode-01 request; true with the decoded physical value on success.
    bool query(uint8_t pid, int timeoutMs, double *value);

    // Block up to timeoutMs draining broadcasts only.
    void idle(int timeoutMs);

private:
    bool request(uint8_t pid, int timeoutMs, Response *out);
    CanChannel m_ch;
    long m_broadcastId = -1;
    std::function<void(const Frame &)> m_broadcastSink;
    uint32_t m_primaryEcu = 0;
    std::set<uint8_t> m_supported;
};

} // namespace obd
