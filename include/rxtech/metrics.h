#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <string>
#include <vector>

#include "rxtech/sample_packet_parser.h"

namespace rxtech
{

    struct ProtocolChannelSummary
    {
        std::uint16_t channel = 0;
        std::uint64_t data_packets = 0;
        std::uint64_t iq_count = 0;
    };

    struct ProtocolCpiSummary
    {
        std::uint64_t cpi = 0;
        std::uint64_t data_packets = 0;
        std::uint64_t prt_count = 0;
    };

    struct ProtocolPrtChannelCoverageSummary
    {
        std::uint16_t channel = 0;
        std::uint64_t packet_count = 0;
        bool complete = false;
    };

    struct StepSummary
    {
        std::size_t step_index = 0;
        std::string step_name;
        std::string phase = "measure";
        std::string traffic_profile;
        std::string packet_size_profile;
        std::string run_status = "success";
        std::string error_message;
        double target_rate_gbps = 0.0;
        double burst_multiplier = 1.0;
        std::uint32_t duration_seconds = 0;
        std::uint32_t face_count = 1;
        std::uint32_t packet_size_bytes = 0;
        std::uint32_t burst_window_ms = 0;
        std::uint64_t rx_packets = 0;
        std::uint64_t rx_bytes = 0;
        std::uint64_t parsed_packets = 0;
        std::uint64_t dropped_packets = 0;
        std::uint64_t backend_errors = 0;
        std::uint64_t nic_drops = 0;
        std::uint64_t pool_exhaustion_count = 0;
        std::uint64_t ring_high_watermark = 0;
        std::uint64_t rx_polls = 0;
        std::uint64_t empty_polls = 0;
        double actual_rx_gbps = 0.0;
        double actual_rx_mpps = 0.0;
        double cpu_user_pct = 0.0;
        double cpu_sys_pct = 0.0;
        double cpu_peak_pct = 0.0;
        double latency_p50_us = 0.0;
        double latency_p99_us = 0.0;
        double batch_avg = 0.0;
        double empty_poll_ratio = 0.0;
        std::uint64_t batch_p99 = 0;
        bool cpu_metrics_available = false;
        std::string cpu_metrics_status = "unavailable";
    };

    struct RunInfoSummary
    {
        std::string backend_name;
        std::string mode;
        std::string scenario_name;
        std::string packet_size_profile;
        std::string status = "success";
        std::string error_message;
        std::string human_summary;
        std::string structured_log_backend = "disabled";
    };

    struct BackendSummary
    {
        bool available = true;
        std::string status = "available";
        std::string reason;
        std::uint64_t raw_rx_packets = 0;
        std::uint64_t raw_rx_bytes = 0;
        std::uint64_t filtered_packets = 0;
        std::uint64_t arp_request_packets = 0;
        std::uint64_t arp_reply_packets = 0;
        std::uint64_t dropped_packets = 0;
        std::uint64_t errors = 0;
        std::uint64_t nic_drops = 0;
        std::uint64_t receive_batches = 0;
        std::uint64_t kernel_drops = 0;
        std::uint32_t queue_id = 0;
        std::uint32_t frame_size = 0;
        std::uint32_t max_burst_size = 0;
    };

    struct CaptureSummary
    {
        std::string packets_path;
        std::string index_path;
        std::string raw_record_output_dir;
        std::string raw_record_latest_file_path;
        std::string run_artifact_dir;
        std::uint64_t captured_packets = 0;
        std::uint64_t captured_bytes = 0;
        std::uint64_t recorded_packets = 0;
        std::uint64_t recorded_bytes = 0;
        std::uint64_t packet_count = 0;
        std::uint64_t raw_record_written_frames = 0;
        std::uint64_t raw_record_written_bytes = 0;
        std::uint64_t raw_record_dropped_frames = 0;
        std::uint64_t raw_record_dropped_bytes = 0;
        std::uint64_t raw_record_retained_bytes = 0;
        std::uint64_t raw_record_queue_high_watermark = 0;
    };

