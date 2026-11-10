// Applications (tier-2), the image and ML rows `main`'s `apps_bench` had and this branch's port pruned (P6.2,
// BP.6; `Plan/archive/parallel/06` §5): each a nest arm over the same F32 data as `main`'s plain C++ arms, ported
// verbatim, every arm checked against a float64 reference before it is timed.
//
//   img/box3, img/box5   the mean of a 3 x 3 / 5 x 5 box over a 512 x 512 image     a window over two axes
//   img/sharpen3         a 3 x 3 kernel fed at run time                              the same, weighted
//   ml/gemm256           a 256 x 256 x 256 product                                   a contraction
//   ml/softmax           row softmax over {1024, 256}                                two folds, exp between
//
// (`main`'s `img/sharpen3/hz_const` bakes the kernel as a constant tensor; this tree has no constant tensor
// spelling, so that arm is not ported.)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "page_vector.hpp"
#include "parallel_arm.hpp"

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace hz = heinzel;

namespace {

constexpr int kH = 512;
constexpr int kW = 512;
constexpr int kG = 256;
constexpr int kRM = 1024;
constexpr int kRK = 256;

PageVector<float> ramp(std::size_t n, float a, float b) {
    PageVector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = a * static_cast<float>((i * 2654435761u) % 1009) / 1009.0f + b;
    }
    return v;
}

constexpr hz::Graph analysed(hz::Graph g) {
    if (!hz::infer(g).ok) {
        hz::fail("bench_apps_image_ml: a graph did not infer");
    }
    return g;
}

template <int Box>
constexpr hz::Graph box() {
    hz::Input x{"x", {kH, kW}};
    return analysed(hz::finish(hz::Output{"y"} = x.unfold2d(0, Box, 1, Box).sum({1, 3}) *
                                               static_cast<double>(1.0f / (Box * Box))));
}

constexpr hz::Graph kSharpen = [] {
    hz::Input x{"x", {kH, kW}};
    hz::Input k{"k", {1, 3, 1, 3}};
    return analysed(hz::finish(hz::Output{"y"} = (x.unfold2d(0, 3, 1, 3) * k).sum({1, 3})));
}();

constexpr hz::Graph kGemm = [] {
    hz::Input a{"a", {kG, kG}};
    hz::Input b{"b", {kG, kG}};
    return analysed(hz::finish(hz::Output{"y"} = hz::einsum("ik,kj->ij", a, b)));
}();

constexpr hz::Graph kSoftmax = [] {
    hz::Input x{"x", {kRM, kRK}};
    hz::Expr e = hz::exp(x - x.max(1, hz::keepdims));
    return analysed(hz::finish(hz::Output{"y"} = e / e.sum(1, hz::keepdims)));
}();

constexpr hz::Graph kBox3 = box<3>();
constexpr hz::Graph kBox5 = box<5>();
template <const hz::Graph& G>
constexpr const hz::loops::Program& kNest = heinzel_bench::kProgramOf<G, heinzel_bench::kMachine>;

// ---- the references, in double ----------------------------------------------------------------------------

std::vector<double> box_ref(const PageVector<float>& x, const PageVector<float>* k, int b) {
    const int ho = kH - b + 1;
    const int wo = kW - b + 1;
    std::vector<double> y(static_cast<std::size_t>(ho) * wo);
    for (int i = 0; i < ho; ++i) {
        for (int j = 0; j < wo; ++j) {
            double a = 0;
            for (int u = 0; u < b; ++u) {
                for (int v = 0; v < b; ++v) {
                    a += static_cast<double>(x[static_cast<std::size_t>((i + u) * kW + j + v)]) * (k ? (*k)[static_cast<std::size_t>(u * b + v)] : 1.0);
                }
            }
            y[static_cast<std::size_t>(i * wo + j)] = k ? a : a * static_cast<double>(1.0f / (b * b));
        }
    }
    return y;
}

std::vector<double> gemm_ref(const PageVector<float>& a, const PageVector<float>& b) {
    std::vector<double> y(static_cast<std::size_t>(kG) * kG, 0.0);
    for (int i = 0; i < kG; ++i) {
        for (int k = 0; k < kG; ++k) {
            for (int j = 0; j < kG; ++j) {
                y[static_cast<std::size_t>(i * kG + j)] += static_cast<double>(a[static_cast<std::size_t>(i * kG + k)]) * b[static_cast<std::size_t>(k * kG + j)];
            }
        }
    }
    return y;
}

std::vector<double> softmax_ref(const PageVector<float>& x) {
    std::vector<double> y(x.size());
    for (int i = 0; i < kRM; ++i) {
        const auto row = static_cast<std::size_t>(i) * kRK;
        double m = x[row];
        double s = 0;
        for (int j = 1; j < kRK; ++j) {
            m = std::max(m, static_cast<double>(x[row + static_cast<std::size_t>(j)]));
        }
        for (int j = 0; j < kRK; ++j) {
            y[row + static_cast<std::size_t>(j)] = std::exp(x[row + static_cast<std::size_t>(j)] - m);
            s += y[row + static_cast<std::size_t>(j)];
        }
        for (int j = 0; j < kRK; ++j) {
            y[row + static_cast<std::size_t>(j)] /= s;
        }
    }
    return y;
}

