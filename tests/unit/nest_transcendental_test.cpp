// The transcendentals with a float core, end to end on the loop-nest path (plan, lower, run):
// the channel strip the milestone is named after — gain, three saturations, gain, clamp — a bare
// `tanh`, `exp`, `sinpi`, `sin` and `log`, in lanes with every remainder length, and `log(|sin x| + 1)`
// along time and over eleven voices. The sine is also run over |x| up to 8166, where its reduction
// needs all five parts, and the logarithm over positive floats of every magnitude, subnormals and zero
// among them.
//
// WHAT IS CHECKED, and the two levels are checked differently on purpose. Under `Normal` an F32 value
// is computed IN FLOAT, so the oracle is the double core rounded to float, to the 2 f32 ulp the float
// cores are measured at (`tests/unit/float_cores_test.cpp`, Plan/archive/03 §3) — and, for the strip, the same
// float cores applied by hand, which the run must match BIT FOR BIT: a chain of three saturations
// compounds a rounding bound into a number nobody can argue about, while "the nest path computes what
// the core computes, in the order the graph says" is exactly the claim worth making.
// Under `High` the runner computes each node in double and rounds once, which is what the reference
// walk does, so there the check is exact at every node — and the strip's `High` chain is a hand
// reference that rounds where the runner does.
//
// AND THE REFUSAL. Every transcendental carries the same rule columns; which of them the nest path
// admits is the table's list of the ones with a writing over the value type. So a graph holding an arc
// tangent — none written yet — must be refused at planning, and `loops::lowers` must say so; a cosine, a
// float core like the sine's (`cos_tan_pow_exp2_test.cpp` holds it to the walk, within 2 ulp where it
// runs in float), lowers here.

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "planned.hpp"
#include "tree_facts.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

// The strip's constants, as the benchmark has them.
constexpr double kInputGain = 2.0;
constexpr double kOutputGain = 0.7;
constexpr double kDrive[3] = {1.5, 1.3, 1.1};
constexpr double kBias[3] = {0.10, -0.05, 0.0};

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_transcendental_test: a graph did not resolve");
    }
    return g;
}

constexpr Length kSamples{.multiple_of = 1, .min = 1, .expect = 64};

constexpr Expr saturate(Expr s, double drive, double bias) { return tanh(constant(drive) * s + bias); }

// y = clamp(gout * tanh(d3 * tanh(d2 * tanh(d1 * (gin * x) + b1) + b2) + b3), -1, 1), over a run-time
// length — every node elementwise, so the whole strip is one fused stage walked in lanes.
constexpr Graph strip(numerics::Precision level) {
    Expr s = constant(kInputGain) * Input{"x", Signal{kSamples}};
    for (int stage = 0; stage < 3; ++stage) {
        s = saturate(s, kDrive[stage], kBias[stage]);
    }
    Graph g = finish(Output{"y"} = clamp(constant(kOutputGain) * s, -1.0, 1.0));
    set_default_precision(g, level);
    return analysed(g);
}

// One transcendental and nothing else, which is where an ulp bound means what it says.
constexpr Graph alone(OpCode which, numerics::Precision level) {
    const Expr x = Input{"x", Signal{kSamples}};
    const Expr y = which == op::Exp ? exp(x) : which == op::Tanh ? tanh(x) : which == op::Sin ? sin(x) : which == op::Log ? log(x) : sinpi(x);
    Graph g = finish(Output{"y"} = y);
    set_default_precision(g, level);
    return analysed(g);
}

// The two new cores composed, to show they are plain maps like the rest: one stage, in lanes —
// over one stream, or over eleven voices of it.
constexpr Graph sine_then_logarithm(std::int32_t voices) {
    const Expr x = voices == 1 ? Input{"x", Signal{kSamples}} : Input{"x", Signal{voices, kSamples}};
    return analysed(finish(Output{"y"} = log(abs(sin(x)) + 1.0)));
}

