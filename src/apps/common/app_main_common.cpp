#include "app_main_common.h"

#include <iostream>
#include <memory>
#include <sstream>
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

class UnavailableBackend final : public IRxBackend {
public:
    UnavailableBackend(std::string backend_name, std::string reason)
        : backend_name_(std::move(backend_name)), reason_(std::move(reason)) {
    }

    std::string name() const override {
        return backend_name_;
    }

    BackendInitResult init(const RxConfig&) override {
        BackendInitResult result;
        result.available = false;
        result.reason = reason_;
        return result;
    }

    bool recv_burst(RxBurst&, std::uint32_t) override {
        return false;
    }

    void release_burst(RxBurst& burst) override {
        burst.packets.clear();
    }

    BackendStats stats() const override {
        return {};
    }

    void shutdown() override {
    }

private:
    std::string backend_name_;
    std::string reason_;
};

BackendPtr make_backend(const std::string& backend_name) {
    if (backend_name == "socket") {
#if defined(RXTECH_HAS_SOCKET_BACKEND)
        return std::make_unique<SocketBackend>();
#else
        return std::make_unique<UnavailableBackend>("socket", "socket backend is disabled at build time");
#endif
    }

    if (backend_name == "af_xdp") {
#if defined(RXTECH_HAS_AF_XDP_BACKEND)
        return std::make_unique<XdpBackend>();
#else
        return std::make_unique<UnavailableBackend>("af_xdp", "AF_XDP backend is disabled at build time");
#endif
    }

    if (backend_name == "dpdk") {
#if defined(RXTECH_HAS_DPDK_BACKEND)
        return std::make_unique<DpdkBackend>();
#else
        return std::make_unique<UnavailableBackend>("dpdk", "DPDK backend is disabled at build time");
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

RxConfig make_empty_overrides() {
    RxConfig overrides;
    overrides.backend_name.clear();
    overrides.mode_name.clear();
    overrides.scenario_path.clear();
    overrides.config_path.clear();
    overrides.output_dir.clear();
    overrides.interface_name.clear();
    overrides.bind_address.clear();
    overrides.dpdk_pci_addr.clear();
    overrides.xdp_bind_mode.clear();
    overrides.cpu_cores.clear();
    return overrides;
}

RxConfig cli_args_to_overrides(const CliArgs& args) {
    RxConfig overrides = make_empty_overrides();
    if (!args.mode.empty()) {
        overrides.mode_name = args.mode;
    }
    if (!args.scenario_path.empty()) {
        overrides.scenario_path = args.scenario_path;
    }
    if (!args.output_dir.empty()) {
        overrides.output_dir = args.output_dir;
    }
    if (!args.interface_name.empty()) {
        overrides.interface_name = args.interface_name;
    }
    if (!args.queue_id.empty()) {
        overrides.queue_id = static_cast<std::uint32_t>(std::stoul(args.queue_id));
    }
    if (!args.duration_seconds.empty()) {
        overrides.duration_seconds = static_cast<std::uint32_t>(std::stoul(args.duration_seconds));
    }
    if (!args.max_burst.empty()) {
        overrides.max_burst = static_cast<std::uint32_t>(std::stoul(args.max_burst));
    }
    if (!args.cpu_cores.empty()) {
        std::stringstream stream(args.cpu_cores);
        std::string item;
        while (std::getline(stream, item, ',')) {
            if (!item.empty()) {
                overrides.cpu_cores.push_back(std::stoi(item));
            }
        }
    }
    return overrides;
}

RxConfig build_effective_config(const std::string& backend_name, const CliArgs& args) {
    RxConfig effective = load_default_config();
    if (!args.config_path.empty()) {
        merge_config(effective, load_config_file(args.config_path));
        effective.config_path = args.config_path;
    }

    merge_config(effective, cli_args_to_overrides(args));
    effective.backend_name = backend_name;
    return effective;
}

void print_dry_run(const RxConfig& config, const Scenario& scenario) {
    std::cout << "[dry-run]" << std::endl;
    std::cout << "backend=" << config.backend_name << std::endl;
    std::cout << "mode=" << config.mode_name << std::endl;
    std::cout << "config_path=" << config.config_path << std::endl;
    std::cout << "scenario=" << scenario.scenario_name << std::endl;
    std::cout << "scenario_path=" << config.scenario_path << std::endl;
    std::cout << "output_dir=" << config.output_dir << std::endl;
    std::cout << "interface=" << config.interface_name << std::endl;
    std::cout << "queue_id=" << config.queue_id << std::endl;
    std::cout << "duration_seconds=" << config.duration_seconds << std::endl;
    std::cout << "max_burst=" << config.max_burst << std::endl;
    std::cout << "packet_size_bytes=" << config.packet_size_bytes << std::endl;
    std::cout << "steps=" << scenario.steps.size() << std::endl;
    for (std::size_t index = 0; index < scenario.steps.size(); ++index) {
        const ScenarioStep& step = scenario.steps[index];
        std::cout << "step[" << index << "]"
                  << " name=" << step.name
                  << " phase=" << step.phase
                  << " duration_seconds=" << step.duration_seconds
                  << " packet_size_bytes=" << step.packet_size_bytes
                  << " face_count=" << step.face_count
                  << " target_rate_gbps=" << step.target_rate_gbps
                  << " burst_window_ms=" << step.burst_window_ms
                  << std::endl;
    }
}

}  // namespace

int run_app(const std::string& backend_name, int argc, char** argv) {
    try {
        const CliArgs args = parse_cli_args(argc, argv);

        BenchContext context;
        context.config = build_effective_config(backend_name, args);
        context.scenario = load_scenario(context.config.scenario_path);

        if (args.dry_run) {
            print_dry_run(context.config, context.scenario);
            return 0;
        }

        context.backend = make_backend(backend_name);
        context.mode = make_mode(context.config.mode_name);
        context.metrics = std::make_unique<MetricsCollector>();

        BenchRunner runner;
        const RunSummary summary = runner.run(context);
        std::cout << "status=" << summary.run_status
                  << " backend=" << summary.backend
                  << " mode=" << summary.mode
                  << " scenario=" << summary.scenario
                  << " queue_id=" << summary.queue_id
                  << " rx_packets=" << summary.rx_packets
                  << std::endl;
        if (summary.run_status != "success") {
            std::cerr << "run failed: " << summary.error_message << std::endl;
            return summary.run_status == "unavailable" ? 2 : 1;
        }
    } catch (const std::exception& ex) {
        std::cerr << "run failed: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}

}  // namespace rxtech
