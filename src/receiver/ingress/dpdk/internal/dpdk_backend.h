#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rxtech/packet_desc.h"
#include "rxtech/rx_backend.h"

namespace rxtech {

std::vector<std::string> build_dpdk_eal_args(const RxConfig& config);

class DpdkDatagramAdapter final {
public:
    explicit DpdkDatagramAdapter(std::uint32_t local_ip_be);

    bool adapt_frame(const PacketDesc& packet, BackendStats& stats, UdpDatagramBurst& burst) const;

private:
    std::uint32_t local_ip_be_ = 0;
};

class DpdkIngress final : public IRxBackend {
public:
    DpdkIngress();
    ~DpdkIngress() override;

    std::string name() const override;
    BackendInitResult init(const RxConfig& config) override;
    bool recv_burst(UdpDatagramBurst& burst, std::uint32_t max_burst) override;
    void release_burst(UdpDatagramBurst& burst) override;
    BackendStats stats() const override;
    void shutdown() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    BackendStats stats_;
};

using DpdkBackend = DpdkIngress;

}  // namespace rxtech
