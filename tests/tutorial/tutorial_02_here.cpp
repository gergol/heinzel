// Tutorial chapter 02 — "the DSL" (tutorial/02-the-dsl.md), built in THIS tree's language, run on
// `Executor`, and its samples printed. The chapter is four small graphs; this program grows one graph-family at a
// time (M8.3, S5 — one graph per commit, bisectable), each RUN and output-compared to the same graph
// on the old tree at c4012b8 (`tutorial_02_there.cpp`). Per the M8.3 frontend-DSL decision
// (2026-09-04 11:58) every spelling this needs LOWERS to an already-checked M4/M5 op; nothing here
// adds an operation.
//
// Graph 1 — elementwise math + the frozen promotion rule:
//     y = tanh(x*a) + sqrt(|x|) - log(|x|+1) + clamp(x,-1,1)*2
// `a` is the old tree's `Param a`; here — as ch01 and the conditionals port established — a runtime
// scalar knob is an ordinary named input fed a constant-valued stream on the MW surface. The literals
// (`1`, `2`) are doubles that cast DOWN to the graph's f32: the graph never silently becomes double.
//
// Graph 2 — comparisons + where (the branchless blend):
//     relu_like = where(x > thr, x, 0)   and the raw 0/1 mask = (x > thr)
// The old tree's one two-output graph is two single-output graphs here, as in the conditionals port;
// the two streams are printed relu_like then mask.
#include <array>
#include <cstddef>
#include <cstdio>
#include <string_view>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t N = 32;  // the chapter's elementwise length
constexpr double kA = 0.7f;    // graph 1's runtime knob (ex.param("a") = 0.7f)
constexpr double kThr = 0.5;   // graph 2's runtime knob (ex.param("thr") = 0.5f)

// Graph 1: the elementwise math graph. sqrt, clamp and abs are the newly-spelled composed helpers;
// tanh and log were spelled for earlier ports. Each names or composes a closed op.
constexpr Graph build_math() {
    Input x{"x", Signal{N}};
    Input a{"a", Signal{N}}; // the knob, a named input fed a constant
    Expr soft = tanh(x * a) + sqrt(abs(x)) - log(abs(x) + 1.0);
    Expr clamped = clamp(x, -1.0, 1.0) * 2.0; // 2.0 promotes down; the graph stays float
    return finish(Output{"y"} = soft + clamped);
}

// Graph 2a: relu_like = where(x > thr, x, 0). `thr` is a named input (a runtime knob), not a literal.
constexpr Graph build_relu() {
    Input x{"x", Signal{N}};
    Input thr{"thr", Signal{N}};
    return finish(Output{"relu_like"} = where(x > thr, x, 0.0));
}

// Graph 2b: the raw 0/1 mask = (x > thr) — a comparison IS a value. 1.0/0.0 are exactly representable.
constexpr Graph build_mask() {
    Input x{"x", Signal{N}};
    Input thr{"thr", Signal{N}};
    return finish(Output{"mask"} = x > thr);
}

// Graph 3 — broadcasting. Old ch02: x{R,C}, per-column bias{C} broadcasts over every row, gain a
// scalar knob; y = gain*x + bias. All three are tensors: x{R,C}, bias{1,C} (extent 1 on rows →
// broadcasts over rows) and gain{}, one number (each recorded with the stream of one sample this tree
// keeps innermost, the image_elementwise convention). Elementwise; the broadcast is numpy's extent-1
// rule, built and tested at milestone MB (the resolved batch-axis model, not the reserved boundary). No
// new op.
constexpr std::int32_t kR = 4; // old ch02's R=4 rows
constexpr std::int32_t kC = 8; // old ch02's C=8 columns
constexpr double kGain = 3.0;          // graph 3's runtime knob (ex.param("gain") = 3.0f)
constexpr Graph build_bias() {
    Input x{"x", {kR, kC}};
    Input bias{"bias", {1, kC}};
    Input gain{"gain", {}};
    return finish(Output{"y"} = gain * x + bias);
}

// Graph 4 — views: transpose / reshape / slice / iota (and, in 4b, unfold). Views record how to READ
// a value. Old ch02: x{R,C}; xt = x.transpose({1,0}).realize() (a materialised transpose {C,R}); xs =
// x.reshape(R*C).slice(2,5) (flatten, then 5 elements from offset 2); ramp = iota({N})*0.5. Here too x
// is a tensor {R,C} (recorded {R,C,1}) and the ramp is iota({N}); the transpose order {1,0} names the
// element axes, and the stream stays where it is.
constexpr Graph build_xt() {
    Input x{"x", {kR, kC}};
    return finish(Output{"xt"} = x.transpose({1, 0}).realize()); // {C,R,1}
}
constexpr Graph build_xs() {
    Input x{"x", {kR, kC}};
    Expr flat = x.reshape({kR * kC});           // {32,1}, streamed axis kept whole
    return finish(Output{"xs"} = flat.slice(0, 2, 5, 1)); // {5,1}: 5 elements from offset 2
}
constexpr Graph build_ramp() {
    return finish(Output{"ramp"} = iota({static_cast<std::int32_t>(N)}) * 0.5); // 0,0.5,..
}

