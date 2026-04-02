#include "cli_args.h"

#include <limits>
#include <string>

namespace rxtech
{

    namespace
    {

        bool parse_u32_argument(const std::string &value, std::uint32_t &parsed_value)
        {
            try
            {
                std::size_t processed = 0;
                const unsigned long raw_value = std::stoul(value, &processed, 10);
                if (processed != value.size() || raw_value > std::numeric_limits<std::uint32_t>::max())
                {
                    return false;
                }
                parsed_value = static_cast<std::uint32_t>(raw_value);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

    } // namespace

    CliArgs parse_cli_args(int argc, char **argv)
    {
        CliArgs args;

        for (int index = 1; index < argc; ++index)
        {
            const std::string current = argv[index];
            if (current == "-h" || current == "--help")
            {
                args.help = true;
                continue;
            }

            if (current.rfind("--", 0) == 0)
            {
                if (current == "--dry-run")
                {
                    args.dry_run = true;
                    continue;
                }
                if (current == "--run-until-stopped")
                {
                    args.run_until_stopped = true;
                    continue;
                }
                if (index + 1 >= argc)
                {
                    args.valid = false;
                    args.error_message = "missing value for argument: " + current;
                    return args;
                }

                const std::string value = argv[++index];
                if (current == "--config")
                {
                    args.config_path = value;
                }
                else if (current == "--duration")
                {
                    std::uint32_t duration_seconds = 0;
                    if (!parse_u32_argument(value, duration_seconds))
                    {
                        args.valid = false;
                        args.error_message = "invalid unsigned integer for argument: --duration";
                        return args;
                    }
                    args.duration_seconds = duration_seconds;
                }
                else if (current == "--status-interval")
                {
                    std::uint32_t status_interval_seconds = 0;
                    if (!parse_u32_argument(value, status_interval_seconds))
                    {
                        args.valid = false;
                        args.error_message = "invalid unsigned integer for argument: --status-interval";
                        return args;
                    }
                    args.status_interval_seconds = status_interval_seconds;
                }
                else
                {
                    args.valid = false;
                    args.error_message = "unknown argument: " + current;
                    return args;
                }
                continue;
            }

            args.valid = false;
            args.error_message = "unexpected positional argument: " + current;
            return args;
        }

        return args;
    }

} // namespace rxtech
