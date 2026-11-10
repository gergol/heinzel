// An operation that REARRANGES axes, run: which element of its operand one element of its result is.
//
// Milestone M5's executor, the other half of what `MeaningRule::AxisChanging` covers. A fold reads
// MANY of an operand's elements into one and `collapse_test.cpp` checks that; a rearrangement reads
// exactly ONE, from somewhere else, and nothing is computed on the way — which is why the two are
// separate files as well as separate writings.
//
// WHAT A REARRANGEMENT IS HERE, and it is smaller than the word suggests. `_reshape`'s three kinds —
// re-cutting the same numbers into different extents, dropping an axis of extent one, inserting one —
// all keep the element SEQUENCE and change only the extents a reader names it with. So the mapping is
// the identity in flat order, and what is worth checking is not the arithmetic of the mapping but the
// two things around it: that a run really does move every element rather than only the first, and
// that a rearrangement which would re-cut TIME is refused instead of reading past a value's storage.
//
// A REARRANGEMENT THAT MOVES ELEMENTS — a transpose, a slice, a flip — answers in
// `rearranged_element` and is checked here on its indices, the way `folded_element` is next door. The
// transpose is the first, and it is checked below; a slice and a flip will join it.
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/runtime/compute.hpp"
#include "heinzel/sema/analyze.hpp"

#include "elements.hpp"
#include "hand_built_graph.hpp"
#include "interpret.hpp"
#include "meaning_coverage.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t kT = 4; // the streamed axis, kept short: nothing here is about time

// ---- the mapping itself ---------------------------------------------------------------------------
//
// Six numbers per sample, read with two axes and then with one. The element count is what a
// rearrangement conserves, and the flat index is what it leaves alone.
constexpr Elements kTwoByThree = elements_of(Shape{2, 3, kT});
constexpr Elements kSix = elements_of(Shape{6, kT});
static_assert(kTwoByThree.rank == 2 && kTwoByThree.count == 6);
static_assert(kSix.rank == 1 && kSix.count == 6);

// Every element, not only the first: a mapping written as `return 0` would answer the first one
// correctly, and a run whose elements are all equal could not tell the difference.
static_assert([] {
    for (std::int32_t e = 0; e < 6; ++e) {
        if (same_elements_in_order(kSix, kTwoByThree, e) != e) {
            return false;
        }
    }
    return true;
}(), "a rearrangement keeps the element sequence, so element e comes from element e");

// The layouts may differ in rank and in how the extents are cut, which is the whole point of a view.
static_assert(same_elements_in_order(elements_of(Shape{1, 6, kT}), kTwoByThree, 5) == 5,
              "an inserted axis of extent one changes the extents and not the sequence");
static_assert(same_elements_in_order(elements_of(Shape{6, kT}), elements_of(Shape{1, 6, kT}), 3) == 3,
              "and neither does dropping one");

// ---- a transpose, the first mapping worth checking on its INDICES ----------------------------------
//
// `{2, 3, T}` with its two outer axes swapped is `{3, 2, T}`. Output element (i, j) — read over the
// result's extents [3, 2] — is source element (j, i) over [2, 3]. So unlike a reshape this moves
// numbers, and the check is that `permuted_element` recomposes exactly that index against the SOURCE's
// extents, for every element and not only the first. The expectation is written out here rather than
// derived, so it does not lean on the arithmetic it is checking.
constexpr Elements kThreeByTwo = elements_of(Shape{3, 2, kT});
static_assert(kThreeByTwo.rank == 2 && kThreeByTwo.count == 6);
constexpr Axes kSwapOuter{1, 0, 2}; // output axis 0 reads source axis 1 and vice versa; time stays

static_assert([] {
    for (std::int32_t i0 = 0; i0 < 3; ++i0) {   // result axis 0, extent 3, the outer one
        for (std::int32_t i1 = 0; i1 < 2; ++i1) { // result axis 1, extent 2, the inner element axis
            const std::int32_t e = i0 * 2 + i1;       // flat over the result's [3, 2]
            const std::int32_t expected = i1 * 3 + i0; // source element (i1, i0), flat over [2, 3]
            if (permuted_element(kThreeByTwo, kTwoByThree, kSwapOuter, e) != expected) {
                return false;
            }
        }
    }
    return true;
}(), "a transpose reads output element (i, j) from source element (j, i), recomposed against the source");

