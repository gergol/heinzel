// Plan/23 P4 4.3f (sets `Machine::memory_bytes`): what a byte stored or loaded past the last-level cache costs a core,
// beside the same pass in the first-level cache. Maps over a run of float samples, as a stage of maps runs (vectorised,
// a sample at a time in lanes): a load of x alone (a sum), a store alone, x loaded and a float stored, x loaded and a
// double stored, x and a double loaded and a float stored. Each over a run of as many samples as the last-level cache
// has bytes (the argument, the L3's: a float buffer of four times the cache), but at least `kFloor`, and over one block
// of 256 fed again and again (in L1); cycles a sample, the least of 7 runs; the difference over the bytes a sample moves
// is what a byte through memory costs. Cycles as `latency.cpp` counts them (`cycles.hpp`).
//   clang++-22 -std=c++2c -O3 -march=native -ffp-contract=off tools/tune/memory.cpp -o memory &&
//     taskset -c 4 ./memory [the last-level cache's bytes]
//
// How it decides `Machine::memory_bytes` (printed as `tuned memory_bytes <bytes a cycle>`): each pass's cycles a
// sample through memory, the least of two rounds, against the bytes it moves, fitted by least squares through the
// origin over the five; bytes a cycle, rounded. The fit is to the whole pass, not to its cycles beyond L1's: the cost
// model prices a stage that streams through memory at the larger of its work and its bytes over `memory_bytes`
// (`schedule/cost.hpp`), not at their sum. (Beyond L1's alone zen2 fits 0.200 cycles a byte, 5.0 bytes a cycle.)
//
// zen2, core 4, 2026-10-09, 64 MB buffers (two rounds alike within 0.3), cycles a sample in L1 / through memory, and a
// byte's:
//   load a float (a sum)                 0.13 / 0.78         0.16  (6.2 bytes a cycle)
//   store a float                        0.25 / 1.17         0.23  (4.4: a line is read before it is written)
//   load a float, store a float          0.25 / 2.04-2.13    0.22-0.24
//   load a float, store a double         0.50 / 3.27-3.37    0.23-0.24
//   load a float and a double, store     0.67 / 3.31-3.62    0.17-0.19
// Fitted over the five (least squares through the origin, a pass's cycles through memory against its bytes): 0.241
// cycles a byte, 4.1 bytes a cycle. `Machine::memory_bytes` is 4.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "cycles.hpp"

namespace {

tune::Cycles counter;

// (sized at run time, not here: clang would try the initializer of a vector of millions as a constant expression first,
// for minutes under the probes' constexpr budget)
constexpr long kBlock = 256;
// The fewest samples a pass, whatever the argument: a float buffer of 256 MiB (1 GiB in all), past any cache the
// operating system does not report. macOS reports none past the L2, so the M1 Pro's argument is its 12 MiB L2, which
// left a buffer of 48 MiB, only twice its 24 MB system-level cache (the M1 Max's is 48 MB), and the reading high.
constexpr long kFloor = 64L << 20;
long g_whole = 0; // samples a pass through memory
float g_sink;
std::vector<float> x;
std::vector<float> y;
std::vector<double> d;

__attribute__((noinline)) void sum(long at, long n) {
    float s = 0;
#pragma clang loop vectorize(enable) interleave(enable)
    for (long i = at; i < at + n; ++i) {
        s += x[static_cast<std::size_t>(i)];
    }
    g_sink += s;
}
__attribute__((noinline)) void fill(long at, long n) {
    for (long i = at; i < at + n; ++i) {
        y[static_cast<std::size_t>(i)] = 0.5f;
    }
}
__attribute__((noinline)) void scale(long at, long n) {
    for (long i = at; i < at + n; ++i) {
        y[static_cast<std::size_t>(i)] = 2.0f * x[static_cast<std::size_t>(i)];
    }
}
__attribute__((noinline)) void widen(long at, long n) {
    for (long i = at; i < at + n; ++i) {
        d[static_cast<std::size_t>(i)] = 1.88 * static_cast<double>(x[static_cast<std::size_t>(i)]);
    }
}
__attribute__((noinline)) void narrow(long at, long n) {
    for (long i = at; i < at + n; ++i) {
        y[static_cast<std::size_t>(i)] = static_cast<float>(d[static_cast<std::size_t>(i)] + static_cast<double>(x[static_cast<std::size_t>(i)]));
    }
}

// Cycles a sample: the whole run once, or one block fed g_whole / kBlock times; the least of 7.
double cycles(void (*pass)(long, long), bool whole) {
    const auto run = [&] {
        if (whole) {
            pass(0, g_whole);
            return;
        }
        for (long b = 0; b < g_whole / kBlock; ++b) {
            pass(0, kBlock);
        }
    };
    return counter.least(7, run) / double(g_whole);
}

// A pass, the bytes a sample it moves, and its cycles a sample in L1 and through memory, the least of the rounds.
struct Pass {
    const char* name;
    void (*run)(long, long);
    double bytes;
    double l1 = 1e300;
    double memory = 1e300;
};

void row(Pass& p) {
    const double l1 = cycles(p.run, false);
    const double whole = cycles(p.run, true);
    std::printf("%-34s | %4.0f bytes | L1 %5.2f | memory %5.2f | %.3f cycles a byte (%.1f bytes a cycle)\n", p.name,
                p.bytes, l1, whole, (whole - l1) / p.bytes, p.bytes / (whole - l1));
    p.l1 = std::min(p.l1, l1), p.memory = std::min(p.memory, whole);
}

} // namespace

int main(int argc, char** argv) {
    g_whole = std::max(argc > 1 ? std::atol(argv[1]) : 0L, kFloor) / kBlock * kBlock;
    x.resize(static_cast<std::size_t>(g_whole)), y.resize(x.size()), d.resize(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = float((i * 7919) % 1000) / 1000.0f - 0.5f;
        d[i] = x[i];
    }
    std::array<Pass, 5> passes{{{"load a float (a sum)", sum, 4},
                                {"store a float", fill, 4},
                                {"load a float, store a float", scale, 8},
                                {"load a float, store a double", widen, 12},
                                {"load a float and a double, store", narrow, 16}}};
    std::printf("%ld samples a pass (%ld MB a float buffer)\n", g_whole, g_whole * 4 / 1000000);
    for (int round = 0; round < 2; ++round) {
        for (Pass& p : passes) {
            row(p);
        }
    }
    double bb = 0; // bytes squared
    double bw = 0; // bytes times the cycles through memory
    double be = 0; // bytes times those beyond L1's
    for (const Pass& p : passes) {
        bb += p.bytes * p.bytes, bw += p.bytes * p.memory, be += p.bytes * (p.memory - p.l1);
    }
    std::printf("fitted: %.3f cycles a byte through memory, %.2f bytes a cycle (beyond L1's alone %.3f, %.2f)\n",
                bw / bb, bb / bw, be / bb, bb / be);
    std::printf("tuned memory_bytes %lld\n", std::llround(bb / bw));
    std::printf("(%g)\n", double(g_sink));
}
