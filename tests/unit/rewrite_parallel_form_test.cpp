// The rewrite layer's third rule (Plan/23 3.7): a linear filter, a chain of second-order sections each reading the one
// before, computed as the sum of its partial fractions: a bank of sections in double, each over the chain's input,
// summed, plus a direct term.
//
//   recognised    KFR's eight-section EQ and `dsp/cascade4` as the benches write them: every section, its coefficients
//                 as written (the first read through `0.996 x`, not as a signal of its own)
//   expanded      the same, KFR's three-section EQ, Butterworth low-passes of order 8 and 10 at 30 Hz and a high-pass
//                 of order 8 at 5 Hz, against `Plan/probes/audio-iir/parallel_lanes.cpp`'s `parallel_of` in long
//                 double, within 1e-12 of the largest number (the high-pass, its poles 1e-4 apart, within 1e-11)
//   the bank      one feedback cycle over [sections, time] in double, its feedback fused; nothing of the chain left;
//                 klang's three sections fold too
//   declined      High; one section; a Linkwitz-Riley pair (its poles repeat); a first-order section; a chain read in
//                 its middle; a bank of voices; a map that is not linear between two sections; the review's cases and a
//                 chain whose result is double in `rewrite_parallel_form_declines_test.cpp`
//   in a loop     a chain inside an enclosing loop (`eq8fb`, Plan/23 10) becomes a bank on the loop where the loop's
//                 chain does not grow: of 2 and 4 sections not, of 6 and 8 it does, as measured on zen2; and where the
//                 planner's sample holds the bank (nine sections on m1_pro, eight with no lanes: the cascade, planned
//                 with the other rewrites, not as written); `eq8fb`'s bank
//                 walked, no further from the loop in long double than the cascade; its program within 5e-7 of the
//                 walk of the graph it runs, whole and in blocks of 256
//   fixed points  the rule twice is the rule once, `infer` agrees, `rewritten` settles
//   numbers       the bank's sum walked, in double before its conversion to float, against the cascade in double,
//                 within 1e-11 of its scale (the 10th order and the high-pass at 5 Hz, which amplify 90 and 24, within
//                 1e-9: well under the float output's rounding); `Executor<…, zen2>` and the walk of the graph it runs
//                 against the cascade in long double, no further than twice the double cascade (its output rounded to
//                 float, as theirs is), whole and in blocks of 256

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/sema/analyze.hpp"

#include "interpret.hpp"
#include "parallel_form_sections.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <memory>
#include <vector>

using namespace heinzel;

