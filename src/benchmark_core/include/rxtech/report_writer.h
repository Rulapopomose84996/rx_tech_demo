#pragma once

#include <string>

#include "rxtech/metrics.h"

namespace rxtech {

void write_summary_json(const RunSummary& summary, const std::string& output_dir);
void write_summary_csv(const RunSummary& summary, const std::string& output_dir);

}  // namespace rxtech