    struct ProtocolSummary
    {
        std::uint64_t rx_packets = 0;
        std::uint64_t rx_bytes = 0;
        std::uint64_t parsed_packets = 0;
        std::uint64_t control_table_packets = 0;
        std::uint64_t data_packets = 0;
        std::uint64_t dropped_packets = 0;
        std::uint64_t cpi_count = 0;
        std::uint64_t prt_count = 0;
        std::uint64_t channel_count = 0;
        std::uint64_t complete_prt_count = 0;
        std::uint64_t final_tail_packets = 0;
        std::vector<ProtocolChannelSummary> channels;
        std::vector<ProtocolCpiSummary> cpis;
    };

    struct DataOrderSummary
    {
        std::string assessment = "未评估";
        std::string first_mismatch;
        std::uint64_t checked_packets = 0;
    };

    struct GlobalPacketSequenceSummary
    {
        bool available = false;
        std::string status = "unavailable";
        std::uint64_t checked_packets = 0;
        std::uint64_t gap_count = 0;
        std::uint64_t missing_packets = 0;
        std::string first_gap;
    };

    struct MetricsExportSummary
    {
        bool enabled = false;
        std::string mode = "none";
        std::string target_path;
        std::string status = "disabled";
        std::uint64_t success_count = 0;
        std::uint64_t error_count = 0;
    };

    struct ActivePrtSummary
    {
        bool available = false;
        bool complete = false;
        std::uint64_t cpi = 0;
        std::uint64_t prt = 0;
        std::uint64_t channel_count = 0;
        std::uint32_t packets_per_channel = 0;
        std::vector<ProtocolPrtChannelCoverageSummary> channels;
    };

    struct PerformanceSummary
    {
        std::uint64_t pool_exhaustion_count = 0;
        std::uint64_t output_backpressure_count = 0;
        std::uint64_t late_packet_accepted_count = 0;
        std::uint64_t late_packet_rejected_count = 0;
        std::uint64_t ring_high_watermark = 0;
        std::uint64_t rx_polls = 0;
        std::uint64_t empty_polls = 0;
        double target_rate_gbps = 0.0;
        double burst_multiplier = 1.0;
        double actual_rx_gbps = 0.0;
        double actual_rx_mpps = 0.0;
        double cpu_user_pct = 0.0;
        double cpu_sys_pct = 0.0;
        double cpu_peak_pct = 0.0;
        double latency_p50_us = 0.0;
        double latency_p99_us = 0.0;
        double batch_avg = 0.0;
        double empty_poll_ratio = 0.0;
        std::uint64_t batch_p99 = 0;
        bool cpu_metrics_available = false;
        std::string cpu_metrics_status = "unavailable";
    };

    struct ScenarioSummary
    {
        std::uint32_t total_step_count = 0;
        std::uint32_t measure_step_count = 0;
        std::uint32_t duration_seconds = 0;
        std::uint32_t face_count = 1;
        std::uint32_t packet_size_bytes = 0;
        std::uint32_t burst_window_ms = 0;
    };

    struct RunSummary
    {
        RunInfoSummary run;
        BackendSummary backend;
        CaptureSummary capture;
        ProtocolSummary protocol;
        DataOrderSummary data_order;
        GlobalPacketSequenceSummary global_packet_sequence;
        ActivePrtSummary active_prt;
        PerformanceSummary performance;
        MetricsExportSummary metrics_export;
        ScenarioSummary scenario;
        std::vector<StepSummary> steps;
        std::array<std::uint64_t, kRejectReasonCount> reject_by_reason{};
    };

    class IMetricsCollector
    {
      public:
        virtual ~IMetricsCollector() = default;

