// A `_reshape`'s refusals, observed in a run: the analysis pass rejects a rearrangement that does not
// conserve its elements, by name.
//
// Lifted out of `rearrange_test.cpp` at S10b (operator decision 2026-09-01,
// `operator-decision-reshape-lift.md`) once reshape earned a concern of its own — its check, now
// `check_reshape_attribute` in `sema/attribute_checks.hpp` — the same per-view-kind split `rearrange_flip_test.cpp` and
// `rearrange_pad_test.cpp` already are. The same-elements INDEX MAPPING (`same_elements_in_order`,
// shared with drop/insert-axis) stays in the base file, whose identity is "which element comes from
// where"; what lives here is the pass refusal the lift moved out of the executor.
//
// In `main` rather than as `static_assert`s for the reason `width_test.cpp` gives: under constant
// evaluation a `fail()` IS the compile error, so a refusal can only be watched from a run.
//
// And a regroup of a computed value (Plan/28-dsl-proposals/03 B3): `rearrange` records it as written and the analysis
// works its target out once the operand has its shape, so the analysed graph is the one `.reshape` builds with the
// extents written out, a recurrence's own past included, one read before it can be completed too, and it runs on
// `Executor` as the walk runs it; one spec that reorders and regroups too.

#include <cmath>
#include <cstddef>
#include <string_view>
#include <vector>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/ir/ir.hpp"
#include "heinzel/notation/rearrange.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

#include "elements.hpp"
#include "executed.hpp"
#include "hand_built_graph.hpp"
#include "rearrange_element.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t kT = 4; // the streamed axis, kept short: nothing here is about time

// The executor backstop's message, short because `Diagnostic` truncates past `kMaxMessageLength`; what
// it MEANS is the paragraph over `same_elements_in_order` in `tests/reference/elements.hpp` where it is raised.
constexpr std::string_view kReCutsTime =
    "heinzel::same_elements_in_order: a rearrangement changed how many numbers a sample has";

// The PASS diagnostics, short because `Diagnostic` truncates past `kMaxMessageLength` (96); what each
// MEANS is the comment on `check_reshape_attribute` (`sema/attribute_checks.hpp`). The two ways a reshape
// fails to conserve its elements are refused by name in the pass, and the executor keeps its own refusal
// as the hand-built-graph backstop.
constexpr std::string_view kPassReCutsCount =
    "_reshape changes how many numbers a sample has, from 6 to 1";
constexpr std::string_view kPassReCutsTime =
    "_reshape re-cuts the streamed axis; that is time and stays whole";

// `Shape{2, 3, T}` to `Shape{6 * T}` conserves NUMBERS and not numbers per SAMPLE: it says six
// numbers per sample for T samples become one number per sample for six T. Under this tree's settled
// convention the innermost axis is the streamed one, so that is a reframing over time — which is what
// `_unfold` and `_ovadd` are for. The pass refuses it BY NAME now (S10b); the executor keeps its own
// refusal as the backstop for a hand-built graph that never went through the pass.
bool a_rearrangement_that_re_cuts_time_is_refused() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{2, 3, kT}, ValueCategory::Input);
    OpAttr view{};
    view.kind = AttrKind::View;
    view.view.kind = ViewKind::Reshape;
    view.view.shape = Shape{6 * kT};
    add_node(g, op::Reshape, x, kNoValue, kNoValue, view);
    const Diagnostic d = infer(g);
    if (d.ok || d.message.view() != kPassReCutsCount) {
        return false; // the pass must refuse it now, by name — the lifted refusal
    }
    // The reference walk still refuses the same bad reshape built by hand: its backstop behind the pass.
    try {
        (void)rearranged_element(g.nodes[0], elements_of(Shape{6 * kT}), elements_of(Shape{2, 3, kT}), 0);
        return false;
    } catch (const error& e) {
        return e.message() == kReCutsTime;
    }
}

// The narrower samples-changing sub-case the S8 finding flagged unverified: `Shape{8, 4}` to
// `Shape{8, 3}` keeps numbers-per-sample (8 both ways) so the executor count check does NOT see it, but
// it re-cuts the STREAMED axis (4 -> 3), which is time. The pass refuses it by name — the resolution the
// operator decision required, not left as a "may or may not".
bool a_reshape_that_re_cuts_the_streamed_axis_is_refused() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{8, 4}, ValueCategory::Input);
    OpAttr view{};
    view.kind = AttrKind::View;
    view.view.kind = ViewKind::Reshape;
    view.view.shape = Shape{8, 3};
    add_node(g, op::Reshape, x, kNoValue, kNoValue, view);
    const Diagnostic d = infer(g);
    return !d.ok && d.message.view() == kPassReCutsTime;
}

