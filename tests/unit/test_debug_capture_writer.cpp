#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <sstream>
#include <string>

#include "debug_capture_writer.h"

int main()
{
    std::ostringstream packet_stream;
    std::ostringstream index_stream;

    rxtech::DebugCaptureWriter writer(rxtech::CapturePolicy::first_effective_cpi, &packet_stream, &index_stream,
                                      "results/run");

    const std::array<std::uint8_t, 4> first_payload{{'a', 'b', 'c', 'd'}};
    const std::array<std::uint8_t, 4> second_payload{{'e', 'f', 'g', 'h'}};
    const std::array<std::uint8_t, 4> third_payload{{'i', 'j', 'k', 'l'}};

    const rxtech::DebugCaptureRecord first{2U, 0U, 1U, 1U, rxtech::PacketKind::data_packet, true,
                                           first_payload.data(), first_payload.size()};
    const rxtech::DebugCaptureRecord second_same_cpi{2U, 0U, 1U, 2U, rxtech::PacketKind::data_packet, true,
                                                     second_payload.data(), second_payload.size()};
    const rxtech::DebugCaptureRecord next_cpi{3U, 0U, 1U, 1U, rxtech::PacketKind::data_packet, true,
                                              third_payload.data(), third_payload.size()};

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
