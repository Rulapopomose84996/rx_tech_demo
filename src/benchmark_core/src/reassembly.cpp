#include "rxtech/reassembly.h"

#include <algorithm>

#include "rxtech/time_utils.h"

namespace rxtech {

namespace {

bool has_flag(std::uint16_t flags, std::uint16_t bit) {
    return (flags & bit) != 0U;
}

}  // namespace

BlockReassembler::BlockReassembler(std::uint32_t timeout_ms)
    : timeout_ns_(static_cast<std::uint64_t>(timeout_ms) * 1'000'000ULL) {
}

std::size_t BlockReassembler::BlockKeyHash::operator()(const BlockKey& key) const {
    return std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(key.port_id) << 32U) ^ key.block_id);
}

ReassemblyPushResult BlockReassembler::push(const PacketDesc& packet, const ParsedPacketMeta& meta) {
    ReassemblyPushResult result;
    if (!meta.valid || packet.data == nullptr || packet.len < meta.payload_offset) {
        return result;
    }

    const std::uint64_t now_ns = packet.ts_ns != 0U ? packet.ts_ns : steady_clock_now_ns();

    const BlockKey key{meta.port_id, meta.block_id};
    auto [it, inserted] = blocks_.try_emplace(key);
    BlockState& state = it->second;
    if (inserted) {
        state.stream_id = meta.stream_id;
        state.block_bytes = meta.block_bytes;
        state.frag_count = meta.frag_count;
        state.last_update_ns = now_ns;
        state.present.assign(meta.frag_count, false);
        state.fragments.resize(meta.frag_count);
    } else {
        state.last_update_ns = now_ns;
    }

    result.accepted = true;
    if (state.frag_count != meta.frag_count || state.block_bytes != meta.block_bytes || state.stream_id != meta.stream_id) {
        return result;
    }

    if ((has_flag(meta.flags, kDemoFlagFirstFragment) && meta.frag_idx != 0U) ||
        (has_flag(meta.flags, kDemoFlagLastFragment) && meta.frag_idx + 1U != meta.frag_count)) {
        result.flag_conflict = true;
    }

    if (state.present[meta.frag_idx]) {
        result.duplicate_fragment = true;
        return result;
    }

    const std::uint8_t* payload = packet.data + meta.payload_offset;
    state.fragments[meta.frag_idx].assign(payload, payload + meta.frag_payload_bytes);
    state.present[meta.frag_idx] = true;
    ++state.received_fragments;
    state.received_payload_bytes += meta.frag_payload_bytes;

    if (state.received_fragments != state.frag_count || state.received_payload_bytes != state.block_bytes) {
        return result;
    }

    result.complete = true;
    result.block.port_id = meta.port_id;
    result.block.stream_id = meta.stream_id;
    result.block.block_id = meta.block_id;
    result.block.block_bytes = meta.block_bytes;
    result.block.payload.reserve(state.received_payload_bytes);
    for (const std::vector<std::uint8_t>& fragment : state.fragments) {
        result.block.payload.insert(result.block.payload.end(), fragment.begin(), fragment.end());
    }
    blocks_.erase(it);
    return result;
}

ReassemblyExpiry BlockReassembler::expire_before(std::uint64_t now_ns) {
    ReassemblyExpiry expiry;
    if (timeout_ns_ == 0U) {
        return expiry;
    }

    for (auto it = blocks_.begin(); it != blocks_.end();) {
        const bool expired = now_ns >= it->second.last_update_ns && (now_ns - it->second.last_update_ns) >= timeout_ns_;
        if (!expired) {
            ++it;
            continue;
        }

        ++expiry.expired_blocks;
        const std::uint64_t missing = static_cast<std::uint64_t>(it->second.frag_count - it->second.received_fragments);
        expiry.missing_fragments += missing;
        ++expiry.expired_blocks_by_port[it->first.port_id];
        expiry.missing_fragments_by_port[it->first.port_id] += missing;
        it = blocks_.erase(it);
    }

    return expiry;
}

}  // namespace rxtech
