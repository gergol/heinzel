#pragma once

// Graphs the loop-tree work plans and runs outside the test that owns them: copies of every gated benchmark row's
// builder and of the nest tests' median, recurrences, FIR, delay line, padded value and tall contraction
// (`Plan/archive/13-loop-tree/04` §4). Test and benchmark `.cpp` files cannot be included, so the builders live here —
// graph builders only, through the DSL: nothing of any runner or planner, so `loops_alone` admits this header.

#include <bit>
#include <cstdint>

#include "heinzel/dsp/oscillator.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

namespace corpus {

using namespace heinzel;

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("corpus_graphs: a graph did not resolve");
    }
    return g;
}

constexpr Index n{};

// ---- the gated benchmark rows (their `_f32` builders) --------------------------------------------------

constexpr Graph elem() { // bench_core: y = a*b + b over a length bound at run time
    constexpr Length samples{.multiple_of = 1, .min = 1, .expect = 262144};
    Input a{"a", Signal{samples}};
    Input b{"b", Signal{samples}};
    Output y{"y"};
    y = a * b + b;
    return analysed(y);
}

template <int C>
constexpr Graph rowsum() { // bench_core: C columns folded per row, the rows bound at run time
    // its length is symbol 1, which no declaration spells (DS-18)
    const DimDecl r{1, DimConstraints{1, 1}};
    Expr x = detail::input("x", Shape{C, Extent{Dim{r.id}}}, ElemType::F32, r);
    Output y{"y"};
    y = x.sum(0);
    return analysed(y);
}

template <int M, int K, int N>
constexpr Graph matmul() { // bench_contraction
    Input a{"a", {M, K}};
    Input b{"b", {K, N}};
    Output y{"y"};
    y = einsum("ik,kj->ij", a, b);
    return analysed(y);
}

constexpr Graph linear_relu() { // bench_ml_layers
    Input x{"x", {128, 512}};
    Input w{"w", {512, 512}};
    Input b{"b", {1, 512}};
    Output y{"y"};
    y = max(einsum("ik,kj->ij", x, w) + b, 0.0);
    return analysed(y);
}

template <int H, int W, int K>
constexpr Graph blurxy() { // bench_image_pipeline: a horizontal blur, then a vertical one over its result
    Input x{"x", {H, W}};
    Input wh{"wh", {1, 1, K}};
    Input wv{"wv", {1, K, 1}};
    Expr bx = (x.unfold(1, K) * wh).sum(2);
    Output y{"y"};
    y = (bx.unfold(0, K) * wv).sum(1);
    return analysed(y);
}

constexpr Graph rmsnorm() { // bench_apps
    Input x{"x", {1024, 256}};
    Expr ms = (x * x).mean(1, keepdims);
    Output y{"y"};
    y = x / sqrt(ms + 1e-6f);
    return analysed(y);
}

constexpr Graph softmax() { // bench_apps_image_ml
    Input x{"x", {1024, 256}};
    Expr e = exp(x - x.max(1, keepdims));
    Output y{"y"};
    y = e / e.sum(1, keepdims);
    return analysed(y);
}

constexpr Graph ladder_bank() { // bench_lane_recurrence: sixteen voices of a five-stage ladder, one coupled cycle
    constexpr float a1 = 0.87730577f;
    constexpr float g = 1.0f - a1;
    constexpr float b0 = g * 0.76923076923f;
    constexpr float b1 = g * 0.23076923076f;
    constexpr float drive = 1.2f;
    constexpr float gain = 0.76730449f;
    constexpr float drive2 = 1.008f;
    constexpr float gain2 = 0.98788632f;
    constexpr float comp = 0.5f;
    constexpr float k = -2.92f;
    constexpr float out = 1.2f;
    const auto f = [](float v) { return static_cast<double>(v); };
    Input x{"x", Signal{16, Length{.multiple_of = 1, .min = 1, .expect = 512}}};
    Variable s0;
    Variable s1;
    Variable s2;
    Variable s3;
    Variable s4;
    Expr dx = f(gain) * tanh(f(drive) * x);
    s0[n] = dx + f(k) * (f(gain2) * tanh(f(drive2) * s4[n - 1]) - dx * f(comp));
    s1[n] = f(b1) * s0[n - 1] + f(a1) * s1[n - 1] + f(b0) * s0[n];
    s2[n] = f(b1) * s1[n - 1] + f(a1) * s2[n - 1] + f(b0) * s1[n];
    s3[n] = f(b1) * s2[n - 1] + f(a1) * s3[n - 1] + f(b0) * s2[n];
    s4[n] = f(b1) * s3[n - 1] + f(a1) * s4[n - 1] + f(b0) * s3[n];
    Output y{"y"};
    y = f(out) * s2;
    return analysed(y);
}

