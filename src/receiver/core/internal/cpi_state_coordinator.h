#pragma once

#include <cstdint>
#include <string>

#include "rxtech/cpi_admission.h"
#include "rxtech/cpi_context_pool.h"
#include "rxtech/cpi_finalizer.h"
#include "rxtech/metrics.h"
#include "rxtech/protocol_sequence_interpreter.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/progress_tracker.h"
#include "rxtech/sample_packet_parser.h"
#include "rxtech/slot_writer.h"

namespace rxtech
{

    struct CpiProcessResult
    {
        bool accepted = false;
    };

    class CpiStateCoordinator
    {
    public:
        CpiStateCoordinator() = default;
        explicit CpiStateCoordinator(const ProtocolSpec &spec)
            : slot_writer_(spec), progress_tracker_(spec), spec_(spec) {}

        void process_control_packet(const ParsedPacketView &parsed);

        /// Check if the active CPI has exceeded its timeout; finalize if so.
        bool check_timeout(std::uint64_t now_ns, IMetricsCollector &metrics);

        CpiProcessResult process_data_packet(const ParsedPacketView &parsed,
                                             const InterpretedPacketView &packet,
                                             const ProtocolSpec &spec,
                                             IMetricsCollector &metrics,
                                             std::string &run_status,
                                             std::string &run_error);

        void release_active();

    private:
        bool open_active(std::uint16_t cpi_id,
                         IMetricsCollector &metrics,
                         std::string &run_status,
                         std::string &run_error);
        void finalize_active(std::uint32_t trigger);
        void bind_snapshot_to_active();

        CpiContextPool ctx_pool_;
        RecentClosedRing closed_ring_;
        CpiAdmission admission_;
        SlotWriter slot_writer_;
        ProgressTracker progress_tracker_;
        CpiFinalizer finalizer_;
        ProtocolSpec spec_{};
        BoundWaveSnapshotLite current_snapshot_{};
        std::uint32_t active_ctx_index_ = kInvalidPoolIndex;
        CpiContext *active_ctx_ = nullptr;
    };

} // namespace rxtech
