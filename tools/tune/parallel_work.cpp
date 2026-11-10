// Plan/23 P6 (6.1): `Machine::parallel_work` — the statement-points (points of a stage's domain times its statements)
// below which a worker's share of a split stage no longer pays for the dispatch and the caches the split leaves cold.
// `Plan/probes/parallel_threshold.cpp` measured it on the old tree's headers (`Plan/archive/parallel/04` §1); this is
// the same measurement on this tree.
//
// Four kinds of stage, each planned for the base machine with every split allowed (`parallel_work` 1) and no bands
// (`band_bytes` 0: stage splits alone): an elementwise chain (`a*b + b`) over 2^12 to 2^22 samples, a fold of 256
// columns over 16 to 16384 rows, conv's 16 taps over 2^12 to 2^20 samples, and square matrix products of 64 to 512.
// Each runs hot (back to back, the workers spinning between runs), serial and split among k = 2, 4, 8, ... workers up
// to the probe's argument (default: the machine's cores, at most the hardware's threads): the median of 11
// measurements each; a split run's output must equal the serial run's bit for bit.
//
// How it decides (`Plan/archive/parallel/01` §2): a split loses where it is more than 3 % slower than serial; the
// threshold is the smallest power of two above every losing split's statement-points per worker (its largest stage's
// work over k), so that no measured split with that much a worker loses. zen2's 2^18, measured with 16 workers.
//   clang++-22 -std=c++2c -O3 -march=native -DNDEBUG -fconstexpr-steps=268435456 -ffp-contract=off -fno-math-errno \
//     -Wno-psabi -DHEINZEL_HAVE_THREADS -pthread -I include -I tests/support -I tools/tune tools/tune/parallel_work.cpp \
//     -o parallel_work && taskset -c 0-3 ./parallel_work 4
// (on zen2 four workers at most, the box's load limit; on the M1 unpinned, its eight performance cores.)
#if !defined(HEINZEL_HAVE_THREADS)
#error "parallel_work.cpp splits stages among threads: build it with -DHEINZEL_HAVE_THREADS -pthread"
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

#include "heinzel/runtime/thread_pool.hpp"

#include "corpus_graphs.hpp"
#include "planned.hpp"
#include "probe.hpp"

namespace hz = heinzel;
namespace loops = heinzel::loops;

namespace {

constexpr hz::Machine kSplits = [] {
    hz::Machine m = tune::kBase;
    m.parallel_work = 1;
    m.band_bytes = 0;
    return m;
}();

template <hz::Graph (*G)()>
struct Arm {
    static constexpr planned::Held kHeld{planned::program_of(G(), kSplits)};
};

// The workers a split run gets: a pool as large as the most asked for, which says it has `k`.
std::unique_ptr<hz::ThreadPool> pool;
struct Capped {
    unsigned k = 1;
    unsigned size() const { return k; }
    template <class Task>
    void run(unsigned count, Task& task) {
        pool->run(count, task);
    }
};
Capped capped;

// Every split measured: a worker's statement-points, and whether it lost.
struct Seen {
    std::int64_t per_worker;
    bool loses;
};
std::vector<Seen> seen;

// The work of the program's largest split stage, in statement-points (0: nothing is split).
std::int64_t split_work(const loops::Program& p, const loops::Sizes& sizes) {
    std::int64_t most = 0;
    for (const loops::Stage& s : p.stages) {
        if (s.split < 0) {
            continue;
        }
        std::int64_t points = 1;
        for (const hz::Extent e : s.axes) {
            points *= loops::resolved(e, sizes);
        }
        most = std::max(most, points * s.count);
    }
    return most;
}

template <const loops::Program& P>
struct Split {
    static inline loops::Workspace<P>* at = nullptr;
    static inline loops::Feeds feeds{};

    [[gnu::noinline]] static void serial() { loops::run<P>(feeds, *at); }
    [[gnu::noinline]] static void split() { loops::run<P>(feeds, *at, capped); }

