#pragma once

#include "rxtech/rx_backend.h"

namespace rxtech {

class DpdkBackend final : public IRxBackend {
public:
    std::string name() const override;
    bool init(const RxConfig& config) override;
    bool recv_burst(RxBurst& burst, std::uint32_t max_burst) override;
    void release_burst(RxBurst& burst) override;
    BackendStats stats() const override;
    void shutdown() override;

private:
    BackendStats stats_;
};

}  // namespace rxtech
