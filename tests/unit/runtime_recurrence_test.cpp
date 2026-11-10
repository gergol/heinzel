// MU.2 (reopen item c) — a single-sample RECURRENCE along the RUNTIME-resolved streamed axis.
//
// Decision 42 (Q1 = A) defines the semantics; this is its first executable consequence: a recurrence
// whose block length N is resolved when the program is prepared runs, through ONE compiled program, at
// three distinct N, matching a hand oracle. This is the tutorial-10 one-pole's OFFLINE (one-block)
// path, which Decision 42 fixes as exact.
//
// It also pins the "lag depth vs block" fact Decision 42 states: a lag reads silence for n-D<0 and the
// value's own samples otherwise, for ANY depth — N = 3 is shorter than a plausible outer lag, with no
// out-of-bounds and no refusal owed. Each `prepare` is a fresh start, so each length is one block.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

#include "executed.hpp"

using namespace heinzel;
constexpr Index n{};

namespace {

constexpr Length kBlock{.multiple_of = 1, .min = 1, .expect = 256};
constexpr double kFeedback = 0.5; // a leaky integrator: y[n] = x[n] + 0.5*y[n-1]

// One mono input x{N}, N the runtime-resolved streamed axis; y is the recurrence over it. Compiled
// once, symbolic in N.
constexpr Graph build() {
    Input x{"x", Signal{kBlock}, ElemType::F64};
    Variable y;
    y[n] = x + kFeedback * y[n - 1];
    Graph g = finish(Output{"y"} = y);
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("runtime_recurrence: the graph did not infer");
    }
    return g;
}

static_assert(!is_static(build()), "the runtime-length recurrence graph is not static — N is a symbol");

// Run the leaky integrator over N samples, freshly prepared, and check it against the recurrence by hand
//   y[n] = x[n] + kFeedback * y[n-1]   (silence before n=0).
bool run_at(Executor<build>& ex, std::int32_t frames) {
    const std::size_t n = static_cast<std::size_t>(frames);
    std::vector<double> x(n);
    std::vector<double> y(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = 0.01 * static_cast<double>(i % 257) - 1.0;
    }
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.prepare(frames);
    ex.run(frames);

    double max_err = 0.0;
    double ref = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        ref = x[i] + kFeedback * ref;
        max_err = std::fmax(max_err, std::fabs(y[i] - ref));
    }
    return max_err < 1e-9;
}

} // namespace

int main() {
    // The same compiled program, run at distinct runtime lengths (re-prepared between them).
    Executor<build> ex;
    testing::held(ex);
    if (!run_at(ex, 256)) {
        fail("runtime_recurrence: N=256 did not match the hand oracle");
    }
    if (!run_at(ex, 64)) {
        fail("runtime_recurrence: N=64 did not match the hand oracle");
    }
    // A short block, N smaller than a plausible outer lag: still exact (silence before n=0 only).
    if (!run_at(ex, 3)) {
        fail("runtime_recurrence: N=3 did not match the hand oracle");
    }
    std::printf("runtime_recurrence ok\n");
    return 0;
}
