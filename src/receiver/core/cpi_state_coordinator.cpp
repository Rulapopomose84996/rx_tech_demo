#include "internal/cpi_state_coordinator.h"

#include "rxtech/time_utils.h"

namespace rxtech
{

    void CpiStateCoordinator::process_control_packet(const ParsedPacketView &parsed)
    {
        if (!parsed.valid || parsed.kind != PacketKind::control_table)
        {
            return;
        }
        current_snapshot_.wave_cpi = parsed.cpi;
        current_snapshot_.n_prt = static_cast<std::uint16_t>(spec_.expected_n_prt);
        current_snapshot_.channel_count = static_cast<std::uint16_t>(spec_.channels_per_prt);
        current_snapshot_.packets_per_channel = static_cast<std::uint16_t>(spec_.packets_per_channel);
        current_snapshot_.timeout_ns = spec_.cpi_timeout_ns;
        current_snapshot_.bind_tsc = steady_clock_now_ns();
        current_snapshot_.valid = (spec_.expected_n_prt > 0U);
    }

    void CpiStateCoordinator::bind_snapshot_to_active()
    {
        if (active_ctx_ == nullptr)
        {
            return;
        }
        active_ctx_->bind = current_snapshot_;
        if (current_snapshot_.valid && current_snapshot_.n_prt > 0U)
        {
            set_expected_prt_count(*active_ctx_, current_snapshot_.n_prt,
                                   current_snapshot_.channel_count,
                                   current_snapshot_.packets_per_channel);
        }
    }

    bool CpiStateCoordinator::check_timeout(std::uint64_t now_ns, IMetricsCollector &metrics)
    {
        if (active_ctx_ == nullptr)
        {
            return false;
        }
        const std::uint64_t timeout_ns = active_ctx_->bind.timeout_ns;
        if (timeout_ns == 0U)
        {
            return false;
        }
        const std::uint64_t anchor = active_ctx_->header.first_rx_tsc;
        if (anchor == 0U)
        {
            return false;
        }
        if (now_ns > anchor && (now_ns - anchor) >= timeout_ns)
        {
            active_ctx_->header.trigger_bits |= TriggerTimeout;
            finalize_active(TriggerTimeout, metrics);
            metrics.on_error();
            return true;
        }
        return false;
    }

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
        active_ctx_->header.channels_per_prt = static_cast<std::uint16_t>(spec_.channels_per_prt);
        active_ctx_->header.packets_per_channel = static_cast<std::uint16_t>(spec_.packets_per_channel);
        bind_snapshot_to_active();
        return true;
    }

    void CpiStateCoordinator::finalize_active(std::uint32_t trigger, IMetricsCollector &metrics)
    {
        if (active_ctx_ == nullptr)
        {
            return;
        }
        const std::optional<CpiOutput> output = finalizer_.try_finalize(*active_ctx_, trigger);
        if (output.has_value())
        {
            closed_ring_.push(output->cpi_id, output->seal_tsc, output->decision);
            if (output_ring_ != nullptr)
            {
                CpiOutput out = *output;
                out.output_id = next_output_id_++;
                if (!output_ring_->push(out))
                {
                    // SPSC full — backpressure: discard and release immediately
                    metrics.on_pool_exhaustion();
                    ctx_pool_.release(active_ctx_index_);
                }
                // else: pool slot stays occupied until consumer returns ReleaseToken
            }
            else
            {
                // No output ring attached — legacy path, release immediately
                ctx_pool_.release(active_ctx_index_);
            }
        }
        else
        {
            // Finalization declined — release pool slot
            ctx_pool_.release(active_ctx_index_);
        }
        active_ctx_index_ = kInvalidPoolIndex;
        active_ctx_ = nullptr;
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
            finalize_active(TriggerCpiSwitch, metrics);
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
        const std::uint32_t finalize_mask = TriggerFullReady | TriggerWaveEnd;
        if ((active_ctx_->header.trigger_bits & finalize_mask) != 0U)
        {
            finalize_active(active_ctx_->header.trigger_bits & finalize_mask, metrics);
        }

        result.accepted = true;
        return result;
    }

    void CpiStateCoordinator::drain_recycle(IMetricsCollector & /*metrics*/)
    {
        if (recycle_ring_ == nullptr)
        {
            return;
        }
        ReleaseToken token;
        while (recycle_ring_->pop(token))
        {
            ctx_pool_.release(token.ctx_pool_index);
        }
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
