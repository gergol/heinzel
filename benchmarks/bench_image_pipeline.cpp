// Image-pipeline benchmark — the rewrite arm, on the nest path.
//
// Ported from ctg3 benchmarks/image_pipeline_bench.cpp, three of its five families. Its other two,
// `vblur` and `hconv`, are already on this branch as `blur/*` and `conv/*` in bench_fusion.cpp.
//
//   unsharp  b = 5-tap vertical blur(x) ; y = (1+a)·centre(x) − a·b
//   blurxy   a 3-tap horizontal blur, then a 3-tap vertical blur over its result
//   harris   dx/dy 3×3 stencils → three products → 3×3 box sums → the corner response
//
// Each at the old bench's three cache regimes (W = 2048; H = 32 / 1024 / 8192, so the image is
// 256 KB / 8 MB / 64 MB — L2-resident, L3-resident, twice L3). F32 throughout, which is what the hand
// arms and the old tree's arms compute in.
//
// MAPPING to the nest path: an image is the tensor `{H, W}` (recorded `{H, W, 1}`, a stream of one
// sample), as `blur` in bench_fusion.cpp lays it out. A stencil is then `x.unfold(axis, k)` over an
// element axis (rows for a vertical tap, columns for a horizontal one) and `.sum(axis)` over the window
// axis it inserts; harris windows BOTH element axes at once with `x.unfold2d(…)`. The centre tap unsharp
// subtracts its blur from is `x.slice(…)` of the image over the rows the blur's valid region covers. No kernel and no fused stencil op: the same
// planner that lays out a matmul decides whether each stage is stored or computed inside its reader.
//
// The old bench's OPTION arms (fused/materialized pinned through a roofline `TargetInfo`, the segment
// and tile/band schedules, the parallel picks) are DEFERRED for the same reason bench_fusion.cpp's
// header gives: the rewrite's `Machine` has no roofline and its schedule has
// no fuse↔materialize decision to pin, so there is nothing to sweep. One `heinzel` arm per family runs
// what the planner decides, beside the old bench's two hand-written bounds.
//
// WHAT THE PLANNER CHOSE, and what it costs (measured on zen2, medians of 7, taskset-pinned, 2026-09-24):
// `unsharp` is ONE stage — the fold over the five taps, the centre slice and the two maps share a nest,
// the slice being a Load at the image's own address — so the image is read once and the result written
// once, and the arm is at or ahead of `hand-fused` at every size. `blurxy` is two stages (the horizontal
// blur, the vertical one reading it through its window); `harris` two as well — dx and dy are sibling folds
// of one window of the image, with the three products after them, and the three box sums are siblings with
// the response after them. From 8 MB up the two stages of each run under ONE loop over bands of rows
// (`loops::Program::bands`): blurxy 30 rows at a time, harris 8, as high as keeps half the L2; only those rows
// of the intermediate (and the two its window reaches ahead) are kept, and between bands those two rows are
// copied to the front of their buffer — the hand arms' three-row ring, as a CARRY, so every address stays
// affine. blurxy is level with `hand-fused` (844 vs 803 µs at 8 MB, 10.0 vs 10.0 ms at 64 MB), harris 0.8x
// of it (2.48 vs 3.16 ms, 20.3 vs 25.8 ms) — `hand-fused` works out its ring's row, a modulo, per element
// and tap, where the band's addresses are affine. At 256 KB nothing is banded (what would be kept is under a third
// of the last-level cache, and harris banded there is slower): blurxy level with the hand arms, harris 0.89x
// of `hand-mat`.
//
// `hand-mat` and `hand-fused` are the old bench's loops, ported verbatim but for `fma_rn` — this
// tree has no such primitive, and the library is built with `-ffp-contract=off`, so a hand arm that
// fused its multiply-add would round differently from every Heinzel arm it is the oracle for. The
// plain `acc += a * b` is the same arithmetic the nest path performs.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <benchmark/benchmark.h>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"
#include "page_vector.hpp"
#include "parallel_arm.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace hz = heinzel;

namespace {

constexpr float kAlpha = 0.5f;
constexpr double kHarrisK = static_cast<double>(0.04f);

// The old bench's ramp, in float.
PageVector<float> ramp(std::size_t n, float bias) {
    PageVector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = 0.001f * static_cast<float>(i % 977) - bias;
    }
    return v;
}

