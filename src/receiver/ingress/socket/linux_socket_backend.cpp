#include "linux_socket_backend.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "rxtech/rx_config.h"
#include "rxtech/time_utils.h"

#if defined(__linux__)
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace rxtech
{

    namespace
    {

        constexpr std::size_t kEthernetHeaderBytes = 14U;
        constexpr std::size_t kIpv4HeaderBytes = 20U;
        constexpr std::size_t kUdpHeaderBytes = 8U;
        constexpr std::size_t kSyntheticFramePrefixBytes =
            kEthernetHeaderBytes + kIpv4HeaderBytes + kUdpHeaderBytes;
        constexpr std::uint16_t kEtherTypeIpv4 = 0x0800U;
        constexpr std::uint8_t kIpVersionAndIhl = 0x45U;
        constexpr std::uint8_t kIpTtl = 64U;
        constexpr std::uint8_t kIpProtocolUdp = 17U;
        constexpr std::size_t kMaxUdpPayloadBytes = 65507U;

        BackendInitResult make_socket_result(bool available, const std::string &reason)
        {
            BackendInitResult result;
            result.available = available;
            result.reason = reason;
            return result;
        }

        void write_u16_be(std::uint8_t *data, std::uint16_t value)
        {
            data[0] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
            data[1] = static_cast<std::uint8_t>(value & 0xFFU);
        }

        void write_u32_be(std::uint8_t *data, std::uint32_t value)
        {
            data[0] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
            data[1] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
            data[2] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
            data[3] = static_cast<std::uint8_t>(value & 0xFFU);
        }

        std::string socket_error_message(const std::string &action)
        {
#if defined(__linux__)
            return action + " 失败: " + std::strerror(errno);
#else
            (void)action;
            return "Linux socket 后端仅支持在 Linux 上运行";
#endif
        }

        std::string effective_socket_frame_dest_ip(const RxConfig &config)
        {
            return config.receiver_ipv4.empty() ? effective_socket_bind_ip(config) : config.receiver_ipv4;
        }

    } // namespace

    struct LinuxSocketIngress::Impl
    {
#if defined(__linux__)
        int socket_fd = -1;
        bool nonblocking = false;
        std::vector<std::vector<std::uint8_t>> burst_storage;
        std::vector<std::uint8_t> recv_buffer;
        std::uint32_t dest_ipv4_be = 0;
        std::uint16_t bind_port = 0;
        std::uint16_t next_ip_identification = 1;

        void cleanup()
        {
            if (socket_fd >= 0)
            {
                ::close(socket_fd);
                socket_fd = -1;
            }
            nonblocking = false;
            burst_storage.clear();
            recv_buffer.clear();
            dest_ipv4_be = 0;
            bind_port = 0;
            next_ip_identification = 1;
        }
#endif
    };

    LinuxSocketIngress::LinuxSocketIngress() : impl_(new Impl())
    {
    }

    LinuxSocketIngress::~LinuxSocketIngress()
    {
        shutdown();
    }

    std::string LinuxSocketIngress::name() const
    {
        return "socket";
    }

    BackendInitResult LinuxSocketIngress::init(const RxConfig &config)
    {
        stats_ = {};
#if defined(__linux__)
        if (impl_ == nullptr)
        {
            return make_socket_result(false, "Linux socket 后端内部状态不可用");
        }

        impl_->cleanup();

        const std::string bind_ip = effective_socket_bind_ip(config);
        const std::string frame_dest_ip = effective_socket_frame_dest_ip(config);
        const std::uint16_t bind_port = effective_socket_bind_port(config);
        if (bind_ip.empty() || bind_port == 0U)
        {
            return make_socket_result(true, "socket 绑定地址或端口配置无效");
        }

        sockaddr_in bind_addr{};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = htons(bind_port);
        if (inet_pton(AF_INET, bind_ip.c_str(), &bind_addr.sin_addr) != 1)
        {
            ++stats_.rx_errors;
            return make_socket_result(true, "socket_bind_ip 不是合法 IPv4 地址: " + bind_ip);
        }

        in_addr frame_dest_addr{};
        if (inet_pton(AF_INET, frame_dest_ip.c_str(), &frame_dest_addr) != 1)
        {
            ++stats_.rx_errors;
            return make_socket_result(true, "receiver_ipv4 不是合法 IPv4 地址: " + frame_dest_ip);
        }

        const int socket_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0)
        {
            ++stats_.rx_errors;
            return make_socket_result(true, socket_error_message("创建 UDP socket"));
        }

        int reuse_addr = 1;
        (void)::setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));

        const int rcvbuf = static_cast<int>(std::min<std::uint32_t>(
            config.socket_rcvbuf_bytes,
            static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
        if (::setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) != 0)
        {
            ++stats_.rx_errors;
            ::close(socket_fd);
            return make_socket_result(true, socket_error_message("设置 SO_RCVBUF"));
        }

        if (config.socket_batch_timeout_ms > 0U)
        {
            timeval timeout{};
            timeout.tv_sec = static_cast<long>(config.socket_batch_timeout_ms / 1000U);
            timeout.tv_usec = static_cast<long>((config.socket_batch_timeout_ms % 1000U) * 1000U);
            if (::setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
            {
                ++stats_.rx_errors;
                ::close(socket_fd);
                return make_socket_result(true, socket_error_message("设置 SO_RCVTIMEO"));
            }
        }

        if (config.socket_nonblocking)
        {
            const int flags = ::fcntl(socket_fd, F_GETFL, 0);
            if (flags < 0 || ::fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) != 0)
            {
                ++stats_.rx_errors;
                ::close(socket_fd);
                return make_socket_result(true, socket_error_message("设置 O_NONBLOCK"));
            }
        }

        if (::bind(socket_fd,
                   reinterpret_cast<const sockaddr *>(&bind_addr),
                   sizeof(bind_addr)) != 0)
        {
            ++stats_.rx_errors;
            ::close(socket_fd);
            return make_socket_result(true, socket_error_message("绑定 UDP socket"));
        }

        impl_->socket_fd = socket_fd;
        impl_->nonblocking = config.socket_nonblocking;
        impl_->dest_ipv4_be = ntohl(frame_dest_addr.s_addr);
        impl_->bind_port = bind_port;
        impl_->recv_buffer.resize(kMaxUdpPayloadBytes);

        stats_.queue_id = config.queue_id;
        stats_.frame_size = static_cast<std::uint32_t>(kSyntheticFramePrefixBytes + config.protocol_udp_packet_size);

        BackendInitResult result;
        result.ok = true;
        return result;
#else
        (void)config;
        return make_socket_result(false, "未在 Linux 环境构建 Linux socket 后端");
#endif
    }

    bool LinuxSocketIngress::recv_burst(UdpDatagramBurst &burst, std::uint32_t max_burst)
    {
        burst.datagrams.clear();
#if defined(__linux__)
        if (impl_ == nullptr || impl_->socket_fd < 0)
        {
            return false;
        }

        ++stats_.rx_polls;
        if (max_burst == 0U)
        {
            ++stats_.empty_polls;
            return true;
        }

        if (impl_->burst_storage.size() < max_burst)
        {
            impl_->burst_storage.resize(max_burst);
        }

        for (std::uint32_t index = 0; index < max_burst; ++index)
        {
            sockaddr_storage remote_addr{};
            socklen_t remote_addr_len = sizeof(remote_addr);
            const int recv_flags = (index == 0U && !impl_->nonblocking) ? 0 : MSG_DONTWAIT;
            const ssize_t received = ::recvfrom(impl_->socket_fd,
                                                impl_->recv_buffer.data(),
                                                impl_->recv_buffer.size(),
                                                recv_flags,
                                                reinterpret_cast<sockaddr *>(&remote_addr),
                                                &remote_addr_len);
            if (received < 0)
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                ++stats_.rx_errors;
                return false;
            }
            if (received == 0)
            {
                continue;
            }
            if (remote_addr.ss_family != AF_INET)
            {
                ++stats_.backend_drops;
                continue;
            }

            const auto *remote_ipv4 = reinterpret_cast<const sockaddr_in *>(&remote_addr);
            const std::uint32_t source_ipv4_be = ntohl(remote_ipv4->sin_addr.s_addr);
            const std::uint16_t source_port = ntohs(remote_ipv4->sin_port);

            std::vector<std::uint8_t> &frame = impl_->burst_storage[index];
            frame.resize(kSyntheticFramePrefixBytes + static_cast<std::size_t>(received));
            std::fill(frame.begin(), frame.begin() + 12U, 0U);
            write_u16_be(frame.data() + 12U, kEtherTypeIpv4);

            std::uint8_t *ip_header = frame.data() + kEthernetHeaderBytes;
            ip_header[0] = kIpVersionAndIhl;
            ip_header[1] = 0U;
            write_u16_be(ip_header + 2U,
                         static_cast<std::uint16_t>(kIpv4HeaderBytes + kUdpHeaderBytes + received));
            write_u16_be(ip_header + 4U, impl_->next_ip_identification++);
            write_u16_be(ip_header + 6U, 0U);
            ip_header[8] = kIpTtl;
            ip_header[9] = kIpProtocolUdp;
            write_u16_be(ip_header + 10U, 0U);
            write_u32_be(ip_header + 12U, source_ipv4_be);
            write_u32_be(ip_header + 16U, impl_->dest_ipv4_be);

            std::uint8_t *udp_header = ip_header + kIpv4HeaderBytes;
            write_u16_be(udp_header + 0U, source_port);
            write_u16_be(udp_header + 2U, impl_->bind_port);
            write_u16_be(udp_header + 4U,
                         static_cast<std::uint16_t>(kUdpHeaderBytes + received));
            write_u16_be(udp_header + 6U, 0U);

            std::memcpy(udp_header + kUdpHeaderBytes,
                        impl_->recv_buffer.data(),
                        static_cast<std::size_t>(received));

            UdpDatagramDesc datagram;
            datagram.raw_frame_data = frame.data();
            datagram.raw_frame_len = static_cast<std::uint32_t>(frame.size());
            datagram.payload_data = frame.data() + kSyntheticFramePrefixBytes;
            datagram.payload_len = static_cast<std::uint32_t>(received);
            datagram.src_ipv4_be = source_ipv4_be;
            datagram.dst_ipv4_be = impl_->dest_ipv4_be;
            datagram.src_port = source_port;
            datagram.dst_port = impl_->bind_port;
            datagram.ts_ns = steady_clock_now_ns();
            datagram.queue_id = stats_.queue_id;
            datagram.cookie = static_cast<std::uintptr_t>(index);
            datagram.backend_kind = BackendKind::socket;
            burst.datagrams.push_back(datagram);

            ++stats_.rx_packets;
            stats_.rx_bytes += datagram.raw_frame_len;
        }

        if (burst.datagrams.empty())
        {
            ++stats_.empty_polls;
        }
        return true;
#else
        (void)max_burst;
        return false;
#endif
    }

    void LinuxSocketIngress::release_burst(UdpDatagramBurst &burst)
    {
        burst.datagrams.clear();
    }

    BackendStats LinuxSocketIngress::stats() const
    {
        return stats_;
    }

    void LinuxSocketIngress::shutdown()
    {
#if defined(__linux__)
        if (impl_ != nullptr)
        {
            impl_->cleanup();
        }
#endif
    }

} // namespace rxtech
