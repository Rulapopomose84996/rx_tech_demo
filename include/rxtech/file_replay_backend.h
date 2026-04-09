#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rxtech/rx_backend.h"

namespace rxtech
{

    /// Options for the file-based replay backend.
    struct FileReplayOptions
    {
        std::vector<std::string> data_dirs; ///< Directories with sample data (loaded in order)
        std::uint32_t pps = 0;              ///< Target packets-per-second (0 = as-fast-as-possible)
        std::uint32_t loop_count = 1;       ///< How many times to replay the full sequence

        // Framing overrides (leave zero to use defaults from config)
        std::uint32_t src_ipv4_be = 0;
        std::uint32_t dst_ipv4_be = 0;
        std::uint16_t src_port = 0;
        std::uint16_t dst_port = 0;
    };

    /// IRxBackend implementation that replays pre-recorded binary samples.
    ///
    /// Does NOT depend on DPDK.  Designed for integration tests and the
    /// src/replay_sender pipeline, where a live network card is unavailable.
    ///
    /// All frames are pre-built in memory during init().  recv_burst() returns
    /// a window of the preloaded frames, optionally rate-limiting via pps.
    class FileReplayBackend final : public IRxBackend
    {
    public:
        explicit FileReplayBackend(FileReplayOptions opts);
        ~FileReplayBackend() override;

        std::string name() const override { return "file_replay"; }
        BackendInitResult init(const RxConfig &config) override;
        bool recv_burst(UdpDatagramBurst &burst, std::uint32_t max_burst) override;
        void release_burst(UdpDatagramBurst &burst) override;
        BackendStats stats() const override;
        void shutdown() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace rxtech