double maxabs(std::span<const float> v) {
    double m = 0.0;
    for (const float x : v) {
        m = std::max(m, std::fabs(static_cast<double>(x)));
    }
    return m;
}

// The hand arm is the oracle, over every element. The two arms sum the same terms, so what separates
// them is the order they sum them in: `roundings` float roundings at `scale`, the magnitude the
// family's own arithmetic reaches on this data.
bool agrees(benchmark::State& state, const char* what, const float* got, const std::vector<float>& want,
            int roundings, double scale) {
    double worst = 0.0;
    for (std::size_t k = 0; k < want.size(); ++k) {
        worst = std::max(worst, std::fabs(static_cast<double>(got[k]) - static_cast<double>(want[k])));
    }
    state.counters["max_rel_err"] = worst / std::max(scale, 1e-30);
    if (!(worst <= roundings * 2.4e-7 * scale)) {
        state.SkipWithError(what);
        return false;
    }
    return true;
}

// ---- unsharp: b = 5-tap vertical blur(x); y = (1+a)·centre(x) − a·b -------------------------------
template <int H, int W, int K>
constexpr hz::Graph kUnsharp = [] {
    hz::Input x{"x", {H, W}};
    hz::Input w{"w", {1, K, 1}};
    hz::Expr b = (x.unfold(0, K) * w).sum(1);
    hz::Expr centre = x.slice(0, K / 2, H - K + 1); // the rows the blur is centred on
    hz::Graph g = hz::finish(hz::Output{"y"} = centre * static_cast<double>(1.0f + kAlpha) - b * static_cast<double>(kAlpha));
    if (!hz::infer(g).ok) {
        hz::fail("bench_image_pipeline: the unsharp graph did not infer");
    }
    return g;
}();
template <int H, int W, int K>
constexpr const hz::loops::Program& kUnsharpNest = heinzel_bench::kProgramOf<kUnsharp<H, W, K>, heinzel_bench::kMachine>;

template <int H, int W, int K>
void unsharp_hand_fused(const float* x, const float* w, float* y) {
    for (int i = 0; i < H - K + 1; ++i) {
        for (int j = 0; j < W; ++j) {
            float b = 0.0f;
            for (int k = 0; k < K; ++k) {
                b += x[(i + k) * W + j] * w[k];
            }
            y[i * W + j] = x[(i + K / 2) * W + j] * (1.0f + kAlpha) - b * kAlpha;
        }
    }
}

template <int H, int W, int K>
void unsharp_hand_mat(const float* x, const float* w, float* b, float* y) {
    for (int i = 0; i < H - K + 1; ++i) {
        for (int j = 0; j < W; ++j) {
            float acc = 0.0f;
            for (int k = 0; k < K; ++k) {
                acc += x[(i + k) * W + j] * w[k];
            }
            b[i * W + j] = acc;
        }
    }
    for (int i = 0; i < H - K + 1; ++i) {
        for (int j = 0; j < W; ++j) {
            y[i * W + j] = x[(i + K / 2) * W + j] * (1.0f + kAlpha) - b[i * W + j] * kAlpha;
        }
    }
}

template <int H, int W, int K, bool Par = false>
void bench_unsharp_nest(benchmark::State& state) {
    const PageVector<float> x = ramp(std::size_t(H) * W, 0.5f);
    const PageVector<float> w = ramp(K, -0.2f);
    const std::size_t n = std::size_t(H - K + 1) * W;
    std::vector<float> want(n);
    std::vector<float> b(n);
    unsharp_hand_mat<H, W, K>(x.data(), w.data(), b.data(), want.data());
    hz::loops::Workspace<kUnsharpNest<H, W, K>, heinzel_bench::copies<Par>()> work(hz::loops::Sizes{});
    const hz::loops::Feeds feeds{x.data(), w.data()};
    heinzel_bench::run<kUnsharpNest<H, W, K>, Par>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(kUnsharpNest<H, W, K>.outputs[0]));
    const double scale = (1.0 + kAlpha) * maxabs(x) + kAlpha * maxabs(b);
    if (!agrees(state, "bench_image_pipeline: the unsharp nest arm disagrees with the hand arm", y, want, K + 1, scale)) {
        return;
    }
    if (Par && !heinzel_bench::same_as_serial<kUnsharpNest<H, W, K>>(feeds, work)) {
        state.SkipWithError("bench_image_pipeline: the split run differs from the serial run");
        return;
    }
    for (auto _ : state) {
        heinzel_bench::run<kUnsharpNest<H, W, K>, Par>(feeds, work);
        benchmark::DoNotOptimize(y);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(n));
}

