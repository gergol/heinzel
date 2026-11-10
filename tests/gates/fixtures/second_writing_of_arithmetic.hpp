#pragma once

// A DELIBERATE OFFENDER. This file exists to be rejected.
//
// The one-writing gate (milestone M3.1) is pointed at this directory by a test marked WILL_FAIL, so
// the gate is observed OBJECTING and not merely observed passing.
//
// Nothing includes this. What it does is exactly how a second writing really appears — not as
// somebody deciding to duplicate the arithmetic, but as a "fast path" written where the code that
// wanted it lives, covering the two operations that happened to matter that day. It agrees with
// `runtime/compute.hpp` the moment it is written and drifts from it afterwards, and no test
// comparing one runner against another can see the difference, because both would be built on it.

#include "heinzel/ir/ir.hpp"

namespace heinzel::gate_fixture {

constexpr double quick(const Node& n, double a, double b) {
    if (n.op == op::Add) {
        return a + b;
    }
    if (n.op == op::Mul) {
        return a * b;
    }
    return 0.0;
}

} // namespace heinzel::gate_fixture
