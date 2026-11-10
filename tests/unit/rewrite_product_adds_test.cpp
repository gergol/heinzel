// A product's add or subtract fused where the machine has the multiply-add, outside feedback cycles
// (`rewrite/product_adds.hpp`), and a sum folding a product fused with it (`Schedule::Stage::contract` on a fold's
// stage, `planner/loops.hpp` `sums_a_product`): one rounding instead of two, only where the precision grants reordering.
//
//   brightness  clamp(1.2x + 0.05, 0, 1)        -> clamp(madd(1.2, x, 0.05), 0, 1): the multiply gone
//   either side 0.05 + 1.2x                      -> the same
//   wrap        c - 2pi*a                        -> madd(-2pi, a, c): the constant negated, no node added
//   subtracted  c - a*b                          -> madd(-a, b, c): a `_neg` of a factor, folded by the compiler
//   minuend     1.2a - 0.05,  a*b - c            -> madd(1.2, a, -0.05),  madd(a, b, -c)
//   a Param k   c - k*a,  c - a*k                -> madd(k, -a, c),  madd(-a, k, c): never a `_neg` of a value made apart
//               a*b - k                          -> as written: a `-k` would be made once a run and reloaded a vector
//               c - k*m                          -> as written: the product a Param's shape, not the sum's
//   read twice  p = a*b; y = p + c, z = 2p       -> as written: the product is a value of its own (subtracted too)
//   in a cycle  y = x +- .75y[-1]                 -> as written by this rule (the feedback rule's, `feedback_sums.hpp`)
//   into one    y = .3x + .4x[-1] + .5y[-1]       -> as written: its feed-forward terms are the feedback sum's too
//   wider sum   float a*b + double d             -> as written: the add rounds in another type
//   no fma, High                                 -> as written
//   conv        Σ_k (x*x)[i+k]·h[k]              -> its window's sum fused with the product on the M1 Pro: the fold reads
//                                                   x*x and h (not on zen2, whose adds have pipes of their own)
//   a sum of x  Σ_k x[i+k]                       -> no product: as written
//
// And the numbers: each fused program against the graph walked in double, within a float's rounding of the terms.

#include "heinzel/compile.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/sema/analyze.hpp"

#include "interpret.hpp"
#include "planned.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr std::int32_t kSamples = 256;
constexpr std::int32_t kTaps = 16;
constexpr Index n{};

constexpr Graph analysed(Graph g, numerics::Precision level) {
    set_default_precision(g, level);
    if (!infer(g).ok) {
        fail("rewrite_product_adds_test: a graph did not resolve");
    }
    return g;
}

