// A delay that changes (Plan/28-dsl-proposals/05 D4): `x[n - d.at_most(2.0) * ms]` builds a delay line of x, its
// position `d` times the samples a millisecond holds at the rate `n` carries, its longest distance the bound in samples
// rounded up (graph for graph: in ms, us and second; of an Input, a computed value, a Variable and an Output; `at_most`
// on a Variable and an Output; bounds a whole number of samples but for their rounding, and bounds between two); the
// copy compiling makes of an input, one for two lines, a fixed point of `infer` and of itself. On Executor at High,
// Normal and Fast, over f32 and f64, on the default machine and zen2, a delay line over an input (which compiling
// copies into storage that keeps its past) and over a computed value, its distance sweeping below 0, past the bound,
// whole samples and between them, at a NaN and the infinities: at High and over f64 every number equal to the walk's
// bit for bit; an f32 line at Normal and Fast blends in float, so within Decision 78's bound of the walk
// (`float_reads.hpp`: its tap the walk's, the blend 3.5 float steps of the larger tap), at an f64 distance in double
// and the walk's to the bit; over three voices at High too; a distance that is a Param against the float blend written
// out. A chorus, its distance an oscillator's, as the walk reads it at High; at Normal on both machines in blocks
// shorter and longer than its line, and a 120 ms line in blocks of 64, the numbers of one run, and again after a reset.
// Refused by name: a delay line in feedback, through its signal (a flanger) or its distance, and one of a value defined
// later with no loop through it, a delay of a constant, a delay over an input planned as written (no copy). The
// unbounded spelling, a bound with no unit, a negative bound and an unset rate are the build_refuses_delay_* fixtures;
// the copy past a capacity is two build_refuses_capacity_* ones.
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/rewrite/copied_inputs.hpp"
#include "heinzel/sema/analyze.hpp"

#include "executed.hpp"
#include "float_reads.hpp"
#include "interpret.hpp"
#include "planned.hpp"

