#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "rxtech/packet_desc.h"

namespace rxtech
{

    struct RxConfig;

    struct RawFrameRecorderStats
    {
        std::uint64_t written_frames = 0;
        std::uint64_t written_bytes = 0;
        std::uint64_t dropped_frames = 0;
        std::uint64_t dropped_bytes = 0;
        std::uint64_t retained_bytes = 0;
        std::uint64_t queue_high_watermark = 0;
        std::string latest_file_path;
    };

    class RawFrameRecorder
    {
      public:
        explicit RawFrameRecorder(const RxConfig &config);
        ~RawFrameRecorder();

        void start();
        void stop();
        void submit(const PacketDesc &packet);
        bool enabled() const;
        const std::string &output_dir() const;
        RawFrameRecorderStats snapshot() const;
        std::string error_message() const;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace rxtech
