#include <cassert>
#include <cstdint>
#include <vector>

#include "rxtech/demo_protocol.h"
#include "rxtech/parser.h"
#include "rxtech/packet_desc.h"
#include "rxtech/reassembly.h"

namespace {

void append_u16_be(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u32_be(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u16_le(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void append_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

void append_u64_le(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 0; shift <= 56; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

std::vector<std::uint8_t> make_fragment(std::uint64_t block_id,
                                        std::uint16_t frag_idx,
                                        std::uint16_t frag_count,
                                        std::uint16_t flags,
                                        const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(rxtech::kDemoHeaderWireBytes + payload.size());
    append_u32_be(bytes, rxtech::kDemoMagic);
    append_u16_le(bytes, rxtech::kDemoVersion);
    append_u16_le(bytes, flags);
    append_u32_le(bytes, 7U);
    append_u64_le(bytes, block_id);
    append_u32_le(bytes, 0U);
    for (std::uint16_t index = 0; index < frag_count; ++index) {
        if (index == frag_idx) {
            bytes[20] = 0;  // no-op placeholder to keep layout obvious for test readers
        }
    }
    append_u16_le(bytes, frag_idx);
    append_u16_le(bytes, frag_count);
    append_u16_le(bytes, static_cast<std::uint16_t>(payload.size()));
    append_u16_le(bytes, 0U);
    bytes.insert(bytes.end(), payload.begin(), payload.end());

    const std::uint32_t block_bytes = static_cast<std::uint32_t>(payload.size()) * frag_count;
    bytes[20] = static_cast<std::uint8_t>(block_bytes & 0xFFU);
    bytes[21] = static_cast<std::uint8_t>((block_bytes >> 8U) & 0xFFU);
    bytes[22] = static_cast<std::uint8_t>((block_bytes >> 16U) & 0xFFU);
    bytes[23] = static_cast<std::uint8_t>((block_bytes >> 24U) & 0xFFU);
    return bytes;
}

rxtech::PacketDesc make_packet(std::vector<std::uint8_t>& bytes, std::uint32_t port_id, std::uint64_t ts_ns) {
    rxtech::PacketDesc packet;
    packet.data = bytes.data();
    packet.len = static_cast<std::uint32_t>(bytes.size());
    packet.port_id = port_id;
    packet.ts_ns = ts_ns;
    return packet;
}

rxtech::ParsedPacketMeta parse(std::vector<std::uint8_t>& bytes, std::uint32_t port_id, std::uint64_t ts_ns) {
    const rxtech::PacketDesc packet = make_packet(bytes, port_id, ts_ns);
    return rxtech::parse_packet(packet);
}

}  // namespace

int main() {
    {
        rxtech::BlockReassembler reassembler(5U);
        std::vector<std::uint8_t> first = make_fragment(10U, 0U, 2U, rxtech::kDemoFlagFirstFragment, {1U, 2U});
        std::vector<std::uint8_t> second = make_fragment(10U, 1U, 2U, rxtech::kDemoFlagLastFragment, {3U, 4U});
        const rxtech::PacketDesc first_packet = make_packet(first, 0U, 1'000'000U);
        const rxtech::PacketDesc second_packet = make_packet(second, 0U, 1'100'000U);

        const auto pending = reassembler.push(first_packet, rxtech::parse_packet(first_packet));
        assert(pending.accepted);
        assert(!pending.complete);

        const auto complete = reassembler.push(second_packet, rxtech::parse_packet(second_packet));
        assert(complete.accepted);
        assert(complete.complete);
        assert(complete.block.port_id == 0U);
        assert(complete.block.block_id == 10U);
        assert(complete.block.payload.size() == 4U);
        assert(complete.block.payload[0] == 1U);
        assert(complete.block.payload[3] == 4U);
    }

    {
        rxtech::BlockReassembler reassembler(5U);
        std::vector<std::uint8_t> second = make_fragment(11U, 1U, 2U, rxtech::kDemoFlagLastFragment, {3U, 4U});
        std::vector<std::uint8_t> first = make_fragment(11U, 0U, 2U, rxtech::kDemoFlagFirstFragment, {1U, 2U});
        const auto second_result = reassembler.push(make_packet(second, 1U, 2'000'000U), parse(second, 1U, 2'000'000U));
        assert(second_result.accepted);
        assert(!second_result.complete);
        const auto first_result = reassembler.push(make_packet(first, 1U, 2'100'000U), parse(first, 1U, 2'100'000U));
        assert(first_result.complete);
        assert(first_result.block.port_id == 1U);
        assert(first_result.block.payload[0] == 1U);
        assert(first_result.block.payload[3] == 4U);
    }

    {
        rxtech::BlockReassembler reassembler(5U);
        std::vector<std::uint8_t> first = make_fragment(12U, 0U, 2U, rxtech::kDemoFlagFirstFragment, {1U, 2U});
        const auto first_result = reassembler.push(make_packet(first, 0U, 3'000'000U), parse(first, 0U, 3'000'000U));
        assert(first_result.accepted);
        const auto duplicate = reassembler.push(make_packet(first, 0U, 3'100'000U), parse(first, 0U, 3'100'000U));
        assert(duplicate.accepted);
        assert(duplicate.duplicate_fragment);
    }

    {
        rxtech::BlockReassembler reassembler(5U);
        std::vector<std::uint8_t> first = make_fragment(13U, 0U, 3U, rxtech::kDemoFlagFirstFragment, {1U, 2U});
        const auto first_result = reassembler.push(make_packet(first, 2U, 4'000'000U), parse(first, 2U, 4'000'000U));
        assert(first_result.accepted);
        const rxtech::ReassemblyExpiry expiry = reassembler.expire_before(10'000'000U);
        assert(expiry.expired_blocks == 1U);
        assert(expiry.missing_fragments == 2U);
    }

    {
        rxtech::BlockReassembler reassembler(5U);
        std::vector<std::uint8_t> conflict =
            make_fragment(14U, 1U, 2U, rxtech::kDemoFlagFirstFragment | rxtech::kDemoFlagLastFragment, {5U, 6U});
        const auto result = reassembler.push(make_packet(conflict, 0U, 5'000'000U), parse(conflict, 0U, 5'000'000U));
        assert(result.accepted);
        assert(result.flag_conflict);
    }

    return 0;
}
