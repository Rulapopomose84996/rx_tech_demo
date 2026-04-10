#include <cstddef>
#include <cstdint>
#include <vector>

#include "rxtech/packet_desc.h"
#include "rxtech/sample_packet_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    std::vector<std::uint8_t> frame(data, data + size);
    rxtech::PacketDesc packet;
    packet.data = frame.empty() ? nullptr : frame.data();
    packet.len = static_cast<std::uint32_t>(frame.size());

    rxtech::PacketParser parser;
    (void)parser.parse(packet);
    return 0;
}
