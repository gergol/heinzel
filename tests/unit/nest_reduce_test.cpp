// Folds on the loop-nest path, against folds written by hand: every reduction, in lanes (with every
// remainder length) and strictly; the maps that feed a fold fused inside its loops and the maps over its
// result run after them; a folded value read back, broadcast, by a later stage; a fold over an axis
// memory is not contiguous in; and a fold of everything to one number. A max or a min of zeros of both signs,
// the lanes' one way and the rest's the other: the order the two are combined in decides the sign, to the bit
// (a review found the loop tree could swap it unseen).
//
// A fold that keeps its axes, chunked along one it collapses by an edited schedule (the planner chunks only
// contractions), is refused by name by the lowering: each chunk's partial result would be kept one chunk further
// along an axis of one.
//
// A fold in lanes is a REASSOCIATION, so it is held to the bound two orders of one sum can differ by —
// (n-1) * eps * sum|x|, derived from the data — and not to a number picked to pass. A `High` fold is
// owed its strict order and is held to the bits.

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "planned.hpp"
#include "tree_facts.hpp"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr std::int32_t kColumns = 45; // 5 whole lanes of 8, and 5 left over

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_reduce_test: a graph did not resolve");
    }
    return g;
}

// y[t] = fold over the columns of x[c, t], a run-time number of samples.
constexpr Graph columns_folded(ReduceOp op, numerics::Precision level) {
    Input x{"x", Signal{kColumns, Length{.multiple_of = 1, .min = 1, .expect = 64}}};
    Graph g = finish(Output{"y"} = detail::reduce(x, op, Axes{0}));
    set_default_precision(g, level);
    return analysed(g);
}

// y = x * rsqrt(mean(x*x over the columns) + 1e-6): a map fused INTO the fold, two maps after it, and a
// last stage that reads the folded value back, broadcast along the columns.
constexpr std::int32_t kRows = 6;
constexpr std::int32_t kWide = 19;
constexpr Graph normalised() {
    Input x{"x", {kRows, kWide}};
    Expr scale = rsqrt((x * x).mean(1, keepdims) + 1e-6f);
    return analysed(finish(Output{"y"} = x * scale));
}

// A fold over an axis memory is NOT contiguous in (the rows), and a fold of everything.
constexpr Graph rows_folded() {
    Input x{"x", {kRows, kWide}};
    return analysed(finish(Output{"y"} = x.sum(0)));
}
constexpr Graph everything_folded() {
    Input x{"x", {kRows, kWide}};
    return analysed(finish(Output{"y"} = x.max({0, 1})));
}

template <ReduceOp Op, numerics::Precision Level>
constexpr planned::Held kFoldedHeld{planned::program_of(columns_folded(Op, Level), zen2)};
template <ReduceOp Op, numerics::Precision Level>
constexpr const loops::Program& kFolded = kFoldedHeld<Op, Level>.program;
constexpr planned::Held kNormalisedHeld{planned::program_of(normalised(), zen2)};
constexpr const loops::Program& kNormalised = kNormalisedHeld.program;
constexpr planned::Held kRowsFoldedHeld{planned::program_of(rows_folded(), zen2)};
constexpr const loops::Program& kRowsFolded = kRowsFoldedHeld.program;
constexpr planned::Held kEverythingHeld{planned::program_of(everything_folded(), zen2)};
constexpr const loops::Program& kEverything = kEverythingHeld.program;

// For each walking node of stage `s`, in `tree_facts::walks` order: does it stand under the fold's Registers node —
// is it one of the fold's own loops?
constexpr heinzel::fixed_vector<bool, 64> under_the_fold(const loops::Program& p, std::size_t s) {
    heinzel::fixed_vector<bool, 64> out{};
    for (std::int32_t n = p.stages[s].root + 1; n < tree_facts::subtree_end(p, s); ++n) {
        const loops::NodeKind k = loops::node_at(p, n).kind;
        if (k == loops::NodeKind::Loop || k == loops::NodeKind::Unroll || k == loops::NodeKind::Vector) {
            const auto fold = [](const loops::Node& a) { return a.kind == loops::NodeKind::Registers && a.role == loops::Role::Accumulator; };
            out.push_back(loops::ancestor(p, n, fold) >= 0);
        }
    }
    return out;
}

// What was decided, read off the programs. (The samples walked outside the fold's Registers node; under it the
// columns: a strip of four blocks of lanes, an Unroll of the four bodies, a Vector of eight.)
static_assert([] {
    const auto w = tree_facts::walks(kFolded<ReduceOp::Sum, numerics::Precision::Normal>, 0);
    const auto under = under_the_fold(kFolded<ReduceOp::Sum, numerics::Precision::Normal>, 0);
    return w.size() == 4 && tree_facts::serial(w[0]) && w[0].axis == 1 && !under[0] && loops::is_strip(w[1]) && w[1].axis == 0 && w[1].step == 32 && under[1] &&
           w[2].kind == loops::NodeKind::Unroll && w[2].step == 8 && under[2] && w[3].kind == loops::NodeKind::Vector && w[3].step == 8 && under[3];
}(), "samples outside, the columns folded in lanes, four accumulators side by side");
static_assert(tree_facts::walks(kFolded<ReduceOp::Sum, numerics::Precision::High>, 0).size() == 2 &&
                  tree_facts::serial(tree_facts::walk(kFolded<ReduceOp::Sum, numerics::Precision::High>, 0, 1)) &&
                  tree_facts::stmt(kFolded<ReduceOp::Sum, numerics::Precision::High>, 0, 1).strict,
              "a High fold keeps its one order: no lanes, a strict accumulator");
