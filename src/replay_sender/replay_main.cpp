#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <vector>

#include "af_packet_sender.h"
#include "frame_builder.h"
#include "manifest_loader.h"
#include "pps_controller.h"
#include "replay_config.h"

int main(int argc, char **argv)
{
    rxtech::replay::ReplaySenderConfig cfg;
    if (!rxtech::replay::parse_replay_args(argc, argv, cfg))
    {
        rxtech::replay::print_replay_usage(argv[0]);
        return 1;
    }

    rxtech::replay::FrameConfig fcfg;
    {
        unsigned a{}, b{}, c{}, d{};
        if (std::sscanf(cfg.dest_ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4)
            fcfg.dst_ipv4_be = (a << 24U) | (b << 16U) | (c << 8U) | d;
        if (!cfg.src_ip.empty() &&
            std::sscanf(cfg.src_ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4)
            fcfg.src_ipv4_be = (a << 24U) | (b << 16U) | (c << 8U) | d;
        fcfg.dst_port = cfg.dest_port;
        fcfg.src_port = cfg.src_port;

        std::array<std::uint8_t, 6> parsed_mac{};
        if (!cfg.src_mac.empty())
        {
            if (!rxtech::replay::parse_mac_address(cfg.src_mac, parsed_mac))
            {
                std::fprintf(stderr, "--src-mac 格式无效: %s\n", cfg.src_mac.c_str());
                return 1;
            }
            fcfg.src_mac = parsed_mac;
        }
        if (!cfg.dst_mac.empty())
        {
            if (!rxtech::replay::parse_mac_address(cfg.dst_mac, parsed_mac))
            {
                std::fprintf(stderr, "--dst-mac 格式无效: %s\n", cfg.dst_mac.c_str());
                return 1;
            }
            fcfg.dst_mac = parsed_mac;
        }
    }

    std::vector<rxtech::replay::ReplayEntry> entries;
    try
    {
        entries = rxtech::replay::load_replay_entries(cfg.data_dirs);
    }
    catch (const std::exception &ex)
    {
        std::fprintf(stderr, "加载回放数据失败: %s\n", ex.what());
        return 1;
    }

    if (entries.empty())
    {
        std::fprintf(stderr, "在指定目录中未找到可回放数据条目。\n");
        return 1;
    }

    rxtech::replay::AfPacketSender sender(cfg.interface);
    rxtech::replay::PpsController rate(cfg.pps);

    const std::uint64_t total_entries = entries.size();
    if (cfg.verbose)
    {
        std::fprintf(stdout, "[rx_replay_sender] %zu entries, %u loop(s), pps=%u, iface=%s\n",
                     entries.size(), cfg.loop_count, cfg.pps, cfg.interface.c_str());
        std::fflush(stdout);
    }

    std::uint64_t sent_total = 0;

    for (std::uint32_t loop = 0; loop < cfg.loop_count; ++loop)
    {
        if (cfg.verbose && cfg.loop_count > 1)
        {
            std::fprintf(stdout, "[rx_replay_sender] Loop %u/%u\n", loop + 1, cfg.loop_count);
            std::fflush(stdout);
        }

        std::uint16_t seq = 0;
        for (const auto &entry : entries)
        {
            std::ifstream f(entry.bin_file, std::ios::binary);
            if (!f.is_open())
            {
                std::fprintf(stderr, "无法打开文件: %s\n", entry.bin_file.c_str());
                return 1;
            }
            f.seekg(static_cast<std::streamoff>(entry.offset));
            std::vector<std::uint8_t> payload(entry.length);
            f.read(reinterpret_cast<char *>(payload.data()), entry.length);
            if (!f)
            {
                std::fprintf(stderr, "读取数据长度不足: %s, offset=%llu\n",
                             entry.bin_file.c_str(),
                             static_cast<unsigned long long>(entry.offset));
                return 1;
            }

            const auto frame = rxtech::replay::build_eth_frame(
                payload.data(), entry.length, fcfg, seq++);

            rate.wait_for_next_slot();
            if (!sender.send_frame(frame.data(), frame.size()))
            {
                std::fprintf(stderr, "发送数据帧失败，entry=%llu\n",
                             static_cast<unsigned long long>(sent_total));
                return 1;
            }
            ++sent_total;

            if (cfg.verbose && (sent_total % 1000ULL == 0))
            {
                std::fprintf(stdout, "\r[rx_replay_sender] sent %llu / %llu",
                             static_cast<unsigned long long>(sent_total),
                             static_cast<unsigned long long>(total_entries * cfg.loop_count));
                std::fflush(stdout);
            }
        }
    }

    if (cfg.verbose)
        std::fprintf(stdout, "\n[rx_replay_sender] Done. Sent %llu packets.\n",
                     static_cast<unsigned long long>(sender.sent_packets()));

    return 0;
}
