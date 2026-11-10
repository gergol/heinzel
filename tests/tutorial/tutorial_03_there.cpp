// Tutorial chapter 03 — "reductions and einsum", built on the OLD tree at c4012b8, run, and its
// samples printed in the same format as `tutorial_03_here.cpp`. Grows one graph-family at a time
// alongside the `_here` program (M8.3, S5). The old tree's headers are pinned from the object
// database at configure time; nothing here opens /home/user/ctg3.
//
// Graph 1 (1a) — axis reductions: rowsum = x.sum(1), rowmax = x.max(-1), cent = x - x.mean(1,keepdims).
// `set_fast_math(false)` pins the arithmetic; what remains is the reduction rounding (mean divides by
// the column count), the test's named allowance.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/builder.hpp"
#include "heinzel/frontend/einsum.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr int R = 3;
constexpr int C = 5;

constexpr TargetInfo kTarget{
    .logical_cores = 1,
    .l1_cache_bytes = 16 * 1024,
    .simd_width_bytes = 16,
};

// Graph 1a: the three reductions (softmax follows as 1b).
constexpr Graph build_reduce_a() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, {.name = "x", .shape = {R, C}}};
    x.sum(1).as_output("rowsum");                // {R}
    x.max(-1).as_output("rowmax");               // {R}
    (x - x.mean(1, keepdims)).as_output("cent"); // {R,1} mean broadcasts back over the columns
    return g;
}

inline constexpr auto kReduceA = compile<build_reduce_a(), kTarget>();

// Graph 1b: softmax over the columns (the old tree's composed, max-shifted helper).
constexpr Graph build_soft() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, {.name = "x", .shape = {R, C}}};
    softmax(x, 1).as_output("soft"); // {R,C}
    return g;
}

inline constexpr auto kSoft = compile<build_soft(), kTarget>();

// Graph 2: contraction spelled as einsum — c = matmul, ct = transposed, arow = unary reduce.
constexpr int M = 4;
constexpr int K = 6;
constexpr int EN = 3;
constexpr Graph build_matmul() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input a{g, {.name = "a", .shape = {M, K}}};
    Input b{g, {.name = "b", .shape = {K, EN}}};
    einsum("ik,kj->ij", a, b).as_output("c");  // {M,EN}
    einsum("ik,kj->ji", a, b).as_output("ct"); // {EN,M}
    einsum("ik->i", a).as_output("arow");      // {M}
    return g;
}

inline constexpr auto kMatmul = compile<build_matmul(), kTarget>();

// Graph 3: einops — split [B, C*F, T] into [B,C,F,T], mean over f and t → [B,C].
constexpr int B = 2;
constexpr int EC = 3;
constexpr int F = 4;
constexpr int T = 5;
constexpr Graph build_einops() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input x{g, {.name = "x", .shape = {B, EC * F, T}}};
    auto split = rearrange("b (c f) t -> b c f t", x, {{"f", F}});
    reduce("b c f t -> b c", split, ReduceOp::Mean).as_output("y"); // {B,EC}
    return g;
}

inline constexpr auto kEinops = compile<build_einops(), kTarget>();

// Graph 2: einsum — c {M,EN}, ct {EN,M}, arow {M}, printed in that order.
void run_matmul() {
    std::array<float, M * K> a{};
    std::array<float, K * EN> b{};
    std::array<float, M * EN> c{};
    std::array<float, M * EN> ct{};
    std::array<float, M> arow{};
    for (int i = 0; i < M * K; ++i) {
        a[static_cast<std::size_t>(i)] = 0.21f * static_cast<float>(i) - 1.5f;
    }
    for (int i = 0; i < K * EN; ++i) {
        b[static_cast<std::size_t>(i)] = 0.13f * static_cast<float>(i) + 0.4f;
    }
    Executor<kMatmul, scalar_backend> ex;
    ex.input("a") = a.data();
    ex.input("b") = b.data();
    ex.output("c") = c.data();
    ex.output("ct") = ct.data();
    ex.output("arow") = arow.data();
    ex.run();
    for (int i = 0; i < M * EN; ++i) {
        std::printf("%.9e\n", static_cast<double>(c[static_cast<std::size_t>(i)]));
    }
    for (int i = 0; i < EN * M; ++i) {
        std::printf("%.9e\n", static_cast<double>(ct[static_cast<std::size_t>(i)]));
    }
    for (int i = 0; i < M; ++i) {
        std::printf("%.9e\n", static_cast<double>(arow[static_cast<std::size_t>(i)]));
    }
}

// Graph 3: einops split + mean reduce, printed over (b,c) row-major.
void run_einops() {
    std::array<float, B*(EC * F) * T> x{};
    std::array<float, B * EC> y{};
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = 0.05f * static_cast<float>(i) + 0.3f;
    }
    Executor<kEinops, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.output("y") = y.data();
    ex.run();
    for (int i = 0; i < B * EC; ++i) {
        std::printf("%.9e\n", static_cast<double>(y[static_cast<std::size_t>(i)]));
    }
}

} // namespace

int main() {
    std::array<float, R * C> x{};
    std::array<float, R * C> cent{};
    std::array<float, R> rs{};
    std::array<float, R> rm{};
    for (int i = 0; i < R * C; ++i) {
        x[static_cast<std::size_t>(i)] = 0.37f * static_cast<float>(i) - 2.0f;
    }
    Executor<kReduceA, scalar_backend> ex;
    ex.input("x") = x.data();
    ex.output("rowsum") = rs.data();
    ex.output("rowmax") = rm.data();
    ex.output("cent") = cent.data();
    ex.run();
    for (int r = 0; r < R; ++r) {
        std::printf("%.9e\n", static_cast<double>(rs[static_cast<std::size_t>(r)]));
    }
    for (int r = 0; r < R; ++r) {
        std::printf("%.9e\n", static_cast<double>(rm[static_cast<std::size_t>(r)]));
    }
    for (int i = 0; i < R * C; ++i) {
        std::printf("%.9e\n", static_cast<double>(cent[static_cast<std::size_t>(i)]));
    }
    { // graph 1b: softmax
        std::array<float, R * C> soft{};
        Executor<kSoft, scalar_backend> ex;
        ex.input("x") = x.data();
        ex.output("soft") = soft.data();
        ex.run();
        for (int i = 0; i < R * C; ++i) {
            std::printf("%.9e\n", static_cast<double>(soft[static_cast<std::size_t>(i)]));
        }
    }
    run_matmul(); // graph 2: einsum (c, ct, arow)
    run_einops(); // graph 3: einops split + mean reduce
    return 0;
}
