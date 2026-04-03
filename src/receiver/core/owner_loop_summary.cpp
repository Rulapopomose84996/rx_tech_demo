#include "owner_loop_summary.h"

#include <sstream>

#include "rxtech/raw_frame_recorder.h"

namespace rxtech
{

    void merge_backend_stats(RunSummary &summary, const BackendStats &backend_stats)
    {
        summary.raw_rx_packets = backend_stats.rx_packets;
        summary.raw_rx_bytes = backend_stats.rx_bytes;
        summary.dropped_packets += backend_stats.backend_drops;
        summary.backend_errors += backend_stats.rx_errors;
        summary.rx_polls = backend_stats.rx_polls;
        summary.empty_polls = backend_stats.empty_polls;
        summary.arp_request_packets = backend_stats.arp_request_packets;
        summary.arp_reply_packets = backend_stats.arp_reply_packets;
        summary.queue_id = backend_stats.queue_id;
        summary.umem_size = backend_stats.umem_size;
        summary.frame_size = backend_stats.frame_size;
        summary.fill_ring_size = backend_stats.fill_ring_size;
        summary.completion_ring_size = backend_stats.completion_ring_size;
        if (summary.rx_polls > 0U)
        {
            summary.empty_poll_ratio = static_cast<double>(summary.empty_polls) / static_cast<double>(summary.rx_polls);
        }
    }

    void apply_raw_record_stats(RunSummary &summary, const RawFrameRecorder *raw_frame_recorder)
    {
        if (raw_frame_recorder == nullptr || !raw_frame_recorder->enabled())
        {
            return;
        }

        const RawFrameRecorderStats stats = raw_frame_recorder->snapshot();
        summary.raw_record_output_dir = raw_frame_recorder->output_dir();
        summary.raw_record_latest_file_path = stats.latest_file_path;
        summary.raw_record_written_frames = stats.written_frames;
        summary.raw_record_written_bytes = stats.written_bytes;
        summary.raw_record_dropped_frames = stats.dropped_frames;
        summary.raw_record_dropped_bytes = stats.dropped_bytes;
        summary.raw_record_retained_bytes = stats.retained_bytes;
        summary.raw_record_queue_high_watermark = stats.queue_high_watermark;
    }

    double calculate_drop_rate(const RunSummary &summary)
    {
        const double total = static_cast<double>(summary.rx_packets + summary.dropped_packets);
        if (total <= 0.0)
        {
            return 0.0;
        }
        return static_cast<double>(summary.dropped_packets) / total;
    }

    const char *protocol_channel_name(std::uint16_t channel)
    {
        switch (channel)
        {
        case 0:
            return "和路";
        case 1:
            return "俯仰差";
        case 2:
            return "方位差";
        case 3:
            return "辅助通道";
        default:
            return "未知通道";
        }
    }

    std::string build_run_human_summary(const RunSummary &summary)
    {
        std::ostringstream out;
        out << "\n========== 接收结束汇总 ==========\n";
        out << "运行结果： " << (summary.run_status == "success" ? "成功" : "失败") << "\n";
        out << "后端类型： " << summary.backend << "\n";
        out << "接收队列： " << summary.queue_id << "\n";
        out << "原始收包： " << summary.raw_rx_packets << " 包，" << summary.raw_rx_bytes << " 字节\n";
        out << "ARP 请求： " << summary.arp_request_packets << " 包\n";
        out << "ARP 应答： " << summary.arp_reply_packets << " 包\n";
        out << "过滤丢弃： " << summary.filtered_packets << " 包\n";
        out << "候选业务包： " << summary.rx_packets << " 包，" << summary.rx_bytes << " 字节\n";
        out << "解析有效包： " << summary.parsed_packets << " 包\n";
        out << "控制表包： " << summary.control_table_packets << " 包\n";
        out << "数据包： " << summary.data_packets << " 包\n";
        out << "协议丢弃： " << summary.dropped_packets << " 包\n";
        out << "CPI 数： " << summary.cpi_count << "\n";
        out << "PRT 数： " << summary.prt_count << "\n";
        out << "完整 PRT 数： " << summary.complete_prt_count << "\n";
        out << "通道数： " << summary.channel_count << "\n";
        out << "接收顺序： " << summary.data_order_assessment << "\n";
        if (!summary.data_order_first_mismatch.empty())
        {
            out << "首个顺序偏差： " << summary.data_order_first_mismatch << "\n";
        }
        out << "最终包尾数量： " << summary.final_tail_packets << "\n";
        out << "已落盘包数： " << summary.packet_count << "\n";
        out << "抓包索引： " << summary.capture_index_path << "\n";
        out << "抓包数据： " << summary.capture_packets_path << "\n";
        if (!summary.raw_record_output_dir.empty())
        {
            out << "原始帧目录： " << summary.raw_record_output_dir << "\n";
            out << "原始帧已写： " << summary.raw_record_written_frames << " 帧，"
                << summary.raw_record_written_bytes << " 字节\n";
            out << "原始帧丢弃： " << summary.raw_record_dropped_frames << " 帧\n";
            out << "原始帧保留量： " << summary.raw_record_retained_bytes << " 字节\n";
            out << "录制队列高水位： " << summary.raw_record_queue_high_watermark << "\n";
            if (!summary.raw_record_latest_file_path.empty())
            {
                out << "最新原始帧文件： " << summary.raw_record_latest_file_path << "\n";
            }
        }
        if (!summary.protocol_channels.empty())
        {
            out << "通道分布：\n";
            for (const auto &channel : summary.protocol_channels)
            {
                out << "  - 通道 " << channel.channel << "（" << protocol_channel_name(channel.channel) << "）："
                    << channel.data_packets << " 个数据包，"
                    << channel.iq_count << " 个 IQ\n";
            }
        }
        if (!summary.protocol_cpis.empty())
        {
            out << "CPI 分布：\n";
            for (const auto &cpi : summary.protocol_cpis)
            {
                out << "  - CPI " << cpi.cpi
                    << "：数据包 " << cpi.data_packets
                    << " 包，PRT 数 " << cpi.prt_count << "\n";
            }
        }
        if (summary.active_prt_available && !summary.active_prt_channels.empty())
        {
            out << "当前重组 PRT： CPI " << summary.active_cpi
                << " / PRT " << summary.active_prt
                << "（" << (summary.active_prt_complete ? "完整" : "接收中") << "）\n";
            out << "当前重组 PRT 覆盖：\n";
            for (const auto &channel : summary.active_prt_channels)
            {
                out << "  - 通道 " << channel.channel << "（" << protocol_channel_name(channel.channel) << "）："
                    << channel.packet_count << '/' << summary.active_prt_packets_per_channel << " 包\n";
            }
        }
        out << "==================================\n";
        return out.str();
    }

} // namespace rxtech
