// Tutorial chapter 02 — "the DSL", built on the OLD tree at c4012b8, run, and its samples printed in
// the same format as `tutorial_02_here.cpp`. Grows one graph-family at a time alongside the `_here`
// program (M8.3, S5). The old tree's headers are pinned from the shared object database at configure
// time; nothing here opens /home/user/ctg3.
//
// Graph 1 — the elementwise math graph. `set_fast_math(false)` pins the arithmetic so the two sides
// cannot differ over reassociation; what remains is the transcendental family (tanh/sqrt/log), whose
// last-bit difference between the two implementations is the test's named allowance.
//
// Graph 2 — comparisons + where: relu_like = where(x > thr, x, 0), mask = (x > thr). Printed in that
// order to match the `_here` streams.
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr std::size_t N = 32;

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

// y = tanh(x*a) + sqrt(|x|) - log(|x|+1) + clamp(x,-1,1)*2, with `a` a runtime Param.
constexpr Graph build_math() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {N}};
    Param a{g, "a"};
    Output y{g, "y", {N}};
    auto soft = tanh(x * a) + sqrt(abs(x)) - log(abs(x) + 1.0f);
    auto clamped = clamp(x, -1.0f, 1.0f) * 2.0;
    y = soft + clamped;
    return g;
}

inline constexpr auto kMath = compile<build_math(), kTarget>();

// Graph 2: the gate, one graph with two outputs — where(x>thr,x,0) and the raw mask.
constexpr Graph build_gate() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, "x", {N}};
    Param thr{g, "thr"};
    where(x > thr, x, 0.0f).as_output("relu_like");
    (x > thr).as_output("mask");
    return g;
}

inline constexpr auto kGate = compile<build_gate(), kTarget>();

// Graph 3: broadcasting — x{R,C}, per-column bias{C} broadcasts over rows, gain a runtime Param.
constexpr std::int32_t R = 4;
constexpr std::int32_t Cw = 8;
constexpr Graph build_bias() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, {.name = "x", .shape = {R, Cw}}};
    Input bias{g, {.name = "bias", .shape = {Cw}}};
    Param gain{g, {.name = "gain"}};
    (gain * x + bias).as_output("y");
    return g;
}

inline constexpr auto kBias = compile<build_bias(), kTarget>();

// Graph 4a: views — xt = transpose+realize {C,R}, xs = reshape+slice {5}, ramp = iota*0.5 {N}.
constexpr Graph build_views_a() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, {.name = "x", .shape = {R, Cw}}};
    x.transpose({1, 0}).realize().as_output("xt");
    x.reshape(R * Cw).slice(2, 5).as_output("xs");
    (g.iota({static_cast<int>(N)}) * 0.5f).as_output("ramp");
    return g;
}

inline constexpr auto kViewsA = compile<build_views_a(), kTarget>();

// Graph 4b: the FIR view — (sig.unfold(3,1) * kernel).sum(1), a valid 3-tap FIR of length N-2.
constexpr float kKernel[3] = {0.25f, 0.5f, 0.25f};
constexpr Graph build_fir() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input sig{g, {.name = "sig", .shape = {static_cast<int>(N)}}};
    auto w = g.constant(std::span<const float>{kKernel}, {3});
    (sig.unfold(3, 1) * w).sum(1).as_output("fir");
    return g;
}

inline constexpr auto kFir = compile<build_fir(), kTarget>();

// Graph 3: broadcasting, printed over (r,c) row-major.
void run_graph3() {
    std::array<float, R * Cw> x3{};
    std::array<float, R * Cw> y3{};
    std::array<float, Cw> bias3{};
    for (std::int32_t i = 0; i < R * Cw; ++i) {
        x3[static_cast<std::size_t>(i)] = 0.1f * static_cast<float>(i);
    }
    for (std::int32_t c = 0; c < Cw; ++c) {
        bias3[static_cast<std::size_t>(c)] = 10.0f * static_cast<float>(c);
    }
    Executor<kBias, scalar_backend> ex;
    ex.input("x") = x3.data();
    ex.input("bias") = bias3.data();
    ex.param("gain") = 3.0f;
    ex.output("y") = y3.data();
    ex.run();
    for (std::int32_t i = 0; i < R * Cw; ++i) {
        std::printf("%.9e\n", static_cast<double>(y3[static_cast<std::size_t>(i)]));
    }
}

// Graph 4a: views — xt {C,R}, xs {5}, ramp {N}, printed in that order.
void run_graph4a() {
    std::array<float, R * Cw> x4{};
    std::array<float, R * Cw> xt{};
    std::array<float, 5> xs{};
    std::array<float, N> ramp{};
    for (std::int32_t i = 0; i < R * Cw; ++i) {
        x4[static_cast<std::size_t>(i)] = 1.0f + static_cast<float>(i);
    }
    Executor<kViewsA, scalar_backend> ex;
    ex.input("x") = x4.data();
    ex.output("xt") = xt.data();
    ex.output("xs") = xs.data();
    ex.output("ramp") = ramp.data();
    ex.run();
    for (std::int32_t i = 0; i < R * Cw; ++i) {
        std::printf("%.9e\n", static_cast<double>(xt[static_cast<std::size_t>(i)]));
    }
    for (std::int32_t i = 0; i < 5; ++i) {
        std::printf("%.9e\n", static_cast<double>(xs[static_cast<std::size_t>(i)]));
    }
    for (std::size_t i = 0; i < N; ++i) {
        std::printf("%.9e\n", static_cast<double>(ramp[i]));
    }
}

// Graph 4b: the FIR — sig fed 0.5*i-1, output length N-2.
void run_graph4b() {
    std::array<float, N> sig{};
    std::array<float, N - 2> fir{};
    for (std::size_t i = 0; i < N; ++i) {
        sig[i] = 0.5f * static_cast<float>(i) - 1.0f;
    }
    Executor<kFir, scalar_backend> ex;
    ex.input("sig") = sig.data();
    ex.output("fir") = fir.data();
    ex.run();
    for (std::size_t i = 0; i < N - 2; ++i) {
        std::printf("%.9e\n", static_cast<double>(fir[i]));
    }
}

} // namespace

int main() {
    std::array<float, N> x{};
    for (std::size_t i = 0; i < N; ++i) {
        x[i] = 0.2f * static_cast<float>(i) - 3.0f;
    }
    auto print = [](const std::array<float, N>& v) {
        for (std::size_t n = 0; n < N; ++n) {
            std::printf("%.9e\n", static_cast<double>(v[n]));
        }
    };

    { // graph 1
        std::array<float, N> y{};
        Executor<kMath, scalar_backend> ex;
        ex.input("x") = x.data();
        ex.param("a") = 0.7f;
        ex.output("y") = y.data();
        ex.run();
        print(y);
    }
    { // graph 2: relu_like then mask
        std::array<float, N> relu{};
        std::array<float, N> mask{};
        Executor<kGate, scalar_backend> ex;
        ex.input("x") = x.data();
        ex.param("thr") = 0.5f;
        ex.output("relu_like") = relu.data();
        ex.output("mask") = mask.data();
        ex.run();
        print(relu);
        print(mask);
    }
    run_graph3();  // graph 3: broadcasting
    run_graph4a(); // graph 4a: views (xt, xs, ramp)
    run_graph4b(); // graph 4b: the FIR
    return 0;
}
