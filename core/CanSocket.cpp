#include "CanSocket.h"
#include <cerrno>
#include <cstring>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace canproxy {

CanSocket::~CanSocket() { close(); }

std::string CanSocket::open(const std::string &interface)
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

    // The proxy never reads the contract bus; drop everything inbound.
    ::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FILTER, nullptr, 0);

    m_fd = fd;
    return "";
}

void CanSocket::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool CanSocket::send(uint32_t id, const uint8_t *data, unsigned len)
{
    if (m_fd < 0 || len > CAN_MAX_DLEN)
        return false;
    struct can_frame frame;
    std::memset(&frame, 0, sizeof frame);
    frame.can_id = id & CAN_SFF_MASK;
    frame.can_dlc = static_cast<__u8>(len);
    std::memcpy(frame.data, data, len);
    return ::write(m_fd, &frame, sizeof frame) == static_cast<ssize_t>(sizeof frame);
}

} // namespace canproxy
