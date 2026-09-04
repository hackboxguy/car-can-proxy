#include "obd/IsoTp.h"
#include <cerrno>
#include <cstring>
#include <linux/can.h>
#include <linux/can/isotp.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace obd {

IsoTpChannel::~IsoTpChannel() { close(); }

bool IsoTpChannel::kernelSupportAvailable()
{
    int fd = ::socket(PF_CAN, SOCK_DGRAM, CAN_ISOTP);
    if (fd < 0)
        return false;
    ::close(fd);
    return true;
}

std::string IsoTpChannel::open(const std::string &interface, uint32_t txId, uint32_t rxId)
{
    close();
    int fd = ::socket(PF_CAN, SOCK_DGRAM, CAN_ISOTP);
    if (fd < 0)
        return std::string("socket(CAN_ISOTP): ") + std::strerror(errno) +
               (errno == EPROTONOSUPPORT ? " (is the can-isotp kernel module loaded?)" : "");

    // Drain stale data; keep the kernel's default flow-control parameters.
    struct can_isotp_options opts;
    std::memset(&opts, 0, sizeof opts);
    ::setsockopt(fd, SOL_CAN_ISOTP, CAN_ISOTP_OPTS, &opts, sizeof opts);

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
    addr.can_addr.tp.tx_id = txId;
    addr.can_addr.tp.rx_id = rxId;
    if (::bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof addr) < 0) {
        std::string err = "bind isotp " + interface + ": " + std::strerror(errno);
        ::close(fd);
        return err;
    }
    m_fd = fd;
    return "";
}

void IsoTpChannel::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool IsoTpChannel::transact(const std::vector<uint8_t> &request, std::vector<uint8_t> &response, int timeoutMs)
{
    if (m_fd < 0 || request.empty())
        return false;
    // Discard anything left over from an earlier, late answer.
    for (;;) {
        struct pollfd pfd = { m_fd, POLLIN, 0 };
        if (::poll(&pfd, 1, 0) <= 0)
            break;
        uint8_t junk[4096];
        if (::read(m_fd, junk, sizeof junk) <= 0)
            break;
    }
    if (::write(m_fd, request.data(), request.size()) != static_cast<ssize_t>(request.size()))
        return false;
    struct pollfd pfd = { m_fd, POLLIN, 0 };
    if (::poll(&pfd, 1, timeoutMs) <= 0)
        return false;
    uint8_t buf[4096];
    const ssize_t n = ::read(m_fd, buf, sizeof buf);
    if (n <= 0)
        return false;
    response.assign(buf, buf + n);
    return true;
}

} // namespace obd
