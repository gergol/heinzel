// The oracle of the runner's skewed stages (`docs/sections-in-lanes.md`). Each tree of
// `support/skewed_sections.hpp` runs bit for bit as the walk (`tests/reference/`) of its linked graph over 4096 samples
// of noise:
//   - the linked graphs (`support/linked_sections.hpp`), in double at High: the TPT pair (k = 2, a value only the link
//     reads), LR8 (k = 4), the five peaks (k = 5 in two bodies, three padding lanes, their coefficients [5, 1] feeds),
//     the eight tanh sections (two bodies; silence is not mapped to silence) and the pair into the eight (two skewed
//     stages, the second's fill the first's slice);
//   - a Param cascade of direct-form-I biquads as the merge and the rules after it leave it, at Normal (x and y float,
//     the cycle double, the link's term last and fused), of 3 sections (a padding lane) and 8 (two bodies), its five
//     coefficients [k, 1] float feeds or buffers a gathering stage before it fills from 5k Params.
// Each in one run, in blocks of 1, k − 1, k, 32 and 256 and of uneven lengths one after another, and again after
// `reset()`: every run canaried (`canaried.hpp`), each block's input a copy of exactly its length (a read past it is
// the sanitizer's), and nothing stored past the block. Then a coefficient changed between runs: the Param
// cascades, their numbers set before every run, against the walk of the same graph with each coefficient an input
// holding that block's numbers, at blocks of 1, 32 and 256. One listing is pinned; every tree is well formed. The
// gathered coefficients are read from copies in the frame, taken anew every run.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/print.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/loops/workspace.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

#include "canaried.hpp"
#include "hand_built_graph.hpp"
#include "interpret.hpp"
#include "linked_sections.hpp"
#include "skewed_sections.hpp"
#include "well_formed.hpp"

using namespace heinzel;
namespace ls = heinzel::testing::linked_sections;
namespace ss = skewed_sections;

namespace {

int g_wrong = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("FAILED: %s\n", what);
    }
}

constexpr std::size_t kSamples = static_cast<std::size_t>(ls::kLength);
constexpr std::size_t kPlanes = 256;

// ---- the walk ------------------------------------------------------------------------------------------------------

// Noise in [-1, 1], each number a float (the Param cascade's x is one).
std::vector<double> noise() {
    std::vector<double> x(kSamples);
    std::uint32_t state = 20261025u;
    for (double& v : x) {
        state = state * 1664525u + 1013904223u;
        v = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.001);
    }
    return x;
}

// Graph `g`'s output, walked with input i fed `fed[i]`, its numbers a sample one after another.
std::vector<double> walked(const Graph& g, const std::vector<std::vector<double>>& fed) {
    Inputs feeds{};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        const std::int32_t width = elements_of(g.values[static_cast<std::size_t>(g.inputs[i].value)].shape).count;
        feeds.push_back(InputFeed{fed[i].data(), width});
    }
    const auto walk = std::make_unique<Signals<kSamples, kPlanes>>();
    run(g, feeds, *walk);
    std::vector<double> y(kSamples);
    for (std::size_t n = 0; n < kSamples; ++n) {
        y[n] = sample_at(*walk, g.outputs[0].value, 0, n);
    }
    return y;
}

// A timeless input's numbers at every sample.
std::vector<double> every_sample(std::span<const double> numbers) {
    std::vector<double> fed{};
    for (std::size_t n = 0; n < kSamples; ++n) {
        fed.insert(fed.end(), numbers.begin(), numbers.end());
    }
    return fed;
}

// ---- the Param cascade's graph and numbers -------------------------------------------------------------------------

