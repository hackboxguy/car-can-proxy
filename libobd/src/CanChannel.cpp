#include "obd/CanChannel.h"
#include <cerrno>
#include <cstring>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace obd {

CanChannel::~CanChannel()
{
    stopRecording();
    close();
}

std::string CanChannel::open(const std::string &interface, const std::vector<uint32_t> &acceptIds)
{
    close();
    int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0)
        return std::string("socket(PF_CAN): ") + std::strerror(errno);
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof ifr);
    std::strncpy(ifr.ifr_name, interface.c_str(), sizeof(ifr.ifr_name) - 1);
    if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        std::string err = "interface " + interface + ": " + std::strerror(errno);
        ::close(fd);
        return err;
    }
    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof addr);
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (::bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof addr) < 0) {
        std::string err = "bind " + interface + ": " + std::strerror(errno);
        ::close(fd);
        return err;
    }
    if (!acceptIds.empty()) {
        std::vector<struct can_filter> filters;
        for (uint32_t id : acceptIds)
            filters.push_back({ id, CAN_SFF_MASK });
        ::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FILTER, filters.data(),
                     static_cast<socklen_t>(filters.size() * sizeof(struct can_filter)));
    }
    // We want to see our own requests in the log for replay fidelity, but
    // not to consume them as responses; the caller filters by ID.
    m_fd = fd;
    m_interface = interface;
    return "";
}

void CanChannel::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

std::string CanChannel::startRecording(const std::string &path)
{
    stopRecording();
    m_log = std::fopen(path.c_str(), "w");
    if (!m_log)
        return "record " + path + ": " + std::strerror(errno);
    return "";
}

void CanChannel::stopRecording()
{
    if (m_log) {
        std::fclose(m_log);
        m_log = nullptr;
    }
}

void CanChannel::record(const Frame &f)
{
    if (!m_log)
        return;
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    std::fprintf(m_log, "(%ld.%06ld) %s %03X#", static_cast<long>(tv.tv_sec),
                 static_cast<long>(tv.tv_usec), m_interface.c_str(), f.id);
    for (int i = 0; i < f.dlc; i++)
        std::fprintf(m_log, "%02X", f.data[i]);
    std::fputc('\n', m_log);
    std::fflush(m_log);
}

bool CanChannel::send(const Frame &f)
{
    if (m_fd < 0 || f.dlc > 8)
        return false;
    struct can_frame cf;
    std::memset(&cf, 0, sizeof cf);
    cf.can_id = f.id & CAN_SFF_MASK;
    cf.can_dlc = f.dlc;
    std::memcpy(cf.data, f.data, f.dlc);
    if (::write(m_fd, &cf, sizeof cf) != static_cast<ssize_t>(sizeof cf))
        return false;
    record(f);
    return true;
}

bool CanChannel::receive(Frame &f, int timeoutMs)
{
    if (m_fd < 0)
        return false;
    struct pollfd pfd = { m_fd, POLLIN, 0 };
    const int rc = ::poll(&pfd, 1, timeoutMs);
    if (rc <= 0)
        return false;
    struct can_frame cf;
    if (::read(m_fd, &cf, sizeof cf) != static_cast<ssize_t>(sizeof cf))
        return false;
    f.id = cf.can_id & CAN_SFF_MASK;
    f.dlc = cf.can_dlc > 8 ? 8 : cf.can_dlc;
    std::memcpy(f.data, cf.data, 8);
    record(f);
    return true;
}

} // namespace obd
