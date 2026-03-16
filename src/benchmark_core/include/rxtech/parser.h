#pragma once

#include <cstdint>

#include "rxtech/packet_desc.h"

namespace rxtech {

struct ParsedPacketMeta {
    bool valid = false;
    std::uint16_t packet_type = 0;
    std::uint16_t version = 0;
};

ParsedPacketMeta parse_packet(const PacketDesc& packet);

}  // namespace rxtech
