#include "rxtech/parser.h"

namespace rxtech {

ParsedPacketMeta parse_packet(const PacketDesc& packet) {
    ParsedPacketMeta meta;
    meta.valid = packet.len > 0;
    meta.packet_type = static_cast<std::uint16_t>(packet.len & 0xFFU);
    meta.version = 1;
    return meta;
}

}  // namespace rxtech