// ---- a slice, a mapping that SHIFTS along one axis and leaves the others ----------------------------
//
// `{4, T}` sliced on axis 0 to two elements read at stride two — offset zero — is `{2, T}`, and output
// element i reads source element `0 + i*2`: elements 0 and 2, not 0 and 1. So it moves numbers the way
// the transpose does, and the check is written out rather than derived from the arithmetic it checks.
constexpr Elements kFour = elements_of(Shape{4, kT});
constexpr Elements kTwo = elements_of(Shape{2, kT});
static_assert([] {
    for (std::int32_t i = 0; i < 2; ++i) {
        if (sliced_element(kTwo, kFour, 0, 0, 2, i) != i * 2) {
            return false;
        }
    }
    return true;
}(), "a slice reads output element i from source element offset + i*step");

// A non-zero offset, so the first element read is not the source's first. `{5, T}` offset 1 length 2
// step 2 reads source indices 1 and 3.
constexpr Elements kFive = elements_of(Shape{5, kT});
static_assert(sliced_element(kTwo, kFive, 0, 1, 2, 0) == 1 && sliced_element(kTwo, kFive, 0, 1, 2, 1) == 3,
              "a slice's first output element is its offset, and it steps from there");

// Two element axes, the sliced one outer: `{4, 3, T}` offset 1 length 2 step 1 on axis 0 is `{2, 3, T}`,
// and output (i, j) over [2, 3] reads source (1 + i, j) over [4, 3] — the sliced axis shifts, the other
// is the identity, recomposed against the SOURCE extents.
constexpr Elements kFourByThree = elements_of(Shape{4, 3, kT});
constexpr Elements kTwoByThreeSliced = elements_of(Shape{2, 3, kT});
static_assert([] {
    for (std::int32_t i = 0; i < 2; ++i) {
        for (std::int32_t j = 0; j < 3; ++j) {
            const std::int32_t e = i * 3 + j;
            const std::int32_t expected = (1 + i) * 3 + j;
            if (sliced_element(kTwoByThreeSliced, kFourByThree, 0, 1, 1, e) != expected) {
                return false;
            }
        }
    }
    return true;
}(), "a slice shifts its own axis and leaves the others, recomposed against the source");

// ---- an unfold, a mapping that ADDS an axis and reads overlapping windows ----------------------------
//
// `{6, T}` unfolded on axis 0 with window three at hop one is `{4, 3, T}`: the axis becomes N_out=4
// frames and a window axis of three appears beside it. Output element (f, k) over [4, 3] reads source
// element `f*hop + k` — so frame 0 is elements 0,1,2, frame 1 is 1,2,3, and the windows OVERLAP: a
// source element is read into more than one frame. The check is written out rather than derived from
// the arithmetic it checks, and it is the first mapping here whose output has one axis MORE than its
// source.
constexpr Elements kFrames = elements_of(Shape{4, 3, kT});
static_assert(kFrames.rank == 2 && kFrames.count == 12 && kFrames.full_rank == 3);
static_assert([] {
    for (std::int32_t f = 0; f < 4; ++f) {       // the frame axis
        for (std::int32_t k = 0; k < 3; ++k) {   // the window axis
            const std::int32_t e = f * 3 + k;        // flat over the result's [4, 3]
            if (unfolded_element(kFrames, kSix, 0, 3, 1, e) != f + k) { // hop 1: f*1 + k
                return false;
            }
        }
    }
    return true;
}(), "an unfold reads output frame f tap k from source element f*hop + k, over overlapping windows");

// And at a hop above one the frames advance by the hop: `{6, T}` window three hop two is `{2, 3, T}`,
// and output (f, k) reads source `f*2 + k` — frames 0,1,2 and 2,3,4, overlapping by window minus hop.
constexpr Elements kHopFrames = elements_of(Shape{2, 3, kT});
static_assert([] {
    for (std::int32_t f = 0; f < 2; ++f) {
        for (std::int32_t k = 0; k < 3; ++k) {
            if (unfolded_element(kHopFrames, kSix, 0, 3, 2, f * 3 + k) != f * 2 + k) {
                return false;
            }
        }
    }
    return true;
}(), "an unfold at hop two advances each frame by two, so its windows overlap by one");

