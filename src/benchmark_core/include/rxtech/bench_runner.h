#pragma once

#include "rxtech/bench_context.h"

namespace rxtech {

void request_bench_stop();
void reset_bench_stop();

class BenchRunner {
public:
    RunSummary run(BenchContext& context);
};

}  // namespace rxtech
