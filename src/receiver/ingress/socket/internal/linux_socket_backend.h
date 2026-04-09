#pragma once

#include <memory>
#include <string>

#include "rxtech/rx_backend.h"

namespace rxtech
{

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
