#pragma once

// A DELIBERATE offender for the `notation_no_cost_model` gate (M8.2 / O12). Compiled by nothing; it
// exists only so the gate can be pointed at it and OBSERVED to object. The notation is a capture layer
// and must consult no cost model while capturing, so a notation header reaching into the planner — the
// cost model, where the old tree's `cost::contraction_path` lived — is exactly the include the gate
// forbids. The line the include sits on is pinned by the would-object test's regular expression.

#include "heinzel/schedule/plan.hpp"

namespace heinzel::notation {
inline constexpr int a_notation_header_that_consults_the_cost_model = 0;
} // namespace heinzel::notation
