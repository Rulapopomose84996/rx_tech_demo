#include "internal/runtime_status_reporter.h"

#include <algorithm>
#include "data_order_tracker.h"
#include "owner_loop_summary.h"

namespace rxtech
{

    RuntimeStatusReporter::RuntimeStatusReporter(const RxConfig &config, const ProtocolSpec &spec,
                                                 std::ostream *status_output,
                                                 const std::chrono::steady_clock::time_point &start_time)
        : config_(config), spec_(spec), start_time_(start_time),
          status_interval_(
              config.operations.status_interval_seconds == 0U
                  ? std::chrono::seconds{0}
                  : std::chrono::seconds{std::max<std::uint32_t>(1U, config.operations.status_interval_seconds)}),
          next_status_at_(config.operations.status_interval_seconds == 0U
                              ? std::chrono::steady_clock::time_point::max()
                              : start_time + std::chrono::seconds{std::max<std::uint32_t>(
                                                 1U, config.operations.status_interval_seconds)}),
          status_panel_(status_output), metrics_exporter_(config, start_time)
    {
    }

    RunSummary RuntimeStatusReporter::build_summary(ReceiveContext &context, CaptureArtifacts &artifacts,
                                                    const OwnerLoopRuntimeState &runtime_state,
                                                    const DataOrderTracker &data_order_tracker,
                                                    std::uint32_t elapsed_seconds) const
    {
        RunSummary summary =
            context.metrics->finalize(context.backend->name(), "light_parse", "sample_replay", elapsed_seconds);
        summary.backend.available = true;
        summary.backend.status = "available";
        summary.capture.captured_packets = artifacts.captured_packets;
        summary.capture.captured_bytes = artifacts.captured_bytes;
        summary.capture.recorded_packets = artifacts.recorded_packets;
        summary.capture.recorded_bytes = artifacts.recorded_bytes;
        summary.capture.packet_count = artifacts.recorded_packets;
        runtime_state.populate_common_summary(summary);
        data_order_tracker.populate_summary(summary);
        populate_active_prt_summary(summary, spec_, runtime_state.latest_data_seen, runtime_state.latest_data_cpi,
                                    runtime_state.latest_data_prt, runtime_state.prt_coverage);
        merge_backend_stats(summary, context.backend->stats());
        apply_raw_record_stats(summary, artifacts.raw_frame_recorder);
        metrics_exporter_.populate_summary(summary);
        return summary;
    }

    void RuntimeStatusReporter::emit_periodic(ReceiveContext &context, CaptureArtifacts &artifacts,
                                              const OwnerLoopRuntimeState &runtime_state,
                                              const DataOrderTracker &data_order_tracker,
                                              const std::chrono::steady_clock::time_point &now)
    {
        if (!config_.runtime.run_until_stopped)
        {
            return;
        }
        if (diagnostic_output() == nullptr || now < next_status_at_)
        {
            return;
        }

        RunSummary summary =
            build_summary(context, artifacts, runtime_state, data_order_tracker,
                          std::max<std::uint32_t>(
                              1U, static_cast<std::uint32_t>(
                                      std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count())));
        if (diagnostic_output() != nullptr && now >= next_status_at_)
        {
            status_panel_.render(summary, now - start_time_);
            next_status_at_ = now + status_interval_;
        }
        metrics_exporter_.maybe_export(summary, now);
    }

    RunSummary RuntimeStatusReporter::build_final_summary(ReceiveContext &context, CaptureArtifacts &artifacts,
                                                          const OwnerLoopRuntimeState &runtime_state,
                                                          const DataOrderTracker &data_order_tracker,
                                                          const std::chrono::steady_clock::time_point &end_time) const
    {
        RunSummary summary = build_summary(
            context, artifacts, runtime_state, data_order_tracker,
            std::max<std::uint32_t>(
                1U, static_cast<std::uint32_t>(
                        std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time_).count())));
        for (const auto &entry : runtime_state.prt_coverage)
        {
            bool complete = true;
            for (std::uint16_t channel = 0; channel < spec_.channels_per_prt; ++channel)
            {
                const auto channel_it = entry.second.find(channel);
                if (channel_it == entry.second.end())
                {
                    complete = false;
                    break;
                }
                for (std::uint16_t packet_index = 1U; packet_index <= spec_.packets_per_channel; ++packet_index)
                {
                    if (channel_it->second.count(packet_index) == 0U)
                    {
                        complete = false;
                        break;
                    }
                }
                if (!complete)
                {
                    break;
                }
            }
            if (complete)
            {
                ++summary.protocol.complete_prt_count;
            }
        }
        runtime_state.populate_protocol_summaries(summary);
        metrics_exporter_.populate_summary(summary);
        metrics_exporter_.export_final(summary);
        metrics_exporter_.populate_summary(summary);
        summary.run.human_summary = build_run_human_summary(summary);
        return summary;
    }

    void RuntimeStatusReporter::render_final(const RunSummary &summary,
                                             const std::chrono::steady_clock::duration &elapsed)
    {
        if (config_.runtime.run_until_stopped && diagnostic_output() != nullptr)
        {
            status_panel_.render(summary, elapsed);
        }
    }

    std::ostream *RuntimeStatusReporter::diagnostic_output() const
    {
        return status_panel_.diagnostic_output();
    }

} // namespace rxtech
