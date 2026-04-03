#pragma once

#include <cstdint>

#include "rxtech/cpi_context.h"
#include "rxtech/protocol_spec.h"
#include "rxtech/sample_packet_parser.h"

namespace rxtech
{

    struct SlotWriteResult
    {
        bool first_write = false;
        bool duplicate = false;
        std::uint32_t slot_index = 0;
        RejectReason reason = RejectReason::none;
    };

    class SlotWriter
    {
    public:
        SlotWriter() = default;
        explicit SlotWriter(const ProtocolSpec &spec) : spec_(spec) {}

        SlotWriteResult write(CpiContext &ctx, const ParsedPacketView &packet) const noexcept;

    private:
        ProtocolSpec spec_{};
    };

} // namespace rxtech
