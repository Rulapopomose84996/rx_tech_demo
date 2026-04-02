#include "rxtech/metrics.h"

#include <algorithm>

namespace rxtech {

MetricsCollector::PerPortMetrics& MetricsCollector::get_or_create_port_metrics(std::uint32_t port_id) {
    for (auto& entry : per_port_metrics_) {
        if (entry.first == port_id) {
            return entry.second;
        }
    }
    per_port_metrics_.emplace_back(port_id, PerPortMetrics{});
    return per_port_metrics_.back().second;
}

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

void MetricsCollector::on_control_table_packet() {
    ++control_table_packets_;
}

void MetricsCollector::on_data_packet() {
    ++data_packets_;
}

void MetricsCollector::on_packet_latency_ns(std::uint64_t latency_ns) {
    latencies_ns_.push_back(latency_ns);
}

void MetricsCollector::on_ring_depth(std::size_t depth) {
    ring_high_watermark_ = std::max<std::uint64_t>(ring_high_watermark_, depth);
}

void MetricsCollector::on_port_packet(std::uint32_t port_id, std::uint64_t bytes) {
    PerPortMetrics& metrics = get_or_create_port_metrics(port_id);
    ++metrics.rx_packets;
    metrics.rx_bytes += bytes;
}

void MetricsCollector::on_reassembled_block(std::uint32_t port_id, std::uint64_t) {
    ++get_or_create_port_metrics(port_id).reassembled_blocks;
}

void MetricsCollector::on_missing_fragments(std::uint32_t port_id, std::uint64_t count) {
    get_or_create_port_metrics(port_id).missing_fragments += count;
}

void MetricsCollector::on_duplicate_fragment(std::uint32_t port_id) {
    ++get_or_create_port_metrics(port_id).duplicate_fragments;
}

void MetricsCollector::on_invalid_header(std::uint32_t port_id) {
    ++get_or_create_port_metrics(port_id).invalid_header_count;
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
    control_table_packets_ += other_metrics->control_table_packets_;
    data_packets_ += other_metrics->data_packets_;
    burst_count_ += other_metrics->burst_count_;
    burst_sum_ += other_metrics->burst_sum_;
    burst_max_ = std::max(burst_max_, other_metrics->burst_max_);
    ring_high_watermark_ = std::max(ring_high_watermark_, other_metrics->ring_high_watermark_);
    bursts_.insert(bursts_.end(), other_metrics->bursts_.begin(), other_metrics->bursts_.end());
    latencies_ns_.insert(latencies_ns_.end(), other_metrics->latencies_ns_.begin(), other_metrics->latencies_ns_.end());
    for (const auto& entry : other_metrics->per_port_metrics_) {
        const std::uint32_t port_id = entry.first;
        const PerPortMetrics& per_port = entry.second;
        PerPortMetrics& current = get_or_create_port_metrics(port_id);
        current.rx_packets += per_port.rx_packets;
        current.rx_bytes += per_port.rx_bytes;
        current.reassembled_blocks += per_port.reassembled_blocks;
        current.missing_fragments += per_port.missing_fragments;
        current.duplicate_fragments += per_port.duplicate_fragments;
        current.invalid_header_count += per_port.invalid_header_count;
    }
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
    summary.control_table_packets = control_table_packets_;
    summary.data_packets = data_packets_;
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

    summary.per_port.reserve(per_port_metrics_.size());
    for (const auto& entry : per_port_metrics_) {
        const std::uint32_t port_id = entry.first;
        const PerPortMetrics& metrics = entry.second;
        PerPortSummary per_port;
        per_port.port_id = port_id;
        per_port.rx_packets = metrics.rx_packets;
        per_port.rx_bytes = metrics.rx_bytes;
        per_port.reassembled_blocks = metrics.reassembled_blocks;
        per_port.missing_fragments = metrics.missing_fragments;
        per_port.duplicate_fragments = metrics.duplicate_fragments;
        per_port.invalid_header_count = metrics.invalid_header_count;
        if (duration_seconds > 0U) {
            per_port.throughput_gbps =
                (static_cast<double>(metrics.rx_bytes) * 8.0) / static_cast<double>(duration_seconds) / 1'000'000'000.0;
        }
        summary.per_port.push_back(per_port);
    }
    std::sort(summary.per_port.begin(),
              summary.per_port.end(),
              [](const PerPortSummary& lhs, const PerPortSummary& rhs) { return lhs.port_id < rhs.port_id; });

    return summary;
}

}  // namespace rxtech
