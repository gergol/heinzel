// Adversarial review of the loop-nest path's fold (V2) and contraction (V3) slices.
//
// Two halves:
//
//   PART A — what held up. Odd shapes through the blocked/tiled/relaid contraction: extents that are
//            prime, one, smaller than a lane and smaller than a relayout block; the lane axis on the
//            first operand, on neither, with extra free axes and with a batch axis; mixed F32/F64
//            through a relaid copy; a resumed fold re-run on a workspace that already holds its last
//            answer. These all pass and are kept as regression coverage.
//
//   PART B — the two divergences from the reference walk (`tests/reference/interpret.hpp`) the review found,
//            kept as the tests of their fixes:
//
//     B1  A fold the schedule reorders nothing in — every loop serial — owes the walk's answer bit for
//         bit, so it opens at its first element (`Registers::opens_first`), not at the identity: a max from
//         -inf loses a leading NaN, and a sum of negative zeros from +0.0 comes out positive.
//
//     B2  A fold over the STREAMED axis folds over time: the answer would be the block's (mean of
//         x[t] = t is 1.5 at N=4 and 31.5 at N=64). The walk refuses it by name; so does the planner.

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/runtime/hardware_twins.hpp"
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
#include <memory>
#include <string_view>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_review_test: a graph did not resolve");
    }
    return g;
}

// zen2, and two machines whose caches (and pages) are small enough to force the Tile loops and the
// fetch-ahead — one on which a relaid copy never pays, one on which it pays at once.
enum class Mach { Zen2, Tiny, Tinier };
constexpr Machine machine_of(Mach m) {
    constexpr Machine tiny{.vector_bytes = 32, .registers = 16, .l1_bytes = 1024, .l2_bytes = 2048, .page_bytes = 256,
                           .relay_pays_after = 1 << 20, .fma = true};
    constexpr Machine tinier{.vector_bytes = 32, .registers = 16, .l1_bytes = 1024, .l2_bytes = 1024, .page_bytes = 256,
                             .relay_pays_after = 1, .fma = true};
    return m == Mach::Zen2 ? zen2 : m == Mach::Tiny ? tiny : tinier;
}

int g_wrong = 0;

// Feeds allocated EXACTLY at size, so a sanitizer sees a read one past the end.
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

// Lanes run ACROSS outputs, so no sum is reordered and the comparison is exact — with the multiply-add
// fused exactly where the program says it is (rounded as the machine rounds, as the program does).
bool is_fused(const loops::Program& p) {
    for (const loops::Stmt& st : p.stmts) { // (stage by stage, in order)
        if (st.kind == loops::StmtKind::Fold) {
            return st.fused;
        }
    }
    return false;
}

template <class T>
void expect(const char* what, std::size_t at, T got, T want) {
    if (!(got == want) && ++g_wrong <= 20) {
        std::printf("  %s [%zu]: got %a, want %a\n", what, at, static_cast<double>(got), static_cast<double>(want));
    }
}

// ================================ PART A — what held up ==========================================

// ---- ik,kj->ij at odd shapes, on every machine ---------------------------------------------------
template <int M, int K, int N>
constexpr Graph plain() {
    Input a{"a", {M, K}};
    Input b{"b", {K, N}};
    return analysed(finish(Output{"y"} = einsum("ik,kj->ij", a, b)));
}
template <int M, int K, int N, Mach Mc>
inline constexpr planned::Held kPlainHeld{planned::program_of(plain<M, K, N>(), machine_of(Mc))};
template <int M, int K, int N, Mach Mc>
inline constexpr const loops::Program& kPlain = kPlainHeld<M, K, N, Mc>.program;

template <int M, int K, int N, Mach Mc>
void check_plain(const char* what) {
    constexpr const loops::Program& P = kPlain<M, K, N, Mc>;
    auto a = noise<float>(static_cast<std::size_t>(M) * K, 45);
    auto b = noise<float>(static_cast<std::size_t>(K) * N, 46);
    canaried::Workspace<P> work(loops::Sizes{});
    canaried::run<P>(loops::Feeds{a.get(), b.get()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    const bool fused = is_fused(P);
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            float sum = 0.0f;
            for (int k = 0; k < K; ++k) {
                sum = fused ? multiply_add_as_the_machine_rounds(a[i * K + k], b[k * N + j], sum) : sum + a[i * K + k] * b[k * N + j];
            }
            expect(what, static_cast<std::size_t>(i * N + j), y[i * N + j], sum);
        }
    }
}

