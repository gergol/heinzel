// What the parallel form (Plan/23 3.7, `rewrite_parallel_form_test.cpp`) declines since its review (2026-10-09):
//
//   amplifying     a chain whose partial fractions amplify past `kMostAmplification`: a 16th-order Butterworth
//                  low-pass at 50 Hz (2.2e3); the review's chains of eight sections at one frequency, Qs 0.50-0.67,
//                  low-passes at 50, 10 and 2 Hz and a high-pass at 20 Hz (4.3e7-6.4e7, or a pole on the unit
//                  circle), and four low-passes at 30.0-30.3 Hz (1.8e7), whose banks were 9.9e-6 to 2.5e-2 from long
//                  double; the program of the first of the review's, as far from double-double as the double cascade
//                  (its output in float and wholly in double)
//   in double      a chain whose result is double stays a cascade: an 8th-order Butterworth high-pass at 30 Hz, its
//                  bank 15 times the double cascade's error fed noise (148 fed a chirp); the same chain over float
//                  folds; the program in double as far from double-double as the double cascade
//   out of range   a coefficient that would overflow the expansion or a section's linear form compiles, declined or
//                  folded (a2 = 1e-160 and 1e-20 before a section; eight sections after one at a2 = 1e-12; b0 = 1e200;
//                  1e300 (1e300 x))
//   in linear time a value read twice is a signal of its own: `t = t + t` 32 times, then a section, is decided at once
//                  (the paths through it doubled at every step); a chain over such a `t` still folds
//   High inside    a value pinned `High` inside a `Normal` section keeps that section as written (the review's G1);
//                  in a chain of three, the two `Normal` sections fold and the third stays

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/sema/analyze.hpp"

#include "double_double.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace heinzel;

