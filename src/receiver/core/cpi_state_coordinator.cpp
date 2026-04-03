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

        if (spec_.dynamic_prt_enabled)
        {
            // ParsedPacketView::prt is reused to carry the n_prt field from control packets.
            // The parser writes the control table's n_prt value into this field.
            const auto parsed_n_prt = static_cast<std::uint32_t>(parsed.prt);

            if (active_ctx_ != nullptr && active_ctx_->bind.bind_source != BindSource::fixed)
            {
                // CPI already has a binding — check for duplicate or conflict
                if (active_ctx_->bind.bind_source == BindSource::control)
                {
                    // Already bound by a previous control packet
                    if (static_cast<std::uint32_t>(active_ctx_->bind.n_prt) != parsed_n_prt)
                    {
                        active_ctx_->bind.conflict = true;
                    }
                    // Either way, do not overwrite the first binding
                }
                else if (active_ctx_->bind.bind_source == BindSource::provisional)
                {
                    // Provisional → try to converge
                    if (parsed_n_prt >= 1U && parsed_n_prt <= spec_.max_n_prt &&
                        parsed_n_prt >= active_ctx_->header.observed_n_prt)
                    {
                        // Converge: refine n_prt from provisional max to actual control value
                        current_snapshot_.n_prt = static_cast<std::uint16_t>(parsed_n_prt);
                        current_snapshot_.valid = true;
                        active_ctx_->bind.bind_source = BindSource::control;
                        active_ctx_->bind.n_prt = current_snapshot_.n_prt;
                        set_expected_prt_count(*active_ctx_, current_snapshot_.n_prt,
                                               current_snapshot_.channel_count,
                                               current_snapshot_.packets_per_channel);
                    }
                    else
                    {
                        // Conflict: control n_prt < observed prt or out of range
                        active_ctx_->bind.conflict = true;
                    }
                }
            }
            else
            {
                // No active CPI or still at fixed default — set snapshot from control packet
                if (parsed_n_prt >= 1U && parsed_n_prt <= spec_.max_n_prt)
                {
                    current_snapshot_.n_prt = static_cast<std::uint16_t>(parsed_n_prt);
                    current_snapshot_.valid = true;
                    current_snapshot_.bind_source = BindSource::control;
                }
                else
                {
                    // Invalid n_prt from control packet — fall back to expected_n_prt
                    current_snapshot_.n_prt = static_cast<std::uint16_t>(spec_.expected_n_prt);
                    current_snapshot_.valid = (spec_.expected_n_prt > 0U);
                    current_snapshot_.bind_source = BindSource::fixed;
                }
            }
        }
        else
        {
            current_snapshot_.n_prt = static_cast<std::uint16_t>(spec_.expected_n_prt);
            current_snapshot_.valid = (spec_.expected_n_prt > 0U);
            current_snapshot_.bind_source = BindSource::fixed;
        }

        current_snapshot_.wave_cpi = parsed.cpi;
        current_snapshot_.channel_count = static_cast<std::uint16_t>(spec_.channels_per_prt);
        current_snapshot_.packets_per_channel = static_cast<std::uint16_t>(spec_.packets_per_channel);
        current_snapshot_.timeout_ns = spec_.cpi_timeout_ns;
        current_snapshot_.bind_tsc = steady_clock_now_ns();
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
        bool timed_out = false;

        // Check previous CPI timeout first
        if (previous_ctx_ != nullptr)
        {
            const std::uint64_t prev_timeout = previous_ctx_->bind.timeout_ns;
            if (prev_timeout != 0U)
            {
                const std::uint64_t prev_anchor = previous_ctx_->header.first_rx_tsc;
                if (prev_anchor != 0U && now_ns > prev_anchor && (now_ns - prev_anchor) >= prev_timeout)
                {
                    previous_ctx_->header.trigger_bits |= TriggerTimeout;
                    finalize_previous(TriggerTimeout, metrics);
                    timed_out = true;
                }
            }
        }

        // Check active CPI timeout
        if (active_ctx_ == nullptr)
        {
            return timed_out;
        }
        const std::uint64_t timeout_ns = active_ctx_->bind.timeout_ns;
        if (timeout_ns == 0U)
        {
            return timed_out;
        }
        const std::uint64_t anchor = active_ctx_->header.first_rx_tsc;
        if (anchor == 0U)
        {
            return timed_out;
        }
        if (now_ns > anchor && (now_ns - anchor) >= timeout_ns)
        {
            active_ctx_->header.trigger_bits |= TriggerTimeout;
            finalize_active(TriggerTimeout, metrics);
            metrics.on_error();
            return true;
        }
        return timed_out;
    }

    bool CpiStateCoordinator::open_active(std::uint16_t cpi_id,
                                          IMetricsCollector &metrics,
                                          std::string &run_status,
                                          std::string &run_error)
    {
        // Release only the active context, not the previous (dual-window).
        if (active_ctx_index_ != kInvalidPoolIndex)
        {
            ctx_pool_.release(active_ctx_index_);
            active_ctx_ = nullptr;
            active_ctx_index_ = kInvalidPoolIndex;
        }
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

        if (spec_.dynamic_prt_enabled && current_snapshot_.bind_source != BindSource::control)
        {
            // Data arrived first — provisional with max_n_prt as upper bound
            current_snapshot_.n_prt = static_cast<std::uint16_t>(std::min(spec_.max_n_prt, static_cast<std::uint32_t>(kCpiPrtMax)));
            current_snapshot_.channel_count = static_cast<std::uint16_t>(spec_.channels_per_prt);
            current_snapshot_.packets_per_channel = static_cast<std::uint16_t>(spec_.packets_per_channel);
            current_snapshot_.timeout_ns = spec_.cpi_timeout_ns;
            current_snapshot_.valid = true;
            current_snapshot_.bind_source = BindSource::provisional;
        }

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
                    metrics.on_output_backpressure();
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

    void CpiStateCoordinator::finalize_previous(std::uint32_t trigger, IMetricsCollector &metrics)
    {
        if (previous_ctx_ == nullptr)
        {
            return;
        }
        const std::optional<CpiOutput> output = finalizer_.try_finalize(*previous_ctx_, trigger);
        if (output.has_value())
        {
            closed_ring_.push(output->cpi_id, output->seal_tsc, output->decision);
            if (output_ring_ != nullptr)
            {
                CpiOutput out = *output;
                out.output_id = next_output_id_++;
                if (!output_ring_->push(out))
                {
                    metrics.on_output_backpressure();
                    ctx_pool_.release(previous_ctx_index_);
                }
            }
            else
            {
                ctx_pool_.release(previous_ctx_index_);
            }
        }
        else
        {
            ctx_pool_.release(previous_ctx_index_);
        }
        previous_ctx_index_ = kInvalidPoolIndex;
        previous_ctx_ = nullptr;
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
            // Move current → previous (finalize old previous first)
            finalize_previous(TriggerCpiSwitch, metrics);
            previous_ctx_ = active_ctx_;
            previous_ctx_index_ = active_ctx_index_;
            active_ctx_ = nullptr;
            active_ctx_index_ = kInvalidPoolIndex;

            if (!open_active(packet.cpi, metrics, run_status, run_error))
            {
                return result;
            }
            admission_result = AdmissionResult::WRITE_ACTIVE;
        }

        if (admission_result != AdmissionResult::WRITE_ACTIVE)
        {
            // Check if packet belongs to previous CPI (late packet tolerance)
            if (previous_ctx_ != nullptr && parsed.cpi == previous_ctx_->header.cpi_id)
            {
                const SlotWriteResult late_write = slot_writer_.write(*previous_ctx_, parsed);
                if (late_write.first_write && !late_write.duplicate)
                {
                    progress_tracker_.advance(*previous_ctx_, packet.prt, packet.channel, parsed.tail == spec.magic_tail);
                    metrics.on_late_packet_accepted();
                    result.accepted = true;
                    return result;
                }
            }
            metrics.on_late_packet_rejected();
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

    void CpiStateCoordinator::finalize_active_for_shutdown(IMetricsCollector &metrics)
    {
        finalize_previous(TriggerStop, metrics);
        if (active_ctx_ != nullptr)
        {
            active_ctx_->header.trigger_bits |= TriggerStop;
            finalize_active(TriggerStop, metrics);
        }
    }

    void CpiStateCoordinator::release_active()
    {
        if (previous_ctx_index_ != kInvalidPoolIndex)
        {
            ctx_pool_.release(previous_ctx_index_);
            previous_ctx_index_ = kInvalidPoolIndex;
            previous_ctx_ = nullptr;
        }
        if (active_ctx_index_ != kInvalidPoolIndex)
        {
            ctx_pool_.release(active_ctx_index_);
            active_ctx_index_ = kInvalidPoolIndex;
            active_ctx_ = nullptr;
        }
    }

} // namespace rxtech
