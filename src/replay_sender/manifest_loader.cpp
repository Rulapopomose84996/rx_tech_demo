#include "manifest_loader.h"

#include <algorithm>
#if defined(__linux__) || defined(__APPLE__)
#include <dirent.h>
#endif
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

// nlohmann/json is a header-only library vendored or fetched via CMake.
#include "nlohmann/json.hpp"

namespace rxtech::replay
{

    namespace
    {
        // ── helpers ──────────────────────────────────────────────────────────

        std::string join_path(const std::string &dir, const std::string &name)
        {
            if (dir.empty())
                return name;
            if (dir.back() == '/' || dir.back() == '\\')
                return dir + name;
            return dir + "/" + name;
        }

        // List JSON files in a directory that match a suffix.
        std::vector<std::string> glob_suffix(const std::string &dir, const std::string &suffix)
        {
#if defined(__linux__) || defined(__APPLE__)
            std::vector<std::string> result;
            DIR *d = opendir(dir.c_str());
            if (!d)
                return result;
            struct dirent *entry = nullptr;
            while ((entry = readdir(d)) != nullptr)
            {
                const std::string name(entry->d_name);
                if (name.size() >= suffix.size() &&
                    name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
                {
                    result.push_back(join_path(dir, name));
                }
            }
            closedir(d);
            std::sort(result.begin(), result.end());
            return result;
#else
            (void)dir;
            (void)suffix;
            return {};
#endif
        }

        // ── replay_manifest.json loader ──────────────────────────────────────

        std::vector<ReplayEntry> load_from_replay_manifest(const std::string &manifest_path,
                                                           const std::string &data_dir)
        {
            std::ifstream f(manifest_path);
            if (!f.is_open())
                throw std::runtime_error("无法打开回放清单文件: " + manifest_path);
            nlohmann::json j;
            f >> j;

            std::vector<ReplayEntry> entries;
            for (const auto &item : j.at("replay_sequence"))
            {
                ReplayEntry e;
                const std::string kind_str = item.at("kind").get<std::string>();
                e.kind =
                    (kind_str == "control_table") ? ReplayEntry::Kind::control_table : ReplayEntry::Kind::data_packet;

                const std::string rel_file = item.at("file").get<std::string>();
                e.bin_file = join_path(data_dir, rel_file);
                e.offset = item.at("offset_bytes").get<std::uint64_t>();
                e.length = item.at("length_bytes").get<std::uint32_t>();
                e.cpi = static_cast<std::uint16_t>(item.at("cpi").get<int>());

                if (e.kind == ReplayEntry::Kind::data_packet)
                {
                    e.prt = static_cast<std::uint16_t>(item.at("prt").get<int>());
                    e.channel = static_cast<std::uint16_t>(item.at("channel").get<int>());
                    e.packet_index = static_cast<std::uint16_t>(item.at("packet_index").get<int>());
                }
                entries.push_back(e);
            }
            return entries;
        }

        // ── metadata.json + packet_manifest.csv fallback ─────────────────────

        std::vector<ReplayEntry> load_from_metadata(const std::string &metadata_path, const std::string &data_dir)
        {
            std::ifstream mf(metadata_path);
            if (!mf.is_open())
                throw std::runtime_error("无法打开元数据文件: " + metadata_path);
            nlohmann::json meta;
            mf >> meta;

            const std::string ctrl_file = join_path(data_dir, meta.at("files").at("control_table").get<std::string>());
            const std::string data_file = join_path(data_dir, meta.at("files").at("data_payloads").get<std::string>());
            const std::string csv_file = join_path(data_dir, meta.at("files").at("packet_manifest").get<std::string>());

            // Control table entry (always first, offset 0).
            std::vector<ReplayEntry> entries;
            {
                ReplayEntry e;
                e.kind = ReplayEntry::Kind::control_table;
                e.bin_file = ctrl_file;
                e.offset = 0;
                e.length = 2048;
                e.cpi = static_cast<std::uint16_t>(meta.at("cpi").get<int>());
                entries.push_back(e);
            }

            // Parse CSV: sequence,cpi,prt,channel,channel_name,packet_index,iq_count,tail_hex
            std::ifstream csv(csv_file);
            if (!csv.is_open())
                throw std::runtime_error("无法打开数据包清单文件: " + csv_file);

            std::string line;
            std::getline(csv, line); // skip header

            std::uint64_t row = 0;
            while (std::getline(csv, line))
            {
                if (line.empty())
                    continue;
                std::istringstream ss(line);
                std::string token;
                std::vector<std::string> fields;
                while (std::getline(ss, token, ','))
                    fields.push_back(token);
                if (fields.size() < 6)
                    continue;

                ReplayEntry e;
                e.kind = ReplayEntry::Kind::data_packet;
                e.bin_file = data_file;
                e.offset = row * 2048ULL;
                e.length = 2048;
                e.cpi = static_cast<std::uint16_t>(std::stoi(fields[1]));
                e.prt = static_cast<std::uint16_t>(std::stoi(fields[2]));
                e.channel = static_cast<std::uint16_t>(std::stoi(fields[3]));
                e.packet_index = static_cast<std::uint16_t>(std::stoi(fields[5]));
                entries.push_back(e);
                ++row;
            }
            return entries;
        }

    } // anonymous namespace

    // ── public API ───────────────────────────────────────────────────────────

    std::vector<ReplayEntry> load_replay_entries(const std::string &data_dir)
    {
        // 1. Try replay_manifest.json
        const auto manifests = glob_suffix(data_dir, "_replay_manifest.json");
        if (!manifests.empty())
        {
            return load_from_replay_manifest(manifests.front(), data_dir);
        }

        // 2. Fallback: metadata.json
        const auto metas = glob_suffix(data_dir, "_metadata.json");
        if (!metas.empty())
        {
            return load_from_metadata(metas.front(), data_dir);
        }

        throw std::runtime_error("未找到回放清单或元数据文件: " + data_dir);
    }

    std::vector<ReplayEntry> load_replay_entries(const std::vector<std::string> &data_dirs)
    {
        std::vector<ReplayEntry> all;
        for (const auto &dir : data_dirs)
        {
            auto part = load_replay_entries(dir);
            all.insert(all.end(), part.begin(), part.end());
        }
        return all;
    }

} // namespace rxtech::replay
