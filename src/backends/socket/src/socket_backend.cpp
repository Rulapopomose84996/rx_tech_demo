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

#include "rxtech/rx_config.h"
#include "rxtech/time_utils.h"

#ifdef __linux__
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace rxtech {

struct SocketBackend::Impl {
#ifdef __linux__
    int recv_fd = -1;
    int send_fd = -1;
    sockaddr_in bind_addr{};
    std::vector<std::uint8_t> send_buffer;
    bool enable_internal_traffic = false;
    std::uint32_t queue_id = 0;
    std::uint32_t packet_size_bytes = 256U;

    void cleanup() {
        if (recv_fd >= 0) {
            close(recv_fd);
            recv_fd = -1;
        }
        if (send_fd >= 0) {
            close(send_fd);
            send_fd = -1;
        }
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

bool SocketBackend::init(const RxConfig& config) {
    stats_ = {};
#ifdef __linux__
    impl_->cleanup();
    impl_->queue_id = config.queue_id;
    impl_->packet_size_bytes = std::max<std::uint32_t>(config.packet_size_bytes, 64U);
    impl_->enable_internal_traffic = config.enable_internal_traffic;
    impl_->send_buffer.assign(impl_->packet_size_bytes, 0xABU);

    impl_->recv_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (impl_->recv_fd < 0) {
        ++stats_.rx_errors;
        return false;
    }

    const int reuse = 1;
    (void)setsockopt(impl_->recv_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#ifdef SO_REUSEPORT
    (void)setsockopt(impl_->recv_fd, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#endif

    const int flags = fcntl(impl_->recv_fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(impl_->recv_fd, F_SETFL, flags | O_NONBLOCK);
    }

    std::memset(&impl_->bind_addr, 0, sizeof(impl_->bind_addr));
    impl_->bind_addr.sin_family = AF_INET;
    impl_->bind_addr.sin_port = htons(config.udp_port);
    if (config.bind_address.empty() || config.bind_address == "0.0.0.0") {
        impl_->bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, config.bind_address.c_str(), &impl_->bind_addr.sin_addr) != 1) {
        ++stats_.rx_errors;
        impl_->cleanup();
        return false;
    }

    if (bind(impl_->recv_fd,
             reinterpret_cast<const sockaddr*>(&impl_->bind_addr),
             sizeof(impl_->bind_addr)) != 0) {
        ++stats_.rx_errors;
        impl_->cleanup();
        return false;
    }

    if (impl_->enable_internal_traffic) {
        impl_->send_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (impl_->send_fd < 0) {
            ++stats_.rx_errors;
            impl_->cleanup();
            return false;
        }

        if (connect(impl_->send_fd,
                    reinterpret_cast<const sockaddr*>(&impl_->bind_addr),
                    sizeof(impl_->bind_addr)) != 0) {
            ++stats_.rx_errors;
            impl_->cleanup();
            return false;
        }
    }

    stats_.queue_id = config.queue_id;
    return true;
#else
    (void)config;
    return true;
#endif
}

bool SocketBackend::recv_burst(RxBurst& burst, std::uint32_t max_burst) {
    burst.packets.clear();
#ifdef __linux__
    if (impl_ == nullptr || impl_->recv_fd < 0) {
        return false;
    }

    owned_packets_.clear();
    ++stats_.rx_polls;

    const std::uint32_t budget = std::max<std::uint32_t>(1U, std::min<std::uint32_t>(max_burst, 32U));
    if (impl_->enable_internal_traffic && impl_->send_fd >= 0) {
        const std::uint32_t send_count = std::min<std::uint32_t>(budget, 4U);
        for (std::uint32_t index = 0; index < send_count; ++index) {
            if (send(impl_->send_fd,
                     reinterpret_cast<const char*>(impl_->send_buffer.data()),
                     static_cast<int>(impl_->send_buffer.size()),
                     0) < 0) {
                ++stats_.rx_errors;
            }
        }
    }

    std::vector<mmsghdr> messages(budget);
    std::vector<iovec> iovecs(budget);
    std::vector<std::vector<std::uint8_t>> buffers(
        budget,
        std::vector<std::uint8_t>(impl_->packet_size_bytes));

    for (std::uint32_t index = 0; index < budget; ++index) {
        iovecs[index].iov_base = buffers[index].data();
        iovecs[index].iov_len = buffers[index].size();
        std::memset(&messages[index], 0, sizeof(mmsghdr));
        messages[index].msg_hdr.msg_iov = &iovecs[index];
        messages[index].msg_hdr.msg_iovlen = 1;
    }

    const int received = recvmmsg(impl_->recv_fd, messages.data(), budget, MSG_DONTWAIT, nullptr);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ++stats_.empty_polls;
            return true;
        }
        ++stats_.rx_errors;
        return false;
    }

    if (received == 0) {
        ++stats_.empty_polls;
        return true;
    }

    owned_packets_.reserve(static_cast<std::size_t>(received));
    burst.packets.reserve(static_cast<std::size_t>(received));
    for (int index = 0; index < received; ++index) {
        buffers[static_cast<std::size_t>(index)].resize(messages[static_cast<std::size_t>(index)].msg_len);
        owned_packets_.push_back(std::move(buffers[static_cast<std::size_t>(index)]));

        PacketDesc packet;
        packet.data = owned_packets_.back().data();
        packet.len = static_cast<std::uint32_t>(owned_packets_.back().size());
        packet.ts_ns = steady_clock_now_ns();
        packet.queue_id = impl_->queue_id;
        packet.cookie = static_cast<std::uintptr_t>(index);
        burst.packets.push_back(packet);

        ++stats_.rx_packets;
        stats_.rx_bytes += packet.len;
    }

    return true;
#else
    owned_packets_.assign(std::min<std::uint32_t>(max_burst, 4U), std::vector<std::uint8_t>(256U, 0xABU));
    burst.packets.reserve(owned_packets_.size());
    for (std::size_t index = 0; index < owned_packets_.size(); ++index) {
        PacketDesc packet;
        packet.data = owned_packets_[index].data();
        packet.len = static_cast<std::uint32_t>(owned_packets_[index].size());
        packet.ts_ns = steady_clock_now_ns();
        packet.cookie = static_cast<std::uintptr_t>(index);
        burst.packets.push_back(packet);
        ++stats_.rx_packets;
        stats_.rx_bytes += packet.len;
    }
    return true;
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
