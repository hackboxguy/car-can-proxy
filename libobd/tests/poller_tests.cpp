#include "obd/Poller.h"
#include <cstdio>

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main()
{
    obd::Poller p;
    CHECK(p.empty() && !p.nextDue(0));
    p.add(0x0D, 100);   // fast
    p.add(0x0C, 100);
    p.add(0x05, 500);   // slow
    p.add(0x2F, 2000);  // rare
    p.add(0x0D, 100);   // duplicate ignored
    CHECK(!p.empty());

    // Everything is due at t=0; each request pushes its own deadline out.
    int seen[256] = {};
    for (int i = 0; i < 4; i++) {
        auto pid = p.nextDue(0);
        CHECK(pid.has_value());
        if (pid) { seen[*pid]++; p.markRequested(*pid, 0); }
    }
    CHECK(seen[0x0D] == 1 && seen[0x0C] == 1 && seen[0x05] == 1 && seen[0x2F] == 1);
    CHECK(!p.nextDue(50));
    CHECK(p.nextDeadline(50) == 100);

    // Over 2 s, count requests per PID: 100 ms PIDs ~20x, 500 ms ~4x, 2000 ms ~1x.
    int count[256] = {};
    for (int64_t t = 1; t <= 2000; t++) {
        while (auto pid = p.nextDue(t)) { count[*pid]++; p.markRequested(*pid, t); p.markAnswered(*pid, t); }
    }
    CHECK(count[0x0D] == 20 && count[0x0C] == 20 && count[0x05] == 4 && count[0x2F] == 1);

    // Freshness: three intervals.
    obd::Poller q;
    q.add(0x0D, 100);
    CHECK(!q.fresh(0x0D, 0));
    q.markRequested(0x0D, 0);
    q.markAnswered(0x0D, 5);
    CHECK(q.fresh(0x0D, 305) && !q.fresh(0x0D, 306));
    CHECK(!q.fresh(0x99, 0));

    // Unanswered streak resets on any answer.
    CHECK(q.unansweredStreak() == 0);
    q.markRequested(0x0D, 400); q.markRequested(0x0D, 500); q.markRequested(0x0D, 600);
    CHECK(q.unansweredStreak() == 3);
    q.markAnswered(0x0D, 610);
    CHECK(q.unansweredStreak() == 0);

    // A slow PID is not starved by fast ones: when both are overdue the one
    // further past its deadline relative to its interval goes first.
    obd::Poller r;
    r.add(0x0D, 100);
    r.add(0x05, 500);
    r.markRequested(0x0D, 0);
    r.markRequested(0x05, 0);
    CHECK(r.nextDue(600).value_or(0) == 0x0D);   // 500 ms overdue on 100 vs 100 on 500
    r.markRequested(0x0D, 600);
    CHECK(r.nextDue(600).value_or(0) == 0x05);

    std::printf("poller_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
