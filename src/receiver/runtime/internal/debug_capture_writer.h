#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

#include "rxtech/rx_config.h"
#include "rxtech/sample_packet_parser.h"

namespace rxtech
{

    struct DebugCaptureRecord
    {
        std::uint16_t cpi = 0;
        std::uint16_t channel = 0;
        std::uint16_t prt = 0;
        std::uint16_t packet_index = 0;
        PacketKind kind = PacketKind::data_packet;
        bool valid = false;
        const std::uint8_t *payload_data = nullptr;
        std::size_t payload_len = 0;
    };

    struct DebugCaptureManifest
    {
        std::string policy;
        std::uint16_t selected_cpi = 0;
        bool selected = false;
    };

    class DebugCaptureWriter
    {
      public:
        DebugCaptureWriter(CapturePolicy policy, std::ostream *packet_stream, std::ostream *index_stream,
                           std::string run_dir);

        bool record(const DebugCaptureRecord &record);
        void finish();
        const DebugCaptureManifest &manifest() const noexcept;

      private:
        CapturePolicy policy_;
        std::ostream *packet_stream_ = nullptr;
        std::ostream *index_stream_ = nullptr;
        std::string run_dir_;
        DebugCaptureManifest manifest_{};
    };

} // namespace rxtech
