#pragma once

#include <memory>

#include "rxtech/rx_backend.h"

namespace rxtech {

class DpdkIngress final : public IRxBackend {
public:
    DpdkIngress();
    ~DpdkIngress() override;

    std::string name() const override;
    BackendInitResult init(const RxConfig& config) override;
    bool recv_burst(RxBurst& burst, std::uint32_t max_burst) override;
    void release_burst(RxBurst& burst) override;
    BackendStats stats() const override;
    void shutdown() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    BackendStats stats_;
};

using DpdkBackend = DpdkIngress;

}  // namespace rxtech
