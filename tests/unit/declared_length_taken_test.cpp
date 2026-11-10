// A recurrence no signal drives takes the graph's one length (Plan/28-dsl-proposals/03 B1, Decision 76), split from
// declared_length (gate structure_limits): undeclared, each graph analyses to the one with its variables declared,
// twice to the same, and runs on `loops::run` and `Executor` at `High` bit for bit as the walk does; two lengths and a
// definition that misses the length taken are refused. A variable that took the length is typed by its definition, and
// only one that nothing types is f32: a definition typed in a later sweep, through a variable defined after it, or by
// a `where`'s condition, types it as the declaration does (the review of package B).

#include <cstdio>
#include <span>
#include <string_view>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr Index n{};
constexpr int kRate = 48'000;
constexpr std::int32_t kN = 997; // not a multiple of any lane count: ragged ends
constexpr std::int32_t kC = 5;   // voices in the bank
constexpr Length kFrames{.multiple_of = 1, .min = 1, .expect = 512};

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("declared_length_taken_test: a graph did not resolve");
    }
    return g;
}
constexpr Shape shape_of_variable(const Graph& g) {
    for (const Value& v : g.values) {
        if (v.category == ValueCategory::Variable) {
            return v.shape;
        }
    }
    return Shape{};
}
constexpr bool idempotent(const Graph& once) {
    Graph twice = once;
    return infer(twice).ok && twice == once;
}
constexpr bool says(Graph g, std::string_view text) { return infer(g).message.view() == text; }

// ---- the graph's one length, taken (Plan/28-dsl-proposals/03 B1) -----------------------------------------------
//
// Each graph twice: its undriven variables undeclared, and declared at the length they take.

template <bool Declare, bool Symbolic>
constexpr Graph beside_an_input() { // the knob's phase beside an input, at 997 samples or over a Length
    const Signal length = Symbolic ? Signal{kFrames} : Signal{kN};
    Input x{"x", length};
    Param f{"f"};
    Variable phase = Declare ? Variable{length} : Variable{};
    phase[n] = frac(phase[n - 1] + f / kRate);
    Output out{"out"};
    out = x * (2.0 * phase - 1.0);
    return out;
}
template <bool Declare>
constexpr Graph bank_beside_an_input() { // a tensor drives it: [5] becomes [5, 997]
    Input f{"f", {kC}};
    Input x{"x", Signal{kN}};
    Variable phase = Declare ? Variable{Signal{kC, kN}} : Variable{};
    phase[n] = frac(phase[n - 1] + f / kRate);
    Output out{"out"};
    out = x * phase;
    return out;
}
template <bool Declare>
constexpr Graph beside_a_clock() { // 07_oscillators: the clock declares the length, the phase takes it
    Param f{"f"};
    Variable t{Signal{kN}};
    t[n] = t[n - 1] + 1.0 / kRate;
    Variable phase = Declare ? Variable{Signal{kN}} : Variable{};
    phase[n] = frac(phase[n - 1] + f / kRate);
    Output out{"out"};
    out = t * phase;
    return out;
}
template <bool Declare>
constexpr Graph beside_a_ramp() { // the only seed a generator: iota(Signal{4, frames})
    Param f{"f"};
    Expr ramp = iota(Signal{4, kFrames});
    Variable phase = Declare ? Variable{Signal{kFrames}} : Variable{};
    phase[n] = frac(phase[n - 1] + f / kRate);
    Output out{"out"};
    out = ramp * phase;
    return out;
}
template <bool Declare>
constexpr Graph coupled_beside_an_input() { // both of a pair take it, numbers alone typing neither: f32
    Input x{"x", Signal{2, kFrames}}; // one length with y's: every Length is the one symbol
    Input y{"y", Signal{kFrames}};
    Variable low = Declare ? Variable{Signal{kFrames}} : Variable{};
    Variable band = Declare ? Variable{Signal{kFrames}} : Variable{};
    low[n] = low[n - 1] + 0.125 * band[n - 1];
    band[n] = band[n - 1] + 0.125 * (0.25 - low[n] - 0.3 * band[n - 1]);
    Output out{"out"};
    out = x * low + y;
    return out;
}
template <bool Declare>
constexpr Graph f64_beside_an_input() { // the definition's type
    Input x{"x", Signal{kN}};
    Param f{"f", f64};
    Variable phase = Declare ? Variable{Signal{kN}, f64} : Variable{};
    phase[n] = frac(phase[n - 1] + f / kRate);
    Output out{"out"};
    out = x * phase;
    return out;
}

template <Graph (*Undeclared)(), Graph (*Declared)()>
constexpr bool takes_the_length() {
    const Graph taken = analysed(Undeclared());
    return taken == analysed(Declared()) && idempotent(taken);
}
static_assert(takes_the_length<beside_an_input<false, false>, beside_an_input<true, false>>());
static_assert(takes_the_length<beside_an_input<false, true>, beside_an_input<true, true>>());
static_assert(takes_the_length<bank_beside_an_input<false>, bank_beside_an_input<true>>());
static_assert(takes_the_length<beside_a_clock<false>, beside_a_clock<true>>());
static_assert(takes_the_length<beside_a_ramp<false>, beside_a_ramp<true>>());
static_assert(takes_the_length<coupled_beside_an_input<false>, coupled_beside_an_input<true>>());
static_assert(takes_the_length<f64_beside_an_input<false>, f64_beside_an_input<true>>());
static_assert(shape_of_variable(analysed(bank_beside_an_input<false>())) == Shape{kC, kN});

