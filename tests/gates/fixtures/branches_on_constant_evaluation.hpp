#pragma once

// A DELIBERATE OFFENDER. This file exists to be rejected.
//
// The constant-evaluation gate (milestone M2.2) is pointed at this directory by a test marked
// WILL_FAIL, so the gate is observed OBJECTING and not merely observed passing.
//
// Nothing includes this. What it does is exactly the thing the gate forbids and exactly the way it
// would really appear: a "fast path for run time" that quietly makes the two worlds disagree — here
// by rounding differently, which no test comparing one world against itself could ever see.

#include <cstdint>
#include <type_traits>

namespace heinzel::gate_fixture {

constexpr std::int32_t halve(std::int32_t n) {
    if (std::is_constant_evaluated()) {
        return n / 2;
    }
    return (n + 1) / 2;
}

constexpr bool careful() {
    if consteval {
        return true;
    } else {
        return false;
    }
}

} // namespace heinzel::gate_fixture
