#pragma once

// An operation added half-way, written once.
//
// Two files need it and they need it for opposite purposes: `unit/meaning_test.cpp` hands it to the
// coverage check and requires the check to say no, and `gates/refusals/row_with_no_meaning.cpp` puts
// the same table under a `static_assert` that must therefore fail to compile. Written out in each,
// the two copies drift, and then the test that says "the check objects" and the fixture that says
// "the build refuses" would stop being about the same defect.
//
// The defect: the rule table says `_reduce` has a per-sample meaning, and neither `runtime/compute.hpp`
// nor `tests/reference/compute_reference.hpp` has arithmetic for it. That is what happens when somebody
// adds an operation, fills in the row, and stops. It is a COPY of the real table with one row flipped,
// because the real one has to stay correct for every other translation unit — which is the reason the
// check takes a table as an argument in the first place.

#include <array>
#include <cstdint>

#include "heinzel/ir/op_rules.hpp"

namespace heinzel {

inline constexpr std::array<OpTableRow, kOpCount> kRowSaysItHasAMeaning = [] {
    std::array<OpTableRow, kOpCount> rows{};
    for (std::uint8_t i = 0; i < kOpCount; ++i) {
        rows[i] = kOpTable[i];
    }
    rows[op::Reduce.index].rules.meaning = MeaningRule::PerSample;
    return rows;
}();

// A third half-done operation, and a different kind of half-done: a row whose COST column was never
// stated. `_sqrt` is the row cleared, deliberately — it is `Software`, so before milestone M4 gave
// this column a sentinel, forgetting it did not leave a hole, it left the row quietly claiming to be
// as cheap as an add. That is the failure mode worth showing: not an omission that announces itself,
// but one that reads as a confident wrong answer.
inline constexpr std::array<OpTableRow, kOpCount> kRowWithNoCost = [] {
    std::array<OpTableRow, kOpCount> rows{};
    for (std::uint8_t i = 0; i < kOpCount; ++i) {
        rows[i] = kOpTable[i];
    }
    rows[op::Sqrt.index].rules.cost = CostClass::Unclassified;
    return rows;
}();

// A fourth: a row that never states what it does to a range or where its operands must lie (Plan/26-value-ranges/03).
// `_div` is the row cleared, the one whose domain matters most: left unstated, the proof could not see a division by 0.
inline constexpr std::array<OpTableRow, kOpCount> kRowWithNoRange = [] {
    std::array<OpTableRow, kOpCount> rows{};
    for (std::uint8_t i = 0; i < kOpCount; ++i) {
        rows[i] = kOpTable[i];
    }
    rows[op::Div.index].rules.range = RangeRule::Unclassified;
    rows[op::Div.index].rules.domain = Domain::Unclassified;
    return rows;
}();

} // namespace heinzel
