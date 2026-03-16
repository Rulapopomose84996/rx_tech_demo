#include <cassert>

#include "rxtech/spsc_ring.h"

int main() {
    rxtech::SpscRing<int> ring(2U);
    assert(ring.push(1));
    assert(ring.push(2));
    assert(!ring.push(3));

    int value = 0;
    assert(ring.pop(value));
    assert(value == 1);
    return 0;
}
