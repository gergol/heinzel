// Two graphs compared node for node, but for the numbers of their values (sections in lanes and the rules after it):
// a rewrite against the graph built by hand that it should make.
#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/graph/facts.hpp"

namespace heinzel::testing {

// `g`'s nodes, its generators first, in order otherwise: where a constant tensor stands before its readers changes
// nothing.
inline std::vector<Node> generators_first(const Graph& g) {
    std::vector<Node> nodes(g.nodes.begin(), g.nodes.end());
    std::stable_partition(nodes.begin(), nodes.end(), [](const Node& n) { return rules_of(n.op).arity == 0; });
    return nodes;
}

// The values of two graphs paired as they are met: each with one other only, the two laid out alike. A read is of what
// it finds (`Facts::read_as`): a variable and its definition are one number.
struct Pairs {
    const Graph& a;
    const Graph& b;
    std::vector<std::int32_t> to;
    std::vector<std::int32_t> from;
    std::vector<std::int32_t> read_a;
    std::vector<std::int32_t> read_b;

    bool read(std::int32_t x, std::int32_t y) {
        const bool none = x < 0 || y < 0;
        return none ? x == y : met(read_a[static_cast<std::size_t>(x)], read_b[static_cast<std::size_t>(y)]);
    }

    bool met(std::int32_t x, std::int32_t y) {
        if (x < 0 || y < 0) {
            return x == y;
        }
        std::int32_t& there = to[static_cast<std::size_t>(x)];
        std::int32_t& back = from[static_cast<std::size_t>(y)];
        if (there >= 0 || back >= 0) {
            return there == y && back == x;
        }
        there = y;
        back = x;
        const Value& u = a.values[static_cast<std::size_t>(x)];
        const Value& v = b.values[static_cast<std::size_t>(y)];
        return u.type == v.type && u.shape == v.shape && u.category == v.category && u.precision == v.precision && u.timeless == v.timeless &&
               std::bit_cast<std::uint64_t>(u.constant) == std::bit_cast<std::uint64_t>(v.constant);
    }
};

// One row and attribute, a constant tensor's numbers one, the values met paired. A view on another view row than
// written passes: every view row is one row of the table but for its name.
inline bool same_node(Pairs& p, const Node& x, const Node& y) {
    const OpRules& rows = rules_of(x.op);
    const bool views = rows.attr == AttrKind::View && rows == rules_of(y.op);
    OpAttr ax = x.attr;
    OpAttr ay = y.attr;
    ax.recurrence.defines = kNoValue;
    ay.recurrence.defines = kNoValue;
    ax.generator.first = -1;
    ay.generator.first = -1;
    bool same = (x.op == y.op || views) && ax == ay;
    for (std::int32_t e = 0; same && rows.attr == AttrKind::Constant && e < numbers_per_sample(x.attr.generator.shape); ++e) {
        same = p.a.constants[static_cast<std::size_t>(x.attr.generator.first + e)] == p.b.constants[static_cast<std::size_t>(y.attr.generator.first + e)];
    }
    for (std::size_t k = 0; k < kMaxNodeInputs && same; ++k) {
        same = p.read(x.inputs[k], y.inputs[k]);
    }
    same = same && p.met(x.output, y.output);
    return same && (x.attr.kind != AttrKind::Recurrence || p.met(x.attr.recurrence.defines, y.attr.recurrence.defines));
}

// Are `a` and `b` one graph but for the numbers of their values, where their generators stand and whether a read finds
// a variable or its definition? Node for node, the inputs paired by name, the outputs in order.
inline bool same_graph(const Graph& a, const Graph& b) {
    Pairs p{a, b, std::vector<std::int32_t>(a.values.size(), -1), std::vector<std::int32_t>(b.values.size(), -1), loops::graph::facts_of(a).read_as,
            loops::graph::facts_of(b).read_as};
    bool same = a.nodes.size() == b.nodes.size() && a.inputs.size() == b.inputs.size() && a.outputs.size() == b.outputs.size();
    for (std::size_t i = 0; i < a.inputs.size() && same; ++i) {
        const auto named = [&](const NamedValue& io) { return io.name == a.inputs[i].name; };
        const auto there = std::find_if(b.inputs.begin(), b.inputs.end(), named);
        same = there != b.inputs.end() && p.met(a.inputs[i].value, there->value);
    }
    const std::vector<Node> na = generators_first(a);
    const std::vector<Node> nb = generators_first(b);
    for (std::size_t i = 0; i < na.size() && same; ++i) {
        same = same_node(p, na[i], nb[i]);
    }
    for (std::size_t o = 0; o < a.outputs.size() && same; ++o) {
        same = a.outputs[o].name == b.outputs[o].name && p.read(a.outputs[o].value, b.outputs[o].value);
    }
    return same;
}

} // namespace heinzel::testing
