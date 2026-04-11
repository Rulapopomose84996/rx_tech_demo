#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rxtech::replay
{

    /// One unit of replay work: enough to locate a raw 2048-byte UDP payload in a
    /// binary file.  The loader fills this from either an existing replay_manifest.json
    /// or from a metadata.json + packet_manifest.csv pair.
    struct ReplayEntry
    {
        enum class Kind : std::uint8_t
        {
            control_table,
            data_packet,
        };

        Kind kind = Kind::data_packet;
        std::string bin_file;     ///< Absolute path to the binary file
        std::uint64_t offset = 0; ///< Byte offset within bin_file
        std::uint32_t length = 0; ///< Payload length in bytes (typically 2048)

        // Metadata for framing / diagnostics
        std::uint16_t cpi = 0;
        std::uint16_t prt = 0;
        std::uint16_t channel = 0;
        std::uint16_t packet_index = 0;
    };

    /// Load replay sequence for all CPIs found in @p data_dir.
    ///
    /// Priority:
    ///   1. *_replay_manifest.json   (pre-built replay sequence, may span multiple CPIs)
    ///   2. *_metadata.json + *_packet_manifest.csv  (reconstructed; offset = row * 2048)
    ///
    /// Multiple CPI directories can be passed; entries are appended in order.
    ///
    /// @param data_dirs  One or more directories, each containing one CPI sample set.
    /// @throws std::runtime_error on unreadable or malformed files.
    std::vector<ReplayEntry> load_replay_entries(const std::vector<std::string> &data_dirs);

    /// Convenience overload for a single directory.
    std::vector<ReplayEntry> load_replay_entries(const std::string &data_dir);

} // namespace rxtech::replay
