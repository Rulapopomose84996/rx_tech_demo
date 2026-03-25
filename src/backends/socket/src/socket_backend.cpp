#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include "rxtech/socket_backend.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <vector>

#include "rxtech/demo_protocol.h"
#include "rxtech/rx_config.h"
#include "rxtech/time_utils.h"

#ifdef __linux__
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace rxtech {

namespace {

#ifdef __linux__
BackendInitResult make_socket_error(const std::string& message) {
    BackendInitResult result;
    result.reason = message + ": " + std::strerror(errno);
    return result;
}
#endif

}  // namespace

struct SocketBackend::Impl {
#ifdef __linux__
    struct Channel {
        int recv_fd = -1;
        int send_fd = -1;
        sockaddr_in bind_addr{};
        std::vector<std::uint8_t> send_buffer;
        std::uint32_t port_id = 0;
        std::uint16_t udp_port = 0;
        std::uint64_t next_block_id = 1;
    };

    std::vector<Channel> channels;
    bool enable_internal_traffic = false;
    std::uint32_t queue_id = 0;
    std::uint32_t packet_size_bytes = 256U;
    std::uint32_t poll_timeout_ms = 50U;
    int recv_buffer_bytes = 4 * 1024 * 1024;

    static void append_u16_be(std::vector<std::uint8_t>& out, std::uint16_t value) {
        out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    }

    static void append_u32_be(std::vector<std::uint8_t>& out, std::uint32_t value) {
        out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    }

    static void append_u64_be(std::vector<std::uint8_t>& out, std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
        }
    }

    static std::vector<std::uint8_t> build_demo_packet(const Channel& channel, std::uint32_t packet_size_bytes) {
        const std::uint32_t payload_bytes = std::max<std::uint32_t>(packet_size_bytes, kDemoHeaderWireBytes + 1U) -
                                            static_cast<std::uint32_t>(kDemoHeaderWireBytes);
        std::vector<std::uint8_t> packet;
        packet.reserve(kDemoHeaderWireBytes + payload_bytes);
        append_u32_be(packet, kDemoMagic);
        append_u16_be(packet, kDemoVersion);
        append_u16_be(packet, kDemoFlagFirstFragment | kDemoFlagLastFragment);
        append_u32_be(packet, channel.port_id);
        append_u64_be(packet, channel.next_block_id);
        append_u32_be(packet, payload_bytes);
        append_u16_be(packet, 0U);
        append_u16_be(packet, 1U);
        append_u16_be(packet, static_cast<std::uint16_t>(payload_bytes));
        append_u16_be(packet, 0U);
        packet.resize(kDemoHeaderWireBytes + payload_bytes, static_cast<std::uint8_t>(0xA0U + channel.port_id));
        return packet;
    }

    void cleanup() {
        for (Channel& channel : channels) {
            if (channel.recv_fd >= 0) {
                close(channel.recv_fd);
                channel.recv_fd = -1;
            }
            if (channel.send_fd >= 0) {
                close(channel.send_fd);
                channel.send_fd = -1;
            }
        }
        channels.clear();
    }
#endif
};

SocketBackend::SocketBackend() : impl_(new Impl()) {
}

SocketBackend::~SocketBackend() {
    shutdown();
}

std::string SocketBackend::name() const {
    return "socket";
}

