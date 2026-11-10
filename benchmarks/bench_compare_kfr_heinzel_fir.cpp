// The Heinzel arms of the KFR comparison's FIR rows (bench_compare_kfr, Plan/23 P7): the windowed-sinc low-pass of
// kfr_compare_common.hpp over the whole input, `y[i] = Σ_j h[K-1-j]·x[i+j]` written as a window over the input times
// the reversed taps, summed (the valid region, N − K + 1 outputs, as KFR's are counted). The taps are an input, bound
// at run time. Checked against the hand convolution, with KFR's tolerance, before it is timed.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "heinzel_arms.hpp"
#include "kfr_compare_common.hpp"
#include "kfr_compare_refs.hpp"

#include "page_vector.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace {

using namespace kfrcmp;
using namespace heinzel_arms;

template <int K>
constexpr hz::Graph fir_graph() {
    hz::Input x{"x", {N}};
    hz::Input h{"h", {1, K}};
    return hz::finish(hz::Output{"y"} = (x.unfold(0, K) * h).sum(1));
}

template <int K>
void bench_heinzel_fir(benchmark::State& state) {
    constexpr int kOut = N - K + 1;
    const std::array<float, K>& taps = kFirTaps<K>;
    const PageVector<float> reversed(taps.rbegin(), taps.rend());
    const int checked = std::min(4096, kOut);
    OutputVector<float> y(static_cast<std::size_t>(kOut));
    std::vector<float> ref(static_cast<std::size_t>(checked));
    hand_fir(g_sine_in.data(), taps.data(), K, checked, ref.data());
    timed<fir_graph<K>>(
        state, kOut,
        [&](auto& run) {
            run.input("x") = g_sine_in.data();
            run.input("h") = reversed.data();
            run.output("y") = y.data();
        },
        [&]() -> const char* {
            return nearly_equal(y.data(), ref.data(), ref.size(), 2e-4f * static_cast<float>(K) / 65.0f)
                       ? nullptr
                       : "fir/heinzel: mismatch vs the hand convolution";
        });
}

BENCHMARK(bench_heinzel_fir<15>)->Name("fir15/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_fir<65>)->Name("fir65/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_fir<79>)->Name("fir79/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_fir<257>)->Name("fir257/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_fir<1025>)->Name("fir1025/heinzel")->UseRealTime();
BENCHMARK(bench_heinzel_fir<4097>)->Name("fir4097/heinzel")->UseRealTime();

} // namespace
