// Third-party comparison benchmark: Heinzel vs crv (github.com/scztt/crv, Scott Carver's ADC 2025 library, a
// header-only C++26 prototype with Heinzel's premise: a graph apart from its execution plan, values on a voices × time
// grid). Its recorded benchmark, an additive synth, moved here from Plan/probes/crv/ (README.md there has the history
// and the findings).
//
// This file carries crv's arms, copied as crv's benchmarks/performance_benchmarks.cpp ("Subgraph performance") writes
// them, and is compiled with crv's benchmark flags (benchmarks/CMakeLists.txt), once as crv builds it and once more
// with `-mllvm -unroll-threshold=4000` (`bench_compare_crv_unrolled.cpp`, whose `CRV_ARM` names those arms apart).
// Each arm is a plain function that writes the 1024 samples to `out`. Heinzel's arms, the hand arms, the checks and the
// registrations are in `bench_compare_crv_heinzel.cpp`, compiled with Heinzel's flags.

#include <crv/graph/graph_processing.hpp>
#include <crv/graph/memory.hpp>
#include <crv/node_syntax.hpp>
#include <graph_helpers.hpp>

#include "additive_synth_common.hpp"

using namespace crv;

namespace {

// performance_benchmarks.cpp's `run_graph`: every voice's partial summed by `subgraph_sum<vblock, sblock>`, the
// samples visited `sblock` at a time, every input a compile-time value.
template <std::size_t vblock, std::size_t sblock, axis out_axis>
void run_graph(double* out) {
    using namespace additive_synth;
    namespace n = crv::nodes;
    auto inputs = inputs_t::make(
        cv<test_count>, cv<test_fundamental>, cv<test_formant_freq>, cv<test_formant_width>, cv<test_formant_amp>,
        cv<test_formant_detune>);
    constexpr auto count = n::input(props::count);
    constexpr auto sum = n::subgraph_sum<vblock, sblock, double>(count, n::cursor(), make_partial_graph<cv<true>, out_axis>())(
        n::input(props::fundamental), n::input(props::formant_freq), n::input(props::formant_width),
        n::input(props::formant_amp), n::input(props::formant_detune));
    constexpr auto graph = digraph::make_graph(sum);
    for (std::size_t i = 0; i < output_samples; i += sblock) {
        auto result = visit_fold(graph_visitor<1, sblock, decltype(inputs) const&>{inputs, 0, i}, graph);
        auto res_block = unwrap_value(result);
        for (std::size_t j = 0; j < sblock; ++j) {
            out[i + j] = res_block(0, j);
        }
    }
}

// crv's reference loop, the voice constants recomputed per sample, four samples a block.
void naive(double* out) {
    using namespace additive_synth;
    const auto result = naive_additive_synth_blocked<4>(
        test_count, test_fundamental, test_formant_freq, test_formant_width, test_formant_amp, test_formant_detune);
    for (std::size_t i = 0; i < output_samples; ++i) {
        out[i] = result[i];
    }
}

} // namespace

#ifndef CRV_ARM
#define CRV_ARM(name) name
#endif

extern "C" __attribute__((noinline)) void CRV_ARM(crv_graph_col_1x16)(double* out) { run_graph<1, 16, axis::col>(out); }
extern "C" __attribute__((noinline)) void CRV_ARM(crv_graph_col_4x4)(double* out) { run_graph<4, 4, axis::col>(out); }
extern "C" __attribute__((noinline)) void CRV_ARM(crv_graph_row_2x8)(double* out) { run_graph<2, 8, axis::row>(out); }
extern "C" __attribute__((noinline)) void CRV_ARM(crv_graph_row_4x4)(double* out) { run_graph<4, 4, axis::row>(out); }
extern "C" __attribute__((noinline)) void CRV_ARM(crv_naive_blocked4)(double* out) { naive(out); }