// Every output within `tol` of the reference, relative to the reference's largest magnitude; the largest
// relative error seen, or -1 where one is out of bounds.
double checked(const float* y, const std::vector<double>& want, double tol) {
    double scale = 0.0;
    double worst = 0.0;
    for (const double w : want) {
        scale = std::max(scale, std::fabs(w));
    }
    for (std::size_t i = 0; i < want.size(); ++i) {
        const double off = std::fabs(static_cast<double>(y[i]) - want[i]) / std::max(scale, 1e-30);
        if (!(off <= tol)) {
            return -1.0;
        }
        worst = std::max(worst, off);
    }
    return worst;
}

template <const hz::loops::Program& P>
void time_nest(benchmark::State& state, const hz::loops::Feeds& feeds, const std::vector<double>& want, double tol, std::int64_t items) {
    hz::loops::Workspace<P> work(hz::loops::Sizes{});
    hz::loops::run<P>(feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(P.outputs[0]));
    const double worst = checked(y, want, tol);
    if (worst < 0) {
        state.SkipWithError("bench_apps_image_ml: the nest arm is outside its bound");
        return;
    }
    state.counters["max_rel_err"] = worst;
    for (auto _ : state) {
        hz::loops::run<P>(feeds, work);
        benchmark::DoNotOptimize(y);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * items);
}

template <class Run>
void time_ref(benchmark::State& state, OutputVector<float>& y, const std::vector<double>& want, double tol, std::int64_t items, const Run& run) {
    run();
    if (checked(y.data(), want, tol) < 0) {
        state.SkipWithError("bench_apps_image_ml: a reference arm disagrees with the float64 reference");
        return;
    }
    for (auto _ : state) {
        benchmark::ClobberMemory();
        run();
        benchmark::DoNotOptimize(y.data());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * items);
}

// ---- the arms -----------------------------------------------------------------------------------------------

template <int B, const hz::Graph& G>
void box_nest(benchmark::State& s) {
    const PageVector<float> x = ramp(static_cast<std::size_t>(kH) * kW, 1, 0.0f);
    time_nest<kNest<G>>(s, hz::loops::Feeds{x.data()}, box_ref(x, nullptr, B), 1e-5, static_cast<std::int64_t>(kH - B + 1) * (kW - B + 1));
}
template <int B>
void box_naive(benchmark::State& s) {
    constexpr int ho = kH - B + 1;
    constexpr int wo = kW - B + 1;
    const PageVector<float> x = ramp(static_cast<std::size_t>(kH) * kW, 1, 0.0f);
    OutputVector<float> y(static_cast<std::size_t>(ho) * wo);
    const float inv = 1.0f / (B * B);
    time_ref(s, y, box_ref(x, nullptr, B), 1e-5, static_cast<std::int64_t>(ho) * wo, [&] {
        for (int i = 0; i < ho; ++i) {
            for (int j = 0; j < wo; ++j) {
                float a = 0;
                for (int u = 0; u < B; ++u) {
                    for (int v = 0; v < B; ++v) {
                        a += x[static_cast<std::size_t>((i + u) * kW + (j + v))];
                    }
                }
                y[static_cast<std::size_t>(i * wo + j)] = a * inv;
            }
        }
    });
}
template <int B>
void box_hand_separable(benchmark::State& s) { // a row pass, then a column pass
    constexpr int ho = kH - B + 1;
    constexpr int wo = kW - B + 1;
    const PageVector<float> x = ramp(static_cast<std::size_t>(kH) * kW, 1, 0.0f);
    OutputVector<float, 1> tmp(static_cast<std::size_t>(kH) * wo);
    OutputVector<float> y(static_cast<std::size_t>(ho) * wo);
    const float inv = 1.0f / (B * B);
    time_ref(s, y, box_ref(x, nullptr, B), 1e-5, static_cast<std::int64_t>(ho) * wo, [&] {
        for (int i = 0; i < kH; ++i) {
            for (int j = 0; j < wo; ++j) {
                float a = 0;
                for (int v = 0; v < B; ++v) {
                    a += x[static_cast<std::size_t>(i * kW + j + v)];
                }
                tmp[static_cast<std::size_t>(i * wo + j)] = a;
            }
        }
        for (int i = 0; i < ho; ++i) {
            for (int j = 0; j < wo; ++j) {
                float a = 0;
                for (int u = 0; u < B; ++u) {
                    a += tmp[static_cast<std::size_t>((i + u) * wo + j)];
                }
                y[static_cast<std::size_t>(i * wo + j)] = a * inv;
            }
        }
    });
}

const PageVector<float> kSharpenK{0, -1, 0, -1, 5, -1, 0, -1, 0};