constexpr Graph brightness(bool product_first, numerics::Precision level = numerics::Precision::Normal) {
    Input x{"x", {kSamples}};
    const Expr affine = product_first ? constant(1.2) * x + 0.05 : 0.05 + constant(1.2) * x;
    return analysed(finish(Output{"y"} = clamp(affine, 0.0, 1.0)), level);
}
constexpr Graph read_twice() {
    Input a{"a", Signal{kSamples}};
    Input b{"b", Signal{kSamples}};
    Input c{"c", Signal{kSamples}};
    const Expr p = a * b;
    return analysed(finish(Output{"y"} = p + c, Output{"z"} = 2.0 * p), numerics::Precision::Normal);
}
// The subtractions: `which` 0 `c - 2pi*a` (a constant factor), 1 `c - a*b`, 2 `1.2a - 0.05` (a constant minuend's
// sum), 3 `a*b - c`; with Params `k`, `m`: 4 `c - k*a`, 5 `c - a*k`, 6 `a*b - k`, 7 `c - k*m`.
constexpr Graph subtracted(int which, numerics::Precision level = numerics::Precision::Normal) {
    Input a{"a", {kSamples}};
    Input b{"b", {kSamples}};
    Input c{"c", {kSamples}};
    if (which >= 4) {
        Param k{"k"};
        Param m{"m"};
        const Expr y = which == 4 ? c - k * a : which == 5 ? c - a * k : which == 6 ? a * b - k : c - k * m;
        return analysed(finish(Output{"y"} = y), level);
    }
    if (which == 0) {
        return analysed(finish(Output{"y"} = c - constant(6.283185307179586) * a), level);
    }
    if (which == 1) {
        return analysed(finish(Output{"y"} = c - a * b), level);
    }
    if (which == 2) {
        return analysed(finish(Output{"y"} = constant(1.2) * a - 0.05), level);
    }
    return analysed(finish(Output{"y"} = a * b - c), level);
}
constexpr Graph read_twice_subtracted() {
    Input a{"a", Signal{kSamples}};
    Input b{"b", Signal{kSamples}};
    Input c{"c", Signal{kSamples}};
    const Expr p = a * b;
    return analysed(finish(Output{"y"} = c - p, Output{"z"} = 2.0 * p), numerics::Precision::Normal);
}
constexpr Graph one_pole(bool minus = false) {
    Input x{"x", Signal{kSamples}};
    Variable y;
    y[n] = minus ? x - 0.75 * y[n - 1] : x + 0.75 * y[n - 1];
    return analysed(finish(Output{"y"} = y), numerics::Precision::Normal);
}
constexpr Graph into_a_cycle() {
    Input x{"x", Signal{kSamples}};
    Variable y;
    y[n] = 0.3 * x + 0.4 * x[n - 1] + 0.5 * y[n - 1];
    return analysed(finish(Output{"y"} = y), numerics::Precision::Normal);
}
constexpr Graph wider_sum() {
    Input a{"a", Signal{kSamples}};
    Input b{"b", Signal{kSamples}};
    Input d{"d", Signal{kSamples}, f64};
    return analysed(finish(Output{"y"} = a * b + d), numerics::Precision::Normal);
}
constexpr Graph conv(bool squared, numerics::Precision level = numerics::Precision::Normal) {
    Input x{"x", {kSamples}};
    Input h{"h", {1, kTaps}};
    const Expr window = squared ? (x * x).unfold(0, kTaps) * h : x.unfold(0, kTaps);
    return analysed(finish(Output{"y"} = window.sum(1)), level);
}

constexpr Machine kNoFma{.vector_bytes = 32, .registers = 16, .fma = false};

// How many nodes play `role`.
constexpr std::int32_t count(const Graph& g, Algebra role) {
    std::int32_t k = 0;
    for (const Node& node : g.nodes) {
        k += rules_of(node.op).algebra == role ? 1 : 0;
    }
    return k;
}
constexpr Graph fused(const Graph& g) { return rewrite::fused_product_adds(g, zen2); }

static_assert(count(fused(brightness(true)), Algebra::ProductAdd) == 1 && count(fused(brightness(true)), Algebra::Product) == 0 &&
                  fused(brightness(true)).nodes.size() + 1 == brightness(true).nodes.size(),
              "brightness: the affine map is one multiply-add, its multiply gone");
static_assert(count(fused(brightness(false)), Algebra::ProductAdd) == 1, "the product on either side of the add");
static_assert(fused(read_twice()) == read_twice(), "a product read twice is a value of its own");
static_assert(fused(one_pole()) == one_pole(), "a product in a feedback cycle is the feedback rule's");
static_assert(fused(one_pole(true)) == one_pole(true), "subtracted there too (Decision 66's, not this rule's)");
static_assert(fused(into_a_cycle()) == into_a_cycle(), "and so are the feed-forward terms of a feedback sum");
static_assert(fused(wider_sum()) == wider_sum(), "a float product added in double: another rounding, as written");
static_assert(rewrite::fused_product_adds(brightness(true), kNoFma) == brightness(true), "no multiply-add: as written");
static_assert(fused(brightness(true, numerics::Precision::High)) == brightness(true, numerics::Precision::High),
              "High grants no reordering: as written");
static_assert(count(rewrite::rewritten(brightness(true), zen2), Algebra::ProductAdd) == 1, "and `compile` applies it");

