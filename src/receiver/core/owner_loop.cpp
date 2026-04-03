#include "rxtech/owner_loop.h"

#include <chrono>

#include "internal/cpi_state_coordinator.h"
#include "internal/owner_loop_runtime_state.h"
#include "capture_sink.h"
#include "data_order_tracker.h"
#include "packet_pipeline.h"
#include "rxtech/raw_frame_recorder.h"
#include "rxtech/time_utils.h"
#include "runtime_status_reporter.h"

namespace rxtech
{

    void OwnerLoop::set_status_output(std::ostream *output)
    {
        status_output_ = output;
    }

    RunSummary OwnerLoop::run(ReceiveContext &context,
                              CaptureArtifacts &artifacts,
                              const std::function<bool()> &should_stop) const
    {
        const ProtocolSpec spec = protocol_spec_from_config(context.config);
        PacketPipeline packet_pipeline(context.config, spec);
        CpiStateCoordinator cpi_state_coordinator(spec);
        DataOrderTracker data_order_tracker(spec);
        CaptureSink capture_sink(artifacts);

        const auto start_time = std::chrono::steady_clock::now();
        RuntimeStatusReporter status_reporter(context.config, spec, status_output_, start_time);

        OwnerLoopRuntimeState runtime_state;
        std::uint32_t invalid_dumped = 0;

        while (!should_stop())
        {
            RxBurst burst;
            if (!context.backend->recv_burst(burst, context.config.max_burst))
            {
                runtime_state.run_status = "error";
                runtime_state.run_error = "backend recv_burst failed";
                context.backend->release_burst(burst);
                break;
            }

            std::uint64_t burst_bytes = 0;
            std::size_t accepted_packets = 0U;
            for (const PacketDesc &packet : burst.packets)
            {
                if (artifacts.raw_frame_recorder != nullptr)
                {
                    artifacts.raw_frame_recorder->submit(packet);
                }

                const PacketProcessStats process_stats = packet_pipeline.process_packet(
                    packet,
                    *context.metrics,
                    status_reporter.diagnostic_output(),
                    invalid_dumped,
                    [&](const ProcessedPacket &processed)
                    {
                        runtime_state.record_protocol_packet(processed.interpreted);
                        if (processed.interpreted.kind == PacketKind::control_table)
                        {
                            cpi_state_coordinator.process_control_packet(processed.parsed);
                        }
                        if (processed.interpreted.kind == PacketKind::data_packet)
                        {
                            data_order_tracker.observe(processed.interpreted);
                            const CpiProcessResult cpi_result = cpi_state_coordinator.process_data_packet(processed.parsed,
                                                                                                           processed.interpreted,
                                                                                                           spec,
                                                                                                           *context.metrics,
                                                                                                           runtime_state.run_status,
                                                                                                           runtime_state.run_error);
                            if (!cpi_result.accepted)
                            {
                                return;
                            }
                            runtime_state.record_data_packet(processed.parsed, processed.interpreted, spec);
                        }

                        runtime_state.record_captured_packet(processed.interpreted);
                        capture_sink.write(processed);
                    });

                burst_bytes += process_stats.accepted_bytes;
                accepted_packets += process_stats.accepted_packets;
                runtime_state.filtered_packets += process_stats.filtered_packets;
            }

            if (accepted_packets > 0U)
            {
                context.metrics->on_burst(accepted_packets, burst_bytes);
            }
            context.backend->release_burst(burst);

            // T-004: periodic timeout check on active CPI
            cpi_state_coordinator.check_timeout(steady_clock_now_ns(), *context.metrics);

            status_reporter.emit_periodic(context,
                                          artifacts,
                                          runtime_state,
                                          data_order_tracker,
                                          std::chrono::steady_clock::now());
        }

        capture_sink.flush();
        const auto end_time = std::chrono::steady_clock::now();
        RunSummary summary = status_reporter.build_final_summary(context,
                                                                 artifacts,
                                                                 runtime_state,
                                                                 data_order_tracker,
                                                                 end_time);
        status_reporter.render_final(summary, end_time - start_time);
        cpi_state_coordinator.release_active();
        return summary;
    }

} // namespace rxtech
