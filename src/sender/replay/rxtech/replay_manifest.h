#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rxtech {

struct ReplayEntry {
    std::uint32_t sequence = 0;
    std::string kind;
    std::string file;
    std::uint64_t offset_bytes = 0;
    std::uint64_t length_bytes = 0;
    std::uint32_t cpi = 0;
    std::uint32_t prt = 0;
    std::uint32_t channel = 0;
    std::string channel_name;
    std::uint32_t packet_index = 0;
};

struct ReplayManifest {
    std::uint32_t format_version = 0;
    std::string sample_type;
    std::uint32_t cpi = 0;
    std::uint32_t total_udp_units = 0;
    std::vector<ReplayEntry> entries;
};

ReplayManifest load_replay_manifest(const std::string& path);

}  // namespace rxtech
