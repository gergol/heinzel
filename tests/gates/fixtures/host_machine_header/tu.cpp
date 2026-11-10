// Compiled by `gates/host_machine_header.cmake` against the header it wrote, every field measured: an `OpCost` 7 5,
// `fma` true, every other field 40. The first field, the last, an `OpCost` and the one bool: the header took them.
#include "heinzel/host_machine.hpp"

static_assert(heinzel::host.vector_bytes == 40 && heinzel::host.merge_work == 40);
static_assert(heinzel::host.add.latency == 7 && heinzel::host.shuffle.slots == 5 && heinzel::host.fma);
