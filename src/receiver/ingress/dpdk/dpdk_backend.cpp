#include "dpdk_backend.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

#include "arp_responder.h"
#include "rxtech/byte_order.h"
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
        constexpr std::size_t kEthernetHeaderBytes = 14U;
        constexpr std::size_t kIpv4MinimumHeaderBytes = 20U;
        constexpr std::size_t kUdpHeaderBytes = 8U;
        constexpr std::uint16_t kEtherTypeIpv4 = 0x0800U;
        constexpr std::uint16_t kEtherTypeArp = 0x0806U;
        constexpr std::uint8_t kIpv4Version = 4U;
        constexpr std::uint8_t kIpProtoUdp = 17U;

        BackendInitResult make_dpdk_result(bool available, const std::string &reason)
        {
            BackendInitResult result;
            result.available = available;
            result.reason = reason;
            return result;
        }

#if defined(__linux__) && defined(RXTECH_HAS_DPDK_RUNTIME)
        constexpr std::uint64_t kDpdkLinkCheckIntervalEmptyPolls = 256U;

        bool query_link_up(std::uint16_t port_id, bool &link_up)
        {
            rte_eth_link link{};
            if (rte_eth_link_get_nowait(port_id, &link) != 0)
            {
                return false;
            }
            link_up = link.link_status != 0;
            return true;
        }

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
                    if (rte_eth_dev_info_get(candidate, &info) == 0 && info.device != nullptr &&
                        info.device->name != nullptr)
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

    DpdkDatagramAdapter::DpdkDatagramAdapter(const std::uint32_t local_ip_be) : local_ip_be_(local_ip_be) {}

    bool DpdkDatagramAdapter::adapt_frame(const PacketDesc &packet, BackendStats &stats,
                                          UdpDatagramDesc &datagram) const
    {
        datagram = {};
        if (packet.data == nullptr || packet.len < kEthernetHeaderBytes)
        {
            return false;
        }

        const std::uint16_t ether_type = byte_order::read_u16_be(packet.data + 12U);
        if (ether_type == kEtherTypeArp)
        {
            ArpRequestInfo request{};
            if (parse_arp_request(packet.data, packet.len, local_ip_be_, request))
            {
                ++stats.arp_request_packets;
            }
            return false;
        }

        if (ether_type != kEtherTypeIpv4 || packet.len < kEthernetHeaderBytes + kIpv4MinimumHeaderBytes)
        {
            return false;
        }

        const std::size_t ip_offset = kEthernetHeaderBytes;
        const std::uint8_t version_ihl = packet.data[ip_offset];
        const std::uint8_t version = static_cast<std::uint8_t>(version_ihl >> 4U);
        const std::uint8_t ihl_words = static_cast<std::uint8_t>(version_ihl & 0x0FU);
        const std::size_t ip_header_bytes = static_cast<std::size_t>(ihl_words) * 4U;
        if (version != kIpv4Version || ihl_words < 5U || packet.len < ip_offset + ip_header_bytes)
        {
            return false;
        }

        const std::uint16_t total_length = byte_order::read_u16_be(packet.data + ip_offset + 2U);
        if (total_length < ip_header_bytes || packet.len < ip_offset + total_length)
        {
            return false;
        }

        const std::uint16_t flags_and_offset = byte_order::read_u16_be(packet.data + ip_offset + 6U);
        if ((flags_and_offset & 0x3FFFU) != 0U)
        {
            ++stats.backend_drops;
            return false;
        }

        if (packet.data[ip_offset + 9U] != kIpProtoUdp)
        {
            return false;
        }

        const std::uint8_t *udp_header = packet.data + ip_offset + ip_header_bytes;
        const std::size_t ip_payload_bytes = static_cast<std::size_t>(total_length) - ip_header_bytes;
        if (ip_payload_bytes < kUdpHeaderBytes)
        {
            return false;
        }

        const std::uint16_t udp_length = byte_order::read_u16_be(udp_header + 4U);
        if (udp_length < kUdpHeaderBytes || ip_payload_bytes < udp_length)
        {
            return false;
        }

        datagram.payload_data = udp_header + kUdpHeaderBytes;
        datagram.payload_len = static_cast<std::uint32_t>(udp_length - kUdpHeaderBytes);
        datagram.raw_frame_data = packet.data;
        datagram.raw_frame_len = packet.len;
        datagram.src_ipv4_be = byte_order::read_u32_be(packet.data + ip_offset + 12U);
        datagram.dst_ipv4_be = byte_order::read_u32_be(packet.data + ip_offset + 16U);
        datagram.src_port = byte_order::read_u16_be(udp_header + 0U);
        datagram.dst_port = byte_order::read_u16_be(udp_header + 2U);
        // Keep ingress timestamps in the same steady_clock nanosecond domain used by timeout checks.
        datagram.ts_ns = packet.ts_ns;
        datagram.queue_id = packet.queue_id;
        datagram.cookie = packet.cookie;
        datagram.backend_kind = BackendKind::dpdk;
        return true;
    }

    struct DpdkIngress::Impl
    {
#if defined(__linux__) && defined(RXTECH_HAS_DPDK_RUNTIME)
        rte_mempool *mempool = nullptr;
        std::uint16_t port_id = 0;
        bool started = false;
        std::array<std::uint8_t, 6> local_mac{};
        std::uint32_t local_ip_be = 0;
        std::uint64_t empty_polls_since_link_check = 0;
        bool link_up = true;

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

        bool maybe_reply_arp(rte_mbuf *mbuf, BackendStats &stats)
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
            ++stats.arp_request_packets;

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
                ++stats.arp_reply_packets;
            }
            return true;
        }

        bool extract_udp_datagram(rte_mbuf *mbuf, UdpDatagramDesc &datagram) const
        {
            if (mbuf == nullptr)
            {
                return false;
            }

            PacketDesc packet;
            packet.data = rte_pktmbuf_mtod(mbuf, std::uint8_t *);
            packet.len = rte_pktmbuf_pkt_len(mbuf);
            packet.ts_ns = steady_clock_now_ns();
            packet.queue_id = 0U;
            packet.cookie = reinterpret_cast<std::uintptr_t>(mbuf);

            BackendStats ignored_stats;
            DpdkDatagramAdapter adapter(local_ip_be);
            return adapter.adapt_frame(packet, ignored_stats, datagram);
        }
