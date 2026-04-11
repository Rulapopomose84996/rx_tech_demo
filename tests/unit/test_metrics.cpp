#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "rxtech/metrics.h"

int main()
{
    rxtech::MetricsCollector metrics;
    metrics.on_burst(4U, 512U);
    metrics.on_global_packet_sequence(100U);
    metrics.on_global_packet_sequence(101U);
    metrics.on_global_packet_sequence(104U);
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
    assert(summary.protocol.rx_packets == 4U);
    assert(summary.protocol.rx_bytes == 512U);
    assert(summary.protocol.parsed_packets == 2U);
    assert(summary.protocol.control_table_packets == 1U);
    assert(summary.protocol.data_packets == 1U);
    assert(summary.protocol.dropped_packets == 3U);
    assert(summary.backend.errors == 1U);
    assert(summary.performance.pool_exhaustion_count == 1U);
    assert(summary.performance.output_backpressure_count == 1U);
    assert(summary.performance.late_packet_accepted_count == 1U);
    assert(summary.performance.late_packet_rejected_count == 1U);
    assert(summary.global_packet_sequence.available);
    assert(summary.global_packet_sequence.checked_packets == 3U);
    assert(summary.global_packet_sequence.gap_count == 1U);
    assert(summary.global_packet_sequence.missing_packets == 2U);
    assert(!summary.global_packet_sequence.first_gap.empty());
    assert(summary.reject_by_reason[static_cast<std::size_t>(rxtech::RejectReason::invalid_len)] == 1U);
    assert(summary.reject_by_reason[static_cast<std::size_t>(rxtech::RejectReason::truncated_datagram)] == 1U);
#if defined(__linux__)
    assert(summary.performance.cpu_metrics_available);
    assert(summary.performance.cpu_metrics_status == "ok");
#else
    assert(!summary.performance.cpu_metrics_available);
#endif
#if defined(RXTECH_DEBUG_DIAGNOSTICS) && RXTECH_DEBUG_DIAGNOSTICS
    assert(summary.performance.batch_p99 == 4U);
    assert(summary.performance.latency_p50_us > 0.0);
    assert(summary.performance.latency_p99_us >= summary.performance.latency_p50_us);
#endif

    auto other = metrics.clone_empty();
    other->on_burst(2U, 256U);
    other->on_drop();
    other->on_valid_packet(rxtech::PacketKind::data_packet);
    assert(metrics.absorb(*other));
    const rxtech::RunSummary merged = metrics.finalize("socket", "rx_only", "smoke", 1U);
    assert(merged.protocol.rx_packets == 6U);
    assert(merged.protocol.parsed_packets == 3U);
    assert(merged.protocol.data_packets == 2U);
    assert(merged.protocol.dropped_packets == 4U);
    return 0;
}
