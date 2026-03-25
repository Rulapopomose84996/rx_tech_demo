#pragma once

#include <iosfwd>

#include "rxtech/bench_context.h"

namespace rxtech {

void request_bench_stop();
void reset_bench_stop();

class BenchRunner {
public:
    void set_status_output(std::ostream* output);
    RunSummary run(BenchContext& context);

private:
    std::ostream* status_output_ = nullptr;
};

}  // namespace rxtech
