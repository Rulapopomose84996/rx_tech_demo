#include <cstdint>
#include <array>
#include <cstring>
#include <iostream>
#include <vector>

#include "linux_socket_backend.h"
#include "rxtech/rx_config.h"

#if defined(__linux__)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{

#if defined(__linux__)
    constexpr std::size_t kFullAncillaryControlBytes =
        CMSG_SPACE(sizeof(in_pktinfo)) + CMSG_SPACE(sizeof(std::uint32_t));

    struct AncillarySample
    {
        bool ok = false;
        bool saw_pktinfo = false;
        bool saw_kernel_drop = false;
        std::uint32_t kernel_drop_total = 0U;
        int msg_flags = 0;
    };

    std::uint16_t reserve_loopback_port()
    {
        const int socket_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0)
        {
            return 0U;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(0U);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(socket_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0)
        {
            ::close(socket_fd);
            return 0U;
        }

        socklen_t address_len = sizeof(address);
        if (::getsockname(socket_fd, reinterpret_cast<sockaddr *>(&address), &address_len) != 0)
        {
            ::close(socket_fd);
            return 0U;
        }

        const std::uint16_t port = ntohs(address.sin_port);
        ::close(socket_fd);
        return port;
    }

    std::vector<std::uint8_t> make_sample_udp_payload()
    {
        std::vector<std::uint8_t> payload = {0x03U, 0xFFU, 0xAAU, 0x55U, 0x01U, 0x00U, 0x00U, 0x00U,
                                             0x22U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
        payload.resize(2048U, 0xABU);
        return payload;
    }

    bool send_udp_payload(std::uint16_t port, const std::vector<std::uint8_t> &payload)
    {
        const int socket_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0)
        {
            return false;
        }

        sockaddr_in source{};
        source.sin_family = AF_INET;
        source.sin_port = htons(0U);
        source.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(socket_fd, reinterpret_cast<const sockaddr *>(&source), sizeof(source)) != 0)
        {
            ::close(socket_fd);
            return false;
        }

        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(port);
        target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const ssize_t sent = ::sendto(socket_fd, payload.data(), payload.size(), 0,
                                      reinterpret_cast<const sockaddr *>(&target), sizeof(target));
        ::close(socket_fd);
        return sent == static_cast<ssize_t>(payload.size());
    }

    int create_ancillary_socket(std::uint16_t port, int rcvbuf_bytes)
    {
        const int socket_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0)
        {
            return -1;
        }

        int reuse_addr = 1;
        (void)::setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));
        if (::setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_bytes, sizeof(rcvbuf_bytes)) != 0)
        {
            ::close(socket_fd);
            return -1;
        }

        int enable_pktinfo = 1;
        if (::setsockopt(socket_fd, IPPROTO_IP, IP_PKTINFO, &enable_pktinfo, sizeof(enable_pktinfo)) != 0)
        {
            ::close(socket_fd);
            return -1;
        }

        int enable_kernel_drop_count = 1;
        if (::setsockopt(socket_fd, SOL_SOCKET, SO_RXQ_OVFL, &enable_kernel_drop_count,
                         sizeof(enable_kernel_drop_count)) != 0)
        {
            ::close(socket_fd);
            return -1;
        }

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        (void)::setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(socket_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0)
        {
            ::close(socket_fd);
            return -1;
        }

        return socket_fd;
    }

    AncillarySample receive_udp_with_control(int socket_fd, std::size_t control_bytes)
    {
        AncillarySample sample;
        std::array<std::uint8_t, 256U> payload{};
        std::vector<char> control(control_bytes, 0);
        sockaddr_in peer{};
        iovec iov{};
        iov.iov_base = payload.data();
        iov.iov_len = payload.size();

        msghdr msg{};
        msg.msg_name = &peer;
        msg.msg_namelen = sizeof(peer);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control.data();
        msg.msg_controllen = control.size();

        const ssize_t received = ::recvmsg(socket_fd, &msg, 0);
        if (received < 0)
        {
            return sample;
        }

        sample.ok = true;
        sample.msg_flags = msg.msg_flags;
        for (cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg))
        {
            if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_PKTINFO &&
                cmsg->cmsg_len >= CMSG_LEN(sizeof(in_pktinfo)))
            {
                sample.saw_pktinfo = true;
            }
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_RXQ_OVFL &&
                cmsg->cmsg_len >= CMSG_LEN(sizeof(std::uint32_t)))
            {
                sample.saw_kernel_drop = true;
                std::memcpy(&sample.kernel_drop_total, CMSG_DATA(cmsg), sizeof(sample.kernel_drop_total));
            }
        }

        return sample;
    }

    rxtech::RxConfig make_socket_config(const std::uint16_t port)
    {
        rxtech::RxConfig config = rxtech::load_default_config();
        config.backend_name = "socket";
        config.receiver_ipv4 = "127.0.0.1";
        config.allowed_source_ipv4 = "127.0.0.1";
        config.allowed_dest_port = port;
        config.protocol_udp_packet_size = 2048U;
        return config;
    }
