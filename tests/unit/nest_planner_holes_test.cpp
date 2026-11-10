// The graphs the planner once planned and the lowering refused (`Plan/17-simplify-more/06` § A): the planner now
// avoids what the lowering would refuse, and each graph plans and lowers. Every program is run canaried
// (`canaried.hpp`) and held BIT FOR BIT to the reference walk of the same graph at `High`
// (`tests/reference/interpret.hpp`); a program planned at `Normal` is fed small integers, so that no sum it reorders
// or contracts rounds. `loops_plan_test` pins that the planner plans them.
//
//   (d)  a·b + cᵀ on zen2 and the default machine: lanes across the product's outputs would read cᵀ across them, so
//        they stay off the outputs (along the contracted axis where both operands are contiguous in it, else none)
//   (f)  a product per sample plus a lag of its input, on tiny caches: its rows run along time, untiled, since the
//        stage runs its first samples apart; two blocks, the feed's tail carried from one to the next
//   (b)  a bank of 256 one-poles, its feedback term read through a window over the voices, banded along them on a
//        machine of many cores: the cycle runs ahead and stores its state whole, so the band is not split; eight
//        blocks of one sample, the state carried

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "interpret.hpp"
#include "planned.hpp"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;
using numerics::Precision;

constexpr Graph analysed(Graph g, Precision level) {
    set_default_precision(g, level);
    if (!infer(g).ok) {
        fail("nest_planner_holes_test: a graph did not resolve");
    }
    return g;
}

constexpr Graph transposed_after_a_product(Precision level) { // (d)
    Input a{"a", {64, 32}};
    Input b{"b", {32, 48}};
    Input c{"c", {48, 64}};
    return analysed(finish(Output{"y"} = einsum("ik,kj->ij", a, b) + c.transpose({1, 0})), level);
}

constexpr Graph per_sample(Precision level) { // (f)
    constexpr Index n{};
    Input x{"x", Signal{8, 64}};
    Input w{"w", Signal{8, 8, 64}};
    return analysed(finish(Output{"y"} = einsum("k,ko->o", x, w) + 0.5 * x[n - 1]), level);
}

constexpr Graph bank_ahead(Precision level) { // (b)
    constexpr Index n{};
    Input x{"x", Signal{256, 1}};
    Variable y;
    Expr fed = 0.5 * y[n - 1];
    y[n] = x + fed;
    return analysed(finish(Output{"y"} = fed.unfold(0, 3).sum(1)), level);
}

// Caches small enough that the 64 samples are more than one and a half slabs of rows.
constexpr Machine kTiny{.vector_bytes = 32, .registers = 16, .l1_bytes = 1024, .l2_bytes = 2048, .page_bytes = 256, .relay_pays_after = 1, .fma = true};

// zen2, banding whatever it can, in chunks of 64 voices.
constexpr Machine kBandsAhead = [] {
    Machine m = zen2;
    m.band_when_over = 0;
    m.band_bytes = 66 * 4;
    return m;
}();

constexpr Graph kProduct = transposed_after_a_product(Precision::Normal);
constexpr Graph kProductHigh = transposed_after_a_product(Precision::High);
constexpr planned::Held kProductHeld{planned::program_of(kProduct, zen2)};
constexpr planned::Held kProductAtHighHeld{planned::program_of(kProductHigh, zen2)};
constexpr planned::Held kProductOnDefaultHeld{planned::program_of(kProduct, default_machine)};
constexpr Graph kPerSample = per_sample(Precision::Normal);
constexpr Graph kPerSampleHigh = per_sample(Precision::High);
constexpr planned::Held kPerSampleHeld{planned::program_of(kPerSample, kTiny)};
constexpr Graph kBank = bank_ahead(Precision::Normal);
constexpr Graph kBankHigh = bank_ahead(Precision::High);
constexpr planned::Held kBankHeld{planned::program_of(kBank, kBandsAhead)};
constexpr planned::Held kBankAtHighHeld{planned::program_of(kBankHigh, kBandsAhead)};
static_assert(kBankHeld.program.bands.size() == 1 && !kBankHeld.program.bands[0].parallel && kBankHeld.program.stages[0].lead == 2 &&
              kBankAtHighHeld.program.bands.size() == 1 && !kBankAtHighHeld.program.bands[0].parallel);

