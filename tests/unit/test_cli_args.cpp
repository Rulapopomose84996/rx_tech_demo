#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "cli_args.h"

int main()
{
    {
        char arg0[] = "rx_receiver_dpdk";
        char arg1[] = "--config";
        char arg2[] = "configs/dpdk_single_face.conf";
        char arg3[] = "--run-until-stopped";
        char arg4[] = "--status-interval";
        char arg5[] = "2";
        char arg6[] = "--duration";
        char arg7[] = "15";
        char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7};

        const rxtech::CliArgs args = rxtech::parse_cli_args(8, argv);
        assert(args.valid);
        assert(args.config_path == "configs/dpdk_single_face.conf");
        assert(args.run_until_stopped);
        assert(args.status_interval_seconds.has_value());
        assert(*args.status_interval_seconds == 2U);
        assert(args.duration_seconds.has_value());
        assert(*args.duration_seconds == 15U);
    }

    {
        char arg0[] = "rx_receiver_dpdk";
        char arg1[] = "--duration";
        char arg2[] = "abc";
        char* argv[] = {arg0, arg1, arg2};

        const rxtech::CliArgs args = rxtech::parse_cli_args(3, argv);
        assert(!args.valid);
        assert(args.error_message == "invalid unsigned integer for argument: --duration");
    }

    return 0;
}
