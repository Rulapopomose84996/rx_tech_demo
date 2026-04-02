#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace rxtech
{

    struct CliArgs
    {
        std::string config_path;
        bool dry_run = false;
        bool help = false;
        bool run_until_stopped = false;
        std::optional<std::uint32_t> duration_seconds;
        std::optional<std::uint32_t> status_interval_seconds;
        bool valid = true;
        std::string error_message;
    };

    CliArgs parse_cli_args(int argc, char **argv);

} // namespace rxtech
