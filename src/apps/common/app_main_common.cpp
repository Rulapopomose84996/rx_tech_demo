#include "app_main_common.h"

#include <iostream>
#include <memory>
#include <stdexcept>

#include "cli_args.h"
#include "rxtech/bench_runner.h"
#include "rxtech/metrics.h"
#include "rxtech/parse_mode.h"
#include "rxtech/rx_config.h"
#include "rxtech/rx_only_mode.h"
#include "rxtech/scenario.h"
#include "rxtech/spsc_mode.h"

#if defined(RXTECH_HAS_SOCKET_BACKEND)
#include "rxtech/socket_backend.h"
#endif

#if defined(RXTECH_HAS_AF_XDP_BACKEND)
#include "rxtech/xdp_backend.h"
#endif

#if defined(RXTECH_HAS_DPDK_BACKEND)
#include "rxtech/dpdk_backend.h"
#endif

namespace rxtech {

namespace {

BackendPtr make_backend(const std::string& backend_name) {
    if (backend_name == "socket") {
#if defined(RXTECH_HAS_SOCKET_BACKEND)
        return std::make_unique<SocketBackend>();
#else
        throw std::runtime_error("socket backend is disabled");
#endif
    }

    if (backend_name == "af_xdp") {
#if defined(RXTECH_HAS_AF_XDP_BACKEND)
        return std::make_unique<XdpBackend>();
#else
        throw std::runtime_error("AF_XDP backend is disabled");
#endif
    }

    if (backend_name == "dpdk") {
#if defined(RXTECH_HAS_DPDK_BACKEND)
        return std::make_unique<DpdkBackend>();
#else
        throw std::runtime_error("DPDK backend is disabled");
#endif
    }

    throw std::runtime_error("unknown backend: " + backend_name);
}

ModeProcessorPtr make_mode(const std::string& mode_name) {
    if (mode_name == "rx_only") {
        return std::make_unique<RxOnlyMode>();
    }
    if (mode_name == "parse") {
        return std::make_unique<ParseMode>();
    }
    if (mode_name == "spsc") {
        return std::make_unique<SpscMode>();
    }

    throw std::runtime_error("unknown mode: " + mode_name);
}

}  // namespace

int run_app(const std::string& backend_name, int argc, char** argv) {
    const CliArgs args = parse_cli_args(argc, argv);

    BenchContext context;
    context.config = load_default_config();
    context.config.backend_name = backend_name;
    context.config.mode_name = args.mode.empty() ? "rx_only" : args.mode;
    context.config.scenario_path = args.scenario_path;
    context.config.output_dir = args.output_dir.empty() ? "results" : args.output_dir;
    if (!args.interface_name.empty()) {
        context.config.interface_name = args.interface_name;
    }
    if (!args.queue_id.empty()) {
        context.config.queue_id = static_cast<std::uint32_t>(std::stoul(args.queue_id));
    }
    if (!args.duration_seconds.empty()) {
        context.config.duration_seconds = static_cast<std::uint32_t>(std::stoul(args.duration_seconds));
    }
    context.scenario = load_scenario(context.config.scenario_path);
    context.backend = make_backend(backend_name);
    context.mode = make_mode(context.config.mode_name);
    context.metrics = std::make_unique<MetricsCollector>();

    try {
        BenchRunner runner;
        const RunSummary summary = runner.run(context);
        std::cout << "backend=" << summary.backend
                  << " mode=" << summary.mode
                  << " scenario=" << summary.scenario
                  << " queue_id=" << summary.queue_id
                  << " rx_packets=" << summary.rx_packets
                  << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "run failed: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}

}  // namespace rxtech
