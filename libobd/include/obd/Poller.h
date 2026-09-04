// Poll scheduling for a request/response source: each PID has an interval,
// the scheduler says which PID is due next and when a PID's last good value
// has gone stale (three missed intervals). Pure; driven by the caller's clock.
#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace obd {

class Poller {
public:
    void clear();
    void add(uint8_t pid, int64_t intervalMs);
    void remove(uint8_t pid);
    bool empty() const { return m_items.empty(); }

    // The PID whose request is most overdue at `now`, if any is due.
    std::optional<uint8_t> nextDue(int64_t now) const;
    // Earliest time any PID becomes due (now if one already is).
    int64_t nextDeadline(int64_t now) const;

    void markRequested(uint8_t pid, int64_t now);
    void markAnswered(uint8_t pid, int64_t now);

    // True while a PID's last answer is younger than three intervals.
    bool fresh(uint8_t pid, int64_t now) const;
    // Requests without any answer, across all PIDs, since the last answer.
    int unansweredStreak() const { return m_unanswered; }

private:
    struct Item { uint8_t pid; int64_t interval; int64_t due; int64_t lastAnswer; };
    const Item *find(uint8_t pid) const;
    Item *find(uint8_t pid);
    std::vector<Item> m_items;
    int m_unanswered = 0;
};

} // namespace obd
