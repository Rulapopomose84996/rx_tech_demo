#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "rxtech/rx_config.h"

namespace rxtech
{

    struct RunHeaderSnapshot
    {
        std::string backend;
        std::string build_mode;
        std::string config_path;
        std::string run_id;
        std::string run_dir;
        std::string events_path;
        std::string summary_json_path;
        std::string summary_text_path;
        std::string host = "unknown";
    };

    RunHeaderSnapshot build_run_header_snapshot(const RxConfig &config);
    nlohmann::json render_run_header_event_payload(const RunHeaderSnapshot &snapshot);

} // namespace rxtech
