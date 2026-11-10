// Plan/23 P6 (6.1): `Machine::band_bytes` — how much of what a band's stages make it keeps per chunk (`Schedule::Band`,
// `loops/planner/bands.hpp`), as `Plan/archive/shared-outer-loop/01` D-2 chose it, now through the planner and runner.
//
// blurxy (3 x 3, two passes) and harris over 2048 columns at 1024 and 8192 rows, each planned for the base machine with
// its bands forced (`band_when_over` 0: the question is the chunk, not whether to band) at five budgets — the L1, and
// an eighth, a quarter, a half and all of the L2 — and once whole (`band_bytes` 0). Each is timed through the runner on
// one core; every banded output must equal the whole one bit for bit (a band moves no sum). A program's time moves by
// a few percent with where its pages land, so the measurement is made in 8 child processes (about 45 s) and each
// budget's ratio to its point's best is the median over them.
//
// How it decides: the fastest budget, the one whose worst ratio over the four points is least (`none`: even that is
// over three times the tolerance, 3 % or twice the median's standard error if more, from its worst point's best, so no
// one budget suits the four points). D-2 took the smallest budget within the tolerance of the fastest, but on zen2 the
// noise (4.6-8.6 %) let a quarter of the L2 in three runs of four while half the L2 was the fastest in all four
// (Gerald, 2026-10-10). A budget under which no chunk fits (the L1 for harris) leaves the run whole, and fails where
// the band pays. zen2's half the L2 (256 KiB): at worst 1.02-1.04, the quarter next at 1.07.
//   clang++-22 -std=c++2c -O3 -march=native -DNDEBUG -fconstexpr-steps=268435456 -ffp-contract=off -fno-math-errno \
//     -Wno-psabi -I include -I tests/support -I tools/tune tools/tune/band_bytes.cpp -o band_bytes &&
//     taskset -c 4 ./band_bytes
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include "corpus_graphs.hpp"
#include "planned.hpp"
#include "probe.hpp"

namespace hz = heinzel;
namespace loops = heinzel::loops;

namespace {

constexpr std::int32_t kL1 = tune::kBase.l1_bytes;
constexpr std::int32_t kL2 = tune::kBase.l2_bytes;
constexpr std::array<std::int32_t, 5> kBudgets{kL1, kL2 / 8, kL2 / 4, kL2 / 2, kL2};
constexpr double kTol = 0.03;  // D-2's within
constexpr int kRounds = 1;     // draws of the placement a program is timed on, in each process
constexpr int kProcesses = 8;  // address spaces it is measured in

template <std::int32_t Bytes>
constexpr hz::Machine kBand = [] {
    hz::Machine m = tune::kBase;
    m.band_when_over = 0;
    m.band_bytes = Bytes;
    return m;
}();

template <hz::Graph (*G)(), const hz::Machine& For>
struct Arm {
    static constexpr planned::Held kHeld{planned::program_of(G(), For)};
};

// The whole program and the five budgets of one point, in the order the rounds time them.
template <hz::Graph (*G)(), std::size_t... I>
std::vector<tune::TimeFn> arms_of(std::index_sequence<I...>) {
    return {&tune::Timed<Arm<G, kBand<0>>::kHeld.program>::us, &tune::Timed<Arm<G, kBand<kBudgets[I]>>::kHeld.program>::us...};
}

template <hz::Graph (*G)(), std::size_t... I>
std::array<std::int32_t, kBudgets.size()> chunks_of(std::index_sequence<I...>) {
    return {tune::band_step(Arm<G, kBand<kBudgets[I]>>::kHeld.program)...};
}

constexpr std::size_t kArms = kBudgets.size() + 1; // whole, then the budgets

// One process's measurement of a point: the time of each arm, and 1 if every banded output equals the whole one.
template <hz::Graph (*G)()>
std::vector<double> pass() {
    std::vector<std::vector<std::byte>> out;
    const auto t = tune::rounds(arms_of<G>(std::make_index_sequence<kBudgets.size()>{}), loops::Sizes{}, out, kRounds);
    std::vector<double> v;
    bool same = true;
    for (std::size_t a = 0; a < kArms; ++a) {
        v.push_back(tune::trimmed_mean(t[a]));
        same = same && out[a] == out[0];
    }
    v.push_back(same ? 1 : 0);
    return v;
}

std::vector<double> pass_all() {
    std::vector<double> v;
    for (const std::vector<double>& p : {pass<corpus::blurxy<1024, 2048, 3>>(), pass<corpus::blurxy<8192, 2048, 3>>(),
                                         pass<corpus::harris<1024, 2048>>(), pass<corpus::harris<8192, 2048>>()}) {
        v.insert(v.end(), p.begin(), p.end());
    }
    return v;
}

} // namespace

