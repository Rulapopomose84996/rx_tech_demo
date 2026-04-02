#pragma once

#include <cstdint>

#include "rxtech/packet_desc.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/udp_payload_assembler.h"

namespace rxtech {

enum class PacketKind {
    unknown,
    control_table,
    data_packet
};

enum class RejectReason {
    none,
    invalid_len,
    invalid_header,
    invalid_channel,
    invalid_packet_index,
    invalid_tail,
    invalid_field_combo
};

struct ParsedPacketView {
    bool valid = false;
    PacketKind kind = PacketKind::unknown;
    std::uint16_t cpi = 0;
    std::uint16_t channel = 0;
    std::uint16_t prt = 0;
    std::uint16_t packet_index = 0;
    std::uint32_t tail = 0;
    const std::uint8_t* payload_ptr = nullptr;
    std::uint32_t payload_len = 0;
    std::uint64_t rx_tsc = 0;
    RejectReason reject_reason = RejectReason::none;
};

class PacketParser {
public:
    PacketParser() = default;
    explicit PacketParser(const ProtocolSpec& spec) : spec_(spec) {
    }

    ParsedPacketView parse(const PacketDesc& packet) const noexcept;
    ParsedPacketView parse(const UdpPayloadFrame& frame) const noexcept;

private:
    ProtocolSpec spec_{};
};

const char* packet_kind_name(PacketKind kind) noexcept;
const char* reject_reason_name(RejectReason reason) noexcept;

}  // namespace rxtech
