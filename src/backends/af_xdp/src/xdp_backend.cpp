#include "rxtech/xdp_backend.h"

#include "rxtech/rx_config.h"

namespace rxtech {

std::string XdpBackend::name() const {
    return "af_xdp";
}

bool XdpBackend::init(const RxConfig& config) {
    (void)config;
    stats_ = {};
    return true;
}

bool XdpBackend::recv_burst(RxBurst& burst, std::uint32_t max_burst) {
    (void)max_burst;
    burst.packets.clear();
    return true;
}

void XdpBackend::release_burst(RxBurst& burst) {
    burst.packets.clear();
}

BackendStats XdpBackend::stats() const {
    return stats_;
}

void XdpBackend::shutdown() {
}

}  // namespace rxtech
