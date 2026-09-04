#include "Publisher.h"
#include "test_support.h"
#include <cstring>
#include <map>
#include <vector>

using namespace canproxy;

static const Nanos MS = 1000000LL;

struct Capture {
    struct Rec { Nanos t; uint32_t id; uint8_t d[8]; };
    std::vector<Rec> recs;
    Nanos now = 0;
    int countOf(uint32_t id) const { int n = 0; for (auto &r : recs) if (r.id == id) n++; return n; }
    const Rec *last(uint32_t id) const { const Rec *p = nullptr; for (auto &r : recs) if (r.id == id) p = &r; return p; }
};

static canproxy_vehicle_state fullState()
{
    canproxy_vehicle_state s;
    std::memset(&s, 0, sizeof s);
    s.capable = CANPROXY_SIG_BIT(CANPROXY_SIG_SPEED) | CANPROXY_SIG_BIT(CANPROXY_SIG_SOC) | CANPROXY_SIG_BIT(CANPROXY_SIG_ECO_SCORE);
    s.valid = s.capable;
    s.speed_kmh = 60; s.soc_pct = 50; s.eco_score = 90;
    s.drivetrain = CP_DRIVETRAIN_BEV; s.source = CP_SOURCE_SIMULATED;
    return s;
}

static void testDerive()
{
    StateStore::Snapshot snap;
    Derived d = derive(snap, 0, 1000 * MS);
    CHECK(d.proxyState == CANPROXY_STATE_STARTING && !d.live);

    snap.linkKnown = true; snap.linkPresent = false;
    d = derive(snap, 0, 1000 * MS);
    CHECK(d.proxyState == CANPROXY_STATE_NO_VEHICLE && !d.live);

    snap.linkPresent = true;                       // link up, nothing published yet
    d = derive(snap, 0, 1000 * MS);
    CHECK(d.proxyState == CANPROXY_STATE_DEGRADED && !d.live);

    snap.everPublished = true; snap.lastPublish = 100 * MS; snap.state = fullState();
    d = derive(snap, 200 * MS, 1000 * MS);
    CHECK(d.proxyState == CANPROXY_STATE_OK && d.live);

    snap.state.valid &= ~CANPROXY_SIG_BIT(CANPROXY_SIG_SOC);   // one advertised signal missing
    d = derive(snap, 200 * MS, 1000 * MS);
    CHECK(d.proxyState == CANPROXY_STATE_DEGRADED && d.live);

    snap.state.valid = snap.state.capable;
    d = derive(snap, 1101 * MS, 1000 * MS);        // plugin silent past timeout
    CHECK(d.proxyState == CANPROXY_STATE_DEGRADED && !d.live);
}

