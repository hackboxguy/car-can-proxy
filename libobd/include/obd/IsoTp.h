// ISO 15765-2 transport as a kernel socket (CAN_ISOTP). The kernel does
// segmentation, flow control and reassembly; this is a thin client wrapper
// for one request/response pair (tester tx id -> ECU rx id).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace obd {

class IsoTpChannel {
public:
    IsoTpChannel() = default;
    ~IsoTpChannel();
    IsoTpChannel(const IsoTpChannel &) = delete;
    IsoTpChannel &operator=(const IsoTpChannel &) = delete;

    // txId: where we send (e.g. 0x7E4), rxId: where the ECU answers (0x7EC).
    // Returns an error string; mentions the kernel module if it is missing.
    std::string open(const std::string &interface, uint32_t txId, uint32_t rxId);
    void close();
    bool isOpen() const { return m_fd >= 0; }

    // Send one PDU and wait for one PDU back. False on timeout or error.
    bool transact(const std::vector<uint8_t> &request, std::vector<uint8_t> &response, int timeoutMs);

    static bool kernelSupportAvailable();

private:
    int m_fd = -1;
};

} // namespace obd
