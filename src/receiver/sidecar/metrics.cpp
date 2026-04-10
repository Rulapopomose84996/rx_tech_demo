#include "rxtech/metrics.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

#include "rxtech/time_utils.h"

namespace rxtech
{

    namespace
    {

        template <typename Sample, std::size_t Capacity>
        void append_bounded_sample(std::array<Sample, Capacity> &storage, std::size_t &count, std::size_t &next_index,
                                   Sample value) noexcept
        {
            storage[next_index] = value;
            next_index = (next_index + 1U) % Capacity;
            if (count < Capacity)
            {
                ++count;
            }
        }

        template <typename Sample, std::size_t Capacity>
        std::vector<Sample> collect_bounded_samples(const std::array<Sample, Capacity> &storage, std::size_t count)
        {
            return std::vector<Sample>(storage.begin(), storage.begin() + count);
        }

        struct ProcessCpuSnapshot
        {
            bool available = false;
            std::uint64_t wall_ns = 0;
            std::uint64_t user_ticks = 0;
            std::uint64_t sys_ticks = 0;
            long clock_ticks_per_second = 0;
            std::string status = "unavailable";
        };

        ProcessCpuSnapshot read_process_cpu_snapshot()
        {
            ProcessCpuSnapshot snapshot;
            snapshot.wall_ns = steady_clock_now_ns();

#if defined(__linux__)
            const long clock_ticks = ::sysconf(_SC_CLK_TCK);
            if (clock_ticks <= 0)
            {
                snapshot.status = "sysconf_clk_tck_failed";
                return snapshot;
            }

            std::ifstream input("/proc/self/stat");
            if (!input.is_open())
            {
                snapshot.status = "proc_stat_open_failed";
                return snapshot;
            }

            std::string line;
            std::getline(input, line);
            if (line.empty())
            {
                snapshot.status = "proc_stat_empty";
                return snapshot;
            }

            const std::size_t close_paren = line.rfind(')');
            if (close_paren == std::string::npos || close_paren + 2U >= line.size())
            {
                snapshot.status = "proc_stat_parse_failed";
                return snapshot;
            }

            std::istringstream rest(line.substr(close_paren + 2U));
            std::vector<std::string> fields;
            std::string field;
            while (rest >> field)
            {
                fields.push_back(field);
            }

            if (fields.size() <= 12U)
            {
                snapshot.status = "proc_stat_fields_missing";
                return snapshot;
            }

            try
            {
                snapshot.user_ticks = static_cast<std::uint64_t>(std::stoull(fields[11]));
                snapshot.sys_ticks = static_cast<std::uint64_t>(std::stoull(fields[12]));
            }
            catch (...)
            {
                snapshot.status = "proc_stat_parse_failed";
                return snapshot;
            }

            snapshot.available = true;
            snapshot.clock_ticks_per_second = clock_ticks;
            snapshot.status = "ok";
            return snapshot;
#else
            snapshot.status = "unsupported_platform";
            return snapshot;
#endif
        }

        double ticks_to_cpu_pct(std::uint64_t ticks, long clock_ticks_per_second, std::uint64_t wall_ns)
        {
            if (clock_ticks_per_second <= 0 || wall_ns == 0U)
            {
                return 0.0;
            }

            const double cpu_seconds = static_cast<double>(ticks) / static_cast<double>(clock_ticks_per_second);
            const double wall_seconds = static_cast<double>(wall_ns) / 1'000'000'000.0;
            if (wall_seconds <= 0.0)
            {
                return 0.0;
            }
            return (cpu_seconds / wall_seconds) * 100.0;
        }

        std::string describe_global_sequence_gap(std::uint16_t previous_sequence, std::uint16_t current_sequence,
                                                 std::uint64_t missing_packets)
        {
            const std::uint16_t expected_sequence = static_cast<std::uint16_t>(previous_sequence + 1U);
            std::ostringstream out;
            out << "前一包序列号=" << previous_sequence << "，期望下一包=" << expected_sequence
                << "，实际收到=" << current_sequence << "，累计缺失=" << missing_packets;
            return out.str();
        }

    } // namespace

