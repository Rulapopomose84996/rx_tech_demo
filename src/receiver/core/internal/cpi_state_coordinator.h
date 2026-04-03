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

        CpiContextPool ctx_pool_;
        RecentClosedRing closed_ring_;
        CpiAdmission admission_;
        SlotWriter slot_writer_;
        ProgressTracker progress_tracker_;
        CpiFinalizer finalizer_;
        std::uint32_t active_ctx_index_ = kInvalidPoolIndex;
        CpiContext *active_ctx_ = nullptr;
    };

} // namespace rxtech
