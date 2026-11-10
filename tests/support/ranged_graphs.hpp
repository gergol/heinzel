#pragma once

// Graphs for the proof's tests (Plan/26-value-ranges/03): ranges written piece by piece, and one operation over ports
// of given ranges, analysed. Shared by `unit/ranges_rules_test.cpp` and `unit/ranges_walk_test.cpp`.

#include <cstddef>
#include <cstdint>
#include <limits>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ranges/proven.hpp"
#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"

namespace heinzel::testing {

inline constexpr double kInfinity = std::numeric_limits<double>::infinity();

constexpr Interval closed(double lo, double hi) { return Interval{lo, hi, false, false}; }
constexpr Interval open(double lo, double hi) { return Interval{lo, hi, true, true}; }
constexpr Interval open_closed(double lo, double hi) { return Interval{lo, hi, true, false}; } // (lo, hi]
constexpr Interval closed_open(double lo, double hi) { return Interval{lo, hi, false, true}; } // [lo, hi)

template <class... I>
constexpr Range made_of(I... piece) {
    Range r{};
    std::size_t k = 0;
    ((r.pieces[k++] = piece), ...);
    r.count = static_cast<std::int8_t>(k);
    return r;
}

constexpr Graph analysed(Graph g) {
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail(d.message.view());
    }
    return g;
}

// A port of an operation: its range and its shape.
struct Port {
    Range range{};
    Shape shape{8};
};

// `op` over ports a, b, c, in double: the graph analysed, its result the output.
constexpr Graph over_ports(OpCode op, Port a, Port b = Port{}, Port c = Port{}, OpAttr attr = OpAttr{}) {
    Graph g{};
    const Port ports[3] = {a, b, c};
    const Name names[3] = {Name{"a"}, Name{"b"}, Name{"c"}};
    std::int32_t in[3] = {kNoValue, kNoValue, kNoValue};
    for (std::uint8_t k = 0; k < rules_of(op).arity; ++k) {
        in[k] = add_value(g, ElemType::F64, ports[k].shape, ValueCategory::Input);
        g.inputs.push_back(NamedValue{names[k], in[k]});
        add_range(g, in[k], ports[k].range);
    }
    const std::int32_t out = add_node(g, op, in[0], in[1], in[2], attr);
    g.outputs.push_back(NamedValue{Name{"y"}, out});
    return analysed(g);
}

// The same, every port of one shape.
constexpr Graph one_operation(OpCode op, Range a, Range b = Range{}, Range c = Range{}, OpAttr attr = OpAttr{},
                              Shape shape = Shape{8}) {
    return over_ports(op, Port{a, shape}, Port{b, shape}, Port{c, shape}, attr);
}

constexpr Range result_of(const Graph& g) { return ranges::proof(g).values[static_cast<std::size_t>(g.outputs[0].value)]; }

constexpr Range result_of(OpCode op, Range a, Range b = Range{}, Range c = Range{}, OpAttr attr = OpAttr{}) {
    return result_of(one_operation(op, a, b, c, attr));
}

constexpr std::size_t findings_of(const Graph& g) { return ranges::proof(g).findings.size(); }

constexpr std::size_t findings_of(OpCode op, Range a, Range b = Range{}, Range c = Range{}) {
    return findings_of(one_operation(op, a, b, c));
}

// The first finding, worded: what `ranges::refusal` says.
constexpr fixed_string<kMaxMessageLength> refusal_of(const Graph& g) { return ranges::refusal(g).message; }

} // namespace heinzel::testing
