// The `einsum_contractions` example's matmul and batched-matmul, built on the OLD tree at c4012b8, run,
// and their outputs printed in the same order as `einsum_contractions_here.cpp`.
//
// The old tree's headers are extracted from the shared-object database at configure time, not read out
// of /home/user/ctg3's working directory — the reference is pinned to the commit `main` names, the same
// as the other `_there` programs.
//
// This is the old example's own `build_matmul` / `build_bmm` graphs verbatim (`"ik,kj->ij"` and
// `"bik,bkj->bij"` — the old tree is whole-tensor and names no streamed axis). The example's THIRD part,
// the three-operand chain `"ij,jk,kl->il"`, is not built here: this tree's capture layer refuses an
// n-ary chain (a pairing-order choice M8.2 forbids), so there is nothing on the here side to compare it
// to. `set_fast_math(false)` is pinned so this side takes the deterministic left-fold accumulation the
// example's exact mode uses.
#include <array>
#include <cstddef>
#include <cstdio>

#include "heinzel/backends/scalar_backend.hpp"
#include "heinzel/frontend/einsum.hpp"
#include "heinzel/runtime/compile.hpp"
#include "heinzel/runtime/executor.hpp"

using namespace heinzel;

namespace {

constexpr TargetInfo kTarget{.logical_cores = 1, .l1_cache_bytes = 32 * 1024, .simd_width_bytes = 32};
constexpr int M = 3;
constexpr int K = 4;
constexpr int N = 5;
constexpr int Bt = 2;

constexpr Graph build_matmul() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input a{g, "a", {M, K}};
    Input b{g, "b", {K, N}};
    Output y{g, "y", {M, N}};
    y = einsum("ik,kj->ij", a, b);
    return g;
}

constexpr Graph build_bmm() {
    GraphBuilder g;
    g.set_fast_math(false);
    Input a{g, "a", {Bt, M, K}};
    Input b{g, "b", {Bt, K, N}};
    Output y{g, "y", {Bt, M, N}};
    y = einsum("bik,bkj->bij", a, b);
    return g;
}

inline constexpr auto kMatmul = compile<build_matmul(), kTarget>();
inline constexpr auto kBmm = compile<build_bmm(), kTarget>();

} // namespace

int main() {
    // ---- matmul ----
    std::array<float, M * K> A{};
    std::array<float, K * N> B{};
    for (int i = 0; i < M * K; ++i) {
        A[i] = 0.1f * float(i) + 1.0f;
    }
    for (int i = 0; i < K * N; ++i) {
        B[i] = 0.2f * float(i) - 0.5f;
    }
    std::array<float, M * N> Y{};
    Executor<kMatmul, scalar_backend> e1;
    e1.input("a") = A.data();
    e1.input("b") = B.data();
    e1.output("y") = Y.data();
    e1.run();
    for (int e = 0; e < M * N; ++e) {
        std::printf("%.9e\n", static_cast<double>(Y[e]));
    }

    // ---- batched matmul ----
    std::array<float, Bt * M * K> Ab{};
    std::array<float, Bt * K * N> Bb{};
    for (int i = 0; i < Bt * M * K; ++i) {
        Ab[i] = 0.05f * float(i) + 0.5f;
    }
    for (int i = 0; i < Bt * K * N; ++i) {
        Bb[i] = 0.07f * float(i) - 0.2f;
    }
    std::array<float, Bt * M * N> Yb{};
    Executor<kBmm, scalar_backend> e2;
    e2.input("a") = Ab.data();
    e2.input("b") = Bb.data();
    e2.output("y") = Yb.data();
    e2.run();
    for (int e = 0; e < Bt * M * N; ++e) {
        std::printf("%.9e\n", static_cast<double>(Yb[e]));
    }
    return 0;
}
