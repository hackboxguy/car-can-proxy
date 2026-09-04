#include "Publisher.h"
#include "Log.h"
#include <ctime>

namespace canproxy {

Nanos monotonicNow()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<Nanos>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

void StateStore::publish(const canproxy_vehicle_state &s, Nanos now)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.state = s;
    m_data.everPublished = true;
    m_data.lastPublish = now;
}

void StateStore::setLink(bool present, Nanos now)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.linkKnown = true;
    m_data.linkPresent = present;
    (void)now;
}

StateStore::Snapshot StateStore::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data;
}

Derived derive(const StateStore::Snapshot &snap, Nanos now, Nanos pluginTimeout)
{
    if (!snap.linkKnown)
        return { CANPROXY_STATE_STARTING, false };
    if (!snap.linkPresent)
        return { CANPROXY_STATE_NO_VEHICLE, false };
    if (!snap.everPublished || now - snap.lastPublish > pluginTimeout)
        return { CANPROXY_STATE_DEGRADED, false };
    const uint32_t capable = snap.state.capable;
    const bool allFresh = (snap.state.valid & capable) == capable;
    return { static_cast<uint8_t>(allFresh ? CANPROXY_STATE_OK : CANPROXY_STATE_DEGRADED), true };
}

static const uint32_t kOrder[CANPROXY_FRAME_COUNT] = {
    CANPROXY_ID_STATUS, CANPROXY_ID_IDENTITY, CANPROXY_ID_MOTION, CANPROXY_ID_EDRIVE,
    CANPROXY_ID_TELLTALES, CANPROXY_ID_ENERGY, CANPROXY_ID_TRIP, CANPROXY_ID_THERMAL,
    CANPROXY_ID_ASSIST,
};

Publisher::Publisher(StateStore &store, Sink sink, Clock clock, Nanos pluginTimeout,
                     uint8_t pluginMajor, uint8_t pluginMinor)
    : m_store(store), m_sink(std::move(sink)), m_clock(std::move(clock)),
      m_timeout(pluginTimeout), m_pluginMajor(pluginMajor), m_pluginMinor(pluginMinor)
{
    for (unsigned i = 0; i < CANPROXY_FRAME_COUNT; i++) {
        m_slots[i].id = kOrder[i];
        m_slots[i].cycle = static_cast<Nanos>(canproxy_cycle_ms(kOrder[i])) * 1000000LL;
        m_slots[i].due = 0;
    }
}

void Publisher::arm(Nanos now)
{
    for (auto &s : m_slots)
        s.due = now;
    m_armed = true;
}

void Publisher::sendFrame(unsigned idx, const Frames &f)
{
    uint8_t d[CANPROXY_FRAME_DLC];
    switch (m_slots[idx].id) {
    case CANPROXY_ID_STATUS:    canproxy_pack_status(&f.status, d); break;
    case CANPROXY_ID_IDENTITY:  canproxy_pack_identity(&f.identity, d); break;
    case CANPROXY_ID_MOTION:    canproxy_pack_motion(&f.motion, d); break;
    case CANPROXY_ID_EDRIVE:    canproxy_pack_edrive(&f.edrive, d); break;
    case CANPROXY_ID_TELLTALES: canproxy_pack_telltales(&f.telltales, d); break;
    case CANPROXY_ID_ENERGY:    canproxy_pack_energy(&f.energy, d); break;
    case CANPROXY_ID_TRIP:      canproxy_pack_trip(&f.trip, d); break;
    case CANPROXY_ID_THERMAL:   canproxy_pack_thermal(&f.thermal, d); break;
    case CANPROXY_ID_ASSIST:
        if (!(f.status.capabilities & CANPROXY_CAP_ASSIST))
            return;               // optional frame, not published
        canproxy_pack_assist(&f.assist, d);
        break;
    default: return;
    }
    m_sink(m_slots[idx].id, d);
}

Nanos Publisher::tick(Nanos now)
{
    if (!m_armed)
        arm(now);

    bool anyDue = false;
    for (auto &s : m_slots)
        if (now >= s.due) { anyDue = true; break; }

    if (anyDue) {
        const auto snap = m_store.snapshot();
        const Derived dv = derive(snap, now, m_timeout);
        if (dv.proxyState != m_lastState.load()) {
            static const char *names[] = { "starting", "no-vehicle", "degraded", "ok" };
            CANPROXY_LOG(Info, "publisher", std::string("proxy state -> ") + names[dv.proxyState & 3]);
            m_lastState = dv.proxyState;
        }
        Frames f;
        framesFrom(snap.state, dv.live, dv.proxyState, m_counter, m_pluginMajor, m_pluginMinor, f);

        for (unsigned i = 0; i < CANPROXY_FRAME_COUNT; i++) {
            Slot &s = m_slots[i];
            if (now < s.due)
                continue;
            if (s.id == CANPROXY_ID_STATUS)
                m_counter = static_cast<uint8_t>(m_counter + 1);
            sendFrame(i, f);
            s.due += s.cycle;
            if (s.due < now)            // fell far behind (suspended?): resync, do not burst
                s.due = now + s.cycle;
        }
    }

    Nanos next = m_slots[0].due;
    for (auto &s : m_slots)
        if (s.due < next) next = s.due;
    return next;
}

void Publisher::start()
{
    if (m_running.exchange(true))
        return;
    m_thread = std::thread([this] {
        arm(m_clock());
        while (m_running.load()) {
            const Nanos next = tick(m_clock());
            struct timespec ts;
            ts.tv_sec = static_cast<time_t>(next / 1000000000LL);
            ts.tv_nsec = static_cast<long>(next % 1000000000LL);
            while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR && m_running.load()) {}
        }
    });
}

void Publisher::stop()
{
    if (!m_running.exchange(false))
        return;
    if (m_thread.joinable())
        m_thread.join();
}

} // namespace canproxy
