// The Heinzel arm of the KFR comparison's many-channel rows (bench_compare_kfr, Plan/23 09 (b); channels_common.hpp has
// the rows): kEq's three direct-form-I sections over `Signal{C, Length{}}`, the channels in lanes, on `Executor<…,
// kMachine>`; `heinzel-param` takes the coefficients as Params, set at run time. Heinzel takes its channels frame-major, so `inter` is read where it lies; `planar` (a pointer a channel,
// which a port cannot declare: the reserved `input_layout`) is interleaved into a scratch block and back inside the
// timed run, as every other arm converts the layout it does not take. An object library of the target: compiled with
// Heinzel's flags, apart from KFR's and the hand arms' files.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <benchmark/benchmark.h>

#include "bench_machine.hpp"
#include "channels_common.hpp"
#include "iir_arms.hpp"
#include "kfr_compare_common.hpp"
#include "kfr_compare_refs.hpp"

#include "page_vector.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace {

namespace hz = heinzel;
using namespace kfrcmp;
using channels::kFrames;
using channels::Layout;

// `Param`: the coefficients known only at run time (`heinzel-param`), set from kEq before the first run.
template <std::size_t C, bool Param>
constexpr hz::Graph eq_channels() {
    if constexpr (Param) {
        return iir_arms::df1_cascade_param<3>(static_cast<std::int32_t>(C), hz::Length{});
    } else {
        return iir_arms::df1_cascade(iir_arms::kEq, static_cast<std::int32_t>(C), hz::Length{});
    }
}

// A channel's two blocks through the textbook DF-I cascade: the reference.
void eq_reference(const float* x, float* y, std::size_t n) { df1_cascade<3>(kEq, x, y, static_cast<std::int32_t>(n)); }

template <std::size_t C, bool Param>
void bench_heinzel(benchmark::State& state, Layout layout) {
    if (!Param && !iir_arms::same_bits(iir_arms::kEq, kEq)) {
        state.SkipWithError("biquad3ch/heinzel: the coefficients' copy differs from kEq");
        return;
    }
    const auto run = std::make_unique<hz::Executor<eq_channels<C, Param>, heinzel_bench::kMachine>>();
    run->prepare(static_cast<std::int64_t>(kFrames));
    if constexpr (Param) {
        iir_arms::bind_coefficients(*run, kEq);
    }
    const PageVector<float> xi = channels::interleaved(g_sine_in.data(), C);
    const std::vector<float> ref = channels::reference(g_sine_in.data(), C, eq_reference);
    OutputVector<float> y(C * kFrames);
    OutputVector<float, 1> scratch_in(C * kFrames);
    OutputVector<float, 2> scratch_out(C * kFrames);
    const bool planar = layout == Layout::planar;
    run->input("x") = planar ? scratch_in.data() : xi.data();
    run->output("y") = planar ? scratch_out.data() : y.data();
    std::size_t bound = 0; // the inter block the input is bound to
    channels::time_arm(state, layout, C, ref, y.data(), [&](std::size_t b) {
        if (planar) {
            channels::to_inter(g_sine_in.data() + b * kFrames, C, scratch_in.data());
            run->run(static_cast<std::int64_t>(kFrames));
            channels::to_planar(scratch_out.data(), C, y.data());
            return;
        }
        if (b != bound) {
            run->input("x") = xi.data() + b * kFrames * C;
            bound = b;
        }
        run->run(static_cast<std::int64_t>(kFrames));
    });
}

template <std::size_t C>
void register_arm() {
    for (const Layout layout : {Layout::planar, Layout::inter}) {
        benchmark::RegisterBenchmark(channels::name_of(layout, "heinzel", C), bench_heinzel<C, false>, layout)->UseRealTime();
        benchmark::RegisterBenchmark(channels::name_of(layout, "heinzel-param", C), bench_heinzel<C, true>, layout)->UseRealTime();
    }
}

const bool g_registered = [] {
    register_arm<2>();
    register_arm<8>();
    register_arm<16>();
    return true;
}();

} // namespace