template <int H, int W, int K, bool Materialized>
void bench_unsharp_hand(benchmark::State& state) {
    const PageVector<float> x = ramp(std::size_t(H) * W, 0.5f);
    const PageVector<float> w = ramp(K, -0.2f);
    const std::size_t n = std::size_t(H - K + 1) * W;
    OutputVector<float> y(n);
    OutputVector<float, 1> b(Materialized ? n : 0);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        if constexpr (Materialized) {
            unsharp_hand_mat<H, W, K>(x.data(), w.data(), b.data(), y.data());
        } else {
            unsharp_hand_fused<H, W, K>(x.data(), w.data(), y.data());
        }
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(n));
}

// ---- blurxy: a 3-tap horizontal blur, then a 3-tap vertical one over its result -------------------
template <int H, int W, int K>
constexpr hz::Graph kBlurxy = [] {
    hz::Input x{"x", {H, W}};
    hz::Input wh{"wh", {1, 1, K}};
    hz::Input wv{"wv", {1, K, 1}};
    // Columns first: {H, W-K+1, K, 1} folded over the window axis back to {H, W-K+1, 1}.
    hz::Expr bx = (x.unfold(1, K) * wh).sum(2);
    hz::Expr y = (bx.unfold(0, K) * wv).sum(1);
    hz::Graph g = hz::finish(hz::Output{"y"} = y);
    if (!hz::infer(g).ok) {
        hz::fail("bench_image_pipeline: the blurxy graph did not infer");
    }
    return g;
}();
template <int H, int W, int K>
constexpr const hz::loops::Program& kBlurxyNest = heinzel_bench::kProgramOf<kBlurxy<H, W, K>, heinzel_bench::kMachine>;

template <int H, int W, int K>
void blurxy_hand_mat(const float* x, const float* wh, const float* wv, float* bx, float* y) {
    constexpr int Wo = W - K + 1;
    constexpr int Ho = H - K + 1;
    for (int i = 0; i < H; ++i) {
        for (int j = 0; j < Wo; ++j) {
            float acc = 0.0f;
            for (int k = 0; k < K; ++k) {
                acc += x[i * W + j + k] * wh[k];
            }
            bx[i * Wo + j] = acc;
        }
    }
    for (int i = 0; i < Ho; ++i) {
        for (int j = 0; j < Wo; ++j) {
            float acc = 0.0f;
            for (int k = 0; k < K; ++k) {
                acc += bx[(i + k) * Wo + j] * wv[k];
            }
            y[i * Wo + j] = acc;
        }
    }
}

// The old bench's storage-folding ideal: a K-row ring of horizontal results, never the whole image.
template <int H, int W, int K>
void blurxy_hand_fused(const float* x, const float* wh, const float* wv, float* ring, float* y) {
    constexpr int Wo = W - K + 1;
    constexpr int Ho = H - K + 1;
    const auto hrow = [&](int r, float* dst) {
        for (int j = 0; j < Wo; ++j) {
            float acc = 0.0f;
            for (int k = 0; k < K; ++k) {
                acc += x[r * W + j + k] * wh[k];
            }
            dst[j] = acc;
        }
    };
    for (int r = 0; r < K - 1; ++r) {
        hrow(r, ring + std::size_t(r % K) * Wo);
    }
    for (int i = 0; i < Ho; ++i) {
        hrow(i + K - 1, ring + std::size_t((i + K - 1) % K) * Wo); // the new row enters the ring
        for (int j = 0; j < Wo; ++j) {
            float acc = 0.0f;
            for (int k = 0; k < K; ++k) {
                acc += ring[std::size_t((i + k) % K) * Wo + j] * wv[k];
            }
            y[i * Wo + j] = acc;
        }
    }
}

