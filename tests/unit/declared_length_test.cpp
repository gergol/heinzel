// Declared lengths (Plan/archive/14-dsl/10, DS-22): a recurrence no signal drives says its length itself —
// `Variable phase{Signal{N}}`, `Output y{"y", Signal{N}}` — and its definition must come out at exactly that shape
// and type. The positive graphs analyse to the declared shape (a `Length` recorded in `dims`), a second analysis
// changes nothing, and they run at `Normal` and `High` bit for bit against plain loops, a `Length` program at several
// lengths: in float at `High`; at `Normal` a one-voice cycle of cheap work keeps its state in double, converted to float
// where it is read (Decision 66), and a bank stays in float. Each wrong declaration is refused with its text, a declared `Output`
// assigned with `=` too; the declarations that are no `Signal` do not compile (`gates/refusals/declared_length.cpp`).
// Undeclared in a graph of no stream, it is refused; in a graph of one length it takes that length
// (Plan/28-dsl-proposals/03 B1): `declared_length_taken_test`.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/runtime/hardware_twins.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "hand_built_graph.hpp"

using namespace heinzel;
using numerics::Precision;

namespace {

constexpr Index n{};
constexpr int kRate = 48'000;
constexpr std::int32_t kN = 997; // not a multiple of any lane count: ragged ends
constexpr std::int32_t kC = 5;   // voices in the bank
constexpr Length kFrames{.multiple_of = 1, .min = 1, .expect = 512};

// ---- the graphs ---------------------------------------------------------------------------------------------

template <bool Symbolic>
constexpr Graph oscillator() { // a phase accumulator a knob drives, and its saw
    Param f{"f"};
    Variable phase{Symbolic ? Signal{kFrames} : Signal{kN}};
    phase[n] = frac(phase[n - 1] + f / kRate);
    Output out{"out"};
    out = 2.0 * phase - 1.0;
    return out;
}
constexpr Graph output_declared() { // the output is the recurrence
    Param f{"f"};
    Output y{"y", Signal{kN}};
    y[n] = frac(y[n - 1] + f / kRate);
    return y;
}
constexpr Graph bank() { // one frequency per voice, a tensor
    Input f{"f", {kC}};
    Variable phase{Signal{kC, kN}};
    phase[n] = frac(phase[n - 1] + f / kRate);
    Output out{"out"};
    out = 2.0 * phase - 1.0;
    return out;
}
constexpr Graph constants_only() {
    Variable phase{Signal{kN}};
    phase[n] = frac(phase[n - 1] + 0.01);
    Output out{"out"};
    out = 2.0 * phase - 1.0;
    return out;
}
// The state-variable pair as a resonator: one declared, the other learns its length from it — either one.
constexpr Expr resonate(Variable& low, Variable& band) {
    Param f{"f"};
    Param g{"g"};
    low[n] = low[n - 1] + f * band[n - 1];
    band[n] = band[n - 1] + f * (g - low[n] - 0.3 * band[n - 1]);
    return low * 1.0;
}
constexpr Graph low_declared() {
    Variable low{Signal{kN}};
    Variable band;
    Output out{"out"};
    out = resonate(low, band);
    return out;
}
constexpr Graph band_declared() {
    Variable low;
    Variable band{Signal{kN}};
    Output out{"out"};
    out = resonate(low, band);
    return out;
}
constexpr Graph widened() { // a declaration wider than the definition's operands: the definition broadcasts to it
    Input x{"x", Signal{kN}};
    Variable v{Signal{4, kN}};
    v[n] = x[n] + 0.5 * v[n - 1];
    Output out{"out"};
    out = v * 1.0;
    return out;
}

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("declared_length_test: a graph did not resolve");
    }
    return g;
}
constexpr Shape shape_of_variable(const Graph& g, std::size_t k = 0) {
    for (const Value& v : g.values) {
        if (v.category == ValueCategory::Variable && k-- == 0) {
            return v.shape;
        }
    }
    return Shape{};
}
constexpr bool idempotent(const Graph& once) {
    Graph twice = once;
    return infer(twice).ok && twice == once;
}

constexpr Graph kOsc = analysed(oscillator<false>());
constexpr Graph kOscFrames = analysed(oscillator<true>());
static_assert(shape_of_variable(kOsc) == Shape{kN} && kOsc.dims.empty() && idempotent(kOsc));
static_assert(shape_of_variable(kOscFrames) == Shape{Dim{kStreamLength}} && kOscFrames.dims.size() == 1 &&
                  kOscFrames.dims[0].constraints.min == 1 && idempotent(kOscFrames),
              "a Length recorded where the variable is its only carrier");