#endif
    };

    DpdkIngress::DpdkIngress() : impl_(new Impl()) {}

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
        impl_->mempool =
            rte_pktmbuf_pool_create("rxtech_dpdk_mbuf_pool", config.dpdk_mempool_size, config.dpdk_mbuf_cache_size, 0,
                                    RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
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
        if (rte_eth_rx_queue_setup(port_id, 0, static_cast<std::uint16_t>(config.dpdk_rx_desc),
                                   rte_eth_dev_socket_id(port_id), nullptr, impl_->mempool) < 0)
        {
            ++stats_.rx_errors;
            return make_dpdk_result(true, "rte_eth_rx_queue_setup() 调用失败");
        }
        if (rte_eth_tx_queue_setup(port_id, 0, static_cast<std::uint16_t>(config.dpdk_tx_desc),
                                   rte_eth_dev_socket_id(port_id), nullptr) < 0)
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
            std::array<rte_mbuf *, kDpdkMaxBurstSize> drain_mbufs{};
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
        if (config.max_burst > kDpdkMaxBurstSize)
        {
            std::fprintf(stderr,
                         "[dpdk] configured max_burst=%u exceeds backend limit=%u; recv_burst will clamp to %u\n",
                         config.max_burst, kDpdkMaxBurstSize, kDpdkMaxBurstSize);
        }

        bool link_up = true;
        if (!query_link_up(port_id, link_up))
        {
            ++stats_.rx_errors;
            return make_dpdk_result(true, "rte_eth_link_get_nowait() 调用失败");
        }
        impl_->link_up = link_up;
        impl_->empty_polls_since_link_check = 0;
        if (!link_up)
        {
            ++stats_.rx_errors;
            return make_dpdk_result(true, "DPDK 端口链路未就绪");
        }

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
        const std::uint16_t budget =
            static_cast<std::uint16_t>(std::min<std::uint32_t>(std::min<std::uint32_t>(max_burst, kDpdkMaxBurstSize),
                                                               static_cast<std::uint32_t>(burst.datagrams.max_size())));
        std::array<rte_mbuf *, kDpdkMaxBurstSize> mbufs{};
        const std::uint16_t received = rte_eth_rx_burst(impl_->port_id, 0, mbufs.data(), budget);
        if (received == 0U)
        {
            ++stats_.empty_polls;
            ++impl_->empty_polls_since_link_check;
            if (impl_->empty_polls_since_link_check >= kDpdkLinkCheckIntervalEmptyPolls)
            {
                bool link_up = true;
                if (!query_link_up(impl_->port_id, link_up))
                {
                    ++stats_.rx_errors;
                    std::fprintf(stderr, "[dpdk] failed to query link status for port %u\n", impl_->port_id);
                    return false;
                }
                impl_->empty_polls_since_link_check = 0;
                impl_->link_up = link_up;
                if (!link_up)
                {
                    ++stats_.rx_errors;
                    std::fprintf(stderr, "[dpdk] link down detected on port %u; stopping receiver loop\n",
                                 impl_->port_id);
                    return false;
                }
            }
            return true;
        }

        impl_->empty_polls_since_link_check = 0;
        ++stats_.receive_batches;
        stats_.max_burst_size = std::max<std::uint32_t>(stats_.max_burst_size, static_cast<std::uint32_t>(received));

        for (std::uint16_t index = 0; index < received; ++index)
        {
            rte_mbuf *mbuf = mbufs[index];
            ++stats_.rx_packets;
            stats_.rx_bytes += rte_pktmbuf_pkt_len(mbuf);
            if (impl_->maybe_reply_arp(mbuf, stats_))
            {
                rte_pktmbuf_free(mbuf);
                continue;
            }
            UdpDatagramDesc datagram;
            if (!impl_->extract_udp_datagram(mbuf, datagram))
            {
                rte_pktmbuf_free(mbuf);
                continue;
            }

            burst.datagrams.push_back(datagram);
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
        return stats_;
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
