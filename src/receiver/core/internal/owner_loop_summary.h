#pragma once

#include "rxtech/owner_loop.h"

namespace rxtech
{

class RawFrameRecorder;

void merge_backend_stats(RunSummary &summary, const BackendStats &backend_stats);
void apply_raw_record_stats(RunSummary &summary, const RawFrameRecorder *raw_frame_recorder);
double calculate_drop_rate(const RunSummary &summary);
const char *protocol_channel_name(std::uint16_t channel);

} // namespace rxtech
