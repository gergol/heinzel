// The Heinzel arms of the JUCE comparison's whole-picture rows biquad8 and eq8fb (bench_compare_juce, Plan/23 09 (a)):
// KFR's eight-section EQ alone, with constant coefficients and as Params (at rest and with one band moving), and with
// its output fed back through a clip; the graphs of eq8_graph.hpp on `Executor<…, kMachine>`, over the whole input and
// in blocks of 256, checked against the row's double reference (eq8_common.hpp) before they are timed. The same arms as
// the KFR target's, beside JUCE's in this binary. An object library of the target: compiled with Heinzel's flags, apart
// from JUCE's files.

#include <benchmark/benchmark.h>

#include "eq8_graph.hpp"

namespace {

namespace hz = heinzel;
using kfrcmp::Row;

constexpr hz::Graph biquad8_whole() { return eq8_graph::biquad8(kfrcmp::N); }
constexpr hz::Graph biquad8_block() { return eq8_graph::biquad8(hz::Length{}); }
constexpr hz::Graph biquad8_param_whole() { return iir_arms::df1_cascade_param<8>(kfrcmp::N); }
constexpr hz::Graph biquad8_param_block() { return iir_arms::df1_cascade_param<8>(hz::Length{}); }
constexpr hz::Graph feedback_whole() { return eq8_graph::feedback(kfrcmp::N); }
constexpr hz::Graph feedback_block() { return eq8_graph::feedback(hz::Length{}); }

BENCHMARK(eq8_graph::time_whole<Row::biquad8, biquad8_whole>)->Name("biquad8/heinzel")->UseRealTime();
BENCHMARK(eq8_graph::time_blocks<Row::biquad8, biquad8_block>)->Name("biquad8/heinzel/B256")->UseRealTime();
BENCHMARK(eq8_graph::param_whole<biquad8_param_whole>)->Name("biquad8/heinzel-param")->UseRealTime();
BENCHMARK(eq8_graph::param_blocks<biquad8_param_block, false>)->Name("biquad8/heinzel-param/B256")->UseRealTime();
BENCHMARK(eq8_graph::param_blocks<biquad8_param_block, true>)->Name("biquad8/heinzel-param-moving/B256")->UseRealTime();
BENCHMARK(eq8_graph::time_whole<Row::fb, feedback_whole>)->Name("eq8fb/heinzel")->UseRealTime();
BENCHMARK(eq8_graph::time_blocks<Row::fb, feedback_block>)->Name("eq8fb/heinzel/B256")->UseRealTime();

} // namespace
