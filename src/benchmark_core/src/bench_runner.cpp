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

    RxConfig effective_config = context.config;
    const std::uint32_t configured_duration_override = context.config.duration_seconds;
    if (!context.scenario.steps.empty()) {
        const ScenarioStep& first_step = context.scenario.steps.front();
        if (effective_config.packet_size_bytes == 0U && first_step.packet_size_bytes != 0U) {
            effective_config.packet_size_bytes = first_step.packet_size_bytes;
        }
    }

    if (effective_config.duration_seconds == 0U && context.scenario.steps.empty()) {
        effective_config.duration_seconds = 5U;
    }

    if (!context.backend->init(effective_config)) {
        throw std::runtime_error("backend init failed");
    }

    std::uint32_t total_duration_seconds = 0U;
    const std::vector<ScenarioStep> steps =
        context.scenario.steps.empty()
            ? std::vector<ScenarioStep>{{"measure",
                                         "steady",
                                         "fixed",
                                         0.0,
                                         1.0,
                                         effective_config.duration_seconds,
                                         1U,
                                         effective_config.packet_size_bytes,
                                         0U}}
            : context.scenario.steps;

    for (const ScenarioStep& step : steps) {
        const std::uint32_t step_duration = configured_duration_override != 0U
                                                ? configured_duration_override
                                                : std::max<std::uint32_t>(1U, step.duration_seconds);
        total_duration_seconds += step_duration;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(std::max<std::uint32_t>(1U, step_duration));
        while (std::chrono::steady_clock::now() < deadline) {
            RxBurst burst;
            const bool ok = context.backend->recv_burst(burst, effective_config.max_burst);
            if (!ok) {
                throw std::runtime_error("backend recv_burst failed");
            }

            if (!burst.packets.empty()) {
                context.mode->process(burst, *context.metrics);
            }
            context.backend->release_burst(burst);
        }
    }

    RunSummary summary = context.metrics->finalize(
        context.backend->name(),
        context.mode->name(),
        context.scenario.scenario_name,
        std::max<std::uint32_t>(1U, total_duration_seconds));

    if (!steps.empty()) {
        summary.packet_size_profile = steps.front().packet_size_profile;
    }

    const BackendStats backend_stats = context.backend->stats();
    summary.rx_packets = backend_stats.rx_packets != 0U ? backend_stats.rx_packets : summary.rx_packets;
    summary.rx_bytes = backend_stats.rx_bytes != 0U ? backend_stats.rx_bytes : summary.rx_bytes;
    summary.dropped_packets += backend_stats.backend_drops;
    summary.backend_errors += backend_stats.rx_errors;
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
    summary.xsk_mode = backend_stats.xsk_mode;
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
