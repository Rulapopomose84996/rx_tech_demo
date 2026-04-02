#pragma once

#include <iosfwd>

#include "rxtech/receive_context.h"

namespace rxtech {

void request_receive_stop();
void reset_receive_stop();

class ReceiveRunner {
public:
    void set_status_output(std::ostream* output);
    RunSummary run(ReceiveContext& context);

private:
    std::ostream* status_output_ = nullptr;
};

}  // namespace rxtech
