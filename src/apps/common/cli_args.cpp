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
    if (argc > 4) {
        args.interface_name = argv[4];
    }
    if (argc > 5) {
        args.queue_id = argv[5];
    }
    if (argc > 6) {
        args.duration_seconds = argv[6];
    }
    return args;
}

}  // namespace rxtech
