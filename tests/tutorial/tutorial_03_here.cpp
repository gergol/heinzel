// Tutorial chapter 03 — "reductions and einsum" (tutorial/03-reductions-and-einsum.md), built in
// THIS tree's language, run on `Executor`, and its samples printed. The chapter is three graph-families — axis
// reductions, a contraction spelled as einsum, and an einops rearrange+reduce — grown one family at a
// time (M8.3, S5), each RUN and output-compared to the same graph on the old tree at c4012b8
// (`tutorial_03_there.cpp`).
//
// Graph 1 (this commit's 1a) — axis reductions over an ELEMENT axis. The old chapter's x{R,C} reduces
// over the columns; here x is the tensor {R,C} and the columns are its axis 1 (axes count from the
// front; a negative one is refused). The keepdims mean broadcasts back over the columns for the
// centering. Reductions lower to op::Reduce
// (M5, closed); nothing new is spelled but the keepdims flag the analysis pass already reads.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/notation/rearrange.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t R = 3; // the chapter's reduction rows/cols
constexpr std::int32_t C = 5;
constexpr std::int32_t kIn = R * C;

// rowsum = sum over the columns → {R,1}. Over element axis 1; the stream of one sample stays.
constexpr Graph build_rowsum() {
    Input x{"x", {R, C}};
    return finish(Output{"rowsum"} = x.sum(1));
}
// rowmax = max over the columns → {R,1}. The old chapter's `x.max(-1)`; here axis 1, counted from the
// front.
constexpr Graph build_rowmax() {
    Input x{"x", {R, C}};
    return finish(Output{"rowmax"} = x.max(1));
}
// cent = x - mean(columns, keepdims). The mean is {R,1,1} and broadcasts back over the columns.
constexpr Graph build_cent() {
    Input x{"x", {R, C}};
    Expr mean = x.mean(1, keepdims);
    return finish(Output{"cent"} = x - mean);
}

// soft = softmax over the columns, max-shifted for stability: exp(x - max) / sum(exp(x - max)). Both
// the max and the sum are keepdims reductions over element axis 1, so each broadcasts back over the
// columns. The old chapter's `softmax(x, 1)` composed helper, spelled here from the same pieces.
constexpr Graph build_soft() {
    Input x{"x", {R, C}};
    Expr m = x.max(1, keepdims);
    Expr e = exp(x - m);
    Expr s = e.sum(1, keepdims);
    return finish(Output{"soft"} = e / s);
}

// Graph 2 — a contraction spelled as einsum. The old chapter's "ik,kj->ij" reads as written: a{M,K} and
// b{K,N} are tensors, and a spec names their axes only. c = matmul, ct = transposed output, arow = the
// unary einsum (a pure reduce over k). All lower to op::MatMul / op::Reduce (MB / M5), no new op.
constexpr std::int32_t M = 4;
constexpr std::int32_t K = 6;
constexpr std::int32_t EN = 3;
constexpr std::int32_t kA = M * K;
constexpr std::int32_t kB = K * EN;
constexpr Graph build_c() {
    Input a{"a", {M, K}};
    Input b{"b", {K, EN}};
    return finish(Output{"c"} = einsum("ik,kj->ij", a, b)); // {M,EN,1}
}
constexpr Graph build_ct() {
    Input a{"a", {M, K}};
    Input b{"b", {K, EN}};
    return finish(Output{"ct"} = einsum("ik,kj->ji", a, b)); // {EN,M,1} transposed output
}
constexpr Graph build_arow() {
    Input a{"a", {M, K}};
    return finish(Output{"arow"} = einsum("ik->i", a)); // {M,1} unary einsum = reduce over k
}

// Graph 3 — einops: a packed [B, C*F, T] buffer split into [B, C, F, T], then mean over the named f
// and t axes → [B, C]. The old chapter's rearrange + einops reduce, as written: x is a tensor, so both
// drops are over ELEMENT axes (the einops_pipeline mapping, MB.S5). No new op.
constexpr std::int32_t B = 2;
constexpr std::int32_t EC = 3;
constexpr std::int32_t F = 4;
constexpr std::int32_t T = 5;
constexpr std::int32_t kEin = B * (EC * F) * T;
constexpr Graph build_einops() {
    Input x{"x", {B, EC * F, T}};
    Expr split = rearrange("b (c f) t -> b c f t", x, {{"f", F}});
    return finish(Output{"y"} = reduce("b c f t -> b c", split, ReduceOp::Mean)); // {B,EC,1}
}

// Print an output's numbers in row-major element order (every graph here is one sample).
void print_all(const testing::Outputs& y) {
    for (const double v : y[0]) {
        std::printf("%.9e\n", v);
    }
}

// Run a one-sample graph fed only `x`, and print it.
template <auto Build>
void run_x(const std::array<double, kIn>& x) {
    print_all(testing::executed<Build>({{"x", x.data(), kIn}}));
}

// Graph 2: einsum matmul / transposed / unary, fed a and b, printed c then ct then arow.
void run_graph2() {
    std::array<double, static_cast<std::size_t>(kA)> a{};
    std::array<double, static_cast<std::size_t>(kB)> b{};
    for (std::int32_t i = 0; i < kA; ++i) {
        a[static_cast<std::size_t>(i)] = static_cast<double>(0.21f * static_cast<float>(i) - 1.5f);
    }
    for (std::int32_t i = 0; i < kB; ++i) {
        b[static_cast<std::size_t>(i)] = static_cast<double>(0.13f * static_cast<float>(i) + 0.4f);
    }
    print_all(testing::executed<build_c>({{"a", a.data(), kA}, {"b", b.data(), kB}}));  // c  {M,EN}
    print_all(testing::executed<build_ct>({{"a", a.data(), kA}, {"b", b.data(), kB}})); // ct {EN,M}
    print_all(testing::executed<build_arow>({{"a", a.data(), kA}}));                    // arow {M}
}

// Graph 3: einops split + mean reduce, fed x, printed over (b,c) row-major.
void run_einops() {
    std::array<double, static_cast<std::size_t>(kEin)> x{};
    for (std::int32_t e = 0; e < kEin; ++e) {
        x[static_cast<std::size_t>(e)] = static_cast<double>(0.05f * static_cast<float>(e) + 0.3f);
    }
    print_all(testing::executed<build_einops>({{"x", x.data(), kEin}}));
}

} // namespace

int main() {
    std::array<double, kIn> x{};
    for (std::int32_t i = 0; i < kIn; ++i) {
        x[static_cast<std::size_t>(i)] = static_cast<double>(0.37f * static_cast<float>(i) - 2.0f);
    }
    run_x<build_rowsum>(x); // graph 1: rowsum {R}
    run_x<build_rowmax>(x); // graph 1: rowmax {R}
    run_x<build_cent>(x);   // graph 1: cent {R,C}
    run_x<build_soft>(x);   // graph 1: soft {R,C}
    run_graph2();           // graph 2: einsum (c, ct, arow)
    run_einops();           // graph 3: einops split + mean reduce
    return 0;
}
