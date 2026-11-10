// Equal values computed once (`rewrite/equal_values.hpp`, Plan/33-knobs-that-move S1): two nodes of one operation over
// the same values, of one attribute, type, shape and precision, are one; two literals are one where their bits agree.
//
//   combs      eight combs, each `o * (1 - damp)`, damp a signal  -> one `1 - damp`: seven nodes, their seven 1s gone
//   zeros      x * 0 + x * 0,  x * 0 + x * -0                     -> one product; apart: -0's bits are not 0's
//   NaNs       x * NaN + x * NaN                                  -> one where the payloads agree, apart where not
//   own        two `_realize`s of one value, two variables alike  -> apart: the sharing column's `Own`
//   literals   0.5 a[n-1] and 0.5 b[n-1]                          -> each its own 0.5: a literal is merged with its node
//   marks      a + b, a + b, (a + b) promised                     -> the two merged, the promised apart, its mark kept
//   outputs    y = a*b, z = a*b                                   -> apart: an output is merged into no other value
//   precision  y = (a*b) at High, z = a*b + 1                     -> apart
//   compile    `rewritten` and `try_compile` merge; a second pass merges nothing
//
// And the oracle: every graph of the both-ways corpus (every operation), doubled (each node copied over copies of its
// literals and variables, the two outputs added) and merged, walks to the bits of the doubled graph as written.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/rewrite/equal_values.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/sema/analyze.hpp"

#include "both_ways.hpp"
#include "filters.hpp"
#include "hand_built_graph.hpp"
#include "interpret.hpp"

using namespace heinzel;
using namespace heinzel::testing;

