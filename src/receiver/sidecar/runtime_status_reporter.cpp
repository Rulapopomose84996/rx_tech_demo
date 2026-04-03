#include "internal/runtime_status_reporter.h"

#include <algorithm>
#include <sstream>

#ifdef __linux__
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "data_order_tracker.h"
#include "owner_loop_summary.h"

namespace rxtech
{

    RuntimeStatusReporter::RuntimeStatusReporter(const RxConfig &config,
                                                 const ProtocolSpec &spec,
                                                 std::ostream *status_output,
                                                 const std::chrono::steady_clock::time_point &start_time)
        : config_(config),
          spec_(spec),
          start_time_(start_time),
          status_interval_(config.status_interval_seconds == 0U ? std::chrono::seconds{0} : std::chrono::seconds{std::max<std::uint32_t>(1U, config.status_interval_seconds)}),
          feedback_interval_(std::max<std::uint32_t>(1U, config.feedback_interval_seconds)),
          next_status_at_(config.status_interval_seconds == 0U ? std::chrono::steady_clock::time_point::max() : start_time + std::chrono::seconds{std::max<std::uint32_t>(1U, config.status_interval_seconds)}),
          next_feedback_at_(start_time + feedback_interval_),
          status_panel_(status_output)
    {
    }

    RunSummary RuntimeStatusReporter::build_summary(ReceiveContext &context,
                                                    CaptureArtifacts &artifacts,
                                                    const OwnerLoopRuntimeState &runtime_state,
                                                    const DataOrderTracker &data_order_tracker,
                                                    std::uint32_t elapsed_seconds) const
    {
        RunSummary summary = context.metrics->finalize(context.backend->name(), "light_parse", "sample_replay", elapsed_seconds);
        summary.backend_available = true;
        summary.backend_status = "available";
        summary.captured_packets = artifacts.captured_packets;
        summary.captured_bytes = artifacts.captured_bytes;
        summary.recorded_packets = artifacts.recorded_packets;
        summary.recorded_bytes = artifacts.recorded_bytes;
        summary.packet_count = artifacts.recorded_packets;
        runtime_state.populate_common_summary(summary);
        data_order_tracker.populate_summary(summary);
        populate_active_prt_summary(summary,
                                    spec_,
                                    runtime_state.latest_data_seen,
                                    runtime_state.latest_data_cpi,
                                    runtime_state.latest_data_prt,
                                    runtime_state.prt_coverage);
        merge_backend_stats(summary, context.backend->stats());
        apply_raw_record_stats(summary, artifacts.raw_frame_recorder);
        return summary;
    }

    void RuntimeStatusReporter::emit_periodic(ReceiveContext &context,
                                              CaptureArtifacts &artifacts,
                                              const OwnerLoopRuntimeState &runtime_state,
                                              const DataOrderTracker &data_order_tracker,
                                              const std::chrono::steady_clock::time_point &now)
    {
        if (!config_.run_until_stopped)
        {
            return;
        }
        if ((diagnostic_output() == nullptr || now < next_status_at_) &&
            (!config_.feedback_enabled || now < next_feedback_at_))
        {
            return;
        }

        RunSummary summary = build_summary(context,
                                           artifacts,
                                           runtime_state,
                                           data_order_tracker,
                                           std::max<std::uint32_t>(1U,
                                                                   static_cast<std::uint32_t>(
                                                                       std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count())));
        if (diagnostic_output() != nullptr && now >= next_status_at_)
        {
            status_panel_.render(summary, now - start_time_);
            next_status_at_ = now + status_interval_;
        }
        if (config_.feedback_enabled && now >= next_feedback_at_)
        {
            send_feedback_snapshot(summary);
            next_feedback_at_ = now + feedback_interval_;
        }
    }

    RunSummary RuntimeStatusReporter::build_final_summary(ReceiveContext &context,
                                                          CaptureArtifacts &artifacts,
                                                          const OwnerLoopRuntimeState &runtime_state,
                                                          const DataOrderTracker &data_order_tracker,
                                                          const std::chrono::steady_clock::time_point &end_time) const
    {
        RunSummary summary = build_summary(context,
                                           artifacts,
                                           runtime_state,
                                           data_order_tracker,
                                           std::max<std::uint32_t>(1U,
                                                                   static_cast<std::uint32_t>(
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
                ++summary.complete_prt_count;
            }
        }
        runtime_state.populate_protocol_summaries(summary);
        summary.human_summary = build_run_human_summary(summary);
        return summary;
    }

    void RuntimeStatusReporter::render_final(const RunSummary &summary,
                                             const std::chrono::steady_clock::duration &elapsed)
    {
        if (config_.run_until_stopped && diagnostic_output() != nullptr)
        {
            status_panel_.render(summary, elapsed);
        }
        send_feedback_snapshot(summary);
    }

    std::ostream *RuntimeStatusReporter::diagnostic_output() const
    {
        return status_panel_.diagnostic_output();
    }

    void RuntimeStatusReporter::send_feedback_snapshot(const RunSummary &summary) const
    {
#ifdef __linux__
        if (!config_.feedback_enabled || config_.feedback_host.empty() || config_.feedback_port == 0U)
        {
            return;
        }

        const int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0)
        {
            if (diagnostic_output() != nullptr)
            {
                *diagnostic_output() << "[feedback] socket_error\n";
                diagnostic_output()->flush();
            }
            return;
        }

        if (!config_.feedback_bind_host.empty())
        {
            sockaddr_in bind_addr{};
            bind_addr.sin_family = AF_INET;
            bind_addr.sin_port = 0;
            if (inet_pton(AF_INET, config_.feedback_bind_host.c_str(), &bind_addr.sin_addr) == 1)
            {
                if (bind(fd, reinterpret_cast<const sockaddr *>(&bind_addr), sizeof(bind_addr)) != 0 && diagnostic_output() != nullptr)
                {
                    *diagnostic_output() << "[feedback] bind_error source=" << config_.feedback_bind_host << "\n";
                    diagnostic_output()->flush();
                }
            }
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<std::uint16_t>(config_.feedback_port));
        if (inet_pton(AF_INET, config_.feedback_host.c_str(), &addr.sin_addr) != 1)
        {
            if (diagnostic_output() != nullptr)
            {
                *diagnostic_output() << "[feedback] invalid_target=" << config_.feedback_host << ':' << config_.feedback_port << "\n";
                diagnostic_output()->flush();
            }
            close(fd);
            return;
        }

        std::ostringstream payload;
        payload << "{\"type\":\"receiver_feedback\""
                << ",\"rx_packets\":" << summary.rx_packets
                << ",\"rx_bytes\":" << summary.rx_bytes
                << ",\"parsed_packets\":" << summary.parsed_packets
                << ",\"control_table_packets\":" << summary.control_table_packets
                << ",\"data_packets\":" << summary.data_packets
                << ",\"loss_rate\":" << calculate_drop_rate(summary)
                << ",\"queue_id\":" << summary.queue_id
                << ",\"gbps\":" << summary.actual_rx_gbps
                << '}';

        const std::string message = payload.str();
        const ssize_t sent = sendto(fd, message.data(), message.size(), 0, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
        if (sent < 0 && diagnostic_output() != nullptr)
        {
            *diagnostic_output() << "[feedback] send_error target=" << config_.feedback_host << ':' << config_.feedback_port << "\n";
            diagnostic_output()->flush();
        }
        close(fd);
#else
        (void)summary;
#endif
    }

} // namespace rxtech
