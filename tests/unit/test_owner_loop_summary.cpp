#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <string>
#include <vector>

#include "rxtech/rx_backend.h"
#include "rxtech/owner_loop.h"
#include "owner_loop_summary.h"
#include "status_panel.h"

int main()
{
    rxtech::RunSummary summary;
    summary.run_status = "success";
    summary.backend = "dpdk";
    summary.queue_id = 3U;
    summary.raw_rx_packets = 12U;
    summary.raw_rx_bytes = 3456U;
    summary.arp_request_packets = 2U;
    summary.arp_reply_packets = 1U;
    summary.filtered_packets = 4U;
    summary.rx_packets = 10U;
    summary.rx_bytes = 20480U;
    summary.parsed_packets = 8U;
    summary.control_table_packets = 1U;
    summary.data_packets = 7U;
    summary.dropped_packets = 2U;
    summary.captured_packets = 9U;
    summary.captured_bytes = 18000U;
    summary.recorded_bytes = 12345U;
    summary.raw_record_written_frames = 5U;
    summary.raw_record_written_bytes = 5000U;
    summary.raw_record_dropped_frames = 1U;
    summary.raw_record_dropped_bytes = 256U;
    summary.raw_record_output_dir = "/data/rx_tech_demo/raw_frames";
    summary.raw_record_queue_high_watermark = 7U;
    summary.cpi_count = 2U;
    summary.prt_count = 6U;
    summary.channel_count = 3U;
    summary.data_order_assessment = "偏离按 PRT 推进顺序，当前捕获更像按通道分批到达";
    summary.data_order_first_mismatch = "第 10 个数据包开始偏离，期望 CPI 2 / PRT 41 / CH 1 / PKT 1，实际 CPI 2 / PRT 42 / CH 0 / PKT 1";
    summary.active_prt_available = true;
    summary.active_cpi = 2U;
    summary.active_prt = 41U;
    summary.active_prt_packets_per_channel = 9U;
    summary.active_prt_channel_count = 2U;
    summary.active_prt_complete = false;
    summary.active_prt_channels.push_back({0U, 9U, true});
    summary.active_prt_channels.push_back({1U, 9U, true});
    summary.active_prt_channels.push_back({2U, 0U, false});
    summary.packet_count = 9U;
    summary.empty_poll_ratio = 0.25;
    summary.backend_receive_batches = 12U;
    summary.backend_max_burst_size = 6U;
    summary.backend_kernel_drops = 9U;

    rxtech::BackendStats backend{};
    backend.receive_batches = 12U;
    backend.max_burst_size = 6U;
    backend.kernel_drop_count = 9U;

    rxtech::RunSummary merged_summary{};
    rxtech::merge_backend_stats(merged_summary, backend);

    assert(merged_summary.backend_receive_batches == 12U);
    assert(merged_summary.backend_max_burst_size == 6U);
    assert(merged_summary.backend_kernel_drops == 9U);

    const std::string human = rxtech::build_run_human_summary(summary);
    assert(human.find("接收结束汇总") != std::string::npos);
    assert(human.find("后端类型： dpdk") != std::string::npos);
    assert(human.find("原始收包： 12 包，3456 字节") != std::string::npos);
    assert(human.find("原始帧已写： 5 帧，5000 字节") != std::string::npos);
    assert(human.find("接收顺序： 偏离按 PRT 推进顺序，当前捕获更像按通道分批到达") != std::string::npos);
    assert(human.find("首个顺序偏差： 第 10 个数据包开始偏离") != std::string::npos);
    assert(human.find("当前重组 PRT： CPI 2 / PRT 41（接收中）") != std::string::npos);
    assert(human.find("通道 2（方位差）：0/9 包") != std::string::npos);

    const std::vector<std::string> lines =
        rxtech::build_status_snapshot_lines_for_test(summary, std::chrono::seconds(2));
    assert(!lines.empty());
    assert(lines.front().find("实时接收状态") != std::string::npos);

    bool saw_link_state = false;
    bool saw_protocol_section = false;
    bool saw_result_section = false;
    bool saw_drop_rate = false;
    bool saw_debug_lines = false;
    bool saw_receive_batches = false;
    bool saw_max_burst_size = false;
    bool saw_kernel_drops = false;
    bool saw_protocol_drops = false;
    for (const std::string &line : lines)
    {
        if (line.find("链路判定") != std::string::npos && line.find("已检测到业务协议流量") != std::string::npos)
        {
            saw_link_state = true;
        }
        if (line.find("[协议层统计]") != std::string::npos)
        {
            saw_protocol_section = true;
        }
        if (line.find("[结果层统计]") != std::string::npos)
        {
            saw_result_section = true;
        }
        if (line.find("接收顺序") != std::string::npos ||
            line.find("当前重组 PRT") != std::string::npos ||
            line.find("首个顺序偏差") != std::string::npos)
        {
            saw_debug_lines = true;
        }
        if (line.find("丢包率") != std::string::npos)
        {
            saw_drop_rate = true;
        }
        if (line.find("接收批次") != std::string::npos && line.find("12") != std::string::npos)
        {
            saw_receive_batches = true;
        }
        if (line.find("最大突发批次") != std::string::npos && line.find("6") != std::string::npos)
        {
            saw_max_burst_size = true;
        }
        if (line.find("内核丢弃报文") != std::string::npos && line.find("9") != std::string::npos)
        {
            saw_kernel_drops = true;
        }
        if (line.find("协议丢弃报文") != std::string::npos && line.find("2") != std::string::npos)
        {
            saw_protocol_drops = true;
        }
    }

    assert(saw_link_state);
    assert(saw_protocol_section);
    assert(saw_result_section);
    assert(saw_drop_rate);
    assert(saw_receive_batches);
    assert(saw_max_burst_size);
    assert(saw_kernel_drops);
    assert(saw_protocol_drops);
    assert(!saw_debug_lines);

    rxtech::RunSummary pre_business_summary;
    pre_business_summary.raw_rx_packets = 5U;
    pre_business_summary.raw_rx_bytes = 300U;
    pre_business_summary.filtered_packets = 2U;
    pre_business_summary.empty_poll_ratio = 1.0;
    const std::vector<std::string> pre_business_lines =
        rxtech::build_status_snapshot_lines_for_test(pre_business_summary, std::chrono::seconds(1));

    bool saw_pre_business_state = false;
    bool saw_protocol_section_pre = false;
    bool saw_result_section_pre = false;
    for (const std::string &line : pre_business_lines)
    {
        if (line.find("链路判定") != std::string::npos && line.find("尚未检测到业务协议流量") != std::string::npos)
        {
            saw_pre_business_state = true;
        }
        if (line.find("[协议层统计]") != std::string::npos)
        {
            saw_protocol_section_pre = true;
        }
        if (line.find("[结果层统计]") != std::string::npos)
        {
            saw_result_section_pre = true;
        }
    }

    assert(saw_pre_business_state);
    assert(saw_protocol_section_pre);
    assert(saw_result_section_pre);
    return 0;
}
