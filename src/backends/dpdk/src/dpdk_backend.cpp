#include "rxtech/dpdk_backend.h"

#include <algorithm>
#include <vector>

#include "rxtech/rx_config.h"
#include "rxtech/time_utils.h"

#if defined(__linux__) && defined(RXTECH_HAS_DPDK_RUNTIME)
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#endif

namespace rxtech {

namespace {

BackendInitResult make_dpdk_result(bool available, const std::string& reason) {
    BackendInitResult result;
    result.available = available;
    result.reason = reason;
    return result;
}

}  // namespace

struct DpdkBackend::Impl {
#if defined(__linux__) && defined(RXTECH_HAS_DPDK_RUNTIME)
    rte_mempool* mempool = nullptr;
    std::uint16_t port_id = 0;
    bool started = false;

    void cleanup() {
        if (started) {
            rte_eth_dev_stop(port_id);
            rte_eth_dev_close(port_id);
            started = false;
        }
        mempool = nullptr;
    }
#endif
};

DpdkBackend::DpdkBackend() : impl_(new Impl()) {
}

DpdkBackend::~DpdkBackend() {
    shutdown();
}

std::string DpdkBackend::name() const {
    return "dpdk";
}

BackendInitResult DpdkBackend::init(const RxConfig& config) {
    stats_ = {};
#if defined(__linux__) && defined(RXTECH_HAS_DPDK_RUNTIME)
    if (impl_ == nullptr) {
        return make_dpdk_result(false, "DPDK backend internal state is unavailable");
    }

    impl_->cleanup();

    std::vector<std::string> eal_args;
    eal_args.emplace_back("rxbench_dpdk");
    if (!config.cpu_cores.empty()) {
        std::string core_list;
        for (std::size_t index = 0; index < config.cpu_cores.size(); ++index) {
            if (index != 0U) {
                core_list += ",";
            }
            core_list += std::to_string(config.cpu_cores[index]);
        }
        eal_args.emplace_back("-l");
        eal_args.emplace_back(core_list);
    }
    eal_args.emplace_back("-n");
    eal_args.emplace_back("4");
    eal_args.emplace_back("--in-memory");
    eal_args.emplace_back("--no-telemetry");
    if (!config.dpdk_pci_addr.empty()) {
        eal_args.emplace_back("-a");
        eal_args.emplace_back(config.dpdk_pci_addr);
    }

    std::vector<char*> argv;
    argv.reserve(eal_args.size());
    for (std::string& arg : eal_args) {
        argv.push_back(arg.data());
    }

    const int eal_rc = rte_eal_init(static_cast<int>(argv.size()), argv.data());
    if (eal_rc < 0) {
        ++stats_.rx_errors;
        return make_dpdk_result(true, "rte_eal_init() failed");
    }

    std::uint16_t port_id = static_cast<std::uint16_t>(config.dpdk_port_id);
    if (!config.dpdk_pci_addr.empty()) {
        std::uint16_t resolved_port = 0;
        if (rte_eth_dev_get_port_by_name(config.dpdk_pci_addr.c_str(), &resolved_port) == 0) {
            port_id = resolved_port;
        }
    }

    if (!rte_eth_dev_is_valid_port(port_id)) {
        ++stats_.rx_errors;
        return make_dpdk_result(true, "invalid DPDK port: " + std::to_string(port_id));
    }

    impl_->port_id = port_id;
    impl_->mempool = rte_pktmbuf_pool_create("rxtech_dpdk_mbuf_pool",
                                             config.dpdk_mempool_size,
                                             config.dpdk_mbuf_cache_size,
                                             0,
                                             RTE_MBUF_DEFAULT_BUF_SIZE,
                                             rte_socket_id());
    if (impl_->mempool == nullptr) {
        ++stats_.rx_errors;
        return make_dpdk_result(true, "rte_pktmbuf_pool_create() failed");
    }

    rte_eth_conf port_conf{};
    if (rte_eth_dev_configure(port_id, 1, 1, &port_conf) < 0) {
        ++stats_.rx_errors;
        return make_dpdk_result(true, "rte_eth_dev_configure() failed");
    }
    if (rte_eth_rx_queue_setup(port_id,
                               0,
                               static_cast<std::uint16_t>(config.dpdk_rx_desc),
                                rte_eth_dev_socket_id(port_id),
                                nullptr,
                                impl_->mempool) < 0) {
        ++stats_.rx_errors;
        return make_dpdk_result(true, "rte_eth_rx_queue_setup() failed");
    }
    if (rte_eth_tx_queue_setup(port_id,
                               0,
                                static_cast<std::uint16_t>(config.dpdk_tx_desc),
                                rte_eth_dev_socket_id(port_id),
                                nullptr) < 0) {
        ++stats_.rx_errors;
        return make_dpdk_result(true, "rte_eth_tx_queue_setup() failed");
    }
    if (rte_eth_dev_start(port_id) < 0) {
        ++stats_.rx_errors;
        return make_dpdk_result(true, "rte_eth_dev_start() failed");
    }

    impl_->started = true;
    stats_.queue_id = 0;
    BackendInitResult result;
    result.ok = true;
    return result;
#else
    (void)config;
    return make_dpdk_result(false, "DPDK runtime not linked; install libdpdk and rebuild on Linux");
#endif
}

bool DpdkBackend::recv_burst(RxBurst& burst, std::uint32_t max_burst) {
    burst.packets.clear();
#if defined(__linux__) && defined(RXTECH_HAS_DPDK_RUNTIME)
    if (impl_ == nullptr || !impl_->started) {
        return false;
    }

    ++stats_.rx_polls;
    const std::uint16_t budget = static_cast<std::uint16_t>(std::min<std::uint32_t>(max_burst, 64U));
    std::vector<rte_mbuf*> mbufs(budget);
    const std::uint16_t received = rte_eth_rx_burst(impl_->port_id, 0, mbufs.data(), budget);
    if (received == 0U) {
        ++stats_.empty_polls;
        return true;
    }

    burst.packets.reserve(received);
    for (std::uint16_t index = 0; index < received; ++index) {
        rte_mbuf* mbuf = mbufs[index];
        PacketDesc packet;
        packet.data = rte_pktmbuf_mtod(mbuf, std::uint8_t*);
        packet.len = rte_pktmbuf_pkt_len(mbuf);
        packet.ts_ns = steady_clock_now_ns();
        packet.queue_id = 0;
        packet.cookie = reinterpret_cast<std::uintptr_t>(mbuf);
        burst.packets.push_back(packet);
        ++stats_.rx_packets;
        stats_.rx_bytes += packet.len;
    }
    return true;
#else
    (void)max_burst;
    return false;
#endif
}

void DpdkBackend::release_burst(RxBurst& burst) {
#if defined(__linux__) && defined(RXTECH_HAS_DPDK_RUNTIME)
    for (const PacketDesc& packet : burst.packets) {
        auto* mbuf = reinterpret_cast<rte_mbuf*>(packet.cookie);
        if (mbuf != nullptr) {
            rte_pktmbuf_free(mbuf);
        }
    }
#endif
    burst.packets.clear();
}

BackendStats DpdkBackend::stats() const {
    return stats_;
}

void DpdkBackend::shutdown() {
#if defined(__linux__) && defined(RXTECH_HAS_DPDK_RUNTIME)
    if (impl_ != nullptr) {
        impl_->cleanup();
    }
#endif
}

}  // namespace rxtech