namespace {

constexpr std::int32_t kBlock = 4096;
constexpr Index n{};
constexpr double f(float v) { return static_cast<double>(v); }
using numerics::Precision;

// ---- the graphs ------------------------------------------------------------------------------------------------

constexpr Graph analysed(Graph g, Precision level = Precision::Normal) {
    set_default_precision(g, level);
    if (!infer(g).ok) {
        fail("rewrite_parallel_form_test: a graph did not resolve");
    }
    return g;
}

// One section over `x` as `iir_arms::df1` writes it.
constexpr Expr df1(Expr x, const Section& c) {
    Variable y;
    y[n] = f(c.b0) * x + f(c.b1) * x[n - 1] + f(c.b2) * x[n - 2] - f(c.a1) * y[n - 1] - f(c.a2) * y[n - 2];
    return y;
}

// The sections in series over a signal of `length` samples (unanalysed).
template <std::size_t K>
constexpr Graph cascade(const std::array<Section, K>& c, auto length) {
    Input x{"x", Signal{length}};
    Expr s = x;
    for (const Section& section : c) {
        s = df1(s, section);
    }
    return finish(Output{"y"} = s);
}

// `dsp/cascade4` as the bench writes it: no `x[n-2]`, the feedback's signs as they come.
constexpr Graph cascade4() {
    Input x{"x", Signal{kBlock}};
    Variable y1;
    Variable y2;
    Variable y3;
    Variable y4;
    y1[n] = f(0.3f) * x + f(0.2f) * x[n - 1] - f(0.4f) * y1[n - 1] + f(0.1f) * y1[n - 2];
    y2[n] = f(0.5f) * y1 + f(0.1f) * y1[n - 1] + f(0.3f) * y2[n - 1] - f(0.2f) * y2[n - 2];
    y3[n] = f(0.4f) * y2 + f(0.2f) * y2[n - 1] - f(0.1f) * y3[n - 1] + f(0.15f) * y3[n - 2];
    y4[n] = f(0.6f) * y3 + f(0.1f) * y3[n - 1] + f(0.2f) * y4[n - 1] - f(0.1f) * y4[n - 2];
    return finish(Output{"y"} = y4);
}

// Sections inside a loop, their output clipped and fed back (Plan/23 09, `eq8fb`), over a block or a `Length`.
template <std::size_t K>
constexpr Graph in_a_loop(const std::array<Section, K>& c, auto length) {
    Input x{"x", Signal{length}};
    Variable y;
    Expr s = x - 0.6 * clamp(y[n - 1], -0.25, 0.25);
    for (const Section& section : c) {
        s = df1(s, section);
    }
    y[n] = s;
    return finish(Output{"y"} = y);
}
constexpr Graph eq8_in_a_loop(auto length) { return in_a_loop(kEq8, length); }
constexpr Graph eq8fb() { return eq8_in_a_loop(kBlock); }

enum class Decline : std::uint8_t { High, One, LinkwitzRiley, FirstOrder, ReadInTheMiddle, Voices, NotLinearBetween };

constexpr Graph declined(Decline which) {
    if (which == Decline::High) {
        return analysed(cascade(kEq3, kBlock), Precision::High);
    }
    if (which == Decline::One) {
        return analysed(cascade(std::array<Section, 1>{kEq3[0]}, kBlock));
    }
    if (which == Decline::LinkwitzRiley) {
        return analysed(cascade(std::array<Section, 2>{kLinkwitzRiley, kLinkwitzRiley}, kBlock));
    }
    if (which == Decline::Voices) {
        Input x{"x", Signal{4, kBlock}};
        return analysed(finish(Output{"y"} = df1(df1(x, kEq3[0]), kEq3[1])));
    }
    Input x{"x", Signal{kBlock}};
    const Expr first = df1(x, kEq3[0]);
    if (which == Decline::FirstOrder) {
        Variable y;
        y[n] = 0.5 * first + 0.25 * first[n - 1] + 0.5 * y[n - 1];
        return analysed(finish(Output{"y"} = df1(y, kEq3[2])));
    }
    if (which == Decline::ReadInTheMiddle) {
        Output middle{"middle"};
        middle = first;
        return analysed(finish(Output{"y"} = df1(df1(first, kEq3[1]), kEq3[2]), middle));
    }
    return analysed(finish(Output{"y"} = df1(which == Decline::NotLinearBetween ? tanh(first) : first, kEq3[1])));
}

// ---- what the rule sees and makes ---------------------------------------------------------------------------

// The sections the rule finds in `g`, in graph order, against `c` as written: each coefficient exactly.
template <std::size_t K>
constexpr bool recognised(const Graph& g, const std::array<Section, K>& c) {
    const rewrite::detail::Lookup at = rewrite::detail::lookup_of(g);
    std::size_t k = 0;
    bool same = true;
    for (std::size_t r = 0; r < g.nodes.size(); ++r) {
        if (g.nodes[r].attr.kind != AttrKind::Recurrence) {
            continue;
        }
        const rewrite::detail::Section s = rewrite::detail::section_of(g, at, r);
        same = same && s.valid && k < K && s.b[0] == f(c[k].b0) && s.b[1] == f(c[k].b1) && s.b[2] == f(c[k].b2) && s.a[1] == f(c[k].a1) &&
               s.a[2] == f(c[k].a2);
        ++k;
    }
    return same && k == K;
}

// The bank's numbers as the rule computes them (in double, at compile time); `ok` false where it declines.
template <std::size_t K>
struct Expanded {
    std::array<double, K> c0{};
    std::array<double, K> c1{};
    std::array<double, K> a1{};
    std::array<double, K> a2{};
    double direct{0};
    bool ok{false};
};
template <std::size_t K>
constexpr Expanded<K> expanded(const Graph& g) {
    namespace d = rewrite::detail;
    const d::Lookup at = d::lookup_of(g);
    std::vector<d::Section> sections{};
    for (std::size_t r = 0; r < g.nodes.size(); ++r) {
        if (g.nodes[r].attr.kind == AttrKind::Recurrence) {
            sections.push_back(d::section_of(g, at, r));
        }
    }
    const std::vector<std::vector<std::size_t>> chains = d::chains_of(sections);
    Expanded<K> out{};
    const std::optional<d::Expansion> e = chains.size() == 1 && chains[0].size() == K ? d::expansion_of(sections, chains[0]) : std::nullopt;
    for (std::size_t k = 0; e && k < K; ++k) {
        out.c0[k] = e->c0[k], out.c1[k] = e->c1[k], out.a1[k] = -e->a1[k], out.a2[k] = -e->a2[k];
    }
    out.direct = e ? e->direct : 0;
    out.ok = e.has_value();
    return out;
}

// One recurrence over [K, time] in double, fed back through a multiply-add once `rewritten`; nothing of the chain left.
constexpr bool one_bank(const Graph& g, std::int32_t sections) {
    int cycles = 0;
    int fused = 0;
    bool wide = true;
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const loops::graph::Feedback feedback = loops::graph::find_feedback(g, facts);
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const Node& node = g.nodes[i];
        if (node.attr.kind == AttrKind::Recurrence) {
            const Value& v = g.values[static_cast<std::size_t>(node.attr.recurrence.defines)];
            wide = wide && v.type == ElemType::F64 && v.shape.rank == 2 && extent_at(v.shape, 0) == literal_extent(sections);
            ++cycles;
        }
        // (in the cycle: the bank's output, a sum of products outside it, is fused too, `rewrite/product_adds.hpp`)
        fused += rules_of(node.op).algebra == Algebra::ProductAdd && feedback.cycle[i] >= 0 ? 1 : 0;
    }
    return cycles == 1 && wide && fused == 1;
}

