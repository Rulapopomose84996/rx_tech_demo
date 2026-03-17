#include "rxtech/bench_runner.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#ifdef __linux__
#include <sys/resource.h>
#endif

#include "rxtech/report_writer.h"

namespace rxtech {

namespace {

struct CpuSnapshot {
    bool available = false;
    std::string reason = "unavailable";
    std::chrono::steady_clock::time_point wall_time = std::chrono::steady_clock::now();
#ifdef __linux__
    rusage self_usage{};
    rusage thread_usage{};
#endif
};

struct CpuMetrics {
    bool available = false;
    std::string status = "unavailable";
    double user_pct = 0.0;
    double sys_pct = 0.0;
    double peak_pct = 0.0;
};

double timeval_diff_seconds(const timeval& start, const timeval& end) {
    const double seconds = static_cast<double>(end.tv_sec - start.tv_sec);
    const double micros = static_cast<double>(end.tv_usec - start.tv_usec) / 1'000'000.0;
    return seconds + micros;
}

CpuSnapshot capture_cpu_snapshot() {
    CpuSnapshot snapshot;
    snapshot.wall_time = std::chrono::steady_clock::now();
#ifdef __linux__
    if (getrusage(RUSAGE_SELF, &snapshot.self_usage) != 0) {
        snapshot.reason = "getrusage(RUSAGE_SELF) failed";
        return snapshot;
    }
#ifdef RUSAGE_THREAD
    if (getrusage(RUSAGE_THREAD, &snapshot.thread_usage) != 0) {
        snapshot.reason = "getrusage(RUSAGE_THREAD) failed";
        return snapshot;
    }
    snapshot.available = true;
    snapshot.reason = "available";
#else
    snapshot.reason = "RUSAGE_THREAD is unavailable";
#endif
#else
    snapshot.reason = "CPU metrics require Linux or WSL";
#endif
    return snapshot;
}

CpuMetrics diff_cpu_metrics(const CpuSnapshot& start, const CpuSnapshot& end) {
    CpuMetrics metrics;
    if (!start.available || !end.available) {
        metrics.status = start.available ? end.reason : start.reason;
        return metrics;
    }

    const auto wall_duration = std::chrono::duration_cast<std::chrono::microseconds>(end.wall_time - start.wall_time).count();
    if (wall_duration <= 0) {
        metrics.status = "wall clock duration is not positive";
        return metrics;
    }

    const double wall_seconds = static_cast<double>(wall_duration) / 1'000'000.0;
#ifdef __linux__
    metrics.user_pct = timeval_diff_seconds(start.self_usage.ru_utime, end.self_usage.ru_utime) / wall_seconds * 100.0;
    metrics.sys_pct = timeval_diff_seconds(start.self_usage.ru_stime, end.self_usage.ru_stime) / wall_seconds * 100.0;
    metrics.peak_pct = (timeval_diff_seconds(start.thread_usage.ru_utime, end.thread_usage.ru_utime) +
                        timeval_diff_seconds(start.thread_usage.ru_stime, end.thread_usage.ru_stime)) /
                       wall_seconds * 100.0;
    metrics.available = true;
    metrics.status = "available";
#endif
    return metrics;
}

void apply_cpu_metrics(StepSummary& summary, const CpuMetrics& metrics) {
    summary.cpu_metrics_available = metrics.available;
    summary.cpu_metrics_status = metrics.status;
    if (metrics.available) {
        summary.cpu_user_pct = metrics.user_pct;
        summary.cpu_sys_pct = metrics.sys_pct;
        summary.cpu_peak_pct = metrics.peak_pct;
    }
}

void apply_cpu_metrics(RunSummary& summary, const CpuMetrics& metrics) {
    summary.cpu_metrics_available = metrics.available;
    summary.cpu_metrics_status = metrics.status;
    if (metrics.available) {
        summary.cpu_user_pct = metrics.user_pct;
        summary.cpu_sys_pct = metrics.sys_pct;
        summary.cpu_peak_pct = metrics.peak_pct;
    }
}

void apply_step_metadata(StepSummary& summary, const ScenarioStep& step, std::size_t index) {
    summary.step_index = index;
    summary.step_name = step.name;
    summary.phase = step.phase;
    summary.traffic_profile = step.traffic_profile;
    summary.packet_size_profile = step.packet_size_profile;
    summary.target_rate_gbps = step.target_rate_gbps;
    summary.burst_multiplier = step.burst_multiplier;
    summary.duration_seconds = step.duration_seconds;
    summary.face_count = step.face_count;
    summary.packet_size_bytes = step.packet_size_bytes;
    summary.burst_window_ms = step.burst_window_ms;
}

StepSummary make_step_summary(const RunSummary& metrics_summary,
                              const ScenarioStep& step,
                              std::size_t index,
                              const CpuMetrics& cpu_metrics,
                              const std::string& run_status,
                              const std::string& error_message) {
    StepSummary summary;
    apply_step_metadata(summary, step, index);
    summary.run_status = run_status;
    summary.error_message = error_message;
    summary.rx_packets = metrics_summary.rx_packets;
    summary.rx_bytes = metrics_summary.rx_bytes;
    summary.parsed_packets = metrics_summary.parsed_packets;
    summary.dropped_packets = metrics_summary.dropped_packets;
    summary.backend_errors = metrics_summary.backend_errors;
    summary.nic_drops = metrics_summary.nic_drops;
    summary.pool_exhaustion_count = metrics_summary.pool_exhaustion_count;
    summary.ring_high_watermark = metrics_summary.ring_high_watermark;
    summary.rx_polls = metrics_summary.rx_polls;
    summary.empty_polls = metrics_summary.empty_polls;
    summary.actual_rx_gbps = metrics_summary.actual_rx_gbps;
    summary.actual_rx_mpps = metrics_summary.actual_rx_mpps;
    summary.latency_p50_us = metrics_summary.latency_p50_us;
    summary.latency_p99_us = metrics_summary.latency_p99_us;
    summary.batch_avg = metrics_summary.batch_avg;
    summary.empty_poll_ratio = metrics_summary.empty_poll_ratio;
    summary.batch_p99 = metrics_summary.batch_p99;
    apply_cpu_metrics(summary, cpu_metrics);
    return summary;
}

std::vector<ScenarioStep> build_steps(const RxConfig& effective_config, const Scenario& scenario) {
    if (!scenario.steps.empty()) {
        return scenario.steps;
    }

    return std::vector<ScenarioStep>{{"measure",
                                      "measure",
                                      "steady",
                                      "fixed",
                                      0.0,
                                      1.0,
                                      effective_config.duration_seconds == 0U ? 5U : effective_config.duration_seconds,
                                      1U,
                                      effective_config.packet_size_bytes == 0U ? 512U : effective_config.packet_size_bytes,
                                      0U}};
}

void write_results(const RunSummary& summary, const RxConfig& config) {
    write_summary_json(summary, config.output_dir);
    write_summary_csv(summary, config.output_dir);
    write_step_summaries_json(summary.steps, config.output_dir);
    write_step_summaries_csv(summary.steps, config.output_dir);
}

RunSummary make_terminal_summary(const BenchContext& context,
                                 const RxConfig& effective_config,
                                 const std::vector<ScenarioStep>& steps,
                                 const std::string& run_status,
                                 const std::string& error_message,
                                 bool backend_available,
                                 const std::string& backend_reason) {
    RunSummary summary;
    summary.backend = context.backend != nullptr ? context.backend->name() : effective_config.backend_name;
    summary.mode = context.mode != nullptr ? context.mode->name() : effective_config.mode_name;
    summary.scenario = context.scenario.scenario_name;
    summary.run_status = run_status;
    summary.error_message = error_message;
    summary.backend_available = backend_available;
    summary.backend_status = backend_available ? "available" : "unavailable";
    summary.backend_reason = backend_reason;
    summary.total_step_count = static_cast<std::uint32_t>(steps.size());
    summary.measure_step_count = static_cast<std::uint32_t>(
        std::count_if(steps.begin(), steps.end(), [](const ScenarioStep& step) { return is_measure_step(step); }));
    for (const ScenarioStep& step : steps) {
        summary.scenario_duration_seconds += step.duration_seconds;
    }
    const ScenarioStep* representative = nullptr;
    for (const ScenarioStep& step : steps) {
        if (is_measure_step(step)) {
            representative = &step;
            break;
        }
    }
    if (representative == nullptr && !steps.empty()) {
        representative = &steps.front();
    }
    if (representative != nullptr) {
        summary.packet_size_profile = representative->packet_size_profile;
        summary.target_rate_gbps = representative->target_rate_gbps;
        summary.burst_multiplier = representative->burst_multiplier;
        summary.face_count = representative->face_count;
        summary.packet_size_bytes = representative->packet_size_bytes;
        summary.burst_window_ms = representative->burst_window_ms;
    }
    return summary;
}

void merge_backend_stats(RunSummary& summary, const BackendStats& backend_stats) {
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
        summary.empty_poll_ratio = static_cast<double>(summary.empty_polls) / static_cast<double>(summary.rx_polls);
    }
}

}  // namespace