// y = x + a·y[t-1] over `voices` voices, the state handed on in registers — bench_lane_recurrence (16 voices,
// a = 0.95f, 512 samples expected) and nest_recurrence_test (11, 0.9375, 64).
constexpr Graph one_pole_bank(std::int32_t voices, double a, std::int32_t expect) {
    Input x{"x", Signal{voices, Length{.multiple_of = 1, .min = 1, .expect = expect}}};
    Output y{"y"};
    y[n] = x[n] + a * y[n - 1];
    return analysed(y);
}

// The bank of no voices (`loops_plan_test`'s boundary): a signal whose element axis has no elements, which no
// declaration says (a signal's extents are at least one), keeps the old input (DS-18).
constexpr Graph one_pole_bank_of_none(double a) {
    const DimDecl samples{0, DimConstraints{1, 1}};
    Expr x = detail::input("x", Shape{0, Extent{Dim{samples.id}}}, ElemType::F32, samples);
    Output y{"y"};
    y[n] = x[n] + a * y[n - 1];
    return analysed(y);
}

// A sliding median over `length` samples, a network in lanes across the outputs — bench_order_stats (2^20
// samples, 3, 5 and 9 taps) and nest_window_test (37, 5).
constexpr Graph sliding_median(std::int32_t length, std::int32_t taps) {
    Input x{"x", {length}};
    Output y{"y"};
    y = x.unfold(0, taps).median(1);
    return analysed(y);
}

constexpr Graph strip() { // bench_channel_strip: a gain, three tanh saturators, a gain and a clamp
    const auto saturate = [](Expr s, double d, double b) { return tanh(constant(d) * s + b); };
    Expr s = constant(2.0) * Input{"x", Signal{1 << 20}}; // one expression, as it has always been: the same node order
    s = saturate(s, 1.5, 0.10);
    s = saturate(s, 1.3, -0.05);
    s = saturate(s, 1.1, 0.0);
    Output y{"y"};
    y = clamp(constant(0.7) * s, -1.0, 1.0);
    return analysed(y);
}

template <int N, int D>
constexpr Graph attention() { // bench_attention: softmax(Q·Kᵀ / 8)·V
    Input q{"Q", {N, D}};
    Input k{"K", {N, D}};
    Input v{"V", {N, D}};
    Expr scores = einsum("qd,kd->qk", q, k) * (1.0 / 8.0);
    Expr e = exp(scores - scores.max(1, keepdims));
    Expr attn = e / e.sum(1, keepdims);
    Output out{"out"};
    out = einsum("qk,kd->qd", attn, v);
    return analysed(out);
}

template <int N, int K>
constexpr Graph conv() { // bench_fusion: a K-tap filter over the square of x
    Input x{"x", {N}};
    Input h{"h", {1, K}};
    Output y{"y"};
    y = ((x * x).unfold(0, K) * h).sum(1);
    return analysed(y);
}

template <int H, int W, int K>
constexpr Graph blur() { // bench_fusion: a K-tap vertical blur over the square of x
    Input x{"x", {H, W}};
    Input w{"w", {1, K, 1}};
    Output y{"y"};
    y = ((x * x).unfold(0, K) * w).sum(1);
    return analysed(y);
}

template <int H, int W>
constexpr Graph harris() { // bench_image_pipeline: 3×3 gradients, their products box-summed, the corner response
    Input x{"x", {H, W}};
    Input wx{"wx", {1, 3, 1, 3}};
    Input wy{"wy", {1, 3, 1, 3}};
    Expr taps = x.unfold2d(0, 3, 1, 3);
    Expr dx = (taps * wx).sum({1, 3});
    Expr dy = (taps * wy).sum({1, 3});
    const auto box = [](Expr v) { return v.unfold2d(0, 3, 1, 3).sum({1, 3}); };
    Expr sxx = box(dx * dx);
    Expr sxy = box(dx * dy);
    Expr syy = box(dy * dy);
    Expr tr = sxx + syy;
    Output y{"y"};
    y = sxx * syy - sxy * sxy - static_cast<double>(0.04f) * tr * tr;
    return analysed(y);
}

constexpr Graph fm_oscillator() { // bench_oscillator: a sine whose phase increment is a signal
    Input inc{"inc", Signal{Length{.multiple_of = 1, .min = 1, .expect = 512}}};
    Output y{"y"};
    y = dsp::oscillator(inc, constant(static_cast<double>(0.5f)));
    return analysed(y);
}