constexpr Graph kStrip = strip(numerics::Precision::Normal);
constexpr Graph kStripHigh = strip(numerics::Precision::High);
constexpr Graph kTanh = alone(op::Tanh, numerics::Precision::Normal);
constexpr Graph kExp = alone(op::Exp, numerics::Precision::Normal);
constexpr Graph kSinPi = alone(op::SinPi, numerics::Precision::Normal);
constexpr Graph kTanhHigh = alone(op::Tanh, numerics::Precision::High);
constexpr Graph kExpHigh = alone(op::Exp, numerics::Precision::High);
constexpr Graph kSinPiHigh = alone(op::SinPi, numerics::Precision::High);
constexpr Graph kSin = alone(op::Sin, numerics::Precision::Normal);
constexpr Graph kLog = alone(op::Log, numerics::Precision::Normal);
constexpr Graph kSinHigh = alone(op::Sin, numerics::Precision::High);
constexpr Graph kLogHigh = alone(op::Log, numerics::Precision::High);

constexpr planned::Held kStripNestHeld{planned::program_of(kStrip, zen2)};
constexpr const loops::Program& kStripNest = kStripNestHeld.program;
constexpr planned::Held kStripHighNestHeld{planned::program_of(kStripHigh, zen2)};
constexpr const loops::Program& kStripHighNest = kStripHighNestHeld.program;
constexpr planned::Held kTanhNestHeld{planned::program_of(kTanh, zen2)};
constexpr const loops::Program& kTanhNest = kTanhNestHeld.program;
constexpr planned::Held kExpNestHeld{planned::program_of(kExp, zen2)};
constexpr const loops::Program& kExpNest = kExpNestHeld.program;
constexpr planned::Held kSinPiNestHeld{planned::program_of(kSinPi, zen2)};
constexpr const loops::Program& kSinPiNest = kSinPiNestHeld.program;
constexpr planned::Held kTanhHighNestHeld{planned::program_of(kTanhHigh, zen2)};
constexpr const loops::Program& kTanhHighNest = kTanhHighNestHeld.program;
constexpr planned::Held kExpHighNestHeld{planned::program_of(kExpHigh, zen2)};
constexpr const loops::Program& kExpHighNest = kExpHighNestHeld.program;
constexpr planned::Held kSinPiHighNestHeld{planned::program_of(kSinPiHigh, zen2)};
constexpr const loops::Program& kSinPiHighNest = kSinPiHighNestHeld.program;
constexpr planned::Held kSinNestHeld{planned::program_of(kSin, zen2)};
constexpr const loops::Program& kSinNest = kSinNestHeld.program;
constexpr planned::Held kLogNestHeld{planned::program_of(kLog, zen2)};
constexpr const loops::Program& kLogNest = kLogNestHeld.program;
constexpr planned::Held kSinHighNestHeld{planned::program_of(kSinHigh, zen2)};
constexpr const loops::Program& kSinHighNest = kSinHighNestHeld.program;
constexpr planned::Held kLogHighNestHeld{planned::program_of(kLogHigh, zen2)};
constexpr const loops::Program& kLogHighNest = kLogHighNestHeld.program;
constexpr Graph kChain = sine_then_logarithm(1);
constexpr Graph kVoices = sine_then_logarithm(11);
constexpr planned::Held kChainNestHeld{planned::program_of(kChain, zen2)};
constexpr const loops::Program& kChainNest = kChainNestHeld.program;
constexpr planned::Held kVoicesNestHeld{planned::program_of(kVoices, zen2)};
constexpr const loops::Program& kVoicesNest = kVoicesNestHeld.program;

// Stage 0's outermost walk is in lanes: a strip with a Vector over the same axis inside it.
constexpr bool lanes_outermost(const loops::Program& p) {
    const auto w = tree_facts::walks(p, 0);
    return w.size() >= 2 && loops::is_strip(w[0]) && w[1].kind == loops::NodeKind::Vector && w[1].axis == w[0].axis;
}

