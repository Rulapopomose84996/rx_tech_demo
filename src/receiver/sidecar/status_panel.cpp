#include "internal/status_panel.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

#ifdef __linux__
#include <unistd.h>
#endif

namespace rxtech
{

    namespace
    {

        bool has_business_traffic(const RunSummary &summary)
        {
            return summary.protocol.parsed_packets > 0U || summary.protocol.control_table_packets > 0U ||
                   summary.protocol.data_packets > 0U;
        }

        std::string format_wall_clock_timestamp()
        {
            const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm local_time{};
#ifdef _WIN32
            localtime_s(&local_time, &now);
#else
            localtime_r(&now, &local_time);
#endif
            char buffer[32] = {};
            if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local_time) == 0U)
            {
                return {};
            }
            return buffer;
        }

        std::string describe_link_state(const RunSummary &summary)
        {
            if (has_business_traffic(summary))
            {
                return "已检测到业务协议流量";
            }
            return "尚未检测到业务协议流量";
        }

        std::string format_decimal(double value, int precision)
        {
            std::ostringstream out;
            out << std::fixed << std::setprecision(precision) << value;
            return out.str();
        }

        std::string build_metric_line(const std::string &label, const std::string &value)
        {
            std::ostringstream out;
            out << std::left << std::setw(14) << label << ": " << value;
            return out.str();
        }

        double calculate_drop_rate(const RunSummary &summary)
        {
            const double total = static_cast<double>(summary.protocol.rx_packets + summary.protocol.dropped_packets +
                                                     summary.backend.dropped_packets + summary.backend.kernel_drops);
            if (total <= 0.0)
            {
                return 0.0;
            }
            return static_cast<double>(summary.protocol.dropped_packets + summary.backend.dropped_packets +
                                       summary.backend.kernel_drops) /
                   total;
        }

        std::vector<std::string> build_status_snapshot_lines(const RunSummary &summary,
                                                             const std::chrono::steady_clock::duration &elapsed)
        {
            const auto elapsed_seconds = std::max<std::uint64_t>(
                1ULL, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count()));
            const double aggregate_gbps = (static_cast<double>(summary.protocol.rx_bytes) * 8.0) /
                                          static_cast<double>(elapsed_seconds) / 1'000'000'000.0;

            std::vector<std::string> lines;
            lines.reserve(32);
            lines.push_back("================ 实时接收状态 ================");
            lines.push_back(build_metric_line("时间戳", format_wall_clock_timestamp()));
            lines.push_back(build_metric_line("运行时长", std::to_string(elapsed_seconds) + " s"));
            lines.push_back(build_metric_line("链路判定", describe_link_state(summary)));
            lines.push_back("");
            lines.push_back("[链路层统计]");
            lines.push_back(build_metric_line("原始接收帧", std::to_string(summary.backend.raw_rx_packets) + " 帧 / " +
                                                                std::to_string(summary.backend.raw_rx_bytes) +
                                                                " 字节"));
            lines.push_back(
                build_metric_line("ARP 请求帧", std::to_string(summary.backend.arp_request_packets) + " 帧"));
            lines.push_back(build_metric_line("ARP 应答帧", std::to_string(summary.backend.arp_reply_packets) + " 帧"));
            lines.push_back(build_metric_line("过滤报文", std::to_string(summary.backend.filtered_packets) + " 报文"));
            lines.push_back(build_metric_line("空轮询占比", format_decimal(summary.performance.empty_poll_ratio, 4)));
            lines.push_back("");
            lines.push_back("[协议层统计]");
            lines.push_back(build_metric_line("候选业务报文", std::to_string(summary.protocol.rx_packets) + " 报文 / " +
                                                                  std::to_string(summary.protocol.rx_bytes) + " 字节"));
            lines.push_back(
                build_metric_line("协议有效报文", std::to_string(summary.protocol.parsed_packets) + " 报文"));
            lines.push_back(
                build_metric_line("控制表报文", std::to_string(summary.protocol.control_table_packets) + " 报文"));
            lines.push_back(build_metric_line("数据报文", std::to_string(summary.protocol.data_packets) + " 报文"));
            lines.push_back(
                build_metric_line("协议丢弃报文", std::to_string(summary.protocol.dropped_packets) + " 报文"));
            lines.push_back(
                build_metric_line("后端丢弃报文", std::to_string(summary.backend.dropped_packets) + " 报文"));
            lines.push_back(build_metric_line("接收批次", std::to_string(summary.backend.receive_batches)));
            lines.push_back(build_metric_line("最大突发批次", std::to_string(summary.backend.max_burst_size)));
            lines.push_back(build_metric_line("内核丢弃报文", std::to_string(summary.backend.kernel_drops) + " 报文"));
            lines.push_back("");
            lines.push_back("[输出链路统计]");
            if (summary.performance.output_backpressure_count > 0U)
            {
                lines.push_back(
                    build_metric_line("输出退化次数", std::to_string(summary.performance.output_backpressure_count)));
            }
            else
            {
                lines.push_back(build_metric_line("输出退化次数", "0"));
            }
            {
                const char *run_result_label = "成功";
                if (summary.run.status == "degraded")
                {
                    run_result_label = "退化";
                }
                else if (summary.run.status != "success")
                {
                    run_result_label = "失败";
                }
                lines.push_back(build_metric_line("运行结论", run_result_label));
            }
            lines.push_back("");
            lines.push_back("[可观测性]");
            if (summary.performance.cpu_metrics_available)
            {
                lines.push_back(
                    build_metric_line("CPU 用户/系统", format_decimal(summary.performance.cpu_user_pct, 2) + "% / " +
                                                           format_decimal(summary.performance.cpu_sys_pct, 2) + "%"));
                lines.push_back(
                    build_metric_line("CPU 峰值", format_decimal(summary.performance.cpu_peak_pct, 2) + "%"));
            }
            else
            {
                lines.push_back(build_metric_line("CPU 指标", summary.performance.cpu_metrics_status));
            }
            if (summary.global_packet_sequence.available)
            {
                lines.push_back(build_metric_line(
                    "全局序列号 gap", std::to_string(summary.global_packet_sequence.gap_count) + " / 缺失 " +
                                          std::to_string(summary.global_packet_sequence.missing_packets)));
            }
            else
            {
                lines.push_back(build_metric_line("全局序列号", summary.global_packet_sequence.status));
            }
            if (summary.metrics_export.enabled)
            {
                lines.push_back(
                    build_metric_line("指标导出", summary.metrics_export.mode + " / " + summary.metrics_export.status));
            }
            lines.push_back("");
            lines.push_back("[结果层统计]");
            lines.push_back(build_metric_line("全局 CPI 数", std::to_string(summary.protocol.cpi_count)));
            lines.push_back(build_metric_line("全局 PRT 数", std::to_string(summary.protocol.prt_count)));
            lines.push_back(build_metric_line("全局通道数", std::to_string(summary.protocol.channel_count)));
            lines.push_back(build_metric_line("落盘记录", std::to_string(summary.capture.packet_count) + " 报文 / " +
                                                              std::to_string(summary.capture.recorded_bytes) +
                                                              " 字节"));
            if (!summary.capture.raw_record_output_dir.empty() || summary.capture.raw_record_written_frames > 0U ||
                summary.capture.raw_record_dropped_frames > 0U)
            {
                lines.push_back(build_metric_line(
                    "原始帧录制", std::to_string(summary.capture.raw_record_written_frames) + " 帧 / " +
                                      std::to_string(summary.capture.raw_record_written_bytes) + " 字节"));
                lines.push_back(
                    build_metric_line("录制丢弃帧", std::to_string(summary.capture.raw_record_dropped_frames) + " 帧"));
                lines.push_back(build_metric_line("录制队列高水位",
                                                  std::to_string(summary.capture.raw_record_queue_high_watermark)));
            }
            lines.push_back(build_metric_line("接收吞吐率", format_decimal(aggregate_gbps, 6) + " Gbps"));
            lines.push_back(build_metric_line("丢包率", format_decimal(calculate_drop_rate(summary), 6)));
            lines.push_back("==============================================");
            return lines;
        }

    } // namespace

    std::vector<std::string> build_status_snapshot_lines_for_test(const RunSummary &summary,
                                                                  const std::chrono::steady_clock::duration &elapsed)
    {
        return build_status_snapshot_lines(summary, elapsed);
    }

    StatusPanelWriter::StatusPanelWriter(std::ostream *output) : output_(output)
    {
#ifdef __linux__
        interactive_console_ = (output_ == &std::cout && ::isatty(STDOUT_FILENO) == 1) ||
                               (output_ == &std::cerr && ::isatty(STDERR_FILENO) == 1);
#endif
    }

    StatusPanelWriter::~StatusPanelWriter()
    {
        finish();
    }

    void StatusPanelWriter::render(const RunSummary &summary, const std::chrono::steady_clock::duration &elapsed)
    {
        if (output_ == nullptr)
        {
            return;
        }

        const std::vector<std::string> lines = build_status_snapshot_lines(summary, elapsed);
        line_count_ = lines.size();
        if (interactive_console_)
        {
            if (!initialized_)
            {
                *output_ << "\x1b[?25l\x1b[2J";
                initialized_ = true;
            }
            *output_ << "\x1b[H";
            for (const std::string &line : lines)
            {
                *output_ << "\x1b[2K" << line << '\n';
            }
            *output_ << "\x1b[J";
        }
        else
        {
            for (const std::string &line : lines)
            {
                *output_ << line << '\n';
            }
        }
        output_->flush();
    }

    std::ostream *StatusPanelWriter::diagnostic_output() const
    {
        if (interactive_console_)
        {
            return &std::cerr;
        }
        return output_ != nullptr ? output_ : &std::cerr;
    }

    void StatusPanelWriter::finish()
    {
        if (interactive_console_ && initialized_ && output_ != nullptr)
        {
            *output_ << "\x1b[" << (line_count_ + 1U) << ";1H\x1b[2K\x1b[?25h";
            output_->flush();
            initialized_ = false;
        }
    }

} // namespace rxtech