int g_wrong = 0;

// Small integers where `exact`; otherwise numbers whose products and sums round.
std::vector<float> noise(std::size_t n, std::uint32_t seed, bool exact) {
    std::vector<float> v(n);
    for (float& x : v) {
        seed = seed * 1664525u + 1013904223u;
        const auto r = static_cast<std::int32_t>(seed >> 8);
        x = exact ? static_cast<float>(r % 17 - 8) : static_cast<float>(r % 2001 - 1000) * 0.00173f;
    }
    return v;
}

// `P`, a program of `walked`'s graph (at any precision), run `Runs` times on one workspace, a block of `Block` samples
// each and its state carried, against the walk of `walked` over all of them.
template <const loops::Program& P, std::size_t Block, std::size_t Runs, std::size_t Planes>
void against_the_walk(const char* what, const Graph& walked, bool exact) {
    constexpr std::size_t samples = Block * Runs;
    std::vector<std::vector<float>> held;
    std::vector<std::vector<double>> widened;
    Inputs feeds{};
    for (std::size_t i = 0; i < walked.inputs.size(); ++i) {
        const auto width = static_cast<std::size_t>(numbers_per_sample(walked.values[static_cast<std::size_t>(walked.inputs[i].value)].shape));
        held.push_back(noise(width * samples, static_cast<std::uint32_t>(i + 1), exact));
        widened.emplace_back(held[i].begin(), held[i].end());
    }
    for (std::size_t i = 0; i < walked.inputs.size(); ++i) {
        feeds.push_back(InputFeed{widened[i].data(), static_cast<std::int32_t>(held[i].size() / samples)});
    }
    const auto walk = std::make_unique<Signals<samples, Planes>>();
    run(walked, feeds, *walk);
    const std::int32_t made = walked.outputs[0].value;
    const auto width = static_cast<std::size_t>(numbers_per_sample(walked.values[static_cast<std::size_t>(made)].shape));
    canaried::Workspace<P> work(loops::Sizes{});
    int bad = 0;
    for (std::size_t r = 0; r < Runs; ++r) {
        loops::Feeds block{};
        for (std::size_t i = 0; i < held.size(); ++i) {
            block[i] = held[i].data() + held[i].size() / Runs * r;
        }
        canaried::run<P>(block, work);
        const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
        for (std::size_t k = 0; k < width * Block; ++k) {
            const auto want = static_cast<float>(sample_at(*walk, made, static_cast<std::int32_t>(k % width), r * Block + k / width));
            if (std::bit_cast<std::uint32_t>(y[k]) != std::bit_cast<std::uint32_t>(want) && ++bad <= 2) {
                std::printf("  %s, run %zu [%zu]: got %a want %a\n", what, r, k, static_cast<double>(y[k]), static_cast<double>(want));
            }
        }
    }
    g_wrong += bad != 0 ? 1 : 0;
    std::printf("  %-48s %s\n", what, bad == 0 ? "ok" : "WRONG");
}

} // namespace

int main() {
    against_the_walk<kProductHeld.program, 1, 1, 16384>("(d) a.b + c^T on zen2, Normal", kProductHigh, true);
    against_the_walk<kProductAtHighHeld.program, 1, 1, 16384>("(d) a.b + c^T on zen2, High", kProductHigh, false);
    against_the_walk<kProductOnDefaultHeld.program, 1, 1, 16384>("(d) a.b + c^T, no fma, one core", kProductHigh, true);
    against_the_walk<kPerSampleHeld.program, 64, 2, 256>("(f) a product per sample and a lag, tiny caches", kPerSampleHigh, true);
    against_the_walk<kBankHeld.program, 1, 8, 4096>("(b) a bank banded ahead, Normal", kBankHigh, true);
    against_the_walk<kBankAtHighHeld.program, 1, 8, 4096>("(b) a bank banded ahead, High", kBankHigh, false);
    if (g_wrong != 0) {
        std::printf("%d programs differ from the walk\n", g_wrong);
        return 1;
    }
    std::printf("nest_planner_holes: every program planned where the lowering once refused runs as the walk, bit for bit\n");
    return 0;
}
