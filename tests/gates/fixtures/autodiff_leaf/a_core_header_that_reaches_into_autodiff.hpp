#pragma once

// A DELIBERATE OFFENDER for the autodiff-leaf gate (M7.4 / A4-2). Nothing includes this file; it exists
// only to be the one thing the `_would_object` test points the gate at, so a gate that has quietly
// stopped objecting is caught. A core header reaching into `heinzel/autodiff/` is the edge A4-2 forbids:
// it would mean the core was shaped by the feature and could not compile with the feature deleted.
#include "heinzel/autodiff/gradients.hpp"

namespace heinzel::fixture {
inline constexpr int autodiff_leaf_offender = 0;
}
