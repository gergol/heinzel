// From the adversarial review of d64ad024 / 1782be01 / b0caa735: y = max(a.b + bias, 0) on odd shapes,
// against a plain triple loop in float with the same multiply-add per step (rounded as the machine rounds, as the
// fused program does) and one order per output. Every shape runs as the planner decides it and with the relayout taken back, which must not
// change a single number.
//
// Two machines: zen2, and one with tiny caches and pages on which a relaid copy pays at once — so that
// the chunking, the relayout, the row and column tails and the fetch-ahead all happen together, at
// shapes small enough to compile (one row, fewer rows than a block of six, a block and one, ...).

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/runtime/hardware_twins.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "edited_schedules.hpp"
#include "planned.hpp"
#include "tree_facts.hpp"

#include <cstdio>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

using edited_schedules::fed_as_it_is;
constexpr const Machine& kTiny = edited_schedules::kTinyCaches;

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("nest_odd_shapes_test: a graph did not resolve");
    }
    return g;
}

template <std::int32_t M, std::int32_t K, std::int32_t N>
constexpr Graph linear_relu() {
    Input a{"a", {M, K}};
    Input b{"b", {K, N}};
    Input bias{"bias", {N}};
    return analysed(finish(Output{"y"} = max(einsum("ik,kj->ij", a, b) + bias, 0.0)));
}

template <std::int32_t M, std::int32_t K, std::int32_t N>
constexpr planned::Held kZen2Held{planned::program_of(linear_relu<M, K, N>(), zen2)};
template <std::int32_t M, std::int32_t K, std::int32_t N>
constexpr const loops::Program& kZen2 = kZen2Held<M, K, N>.program;
// Planned once for the tiny machine: lowered as planned, and with the relayout taken back.
template <std::int32_t M, std::int32_t K, std::int32_t N>
constexpr loops::Schedule kTinySchedule = planned::schedule_of(linear_relu<M, K, N>(), kTiny);
template <std::int32_t M, std::int32_t K, std::int32_t N>
constexpr planned::Held kTinyPlannedHeld{planned::program_of(linear_relu<M, K, N>(), kTinySchedule<M, K, N>)};
template <std::int32_t M, std::int32_t K, std::int32_t N>
constexpr const loops::Program& kTinyPlanned = kTinyPlannedHeld<M, K, N>.program;
template <std::int32_t M, std::int32_t K, std::int32_t N>
constexpr planned::Held kAsFedHeld{planned::program_of(linear_relu<M, K, N>(), fed_as_it_is(kTinySchedule<M, K, N>))};
template <std::int32_t M, std::int32_t K, std::int32_t N>
constexpr const loops::Program& kAsFed = kAsFedHeld<M, K, N>.program;

int g_wrong = 0;

std::vector<float> noise(std::size_t n, std::uint32_t seed) {
    std::vector<float> v(n);
    std::uint32_t state = seed;
    for (float& x : v) {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.00173f;
    }
    return v;
}

template <const loops::Program& P, std::int32_t M, std::int32_t K, std::int32_t N>
void check(const char* what) {
    const std::vector<float> a = noise(static_cast<std::size_t>(M) * K, 11);
    const std::vector<float> b = noise(static_cast<std::size_t>(K) * N, 12);
    const std::vector<float> bias = noise(static_cast<std::size_t>(N), 13);
    canaried::Workspace<P> work(loops::Sizes{});
    canaried::run<P>(loops::Feeds{a.data(), b.data(), bias.data()}, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    int wrong_here = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(M); ++i) {
        for (std::size_t j = 0; j < static_cast<std::size_t>(N); ++j) {
            float sum = 0.0f;
            for (std::size_t k = 0; k < static_cast<std::size_t>(K); ++k) {
                sum = multiply_add_as_the_machine_rounds(a[i * K + k], b[k * N + j], sum);
            }
            const float biased = sum + bias[j];
            const float want = biased > 0.0f ? biased : 0.0f;
            const float got = y[i * N + j];
            if (got != want) {
                ++g_wrong;
                if (++wrong_here <= 3) {
                    std::printf("  %s %dx%dx%d [%zu,%zu]: got %a, want %a\n", what, M, K, N, i, j, static_cast<double>(got),
                                static_cast<double>(want));
                }
            }
        }
    }
}

template <std::int32_t M, std::int32_t K, std::int32_t N>
void both() {
    check<kZen2<M, K, N>, M, K, N>("zen2, as planned");
    check<kTinyPlanned<M, K, N>, M, K, N>("tiny caches, as planned");
    check<kAsFed<M, K, N>, M, K, N>("tiny caches, not relaid");
}

// What actually ran, for the shapes that are meant to exercise the new paths: the stages, the last one's walking
// nodes and how many of them are the contraction's own (under its Registers node), whether it fetches ahead, and
// whether a relaid copy is made.
void say(const loops::Program& p, std::int32_t m, std::int32_t k, std::int32_t n, const char* machine) {
    const std::size_t last = p.stages.size() - 1;
    std::int32_t own = 0;
    for (std::int32_t at = p.stages[last].root + 1; at < tree_facts::subtree_end(p, last); ++at) {
        const loops::NodeKind kind = loops::node_at(p, at).kind;
        const bool walking = kind == loops::NodeKind::Loop || kind == loops::NodeKind::Unroll || kind == loops::NodeKind::Vector;
        own += walking && loops::ancestor(p, at, [](const loops::Node& a) { return a.kind == loops::NodeKind::Registers; }) >= 0 ? 1 : 0;
    }
    bool relaid = false;
    for (const loops::Buffer& b : p.buffers) {
        relaid = relaid || b.blocked_axis >= 0;
    }
    std::printf("  %-12s %4dx%4dx%4d: stages %zu, walking nodes %zu (%d the fold's own), fetches ahead %d, relaid %d\n", machine, m, k, n,
                p.stages.size(), tree_facts::walks(p, last).size(), own, tree_facts::count(p, last, loops::NodeKind::Prefetch) > 0 ? 1 : 0,
                relaid ? 1 : 0);
}

} // namespace

int main() {
#define SHAPE(M, K, N) both<M, K, N>();
    // tails everywhere: rows against blocks of 6, columns against blocks of 16 and lanes of 8
    SHAPE(1, 1, 1)
    SHAPE(1, 300, 100)
    SHAPE(5, 63, 15)
    SHAPE(6, 65, 17)
    SHAPE(7, 1, 100)
    SHAPE(7, 64, 33)
    SHAPE(13, 8, 1)
    SHAPE(13, 300, 17)
    SHAPE(17, 300, 100)
#undef SHAPE
    std::printf("what ran:\n");
    say(kZen2<17, 300, 100>, 17, 300, 100, "zen2");
    say(kTinyPlanned<17, 300, 100>, 17, 300, 100, "tiny");
    say(kTinyPlanned<7, 64, 33>, 7, 64, 33, "tiny");
    say(kTinyPlanned<1, 300, 100>, 1, 300, 100, "tiny");
    if (g_wrong != 0) {
        std::printf("%d elements are wrong\n", g_wrong);
        return 1;
    }
    std::printf("all shapes agree\n");
    return 0;
}
