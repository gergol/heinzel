// The 2-D rearrangement, run: which element of its operand one element of a `_unfld2d` result is.
//
// Milestone M5's S3f, the second windowed view. `rearrange_test.cpp` checks the single unfold's element
// mapping (and the transpose's and slice's); this checks the 2-D one, kept in its own file because that
// file already covers five mappings and their refusals and had reached its size — operator amendment
// 06's limit is why a sixth mapping starts a file rather than lengthening one. The mapping is the 1-D
// unfold applied to two axes at once: output element (f1, k1, f2, k2) reads source `(f1*hop1 + k1)`
// along the first windowed axis and `(f2*hop2 + k2)` along the second, recomposed on the source extents.

#include <string_view>

#include "heinzel/ir/ir.hpp"

#include "elements.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t kT = 4; // the streamed axis, kept short: nothing here is about time

// `{6, 5, T}` unfolded on axis 0 (window two, hop two → 3 frames) and axis 1 (window three, hop one → 3
// frames) is `{3, 2, 3, 3, T}`: each axis becomes its frame count with a window axis beside it,
// interleaved. Output (f1, k1, f2, k2) reads source index `(f1*2 + k1)` along axis 0 and `(f2*1 + k2)`
// along axis 1 — recomposed against the source extents [6, 5] as `i0*5 + i1`. Written out rather than
// derived from the arithmetic it checks; it is the first mapping whose output has TWO axes more than its
// source, and overlapping windows (axis 1, hop below window) read a source element into more than one.
constexpr Elements kSheet = elements_of(Shape{6, 5, kT});
constexpr Elements kFrames2d = elements_of(Shape{3, 2, 3, 3, kT});
static_assert(kFrames2d.rank == 4 && kFrames2d.count == 54 && kFrames2d.full_rank == 5);
static_assert([] {
    for (std::int32_t f1 = 0; f1 < 3; ++f1) {
        for (std::int32_t k1 = 0; k1 < 2; ++k1) {
            for (std::int32_t f2 = 0; f2 < 3; ++f2) {
                for (std::int32_t k2 = 0; k2 < 3; ++k2) {
                    const std::int32_t e = ((f1 * 2 + k1) * 3 + f2) * 3 + k2; // flat over [3, 2, 3, 3]
                    const std::int32_t want = (f1 * 2 + k1) * 5 + (f2 * 1 + k2);
                    if (unfolded2d_element(kFrames2d, kSheet, 0, 2, 2, 1, 3, 1, e) != want) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}(), "a 2-D unfold reads (f1*hop1+k1) along axis 0 and (f2*hop2+k2) along axis 1, recomposed on the source");

// ---- the refusals, for a hand-built node the pass never saw -----------------------------------------
//
// The pass refuses each of these by name in `view_inference_test.cpp`; this is the second line, the run
// refusing them again — a runner reaches `unfolded2d_element` without the pass. The five are the single
// unfold's over two axes plus the one new to two: the axes must be distinct and ordered.
constexpr std::string_view kNotTwoMoreAxes =
    "heinzel::unfolded2d_element: a 2-D unfold does not add exactly two axes to its value";
constexpr std::string_view kHopOrWindow2d =
    "heinzel::unfolded2d_element: a 2-D unfold with a hop or window below one";
constexpr std::string_view kNotTwoDistinctAxes =
    "heinzel::unfolded2d_element: a 2-D unfold along axes that are not two distinct source axes";
constexpr std::string_view kWindow2dDisagrees =
    "heinzel::unfolded2d_element: a window axis disagrees with its declared window";
constexpr std::string_view kUnfold2dReadsPast =
    "heinzel::unfolded2d_element: a 2-D unfold reads past a source axis";

// A 2-D unfold adds exactly two axes; a result with the source's own rank was not unfolded into it.
bool a_2d_unfold_that_does_not_add_two_axes_is_refused() {
    try {
        (void)unfolded2d_element(kSheet, kSheet, 0, 2, 2, 1, 3, 1, 0);
        return false;
    } catch (const error& e) {
        return e.message() == kNotTwoMoreAxes;
    }
}

// A hop or window below one, on either axis, is not a stride or a frame.
bool a_2d_unfold_with_a_hop_below_one_is_refused() {
    try {
        (void)unfolded2d_element(kFrames2d, kSheet, 0, 2, 2, 1, 3, 0, 0); // hop2 = 0
        return false;
    } catch (const error& e) {
        return e.message() == kHopOrWindow2d;
    }
}

// Axes that are not two distinct ordered source axes — here the same axis named twice — do not frame a
// 2-D window.
bool a_2d_unfold_along_non_distinct_axes_is_refused() {
    try {
        (void)unfolded2d_element(kFrames2d, kSheet, 1, 2, 2, 1, 3, 1, 0); // axis1 == axis2
        return false;
    } catch (const error& e) {
        return e.message() == kNotTwoDistinctAxes;
    }
}

// A window axis whose extent disagrees with its declared window is a node whose shape and attribute were
// built inconsistently — here the first window axis is 2, but 3 is claimed.
bool a_2d_unfold_whose_window_axis_disagrees_is_refused() {
    try {
        (void)unfolded2d_element(kFrames2d, kSheet, 0, 3, 2, 1, 3, 1, 0);
        return false;
    } catch (const error& e) {
        return e.message() == kWindow2dDisagrees;
    }
}

// A frame that reaches past a source axis reads outside the operand's storage: a hand-built result
// claiming four frames on axis 0 at window two hop two asks frame 3 tap 1 for source index 7 of an
// extent-6 axis.
bool a_2d_unfold_that_reads_past_the_source_is_refused() {
    const Elements too_many = elements_of(Shape{4, 2, 3, 3, kT});
    try {
        (void)unfolded2d_element(too_many, kSheet, 0, 2, 2, 1, 3, 1, ((3 * 2 + 1) * 3 + 0) * 3 + 0);
        return false;
    } catch (const error& e) {
        return e.message() == kUnfold2dReadsPast;
    }
}

} // namespace

int main() {
    if (!a_2d_unfold_that_does_not_add_two_axes_is_refused()) return 1;
    if (!a_2d_unfold_with_a_hop_below_one_is_refused()) return 2;
    if (!a_2d_unfold_along_non_distinct_axes_is_refused()) return 3;
    if (!a_2d_unfold_whose_window_axis_disagrees_is_refused()) return 4;
    if (!a_2d_unfold_that_reads_past_the_source_is_refused()) return 5;

    // And none of the refusals is refusing everything: an ordinary 2-D unfold still answers.
    return unfolded2d_element(kFrames2d, kSheet, 0, 2, 2, 1, 3, 1, 0) == 0 ? 0 : 6; // frame 0 of both
}
