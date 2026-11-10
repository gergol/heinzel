// Plan/23 P6 (6.1): `Machine::band_stream_rows` — how many samples a chunk of a stream band holds (a band whose last
// stage reads what the one before makes at the point it is made: `loops/planner/bands.hpp`), measured through the
// runner as `Plan/archive/shared-outer-loop/00-Overview.md` D-6 measured it.
//
// The bench's two streams: the FM oscillator over 2^22 samples (a phase recurrence, then the sine of it) and the ladder
// bank, sixteen voices over 2^16 samples (a recurrence of `tanh`s, then a gain). Each planned for the base machine at
// `band_stream_rows` 0 (no stream band: whole), 4, 8, 16, 32, 64 and 128, and timed through the runner on one core;
// every output must equal the first's bit for bit (a band moves no sum). A program's time moves by a few percent with
// where its pages land, so the measurement is made in 12 child processes of two rounds each (about 35 s) and each
// count's worst ratio is the median over them.
//
// How it decides: the row count whose worst time, against each stream's best at any count, is least (0 if whole wins),
// as `band_bytes` takes its fastest budget. The counts within 3 % (or twice the median's standard error) of it are
// printed as "flat from A to B", and decide nothing: the smallest of them, the pick until 2026-10-12, is the plateau's
// edge, where a band whose chunk carries more than these two streams falls off (the M1 read 16 least worst, 1.036, and
// took 8, 1.060: the reverb, cut into a feedback stage and its tail, ran 6.41 ns a sample at 8 rows, 4.88 at 16,
// `Plan/probes/tail-cut/tail_cut.cpp`). On zen2 16 alone is on it (1.01; 32 and 64 are 1.06, 8 1.11, 128 1.09).
// zen2's 16: the oscillator 16 samples 11 % faster than whole (32: 6 %, 64: 7 %, 128 and more 3 % and less), the ladder
// bank 8 to 16 samples 13 % (4: 10 %, 32: 4 %).
//   clang++-22 -std=c++2c -O3 -march=native -DNDEBUG -fconstexpr-steps=268435456 -ffp-contract=off -fno-math-errno \
//     -Wno-psabi -I include -I tests/support -I tools/tune tools/tune/stream_rows.cpp -o stream_rows &&
//     taskset -c 4 ./stream_rows
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

constexpr std::array<std::int32_t, 7> kRows{0, 4, 8, 16, 32, 64, 128};
constexpr std::int64_t kFmSamples = std::int64_t{1} << 22;
constexpr std::int64_t kLadderSamples = std::int64_t{1} << 16;
constexpr double kTol = 0.03;  // the 3 % that D-6 and `band_bytes` call within
constexpr int kRounds = 2;     // draws of the placement a program is timed on, in each process
constexpr int kProcesses = 12; // address spaces it is measured in

template <std::int32_t Rows>
constexpr hz::Machine kStream = [] {
    hz::Machine m = tune::kBase;
    m.band_stream_rows = Rows;
    return m;
}();

template <hz::Graph (*G)(), const hz::Machine& For>
struct Arm {
    static constexpr planned::Held kHeld{planned::program_of(G(), For)};
};

template <hz::Graph (*G)(), std::size_t... I>
std::vector<tune::TimeFn> arms_of(std::index_sequence<I...>) {
    return {&tune::Timed<Arm<G, kStream<kRows[I]>>::kHeld.program>::us...};
}

// The rows of the program's first band at each count, for the print (0: no band).
template <hz::Graph (*G)(), std::size_t... I>
std::array<std::int32_t, kRows.size()> chunks_of(std::index_sequence<I...>) {
    return {tune::band_step(Arm<G, kStream<kRows[I]>>::kHeld.program)...};
}

// One process's measurement of a stream: its time at each count, and 1 if every output equals the first's.
template <hz::Graph (*G)()>
std::vector<double> pass(std::int64_t samples) {
    loops::Sizes sizes{};
    sizes[static_cast<std::size_t>(hz::kStreamLength)] = samples;
    std::vector<std::vector<std::byte>> out;
    const auto t = tune::rounds(arms_of<G>(std::make_index_sequence<kRows.size()>{}), sizes, out, kRounds);
    std::vector<double> v;
    bool same = true;
    for (std::size_t r = 0; r < kRows.size(); ++r) {
        v.push_back(tune::trimmed_mean(t[r]));
        same = same && out[r] == out[0];
    }
    v.push_back(same ? 1 : 0);
    return v;
}

