// Plan/23 P6 (6.1): `Machine::relay_pays_after` — after how many blocks of rows copying a contraction's strided operand
// into blocks (`Schedule::Stage::relaid`) pays for the copy.
//
// The product `ik,kj->ij` with B 256 x 256 (256 KiB: over the first-level cache of zen2 and of the M1, so the planner
// may relay it) and A of M rows, M = w blocks of the base machine's register rows ((registers - 3) / 2: 6 on zen2, 14
// on the M1). Each M is planned twice for the base machine, B never relaid (`relay_pays_after` 2^30) and always (0), so
// the two programs differ in the copy alone; each is timed through the runner on one core, and the two outputs must
// agree bit for bit (the copy moves no sum). The not-relaid arm reads B along its columns, 1 KiB apart, and its time
// moves by several percent with where the pages land (the address space's layout, ASLR): so the whole sweep is made in
// 60 child processes of two rounds (about 55 s), every round timing every program in turn on freshly placed inputs, and
// each point's gain is the median over the processes of (not relaid / relaid), with the range printed.
//
// How it decides: the copy is one pass over B and what it saves grows with M, so the threshold is the fewest blocks w
// from which on the relaid program is no longer clearly slower at any larger w measured: not by more than 2 % or twice
// the median's standard error (`none`: at none). On zen2 the gain climbs from 0.68 (2 blocks) to 0.96 at 10 and
// stays there to 16, 0.99 at 24, 1.01 at 32 and 1.04 at 64: the probe reads 24, where the curve leaves its plateau (a
// 4 % tolerance would read 12 or 16 by turns, 1 % 24 or 32: the plateau is within the noise). zen2's 12 (`machine.hpp`)
// came from a break-even at 64 rows (about ten blocks of six), and at one row the copy cost 2.4 times the product.
//   clang++-22 -std=c++2c -O3 -march=native -DNDEBUG -fconstexpr-steps=268435456 -ffp-contract=off -fno-math-errno \
//     -Wno-psabi -I include -I tests/support -I tools/tune tools/tune/relay.cpp -o relay &&
//     taskset -c 4 ./relay
// (`Plan/probes/thresholds/run.sh` builds and runs every probe; `-DHEINZEL_BENCH_MACHINE=m1_pro` plans for the M1.)
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

constexpr std::int32_t kRows = (tune::kBase.registers - 3) / 2; // `plan_contraction_loops`' rows, unrolled twice
constexpr std::int32_t kNever = 1 << 30;
constexpr std::int32_t kAlways = 0;

template <std::int32_t After>
constexpr hz::Machine kRelay = [] {
    hz::Machine m = tune::kBase;
    m.relay_pays_after = After;
    return m;
}();

template <int M, const hz::Machine& For>
struct Arm {
    static constexpr planned::Held kHeld{planned::program_of(corpus::matmul<M, 256, 256>(), For)};
};

constexpr bool relaid(const loops::Program& p) {
    for (const loops::Buffer& b : p.buffers) {
        if (b.blocked_axis >= 0) {
            return true;
        }
    }
    return false;
}

constexpr double kTol = 0.02; 
constexpr int kRounds = 2;    // draws of the placement a program is timed on, in each process
constexpr int kProcesses = 60; // address spaces it is measured in
constexpr std::array<int, 10> kBlocks{2, 4, 6, 8, 10, 12, 16, 24, 32, 64};

// The two programs of w blocks, in the order the rounds time them (not relaid, relaid).
template <int W>
void add_arms(std::vector<tune::TimeFn>& arms) {
    using Never = Arm<W * kRows, kRelay<kNever>>;
    using Always = Arm<W * kRows, kRelay<kAlways>>;
    if (relaid(Never::kHeld.program) || !relaid(Always::kHeld.program)) {
        std::printf("  %d blocks: the planner did not relay as asked\n", W);
    }
    arms.push_back(&tune::Timed<Never::kHeld.program>::us);
    arms.push_back(&tune::Timed<Always::kHeld.program>::us);
}

template <std::size_t... I>
std::vector<tune::TimeFn> arms_of(std::index_sequence<I...>) {
    std::vector<tune::TimeFn> arms;
    (add_arms<kBlocks[I]>(arms), ...);
    return arms;
}

// One process's measurement: for each point the two times and 1 if the outputs agree. Every round times every program,
// so that a disturbance of the box that lasts seconds (another process on the core's sibling thread slows the strided
// arm by up to 30 %) is spread over the points instead of settling on a few.
std::vector<double> pass() {
    std::vector<std::vector<std::byte>> out;
    const auto t = tune::rounds(arms_of(std::make_index_sequence<kBlocks.size()>{}), loops::Sizes{}, out, kRounds);
    std::vector<double> v;
    for (std::size_t w = 0; w < kBlocks.size(); ++w) {
        v.push_back(tune::trimmed_mean(t[2 * w]));
        v.push_back(tune::trimmed_mean(t[2 * w + 1]));
        v.push_back(out[2 * w] == out[2 * w + 1] ? 1 : 0);
    }
    return v;
}

} // namespace

int main() {
    tune::heading("relay_pays_after: A (M x 256) . B (256 x 256), B relaid into blocks or not");
    std::printf("  a block: %d rows; the machine's relay_pays_after: %d\n", kRows, tune::kBase.relay_pays_after);
    const auto passes = tune::in_processes(kProcesses, pass);
    std::array<double, kBlocks.size()> gain{};
    std::array<double, kBlocks.size()> spread{};
    for (std::size_t w = 0; w < kBlocks.size(); ++w) {
        std::vector<double> ratio;
        std::vector<double> never;
        std::vector<double> always;
        bool same = true;
        for (const std::vector<double>& p : passes) {
            never.push_back(p[3 * w]);
            always.push_back(p[3 * w + 1]);
            ratio.push_back(p[3 * w] / p[3 * w + 1]);
            same = same && p[3 * w + 2] == 1;
        }
        gain[w] = tune::median(ratio);
        spread[w] = tune::spread(ratio);
        std::printf("  %3d blocks (%4d rows): not relaid %9.1f us  relaid %9.1f us  %5.3fx, %.3f-%.3f%s\n", kBlocks[w],
                    kBlocks[w] * kRows, tune::median(never), tune::median(always), gain[w],
                    *std::min_element(ratio.begin(), ratio.end()), *std::max_element(ratio.begin(), ratio.end()),
                    same ? "" : "  (outputs DIFFER)");
    }
    // The copy has paid when the relaid program is no longer clearly slower: not by more than kTol, nor than twice the
    // median's standard error at that point (the spread between the processes is 2.56 standard deviations, the median
    // of n of them errs by 1.25 / sqrt(n) of one). Earlier it clearly loses, so the threshold is the first point after
    // the last clear loss. (The gain climbs from 0.66 to 1.03 over 2 to 64 blocks and sits at 0.96-0.97 from 10 to 16:
    // the pick is where it leaves that plateau, not a break-even a percent either way would move to 10 or 32.)
    std::size_t from = 0;
    for (std::size_t w = 0; w < kBlocks.size(); ++w) {
        if (gain[w] < 1.0 - std::max(kTol, 2 * 0.49 * spread[w] / std::sqrt(kProcesses))) {
            from = w + 1;
        }
    }
    if (from == kBlocks.size()) {
        tune::none("relay_pays_after");
    } else {
        tune::tuned("relay_pays_after", kBlocks[from]);
    }
}
