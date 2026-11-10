#pragma once

// FIXTURE for the `loops_closure` gate, never compiled. It includes an allowed header that in turn includes a
// forbidden one: the case the direct-edge gates miss, which only a walk of the whole closure sees.

#include "heinzel/runtime/allowed.hpp"