// Its linked graph, in the order and types `skewed_sections::param_stage` runs it, at Normal (`_madd` is not granted at
// High): x and the five coefficients (over [k, 1]) float inputs, the cycle in double, its last section converted down.
constexpr Graph param_graph(std::int32_t k) {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{ls::kLength}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    constexpr std::array<Name, 5> kNames{Name{"b0"}, Name{"b1"}, Name{"b2"}, Name{"na1"}, Name{"na2"}};
    std::array<std::int32_t, 5> c{};
    for (std::size_t i = 0; i < c.size(); ++i) {
        c[i] = add_value(g, ElemType::F32, Shape{k, 1}, ValueCategory::Input);
        g.values[static_cast<std::size_t>(c[i])].timeless = true;
        g.inputs.push_back(NamedValue{kNames[i], c[i]});
    }
    const auto v = ls::variable(g, Shape{k, ls::kLength});
    const auto u = add_node(g, op::Chain, x, v);
    const auto u1 = ls::lag(g, u, 1);
    const auto u2 = ls::lag(g, u, 2);
    const auto v1 = ls::lag(g, v, 1);
    const auto v2 = ls::lag(g, v, 2);
    const auto p1 = add_node(g, op::Mul, c[2], u2);
    const auto p2 = add_node(g, op::Mul, c[4], v2);
    const auto old = add_node(g, op::Add, p1, p2);
    const auto p3 = add_node(g, op::Mul, c[1], u1);
    const auto t1 = add_node(g, op::Add, old, p3);
    const auto p4 = add_node(g, op::Mul, c[3], v1);
    const auto t2 = add_node(g, op::Add, t1, p4);
    const auto next = add_node(g, op::MultiplyAdd, c[0], u, t2);
    ls::settle(g, next, v);
    OpAttr down{};
    down.kind = AttrKind::Cast;
    down.cast.to = ElemType::F32;
    const auto narrowed = add_node(g, op::Cast, v, kNoValue, kNoValue, down);
    const auto y = ls::section_of(g, narrowed, k - 1, Shape{ls::kLength});
    g.outputs.push_back(NamedValue{Name{"y"}, y});
    set_default_precision(g, numerics::Precision::Normal);
    if (const Diagnostic d = infer(g); !d.ok) {
        fail(d.message.view());
    }
    return g;
}

// Section j's numbers for the block from sample `at`: RBJ's peak, +6 dB, Q 1, at 48 kHz, at 62.5 · 2^j Hz (swept by up
// to a quarter over 2048 samples where `sweep`), as the stage reads them (b0 b1 b2 −a1 −a2), each a float.
std::array<double, 5> section_at(std::int32_t j, std::size_t at, bool sweep) {
    const double turn = sweep ? std::sin(2 * std::numbers::pi * static_cast<double>(at) / 2048) : 0;
    const double w = 2 * std::numbers::pi * 62.5 * static_cast<double>(1 << j) * (1 + 0.25 * turn) / 48000;
    const double big = std::pow(10.0, 6.0 / 40.0);
    const double alpha = std::sin(w) / 2;
    const double a0 = 1 + alpha / big;
    const std::array<double, 5> exact{(1 + alpha * big) / a0, -2 * std::cos(w) / a0, (1 - alpha * big) / a0, 2 * std::cos(w) / a0, -(1 - alpha / big) / a0};
    std::array<double, 5> rounded{};
    for (std::size_t i = 0; i < exact.size(); ++i) {
        rounded[i] = static_cast<float>(exact[i]);
    }
    return rounded;
}

// All k sections' numbers for the block from `at`: coefficient i of section j at i k + j.
std::vector<double> numbers_at(std::int32_t k, std::size_t at, bool sweep) {
    std::vector<double> all(static_cast<std::size_t>(5 * k));
    for (std::int32_t j = 0; j < k; ++j) {
        const std::array<double, 5> section = section_at(j, at, sweep);
        for (std::size_t i = 0; i < section.size(); ++i) {
            all[i * static_cast<std::size_t>(k) + static_cast<std::size_t>(j)] = section[i];
        }
    }
    return all;
}

// What a Param cascade's feeds point at besides x: its five [k, 1] columns (fed) or its 5k Params (gathered), set to the
// same numbers, as `Executor::param` sets a Param before a run.
struct Knobs {
    std::int32_t k;
    std::array<std::vector<float>, 5> columns;
    std::vector<float> params;

    explicit Knobs(std::int32_t sections) : k(sections), params(static_cast<std::size_t>(5 * sections)) {
        for (std::vector<float>& column : columns) {
            column.resize(static_cast<std::size_t>(sections));
        }
    }
    void set(const std::vector<double>& numbers) {
        for (std::size_t at = 0; at < numbers.size(); ++at) {
            params[at] = static_cast<float>(numbers[at]);
            columns[at / static_cast<std::size_t>(k)][at % static_cast<std::size_t>(k)] = params[at];
        }
    }
    loops::Feeds feeds(ss::Coefficients from) const {
        loops::Feeds f{};
        if (from == ss::Coefficients::Fed) {
            for (std::size_t i = 0; i < columns.size(); ++i) {
                f[1 + i] = columns[i].data();
            }
        } else {
            for (std::size_t at = 0; at < params.size(); ++at) {
                f[1 + at] = &params[at];
            }
        }
        return f;
    }
};