// Graph 4b — the FIR view: (sig.unfold(3,1) * w).sum(1). unfold the signal's outer axis into
// (frame, tap) windows, weight by the 3-tap kernel (broadcast over frames and the stream), sum the
// tap axis away → a valid 3-tap FIR of length N-2. The kernel is a named input fed the constant taps,
// the stencil port's idiom (the old tree's `g.constant` tensor has no numeric bearing on the result).
constexpr std::int32_t kK = 3; // FIR tap count
constexpr Graph build_fir() {
    Input sig{"sig", {static_cast<std::int32_t>(N)}};
    Input w{"w", {1, kK}}; // extent 1 on frames → broadcast; 1 stream
    Expr windowed = sig.unfold(0, kK); // {N-2, K, 1}
    return finish(Output{"fir"} = (windowed * w).sum(1));  // {N-2, 1}
}

// Print an output's numbers in order: (row, column) row-major for the tensors, sample by sample for the
// signals.
void print_all(const testing::Outputs& y) {
    for (const double v : y[0]) {
        std::printf("%.9e\n", v);
    }
}

// Run one single-output {N} graph over one block and print its N samples. The signal `x` is fed to
// the input named "x"; the other input is a runtime knob, fed `knob` every sample.
template <auto Build>
void run_and_print(const std::array<double, N>& x, std::string_view knob_name, double knob) {
    std::array<double, N> k{};
    k.fill(knob);
    print_all(testing::executed<Build>({{"x", x.data()}, {knob_name, k.data()}}));
}

// Graph 3: broadcasting — a one-sample {R,C,1} image, its own feeds and (r,c) print order.
void run_graph3() {
    std::array<double, static_cast<std::size_t>(kR * kC)> x3{};
    std::array<double, static_cast<std::size_t>(kC)> bias3{};
    std::array<double, 1> gain3{kGain};
    for (std::int32_t i = 0; i < kR * kC; ++i) {
        x3[static_cast<std::size_t>(i)] = static_cast<double>(0.1f * static_cast<float>(i));
    }
    for (std::int32_t c = 0; c < kC; ++c) {
        bias3[static_cast<std::size_t>(c)] = static_cast<double>(10.0f * static_cast<float>(c));
    }
    print_all(testing::executed<build_bias>(
        {{"x", x3.data(), kR * kC}, {"bias", bias3.data(), kC}, {"gain", gain3.data()}}));
}

// Graph 4a: views — xt (materialised transpose), xs (reshape+slice), ramp (iota). One sample.
void run_graph4a() {
    std::array<double, static_cast<std::size_t>(kR * kC)> x4{};
    for (std::int32_t i = 0; i < kR * kC; ++i) {
        x4[static_cast<std::size_t>(i)] = static_cast<double>(1.0f + static_cast<float>(i));
    }
    // xt: transpose {R,C,1} -> {C,R,1}, printed (c,r) row-major to match _there's {C,R} buffer
    print_all(testing::executed<build_xt>({{"x", x4.data(), kR * kC}}));
    // xs: 5 elements from offset 2 of the flattened image
    print_all(testing::executed<build_xs>({{"x", x4.data(), kR * kC}}));
    // ramp: iota({N})*0.5 — a generator, no input
    print_all(testing::executed<build_ramp>());
}

// Graph 4b: the FIR — sig fed 0.5*i-1, w fed the 3-tap kernel. Output length N-2.
void run_graph4b() {
    std::array<double, N> sig{};
    for (std::size_t i = 0; i < N; ++i) {
        sig[i] = static_cast<double>(0.5f * static_cast<float>(i) - 1.0f);
    }
    std::array<double, static_cast<std::size_t>(kK)> w{static_cast<double>(0.25f), static_cast<double>(0.5f),
                                                       static_cast<double>(0.25f)};
    print_all(testing::executed<build_fir>({{"sig", sig.data(), static_cast<std::int32_t>(N)}, {"w", w.data(), kK}}));
}

} // namespace

int main() {
    // The chapter's own signal, computed through float so its bits match the old tree's f32 input. It
    // sweeps -3.0..+3.2, so it crosses the clamp bounds ±1, the gate threshold 0.5, and abs at 0.
    std::array<double, N> x{};
    for (std::size_t i = 0; i < N; ++i) {
        x[i] = static_cast<double>(0.2f * static_cast<float>(i) - 3.0f);
    }
    run_and_print<build_math>(x, "a", kA);    // graph 1
    run_and_print<build_relu>(x, "thr", kThr); // graph 2a: relu_like
    run_and_print<build_mask>(x, "thr", kThr); // graph 2b: mask
    run_graph3();                  // graph 3: broadcasting
    run_graph4a();                 // graph 4a: views (xt, xs, ramp)
    run_graph4b();                 // graph 4b: the FIR (unfold + kernel + sum)
    return 0;
}
