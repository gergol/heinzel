// Adversarial review of the loop-nest RUNNER's compile-time expansion (4fedc6a6), of a fold opened at its first
// element (`Registers::opens_first`, 485ccb00) and of the streamed-axis refusal (a1598c6b).
//
//   A  RAGGED TAILS WITH STATEMENTS AFTER THE FOLD. einsum + bias{1,N} + relu at extents that leave a
//      remainder in every loop (7x13x5, 129x257x63): the lane tail, the unrolled-row remainder and the
//      after-statements meet in one run, which no other test puts together. Against a float64
//      reference within the bound a float sum of K terms may differ by. Feeds are allocated EXACTLY at
//      size, so a sanitizer sees a read one past the end.
//
//   B  `opens_first` AGAINST THE REFERENCE WALK, BIT FOR BIT. A fold whose loops are all Serial owes the
//      walk's own answer, so NaN, -0.0 and +-inf have to come out of it unchanged — through a fold over
//      TWO axes (where "the first element" is the first iteration of the OUTERMOST fold loop only),
//      through a fold with a producer map inside it, and for Max/Min/Sum/Prod. Rule: the same bits, or
//      both NaN.
//
//   C  AN AXIS THE OPERAND DOES NOT HAVE. `Axes{-2}` (what a numpy-minded caller writes for "the
//      second-to-last axis", where `-1` IS accepted as the innermost) and `Axes{3}` on a rank-3 operand
//      are refused by the reference walk by name — and by the planner (`folds_a_missing_axis`), which
//      used to plan a stage with no fold loop at all and hand back a copy of the operand.
//
//   D  A STRICT FOLD IS NEVER CHUNKED OR PUT IN LANES. A fold that opens at its first element opens again
//      at the start of every chunk, so a strict fold that resumed from a stored partial would throw it
//      away. The lowering refuses both shapes by name; this pins that.

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "interpret.hpp"
#include "planned.hpp"
#include "tree_facts.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <expected>
#include <limits>
#include <memory>
#include <string_view>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

int g_wrong = 0;

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_runner_review_test: a graph did not resolve");
    }
    return g;
}

template <class T>
std::unique_ptr<T[]> noise(std::size_t n, std::uint32_t seed) {
    std::unique_ptr<T[]> v(new T[n]);
    std::uint32_t state = seed;
    for (std::size_t i = 0; i < n; ++i) {
        state = state * 1664525u + 1013904223u;
        v[i] = static_cast<T>(static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.00173f);
    }
    return v;
}

// ================================ A — ragged tails with after-statements =========================

template <int M, int K, int N>
constexpr Graph linear_relu() {
    Input x{"x", {M, K}};
    Input w{"w", {K, N}};
    Input b{"b", {1, N}};
    return analysed(finish(Output{"y"} = max(einsum("ik,kj->ij", x, w) + b, 0.0)));
}
template <int M, int K, int N>
inline constexpr planned::Held kLinReluHeld{planned::program_of(linear_relu<M, K, N>(), zen2)};
template <int M, int K, int N>
inline constexpr const loops::Program& kLinRelu = kLinReluHeld<M, K, N>.program;

// The after-statements really are after the fold, and the fold really is one: otherwise this test
// would pass without covering anything.
static_assert(kLinRelu<7, 13, 5>.stages[kLinRelu<7, 13, 5>.stages.size() - 1].count >= 5,
              "the bias and the relu ride in the contraction's stage");

template <int M, int K, int N>
void check_linear_relu(const char* what) {
    constexpr const loops::Program& P = kLinRelu<M, K, N>;
    auto x = noise<float>(static_cast<std::size_t>(M) * K, 11);
    auto w = noise<float>(static_cast<std::size_t>(K) * N, 12);
    auto b = noise<float>(static_cast<std::size_t>(N), 13);
    canaried::Workspace<P> work(loops::Sizes{});
    canaried::run<P>(loops::Feeds{x.get(), w.get(), b.get()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            double sum = 0.0;
            double magnitude = 0.0;
            for (int k = 0; k < K; ++k) {
                const double term = static_cast<double>(x[static_cast<std::size_t>(i) * K + k]) * w[static_cast<std::size_t>(k) * N + j];
                sum += term;
                magnitude += std::fabs(term);
            }
            sum += static_cast<double>(b[static_cast<std::size_t>(j)]);
            magnitude += std::fabs(static_cast<double>(b[static_cast<std::size_t>(j)]));
            const double want = sum < 0.0 ? 0.0 : sum;
            const double got = static_cast<double>(y[static_cast<std::size_t>(i) * N + j]);
            if (!(std::fabs(got - want) <= (K + 1) * 1.2e-7 * magnitude) && ++g_wrong <= 20) {
                std::printf("  FAILING (A) %s at (%d,%d): got %.9g, want %.9g\n", what, i, j, got, want);
            }
        }
    }
}

