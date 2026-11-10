// Tutorial chapter 01 — "getting started" (tutorial/01-getting-started.md), built in THIS tree's
// language, run on `Executor`, and its samples printed. It is the first of the twelve tutorial programs M8.3 asks
// be RUN and output-compared (the twelfth, 12_python.py, is M9.1's — the eleven-vs-twelve split the
// plan makes explicit).
//
// The chapter's one graph is y = a*x + 1: `x` a named input signal, `a` a runtime scalar knob, `1`
// a literal that casts down to the graph's f32. In the old tree `a` is a `Param` set by
// `ex.param("a") = 2.0` between runs; in THIS tree — as the conditionals port already established for
// M8.3 — a runtime scalar knob is an ORDINARY named input fed a constant-valued stream on the MW
// multi-input surface. No new op, no new value category; `a*x + 1.0` lowers to the M4 codes op::Mul
// and op::Add that were checked when M4 closed. The chapter's real subject — that the whole compiler
// runs under consteval and only the specialized runner survives — is true of this tree by
// construction: `Executor` analyses the graph at compile time and a graph that did not resolve is a build
// failure, not a test that runs and reports one.
//
// The companion `tutorial_01_there.cpp` builds the same y = a*x + 1 on the old tree at c4012b8 with a
// real `Param a` and prints the same 256 samples in the same format. A test runs both and compares
// numerically (the two trees' IR types differ deliberately, so only the numbers are comparable).
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t kBlock = 256; // the chapter's N
constexpr double kA = 2.0;          // the chapter's runtime knob value (ex.param("a") = 2.0)

// y = a*x + 1. `a` is a named input (a runtime knob), not a literal; the literal 1.0 casts down to
// the graph's f32 exactly as the chapter's `+ 1.0f` does.
//
// Each input is declared with what it is — here a signal of the chapter's N samples, f32 unless told
// otherwise — and the result is an `Output` the graph is made from. No context, nothing threaded.
constexpr Graph build() {
    Input x{"x", Signal{kBlock}};
    Input a{"a", Signal{kBlock}};
    Output y{"y"};
    y = a * x + 1.0;
    return y;
}

} // namespace

int main() {
    // The chapter's own signal, computed through float so its bits match the old tree's f32 input.
    std::array<double, kBlock> x{};
    for (std::size_t i = 0; i < kBlock; ++i) {
        x[i] = static_cast<double>(0.25f * static_cast<float>(i) - 8.0f);
    }
    // The knob, fed the same value every sample (one number per sample — the graph never sees it as
    // anything but a scalar).
    std::array<double, kBlock> a{};
    a.fill(kA);

    const testing::Outputs y = testing::executed<build>({{"x", x.data()}, {"a", a.data()}});
    for (std::size_t n = 0; n < kBlock; ++n) {
        std::printf("%.9e\n", y[0][n]);
    }
    return 0;
}