// The walk's coefficient inputs: at each sample, the numbers of the block of `block` samples it lies in.
std::vector<std::vector<double>> fed_in_blocks(const std::vector<double>& x, std::int32_t k, std::int64_t block, bool sweep) {
    std::vector<std::vector<double>> fed{x, {}, {}, {}, {}, {}};
    for (std::size_t n = 0; n < kSamples; ++n) {
        const std::vector<double> numbers = numbers_at(k, n - n % static_cast<std::size_t>(block), sweep);
        for (std::size_t i = 0; i < 5; ++i) {
            const auto from = numbers.begin() + static_cast<std::ptrdiff_t>(i) * k;
            fed[1 + i].insert(fed[1 + i].end(), from, from + k);
        }
    }
    return fed;
}

// ---- the runs ------------------------------------------------------------------------------------------------------

// `P` over `x` in blocks of `blocks` (cycled) one after another, reset before the run once `reset_at` samples are done,
// `before(at)` called before each run (`at` its first sample). Each block's x is a copy of exactly its length; every run
// is canaried, and a run storing past its block is counted. Its output.
template <const loops::Program& P, class In, class Out>
std::vector<double> ran(const std::vector<double>& x, const std::vector<std::int64_t>& blocks, loops::Feeds feeds, std::size_t reset_at, const auto& before) {
    constexpr Out kUntouched = 1234.5;
    const std::int64_t most = *std::max_element(blocks.begin(), blocks.end());
    canaried::Workspace<P> work(loops::sizes_of_length<P>(most));
    std::vector<double> y;
    std::size_t at = 0;
    std::size_t stored_past = 0;
    for (std::size_t b = 0; at < x.size(); ++b) {
        if (at == reset_at) {
            work.reset();
        }
        const auto m = static_cast<std::size_t>(std::min<std::int64_t>(blocks[b % blocks.size()], static_cast<std::int64_t>(x.size() - at)));
        work.set_length(static_cast<std::int64_t>(m));
        before(at);
        const std::vector<In> exact(x.begin() + static_cast<std::ptrdiff_t>(at), x.begin() + static_cast<std::ptrdiff_t>(at + m));
        feeds[0] = exact.data();
        Out* out = work.template data<Out>(static_cast<std::size_t>(P.outputs[0]));
        std::fill(out + m, out + most, kUntouched);
        canaried::run<P>(feeds, work);
        stored_past += std::all_of(out + m, out + most, [](Out v) { return v == kUntouched; }) ? 0 : 1;
        y.insert(y.end(), out, out + m);
        at += m;
    }
    check(stored_past == 0, "nothing is stored past the block");
    return y;
}

// Do `got` from sample `from` and `want` hold the same bits? The first that differs is printed.
bool same_bits(const char* what, const std::vector<double>& got, const std::vector<double>& want, std::size_t from = 0) {
    for (std::size_t n = 0; n < want.size(); ++n) {
        if (std::bit_cast<std::uint64_t>(got[from + n]) != std::bit_cast<std::uint64_t>(want[n])) {
            std::printf("  %s, sample %zu: ran %.17g, walked %.17g\n", what, n, got[from + n], want[n]);
            return false;
        }
    }
    return true;
}

// `P` (x of type In, y of Out) against `want`, the walk of its linked graph over `x`: in one run, in blocks of 1, 32,
// 256, of each of `short_blocks` (k − 1 and k) and of uneven lengths one after another, and again after reset.
template <const loops::Program& P, class In, class Out>
[[gnu::noinline]] void check_runs(const char* what, std::initializer_list<std::int64_t> short_blocks, const std::vector<double>& x, const std::vector<double>& want,
                                  const loops::Feeds& feeds) {
    static_assert(loops::accepted<P>());
    const bool finite = std::all_of(want.begin(), want.end(), [](double y) { return std::isfinite(y); });
    const bool moves = std::any_of(want.begin(), want.end(), [](double y) { return std::abs(y) > 0.01; });
    check(finite && moves, (std::string(what) + ": the walk's output moves and stays finite").c_str());
    const auto nothing = [](std::size_t) {};
    std::vector<std::vector<std::int64_t>> blockings{{static_cast<std::int64_t>(kSamples)}, {1}, {32}, {256}, {5, 1, 13, 2, 64, 3, 30, 1, 9}};
    for (const std::int64_t b : short_blocks) {
        blockings.push_back({b});
    }
    for (const std::vector<std::int64_t>& blocks : blockings) {
        const std::string named = std::string(what) + ", blocks of " + std::to_string(blocks[0]) + (blocks.size() > 1 ? " and more" : "");
        check(same_bits(named.c_str(), ran<P, In, Out>(x, blocks, feeds, kSamples, nothing), want), named.c_str());
    }
    std::vector<double> twice = x;
    twice.insert(twice.end(), x.begin(), x.end());
    const std::string named = std::string(what) + ", after reset()";
    check(same_bits(named.c_str(), ran<P, In, Out>(twice, {32}, feeds, kSamples, nothing), want, kSamples), named.c_str());
}