    static void measure(const char* what, const loops::Sizes& sizes, unsigned most) {
        const std::int64_t work = split_work(P, sizes);
        if (work == 0) {
            std::printf("  %-28s nothing split\n", what);
            return;
        }
        std::vector<std::vector<float>> inputs;
        tune::fill<P>(feeds, inputs, sizes);
        const auto ws = std::make_unique<loops::Workspace<P>>(sizes);
        at = ws.get();
        serial();
        const std::vector<std::byte> want = tune::output_of<P>(*ws, sizes);
        const double one = tune::median_us(serial);
        std::printf("  %-28s %10lld points  serial %9.2f us", what, static_cast<long long>(work), one);
        for (unsigned k = 2; k <= most; k *= 2) {
            capped.k = k;
            split();
            const bool same = tune::output_of<P>(*ws, sizes) == want;
            const double t = tune::median_us(split);
            seen.push_back(Seen{work / k, t > 1.03 * one});
            std::printf(" | %u: %9.2f us %5.2fx%s", k, t, one / t, same ? "" : " (DIFFERS)");
        }
        std::printf("\n");
        at = nullptr;
    }
};

loops::Sizes sized(std::size_t symbol, std::int64_t n) {
    loops::Sizes sizes{};
    sizes[symbol] = n;
    return sizes;
}

template <int N>
void conv(unsigned most) {
    char what[64];
    std::snprintf(what, sizeof what, "conv 16 taps, %d samples", N);
    Split<Arm<corpus::conv<N, 16>>::kHeld.program>::measure(what, loops::Sizes{}, most);
}

template <int S>
void matmul(unsigned most) {
    char what[64];
    std::snprintf(what, sizeof what, "matmul %d", S);
    Split<Arm<corpus::matmul<S, S, S>>::kHeld.program>::measure(what, loops::Sizes{}, most);
}

} // namespace

int main(int argc, char** argv) {
    const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    const unsigned most = argc > 1 ? static_cast<unsigned>(std::atoi(argv[1]))
                                   : std::min(static_cast<unsigned>(tune::kBase.cores), threads);
    pool = std::make_unique<hz::ThreadPool>(most);
    tune::heading("parallel_work: stages split among workers, against serial");
    std::printf("  up to %u workers; the machine's parallel_work: %lld\n", most, static_cast<long long>(tune::kBase.parallel_work));
    using Elem = Arm<corpus::elem>;
    using Rows = Arm<corpus::rowsum<256>>;
    for (std::int64_t n = std::int64_t{1} << 12; n <= (std::int64_t{1} << 22); n *= 4) {
        char what[64];
        std::snprintf(what, sizeof what, "elem, %lld samples", static_cast<long long>(n));
        Split<Elem::kHeld.program>::measure(what, sized(static_cast<std::size_t>(hz::kStreamLength), n), most);
    }
    for (std::int64_t r = 16; r <= 16384; r *= 4) {
        char what[64];
        std::snprintf(what, sizeof what, "256 columns folded, %lld rows", static_cast<long long>(r));
        Split<Rows::kHeld.program>::measure(what, sized(1, r), most);
    }
    conv<4096>(most), conv<16384>(most), conv<65536>(most), conv<262144>(most), conv<1048576>(most);
    matmul<64>(most), matmul<128>(most), matmul<256>(most), matmul<512>(most);

    std::int64_t lost = 0;
    std::int64_t least = 0;
    for (const Seen& s : seen) {
        lost = s.loses ? std::max(lost, s.per_worker) : lost;
        least = least == 0 ? s.per_worker : std::min(least, s.per_worker);
    }
    std::int64_t threshold = 1;
    while (threshold <= lost || (lost == 0 && threshold * 2 <= least)) {
        threshold *= 2;
    }
    tune::tuned("parallel_work", threshold);
}
