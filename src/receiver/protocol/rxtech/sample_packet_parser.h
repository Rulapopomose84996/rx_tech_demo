#pragma once

#include <cstdint>
#include <string>

#include "rxtech/packet_desc.h"

namespace rxtech {

enum class SamplePacketKind {
    unknown,
    control_table,
    data_packet
};

struct SamplePacketView {
    bool valid = false;
    SamplePacketKind kind = SamplePacketKind::unknown;
    std::uint32_t magic = 0;
    std::uint16_t cpi = 0;
    std::uint16_t channel = 0;
    std::uint16_t prt = 0;
    std::uint16_t packet_index = 0;
    std::uint32_t tail = 0;
    std::uint32_t header_offset = 0;
    std::uint32_t frame_length = 0;
    const std::uint8_t* payload_ptr = nullptr;
    std::uint32_t payload_len = 0;
    std::string error_reason;
};

class SamplePacketParser {
public:
    SamplePacketView parse(const PacketDesc& packet) const noexcept;
};

const char* sample_packet_kind_name(SamplePacketKind kind) noexcept;

}  // namespace rxtech
