#include "replay_config.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace rxtech::replay
{
    namespace
    {
        int hex_value(char ch) noexcept
        {
            if (ch >= '0' && ch <= '9')
            {
                return ch - '0';
            }
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (ch >= 'a' && ch <= 'f')
            {
                return 10 + (ch - 'a');
            }
            return -1;
        }
    } // namespace

    bool parse_mac_address(const std::string &text, std::array<std::uint8_t, 6> &out_mac) noexcept
    {
        if (text.size() != 17U)
        {
            return false;
        }

        std::array<std::uint8_t, 6> parsed{};
        for (std::size_t index = 0; index < parsed.size(); ++index)
        {
            const std::size_t offset = index * 3U;
            const int hi = hex_value(text[offset]);
            const int lo = hex_value(text[offset + 1U]);
            if (hi < 0 || lo < 0)
            {
                return false;
            }
            if (index != parsed.size() - 1U && text[offset + 2U] != ':')
            {
                return false;
            }
            parsed[index] = static_cast<std::uint8_t>((hi << 4U) | lo);
        }

        out_mac = parsed;
        return true;
    }

    void print_replay_usage(const char *argv0)
    {
        std::fprintf(stderr,
                     "Usage: %s --data-dir DIR [--data-dir DIR ...]\n"
                     "          --iface IF\n"
                     "          [--src-mac MAC]   (default: 02:00:00:00:00:01)\n"
                     "          [--dst-mac MAC]   (default: ff:ff:ff:ff:ff:ff)\n"
                     "          [--dest-ip IP]    (default: 172.20.11.100)\n"
                     "          [--src-ip IP]     (default: 172.20.11.222)\n"
                     "          [--dest-port N]   (default: 9999)\n"
                     "          [--src-port N]    (default: 30001)\n"
                     "          [--pps N]         (default: 1000, 0 = unlimited)\n"
                     "          [--loops N]       (default: 1)\n"
                     "          [--verbose]\n",
                     argv0);
    }

    bool parse_replay_args(int argc, char **argv, ReplaySenderConfig &out_cfg)
    {
        out_cfg = {};
        out_cfg.dest_ip = "172.20.11.100";
        out_cfg.src_ip = "172.20.11.222";
        out_cfg.dest_port = 9999U;
        out_cfg.src_port = 30001U;
        out_cfg.pps = 1000U;
        out_cfg.loop_count = 1U;

        for (int i = 1; i < argc; ++i)
        {
            const char *arg = argv[i];

            auto next = [&]() -> const char *
            {
                if (i + 1 >= argc)
                    return nullptr;
                return argv[++i];
            };

            if (std::strcmp(arg, "--data-dir") == 0)
            {
                const char *val = next();
                if (!val)
                {
                    std::fprintf(stderr, "--data-dir requires a value\n");
                    return false;
                }
                out_cfg.data_dirs.emplace_back(val);
            }
            else if (std::strcmp(arg, "--iface") == 0)
            {
                const char *val = next();
                if (!val)
                {
                    std::fprintf(stderr, "--iface requires a value\n");
                    return false;
                }
                out_cfg.interface = val;
            }
            else if (std::strcmp(arg, "--src-mac") == 0)
            {
                const char *val = next();
                if (!val)
                {
                    std::fprintf(stderr, "--src-mac requires a value\n");
                    return false;
                }
                out_cfg.src_mac = val;
            }
            else if (std::strcmp(arg, "--dst-mac") == 0)
            {
                const char *val = next();
                if (!val)
                {
                    std::fprintf(stderr, "--dst-mac requires a value\n");
                    return false;
                }
                out_cfg.dst_mac = val;
            }
            else if (std::strcmp(arg, "--dest-ip") == 0)
            {
                const char *val = next();
                if (!val)
                {
                    std::fprintf(stderr, "--dest-ip requires a value\n");
                    return false;
                }
                out_cfg.dest_ip = val;
            }
            else if (std::strcmp(arg, "--src-ip") == 0)
            {
                const char *val = next();
                if (!val)
                {
                    std::fprintf(stderr, "--src-ip requires a value\n");
                    return false;
                }
                out_cfg.src_ip = val;
            }
            else if (std::strcmp(arg, "--dest-port") == 0)
            {
                const char *val = next();
                if (!val)
                {
                    std::fprintf(stderr, "--dest-port requires a value\n");
                    return false;
                }
                out_cfg.dest_port = static_cast<std::uint16_t>(std::atoi(val));
            }
            else if (std::strcmp(arg, "--src-port") == 0)
            {
                const char *val = next();
                if (!val)
                {
                    std::fprintf(stderr, "--src-port requires a value\n");
                    return false;
                }
                out_cfg.src_port = static_cast<std::uint16_t>(std::atoi(val));
            }
            else if (std::strcmp(arg, "--pps") == 0)
            {
                const char *val = next();
                if (!val)
                {
                    std::fprintf(stderr, "--pps requires a value\n");
                    return false;
                }
                out_cfg.pps = static_cast<std::uint32_t>(std::atoi(val));
            }
            else if (std::strcmp(arg, "--loops") == 0)
            {
                const char *val = next();
                if (!val)
                {
                    std::fprintf(stderr, "--loops requires a value\n");
                    return false;
                }
                out_cfg.loop_count = static_cast<std::uint32_t>(std::atoi(val));
            }
            else if (std::strcmp(arg, "--verbose") == 0)
            {
                out_cfg.verbose = true;
            }
            else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0)
            {
                return false;
            }
            else
            {
                std::fprintf(stderr, "Unknown option: %s\n", arg);
                return false;
            }
        }

        if (out_cfg.data_dirs.empty())
        {
            std::fprintf(stderr, "Error: at least one --data-dir is required\n");
            return false;
        }
        if (out_cfg.interface.empty())
        {
            std::fprintf(stderr, "Error: --iface is required\n");
            return false;
        }

        std::array<std::uint8_t, 6> parsed_mac{};
        if (!out_cfg.src_mac.empty() && !parse_mac_address(out_cfg.src_mac, parsed_mac))
        {
            std::fprintf(stderr, "Error: invalid --src-mac format: %s\n", out_cfg.src_mac.c_str());
            return false;
        }
        if (!out_cfg.dst_mac.empty() && !parse_mac_address(out_cfg.dst_mac, parsed_mac))
        {
            std::fprintf(stderr, "Error: invalid --dst-mac format: %s\n", out_cfg.dst_mac.c_str());
            return false;
        }
        return true;
    }

} // namespace rxtech::replay
