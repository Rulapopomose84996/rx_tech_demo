#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rxtech::replay
{

    /// Raw socket sender using AF_PACKET (Linux only).
    ///
    /// Writes complete Ethernet frames (including all headers) directly
    /// to the specified network interface.  Requires CAP_NET_RAW or root.
    class AfPacketSender
    {
    public:
        /// Open a raw socket bound to @p interface.
        /// Throws std::runtime_error on failure.
        explicit AfPacketSender(const std::string &interface);
        ~AfPacketSender();

        AfPacketSender(const AfPacketSender &) = delete;
        AfPacketSender &operator=(const AfPacketSender &) = delete;

        /// Send one complete Ethernet frame.
        /// Returns true on success, false on a transient send error.
        bool send_frame(const std::uint8_t *data, std::size_t len) noexcept;

        std::uint64_t sent_packets() const noexcept { return sent_packets_; }
        std::uint64_t sent_bytes() const noexcept { return sent_bytes_; }

    private:
        int fd_ = -1;
        int if_index_ = -1;
        std::uint64_t sent_packets_ = 0;
        std::uint64_t sent_bytes_ = 0;
    };

} // namespace rxtech::replay