// The multiply-add a subtraction became.
constexpr const Node& the_madd(const Graph& g) {
    for (const Node& node : g.nodes) {
        if (rules_of(node.op).algebra == Algebra::ProductAdd) {
            return node;
        }
    }
    fail("rewrite_product_adds_test: no multiply-add");
    return g.nodes[0];
}
constexpr bool constant_is(const Graph& g, std::int32_t v, double c) {
    return g.values[static_cast<std::size_t>(v)].category == ValueCategory::Const && g.values[static_cast<std::size_t>(v)].constant == c;
}
// The value `g`'s input `name` is.
constexpr std::int32_t input(const Graph& g, char name) {
    for (const NamedValue& io : g.inputs) {
        if (io.name.view()[0] == name) {
            return io.value;
        }
    }
    return kNoValue;
}
// The operand of the `_neg` making value `v`, or kNoValue.
constexpr std::int32_t negation_of(const Graph& g, std::int32_t v) {
    for (const Node& node : g.nodes) {
        if (node.output == v && rules_of(node.op).algebra == Algebra::Negate) {
            return node.inputs[0];
        }
    }
    return kNoValue;
}
// Fused, the product gone, `negations` `_neg` nodes added.
constexpr bool fuses_as(int which, std::int32_t negations) {
    const Graph g = fused(subtracted(which));
    return count(g, Algebra::ProductAdd) == 1 && count(g, Algebra::Product) == 0 && count(g, Algebra::Subtract) == 0 &&
           count(g, Algebra::Negate) == negations && g.nodes.size() + 1 == subtracted(which).nodes.size() + static_cast<std::size_t>(negations);
}
static_assert(fuses_as(0, 0) && fuses_as(2, 0), "a constant's negation folded: one node fewer");
static_assert(fuses_as(1, 1) && fuses_as(3, 1), "otherwise a `_neg` in the product's place");
constexpr Graph kWrapFused = fused(subtracted(0));
constexpr Graph kMinusFused = fused(subtracted(1));
constexpr Graph kAffineFused = fused(subtracted(2));
constexpr Graph kMinuendFused = fused(subtracted(3));
static_assert(constant_is(kWrapFused, the_madd(kWrapFused).inputs[0], -6.283185307179586) && the_madd(kWrapFused).inputs[1] == input(kWrapFused, 'a') &&
                  the_madd(kWrapFused).inputs[2] == input(kWrapFused, 'c'),
              "c - 2pi*a: madd(-2pi, a, c)");
static_assert(negation_of(kMinusFused, the_madd(kMinusFused).inputs[0]) == input(kMinusFused, 'a') && the_madd(kMinusFused).inputs[1] == input(kMinusFused, 'b') &&
                  the_madd(kMinusFused).inputs[2] == input(kMinusFused, 'c'),
              "c - a*b: madd(-a, b, c)");
static_assert(constant_is(kAffineFused, the_madd(kAffineFused).inputs[0], 1.2) && the_madd(kAffineFused).inputs[1] == input(kAffineFused, 'a') &&
                  constant_is(kAffineFused, the_madd(kAffineFused).inputs[2], -0.05),
              "1.2a - 0.05: madd(1.2, a, -0.05)");
static_assert(the_madd(kMinuendFused).inputs[0] == input(kMinuendFused, 'a') && the_madd(kMinuendFused).inputs[1] == input(kMinuendFused, 'b') &&
                  negation_of(kMinuendFused, the_madd(kMinuendFused).inputs[2]) == input(kMinuendFused, 'c'),
              "a*b - c: madd(a, b, -c)");
constexpr Graph kParamFactorFused = fused(subtracted(4));
constexpr Graph kParamSecondFused = fused(subtracted(5));
static_assert(count(kParamFactorFused, Algebra::Negate) == 1 && the_madd(kParamFactorFused).inputs[0] == input(kParamFactorFused, 'k') &&
                  negation_of(kParamFactorFused, the_madd(kParamFactorFused).inputs[1]) == input(kParamFactorFused, 'a') &&
                  the_madd(kParamFactorFused).inputs[2] == input(kParamFactorFused, 'c'),
              "c - k*a, k a Param: madd(k, -a, c), the signal negated, not the Param");
