#include "obd/J1979Client.h"
#include <chrono>

namespace obd {

static int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

void J1979Client::setBroadcastSink(long id, std::function<void(const Frame &)> sink)
{
    m_broadcastId = id;
    m_broadcastSink = std::move(sink);
}

std::string J1979Client::open(const std::string &interface)
{
    std::vector<uint32_t> accept;
    for (uint32_t id = kResponseIdFirst; id <= kResponseIdLast; id++)
        accept.push_back(id);
    if (m_broadcastId >= 0)
        accept.push_back(static_cast<uint32_t>(m_broadcastId));
    return m_ch.open(interface, accept);
}

void J1979Client::close()
{
    m_ch.close();
}

bool J1979Client::request(uint8_t pid, int timeoutMs, Response *out)
{
    Frame req;
    req.id = kRequestId;
    req.dlc = 8;
    buildRequest(kModeCurrentData, pid, req.data);
    if (!m_ch.send(req))
        return false;
    const int64_t deadline = nowMs() + timeoutMs;
    for (;;) {
        const int64_t left = deadline - nowMs();
        if (left <= 0)
            return false;
        Frame f;
        if (!m_ch.receive(f, static_cast<int>(left)))
            return false;
        if (m_broadcastId >= 0 && f.id == static_cast<uint32_t>(m_broadcastId)) {
            if (m_broadcastSink) m_broadcastSink(f);
            continue;
        }
        if (!isResponseId(f.id))
            continue;
        auto r = parseSingleFrame(f.data, f.dlc);
        if (!r || r->mode != kModeCurrentData + kResponseOffset || r->pid != pid)
            continue;
        // First responder wins, then stick: once an ECU has answered us,
        // other ECUs' answers to the functional request are ignored.
        if (m_primaryEcu == 0)
            m_primaryEcu = f.id;
        else if (f.id != m_primaryEcu)
            continue;
        *out = *r;
        return true;
    }
}

bool J1979Client::discover(int timeoutMs)
{
    std::set<uint8_t> found;
    m_primaryEcu = 0;
    uint8_t base = PID_SUPPORTED_00;
    for (int block = 0; block < 7; block++) {
        Response r;
        if (!request(base, timeoutMs, &r) || r.len < 4) {
            if (block == 0)
                return false;
            break;
        }
        for (uint8_t pid : supportedPidsFrom(base, r.data))
            if (!isSupportedPid(pid))
                found.insert(pid);
        if (!nextBlockAdvertised(r.data))
            break;
        base = static_cast<uint8_t>(base + 0x20);
    }
    m_supported = found;
    return true;
}

bool J1979Client::query(uint8_t pid, int timeoutMs, double *value)
{
    Response r;
    if (!request(pid, timeoutMs, &r))
        return false;
    auto v = decode(pid, r.data, r.len);
    if (!v)
        return false;
    *value = *v;
    return true;
}

void J1979Client::idle(int timeoutMs)
{
    Frame f;
    if (m_ch.receive(f, timeoutMs))
        if (m_broadcastId >= 0 && f.id == static_cast<uint32_t>(m_broadcastId) && m_broadcastSink)
            m_broadcastSink(f);
}

} // namespace obd
