#pragma once

#include <memory>
#include <vector>

#include "rxtech/rx_backend.h"

namespace rxtech {

class SocketBackend final : public IRxBackend {
public:
    SocketBackend();
    ~SocketBackend() override;

    std::string name() const override;
    bool init(const RxConfig& config) override;
    bool recv_burst(RxBurst& burst, std::uint32_t max_burst) override;
    void release_burst(RxBurst& burst) override;
    BackendStats stats() const override;
    void shutdown() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    BackendStats stats_;
    std::vector<std::vector<std::uint8_t>> owned_packets_;
};

}  // namespace rxtech
