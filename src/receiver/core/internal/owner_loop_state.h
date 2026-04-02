#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace rxtech
{

struct CapturedPacket
{
    std::vector<std::uint8_t> payload;
    std::uint16_t cpi = 0;
    std::uint16_t channel = 0;
    std::uint16_t prt = 0;
    std::uint16_t packet_index = 0;
    std::string packet_kind;
    bool valid = false;
};

struct ProtocolCpiStats
{
    std::uint64_t data_packets = 0;
    std::unordered_set<std::uint16_t> prts;
};

struct ProtocolChannelStats
{
    std::uint64_t data_packets = 0;
    std::uint64_t iq_count = 0;
};

using ProtocolPrtCoverage = std::map<std::uint16_t, std::unordered_set<std::uint16_t>>;

} // namespace rxtech
