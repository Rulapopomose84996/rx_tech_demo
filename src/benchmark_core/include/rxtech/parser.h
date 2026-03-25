#pragma once

#include <cstdint>
#include <string>

#include "rxtech/demo_protocol.h"
#include "rxtech/packet_desc.h"

namespace rxtech {

struct ParsedPacketMeta {
    bool valid = false;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint32_t port_id = 0;
    std::uint32_t stream_id = 0;
    std::uint64_t block_id = 0;
    std::uint32_t block_bytes = 0;
    std::uint16_t frag_idx = 0;
    std::uint16_t frag_count = 0;
    std::uint16_t frag_payload_bytes = 0;
    std::uint32_t payload_offset = 0;
    std::string error_reason;
};

ParsedPacketMeta parse_packet(const PacketDesc& packet);

}  // namespace rxtech
