#include "internal/summary_renderer.h"

#include <sstream>

#include <nlohmann/json.hpp>

#include "owner_loop_summary.h"

namespace rxtech
{

    std::string summary_json_path(const std::string &run_dir)
    {
        return run_dir + "/summary.json";
    }

    std::string summary_text_path(const std::string &run_dir)
    {
        return run_dir + "/summary.txt";
    }

    std::string render_summary_json(const RunSummary &summary, const RunHeaderSnapshot &header)
    {
        const nlohmann::json json_summary = {
            {"header",
             {{"backend", header.backend},
              {"build_mode", header.build_mode},
              {"config_path", header.config_path},
              {"run_id", header.run_id},
              {"events_path", header.events_path},
              {"run_dir", header.run_dir}}},
            {"summary",
             {{"run", {{"status", summary.run.status}, {"error_message", summary.run.error_message}}},
              {"protocol",
               {{"rx_packets", summary.protocol.rx_packets},
                {"parsed_packets", summary.protocol.parsed_packets},
                {"data_packets", summary.protocol.data_packets},
                {"control_table_packets", summary.protocol.control_table_packets},
                {"dropped_packets", summary.protocol.dropped_packets}}},
              {"capture",
               {{"capture_policy", summary.capture.capture_policy},
                {"packets_path", summary.capture.packets_path},
                {"index_path", summary.capture.index_path},
                {"run_artifact_dir", summary.capture.run_artifact_dir}}},
              {"performance",
               {{"cpu_metrics_available", summary.performance.cpu_metrics_available},
                {"cpu_user_pct", summary.performance.cpu_user_pct},
                {"cpu_sys_pct", summary.performance.cpu_sys_pct},
                {"cpu_peak_pct", summary.performance.cpu_peak_pct}}}}}};
        return json_summary.dump(2);
    }

    std::string render_summary_text(const RunSummary &summary, const RunHeaderSnapshot &header)
    {
        std::ostringstream out;
        out << "========== 运行头部 ==========\n";
        out << "后端类型： " << header.backend << '\n';
        out << "构建模式： " << header.build_mode << '\n';
        out << "配置路径： " << header.config_path << '\n';
        out << "运行目录： " << header.run_dir << '\n';
        out << "事件文件： " << header.events_path << "\n\n";
        out << build_run_human_summary(summary);
        return out.str();
    }

} // namespace rxtech
