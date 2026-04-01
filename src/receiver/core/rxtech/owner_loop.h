#pragma once

#include <cstdint>
#include <functional>
#include <iosfwd>

#include "rxtech/receive_context.h"

namespace rxtech {

struct CaptureArtifacts {
    std::ostream* packet_stream = nullptr;
    std::ostream* index_stream = nullptr;
    std::uint64_t file_offset = 0;
    std::uint64_t recorded_packets = 0;
    std::uint64_t recorded_bytes = 0;
    std::uint64_t captured_packets = 0;
    std::uint64_t captured_bytes = 0;
};

class OwnerLoop {
public:
    void set_status_output(std::ostream* output);
    RunSummary run(ReceiveContext& context,
                   CaptureArtifacts& artifacts,
                   const std::function<bool()>& should_stop) const;

private:
    std::ostream* status_output_ = nullptr;
};

}  // namespace rxtech
