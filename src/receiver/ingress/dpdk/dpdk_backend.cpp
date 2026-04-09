#include "dpdk_backend.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "arp_responder.h"
#include "rxtech/rx_config.h"
#include "rxtech/time_utils.h"

#if defined(__linux__) && defined(RXTECH_HAS_DPDK_RUNTIME)
#include <arpa/inet.h>
#include <rte_eal.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#endif

namespace rxtech
{

    namespace
    {
        constexpr std::size_t kEthernetHeaderBytes = 14U;
        constexpr std::size_t kIpv4HeaderBytes = 20U;
        constexpr std::size_t kUdpHeaderBytes = 8U;
        constexpr std::size_t kUdpPayloadOffset = kEthernetHeaderBytes + kIpv4HeaderBytes + kUdpHeaderBytes;
    }

    std::vector<std::string> build_dpdk_eal_args(const RxConfig &config)
    {
        std::vector<std::string> eal_args;
        eal_args.emplace_back("rxbench_dpdk");
        if (!config.cpu_cores.empty())
        {
            std::string core_list;
            for (std::size_t index = 0; index < config.cpu_cores.size(); ++index)
            {
                if (index != 0U)
                {
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
        if (!config.dpdk_pci_addr.empty())
        {
            eal_args.emplace_back("-w");
            eal_args.emplace_back(config.dpdk_pci_addr);
        }
        return eal_args;
    }

    namespace
    {

        BackendInitResult make_dpdk_result(bool available, const std::string &reason)
        {
            BackendInitResult result;
            result.available = available;
            result.reason = reason;
            return result;
        }

#if defined(__linux__) && defined(RXTECH_HAS_DPDK_RUNTIME)
        bool resolve_port_id(const RxConfig &config, std::uint16_t &port_id)
        {
            std::vector<std::uint16_t> available_ports;
            for (std::uint16_t candidate = 0; candidate < RTE_MAX_ETHPORTS; ++candidate)
            {
                if (!rte_eth_dev_is_valid_port(candidate))
                {
                    continue;
                }
                available_ports.push_back(candidate);

                if (!config.dpdk_pci_addr.empty())
                {
                    rte_eth_dev_info info{};
                    if (rte_eth_dev_info_get(candidate, &info) == 0 && info.device != nullptr && info.device->name != nullptr)
                    {
                        if (config.dpdk_pci_addr == info.device->name)
                        {
                            port_id = candidate;
                            return true;
                        }
                    }

                    char port_name[RTE_ETH_NAME_MAX_LEN] = {};
                    if (rte_eth_dev_get_name_by_port(candidate, port_name) == 0)
                    {
                        if (config.dpdk_pci_addr == port_name)
                        {
                            port_id = candidate;
                            return true;
                        }
                    }
                }
            }

            if (!config.dpdk_pci_addr.empty())
            {
                if (available_ports.size() == 1U)
                {
                    port_id = available_ports.front();
                    return true;
                }
                return false;
            }

            if (rte_eth_dev_is_valid_port(static_cast<std::uint16_t>(config.dpdk_port_id)))
            {
                port_id = static_cast<std::uint16_t>(config.dpdk_port_id);
                return true;
            }

            if (available_ports.size() == 1U)
            {
                port_id = available_ports.front();
                return true;
            }

            return false;
        }
#endif

    } // namespace

    struct DpdkIngress::Impl
    {
#if defined(__linux__) && defined(RXTECH_HAS_DPDK_RUNTIME)
        rte_mempool *mempool = nullptr;
        std::uint16_t port_id = 0;
        bool started = false;
        std::array<std::uint8_t, 6> local_mac{};
        std::uint32_t local_ip_be = 0;
        std::uint64_t arp_request_packets = 0;
        std::uint64_t arp_reply_packets = 0;

        void cleanup()
        {
            if (started)
            {
                rte_eth_dev_stop(port_id);
                rte_eth_dev_close(port_id);
                started = false;
            }
            mempool = nullptr;
        }

        bool maybe_reply_arp(rte_mbuf *mbuf)
        {
            if (mbuf == nullptr)
            {
                return false;
            }

            const std::uint8_t *frame = rte_pktmbuf_mtod(mbuf, std::uint8_t *);
            const std::size_t frame_len = rte_pktmbuf_pkt_len(mbuf);
            ArpRequestInfo request{};
            if (!parse_arp_request(frame, frame_len, local_ip_be, request))
            {
                return false;
            }

            ++arp_request_packets;

            const std::vector<std::uint8_t> reply = build_arp_reply(request, local_mac);
            rte_mbuf *tx = rte_pktmbuf_alloc(mempool);
            if (tx == nullptr)
            {
                return true;
            }

            std::uint8_t *tx_data = rte_pktmbuf_mtod(tx, std::uint8_t *);
            if (rte_pktmbuf_tailroom(tx) < reply.size())
            {
                rte_pktmbuf_free(tx);
                return true;
            }
            std::memcpy(tx_data, reply.data(), reply.size());
            tx->data_len = static_cast<std::uint16_t>(reply.size());
            tx->pkt_len = static_cast<std::uint32_t>(reply.size());

            rte_mbuf *tx_packets[1] = {tx};
            const std::uint16_t sent = rte_eth_tx_burst(port_id, 0, tx_packets, 1);
            if (sent == 0U)
            {
                rte_pktmbuf_free(tx);
            }
            else
            {
                ++arp_reply_packets;
            }
            return true;
        }
#endif
    };

    DpdkIngress::DpdkIngress() : impl_(new Impl())
    {
    }

    DpdkIngress::~DpdkIngress()
    {
        shutdown();
    }

    std::string DpdkIngress::name() const
    {
        return "dpdk";
    }

    BackendInitResult DpdkIngress::init(const RxConfig &config)
    {
        stats_ = {};
#if defined(__linux__) && defined(RXTECH_HAS_DPDK_RUNTIME)
        if (impl_ == nullptr)
        {
            return make_dpdk_result(false, "DPDK 后端内部状态不可用");
        }

        impl_->cleanup();
        impl_->arp_request_packets = 0;
        impl_->arp_reply_packets = 0;

        std::vector<std::string> eal_args = build_dpdk_eal_args(config);

        std::vector<char *> argv;
        argv.reserve(eal_args.size());
        for (std::string &arg : eal_args)
        {
            argv.push_back(arg.data());
        }

        const int eal_rc = rte_eal_init(static_cast<int>(argv.size()), argv.data());
        if (eal_rc < 0)
        {
            ++stats_.rx_errors;
            return make_dpdk_result(true, "rte_eal_init() 调用失败");
        }

        std::uint16_t port_id = static_cast<std::uint16_t>(config.dpdk_port_id);
        if (!resolve_port_id(config, port_id) || !rte_eth_dev_is_valid_port(port_id))
        {
            ++stats_.rx_errors;
            return make_dpdk_result(true, "解析 DPDK 设备对应端口失败: " + config.dpdk_pci_addr);
        }

        impl_->port_id = port_id;
        impl_->mempool = rte_pktmbuf_pool_create("rxtech_dpdk_mbuf_pool",
                                                 config.dpdk_mempool_size,
                                                 config.dpdk_mbuf_cache_size,
                                                 0,
                                                 RTE_MBUF_DEFAULT_BUF_SIZE,
                                                 rte_socket_id());
        if (impl_->mempool == nullptr)
        {
            ++stats_.rx_errors;
            return make_dpdk_result(true, "rte_pktmbuf_pool_create() 调用失败");
        }

        rte_eth_conf port_conf{};
        if (rte_eth_dev_configure(port_id, 1, 1, &port_conf) < 0)
        {
            ++stats_.rx_errors;
            return make_dpdk_result(true, "rte_eth_dev_configure() 调用失败");
        }
        if (rte_eth_rx_queue_setup(port_id,
                                   0,
                                   static_cast<std::uint16_t>(config.dpdk_rx_desc),
                                   rte_eth_dev_socket_id(port_id),
                                   nullptr,
                                   impl_->mempool) < 0)
        {
            ++stats_.rx_errors;
            return make_dpdk_result(true, "rte_eth_rx_queue_setup() 调用失败");
        }
        if (rte_eth_tx_queue_setup(port_id,
                                   0,
                                   static_cast<std::uint16_t>(config.dpdk_tx_desc),
                                   rte_eth_dev_socket_id(port_id),
                                   nullptr) < 0)
        {
            ++stats_.rx_errors;
            return make_dpdk_result(true, "rte_eth_tx_queue_setup() 调用失败");
        }
        if (rte_eth_dev_start(port_id) < 0)
        {
            ++stats_.rx_errors;
            return make_dpdk_result(true, "rte_eth_dev_start() 调用失败");
        }

        impl_->started = true;
        rte_ether_addr mac{};
        rte_eth_macaddr_get(port_id, &mac);
        std::memcpy(impl_->local_mac.data(), mac.addr_bytes, impl_->local_mac.size());
        if (!config.receiver_ipv4.empty())
        {
            in_addr addr{};
            if (inet_pton(AF_INET, config.receiver_ipv4.c_str(), &addr) == 1)
            {
                impl_->local_ip_be = ntohl(addr.s_addr);
            }
        }
        // Drain any stale packets already queued in the NIC hardware ring before
        // starting normal operation so that background traffic received prior to
        // this application launching does not appear in the statistics.
        {
            std::vector<rte_mbuf *> drain_mbufs(64);
            std::uint16_t drained = 0;
            do
            {
                drained = rte_eth_rx_burst(impl_->port_id, 0, drain_mbufs.data(),
                                           static_cast<std::uint16_t>(drain_mbufs.size()));
                for (std::uint16_t i = 0; i < drained; ++i)
                {
                    rte_pktmbuf_free(drain_mbufs[i]);
                }
            } while (drained > 0);
        }

        stats_ = {};
        stats_.queue_id = 0;
        BackendInitResult result;
        result.ok = true;
        return result;
#else
        (void)config;
        return make_dpdk_result(false, "未链接 DPDK 运行时；请在 Linux 环境安装 libdpdk 后重新构建");
#endif
    }

    bool DpdkIngress::recv_burst(UdpDatagramBurst &burst, std::uint32_t max_burst)
    {
        burst.datagrams.clear();
#if defined(__linux__) && defined(RXTECH_HAS_DPDK_RUNTIME)
        if (impl_ == nullptr || !impl_->started)
        {
            return false;
        }

        ++stats_.rx_polls;
        const std::uint16_t budget = static_cast<std::uint16_t>(std::min<std::uint32_t>(max_burst, 64U));
        std::vector<rte_mbuf *> mbufs(budget);
        const std::uint16_t received = rte_eth_rx_burst(impl_->port_id, 0, mbufs.data(), budget);
        if (received == 0U)
        {
            ++stats_.empty_polls;
            return true;
        }

        burst.datagrams.reserve(received);
        for (std::uint16_t index = 0; index < received; ++index)
        {
            rte_mbuf *mbuf = mbufs[index];
            if (impl_->maybe_reply_arp(mbuf))
            {
                rte_pktmbuf_free(mbuf);
                continue;
            }
            const std::uint32_t frame_len = rte_pktmbuf_pkt_len(mbuf);
            if (frame_len < kUdpPayloadOffset)
            {
                rte_pktmbuf_free(mbuf);
                ++stats_.backend_drops;
                continue;
            }
            UdpDatagramDesc datagram;
            datagram.raw_frame_data = rte_pktmbuf_mtod(mbuf, std::uint8_t *);
            datagram.raw_frame_len = frame_len;
            datagram.payload_data = datagram.raw_frame_data + kUdpPayloadOffset;
            datagram.payload_len = frame_len - static_cast<std::uint32_t>(kUdpPayloadOffset);
            datagram.ts_ns = steady_clock_now_ns();
            datagram.queue_id = 0;
            datagram.cookie = reinterpret_cast<std::uintptr_t>(mbuf);
            datagram.backend_kind = BackendKind::dpdk;
            burst.datagrams.push_back(datagram);
            ++stats_.rx_packets;
            stats_.rx_bytes += datagram.raw_frame_len;
        }
        return true;
#else
        (void)max_burst;
        return false;
#endif
    }

    void DpdkIngress::release_burst(UdpDatagramBurst &burst)
    {
#if defined(__linux__) && defined(RXTECH_HAS_DPDK_RUNTIME)
        for (const UdpDatagramDesc &datagram : burst.datagrams)
        {
            auto *mbuf = reinterpret_cast<rte_mbuf *>(datagram.cookie);
            if (mbuf != nullptr)
            {
                rte_pktmbuf_free(mbuf);
            }
        }
#endif
        burst.datagrams.clear();
    }

    BackendStats DpdkIngress::stats() const
    {
        BackendStats snapshot = stats_;
#if defined(__linux__) && defined(RXTECH_HAS_DPDK_RUNTIME)
        if (impl_ != nullptr)
        {
            snapshot.arp_request_packets = impl_->arp_request_packets;
            snapshot.arp_reply_packets = impl_->arp_reply_packets;
        }
#endif
        return snapshot;
    }

    void DpdkIngress::shutdown()
    {
#if defined(__linux__) && defined(RXTECH_HAS_DPDK_RUNTIME)
        if (impl_ != nullptr)
        {
            impl_->cleanup();
        }
#endif
    }

} // namespace rxtech