static_assert(idempotent(analysed(output_declared())) && idempotent(analysed(bank())) && idempotent(analysed(constants_only())));
static_assert(shape_of_variable(analysed(bank())) == Shape{kC, kN});
static_assert(shape_of_variable(analysed(low_declared()), 1) == Shape{kN} && shape_of_variable(analysed(band_declared())) == Shape{kN},
              "the undeclared one of a coupled pair learns the declared one's length");
static_assert(shape_of_variable(analysed(widened())) == Shape{4, kN});
static_assert(idempotent(analysed(low_declared())) && idempotent(analysed(band_declared())) && idempotent(analysed(widened())));

// ---- refusals: a wrong declaration, by its text ----------------------------------------------------------------

constexpr bool says(Graph g, std::string_view text) { return infer(g).message.view() == text; }

constexpr Graph wider() { // R1: the definition wider than the declaration
    Input x{"x", Signal{4, kN}};
    Variable v{Signal{kN}};
    v[n] = x[n] + 0.5 * v[n - 1];
    Output out{"out"};
    out = v * 1.0;
    return out;
}
constexpr Graph no_past() { // R2: a knob-only definition that never reads its past
    Param f{"f"};
    Variable v{Signal{kN}};
    v[n] = f * 2.0;
    Output out{"out"};
    out = v * 1.0;
    return out;
}
constexpr Graph wider_type() { // R4: the definition f64, the declaration f32
    Input x{"x", Signal{kN}, f64};
    Variable v{Signal{kN}};
    v[n] = x[n] + 0.5 * v[n - 1];
    Output out{"out"};
    out = v * 1.0;
    return out;
}
constexpr Graph two_lengths() { // R5: a declared 64 against 997 samples in
    Input x{"x", Signal{kN}};
    Variable v{Signal{64}};
    v[n] = x[n] + 0.5 * v[n - 1];
    Output out{"out"};
    out = v * 1.0;
    return out;
}
constexpr Graph folded_past() { // R6: the past read through a fold, the declared voices dropped
    Param f{"f"};
    Variable v{Signal{4, kN}};
    v[n] = frac(v[n - 1].sum(0) + f);
    Output out{"out"};
    out = v * 1.0;
    return out;
}
constexpr Graph never_defined_by_hand() { // R3, as the analysis sees it: a declared variable no recurrence names
    Graph g{};
    const auto f = add_value(g, ElemType::F32, Shape{}, ValueCategory::Input);
    const auto v = add_value(g, ElemType::F32, Shape{kN}, ValueCategory::Variable);
    const auto y = add_node(g, op::Mul, v, f);
    g.inputs.push_back(NamedValue{"f", f});
    g.outputs.push_back(NamedValue{"y", y});
    return g;
}
constexpr Graph one_sample() { // a declared length of 1 and no signal in the definition (Plan/archive/14-dsl/10 §4)
    Input w{"w", {4}};
    Variable v{Signal{4, 1}};
    v[n] = w * 0.5;
    Output out{"out"};
    out = v * 1.0;
    return out;
}
static_assert(says(wider(), "heinzel: a Variable is defined as [4, 997] f32 but declared [997] f32"));
static_assert(says(no_past(), "heinzel: a Variable is defined as [] f32 but declared [997] f32"));
static_assert(says(wider_type(), "heinzel: a Variable is defined as [997] f64 but declared [997] f32"));
static_assert(says(two_lengths(), "shapes disagree at +: [997] and [64]")); // as before declarations
static_assert(says(folded_past(), "heinzel: a Variable is defined as [997] f32 but declared [4, 997] f32"));
static_assert(says(never_defined_by_hand(), "heinzel: a declared Variable is never defined"));
static_assert(says(one_sample(), "heinzel: a recurrence needs a Signal in it, or a declared length"));
// Undeclared in a graph of no stream, the knob-only oscillator has no length to take: the text names the fix.
constexpr Graph undeclared() {
    Param f{"f"};
    Variable phase;
    phase[n] = frac(phase[n - 1] + f / kRate);
    Output out{"out"};
    out = 2.0 * phase - 1.0;
    return out;
}
static_assert(says(undeclared(), "heinzel: a recurrence needs a Signal in it, or a declared length"));

