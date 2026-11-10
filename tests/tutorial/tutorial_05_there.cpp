// Tutorial 05 — the RUNTIME-DIMENSION section, on the OLD tree at c4012b8 (MD S5 reference).
//
// The reference half of the tutorial-05 runtime-dimension port. This is the old tree's own
// `build_dynamic` from `tutorial/code/05_shape_generic_and_dynamic.cpp` verbatim in substance: a
// dynamic dimension `N` declared with `g.dim("N", {.multiple_of=4, .expect=256})`, a 1-D input `x{N}`,
// a `Param gain`, `y = x*gain`, run at N=64 and N=96 with gain=2.5 and `x[i]=0.3f*i-5` — the audio
// prepareToPlay() model: compile once, fix the extent at prepare(), run allocation-free.
//
// The old tree's headers are extracted from the shared object database at configure time (only the
// include/ tree is archived), so the section's body is reconstructed here rather than #included. The
// two shape-generic FIR sections of tutorial 05 are NOT reproduced on either side — they are a
// streaming FIR whose unfold is a lag along the streamed axis, the reserved innermost-axis `_delay`
// question (see tutorial_05_here and plan/MD-S5-NOTES.md). This program covers exactly what the `_here`
// port covers, so the comparison is apples-to-apples.
//
// Value-self-checking (the tutorial's own `check()`), so parity is same-PASS: this and the `_here` port
// both recover `y = 2.5*x` at both lengths.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

// The old section's dynamic graph verbatim: y = x*gain over x{N}, N resolved at prepare().
constexpr Graph build_dynamic() {
    GraphBuilder g;
    auto N = g.dim("N", {.multiple_of = 4, .expect = 256});
    Input x{g, {.name = "x", .shape = {N}}};
    Param gain{g, {.name = "gain"}};
    (x * gain).as_output("y");
    return g;
}

int g_fail = 0;
void check(const char* name, float err, float tol = 1e-6f) {
    const bool ok = std::isfinite(err) && err <= tol;
    if (!ok) {
        ++g_fail;
    }
    std::printf("  %-20s max_err=%.3e  %s\n", name, static_cast<double>(err), ok ? "ok" : "FAIL");
}
float fmaxerr(float e, float a, float b) { return std::fmax(e, std::fabs(a - b)); }

} // namespace

int main() {
    std::printf("05 shape-generic & dynamic (runtime-dimension section):\n");

    // dynamic N: one executor, re-prepared at two block sizes (the old section's 64 and 96).
    executor<build_dynamic()> ex;
    ex.param("gain") = 2.5f;
    float err = 0;
    for (const int n : {64, 96}) {
        std::vector<float> in(static_cast<std::size_t>(n));
        std::vector<float> out(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            in[static_cast<std::size_t>(i)] = 0.3f * static_cast<float>(i) - 5.0f;
        }
        ex.dim("N") = n;
        ex.prepare(); // validates the multiple_of constraint + the single allocation
        ex.input("x") = in.data();
        ex.output("y") = out.data();
        ex.run(); // allocation-free
        for (int i = 0; i < n; ++i) {
            err = fmaxerr(err, out[static_cast<std::size_t>(i)], 2.5f * in[static_cast<std::size_t>(i)]);
        }
    }
    check("dynamic dim", err);

    std::printf("%s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