// ================================ B — opens_first against the walk ===============================

constexpr std::size_t kBlock = 4;

bool same_bits(float a, float b) {
    if (std::isnan(a) && std::isnan(b)) {
        return true;
    }
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::memcpy(&x, &a, 4);
    std::memcpy(&y, &b, 4);
    return x == y;
}

// x{A, B, C, T} folded over the two LEADING axes — two fold loops, so "the first element" is the first
// iteration of the OUTERMOST one, the only moment the fold may open at (each peels its first iteration). C is left
// free and narrower than a lane, which is what keeps every loop serial — and so the fold owed the
// walk's own bits.
constexpr int kA = 3;
constexpr int kB = 5;
constexpr int kC = 4;
template <ReduceOp Op, bool Inside>
constexpr Graph both_axes_folded() {
    Input x{"x", Signal{kA, kB, kC, static_cast<std::int32_t>(kBlock)}};
    // `* 1.0` is a producer map INSIDE the fold that moves no number: -0.0, the infinities and every
    // NaN come through it unchanged, so the comparison with the walk stays a comparison of the fold.
    Expr folded = Inside ? x * 1.0 : x;
    return analysed(finish(Output{"y"} = detail::reduce(folded, Op, Axes{0, 1})));
}
template <ReduceOp Op, bool Inside>
inline constexpr planned::Held kBothHeld{planned::program_of(both_axes_folded<Op, Inside>(), zen2)};
template <ReduceOp Op, bool Inside>
inline constexpr const loops::Program& kBoth = kBothHeld<Op, Inside>.program;

constexpr bool is_a_fold(const loops::Node& n) { return n.kind == loops::NodeKind::Registers && n.role == loops::Role::Accumulator; }

// One serial loop per folded axis, and the fold is owed its first element: if the schedule ever stops
// being that, this test is comparing something else and says so here rather than silently passing.
template <const loops::Program& P>
constexpr bool serial_and_opens_first() {
    const std::size_t last = P.stages.size() - 1;
    std::int32_t fold_loops = 0; // (the fold's own loops stand under its Registers node)
    for (std::int32_t n = P.stages[last].root + 1; n < tree_facts::subtree_end(P, last); ++n) {
        const loops::Node& node = loops::node_at(P, n);
        const bool walks = node.kind == loops::NodeKind::Loop || node.kind == loops::NodeKind::Unroll || node.kind == loops::NodeKind::Vector;
        if ((walks && (node.kind != loops::NodeKind::Loop || node.step != 1 || node.tail != loops::Tail::Clip)) || (is_a_fold(node) && !node.opens_first)) {
            return false;
        }
        fold_loops += walks && loops::ancestor(P, n, is_a_fold) >= 0 ? 1 : 0;
    }
    return fold_loops == 2;
}
static_assert(serial_and_opens_first<kBoth<ReduceOp::Max, false>>(),
              "the two-axis fold is two serial loops opened at their first element");
static_assert(serial_and_opens_first<kBoth<ReduceOp::Max, true>>(),
              "a producer map inside it changes neither");

template <ReduceOp Op, bool Inside>
void check_against_the_walk(const char* what, const float* data, std::size_t n) {
    constexpr const loops::Program& P = kBoth<Op, Inside>;
    const std::size_t w = static_cast<std::size_t>(kA) * kB * kC;
    std::unique_ptr<float[]> x(new float[w * kBlock]);
    std::array<double, 512> xd{};
    for (std::size_t i = 0; i < w * kBlock; ++i) {
        x[i] = data[i % n];
        xd[i] = static_cast<double>(x[i]);
    }
    canaried::Workspace<P> work(loops::Sizes{});
    canaried::run<P>(loops::Feeds{x.get()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));

    Inputs feeds{};
    feeds.push_back(InputFeed{xd.data(), static_cast<std::int32_t>(w)});
    Signals<kBlock> out{};
    const Graph g = both_axes_folded<Op, Inside>();
    run(g, feeds, out);
    for (std::size_t t = 0; t < kBlock; ++t) {
        for (std::size_t e = 0; e < static_cast<std::size_t>(kC); ++e) {
            const float want = static_cast<float>(sample_at(out, g.outputs[0].value, static_cast<std::int32_t>(e), t));
            if (!same_bits(y[t * kC + e], want) && ++g_wrong <= 20) {
                std::printf("  FAILING (B) %s%s t=%zu e=%zu: nest %a, the walk %a\n", what, Inside ? " (map inside)" : "", t, e,
                            static_cast<double>(y[t * kC + e]), static_cast<double>(want));
            }
        }
    }
}

