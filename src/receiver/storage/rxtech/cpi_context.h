#pragma once

#include <array>
#include <cstdint>
#include <limits>

namespace rxtech
{

    constexpr std::uint16_t kCpiPrtMax = 64U;
    constexpr std::uint16_t kCpiChannelCount = 3U;
    constexpr std::uint16_t kCpiPacketsPerChannel = 9U;
    constexpr std::uint16_t kCpiSlotsPerPrt = kCpiChannelCount * kCpiPacketsPerChannel;
    constexpr std::uint32_t kCpiSlotStride = 2048U;
    constexpr std::uint32_t kCpiTotalSlots = static_cast<std::uint32_t>(kCpiPrtMax) * kCpiSlotsPerPrt;
    constexpr std::uint32_t kInvalidPoolIndex = std::numeric_limits<std::uint32_t>::max();

    enum class CpiState
    {
        ACTIVE,
        DECIDING,
        SEALED,
        TOMBSTONE,
        RECYCLED,
    };

    enum class CpiDecision
    {
        COMPLETE_OK,
        INCOMPLETE_BUT_COMMITTABLE,
        ABNORMAL_CUTOFF_COMMIT,
        DISCARD_INVALID,
    };

    enum CpiTriggerBits : std::uint32_t
    {
        TriggerNone = 0U,
        TriggerFullReady = 1U << 0U,
        TriggerTailObserved = 1U << 1U,
        TriggerCpiSwitch = 1U << 2U,
        TriggerWaveEnd = 1U << 3U,
        TriggerTimeout = 1U << 4U,
        TriggerStop = 1U << 5U,
    };

    enum PrtFlags : std::uint32_t
    {
        PrtFlagNone = 0U,
        PrtFlagSeenTail = 1U << 0U,
        PrtFlagComplete = 1U << 1U,
    };

    struct PrtSummary
    {
        std::array<std::uint16_t, kCpiChannelCount> ch_pkt_bitmap{};
        std::array<std::uint16_t, kCpiChannelCount> ch_recv_count{};
        std::uint16_t ready_channel_count = 0;
        std::uint16_t recv_packet_count = 0;
        std::uint32_t flags = PrtFlagNone;
    };

    struct CpiHotHeader
    {
        std::uint16_t cpi_id = 0;
        std::uint16_t wave_id = 0;
        std::uint16_t expected_n_prt = 0;
        std::uint16_t observed_n_prt = 0;
        std::uint32_t expected_slot_count = 0;
        std::uint32_t received_slot_count = 0;
        std::uint32_t duplicate_count = 0;
        std::uint32_t ready_prt_count = 0;
        std::uint64_t first_rx_tsc = 0;
        std::uint64_t last_rx_tsc = 0;
        std::uint64_t seal_tsc = 0;
        CpiState state = CpiState::RECYCLED;
        CpiDecision decision = CpiDecision::DISCARD_INVALID;
        std::uint32_t trigger_bits = TriggerNone;
        std::uint32_t pool_index = kInvalidPoolIndex;
    };

    struct CpiContext
    {
        CpiHotHeader header{};
        std::array<PrtSummary, kCpiPrtMax> prt_summary{};
        std::array<std::uint16_t, kCpiTotalSlots> slot_valid_bytes{};
        std::array<std::uint8_t, kCpiTotalSlots * kCpiSlotStride> payload{};

        void reset(std::uint16_t cpi_id = 0, std::uint32_t pool_index = kInvalidPoolIndex)
        {
            header = {};
            header.cpi_id = cpi_id;
            header.pool_index = pool_index;
            header.state = CpiState::ACTIVE;
            prt_summary.fill({});
            slot_valid_bytes.fill(0U);
            payload.fill(0U);
        }

        std::uint8_t *slot_payload(std::uint32_t index)
        {
            return payload.data() + (static_cast<std::size_t>(index) * kCpiSlotStride);
        }

        const std::uint8_t *slot_payload(std::uint32_t index) const
        {
            return payload.data() + (static_cast<std::size_t>(index) * kCpiSlotStride);
        }
    };

    inline bool is_valid_slot_coord(std::uint16_t prt, std::uint16_t channel, std::uint16_t packet_index)
    {
        return prt < kCpiPrtMax && channel < kCpiChannelCount && packet_index >= 1U &&
               packet_index <= kCpiPacketsPerChannel;
    }

    inline std::uint32_t slot_index(std::uint16_t prt, std::uint16_t channel, std::uint16_t packet_index)
    {
        return (static_cast<std::uint32_t>(prt) * kCpiChannelCount + channel) * kCpiPacketsPerChannel +
               (packet_index - 1U);
    }

    inline std::uint16_t packet_bit(std::uint16_t packet_index)
    {
        return static_cast<std::uint16_t>(1U << (packet_index - 1U));
    }

    inline void set_expected_prt_count(CpiContext &ctx, std::uint16_t count)
    {
        ctx.header.expected_n_prt = count;
        ctx.header.expected_slot_count = count == 0U ? 0U : static_cast<std::uint32_t>(count) * kCpiSlotsPerPrt;
    }

} // namespace rxtech
