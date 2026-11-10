// When does turning an operand pay? Its copy is one pass over it; what it saves grows with how often the product reads
// each of its numbers: the extent of the output axes it lacks (`Q`'s rows for `K` of `Q . K^T`). First built against
// two trees, before the planner turns (`Plan/20` step 1) and after (step 2); now on one tree, each graph planned twice
// for the machine (zen2, or the one `-DHEINZEL_BENCH_MACHINE=<name>` names, Plan/23 P6): never turned
// (`turn_pays_after` 2^30) and always (1). Timed through the runner on one core: `Q . K^T` with K 512 x 64 and Q of 1
// to 64 rows, and a dense layer per sample (`einsum("i,oi->o")`, 8 x 16 weights at each of 64 samples: every weight
// read once). A program's time moves by several percent with where its pages land, so the whole sweep is made in 40
// child processes of two rounds (about 30 s; `in_processes`, as `relay.cpp`), every round timing every program in turn
// on freshly placed inputs, and each point's gain is the median over the processes of (not turned / turned).
//
// How it decides `Machine::turn_pays_after`: the fewest rows of Q (reads of each of K's numbers) from which on the
// turned program is at least 3 % faster at every larger Q measured (`none`: at none); printed as `tuned
// turn_pays_after <rows>`. zen2's 32: 16 rows broke even, 32 ran 1.6x faster. Timed in one process (Plan/23 P6) the
// probe read 16 (8 rows 0.92x, 16 rows 1.27x).
//   clang++-22 -std=c++2c -O3 -march=native -DNDEBUG -fconstexpr-steps=268435456 -ffp-contract=off -fno-math-errno \
//     -Wno-psabi -I include -I tests/support -I tools/tune tools/tune/turn.cpp -o turn && taskset -c 4 ./turn
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

#include "corpus_graphs.hpp"
#include "planned.hpp"
#include "probe.hpp"

namespace hz = heinzel;
namespace loops = heinzel::loops;

namespace {

template <std::int32_t After>
constexpr hz::Machine kTurn = [] {
    hz::Machine m = tune::kBase;
    m.turn_pays_after = After;
    return m;
}();
constexpr const hz::Machine& kNever = kTurn<1 << 30>;
constexpr const hz::Machine& kAlways = kTurn<1>;

template <int M>
constexpr hz::Graph score() {
    hz::Input q{"Q", {M, 64}};
    hz::Input k{"K", {512, 64}};
    hz::Output y{"y"};
    y = hz::einsum("qd,kd->qk", q, k) * (1.0 / 8.0);
    return corpus::analysed(y);
}
constexpr hz::Graph dense() {
    hz::Input x{"x", hz::Signal{16, 64}};
    hz::Input w{"w", hz::Signal{8, 16, 64}};
    hz::Output y{"y"};
    y = hz::max(hz::einsum("i,oi->o", x, w), 0.0);
    return corpus::analysed(y);
}

constexpr std::array<int, 7> kRows{1, 2, 4, 8, 16, 32, 64};
constexpr std::array<hz::Graph (*)(), kRows.size() + 1> kGraphs{score<1>,  score<2>,  score<4>,  score<8>,
                                                                score<16>, score<32>, score<64>, dense};
constexpr int kRounds = 2;     // draws of the placement a program is timed on, in each process
constexpr int kProcesses = 40; // address spaces it is measured in

template <hz::Graph (*G)(), const hz::Machine& For>
struct Arm {
    static constexpr planned::Held kHeld{planned::program_of(G(), For)};
};

constexpr bool turned(const loops::Program& p) {
    for (const loops::Buffer& b : p.buffers) {
        if (b.turned >= 0) {
            return true;
        }
    }
    return false;
}

void name(std::size_t g, char (&what)[32]) {
    if (g < kRows.size()) {
        std::snprintf(what, sizeof what, "Q . K^T, Q of %d rows", kRows[g]);
    } else {
        std::snprintf(what, sizeof what, "a dense layer per sample");
    }
}

// The two programs of each graph, in the order the rounds time them (not turned, turned); says where the planner did
// not turn as asked.
template <std::size_t... I>
std::vector<tune::TimeFn> arms_of(std::index_sequence<I...>) {
    std::vector<tune::TimeFn> arms;
    const auto add = [&]<std::size_t J>() {
        using Never = Arm<kGraphs[J], kNever>;
        using Always = Arm<kGraphs[J], kAlways>;
        if (turned(Never::kHeld.program) || !turned(Always::kHeld.program)) {
            char what[32];
            name(J, what);
            std::printf("  %s: the planner did not turn as asked\n", what);
        }
        arms.push_back(&tune::Timed<Never::kHeld.program>::us);
        arms.push_back(&tune::Timed<Always::kHeld.program>::us);
    };
    (add.template operator()<I>(), ...);
    return arms;
}

// One process's measurement: each program's time, every round timing every program.
std::vector<double> pass(const std::vector<tune::TimeFn>& arms) {
    std::vector<std::vector<std::byte>> out;
    const auto t = tune::rounds(arms, loops::Sizes{}, out, kRounds);
    std::vector<double> v;
    for (const std::vector<double>& each : t) {
        v.push_back(tune::trimmed_mean(each));
    }
    return v;
}

} // namespace

int main() {
    tune::heading("turn_pays_after: Q (M x 64) . K^T (K 512 x 64), K turned or not");
    std::printf("  the machine's turn_pays_after: %d\n", tune::kBase.turn_pays_after);
    const std::vector<tune::TimeFn> arms = arms_of(std::make_index_sequence<kGraphs.size()>{});
    const auto passes = tune::in_processes(kProcesses, [&] { return pass(arms); });
    std::array<double, kGraphs.size()> gain{};
    for (std::size_t g = 0; g < kGraphs.size(); ++g) {
        std::vector<double> ratio;
        std::vector<double> never;
        std::vector<double> always;
        for (const std::vector<double>& p : passes) {
            never.push_back(p[2 * g]);
            always.push_back(p[2 * g + 1]);
            ratio.push_back(p[2 * g] / p[2 * g + 1]);
        }
        gain[g] = tune::median(ratio);
        char what[32];
        name(g, what);
        std::printf("  %-26s not turned %9.2f us  turned %9.2f us  %5.2fx, %.2f-%.2f\n", what, tune::median(never),
                    tune::median(always), gain[g], *std::min_element(ratio.begin(), ratio.end()),
                    *std::max_element(ratio.begin(), ratio.end()));
    }
    std::size_t from = kRows.size();
    while (from > 0 && gain[from - 1] >= 1.03) {
        --from;
    }
    if (from == kRows.size()) {
        tune::none("turn_pays_after");
    } else {
        tune::tuned("turn_pays_after", kRows[from]);
    }
}