static_assert(fuses_as(5, 1) && negation_of(kParamSecondFused, the_madd(kParamSecondFused).inputs[0]) == input(kParamSecondFused, 'a') &&
                  the_madd(kParamSecondFused).inputs[1] == input(kParamSecondFused, 'k'),
              "c - a*k: madd(-a, k, c), the Param as it is on either side");
static_assert(fused(subtracted(6)) == subtracted(6), "a*b - k, k a Param: as written, no `-k` made once a run");
static_assert(fused(subtracted(7)) == subtracted(7), "c - k*m: a product of Params, laid out as one, not as the sum: as written");
static_assert(fused(read_twice_subtracted()) == read_twice_subtracted(), "a subtracted product read twice: as written");
static_assert(fused(subtracted(0, numerics::Precision::High)) == subtracted(0, numerics::Precision::High) &&
                  fused(subtracted(1, numerics::Precision::High)) == subtracted(1, numerics::Precision::High),
              "High grants no reordering: as written");
static_assert(rewrite::fused_product_adds(subtracted(1), kNoFma) == subtracted(1) && rewrite::fused_product_adds(subtracted(3), kNoFma) == subtracted(3),
              "no multiply-add: as written");

constexpr bool settled(const Graph& g) {
    const Graph once = fused(g);
    Graph again = once;
    return infer(again).ok && again == once && fused(once) == once;
}
static_assert(settled(brightness(true)) && settled(brightness(false)) && settled(read_twice()), "a fixed point of infer and of the rule");
static_assert(settled(subtracted(0)) && settled(subtracted(1)) && settled(subtracted(2)) && settled(subtracted(3)) && settled(subtracted(4)) &&
                  settled(subtracted(5)),
              "subtracted too");

// The fold's statement in the window's stage of a planned program, and whether it was fused.
constexpr const loops::Stmt& the_fold(const loops::Program& p) {
    for (const loops::Stmt& st : p.stmts) {
        if (st.kind == loops::StmtKind::Fold) {
            return st;
        }
    }
    fail("rewrite_product_adds_test: no fold");
    return p.stmts[0];
}
constexpr Machine kM1NoFma = [] {
    Machine m = m1_pro;
    m.fma = false;
    return m;
}();
constexpr planned::Held kConv{planned::program_of(conv(true), m1_pro)};
constexpr planned::Held kConvHigh{planned::program_of(conv(true, numerics::Precision::High), m1_pro)};
constexpr planned::Held kConvNoFma{planned::program_of(conv(true), kM1NoFma)};
constexpr planned::Held kWindowSum{planned::program_of(conv(false), m1_pro)};
constexpr planned::Held kConvZen2{planned::program_of(conv(true), zen2)};
static_assert(the_fold(kConv.program).fused && the_fold(kConv.program).in[1].source == loops::Source::Local,
              "conv on the M1 Pro: the window's sum folds the product's two operands in one multiply-add");
static_assert(!the_fold(kConvHigh.program).fused && !the_fold(kConvNoFma.program).fused && !the_fold(kWindowSum.program).fused,
              "not at High, not without the instruction, not where the sum folds no product");
static_assert(!the_fold(kConvZen2.program).fused, "not on zen2, whose adds have pipes of their own");

// ---- numbers ----------------------------------------------------------------------------------------------

int g_wrong = 0;

std::vector<float> ramp(std::size_t count, float scale) {
    std::vector<float> v(count);
    for (std::size_t i = 0; i < count; ++i) {
        v[i] = scale * static_cast<float>(static_cast<std::int32_t>(i * 37 % 201) - 100) * 0.01f;
    }
    return v;
}

