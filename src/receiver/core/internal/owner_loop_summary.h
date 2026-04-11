#pragma once

#include "owner_loop_runtime_state.h"
#include "rxtech/owner_loop.h"
#include "rxtech/protocol_spec.h"

namespace rxtech
{

class RawFrameRecorder;

void merge_backend_stats(RunSummary &summary, const BackendStats &backend_stats);
void apply_raw_record_stats(RunSummary &summary, const RawFrameRecorder *raw_frame_recorder);
double calculate_drop_rate(const RunSummary &summary);
const char *protocol_channel_name(std::uint16_t channel);
void populate_data_order_summary(RunSummary &target,
                                 std::uint64_t checked_packets,
                                 bool matches_expected,
                                 bool channel_batched,
                                 const std::string &first_mismatch);
void populate_active_prt_summary(RunSummary &target,
                                 const ProtocolSpec &spec,
                                 bool latest_data_seen,
                                 std::uint16_t latest_data_cpi,
                                 std::uint16_t latest_data_prt,
                                 const CpiPrtCoverageMap &prt_coverage);

} // namespace rxtech