static_assert(kNormalised.stages.size() == 2 && kNormalised.buffers.size() == 3,
              "the square is folded without being stored; only the scale and the result have storage");
static_assert(tree_facts::walks_over(kRowsFolded, 0, 0).size() == 1 && tree_facts::serial(tree_facts::walks_over(kRowsFolded, 0, 0)[0]),
              "a fold along an axis memory is not contiguous in keeps its own loop serial (its lanes, if any, run across its outputs)");

int g_wrong = 0;

std::vector<float> noise(std::size_t n) {
    std::vector<float> v(n);
    std::uint32_t state = 12345;
    for (float& x : v) {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.00173f;
    }
    return v;
}

template <ReduceOp Op, numerics::Precision Level>
void check_columns(std::size_t samples) {
    constexpr const loops::Program& program = kFolded<Op, Level>;
    const std::vector<float> x = noise(samples * kColumns);
    canaried::Workspace<program> work(loops::Sizes{static_cast<std::int64_t>(samples)});
    canaried::run<program>(loops::Feeds{x.data()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(program.outputs[0]));
    for (std::size_t t = 0; t < samples; ++t) {
        double strict = x[t * kColumns];
        double magnitude = std::fabs(strict);
        for (std::size_t c = 1; c < kColumns; ++c) {
            strict = fold_step(Op, strict, static_cast<double>(x[t * kColumns + c]));
            magnitude = Op == ReduceOp::Prod ? std::fabs(strict) : magnitude + std::fabs(x[t * kColumns + c]);
        }
        const float want = static_cast<float>(fold_finish(Op, strict, kColumns));
        const bool exact = Level == numerics::Precision::High || Op == ReduceOp::Max || Op == ReduceOp::Min;
        const double bound = exact ? 0.0 : (kColumns - 1) * static_cast<double>(FLT_EPSILON) * magnitude;
        if (!(std::fabs(static_cast<double>(y[t]) - static_cast<double>(want)) <= bound) && ++g_wrong <= 10) {
            std::printf("  fold %d at level %d, sample %zu: got %.9g, want %.9g, bound %.3g\n", static_cast<int>(Op),
                        static_cast<int>(Level), t, static_cast<double>(y[t]), static_cast<double>(want), bound);
        }
    }
}

// Columns 0-39 go through the lanes (four chunks of 8 into four accumulators, then one sub-chunk), 40-44 one at a
// time into the rest; a step keeps what it has when the two compare equal, so the order of the steps decides the
// sign of a zero. Rows 0 and 1: every accumulator one zero, the rest the other — the order the lanes and the rest
// are combined in. Rows 2 and 3: the first accumulator -0, the three others +0 (and the other way), the rest a
// number the lanes' zero wins over — the order the accumulators are combined in (ties keep the first). The signs are
// pinned — the rest's zero in rows 0 and 1, the first accumulator's in rows 2 and 3, the order the retired runner and
// the loop tree agreed on — so a change of order is seen.
template <ReduceOp Op>
void check_signed_zeros() {
    constexpr const loops::Program& program = kFolded<Op, numerics::Precision::Normal>;
    constexpr float loser = Op == ReduceOp::Max ? -1.0f : 1.0f;
    std::vector<float> x(4 * kColumns);
    for (std::size_t c = 0; c < kColumns; ++c) {
        const bool lanes = c < 40;
        const bool first = c < 8; // (the sub-chunk 32-39 changes no accumulator's sign: a tie keeps it)
        x[c] = lanes ? -0.0f : 0.0f, x[kColumns + c] = lanes ? 0.0f : -0.0f;
        x[2 * kColumns + c] = lanes ? (first ? -0.0f : 0.0f) : loser, x[3 * kColumns + c] = lanes ? (first ? 0.0f : -0.0f) : loser;
    }
    canaried::Workspace<program> work(loops::Sizes{4});
    canaried::run<program>(loops::Feeds{x.data()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(program.outputs[0]));
    for (std::size_t t = 0; t < 4; ++t) {
        const bool negative = t == 1 || t == 2;
        if (!(y[t] == 0.0f && std::signbit(y[t]) == negative) && ++g_wrong <= 10) {
            std::printf("  fold %d of signed zeros, row %zu: got %a, want %s0\n", static_cast<int>(Op), t, static_cast<double>(y[t]), negative ? "-" : "+");
        }
    }
}

void check_normalised() {
    const std::vector<float> x = noise(kRows * kWide);
    canaried::Workspace<kNormalised> work(loops::Sizes{});
    canaried::run<kNormalised>(loops::Feeds{x.data()}, work);
    const float* y = work.data<float>(static_cast<std::size_t>(kNormalised.outputs[0]));
    for (std::size_t r = 0; r < kRows; ++r) {
        double mean = 0.0;
        for (std::size_t k = 0; k < kWide; ++k) {
            mean += static_cast<double>(x[r * kWide + k]) * x[r * kWide + k];
        }
        const double scale = 1.0 / std::sqrt(mean / kWide + 1e-6);
        for (std::size_t k = 0; k < kWide; ++k) {
            const double want = x[r * kWide + k] * scale;
            if (!(std::fabs(y[r * kWide + k] - want) <= 8.0 * FLT_EPSILON * std::fabs(want)) && ++g_wrong <= 10) {
                std::printf("  normalised [%zu,%zu]: got %.9g, want %.9g\n", r, k, static_cast<double>(y[r * kWide + k]), want);
            }
        }
    }
}

void check_rows_and_everything() {
    const std::vector<float> x = noise(kRows * kWide);
    canaried::Workspace<kRowsFolded> rows(loops::Sizes{});
    canaried::Workspace<kEverything> all(loops::Sizes{});
    canaried::run<kRowsFolded>(loops::Feeds{x.data()}, rows);
    canaried::run<kEverything>(loops::Feeds{x.data()}, all);
    const float* sums = rows.data<float>(static_cast<std::size_t>(kRowsFolded.outputs[0]));
    float largest = x[0];
    for (std::size_t k = 0; k < kWide; ++k) {
        double want = 0.0;
        double magnitude = 0.0;
        for (std::size_t r = 0; r < kRows; ++r) {
            want += x[r * kWide + k];
            magnitude += std::fabs(x[r * kWide + k]);
            largest = x[r * kWide + k] > largest ? x[r * kWide + k] : largest;
        }
        if (!(std::fabs(sums[k] - want) <= (kRows - 1) * static_cast<double>(FLT_EPSILON) * magnitude) && ++g_wrong <= 10) {
            std::printf("  rows folded, column %zu: got %.9g, want %.9g\n", k, static_cast<double>(sums[k]), want);
        }
    }
    if (all.data<float>(static_cast<std::size_t>(kEverything.outputs[0]))[0] != largest && ++g_wrong <= 10) {
        std::printf("  everything folded: got %.9g, want %.9g\n",
                    static_cast<double>(all.data<float>(static_cast<std::size_t>(kEverything.outputs[0]))[0]), static_cast<double>(largest));
    }
}

void check_a_kept_fold_is_not_chunked() {
    Input x{"x", Signal{8, 4}};
    const Graph g = analysed(finish(Output{"f"} = x.sum(0, keepdims)));
    using loops::LoopKind;
    using Loop = loops::Schedule::Loop;
    loops::Schedule d = planned::schedule_of(g, zen2);
    loops::Schedule::Stage& fold = d.stages[0];
    fold.loops.clear();
    for (const Loop loop : {Loop{.axis = 1, .step = 1, .kind = LoopKind::Serial}, Loop{.axis = 0, .step = 2, .kind = LoopKind::Tile},
                            Loop{.axis = 0, .step = 1, .kind = LoopKind::Serial}}) {
        fold.loops.push_back(loop);
    }
    fold.inner_from = 2;
    fold.parallel = -1;
    const std::string_view why = "heinzel::compile: a fold that keeps its axes is not chunked along one it collapses";
    const loops::Refusal held = planned::refusal_of(g, d);
    const std::string_view got = held.view();
    if (got.empty()) {
        ++g_wrong;
        std::printf("  a fold that keeps its axes, chunked along its own, lowered\n");
    } else if (got != why && ++g_wrong <= 10) {
        std::printf("  a chunked kept fold: refused as \"%.*s\"\n", static_cast<int>(got.size()), got.data());
    }
}

} // namespace

int main() {
    for (const std::size_t samples : {1u, 5u}) {
        check_columns<ReduceOp::Sum, numerics::Precision::Normal>(samples);
        check_columns<ReduceOp::Mean, numerics::Precision::Normal>(samples);
        check_columns<ReduceOp::Max, numerics::Precision::Normal>(samples);
        check_columns<ReduceOp::Min, numerics::Precision::Normal>(samples);
        check_columns<ReduceOp::Prod, numerics::Precision::Normal>(samples);
        check_columns<ReduceOp::Sum, numerics::Precision::High>(samples);
        check_columns<ReduceOp::Mean, numerics::Precision::High>(samples);
    }
    check_signed_zeros<ReduceOp::Max>();
    check_signed_zeros<ReduceOp::Min>();
    check_normalised();
    check_rows_and_everything();
    check_a_kept_fold_is_not_chunked();
    if (g_wrong != 0) {
        std::printf("%d results are wrong\n", g_wrong);
        return 1;
    }
    return 0;
}
