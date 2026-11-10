#pragma once

// Running one program serially and split among workers, and comparing what the two leave behind, byte for byte
// (`nest_parallel_test`, `nest_parallel_plan_test`, `nest_parallel_review_test`, `nest_band_parallel_test`,
// `streaming_test`).
// A split writes nothing two workers share and moves no sum, so every workspace buffer a run keeps whole — the
// outputs and every intermediate — must come out the same bits.
//
// The workers are an in-order runner (`InOrder`: every range on the calling thread, the LAST first, so a range
// that read what another range writes would read it unwritten) at several sizes, and the thread pool at two.

#include "heinzel/loops/run.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/loops/workspace.hpp"
#include "heinzel/support/fail.hpp"
#if defined(HEINZEL_HAVE_THREADS)
#include "heinzel/runtime/thread_pool.hpp"
#endif

#include "canaried.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace split_runs {

using namespace heinzel;

// Schedule `d` with stage `si` split along its outermost loop over domain axis `axis` (whatever the planner chose),
// every split as wide as its units allow.
constexpr loops::Schedule split(loops::Schedule d, std::size_t si, std::int32_t axis, std::int32_t line_bytes = 0) {
    loops::Schedule::Stage& stage = d.stages[si];
    stage.parallel = -1;
    for (std::size_t l = 0; l < stage.loops.size() && stage.parallel < 0; ++l) {
        stage.parallel = stage.loops[l].axis == axis ? static_cast<std::int32_t>(l) : -1;
    }
    if (stage.parallel < 0) {
        fail("split_runs: no loop walks that axis");
    }
    d.parallel_work = 1;
    d.line_bytes = line_bytes;
    return d;
}

// Calls `task(w)` for `w` from `count - 1` down to 0 on the calling thread; `most` is the widest split it ran,
// `launches` how many it ran and `ranges` how many ranges they made (the tests hold both to `predicted`).
struct InOrder {
    unsigned n{1};
    unsigned most{1};
    unsigned launches{0};
    unsigned ranges{0};
    unsigned size() const { return n; }
    template <class Task>
    void run(unsigned count, Task& task) {
        count = count > n ? n : count;
        most = count > most ? count : most;
        ++launches, ranges += count;
        for (unsigned w = count; w-- > 0;) {
            task(w);
        }
    }
};

#if defined(HEINZEL_HAVE_THREADS)
inline ThreadPool& pool_of(unsigned threads) {
    static ThreadPool four(4);
    static ThreadPool sixteen(16);
    return threads == 4 ? four : sixteen;
}
#endif

// Every feed of a program at `sizes`, each allocated at exactly its size (a read past its end is the sanitizer's
// to see), filled with numbers in [0.25, 2.25) (positive: a position or a divisor stays in range; no NaN, so a
// byte comparison is the comparison).
struct Fed {
    std::vector<std::vector<std::byte>> storage;
    loops::Feeds feeds{};
};
inline Fed fed(const loops::Program& p, const loops::Sizes& sizes) {
    Fed f{};
    f.storage.reserve(p.buffers.size());
    for (const loops::Buffer& b : p.buffers) {
        if (b.home != loops::Home::Feed) {
            continue;
        }
        const std::int64_t n = loops::element_count(b, sizes);
        std::vector<std::byte>& held = f.storage.emplace_back(static_cast<std::size_t>(n * loops::bytes_of(b.type)));
        for (std::int64_t k = 0; k < n; ++k) {
            const double v = 0.25 + static_cast<double>((static_cast<std::uint64_t>(k) * 2654435761u + static_cast<std::uint64_t>(b.feed) * 97u) % 1000u) / 500.0;
            b.type == ElemType::F32 ? void(reinterpret_cast<float*>(held.data())[k] = static_cast<float>(v)) : void(reinterpret_cast<double*>(held.data())[k] = v);
        }
        f.feeds[static_cast<std::size_t>(b.feed)] = held.data();
    }
    return f;
}

// Do both workspaces hold the same bytes — every buffer, and the silence, the gaps and the slack between them,
// which no run writes (a range run past its end would; `canaried` keeps a canary there) — but for a split band's
// buffers, whose copies each worker fills with its own chunks? Each copy's region is zeroed in both first; the gaps
// between the copies keep their canaries.
template <const loops::Program& P, unsigned Copies>
bool same_buffers(canaried::Workspace<P, Copies>& a, canaried::Workspace<P, Copies>& b) {
    for (std::size_t i = 0; i < P.buffers.size(); ++i) {
        const loops::Buffer& buffer = P.buffers[i];
        if (buffer.band < 0 || !P.bands[static_cast<std::size_t>(buffer.band)].parallel) {
            continue;
        }
        for (canaried::Workspace<P, Copies>* w : {&a, &b}) {
            for (unsigned c = 0; c < w->copies(); ++c) {
                const loops::ByteRange r = w->region(i, c);
                std::memset(r.begin, 0, static_cast<std::size_t>(r.end - r.begin));
            }
        }
    }
    return a.size_in_bytes() == b.size_in_bytes() && std::memcmp(a.arena(), b.arena(), a.size_in_bytes()) == 0;
}

