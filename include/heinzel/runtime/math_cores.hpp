#pragma once

// Every core: the arithmetic that needs a method rather than an expression, one file per subject.
// clang allows nothing from `<cmath>`, nor its `__builtin_` spellings, in a constant expression, and every
// graph can be evaluated while it compiles, so each root, logarithm or floor is written here. The same cores
// run at run time, so no number depends on the platform's math library; where a core approximates, its
// measured maximum error over a stated domain is beside it. Which operation uses which core is `compute.hpp`'s.

#include "heinzel/runtime/exact_arithmetic.hpp"
#include "heinzel/runtime/exponential_cores.hpp"
#include "heinzel/runtime/float_bits.hpp"
#include "heinzel/runtime/float_cores.hpp"
#include "heinzel/runtime/fused_multiply_add.hpp"
#include "heinzel/runtime/inverse_circular_cores.hpp"
#include "heinzel/runtime/periodic_cores.hpp"
#include "heinzel/runtime/power_cores.hpp"
#include "heinzel/runtime/root_cores.hpp"
#include "heinzel/runtime/rounding_cores.hpp"