// ---- the trees -----------------------------------------------------------------------------------------------------

constexpr loops::Program kTpt = ss::tpt_pair();
constexpr loops::Program kLr8 = ss::lr8();
constexpr loops::Program kPeaks = ss::peaks();
constexpr loops::Program kTanh = ss::saturating_chain();
constexpr loops::Program kInSeries = ss::in_series();
constexpr loops::Program kParam3 = ss::param_cascade(3, ss::Coefficients::Fed);
constexpr loops::Program kParam3G1 = ss::param_cascade(3, ss::Coefficients::Gathered);
constexpr loops::Program kParam8 = ss::param_cascade(8, ss::Coefficients::Fed);
constexpr loops::Program kParam8G1 = ss::param_cascade(8, ss::Coefficients::Gathered);

// The skewed stage reads the five [k, 1] coefficients gathered from copies of their k sections, taken when it is
// placed (`loops/walk.hpp` `still_slots`), each in whole vectors of four floats aligned as one; feeds where they are.
// The moving runs (`check_moving`) need each copy taken anew every run.
static_assert(loops::kStillOf<kParam3G1, 1>.size() == 5 && loops::kStillOf<kParam8G1, 1>.size() == 5);
static_assert(loops::kStillOf<kParam3, 0>.empty() && loops::kStillOf<kParam8, 0>.empty());
static_assert(sizeof(loops::StillHeld<kParam3G1, 1>) == 5 * 16 && alignof(loops::StillHeld<kParam3G1, 1>) == 16);
static_assert(sizeof(loops::StillHeld<kParam8G1, 1>) == 5 * 32 && alignof(loops::StillHeld<kParam8G1, 1>) == 16);

// (analysed at run time: only the programs must be constants)
const Graph kTptGraph = ls::tpt_pair(ls::Form::Linked);
const Graph kLr8Graph = ls::lr8(ls::Form::Linked);
const Graph kPeaksGraph = ls::peaks(ls::Form::Linked);
const Graph kTanhGraph = ls::saturating_chain(ls::Form::Linked);
const Graph kInSeriesGraph = ls::in_series(ls::Form::Linked);
const Graph kParam3Graph = param_graph(3);
const Graph kParam8Graph = param_graph(8);

[[gnu::noinline]] void check_b2() {
    const std::vector<double> x = noise();
    loops::Feeds only_x{};
    check_runs<kTpt, double, double>("the TPT pair", {1, 2}, x, walked(kTptGraph, {x}), only_x);
    check_runs<kLr8, double, double>("LR8", {3, 4}, x, walked(kLr8Graph, {x}), only_x);
    check_runs<kTanh, double, double>("eight tanh sections", {7, 8}, x, walked(kTanhGraph, {x}), only_x);
    check_runs<kInSeries, double, double>("the pair into the eight", {1, 2, 7, 8}, x, walked(kInSeriesGraph, {x}), only_x);
    std::array<std::array<double, 5>, 5> columns{};
    std::vector<std::vector<double>> fed{x};
    loops::Feeds peaks{};
    for (std::size_t i = 0; i < columns.size(); ++i) {
        for (std::size_t j = 0; j < columns[i].size(); ++j) {
            columns[i][j] = ls::kPeaks[j][i];
        }
        fed.push_back(every_sample(columns[i]));
        peaks[1 + i] = columns[i].data();
    }
    check_runs<kPeaks, double, double>("five peaks", {4, 5}, x, walked(kPeaksGraph, fed), peaks);
}

template <const loops::Program& Fed, const loops::Program& Gathered>
[[gnu::noinline]] void check_params(const char* fed_what, const char* gathered_what, std::int32_t k, const Graph& g) {
    const std::vector<double> x = noise();
    const std::vector<double> want = walked(g, fed_in_blocks(x, k, static_cast<std::int64_t>(kSamples), false));
    Knobs knobs(k);
    knobs.set(numbers_at(k, 0, false));
    check_runs<Fed, float, float>(fed_what, {k - 1, k}, x, want, knobs.feeds(ss::Coefficients::Fed));
    check_runs<Gathered, float, float>(gathered_what, {k - 1, k}, x, want, knobs.feeds(ss::Coefficients::Gathered));
}