// ---- what the WALK does with it ---------------------------------------------------------------------
//
// Six elements filled with six different numbers and one step run over them. Nothing in the operation
// set yet makes a wide value whose elements differ — a literal is one number and repeats — so the
// planes are filled by hand, exactly as `collapse_test.cpp` fills them to check a fold. Without that,
// a run of a rearrangement over equal elements would pass under a mapping that answered zero for
// every element.
constexpr std::int32_t kSourceCount = 6;

constexpr Graph merging() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{2, 3, kT}, ValueCategory::Input);
    OpAttr view{};
    view.kind = AttrKind::View;
    view.view.kind = ViewKind::Reshape;
    view.view.shape = Shape{6, kT};
    const auto out = add_node(g, op::Reshape, x, kNoValue, kNoValue, view);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, out});
    return g;
}

constexpr Graph kMerging = [] {
    Graph g = merging();
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the merging graph did not resolve");
    }
    return g;
}();

// Element e of the source carries e + 1, so a mapping that answered the wrong element answers a
// number that says WHICH element it read.
static_assert([] {
    Signals<kT> signals{};
    lay_out(kMerging, signals);
    const std::int32_t source = kMerging.inputs[0].value;
    // Written into the planes directly, as `collapse_test.cpp` does: the value's elements are
    // consecutive planes from `first[value]`, and filling them by hand is the only way to make a wide
    // value whose elements DIFFER at this point in the milestone.
    for (std::int32_t e = 0; e < kSourceCount; ++e) {
        for (std::size_t n = 0; n < kT; ++n) {
            signals.at[static_cast<std::size_t>(signals.first[static_cast<std::size_t>(source)] + e)][n] =
                static_cast<double>(e + 1);
        }
    }
    detail::walk_rearranging_step(kMerging, kMerging.nodes[0], 0, signals);
    const std::int32_t out = kMerging.outputs[0].value;
    for (std::int32_t e = 0; e < kSourceCount; ++e) {
        if (sample_at(signals, out, e, 0) != static_cast<double>(e + 1)) {
            return false;
        }
    }
    return true;
}(), "the walk moved a rearrangement's elements to the wrong places, or moved only some of them");

// The same, for the transpose — the one whose mapping is NOT the identity, so a walk that read
// element e as element e would answer wrong numbers rather than the same ones. Source planes filled
// by hand with e + 1, as above, and every output element checked against the permutation.
constexpr Graph transposing() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{2, 3, kT}, ValueCategory::Input);
    OpAttr view{};
    view.kind = AttrKind::View;
    view.view.kind = ViewKind::Transpose;
    view.view.order = Axes{1, 0, 2};
    const auto out = add_node(g, op::Transpose, x, kNoValue, kNoValue, view);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, out});
    return g;
}

constexpr Graph kTransposing = [] {
    Graph g = transposing();
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the transposing graph did not resolve");
    }
    return g;
}();

static_assert([] {
    Signals<kT> signals{};
    lay_out(kTransposing, signals);
    const std::int32_t source = kTransposing.inputs[0].value;
    for (std::int32_t e = 0; e < kSourceCount; ++e) {
        for (std::size_t n = 0; n < kT; ++n) {
            signals.at[static_cast<std::size_t>(signals.first[static_cast<std::size_t>(source)] + e)][n] =
                static_cast<double>(e + 1);
        }
    }
    detail::walk_rearranging_step(kTransposing, kTransposing.nodes[0], 0, signals);
    const std::int32_t out = kTransposing.outputs[0].value;
    // Output element (i0, i1) over [3, 2] must hold source element (i1, i0) over [2, 3], which is the
    // number i1 * 3 + i0 + 1 — so a walk that swapped the axes the wrong way, or moved nothing, says so.
    for (std::int32_t i0 = 0; i0 < 3; ++i0) {
        for (std::int32_t i1 = 0; i1 < 2; ++i1) {
            const std::int32_t e = i0 * 2 + i1;
            const std::int32_t src = i1 * 3 + i0;
            if (sample_at(signals, out, e, 0) != static_cast<double>(src + 1)) {
                return false;
            }
        }
    }
    return true;
}(), "the walk did not move a transpose's elements to where the permutation says");