int main() {
    tune::heading("band_bytes: blurxy and harris, banded at five budgets, against whole");
    std::printf("  the machine's band_bytes: %d (L2 %d)\n", tune::kBase.band_bytes, kL2);
    const std::array<const char*, 4> names{"blurxy 1024 x 2048", "blurxy 8192 x 2048", "harris 1024 x 2048", "harris 8192 x 2048"};
    const std::array<std::array<std::int32_t, kBudgets.size()>, 4> chunk{
        chunks_of<corpus::blurxy<1024, 2048, 3>>(std::make_index_sequence<kBudgets.size()>{}),
        chunks_of<corpus::blurxy<8192, 2048, 3>>(std::make_index_sequence<kBudgets.size()>{}),
        chunks_of<corpus::harris<1024, 2048>>(std::make_index_sequence<kBudgets.size()>{}),
        chunks_of<corpus::harris<8192, 2048>>(std::make_index_sequence<kBudgets.size()>{})};
    const auto passes = tune::in_processes(kProcesses, pass_all);
    constexpr std::size_t kStride = kArms + 1;
    // Per process and point, each budget's time against the best budget's; the median over the processes.
    std::array<std::array<double, kBudgets.size()>, 4> ratio{};
    double noise = 0;
    for (std::size_t pt = 0; pt < 4; ++pt) {
        std::vector<double> whole;
        bool same = true;
        std::array<std::vector<double>, kBudgets.size()> time;
        std::array<std::vector<double>, kBudgets.size()> over;
        for (const std::vector<double>& p : passes) {
            const double* at = &p[pt * kStride];
            const double best = *std::min_element(at + 1, at + kArms);
            whole.push_back(at[0]);
            same = same && at[kArms] == 1;
            for (std::size_t c = 0; c < kBudgets.size(); ++c) {
                time[c].push_back(at[1 + c]);
                over[c].push_back(at[1 + c] / best);
            }
        }
        std::printf("  %s: whole %10.1f us%s\n", names[pt], tune::median(whole), same ? "" : "  (output DIFFERS)");
        for (std::size_t c = 0; c < kBudgets.size(); ++c) {
            ratio[pt][c] = tune::median(over[c]);
            noise = std::max(noise, 0.49 * tune::spread(over[c]) / std::sqrt(kProcesses));
            std::printf("    band_bytes %9d: %10.1f us, %5d rows a chunk (0: no band), %.3fx the best\n", kBudgets[c],
                        tune::median(time[c]), chunk[pt][c], ratio[pt][c]);
        }
    }
    // The pick is the budget whose worst ratio over the four points is least; `none` where even that is over three times
    // the tolerance (kTol, or twice the median's standard error if more: the largest of the twenty) from its worst
    // point's best.
    std::array<double, kBudgets.size()> worst{};
    for (std::size_t c = 0; c < kBudgets.size(); ++c) {
        for (const std::array<double, kBudgets.size()>& at : ratio) {
            worst[c] = std::max(worst[c], at[c]);
        }
        std::printf("  band_bytes %9d: at worst %.3fx a point's best\n", kBudgets[c], worst[c]);
    }
    const double tol = std::max(kTol, 2 * noise);
    const auto pick = static_cast<std::size_t>(std::min_element(worst.begin(), worst.end()) - worst.begin());
    std::printf("  the fastest at worst %.3fx (the tolerance %.3f); the L2 over it: %.2f\n", worst[pick], tol,
                static_cast<double>(kL2) / kBudgets[pick]);
    if (worst[pick] > 1.0 + 3 * tol) {
        tune::none("band_bytes");
        return 0;
    }
    tune::tuned("band_bytes", kBudgets[pick]);
}
