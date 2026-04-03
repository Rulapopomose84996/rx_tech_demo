#pragma once

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>

#include "rxtech/cpi_consumer.h"
#include "rxtech/receive_context.h"

namespace rxtech
{

    class RawFrameRecorder;

    std::string build_run_human_summary(const RunSummary &summary);

    struct CaptureArtifacts
    {
        std::ostream *packet_stream = nullptr;
        std::ostream *index_stream = nullptr;
        RawFrameRecorder *raw_frame_recorder = nullptr;
        std::uint64_t file_offset = 0;
        std::uint64_t recorded_packets = 0;
        std::uint64_t recorded_bytes = 0;
        std::uint64_t captured_packets = 0;
        std::uint64_t captured_bytes = 0;
    };

    class OwnerLoop
    {
    public:
        void set_status_output(std::ostream *output);
        void set_output_handler(CpiOutputHandler handler) { output_handler_ = std::move(handler); }
        RunSummary run(ReceiveContext &context,
                       CaptureArtifacts &artifacts,
                       const std::function<bool()> &should_stop) const;

    private:
        std::ostream *status_output_ = nullptr;
        CpiOutputHandler output_handler_;
    };

} // namespace rxtech
