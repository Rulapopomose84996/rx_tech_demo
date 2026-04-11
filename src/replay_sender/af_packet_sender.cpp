#include "af_packet_sender.h"

#include <cerrno>
#include <cstring>
#include <net/if.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>

// Linux-specific headers required by AF_PACKET
#if defined(__linux__)
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace rxtech::replay
{

#if defined(__linux__)

    AfPacketSender::AfPacketSender(const std::string &interface)
    {
        // Open raw socket for all Ethernet protocols
        fd_ = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (fd_ < 0)
            throw std::runtime_error(
                std::string("创建 AF_PACKET socket 失败: ") + std::strerror(errno));

        // Get interface index
        struct ifreq ifr{};
        std::strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ - 1);
        if (::ioctl(fd_, SIOCGIFINDEX, &ifr) < 0)
        {
            ::close(fd_);
            throw std::runtime_error(
                std::string("获取网卡索引失败（接口：") + interface + "）: " + std::strerror(errno));
        }
        if_index_ = ifr.ifr_ifindex;
    }

    AfPacketSender::~AfPacketSender()
    {
        if (fd_ >= 0)
            ::close(fd_);
    }

    bool AfPacketSender::send_frame(const std::uint8_t *data, std::size_t len) noexcept
    {
        struct sockaddr_ll sa{};
        sa.sll_family = AF_PACKET;
        sa.sll_ifindex = if_index_;
        sa.sll_halen = 6;
        // Copy dst MAC from first 6 bytes of Ethernet frame
        std::memcpy(sa.sll_addr, data, 6);

        const ssize_t sent = ::sendto(fd_,
                                      data,
                                      len,
                                      0,
                                      reinterpret_cast<const struct sockaddr *>(&sa),
                                      sizeof(sa));
        if (sent < 0)
            return false;

        ++sent_packets_;
        sent_bytes_ += static_cast<std::uint64_t>(sent);
        return true;
    }

#else // Non-Linux stub

    AfPacketSender::AfPacketSender(const std::string &)
    {
        throw std::runtime_error("AfPacketSender 仅支持 Linux 平台（AF_PACKET）");
    }

    AfPacketSender::~AfPacketSender() = default;

    bool AfPacketSender::send_frame(const std::uint8_t *, std::size_t) noexcept
    {
        return false;
    }

#endif

} // namespace rxtech::replay
