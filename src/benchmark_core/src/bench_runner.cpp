#include "rxtech/bench_runner.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include "rxtech/report_writer.h"

namespace rxtech {

RunSummary BenchRunner::run(BenchContext& context) {
    if (!context.backend || !context.mode || !context.metrics) {
        throw std::runtime_error("bench context is incomplete");
    }

    if (!context.backend->init(context.config)) {
        throw std::runtime_error("backend init failed");
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(std::max<std::uint32_t>(1U, context.config.duration_seconds));
    while (std::chrono::steady_clock::now() < deadline) {
        RxBurst burst;
        const bool ok = context.backend->recv_burst(burst, context.config.max_burst);
        if (!ok) {
            throw std::runtime_error("backend recv_burst failed");
        }

        if (!burst.packets.empty()) {
            context.mode->process(burst, *context.metrics);
        }
        context.backend->release_burst(burst);
    }

    RunSummary summary = context.metrics->finalize(
        context.backend->name(),
        context.mode->name(),
        context.scenario.scenario_name,
        context.config.duration_seconds);

    const BackendStats backend_stats = context.backend->stats();
    summary.rx_packets = backend_stats.rx_packets != 0U ? backend_stats.rx_packets : summary.rx_packets;
    summary.rx_bytes = backend_stats.rx_bytes != 0U ? backend_stats.rx_bytes : summary.rx_bytes;
    summary.dropped_packets += backend_stats.backend_drops;
    summary.rx_polls = backend_stats.rx_polls;
    summary.empty_polls = backend_stats.empty_polls;
    summary.queue_id = backend_stats.queue_id;
    summary.xdp_prog_id = backend_stats.xdp_prog_id;
    summary.xsk_bind_flags = backend_stats.xsk_bind_flags;
    summary.umem_size = backend_stats.umem_size;
    summary.frame_size = backend_stats.frame_size;
    summary.fill_ring_size = backend_stats.fill_ring_size;
    summary.completion_ring_size = backend_stats.completion_ring_size;
    summary.xdp_attach_mode = backend_stats.xdp_attach_mode;
    if (summary.rx_polls > 0U) {
        summary.empty_poll_ratio =
            static_cast<double>(summary.empty_polls) / static_cast<double>(summary.rx_polls);
    }

    write_summary_json(summary, context.config.output_dir);
    write_summary_csv(summary, context.config.output_dir);

    context.backend->shutdown();
    return summary;
}

}  // namespace rxtech
