#include <cassert>

#include "rxtech/replay_sender.h"

int main() {
    const rxtech::ReplayTarget target = rxtech::parse_replay_target("127.0.0.1:9999");
    assert(target.host == "127.0.0.1");
    assert(target.port == 9999U);
    return 0;
}