void sharpen_nest(benchmark::State& s) {
    const PageVector<float> x = ramp(static_cast<std::size_t>(kH) * kW, 1, 0.0f);
    time_nest<kNest<kSharpen>>(s, hz::loops::Feeds{x.data(), kSharpenK.data()}, box_ref(x, &kSharpenK, 3), 1e-5,
                               static_cast<std::int64_t>(kH - 2) * (kW - 2));
}
void sharpen_naive(benchmark::State& s) {
    constexpr int ho = kH - 2;
    constexpr int wo = kW - 2;
    const PageVector<float> x = ramp(static_cast<std::size_t>(kH) * kW, 1, 0.0f);
    OutputVector<float> y(static_cast<std::size_t>(ho) * wo);
    time_ref(s, y, box_ref(x, &kSharpenK, 3), 1e-5, static_cast<std::int64_t>(ho) * wo, [&] {
        for (int i = 0; i < ho; ++i) {
            for (int j = 0; j < wo; ++j) {
                float a = 0;
                for (int u = 0; u < 3; ++u) {
                    for (int v = 0; v < 3; ++v) {
                        a += x[static_cast<std::size_t>((i + u) * kW + (j + v))] * kSharpenK[static_cast<std::size_t>(u * 3 + v)];
                    }
                }
                y[static_cast<std::size_t>(i * wo + j)] = a;
            }
        }
    });
}

void gemm_nest(benchmark::State& s) {
    const PageVector<float> a = ramp(static_cast<std::size_t>(kG) * kG, 0.3f, -0.1f);
    const PageVector<float> b = ramp(static_cast<std::size_t>(kG) * kG, 0.3f, -0.1f);
    time_nest<kNest<kGemm>>(s, hz::loops::Feeds{a.data(), b.data()}, gemm_ref(a, b), 1e-4, 2LL * kG * kG * kG);
}
void gemm_hand(benchmark::State& s) {
    const PageVector<float> a = ramp(static_cast<std::size_t>(kG) * kG, 0.3f, -0.1f);
    const PageVector<float> b = ramp(static_cast<std::size_t>(kG) * kG, 0.3f, -0.1f);
    OutputVector<float> y(static_cast<std::size_t>(kG) * kG);
    time_ref(s, y, gemm_ref(a, b), 1e-4, 2LL * kG * kG * kG, [&] {
        std::fill(y.begin(), y.end(), 0.0f);
        for (int i = 0; i < kG; ++i) {
            for (int k = 0; k < kG; ++k) {
                const float av = a[static_cast<std::size_t>(i * kG + k)];
                for (int j = 0; j < kG; ++j) {
                    y[static_cast<std::size_t>(i * kG + j)] += av * b[static_cast<std::size_t>(k * kG + j)];
                }
            }
        }
    });
}

void softmax_nest(benchmark::State& s) {
    const PageVector<float> x = ramp(static_cast<std::size_t>(kRM) * kRK, 1, -0.5f);
    time_nest<kNest<kSoftmax>>(s, hz::loops::Feeds{x.data()}, softmax_ref(x), 1e-5, static_cast<std::int64_t>(kRM) * kRK);
}
void softmax_naive(benchmark::State& s) {
    const PageVector<float> x = ramp(static_cast<std::size_t>(kRM) * kRK, 1, -0.5f);
    OutputVector<float> y(x.size());
    time_ref(s, y, softmax_ref(x), 1e-5, static_cast<std::int64_t>(kRM) * kRK, [&] {
        for (int i = 0; i < kRM; ++i) {
            const auto row = static_cast<std::size_t>(i) * kRK;
            float m = x[row];
            for (int j = 1; j < kRK; ++j) {
                m = x[row + static_cast<std::size_t>(j)] > m ? x[row + static_cast<std::size_t>(j)] : m;
            }
            float sum = 0;
            for (int j = 0; j < kRK; ++j) {
                const float e = std::exp(x[row + static_cast<std::size_t>(j)] - m);
                y[row + static_cast<std::size_t>(j)] = e;
                sum += e;
            }
            const float inv = 1.0f / sum;
            for (int j = 0; j < kRK; ++j) {
                y[row + static_cast<std::size_t>(j)] *= inv;
            }
        }
    });
}

} // namespace

BENCHMARK((box_nest<3, kBox3>))->Name("img/box3/heinzel")->UseRealTime();
BENCHMARK(box_naive<3>)->Name("img/box3/naive")->UseRealTime();
BENCHMARK(box_hand_separable<3>)->Name("img/box3/hand-separable")->UseRealTime();
BENCHMARK((box_nest<5, kBox5>))->Name("img/box5/heinzel")->UseRealTime();
BENCHMARK(box_naive<5>)->Name("img/box5/naive")->UseRealTime();
BENCHMARK(box_hand_separable<5>)->Name("img/box5/hand-separable")->UseRealTime();
BENCHMARK(sharpen_nest)->Name("img/sharpen3/heinzel")->UseRealTime();
BENCHMARK(sharpen_naive)->Name("img/sharpen3/naive")->UseRealTime();
BENCHMARK(gemm_nest)->Name("ml/gemm256/heinzel")->UseRealTime();
BENCHMARK(gemm_hand)->Name("ml/gemm256/hand")->UseRealTime();
BENCHMARK(softmax_nest)->Name("ml/softmax/heinzel")->UseRealTime();
BENCHMARK(softmax_naive)->Name("ml/softmax/naive")->UseRealTime();

BENCHMARK_MAIN();
