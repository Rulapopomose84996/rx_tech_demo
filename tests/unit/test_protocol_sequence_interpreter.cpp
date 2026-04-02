#include <iostream>
#include <vector>

#include "rxtech/protocol_sequence_interpreter.h"

namespace {

std::vector<std::uint8_t> make_payload(std::uint32_t data_bytes, std::uint8_t fill_byte, std::uint32_t zero_padding_bytes) {
    std::vector<std::uint8_t> payload(data_bytes + zero_padding_bytes, fill_byte);
    for (std::size_t i = data_bytes; i < payload.size(); ++i) {
        payload[i] = 0U;
    }
    return payload;
}

rxtech::SamplePacketView make_data_packet(std::uint16_t cpi,
                                          std::uint16_t prt,
                                          const std::vector<std::uint8_t>& payload_storage) {
    rxtech::SamplePacketView packet;
    packet.valid = true;
    packet.kind = rxtech::SamplePacketKind::data_packet;
    packet.cpi = cpi;
    packet.prt = prt;
    packet.payload_ptr = payload_storage.data();
    packet.payload_len = static_cast<std::uint32_t>(payload_storage.size());
    return packet;
}

}  // namespace

int main() {
    rxtech::ProtocolSequenceInterpreter interpreter;

    {
        std::vector<std::vector<std::uint8_t>> payloads;
        payloads.reserve(27U);
        for (int i = 0; i < 27; ++i) {
            const bool is_ninth_packet = ((i + 1) % 9) == 0;
            payloads.push_back(is_ninth_packet
                                   ? make_payload(476U * 4U, 0xCDU, 128U)
                                   : make_payload(2032U, 0xABU, 0U));
        }

        for (std::size_t i = 0; i < payloads.size(); ++i) {
            const rxtech::ProtocolPacketView view = interpreter.interpret(make_data_packet(2U, 7U, payloads[i]));
            if (!view.valid) {
                std::cerr << "expected packet " << i << " to be valid, got " << view.error_reason << '\n';
                return 1;
            }

            const std::uint16_t expected_channel = static_cast<std::uint16_t>(i / 9U);
            const std::uint16_t expected_packet_index = static_cast<std::uint16_t>((i % 9U) + 1U);
            if (view.prt != 1U || view.channel != expected_channel || view.packet_index != expected_packet_index) {
                std::cerr << "unexpected channel/index at packet " << i
                          << " got prt=" << view.prt
                          << " got channel=" << view.channel
                          << " packet_index=" << view.packet_index << '\n';
                return 1;
            }
        }
    }

    {
        rxtech::ProtocolSequenceInterpreter prt_interpreter;
        std::vector<std::vector<std::uint8_t>> payloads;
        payloads.reserve(54U);
        for (int i = 0; i < 54; ++i) {
            const bool is_ninth_packet = ((i + 1) % 9) == 0;
            payloads.push_back(is_ninth_packet
                                   ? make_payload(476U * 4U, 0x55U, 128U)
                                   : make_payload(2032U, 0x66U, 0U));
        }

        for (std::size_t i = 0; i < payloads.size(); ++i) {
            const rxtech::ProtocolPacketView view = prt_interpreter.interpret(make_data_packet(5U, 99U, payloads[i]));
            const std::uint16_t expected_prt = static_cast<std::uint16_t>((i / 27U) + 1U);
            if (!view.valid || view.prt != expected_prt) {
                std::cerr << "expected logical prt " << expected_prt
                          << " at packet " << i
                          << ", got valid=" << view.valid
                          << " prt=" << view.prt
                          << " reason=" << view.error_reason << '\n';
                return 1;
            }
        }
    }

    {
        rxtech::ProtocolSequenceInterpreter ninth_packet_interpreter;
        std::vector<std::vector<std::uint8_t>> payloads;
        for (int i = 0; i < 8; ++i) {
            payloads.push_back(make_payload(2032U, 0x11U, 0U));
        }
        payloads.push_back(make_payload(476U * 4U, 0x22U, 128U));

        rxtech::ProtocolPacketView final_view;
        for (const auto& payload : payloads) {
            final_view = ninth_packet_interpreter.interpret(make_data_packet(3U, 9U, payload));
        }

        if (!final_view.valid || final_view.packet_index != 9U || final_view.iq_count != 476U ||
            final_view.zero_padding_bytes != 128U) {
            std::cerr << "expected ninth packet iq/padding rule, got valid=" << final_view.valid
                      << " packet_index=" << final_view.packet_index
                      << " iq_count=" << final_view.iq_count
                      << " zero_padding=" << final_view.zero_padding_bytes << '\n';
            return 1;
        }
    }

    {
        rxtech::ProtocolSequenceInterpreter invalid_padding_interpreter;
        std::vector<std::vector<std::uint8_t>> payloads;
        for (int i = 0; i < 8; ++i) {
            payloads.push_back(make_payload(2032U, 0x33U, 0U));
        }
        auto invalid_last = make_payload(476U * 4U, 0x44U, 128U);
        invalid_last.back() = 0x7EU;
        payloads.push_back(invalid_last);

        rxtech::ProtocolPacketView final_view;
        for (const auto& payload : payloads) {
            final_view = invalid_padding_interpreter.interpret(make_data_packet(4U, 11U, payload));
        }

        if (final_view.valid || final_view.error_reason != "ninth packet zero padding is not all zero") {
            std::cerr << "expected invalid ninth-packet padding to be rejected, got valid=" << final_view.valid
                      << " reason=" << final_view.error_reason << '\n';
            return 1;
        }
    }

    return 0;
}