template <int H, int W, int K, bool Par = false>
void bench_blurxy_nest(benchmark::State& state) {
    constexpr int Wo = W - K + 1;
    constexpr int Ho = H - K + 1;
    const PageVector<float> x = ramp(std::size_t(H) * W, 0.5f);
    const PageVector<float> wh = ramp(K, -0.2f);
    const PageVector<float> wv = ramp(K, -0.3f);
    std::vector<float> want(std::size_t(Ho) * Wo);
    std::vector<float> bx(std::size_t(H) * Wo);
    blurxy_hand_mat<H, W, K>(x.data(), wh.data(), wv.data(), bx.data(), want.data());
    hz::loops::Workspace<kBlurxyNest<H, W, K>, heinzel_bench::copies<Par>()> work(hz::loops::Sizes{});
    const hz::loops::Feeds feeds{x.data(), wh.data(), wv.data()};
    heinzel_bench::run<kBlurxyNest<H, W, K>, Par>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(kBlurxyNest<H, W, K>.outputs[0]));
    // Both folds' terms: an error in a horizontal row is carried into the vertical fold by its taps.
    double taps = 0.0;
    for (int k = 0; k < K; ++k) {
        taps += std::fabs(static_cast<double>(wv[static_cast<std::size_t>(k)]));
    }
    if (!agrees(state, "bench_image_pipeline: the blurxy nest arm disagrees with the hand arm", y, want,
                K * K, maxabs(bx) * taps)) {
        return;
    }
    if (Par && !heinzel_bench::same_as_serial<kBlurxyNest<H, W, K>>(feeds, work)) {
        state.SkipWithError("bench_image_pipeline: the split run differs from the serial run");
        return;
    }
    for (auto _ : state) {
        heinzel_bench::run<kBlurxyNest<H, W, K>, Par>(feeds, work);
        benchmark::DoNotOptimize(y);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * Ho * Wo);
}

template <int H, int W, int K, bool Materialized>
void bench_blurxy_hand(benchmark::State& state) {
    constexpr int Wo = W - K + 1;
    constexpr int Ho = H - K + 1;
    const PageVector<float> x = ramp(std::size_t(H) * W, 0.5f);
    const PageVector<float> wh = ramp(K, -0.2f);
    const PageVector<float> wv = ramp(K, -0.3f);
    OutputVector<float> y(std::size_t(Ho) * Wo);
    OutputVector<float, 1> scratch(Materialized ? std::size_t(H) * Wo : std::size_t(K) * Wo);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        if constexpr (Materialized) {
            blurxy_hand_mat<H, W, K>(x.data(), wh.data(), wv.data(), scratch.data(), y.data());
        } else {
            blurxy_hand_fused<H, W, K>(x.data(), wh.data(), wv.data(), scratch.data(), y.data());
        }
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * Ho * Wo);
}

// ---- harris: dx/dy 3×3 stencils → three products → 3×3 box sums → the response --------------------
// dx and dy each read the SAME window of the image and each feed two of the products, so this is the
// wide DAG of the old bench's R7 driver, expressed with windows, folds and maps.
template <int H, int W>
constexpr hz::Graph kHarris = [] {
    hz::Input x{"x", {H, W}};
    hz::Input wx{"wx", {1, 3, 1, 3}};
    hz::Input wy{"wy", {1, 3, 1, 3}};
    // unfold2d makes {H-2, 3, W-2, 3, 1}; the two window axes are the ones that collapse.
    hz::Expr taps = x.unfold2d(0, 3, 1, 3);
    hz::Expr dx = (taps * wx).sum({1, 3});
    hz::Expr dy = (taps * wy).sum({1, 3});
    const auto box = [](hz::Expr v) {
        return v.unfold2d(0, 3, 1, 3).sum({1, 3});
    };
    hz::Expr sxx = box(dx * dx);
    hz::Expr sxy = box(dx * dy);
    hz::Expr syy = box(dy * dy);
    hz::Expr tr = sxx + syy;
    hz::Graph g = hz::finish(hz::Output{"y"} = sxx * syy - sxy * sxy - kHarrisK * tr * tr);
    if (!hz::infer(g).ok) {
        hz::fail("bench_image_pipeline: the harris graph did not infer");
    }
    return g;
}();
template <int H, int W>
constexpr const hz::loops::Program& kHarrisNest = heinzel_bench::kProgramOf<kHarris<H, W>, heinzel_bench::kMachine>;

