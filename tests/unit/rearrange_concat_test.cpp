// The JOIN's element meaning, run: which element of WHICH of two operands one element of a joined result
// is (milestone M5, S3i).
//
// Split from `rearrange_test.cpp` under operator amendment 06 — that file is at its 600-line limit, and a
// join is a rearrangement of its own to check, the same concern-split `rearrange_flip_test.cpp` and
// `rearrange_pad_test.cpp` made. A join is the FIRST meaning here over TWO operands that SELECTS one
// rather than folding: the lower part of the joined axis reads operand 0, the upper part reads operand 1
// shifted down by operand 0's extent — so `concatenated_element` answers a `ConcatRead` (which operand,
// and which of its elements), and this file checks both halves. `view_inference_concat_test.cpp` shows
// the analysis pass refusing the joins it cannot take; this file checks the mapping's arithmetic and the
// run-side refusals a hand-built node the pass never saw would carry.
#include <string_view>

#include "heinzel/ir/ir.hpp"

#include "elements.hpp"
#include "hand_built_graph.hpp"
#include "join_element.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t kT = 4; // the streamed axis, kept short: nothing here is about time

// ---- the mapping itself --------------------------------------------------------------------------
//
// A join of a {2, 2} operand and a {3, 2} operand along the OUTER axis: the result is {5, 2}, output
// (i, j) at flat e = i*2 + j. The lower two rows i in {0, 1} read operand 0 at (i, j); the upper three
// rows i in {2, 3, 4} read operand 1 at (i - 2, j). The split is UNEVEN on purpose — 2 against 3 — so a
// mapping that split at the wrong row, read the wrong operand, or forgot to shift the upper part is
// caught. Exhaustive over all ten elements.
constexpr Elements kTwoByTwo = elements_of(Shape{2, 2, kT});
constexpr Elements kThreeByTwo = elements_of(Shape{3, 2, kT});
constexpr Elements kFiveByTwo = elements_of(Shape{5, 2, kT});
static_assert(kFiveByTwo.rank == 2 && kFiveByTwo.count == 10 && kFiveByTwo.full_rank == 3);
static_assert([] {
    for (std::int32_t i = 0; i < 5; ++i) {
        for (std::int32_t j = 0; j < 2; ++j) {
            const ConcatRead r = concatenated_element(kFiveByTwo, kTwoByTwo, kThreeByTwo, 0, i * 2 + j);
            if (i < 2) {
                if (r.from_second || r.index != i * 2 + j) {
                    return false; // the lower part reads operand 0 at (i, j)
                }
            } else if (!r.from_second || r.index != (i - 2) * 2 + j) {
                return false; // the upper part reads operand 1 at (i - 2, j)
            }
        }
    }
    return true;
}(), "a join reads operand 0 below the split and operand 1 above it, shifted down by operand 0's extent");

// ---- the refusals, observed rather than asserted -------------------------------------------------
//
// In `main` rather than as `static_assert`s for the reason `rearrange_test.cpp` gives: under constant
// evaluation a `fail()` IS the compile error, so a refusal can only be watched from a run. Each is a
// mistake a hand-built node can carry past the pass, refused again at the mapping rather than reading a
// plausible wrong element.

constexpr Elements kSix = elements_of(Shape{6, kT});
constexpr Elements kTwoByThree = elements_of(Shape{2, 3, kT});
constexpr Elements kFourByTwo = elements_of(Shape{4, 2, kT});
constexpr Elements kThreeByThree = elements_of(Shape{3, 3, kT});

// A join keeps every axis of both operands, so a `to`/operand of different rank is a node that claims to
// be a join and is not.
bool a_join_that_changed_the_rank_is_refused() {
    try {
        (void)concatenated_element(kTwoByThree, kSix, kSix, 0, 0);
        return false;
    } catch (const error& e) {
        return e.message() ==
               std::string_view{"heinzel::concatenated_element: a join changed the rank of its operands"};
    }
}

// An axis that is not one of the value's element axes — here axis 1 of a rank-1 value, the streamed axis
// or none at all.
bool a_join_of_a_non_element_axis_is_refused() {
    try {
        (void)concatenated_element(kSix, kSix, kSix, 1, 0);
        return false;
    } catch (const error& e) {
        return e.message() ==
               std::string_view{"heinzel::concatenated_element: a join along an axis that is not an element axis here"};
    }
}

// The joined axis's own extent must be the SUM of the two operands'. A {4, 2} claiming to be {2, 2}
// joined with {3, 2} (which is {5, 2}) is a node whose shape disagrees with its own operands.
bool a_join_whose_axis_extent_is_wrong_is_refused() {
    try {
        (void)concatenated_element(kFourByTwo, kTwoByTwo, kThreeByTwo, 0, 0);
        return false;
    } catch (const error& e) {
        return e.message() ==
               std::string_view{"heinzel::concatenated_element: the joined axis extent is not the sum of the two operands'"};
    }
}

// No broadcast across a join: a NON-joined axis the two operands disagree on would read past that axis of
// one of them. Here the joined axis 0 is right ({5} = {2}+{3}) but axis 1 is 2 in operand 0 and 3 in
// operand 1.
bool a_join_whose_non_joined_axis_disagrees_is_refused() {
    try {
        (void)concatenated_element(kFiveByTwo, kTwoByTwo, kThreeByThree, 0, 0);
        return false;
    } catch (const error& e) {
        return e.message() ==
               std::string_view{"heinzel::concatenated_element: a join's non-joined axis extents disagree"};
    }
}

} // namespace

int main() {
    return a_join_that_changed_the_rank_is_refused() && a_join_of_a_non_element_axis_is_refused() &&
                   a_join_whose_axis_extent_is_wrong_is_refused() &&
                   a_join_whose_non_joined_axis_disagrees_is_refused()
               ? 0
               : 1;
}
