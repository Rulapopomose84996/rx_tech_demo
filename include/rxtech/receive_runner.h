#pragma once

#include <atomic>
#include <iosfwd>

#include "rxtech/receive_context.h"

namespace rxtech
{

    void request_receive_stop();
    void reset_receive_stop();
    void prepare_run_artifact_paths(RxConfig &config);

    class ReceiveRunner
    {
      public:
        void set_status_output(std::ostream *output);
        void request_stop() noexcept;
        void reset_stop() noexcept;
        bool stop_requested() const noexcept;
        RunSummary run(ReceiveContext &context);

      private:
        std::ostream *status_output_ = nullptr;
        std::atomic<bool> stop_requested_{false};
    };

} // namespace rxtech
