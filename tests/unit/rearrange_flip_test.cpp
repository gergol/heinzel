// The FLIP's element meaning, run: which element of its operand one element of a reversal's result is.
//
// Split from `rearrange_test.cpp` under operator amendment 06 — that file (`_reshape`/`_xpose`/`_slice`/
// `_unfold`) reached its 600-line limit, and `_flip` is a rearrangement of its own to check, the same
// concern-split `rearrange2d_test.cpp` made for the 2-D unfold. A flip reverses ONE outer axis and
// changes no extent, so output element i of that axis reads source (extent-1-i) and every other axis is
// the identity. `flipped_element` answers it, and `view_inference_test.cpp` shows the analysis pass
// refusing the axes a flip cannot take (the streamed axis, an axis out of range); this file checks the
// mapping's arithmetic and the run-side refusals a hand-built node the pass never saw would carry.
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/runtime/compute.hpp"

#include "elements.hpp"
#include "hand_built_graph.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t kT = 4; // the streamed axis, kept short: nothing here is about time

// ---- the mapping itself --------------------------------------------------------------------------
//
// A reversal of the OUTER axis of a {3, 2} value: output (i, j), flat e = i*2 + j, reads source
// (2 - i, j). Exhaustive over all six elements, so a mapping that answered zero, or reversed the wrong
// axis, or reversed the inner one too, is caught rather than only the first element being right.
constexpr Elements kThreeByTwo = elements_of(Shape{3, 2, kT});
static_assert(kThreeByTwo.rank == 2 && kThreeByTwo.count == 6 && kThreeByTwo.full_rank == 3);
static_assert([] {
    for (std::int32_t i = 0; i < 3; ++i) {
        for (std::int32_t j = 0; j < 2; ++j) {
            if (flipped_element(kThreeByTwo, kThreeByTwo, 0, i * 2 + j) != (2 - i) * 2 + j) {
                return false;
            }
        }
    }
    return true;
}(), "a flip reverses its axis and leaves the others: output (i, j) reads source (extent-1-i, j)");

// ---- the refusals, observed rather than asserted -------------------------------------------------
//
// In `main` rather than as `static_assert`s for the reason `rearrange_test.cpp` gives: under constant
// evaluation a `fail()` IS the compile error, so a refusal can only be watched from a run. Each is a
// mistake a hand-built node can carry past the pass, refused again at the mapping rather than reading a
// plausible wrong element.

constexpr Elements kSix = elements_of(Shape{6, kT});
constexpr Elements kFive = elements_of(Shape{5, kT});
constexpr Elements kTwoByThree = elements_of(Shape{2, 3, kT});

// A flip keeps every axis, so a `to`/`from` of different rank is a node that claims to be a flip and is
// not one.
bool a_flip_that_changed_the_rank_is_refused() {
    try {
        (void)flipped_element(kTwoByThree, kSix, 0, 0);
        return false;
    } catch (const error& e) {
        return e.message() == std::string_view{"heinzel::flipped_element: a flip changed the rank of its value"};
    }
}

// An axis that is not one of the value's element axes — here axis 1 of a rank-1 value, which is the
// streamed axis or none at all.
bool a_flip_of_a_non_element_axis_is_refused() {
    try {
        (void)flipped_element(kSix, kSix, 1, 0);
        return false;
    } catch (const error& e) {
        return e.message() ==
               std::string_view{"heinzel::flipped_element: a flip along an axis that is not an element axis here"};
    }
}

// The only way a shape-preserving reversal reads past its source: a `to` whose flipped axis is wider
// than the `from`'s — a malformed node the pass would never have produced. Output element 4 of a claimed
// extent 5 reverses to source index 5 - 1 - 4 = -1 against a real extent of 4.
bool a_flip_that_reads_past_the_source_is_refused() {
    try {
        (void)flipped_element(kFive, elements_of(Shape{4, kT}), 0, 4);
        return false;
    } catch (const error& e) {
        return e.message() == std::string_view{"heinzel::flipped_element: a flip reads past the source axis"};
    }
}

} // namespace

int main() {
    return a_flip_that_changed_the_rank_is_refused() && a_flip_of_a_non_element_axis_is_refused() &&
                   a_flip_that_reads_past_the_source_is_refused()
               ? 0
               : 1;
}