constexpr bool settled(const Graph& g) {
    const Graph once = rewrite::parallel_form(g, zen2);
    Graph again = once;
    const Graph rewritten = rewrite::rewritten(g, zen2);
    return infer(again).ok && again == once && rewrite::parallel_form(once, zen2) == once && rewrite::rewritten(rewritten, zen2) == rewritten;
}

constexpr Graph kEq8Graph = analysed(cascade(kEq8, kBlock));
constexpr Graph kCascade4Graph = analysed(cascade4());

static_assert(recognised(kEq8Graph, kEq8), "KFR's graphic EQ: eight sections, the first read through its `0.996 x` as well");
static_assert(recognised(kCascade4Graph, kCascade4), "dsp/cascade4: four sections, no x[n-2], the feedback's signs as written");
static_assert(one_bank(rewrite::rewritten(kEq8Graph, zen2), 8) && one_bank(rewrite::rewritten(kCascade4Graph, zen2), 4),
              "a bank of eight (four) sections in double, its newest state fused");
static_assert(one_bank(rewrite::rewritten(analysed(cascade(kEq3, kBlock), Precision::Fast), zen2), 3), "Fast grants the reordering too");
static_assert(one_bank(rewrite::rewritten(analysed(cascade(kKlang3, kBlock)), zen2), 3), "klang's three sections");
static_assert(settled(kEq8Graph) && settled(kCascade4Graph), "a bank is no chain: a second pass finds nothing, and infer agrees");

constexpr bool declines(Decline which) { return rewrite::parallel_form(declined(which), zen2) == declined(which); }
static_assert(declines(Decline::High), "High keeps every rounding");
static_assert(declines(Decline::One), "one section is its own parallel form");
static_assert(declines(Decline::LinkwitzRiley), "two equal sections: repeated poles have no partial fractions");
static_assert(declines(Decline::FirstOrder), "a first-order section breaks the chain into chains of one");
static_assert(declines(Decline::ReadInTheMiddle), "a section read outside its chain stays");
static_assert(declines(Decline::Voices), "a bank of voices runs in lanes already");
static_assert(declines(Decline::NotLinearBetween), "a map that is not linear between two sections ends the chain");
// The loop with its chain of `sections` a bank: two cycles, the loop's over one voice and the bank's in double; or, kept a
// cascade, a cycle of one voice for each section and the loop's.
constexpr bool a_bank_in_the_loop(const Graph& g, std::int32_t sections) {
    int banks = 0;
    int voices = 0;
    for (const Node& node : g.nodes) {
        const Value* v = node.attr.kind == AttrKind::Recurrence ? &g.values[static_cast<std::size_t>(node.attr.recurrence.defines)] : nullptr;
        banks += v != nullptr && v->type == ElemType::F64 && v->shape.rank == 2 && extent_at(v->shape, 0) == literal_extent(sections) ? 1 : 0;
        voices += v != nullptr && v->shape.rank == 1 ? 1 : 0;
    }
    return banks == 1 && voices == 1;
}
constexpr bool a_cascade_in_the_loop(const Graph& g, std::int32_t sections) {
    int voices = 0;
    for (const Node& node : g.nodes) {
        voices += node.attr.kind == AttrKind::Recurrence && g.values[static_cast<std::size_t>(node.attr.recurrence.defines)].shape.rank == 1 ? 1 : 0;
    }
    return voices == sections + 1;
}
static_assert(a_bank_in_the_loop(rewrite::rewritten(analysed(eq8fb()), zen2), 8),
              "a chain inside an enclosing loop: a bank, its sum on the loop, which a feedback stage folds every sample (Plan/23 10)");