// ---- runs, against plain loops in float ------------------------------------------------------------------------

constexpr float kF = 440.0f;
constexpr float kG = 0.25f;
float frac_of(float x) { return x - std::floor(x); }
float bank_f(std::size_t c) { return 110.0f * static_cast<float>(c + 1) + 0.5f; }

// The phase, in float (`High`) or in double (`Normal`); its step `f / 48000` in float, which reads no cycle.
std::vector<float> phase(std::int32_t length, float f, bool normal) {
    std::vector<float> out(static_cast<std::size_t>(length));
    float p = 0.0f;
    double wide = 0.0;
    for (float& o : out) {
        p = frac_of(p + f / 48000.0f);
        wide = wide + static_cast<double>(f / 48000.0f);
        wide = wide - std::floor(wide);
        o = normal ? static_cast<float>(wide) : p;
    }
    return out;
}
std::vector<float> saw(std::int32_t length, float f, bool normal) {
    std::vector<float> out = phase(length, f, normal);
    for (float& o : out) {
        o = 2.0f * o - 1.0f;
    }
    return out;
}
std::vector<float> saws(std::int32_t length) { // the bank, sample-major
    std::vector<float> out(static_cast<std::size_t>(length) * kC);
    std::vector<float> p(kC, 0.0f);
    for (std::size_t t = 0; t < static_cast<std::size_t>(length); ++t) {
        for (std::size_t c = 0; c < kC; ++c) {
            p[c] = frac_of(p[c] + bank_f(c) / 48000.0f);
            out[t * kC + c] = 2.0f * p[c] - 1.0f;
        }
    }
    return out;
}
// The declaration wider than its operands: each of its four rows the one-pole of x, sample-major. One rounded add a
// step, so float is the reference at both levels.
std::vector<float> rising(std::int32_t length) {
    std::vector<float> x(static_cast<std::size_t>(length));
    for (std::size_t t = 0; t < x.size(); ++t) {
        x[t] = static_cast<float>(t % 7) * 0.25f - 0.75f;
    }
    return x;
}
std::vector<float> widened_rows(const std::vector<float>& x) {
    std::vector<float> out(x.size() * 4);
    float v = 0.0f;
    for (std::size_t t = 0; t < x.size(); ++t) {
        v = x[t] + 0.5f * v;
        for (std::size_t c = 0; c < 4; ++c) {
            out[t * 4 + c] = v;
        }
    }
    return out;
}
// A literal keeps its double value at `High` (rounded once, with the step) and at `Normal` (the cycle in double):
// 0.01 and 0.3 are not the floats 0.01f and 0.3f.
std::vector<float> fixed_saw(std::int32_t length, bool high) {
    std::vector<float> out(static_cast<std::size_t>(length));
    float p = 0.0f;
    double wide = 0.0;
    for (float& o : out) {
        p = frac_of(static_cast<float>(static_cast<double>(p) + 0.01));
        wide = wide + 0.01;
        wide = wide - std::floor(wide);
        o = 2.0f * (high ? p : static_cast<float>(wide)) - 1.0f;
    }
    return out;
}
std::vector<float> resonator(std::int32_t length, bool high) {
    std::vector<float> out(static_cast<std::size_t>(length));
    if (high) {
        float low = 0.0f;
        float band = 0.0f;
        const float f = 0.125f;
        for (float& o : out) {
            const float l = low + f * band;
            band = band + f * ((kG - l) - static_cast<float>(0.3 * static_cast<double>(band)));
            low = l;
            o = low;
        }
        return out;
    }
    // At Normal in double, each sum in the rule's order, its last product fused as the program fuses it (Decision 66):
    // `low` after the damping.
    double low = 0.0;
    double band = 0.0;
    const double f = 0.125;
    const double g = kG;
    for (float& o : out) {
        const double l = multiply_add_as_the_machine_rounds(f, band, low);
        band = multiply_add_as_the_machine_rounds(f, (g - 0.3 * band) - l, band);
        low = l;
        o = static_cast<float>(low);
    }
    return out;
}

template <Graph (*Build)(), Precision L>
constexpr Graph at_level() {
    Graph g = Build();
    set_default_precision(g, L);
    return g;
}
template <Graph (*Build)(), Precision L>
inline constexpr Compiled kCompiled = compile(at_level<Build, L>(), zen2);

int g_wrong = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

