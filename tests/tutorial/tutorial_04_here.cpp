// Tutorial chapter 04 — "recurrences and streaming"
// (tutorial/04-recurrences-and-streaming.md), built in THIS tree's language, run on `Executor`, and its
// samples printed. Grown one graph-family at a time alongside `tutorial_04_there.cpp` (M8.3, S5), each
// output-compared to the same graph on the old tree at c4012b8.
//
// WHAT THIS CHAPTER IS, AND WHICH OF ITS PARTS REACH THIS TREE TODAY (Decision 27). The old chapter
// has four sections: (1) a one-pole recurrence written on the output, y[n] = (1-a)*x[n] + a*y[n-1],
// run whole-buffer and then again with a SEEDED initial history y[-1]=2; (2) the SAME recurrence
// compiled for one block and driven block-by-block with `set_streaming(true)`, shown bit-identical to
// the whole-buffer run; (3) a `Variable` — an internal recurrent signal that is never a graph output —
// chaining two one-pole sections; (4) the dsp batteries, an RBJ-designed biquad cascade from
// `heinzel/dsp/biquad.hpp`.
//
// Sections (1)'s recurrence and (3)'s Variable chain are M1 CORE and run now: a recurrence is spelled
// `Variable y; y[n] = ...y[n - 1]...` (the same spelling `tests/support/filters.hpp` and
// `biquad_here.cpp` already run). Section (4)'s biquad cascade needs the opt-in `dsp/` subtree, which
// this tree ports by-need at M8 S4 (Decision 15/21) — it is NOT part of this chapter's port.
//
// TWO OF THE OLD CHAPTER'S RUNTIME KNOBS, AND HOW THIS PORT RE-EXPRESSES THEM. The old executor exposes
// `set_initial("y", history)` and `set_streaming(true)` + `reset()` — a runtime that carries recurrence
// state ACROSS separate run() calls and lets the host seed it. This port does not use them: (1)'s
// compared run is the whole-buffer, zero-initial-history run (the recurrence's own past reads back to
// zero before sample 0, the FixedDelay clamp); (2)'s "block-wise streaming equals whole-buffer" is
// re-expressed as the graph landing on one set of numbers however this tree runs it: each graph is at
// `High`, the reference walk's arithmetic, and its run on the loop tree (`Executor`) is checked bit for bit
// against the walk.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;
constexpr Index n{};

namespace {

constexpr std::size_t N = 48;      // the chapter's signal length
constexpr double kA = 0.85f;       // the chapter's one-pole coefficient (a stable, well-damped pole)

// y[n] = (1 - a)*x[n] + a*y[n-1]. A leaky integrator (one-pole lowpass) written on the output: the
// output reads its own previous sample, which in this tree is a `Variable` defined by a recurrence.
// `a` is folded in as the constant coefficient it is here (the runtime-knob spelling was already
// shown in ch01/ch02); the recurrence, not the knob, is section (1)'s subject.
constexpr Graph build_leaky() {
    Input x{"x", Signal{N}};
    Variable y;
    y[n] = (1.0 - kA) * x + kA * y[n - 1];
    return finish(High, Output{"y"} = y);
}

// Section (3): a `Variable` used as an INTERNAL recurrent signal that is never a graph output. Two
// one-pole sections chain — s is internal state (0.7*x + 0.3*s[n-1]), only y (0.6*s + 0.4*y[n-1]) is
// the output. In this tree s is an ordinary `Variable` whose value feeds y's definition; s is
// never named as an output, so it stays internal exactly as the chapter's bare `Variable s` does.
constexpr Graph build_twopole() {
    Input x{"x", Signal{N}};
    Variable s;
    s[n] = 0.7f * x + 0.3f * s[n - 1];
    Variable y;
    y[n] = 0.6f * s + 0.4f * y[n - 1];
    return finish(High, Output{"y"} = y);
}

// Run a one-input recurrence over `x`, check it against the walk, and print its N samples.
template <auto Build>
void run_and_print(const std::array<float, N>& x) {
    std::array<float, N> y{};
    Executor<Build> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    testing::require_walked("y", y, testing::walked<Build>({{"x", x.data()}})[0]);
    for (const float v : y) {
        std::printf("%.9e\n", static_cast<double>(v));
    }
}

} // namespace

int main() {
    // The chapter's own signal, in float as the old tree's f32 input is.
    std::array<float, N> x{};
    for (std::size_t i = 0; i < N; ++i) {
        x[i] = std::sin(0.3f * static_cast<float>(i)) + 0.2f;
    }

    run_and_print<build_leaky>(x);   // section (1): the leaky one-pole
    run_and_print<build_twopole>(x); // section (3): the internal Variable two-pole chain
    return 0;
}