// Inside a loop the form is taken only where the loop's chain does not grow (`rewrite/priced.hpp`), double state
// applied. On zen2 the cascade puts 5 cycles a section on it, the bank one multiply-add and the fold across its lanes,
// the loop's clip and `u` in double with it (P4 4.3b: no conversion on the loop): 31 cycles in one lane body, 34 in
// two, against 26 for three sections, 31 for four, 36 for five. So a bank from four sections on (a tie at four), as
// measured (`Plan/probes/audio-iir/cost_corpus17.cpp`, blocks of 256: as a bank, 2 sections 1.38x the cascade's time,
// 3 1.34x, 4 0.99x, 5 1.06x, 6 0.82x, 8 0.67x; five, the miss, is the bank's second body of one lane).
constexpr std::array<Section, 2> kEq3Two{{kEq3[0], kEq3[1]}};
constexpr std::array<Section, 4> kEq8Four{{kEq8[0], kEq8[1], kEq8[2], kEq8[3]}};
constexpr std::array<Section, 6> kEq8Six{{kEq8[0], kEq8[1], kEq8[2], kEq8[3], kEq8[4], kEq8[5]}};
static_assert(a_cascade_in_the_loop(rewrite::rewritten(analysed(in_a_loop(kEq3Two, kBlock)), zen2), 2), "two sections in a loop: the cascade's chain is shorter");
static_assert(a_bank_in_the_loop(rewrite::rewritten(analysed(in_a_loop(kEq8Four, kBlock)), zen2), 4), "four sections in a loop: the chains tie");
static_assert(a_bank_in_the_loop(rewrite::rewritten(analysed(in_a_loop(kEq8Six, kBlock)), zen2), 6), "six sections in a loop: the bank's chain is shorter");
// The bank must fit the sample the planner lays out too (`loops::graph::kMostSampleBodies` lane bodies): nine double
// voices fit zen2's lanes of four, not m1_pro's of two, and a machine of 8-byte vectors has none. There the cascade
// stays, with the other rewrites (the review of P10: the planner refused the bank, and `compile` planned the graph as
// written). The ninth section is the first moved off its poles, as the review's probe made it.
constexpr Section kNinth{kEq8[0].b0 * 1.01f, kEq8[0].b1, kEq8[0].b2, kEq8[0].a1 * 0.999f, kEq8[0].a2};
constexpr std::array<Section, 9> kEq8AndOne{{kEq8[0], kEq8[1], kEq8[2], kEq8[3], kEq8[4], kEq8[5], kEq8[6], kEq8[7], kNinth}};
constexpr Machine kNoLanes{.vector_bytes = 8, .cores = 1, .fma = true};
static_assert(a_bank_in_the_loop(rewrite::rewritten(analysed(in_a_loop(kEq8AndOne, kBlock)), zen2), 9), "nine in a loop: three bodies of four");
static_assert(a_cascade_in_the_loop(rewrite::rewritten(analysed(in_a_loop(kEq8AndOne, kBlock)), m1_pro), 9), "nine: five bodies of two");
static_assert(a_bank_in_the_loop(rewrite::rewritten(analysed(eq8fb()), m1_pro), 8), "eight in a loop: four bodies of two");
static_assert(a_cascade_in_the_loop(rewrite::rewritten(analysed(eq8fb()), kNoLanes), 8), "eight in a loop and no lanes: a cascade");

// ---- the expansion against long double ------------------------------------------------------------------------

using LD = long double;
using CL = std::complex<LD>;

