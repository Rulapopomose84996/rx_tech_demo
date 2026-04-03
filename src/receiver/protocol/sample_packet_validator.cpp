#include "rxtech/sample_packet_validator.h"

namespace rxtech
{

    namespace
    {

        std::uint32_t control_table_payload_len(const ProtocolSpec &spec)
        {
            return spec.control_table_size > spec.packet_header_size ? (spec.control_table_size - spec.packet_header_size) : 0U;
        }
    } // namespace

    PacketValidity PacketValidator::validate(const ParsedPacketView &packet) const noexcept
    {
        if (!packet.valid)
        {
            return {false, packet.reject_reason == RejectReason::none ? RejectReason::invalid_header : packet.reject_reason};
        }

        if (packet.kind == PacketKind::control_table)
        {
            if (packet.payload_ptr == nullptr || packet.payload_len != control_table_payload_len(spec_))
            {
                return {false, RejectReason::invalid_len};
            }
            return {true, RejectReason::none};
        }

        if (packet.kind == PacketKind::data_packet)
        {
            if (packet.channel >= spec_.channels_per_prt)
            {
                return {false, RejectReason::invalid_channel};
            }
            if (packet.packet_index == 0U || packet.packet_index > spec_.packets_per_channel)
            {
                return {false, RejectReason::invalid_packet_index};
            }
            if (packet.payload_ptr == nullptr || packet.payload_len != spec_.packet_data_size)
            {
                return {false, RejectReason::invalid_len};
            }
            if (packet.tail != 0U && packet.tail != spec_.magic_tail)
            {
                return {false, RejectReason::invalid_tail};
            }
            // V-008 / C-001: tail marker only allowed on last packet in channel
            if (packet.tail == spec_.magic_tail &&
                packet.packet_index != spec_.packets_per_channel)
            {
                return {false, RejectReason::invalid_field_combo};
            }
            return {true, RejectReason::none};
        }

        return {false, RejectReason::invalid_field_combo};
    }

} // namespace rxtech