// ================================ C — an axis the operand does not have ==========================

Graph folds_a_missing_axis(std::int32_t axis) {
    Input x{"x", Signal{2, 9, static_cast<std::int32_t>(kBlock)}};
    Graph g = finish(Output{"y"} = x.sum(axis));
    if (!infer(g).ok) {
        fail("nest_runner_review_test: the missing-axis graph did not resolve");
    }
    return g;
}

template <class Run>
bool refuses(const char* who, Run go) {
    try {
        go();
    } catch (const error& e) {
        std::printf("  (%s refuses it: \"%.*s\")\n", who, static_cast<int>(e.message().size()), e.message().data());
        return true;
    }
    return false;
}

void check_missing_axis_is_refused(std::int32_t axis) {
    Graph g{};
    try {
        g = folds_a_missing_axis(axis);
    } catch (const error&) {
        std::printf("  (the builder refuses axis %d)\n", axis); // then there is nothing to review
        return;
    }
    std::array<double, 18 * kBlock> xd{};
    Inputs feeds{};
    feeds.push_back(InputFeed{xd.data(), 18});
    Signals<kBlock> out{};
    const bool walk = refuses("the reference walk", [&] { run(g, feeds, out); });
    const loops::Refusal held = planned::refusal_of(g, zen2); // (planned on the heap: a planned graph has no place on a stack)
    const std::string_view why = held.view();
    if (!why.empty()) {
        std::printf("  (the loop-nest path refuses it: \"%.*s\")\n", static_cast<int>(why.size()), why.data());
    }
    if (walk && why.empty()) {
        ++g_wrong;
        std::printf("  FAILING (C) axis %d: the walk refuses this fold by name, the loop-nest path runs it "
                    "(loops::lowers = 1, and the result keeps the operand's rank %d)\n",
                    axis, g.values[static_cast<std::size_t>(g.outputs[0].value)].shape.rank);
    }
}

// ================================ D — a strict fold is never chunked or laned ====================
//
// A fold that opens at its first element (`opens_first`) makes its first contributor the accumulator. A
// chunked fold opens again at every chunk, so the first contributor of the second chunk would REPLACE the
// partial sum resumed from memory; lanes along a strict fold's own axis would reorder it. The lowering
// refuses both, by name — which is what keeps the runner's opening at the first element correct, so it
// is pinned here.
constexpr int kBig = 1024;
constexpr Graph strict_matmul() {
    Input a{"a", {kBig, kBig}};
    Input b{"b", {kBig, kBig}};
    Graph g = finish(Output{"y"} = einsum("ik,kj->ij", a, b));
    set_default_precision(g, numerics::Precision::High);
    if (!infer(g).ok) {
        fail("nest_runner_review_test: the strict matmul did not resolve");
    }
    return g;
}
constexpr Graph strict_full_fold() {
    Input x{"x", Signal{4096, static_cast<std::int32_t>(kBlock)}};
    Graph g = finish(Output{"y"} = x.sum(0));
    set_default_precision(g, numerics::Precision::High);
    if (!infer(g).ok) {
        fail("nest_runner_review_test: the strict fold did not resolve");
    }
    return g;
}

// What became of a graph's folds: the refusal ("" where it lowers), and of the folds opened at their first element
// how many resume — a `chunks_fold` loop encloses their Registers — and how many run their own axis in lanes — a
// Vector under their Registers over an axis one of their own loops walks.
struct Folds {
    loops::Refusal refused{};
    std::int32_t opening_first{0};
    std::int32_t resumed{0};
    std::int32_t laned{0};
};

constexpr bool lanes_its_own_axis(const loops::Program& p, std::int32_t fold) {
    const auto under = [&p, fold](std::int32_t n) { return n < static_cast<std::int32_t>(p.nodes.size()) && loops::encloses(p, fold, n); };
    for (std::int32_t v = fold + 1; under(v); ++v) {
        for (std::int32_t l = fold + 1; loops::node_at(p, v).kind == loops::NodeKind::Vector && under(l); ++l) {
            if (loops::node_at(p, l).kind == loops::NodeKind::Loop && loops::node_at(p, l).axis == loops::node_at(p, v).axis) {
                return true;
            }
        }
    }
    return false;
}

