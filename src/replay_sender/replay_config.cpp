#include "replay_config.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace rxtech::replay
{

    void print_replay_usage(const char *argv0)
    {
        std::fprintf(stderr,
                     "Usage: %s --data-dir DIR [--data-dir DIR ...]\n"
                     "          --iface IF\n"
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
        return true;
    }

} // namespace rxtech::replay
