#pragma once

#include <cstdint>
#include <optional>

#include "rxtech/cpi_context.h"

namespace rxtech
{

    struct CpiReadOnlyView
    {
        const std::uint8_t *payload_base =
            nullptr; ///< 生存期绑定到源 CpiContext；matching ReleaseToken 被回收后立即失效
        const std::uint16_t *slot_valid_bytes =
            nullptr;                             ///< 生存期绑定到源 CpiContext；matching ReleaseToken 被回收后立即失效
        const PrtSummary *prt_summary = nullptr; ///< 生存期绑定到源 CpiContext；matching ReleaseToken 被回收后立即失效
        std::uint32_t n_prt = 0;
        std::uint32_t slot_count = 0;
        std::uint32_t payload_stride = kCpiSlotStride;
    };

    struct CpiOutput
    {
        std::uint16_t cpi_id = 0;
        ControlSnapshot control{};
        CpiDecision decision = CpiDecision::DISCARD_INVALID;
        std::uint32_t received_slot_count = 0;
        std::uint32_t missing_slot_count = 0;
        std::uint32_t duplicate_count = 0;
        std::uint32_t ready_prt_count = 0;
        std::uint32_t trigger_bits = TriggerNone;
        std::uint64_t first_rx_tsc = 0;
        std::uint64_t last_rx_tsc = 0;
        std::uint64_t seal_tsc = 0;
        CpiReadOnlyView view{};
        std::uint32_t pool_index = kInvalidPoolIndex;
        std::uint64_t output_id = 0;
    };

    struct ReleaseToken
    {
        std::uint64_t output_id = 0;
        std::uint32_t ctx_pool_index = kInvalidPoolIndex;
    };

    class CpiFinalizer
    {
      public:
        std::optional<CpiOutput> try_finalize(CpiContext &ctx, std::uint32_t trigger) const;
    };

} // namespace rxtech
