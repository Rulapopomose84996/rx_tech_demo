#include "internal/debug_capture_writer.h"

#include <ostream>
#include <utility>

namespace rxtech
{

    DebugCaptureWriter::DebugCaptureWriter(CapturePolicy policy, std::ostream *packet_stream, std::ostream *index_stream,
                                           std::string run_dir)
        : policy_(policy), packet_stream_(packet_stream), index_stream_(index_stream), run_dir_(std::move(run_dir))
    {
        manifest_.policy = capture_policy_name(policy_);
    }

    bool DebugCaptureWriter::record(const DebugCaptureRecord &record)
    {
        if (policy_ == CapturePolicy::disabled)
        {
            return false;
        }

        if (policy_ == CapturePolicy::first_effective_cpi)
        {
            if (!manifest_.selected)
            {
                manifest_.selected = true;
                manifest_.selected_cpi = record.cpi;
            }
            if (record.cpi != manifest_.selected_cpi)
            {
                return false;
            }
        }

        if (packet_stream_ != nullptr)
        {
            (*packet_stream_) << record.payload;
        }

        if (index_stream_ != nullptr)
        {
            (*index_stream_) << record.cpi << ',' << record.channel << ',' << record.prt << ',' << record.packet_index
                             << ',' << record.packet_kind << ',' << (record.valid ? "true" : "false") << '\n';
        }
        return true;
    }

    void DebugCaptureWriter::finish() {}

    const DebugCaptureManifest &DebugCaptureWriter::manifest() const noexcept
    {
        return manifest_;
    }

} // namespace rxtech