std::vector<double> pass_both() {
    std::vector<double> v = pass<corpus::fm_oscillator>(kFmSamples);
    const std::vector<double> ladder = pass<corpus::ladder_bank>(kLadderSamples);
    v.insert(v.end(), ladder.begin(), ladder.end());
    return v;
}

constexpr std::size_t kStride = kRows.size() + 1;

// Per process, each count's worst ratio to a stream's best at any count; the median over the processes.
std::vector<std::vector<double>> worst_ratios(const std::vector<std::vector<double>>& passes) {
    std::vector<std::vector<double>> worst(kRows.size());
    for (const std::vector<double>& p : passes) {
        for (std::size_t r = 0; r < kRows.size(); ++r) {
            double w = 0;
            for (std::size_t s = 0; s < 2; ++s) {
                const double* at = &p[s * kStride];
                w = std::max(w, at[r] / *std::min_element(at, at + kRows.size()));
            }
            worst[r].push_back(w);
        }
    }
    return worst;
}

} // namespace

int main() {
    tune::heading("band_stream_rows: the FM oscillator and the ladder bank, in stream bands of 0 to 128 samples");
    std::printf("  the machine's band_stream_rows: %d\n", tune::kBase.band_stream_rows);
    const auto chunk = std::array{chunks_of<corpus::fm_oscillator>(std::make_index_sequence<kRows.size()>{}),
                                  chunks_of<corpus::ladder_bank>(std::make_index_sequence<kRows.size()>{})};
    const auto passes = tune::in_processes(kProcesses, pass_both);
    const std::vector<std::vector<double>> worst = worst_ratios(passes);
    for (std::size_t s = 0; s < 2; ++s) {
        std::printf("  %s\n", s == 0 ? "FM oscillator, 2^22 samples" : "ladder bank, 16 voices, 2^16 samples");
        for (std::size_t r = 0; r < kRows.size(); ++r) {
            std::vector<double> t;
            bool same = true;
            for (const std::vector<double>& p : passes) {
                t.push_back(p[s * kStride + r]);
                same = same && p[s * kStride + kRows.size()] == 1;
            }
            std::printf("    band_stream_rows %3d: %10.1f us, %4d rows a chunk (0: no band)%s\n", kRows[r], tune::median(t),
                        chunk[s][r], same ? "" : "  (output DIFFERS)");
        }
    }
    std::array<double, kRows.size()> at{};
    double least = 1e300;
    double noise = 0;
    std::size_t pick = 0;
    for (std::size_t r = 0; r < kRows.size(); ++r) {
        at[r] = tune::median(worst[r]);
        std::printf("  %3d rows: at worst %.3fx a stream's best (%.3f-%.3f over %d processes)\n", kRows[r], at[r],
                    *std::min_element(worst[r].begin(), worst[r].end()), *std::max_element(worst[r].begin(), worst[r].end()),
                    kProcesses);
        if (at[r] < least) {
            least = at[r];
            pick = r;
            noise = 0.49 * tune::spread(worst[r]) / std::sqrt(kProcesses);
        }
    }
    // The least worst count is the pick. The counts within kTol (or twice the median's standard error, if more) of it
    // are printed, for how sharp the pick is: their smallest is the plateau's edge, not a safe choice (the header).
    const double within = least + std::max(kTol, 2 * noise);
    std::size_t first = kRows.size();
    std::size_t last = 0;
    for (std::size_t r = 0; r < kRows.size(); ++r) {
        if (at[r] <= within) {
            first = std::min(first, r);
            last = r;
        }
    }
    std::printf("  flat from %d to %d rows (within %.3f of the least worst, %.3f)\n", kRows[first], kRows[last], within - least, least);
    tune::tuned("band_stream_rows", kRows[pick]);
}