static_assert(kChainNest.stages.size() == 1 && lanes_outermost(kChainNest) && kVoicesNest.stages.size() == 1 &&
                  tree_facts::walk(kVoicesNest, 0, tree_facts::walks(kVoicesNest, 0).size() - 1).kind == loops::NodeKind::Vector,
              "a sine and a logarithm are plain maps: one stage, in lanes");

// What was decided, read off the program: the whole strip is ONE stage — one load, nothing stored but
// the result — walked by one strip of lanes of eight floats (a core keeps a sixteenth of the registers: one). A transcendental that had not joined the
// plain maps would have been a stage of its own with a buffer between.
static_assert(kStripNest.stages.size() == 1 && kStripNest.buffers.size() == 2 && tree_facts::walks(kStripNest, 0).size() == 2 &&
                  lanes_outermost(kStripNest) && tree_facts::walk(kStripNest, 0, 1).step == 8,
              "the channel strip is one fused stage walked in lanes of eight floats");
// `High` computes each node in double and rounds once; the lane is still eight wide, since the lane
// COUNT follows the value's stored type and only the arithmetic widens.
static_assert(kStripHighNest.stages.size() == 1 && lanes_outermost(kStripHighNest) && tree_facts::walk(kStripHighNest, 0, 1).step == 8,
              "High changes what the strip computes in, not how wide it is walked");

// An arc tangent has the same rule columns as the hyperbolic tangent and no writing over the value type, so the
// nest path must decline it by name, and `loops::lowers` — what the router asks — say no. (The frontend spells no
// `atan`; the operation is applied by its code.) A cosine lowers.
Graph arc_tangent_of_input() {
    return analysed(finish(Output{"y"} = apply(op::Atan, Input{"x", Signal{kSamples}})));
}

Graph cosine_of_input() {
    return analysed(finish(Output{"y"} = cos(Input{"x", Signal{kSamples}})));
}

int g_wrong = 0;

// How many representable floats apart two answers are (`tests/unit/float_cores_test.cpp` says why the
// negative half is flipped).
std::int64_t ordered(float x) {
    const std::int64_t bits = std::bit_cast<std::int32_t>(x);
    return bits >= 0 ? bits : -0x80000000LL - bits;
}

std::int64_t ulps_apart(float got, float want) {
    if (got != got || want != want) {
        return (got != got) == (want != want) ? 0 : (1LL << 40);
    }
    const std::int64_t d = ordered(got) - ordered(want);
    return d < 0 ? -d : d;
}

void expect(const char* what, std::size_t n, std::size_t i, float got, float want, std::int64_t bound) {
    if (ulps_apart(got, want) > bound && ++g_wrong <= 10) {
        std::printf("  %s, n=%zu, sample %zu: got %a, want %a (%lld ulp, bound %lld)\n", what, n, i, got, want,
                    static_cast<long long>(ulps_apart(got, want)), static_cast<long long>(bound));
    }
}

std::vector<float> ramp(std::size_t n) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        // A sweep through the saturation's whole interesting range, below and above the 0.625 its core
        // changes method at, and through zero.
        v[i] = static_cast<float>(-3.2 + 0.0731 * static_cast<double>(i % 89));
    }
    return v;
}

// |x| up to 8166, where the sine's quadrant count reaches the thousands.
std::vector<float> wide(std::size_t n) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = static_cast<float>((static_cast<double>(i % 211) - 105.0) * 77.77);
    }
    return v;
}

// Positive floats spread evenly in exponent, from subnormal (2^-140) to 2^70, and every 53rd a zero.
std::vector<float> positive(std::size_t n) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = i % 53 == 52 ? 0.0f : static_cast<float>(std::ldexp(1.0 + 0.0137 * static_cast<double>(i % 61), static_cast<int>(i % 211) - 140));
    }
    return v;
}

