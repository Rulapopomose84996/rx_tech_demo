#pragma once

#include <memory>
#include <string>

#include "rxtech/rx_backend.h"

#if defined(__linux__)
#include <cstdint>
#include <sys/socket.h>
#endif

namespace rxtech
{

#if defined(__linux__)
    std::uint64_t update_kernel_drop_count_from_cmsg(const msghdr &msg,
                                                     std::uint32_t &last_seen_kernel_drop_count);
#endif

    class LinuxSocketIngress final : public IRxBackend
    {
    public:
        LinuxSocketIngress();
        ~LinuxSocketIngress() override;

        std::string name() const override;
        BackendInitResult init(const RxConfig &config) override;
        bool recv_burst(UdpDatagramBurst &burst, std::uint32_t max_burst) override;
        void release_burst(UdpDatagramBurst &burst) override;
        BackendStats stats() const override;
        void shutdown() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        BackendStats stats_;
    };

} // namespace rxtech