// And a valid reshape is ACCEPTED: `Shape{2, 3, T}` to `Shape{6, T}` re-cuts only the element axes
// (numbers-per-sample 6 both ways) and keeps the streamed axis whole — the S8 twin's shape. The lifted
// refusal must not object to the case it exists to let through.
bool a_reshape_that_only_re_cuts_element_axes_is_accepted() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{2, 3, kT}, ValueCategory::Input);
    OpAttr view{};
    view.kind = AttrKind::View;
    view.view.kind = ViewKind::Reshape;
    view.view.shape = Shape{6, kT};
    add_node(g, op::Reshape, x, kNoValue, kNoValue, view);
    return infer(g).ok;
}

// ---- a regroup of a computed value ----------------------------------------------------------------------------

constexpr Index n{};

constexpr Graph analysed(Graph g, const Bindings& sizes = Bindings{}) {
    if (const Diagnostic d = infer(g, sizes); !d.ok) {
        fail(d.message.view());
    }
    return g;
}

// A split of a computed value; built, the two differ (the regroup is marked, its spec's rank recorded).
template <bool Spelled>
constexpr Graph split_of_a_computed_value() {
    Input x{"x", Signal{12, 16}};
    Expr twice = x * 2.0;
    Expr cut = Spelled ? rearrange("(c f) -> c f", twice, {{"f", 4}}) : twice.reshape({3, 4});
    Expr shifted = cut + 1.0;
    Output y{"y"};
    y = shifted;
    return finish(y);
}
static_assert(analysed(split_of_a_computed_value<true>()) == analysed(split_of_a_computed_value<false>()));
static_assert(split_of_a_computed_value<true>() != split_of_a_computed_value<false>());

// A merge over a stream of run-time length: a group of one takes its axis's extent, a symbol too.
template <bool Spelled>
constexpr Graph merge_over_a_length() {
    Input x{"x", Signal{3, 4, Length{}}};
    Expr twice = x * 2.0;
    Expr merged = Spelled ? rearrange("c f -> (c f)", twice) : twice.reshape({12});
    Output y{"y"};
    y = merged;
    return finish(y);
}
static_assert(analysed(merge_over_a_length<true>()) == analysed(merge_over_a_length<false>()));

// A recurrence that regroups its own past, which has no rank in the first sweep: the regroup waits for it. Analysed
// again, nothing is left to complete.
template <bool Spelled>
constexpr Graph regrouped_in_a_recurrence() {
    Input x{"x", Signal{3, 4, 16}};
    Variable v;
    Expr past = v[n - 1];
    Expr flat = Spelled ? rearrange("c f -> (c f)", past) : past.reshape({12});
    Expr halved = flat * 0.5;
    Expr back = Spelled ? rearrange("(c f) -> c f", halved, {{"c", 3}}) : halved.reshape({3, 4});
    v[n] = back + x;
    Output y{"y"};
    y = v;
    return finish(y);
}
static_assert(analysed(regrouped_in_a_recurrence<true>()) == analysed(regrouped_in_a_recurrence<false>()));
static_assert(analysed(analysed(regrouped_in_a_recurrence<true>())) == analysed(regrouped_in_a_recurrence<true>()));

// A merge of views: `image_pipeline`'s nine values of each 3×3 window side by side, merged by a second rearrange.
template <bool Spelled>
constexpr Graph merged_windows() {
    Input grey{"grey", {8, 10}};
    Expr windows = grey.unfold(0, 3).unfold(2, 3);
    Expr side_by_side = rearrange("h a w b -> h w a b", windows);
    Expr nine = Spelled ? rearrange("h w a b -> h w (a b)", side_by_side) : side_by_side.reshape({6, 8, 9});
    Expr middle = nine.median(2);
    Output y{"y"};
    y = middle;
    return finish(y);
}
static_assert(analysed(merged_windows<true>()) == analysed(merged_windows<false>()));

