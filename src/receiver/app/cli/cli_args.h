#pragma once

#include <string>

namespace rxtech
{

    struct CliArgs
    {
        std::string config_path;
        bool dry_run = false;
        bool help = false;
        bool valid = true;
        std::string error_message;
    };

    CliArgs parse_cli_args(int argc, char **argv);

} // namespace rxtech