#endif

} // namespace

int main()
{
#if !defined(__linux__)
    return 0;
#else
    const std::uint16_t port = reserve_loopback_port();
    if (port == 0U)
    {
        std::cerr << "failed to reserve loopback UDP port\n";
        return 1;
    }

    rxtech::RxConfig config = make_socket_config(port);

    rxtech::LinuxSocketIngress backend;
    const rxtech::BackendInitResult init_result = backend.init(config);
    if (!init_result.ok)
    {
        std::cerr << "socket backend init failed: " << init_result.reason << '\n';
        return 1;
    }

    const std::vector<std::uint8_t> payload_a = make_sample_udp_payload();
    std::vector<std::uint8_t> payload_b = make_sample_udp_payload();
    payload_b[0] = 0x11U;
    payload_b[1] = 0x22U;
    if (!send_udp_payload(port, payload_a) || !send_udp_payload(port, payload_b))
    {
        backend.shutdown();
        std::cerr << "failed to send loopback UDP payload\n";
        return 1;
    }

    rxtech::UdpDatagramBurst burst;
    if (!backend.recv_burst(burst, 4U))
    {
        backend.shutdown();
        std::cerr << "socket backend recv_burst failed\n";
        return 1;
    }
    if (burst.datagrams.size() != 2U)
    {
        backend.release_burst(burst);
        backend.shutdown();
        std::cerr << "expected exactly two received datagrams, got " << burst.datagrams.size() << '\n';
        return 1;
    }

    const rxtech::UdpDatagramDesc &datagram_a = burst.datagrams[0];
    const rxtech::UdpDatagramDesc &datagram_b = burst.datagrams[1];
    if (datagram_a.payload_data == nullptr || datagram_b.payload_data == nullptr)
    {
        backend.release_burst(burst);
        backend.shutdown();
        std::cerr << "expected UDP payload view to be populated\n";
        return 1;
    }
    if (datagram_a.payload_len != 2048U || datagram_b.payload_len != 2048U)
    {
        backend.release_burst(burst);
        backend.shutdown();
        std::cerr << "expected UDP payload lengths to remain stable\n";
        return 1;
    }
    if (datagram_a.payload_data == datagram_b.payload_data)
    {
        backend.release_burst(burst);
        backend.shutdown();
        std::cerr << "expected payload_data pointers to stay slot-distinct within one burst\n";
        return 1;
    }
    if (datagram_a.src_ipv4_be != 0x7F000001U || datagram_b.src_ipv4_be != 0x7F000001U)
    {
        backend.release_burst(burst);
        backend.shutdown();
        std::cerr << "expected loopback source IPv4 in big-endian form\n";
        return 1;
    }
    if (datagram_a.dst_port != port || datagram_b.dst_port != port)
    {
        backend.release_burst(burst);
        backend.shutdown();
        std::cerr << "expected destination port to match the bound port\n";
        return 1;
    }
    if (datagram_a.backend_kind != rxtech::BackendKind::socket ||
        datagram_b.backend_kind != rxtech::BackendKind::socket)
    {
        backend.release_burst(burst);
        backend.shutdown();
        std::cerr << "expected socket backend kind on received datagram\n";
        return 1;
    }
    if (datagram_a.raw_frame_data != nullptr || datagram_b.raw_frame_data != nullptr ||
        datagram_a.raw_frame_len != 0U || datagram_b.raw_frame_len != 0U)
    {
        backend.release_burst(burst);
        backend.shutdown();
        std::cerr << "expected datagram-first socket ingress without synthetic raw frames\n";
        return 1;
    }
    if (std::memcmp(datagram_a.payload_data, payload_a.data(), payload_a.size()) != 0 ||
        std::memcmp(datagram_b.payload_data, payload_b.data(), payload_b.size()) != 0)
    {
        backend.release_burst(burst);
        backend.shutdown();
        std::cerr << "expected received payload bytes to remain readable until release\n";
        return 1;
    }

    const rxtech::BackendStats stats = backend.stats();
    if (stats.receive_batches < 1U)
    {
        backend.release_burst(burst);
        backend.shutdown();
        std::cerr << "expected at least one recorded receive batch\n";
        return 1;
    }
    if (stats.max_burst_size < 1U)
    {
        backend.release_burst(burst);
        backend.shutdown();
        std::cerr << "expected max burst size to reflect the received datagram\n";
        return 1;
    }
    if (stats.kernel_drop_count != 0U)
    {
        backend.release_burst(burst);
        backend.shutdown();
        std::cerr << "expected no kernel drops for the basic loopback receive case\n";
        return 1;
    }

    backend.release_burst(burst);
    backend.shutdown();

    const std::uint16_t timeout_poll_port = reserve_loopback_port();
    if (timeout_poll_port == 0U)
    {
        std::cerr << "failed to reserve loopback UDP port for timeout empty poll\n";
        return 1;
    }

    rxtech::RxConfig timeout_poll_config = make_socket_config(timeout_poll_port);
    timeout_poll_config.socket_batch_timeout_ms = 5U;
    rxtech::LinuxSocketIngress timeout_poll_backend;
    const rxtech::BackendInitResult timeout_poll_init = timeout_poll_backend.init(timeout_poll_config);
    if (!timeout_poll_init.ok)
    {
        std::cerr << "socket backend init for timeout empty poll failed: " << timeout_poll_init.reason << '\n';
        return 1;
    }

    rxtech::UdpDatagramBurst timeout_poll_burst;
    if (!timeout_poll_backend.recv_burst(timeout_poll_burst, 4U))
    {
        timeout_poll_backend.shutdown();
        std::cerr << "socket backend timeout empty poll recv_burst failed\n";
        return 1;
    }
    if (!timeout_poll_burst.datagrams.empty())
    {
        timeout_poll_backend.release_burst(timeout_poll_burst);
        timeout_poll_backend.shutdown();
        std::cerr << "expected timeout empty poll to return no datagrams\n";
        return 1;
    }
    const rxtech::BackendStats timeout_poll_stats = timeout_poll_backend.stats();
    if (timeout_poll_stats.empty_polls < 1U)
    {
        timeout_poll_backend.release_burst(timeout_poll_burst);
        timeout_poll_backend.shutdown();
        std::cerr << "expected timeout empty poll to increment empty poll accounting\n";
        return 1;
    }
    timeout_poll_backend.release_burst(timeout_poll_burst);
    timeout_poll_backend.shutdown();

    constexpr std::uint32_t full_burst_budget = 4U;
    const std::uint16_t full_burst_port = reserve_loopback_port();
    if (full_burst_port == 0U)
    {
        std::cerr << "failed to reserve loopback UDP port for full burst\n";
        return 1;
    }

    rxtech::LinuxSocketIngress full_burst_backend;
    const rxtech::BackendInitResult full_burst_init = full_burst_backend.init(make_socket_config(full_burst_port));
    if (!full_burst_init.ok)
    {
        std::cerr << "socket backend init for full burst failed: " << full_burst_init.reason << '\n';
        return 1;
    }

    std::vector<std::vector<std::uint8_t>> full_burst_payloads;
    full_burst_payloads.reserve(full_burst_budget);
    for (std::uint32_t index = 0; index < full_burst_budget; ++index)
    {
        std::vector<std::uint8_t> payload = make_sample_udp_payload();
        payload[0] = static_cast<std::uint8_t>(0x30U + index);
        payload[1] = static_cast<std::uint8_t>(0x40U + index);
        full_burst_payloads.push_back(payload);
        if (!send_udp_payload(full_burst_port, full_burst_payloads.back()))
        {
            full_burst_backend.shutdown();
            std::cerr << "failed to send full-burst UDP payload\n";
            return 1;
        }
    }

    rxtech::UdpDatagramBurst full_burst;
    if (!full_burst_backend.recv_burst(full_burst, full_burst_budget))
    {
        full_burst_backend.shutdown();
        std::cerr << "socket backend full-burst recv_burst failed\n";
        return 1;
    }
    if (full_burst.datagrams.size() != full_burst_budget)
    {
        full_burst_backend.release_burst(full_burst);
        full_burst_backend.shutdown();
        std::cerr << "expected exactly " << full_burst_budget << " received datagrams in full burst, got "
                  << full_burst.datagrams.size() << '\n';
        return 1;
    }
    if (full_burst.datagrams.capacity() < full_burst_budget)
    {
        full_burst_backend.release_burst(full_burst);
        full_burst_backend.shutdown();
        std::cerr << "expected fixed-capacity descriptor storage to cover the requested full burst\n";
        return 1;
    }
    for (std::uint32_t index = 0; index < full_burst_budget; ++index)
    {
        const rxtech::UdpDatagramDesc &datagram = full_burst.datagrams[index];
        if (datagram.payload_data == nullptr || datagram.payload_len != full_burst_payloads[index].size() ||
            datagram.raw_frame_data != nullptr || datagram.raw_frame_len != 0U)
        {
            full_burst_backend.release_burst(full_burst);
            full_burst_backend.shutdown();
            std::cerr << "expected full-burst datagram slot " << index << " to expose datagram-only storage\n";
            return 1;
        }
        if (std::memcmp(datagram.payload_data, full_burst_payloads[index].data(), full_burst_payloads[index].size()) !=
            0)
        {
            full_burst_backend.release_burst(full_burst);
            full_burst_backend.shutdown();
            std::cerr << "expected full-burst payload bytes to remain readable until release\n";
            return 1;
        }
        if (index > 0U && datagram.payload_data == full_burst.datagrams[index - 1U].payload_data)
        {
            full_burst_backend.release_burst(full_burst);
            full_burst_backend.shutdown();
            std::cerr << "expected full-burst payload_data pointers to remain slot-distinct\n";
            return 1;
        }
    }

    const rxtech::BackendStats full_burst_stats = full_burst_backend.stats();
    if (full_burst_stats.max_burst_size != full_burst_budget)
    {
        full_burst_backend.release_burst(full_burst);
        full_burst_backend.shutdown();
        std::cerr << "expected max_burst_size to equal the full burst budget\n";
        return 1;
    }

    full_burst_backend.release_burst(full_burst);
    full_burst_backend.shutdown();

    if (rxtech::socket_control_bytes_for_tests() < kFullAncillaryControlBytes)
    {
        std::cerr
            << "expected socket ancillary control buffer to reserve space for both pktinfo and kernel-drop metadata\n";
        return 1;
    }

    const std::uint16_t ancillary_port = reserve_loopback_port();
    if (ancillary_port == 0U)
    {
        std::cerr << "failed to reserve loopback UDP port for ancillary receive\n";
        return 1;
    }
    const int ancillary_socket_fd = create_ancillary_socket(ancillary_port, 1 << 20);
    if (ancillary_socket_fd < 0)
    {
        std::cerr << "failed to create ancillary test socket\n";
        return 1;
    }
    std::vector<std::uint8_t> small_payload(64U, 0x5AU);
    if (!send_udp_payload(ancillary_port, small_payload))
    {
        ::close(ancillary_socket_fd);
        std::cerr << "failed to send ancillary loopback UDP payload\n";
        return 1;
    }
    const AncillarySample ancillary_sample = receive_udp_with_control(ancillary_socket_fd, kFullAncillaryControlBytes);
    if (!ancillary_sample.ok || !ancillary_sample.saw_pktinfo || (ancillary_sample.msg_flags & MSG_CTRUNC) != 0)
    {
        ::close(ancillary_socket_fd);
        std::cerr << "expected full ancillary buffer to carry pktinfo without control truncation\n";
        return 1;
    }
    ::close(ancillary_socket_fd);

    const std::uint16_t ctrunc_port = reserve_loopback_port();
    if (ctrunc_port == 0U)
    {
        std::cerr << "failed to reserve loopback UDP port for control truncation receive\n";
        return 1;
    }
    const int ctrunc_socket_fd = create_ancillary_socket(ctrunc_port, 1 << 20);
    if (ctrunc_socket_fd < 0)
    {
        std::cerr << "failed to create control truncation test socket\n";
        return 1;
    }
    if (!send_udp_payload(ctrunc_port, small_payload))
    {
        ::close(ctrunc_socket_fd);
        std::cerr << "failed to send control truncation test payload\n";
        return 1;
    }
    const AncillarySample ctrunc_sample = receive_udp_with_control(ctrunc_socket_fd, 1U);
    ::close(ctrunc_socket_fd);
    if (!ctrunc_sample.ok || (ctrunc_sample.msg_flags & MSG_CTRUNC) == 0)
    {
        std::cerr << "expected undersized live ancillary buffer to report MSG_CTRUNC\n";
        return 1;
    }

    std::array<char, CMSG_SPACE(sizeof(std::uint32_t))> drop_control{};
    msghdr drop_msg{};
    drop_msg.msg_control = drop_control.data();
    drop_msg.msg_controllen = drop_control.size();
    cmsghdr *drop_cmsg = CMSG_FIRSTHDR(&drop_msg);
    if (drop_cmsg == nullptr)
    {
        std::cerr << "failed to allocate kernel-drop control message\n";
        return 1;
    }
    drop_cmsg->cmsg_level = SOL_SOCKET;
    drop_cmsg->cmsg_type = SO_RXQ_OVFL;
    drop_cmsg->cmsg_len = CMSG_LEN(sizeof(std::uint32_t));
    const std::uint32_t kernel_drop_total = 11U;
    std::memcpy(CMSG_DATA(drop_cmsg), &kernel_drop_total, sizeof(kernel_drop_total));

    std::uint32_t last_seen_kernel_drop_count = 3U;
    const std::uint64_t kernel_drop_delta =
        rxtech::update_kernel_drop_count_from_cmsg(drop_msg, last_seen_kernel_drop_count);
    if (kernel_drop_delta != 8U || last_seen_kernel_drop_count != kernel_drop_total)
    {
        std::cerr << "expected kernel-drop control parsing to report only the incremental delta\n";
        return 1;
    }

    msghdr no_drop_msg{};
    std::uint32_t unchanged_kernel_drop_count = last_seen_kernel_drop_count;
    const std::uint64_t no_drop_delta =
        rxtech::update_kernel_drop_count_from_cmsg(no_drop_msg, unchanged_kernel_drop_count);
    if (no_drop_delta != 0U || unchanged_kernel_drop_count != last_seen_kernel_drop_count)
    {
        std::cerr << "expected missing kernel-drop control data to leave counters unchanged\n";
        return 1;
    }

    const std::uint16_t empty_poll_port = reserve_loopback_port();
    if (empty_poll_port == 0U)
    {
        std::cerr << "failed to reserve loopback UDP port for empty poll\n";
        return 1;
    }

    rxtech::RxConfig empty_poll_config = make_socket_config(empty_poll_port);
    empty_poll_config.socket_nonblocking = true;

    rxtech::LinuxSocketIngress empty_poll_backend;
    const rxtech::BackendInitResult empty_poll_init = empty_poll_backend.init(empty_poll_config);
    if (!empty_poll_init.ok)
    {
        std::cerr << "socket backend init for empty poll failed: " << empty_poll_init.reason << '\n';
        return 1;
    }

    rxtech::UdpDatagramBurst empty_burst;
    if (!empty_poll_backend.recv_burst(empty_burst, 4U))
    {
        empty_poll_backend.shutdown();
        std::cerr << "socket backend empty recv_burst failed\n";
        return 1;
    }
    if (!empty_burst.datagrams.empty())
    {
        empty_poll_backend.release_burst(empty_burst);
        empty_poll_backend.shutdown();
        std::cerr << "expected empty nonblocking poll to return no datagrams\n";
        return 1;
    }

    const rxtech::BackendStats empty_stats = empty_poll_backend.stats();
    if (empty_stats.empty_polls < 1U)
    {
        empty_poll_backend.shutdown();
        std::cerr << "expected empty poll accounting for nonblocking recvmmsg\n";
        return 1;
    }

    empty_poll_backend.shutdown();
    return 0;
#endif
}
