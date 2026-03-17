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

    const rxtech::RunSummary summary = metrics.finalize("socket", "rx_only", "smoke", 1U);
    assert(summary.rx_packets == 4U);
    assert(summary.rx_bytes == 512U);
    assert(summary.batch_p99 == 4U);
    assert(summary.dropped_packets == 1U);
    assert(summary.backend_errors == 1U);
    assert(summary.pool_exhaustion_count == 1U);
    assert(summary.latency_p50_us > 0.0);
    assert(summary.latency_p99_us >= summary.latency_p50_us);
    return 0;
}