// The launches and ranges a run of `P` makes among `size` workers, a split band's among at most `copies`: per split
// mark the runner's own rule (`loops::split_of` over the mark's iterations and work), so a mark the runner ignored
// shows as a launch too few. What `InOrder` counts must be this, run by run.
struct Launches {
    unsigned launches{0};
    unsigned ranges{0};
    constexpr bool operator==(const Launches&) const = default;
};
template <const loops::Program& P, std::size_t S>
void predict_stage(Launches& l, const loops::Sizes& sizes, unsigned size) {
    constexpr const loops::Stage& stage = P.stages[S];
    if constexpr (stage.split >= 0) {
        constexpr std::size_t axis = static_cast<std::size_t>(loops::node_at(P, stage.split).axis);
        const loops::Extents whole = loops::resolved_axes<P, S>(sizes);
        const std::int64_t n = whole[axis];
        const std::int64_t head = stage.head > 0 && stage.time_loop == stage.split ? (n < stage.head ? n : stage.head) : 0;
        const loops::Split split = loops::split_of(n - head, loops::split_unit<P, S>(), loops::work_of<P, S>(whole), P.parallel_work, size);
        l.launches += split.workers > 1 ? 1u : 0u, l.ranges += split.workers > 1 ? split.workers : 0u;
    }
}
template <const loops::Program& P, std::size_t B, std::size_t... I>
void predict_band(Launches& l, const loops::Sizes& sizes, unsigned size, unsigned copies, std::index_sequence<I...>) {
    constexpr const loops::BandInfo& band = P.bands[B];
    constexpr std::size_t first = static_cast<std::size_t>(band.first);
    constexpr std::size_t last = first + sizeof...(I) - 1;
    if constexpr (band.parallel) {
        const std::int64_t tail = loops::resolved(P.stages[last].axes[static_cast<std::size_t>(P.stages[last].band_axis)], sizes);
        const std::int64_t all = (loops::work_of<P, first + I>(loops::resolved_axes<P, first + I>(sizes)) + ...);
        const loops::Split split = loops::split_of((tail + band.step - 1) / band.step, 1, all, P.parallel_work, size < copies ? size : copies);
        l.launches += tail > 0 && split.workers > 1 ? 1u : 0u, l.ranges += tail > 0 && split.workers > 1 ? split.workers : 0u;
    }
}
template <const loops::Program& P>
Launches predicted(const loops::Sizes& sizes, unsigned size, unsigned copies) {
    Launches l{};
    [&]<std::size_t... S>(std::index_sequence<S...>) {
        // (a stage in a band runs with it: its own mark is the band's to take)
        ((loops::detail::band_of(P, S) < 0 ? predict_stage<P, S>(l, sizes, size) : void()), ...);
    }(std::make_index_sequence<P.stages.size()>{});
    [&]<std::size_t... B>(std::index_sequence<B...>) {
        (predict_band<P, B>(l, sizes, size, copies, std::make_index_sequence<static_cast<std::size_t>(P.bands[B].count)>{}), ...);
    }(std::make_index_sequence<P.bands.size()>{});
    return l;
}

// Runs `P` split — in order at 1, 2, 3, 7 and 16 workers, twice on each workspace, and on pools of 4 and 16 —
// each split workspace beside a serial one run as often, in lockstep (a run continues from the one before it,
// Plan/archive/14-dsl/05), and counts what differs — and, in order, each run's launches and ranges against `predicted`.
// `must_split`: some run cut a stage or a band into two or more ranges (else the test would pass by never splitting).
// `copies`: the workspaces' band buffers per split band (`canaried::Workspace`), which caps how many workers a split
// band gets.
template <const loops::Program& P, unsigned copies = 16>
int check(const char* what, const loops::Sizes& sizes, bool must_split) {
    const Fed f = fed(P, sizes);
    int wrong = 0;
    unsigned widest = 1;
    for (const unsigned n : {1u, 2u, 3u, 7u, 16u}) {
        InOrder workers{n};
        canaried::Workspace<P, copies> split(sizes);
        canaried::Workspace<P, copies> serial(sizes);
        const Launches want = predicted<P>(sizes, n, copies);
        for (int twice = 0; twice < 2; ++twice) {
            const Launches before{workers.launches, workers.ranges};
            canaried::run<P>(f.feeds, serial);
            canaried::run<P>(f.feeds, split, workers);
            if (!same_buffers(serial, split) && ++wrong <= 3) {
                std::printf("  %s: split among %u in order (run %d) differs from the serial run\n", what, n, twice + 1);
            }
            const Launches made{workers.launches - before.launches, workers.ranges - before.ranges};
            if (made != want && ++wrong <= 3) {
                std::printf("  %s: among %u in order (run %d) %u launches into %u ranges, its split marks say %u into %u\n", what, n, twice + 1,
                            made.launches, made.ranges, want.launches, want.ranges);
            }
        }
        widest = workers.most > widest ? workers.most : widest;
    }
#if defined(HEINZEL_HAVE_THREADS)
    for (const unsigned threads : {4u, 16u}) {
        canaried::Workspace<P, copies> split(sizes);
        canaried::Workspace<P, copies> serial(sizes);
        canaried::run<P>(f.feeds, serial);
        canaried::run<P>(f.feeds, split, pool_of(threads));
        if (!same_buffers(serial, split) && ++wrong <= 3) {
            std::printf("  %s: split on a pool of %u differs from the serial run\n", what, threads);
        }
    }
#endif
    if (must_split && widest < 2) {
        std::printf("  %s: never split\n", what);
        ++wrong;
    }
    return wrong;
}

} // namespace split_runs
