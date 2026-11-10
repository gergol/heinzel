// The CONSTANT PAD's element meaning, run: which element of its operand one element of a padded result
// is — OR that it is a MARGIN and reads no source element at all (milestone M5, S3h).
//
// Split from `rearrange_test.cpp` under operator amendment 06 — that file is at its 600-line limit, and
// `_padc` is a rearrangement of its own to check, the same concern-split `rearrange_flip_test.cpp` made
// for the reversal. A pad is the FIRST meaning here that is not one-source-in for one-out: axis 0 grows
// to `L + before + after`, the body reads source `(i - before)` and the margins read a constant that
// lives in the attribute, not here — so `padded_element` answers a `PadRead` (a source index AND whether
// there is one), and this file checks both halves. `view_inference_pad_test.cpp` shows the analysis pass
// refusing the axes a pad cannot take; this file checks the mapping's arithmetic and the run-side
// refusals a hand-built node the pass never saw would carry.
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
// A constant pad of the OUTER axis of a {3, 2} value by one element in front and one behind: the result
// is {5, 2}, output (i, j) at flat e = i*2 + j. The three body rows i in {1, 2, 3} read source (i-1, j);
// the two margin rows i in {0, 4} read no source element (`in_source` false) and the runner writes the
// fill there. Exhaustive over all ten elements, so a mapping that filled the wrong rows, shifted by the
// wrong amount, or claimed a margin was a source read is caught rather than only the first element.
constexpr Elements kFiveByTwo = elements_of(Shape{5, 2, kT});
constexpr Elements kThreeByTwo = elements_of(Shape{3, 2, kT});
static_assert(kFiveByTwo.rank == 2 && kFiveByTwo.count == 10 && kFiveByTwo.full_rank == 3);
static_assert([] {
    for (std::int32_t i = 0; i < 5; ++i) {
        for (std::int32_t j = 0; j < 2; ++j) {
            const PadRead r = padded_element(kFiveByTwo, kThreeByTwo, 0, 1, 1, i * 2 + j);
            if (i >= 1 && i <= 3) {
                if (!r.in_source || r.index != (i - 1) * 2 + j) {
                    return false; // a body element reads source (i - before, j)
                }
            } else if (r.in_source) {
                return false; // a margin element reads no source element
            }
        }
    }
    return true;
}(), "a pad reads source (i - before) in the body and marks the margins as reading the constant fill");

// ---- the refusals, observed rather than asserted -------------------------------------------------
//
// In `main` rather than as `static_assert`s for the reason `rearrange_test.cpp` gives: under constant
// evaluation a `fail()` IS the compile error, so a refusal can only be watched from a run. Each is a
// mistake a hand-built node can carry past the pass, refused again at the mapping rather than reading a
// plausible wrong element or a wrong margin.

constexpr Elements kSix = elements_of(Shape{6, kT});
constexpr Elements kTwoByThree = elements_of(Shape{2, 3, kT});
constexpr Elements kSixByTwo = elements_of(Shape{6, 2, kT});
constexpr Elements kThreeByThree = elements_of(Shape{3, 3, kT});

// A pad keeps every axis, so a `to`/`from` of different rank is a node that claims to be a pad and is not.
bool a_pad_that_changed_the_rank_is_refused() {
    try {
        (void)padded_element(kTwoByThree, kSix, 0, 0, 0, 0);
        return false;
    } catch (const error& e) {
        return e.message() == std::string_view{"heinzel::padded_element: a pad changed the rank of its value"};
    }
}

// An axis that is not one of the value's element axes — here axis 1 of a rank-1 value, the streamed axis
// or none at all.
bool a_pad_of_a_non_element_axis_is_refused() {
    try {
        (void)padded_element(kSix, kSix, 1, 0, 0, 0);
        return false;
    } catch (const error& e) {
        return e.message() ==
               std::string_view{"heinzel::padded_element: a pad along an axis that is not an element axis here"};
    }
}

// A pad never shrinks, so a negative amount is a malformed node — refused before any index is computed.
bool a_pad_by_a_negative_amount_is_refused() {
    try {
        (void)padded_element(kThreeByTwo, kThreeByTwo, 0, -1, 0, 0);
        return false;
    } catch (const error& e) {
        return e.message() == std::string_view{"heinzel::padded_element: a pad by a negative amount"};
    }
}

// The padded axis's own extent must be `source + before + after`. A {6, 2} claiming to be {3, 2} padded
// by one and one (which is {5, 2}) is a node whose shape disagrees with its own attribute.
bool a_pad_whose_axis_extent_is_wrong_is_refused() {
    try {
        (void)padded_element(kSixByTwo, kThreeByTwo, 0, 1, 1, 0);
        return false;
    } catch (const error& e) {
        return e.message() ==
               std::string_view{"heinzel::padded_element: the padded axis extent is not source + before + after"};
    }
}

// The only OTHER way a pad's shape can be wrong: a non-padded axis whose extent disagrees, which would
// read past that axis of the source. Here the padded axis 0 is right ({5} = {3}+1+1) but axis 1 claims 2
// against a source of 3.
bool a_pad_that_reads_past_a_non_padded_axis_is_refused() {
    try {
        (void)padded_element(kFiveByTwo, kThreeByThree, 0, 1, 1, 0);
        return false;
    } catch (const error& e) {
        return e.message() ==
               std::string_view{"heinzel::padded_element: a pad changed a non-padded axis's extent"};
    }
}

} // namespace

int main() {
    return a_pad_that_changed_the_rank_is_refused() && a_pad_of_a_non_element_axis_is_refused() &&
                   a_pad_by_a_negative_amount_is_refused() && a_pad_whose_axis_extent_is_wrong_is_refused() &&
                   a_pad_that_reads_past_a_non_padded_axis_is_refused()
               ? 0
               : 1;
}