// Two passes: the three products materialized full-size, then the box sums and the response.
template <int H, int W>
void harris_hand_mat(const float* x, const float* wx, const float* wy, float* ixx, float* ixy, float* iyy,
                     float* y) {
    constexpr int Wd = W - 2;
    constexpr int Hd = H - 2;
    constexpr int Wo = W - 4;
    constexpr int Ho = H - 4;
    for (int i = 0; i < Hd; ++i) {
        for (int j = 0; j < Wd; ++j) {
            float ax = 0.0f;
            float ay = 0.0f;
            for (int a = 0; a < 3; ++a) {
                for (int b = 0; b < 3; ++b) {
                    const float v = x[(i + a) * W + j + b];
                    ax += v * wx[a * 3 + b];
                    ay += v * wy[a * 3 + b];
                }
            }
            ixx[i * Wd + j] = ax * ax;
            ixy[i * Wd + j] = ax * ay;
            iyy[i * Wd + j] = ay * ay;
        }
    }
    for (int i = 0; i < Ho; ++i) {
        for (int j = 0; j < Wo; ++j) {
            float sxx = 0.0f;
            float sxy = 0.0f;
            float syy = 0.0f;
            for (int a = 0; a < 3; ++a) {
                for (int b = 0; b < 3; ++b) {
                    sxx += ixx[(i + a) * Wd + j + b];
                    sxy += ixy[(i + a) * Wd + j + b];
                    syy += iyy[(i + a) * Wd + j + b];
                }
            }
            const float tr = sxx + syy;
            y[i * Wo + j] = sxx * syy - sxy * sxy - 0.04f * tr * tr;
        }
    }
}

// The old bench's storage-folding ideal: 3-row rings of the three product images.
template <int H, int W>
void harris_hand_fused(const float* x, const float* wx, const float* wy, float* rxx, float* rxy, float* ryy,
                       float* y) {
    constexpr int Wd = W - 2;
    constexpr int Wo = W - 4;
    constexpr int Ho = H - 4;
    const auto drow = [&](int r) {
        float* xx = rxx + std::size_t(r % 3) * Wd;
        float* xy = rxy + std::size_t(r % 3) * Wd;
        float* yy = ryy + std::size_t(r % 3) * Wd;
        for (int j = 0; j < Wd; ++j) {
            float ax = 0.0f;
            float ay = 0.0f;
            for (int a = 0; a < 3; ++a) {
                for (int b = 0; b < 3; ++b) {
                    const float v = x[(r + a) * W + j + b];
                    ax += v * wx[a * 3 + b];
                    ay += v * wy[a * 3 + b];
                }
            }
            xx[j] = ax * ax, xy[j] = ax * ay, yy[j] = ay * ay;
        }
    };
    drow(0);
    drow(1);
    for (int i = 0; i < Ho; ++i) {
        drow(i + 2); // the new product row enters the rings
        for (int j = 0; j < Wo; ++j) {
            float sxx = 0.0f;
            float sxy = 0.0f;
            float syy = 0.0f;
            for (int a = 0; a < 3; ++a) {
                const std::size_t row = std::size_t((i + a) % 3) * Wd;
                for (int b = 0; b < 3; ++b) {
                    sxx += rxx[row + j + b], sxy += rxy[row + j + b], syy += ryy[row + j + b];
                }
            }
            const float tr = sxx + syy;
            y[i * Wo + j] = sxx * syy - sxy * sxy - 0.04f * tr * tr;
        }
    }
}

template <int H, int W, bool Par = false>
void bench_harris_nest(benchmark::State& state) {
    constexpr int Wd = W - 2;
    constexpr int Hd = H - 2;
    constexpr int Wo = W - 4;
    constexpr int Ho = H - 4;
    const PageVector<float> x = ramp(std::size_t(H) * W, 0.5f);
    const PageVector<float> wx = ramp(9, -0.2f);
    const PageVector<float> wy = ramp(9, -0.3f);
    std::vector<float> want(std::size_t(Ho) * Wo);
    std::vector<float> ixx(std::size_t(Hd) * Wd);
    std::vector<float> ixy(ixx.size());
    std::vector<float> iyy(ixx.size());
    harris_hand_mat<H, W>(x.data(), wx.data(), wy.data(), ixx.data(), ixy.data(), iyy.data(), want.data());
    hz::loops::Workspace<kHarrisNest<H, W>, heinzel_bench::copies<Par>()> work(hz::loops::Sizes{});
    const hz::loops::Feeds feeds{x.data(), wx.data(), wy.data()};
    heinzel_bench::run<kHarrisNest<H, W>, Par>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(kHarrisNest<H, W>.outputs[0]));
    // A box sum reaches nine products, and the response multiplies two of those sums: the terms the
    // two arms round are that large, whatever the response itself cancels down to.
    const double box = 9.0 * std::max({maxabs(ixx), maxabs(ixy), maxabs(iyy)});
    if (!agrees(state, "bench_image_pipeline: the harris nest arm disagrees with the hand arm", y, want, 40,
                box * box * (2.0 + 4.0 * 0.04))) {
        return;
    }
    if (Par && !heinzel_bench::same_as_serial<kHarrisNest<H, W>>(feeds, work)) {
        state.SkipWithError("bench_image_pipeline: the split run differs from the serial run");
        return;
    }
    for (auto _ : state) {
        heinzel_bench::run<kHarrisNest<H, W>, Par>(feeds, work);
        benchmark::DoNotOptimize(y);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * Ho * Wo);
}