// The same nine values in one spec that reorders and merges at once: the graph of the two rearranges.
constexpr Expr nine_values(Expr windows, bool one_spec) {
    if (one_spec) {
        return rearrange("h a w b -> h w (a b)", windows);
    }
    Expr side_by_side = rearrange("h a w b -> h w a b", windows);
    return rearrange("h w a b -> h w (a b)", side_by_side);
}
template <bool OneSpec>
constexpr Graph windows_in_one_spec() {
    Input grey{"grey", {8, 10}};
    Expr windows = grey.unfold(0, 3).unfold(2, 3);
    Expr nine = nine_values(windows, OneSpec);
    Expr middle = nine.median(2);
    Output y{"y"};
    y = middle;
    return finish(y);
}
static_assert(analysed(windows_in_one_spec<true>()) == analysed(windows_in_one_spec<false>()));

// A regroup read before it can be completed hands on no shape, so its reader waits with it rather than disagree with
// the operand's (the second review of package B). Here the operand's rank still grows: `w * v[n - 1]` has `w`'s two
// axes first, the spec three. Analysed again, nothing is left to complete.
template <bool Spelled>
constexpr Graph regrouped_while_the_rank_grows() {
    Input w{"w", Signal{6, 16}};
    Input z{"z", Signal{2, 2, 3, 16}};
    Variable v;
    Expr past = v[n - 1];
    Expr fed = w * past;
    Expr split = Spelled ? rearrange("c (a b) -> c a b", fed, {{"a", 2}}) : fed.reshape({2, 2, 3});
    Expr scaled = split * z;
    Expr merged = Spelled ? rearrange("c a b -> c (a b)", scaled) : scaled.reshape({2, 6});
    v[n] = merged;
    Output y{"y"};
    y = v;
    return finish(y);
}
static_assert(analysed(regrouped_while_the_rank_grows<true>()) == analysed(regrouped_while_the_rank_grows<false>()));
static_assert(analysed(analysed(regrouped_while_the_rank_grows<true>())) ==
              analysed(regrouped_while_the_rank_grows<true>()));

// And here a split meets a symbol the solver binds to 8 only after the first sweep.
template <bool Spelled>
constexpr Graph split_of_a_bound_symbol() {
    Input x{"x", Signal{Dim{0}, 16}};
    Input z{"z", Signal{4, 2, 16}};
    Expr split = Spelled ? rearrange("(a b) -> a b", x, {{"a", 4}}) : x.reshape({4, 2});
    Output y{"y"};
    y = split * z;
    return finish(y);
}
constexpr Bindings kEight = Bindings{}.bind(Dim{0}, 8);
static_assert(analysed(split_of_a_bound_symbol<true>(), kEight) == analysed(split_of_a_bound_symbol<false>(), kEight));

// A computed value split, folded along one of its new axes, scaled by the fold and merged back, and one cut into
// patches: on `Executor` at High, bit for bit as the walk runs it, or the test exits.
constexpr std::size_t kRun = 37;

constexpr Graph split_folded_merged() {
    Input x{"x", Signal{12, kRun}};
    Expr twice = x * 2.0;
    Expr split = rearrange("(c f) -> c f", twice, {{"f", 4}});
    Expr rows = split.sum(1);
    Expr column = rows.reshape({3, 1});
    Expr scaled = split * column;
    Output y{"y"};
    y = rearrange("c f -> (c f)", scaled);
    return finish(y);
}

// A computed value cut into patches by one spec that splits, reorders and merges, and each patch summed.
constexpr Graph patches_summed() {
    Input x{"x", Signal{6, 20, kRun}};
    Expr twice = x * 2.0;
    Expr patches = rearrange("(h a) (w b) -> (h w) (a b)", twice, {{"a", 3}, {"b", 5}});
    Expr sums = patches.sum(1);
    Output y{"y"};
    Output z{"z"};
    y = patches;
    z = sums;
    return finish(y, z);
}

void run_against_the_walk() {
    std::vector<float> x(12 * kRun);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = static_cast<float>(std::sin(0.37 * static_cast<double>(i)) * static_cast<double>(1 + i % 5));
    }
    testing::executed<split_folded_merged>({{"x", x.data(), 12}});
    std::vector<float> image(120 * kRun);
    for (std::size_t i = 0; i < image.size(); ++i) {
        image[i] = static_cast<float>(std::cos(0.11 * static_cast<double>(i)) * static_cast<double>(1 + i % 7));
    }
    testing::executed<patches_summed>({{"x", image.data(), 120}});
}

} // namespace

int main() {
    if (!a_rearrangement_that_re_cuts_time_is_refused()) return 1;
    if (!a_reshape_that_re_cuts_the_streamed_axis_is_refused()) return 2;
    if (!a_reshape_that_only_re_cuts_element_axes_is_accepted()) return 3;
    run_against_the_walk();
    return 0;
}
