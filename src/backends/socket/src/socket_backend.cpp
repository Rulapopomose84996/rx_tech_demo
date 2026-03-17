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
    int recv_fd = -1;
    int send_fd = -1;
    sockaddr_in bind_addr{};
    std::vector<std::uint8_t> send_buffer;
    bool enable_internal_traffic = false;
    std::uint32_t queue_id = 0;
    std::uint32_t packet_size_bytes = 256U;
    std::uint32_t poll_timeout_ms = 50U;
    int recv_buffer_bytes = 4 * 1024 * 1024;

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
    impl_->send_buffer.assign(impl_->packet_size_bytes, 0xABU);

    impl_->recv_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (impl_->recv_fd < 0) {
        ++stats_.rx_errors;
        return make_socket_error("socket() failed");
    }

    const int reuse = 1;
    (void)setsockopt(impl_->recv_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#ifdef SO_REUSEPORT
    (void)setsockopt(impl_->recv_fd, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#endif
    (void)setsockopt(impl_->recv_fd,
                     SOL_SOCKET,
                     SO_RCVBUF,
                     reinterpret_cast<const char*>(&impl_->recv_buffer_bytes),
                     sizeof(impl_->recv_buffer_bytes));

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
        BackendInitResult result;
        result.reason = "invalid bind_address: " + config.bind_address;
        return result;
    }

    if (bind(impl_->recv_fd,
             reinterpret_cast<const sockaddr*>(&impl_->bind_addr),
             sizeof(impl_->bind_addr)) != 0) {
        ++stats_.rx_errors;
        impl_->cleanup();
        return make_socket_error("bind() failed");
    }

    if (impl_->enable_internal_traffic) {
        impl_->send_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (impl_->send_fd < 0) {
            ++stats_.rx_errors;
            impl_->cleanup();
            return make_socket_error("send socket() failed");
        }

        if (connect(impl_->send_fd,
                    reinterpret_cast<const sockaddr*>(&impl_->bind_addr),
                    sizeof(impl_->bind_addr)) != 0) {
            ++stats_.rx_errors;
            impl_->cleanup();
            return make_socket_error("connect() failed");
        }
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
    if (impl_ == nullptr || impl_->recv_fd < 0) {
        return false;
    }

    owned_packets_.clear();
    ++stats_.rx_polls;

    pollfd pfd{};
    pfd.fd = impl_->recv_fd;
    pfd.events = POLLIN;
    const int poll_rc = poll(&pfd, 1, static_cast<int>(impl_->poll_timeout_ms));
    if (poll_rc < 0) {
        ++stats_.rx_errors;
        return false;
    }
    if (poll_rc == 0 || (pfd.revents & POLLIN) == 0) {
        ++stats_.empty_polls;
        return true;
    }

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
