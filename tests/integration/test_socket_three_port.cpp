#include <cassert>
#include <memory>

#include "rxtech/bench_runner.h"
#include "rxtech/metrics.h"
#include "rxtech/parse_mode.h"
#include "rxtech/rx_config.h"
#include "rxtech/scenario.h"
#include "rxtech/socket_backend.h"

int main() {
    rxtech::BenchContext context;
    context.config = rxtech::load_default_config();
    context.config.output_dir = "results/test_socket_three_port";
    context.config.enable_internal_traffic = true;
    context.config.duration_seconds = 1U;
    context.config.packet_size_bytes = 128U;
    context.config.reassembly_timeout_ms = 1000U;
    context.config.receiver_endpoints = {
        {0U, "127.0.0.1", 20010U},
        {1U, "127.0.0.1", 20011U},
        {2U, "127.0.0.1", 20012U},
    };
    context.scenario = rxtech::load_scenario("smoke");
    context.backend = std::make_unique<rxtech::SocketBackend>();
    context.mode = std::make_unique<rxtech::ParseMode>(context.config.reassembly_timeout_ms);
    context.metrics = std::make_unique<rxtech::MetricsCollector>();

    rxtech::BenchRunner runner;
    const rxtech::RunSummary summary = runner.run(context);
    assert(summary.run_status == "success");
    assert(summary.per_port.size() == 3U);
    for (std::size_t index = 0; index < summary.per_port.size(); ++index) {
        assert(summary.per_port[index].port_id == index);
        assert(summary.per_port[index].rx_packets > 0U);
        assert(summary.per_port[index].reassembled_blocks > 0U);
        assert(summary.per_port[index].invalid_header_count == 0U);
    }
    return 0;
}