// `parallel_lanes.cpp`'s `parallel_of`: the residues of the chain's poles, paired by section.
template <std::size_t K>
Expanded<K> parallel_of(const std::array<Section, K>& c) {
    std::array<std::array<CL, 2>, K> poles{};
    Expanded<K> out{.direct = 1, .ok = true};
    for (std::size_t k = 0; k < K; ++k) {
        const CL d = std::sqrt(CL(LD(c[k].a1) * c[k].a1 - 4 * LD(c[k].a2)));
        poles[k] = {(-LD(c[k].a1) + d) / LD(2), (-LD(c[k].a1) - d) / LD(2)};
        out.direct *= double(LD(c[k].b2) / LD(c[k].a2));
    }
    for (std::size_t k = 0; k < K; ++k) {
        std::array<CL, 2> r{};
        for (std::size_t i = 0; i < 2; ++i) {
            const CL p = poles[k][i];
            const CL w = LD(1) / p;
            CL num = 1;
            CL rest = 1;
            for (const Section& s : c) {
                num *= LD(s.b0) + LD(s.b1) * w + LD(s.b2) * w * w;
            }
            for (std::size_t j = 0; j < K; ++j) {
                for (std::size_t m = 0; m < 2; ++m) {
                    rest *= j != k || m != i ? LD(1) - poles[j][m] / p : LD(1);
                }
            }
            r[i] = num / rest;
        }
        out.c0[k] = double((r[0] + r[1]).real()), out.c1[k] = double(-(r[0] * poles[k][1] + r[1] * poles[k][0]).real());
        out.a1[k] = f(c[k].a1), out.a2[k] = f(c[k].a2);
    }
    return out;
}

int g_wrong = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  wrong: %s\n", what);
    }
}

// The rule's bank against long double's: each number within `bound` of the largest, the denominators exactly.
template <std::size_t K>
void check_expansion(const char* what, const Expanded<K>& got, const std::array<Section, K>& c, double bound) {
    const Expanded<K> want = parallel_of(c);
    double scale = std::fabs(want.direct);
    double worst = std::fabs(got.direct - want.direct);
    bool same_poles = got.ok;
    for (std::size_t k = 0; k < K; ++k) {
        scale = std::max({scale, std::fabs(want.c0[k]), std::fabs(want.c1[k])});
        worst = std::max({worst, std::fabs(got.c0[k] - want.c0[k]), std::fabs(got.c1[k] - want.c1[k])});
        same_poles = same_poles && got.a1[k] == want.a1[k] && got.a2[k] == want.a2[k];
    }
    std::printf("  %s: the expansion %.3g of its scale %.3g from long double's\n", what, worst / scale, scale);
    check(same_poles && worst <= bound * scale, what);
}

// ---- numbers --------------------------------------------------------------------------------------------------

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

// Graph `g` walked over `x`, in the types it holds: its value `v` (kNoValue: its output).
std::vector<double> walked(const Graph& g, const std::vector<float>& x, std::int32_t v = kNoValue) {
    const std::vector<double> widened(x.begin(), x.end());
    Inputs feeds{};
    feeds.push_back(InputFeed{widened.data(), 1});
    const auto out = std::make_unique<Signals<kBlock>>();
    run(g, feeds, *out);
    std::vector<double> y(kBlock);
    for (std::size_t t = 0; t < static_cast<std::size_t>(kBlock); ++t) {
        y[t] = sample_at(*out, v == kNoValue ? g.outputs[0].value : v, 0, t);
    }
    return y;
}

// The value the bank's conversion to the output's type reads: its sum, in double.
std::int32_t in_double(const Graph& g) {
    for (const Node& node : g.nodes) {
        if (node.output == g.outputs[0].value && node.attr.kind == AttrKind::Cast) {
            return node.inputs[0];
        }
    }
    return kNoValue;
}

// The bank's sum walked in double, before its conversion to float, against the cascade by hand in double, within
// `bound` of its scale. The expansion is exact to double's rounding, but the bank's sum is not the cascade's: an
// 8th-order Butterworth low-pass at 30 Hz, its poles close and near 1, 7.2e-12 from long double against the cascade's
// 1.1e-12 (with its output in float, both 5.1e-8). (Printed beside it: each against the cascade in long double.)
template <std::size_t K>
void check_the_walk(const char* what, const Graph& written, const std::array<Section, K>& c, double bound = 1e-11) {
    const std::vector<float> x = noise(kBlock);
    const Graph bank = rewrite::parallel_form(written, zen2);
    const std::int32_t sum = in_double(bank);
    const std::vector<double> got = sum == kNoValue ? std::vector<double>(x.size()) : walked(bank, x, sum);
    const std::vector<double> exact = by_hand<LD>(c, x);
    const double apart = error(got, by_hand<double>(c, x));
    std::printf("  %s: the bank walked in double %.3g from the cascade in double (from long double: %.3g, the cascade %.3g)\n", what, apart,
                error(got, exact), error(by_hand<double>(c, x), exact));
    check(sum != kNoValue && apart <= bound, what);
}