BackendInitResult SocketBackend::init(const RxConfig& config) {
    stats_ = {};
#ifdef __linux__
    impl_->cleanup();
    impl_->queue_id = config.queue_id;
    impl_->packet_size_bytes = std::max<std::uint32_t>(config.packet_size_bytes, 64U);
    impl_->enable_internal_traffic = config.enable_internal_traffic;
    impl_->poll_timeout_ms = config.socket_poll_timeout_ms;
    impl_->recv_buffer_bytes = static_cast<int>(
        std::max<std::uint32_t>(4U * 1024U * 1024U,
                                impl_->packet_size_bytes * std::max<std::uint32_t>(config.max_burst, 1U) * 256U));
    std::vector<ReceiverEndpoint> endpoints = config.receiver_endpoints;
    if (endpoints.empty()) {
        endpoints.push_back(ReceiverEndpoint{0U, config.bind_address, config.udp_port});
    } else if (endpoints.size() == 1U) {
        endpoints[0].bind_address = config.bind_address;
        endpoints[0].udp_port = config.udp_port;
    }

    impl_->channels.reserve(endpoints.size());
    for (const ReceiverEndpoint& endpoint : endpoints) {
        Impl::Channel channel;
        channel.port_id = endpoint.port_id;
        channel.udp_port = endpoint.udp_port;

        channel.recv_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (channel.recv_fd < 0) {
            ++stats_.rx_errors;
            impl_->cleanup();
            return make_socket_error("socket() failed");
        }

        const int reuse = 1;
        (void)setsockopt(channel.recv_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#ifdef SO_REUSEPORT
        (void)setsockopt(channel.recv_fd, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#endif
        (void)setsockopt(channel.recv_fd,
                         SOL_SOCKET,
                         SO_RCVBUF,
                         reinterpret_cast<const char*>(&impl_->recv_buffer_bytes),
                         sizeof(impl_->recv_buffer_bytes));

        const int flags = fcntl(channel.recv_fd, F_GETFL, 0);
        if (flags >= 0) {
            (void)fcntl(channel.recv_fd, F_SETFL, flags | O_NONBLOCK);
        }

        std::memset(&channel.bind_addr, 0, sizeof(channel.bind_addr));
        channel.bind_addr.sin_family = AF_INET;
        channel.bind_addr.sin_port = htons(endpoint.udp_port);
        if (endpoint.bind_address.empty() || endpoint.bind_address == "0.0.0.0") {
            channel.bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        } else if (inet_pton(AF_INET, endpoint.bind_address.c_str(), &channel.bind_addr.sin_addr) != 1) {
            ++stats_.rx_errors;
            impl_->cleanup();
            BackendInitResult result;
            result.reason = "invalid bind_address: " + endpoint.bind_address;
            return result;
        }

        if (bind(channel.recv_fd,
                 reinterpret_cast<const sockaddr*>(&channel.bind_addr),
                 sizeof(channel.bind_addr)) != 0) {
            ++stats_.rx_errors;
            impl_->cleanup();
            return make_socket_error("bind() failed");
        }

        if (impl_->enable_internal_traffic) {
            channel.send_fd = socket(AF_INET, SOCK_DGRAM, 0);
            if (channel.send_fd < 0) {
                ++stats_.rx_errors;
                impl_->cleanup();
                return make_socket_error("send socket() failed");
            }

            if (connect(channel.send_fd,
                        reinterpret_cast<const sockaddr*>(&channel.bind_addr),
                        sizeof(channel.bind_addr)) != 0) {
                ++stats_.rx_errors;
                impl_->cleanup();
                return make_socket_error("connect() failed");
            }
            channel.send_buffer = Impl::build_demo_packet(channel, impl_->packet_size_bytes);
        } else {
            channel.send_buffer.assign(impl_->packet_size_bytes, 0xABU);
        }

        impl_->channels.push_back(std::move(channel));
    }

    if (impl_->channels.empty()) {
        BackendInitResult result;
        result.reason = "no socket endpoints configured";
        return result;
    }

    stats_.queue_id = config.queue_id;
    BackendInitResult result;
    result.ok = true;
    return result;
#else
    (void)config;
    BackendInitResult result;
    result.available = false;
    result.reason = "socket backend requires Linux with recvmmsg support";
    return result;
#endif
}

bool SocketBackend::recv_burst(RxBurst& burst, std::uint32_t max_burst) {
    burst.packets.clear();
#ifdef __linux__
    if (impl_ == nullptr || impl_->channels.empty()) {
        return false;
    }

    owned_packets_.clear();
    ++stats_.rx_polls;

    std::vector<pollfd> poll_fds(impl_->channels.size());
    for (std::size_t index = 0; index < impl_->channels.size(); ++index) {
        poll_fds[index].fd = impl_->channels[index].recv_fd;
        poll_fds[index].events = POLLIN;
        poll_fds[index].revents = 0;
    }

    if (impl_->enable_internal_traffic) {
        for (Impl::Channel& channel : impl_->channels) {
            channel.send_buffer = Impl::build_demo_packet(channel, impl_->packet_size_bytes);
            if (channel.send_fd >= 0 &&
                send(channel.send_fd,
                     reinterpret_cast<const char*>(channel.send_buffer.data()),
                     static_cast<int>(channel.send_buffer.size()),
                     0) < 0) {
                ++stats_.rx_errors;
            }
            ++channel.next_block_id;
        }
    }

    const int poll_rc = poll(poll_fds.data(), static_cast<nfds_t>(poll_fds.size()), static_cast<int>(impl_->poll_timeout_ms));
    if (poll_rc < 0) {
        if (errno == EINTR) {
            return true;
        }
        ++stats_.rx_errors;
        return false;
    }
    if (poll_rc == 0) {
        ++stats_.empty_polls;
        return true;
    }

    const std::uint32_t budget = std::max<std::uint32_t>(1U, std::min<std::uint32_t>(max_burst, 32U));
    owned_packets_.reserve(budget);
    burst.packets.reserve(budget);
    for (std::size_t channel_index = 0; channel_index < impl_->channels.size() && burst.packets.size() < budget; ++channel_index) {
        if ((poll_fds[channel_index].revents & POLLIN) == 0) {
            continue;
        }

        Impl::Channel& channel = impl_->channels[channel_index];
        while (burst.packets.size() < budget) {
            std::vector<std::uint8_t> buffer(impl_->packet_size_bytes);
            const ssize_t received =
                recv(channel.recv_fd, reinterpret_cast<char*>(buffer.data()), buffer.size(), MSG_DONTWAIT);
            if (received < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                ++stats_.rx_errors;
                return false;
            }
            if (received == 0) {
                break;
            }

            buffer.resize(static_cast<std::size_t>(received));
            owned_packets_.push_back(std::move(buffer));

            PacketDesc packet;
            packet.data = owned_packets_.back().data();
            packet.len = static_cast<std::uint32_t>(owned_packets_.back().size());
            packet.ts_ns = steady_clock_now_ns();
            packet.port_id = channel.port_id;
            packet.queue_id = impl_->queue_id;
            packet.cookie = static_cast<std::uintptr_t>(burst.packets.size());
            burst.packets.push_back(packet);

            ++stats_.rx_packets;
            stats_.rx_bytes += packet.len;
        }
    }

    if (burst.packets.empty()) {
        ++stats_.empty_polls;
    }

    return true;
#else
    (void)max_burst;
    return false;
#endif
}

void SocketBackend::release_burst(RxBurst& burst) {
    burst.packets.clear();
    owned_packets_.clear();
}

BackendStats SocketBackend::stats() const {
    return stats_;
}

void SocketBackend::shutdown() {
#ifdef __linux__
    if (impl_ != nullptr) {
        impl_->cleanup();
    }
#endif
    owned_packets_.clear();
}

}  // namespace rxtech
