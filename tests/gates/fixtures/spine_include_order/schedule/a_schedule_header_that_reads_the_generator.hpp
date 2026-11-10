#pragma once

// FIXTURE for the `spine_include_order` gate, never compiled. A scheduling header that includes the
// generator is the cycle the gate exists to refuse: the generator reads the schedule, so the
// schedule may not read the generator. The first include carries, in its path and in its comment, the
// characters that break a CMake list, and must not hide the second.

#include "heinzel/ir/[ir.hpp" // a; [b
#include "heinzel/codegen/program.hpp"
