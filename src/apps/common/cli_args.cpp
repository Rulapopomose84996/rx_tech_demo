#include "cli_args.h"

#include <stdexcept>
#include <string>

namespace rxtech {

CliArgs parse_cli_args(int argc, char** argv) {
    CliArgs args;
    int positional_index = 0;

    for (int index = 1; index < argc; ++index) {
        const std::string current = argv[index];
        if (current.rfind("--", 0) == 0) {
            if (current == "--dry-run") {
                args.dry_run = true;
                continue;
            }
            if (index + 1 >= argc) {
                throw std::runtime_error("missing value for argument: " + current);
            }

            const std::string value = argv[++index];
            if (current == "--config") {
                args.config_path = value;
            } else if (current == "--mode") {
                args.mode = value;
            } else if (current == "--scenario") {
                args.scenario_path = value;
            } else if (current == "--output") {
                args.output_dir = value;
            } else if (current == "--iface") {
                args.interface_name = value;
            } else if (current == "--queue") {
                args.queue_id = value;
            } else if (current == "--duration") {
                args.duration_seconds = value;
            } else if (current == "--max-burst") {
                args.max_burst = value;
            } else if (current == "--cores") {
                args.cpu_cores = value;
            } else {
                throw std::runtime_error("unknown argument: " + current);
            }
            continue;
        }

        switch (positional_index) {
            case 0:
                args.mode = current;
                break;
            case 1:
                args.scenario_path = current;
                break;
            case 2:
                args.output_dir = current;
                break;
            case 3:
                args.interface_name = current;
                break;
            case 4:
                args.queue_id = current;
                break;
            case 5:
                args.duration_seconds = current;
                break;
            default:
                throw std::runtime_error("unexpected positional argument: " + current);
        }
        ++positional_index;
    }

    return args;
}

}  // namespace rxtech
