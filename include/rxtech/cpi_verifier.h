#pragma once

#include <cstdint>
#include <string>

#include "rxtech/cpi_finalizer.h"
#include "rxtech/protocol_spec.h"

namespace rxtech
{

    /// Bitmask error flags for CpiVerificationResult.
    /// Multiple flags can be set simultaneously.
    enum class CpiVerifyError : std::uint32_t
    {
        kNone = 0U,

        /// CpiDecision is not COMPLETE_OK (ABNORMAL or INCOMPLETE).
        kAbnormalFinalize = 1U << 0U,

        /// decision is INCOMPLETE_BUT_COMMITTABLE (soft premature stop).
        kPrematureFinalize = 1U << 1U,

        /// One or more expected PRTs are missing (ready_prt_count < expected).
        kMissingPrt = 1U << 2U,

        /// One or more PRTs have fewer ready channels than expected.
        kChannelCoverage = 1U << 3U,

        /// One or more packets are missing (missing_slot_count > 0 or
        /// per-channel recv count < packets_per_channel).
        kMissingPacket = 1U << 4U,

        /// One or more duplicate packets were detected.
        kDuplicatePacket = 1U << 5U,
    };

    inline CpiVerifyError operator|(CpiVerifyError a, CpiVerifyError b) noexcept
    {
        return static_cast<CpiVerifyError>(
            static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }

    inline CpiVerifyError &operator|=(CpiVerifyError &a, CpiVerifyError b) noexcept
    {
        a = a | b;
        return a;
    }

    inline bool has_error(CpiVerifyError flags, CpiVerifyError bit) noexcept
    {
        return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(bit)) != 0U;
    }

    struct CpiVerificationResult
    {
        bool passed = false;
        CpiVerifyError error_flags = CpiVerifyError::kNone;

        /// Human-readable first failure reason (empty if passed).
        std::string reason_text;

        // Diagnostic counts
        std::uint32_t missing_slot_count = 0;
        std::uint32_t duplicate_count = 0;
        std::uint32_t missing_prt_count = 0;     ///< PRTs not fully ready
        std::uint32_t bad_channel_prt_count = 0; ///< PRTs with channel coverage < expected
    };

    /// Verifies a CpiOutput against its bound control snapshot, with ProtocolSpec
    /// retained as a fallback when legacy or synthetic outputs omit control data.
    ///
    /// Checks (in order):
    ///   1. decision == COMPLETE_OK
    ///   2. missing_slot_count == 0
    ///   3. duplicate_count == 0
    ///   4. ready_prt_count == expected n_prt
    ///   5. Per-PRT channel coverage (ready_channel_count == control.channel_count)
    ///   6. Per-channel packet count (ch_recv_count[ch] == control.packets_per_channel)
    ///
    /// All applicable errors are accumulated; the result is only passed=true when
    /// no errors are found.
    class CpiVerifier
    {
    public:
        CpiVerificationResult verify(const CpiOutput &output,
                                     const ProtocolSpec &spec) const noexcept;
    };

} // namespace rxtech
