#include "rxtech/socket_backend.h"

#include <vector>

#include "rxtech/rx_config.h"
#include "rxtech/time_utils.h"

namespace rxtech {

std::string SocketBackend::name() const {
    return "socket";
}

bool SocketBackend::init(const RxConfig& config) {
    (void)config;
    stats_ = {};
    return true;
}

bool SocketBackend::recv_burst(RxBurst& burst, std::uint32_t max_burst) {
    burst.packets.clear();
    const std::uint32_t packet_count = max_burst > 0U ? (max_burst < 4U ? max_burst : 4U) : 0U;
    burst.packets.reserve(packet_count);

    static std::vector<std::uint8_t> dummy_payload(256U, 0xABU);
    for (std::uint32_t index = 0; index < packet_count; ++index) {
        PacketDesc packet;
        packet.data = dummy_payload.data();
        packet.len = static_cast<std::uint32_t>(dummy_payload.size());
        packet.ts_ns = steady_clock_now_ns();
        burst.packets.push_back(packet);
        ++stats_.rx_packets;
        stats_.rx_bytes += packet.len;
    }

    return true;
}

void SocketBackend::release_burst(RxBurst& burst) {
    burst.packets.clear();
}

BackendStats SocketBackend::stats() const {
    return stats_;
}

void SocketBackend::shutdown() {
}

}  // namespace rxtech
