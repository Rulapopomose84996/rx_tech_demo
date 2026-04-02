#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <string>
#include <vector>

#include "rxtech/owner_loop.h"
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
    summary.packet_count = 9U;
    summary.empty_poll_ratio = 0.25;

    const std::string human = rxtech::build_run_human_summary(summary);
    assert(human.find("接收结束汇总") != std::string::npos);
    assert(human.find("后端类型： dpdk") != std::string::npos);
    assert(human.find("原始收包： 12 包，3456 字节") != std::string::npos);
    assert(human.find("原始帧已写： 5 帧，5000 字节") != std::string::npos);

    const std::vector<std::string> lines =
        rxtech::build_status_snapshot_lines_for_test(summary, std::chrono::seconds(2));
    assert(!lines.empty());
    assert(lines.front().find("实时接收状态") != std::string::npos);

    bool saw_link_state = false;
    bool saw_result_section = false;
    bool saw_drop_rate = false;
    for (const std::string& line : lines)
    {
        if (line.find("链路判定") != std::string::npos && line.find("已检测到业务协议流量") != std::string::npos)
        {
            saw_link_state = true;
        }
        if (line.find("[结果层统计]") != std::string::npos)
        {
            saw_result_section = true;
        }
        if (line.find("丢包率") != std::string::npos)
        {
            saw_drop_rate = true;
        }
    }

    assert(saw_link_state);
    assert(saw_result_section);
    assert(saw_drop_rate);
    return 0;
}