template <int H, int W, bool Materialized>
void bench_harris_hand(benchmark::State& state) {
    constexpr int Wd = W - 2;
    constexpr int Hd = H - 2;
    constexpr int Wo = W - 4;
    constexpr int Ho = H - 4;
    const PageVector<float> x = ramp(std::size_t(H) * W, 0.5f);
    const PageVector<float> wx = ramp(9, -0.2f);
    const PageVector<float> wy = ramp(9, -0.3f);
    const std::size_t rows = Materialized ? std::size_t(Hd) : 3;
    OutputVector<float> y(std::size_t(Ho) * Wo);
    OutputVector<float, 1> ixx(rows * Wd);
    OutputVector<float, 2> ixy(rows * Wd);
    OutputVector<float, 3> iyy(rows * Wd);
    for (auto _ : state) {
        benchmark::ClobberMemory();
        if constexpr (Materialized) {
            harris_hand_mat<H, W>(x.data(), wx.data(), wy.data(), ixx.data(), ixy.data(), iyy.data(), y.data());
        } else {
            harris_hand_fused<H, W>(x.data(), wx.data(), wy.data(), ixx.data(), ixy.data(), iyy.data(), y.data());
        }
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * Ho * Wo);
}

} // namespace

// The old bench's three cache regimes at W = 2048: the image is 256 KB, 8 MB and 64 MB.
#define HEINZEL_UNSHARP(H)                                                                                   \
    BENCHMARK(bench_unsharp_nest<H, 2048, 5>)->Name("unsharp/heinzel/H" #H "_W2048")->UseRealTime();          \
    BENCHMARK((bench_unsharp_nest<H, 2048, 5, true>))->Name("unsharp/heinzel-par/H" #H "_W2048")->UseRealTime(); \
    BENCHMARK((bench_unsharp_hand<H, 2048, 5, true>))->Name("unsharp/hand-mat/H" #H "_W2048")->UseRealTime(); \
    BENCHMARK((bench_unsharp_hand<H, 2048, 5, false>))->Name("unsharp/hand-fused/H" #H "_W2048")->UseRealTime();
HEINZEL_UNSHARP(32)
HEINZEL_UNSHARP(1024)
HEINZEL_UNSHARP(8192)

#define HEINZEL_BLURXY(H)                                                                                   \
    BENCHMARK(bench_blurxy_nest<H, 2048, 3>)->Name("blurxy/heinzel/H" #H "_W2048")->UseRealTime();          \
    BENCHMARK((bench_blurxy_nest<H, 2048, 3, true>))->Name("blurxy/heinzel-par/H" #H "_W2048")->UseRealTime(); \
    BENCHMARK((bench_blurxy_hand<H, 2048, 3, true>))->Name("blurxy/hand-mat/H" #H "_W2048")->UseRealTime(); \
    BENCHMARK((bench_blurxy_hand<H, 2048, 3, false>))->Name("blurxy/hand-fused/H" #H "_W2048")->UseRealTime();
HEINZEL_BLURXY(32)
HEINZEL_BLURXY(1024)
HEINZEL_BLURXY(8192)

#define HEINZEL_HARRIS(H)                                                                                \
    BENCHMARK(bench_harris_nest<H, 2048>)->Name("harris/heinzel/H" #H "_W2048")->UseRealTime();          \
    BENCHMARK((bench_harris_nest<H, 2048, true>))->Name("harris/heinzel-par/H" #H "_W2048")->UseRealTime(); \
    BENCHMARK((bench_harris_hand<H, 2048, true>))->Name("harris/hand-mat/H" #H "_W2048")->UseRealTime(); \
    BENCHMARK((bench_harris_hand<H, 2048, false>))->Name("harris/hand-fused/H" #H "_W2048")->UseRealTime();
HEINZEL_HARRIS(32)
HEINZEL_HARRIS(1024)
HEINZEL_HARRIS(8192)

BENCHMARK_MAIN();