namespace {

constexpr std::int32_t kBlock = 4096;
constexpr Index n{};
constexpr double f(float v) { return static_cast<double>(v); }
using numerics::Precision;

struct Section {
    // y = b0 x + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
};

// KFR's EQ (`benchmarks/iir_arms.hpp`).
constexpr std::array<Section, 3> kEq3{{
    {0x1.fc38e2p-1f, -0x1.fc38e2p+0f, 0x1.fc38e2p-1f, -0x1.fc355p+0f, 0x1.f878e4p-1f},
    {0x1.0b4082p+0f, -0x1.e533bcp+0f, 0x1.bc4618p-1f, -0x1.e533bcp+0f, 0x1.d2c71cp-1f},
    {0x1.3d86bap-3f, 0x1.3d86bap-2f, 0x1.3d86bap-3f, -0x1.3d86bap-1f, 0x1.ec35d2p-3f},
}};

// RBJ designs at 48 kHz rounded to float (`Plan/probes/audio-iir/parallel_conditioning.cpp`): Butterworth designs, a
// high-pass of order 8 at 30 Hz and a low-pass of order 16 at 50 Hz, Q_k = 1 / (2 cos(pi (2k + 1) / 2 order)); the
// review's eight sections at one frequency with Q_k = 1 / (2 cos(pi (2k + 1) / 64)); four low-passes at 30.0, 30.1,
// 30.2 and 30.3 Hz, Q 0.7071.
constexpr std::array<Section, 4> kHighPass8At30{{
    {0x1.fe089cp-1f, -0x1.fe089cp+0f, 0x1.fe089cp-1f, -0x1.fe081ap+0f, 0x1.fc1238p-1f},
    {0x1.fe54eap-1f, -0x1.fe54eap+0f, 0x1.fe54eap-1f, -0x1.fe546ap+0f, 0x1.fcaad8p-1f},
    {0x1.fee228p-1f, -0x1.fee228p+0f, 0x1.fee228p-1f, -0x1.fee1a8p+0f, 0x1.fdc552p-1f},
    {0x1.ff9b28p-1f, -0x1.ff9b28p+0f, 0x1.ff9b28p-1f, -0x1.ff9aa6p+0f, 0x1.ff3752p-1f},
}};
constexpr std::array<Section, 8> kButterworth16At50{{
    {0x1.6503cp-17f, 0x1.6503cp-16f, 0x1.6503cp-17f, -0x1.fcad02p+0f, 0x1.f95f98p-1f},
    {0x1.651a7cp-17f, 0x1.651a7cp-16f, 0x1.651a7cp-17f, -0x1.fccd66p+0f, 0x1.f9a062p-1f},
    {0x1.65471ep-17f, 0x1.65471ep-16f, 0x1.65471ep-17f, -0x1.fd0cfcp+0f, 0x1.fa1f8ep-1f},
    {0x1.6587fcp-17f, 0x1.6587fcp-16f, 0x1.6587fcp-17f, -0x1.fd696ap+0f, 0x1.fad868p-1f},
    {0x1.65daaep-17f, 0x1.65daaep-16f, 0x1.65daaep-17f, -0x1.fddf3cp+0f, 0x1.fbc41p-1f},
    {0x1.663c1cp-17f, 0x1.663c1cp-16f, 0x1.663c1cp-17f, -0x1.fe6a0ep+0f, 0x1.fcd9b4p-1f},
    {0x1.66a89cp-17f, 0x1.66a89cp-16f, 0x1.66a89cp-17f, -0x1.ff04a6p+0f, 0x1.fe0ee6p-1f},
    {0x1.671c14p-17f, 0x1.671c14p-16f, 0x1.671c14p-17f, -0x1.ffa92ap+0f, 0x1.ff57fp-1f},
}};
constexpr std::array<Section, 8> kReview16At50{{
    {0x1.65019cp-17f, 0x1.65019cp-16f, 0x1.65019cp-17f, -0x1.fca9f4p+0f, 0x1.f9597ap-1f},
    {0x1.650752p-17f, 0x1.650752p-16f, 0x1.650752p-17f, -0x1.fcb218p+0f, 0x1.f969c6p-1f},
    {0x1.6512b4p-17f, 0x1.6512b4p-16f, 0x1.6512b4p-17f, -0x1.fcc25p+0f, 0x1.f98a34p-1f},
    {0x1.6523a4p-17f, 0x1.6523a4p-16f, 0x1.6523a4p-17f, -0x1.fcda72p+0f, 0x1.f9ba78p-1f},
    {0x1.6539fcp-17f, 0x1.6539fcp-16f, 0x1.6539fcp-17f, -0x1.fcfa46p+0f, 0x1.f9fa22p-1f},
    {0x1.655584p-17f, 0x1.655584p-16f, 0x1.655584p-17f, -0x1.fd2182p+0f, 0x1.fa4898p-1f},
    {0x1.6575fep-17f, 0x1.6575fep-16f, 0x1.6575fep-17f, -0x1.fd4fc6p+0f, 0x1.faa522p-1f},
    {0x1.659b18p-17f, 0x1.659b18p-16f, 0x1.659b18p-17f, -0x1.fd84a4p+0f, 0x1.fb0eep-1f},
}};
constexpr std::array<Section, 8> kReviewHighPass16At20{{
    {0x1.fea9fp-1f, -0x1.fea9fp+0f, 0x1.fea9fp-1f, -0x1.fea9b6p+0f, 0x1.fd5452p-1f},
    {0x1.fead38p-1f, -0x1.fead38p+0f, 0x1.fead38p-1f, -0x1.feadp+0f, 0x1.fd5ae4p-1f},
    {0x1.feb3c2p-1f, -0x1.feb3c2p+0f, 0x1.feb3c2p-1f, -0x1.feb388p+0f, 0x1.fd67f6p-1f},
    {0x1.febd7cp-1f, -0x1.febd7cp+0f, 0x1.febd7cp-1f, -0x1.febd42p+0f, 0x1.fd7b6ap-1f},
    {0x1.feca5p-1f, -0x1.feca5p+0f, 0x1.feca5p-1f, -0x1.feca16p+0f, 0x1.fd951p-1f},
    {0x1.feda1cp-1f, -0x1.feda1cp+0f, 0x1.feda1cp-1f, -0x1.fed9e2p+0f, 0x1.fdb4aap-1f},
    {0x1.feecbcp-1f, -0x1.feecbcp+0f, 0x1.feecbcp-1f, -0x1.feec84p+0f, 0x1.fdd9ecp-1f},
    {0x1.ff0204p-1f, -0x1.ff0204p+0f, 0x1.ff0204p-1f, -0x1.ff01cap+0f, 0x1.fe047ap-1f},
}};
constexpr std::array<Section, 8> kReview16At10{{
    {0x1.cb5b34p-22f, 0x1.cb5b34p-21f, 0x1.cb5b34p-22f, -0x1.ff54bep+0f, 0x1.fea9b8p-1f},
    {0x1.cb5caep-22f, 0x1.cb5caep-21f, 0x1.cb5caep-22f, -0x1.ff5664p+0f, 0x1.fead02p-1f},
    {0x1.cb5fap-22f, 0x1.cb5fap-21f, 0x1.cb5fap-22f, -0x1.ff59aap+0f, 0x1.feb39p-1f},
    {0x1.cb6402p-22f, 0x1.cb6402p-21f, 0x1.cb6402p-22f, -0x1.ff5e8cp+0f, 0x1.febd5p-1f},
    {0x1.cb69c8p-22f, 0x1.cb69c8p-21f, 0x1.cb69c8p-22f, -0x1.ff64f8p+0f, 0x1.feca2ap-1f},
    {0x1.cb70e4p-22f, 0x1.cb70e4p-21f, 0x1.cb70e4p-22f, -0x1.ff6ce4p+0f, 0x1.feda02p-1f},
    {0x1.cb7948p-22f, 0x1.cb7948p-21f, 0x1.cb7948p-22f, -0x1.ff763ap+0f, 0x1.feecacp-1f},
    {0x1.cb82dcp-22f, 0x1.cb82dcp-21f, 0x1.cb82dcp-22f, -0x1.ff80e2p+0f, 0x1.ff01fep-1f},
}};
constexpr std::array<Section, 8> kReview16At2{{
    {0x1.264ba4p-26f, 0x1.264ba4p-25f, 0x1.264ba4p-26f, -0x1.ffddbcp+0f, 0x1.ffbb78p-1f},
    {0x1.264bd4p-26f, 0x1.264bd4p-25f, 0x1.264bd4p-26f, -0x1.ffde1p+0f, 0x1.ffbc22p-1f},
    {0x1.264c34p-26f, 0x1.264c34p-25f, 0x1.264c34p-26f, -0x1.ffdeb8p+0f, 0x1.ffbd72p-1f},
    {0x1.264cc4p-26f, 0x1.264cc4p-25f, 0x1.264cc4p-26f, -0x1.ffdfb2p+0f, 0x1.ffbf66p-1f},
    {0x1.264d82p-26f, 0x1.264d82p-25f, 0x1.264d82p-26f, -0x1.ffe0fcp+0f, 0x1.ffc1fap-1f},
    {0x1.264e6cp-26f, 0x1.264e6cp-25f, 0x1.264e6cp-26f, -0x1.ffe292p+0f, 0x1.ffc526p-1f},
    {0x1.264f7ep-26f, 0x1.264f7ep-25f, 0x1.264f7ep-26f, -0x1.ffe47p+0f, 0x1.ffc8e4p-1f},
    {0x1.2650bap-26f, 0x1.2650bap-25f, 0x1.2650bap-26f, -0x1.ffe694p+0f, 0x1.ffcd28p-1f},
}};
constexpr std::array<Section, 4> kFourAt30{{
    {0x1.02024cp-18f, 0x1.02024cp-17f, 0x1.02024cp-18f, -0x1.fe940ap+0f, 0x1.fd2a16p-1f},
    {0x1.03bacp-18f, 0x1.03bacp-17f, 0x1.03bacp-18f, -0x1.fe92d2p+0f, 0x1.fd27acp-1f},
    {0x1.0574acp-18f, 0x1.0574acp-17f, 0x1.0574acp-18f, -0x1.fe919cp+0f, 0x1.fd2544p-1f},
    {0x1.07300ap-18f, 0x1.07300ap-17f, 0x1.07300ap-18f, -0x1.fe9066p+0f, 0x1.fd22dap-1f},
}};

// ---- the graphs ------------------------------------------------------------------------------------------------

constexpr Graph analysed(Graph g) {
    set_default_precision(g, Precision::Normal);
    if (!infer(g).ok) {
        fail("rewrite_parallel_form_declines_test: a graph did not resolve");
    }
    return g;
}

// One section over `x` as `iir_arms::df1` writes it, its `b0 x` pinned `High` where `high`.
constexpr Expr df1(Expr x, const Section& c, bool high = false) {
    Variable y;
    const Expr now = high ? (f(c.b0) * x).precision(Precision::High) : f(c.b0) * x;
    y[n] = (now + f(c.b1) * x[n - 1] + f(c.b2) * x[n - 2] - f(c.a1) * y[n - 1] - f(c.a2) * y[n - 2])
               .precision(Precision::Normal);
    return y;
}

template <std::size_t K>
constexpr Graph cascade(const std::array<Section, K>& c, ElemType type = ElemType::F32) {
    Input x{"x", Signal{kBlock}, type};
    Expr s = x;
    for (const Section& section : c) {
        s = df1(s, section);
    }
    return finish(Output{"y"} = s);
}

constexpr bool declines(const Graph& g) { return rewrite::parallel_form(g, zen2) == g; }
constexpr bool compiles(const Graph& g) { return try_compile(g, zen2).refusal.view().empty(); }

static_assert(declines(analysed(cascade(kButterworth16At50))), "a 16th-order Butterworth at 50 Hz: its fractions amplify 2.2e3");
static_assert(declines(analysed(cascade(kReview16At50))) && declines(analysed(cascade(kReviewHighPass16At20))) &&
                  declines(analysed(cascade(kReview16At10))) && declines(analysed(cascade(kReview16At2))),
              "the review's eight sections at one frequency: 4.3e7-6.4e7, or a pole on the unit circle");
static_assert(declines(analysed(cascade(kFourAt30))), "four low-passes at 30.0-30.3 Hz: 1.8e7");

static_assert(declines(analysed(cascade(kHighPass8At30, ElemType::F64))), "a chain whose result is double stays a cascade");
static_assert(!declines(analysed(cascade(kHighPass8At30))), "the same chain over float folds (it amplifies 34.5)");

// A section with a pole near zero, a2 = `a2`, before another (the review's p7).
constexpr Graph tiny(double a2) {
    Input x{"x", Signal{256}};
    Variable y1;
    Variable y2;
    y1[n] = x + 0.5 * y1[n - 1] - a2 * y1[n - 2];
    y2[n] = 0.2 * y1 + 0.3 * y1[n - 1] + 0.2 * y1[n - 2] + 0.4 * y2[n - 1] - 0.2 * y2[n - 2];
    return finish(Output{"y"} = y2);
}

// Seven sections after one whose a2 is small.
constexpr Graph eight_after_one(float a2) {
    Input x{"x", Signal{256}};
    Expr s = df1(x, Section{1, 0, 0, -0.5f, a2});
    for (int k = 0; k < 7; ++k) {
        s = df1(s, Section{0.2f, 0.3f, 0.2f, -0.4f + 0.05f * static_cast<float>(k), 0.2f + 0.03f * static_cast<float>(k)});
    }
    return finish(Output{"y"} = s);
}

// KFR's EQ, a coefficient of its first section `big` (`twice`: written as `big (big x)` instead of `big x`).
constexpr Graph out_of_range(double big, bool twice) {
    Input x{"x", Signal{256}};
    const Section& c = kEq3[0];
    Variable y;
    y[n] = (twice ? big * (big * x) : big * x) + f(c.b1) * x[n - 1] + f(c.b2) * x[n - 2] - f(c.a1) * y[n - 1] - f(c.a2) * y[n - 2];
    return finish(Output{"y"} = df1(df1(y, kEq3[1]), kEq3[2]));
}

static_assert(compiles(tiny(1e-160)) && compiles(tiny(1e-20)), "a pole near zero: the expansion would divide by it");
static_assert(compiles(eight_after_one(1e-12f)), "a pole near zero before seven sections: their product would overflow");
static_assert(compiles(out_of_range(1e200, false)) && compiles(out_of_range(1e300, true)), "a coefficient past double's range");

// `t = t + t` `D` times, then a section, or (`chain`) two of KFR's.
template <int D>
constexpr Graph doubled(bool chain) {
    Input x{"x", Signal{256}};
    Expr t = x;
    for (int k = 0; k < D; ++k) {
        t = t + t;
    }
    if (chain) {
        return finish(Output{"y"} = df1(df1(t, kEq3[0]), kEq3[1]));
    }
    Variable y;
    y[n] = 1e-6 * t + 0.5 * y[n - 1] - 0.25 * y[n - 2];
    return finish(Output{"y"} = y);
}

static_assert(declines(analysed(doubled<32>(false))), "one section over 2^32 paths: decided in linear time");
static_assert(!declines(analysed(doubled<32>(true))), "a chain over a value read twice folds");

// KFR's EQ, the `high`-th section's `b0 x` pinned `High` (the review's G1 had it in the first of two).
constexpr Graph high_inside(int high) {
    Input x{"x", Signal{kBlock}};
    Expr s = x;
    for (int k = 0; k < 3; ++k) {
        s = df1(s, kEq3[static_cast<std::size_t>(k)], k == high);
    }
    return finish(Output{"y"} = s);
}

// The two Normal sections folded, the High one kept: two recurrences (the bank, the section), a High value.
constexpr bool high_kept(const Graph& g) {
    int cycles = 0;
    bool high = false;
    for (const Node& node : g.nodes) {
        cycles += node.attr.kind == AttrKind::Recurrence ? 1 : 0;
        high = high || g.values[static_cast<std::size_t>(node.output)].precision == Precision::High;
    }
    return cycles == 2 && high;
}

static_assert(declines(analysed(high_inside(1))), "High in the middle section: the chains left of one section each");
static_assert(high_kept(rewrite::parallel_form(analysed(high_inside(2)), zen2)), "High in the last section: the first two fold, the last stays");

// ---- the program of a declined chain ----------------------------------------------------------------------------

std::vector<float> noise(std::size_t count) {
    std::vector<float> v(count);
    std::uint32_t state = 7;
    for (float& x : v) {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.001f;
    }
    return v;
}

// The cascade by hand in `T`, direct form I.
template <class T, std::size_t K>
std::vector<double> by_hand(const std::array<Section, K>& c, const std::vector<float>& x) {
    std::array<T, K> x1{};
    std::array<T, K> x2{};
    std::array<T, K> y1{};
    std::array<T, K> y2{};
    std::vector<double> y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        T u = x[i];
        for (std::size_t k = 0; k < K; ++k) {
            const T o = T(c[k].b0) * u + T(c[k].b1) * x1[k] + T(c[k].b2) * x2[k] - T(c[k].a1) * y1[k] - T(c[k].a2) * y2[k];
            x2[k] = x1[k], x1[k] = u, y2[k] = y1[k], y1[k] = o, u = o;
        }
        y[i] = static_cast<double>(u);
    }
    return y;
}

