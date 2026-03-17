#pragma once

#include <string>

namespace rxtech {

struct CliArgs {
    std::string config_path;
    std::string mode;
    std::string scenario_path;
    std::string output_dir;
    std::string interface_name;
    std::string queue_id;
    std::string duration_seconds;
    std::string max_burst;
    std::string cpu_cores;
};

CliArgs parse_cli_args(int argc, char** argv);

}  // namespace rxtech