// `Build`'s Executor over `x`, whole (`Block` 0) or a block at a time, the state carried from block to block.
template <auto Build, std::int32_t Block>
std::vector<double> executed(const std::vector<float>& x) {
    const auto run = std::make_unique<Executor<Build, zen2>>();
    std::vector<float> y(x.size());
    if constexpr (Block == 0) {
        run->input("x") = x.data(), run->output("y") = y.data();
        run->run();
    } else {
        run->prepare(Block);
        for (std::size_t at = 0; at < x.size(); at += static_cast<std::size_t>(Block)) {
            run->input("x") = x.data() + at, run->output("y") = y.data() + at;
            run->run(Block);
        }
    }
    return std::vector<double>(y.begin(), y.end());
}

// The program against the cascade in long double: no further than twice the cascade in double, its output in float.
template <auto Build, std::int32_t Block = 0, std::size_t K>
void check_the_program(const char* what, const std::array<Section, K>& c) {
    const std::vector<float> x = noise(kBlock);
    const std::vector<double> exact = by_hand<LD>(c, x);
    std::vector<double> in_double = by_hand<double>(c, x);
    for (double& y : in_double) {
        y = static_cast<float>(y);
    }
    const double got = error(executed<Build, Block>(x), exact);
    const double cascade = error(in_double, exact);
    const double walk = Block == 0 ? error(walked(rewrite::rewritten(analysed(Build()), zen2), x), exact) : 0;
    std::printf("  %s: the program %.3g, its graph walked %.3g (0: not walked), the double cascade in float %.3g, from long double\n", what, got, walk,
                cascade);
    check(got <= 2 * cascade && walk <= 2 * cascade, what);
}

// `eq8fb`'s loop by hand in `T`, direct form I: u = x - 0.6 clamp(y[n-1], ±0.25), the 0.6 a float as the graph's.
template <class T>
std::vector<double> loop_by_hand(const std::vector<float>& x) {
    std::array<T, 8> x1{};
    std::array<T, 8> x2{};
    std::array<T, 8> y1{};
    std::array<T, 8> y2{};
    T fed = 0;
    std::vector<double> y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        T u = T(x[i]) - T(0.6f) * std::min(std::max(fed, T(-0.25)), T(0.25));
        for (std::size_t k = 0; k < 8; ++k) {
            const Section& c = kEq8[k];
            const T o = T(c.b0) * u + T(c.b1) * x1[k] + T(c.b2) * x2[k] - T(c.a1) * y1[k] - T(c.a2) * y2[k];
            x2[k] = x1[k], x1[k] = u, y2[k] = y1[k], y1[k] = o, u = o;
        }
        y[i] = static_cast<double>(u), fed = u;
    }
    return y;
}

// KFR's graphic EQ in a loop (Plan/23 10): the rewritten graph walked, a bank on the loop, no further from the loop in
// long double than the graph as written walked (a float cascade); the program within `kLoopBound` of the walk of the
// graph it runs, whole and in blocks of 256 (its fold adds the bank's lanes pairwise, the walk in order).
constexpr double kLoopBound = 5e-7; // (measured 1.1e-7)
constexpr Graph eq8fb_blocks() { return eq8_in_a_loop(Length{}); }
void check_the_loop() {
    const std::vector<float> x = noise(kBlock);
    const std::vector<double> exact = loop_by_hand<LD>(x);
    // (On the heap: `compile`'s result and the graph as written, at these capacities, took 1.5 MB of this frame in an
    // unoptimized build, above the rewrite's frames below `compile`; 8 MB of stack did not hold them all.)
    const std::unique_ptr<const Graph> as_written(new auto(analysed(eq8fb())));
    const std::unique_ptr<const Compiled> compiled(new auto(compile(*as_written, zen2)));
    const std::vector<double> bank = walked(compiled->graph, x);
    const std::vector<double> written = walked(*as_written, x);
    const double whole = error(executed<eq8fb, 0>(x), bank);
    const double blocks = error(executed<eq8fb_blocks, 256>(x), bank);
    std::printf("  KFR's graphic EQ in a loop: the bank walked %.3g from long double, the cascade as written %.3g; the program %.3g "
                "from its walk, in blocks of 256 %.3g\n",
                error(bank, exact), error(written, exact), whole, blocks);
    check(error(bank, exact) <= error(written, exact) && whole <= kLoopBound && blocks <= kLoopBound, "KFR's graphic EQ in a loop");
}