constexpr Graph brightness() { // bench_apps
    Input x{"x", {512, 512}};
    Output y{"y"};
    y = clamp(constant(1.2) * x + 0.05, 0.0, 1.0);
    return analysed(y);
}

constexpr Graph rownorm() { // bench_apps
    Input x{"x", {1024, 256}};
    Output y{"y"};
    y = x - x.mean(1, keepdims);
    return analysed(y);
}

constexpr Graph mlp2() { // bench_ml_layers: two linear layers with a bias and a relu each
    Input x{"x", {128, 512}};
    Input w1{"w1", {512, 256}};
    Input b1{"b1", {1, 256}};
    Input w2{"w2", {256, 512}};
    Input b2{"b2", {1, 512}};
    Expr h = max(einsum("ik,kj->ij", x, w1) + b1, 0.0);
    Output y{"y"};
    y = max(einsum("ik,kj->ij", h, w2) + b2, 0.0);
    return analysed(y);
}

// ---- the planner's (`loops_plan`) ----------------------------------------------------------------------

template <int H, int W, int K>
constexpr Graph unsharp() { // bench_image_pipeline: b = a K-tap vertical blur of x, y = 1.5·centre(x) − 0.5·b
    Input x{"x", {H, W}};
    Input w{"w", {1, K, 1}};
    Expr b = (x.unfold(0, K) * w).sum(1);
    Expr centre = x.slice(0, K / 2, H - K + 1); // the rows the blur is centred on
    Output y{"y"};
    y = centre * 1.5 - b * 0.5;
    return analysed(y);
}

template <int H, int W>
constexpr Graph three_blurs() { // nest_band_test's chain: three 3-tap vertical blurs, each over the one before
    Input x{"x", {H, W}};
    Input w{"w", {1, 3, 1}};
    const auto vblur = [&](Expr v) { return (v.unfold(0, 3) * w).sum(1); };
    Output y{"y"};
    y = vblur(vblur(vblur(x)));
    return analysed(y);
}

// ---- the nest tests' ----------------------------------------------------------------------------------

constexpr Graph lag_of_a_lag() { // nest_v6_review_test: y = x + 0.5 y[t-1] + 0.25 y[t-2], the second lag OF the first
    Input x{"x", Signal{9, 19}};
    Output y{"y"};
    Expr a = y[n - 1];
    Expr b = a[n - 1]; // built before the definition, so the two lags stand side by side
    y[n] = x[n] + 0.5 * a + 0.25 * b;
    return analysed(y);
}

constexpr Graph fir_over_a_feed() { // nest_recurrence_test: the past of an input, its first samples against silence
    Input x{"x", Signal{Length{.multiple_of = 1, .min = 1, .expect = 64}}};
    Output y{"y"};
    y = x + 0.5 * x[n - 1] + 0.25 * x[n - 3];
    return analysed(y);
}

constexpr Graph delay_line() { // nest_gather_test: y[t] = a[t - p[t]], a = x/2, a depth that is a signal
    Input x{"x", Signal{21}};
    Input p{"p", Signal{21}};
    Expr a = x * 0.5;
    Expr depth = p;
    Arena* arena = bring_together(a, depth);
    BuildNode node{};
    node.op = op::DelayLine;
    node.inputs[0] = a.position();
    node.inputs[1] = depth.position();
    node.attr.kind = AttrKind::SignalledDelay;
    node.attr.signalled_delay.max_delay = 5;
    arena->nodes.push_back(node);
    Output y{"y"};
    y = Expr{arena, static_cast<std::int32_t>(arena->nodes.size() - 1)};
    return analysed(y);
}

constexpr Graph padded() { // nest_parallel_plan_test: a value made in parts — a margin of 0.5, the body
    Input x{"x", Signal{13, 7, 24}};
    OpAttr attr{};
    attr.kind = AttrKind::View;
    attr.view.kind = ViewKind::Pad;
    attr.view.axis = 1;
    attr.view.pad_before = 2;
    attr.view.pad_after = 3;
    attr.view.pad_bits = std::bit_cast<std::uint64_t>(0.5);
    Output y{"y"};
    y = detail::append_unary(x, op::Pad, attr) * 2.0;
    return analysed(y);
}

constexpr Graph tall(bool with_relu) { // nest_contract_test: 50 x 21 times 21 x 19, a bias and a relu
    Input a{"a", {50, 21}};
    Input b{"b", {21, 19}};
    Input bias{"bias", {19}};
    Expr product = einsum("ik,kj->ij", a, b);
    Output y{"y"};
    y = with_relu ? max(product + bias, 0.0) : product + 0.0 * bias;
    return analysed(y);
}

} // namespace corpus
