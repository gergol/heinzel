// Time is declared (Plan/archive/14-dsl/03, Decision 53): a value is a SIGNAL or a TENSOR, and the analysis keeps each
// where it belongs.
//
//   kinds       inputs, `iota`'s seed, a constant-only value (neutral), coupled recurrences a sweep apart
//   refusals    a tensor-only recurrence (first, before its lag), a lag, an fft, a contraction of a tensor with a
//               signal, the hidden stream axis named by a fold, an order statistic, a slice, a window and an
//               order; squeeze and unsqueeze bounds for every value, the rank guard
//   marked      reshape targets of element axes only, completed after the sweep (graph-equal to the full target),
//               inside a recurrence's own definition, of a rank-0 value, past kMaxRank; short transpose orders
//   iota        `iota({…})` a tensor, `iota(Signal{…, frames})` a signal recording its `Length`; the refusals
//   notation    in `tensor_signal_notation_test.cpp`
//   mixed       a tensor weight and a Param against a signal, run, against a plain loop
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string_view>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/notation/rearrange.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "hand_built_graph.hpp"

using namespace heinzel;

namespace {

constexpr Index n{};
constexpr Length kT{};
using detail::TimeKind;

constexpr Graph analysed(Graph g) {
    if (const Diagnostic d = infer(g); !d.ok) {
        fail(d.message.view());
    }
    return g;
}

// What the analysis says of a graph: nothing (ok) or its refusal's text.
constexpr bool says(Graph g, std::string_view text) {
    const Diagnostic d = infer(g);
    return text.empty() ? d.ok : !d.ok && d.message.view() == text;
}

// ---- kinds ------------------------------------------------------------------------------------------------

constexpr TimeKind kind_of(const Graph& g, std::int32_t value, const detail::Declared& declared) {
    detail::TimeKinds kind{};
    detail::time_kinds(g, declared, kind);
    return kind[static_cast<std::size_t>(value)];
}
constexpr TimeKind kind_of(const Graph& g, std::int32_t value) { return kind_of(g, value, detail::declared_variables(g)); }
// An analysed graph's kinds, its declared variables taken as it was built: after one analysis every variable has a
// shape and would read as declared, seeding a signal where the join should decide.
constexpr TimeKind output_kind(Graph g, std::size_t o) {
    return kind_of(analysed(g), g.outputs[o].value, detail::declared_variables(g));
}

constexpr Graph declared_kinds() {
    Input x{"x", Signal{4, 16}};
    Input w{"w", {4}};
    Param p{"p"};
    Output a{"a"};
    Output b{"b"};
    Output c{"c"};
    Output d{"d"};
    Output e{"e"};
    a = x * w;                         // signal
    b = w * 2.0;                       // tensor
    c = p * w;                         // tensor
    d = iota({4}) * 1.0;               // tensor: the generator keeps its seed
    e = iota(Signal{4, 16}) + 0.0;     // signal
    return finish(a, b, c, d, e);
}
static_assert(output_kind(declared_kinds(), 0) == TimeKind::Signal && output_kind(declared_kinds(), 1) == TimeKind::Tensor &&
              output_kind(declared_kinds(), 2) == TimeKind::Tensor && output_kind(declared_kinds(), 3) == TimeKind::Tensor &&
              output_kind(declared_kinds(), 4) == TimeKind::Signal);

// A generator has no operand: the kind it records is kept — here a tensor, where the join alone would leave it
// neutral — and one that records none (`iota(Signal{…})`, spanning a real stream) is a signal.
constexpr bool generator_keeps_its_seed(bool timeless) {
    Graph g{};
    OpAttr ramp{};
    ramp.kind = AttrKind::Iota;
    ramp.generator.shape = Shape{4, 1};
    const auto r = add_node(g, op::Iota, kNoValue, kNoValue, kNoValue, ramp);
    g.values[static_cast<std::size_t>(r)].timeless = timeless;
    const auto y = add_node(g, op::Mul, r, r);
    return kind_of(g, y) == (timeless ? TimeKind::Tensor : TimeKind::Signal);
}
static_assert(generator_keeps_its_seed(true) && generator_keeps_its_seed(false));

// Two literals multiplied stay neutral; what they meet decides: here a tensor.
constexpr bool constants_stay_neutral() {
    Graph g{};
    const auto w = add_value(g, ElemType::F32, Shape{4, 1}, ValueCategory::Input);
    g.values[static_cast<std::size_t>(w)].timeless = true;
    const auto half = add_value(g, ElemType::Unknown, Shape{}, ValueCategory::Const);
    const auto three = add_value(g, ElemType::Unknown, Shape{}, ValueCategory::Const);
    const auto k = add_node(g, op::Mul, half, three);
    const auto y = add_node(g, op::Mul, w, k);
    return kind_of(g, k) == TimeKind::Neutral && kind_of(g, y) == TimeKind::Tensor && kind_of(g, half) == TimeKind::Neutral;
}
static_assert(constants_stay_neutral());

// `a` reads `b`'s past and a tensor; `b` reads the signal. `a` is defined first, so its kind arrives a sweep
// after `b`'s.
constexpr Graph coupled() {
    Input x{"x", Signal{2, 16}};
    Input w{"w", {2}};
    Variable a;
    Variable b;
    a[n] = w * b[n - 1];
    b[n] = x[n] + 0.5 * a[n - 1];
    Output y{"y"};
    Output z{"z"};
    y = a * 1.0;
    z = b * 1.0;
    return finish(y, z);
}
static_assert(output_kind(coupled(), 0) == TimeKind::Signal && output_kind(coupled(), 1) == TimeKind::Signal);

// ---- refusals ---------------------------------------------------------------------------------------------

constexpr std::string_view kNeedsASignal = "heinzel: a recurrence needs a Signal in it, or a declared length";
constexpr std::string_view kLagsATensor = "heinzel: a lag reads a tensor — declare it as a Signal to read its past";
constexpr std::string_view kPastTheLast = "heinzel: that axis is past the tensor's last";

constexpr Graph tensor_only_recurrence() { // its lag comes first in the graph; the recurrence refusal wins
    Input w{"w", {4}};
    Output y{"y"};
    y[n] = w * y[n - 1];
    return y;
}
constexpr Graph literal_only_recurrence() {
    Variable v;
    v[n] = 0.5 * v[n - 1] + 1.0;
    Input x{"x", Signal{16}};
    Output y{"y"};
    y = x * v;
    return y;
}
constexpr Graph lagged_tensor() {
    Input x{"x", Signal{4, 16}};
    Input w{"w", {4}};
    Output y{"y"};
    y = x * w[n - 1];
    return y;
}
constexpr Graph fft_of_a_tensor() {
    Input w{"w", {4}};
    OpAttr attr{};
    attr.kind = AttrKind::Fft;
    attr.fft.n = 4;
    Output y{"y"};
    y = detail::append_unary(w, op::Fft, attr);
    return y;
}
constexpr Graph tensor_times_signal() {
    Input w{"w", {4, 3}};
    Input x{"x", Signal{3, 5, 16}};
    Output y{"y"};
    y = einsum("ik,kj->ij", w, x);
    return y;
}
static_assert(says(tensor_only_recurrence(), kNeedsASignal), "no stream in the graph: no length to take");
// Their length declared (Plan/archive/14-dsl/10), the same recurrences are signals; the literal one takes x's length
// undeclared too (Plan/28-dsl-proposals/03 B1), below.
constexpr Graph tensor_driven_declared() {
    Input w{"w", {4}};
    Output y{"y", Signal{4, 16}};
    y[n] = w * y[n - 1];
    return y;
}
constexpr Graph literal_only_declared() {
    Variable v{Signal{16}};
    v[n] = 0.5 * v[n - 1] + 1.0;
    Input x{"x", Signal{16}};
    Output y{"y"};
    y = x * v;
    return y;
}
constexpr Graph knob_oscillator() { // no signal drives it; declared, it is one
    Param f{"f"};
    Variable phase{Signal{16}};
    phase[n] = frac(phase[n - 1] + f * 0.01);
    Output y{"y"};
    y = phase * 2.0;
    return y;
}
static_assert(says(tensor_driven_declared(), "") && says(literal_only_declared(), "") && says(knob_oscillator(), ""));
static_assert(analysed(literal_only_recurrence()) == analysed(literal_only_declared()));
static_assert(says(lagged_tensor(), kLagsATensor) && says(fft_of_a_tensor(), "heinzel: an fft needs a Signal"));
constexpr Graph signal_times_tensor() {
    Input x{"x", Signal{4, 3, 16}};
    Input w{"w", {3, 5}};
    Output y{"y"};
    y = einsum("ik,kj->ij", x, w);
    return y;
}
static_assert(says(tensor_times_signal(), "heinzel: a tensor times a signal needs a multiply and sum, not a contraction") &&
              says(signal_times_tensor(), "heinzel: a tensor times a signal needs a multiply and sum, not a contraction"));

// A delay's distance is its second operand, and a `Param` may be one: only what is delayed must be a signal.
constexpr Graph delayed_by_a_param() {
    Input x{"x", Signal{3, 16}};
    Param by{"by"};
    Expr a = x * 1.0;
    Expr depth = by * 1.0;
    Arena* arena = bring_together(a, depth);
    BuildNode node{};
    node.op = op::DelayLine;
    node.inputs[0] = a.position();
    node.inputs[1] = depth.position();
    node.attr.kind = AttrKind::SignalledDelay;
    node.attr.signalled_delay.max_delay = 4;
    arena->nodes.push_back(node);
    Output y{"y"};
    y = Expr{arena, static_cast<std::int32_t>(arena->nodes.size() - 1)};
    return y;
}
static_assert(says(delayed_by_a_param(), ""));

// The hidden axis of a tensor `{4, 3}` (recorded `{4, 3, 1}`) named by each rule that takes an axis.
template <int Case>
constexpr Graph hidden_axis() {
    Input w{"w", {4, 3}};
    Output y{"y"};
    if constexpr (Case == 0) {
        y = w.sum(2);
    } else if constexpr (Case == 1) {
        y = detail::reduce(w, ReduceOp::Sum, Axes{kInnermostAxis}); // no member takes a negative axis
    } else if constexpr (Case == 2) {
        y = w.sort(2);
    } else if constexpr (Case == 3) {
        y = w.slice(2, 0, 1);
    } else if constexpr (Case == 4) {
        y = w.unfold(2, 1);
    } else {
        y = detail::transpose(w, Axes{2, 1, 0}); // unmarked: the member's order names element axes only
    }
    return y;
}
static_assert(says(hidden_axis<0>(), kPastTheLast) && says(hidden_axis<1>(), kPastTheLast) && says(hidden_axis<2>(), kPastTheLast) &&
              says(hidden_axis<3>(), kPastTheLast) && says(hidden_axis<4>(), kPastTheLast) && says(hidden_axis<5>(), kPastTheLast));
// ...among several axes, past the rank, and as a two-axis window's second.
template <int Case>
constexpr Graph hidden_elsewhere() {
    Input w{"w", {4, 3}};
    Output y{"y"};
    if constexpr (Case == 0) {
        y = w.sum({0, 2});
    } else if constexpr (Case == 1) {
        y = w.sum(3);
    } else {
        y = w.unfold2d(0, 1, 2, 1);
    }
    return y;
}
static_assert(says(hidden_elsewhere<0>(), kPastTheLast) && says(hidden_elsewhere<1>(), kPastTheLast) && says(hidden_elsewhere<2>(), kPastTheLast));
// ...and the element axes of the same tensor are its own.
constexpr Graph element_axes() {
    Input w{"w", {4, 3}};
    Output a{"a"};
    Output b{"b"};
    Output c{"c"};
    a = w.sum(1);
    b = w.sort(0);
    c = w.transpose({1, 0});
    return finish(a, b, c);
}
static_assert(says(element_axes(), ""));

// Squeeze and unsqueeze, for every value: `{4, 1, 16}`, `{4, 1}` (time of one sample, still time) and, at the
// rank guard, a value of kMaxRank axes.
template <ViewKind Kind, std::int32_t Axis, int Which = 0>
constexpr Graph viewed() {
    Input x{"x", Which == 0 ? Signal{4, 1, 16} : Which == 1 ? Signal{4, 1} : Signal{1, 1, 1, 1, 1, 16}};
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = Kind;
    attr.view.axis = Axis;
    Output y{"y"};
    y = detail::append_unary(x, op::Reshape, attr); // a squeeze and an unsqueeze are `_reshape`s, a negative axis too
    return y;
}
constexpr std::string_view kSqueeze = "heinzel: a squeeze drops an element axis of extent 1";
constexpr std::string_view kUnsqueeze = "heinzel: an unsqueeze inserts an element axis, and a value has at most kMaxRank axes";
static_assert(says(viewed<ViewKind::Squeeze, 1>(), "") && says(viewed<ViewKind::Squeeze, 0>(), kSqueeze) &&
              says(viewed<ViewKind::Squeeze, 2>(), kSqueeze) && says(viewed<ViewKind::Squeeze, -1>(), kSqueeze) &&
              says(viewed<ViewKind::Squeeze, 1, 1>(), kSqueeze));
// The shape helpers hand an axis the value has no room for back, rather than writing past the storage.
constexpr ViewAttr at_axis(std::int32_t axis) {
    ViewAttr v{};
    v.axis = axis;
    return v;
}
static_assert(detail::inserted_axis_shape(Shape{4, 1, 16}, at_axis(5)) == Shape{4, 1, 16} &&
              detail::inserted_axis_shape(Shape{4, 1, 16}, at_axis(-1)) == Shape{4, 1, 16} &&
              detail::inserted_axis_shape(Shape{1, 1, 1, 1, 1, 16}, at_axis(0)) == Shape{1, 1, 1, 1, 1, 16} &&
              detail::dropped_axis_shape(Shape{4, 1, 16}, at_axis(5)) == Shape{4, 1, 16} &&
              detail::dropped_axis_shape(Shape{4, 1, 16}, at_axis(-1)) == Shape{4, 1, 16});
static_assert(says(viewed<ViewKind::Unsqueeze, 0>(), "") && says(viewed<ViewKind::Unsqueeze, 2>(), "") &&
              says(viewed<ViewKind::Unsqueeze, 3>(), kUnsqueeze) && says(viewed<ViewKind::Unsqueeze, -1>(), kUnsqueeze) &&
              says(viewed<ViewKind::Unsqueeze, 0, 2>(), kUnsqueeze));

// ---- marked reshapes and short orders -----------------------------------------------------------------------

template <bool Marked>
constexpr Graph regrouped() {
    Input x{"x", Signal{6, 16}};
    Output y{"y"};
    y = Marked ? x.reshape({2, 3}) * 2.0 : detail::reshape(x, Shape{2, 3, 16}) * 2.0;
    return y;
}
template <bool Marked>
constexpr Graph transposed() {
    Input x{"x", Signal{2, 3, 16}};
    Output y{"y"};
    y = (Marked ? x.transpose({1, 0}) : detail::transpose(x, Axes{1, 0, 2})) * 2.0;
    return y;
}
static_assert(analysed(regrouped<true>()) == analysed(regrouped<false>()), "the marked target completed: the full one");
static_assert(analysed(transposed<true>()) == analysed(transposed<false>()), "the marked order completed: the full one");
static_assert(analysed(analysed(regrouped<true>())) == analysed(regrouped<true>()) &&
                  analysed(analysed(transposed<true>())) == analysed(transposed<true>()),
              "and a second analysis changes nothing");

// An unmarked order is read as written: one short of the rank is no permutation of it.
constexpr Graph short_and_unmarked() {
    Input x{"x", Signal{2, 3, 16}};
    Output y{"y"};
    y = detail::transpose(x, Axes{1, 0}) * 2.0;
    return y;
}
static_assert(says(short_and_unmarked(), "_xpose over rank 3: an order must name every axis of it exactly once"));

// In a recurrence the value's rank grows between sweeps: `w * y[n - 1]` has `w`'s rank before the past has one,
// and `{1, 0}` is a whole order of that. A marked order waits for the rank it is one short of.
template <bool Marked>
constexpr Graph ordered_in_a_recurrence() {
    Input x{"x", Signal{4, 4, 16}};
    Input w{"w", {4}};
    Output y{"y"};
    const Expr past = w * y[n - 1];
    y[n] = x[n] + (Marked ? past.transpose({1, 0}) : detail::transpose(past, Axes{1, 0, 2}));
    return y;
}
static_assert(analysed(ordered_in_a_recurrence<true>()) == analysed(ordered_in_a_recurrence<false>()));

// A marked order that does not fit the value once shapes settle; one that puts a tensor's hidden axis first.
template <int Case>
constexpr Graph misordered() {
    Input x{"x", Signal{2, 3, 16}};
    Input w{"w", {4, 3}};
    Output y{"y"};
    y = Case == 0 ? x.transpose({1, 0, 2}) * 2.0 : w.transpose({2, 0}) * 2.0;
    return y;
}
static_assert(says(misordered<0>(), "heinzel: a transpose order names the element axes, each once") &&
              says(misordered<1>(), "heinzel: that axis is past the tensor's last"));

// Inside a recurrence's own definition the past is rank 0 in the first sweep: the marked target waits.
constexpr Graph marked_in_a_recurrence() {
    Input x{"x", Signal{2, 3, 16}};
    Input g{"g", {6}};
    Output y{"y"};
    y[n] = x[n] + (y[n - 1].reshape({6}) * g).reshape({2, 3});
    return y;
}
static_assert(says(marked_in_a_recurrence(), ""));

constexpr Graph marked_param() {
    Param p{"p"};
    Input x{"x", Signal{16}};
    Output y{"y"};
    y = x * p.reshape({1});
    return y;
}
// The member refuses such a target itself (`members_test`); the analysis's own refusal, through a node built by hand.
constexpr Graph marked_past_the_rank() {
    Input x{"x", Signal{4, 16}};
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Reshape;
    attr.view.elements_only = true;
    attr.view.shape = Shape{1, 1, 1, 1, 1, 4};
    Output y{"y"};
    y = detail::append_unary(x, op::Reshape, attr) * 2.0;
    return y;
}
static_assert(says(marked_param(), "heinzel: a reshape of a rank-0 value; broadcast it instead") &&
              says(marked_past_the_rank(), "heinzel: a reshape target has at most kMaxRank - 1 axes"));
// (A node was 368 bytes until a table read's mark, `TableAttr::phase_in_one_turn`: a table's size leaves no room.)
static_assert(sizeof(ViewAttr) == 120 && sizeof(Node) == 376, "the mark sits in the padding after `kind`");

// ---- iota ---------------------------------------------------------------------------------------------------

constexpr Length kFrames{.multiple_of = 4};
constexpr Graph iota_over_frames() {
    Output y{"y"};
    y = iota(Signal{4, kFrames}) * 2.0;
    return y;
}
static_assert(iota_over_frames().dims.size() == 1 && iota_over_frames().dims[0].constraints.multiple_of == 4,
              "the Length recorded where the ramp is its only carrier");
static_assert(analysed(iota_over_frames()).values[0].shape == Shape{4, Dim{kStreamLength}});
constexpr Graph ramp() {
    Output y{"y"};
    y = iota({4}) * 1.0;
    return y;
}
static_assert(ramp().nodes[0].attr.generator.shape == Shape{4, 1} && ramp().values[0].timeless && ramp().values[0].shape == Shape{4, 1},
              "a tensor's ramp records its stream of one sample, and its shape while building");

} // namespace