// Spelled with every name qualified, as a program without `using namespace heinzel` spells it: each step's operator is
// found beside its operands' types.
namespace qualified {
constexpr heinzel::Graph line() {
    constexpr heinzel::Index at{48'000};
    heinzel::Input x{"x", heinzel::Signal{8}};
    heinzel::Input d{"d", heinzel::Signal{8}};
    heinzel::Output y{"y"};
    y = x[at - d.at_most(1.0) * heinzel::ms];
    return y;
}
static_assert(line().nodes.size() == 2, "a product for the distance in samples and the delay line");
} // namespace qualified

using namespace heinzel;

namespace {

constexpr Index n{48'000};
constexpr Index cd{44'100};
constexpr std::size_t kPoints = 1024;
constexpr double kMost = 2.0; // ms: 96 samples at 48 kHz

// ---- the spelling builds its graph -------------------------------------------------------------------------------

constexpr Expr line_by_hand(Expr signal, Expr samples, std::int32_t most) {
    Arena* arena = bring_together(signal, samples);
    BuildNode line{};
    line.op = op::DelayLine;
    line.inputs[0] = signal.position();
    line.inputs[1] = samples.position();
    line.attr.kind = AttrKind::SignalledDelay;
    line.attr.signalled_delay.max_delay = most;
    arena->nodes.push_back(line);
    return Expr{arena, static_cast<std::int32_t>(arena->nodes.size() - 1)};
}

template <bool Spelled>
constexpr Graph spelled_or_built() {
    Input x{"x", Signal{8}};
    Input d{"d", Signal{8}};
    Variable v;
    const Expr itself = v; // (a line of a Variable reads the variable, as its lags do, not its definition)
    v[n] = 0.5 * v[n - 1] + x;
    Expr half = 0.5 * x;
    Output o{"o"};
    o = 2.0 * x;
    Output a{"a"};
    Output b{"b"};
    Output c{"c"};
    Output e{"e"};
    Output f{"f"};
    if constexpr (Spelled) {
        a = x[n - d.at_most(25.0) * ms];
        b = half[n - d.at_most(500.0) * us];
        c = v[n - d.at_most(0.025) * second];
        e = o[n - v.at_most(2.0) * ms];
        f = x[n - o.at_most(1.0) * ms];
    } else {
        a = line_by_hand(x, d * (1e-3 * 48'000.0), 1200);
        b = line_by_hand(half, d * (1e-6 * 48'000.0), 24);
        c = line_by_hand(itself, d * 48'000.0, 1200);
        e = line_by_hand(Expr(o), Expr(v) * (1e-3 * 48'000.0), 96);
        f = line_by_hand(x, Expr(o) * (1e-3 * 48'000.0), 48);
    }
    return finish(o, a, b, c, e, f);
}
static_assert(spelled_or_built<true>() == spelled_or_built<false>(), "x[n - d.at_most(b) * unit] builds a delay line of x");

// The longest distance a spelled line keeps: its bound in samples, rounded up.
template <std::int32_t Rate>
constexpr std::int32_t longest(double bound) {
    constexpr Index at{static_cast<double>(Rate)};
    Input x{"x", Signal{8}};
    Input d{"d", Signal{8}};
    const Graph g = finish(Output{"y"} = x[at - d.at_most(bound) * ms]);
    std::int32_t most = -1;
    for (const Node& node : g.nodes) {
        most = node.attr.kind == AttrKind::SignalledDelay ? node.attr.signalled_delay.max_delay : most;
    }
    return most;
}
static_assert(longest<48'000>(120.0) == 5760 && longest<48'000>(25.0) == 1200 && longest<48'000>(0.0) == 0,
              "a bound a whole number of samples but for the rounding of its product is that number");
static_assert(longest<44'100>(2.5) == 111 && longest<44'100>(1.0) == 45 && longest<44'100>(0.1) == 5 && longest<48'000>(0.01) == 1,
              "a bound between two samples is rounded up");

// The copy compiling makes of an input a line reads is what `infer` makes of it: analysed again, the graph is the same;
// copied again, nothing more is copied.
constexpr bool copy_is_a_fixed_point() {
    Input x{"x", Signal{8}};
    Input d{"d", Signal{8}};
    Expr near = x[n - d.at_most(1.0) * ms];
    Expr far = x[n - d.at_most(2.0) * ms];
    Graph g = finish(Output{"y"} = near + far);
    if (!infer(g).ok) {
        return false;
    }
    const std::size_t nodes = g.nodes.size();
    rewrite::copy_inputs_of_delay_lines(g);
    Graph again = g;
    const bool analysed = infer(again).ok && again == g;
    rewrite::copy_inputs_of_delay_lines(again);
    return analysed && again == g && g.nodes.size() == nodes + 1;
}
static_assert(copy_is_a_fixed_point(), "one copy of the input for its two lines, as infer types it; a second pass copies nothing");

// ---- on Executor, as the walk reads ------------------------------------------------------------------------------

enum class Over { AnInput, AComputedValue };

template <Over O, ElemType T, Precision Level, ElemType D = T>
constexpr Graph one_at() {
    Input x{"x", Signal{kPoints}, T};
    Input d{"d", Signal{kPoints}, D};
    Output y{"y"};
    if constexpr (O == Over::AnInput) {
        y = x[n - d.at_most(kMost) * ms];
    } else {
        Expr half = 0.5 * x;
        y = half[n - d.at_most(kMost) * ms];
    }
    return finish(Level, y);
}

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// The distances, in ms: the edges first (a NaN, the infinities, 0, the bound, past it, below 0, a sample, half of one,
// whole samples: 0.0625 ms is 3), then a sweep from below 0 to past the bound, most of it between two samples.
std::vector<double> distances() {
    std::vector<double> d{kNaN, kInf, -kInf, 0.0, -0.0, kMost, kMost + 1.0, -0.25, 1.0 / 48.0, 0.5 / 48.0, 1e30, -1e30,
                          0.0625, 0.125, 0.25, 0.5, 1.0, 1.5};
    while (d.size() < kPoints) {
        d.push_back(-0.5 + (kMost + 1.0) * static_cast<double>((d.size() * 37) % kPoints) / kPoints);
    }
    return d;
}
std::vector<double> signal() {
    std::vector<double> x(kPoints);
    for (std::size_t i = 0; i < kPoints; ++i) {
        x[i] = static_cast<double>(static_cast<float>(std::sin(0.37 * static_cast<double>(i)) + 0.001 * static_cast<double>(i)));
    }
    return x;
}

// What the walk reads: each number, and the line's signal and position.
struct Walked {
    std::vector<double> out = std::vector<double>(kPoints);
    std::vector<double> signal = std::vector<double>(kPoints);
    std::vector<double> position = std::vector<double>(kPoints);
};

// The walk of the graph `Build` makes, analysed at its own level, fed `x` and `d`.
template <auto Build>
Walked walked(const std::vector<double>& x, const std::vector<double>& d) {
    Graph g = Build();
    if (!infer(g).ok) {
        fail("delay_lines_test: a graph did not analyse");
    }
    Inputs feeds{};
    for (const NamedValue& in : g.inputs) {
        feeds.push_back(InputFeed{in.name.view() == "x" ? x.data() : d.data(), 1});
    }
    const auto signals = std::make_unique<Signals<kPoints, 16>>();
    run(g, feeds, *signals);
    const Node* line = nullptr;
    for (const Node& node : g.nodes) {
        line = node.op == op::DelayLine ? &node : line;
    }
    Walked w;
    for (std::size_t s = 0; s < kPoints; ++s) {
        w.out[s] = sample_at(*signals, g.outputs[0].value, 0, s);
        w.signal[s] = sample_at(*signals, line->inputs[0], 0, s);
        w.position[s] = sample_at(*signals, line->inputs[1], 0, s);
    }
    return w;
}

// The float blend's bound at sample `i`: around the walk's two taps (silence before the first sample).
double bound_at(const Walked& w, std::size_t i, std::int32_t most) {
    const SignalledTap tap = signalled_tap(most, w.position[i]);
    const auto at = [&](std::int64_t back) {
        const std::int64_t k = static_cast<std::int64_t>(i) - back;
        return k < 0 ? 0.0 : w.signal[static_cast<std::size_t>(k)];
    };
    return testing::blend_bound(at(tap.whole), at(tap.whole + 1));
}

template <class Number>
bool same(Number a, Number b) {
    using Bits = std::conditional_t<sizeof(Number) == 4, std::uint32_t, std::uint64_t>;
    return std::bit_cast<Bits>(a) == std::bit_cast<Bits>(b);
}

constexpr const char* level_name(Precision l) { return l == High ? "High" : l == Normal ? "Normal" : "Fast"; }

// Bit for bit, but for an f32 line of an f32 distance at Normal and Fast, which is held to the float blend's bound (an
// f64 distance makes it compute in double, as a map would).
template <Over O, ElemType T, Precision Level, const Machine& M, ElemType D = T>
bool agrees() {
    using Number = std::conditional_t<T == ElemType::F32, float, double>;
    using Distance = std::conditional_t<D == ElemType::F32, float, double>;
    constexpr bool in_float = T == ElemType::F32 && D == ElemType::F32 && Level != High;
    const std::vector<double> wide_x = signal();
    const std::vector<double> wide_d = distances();
    const std::vector<Number> x(wide_x.begin(), wide_x.end());
    const std::vector<Distance> d(wide_d.begin(), wide_d.end());
    std::vector<Number> y(kPoints);
    Executor<one_at<O, T, Level, D>, M> ex;
    testing::held(ex);
    ex.input("x") = x.data();
    ex.input("d") = d.data();
    ex.output("y") = y.data();
    ex.run();
    const Walked walk = walked<one_at<O, T, Level, D>>(std::vector<double>(x.begin(), x.end()), std::vector<double>(d.begin(), d.end()));
    constexpr std::int32_t most = 96; // kMost at 48 kHz
    std::size_t differ = 0;
    std::size_t past = 0;
    double worst = 0.0; // the largest difference, as a share of its bound
    for (std::size_t i = 0; i < kPoints; ++i) {
        const auto want = static_cast<Number>(walk.out[i]);
        const double bound = bound_at(walk, i, most);
        differ += same(y[i], want) ? 0 : 1;
        past += !in_float || testing::within(y[i], want, bound) ? 0 : 1;
        const double apart = std::fabs(static_cast<double>(y[i]) - static_cast<double>(want));
        worst = same(y[i], want) ? worst : std::max(worst, apart / bound);
    }
    std::printf("delay_lines: over %s, %s at %s on %s: %zu of %zu numbers differ from the walk (at most %.2f of the float "
                "bound), %zu past it\n",
                O == Over::AnInput ? "an input" : "a computed value", T == D ? (T == ElemType::F32 ? "f32" : "f64") : "f32 at an f64 distance",
                level_name(Level),
                &M == &zen2 ? "zen2" : "the default machine", differ, kPoints, worst, past);
    return in_float ? past == 0 : differ == 0;
}

template <Over O>
bool agrees_everywhere() {
    bool ok = agrees<O, ElemType::F32, High, default_machine>() && agrees<O, ElemType::F32, High, zen2>();
    ok = agrees<O, ElemType::F32, Normal, default_machine>() && agrees<O, ElemType::F32, Normal, zen2>() && ok;
    ok = agrees<O, ElemType::F32, Fast, default_machine>() && agrees<O, ElemType::F32, Fast, zen2>() && ok;
    ok = agrees<O, ElemType::F64, High, default_machine>() && agrees<O, ElemType::F64, High, zen2>() && ok;
    ok = agrees<O, ElemType::F64, Normal, default_machine>() && agrees<O, ElemType::F64, Normal, zen2>() && ok;
    ok = agrees<O, ElemType::F32, Normal, default_machine, ElemType::F64>() && agrees<O, ElemType::F32, Fast, zen2, ElemType::F64>() && ok;
    return agrees<O, ElemType::F64, Fast, default_machine>() && agrees<O, ElemType::F64, Fast, zen2>() && ok;
}

// Three voices, each its own distance, in lanes across them: at High as the walk reads it (`executed` exits on the
// first difference).
constexpr std::size_t kVoices = 3;
constexpr Graph voices() {
    Input x{"x", Signal{kVoices, kPoints}};
    Input d{"d", Signal{kVoices, kPoints}};
    Output y{"y"};
    y = x[n - d.at_most(kMost) * ms];
    return y;
}
void voices_agree() {
    std::vector<double> x;
    std::vector<double> d;
    const std::vector<double> one_x = signal();
    const std::vector<double> one_d = distances();
    for (std::size_t s = 0; s < kPoints; ++s) {
        for (std::size_t v = 0; v < kVoices; ++v) {
            x.push_back(one_x[(s + 100 * v) % kPoints]);
            d.push_back(one_d[(s + 7 * v) % kPoints]);
        }
    }
    testing::executed<voices>({{"x", x.data(), kVoices}, {"d", d.data(), kVoices}});
    testing::executed<voices, 0, zen2>({{"x", x.data(), kVoices}, {"d", d.data(), kVoices}});
}

// A distance set once a run: x read 1.01 ms ago, 48.48 samples, its blend written out as Normal blends a float: its
// fraction toward the far tap, by the machine's multiply-add.
constexpr Graph knob() {
    Input x{"x", Signal{kPoints}};
    Param d{"d"};
    Output y{"y"};
    y = x[n - d.at_most(kMost) * ms];
    return y;
}
bool knob_reads() {
    const std::vector<double> wide = signal();
    const std::vector<float> x(wide.begin(), wide.end());
    std::vector<float> y(kPoints);
    Executor<knob> ex;
    ex.input("x") = x.data();
    ex.param("d") = 1.01;
    ex.output("y") = y.data();
    ex.run();
    const float position = static_cast<float>(1.01) * 48.0f;
    const auto whole = static_cast<std::size_t>(position);
    const float f = position - static_cast<float>(whole);
    std::size_t differ = 0;
    for (std::size_t i = 0; i < kPoints; ++i) {
        const float near = i >= whole ? x[i - whole] : 0.0f;
        const float far = i >= whole + 1 ? x[i - whole - 1] : 0.0f;
        differ += y[i] == multiply_add_as_the_machine_rounds(f, far - near, near) ? 0 : 1;
    }
    std::printf("delay_lines: a distance that is a Param, 1.01 ms: %zu of %zu numbers differ from the blend\n", differ, kPoints);
    return differ == 0;
}

// ---- the float tap stays in its line -----------------------------------------------------------------------------------

// The float writing's tap (`loops::signalled_tap_in_float`), of one number and of eight lanes, for lines as long as
// 0, 1, 96 and 5,760 samples, 2^24 and past it, where a float no longer holds every whole number, up to the longest an
// int32 holds: distances at, beside and past the longest and its float, below 0, tiny, huge and no number. Its whole
// part a whole number in [0, the longest], so no read leaves the line, its fraction in [0, 1), and the two the
// distance held to [0, the longest]; every lane the number's.
bool float_taps_inside() {
    constexpr std::int32_t kLongest[] = {0, 1, 96, 5760, 1 << 24, (1 << 24) + 1, (1 << 24) + 3, 2147483647};
    constexpr float kTiny = 0x1p-149f;
    constexpr float kUp = std::numeric_limits<float>::infinity();
    std::size_t wrong = 0;
    std::size_t taps = 0;
    for (const std::int32_t longest : kLongest) {
        const float most = loops::largest_float_at_most(longest);
        const auto exact = static_cast<float>(longest);
        std::vector<float> d{static_cast<float>(kNaN), kUp, -kUp, 0.0f, -0.0f, 1e30f, -1e30f, kTiny, -kTiny, 0.5f, 1.0f,
                             most, std::nextafter(most, -kUp), std::nextafter(most, kUp), exact, std::nextafter(exact, -kUp),
                             std::nextafter(exact, kUp), 0.5f * exact + 0.5f, std::nextafter(1.0f, -kUp), -0.25f};
        while (d.size() % 8 != 0) {
            d.push_back(0.75f);
        }
        for (std::size_t at = 0; at < d.size(); at += 8) {
            const auto lanes = loops::signalled_tap_in_float(most, Lane<float, 8>::load(d.data() + at));
            for (std::size_t l = 0; l < 8; ++l) {
                const loops::FloatTap<float> tap = loops::signalled_tap_in_float(most, d[at + l]);
                const double held = d[at + l] > 0.0f ? std::min(static_cast<double>(d[at + l]), static_cast<double>(most)) : 0.0;
                const bool inside = tap.whole >= 0.0f && static_cast<double>(tap.whole) <= longest &&
                                    tap.whole == static_cast<float>(static_cast<std::int64_t>(tap.whole));
                const bool split = tap.fraction >= 0.0f && tap.fraction < 1.0f &&
                                   static_cast<double>(tap.whole) + static_cast<double>(tap.fraction) == held;
                const bool lane = same(lanes.whole[l], tap.whole) && same(lanes.fraction[l], tap.fraction);
                wrong += inside && split && lane ? 0 : 1;
                ++taps;
            }
        }
    }
    std::printf("delay_lines: the float tap of %zu distances over 8 lengths of line, in a lane and alone: %zu outside its "
                "line or its lane\n", taps, wrong);
    return wrong == 0;
}

// ---- a chorus, in blocks ----------------------------------------------------------------------------------------------

// x and x 5 to 25 ms ago, swept by a slow oscillator: a chorus, its line 1,201 samples. `Long`: a line of 120 ms.
template <bool Long>
constexpr Graph chorus() {
    Input x{"x", Signal{Length{}}};
    Variable phase;
    phase[n] = frac(phase[n - 1] + 0.7 / 48'000.0);
    Expr wobble = sinpi(2.0 * phase);
    Expr d = (Long ? 70.0 : 15.0) + (Long ? 50.0 : 10.0) * wobble;
    Output y{"y"};
    y = 0.5 * x + 0.5 * x[n - d.at_most(Long ? 120.0 : 25.0) * ms];
    return y;
}

constexpr std::size_t kRun = 3000;
std::vector<double> long_signal() {
    std::vector<double> x(kRun * 4);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = static_cast<double>(static_cast<float>(std::sin(0.013 * static_cast<double>(i)) + 0.25 * std::sin(0.31 * static_cast<double>(i))));
    }
    return x;
}

// One run over `total` samples, then the same in blocks of `blocks` (round and round), then again after a reset:
// every number the same, at Normal (the graphs' level), on machine `M`.
template <auto Build, const Machine& M>
bool blocks_equal_one_run(std::size_t total, std::initializer_list<std::int64_t> blocks, const char* what) {
    const std::vector<double> wide = long_signal();
    const std::vector<float> x(wide.begin(), wide.begin() + static_cast<std::ptrdiff_t>(total));
    std::vector<float> whole(total);
    std::vector<float> parts(total);
    std::vector<float> again(total);
    const auto once = std::make_unique<Executor<Build, M>>();
    once->prepare(static_cast<std::int64_t>(total));
    once->input("x") = x.data();
    once->output("y") = whole.data();
    once->run(static_cast<std::int64_t>(total));
    const auto in_blocks = std::make_unique<Executor<Build, M>>();
    in_blocks->prepare(*std::max_element(blocks.begin(), blocks.end()));
    for (std::vector<float>* into : {&parts, &again}) {
        std::size_t at = 0;
        for (auto b = blocks.begin(); at < total; b = b + 1 == blocks.end() ? blocks.begin() : b + 1) {
            const std::int64_t frames = std::min<std::int64_t>(*b, static_cast<std::int64_t>(total - at));
            in_blocks->input("x") = x.data() + at;
            in_blocks->output("y") = into->data() + at;
            in_blocks->run(frames);
            at += static_cast<std::size_t>(frames);
        }
        in_blocks->reset();
    }
    const bool ok = parts == whole && again == whole && whole != std::vector<float>(total);
    std::printf("delay_lines: %s in blocks %s one run, and after a reset, on %s\n", what, ok ? "equals" : "DIFFERS from",
                &M == &zen2 ? "zen2" : "the default machine");
    return ok;
}

// ---- refused by name ---------------------------------------------------------------------------------------------------

constexpr Graph flanger() {
    Input x{"x", Signal{kPoints}};
    Input d{"d", Signal{kPoints}};
    Output y{"y"};
    y[n] = x + 0.7 * y[n - d.at_most(5.0) * ms];
    return y;
}
// A line over an input whose distance is fed back: the line runs inside the cycle, as a flanger's does.
constexpr Graph distance_fed_back() {
    Input x{"x", Signal{kPoints}};
    Variable v;
    Expr d = 0.5 + 0.25 * v[n - 1];
    Expr wet = x[n - d.at_most(1.0) * ms];
    v[n] = 0.5 * wet;
    Output y{"y"};
    y = v;
    return y;
}
// A line of a Variable written before its definition, no loop through it: refused in the same words, where a lag written
// there is moved after the definition and runs (Plan/28-dsl-proposals/03 B4; a line is no lag to that pass: the
// backlog).
constexpr Graph before_its_definition() {
    Input x{"x", Signal{kPoints}};
    Input d{"d", Signal{kPoints}};
    Variable v;
    Expr late = v[n - d.at_most(1.0) * ms];
    v[n] = x * 0.5;
    Output y{"y"};
    y = late;
    return y;
}
constexpr Graph of_a_number() {
    Input d{"d", Signal{kPoints}};
    Output y{"y"};
    y = d + constant(0.5)[n - d.at_most(1.0) * ms];
    return y;
}
// Planned as written, before compiling copies the input: the planner's refusal of a line it has not given a copy.
bool unplanned_copy_refused() {
    Graph g = one_at<Over::AnInput, ElemType::F32, High>();
    if (!infer(g).ok) {
        fail("delay_lines_test: the graph did not analyse");
    }
    const loops::Refusal why = planned::refusal_of(g, zen2);
    return why.view() == "heinzel::compile: a delay line over an input reads a copy of it, which this graph lacks";
}

} // namespace

int main() {
    bool ok = agrees_everywhere<Over::AnInput>();
    ok = agrees_everywhere<Over::AComputedValue>() && ok;
    voices_agree();
    std::printf("delay_lines: three voices at High, on the default machine and zen2, as the walk reads them\n");
    ok = knob_reads() && ok;
    ok = float_taps_inside() && ok;
    const std::vector<double> x = long_signal();
    testing::executed<chorus<false>, kRun>({{"x", x.data()}});
    testing::executed<chorus<false>, kRun, zen2>({{"x", x.data()}});
    std::printf("delay_lines: a chorus at High, on the default machine and zen2, as the walk reads it\n");
    ok = blocks_equal_one_run<chorus<false>, default_machine>(kRun, {1, 7, 64, 100, 3, 1225, 600}, "a chorus (a line of 1,201 samples)") && ok;
    ok = blocks_equal_one_run<chorus<false>, zen2>(kRun, {1, 7, 64, 100, 3, 1225, 600}, "a chorus (a line of 1,201 samples)") && ok;
    ok = blocks_equal_one_run<chorus<true>, default_machine>(4 * kRun, {64}, "a line of 120 ms, 5,761 samples,") && ok;
    ok = blocks_equal_one_run<chorus<true>, zen2>(4 * kRun, {64}, "a line of 120 ms, 5,761 samples,") && ok;
    constexpr std::string_view in_feedback = "heinzel::compile: a delay line in feedback, or of a value defined later, is not lowered yet";
    testing::still_refused<flanger>(in_feedback);
    testing::still_refused<distance_fed_back>(in_feedback);
    testing::still_refused<before_its_definition>(in_feedback);
    testing::still_refused<of_a_number>("heinzel::compile: a delay line over a constant is not lowered yet");
    const bool unplanned = unplanned_copy_refused();
    std::printf("delay_lines: a delay line in feedback (its signal, its distance) and before its value's definition, of a constant, and planned with no copy refused by name: %s\n",
                unplanned ? "yes" : "NO");
    if (!ok || !unplanned) {
        std::printf("delay_lines_test: FAILED\n");
        return 1;
    }
    std::printf("delay_lines_test: a delay that changes reads as the walk does, in blocks as in one run\n");
    return 0;
}