RunSummary BenchRunner::run(BenchContext& context) {
    if (!context.backend || !context.mode || !context.metrics) {
        throw std::runtime_error("bench context is incomplete");
    }

    RxConfig effective_config = context.config;
    const std::vector<ScenarioStep> steps = build_steps(effective_config, context.scenario);
    if (effective_config.packet_size_bytes == 0U && !steps.empty() && steps.front().packet_size_bytes != 0U) {
        effective_config.packet_size_bytes = steps.front().packet_size_bytes;
    }
    if (effective_config.duration_seconds == 0U && !steps.empty()) {
        effective_config.duration_seconds = steps.front().duration_seconds;
    }
    if (effective_config.duration_seconds == 0U) {
        effective_config.duration_seconds = 5U;
    }

    const BackendInitResult init_result = context.backend->init(effective_config);
    if (!init_result.ok) {
        RunSummary summary = make_terminal_summary(context,
                                                  effective_config,
                                                  steps,
                                                  init_result.available ? "error" : "unavailable",
                                                  init_result.reason,
                                                  init_result.available,
                                                  init_result.reason);
        for (std::size_t index = 0; index < steps.size(); ++index) {
            StepSummary step_summary;
            apply_step_metadata(step_summary, steps[index], index);
            step_summary.run_status = "skipped";
            step_summary.error_message = init_result.reason;
            summary.steps.push_back(step_summary);
        }
        write_results(summary, effective_config);
        context.backend->shutdown();
        return summary;
    }

    auto aggregate_metrics = context.metrics->clone_empty();
    if (!aggregate_metrics) {
        throw std::runtime_error("failed to allocate aggregate metrics collector");
    }

    std::uint32_t measure_duration_seconds = 0U;
    double weighted_cpu_user = 0.0;
    double weighted_cpu_sys = 0.0;
    double weighted_cpu_peak = 0.0;
    std::uint32_t weighted_cpu_duration = 0U;
    std::string cpu_status = "unavailable";
    bool cpu_available = false;
    std::string run_status = "success";
    std::string run_error;
    std::vector<StepSummary> step_results;

    for (std::size_t index = 0; index < steps.size(); ++index) {
        const ScenarioStep& step = steps[index];
        auto step_metrics = context.metrics->clone_empty();
        if (!step_metrics) {
            throw std::runtime_error("failed to allocate step metrics collector");
        }

        const std::uint32_t step_duration = std::max<std::uint32_t>(1U, step.duration_seconds);
        const CpuSnapshot cpu_start = capture_cpu_snapshot();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(step_duration);
        std::string step_run_status = "success";
        std::string step_error;

        while (std::chrono::steady_clock::now() < deadline) {
            RxBurst burst;
            const bool ok = context.backend->recv_burst(burst, effective_config.max_burst);
            if (!ok) {
                step_run_status = "error";
                step_error = "backend recv_burst failed";
                context.backend->release_burst(burst);
                break;
            }

            if (!burst.packets.empty()) {
                context.mode->process(burst, *step_metrics);
            }
            context.backend->release_burst(burst);
        }

        const CpuMetrics cpu_metrics = diff_cpu_metrics(cpu_start, capture_cpu_snapshot());
        RunSummary raw_step_summary =
            step_metrics->finalize(context.backend->name(), context.mode->name(), context.scenario.scenario_name, step_duration);
        StepSummary step_summary =
            make_step_summary(raw_step_summary, step, index, cpu_metrics, step_run_status, step_error);
        step_results.push_back(step_summary);

        if (is_measure_step(step)) {
            measure_duration_seconds += step_duration;
            if (!aggregate_metrics->absorb(*step_metrics)) {
                throw std::runtime_error("failed to merge metrics collectors");
            }
            if (cpu_metrics.available) {
                cpu_available = true;
                cpu_status = "available";
                weighted_cpu_duration += step_duration;
                weighted_cpu_user += cpu_metrics.user_pct * static_cast<double>(step_duration);
                weighted_cpu_sys += cpu_metrics.sys_pct * static_cast<double>(step_duration);
                weighted_cpu_peak += cpu_metrics.peak_pct * static_cast<double>(step_duration);
            } else if (!cpu_available) {
                cpu_status = cpu_metrics.status;
            }
        }

        if (step_run_status != "success") {
            run_status = "error";
            run_error = step_error;
            break;
        }
    }

    RunSummary summary = aggregate_metrics->finalize(context.backend->name(),
                                                     context.mode->name(),
                                                     context.scenario.scenario_name,
                                                     std::max<std::uint32_t>(1U, measure_duration_seconds));
    summary.run_status = run_status;
    summary.error_message = run_error;
    summary.backend_available = true;
    summary.backend_status = "available";
    summary.backend_reason.clear();
    summary.total_step_count = static_cast<std::uint32_t>(steps.size());
    summary.measure_step_count = static_cast<std::uint32_t>(
        std::count_if(steps.begin(), steps.end(), [](const ScenarioStep& step) { return is_measure_step(step); }));
    for (const ScenarioStep& step : steps) {
        summary.scenario_duration_seconds += step.duration_seconds;
    }
    for (const ScenarioStep& step : steps) {
        if (is_measure_step(step)) {
            summary.packet_size_profile = step.packet_size_profile;
            summary.target_rate_gbps = step.target_rate_gbps;
            summary.burst_multiplier = step.burst_multiplier;
            summary.face_count = step.face_count;
            summary.packet_size_bytes = step.packet_size_bytes;
            summary.burst_window_ms = step.burst_window_ms;
            break;
        }
    }
    summary.steps = std::move(step_results);
    if (weighted_cpu_duration > 0U) {
        CpuMetrics aggregate_cpu;
        aggregate_cpu.available = true;
        aggregate_cpu.status = "available";
        aggregate_cpu.user_pct = weighted_cpu_user / static_cast<double>(weighted_cpu_duration);
        aggregate_cpu.sys_pct = weighted_cpu_sys / static_cast<double>(weighted_cpu_duration);
        aggregate_cpu.peak_pct = weighted_cpu_peak / static_cast<double>(weighted_cpu_duration);
        apply_cpu_metrics(summary, aggregate_cpu);
    } else {
        CpuMetrics aggregate_cpu;
        aggregate_cpu.available = false;
        aggregate_cpu.status = cpu_status;
        apply_cpu_metrics(summary, aggregate_cpu);
    }

    const BackendStats backend_stats = context.backend->stats();
    merge_backend_stats(summary, backend_stats);

    write_results(summary, effective_config);
    context.backend->shutdown();
    return summary;
}

}  // namespace rxtech
