#include <cassert>
#include <memory>

#include "rxtech/bench_runner.h"
#include "rxtech/metrics.h"
#include "rxtech/rx_config.h"
#include "rxtech/rx_only_mode.h"
#include "rxtech/scenario.h"
#include "rxtech/socket_backend.h"

int main() {
    rxtech::BenchContext context;
    context.config = rxtech::load_default_config();
    context.config.output_dir = "results/test_smoke";
    context.config.bind_address = "127.0.0.1";
    context.config.udp_port = 19999;
    context.config.enable_internal_traffic = true;
    context.config.duration_seconds = 1U;
    context.config.packet_size_bytes = 256U;
    context.scenario = rxtech::load_scenario("smoke");
    context.backend = std::make_unique<rxtech::SocketBackend>();
    context.mode = std::make_unique<rxtech::RxOnlyMode>();
    context.metrics = std::make_unique<rxtech::MetricsCollector>();

    rxtech::BenchRunner runner;
    const rxtech::RunSummary summary = runner.run(context);
    assert(summary.run_status == "success");
    assert(summary.measure_step_count == 1U);
    assert(summary.total_step_count == 2U);
    assert(summary.rx_packets > 0U);
    return 0;
}
