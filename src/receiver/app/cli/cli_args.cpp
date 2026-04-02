#include "cli_args.h"

#include <string>

namespace rxtech
{

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
