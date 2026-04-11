#pragma once

#include <chrono>
#include <string>

#include "rxtech/metrics.h"
#include "rxtech/rx_config.h"

namespace rxtech
{

    class MetricsExporter
    {
      public:
        MetricsExporter() = default;
        MetricsExporter(const RxConfig &config, const std::chrono::steady_clock::time_point &start_time);

        void configure(const RxConfig &config, const std::chrono::steady_clock::time_point &start_time);
        void maybe_export(const RunSummary &summary, const std::chrono::steady_clock::time_point &now);
        void export_final(const RunSummary &summary);
        void populate_summary(RunSummary &summary) const;

        static std::string render_prometheus_text(const RunSummary &summary);
        static std::string render_json_payload(const RunSummary &summary);

      private:
        bool export_once(const RunSummary &summary);
        bool export_prometheus_text(const RunSummary &summary);
        bool export_json_socket(const RunSummary &summary);

        bool enabled_ = false;
        std::string mode_ = "none";
        std::string target_path_;
        std::chrono::seconds interval_{0};
        std::chrono::steady_clock::time_point next_export_at_{};
        std::uint64_t success_count_ = 0;
        std::uint64_t error_count_ = 0;
        std::string status_ = "disabled";
    };

} // namespace rxtech
