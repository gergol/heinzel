#pragma once

// What the threshold probes share (`README.md`): the machine they plan for, a program timed through the runner on one
// workspace, the child processes they measure in, and the machine-readable line each prints a field, `tuned <field>
// <value>`.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <utility>

#include <sys/wait.h>
#include <unistd.h>
#include <memory>
#include <vector>

#include "heinzel/loops/layout.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/loops/workspace.hpp"
#include "heinzel/schedule/machine.hpp"
// `heinzel::host`, where the configure step (`machine.cmake`) puts the header it generated on the path: its second stage
// plans the threshold probes for the machine its first measured.
#if __has_include("heinzel/host_machine.hpp")
#include "heinzel/host_machine.hpp"
#endif

// The machine every probe plans for, by its name in `schedule/machine.hpp`: zen2 unless the build says
// `-DHEINZEL_BENCH_MACHINE=m1_pro` (or `host`), as the benchmarks' option of that name does.
#ifndef HEINZEL_BENCH_MACHINE
#define HEINZEL_BENCH_MACHINE zen2
#endif
#define TUNE_NAME_OF(m) #m
#define TUNE_NAME(m) TUNE_NAME_OF(m)

namespace tune {

namespace hz = heinzel;
namespace loops = heinzel::loops;

inline constexpr const hz::Machine& kBase = hz::HEINZEL_BENCH_MACHINE;
inline constexpr const char* kBaseName = TUNE_NAME(HEINZEL_BENCH_MACHINE);

// What a probe measured, for `Plan/probes/thresholds/run.sh` and Plan 24's configure step to collect; `none` where its
// sweep shows no crossing, and the field keeps its default.
inline void tuned(const char* field, long long value) { std::printf("tuned %s %lld\n", field, value); }
inline void none(const char* field) { std::printf("tuned %s none\n", field); }

inline void heading(const char* what) { std::printf("machine %s\n%s\n", kBaseName, what); }

// The median of 11 measurements, in microseconds, each of as many calls of `f` as take about 2 ms.
inline double median_us(void (*f)()) {
    const auto t0 = std::chrono::steady_clock::now();
    f();
    const double once = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    const int reps = once > 2000.0 ? 1 : static_cast<int>(2000.0 / std::max(once, 0.01)) + 1;
    std::array<double, 11> t{};
    for (double& each : t) {
        const auto start = std::chrono::steady_clock::now();
        for (int r = 0; r < reps; ++r) {
            f();
        }
        each = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / reps;
    }
    std::sort(t.begin(), t.end());
    return t[5];
}

// Where an input starts within its allocation, in floats: a multiple of a cache line that every call moves on by one
// step, so that a probe's repeated measurements of one program draw the inputs' placement against the workspace's again
// (a heap that frees and allocates the same blocks would otherwise settle the 4K-aliasing luck once, for all rounds).
inline std::size_t next_offset() {
    static std::size_t calls = 0;
    return (calls++ * 16 * 7) % 1024;
}

// Pages held (and touched) while a program is timed, a different number each call: the kernel hands the workspace
// other physical pages each time instead of the ones the last call freed, so the L2's page colouring is drawn again too.
inline std::vector<char> page_padding() {
    static std::size_t calls = 0;
    return std::vector<char>((1 + (calls++ * 37) % 257) * 4096);
}

// Every input of `P` filled for `sizes` (floats, values in [-0.5, 0.5)), its pointer in `feeds`.
template <const loops::Program& P>
void fill(loops::Feeds& feeds, std::vector<std::vector<float>>& data, const loops::Sizes& sizes) {
    const std::size_t shift = next_offset();
    data.clear();
    data.reserve(P.buffers.size());
    for (const loops::Buffer& b : P.buffers) {
        if (b.home != loops::Home::Feed) {
            continue;
        }
        if (b.type != hz::ElemType::F32) {
            std::printf("an input that is not float: the probe fills floats only\n");
            std::exit(1);
        }
        const auto count = static_cast<std::size_t>(loops::element_count(b, sizes));
        data.emplace_back(count + 1024);
        float* at = data.back().data() + shift;
        for (std::size_t i = 0; i < count; ++i) {
            at[i] = 0.001f * static_cast<float>(i % 1000) - 0.5f;
        }
        feeds[static_cast<std::size_t>(b.feed)] = at;
    }
}

// The bytes of `P`'s first output, as the last run left them.
template <const loops::Program& P, class Work>
std::vector<std::byte> output_of(Work& work, const loops::Sizes& sizes) {
    const auto b = static_cast<std::size_t>(P.outputs[0]);
    const std::byte* at = work.bytes_of(b);
    const loops::Buffer& buffer = P.buffers[b];
    const auto bytes = static_cast<std::size_t>(loops::element_count(buffer, sizes) * loops::bytes_of(buffer.type));
    return std::vector<std::byte>(at, at + bytes);
}

// Program `P` timed on one workspace made for `sizes` (freed once timed): its median time a run, and in `out` its first
// output after the first run, for the probe to compare arms by.
template <const loops::Program& P>
struct Timed {
    static inline loops::Workspace<P>* at = nullptr;
    static inline loops::Feeds feeds{};

