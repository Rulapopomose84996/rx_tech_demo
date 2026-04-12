#include "internal/cpi_state_coordinator.h"

#include <algorithm>

#include "rxtech/time_utils.h"

namespace rxtech
{

    namespace
    {

        std::uint32_t max_supported_n_prt(const ProtocolSpec &spec)
        {
            return std::min(spec.max_n_prt, static_cast<std::uint32_t>(kCpiPrtMax));
        }

        ControlSnapshot make_snapshot_base(std::uint16_t cpi_id, const ProtocolSpec &spec, BindSource bind_source)
        {
            ControlSnapshot snapshot;
            snapshot.cpi_id = cpi_id;
            snapshot.channel_count = static_cast<std::uint16_t>(spec.channels_per_prt);
            snapshot.packets_per_channel = static_cast<std::uint16_t>(spec.packets_per_channel);
            snapshot.timeout_ns = spec.protocol_cpi_timeout_ns;
            snapshot.bind_source = bind_source;
            return snapshot;
        }

        ControlSnapshot make_fixed_snapshot(std::uint16_t cpi_id, const ProtocolSpec &spec)
        {
            ControlSnapshot snapshot = make_snapshot_base(cpi_id, spec, BindSource::fixed);
            snapshot.n_prt =
                static_cast<std::uint16_t>(std::min(spec.expected_n_prt, static_cast<std::uint32_t>(kCpiPrtMax)));
            snapshot.valid = snapshot.n_prt > 0U;
            return snapshot;
        }

        ControlSnapshot make_provisional_snapshot(std::uint16_t cpi_id, const ProtocolSpec &spec)
        {
            ControlSnapshot snapshot = make_snapshot_base(cpi_id, spec, BindSource::provisional);
            snapshot.n_prt = static_cast<std::uint16_t>(max_supported_n_prt(spec));
            snapshot.valid = snapshot.n_prt > 0U;
            return snapshot;
        }

        ControlSnapshot make_control_snapshot(const ParsedPacketView &parsed, const ProtocolSpec &spec)
        {
            if (!spec.dynamic_prt_enabled)
            {
                return make_fixed_snapshot(parsed.cpi, spec);
            }

            ControlSnapshot snapshot = make_snapshot_base(parsed.cpi, spec, BindSource::control);
            const auto parsed_n_prt = static_cast<std::uint32_t>(parsed.prt);
            const auto max_n_prt = max_supported_n_prt(spec);
            if (parsed_n_prt >= 1U && parsed_n_prt <= max_n_prt)
            {
                snapshot.n_prt = static_cast<std::uint16_t>(parsed_n_prt);
                snapshot.valid = true;
                return snapshot;
            }

            return make_fixed_snapshot(parsed.cpi, spec);
        }

        ControlSnapshot select_open_snapshot(std::uint16_t cpi_id, const ProtocolSpec &spec,
                                             const ControlSnapshot &staged_control)
        {
            if (!spec.dynamic_prt_enabled)
            {
                return make_fixed_snapshot(cpi_id, spec);
            }

            if (staged_control.cpi_id == cpi_id && staged_control.valid)
            {
                return staged_control;
            }

            return make_provisional_snapshot(cpi_id, spec);
        }

        bool can_converge_to_control(const ControlSnapshot &candidate, const CpiContext &ctx)
        {
            return candidate.bind_source == BindSource::control && candidate.valid &&
                   candidate.n_prt >= ctx.header.observed_n_prt;
        }

        void merge_control_snapshot(CpiContext &ctx, const ControlSnapshot &candidate)
        {
            ControlSnapshot &bound = ctx.control;
            if (bound.bind_source == BindSource::control)
            {
                if (candidate.bind_source == BindSource::control && bound.valid && candidate.valid &&
                    bound.n_prt != candidate.n_prt)
                {
                    bound.conflict = true;
                }
                return;
            }

            if (bound.bind_source == BindSource::provisional)
            {
                if (can_converge_to_control(candidate, ctx))
                {
                    bind_control_snapshot(ctx, candidate);
                }
                else if (candidate.bind_source == BindSource::control && candidate.valid)
                {
                    bound.conflict = true;
                }
                return;
            }

            if (candidate.bind_source == BindSource::control && candidate.valid)
            {
                bind_control_snapshot(ctx, candidate);
                return;
            }

            if (bound.cpi_id == 0U || !bound.valid)
            {
                bind_control_snapshot(ctx, candidate);
            }
        }

    } // namespace

    void CpiStateCoordinator::process_control_packet(const ParsedPacketView &parsed)
    {
        if (!parsed.valid || parsed.kind != PacketKind::control_table)
        {
            return;
        }

        const ControlSnapshot candidate = make_control_snapshot(parsed, spec_);
        if (active_ctx_ != nullptr && active_ctx_->header.cpi_id == candidate.cpi_id)
        {
            merge_control_snapshot(*active_ctx_, candidate);
            current_control_ = active_ctx_->control;
            return;
        }

        if (previous_ctx_ != nullptr && previous_ctx_->header.cpi_id == candidate.cpi_id)
        {
            merge_control_snapshot(*previous_ctx_, candidate);
            return;
        }

        current_control_ = candidate;
    }

