#include "rxtech/rx_config.h"

namespace rxtech {

RxConfig load_default_config() {
    RxConfig config;
    config.cpu_cores = {0};
    return config;
}

}  // namespace rxtech
