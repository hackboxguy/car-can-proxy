#pragma once
#include "Mapping.h"
#include "canproxy/plugin.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace canproxy {

using Nanos = int64_t;   // monotonic clock, nanoseconds

// The host's copy of what the plugin last said, with timestamps. Written by
// plugin threads, read by the publisher; the mutex covers both.
class StateStore {
public:
    void publish(const canproxy_vehicle_state &s, Nanos now);
    void setLink(bool present, Nanos now);

    struct Snapshot {
        canproxy_vehicle_state state{};
        bool everPublished = false;
        bool linkKnown = false;
        bool linkPresent = false;
        Nanos lastPublish = 0;
    };
    Snapshot snapshot() const;

private:
    mutable std::mutex m_mutex;
    Snapshot m_data;
};

// Derives proxy state and the "live" flag from a snapshot. Pure.
struct Derived {
    uint8_t proxyState;
    bool live;      // publish real values; false = everything SNA
};
Derived derive(const StateStore::Snapshot &snap, Nanos now, Nanos pluginTimeout);

// Runs the contract schedule. `sink` receives (id, 8 bytes) on the calling
// thread of tick(); `run()` drives tick() from its own thread with absolute
// deadlines so cycles do not drift.
class Publisher {
public:
    using Sink = std::function<void(uint32_t id, const uint8_t data[8])>;
    using Clock = std::function<Nanos()>;

    Publisher(StateStore &store, Sink sink, Clock clock, Nanos pluginTimeout,
              uint8_t pluginMajor, uint8_t pluginMinor);

    // Send every frame that is due at `now`; returns the next deadline.
    Nanos tick(Nanos now);
    // First call arms all schedules at `now`.
    void arm(Nanos now);

    void start();
    void stop();

    uint8_t lastProxyState() const { return m_lastState.load(); }

private:
    void sendFrame(unsigned idx, const Frames &f);

    StateStore &m_store;
    Sink m_sink;
    Clock m_clock;
    Nanos m_timeout;
    uint8_t m_pluginMajor, m_pluginMinor;
    uint8_t m_counter = 0;
    std::atomic<uint8_t> m_lastState{0};
    bool m_armed = false;

    struct Slot { uint32_t id; Nanos cycle; Nanos due; };
    Slot m_slots[CANPROXY_FRAME_COUNT];

    std::thread m_thread;
    std::atomic<bool> m_running{false};
};

Nanos monotonicNow();

} // namespace canproxy
