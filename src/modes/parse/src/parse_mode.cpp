#include "rxtech/parse_mode.h"

#include "rxtech/metrics.h"
#include "rxtech/parser.h"

namespace rxtech {

std::string ParseMode::name() const {
    return "parse";
}

void ParseMode::process(RxBurst& burst, IMetricsCollector& metrics) {
    std::uint64_t total_bytes = 0;
    for (const PacketDesc& packet : burst.packets) {
        total_bytes += packet.len;
        const ParsedPacketMeta meta = parse_packet(packet);
        if (meta.valid) {
            metrics.on_parsed_packet();
        } else {
            metrics.on_drop();
        }
    }
    metrics.on_burst(burst.packets.size(), total_bytes);
}

}  // namespace rxtech