// Program `P` run over `feeds`, its first output against graph `G` walked in double, relative to `scale` (the terms'
// magnitude): within a few float roundings.
template <const loops::Program& P>
void check(const char* what, const Graph& g, const std::vector<std::vector<float>>& feeds, std::size_t count, double scale) {
    loops::Feeds ptrs{};
    Inputs walk{};
    std::vector<std::vector<double>> widened;
    for (const auto& f : feeds) {
        widened.emplace_back(f.begin(), f.end());
    }
    for (std::size_t k = 0; k < feeds.size(); ++k) {
        ptrs[k] = feeds[k].data();
        walk.push_back(InputFeed{widened[k].data(), static_cast<std::int32_t>(feeds[k].size())});
    }
    loops::Workspace<P> work(loops::Sizes{});
    loops::run<P>(ptrs, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    Graph in_double = g;
    set_default_precision(in_double, numerics::Precision::High);
    const auto out = std::make_unique<Signals<1, (1 << 15)>>(); // (every value of the walk, conv's window whole)
    run(in_double, walk, *out);
    double worst = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const double want = sample_at(*out, in_double.outputs[0].value, static_cast<std::int32_t>(i), 0);
        worst = std::max(worst, std::fabs(static_cast<double>(y[i]) - want) / scale);
    }
    std::printf("  %s: %.3g of the terms\n", what, worst);
    if (!(worst <= 4e-7 * kTaps)) {
        ++g_wrong;
    }
}

constexpr Graph kBright = rewrite::rewritten(brightness(true), zen2);
constexpr planned::Held kBrightNest{planned::program_of(kBright, zen2)};
constexpr Graph kWrap = rewrite::rewritten(subtracted(0), zen2);
constexpr Graph kMinus = rewrite::rewritten(subtracted(1), zen2);
constexpr Graph kAffine = rewrite::rewritten(subtracted(2), zen2);
constexpr Graph kMinuend = rewrite::rewritten(subtracted(3), zen2);
static_assert(count(kWrap, Algebra::ProductAdd) == 1 && count(kMinus, Algebra::ProductAdd) == 1 && count(kAffine, Algebra::ProductAdd) == 1 &&
                  count(kMinuend, Algebra::ProductAdd) == 1,
              "`compile` fuses the subtractions");
constexpr planned::Held kWrapNest{planned::program_of(kWrap, zen2)};
constexpr planned::Held kMinusNest{planned::program_of(kMinus, zen2)};
constexpr planned::Held kAffineNest{planned::program_of(kAffine, zen2)};
constexpr planned::Held kMinuendNest{planned::program_of(kMinuend, zen2)};
constexpr Graph kParamFactor = rewrite::rewritten(subtracted(4), zen2);
static_assert(count(kParamFactor, Algebra::ProductAdd) == 1, "`compile` fuses c - k*a");
constexpr planned::Held kParamFactorNest{planned::program_of(kParamFactor, zen2)};

} // namespace

int main() {
    check<kBrightNest.program>("brightness, fused", brightness(true), {ramp(kSamples, 1.0f)}, kSamples, 1.2);
    // (each input its own ramp)
    const auto ramps = [](const Graph& g) {
        std::vector<std::vector<float>> feeds{};
        for (std::size_t k = 0; k < g.inputs.size(); ++k) {
            const bool param = g.inputs[k].name.view()[0] == 'k'; // (the Param: one number)
            feeds.push_back(ramp(param ? 1 : kSamples, param ? -0.7f : 1.0f - 0.25f * static_cast<float>(k)));
        }
        return feeds;
    };
    check<kWrapNest.program>("c - 2pi*a, fused", subtracted(0), ramps(subtracted(0)), kSamples, 6.3);
    check<kMinusNest.program>("c - a*b, fused", subtracted(1), ramps(subtracted(1)), kSamples, 1.0);
    check<kAffineNest.program>("1.2a - 0.05, fused", subtracted(2), ramps(subtracted(2)), kSamples, 1.2);
    check<kMinuendNest.program>("a*b - c, fused", subtracted(3), ramps(subtracted(3)), kSamples, 1.0);
    check<kParamFactorNest.program>("c - k*a, k a Param, fused", subtracted(4), ramps(subtracted(4)), kSamples, 1.0);
    check<kConv.program>("conv, its sum fused", conv(true), {ramp(kSamples, 1.0f), ramp(kTaps, 0.5f)}, kSamples - kTaps + 1, kTaps * 0.5);
    if (g_wrong != 0) {
        std::printf("rewrite_product_adds_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("rewrite_product_adds_test: adds and subtractions of products fused where granted, sums of products too, nothing else moved\n");
    return 0;
}
