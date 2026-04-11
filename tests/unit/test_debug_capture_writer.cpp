#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <sstream>
#include <string>

#include "debug_capture_writer.h"

int main()
{
    std::ostringstream packet_stream;
    std::ostringstream index_stream;

    rxtech::DebugCaptureWriter writer(rxtech::CapturePolicy::first_effective_cpi, &packet_stream, &index_stream,
                                      "results/run");

    const rxtech::DebugCaptureRecord first{2U, 0U, 1U, 1U, "data_packet", true, "abcd"};
    const rxtech::DebugCaptureRecord second_same_cpi{2U, 0U, 1U, 2U, "data_packet", true, "efgh"};
    const rxtech::DebugCaptureRecord next_cpi{3U, 0U, 1U, 1U, "data_packet", true, "ijkl"};

    writer.record(first);
    writer.record(second_same_cpi);
    writer.record(next_cpi);
    writer.finish();

    assert(packet_stream.str() == "abcdefgh");
    assert(index_stream.str().find("2,0,1,1") != std::string::npos);
    assert(index_stream.str().find("3,0,1,1") == std::string::npos);
    assert(writer.manifest().selected_cpi == 2U);
    return 0;
}