    [[gnu::noinline]] static void run() { loops::run<P>(feeds, *at); }

    static double us(const loops::Sizes& sizes, std::vector<std::byte>& out) {
        const std::vector<char> padding = page_padding();
        std::vector<std::vector<float>> inputs;
        fill<P>(feeds, inputs, sizes);
        const auto work = std::make_unique<loops::Workspace<P>>(sizes);
        at = work.get();
        run();
        out = output_of<P>(*work, sizes);
        const double t = median_us(run);
        at = nullptr;
        return t;
    }
};

// What the probes that decide by a gain share: a program timed in a number of rounds, the arms interleaved in each round
// (every other round in reverse, so that no arm always runs first) and every time on a workspace of its own, so that a
// placement's luck (the 4K-aliasing of two buffers, the heap's pages) is drawn again in each round instead of settled
// once. An arm's time is the trimmed mean of its rounds; the process it runs in matters as much (the address
// space's layout, and with it the pages and the cache sets a program lands on, differ from one process to the next by
// several percent, and no number of rounds within one process averages that out), so a probe makes its measurement in
// several processes (`in_processes`) and decides by their median; the spread between them is the noise to clear.

// `f` run in each of `n` child processes one after the other (fresh address spaces), the numbers it returns collected.
// A child prints nothing.
template <class F>
std::vector<std::vector<double>> in_processes(int n, F f) {
    std::vector<std::vector<double>> all;
    for (int i = 0; i < n; ++i) {
        int fd[2];
        if (pipe(fd) != 0) {
            std::perror("pipe");
            std::exit(1);
        }
        std::fflush(stdout);
        const pid_t child = fork();
        if (child == 0) {
            close(fd[0]);
            const std::vector<double> v = f();
            const auto bytes = static_cast<ssize_t>(v.size() * sizeof(double));
            _exit(write(fd[1], v.data(), static_cast<std::size_t>(bytes)) == bytes ? 0 : 1);
        }
        close(fd[1]);
        std::vector<double> v;
        double x;
        while (read(fd[0], &x, sizeof x) == static_cast<ssize_t>(sizeof x)) {
            v.push_back(x);
        }
        close(fd[0]);
        int status = 0;
        waitpid(child, &status, 0);
        if (v.empty() || status != 0) {
            std::printf("a measuring process failed\n");
            std::exit(1);
        }
        all.push_back(std::move(v));
    }
    return all;
}

using TimeFn = double (*)(const loops::Sizes&, std::vector<std::byte>&);

inline double quantile(std::vector<double> v, double q) {
    std::sort(v.begin(), v.end());
    const double at = q * static_cast<double>(v.size() - 1);
    const auto lo = static_cast<std::size_t>(at);
    const std::size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (at - static_cast<double>(lo));
}
inline double median(const std::vector<double>& v) { return quantile(v, 0.5); }
// The mean of the middle 60 %: a placement's luck moves a time by several percent in either direction from call to call
// (bimodally), which a median of few draws follows and a mean of many averages, and a disturbance's spikes are cut off.
inline double trimmed_mean(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t cut = v.size() / 5;
    double sum = 0;
    for (std::size_t i = cut; i < v.size() - cut; ++i) {
        sum += v[i];
    }
    return sum / static_cast<double>(v.size() - 2 * cut);
}
// The 10th to 90th percentile, relative to the median: how far the runs of one measurement differ.
inline double spread(const std::vector<double>& v) { return (quantile(v, 0.9) - quantile(v, 0.1)) / median(v); }

// Every arm's time in each round (`[arm][round]`); in `outs[arm]` the output of its first run.
inline std::vector<std::vector<double>> rounds(const std::vector<TimeFn>& arms, const loops::Sizes& sizes,
                                               std::vector<std::vector<std::byte>>& outs, int count) {
    std::vector<std::vector<double>> t(arms.size());
    outs.assign(arms.size(), {});
    for (int r = 0; r < count; ++r) {
        for (std::size_t k = 0; k < arms.size(); ++k) {
            const std::size_t a = r % 2 == 0 ? k : arms.size() - 1 - k;
            std::vector<std::byte> out;
            t[a].push_back(arms[a](sizes, out));
            if (r == 0) {
                outs[a] = std::move(out);
            }
        }
    }
    return t;
}

// The rows a chunk of the program's first band (0: it runs no band).
constexpr std::int32_t band_step(const loops::Program& p) { return p.bands.empty() ? 0 : p.bands[0].step; }

} // namespace tune
