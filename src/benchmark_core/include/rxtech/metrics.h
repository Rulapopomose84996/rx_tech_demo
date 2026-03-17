#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rxtech {

struct StepSummary {
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

struct RunSummary {
    std::string backend;
    std::string mode;
    std::string scenario;
    std::string packet_size_profile;
    std::string xdp_attach_mode;
    std::string xsk_mode;
    std::string run_status = "success";
    std::string error_message;
    std::string backend_status = "available";
    std::string backend_reason;
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
    std::uint32_t queue_id = 0;
    std::uint32_t xdp_prog_id = 0;
    std::uint32_t xsk_bind_flags = 0;
    std::uint64_t umem_size = 0;
    std::uint32_t frame_size = 0;
    std::uint32_t fill_ring_size = 0;
    std::uint32_t completion_ring_size = 0;
    std::uint32_t total_step_count = 0;
    std::uint32_t measure_step_count = 0;
    std::uint32_t scenario_duration_seconds = 0;
    std::uint32_t face_count = 1;
    std::uint32_t packet_size_bytes = 0;
    std::uint32_t burst_window_ms = 0;
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
    bool backend_available = true;
    bool cpu_metrics_available = false;
    std::string cpu_metrics_status = "unavailable";
    std::vector<StepSummary> steps;
};

class IMetricsCollector {
public:
    virtual ~IMetricsCollector() = default;

    virtual void on_burst(std::size_t burst_size, std::uint64_t bytes) = 0;
    virtual void on_parsed_packet() = 0;
    virtual void on_drop() = 0;
    virtual void on_error() = 0;
    virtual void on_pool_exhaustion() = 0;
    virtual void on_packet_latency_ns(std::uint64_t latency_ns) = 0;
    virtual void on_ring_depth(std::size_t depth) = 0;
    virtual std::unique_ptr<IMetricsCollector> clone_empty() const = 0;
    virtual bool absorb(const IMetricsCollector& other) = 0;
    virtual RunSummary finalize(const std::string& backend,
                                const std::string& mode,
                                const std::string& scenario,
                                std::uint32_t duration_seconds) = 0;
};

class MetricsCollector final : public IMetricsCollector {
public:
    void on_burst(std::size_t burst_size, std::uint64_t bytes) override;
    void on_parsed_packet() override;
    void on_drop() override;
    void on_error() override;
    void on_pool_exhaustion() override;
    void on_packet_latency_ns(std::uint64_t latency_ns) override;
    void on_ring_depth(std::size_t depth) override;
    std::unique_ptr<IMetricsCollector> clone_empty() const override;
    bool absorb(const IMetricsCollector& other) override;
    RunSummary finalize(const std::string& backend,
                        const std::string& mode,
                        const std::string& scenario,
                        std::uint32_t duration_seconds) override;

private:
    std::uint64_t rx_packets_ = 0;
    std::uint64_t rx_bytes_ = 0;
    std::uint64_t parsed_packets_ = 0;
    std::uint64_t dropped_packets_ = 0;
    std::uint64_t backend_errors_ = 0;
    std::uint64_t pool_exhaustion_count_ = 0;
    std::uint64_t burst_count_ = 0;
    std::uint64_t burst_sum_ = 0;
    std::uint64_t burst_max_ = 0;
    std::uint64_t ring_high_watermark_ = 0;
    std::vector<std::size_t> bursts_;
    std::vector<std::uint64_t> latencies_ns_;
};

}  // namespace rxtech
