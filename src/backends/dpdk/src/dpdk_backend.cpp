#include "rxtech/dpdk_backend.h"

#include "rxtech/rx_config.h"

namespace rxtech {

std::string DpdkBackend::name() const {
    return "dpdk";
}

bool DpdkBackend::init(const RxConfig& config) {
    (void)config;
    stats_ = {};
    return true;
}

bool DpdkBackend::recv_burst(RxBurst& burst, std::uint32_t max_burst) {
    (void)max_burst;
    burst.packets.clear();
    return true;
}

void DpdkBackend::release_burst(RxBurst& burst) {
    burst.packets.clear();
}

BackendStats DpdkBackend::stats() const {
    return stats_;
}

void DpdkBackend::shutdown() {
}

}  // namespace rxtech