    MetricsCollector::MetricsCollector()
    {
        const ProcessCpuSnapshot snapshot = read_process_cpu_snapshot();
        cpu_metrics_available_ = snapshot.available;
        cpu_metrics_status_ = snapshot.status;
        cpu_clock_ticks_per_second_ = snapshot.clock_ticks_per_second;
        if (snapshot.available)
        {
            cpu_start_wall_ns_ = snapshot.wall_ns;
            cpu_start_user_ticks_ = snapshot.user_ticks;
            cpu_start_sys_ticks_ = snapshot.sys_ticks;
            cpu_last_wall_ns_ = snapshot.wall_ns;
            cpu_last_user_ticks_ = snapshot.user_ticks;
            cpu_last_sys_ticks_ = snapshot.sys_ticks;
        }
    }

    void MetricsCollector::on_burst(std::size_t burst_size, std::uint64_t bytes)
    {
        rx_packets_ += burst_size;
        rx_bytes_ += bytes;
        ++burst_count_;
        burst_sum_ += burst_size;
        burst_max_ = std::max<std::uint64_t>(burst_max_, burst_size);
#if defined(RXTECH_DEBUG_DIAGNOSTICS) && RXTECH_DEBUG_DIAGNOSTICS
        append_bounded_sample(burst_samples_, burst_sample_count_, burst_sample_next_index_, burst_size);
#endif
    }

    void MetricsCollector::on_valid_packet(PacketKind kind)
    {
        ++parsed_packets_;
        if (kind == PacketKind::control_table)
        {
            ++control_table_packets_;
        }
        else if (kind == PacketKind::data_packet)
        {
            ++data_packets_;
        }
    }

    void MetricsCollector::on_packet_latency_ns(std::uint64_t latency_ns)
    {
#if defined(RXTECH_DEBUG_DIAGNOSTICS) && RXTECH_DEBUG_DIAGNOSTICS
        append_bounded_sample(latency_samples_ns_, latency_sample_count_, latency_sample_next_index_, latency_ns);
#else
        (void)latency_ns;
#endif
    }

    void MetricsCollector::on_global_packet_sequence(std::uint16_t sequence)
    {
        ++global_sequence_checked_packets_;
        if (!global_sequence_initialized_)
        {
            last_global_sequence_ = sequence;
            global_sequence_initialized_ = true;
            return;
        }

        const std::uint16_t delta = static_cast<std::uint16_t>(sequence - last_global_sequence_);
        if (delta == 0U)
        {
            return;
        }

        if (delta != 1U)
        {
            ++global_sequence_gap_count_;
            global_sequence_missing_packets_ += static_cast<std::uint64_t>(delta - 1U);
            if (first_global_sequence_gap_.empty())
            {
                first_global_sequence_gap_ =
                    describe_global_sequence_gap(last_global_sequence_, sequence, global_sequence_missing_packets_);
            }
        }

        last_global_sequence_ = sequence;
    }

    void MetricsCollector::on_ring_depth(std::size_t depth)
    {
        ring_high_watermark_ = std::max<std::uint64_t>(ring_high_watermark_, depth);
    }

    void MetricsCollector::on_reject(RejectReason reason)
    {
        ++dropped_packets_;
        const auto index = static_cast<std::size_t>(reason);
        if (index < reject_counts_.size())
        {
            ++reject_counts_[index];
        }
    }

    void MetricsCollector::on_drop()
    {
        ++dropped_packets_;
    }

    void MetricsCollector::on_error()
    {
        ++backend_errors_;
    }

    void MetricsCollector::on_pool_exhaustion()
    {
        ++pool_exhaustion_count_;
    }

    void MetricsCollector::on_output_backpressure()
    {
        ++output_backpressure_count_;
    }

    void MetricsCollector::on_late_packet_accepted()
    {
        ++late_packet_accepted_count_;
    }

    void MetricsCollector::on_late_packet_rejected()
    {
        ++late_packet_rejected_count_;
    }

    std::unique_ptr<IMetricsCollector> MetricsCollector::clone_empty() const
    {
        return std::make_unique<MetricsCollector>();
    }