// Typed late: `a`'s definition by `b`'s past, `b` defined after it and driven by an f64 knob. Its literals adapt to
// that type, as declared, not to an f32 given `a` at its first visit.
template <bool Declare>
constexpr Graph typed_through_a_later_variable() {
    Input x{"x", Signal{kN}};
    Param f{"f", f64};
    Variable a = Declare ? Variable{Signal{kN}, f64} : Variable{};
    Variable b = Declare ? Variable{Signal{kN}, f64} : Variable{};
    a[n] = a[n - 1] * 0.999 + 0.5 * b[n - 1];
    b[n] = frac(b[n - 1] + f / kRate);
    Output out{"out"};
    out = x * a;
    return out;
}
// Typed by a `where`'s condition, an f64 knob compared, through the numbers' step (B2).
template <bool Declare>
constexpr Graph typed_by_a_condition() {
    Input x{"x", Signal{kN}};
    Param f{"f", f64};
    Variable w = Declare ? Variable{Signal{kN}, f64} : Variable{};
    w[n] = where(f > 0.5, 1.0, 0.0) + 0.5 * w[n - 1];
    Output out{"out"};
    out = x * w;
    return out;
}
static_assert(takes_the_length<typed_through_a_later_variable<false>, typed_through_a_later_variable<true>>());
static_assert(takes_the_length<typed_by_a_condition<false>, typed_by_a_condition<true>>());

constexpr std::string_view kTwoLengths = "heinzel: a recurrence no signal drives takes the graph's length, but it has two; declare it";
constexpr Graph two_inputs_two_lengths() {
    Input x{"x", Signal{kN}};
    Input z{"z", Signal{64}};
    Param f{"f"};
    Variable phase;
    phase[n] = frac(phase[n - 1] + f / kRate);
    Output a{"a"};
    Output b{"b"};
    a = x * 2.0;
    b = z * phase;
    return finish(a, b);
}
constexpr Graph a_clock_and_an_input() { // a declared variable is a seed as an input is
    Input x{"x", Signal{kN}};
    Param f{"f"};
    Variable t{Signal{64}};
    t[n] = t[n - 1] + 1.0 / kRate;
    Variable phase;
    phase[n] = frac(phase[n - 1] + f / kRate);
    Output a{"a"};
    Output b{"b"};
    a = x * phase;
    b = t * 2.0;
    return finish(a, b);
}
constexpr Graph no_past_beside_an_input() { // takes the length, but its definition is one number
    Input x{"x", Signal{kN}};
    Param f{"f"};
    Variable v;
    v[n] = f * 2.0;
    Output out{"out"};
    out = x * v;
    return out;
}
constexpr Graph folded_past_beside_an_input() { // takes the length, then folds it away
    Input x{"x", Signal{kN}};
    Param f{"f"};
    Variable v;
    v[n] = frac(v[n - 1].sum(0) + f);
    Output out{"out"};
    out = x * v;
    return out;
}
static_assert(says(two_inputs_two_lengths(), kTwoLengths) && says(a_clock_and_an_input(), kTwoLengths));
static_assert(says(no_past_beside_an_input(), "heinzel: a recurrence needs a Signal in it, or a declared length") &&
              says(folded_past_beside_an_input(), "heinzel: a recurrence needs a Signal in it, or a declared length"));

// ---- runs, against the walk -----------------------------------------------------------------------------------

constexpr float kF = 440.0f;

std::vector<float> rising(std::int32_t length) {
    std::vector<float> x(static_cast<std::size_t>(length));
    for (std::size_t t = 0; t < x.size(); ++t) {
        x[t] = static_cast<float>(t % 7) * 0.25f - 0.75f;
    }
    return x;
}

template <Graph (*Build)()>
inline constexpr Compiled kCompiled = compile(testing::at_high<Build>(), zen2);

// A length taken, on both runners at `High` against the walk, bit for bit: `loops::run` over `compile`'s program and
// `Executor`, the input `rising` and the knob at `kF`.
template <Graph (*Build)(), std::int32_t Length, std::size_t Frames>
void walks(const char* what) {
    const std::vector<float> x = rising(Length);
    const std::vector<float> knob(x.size(), kF); // the walk reads a Param at every sample
    const testing::Outputs walk = testing::walked<Build, Frames>({{"x", x.data()}, {"f", knob.data()}});
    constexpr const Compiled& c = kCompiled<Build>;
    loops::Feeds feeds{};
    for (std::size_t i = 0; i < c.graph.inputs.size(); ++i) {
        const bool is_knob = c.graph.inputs[i].name.view() == "f";
        feeds[i] = is_knob ? static_cast<const void*>(&kF) : static_cast<const void*>(x.data());
    }
    loops::Sizes sizes{};
    sizes[0] = Length;
    canaried::Workspace<c.program> work(sizes);
    canaried::run<c.program>(feeds, work);
    const float* got = work.template data<float>(static_cast<std::size_t>(c.program.outputs[0]));
    testing::require_walked(what, std::span<const float>(got, walk[0].size()), walk[0]);
    std::vector<float> out(walk[0].size());
    Executor<testing::at_high<Build>> ex;
    testing::held(ex);
    ex.input("x") = x.data();
    ex.param("f") = kF;
    ex.output("out") = out.data();
    if constexpr (Frames > 0) {
        ex.prepare(Length);
        ex.run(Length);
    } else {
        ex.run();
    }
    testing::require_walked(what, out, walk[0]);
}

} // namespace

int main() {
    walks<beside_an_input<false, false>, kN, 0>("the knob's phase beside an input, its length taken");
    walks<beside_an_input<false, true>, 64, 64>("the knob's phase beside an input, a Length taken");
    std::printf("declared_length_taken_test: each length taken as declared, run as the walk runs it\n");
    return 0;
}
