#pragma once

#include <cstddef>
#include <cstdint>

namespace rxtech {

constexpr std::uint32_t kDemoMagic = 0x54504458U;  // "TPDX"
constexpr std::uint16_t kDemoVersion = 1U;
constexpr std::uint16_t kDemoFlagFirstFragment = 0x0001U;
constexpr std::uint16_t kDemoFlagLastFragment = 0x0002U;
constexpr std::size_t kDemoHeaderWireBytes = 32U;

struct DemoHeader {
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint32_t stream_id = 0;
    std::uint64_t block_id = 0;
    std::uint32_t block_bytes = 0;
    std::uint16_t frag_idx = 0;
    std::uint16_t frag_count = 0;
    std::uint16_t frag_payload_bytes = 0;
};

}  // namespace rxtech