    void CpiStateCoordinator::bind_snapshot_to_active(const ControlSnapshot &snapshot)
    {
        if (active_ctx_ == nullptr)
        {
            return;
        }

        bind_control_snapshot(*active_ctx_, snapshot);
    }

    bool CpiStateCoordinator::check_timeout(std::uint64_t now_ns, MetricsCollector &metrics)
    {
        bool timed_out = false;

        // now_ns、first_rx_tsc 和 ingress 提供的 rx_tsc 必须统一处于 steady_clock_now_ns() 纳秒时钟域。

        // Check previous CPI timeout first
        if (previous_ctx_ != nullptr)
        {
            const std::uint64_t prev_timeout = previous_ctx_->control.timeout_ns;
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
        const std::uint64_t timeout_ns = active_ctx_->control.timeout_ns;
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

    bool CpiStateCoordinator::open_active(std::uint16_t cpi_id, MetricsCollector &metrics, std::string &run_status,
                                          std::string &run_error)
    {
        // Release only the active context, not the previous (dual-window).
        if (active_ctx_index_ != kInvalidPoolIndex)
        {
            ctx_pool_.release(active_ctx_index_);
            active_ctx_ = nullptr;
            active_ctx_index_ = kInvalidPoolIndex;
        }
        auto drain_recycle_once = [this]() -> bool
        {
            if (recycle_ring_ == nullptr)
            {
                return false;
            }

            bool recycled = false;
            ReleaseToken token;
            while (recycle_ring_->pop(token))
            {
                ctx_pool_.release(token.ctx_pool_index);
                recycled = true;
            }
            return recycled;
        };

        auto try_acquire_active = [this, cpi_id]() -> bool
        {
            active_ctx_index_ = ctx_pool_.acquire(cpi_id);
            return active_ctx_index_ != kInvalidPoolIndex;
        };

        if (!try_acquire_active())
        {
            // Non-blocking retry: drain recycle ring and retry acquire
            // without any sleep_for to avoid violating the zero-blocking
            // guarantee on the real-time owner thread.
            static constexpr int kMaxDrainRetries = 3;
            for (int attempt = 0; attempt < kMaxDrainRetries; ++attempt)
            {
                drain_recycle_once();
                if (try_acquire_active())
                {
                    break;
                }
            }
        }
        active_ctx_ = ctx_pool_.get(active_ctx_index_);
        if (active_ctx_ == nullptr)
        {
            active_ctx_index_ = kInvalidPoolIndex;
            // Degrade gracefully: drop the packet but do not terminate the run.
            // The caller will see open_active() return false and skip the packet.
            run_status = "degraded";
            metrics.on_pool_exhaustion();
            return false;
        }

        current_control_ = select_open_snapshot(cpi_id, spec_, current_control_);
        bind_snapshot_to_active(current_control_);
        return true;
    }

    void CpiStateCoordinator::finalize_ctx(CpiContext *&ctx, std::uint32_t &ctx_index, std::uint32_t trigger,
                                           MetricsCollector &metrics)
    {
        if (ctx == nullptr)
        {
            return;
        }
        const std::optional<CpiOutput> output = finalizer_.try_finalize(*ctx, trigger);
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
                    ctx_pool_.release(ctx_index);
                    output_degraded_ = true;
                }
            }
            else
            {
                ctx_pool_.release(ctx_index);
            }
        }
        else
        {
            ctx_pool_.release(ctx_index);
        }
        ctx_index = kInvalidPoolIndex;
        ctx = nullptr;
    }

    void CpiStateCoordinator::finalize_active(std::uint32_t trigger, MetricsCollector &metrics)
    {
        finalize_ctx(active_ctx_, active_ctx_index_, trigger, metrics);
    }

    void CpiStateCoordinator::finalize_previous(std::uint32_t trigger, MetricsCollector &metrics)
    {
        finalize_ctx(previous_ctx_, previous_ctx_index_, trigger, metrics);
    }

    CpiProcessResult CpiStateCoordinator::process_data_packet(const ParsedPacketView &parsed,
                                                              const InterpretedPacketView &packet,
                                                              const ProtocolSpec &spec, MetricsCollector &metrics,
                                                              std::string &run_status, std::string &run_error)
    {
        CpiProcessResult result;
        if (active_ctx_ == nullptr && closed_ring_.contains(parsed.cpi))
        {
            metrics.on_late_packet_rejected();
            metrics.on_drop();
            return result;
        }
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
                    progress_tracker_.advance(*previous_ctx_, packet.prt, packet.channel,
                                              parsed.tail == spec.magic_tail);
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

    void CpiStateCoordinator::drain_recycle(MetricsCollector & /*metrics*/)
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

    void CpiStateCoordinator::finalize_active_for_shutdown(MetricsCollector &metrics)
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

    void CpiStateCoordinator::configure_output_policy(OutputDropPolicy policy)
    {
        output_drop_policy_ = policy;
    }

    bool CpiStateCoordinator::output_drop_is_error() const
    {
        return output_drop_policy_ == OutputDropPolicy::error;
    }

} // namespace rxtech
