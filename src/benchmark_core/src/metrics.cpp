#include "rxtech/metrics.h"

#include <algorithm>

namespace rxtech {

void MetricsCollector::on_burst(std::size_t burst_size, std::uint64_t bytes) {
    rx_packets_ += burst_size;
    rx_bytes_ += bytes;
    ++burst_count_;
    burst_sum_ += burst_size;
    burst_max_ = std::max<std::uint64_t>(burst_max_, burst_size);
    bursts_.push_back(burst_size);
}

void MetricsCollector::on_parsed_packet() {
    ++parsed_packets_;
}

void MetricsCollector::on_drop() {
    ++dropped_packets_;
}

void MetricsCollector::on_error() {
    ++backend_errors_;
}

void MetricsCollector::on_pool_exhaustion() {
    ++pool_exhaustion_count_;
}

void MetricsCollector::on_packet_latency_ns(std::uint64_t latency_ns) {
    latencies_ns_.push_back(latency_ns);
}

void MetricsCollector::on_ring_depth(std::size_t depth) {
    ring_high_watermark_ = std::max<std::uint64_t>(ring_high_watermark_, depth);
}

std::unique_ptr<IMetricsCollector> MetricsCollector::clone_empty() const {
    return std::make_unique<MetricsCollector>();
}

bool MetricsCollector::absorb(const IMetricsCollector& other) {
    const auto* other_metrics = dynamic_cast<const MetricsCollector*>(&other);
    if (other_metrics == nullptr) {
        return false;
    }

    rx_packets_ += other_metrics->rx_packets_;
    rx_bytes_ += other_metrics->rx_bytes_;
    parsed_packets_ += other_metrics->parsed_packets_;
    dropped_packets_ += other_metrics->dropped_packets_;
    backend_errors_ += other_metrics->backend_errors_;
    pool_exhaustion_count_ += other_metrics->pool_exhaustion_count_;
    burst_count_ += other_metrics->burst_count_;
    burst_sum_ += other_metrics->burst_sum_;
    burst_max_ = std::max(burst_max_, other_metrics->burst_max_);
    ring_high_watermark_ = std::max(ring_high_watermark_, other_metrics->ring_high_watermark_);
    bursts_.insert(bursts_.end(), other_metrics->bursts_.begin(), other_metrics->bursts_.end());
    latencies_ns_.insert(latencies_ns_.end(), other_metrics->latencies_ns_.begin(), other_metrics->latencies_ns_.end());
    return true;
}

RunSummary MetricsCollector::finalize(const std::string& backend,
                                      const std::string& mode,
                                      const std::string& scenario,
                                      std::uint32_t duration_seconds) {
    RunSummary summary;
    summary.backend = backend;
    summary.mode = mode;
    summary.scenario = scenario;
    summary.rx_packets = rx_packets_;
    summary.rx_bytes = rx_bytes_;
    summary.parsed_packets = parsed_packets_;
    summary.dropped_packets = dropped_packets_;
    summary.backend_errors = backend_errors_;
    summary.pool_exhaustion_count = pool_exhaustion_count_;
    summary.ring_high_watermark = ring_high_watermark_;

    if (duration_seconds > 0U) {
        const double seconds = static_cast<double>(duration_seconds);
        summary.actual_rx_gbps = (static_cast<double>(rx_bytes_) * 8.0) / seconds / 1'000'000'000.0;
        summary.actual_rx_mpps = static_cast<double>(rx_packets_) / seconds / 1'000'000.0;
    }

    if (burst_count_ > 0U) {
        summary.batch_avg = static_cast<double>(burst_sum_) / static_cast<double>(burst_count_);
    }

    if (!bursts_.empty()) {
        std::sort(bursts_.begin(), bursts_.end());
        const std::size_t index = static_cast<std::size_t>((bursts_.size() - 1U) * 0.99);
        summary.batch_p99 = static_cast<std::uint64_t>(bursts_[index]);
    }

    if (!latencies_ns_.empty()) {
        std::sort(latencies_ns_.begin(), latencies_ns_.end());
        const std::size_t p50_index = (latencies_ns_.size() - 1U) / 2U;
        const std::size_t p99_index = static_cast<std::size_t>((latencies_ns_.size() - 1U) * 0.99);
        summary.latency_p50_us = static_cast<double>(latencies_ns_[p50_index]) / 1000.0;
        summary.latency_p99_us = static_cast<double>(latencies_ns_[p99_index]) / 1000.0;
    }

    return summary;
}

}  // namespace rxtech