        virtual void on_burst(std::size_t burst_size, std::uint64_t bytes) = 0;
        virtual void on_valid_packet(PacketKind kind) = 0;
        virtual void on_reject(RejectReason reason) = 0;
        virtual void on_drop() = 0;
        virtual void on_error() = 0;
        virtual void on_pool_exhaustion() = 0;
        virtual void on_output_backpressure() = 0;
        virtual void on_late_packet_accepted() = 0;
        virtual void on_late_packet_rejected() = 0;
        virtual void on_packet_latency_ns(std::uint64_t latency_ns) = 0;
        virtual void on_global_packet_sequence(std::uint16_t sequence) = 0;
        virtual void on_ring_depth(std::size_t depth) = 0;
        virtual std::unique_ptr<IMetricsCollector> clone_empty() const = 0;
        virtual bool absorb(const IMetricsCollector &other) = 0;
        virtual RunSummary finalize(const std::string &backend, const std::string &mode, const std::string &scenario,
                                    std::uint32_t duration_seconds) = 0;
    };

    class MetricsCollector final : public IMetricsCollector
    {
      public:
        MetricsCollector();

        void on_burst(std::size_t burst_size, std::uint64_t bytes) override;
        void on_valid_packet(PacketKind kind) override;
        void on_reject(RejectReason reason) override;
        void on_drop() override;
        void on_error() override;
        void on_pool_exhaustion() override;
        void on_output_backpressure() override;
        void on_late_packet_accepted() override;
        void on_late_packet_rejected() override;
        void on_packet_latency_ns(std::uint64_t latency_ns) override;
        void on_global_packet_sequence(std::uint16_t sequence) override;
        void on_ring_depth(std::size_t depth) override;
        std::unique_ptr<IMetricsCollector> clone_empty() const override;
        bool absorb(const IMetricsCollector &other) override;
        RunSummary finalize(const std::string &backend, const std::string &mode, const std::string &scenario,
                            std::uint32_t duration_seconds) override;

      private:
        std::uint64_t rx_packets_ = 0;
        std::uint64_t rx_bytes_ = 0;
        std::uint64_t parsed_packets_ = 0;
        std::uint64_t dropped_packets_ = 0;
        std::uint64_t backend_errors_ = 0;
        std::uint64_t pool_exhaustion_count_ = 0;
        std::uint64_t output_backpressure_count_ = 0;
        std::uint64_t late_packet_accepted_count_ = 0;
        std::uint64_t late_packet_rejected_count_ = 0;
        std::uint64_t control_table_packets_ = 0;
        std::uint64_t data_packets_ = 0;
        std::uint64_t burst_count_ = 0;
        std::uint64_t burst_sum_ = 0;
        std::uint64_t burst_max_ = 0;
        std::uint64_t ring_high_watermark_ = 0;
        std::uint64_t global_sequence_checked_packets_ = 0;
        std::uint64_t global_sequence_gap_count_ = 0;
        std::uint64_t global_sequence_missing_packets_ = 0;
        std::array<std::uint64_t, kRejectReasonCount> reject_counts_{};
        std::vector<std::size_t> bursts_;
        std::vector<std::uint64_t> latencies_ns_;
        std::string first_global_sequence_gap_;
        std::uint16_t last_global_sequence_ = 0;
        bool global_sequence_initialized_ = false;

        std::uint64_t cpu_start_wall_ns_ = 0;
        std::uint64_t cpu_start_user_ticks_ = 0;
        std::uint64_t cpu_start_sys_ticks_ = 0;
        std::uint64_t cpu_last_wall_ns_ = 0;
        std::uint64_t cpu_last_user_ticks_ = 0;
        std::uint64_t cpu_last_sys_ticks_ = 0;
        long cpu_clock_ticks_per_second_ = 0;
        bool cpu_metrics_available_ = false;
        double cpu_peak_pct_ = 0.0;
        std::string cpu_metrics_status_ = "unavailable";

        static constexpr std::size_t kBurstSampleCapacity = 4096U;
        static constexpr std::size_t kLatencySampleCapacity = 4096U;
        std::array<std::size_t, kBurstSampleCapacity> burst_samples_{};
        std::array<std::uint64_t, kLatencySampleCapacity> latency_samples_ns_{};
        std::size_t burst_sample_count_ = 0U;
        std::size_t latency_sample_count_ = 0U;
        std::size_t burst_sample_next_index_ = 0U;
        std::size_t latency_sample_next_index_ = 0U;
    };

} // namespace rxtech
