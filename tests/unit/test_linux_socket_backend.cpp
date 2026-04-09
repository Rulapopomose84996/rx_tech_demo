#include <cstdint>
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
        std::vector<std::uint8_t> payload = {
            0x03U, 0xFFU, 0xAAU, 0x55U,
            0x01U, 0x00U,
            0x00U, 0x00U,
            0x22U, 0x00U,
            0x02U, 0x00U,
            0x00U, 0x00U, 0x00U, 0x00U};
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
        const ssize_t sent = ::sendto(socket_fd,
                                      payload.data(),
                                      payload.size(),
                                      0,
                                      reinterpret_cast<const sockaddr *>(&target),
                                      sizeof(target));
        ::close(socket_fd);
        return sent == static_cast<ssize_t>(payload.size());
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

    rxtech::RxConfig config = rxtech::load_default_config();
    config.backend_name = "socket";
    config.receiver_ipv4 = "127.0.0.1";
    config.allowed_source_ipv4 = "127.0.0.1";
    config.allowed_dest_port = port;
    config.protocol_udp_packet_size = 2048U;

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

    backend.release_burst(burst);
    backend.shutdown();

    const std::uint16_t empty_poll_port = reserve_loopback_port();
    if (empty_poll_port == 0U)
    {
        std::cerr << "failed to reserve loopback UDP port for empty poll\n";
        return 1;
    }

    rxtech::RxConfig empty_poll_config = rxtech::load_default_config();
    empty_poll_config.backend_name = "socket";
    empty_poll_config.receiver_ipv4 = "127.0.0.1";
    empty_poll_config.allowed_source_ipv4 = "127.0.0.1";
    empty_poll_config.allowed_dest_port = empty_poll_port;
    empty_poll_config.protocol_udp_packet_size = 2048U;
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
