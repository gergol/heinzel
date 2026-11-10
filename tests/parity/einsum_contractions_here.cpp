// The `einsum_contractions` example, built in THIS tree's language, run on `Executor`, and its samples
// printed.
//
// The old tree's `examples/einsum_contractions.cpp` spells three contractions with `einsum("...->...")`:
// a matmul `"ik,kj->ij"`, a batched matmul `"bik,bkj->bij"`, and a THREE-operand chain
// `"ij,jk,kl->il"`. Milestone MB's `_matmul` (S4a–S4d) makes the first two expressible: the notation now
// lowers a transposing/batched contraction to `op::MatMul` instead of refusing it. The three-operand
// chain is NOT ported and cannot be: an n-ary chain must CHOOSE a pairing order, and that is a
// cost-model decision `02-MILESTONES.md` §M8.2 forbids the capture layer — the einsum lowering refuses
// three operands by name (shown in `notation_test.cpp`). So this parity program ports the two the
// milestone grants and records the chain as an M8.2-reserved exclusion, not a defect.
//
// THE MAPPING. The operands are tensors, `a{M,K}` and `b{K,N}`, and `einsum` never names the stream, so the
// spec is the old tree's `"ik,kj->ij"`. The batched case is `a{Bt,M,K}`, `b{Bt,K,N}`, `"bik,bkj->bij"` — `b` a
// shared batch axis kept once at the front. A tensor runs as one sample.
//
// THE NUMERIC DIFFERENCE, measured not fitted (the `image_elementwise` discipline). A contraction is a
// SUM of products, and the two trees round that sum differently: the old tree's scalar backend
// accumulates in f32 (rounding at each of the K−1 additions), while this tree, at `High`, accumulates the
// exact products in double and rounds ONCE at the end to the F32 output. So the gap is a
// reduction-order rounding difference, not a disagreement about the operation; the parity test's ALLOWED
// is the measured maximum of it. Inputs are carried through float so their bits match the old tree's f32.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

// A whole-tensor contraction has no time. The old example's own extents.
constexpr std::int32_t M = 3;
constexpr std::int32_t K = 4;
constexpr std::int32_t N = 5;
constexpr std::int32_t Bt = 2;

// The old example's own input values, carried THROUGH FLOAT so their bits match the old tree's f32.
constexpr double a_matmul(std::int32_t i) { return static_cast<double>(0.1f * static_cast<float>(i) + 1.0f); }
constexpr double b_matmul(std::int32_t i) { return static_cast<double>(0.2f * static_cast<float>(i) - 0.5f); }
constexpr double a_bmm(std::int32_t i) { return static_cast<double>(0.05f * static_cast<float>(i) + 0.5f); }
constexpr double b_bmm(std::int32_t i) { return static_cast<double>(0.07f * static_cast<float>(i) - 0.2f); }

// y = einsum("ik,kj->ij", a, b): a{M,K} against b{K,N} -> y{M,N}.
constexpr Graph matmul() {
    Input a{"a", {M, K}};
    Input b{"b", {K, N}};
    return finish(Output{"y"} = einsum("ik,kj->ij", a, b));
}

// y = einsum("bik,bkj->bij", a, b): a{Bt,M,K} against b{Bt,K,N} -> y{Bt,M,N}.
constexpr Graph bmm() {
    Input a{"a", {Bt, M, K}};
    Input b{"b", {Bt, K, N}};
    return finish(Output{"y"} = einsum("bik,bkj->bij", a, b));
}

// The two inputs are the element values in the value's row-major element order — the same order the old
// tree's flat buffers use — and so is the output.
template <auto Build, std::size_t Wa, std::size_t Wb>
void run_and_print(const std::array<double, Wa>& a_buf, const std::array<double, Wb>& b_buf) {
    const testing::Outputs y = testing::executed<Build>(
        {{"a", a_buf.data(), static_cast<std::int32_t>(Wa)}, {"b", b_buf.data(), static_cast<std::int32_t>(Wb)}});
    for (const double v : y[0]) {
        std::printf("%.9e\n", v);
    }
}

} // namespace

int main() {
    // ---- matmul: a{M,K} b{K,N} -> y{M,N}, element e = i*K+k / k*N+j / i*N+j (row-major) ----
    std::array<double, M * K> a_mm{};
    std::array<double, K * N> b_mm{};
    for (std::int32_t i = 0; i < M * K; ++i) {
        a_mm[static_cast<std::size_t>(i)] = a_matmul(i);
    }
    for (std::int32_t i = 0; i < K * N; ++i) {
        b_mm[static_cast<std::size_t>(i)] = b_matmul(i);
    }
    run_and_print<matmul>(a_mm, b_mm);

    // ---- batched matmul: a{Bt,M,K} b{Bt,K,N} -> y{Bt,M,N} ----
    std::array<double, Bt * M * K> a_bm{};
    std::array<double, Bt * K * N> b_bm{};
    for (std::int32_t i = 0; i < Bt * M * K; ++i) {
        a_bm[static_cast<std::size_t>(i)] = a_bmm(i);
    }
    for (std::int32_t i = 0; i < Bt * K * N; ++i) {
        b_bm[static_cast<std::size_t>(i)] = b_bmm(i);
    }
    run_and_print<bmm>(a_bm, b_bm);
    return 0;
}
