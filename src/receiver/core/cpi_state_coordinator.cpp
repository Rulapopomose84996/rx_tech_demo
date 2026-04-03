#include "internal/cpi_state_coordinator.h"

namespace rxtech
{

    bool CpiStateCoordinator::open_active(std::uint16_t cpi_id,
                                          IMetricsCollector &metrics,
                                          std::string &run_status,
                                          std::string &run_error)
    {
        release_active();
        active_ctx_index_ = ctx_pool_.acquire(cpi_id);
        active_ctx_ = ctx_pool_.get(active_ctx_index_);
        if (active_ctx_ == nullptr)
        {
            active_ctx_index_ = kInvalidPoolIndex;
            run_status = "error";
            run_error = "cpi context pool exhausted";
            metrics.on_error();
            return false;
        }
        return true;
    }

    void CpiStateCoordinator::finalize_active(std::uint32_t trigger)
    {
        if (active_ctx_ == nullptr)
        {
            return;
        }
        const std::optional<CpiOutput> output = finalizer_.try_finalize(*active_ctx_, trigger);
        if (output.has_value())
        {
            closed_ring_.push(output->cpi_id, output->seal_tsc, output->decision);
        }
        release_active();
    }

    CpiProcessResult CpiStateCoordinator::process_data_packet(const ParsedPacketView &parsed,
                                                              const InterpretedPacketView &packet,
                                                              const ProtocolSpec &spec,
                                                              IMetricsCollector &metrics,
                                                              std::string &run_status,
                                                              std::string &run_error)
    {
        CpiProcessResult result;
        if (active_ctx_ == nullptr && !open_active(packet.cpi, metrics, run_status, run_error))
        {
            return result;
        }

        AdmissionResult admission_result = admission_.judge(parsed, active_ctx_->header.cpi_id, closed_ring_);
        if (admission_result == AdmissionResult::TRIGGER_CPI_SWITCH)
        {
            finalize_active(TriggerCpiSwitch);
            if (!open_active(packet.cpi, metrics, run_status, run_error))
            {
                return result;
            }
            admission_result = AdmissionResult::WRITE_ACTIVE;
        }

        if (admission_result != AdmissionResult::WRITE_ACTIVE)
        {
            metrics.on_drop();
            return result;
        }

        const SlotWriteResult slot_write = slot_writer_.write(*active_ctx_, parsed);
        if (slot_write.duplicate)
        {
            metrics.on_drop();
            return result;
        }
        if (!slot_write.first_write)
        {
            metrics.on_reject(slot_write.reason);
            return result;
        }

        progress_tracker_.advance(*active_ctx_, packet.prt, packet.channel, parsed.tail == spec.magic_tail);
        if ((active_ctx_->header.trigger_bits & TriggerFullReady) != 0U)
        {
            finalize_active(TriggerFullReady);
        }

        result.accepted = true;
        return result;
    }

    void CpiStateCoordinator::release_active()
    {
        if (active_ctx_index_ != kInvalidPoolIndex)
        {
            ctx_pool_.release(active_ctx_index_);
            active_ctx_index_ = kInvalidPoolIndex;
            active_ctx_ = nullptr;
        }
    }

} // namespace rxtech