    bool MetricsCollector::absorb(const IMetricsCollector &other)
    {
        const auto *other_metrics = dynamic_cast<const MetricsCollector *>(&other);
        if (other_metrics == nullptr)
        {
            return false;
        }

        rx_packets_ += other_metrics->rx_packets_;
        rx_bytes_ += other_metrics->rx_bytes_;
        parsed_packets_ += other_metrics->parsed_packets_;
        dropped_packets_ += other_metrics->dropped_packets_;
        backend_errors_ += other_metrics->backend_errors_;
        pool_exhaustion_count_ += other_metrics->pool_exhaustion_count_;
        output_backpressure_count_ += other_metrics->output_backpressure_count_;
        late_packet_accepted_count_ += other_metrics->late_packet_accepted_count_;
        late_packet_rejected_count_ += other_metrics->late_packet_rejected_count_;
        control_table_packets_ += other_metrics->control_table_packets_;
        data_packets_ += other_metrics->data_packets_;
        burst_count_ += other_metrics->burst_count_;
        burst_sum_ += other_metrics->burst_sum_;
        burst_max_ = std::max(burst_max_, other_metrics->burst_max_);
        ring_high_watermark_ = std::max(ring_high_watermark_, other_metrics->ring_high_watermark_);
        global_sequence_checked_packets_ += other_metrics->global_sequence_checked_packets_;
        global_sequence_gap_count_ += other_metrics->global_sequence_gap_count_;
        global_sequence_missing_packets_ += other_metrics->global_sequence_missing_packets_;
        if (first_global_sequence_gap_.empty())
        {
            first_global_sequence_gap_ = other_metrics->first_global_sequence_gap_;
        }
        for (std::size_t i = 0; i < reject_counts_.size(); ++i)
        {
            reject_counts_[i] += other_metrics->reject_counts_[i];
        }
        for (std::size_t index = 0; index < other_metrics->burst_sample_count_; ++index)
        {
            append_bounded_sample(burst_samples_, burst_sample_count_, burst_sample_next_index_,
                                  other_metrics->burst_samples_[index]);
        }
        for (std::size_t index = 0; index < other_metrics->latency_sample_count_; ++index)
        {
            append_bounded_sample(latency_samples_ns_, latency_sample_count_, latency_sample_next_index_,
                                  other_metrics->latency_samples_ns_[index]);
        }

        cpu_peak_pct_ = std::max(cpu_peak_pct_, other_metrics->cpu_peak_pct_);
        if (!cpu_metrics_available_ && other_metrics->cpu_metrics_available_)
        {
            cpu_metrics_available_ = true;
            cpu_metrics_status_ = other_metrics->cpu_metrics_status_;
            cpu_clock_ticks_per_second_ = other_metrics->cpu_clock_ticks_per_second_;
            cpu_start_wall_ns_ = other_metrics->cpu_start_wall_ns_;
            cpu_start_user_ticks_ = other_metrics->cpu_start_user_ticks_;
            cpu_start_sys_ticks_ = other_metrics->cpu_start_sys_ticks_;
            cpu_last_wall_ns_ = other_metrics->cpu_last_wall_ns_;
            cpu_last_user_ticks_ = other_metrics->cpu_last_user_ticks_;
            cpu_last_sys_ticks_ = other_metrics->cpu_last_sys_ticks_;
        }

        return true;
    }

