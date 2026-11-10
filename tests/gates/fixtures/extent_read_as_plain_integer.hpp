#pragma once

// A DELIBERATE OFFENDER. This file exists to be rejected.
//
// The extents gate (milestone M2.7) is pointed at this directory by a test marked WILL_FAIL, so the
// gate is observed OBJECTING and not merely observed passing. A gate that has only ever been seen
// to pass is not evidence that it would ever fail — the same argument the tree already makes for
// `gate_no_per_op_branches_would_object`.
//
// Nothing includes this. Every line below is one of the three patterns the gate names, written the
// way it would really appear.

#include <cstdint>

#include "heinzel/ir/vocab.hpp"

namespace heinzel::gate_fixture {

// 1 — an extent held in a bare integer. Correct today, wrong the moment a symbol reaches it.
struct BadWindow {
    std::int32_t frame_extent{0};
};

// 2 — arithmetic on the encoding. A symbol encodes negative, so this is not "twice as many".
constexpr std::int32_t doubled(const Shape& s) { return s.dims[0] * 2; }

// 3 — the encoding compared against a number. Reads as "is this axis a single element", and is a
// question about the encoding rather than about the extent.
constexpr bool is_one(const Shape& s) { return s.dims[0] == 1; }

} // namespace heinzel::gate_fixture