namespace {

constexpr std::int32_t kSamples = 64;
constexpr Index n{};

constexpr Graph analysed(Graph g) {
    if (!infer(g).ok) {
        fail("rewrite_equal_values_test: a graph did not resolve");
    }
    return g;
}
constexpr Graph merged(Graph g) {
    rewrite::compute_equal_values_once(g);
    return g;
}
constexpr std::int32_t count(const Graph& g, OpCode op) {
    std::int32_t k = 0;
    for (const Node& node : g.nodes) {
        k += node.op == op ? 1 : 0;
    }
    return k;
}
constexpr std::size_t dropped_nodes(const Graph& g) { return g.nodes.size() - merged(g).nodes.size(); }
constexpr std::size_t dropped_values(const Graph& g) { return g.values.size() - merged(g).values.size(); }

// Freeverb's comb with its damping a signal (`bench_compare_cmajor_heinzel.cpp`).
constexpr Expr comb(Expr in, std::int32_t size, Expr damp) {
    Variable v;
    Variable lp;
    const Expr o = v[n - size];
    lp[n] = o * (1.0 - damp) + lp[n - 1] * damp;
    v[n] = in + lp * 0.84;
    return o;
}
constexpr Graph combs() {
    Input x{"x", Signal{kSamples}};
    Input knob{"knob", Signal{kSamples}};
    const Expr damp = knob * 0.4;
    Expr acc = comb(x, 10, damp);
    for (std::int32_t k = 1; k < 8; ++k) {
        acc = acc + comb(x, 10 + k, damp);
    }
    Output y{"y"};
    y = acc;
    return analysed(finish(y));
}
static_assert(count(combs(), op::Sub) == 8 && count(merged(combs()), op::Sub) == 1, "eight combs: one `1 - damp`");
static_assert(dropped_nodes(combs()) == 7 && dropped_values(combs()) == 7 + 7, "seven subtractions gone, and their 1s");
static_assert(merged(merged(combs())) == merged(combs()), "a fixed point");
static_assert(count(rewrite::rewritten(combs(), zen2), op::Sub) == 1 && count(try_compile(combs()).compiled.graph, op::Sub) == 1,
              "the last rule of `rewritten`, so compiling merges");

constexpr Graph zeros(bool negative) {
    Input x{"x", Signal{kSamples}};
    Output y{"y"};
    y = x * 0.0 + x * (negative ? -0.0 : 0.0);
    return analysed(finish(y));
}
static_assert(dropped_nodes(zeros(false)) == 1 && dropped_values(zeros(false)) == 2, "x * 0 twice: one product, one 0");
static_assert(merged(zeros(true)) == zeros(true), "0 and -0 are two numbers");

constexpr std::uint64_t kQuietNaN = 0x7ff8000000000001ULL;
constexpr Graph nans(std::uint64_t second) {
    Graph g{};
    const std::int32_t x = add_value(g, ElemType::F64, Shape{kSamples}, ValueCategory::Input);
    const std::int32_t a = add_value(g, ElemType::F64, Shape{kSamples}, ValueCategory::Const);
    const std::int32_t b = add_value(g, ElemType::F64, Shape{kSamples}, ValueCategory::Const);
    g.values[static_cast<std::size_t>(a)].constant = std::bit_cast<double>(kQuietNaN);
    g.values[static_cast<std::size_t>(b)].constant = std::bit_cast<double>(second);
    const std::int32_t first = add_node(g, op::Mul, x, a);
    const std::int32_t other = add_node(g, op::Mul, x, b);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    g.outputs.push_back(NamedValue{Name{"y"}, add_node(g, op::Add, first, other)});
    return analysed(g);
}
static_assert(dropped_nodes(nans(kQuietNaN)) == 1 && dropped_values(nans(kQuietNaN)) == 2, "a NaN is itself");
static_assert(dropped_nodes(nans(kQuietNaN + 1)) == 0 && dropped_values(nans(kQuietNaN + 1)) == 0,
              "two payloads are two numbers");

constexpr Graph realized() {
    Input x{"x", Signal{kSamples}};
    Output y{"y"};
    y = (x * 2.0).realize() + (x * 2.0).realize();
    return analysed(finish(y));
}
static_assert(dropped_nodes(realized()) == 1 && count(merged(realized()), op::Realize) == 2,
              "the products merged, each materialisation its own");
constexpr Graph twins() {
    Input x{"x", Signal{kSamples}};
    Variable a;
    Variable b;
    a[n] = x + 0.5 * a[n - 1];
    b[n] = x + 0.5 * b[n - 1];
    Output y{"y"};
    y = a + b;
    return analysed(finish(y));
}
static_assert(merged(twins()) == twins(), "two variables defined alike: apart, each lag its 0.5");

constexpr Graph promised() {
    Input a{"a", Signal{kSamples}};
    Input b{"b", Signal{kSamples}};
    const Expr p = a + b;
    const Expr q = a + b;
    const Expr r = (a + b).promise(ge(-4.0));
    Output y{"y"};
    y = p * q * r;
    return analysed(finish(y));
}
// The promise's mark, after the merge, on the value of an `_add` read by the last product alone.
constexpr bool mark_kept(const Graph& g) {
    const std::int32_t marked = g.ranges[g.ranges.size() - 1].value;
    std::int32_t readers = 0;
    bool made_by_an_add = false;
    for (const Node& node : g.nodes) {
        made_by_an_add = made_by_an_add || (node.output == marked && node.op == op::Add);
        readers += node.inputs[0] == marked || node.inputs[1] == marked ? 1 : 0;
    }
    return g.ranges[g.ranges.size() - 1].promised && made_by_an_add && readers == 1;
}
static_assert(dropped_nodes(promised()) == 1 && count(merged(promised()), op::Add) == 2 && mark_kept(merged(promised())) &&
                  merged(promised()).ranges.size() == promised().ranges.size(),
              "a promised value stays apart, its mark on it");

constexpr Graph outputs(bool high) {
    Input a{"a", Signal{kSamples}};
    Input b{"b", Signal{kSamples}};
    Output y{"y"};
    Output z{"z"};
    y = high ? (a * b).precision(numerics::Precision::High) : a * b;
    z = high ? a * b + 1.0 : a * b;
    return analysed(finish(y, z));
}
static_assert(merged(outputs(false)) == outputs(false), "two outputs: no output merged into another");
static_assert(merged(outputs(true)) == outputs(true), "a product at High and one at Normal: apart");

// ---- the oracle: the corpus doubled, walked as written and merged ---------------------------------

// `g` written twice: each value but an input copied, each node copied over the copies, the two outputs added.
constexpr Graph doubled(Graph g) {
    const std::size_t values = g.values.size();
    const std::size_t nodes = g.nodes.size();
    std::vector<std::int32_t> copy(values);
    for (std::size_t v = 0; v < values; ++v) {
        const Value value = g.values[v];
        copy[v] = value.category == ValueCategory::Input ? static_cast<std::int32_t>(v) : push_value(g, value);
    }
    const auto copied = [&](std::int32_t v) { return v < 0 ? v : copy[static_cast<std::size_t>(v)]; };
    for (std::size_t i = 0; i < nodes; ++i) {
        Node c = g.nodes[i];
        for (std::int32_t& in : c.inputs) {
            in = copied(in);
        }
        c.output = copied(c.output);
        c.attr.recurrence.defines = copied(c.attr.recurrence.defines);
        push_node(g, c);
    }
    const std::int32_t out = g.outputs[0].value;
    Node sum{.op = op::Add, .output = push_value(g, g.values[static_cast<std::size_t>(out)])};
    sum.inputs[0] = out;
    sum.inputs[1] = copied(out);
    push_node(g, sum);
    g.outputs[0].value = sum.output;
    return g;
}

template <std::size_t I>
constexpr Graph doubled_at_high() {
    Graph g = doubled(corpus_graph(I));
    set_default_precision(g, numerics::Precision::High);
    return analysed(g);
}

// The walk's numbers of the one output, as bits.
std::vector<std::uint64_t> walked(const Graph& g) {
    constexpr auto kBlockSamples = static_cast<std::size_t>(kBlock);
    const auto signals = std::make_unique<Signals<kBlockSamples>>();
    run(g, impulse<kBlockSamples>(), *signals);
    const std::int32_t out = g.outputs[0].value;
    const auto width = static_cast<std::size_t>(elements_of(g.values[static_cast<std::size_t>(out)].shape).count);
    std::vector<std::uint64_t> got;
    for (std::size_t s = 0; s < kBlockSamples; ++s) {
        for (std::size_t e = 0; e < width; ++e) {
            got.push_back(std::bit_cast<std::uint64_t>(sample_at(*signals, out, static_cast<std::int32_t>(e), s)));
        }
    }
    return got;
}

template <std::size_t I>
bool entry_holds() {
    static constexpr Graph kWritten = doubled_at_high<I>();
    static constexpr Graph kMerged = merged(kWritten);
    std::printf("rewrite_equal_values: entry %zu, %zu nodes, %zu merged\n", I, kWritten.nodes.size(),
                kWritten.nodes.size() - kMerged.nodes.size());
    if (kMerged.nodes.size() >= kWritten.nodes.size()) {
        std::printf("rewrite_equal_values: entry %zu merged nothing\n", I);
        return false;
    }
    if (!is_runnable(kWritten)) {
        return true;
    }
    if (walked(kMerged) != walked(kWritten)) {
        std::printf("rewrite_equal_values: entry %zu merged walks to other numbers\n", I);
        return false;
    }
    return true;
}

template <std::size_t... I>
bool every_entry_holds(std::index_sequence<I...>) {
    return (entry_holds<I>() & ...);
}

} // namespace

int main() {
    return every_entry_holds(std::make_index_sequence<kCorpusSize>{}) ? 0 : 1;
}
