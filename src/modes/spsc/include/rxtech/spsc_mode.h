#pragma once

#include "rxtech/mode_processor.h"
#include "rxtech/spsc_ring.h"

namespace rxtech {

class SpscMode final : public IModeProcessor {
public:
    SpscMode();

    std::string name() const override;
    void process(RxBurst& burst, IMetricsCollector& metrics) override;

private:
    SpscRing<PacketDesc> ring_;
};

}  // namespace rxtech
