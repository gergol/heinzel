// Plan/23 P6 (6.1): `Machine::recompute_always` and `recompute_if_uncached` — how many operations per output
// recomputing a cheap producer inside the window that reads it may cost before storing it once is cheaper
// (`Schedule::Stage::through`, `loops/planner/inline.hpp`), with the buffer cached and with it half the last-level
// cache or more.
//
// The bench's conv (`p = x*x; y[i] = sum_k p[i+k] h[k]`, f32): the square is one node, so K taps recompute K - 1
// operations an output, K = 3, 5, 9, 17, 33, 65 (2 to 64 extra operations). Two lengths: 2^19 samples (p of 2 MiB,
// cached on zen2 and the M1 alike) and the last-level cache's bytes in samples (p as large as `l3_bytes`, so at least
// half of it). Each is planned twice for the base machine, inside (`recompute_*` 2^30) and stored (-1), both with
// `band_bytes` 0 — stored whole, as the rule was measured, before bands existed; a band over the stored square would
// make storing cheaper at the large length. Each is timed through the runner on one core, and the two outputs must
// agree bit for bit (the same products, summed in the same order). Timed in one process, one arm after the other, the
// probe read 16 for 8 once in eight configures and 64 for 32 once (Plan 24 step 2), so the whole sweep is made in 12
// child processes of two rounds (`in_processes`, as `relay.cpp`; about 30 s), every round timing every program in turn
// on freshly placed inputs, and each point's gain is the median over the processes of (stored / inside).
//
// How it decides: at each length, the most extra operations at which computing inside is clearly faster, and at every
// fewer measured: by more than 6 % (the layout's noise, below) or twice the median's standard error (as `relay.cpp`),
// so that a tie stores. `recompute_always` from the short length, `recompute_if_uncached` from the long one (0: inside
// is not clearly faster already at 2). zen2's 8 and 32: 2 or 3 extra operations inside 1.7-2.3x faster at every size,
// 15 or 31 stored 3-12 % faster up to 4 MB and inside 1.5-1.9x faster from 8 MB (half the L3) up, 63 stored. In
// processes (Plan 24 step 3, three runs, 33 s each): at 2 MiB 8 extra 1.11x, 16 0.87-0.90x; at the L3's size 32 extra
// 1.14x, 64 1.03-1.05x (one process, one arm after the other, in another build: 0.97-1.02x), which 2 % reads as 64.
//   clang++-22 -std=c++2c -O3 -march=native -DNDEBUG -fconstexpr-steps=268435456 -ffp-contract=off -fno-math-errno \
//     -Wno-psabi -I include -I tests/support -I tools/tune tools/tune/recompute.cpp -o recompute &&
//     taskset -c 4 ./recompute
#include <algorithm>
#include <array>
#include <cmath>
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

constexpr int kShort = 1 << 19;
constexpr int kLong = tune::kBase.l3_bytes / 4;

template <std::int32_t Ops>
constexpr hz::Machine kRecompute = [] {
    hz::Machine m = tune::kBase;
    m.recompute_always = m.recompute_if_uncached = Ops;
    m.band_bytes = 0;
    return m;
}();
constexpr std::int32_t kInside = 1 << 30;
constexpr std::int32_t kStored = -1;

template <int N, int K, const hz::Machine& For>
struct Arm {
    static constexpr planned::Held kHeld{planned::program_of(corpus::conv<N, K>(), For)};
};

// The binary's code layout moves a program's time by several percent, the same in every process and round of one build,
// so no repetition here sees it (Plan 24 step 3: inside at 64 extra and the L3's size 9.5-9.7 ms in one build of this
// probe, 8.9-9.1 in another; Plan/23's rows of an unchanged program moved up to 6 %). "Clearly" clears that.
constexpr double kTol = 0.06;
constexpr int kRounds = 2;    // draws of the placement a program is timed on, in each process
constexpr int kProcesses = 12; // address spaces it is measured in
constexpr std::array<int, 6> kTaps{3, 5, 9, 17, 33, 65};
constexpr std::array<int, 2> kLengths{kShort, kLong};
constexpr std::size_t kPoints = kLengths.size() * kTaps.size();

// The two programs of N samples and K taps, in the order the rounds time them (inside, stored).
template <int N, int K>
void add_arms(std::vector<tune::TimeFn>& arms) {
    arms.push_back(&tune::Timed<Arm<N, K, kRecompute<kInside>>::kHeld.program>::us);
    arms.push_back(&tune::Timed<Arm<N, K, kRecompute<kStored>>::kHeld.program>::us);
}

template <std::size_t... I>
std::vector<tune::TimeFn> arms_of(std::index_sequence<I...>) {
    std::vector<tune::TimeFn> arms;
    (add_arms<kShort, kTaps[I]>(arms), ...);
    (add_arms<kLong, kTaps[I]>(arms), ...);
    return arms;
}

// One process's measurement: for each point the two times and 1 if the outputs agree.
std::vector<double> pass() {
    std::vector<std::vector<std::byte>> out;
    const auto t = tune::rounds(arms_of(std::make_index_sequence<kTaps.size()>{}), loops::Sizes{}, out, kRounds);
    std::vector<double> v;
    for (std::size_t p = 0; p < kPoints; ++p) {
        v.push_back(tune::trimmed_mean(t[2 * p]));
        v.push_back(tune::trimmed_mean(t[2 * p + 1]));
        v.push_back(out[2 * p] == out[2 * p + 1] ? 1 : 0);
    }
    return v;
}

// The most extra operations at which computing inside is clearly faster, and at every fewer (0: at none), over the
// points of one length; prints each.
long long most(const std::vector<std::vector<double>>& passes, std::size_t length) {
    long long last = 0;
    bool wins = true;
    for (std::size_t k = 0; k < kTaps.size(); ++k) {
        const std::size_t p = length * kTaps.size() + k;
        std::vector<double> ratio;
        std::vector<double> inside;
        std::vector<double> stored;
        bool same = true;
        for (const std::vector<double>& each : passes) {
            inside.push_back(each[3 * p]);
            stored.push_back(each[3 * p + 1]);
            ratio.push_back(each[3 * p + 1] / each[3 * p]);
            same = same && each[3 * p + 2] == 1;
        }
        const double gain = tune::median(ratio);
        std::printf("  %8d samples, %2d taps (%2d extra): inside %9.1f us  stored %9.1f us  %5.3fx, %.3f-%.3f%s\n",
                    kLengths[length], kTaps[k], kTaps[k] - 1, tune::median(inside), tune::median(stored), gain,
                    *std::min_element(ratio.begin(), ratio.end()), *std::max_element(ratio.begin(), ratio.end()),
                    same ? "" : "  (outputs DIFFER)");
        // (the spread between the processes is 2.56 standard deviations, the median of n of them errs by 1.25 /
        // sqrt(n) of one: `relay.cpp`)
        wins = wins && gain > 1.0 + std::max(kTol, 2 * 0.49 * tune::spread(ratio) / std::sqrt(kProcesses));
        if (wins) {
            last = kTaps[k] - 1;
        }
    }
    return last;
}

} // namespace

int main() {
    tune::heading("recompute_always, recompute_if_uncached: conv's square inside the window or stored whole");
    std::printf("  the machine's: %d and %d; half its last-level cache: %d bytes\n", tune::kBase.recompute_always,
                tune::kBase.recompute_if_uncached, tune::kBase.l3_bytes / 2);
    const auto passes = tune::in_processes(kProcesses, pass);
    tune::tuned("recompute_always", most(passes, 0));
    tune::tuned("recompute_if_uncached", most(passes, 1));
}