// The same, for a strided slice — the mapping shifts, so a walk that read element e as element e would
// answer the wrong numbers. `{4, T}` sliced to `{2, T}` at offset 0 stride 2 takes source elements 0
// and 2, so the output holds 1 and 3 when the source planes are filled with e + 1.
constexpr Graph slicing() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{4, kT}, ValueCategory::Input);
    OpAttr view{};
    view.kind = AttrKind::View;
    view.view.kind = ViewKind::Slice;
    view.view.axis = 0;
    view.view.offset = 0;
    view.view.length = 2;
    view.view.step = 2;
    const auto out = add_node(g, op::Slice, x, kNoValue, kNoValue, view);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, out});
    return g;
}

constexpr Graph kSlicing = [] {
    Graph g = slicing();
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the slicing graph did not resolve");
    }
    return g;
}();

static_assert([] {
    Signals<kT> signals{};
    lay_out(kSlicing, signals);
    const std::int32_t source = kSlicing.inputs[0].value;
    for (std::int32_t e = 0; e < 4; ++e) {
        for (std::size_t n = 0; n < kT; ++n) {
            signals.at[static_cast<std::size_t>(signals.first[static_cast<std::size_t>(source)] + e)][n] =
                static_cast<double>(e + 1);
        }
    }
    detail::walk_rearranging_step(kSlicing, kSlicing.nodes[0], 0, signals);
    const std::int32_t out = kSlicing.outputs[0].value;
    // Output element i holds source element i*2, which is the number i*2 + 1: so 1 then 3, not 1 then 2.
    return sample_at(signals, out, 0, 0) == 1.0 && sample_at(signals, out, 1, 0) == 3.0;
}(), "the walk did not read a slice's elements from the strided source range");

// The same, for an unfold — the mapping ADDS an axis and reads overlapping windows, so a walk that read
// element e as element e, or lost a frame, would answer the wrong numbers. `{4, T}` unfolded to
// `{3, 2, T}` at window two hop one: output (f, k) reads source f + k, and the source planes are filled
// with e + 1, so a source element read into two frames appears in both.
constexpr Graph unfolding() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{4, kT}, ValueCategory::Input);
    OpAttr view{};
    view.kind = AttrKind::View;
    view.view.kind = ViewKind::Unfold;
    view.view.axis = 0;
    view.view.window = 2;
    view.view.hop = 1;
    const auto out = add_node(g, op::Unfold, x, kNoValue, kNoValue, view);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, out});
    return g;
}

constexpr Graph kUnfolding = [] {
    Graph g = unfolding();
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the unfolding graph did not resolve");
    }
    return g;
}();

static_assert([] {
    Signals<kT> signals{};
    lay_out(kUnfolding, signals);
    const std::int32_t source = kUnfolding.inputs[0].value;
    for (std::int32_t e = 0; e < 4; ++e) {
        for (std::size_t n = 0; n < kT; ++n) {
            signals.at[static_cast<std::size_t>(signals.first[static_cast<std::size_t>(source)] + e)][n] =
                static_cast<double>(e + 1);
        }
    }
    detail::walk_rearranging_step(kUnfolding, kUnfolding.nodes[0], 0, signals);
    const std::int32_t out = kUnfolding.outputs[0].value;
    // Output (f, k) over [3, 2] holds source element f + k, the number f + k + 1. Frame 0 is 1,2; frame
    // 1 is 2,3; frame 2 is 3,4 — so the shared elements appear in two frames, which is the overlap.
    for (std::int32_t f = 0; f < 3; ++f) {
        for (std::int32_t k = 0; k < 2; ++k) {
            if (sample_at(signals, out, f * 2 + k, 0) != static_cast<double>(f + k + 1)) {
                return false;
            }
        }
    }
    return true;
}(), "the walk did not read an unfold's overlapping windows from the source");

// ---- the refusals, observed rather than asserted ------------------------------------------------
//
// In `main` rather than as `static_assert`s for the reason `width_test.cpp` gives: under constant
// evaluation a `fail()` IS the compile error, so a refusal can only be watched from a run.