// A coefficient changed between runs: every section's numbers swept, set before each block.
template <const loops::Program& Fed, const loops::Program& Gathered>
[[gnu::noinline]] void check_moving(const char* what, std::int32_t k, const Graph& g) {
    const std::vector<double> x = noise();
    for (const std::int64_t block : {1, 32, 256}) {
        const std::vector<double> want = walked(g, fed_in_blocks(x, k, block, true));
        Knobs knobs(k);
        const auto before = [&](std::size_t at) { knobs.set(numbers_at(k, at, true)); };
        const std::string named = std::string(what) + ", numbers moving in blocks of " + std::to_string(block);
        const std::vector<double> fed = ran<Fed, float, float>(x, {block}, knobs.feeds(ss::Coefficients::Fed), kSamples, before);
        check(same_bits(named.c_str(), fed, want), (named + ", fed").c_str());
        const std::vector<double> gathered = ran<Gathered, float, float>(x, {block}, knobs.feeds(ss::Coefficients::Gathered), kSamples, before);
        check(same_bits(named.c_str(), gathered, want), (named + ", gathered").c_str());
    }
}

// ---- the listing ---------------------------------------------------------------------------------------------------

constexpr std::string_view kParam3Listing = R"(b0 f32 [d0] feed 0 value 0
b1 f32 [3 1] feed 1 value 1
b2 f32 [3 1] feed 2 value 2
b3 f32 [3 1] feed 3 value 3
b4 f32 [3 1] feed 4 value 4
b5 f32 [3 1] feed 5 value 5
b6 f32 [d0] value 6
b7 f64 [3 d0] value 7 prefix 2 only-past
b8 f64 [3 d0] value 8 prefix 2 only-past
stage 0 [3 d0] time n9 skewed
  s0 load f32 b0(- 0)
  s1 load f64 b7(0 -) back 1
  s2 load f64 b7(0 -) back 2
  s3 map f64 _chain normal s0 b8(0 -)@0-1 -> b7(0 -)
  s4 load f64 b8(0 -) back 1
  s5 load f64 b8(0 -) back 2
  s6 load f32 b3(0 -)
  s7 map f64 _mul normal s6 s2
  s8 load f32 b5(0 -)
  s9 map f64 _mul normal s8 s5
  s10 map f64 _add normal s7 s9
  s11 load f32 b2(0 -)
  s12 map f64 _mul normal s11 s1
  s13 map f64 _add normal s10 s12
  s14 load f32 b4(0 -)
  s15 map f64 _mul normal s14 s4
  s16 map f64 _add normal s13 s15
  s17 load f32 b1(0 -)
  s18 map f64 _madd normal s17 s3 s16 -> b8(0 -)
  s19 load f32 s18
  s20 load f32 s19 -> b6(- 0)
outputs b6
n0 stage 0
  n1 loop a0 x4 masked strip 0
    n2 registers state s3 link n10 slots 2
      n3 registers state s18 link n10 slots 2
        n4 vector a0 x4
          n5 open s1 keeps s3
          n6 open s2 keeps s3 slot 1
          n7 open s4 keeps s18
          n8 open s5 keeps s18 slot 1
        n9 loop a1 x1
          n10 vector a0 x4
            n11 compute s0 lane 0
            n12 compute s1 reads s3
            n13 compute s2 reads s3 slot 1
            n14 compute s3 reads s18 shifted keeps s3 closes
            n15 compute s4 reads s18
            n16 compute s5 reads s18 slot 1
            n17 compute s6
            n18 compute s7
            n19 compute s8
            n20 compute s9
            n21 compute s10
            n22 compute s11
            n23 compute s12
            n24 compute s13
            n25 compute s14
            n26 compute s15
            n27 compute s16
            n28 compute s17
            n29 compute s18 keeps s18 closes
            n30 compute s19
            n31 compute s20 lane 2
        n32 vector a0 x4
          n33 close s1 keeps s3
          n34 close s2 keeps s3 slot 1
          n35 close s4 keeps s18
          n36 close s5 keeps s18 slot 1
)";

void check_listing() {
    const std::string got = loops::print(kParam3);
    if (got != kParam3Listing) {
        std::printf("the Param cascade of three, fed, printed as:\n%s\n", got.c_str());
    }
    check(got == kParam3Listing, "the Param cascade of three prints as pinned");
}

} // namespace

int main() {
    check_listing();
    check_b2();
    check_params<kParam3, kParam3G1>("three Param sections, fed", "three Param sections, gathered", 3, kParam3Graph);
    check_params<kParam8, kParam8G1>("eight Param sections, fed", "eight Param sections, gathered", 8, kParam8Graph);
    check_moving<kParam3, kParam3G1>("three Param sections", 3, kParam3Graph);
    check_moving<kParam8, kParam8G1>("eight Param sections", 8, kParam8Graph);
    if (g_wrong != 0) {
        std::printf("%d checks failed\n", g_wrong);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
