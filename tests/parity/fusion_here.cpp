// The fusion benchmark's producer-into-conv graph, built in THIS tree's language, run on `Executor`, and
// printed.
//
// `benchmarks/fusion_bench.cpp`'s headline graph is a squaring producer fused into a 1-D convolution:
//   p = x*x ; y[i] = Σ_k p[i+k]·h[k]   ((N-K+1) outputs).
// In the bench it is the leg-1 LOCALITY measurement (producer kept in cache per consumer tile vs a
// materialized two-pass round-trip). As an S6 CORRECTNESS corpus its numeric content is that
// COMPOSITION — the producer's square feeding the windowed weighted sum. No new op: `_unfold` (M5),
// elementwise, and `_reduce` (M5), the same idiom `stencil_unfold_here` uses, here over a 1-D stream
// with a squaring producer in front of the window.
//
// The length-N signal is a tensor, x{N}, recorded with a length-1 stream, x{N,1}, as the shapes below are. `_unfold` on the DATA axis (axis 0) turns extent N into (N-K+1) frames and appends a window
// axis of K at axis 1 — {N-K+1, K, 1} — then the per-tap weight h{1,K,1} (broadcast over frame and the
// stream) and a SUM over the window axis is the conv, dropping that axis to {N-K+1, 1}.
//
// THE NUMERIC DIFFERENCE, measured not fitted. The old tree spells the SAME conv as
// transpose(unfold(p))·reshape(h) summed over axis 0 (see fusion_there.cpp), and its reduction rounds
// tap-by-tap; here it is the `_reduce` of the products over the window axis. Same conv, same producer,
// different reduction ORDER — so any gap is a reduction-order rounding difference in f32 and the parity
// ALLOWED is its measured max, with that cause named.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/frontend/graph_builder.hpp"

#include "executed.hpp"

using namespace heinzel;

namespace {

// A CORRECTNESS corpus, not the bench's timing sweep (N up to 1<<20 there). The size is chosen so the
// graph's total element-planes stay under `kMaxPlanes` (768): x{N,1}+p{N,1}+unfold{N-K+1,K,1}·2+y —
// N=48,K=5 is 585 planes. Sizing the corpus is not raising a ceiling: the bench's large N buys timing
// signal, which S6 does not measure. (amendment 03: measure the SHAPE of the cost, not its size.)
constexpr std::int32_t N = 48;
constexpr std::int32_t K = 5;
constexpr std::int32_t kFrames = N - K + 1; // (N-window)/hop + 1

// The same oracle the bench feeds its arms: x[i] = 0.001f*(i%1000) - 0.6f, h[k] = 0.001f*(k%1000) - 0.3f,
// through float so the bits match the old tree's f32 inputs.
constexpr double x_value(std::int32_t i) {
    return static_cast<double>(0.001f * static_cast<float>(i % 1000) - 0.6f);
}
constexpr double h_value(std::int32_t k) {
    return static_cast<double>(0.001f * static_cast<float>(k % 1000) - 0.3f);
}

// p = x*x ; y[f] = sum_tap p[f+tap] * h[tap]. Square the input, unfold the data axis into (frame, tap),
// weight by h{1,K,1} (broadcast over frame and the stream), sum the tap axis away.
constexpr Graph build() {
    Input x{"x", {N}};
    Input h{"h", {1, K}};
    Expr p = x * x;
    Expr windowed = p.unfold(/*axis*/ 0, /*window*/ K, /*hop*/ 1); // {N-K+1, K, 1}
    return finish(Output{"y"} = (windowed * h).sum(1));
}

} // namespace

int main() {
    std::array<double, N> x_buf{};
    for (std::int32_t i = 0; i < N; ++i) {
        x_buf[static_cast<std::size_t>(i)] = x_value(i);
    }
    std::array<double, K> h_buf{};
    for (std::int32_t k = 0; k < K; ++k) {
        h_buf[static_cast<std::size_t>(k)] = h_value(k);
    }

    const testing::Outputs y = testing::executed<build>({{"x", x_buf.data(), N}, {"h", h_buf.data(), K}});

    // y over frames — element f. The same order fusion_there.cpp prints its buffer.
    for (std::int32_t f = 0; f < kFrames; ++f) {
        std::printf("%.9e\n", y[0][static_cast<std::size_t>(f)]);
    }
    return 0;
}