// The first output against `want`, bit for bit.
template <Graph (*Build)(), Precision L>
void runs(const char* what, std::int32_t length, const std::vector<float>& want, const float* f) {
    constexpr const Compiled& c = kCompiled<Build, L>;
    loops::Feeds feeds{};
    for (std::size_t i = 0; i < c.graph.inputs.size(); ++i) {
        feeds[i] = c.graph.inputs[i].name.view() == "g" ? static_cast<const void*>(&kG) : static_cast<const void*>(f);
    }
    loops::Sizes sizes{};
    sizes[0] = length;
    canaried::Workspace<c.program> work(sizes);
    canaried::run<c.program>(feeds, work);
    const float* got = work.template data<float>(static_cast<std::size_t>(c.program.outputs[0]));
    expect(std::memcmp(got, want.data(), want.size() * sizeof(float)) == 0, what);
}
template <Graph (*Build)()>
void both_levels(const char* what, std::int32_t length, const std::vector<float>& normal, const std::vector<float>& high, const float* f) {
    runs<Build, Precision::Normal>(what, length, normal, f);
    runs<Build, Precision::High>(what, length, high, f);
}

template <class Build>
bool refused(Build build, std::string_view text) {
    try {
        (void)build();
    } catch (const error& e) {
        return e.message() == text;
    }
    return false;
}

void run_all() {
    const float f = kF;
    const float resonance = 0.125f;
    both_levels<oscillator<false>>("the oscillator, Signal{997}", kN, saw(kN, kF, true), saw(kN, kF, false), &f);
    for (const std::int32_t length : {1, 7, 64, 997, 4096}) {
        both_levels<oscillator<true>>("the oscillator, a Length", length, saw(length, kF, true), saw(length, kF, false), &f);
    }
    both_levels<output_declared>("Output y{\"y\", Signal{997}}", kN, phase(kN, kF, true), phase(kN, kF, false), &f);
    std::vector<float> fs(kC);
    for (std::size_t c = 0; c < kC; ++c) {
        fs[c] = bank_f(c);
    }
    both_levels<bank>("a bank of five", kN, saws(kN), saws(kN), fs.data());
    runs<constants_only, Precision::Normal>("constants only", kN, fixed_saw(kN, false), &f);
    runs<constants_only, Precision::High>("constants only, High", kN, fixed_saw(kN, true), &f);
    runs<low_declared, Precision::Normal>("a coupled pair, low declared", kN, resonator(kN, false), &resonance);
    runs<low_declared, Precision::High>("a coupled pair, low declared, High", kN, resonator(kN, true), &resonance);
    runs<band_declared, Precision::Normal>("a coupled pair, band declared", kN, resonator(kN, false), &resonance);
    runs<band_declared, Precision::High>("a coupled pair, band declared, High", kN, resonator(kN, true), &resonance);
    const std::vector<float> x = rising(kN);
    both_levels<widened>("a declaration wider than its operands", kN, widened_rows(x), widened_rows(x), x.data());
}

void frontend_refusals() {
    constexpr std::string_view assigned = "heinzel: a declared Output is defined by y[n] = ...; assign an undeclared one";
    expect(refused([] {
        Param f{"f"};
        Output y{"y", Signal{kN}};
        y = f * 2.0;
        return Graph(y);
    }, assigned), "a declared Output assigned with =");
    expect(refused([] {
        Param f{"f"};
        Output y{"y"};
        Output z{"z", Signal{kN}};
        y = f * 2.0;
        z = y;
        return finish(y, z);
    }, assigned), "an Output given to a declared one with =");
    expect(refused([] { // R3: declared and read, never defined — refused where the graph is finished
        Param f{"f"};
        Variable v{Signal{kN}};
        Output out{"out"};
        out = v[n - 1] * f;
        return Graph(out);
    }, "heinzel: a Variable or an Output is read but never defined"), "a declared Variable never defined");
    expect(refused([] {
        Variable v{Signal{kN}, ElemType::Unknown};
        return 0;
    }, "heinzel: a declared length needs a type") && refused([] {
        Output y{"y", Signal{kN}, ElemType::Unknown};
        return 0;
    }, "heinzel: a declared length needs a type"), "a declaration of no type");
}

} // namespace

int main() {
    run_all();
    frontend_refusals();
    if (g_wrong != 0) {
        std::printf("declared_length_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("declared_length_test: every declared length holds, run against plain loops\n");
    return 0;
}
