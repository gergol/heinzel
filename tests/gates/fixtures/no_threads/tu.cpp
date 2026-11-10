// What `gate_no_threads_without_the_option` compiles: programs the planner split among workers, run by `loops::run`
// without `HEINZEL_HAVE_THREADS` — serially, and split among workers that run their ranges in order (so the worker
// path — a split stage, a split band, a stage split along the time its feed's past is read on — is compiled too); or,
// for the would-object twin, with a pool.
#include "heinzel/compile.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/lower.hpp"
#include "heinzel/loops/planner/plan.hpp"
#include "heinzel/loops/run.hpp"
#if defined(NO_THREADS_TU_WITH_A_POOL)
#include "heinzel/runtime/thread_pool.hpp"
#endif

#include "well_formed.hpp"

namespace hz = heinzel;

namespace {
// Work enough that a split pays for itself at run time.
constexpr int kRows = 256;
constexpr int kColumns = 4096;
constexpr hz::Graph kGraph = [] {
    hz::Input a{"a", {kRows, kColumns}};
    hz::Input b{"b", {kRows, kColumns}};
    hz::Expr rows = (a * b).sum(1, hz::keepdims); // (a map reading `a` again would band the two: `Plan/21-row-bands`)
    return hz::finish(hz::Output{"y"} = rows + 1.0);
}();
constexpr hz::Compiled kCompiled = hz::compile(kGraph, hz::zen2);
constexpr bool splits_a_stage(const hz::loops::Program& p) {
    for (const hz::loops::Stage& stage : p.stages) {
        if (stage.split >= 0) {
            return true;
        }
    }
    return false;
}
static_assert(splits_a_stage(kCompiled.program), "the check is about a program the planner split: it must have a split stage");

// A blur of 8 MB: banded, the band split among workers.
constexpr hz::Graph kBlurred = [] {
    hz::Input x{"x", {1024, 2048}};
    hz::Input wh{"wh", {1, 1, 3}};
    hz::Input wv{"wv", {1, 3, 1}};
    hz::Expr bx = (x.unfold(1, 3) * wh).sum(2);
    return hz::finish(hz::Output{"y"} = (bx.unfold(0, 3) * wv).sum(1));
}();
constexpr hz::Compiled kBanded = hz::compile(kBlurred, hz::zen2);
static_assert(!kBanded.program.bands.empty() && kBanded.program.bands[0].parallel, "and a program whose band the planner split");

// A filter over a feed's past split along time: worker 0 runs the head samples.
constexpr hz::Graph kFir = [] {
    constexpr hz::Index n{};
    hz::Input x{"x", hz::Signal{1 << 16}};
    hz::Graph g = hz::finish(hz::Output{"y"} = x + 0.5 * x[n - 1] + 0.25 * x[n - 3]);
    (void)hz::infer(g);
    return g;
}();
// (held as a struct's one member: the runner's template argument then names a subobject, `CLAUDE.md`)
struct Held {
    hz::loops::Program program;
};
constexpr Held kFirSplitHeld = [] {
    hz::loops::Schedule d = *hz::loops::planner::schedule_of(kFir, hz::zen2);
    d.stages[0].parallel = 0; // along time, its one loop
    d.parallel_work = 1;
    return Held{hz::loops::lower(kFir, d)};
}();
constexpr const hz::loops::Program& kFirSplit = kFirSplitHeld.program;
static_assert(kFirSplit.stages[0].head > 0 && kFirSplit.stages[0].split == kFirSplit.stages[0].time_loop,
              "and a stage split along the time its feed's past is read on");
static_assert(hz::loops::accepted<kCompiled.program>() && hz::loops::accepted<kBanded.program>() && hz::loops::accepted<kFirSplit>());

// Workers that run every range on the calling thread, in order — as many as the caller says, so the optimizer
// cannot prove a split never happens and drop the worker path this check is about.
struct InOrder {
    unsigned n;
    unsigned size() const { return n; }
    template <class Task>
    void run(unsigned count, Task& task) {
        for (unsigned w = 0; w < count; ++w) {
            task(w);
        }
    }
};
} // namespace

extern "C" void heinzel_split_program_run(const float* a, const float* b, float* y, unsigned threads) {
    hz::loops::Workspace<kCompiled.program> work(hz::loops::Sizes{});
    const hz::loops::Feeds feeds{a, b};
#if defined(NO_THREADS_TU_WITH_A_POOL)
    hz::ThreadPool pool(4);
    hz::loops::run<kCompiled.program>(feeds, work, pool);
#else
    hz::loops::run<kCompiled.program>(feeds, work);
    InOrder workers{threads};
    hz::loops::run<kCompiled.program>(feeds, work, workers);
#endif
    const float* made = work.data<float>(static_cast<std::size_t>(kCompiled.program.outputs[0]));
    for (int i = 0; i < kRows; ++i) { // (the row sums: one a row)
        y[i] = made[i];
    }
}

extern "C" void heinzel_split_band_run(const float* x, const float* wh, const float* wv, unsigned threads) {
    hz::loops::Workspace<kBanded.program, 16> work(hz::loops::Sizes{}); // band buffers for up to 16 workers
    InOrder workers{threads};
    hz::loops::run<kBanded.program>(hz::loops::Feeds{x, wh, wv}, work, workers);
}

extern "C" void heinzel_split_fir_run(const float* x, unsigned threads) {
    hz::loops::Workspace<kFirSplit> work(hz::loops::Sizes{});
    InOrder workers{threads};
    hz::loops::run<kFirSplit>(hz::loops::Feeds{x}, work, workers);
}
