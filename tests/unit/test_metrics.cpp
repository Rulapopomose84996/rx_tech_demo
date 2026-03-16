#include <cassert>

#include "rxtech/metrics.h"

int main() {
    rxtech::MetricsCollector metrics;
    metrics.on_burst(4U, 512U);
    metrics.on_parsed_packet();
    metrics.on_ring_depth(3U);

    const rxtech::RunSummary summary = metrics.finalize("socket", "rx_only", "smoke", 1U);
    assert(summary.rx_packets == 4U);
    assert(summary.rx_bytes == 512U);
    assert(summary.batch_p99 == 4U);
    return 0;
}
