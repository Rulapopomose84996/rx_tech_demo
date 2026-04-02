#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "rxtech/sample_packet_parser.h"

namespace rxtech {

struct ProtocolPacketView {
    bool valid = false;
    SamplePacketKind kind = SamplePacketKind::unknown;
    std::uint16_t cpi = 0;
    std::uint16_t prt = 0;
    std::uint16_t channel = 0;
    std::string channel_name;
    std::uint16_t packet_index = 0;
    std::uint16_t packet_position_in_prt = 0;
    std::uint32_t iq_count = 0;
    std::uint32_t zero_padding_bytes = 0;
    std::string error_reason;
};

class ProtocolSequenceInterpreter {
public:
    ProtocolPacketView interpret(const SamplePacketView& packet) noexcept;

private:
    std::unordered_map<std::uint16_t, std::uint32_t> data_packet_counters_;
};

}  // namespace rxtech