// `x` holds `x.size() / length` voices of `length` samples each.
template <const loops::Program& P>
std::vector<float> run(const std::vector<float>& x, std::size_t length = 0) {
    canaried::Workspace<P> work(loops::Sizes{static_cast<std::int64_t>(length == 0 ? x.size() : length)});
    canaried::run<P>(loops::Feeds{x.data()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    return std::vector<float>(y, y + x.size());
}

// The strip by hand at `Normal`: every step in float, the transcendental the float core, the constants
// rounded to float exactly as the runner broadcasts them.
float strip_in_float(float x) {
    float s = static_cast<float>(kInputGain) * x;
    for (int stage = 0; stage < 3; ++stage) {
        s = hyperbolic_tangent_in_float(static_cast<float>(kDrive[stage]) * s + static_cast<float>(kBias[stage]));
    }
    s = static_cast<float>(kOutputGain) * s;
    return s < 1.0f ? (s > -1.0f ? s : -1.0f) : 1.0f;
}

// And at `High`: every step in double, rounded to float once, which is what the reference walk does.
float strip_via_double(float x) {
    double s = static_cast<double>(static_cast<float>(kInputGain * static_cast<double>(x)));
    for (int stage = 0; stage < 3; ++stage) {
        const float driven = static_cast<float>(kDrive[stage] * s);
        const float biased = static_cast<float>(static_cast<double>(driven) + kBias[stage]);
        s = static_cast<double>(static_cast<float>(hyperbolic_tangent(static_cast<double>(biased), FnAccuracy::Exact)));
    }
    const float out = static_cast<float>(kOutputGain * s);
    const float floored = out > -1.0f ? out : -1.0f;
    return floored < 1.0f ? floored : 1.0f;
}

// The sine far from zero, the logarithm over every magnitude, and the two composed — against the float
// cores applied by hand, bit for bit, as the strip is.
void check_wide(std::size_t n) {
    const std::vector<float> xs = wide(n);
    const std::vector<float> ps = positive(n);
    const std::vector<float> sin_normal = run<kSinNest>(xs);
    const std::vector<float> sin_high = run<kSinHighNest>(xs);
    const std::vector<float> log_normal = run<kLogNest>(ps);
    const std::vector<float> log_high = run<kLogHighNest>(ps);
    const std::vector<float> chain = run<kChainNest>(xs);
    std::vector<float> eleven(11 * n);
    for (std::size_t i = 0; i < eleven.size(); ++i) {
        eleven[i] = xs[i % n] + static_cast<float>(i / n);
    }
    const std::vector<float> voices = run<kVoicesNest>(eleven, n);
    for (std::size_t i = 0; i < n; ++i) {
        expect("sin, wide", n, i, sin_normal[i], static_cast<float>(sine(static_cast<double>(xs[i]), FnAccuracy::Exact)), 2);
        expect("sin at High, wide", n, i, sin_high[i], static_cast<float>(sine(static_cast<double>(xs[i]), FnAccuracy::Exact)), 0);
        expect("log, every magnitude", n, i, log_normal[i], static_cast<float>(natural_logarithm(static_cast<double>(ps[i]), FnAccuracy::Exact)), 2);
        expect("log at High, every magnitude", n, i, log_high[i], static_cast<float>(natural_logarithm(static_cast<double>(ps[i]), FnAccuracy::Exact)),
               0);
        expect("log(|sin x| + 1)", n, i, chain[i], logarithm_in_float(std::fabs(sine_in_float(xs[i])) + 1.0f), 0);
    }
    for (std::size_t i = 0; i < eleven.size(); ++i) {
        expect("log(|sin x| + 1), eleven voices", n, i, voices[i], logarithm_in_float(std::fabs(sine_in_float(eleven[i])) + 1.0f), 0);
    }
}

void check(std::size_t n) {
    const std::vector<float> x = ramp(n);
    const std::vector<float> strip_normal = run<kStripNest>(x);
    const std::vector<float> strip_high = run<kStripHighNest>(x);
    const std::vector<float> tanh_normal = run<kTanhNest>(x);
    const std::vector<float> exp_normal = run<kExpNest>(x);
    const std::vector<float> sinpi_normal = run<kSinPiNest>(x);
    const std::vector<float> tanh_high = run<kTanhHighNest>(x);
    const std::vector<float> exp_high = run<kExpHighNest>(x);
    const std::vector<float> sinpi_high = run<kSinPiHighNest>(x);
    const std::vector<float> sin_normal = run<kSinNest>(x);
    const std::vector<float> log_normal = run<kLogNest>(x);
    const std::vector<float> sin_high = run<kSinHighNest>(x);
    const std::vector<float> log_high = run<kLogHighNest>(x);
    for (std::size_t i = 0; i < n; ++i) {
        const double wide = static_cast<double>(x[i]);
        // The strip against the float cores applied by hand: the same arithmetic in the same order.
        expect("strip", n, i, strip_normal[i], strip_in_float(x[i]), 0);
        expect("strip at High", n, i, strip_high[i], strip_via_double(x[i]), 0);
        // One transcendental alone, against the double core rounded to float.
        expect("tanh", n, i, tanh_normal[i], static_cast<float>(hyperbolic_tangent(wide, FnAccuracy::Exact)), 2);
        expect("exp", n, i, exp_normal[i], static_cast<float>(exponential(wide, FnAccuracy::Exact)), 2);
        expect("sinpi", n, i, sinpi_normal[i], static_cast<float>(sine_of_half_turns(wide, FnAccuracy::Exact)), 2);
        expect("tanh at High", n, i, tanh_high[i], static_cast<float>(hyperbolic_tangent(wide, FnAccuracy::Exact)), 0);
        expect("exp at High", n, i, exp_high[i], static_cast<float>(exponential(wide, FnAccuracy::Exact)), 0);
        expect("sinpi at High", n, i, sinpi_high[i], static_cast<float>(sine_of_half_turns(wide, FnAccuracy::Exact)), 0);
        expect("sin", n, i, sin_normal[i], static_cast<float>(sine(wide, FnAccuracy::Exact)), 2);
        expect("log", n, i, log_normal[i], static_cast<float>(natural_logarithm(wide, FnAccuracy::Exact)), 2);
        expect("sin at High", n, i, sin_high[i], static_cast<float>(sine(wide, FnAccuracy::Exact)), 0);
        expect("log at High", n, i, log_high[i], static_cast<float>(natural_logarithm(wide, FnAccuracy::Exact)), 0);
    }
}

bool refuses(const Graph& g) { return planned::refusal_of(g, zen2).view().starts_with("heinzel::compile: "); }

} // namespace

int main() {
    // Every remainder a lane of eight can leave, and lengths well past one block.
    for (const std::size_t n : {1u, 3u, 7u, 8u, 9u, 15u, 16u, 17u, 31u, 37u, 64u, 1000u}) {
        check(n);
        check_wide(n);
    }
    const Graph arc_tangent = arc_tangent_of_input();
    if (!refuses(arc_tangent) || loops::lowers(arc_tangent, Machine{})) {
        ++g_wrong;
        std::printf("  a transcendental with no writing over the value type must be refused by name\n");
    }
    const auto runs = [](const Graph& g) { return planned::refusal_of(g, Machine{}).empty(); }; // (and the runner accepts it)
    const Graph cosine = cosine_of_input();
    if (!runs(kStrip) || !runs(kExp) || !runs(kSinPi) || !runs(kSin) || !runs(kLog) || !runs(cosine) || !loops::lowers(cosine, Machine{})) {
        ++g_wrong;
        std::printf("  the strip and a bare exponential, sinpi, sine, logarithm and cosine do lower here, and the router must say so\n");
    }
    if (g_wrong != 0) {
        std::printf("%d results are wrong\n", g_wrong);
        return 1;
    }
    return 0;
}
