// The Heinzel arms of the KFR comparison's whole-picture rows (bench_compare_kfr, Plan/23 09 (a)): biquad8, eq8mix,
// eq8fb and eq8sweep over the whole input and in blocks of 256, each a graph on `Executor<…, kMachine>`
// (eq8_graph.hpp), checked against the row's double reference (eq8_common.hpp) before it is timed. An object library of
// the target: compiled with Heinzel's flags, apart from KFR's and the hand arms' files.
//
// And the EQ with its 40 coefficients as Params (`biquad8/heinzel-param`), set once before timing, and
// `biquad8/heinzel-param-moving`: the moving band's five (eq8_common.hpp's sweep) set before every run from a
// table computed before timing; the JUCE target runs biquad8's arms too. And lr8, an eighth-order Linkwitz-Riley
// low-pass as four sections.

#include <benchmark/benchmark.h>

#include "eq8_graph.hpp"

namespace {

namespace hz = heinzel;
using kfrcmp::Row;

constexpr hz::Graph biquad8_whole() { return eq8_graph::biquad8(kfrcmp::N); }
constexpr hz::Graph biquad8_block() { return eq8_graph::biquad8(hz::Length{}); }
constexpr hz::Graph mix_whole() { return eq8_graph::mix(kfrcmp::N); }
constexpr hz::Graph mix_block() { return eq8_graph::mix(hz::Length{}); }
constexpr hz::Graph feedback_whole() { return eq8_graph::feedback(kfrcmp::N); }
constexpr hz::Graph feedback_block() { return eq8_graph::feedback(hz::Length{}); }
constexpr hz::Graph sweep_whole() { return eq8_graph::sweep(kfrcmp::N); }
constexpr hz::Graph sweep_block() { return eq8_graph::sweep(hz::Length{}); }

constexpr hz::Graph biquad8_param_whole() { return iir_arms::df1_cascade_param<8>(kfrcmp::N); }
constexpr hz::Graph biquad8_param_block() { return iir_arms::df1_cascade_param<8>(hz::Length{}); }
constexpr hz::Graph lr8_whole() { return eq8_graph::lr8(kfrcmp::N); }
constexpr hz::Graph lr8_block() { return eq8_graph::lr8(hz::Length{}); }

BENCHMARK(eq8_graph::time_whole<Row::biquad8, biquad8_whole>)->Name("biquad8/heinzel")->UseRealTime();
BENCHMARK(eq8_graph::time_blocks<Row::biquad8, biquad8_block>)->Name("biquad8/heinzel/B256")->UseRealTime();
BENCHMARK(eq8_graph::param_whole<biquad8_param_whole>)->Name("biquad8/heinzel-param")->UseRealTime();
BENCHMARK(eq8_graph::param_blocks<biquad8_param_block, false>)->Name("biquad8/heinzel-param/B256")->UseRealTime();
BENCHMARK(eq8_graph::param_blocks<biquad8_param_block, true>)->Name("biquad8/heinzel-param-moving/B256")->UseRealTime();
BENCHMARK(eq8_graph::time_whole<Row::lr8, lr8_whole>)->Name("lr8/heinzel")->UseRealTime();
BENCHMARK(eq8_graph::time_blocks<Row::lr8, lr8_block>)->Name("lr8/heinzel/B256")->UseRealTime();
BENCHMARK(eq8_graph::time_whole<Row::mix, mix_whole>)->Name("eq8mix/heinzel")->UseRealTime();
BENCHMARK(eq8_graph::time_blocks<Row::mix, mix_block>)->Name("eq8mix/heinzel/B256")->UseRealTime();
BENCHMARK(eq8_graph::time_whole<Row::fb, feedback_whole>)->Name("eq8fb/heinzel")->UseRealTime();
BENCHMARK(eq8_graph::time_blocks<Row::fb, feedback_block>)->Name("eq8fb/heinzel/B256")->UseRealTime();
BENCHMARK(eq8_graph::time_whole<Row::sweep, sweep_whole>)->Name("eq8sweep/heinzel")->UseRealTime();
BENCHMARK(eq8_graph::time_blocks<Row::sweep, sweep_block>)->Name("eq8sweep/heinzel/B256")->UseRealTime();

} // namespace
