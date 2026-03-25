#include <cassert>

#include "rxtech/metrics.h"

int main() {
    rxtech::MetricsCollector metrics;
    metrics.on_burst(4U, 512U);
    metrics.on_parsed_packet();
    metrics.on_drop();
    metrics.on_error();
    metrics.on_pool_exhaustion();
    metrics.on_packet_latency_ns(1200U);
    metrics.on_packet_latency_ns(5200U);
    metrics.on_ring_depth(3U);
    metrics.on_port_packet(0U, 512U);
    metrics.on_reassembled_block(0U, 256U);
    metrics.on_duplicate_fragment(0U);
    metrics.on_invalid_header(1U);
    metrics.on_missing_fragments(1U, 2U);
    metrics.on_reassembly_timeout(1U);

    const rxtech::RunSummary summary = metrics.finalize("socket", "rx_only", "smoke", 1U);
    assert(summary.rx_packets == 4U);
    assert(summary.rx_bytes == 512U);
    assert(summary.batch_p99 == 4U);
    assert(summary.dropped_packets == 1U);
    assert(summary.backend_errors == 1U);
    assert(summary.pool_exhaustion_count == 1U);
    assert(summary.latency_p50_us > 0.0);
    assert(summary.latency_p99_us >= summary.latency_p50_us);
    assert(summary.per_port.size() == 2U);
    assert(summary.per_port[0].port_id == 0U);
    assert(summary.per_port[0].rx_packets == 1U);
    assert(summary.per_port[0].reassembled_blocks == 1U);
    assert(summary.per_port[0].duplicate_fragments == 1U);
    assert(summary.per_port[1].invalid_header_count == 1U);
    assert(summary.per_port[1].missing_fragments == 2U);
    assert(summary.per_port[1].reassembly_timeout_count == 1U);

    auto other = metrics.clone_empty();
    other->on_burst(2U, 256U);
    other->on_drop();
    other->on_port_packet(0U, 256U);
    assert(metrics.absorb(*other));
    const rxtech::RunSummary merged = metrics.finalize("socket", "rx_only", "smoke", 1U);
    assert(merged.rx_packets == 6U);
    assert(merged.dropped_packets == 2U);
    assert(merged.per_port[0].rx_packets == 2U);
    return 0;
}
