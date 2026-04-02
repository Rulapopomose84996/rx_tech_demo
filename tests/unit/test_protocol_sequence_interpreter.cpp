#include <iostream>
#include <vector>

#include "rxtech/protocol_sequence_interpreter.h"

namespace
{

    std::vector<std::uint8_t> make_payload(std::uint32_t data_bytes, std::uint8_t fill_byte, std::uint32_t zero_padding_bytes)
    {
        std::vector<std::uint8_t> payload(data_bytes + zero_padding_bytes, fill_byte);
        for (std::size_t i = data_bytes; i < payload.size(); ++i)
        {
            payload[i] = 0U;
        }
        return payload;
    }

    rxtech::ParsedPacketView make_data_packet(std::uint16_t cpi,
                                              std::uint16_t channel,
                                              std::uint16_t prt,
                                              std::uint16_t packet_index,
                                              const std::vector<std::uint8_t> &payload_storage)
    {
        rxtech::ParsedPacketView packet;
        packet.valid = true;
        packet.kind = rxtech::PacketKind::data_packet;
        packet.cpi = cpi;
        packet.channel = channel;
        packet.prt = prt;
        packet.packet_index = packet_index;
        packet.payload_ptr = payload_storage.data();
        packet.payload_len = static_cast<std::uint32_t>(payload_storage.size());
        return packet;
    }

} // namespace

int main()
{
    rxtech::ProtocolSequenceInterpreter interpreter;

    {
        for (std::uint16_t channel = 0; channel < 3U; ++channel)
        {
            for (std::uint16_t packet_index = 1U; packet_index <= 9U; ++packet_index)
            {
                const auto payload = packet_index == 9U ? make_payload(476U * 4U, 0xCDU, 128U)
                                                        : make_payload(2032U, 0xABU, 0U);
                const rxtech::InterpretedPacketView view =
                    interpreter.interpret(make_data_packet(2U, channel, 7U, packet_index, payload));
                if (!view.valid)
                {
                    std::cerr << "expected channel=" << channel << " packet_index=" << packet_index
                              << " to be valid, got " << rxtech::reject_reason_name(view.reject_reason) << '\n';
                    return 1;
                }

                const std::uint16_t expected_position = static_cast<std::uint16_t>(channel * 9U + packet_index);
                if (view.prt != 7U || view.channel != channel || view.packet_index != packet_index ||
                    view.packet_position_in_prt != expected_position)
                {
                    std::cerr << "unexpected interpreted values at channel=" << channel
                              << " packet_index=" << packet_index
                              << " got prt=" << view.prt
                              << " channel=" << view.channel
                              << " packet_index=" << view.packet_index
                              << " position=" << view.packet_position_in_prt << '\n';
                    return 1;
                }
            }
        }
    }

    {
        rxtech::ProtocolSpec spec;
        spec.channels_per_prt = 4U;
        spec.packets_per_channel = 7U;
        rxtech::ProtocolSequenceInterpreter parameterized_interpreter(spec);
        const auto payload = make_payload(476U * 4U, 0x55U, 128U);
        const rxtech::InterpretedPacketView view =
            parameterized_interpreter.interpret(make_data_packet(5U, 3U, 99U, 7U, payload));
        if (!view.valid || view.prt != 99U || view.channel != 3U || view.packet_index != 7U ||
            view.packet_position_in_prt != 28U)
        {
            std::cerr << "expected parameterized interpreter to preserve packet coordinates, got valid="
                      << view.valid << " prt=" << view.prt << " channel=" << view.channel
                      << " packet_index=" << view.packet_index
                      << " position=" << view.packet_position_in_prt << '\n';
            return 1;
        }
    }

    {
        rxtech::ProtocolSequenceInterpreter ninth_packet_interpreter;
        const auto payload = make_payload(476U * 4U, 0x22U, 128U);
        const rxtech::InterpretedPacketView final_view =
            ninth_packet_interpreter.interpret(make_data_packet(3U, 1U, 9U, 9U, payload));

        if (!final_view.valid || final_view.packet_index != 9U || final_view.iq_count != 476U ||
            final_view.zero_padding_bytes != 128U)
        {
            std::cerr << "expected ninth packet iq/padding rule, got valid=" << final_view.valid
                      << " packet_index=" << final_view.packet_index
                      << " iq_count=" << final_view.iq_count
                      << " zero_padding=" << final_view.zero_padding_bytes << '\n';
            return 1;
        }
    }

    {
        rxtech::ProtocolSequenceInterpreter invalid_padding_interpreter;
        auto invalid_last = make_payload(476U * 4U, 0x44U, 128U);
        invalid_last.back() = 0x7EU;

        const rxtech::InterpretedPacketView final_view =
            invalid_padding_interpreter.interpret(make_data_packet(4U, 2U, 11U, 9U, invalid_last));

        if (final_view.valid || final_view.reject_reason != rxtech::RejectReason::invalid_field_combo)
        {
            std::cerr << "expected invalid ninth-packet padding to be rejected, got valid=" << final_view.valid
                      << " reason=" << rxtech::reject_reason_name(final_view.reject_reason) << '\n';
            return 1;
        }
    }

    return 0;
}
