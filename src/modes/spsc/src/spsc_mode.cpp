#include "rxtech/spsc_mode.h"

#include "rxtech/metrics.h"
#include "rxtech/parser.h"

namespace rxtech {

SpscMode::SpscMode() : ring_(1024U) {
}

std::string SpscMode::name() const {
    return "spsc";
}

void SpscMode::process(RxBurst& burst, IMetricsCollector& metrics) {
    std::uint64_t total_bytes = 0;
    for (const PacketDesc& packet : burst.packets) {
        total_bytes += packet.len;
        const ParsedPacketMeta meta = parse_packet(packet);
        if (!meta.valid) {
            metrics.on_drop();
            continue;
        }

        if (!ring_.push(packet)) {
            metrics.on_drop();
            continue;
        }

        metrics.on_parsed_packet();
        metrics.on_ring_depth(ring_.size());
    }

    PacketDesc drained;
    while (ring_.pop(drained)) {
    }

    metrics.on_burst(burst.packets.size(), total_bytes);
}

}  // namespace rxtech
