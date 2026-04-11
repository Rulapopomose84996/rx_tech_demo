#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>

#include "rxtech/rx_config.h"
#include "dpdk_backend.h"

int main()
{
    rxtech::RxConfig config;
    config.process.cpu_cores = {16, 17, 18};
    config.ingress.dpdk_pci_addr = "0001:05:00.3";

    const std::vector<std::string> args = rxtech::build_dpdk_eal_args(config);

    assert(args.size() == 8U);
    assert(args[0] == "rxbench_dpdk");
    assert(args[1] == "-l");
    assert(args[2] == "16,17,18");
    assert(args[3] == "-n");
    assert(args[4] == "4");
    assert(args[5] == "--in-memory");
    assert(args[6] == "-w");
    assert(args[7] == "0001:05:00.3");

    const auto whitelist_pos = std::find(args.begin(), args.end(), "-w");
    assert(whitelist_pos != args.end());
    assert((whitelist_pos + 1) != args.end());
    assert(*(whitelist_pos + 1) == "0001:05:00.3");

    const auto unsupported_pos = std::find(args.begin(), args.end(), "-a");
    assert(unsupported_pos == args.end());

    return 0;
}
