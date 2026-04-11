#pragma once

#include <cstdint>

#include "rxtech/sample_packet_parser.h"

namespace rxtech
{

    struct InterpretedPacketView
    {
        bool valid = false;
        PacketKind kind = PacketKind::unknown;
        std::uint16_t cpi = 0;
        std::uint16_t prt = 0;
        std::uint16_t channel = 0;
        std::uint16_t packet_index = 0;
        std::uint16_t packet_position_in_prt = 0;
        std::uint32_t iq_count = 0;
        std::uint32_t zero_padding_bytes = 0;
        RejectReason reject_reason = RejectReason::none;
    };

    class ProtocolSequenceInterpreter
    {
      public:
        ProtocolSequenceInterpreter() = default;
        explicit ProtocolSequenceInterpreter(const ProtocolSpec &spec) : spec_(spec) {}

        InterpretedPacketView interpret(const ParsedPacketView &packet) const noexcept;

      private:
        ProtocolSpec spec_{};
    };

} // namespace rxtech
