#include "rxtech/sample_packet_parser.h"

#include "rxtech/byte_order.h"

namespace rxtech
{

    namespace
    {

        constexpr std::uint16_t kEtherTypeIpv4 = 0x0800U;
        constexpr std::uint8_t kIpProtoUdp = 17U;

        std::uint32_t resolve_payload_offset(const PacketDesc &packet)
        {
            if (packet.len >= 14U)
            {
                const std::uint16_t ether_type = byte_order::read_u16_be(packet.data + 12U);
                if (ether_type == kEtherTypeIpv4 && packet.len >= 42U)
                {
                    const std::uint8_t version_ihl = packet.data[14U];
                    const std::uint8_t version = static_cast<std::uint8_t>((version_ihl >> 4U) & 0x0FU);
                    const std::uint8_t ihl_words = static_cast<std::uint8_t>(version_ihl & 0x0FU);
                    const std::uint32_t ip_header_bytes = static_cast<std::uint32_t>(ihl_words) * 4U;
                    if (version == 4U && ihl_words >= 5U && packet.len >= 14U + ip_header_bytes + 8U &&
                        packet.data[23U] == kIpProtoUdp)
                    {
                        return 14U + ip_header_bytes + 8U;
                    }
                }
            }
            return 0U;
        }

    } // namespace

    const char *packet_kind_name(PacketKind kind) noexcept
    {
        switch (kind)
        {
        case PacketKind::control_table:
            return "control_table";
        case PacketKind::data_packet:
            return "data_packet";
        case PacketKind::unknown:
            return "unknown";
        }
        return "unknown";
    }

    const char *reject_reason_name(RejectReason reason) noexcept
    {
        switch (reason)
        {
        case RejectReason::none:
            return "none";
        case RejectReason::invalid_len:
            return "invalid_len";
        case RejectReason::invalid_header:
            return "invalid_header";
        case RejectReason::invalid_channel:
            return "invalid_channel";
        case RejectReason::invalid_prt:
            return "invalid_prt";
        case RejectReason::invalid_packet_index:
            return "invalid_packet_index";
        case RejectReason::invalid_tail:
            return "invalid_tail";
        case RejectReason::invalid_field_combo:
            return "invalid_field_combo";
        case RejectReason::truncated_datagram:
            return "truncated_datagram";
        }
        return "invalid_field_combo";
    }

    ParsedPacketView PacketParser::parse(const PacketDesc &packet) const noexcept
    {
        ParsedPacketView parsed;
        if (packet.data == nullptr)
        {
            parsed.reject_reason = RejectReason::invalid_len;
            return parsed;
        }

        const std::uint32_t payload_offset = resolve_payload_offset(packet);
        if (payload_offset == 0U)
        {
            parsed.reject_reason = RejectReason::invalid_header;
            return parsed;
        }
        if (packet.len < payload_offset + spec_.packet_header_size)
        {
            parsed.reject_reason = RejectReason::invalid_len;
            return parsed;
        }

        const std::uint8_t *payload = packet.data + payload_offset;
        const std::uint32_t magic = byte_order::read_u32_le(payload + 0U);
        parsed.cpi = byte_order::read_u16_le(payload + 4U);
        parsed.rx_tsc = packet.ts_ns;

        if (magic == spec_.magic_control)
        {
            parsed.kind = PacketKind::control_table;
            parsed.payload_ptr = payload + spec_.packet_header_size;
            parsed.payload_len = packet.len - payload_offset - spec_.packet_header_size;
            parsed.valid = true;
            return parsed;
        }

        if (magic == spec_.magic_data)
        {
            parsed.kind = PacketKind::data_packet;
            parsed.channel = byte_order::read_u16_le(payload + 6U);
            parsed.prt = byte_order::read_u16_le(payload + 8U);
            parsed.packet_index = byte_order::read_u16_le(payload + 10U);
            parsed.tail = byte_order::read_u32_le(payload + 12U);
            parsed.payload_ptr = payload + spec_.packet_header_size;
            parsed.payload_len = packet.len - payload_offset - spec_.packet_header_size;
            parsed.valid = true;
            return parsed;
        }

        parsed.reject_reason = RejectReason::invalid_header;
        return parsed;
    }

    ParsedPacketView PacketParser::parse(const UdpPayloadFrame &frame) const noexcept
    {
        ParsedPacketView parsed;
        if (frame.udp_payload.size() < spec_.packet_header_size)
        {
            parsed.reject_reason = RejectReason::invalid_len;
            return parsed;
        }

        const std::uint8_t *payload = frame.udp_payload.data();
        const std::uint32_t magic = byte_order::read_u32_le(payload + 0U);
        parsed.cpi = byte_order::read_u16_le(payload + 4U);

        if (magic == spec_.magic_control)
        {
            parsed.kind = PacketKind::control_table;
            parsed.payload_ptr = payload + spec_.packet_header_size;
            parsed.payload_len = static_cast<std::uint32_t>(frame.udp_payload.size() - spec_.packet_header_size);
            parsed.valid = true;
            return parsed;
        }

        if (magic == spec_.magic_data)
        {
            parsed.kind = PacketKind::data_packet;
            parsed.channel = byte_order::read_u16_le(payload + 6U);
            parsed.prt = byte_order::read_u16_le(payload + 8U);
            parsed.packet_index = byte_order::read_u16_le(payload + 10U);
            parsed.tail = byte_order::read_u32_le(payload + 12U);
            parsed.payload_ptr = payload + spec_.packet_header_size;
            parsed.payload_len = static_cast<std::uint32_t>(frame.udp_payload.size() - spec_.packet_header_size);
            parsed.valid = true;
            return parsed;
        }

        parsed.reject_reason = RejectReason::invalid_header;
        return parsed;
    }

} // namespace rxtech