// The largest error against `want`, relative to its largest magnitude.
double error(const std::vector<double>& got, const std::vector<double>& want) {
    double scale = 0;
    double worst = 0;
    for (std::size_t i = 0; i < want.size(); ++i) {
        scale = std::max(scale, std::fabs(want[i]));
        worst = std::max(worst, std::fabs(got[i] - want[i]));
    }
    return worst / std::max(scale, 1e-300);
}

// `Build`'s Executor over `x`, in the program's type `T`.
template <auto Build, class T>
std::vector<double> executed(const std::vector<float>& x) {
    const auto run = std::make_unique<Executor<Build, zen2>>();
    const std::vector<T> in(x.begin(), x.end());
    std::vector<T> y(x.size());
    run->input("x") = in.data(), run->output("y") = y.data();
    run->run();
    return std::vector<double>(y.begin(), y.end());
}

int g_wrong = 0;

// The program against the cascade in double-double: no further than twice the cascade in double (its output in `T`).
template <auto Build, class T, std::size_t K>
void check_the_program(const char* what, const std::array<Section, K>& c) {
    const std::vector<float> x = noise(kBlock);
    const std::vector<double> exact = by_hand<testing::DoubleDouble>(c, x);
    std::vector<double> in_double = by_hand<double>(c, x);
    for (double& y : in_double) {
        y = static_cast<T>(y);
    }
    const double got = error(executed<Build, T>(x), exact);
    const double cascade = error(in_double, exact);
    std::printf("  %s: the program %.3g, the double cascade %.3g, from double-double\n", what, got, cascade);
    if (!(got <= 2 * cascade)) {
        ++g_wrong;
        std::printf("  wrong: %s\n", what);
    }
}

constexpr Graph review16_at_50() { return cascade(kReview16At50); }
constexpr Graph review16_at_50_in_double() { return cascade(kReview16At50, ElemType::F64); }
constexpr Graph high_pass8_at_30_in_double() { return cascade(kHighPass8At30, ElemType::F64); }

} // namespace

int main() {
    check_the_program<review16_at_50, float>("the review's 16th order at 50 Hz, in float (its bank 9.9e-6)", kReview16At50);
    check_the_program<review16_at_50_in_double, double>("the review's 16th order at 50 Hz, in double (its bank 9.9e-6)", kReview16At50);
    check_the_program<high_pass8_at_30_in_double, double>("a Butterworth high-pass of order 8 at 30 Hz, in double (a cascade now)", kHighPass8At30);
    if (g_wrong != 0) {
        std::printf("rewrite_parallel_form_declines_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("rewrite_parallel_form_declines_test: amplifying chains, double results, coefficients out of range, shared values, High inside\n");
    return 0;
}
