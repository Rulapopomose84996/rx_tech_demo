#include "rxtech/rx_only_mode.h"

#include "rxtech/metrics.h"
#include "rxtech/time_utils.h"

namespace rxtech {

std::string RxOnlyMode::name() const {
    return "rx_only";
}

void RxOnlyMode::process(RxBurst& burst, IMetricsCollector& metrics) {
    std::uint64_t total_bytes = 0;
    for (const PacketDesc& packet : burst.packets) {
        total_bytes += packet.len;
        if (packet.ts_ns != 0U) {
            metrics.on_packet_latency_ns(steady_clock_now_ns() - packet.ts_ns);
        }
    }
    metrics.on_burst(burst.packets.size(), total_bytes);
}

}  // namespace rxtech
