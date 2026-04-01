#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "rxtech/parser.h"
#include "rxtech/packet_desc.h"

namespace rxtech {

struct ReassembledBlock {
    std::uint32_t port_id = 0;
    std::uint32_t stream_id = 0;
    std::uint64_t block_id = 0;
    std::uint32_t block_bytes = 0;
    std::vector<std::uint8_t> payload;
};

struct ReassemblyPushResult {
    bool accepted = false;
    bool complete = false;
    bool duplicate_fragment = false;
    bool flag_conflict = false;
    ReassembledBlock block;
};

struct ReassemblyExpiry {
    std::uint64_t expired_blocks = 0;
    std::uint64_t missing_fragments = 0;
    std::unordered_map<std::uint32_t, std::uint64_t> expired_blocks_by_port;
    std::unordered_map<std::uint32_t, std::uint64_t> missing_fragments_by_port;
};

class BlockReassembler {
public:
    explicit BlockReassembler(std::uint32_t timeout_ms = 1000U);

    ReassemblyPushResult push(const PacketDesc& packet, const ParsedPacketMeta& meta);
    ReassemblyExpiry expire_before(std::uint64_t now_ns);

private:
    struct BlockKey {
        std::uint32_t port_id = 0;
        std::uint64_t block_id = 0;

        bool operator==(const BlockKey& other) const {
            return port_id == other.port_id && block_id == other.block_id;
        }
    };

    struct BlockKeyHash {
        std::size_t operator()(const BlockKey& key) const;
    };

    struct BlockState {
        std::uint32_t stream_id = 0;
        std::uint32_t block_bytes = 0;
        std::uint16_t frag_count = 0;
        std::uint64_t last_update_ns = 0;
        std::uint32_t received_fragments = 0;
        std::uint32_t received_payload_bytes = 0;
        std::vector<bool> present;
        std::vector<std::vector<std::uint8_t>> fragments;
    };

    std::uint64_t timeout_ns_ = 0;
    std::unordered_map<BlockKey, BlockState, BlockKeyHash> blocks_;
};

}  // namespace rxtech
