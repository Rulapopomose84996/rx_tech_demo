#pragma once

#include <string>

#include "run_context_snapshot.h"
#include "rxtech/metrics.h"

namespace rxtech
{

    std::string summary_json_path(const std::string &run_dir);
    std::string summary_text_path(const std::string &run_dir);
    std::string render_summary_json(const RunSummary &summary, const RunHeaderSnapshot &header);
    std::string render_summary_text(const RunSummary &summary, const RunHeaderSnapshot &header);

} // namespace rxtech