constexpr Folds folds_of(const Graph& g) {
    const std::expected<loops::Planned, loops::Refusal> r = loops::plan_and_lower(g, zen2);
    const std::expected<void, loops::Refusal> w = r ? loops::well_formed(r->program) : std::unexpected(r.error());
    Folds f{.refused = w ? loops::Refusal{} : w.error()};
    for (std::int32_t n = 0; r && n < static_cast<std::int32_t>(r->program.nodes.size()); ++n) {
        const loops::Node& node = loops::node_at(r->program, n);
        if (!is_a_fold(node) || !node.opens_first) {
            continue;
        }
        f.opening_first += 1;
        f.resumed += loops::ancestor(r->program, n, [](const loops::Node& a) { return a.kind == loops::NodeKind::Loop && a.chunks_fold; }) >= 0 ? 1 : 0;
        f.laned += lanes_its_own_axis(r->program, n) ? 1 : 0;
    }
    return f;
}
constexpr Folds kStrictMatmul = folds_of(strict_matmul());
constexpr Folds kStrictFullFold = folds_of(strict_full_fold());
static_assert(kStrictMatmul.refused.empty() && kStrictMatmul.opening_first > 0 && kStrictMatmul.resumed == 0 && kStrictMatmul.laned == 0,
              "D a fold opened at its first element is never also chunked, nor in lanes along its own axis (a strict 1024^3 contraction)");
static_assert(kStrictFullFold.refused.empty() && kStrictFullFold.opening_first > 0 && kStrictFullFold.resumed == 0 && kStrictFullFold.laned == 0,
              "D a fold opened at its first element is never also chunked, nor in lanes along its own axis (a strict fold of 4096)");

void report(const char* what, const Folds& f) {
    if (!f.refused.empty()) {
        std::printf("  (%s is refused: \"%.*s\")\n", what, static_cast<int>(f.refused.size()), f.refused.data());
        return;
    }
    std::printf("  (%s lowers: %d fold(s) opened at their first element, %d of them chunked, %d in lanes along their own axis)\n",
                what, f.opening_first, f.resumed, f.laned);
}

} // namespace

int main() {
    std::printf("A — a ragged contraction with a bias and a relu after it\n");
    check_linear_relu<7, 13, 5>("7x13x5");
    check_linear_relu<129, 257, 63>("129x257x63");
    check_linear_relu<1, 1, 1>("1x1x1");
    check_linear_relu<33, 5, 17>("33x5x17");

    std::printf("B — a two-axis fold against the reference walk, bit for bit\n");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float leading_nan[] = {nan, 1.0f, -2.0f, 3.0f, -4.0f, 5.0f, -6.0f};
    const float negative_zeros[] = {-0.0f, -0.0f, -0.0f, -0.0f, -0.0f};
    const float infinities[] = {inf, -inf, 1.0f, -inf, inf, 0.0f, -0.0f};
    const float mixed[] = {-0.0f, nan, inf, -inf, 0.0f, 2.0f, -2.0f};
    check_against_the_walk<ReduceOp::Max, false>("max, a leading NaN", leading_nan, 7);
    check_against_the_walk<ReduceOp::Max, true>("max, a leading NaN", leading_nan, 7);
    check_against_the_walk<ReduceOp::Min, false>("min, a leading NaN", leading_nan, 7);
    check_against_the_walk<ReduceOp::Max, false>("max of infinities", infinities, 7);
    check_against_the_walk<ReduceOp::Min, true>("min of infinities", infinities, 7);
    check_against_the_walk<ReduceOp::Sum, false>("sum of negative zeros", negative_zeros, 5);
    check_against_the_walk<ReduceOp::Sum, true>("sum of negative zeros", negative_zeros, 5);
    check_against_the_walk<ReduceOp::Prod, false>("product of negative zeros", negative_zeros, 5);
    check_against_the_walk<ReduceOp::Max, false>("max of a mixed block", mixed, 7);
    check_against_the_walk<ReduceOp::Min, false>("min of a mixed block", mixed, 7);
    check_against_the_walk<ReduceOp::Sum, false>("sum of a mixed block", mixed, 7);

    std::printf("C — a fold over an axis the operand does not have\n");
    check_missing_axis_is_refused(-2);
    check_missing_axis_is_refused(-3);
    check_missing_axis_is_refused(3);

    std::printf("D — a strict fold is never chunked, and never runs its own axis in lanes\n");
    report("a strict 1024^3 contraction", kStrictMatmul);
    report("a strict fold of 4096 numbers", kStrictFullFold);

    std::printf("%s: %d wrong\n", g_wrong ? "FAILED" : "ok", g_wrong);
    return g_wrong ? 1 : 0;
}
