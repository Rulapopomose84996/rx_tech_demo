#pragma once

#include <memory>

#include "rxtech/rx_backend.h"

namespace rxtech {

class XdpBackend final : public IRxBackend {
public:
    XdpBackend();
    ~XdpBackend() override;

    XdpBackend(const XdpBackend&) = delete;
    XdpBackend& operator=(const XdpBackend&) = delete;

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
};

}  // namespace rxtech
