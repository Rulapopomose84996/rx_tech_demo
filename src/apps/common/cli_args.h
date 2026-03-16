#pragma once

#include <string>

namespace rxtech {

struct CliArgs {
    std::string mode;
    std::string scenario_path;
    std::string output_dir;
};

CliArgs parse_cli_args(int argc, char** argv);

}  // namespace rxtech
