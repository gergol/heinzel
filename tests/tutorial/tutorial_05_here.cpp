// Tutorial 05 — the RUNTIME-DIMENSION section, ported to THIS tree's grown frontend (MD S5).
//
// Tutorial 05 (`tutorial/05-shape-generic-and-dynamic.md`) has three self-checking sections. Two of
// them — "declare now, bind at the end" and "one shape-generic graph, several signatures" — build a
// STREAMING FIR `(x.unfold(4,1) * h).sum(1)` over a 1-D signal. In the old tree the OUTER axis is the
// stream, so that unfold is a moving window ALONG THE STREAM — a lag along the streamed axis, which in
// this tree (streamed axis innermost) is the reserved innermost-axis `_delay` question the operator
// kept separate. Re-casting it as a static outer-axis stencil (the `stencil_unfold` convention) would
// change a streaming filter into a batch stencil — a semantics shift, not a faithful port. So those two
// sections are RECORDED RESERVED (reserved-`_delay`, not symbolic-width), per MD.5's reserved-recording
// discipline; the full disposition is docs/decisions.md (Decision 32 revision) and plan/MD-S5-NOTES.md.
//
// This program ports the third section — "dynamic dimensions" — which is the runtime-dimension half
// MD.5 names. The old `build_dynamic` is `y = x*gain` over a 1-D block whose length N is declared with
// constraints (a multiple of 4; a representative 256) and resolved at prepare(), run at N=64 and N=96.
// Pure elementwise scale — no unfold, no lag — so streaming along the runtime-resolved innermost N is
// clean and needs only the MD runtime-extent surface: a `Length` mints the symbol, and `Executor`'s
// `prepare` validates the bound size and runs the N-independent program for N samples.
//
// The knob `gain` is a named INPUT fed a constant across the block: this tree has no runtime Param
// (`constant()` is compile-time), so a knob is an input held every sample, as the dynamic_length and
// conditionals ports established. The port is value-self-checking exactly as the tutorial is — each
// section prints `max_err` against a hand reference and PASS/FAIL — so its parity with the old tree
// (`tutorial_05_there`, which runs the old tree's own dynamic section) is same-PASS: both recover
// `y = 2.5*x` at both lengths.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

// The declared length's id and the old section's own constraints: a whole multiple of 4, a
// representative 256. No minimum in the old section, so `min` stays 0.
constexpr std::int32_t kLengthId = kStreamLength; // the symbol a Length declares
constexpr Length kFrames{.multiple_of = 4, .min = 0, .expect = 256};

// `y = x * gain` over a 1-D block [N samples], N the runtime-resolved streamed axis. `x` and `gain`
// share the one length (the same `Length`); `gain` is a stream-constant knob, one number per sample held
// the same across the block. Both are 1-D so their per-sample element count is 1.
constexpr Graph build() {
    Input x{"x", Signal{kFrames}, ElemType::F64};
    Input gain{"gain", Signal{kFrames}, ElemType::F64};
    return finish(Output{"y"} = x * gain);
}

static_assert(!is_static(build()), "the dynamic graph is not static — N is a symbol");
static_assert(build().dims.size() == 1 && build().dims[0].id == kLengthId,
              "x and gain share the one declared length, recorded once");

int g_fail = 0;
void check(const char* name, double err, double tol = 1e-6) {
    const bool ok = std::isfinite(err) && err <= tol;
    if (!ok) {
        ++g_fail;
    }
    std::printf("  %-20s max_err=%.3e  %s\n", name, err, ok ? "ok" : "FAIL");
}

// Run the dynamic scale at one block length, freshly prepared, returning the max error against `gain * x`.
// The same block also runs at `High`, checked bit for bit against the walk.
template <std::int32_t Frames>
double run_dynamic(Executor<build>& ex, double gain) {
    const std::size_t n = static_cast<std::size_t>(Frames);
    std::vector<double> x(n);
    std::vector<double> g(n);
    std::vector<double> y(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = 0.3 * static_cast<double>(i) - 5.0; // the old section's own feed
        g[i] = gain;
    }
    ex.input("x") = x.data();
    ex.input("gain") = g.data();
    ex.output("y") = y.data();
    ex.prepare(Frames);
    ex.run(Frames);
    testing::executed<build, Frames>({{"x", x.data()}, {"gain", g.data()}});

    double err = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        err = std::fmax(err, std::fabs(y[i] - gain * x[i]));
    }
    return err;
}

} // namespace

int main() {
    std::printf("05 shape-generic & dynamic (runtime-dimension section; the FIR sections are recorded\n");
    std::printf("reserved — a streaming FIR is a lag along the innermost axis, the reserved _delay work):\n");

    // dynamic N: one compiled graph, re-prepared at two block sizes (the old section's 64 and 96).
    Executor<build> ex;
    testing::held(ex);
    double err = run_dynamic<64>(ex, 2.5);
    err = std::fmax(err, run_dynamic<96>(ex, 2.5));
    check("dynamic dim", err);

    std::printf("%s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
