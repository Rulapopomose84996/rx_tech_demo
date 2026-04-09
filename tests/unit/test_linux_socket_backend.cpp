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

    const std::vector<std::uint8_t> payload = make_sample_udp_payload();
    if (!send_udp_payload(port, payload))
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
    if (burst.datagrams.size() != 1U)
    {
        backend.release_burst(burst);
        backend.shutdown();
        std::cerr << "expected exactly one received packet, got " << burst.datagrams.size() << '\n';
        return 1;
    }

    if (burst.datagrams.front().payload_data == nullptr || burst.datagrams.front().payload_len != 2048U)
    {
        backend.release_burst(burst);
        backend.shutdown();
        std::cerr << "expected UDP payload view to be populated\n";
        return 1;
    }
    if (burst.datagrams.front().payload_data[0] != 0x03U || burst.datagrams.front().payload_data[1] != 0xFFU)
    {
        backend.release_burst(burst);
        backend.shutdown();
        std::cerr << "expected UDP payload bytes to match sample payload\n";
        return 1;
    }

    backend.release_burst(burst);
    backend.shutdown();
    return 0;
#endif
}
