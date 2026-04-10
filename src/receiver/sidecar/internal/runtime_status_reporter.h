#pragma once

#include <chrono>

#include "metrics_exporter.h"
#include "owner_loop_runtime_state.h"
#include "internal/status_panel.h"
#include "rxtech/owner_loop.h"
#include "rxtech/protocol_spec.h"

namespace rxtech
{

    class DataOrderTracker;

    class RuntimeStatusReporter
    {
      public:
        RuntimeStatusReporter(const RxConfig &config, const ProtocolSpec &spec, std::ostream *status_output,
                              const std::chrono::steady_clock::time_point &start_time);

        void emit_periodic(ReceiveContext &context, CaptureArtifacts &artifacts,
                           const OwnerLoopRuntimeState &runtime_state, const DataOrderTracker &data_order_tracker,
                           const std::chrono::steady_clock::time_point &now);

        RunSummary build_final_summary(ReceiveContext &context, CaptureArtifacts &artifacts,
                                       const OwnerLoopRuntimeState &runtime_state,
                                       const DataOrderTracker &data_order_tracker,
                                       const std::chrono::steady_clock::time_point &end_time) const;

        void render_final(const RunSummary &summary, const std::chrono::steady_clock::duration &elapsed);

        std::ostream *diagnostic_output() const;

      private:
        RunSummary build_summary(ReceiveContext &context, CaptureArtifacts &artifacts,
                                 const OwnerLoopRuntimeState &runtime_state, const DataOrderTracker &data_order_tracker,
                                 std::uint32_t elapsed_seconds) const;

        RxConfig config_;
        ProtocolSpec spec_{};
        std::chrono::steady_clock::time_point start_time_{};
        std::chrono::seconds status_interval_{1};
        std::chrono::steady_clock::time_point next_status_at_{};
        StatusPanelWriter status_panel_;
        mutable MetricsExporter metrics_exporter_;
    };

} // namespace rxtech
