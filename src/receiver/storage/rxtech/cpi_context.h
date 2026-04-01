#pragma once

#include <cstdint>
#include <vector>

namespace rxtech {

// The current demo protocol does not expose a dedicated CPI field yet.
// For the incremental receiver skeleton, block_id acts as the current CPI key.
struct CpiContext {
    std::uint64_t cpi_id = 0;
    std::uint32_t port_id = 0;
    std::uint32_t stream_id = 0;
    std::uint16_t expected_frag_count = 0;
    std::uint32_t expected_block_bytes = 0;
    std::uint32_t received_fragments = 0;
    std::uint32_t received_payload_bytes = 0;
    std::uint64_t first_packet_ts_ns = 0;
    std::uint64_t last_packet_ts_ns = 0;
    bool sealed = false;
    std::vector<bool> slot_received;
    std::vector<std::vector<std::uint8_t>> slot_payloads;
};

}  // namespace rxtech
