#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rxtech/replay_manifest.h"

namespace rxtech {

struct ReplayDatagram {
    std::uint32_t sequence = 0;
    std::string kind;
    std::uint32_t cpi = 0;
    std::uint32_t prt = 0;
    std::uint32_t channel = 0;
    std::uint32_t packet_index = 0;
    std::vector<std::uint8_t> payload;
};

struct ReplayPlan {
    std::vector<ReplayDatagram> datagrams;
};

struct ReplayTarget {
    std::string host;
    std::uint16_t port = 0;
};

ReplayPlan build_replay_plan(const std::string& unit_dir, const ReplayManifest& manifest);
ReplayTarget parse_replay_target(const std::string& target);

}  // namespace rxtech
