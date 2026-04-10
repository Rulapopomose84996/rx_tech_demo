#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "rxtech/metrics.h"

int main()
{
    rxtech::MetricsCollector metrics;
    metrics.on_burst(4U, 512U);
    metrics.on_valid_packet(rxtech::PacketKind::control_table);
    metrics.on_valid_packet(rxtech::PacketKind::data_packet);
    metrics.on_reject(rxtech::RejectReason::invalid_len);
    metrics.on_reject(rxtech::RejectReason::truncated_datagram);
    metrics.on_drop();
    metrics.on_error();
    metrics.on_pool_exhaustion();
    metrics.on_output_backpressure();
    metrics.on_late_packet_accepted();
    metrics.on_late_packet_rejected();
    metrics.on_packet_latency_ns(1200U);
    metrics.on_packet_latency_ns(5200U);
    metrics.on_ring_depth(3U);

    const rxtech::RunSummary summary = metrics.finalize("socket", "rx_only", "smoke", 1U);
    assert(summary.rx_packets == 4U);
    assert(summary.rx_bytes == 512U);
    assert(summary.parsed_packets == 2U);
    assert(summary.control_table_packets == 1U);
    assert(summary.data_packets == 1U);
    assert(summary.dropped_packets == 3U);
    assert(summary.backend_errors == 1U);
    assert(summary.pool_exhaustion_count == 1U);
    assert(summary.output_backpressure_count == 1U);
    assert(summary.late_packet_accepted_count == 1U);
    assert(summary.late_packet_rejected_count == 1U);
    assert(summary.reject_by_reason[static_cast<std::size_t>(rxtech::RejectReason::invalid_len)] == 1U);
    assert(summary.reject_by_reason[static_cast<std::size_t>(rxtech::RejectReason::truncated_datagram)] == 1U);
#if defined(RXTECH_DEBUG_DIAGNOSTICS) && RXTECH_DEBUG_DIAGNOSTICS
    assert(summary.batch_p99 == 4U);
    assert(summary.latency_p50_us > 0.0);
    assert(summary.latency_p99_us >= summary.latency_p50_us);
#endif

    auto other = metrics.clone_empty();
    other->on_burst(2U, 256U);
    other->on_drop();
    other->on_valid_packet(rxtech::PacketKind::data_packet);
    assert(metrics.absorb(*other));
    const rxtech::RunSummary merged = metrics.finalize("socket", "rx_only", "smoke", 1U);
    assert(merged.rx_packets == 6U);
    assert(merged.parsed_packets == 3U);
    assert(merged.data_packets == 2U);
    assert(merged.dropped_packets == 4U);
    return 0;
}