// Planned as rewritten, the bank where the sample holds it and the cascade where not: nothing falls back to the graph
// as written.
void check_planned_as_rewritten(const char* what, const Graph& g, const Machine& m) {
    const std::unique_ptr<const CompileResult> r(new auto(try_compile(g, m)));
    const bool ok = r->refusal.view().empty() && r->compiled.graph == rewrite::rewritten(g, m) && !(r->compiled.graph == g);
    std::printf("  %s: planned as rewritten: %s\n", what, ok ? "yes" : "no");
    check(ok, what);
}

constexpr Graph eq3() { return cascade(kEq3, kBlock); }
constexpr Graph eq8() { return cascade(kEq8, kBlock); }
constexpr Graph eq8_blocks() { return cascade(kEq8, Length{}); }
constexpr Graph butterworth8() { return cascade(kButterworth8, kBlock); }
constexpr Graph butterworth10() { return cascade(kButterworth10, kBlock); }
constexpr Graph high_pass_at_5() { return cascade(kHighPassAt5, kBlock); }
constexpr Graph dsp_cascade4() { return cascade4(); }

} // namespace

int main() {
    check_expansion("KFR's EQ", expanded<3>(analysed(eq3())), kEq3, 1e-12);
    check_expansion("KFR's graphic EQ", expanded<8>(kEq8Graph), kEq8, 1e-12);
    check_expansion("dsp/cascade4", expanded<4>(kCascade4Graph), kCascade4, 1e-12);
    check_expansion("Butterworth 8th order at 30 Hz", expanded<4>(analysed(butterworth8())), kButterworth8, 1e-12);
    check_expansion("Butterworth 10th order at 30 Hz", expanded<5>(analysed(butterworth10())), kButterworth10, 1e-12);
    check_expansion("Butterworth high-pass 8th order at 5 Hz", expanded<4>(analysed(high_pass_at_5())), kHighPassAt5, 1e-11);
    check_the_walk("KFR's EQ", analysed(eq3()), kEq3);
    check_the_walk("KFR's graphic EQ", kEq8Graph, kEq8);
    check_the_walk("dsp/cascade4", kCascade4Graph, kCascade4);
    check_the_walk("Butterworth 8th order at 30 Hz", analysed(butterworth8()), kButterworth8);
    check_the_walk("Butterworth 10th order at 30 Hz", analysed(butterworth10()), kButterworth10, 1e-9);
    check_the_walk("Butterworth high-pass 8th order at 5 Hz", analysed(high_pass_at_5()), kHighPassAt5, 1e-9);
    check_the_program<eq3>("KFR's EQ", kEq3);
    check_the_program<eq8>("KFR's graphic EQ", kEq8);
    check_the_program<eq8_blocks, 256>("KFR's graphic EQ in blocks of 256", kEq8);
    check_the_program<dsp_cascade4>("dsp/cascade4", kCascade4);
    check_the_program<butterworth8>("Butterworth 8th order at 30 Hz", kButterworth8);
    check_the_program<butterworth10>("Butterworth 10th order at 30 Hz", kButterworth10);
    check_the_program<high_pass_at_5>("Butterworth high-pass 8th order at 5 Hz", kHighPassAt5);
    check_the_loop();
    check_planned_as_rewritten("nine sections in a loop on m1_pro", analysed(in_a_loop(kEq8AndOne, kBlock)), m1_pro);
    check_planned_as_rewritten("eight sections in a loop on m1_pro", analysed(eq8fb()), m1_pro);
    check_planned_as_rewritten("eight sections in a loop and no lanes", analysed(eq8fb()), kNoLanes);
    if (g_wrong != 0) {
        std::printf("rewrite_parallel_form_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("rewrite_parallel_form_test: linear filters as banks of their partial fractions, in double\n");
    return 0;
}
