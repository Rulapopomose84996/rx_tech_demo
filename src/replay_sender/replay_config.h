#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rxtech::replay
{

    struct ReplaySenderConfig
    {
        std::vector<std::string> data_dirs; ///< One or more sample directories

        // Network (AF_PACKET)
        std::string interface; ///< e.g. "eth0", "lo"
        std::string dest_ip;   ///< Destination IP (filled into IPv4 dst)
        std::string src_ip;    ///< Source IP (optional; default loopback sender)
        std::uint16_t dest_port = 9999U;
        std::uint16_t src_port = 30001U;

        // Replay control
        std::uint32_t pps = 1000U;     ///< Packets per second (0 = unlimited)
        std::uint32_t loop_count = 1U; ///< Number of replay loops

        // Verbosity
        bool verbose = false;
    };

    /// Parse command-line arguments into ReplaySenderConfig.
    /// Returns false and prints usage if arguments are invalid.
    bool parse_replay_args(int argc, char **argv, ReplaySenderConfig &out_cfg);

    void print_replay_usage(const char *argv0);

} // namespace rxtech::replay