    RunSummary MetricsCollector::finalize(const std::string &backend, const std::string &mode,
                                          const std::string &scenario, std::uint32_t duration_seconds)
    {
        RunSummary summary;
        summary.run.backend_name = backend;
        summary.run.mode = mode;
        summary.run.scenario_name = scenario;
        summary.protocol.rx_packets = rx_packets_;
        summary.protocol.rx_bytes = rx_bytes_;
        summary.protocol.parsed_packets = parsed_packets_;
        summary.protocol.dropped_packets = dropped_packets_;
        summary.backend.errors = backend_errors_;
        summary.performance.pool_exhaustion_count = pool_exhaustion_count_;
        summary.performance.output_backpressure_count = output_backpressure_count_;
        summary.performance.late_packet_accepted_count = late_packet_accepted_count_;
        summary.performance.late_packet_rejected_count = late_packet_rejected_count_;
        summary.protocol.control_table_packets = control_table_packets_;
        summary.protocol.data_packets = data_packets_;
        summary.performance.ring_high_watermark = ring_high_watermark_;
        summary.reject_by_reason = reject_counts_;

        summary.global_packet_sequence.available = global_sequence_checked_packets_ > 0U;
        summary.global_packet_sequence.checked_packets = global_sequence_checked_packets_;
        summary.global_packet_sequence.gap_count = global_sequence_gap_count_;
        summary.global_packet_sequence.missing_packets = global_sequence_missing_packets_;
        summary.global_packet_sequence.first_gap = first_global_sequence_gap_;
        if (!summary.global_packet_sequence.available)
        {
            summary.global_packet_sequence.status = "unavailable";
        }
        else if (global_sequence_gap_count_ == 0U)
        {
            summary.global_packet_sequence.status = "ok";
        }
        else
        {
            summary.global_packet_sequence.status = "gap_detected";
        }

        if (duration_seconds > 0U)
        {
            const double seconds = static_cast<double>(duration_seconds);
            summary.performance.actual_rx_gbps = (static_cast<double>(rx_bytes_) * 8.0) / seconds / 1'000'000'000.0;
            summary.performance.actual_rx_mpps = static_cast<double>(rx_packets_) / seconds / 1'000'000.0;
        }

        if (burst_count_ > 0U)
        {
            summary.performance.batch_avg = static_cast<double>(burst_sum_) / static_cast<double>(burst_count_);
        }

        if (burst_sample_count_ > 0U)
        {
            std::vector<std::size_t> burst_samples = collect_bounded_samples(burst_samples_, burst_sample_count_);
            std::sort(burst_samples.begin(), burst_samples.end());
            const std::size_t index = static_cast<std::size_t>((burst_samples.size() - 1U) * 0.99);
            summary.performance.batch_p99 = static_cast<std::uint64_t>(burst_samples[index]);
        }

        if (latency_sample_count_ > 0U)
        {
            std::vector<std::uint64_t> latency_samples =
                collect_bounded_samples(latency_samples_ns_, latency_sample_count_);
            std::sort(latency_samples.begin(), latency_samples.end());
            const std::size_t p50_index = (latency_samples.size() - 1U) / 2U;
            const std::size_t p99_index = static_cast<std::size_t>((latency_samples.size() - 1U) * 0.99);
            summary.performance.latency_p50_us = static_cast<double>(latency_samples[p50_index]) / 1000.0;
            summary.performance.latency_p99_us = static_cast<double>(latency_samples[p99_index]) / 1000.0;
        }

        const ProcessCpuSnapshot snapshot = read_process_cpu_snapshot();
        summary.performance.cpu_metrics_available = snapshot.available && cpu_metrics_available_;
        summary.performance.cpu_metrics_status = snapshot.available ? snapshot.status : cpu_metrics_status_;
        if (snapshot.available && cpu_metrics_available_ && cpu_clock_ticks_per_second_ > 0 &&
            snapshot.wall_ns > cpu_start_wall_ns_)
        {
            const std::uint64_t total_wall_ns = snapshot.wall_ns - cpu_start_wall_ns_;
            summary.performance.cpu_user_pct = ticks_to_cpu_pct(snapshot.user_ticks - cpu_start_user_ticks_,
                                                                cpu_clock_ticks_per_second_, total_wall_ns);
            summary.performance.cpu_sys_pct =
                ticks_to_cpu_pct(snapshot.sys_ticks - cpu_start_sys_ticks_, cpu_clock_ticks_per_second_, total_wall_ns);

            if (cpu_last_wall_ns_ != 0U && snapshot.wall_ns > cpu_last_wall_ns_)
            {
                const std::uint64_t interval_wall_ns = snapshot.wall_ns - cpu_last_wall_ns_;
                const std::uint64_t interval_total_ticks =
                    (snapshot.user_ticks - cpu_last_user_ticks_) + (snapshot.sys_ticks - cpu_last_sys_ticks_);
                cpu_peak_pct_ =
                    std::max(cpu_peak_pct_,
                             ticks_to_cpu_pct(interval_total_ticks, cpu_clock_ticks_per_second_, interval_wall_ns));
            }
            else
            {
                cpu_peak_pct_ =
                    std::max(cpu_peak_pct_, summary.performance.cpu_user_pct + summary.performance.cpu_sys_pct);
            }

            cpu_last_wall_ns_ = snapshot.wall_ns;
            cpu_last_user_ticks_ = snapshot.user_ticks;
            cpu_last_sys_ticks_ = snapshot.sys_ticks;
            cpu_metrics_status_ = snapshot.status;
            summary.performance.cpu_peak_pct = cpu_peak_pct_;
        }
        else
        {
            cpu_metrics_available_ = snapshot.available;
            cpu_metrics_status_ = snapshot.status;
            summary.performance.cpu_metrics_available = snapshot.available;
            summary.performance.cpu_metrics_status = snapshot.status;
        }

        return summary;
    }

} // namespace rxtech
