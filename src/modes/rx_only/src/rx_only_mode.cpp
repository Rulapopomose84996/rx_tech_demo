#include "rxtech/rx_only_mode.h"

#include "rxtech/metrics.h"

namespace rxtech {

std::string RxOnlyMode::name() const {
    return "rx_only";
}

void RxOnlyMode::process(RxBurst& burst, IMetricsCollector& metrics) {
    std::uint64_t total_bytes = 0;
    for (const PacketDesc& packet : burst.packets) {
        total_bytes += packet.len;
    }
    metrics.on_burst(burst.packets.size(), total_bytes);
}

}  // namespace rxtech