static void testSchedule()
{
    StateStore store;
    Capture cap;
    Publisher pub(store, [&](uint32_t id, const uint8_t d[8]) { Capture::Rec r; r.t = cap.now; r.id = id; std::memcpy(r.d, d, 8); cap.recs.push_back(r); },
                  [&] { return cap.now; }, 1000 * MS, 1, 2);

    // Run 10 s of virtual time in 1 ms steps with no plugin at all.
    for (cap.now = 0; cap.now <= 10000 * MS; cap.now += MS)
        pub.tick(cap.now);

    CHECK(cap.countOf(CANPROXY_ID_STATUS) == 101);        // t=0 and every 100 ms
    CHECK(cap.countOf(CANPROXY_ID_MOTION) == 201);
    CHECK(cap.countOf(CANPROXY_ID_EDRIVE) == 201);
    CHECK(cap.countOf(CANPROXY_ID_TELLTALES) == 101);
    CHECK(cap.countOf(CANPROXY_ID_ENERGY) == 21);
    CHECK(cap.countOf(CANPROXY_ID_TRIP) == 11);
    CHECK(cap.countOf(CANPROXY_ID_THERMAL) == 21);
    CHECK(cap.countOf(CANPROXY_ID_IDENTITY) == 11);
    CHECK(cap.countOf(CANPROXY_ID_ASSIST) == 0);          // no capability -> not published

    // Rolling counter has no skips, state is "starting", everything SNA.
    int prev = -1, skips = 0;
    for (auto &r : cap.recs) {
        if (r.id != CANPROXY_ID_STATUS) continue;
        if (prev >= 0 && r.d[2] != static_cast<uint8_t>(prev + 1)) skips++;
        prev = r.d[2];
        CHECK(r.d[3] == CANPROXY_STATE_STARTING);
    }
    CHECK(skips == 0);
    CHECK(pub.lastProxyState() == CANPROXY_STATE_STARTING);
    {
        canproxy_motion_t m; canproxy_unpack_motion(cap.last(CANPROXY_ID_MOTION)->d, &m);
        CHECK(!m.speed_valid && !m.rpm_valid);
        canproxy_status_t st; canproxy_unpack_status(cap.last(CANPROXY_ID_STATUS)->d, &st);
        CHECK(st.capabilities == 0);
    }

    // Plugin comes up: link + state -> ok, values live, assist appears.
    cap.recs.clear();
    store.setLink(true, cap.now);
    store.publish(fullState(), cap.now);
    const Nanos t0 = cap.now;
    for (; cap.now <= t0 + 1000 * MS; cap.now += MS)
        pub.tick(cap.now);
    CHECK(pub.lastProxyState() == CANPROXY_STATE_OK);
    CHECK(cap.countOf(CANPROXY_ID_ASSIST) == 5);
    {
        canproxy_motion_t m; canproxy_unpack_motion(cap.last(CANPROXY_ID_MOTION)->d, &m);
        CHECK(m.speed_valid && m.speed_kmh == 60.0);
        canproxy_status_t st; canproxy_unpack_status(cap.last(CANPROXY_ID_STATUS)->d, &st);
        CHECK(st.capabilities == (CANPROXY_CAP_SPEED | CANPROXY_CAP_SOC | CANPROXY_CAP_ASSIST));
        CHECK(st.proxy_state == CANPROXY_STATE_OK);
        canproxy_identity_t id; canproxy_unpack_identity(cap.last(CANPROXY_ID_IDENTITY)->d, &id);
        CHECK(id.drivetrain == CANPROXY_DRIVETRAIN_BEV && id.plugin_major == 1 && id.plugin_minor == 2);
    }

    // Plugin goes silent: heartbeat continues, state -> degraded within the
    // timeout, values SNA, capabilities held.
    cap.recs.clear();
    const Nanos t1 = cap.now;
    for (; cap.now <= t1 + 3000 * MS; cap.now += MS)
        pub.tick(cap.now);
    CHECK(cap.countOf(CANPROXY_ID_STATUS) == 30 || cap.countOf(CANPROXY_ID_STATUS) == 31);
    CHECK(pub.lastProxyState() == CANPROXY_STATE_DEGRADED);
    {
        canproxy_motion_t m; canproxy_unpack_motion(cap.last(CANPROXY_ID_MOTION)->d, &m);
        CHECK(!m.speed_valid);
        canproxy_status_t st; canproxy_unpack_status(cap.last(CANPROXY_ID_STATUS)->d, &st);
        CHECK(st.capabilities == (CANPROXY_CAP_SPEED | CANPROXY_CAP_SOC | CANPROXY_CAP_ASSIST));
    }
    // Find when the state flipped: must be within timeout + one status cycle.
    Nanos flip = -1;
    for (auto &r : cap.recs) if (r.id == CANPROXY_ID_STATUS && r.d[3] == CANPROXY_STATE_DEGRADED) { flip = r.t; break; }
    CHECK(flip >= 0 && flip - (t0 + 1000 * MS) <= 1100 * MS + MS);

    // Link drops: no-vehicle. Comes back with data: ok again, no restart.
    store.setLink(false, cap.now);
    pub.tick(cap.now += 100 * MS);
    CHECK(pub.lastProxyState() == CANPROXY_STATE_NO_VEHICLE);
    store.setLink(true, cap.now);
    store.publish(fullState(), cap.now);
    pub.tick(cap.now += 100 * MS);
    CHECK(pub.lastProxyState() == CANPROXY_STATE_OK);

    // A long suspension resyncs instead of bursting.
    cap.recs.clear();
    cap.now += 5000 * MS;
    pub.tick(cap.now);
    CHECK(cap.countOf(CANPROXY_ID_MOTION) == 1);
    pub.tick(cap.now + 10 * MS);
    CHECK(cap.countOf(CANPROXY_ID_MOTION) == 1);
    pub.tick(cap.now + 50 * MS);
    CHECK(cap.countOf(CANPROXY_ID_MOTION) == 2);
}

int main()
{
    testDerive();
    testSchedule();
    return REPORT("publisher_tests");
}