// ---- the lane axis on the FIRST operand (ki,k->i), and on NEITHER at rank two (k,kj->j) ------------
template <int M, int K, int T>
constexpr Graph first_lane() {
    Input a{"a", Signal{K, M, T}};
    Input b{"b", Signal{K, T}};
    return analysed(finish(Output{"y"} = einsum("ki,k->i", a, b)));
}
template <int M, int K, int T, Mach Mc>
inline constexpr planned::Held kFirstHeld{planned::program_of(first_lane<M, K, T>(), machine_of(Mc))};
template <int M, int K, int T, Mach Mc>
inline constexpr const loops::Program& kFirst = kFirstHeld<M, K, T, Mc>.program;

template <int M, int K, int T, Mach Mc>
void check_first_lane(const char* what) {
    constexpr const loops::Program& P = kFirst<M, K, T, Mc>;
    auto a = noise<float>(static_cast<std::size_t>(K) * M * T, 21);
    auto b = noise<float>(static_cast<std::size_t>(K) * T, 22);
    canaried::Workspace<P> work(loops::Sizes{});
    canaried::run<P>(loops::Feeds{a.get(), b.get()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    const bool fused = is_fused(P);
    for (int t = 0; t < T; ++t) {
        for (int i = 0; i < M; ++i) {
            float sum = 0.0f;
            for (int k = 0; k < K; ++k) {
                const float x = a[(t * K + k) * M + i];
                const float z = b[t * K + k];
                sum = fused ? multiply_add_as_the_machine_rounds(x, z, sum) : sum + x * z;
            }
            expect(what, static_cast<std::size_t>(t * M + i), y[t * M + i], sum);
        }
    }
}

// ---- a second free axis on the second operand: ik,kjl->ijl -----------------------------------------
template <int M, int K, int N, int L>
constexpr Graph free2() {
    Input a{"a", {M, K}};
    Input b{"b", {K, N, L}};
    return analysed(finish(Output{"y"} = einsum("ik,kjl->ijl", a, b)));
}
template <int M, int K, int N, int L, Mach Mc>
inline constexpr planned::Held kFree2Held{planned::program_of(free2<M, K, N, L>(), machine_of(Mc))};
template <int M, int K, int N, int L, Mach Mc>
inline constexpr const loops::Program& kFree2 = kFree2Held<M, K, N, L, Mc>.program;

template <int M, int K, int N, int L, Mach Mc>
void check_free2(const char* what) {
    constexpr const loops::Program& P = kFree2<M, K, N, L, Mc>;
    auto a = noise<float>(static_cast<std::size_t>(M) * K, 23);
    auto b = noise<float>(static_cast<std::size_t>(K) * N * L, 24);
    canaried::Workspace<P> work(loops::Sizes{});
    canaried::run<P>(loops::Feeds{a.get(), b.get()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    const bool fused = is_fused(P);
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            for (int l = 0; l < L; ++l) {
                float sum = 0.0f;
                for (int k = 0; k < K; ++k) {
                    const float x = a[i * K + k];
                    const float z = b[(k * N + j) * L + l];
                    sum = fused ? multiply_add_as_the_machine_rounds(x, z, sum) : sum + x * z;
                }
                expect(what, static_cast<std::size_t>((i * N + j) * L + l), y[(i * N + j) * L + l], sum);
            }
        }
    }
}

// ---- mixed F32 x F64 through a relaid copy (the blocked buffer is the NARROW operand) ---------------
template <int M, int K, int N>
constexpr Graph mixed() {
    Input a{"a", {M, K}, ElemType::F64};
    Input b{"b", {K, N}};
    return analysed(finish(Output{"y"} = einsum("ik,kj->ij", a, b)));
}
template <int M, int K, int N, Mach Mc>
inline constexpr planned::Held kMixedHeld{planned::program_of(mixed<M, K, N>(), machine_of(Mc))};
template <int M, int K, int N, Mach Mc>
inline constexpr const loops::Program& kMixed = kMixedHeld<M, K, N, Mc>.program;

template <int M, int K, int N, Mach Mc>
void check_mixed(const char* what) {
    constexpr const loops::Program& P = kMixed<M, K, N, Mc>;
    auto a = noise<double>(static_cast<std::size_t>(M) * K, 43);
    auto b = noise<float>(static_cast<std::size_t>(K) * N, 44);
    canaried::Workspace<P> work(loops::Sizes{});
    canaried::run<P>(loops::Feeds{a.get(), b.get()}, work);
    const double* y = work.template data<double>(static_cast<std::size_t>(P.outputs[0]));
    const bool fused = is_fused(P);
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < N; ++j) {
            double sum = 0.0;
            for (int k = 0; k < K; ++k) {
                const double z = b[k * N + j];
                sum = fused ? multiply_add_as_the_machine_rounds(a[i * K + k], z, sum) : sum + a[i * K + k] * z;
            }
            expect(what, static_cast<std::size_t>(i * N + j), y[i * N + j], sum);
        }
    }
}

