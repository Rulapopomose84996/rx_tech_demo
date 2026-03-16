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

void MetricsCollector::on_ring_depth(std::size_t depth) {
    ring_high_watermark_ = std::max<std::uint64_t>(ring_high_watermark_, depth);
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

    return summary;
}

}  // namespace rxtech