// The message is short because `Diagnostic` truncates past `kMaxMessageLength`; what it MEANS is the
// paragraph over the refusal below, and in `tests/reference/elements.hpp` where it is raised.
constexpr std::string_view kNoAttribute =
    "heinzel::rearranged_element: this rearranges axes and carries no attribute saying how";
constexpr std::string_view kOutsideItsOwn =
    "heinzel::same_elements_in_order: an element outside the result's own elements";

// A node that rearranges axes and carries no attribute saying how. The same shape of refusal
// `collapse_of` and `fold_of` make for the other kind of element meaning.
bool a_rearrangement_with_no_attribute_is_refused() {
    Node bare{};
    bare.op = op::Reshape;
    try {
        (void)rearranged_element(bare, kSix, kSix, 0);
        return false;
    } catch (const error& e) {
        return e.message() == kNoAttribute;
    }
}

// And an element outside the result's own elements, which is what a runner would ask for if it
// looped over the operand's count instead of the result's.
bool an_element_outside_the_result_is_refused() {
    try {
        (void)same_elements_in_order(kSix, kTwoByThree, 6);
        return false;
    } catch (const error& e) {
        return e.message() == kOutsideItsOwn;
    }
}

// A transpose is the first rearrangement whose mapping can be WRONG rather than merely mis-shaped, so
// its meaning refuses two mistakes a hand-built graph could carry past the pass — the pass refuses
// them too, by name, in `inference_test.cpp`; this is the second line, for a node the pass never saw.
constexpr std::string_view kMovesStreamed =
    "heinzel::permuted_element: this moves the streamed axis, which is time";
constexpr std::string_view kAxisNotThere =
    "heinzel::permuted_element: an order naming an axis the operand does not have";

// Moving the streamed axis asks a runner to read samples that do not exist yet as though they were
// elements — the innermost axis is time.
bool a_transpose_that_moves_the_streamed_axis_is_refused() {
    try {
        (void)permuted_element(kThreeByTwo, kTwoByThree, Axes{2, 1, 0}, 0);
        return false;
    } catch (const error& e) {
        return e.message() == kMovesStreamed;
    }
}

// And an order naming an axis the operand does not have cannot be read: there is no extent to read it
// with, so it stops rather than answering a plausible wrong element.
bool a_transpose_whose_order_names_a_missing_axis_is_refused() {
    try {
        (void)permuted_element(kThreeByTwo, kTwoByThree, Axes{3, 0, 2}, 0);
        return false;
    } catch (const error& e) {
        return e.message() == kAxisNotThere;
    }
}

// A slice's meaning refuses three mistakes a hand-built node could carry past the pass — the pass
// refuses them too, by name, in `inference_test.cpp`; this is the second line, for a node the pass
// never saw.
constexpr std::string_view kStepsBackward =
    "heinzel::sliced_element: a slice steps by less than one; a reversal is _flip";
constexpr std::string_view kReadsPast =
    "heinzel::sliced_element: a slice reads past the source axis";
constexpr std::string_view kNotAnElementAxis =
    "heinzel::sliced_element: a slice along an axis that is not an element axis here";

// A stride below one is a reversal, which is `_flip`, not a slice.
bool a_slice_that_steps_backward_is_refused() {
    try {
        (void)sliced_element(kTwo, kFour, 0, 0, 0, 0);
        return false;
    } catch (const error& e) {
        return e.message() == kStepsBackward;
    }
}

// A window that reaches past the source axis reads outside the operand's storage: offset 3, stride 1,
// so the second element asks for index 4 of an axis of extent 4.
bool a_slice_that_reads_past_the_source_is_refused() {
    try {
        (void)sliced_element(kTwo, kFour, 0, 3, 1, 1);
        return false;
    } catch (const error& e) {
        return e.message() == kReadsPast;
    }
}

// And an axis that is not one of the value's element axes — here axis 1 of a value with a single
// element axis — cannot be sliced.
bool a_slice_along_a_missing_axis_is_refused() {
    try {
        (void)sliced_element(kTwo, kFour, 1, 0, 1, 0);
        return false;
    } catch (const error& e) {
        return e.message() == kNotAnElementAxis;
    }
}

