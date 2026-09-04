#include "obd/Poller.h"

namespace obd {

void Poller::clear()
{
    m_items.clear();
    m_unanswered = 0;
}

void Poller::add(uint8_t pid, int64_t intervalMs)
{
    if (find(pid))
        return;
    m_items.push_back({ pid, intervalMs, 0, -1 });
}

const Poller::Item *Poller::find(uint8_t pid) const
{
    for (const auto &it : m_items)
        if (it.pid == pid) return &it;
    return nullptr;
}

Poller::Item *Poller::find(uint8_t pid)
{
    for (auto &it : m_items)
        if (it.pid == pid) return &it;
    return nullptr;
}

std::optional<uint8_t> Poller::nextDue(int64_t now) const
{
    const Item *best = nullptr;
    for (const auto &it : m_items) {
        if (now < it.due) continue;
        // most overdue relative to its own interval wins, so a slow PID
        // cannot be starved by fast ones
        if (!best || (now - it.due) * best->interval > (now - best->due) * it.interval)
            best = &it;
    }
    if (!best) return std::nullopt;
    return best->pid;
}

int64_t Poller::nextDeadline(int64_t now) const
{
    int64_t earliest = -1;
    for (const auto &it : m_items)
        if (earliest < 0 || it.due < earliest) earliest = it.due;
    if (earliest < 0) return now;
    return earliest < now ? now : earliest;
}

void Poller::markRequested(uint8_t pid, int64_t now)
{
    if (Item *it = find(pid)) {
        it->due = now + it->interval;
        m_unanswered++;
    }
}

void Poller::markAnswered(uint8_t pid, int64_t now)
{
    if (Item *it = find(pid)) {
        it->lastAnswer = now;
        m_unanswered = 0;
    }
}

bool Poller::fresh(uint8_t pid, int64_t now) const
{
    const Item *it = find(pid);
    return it && it->lastAnswer >= 0 && now - it->lastAnswer <= 3 * it->interval;
}

} // namespace obd
