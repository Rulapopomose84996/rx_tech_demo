#include "rxtech/replay_sender.h"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <unordered_map>

namespace rxtech {

namespace {

std::vector<std::uint8_t> read_file_bytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open replay payload file: " + path);
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

const std::vector<std::uint8_t>& get_file_cache(const std::string& unit_dir,
                                                const std::string& file_name,
                                                std::unordered_map<std::string, std::vector<std::uint8_t>>& cache) {
    auto it = cache.find(file_name);
    if (it == cache.end()) {
        const std::string full_path = unit_dir + "/" + file_name;
        it = cache.emplace(file_name, read_file_bytes(full_path)).first;
    }
    return it->second;
}

}  // namespace

ReplayPlan build_replay_plan(const std::string& unit_dir, const ReplayManifest& manifest) {
    ReplayPlan plan;
    plan.datagrams.reserve(manifest.entries.size());

    std::unordered_map<std::string, std::vector<std::uint8_t>> file_cache;
    for (const ReplayEntry& entry : manifest.entries) {
        const std::vector<std::uint8_t>& source = get_file_cache(unit_dir, entry.file, file_cache);
        const std::uint64_t end_offset = entry.offset_bytes + entry.length_bytes;
        if (end_offset > source.size()) {
            throw std::runtime_error("replay entry exceeds source file bounds: " + entry.file);
        }

        ReplayDatagram datagram;
        datagram.sequence = entry.sequence;
        datagram.kind = entry.kind;
        datagram.cpi = entry.cpi;
        datagram.prt = entry.prt;
        datagram.channel = entry.channel;
        datagram.packet_index = entry.packet_index;
        datagram.payload.assign(source.begin() + static_cast<std::ptrdiff_t>(entry.offset_bytes),
                                source.begin() + static_cast<std::ptrdiff_t>(end_offset));
        plan.datagrams.push_back(std::move(datagram));
    }

    return plan;
}

ReplayTarget parse_replay_target(const std::string& target) {
    const std::size_t separator = target.rfind(':');
    if (separator == std::string::npos || separator == 0U || separator + 1U >= target.size()) {
        throw std::runtime_error("invalid replay target, expected host:port");
    }

    ReplayTarget parsed;
    parsed.host = target.substr(0U, separator);
    parsed.port = static_cast<std::uint16_t>(std::stoul(target.substr(separator + 1U)));
    if (parsed.port == 0U) {
        throw std::runtime_error("invalid replay target port");
    }
    return parsed;
}

}  // namespace rxtech
