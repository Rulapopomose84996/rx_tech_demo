#include "cli_args.h"

namespace rxtech {

CliArgs parse_cli_args(int argc, char** argv) {
    CliArgs args;
    if (argc > 1) {
        args.mode = argv[1];
    }
    if (argc > 2) {
        args.scenario_path = argv[2];
    }
    if (argc > 3) {
        args.output_dir = argv[3];
    }
    return args;
}

}  // namespace rxtech