// An unfold's meaning refuses the mistakes a hand-built node could carry past the pass — the pass
// refuses them too, by name, in `view_inference_test.cpp`; this is the second line, for a node the pass
// never saw.
constexpr std::string_view kNotOneMoreAxis =
    "heinzel::unfolded_element: an unfold does not add exactly one axis to its value";
constexpr std::string_view kHopOrWindow =
    "heinzel::unfolded_element: an unfold with a hop or window below one";
constexpr std::string_view kNotASourceAxis =
    "heinzel::unfolded_element: an unfold along an axis that is not a source element axis";
constexpr std::string_view kWindowDisagrees =
    "heinzel::unfolded_element: the window axis disagrees with the declared window";
constexpr std::string_view kUnfoldReadsPast =
    "heinzel::unfolded_element: an unfold reads past the source axis";

// An unfold adds exactly one axis; a result with the same rank as its source was not unfolded into it.
bool an_unfold_that_does_not_add_an_axis_is_refused() {
    try {
        (void)unfolded_element(kSix, kSix, 0, 3, 1, 0);
        return false;
    } catch (const error& e) {
        return e.message() == kNotOneMoreAxis;
    }
}

// A hop or window below one is not a stride or a frame.
bool an_unfold_with_a_hop_below_one_is_refused() {
    try {
        (void)unfolded_element(kFrames, kSix, 0, 3, 0, 0);
        return false;
    } catch (const error& e) {
        return e.message() == kHopOrWindow;
    }
}

// An axis that is not one of the source's own element axes — here axis one of a value with a single
// element axis — cannot be unfolded.
bool an_unfold_along_a_missing_axis_is_refused() {
    try {
        (void)unfolded_element(kFrames, kSix, 1, 3, 1, 0);
        return false;
    } catch (const error& e) {
        return e.message() == kNotASourceAxis;
    }
}

// A window axis whose extent disagrees with the declared window is a node whose shape and attribute were
// built inconsistently.
bool an_unfold_whose_window_axis_disagrees_is_refused() {
    try {
        (void)unfolded_element(kFrames, kSix, 0, 2, 1, 0); // kFrames' window axis is 3, not 2
        return false;
    } catch (const error& e) {
        return e.message() == kWindowDisagrees;
    }
}

// A frame that reaches past the source axis reads outside the operand's storage: a hand-built result
// claiming five frames of window three at hop two asks frame 4 tap 2 for source index 10 of an extent-6
// axis.
bool an_unfold_that_reads_past_the_source_is_refused() {
    const Elements too_many = elements_of(Shape{5, 3, kT});
    try {
        (void)unfolded_element(too_many, kSix, 0, 3, 2, 5 * 3 - 1); // frame 4, tap 2
        return false;
    } catch (const error& e) {
        return e.message() == kUnfoldReadsPast;
    }
}

} // namespace

int main() {
    if (!a_rearrangement_with_no_attribute_is_refused()) return 2;
    if (!an_element_outside_the_result_is_refused()) return 3;
    if (!a_transpose_that_moves_the_streamed_axis_is_refused()) return 5;
    if (!a_transpose_whose_order_names_a_missing_axis_is_refused()) return 6;
    if (!a_slice_that_steps_backward_is_refused()) return 7;
    if (!a_slice_that_reads_past_the_source_is_refused()) return 8;
    if (!a_slice_along_a_missing_axis_is_refused()) return 9;
    if (!an_unfold_that_does_not_add_an_axis_is_refused()) return 10;
    if (!an_unfold_with_a_hop_below_one_is_refused()) return 11;
    if (!an_unfold_along_a_missing_axis_is_refused()) return 12;
    if (!an_unfold_whose_window_axis_disagrees_is_refused()) return 13;
    if (!an_unfold_that_reads_past_the_source_is_refused()) return 14;

    // And none of the refusals is refusing everything: an ordinary rearrangement, transpose, slice and
    // unfold all still answer.
    return same_elements_in_order(kSix, kTwoByThree, 4) == 4 &&
                   permuted_element(kThreeByTwo, kTwoByThree, kSwapOuter, 0) == 0 &&
                   sliced_element(kTwo, kFour, 0, 0, 2, 1) == 2 &&
                   unfolded_element(kFrames, kSix, 0, 3, 1, 3 * 1 + 2) == 3 // frame 1, tap 2 → source 3
               ? 0
               : 4;
}