// ---- run-time: the refusals a builder makes, and a mixed graph run -----------------------------------------

namespace {

int g_wrong = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

template <class Build>
bool refused(Build build, std::string_view text) {
    try {
        build();
    } catch (const heinzel::error& e) {
        return e.message() == text;
    }
    return false;
}

void builder_refusals() {
    expect(refused([] { return iota(Signal{16}); }, "heinzel: an iota has element axes"), "iota along time alone");
    expect(refused([] { return iota(Shape{}); }, "heinzel: an iota has element axes"), "iota of no axes");
    expect(refused([] { return iota({4, -2}); }, "heinzel: a tensor's extent is a whole number"), "iota of a negative extent");
    expect(refused([] { return iota(Shape{1, 1, 1, 1, 1, 2}); }, "heinzel: a tensor has at most kMaxRank - 1 axes"),
           "iota past the rank");
    const auto six = [] { return Input{"x", Signal{2, 2, 2, 2, 2, 16}}; };
    expect(refused([&] { return einsum("abcdef->a", six()); }, "heinzel: a spec names at most kMaxRank - 1 axes"),
           "einsum: six labels in a term");
    expect(refused([&] { return rearrange("(a b c) (d e f) -> a b c d e f", six()); }, "heinzel: a spec names at most kMaxRank - 1 axes"),
           "rearrange: six labels on a side, in two groups");
    expect(refused([&] { return reduce("a b c d e f -> a", six(), ReduceOp::Sum); }, "heinzel: a spec names at most kMaxRank - 1 axes"),
           "reduce: six labels");
    // A spec names every axis of a value whose rank is known while building: here three element axes, not two.
    const auto three = [] { return Input{"x", Signal{2, 3, 5, 16}}; };
    constexpr std::string_view names = "heinzel: the spec names a different number of axes than the value has";
    expect(refused([&] { return rearrange("a b -> b a", three()); }, names), "rearrange naming two of three axes");
    expect(refused([&] { return reduce("a b -> a", three(), ReduceOp::Sum); }, names), "reduce naming two of three");
    expect(refused([&] { return einsum("ab->ba", three()); }, names), "einsum naming two of three");
    expect(refused([&] { return einsum("ab->a", three()); }, names), "einsum folding two of three");
    expect(refused([&] { return einsum("ab,ab->a", three(), three()); }, names), "einsum of two, naming two of three");
    expect(refused([] { return rearrange("(a b) -> a b", iota({6, 2}), {{"a", 2}}); }, names) &&
               !refused([] { return rearrange("(a b) -> a b", iota({6}), {{"a", 2}}); }, "heinzel"),
           "an iota's rank known while building");
}

// y = w * x + p: a tensor weight per voice and a Param against a signal of 4 voices.
constexpr Graph weighted() {
    Input x{"x", Signal{4, kT}};
    Input w{"w", {4}};
    Param p{"p"};
    Output y{"y"};
    y = w * x + p;
    return y;
}
constexpr Compiled kWeighted = compile(weighted());

bool mixed_run() {
    constexpr std::int64_t kSamples = 37;
    std::vector<float> x(4 * kSamples);
    std::vector<float> w{0.5f, -1.0f, 2.0f, 0.25f};
    for (std::size_t k = 0; k < x.size(); ++k) {
        x[k] = 0.125f * static_cast<float>(k % 11) - 0.5f;
    }
    const float p = 0.75f;
    loops::Feeds feeds{};
    for (std::size_t i = 0; i < kWeighted.graph.inputs.size(); ++i) {
        const std::string_view name = kWeighted.graph.inputs[i].name.view();
        feeds[i] = name == "x" ? static_cast<const void*>(x.data()) : name == "w" ? static_cast<const void*>(w.data()) : &p;
    }
    loops::Sizes sizes{};
    sizes[static_cast<std::size_t>(kStreamLength)] = kSamples;
    canaried::Workspace<kWeighted.program> work(sizes);
    canaried::run<kWeighted.program>(feeds, work);
    const float* y = work.data<float>(static_cast<std::size_t>(kWeighted.program.outputs[0]));
    bool same = true;
    for (std::int64_t t = 0; t < kSamples; ++t) {
        for (std::size_t v = 0; v < 4; ++v) { // sample-major: voice v of sample t at t * 4 + v
            const std::size_t at = static_cast<std::size_t>(t) * 4 + v;
            same = same && y[at] == w[v] * x[at] + p;
        }
    }
    return same;
}

} // namespace

int main() {
    builder_refusals();
    expect(mixed_run(), "a tensor, a Param and a signal, run");
    if (g_wrong != 0) {
        std::printf("tensor_signal_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("tensor_signal_test: tensors and signals kept where they belong\n");
    return 0;
}
