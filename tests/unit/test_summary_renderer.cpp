#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>

#include <nlohmann/json.hpp>

#include "run_context_snapshot.h"
#include "summary_renderer.h"
#include "rxtech/metrics.h"
#include "rxtech/rx_config.h"

int main()
{
    rxtech::RxConfig config = rxtech::load_default_config();
    config.process.backend_name = "socket";
    config.process.config_path = "configs/socket_loopback.conf";
    config.process.run_label = "20260411_235000_socket_loopback";
    config.operations.output_dir = "results/20260411_235000_socket_loopback";
    config.runtime.run_until_stopped = true;

    const rxtech::RunHeaderSnapshot header = rxtech::build_run_header_snapshot(config);
    const nlohmann::json header_payload = rxtech::render_run_header_event_payload(header);
    assert(header_payload.at("backend") == "socket");
    assert(header_payload.at("artifacts").at("run_dir") == "results/20260411_235000_socket_loopback");
    assert(header_payload.at("logging").at("events_path") == "results/20260411_235000_socket_loopback/events.jsonl");

    rxtech::RunSummary summary;
    summary.run.backend_name = "socket";
    summary.run.status = "success";
    summary.run.error_message.clear();
    summary.capture.run_artifact_dir = "results/20260411_235000_socket_loopback";
    summary.capture.packets_path = "results/20260411_235000_socket_loopback/capture_packets.bin";
    summary.capture.index_path = "results/20260411_235000_socket_loopback/capture_index.csv";
    summary.capture.raw_record_role = "heavy_debug_recorder";
    summary.capture.raw_record_output_dir = "/data/rx_tech_demo/raw_frames";
    summary.protocol.rx_packets = 10U;
    summary.protocol.parsed_packets = 8U;
    summary.protocol.data_packets = 7U;
    summary.protocol.control_table_packets = 1U;
    summary.protocol.dropped_packets = 2U;
    summary.performance.cpu_metrics_available = true;
    summary.performance.cpu_user_pct = 12.5;
    summary.performance.cpu_sys_pct = 1.5;
    summary.performance.cpu_peak_pct = 13.0;
    summary.metrics_export.enabled = false;

    const std::string json_text = rxtech::render_summary_json(summary, header);
    const nlohmann::json parsed_json = nlohmann::json::parse(json_text);
    assert(parsed_json.at("header").at("backend") == "socket");
    assert(parsed_json.at("summary").at("protocol").at("parsed_packets") == 8U);
    assert(parsed_json.at("summary").at("capture").at("raw_record_role") == "heavy_debug_recorder");

    const std::string text_summary = rxtech::render_summary_text(summary, header);
    assert(text_summary.find("运行头部") != std::string::npos);
    assert(text_summary.find("后端类型： socket") != std::string::npos);
    assert(text_summary.find("解析有效包： 8 包") != std::string::npos);
    assert(text_summary.find("重型专项录制") != std::string::npos);

    assert(rxtech::summary_json_path("results/20260411_235000_socket_loopback") ==
           "results/20260411_235000_socket_loopback/summary.json");
    assert(rxtech::summary_text_path("results/20260411_235000_socket_loopback") ==
           "results/20260411_235000_socket_loopback/summary.txt");
    return 0;
}
