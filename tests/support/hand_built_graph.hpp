#pragma once

// Building a graph by hand, for tests that need to put a rule under pressure before the language
// has a spelling for it.
//
// Shared rather than copied into each test file: two copies of "how a test adds a node" drift, and
// then two tests that look like they are testing the same thing are not. One spelling everywhere,
// test graphs included.

#include "heinzel/ir/ir.hpp"

namespace heinzel {

constexpr std::int32_t add_value(Graph& g, ElemType t, Shape s, ValueCategory c = ValueCategory::Internal) {
    g.values.push_back(Value{t, s, c, 0.0});
    return static_cast<std::int32_t>(g.values.size() - 1);
}

constexpr std::int32_t add_node(Graph& g, OpCode op, std::int32_t a, std::int32_t b = kNoValue,
                                std::int32_t c = kNoValue, OpAttr attr = OpAttr{}) {
    const std::int32_t out = add_value(g, ElemType::Unknown, Shape{});
    Node n{};
    n.op = op;
    n.inputs[0] = a;
    n.inputs[1] = b;
    n.inputs[2] = c;
    n.output = out;
    n.attr = attr;
    g.nodes.push_back(n);
    return out;
}

// A range for value `v`, as a declaration (a port) or a `promise` (a computed value) records it.
constexpr void add_range(Graph& g, std::int32_t v, Range r, bool promised = false) {
    g.ranges.push_back(RangeMark{v, r, promised});
}

} // namespace heinzel
