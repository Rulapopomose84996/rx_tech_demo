#include "linux_socket_backend.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "rxtech/rx_config.h"
#include "rxtech/time_utils.h"

#if defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
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

        constexpr std::size_t kMaxUdpPayloadBytes = 65507U;
        constexpr std::size_t kSocketControlBytes = CMSG_SPACE(sizeof(in_pktinfo));

        BackendInitResult make_socket_result(bool available, const std::string &reason)
        {
            BackendInitResult result;
            result.available = available;
            result.reason = reason;
            return result;
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

        std::uint32_t socket_dest_ipv4_be(const sockaddr_in &peer,
                                          const in_pktinfo &pktinfo,
                                          const std::uint32_t default_dest_ipv4_be)
        {
            if (pktinfo.ipi_addr.s_addr != 0)
            {
                return ntohl(pktinfo.ipi_addr.s_addr);
            }
            return default_dest_ipv4_be != 0U ? default_dest_ipv4_be : ntohl(peer.sin_addr.s_addr);
        }

    } // namespace

    struct LinuxSocketIngress::Impl
    {
#if defined(__linux__)
        struct SocketSlot
        {
            std::array<std::uint8_t, kMaxUdpPayloadBytes> payload{};
            sockaddr_in peer{};
            in_pktinfo pktinfo{};
            std::array<char, kSocketControlBytes> control{};
            std::uint32_t payload_len = 0;
            bool truncated = false;
        };

        int socket_fd = -1;
        bool nonblocking = false;
        std::uint32_t batch_timeout_ms = 0;
        std::vector<SocketSlot> slots;
        std::vector<iovec> iovecs;
        std::vector<mmsghdr> msgs;
        std::uint32_t dest_ipv4_be = 0;
        std::uint16_t bind_port = 0;

        void prepare_batch(const std::uint32_t capacity)
        {
            if (capacity == 0U || slots.size() >= capacity)
            {
                return;
            }

            slots.resize(capacity);
            iovecs.resize(capacity);
            msgs.resize(capacity);
            for (std::uint32_t index = 0; index < capacity; ++index)
            {
                SocketSlot &slot = slots[index];
                iovec &iov = iovecs[index];
                mmsghdr &msg = msgs[index];
                std::memset(&slot.peer, 0, sizeof(slot.peer));
                std::memset(&slot.pktinfo, 0, sizeof(slot.pktinfo));
                std::memset(slot.control.data(), 0, slot.control.size());
                std::memset(&iov, 0, sizeof(iov));
                std::memset(&msg, 0, sizeof(msg));
                iov.iov_base = slot.payload.data();
                iov.iov_len = slot.payload.size();
                msg.msg_hdr.msg_name = &slot.peer;
                msg.msg_hdr.msg_namelen = sizeof(slot.peer);
                msg.msg_hdr.msg_iov = &iov;
                msg.msg_hdr.msg_iovlen = 1;
                msg.msg_hdr.msg_control = slot.control.data();
                msg.msg_hdr.msg_controllen = slot.control.size();
            }
        }

        void cleanup()
        {
            if (socket_fd >= 0)
            {
                ::close(socket_fd);
                socket_fd = -1;
            }
            nonblocking = false;
            batch_timeout_ms = 0;
            slots.clear();
            iovecs.clear();
            msgs.clear();
            dest_ipv4_be = 0;
            bind_port = 0;
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

        int enable_pktinfo = 1;
        if (::setsockopt(socket_fd, IPPROTO_IP, IP_PKTINFO, &enable_pktinfo, sizeof(enable_pktinfo)) != 0)
        {
            ++stats_.rx_errors;
            ::close(socket_fd);
            return make_socket_result(true, socket_error_message("设置 IP_PKTINFO"));
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
        impl_->batch_timeout_ms = config.socket_batch_timeout_ms;
        impl_->dest_ipv4_be = ntohl(frame_dest_addr.s_addr);
        impl_->bind_port = bind_port;

        stats_.queue_id = config.queue_id;
        stats_.frame_size = config.protocol_udp_packet_size;

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

        if (burst.datagrams.capacity() < max_burst)
        {
            burst.datagrams.reserve(max_burst);
        }

        impl_->prepare_batch(max_burst);
        for (std::uint32_t index = 0; index < max_burst; ++index)
        {
            Impl::SocketSlot &slot = impl_->slots[index];
            mmsghdr &msg = impl_->msgs[index];
            std::memset(&slot.peer, 0, sizeof(slot.peer));
            std::memset(&slot.pktinfo, 0, sizeof(slot.pktinfo));
            std::memset(slot.control.data(), 0, slot.control.size());
            slot.payload_len = 0U;
            slot.truncated = false;
            msg.msg_hdr.msg_namelen = sizeof(slot.peer);
            msg.msg_hdr.msg_controllen = slot.control.size();
            msg.msg_hdr.msg_flags = 0;
            msg.msg_len = 0U;
        }

        timespec timeout{};
        timespec *timeout_ptr = nullptr;
        if (!impl_->nonblocking && impl_->batch_timeout_ms > 0U)
        {
            timeout.tv_sec = static_cast<time_t>(impl_->batch_timeout_ms / 1000U);
            timeout.tv_nsec = static_cast<long>((impl_->batch_timeout_ms % 1000U) * 1000000U);
            timeout_ptr = &timeout;
        }

        const int recv_flags = impl_->nonblocking ? MSG_DONTWAIT : MSG_WAITFORONE;
        const int received = ::recvmmsg(impl_->socket_fd,
                                        impl_->msgs.data(),
                                        max_burst,
                                        recv_flags,
                                        timeout_ptr);
        if (received < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            {
                ++stats_.empty_polls;
                return true;
            }
            ++stats_.rx_errors;
            return false;
        }

        if (received == 0)
        {
            ++stats_.empty_polls;
            return true;
        }

        ++stats_.receive_batches;
        stats_.max_burst_size = std::max<std::uint32_t>(
            stats_.max_burst_size,
            static_cast<std::uint32_t>(received));

        burst.datagrams.resize(static_cast<std::size_t>(received));
        std::size_t datagram_count = 0U;
        for (int index = 0; index < received; ++index)
        {
            Impl::SocketSlot &slot = impl_->slots[static_cast<std::size_t>(index)];
            const mmsghdr &msg = impl_->msgs[static_cast<std::size_t>(index)];
            slot.payload_len = static_cast<std::uint32_t>(msg.msg_len);
            slot.truncated = (msg.msg_hdr.msg_flags & MSG_TRUNC) != 0;
            if (slot.peer.sin_family != AF_INET)
            {
                ++stats_.backend_drops;
                continue;
            }

            for (cmsghdr *cmsg = CMSG_FIRSTHDR(const_cast<msghdr *>(&msg.msg_hdr));
                 cmsg != nullptr;
                 cmsg = CMSG_NXTHDR(const_cast<msghdr *>(&msg.msg_hdr), cmsg))
            {
                if (cmsg->cmsg_level == IPPROTO_IP &&
                    cmsg->cmsg_type == IP_PKTINFO &&
                    cmsg->cmsg_len >= CMSG_LEN(sizeof(in_pktinfo)))
                {
                    std::memcpy(&slot.pktinfo, CMSG_DATA(cmsg), sizeof(slot.pktinfo));
                    break;
                }
            }

            UdpDatagramDesc &datagram = burst.datagrams[datagram_count++];
            datagram.payload_data = slot.payload.data();
            datagram.payload_len = slot.payload_len;
            datagram.raw_frame_data = nullptr;
            datagram.raw_frame_len = 0U;
            datagram.src_ipv4_be = ntohl(slot.peer.sin_addr.s_addr);
            datagram.dst_ipv4_be = socket_dest_ipv4_be(slot.peer, slot.pktinfo, impl_->dest_ipv4_be);
            datagram.src_port = ntohs(slot.peer.sin_port);
            datagram.dst_port = impl_->bind_port;
            datagram.ts_ns = steady_clock_now_ns();
            datagram.queue_id = stats_.queue_id;
            datagram.cookie = static_cast<std::uintptr_t>(index);
            datagram.backend_kind = BackendKind::socket;
            datagram.truncated = slot.truncated;
            burst.datagrams.push_back(datagram);

            ++stats_.rx_packets;
            stats_.rx_bytes += datagram.payload_len;
        }

        burst.datagrams.resize(datagram_count);

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