// ---- a resumed (chunked) fold run again on a workspace that already holds its last answer ------------
template <int M, int K, int N, Mach Mc>
void check_rerun(const char* what) {
    constexpr const loops::Program& P = kPlain<M, K, N, Mc>;
    auto a = noise<float>(static_cast<std::size_t>(M) * K, 45);
    auto b = noise<float>(static_cast<std::size_t>(K) * N, 46);
    canaried::Workspace<P> work(loops::Sizes{});
    canaried::run<P>(loops::Feeds{a.get(), b.get()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    std::unique_ptr<float[]> first(new float[static_cast<std::size_t>(M) * N]);
    for (int i = 0; i < M * N; ++i) {
        first[i] = y[i];
    }
    for (int again = 0; again < 3; ++again) {
        canaried::run<P>(loops::Feeds{a.get(), b.get()}, work);
        for (int i = 0; i < M * N; ++i) {
            expect(what, static_cast<std::size_t>(i), y[i], first[i]);
        }
    }
}

// ================================ PART B — the two failing checks ================================

constexpr std::size_t kBlock = 4; // the streamed extent the walk and the nest run are compared over

bool same_bits(float a, float b) {
    if (std::isnan(a) && std::isnan(b)) {
        return true; // §3: "the same bits, or both a NaN"
    }
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::memcpy(&x, &a, 4);
    std::memcpy(&y, &b, 4);
    return x == y;
}

// ---- B1: a fold the schedule reassociates NOTHING in, against the walk ------------------------------
//
// x{R, C, T} folded along the ROWS — the axis memory is not contiguous in, so the fold's own loop is
// serial: every output's fold runs in the reference walk's order (`nest_reduce_test` pins that). Since
// the window slice its lanes run ACROSS the outputs (the columns), which reorders no fold. Any
// difference from the walk is therefore not a freedom the schedule took.
constexpr int kR = 2;
constexpr int kC = 9;
template <ReduceOp Op>
constexpr Graph rows_folded() {
    Input x{"x", Signal{kR, kC, static_cast<std::int32_t>(kBlock)}};
    return analysed(finish(Output{"y"} = detail::reduce(x, Op, Axes{0})));
}
template <ReduceOp Op>
inline constexpr planned::Held kRowsHeld{planned::program_of(rows_folded<Op>(), machine_of(Mach::Zen2))};
template <ReduceOp Op>
inline constexpr const loops::Program& kRows = kRowsHeld<Op>.program;

// (time outermost, one serial loop; the columns a strip of lanes; the rows the fold's own loop, one and serial)
static_assert(tree_facts::walks_over(kRows<ReduceOp::Max>, 0, 0).size() == 1 && tree_facts::serial(tree_facts::walks_over(kRows<ReduceOp::Max>, 0, 0)[0]) &&
                  tree_facts::walks_over(kRows<ReduceOp::Max>, 0, 2).size() == 1 && tree_facts::serial(tree_facts::walks_over(kRows<ReduceOp::Max>, 0, 2)[0]) &&
                  loops::is_strip(tree_facts::walks_over(kRows<ReduceOp::Max>, 0, 1)[0]),
              "the rows fold's own loop is serial: the schedule reassociates nothing");

template <ReduceOp Op>
void check_against_the_walk(const char* what, const float* data, std::size_t n) {
    constexpr const loops::Program& P = kRows<Op>;
    const std::size_t w = static_cast<std::size_t>(kR) * kC;
    std::unique_ptr<float[]> x(new float[w * kBlock]);
    std::array<double, 128> xd{};
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
    const Graph g = rows_folded<Op>();
    run(g, feeds, out);
    for (std::size_t t = 0; t < kBlock; ++t) {
        for (std::size_t e = 0; e < static_cast<std::size_t>(kC); ++e) {
            const float want = static_cast<float>(sample_at(out, g.outputs[0].value, static_cast<std::int32_t>(e), t));
            if (!same_bits(y[t * kC + e], want) && ++g_wrong <= 20) {
                std::printf("  FAILING (B1) %s t=%zu e=%zu: nest %a, the walk %a\n", what, t, e,
                            static_cast<double>(y[t * kC + e]), static_cast<double>(want));
            }
        }
    }
}

// ---- B2: a fold over the streamed axis -------------------------------------------------------------
constexpr Graph mean_over_time() {
    Input x{"x", Signal{3, Length{.multiple_of = 1, .min = 1, .expect = 256}}};
    return analysed(finish(Output{"y"} = x.mean(1)));
}

// The walk and the loop-nest path decline it, by name: folding over that axis is folding over time, and the
// answer would be whatever block length the caller passed (mean of x[t] = t: 1.5 at N=4, 7.5 at N=16, 31.5 at N=64).
template <class Run>
bool refuses(const char* who, Run run) {
    try {
        run();
    } catch (const error& e) {
        std::printf("  (%s refuses it: \"%.*s\")\n", who, static_cast<int>(e.message().size()), e.message().data());
        return true;
    }
    return false;
}

void check_streamed_fold_is_refused() {
    const Graph g = mean_over_time();
    std::array<double, kBlock> xd{};
    Inputs feeds{};
    feeds.push_back(InputFeed{xd.data(), 3});
    Signals<kBlock> out{};
    const bool walk = refuses("the reference walk", [&] { run(g, feeds, out); });
    const loops::Refusal held = planned::refusal_of(g, machine_of(Mach::Zen2));
    const std::string_view why = held.view();
    if (!why.empty()) {
        std::printf("  (the loop-nest path refuses it: \"%.*s\")\n", static_cast<int>(why.size()), why.data());
    }
    const bool nest = why.starts_with("heinzel::compile: ");
    if (!walk || !nest || loops::lowers(g, Machine{})) {
        ++g_wrong;
        std::printf("  FAILING (B2) a fold over the streamed axis must be refused by the walk and the loop-nest path\n");
    }
}

} // namespace

int main() {
    // ---- part A ----
    check_plain<1, 1, 1, Mach::Zen2>("A 1x1x1");
    check_plain<7, 13, 5, Mach::Zen2>("A 7x13x5");
    check_plain<129, 257, 63, Mach::Zen2>("A 129x257x63");
    check_plain<50, 21, 17, Mach::Tiny>("A 50x21x17 chunked");
    check_plain<9, 300, 1, Mach::Tinier>("A lane extent one, relaid");
    check_plain<9, 300, 3, Mach::Tinier>("A lane extent below a block, relaid");
    check_plain<9, 300, 17, Mach::Tinier>("A lane extent a block plus one, relaid");
    check_first_lane<65, 33, 5, Mach::Tinier>("A ki,k->i relaid");
    check_free2<7, 13, 3, 17, Mach::Tinier>("A ik,kjl->ijl relaid");
    check_mixed<11, 40, 19, Mach::Tinier>("A f64 x f32 relaid");
    check_rerun<50, 21, 19, Mach::Tinier>("A rerun on a used workspace");

    // ---- part B: these are expected to FAIL until the two findings are fixed ----
    const float nan_first[8] = {__builtin_nanf(""), 1.5f, 0.5f, 3.0f, -0.75f, 8.0f, -4.5f, 0.125f};
    const float minus_zero[1] = {-0.0f};
    check_against_the_walk<ReduceOp::Max>("max over a NaN", nan_first, 8);
    check_against_the_walk<ReduceOp::Min>("min over a NaN", nan_first, 8);
    check_against_the_walk<ReduceOp::Sum>("sum of negative zeros", minus_zero, 1);
    check_streamed_fold_is_refused();

    if (g_wrong != 0) {
        std::printf("%d results are wrong\n", g_wrong);
        return 1;
    }
    return 0;
}
