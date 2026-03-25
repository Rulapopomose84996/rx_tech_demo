#include "rxtech/parse_mode.h"

#include "rxtech/metrics.h"
#include "rxtech/parser.h"
#include "rxtech/time_utils.h"

namespace rxtech {

ParseMode::ParseMode(std::uint32_t reassembly_timeout_ms) : reassembler_(reassembly_timeout_ms) {
}

std::string ParseMode::name() const {
    return "parse";
}

void ParseMode::process(RxBurst& burst, IMetricsCollector& metrics) {
    std::uint64_t total_bytes = 0;
    for (const PacketDesc& packet : burst.packets) {
        total_bytes += packet.len;
        metrics.on_port_packet(packet.port_id, packet.len);
        if (packet.ts_ns != 0U) {
            metrics.on_packet_latency_ns(steady_clock_now_ns() - packet.ts_ns);
        }

        const ReassemblyExpiry expiry = reassembler_.expire_before(packet.ts_ns != 0U ? packet.ts_ns : steady_clock_now_ns());
        for (const auto& [port_id, count] : expiry.expired_blocks_by_port) {
            for (std::uint64_t index = 0; index < count; ++index) {
                metrics.on_reassembly_timeout(port_id);
            }
        }
        for (const auto& [port_id, missing] : expiry.missing_fragments_by_port) {
            metrics.on_missing_fragments(port_id, missing);
        }

        const ParsedPacketMeta meta = parse_packet(packet);
        if (!meta.valid) {
            metrics.on_invalid_header(packet.port_id);
            metrics.on_drop();
            continue;
        }

        metrics.on_parsed_packet();
        const ReassemblyPushResult reassembly = reassembler_.push(packet, meta);
        if (reassembly.duplicate_fragment) {
            metrics.on_duplicate_fragment(packet.port_id);
        }
        if (reassembly.complete) {
            metrics.on_reassembled_block(packet.port_id, reassembly.block.block_bytes);
        }
    }
    metrics.on_burst(burst.packets.size(), total_bytes);
}

}  // namespace rxtech
