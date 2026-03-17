#pragma once

#include <string>

#include "rxtech/metrics.h"

namespace rxtech {

void write_summary_json(const RunSummary& summary, const std::string& output_dir);
void write_summary_csv(const RunSummary& summary, const std::string& output_dir);
void write_step_summaries_json(const std::vector<StepSummary>& steps, const std::string& output_dir);
void write_step_summaries_csv(const std::vector<StepSummary>& steps, const std::string& output_dir);

}  // namespace rxtech
