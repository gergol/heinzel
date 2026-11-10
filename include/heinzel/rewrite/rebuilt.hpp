#pragma once

// A rewrite rule's result: the graph it read with the nodes and values it made, the values nothing refers to any more
// dropped. Each rule builds its nodes and values apart and hands them here (`feedback_sums.hpp`, `parallel_form.hpp`,
// `sections_in_lanes.hpp`, `double_state.hpp`, `product_adds.hpp`).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"

namespace heinzel::rewrite::detail {

// `g` with `nodes` and `values`, the values nothing refers to any more dropped and the rest numbered densely, in their
// order; none where the result would outgrow a graph's capacities.
constexpr std::optional<Graph> with(const Graph& g, const std::vector<Node>& nodes, const std::vector<Value>& values) {
    std::vector<std::int32_t> number(values.size(), -1);
    const auto keep = [&](std::int32_t v) { number[static_cast<std::size_t>(v)] = 0; };
    for (const Node& n : nodes) {
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            keep(n.inputs[k]);
        }
        keep(n.output);
        if (n.attr.kind == AttrKind::Recurrence) {
            keep(n.attr.recurrence.defines);
        }
    }
    for (const NamedValue& io : g.inputs) {
        keep(io.value);
    }
    for (const NamedValue& io : g.outputs) {
        keep(io.value);
    }
    std::int32_t count = 0;
    for (std::int32_t& v : number) {
        v = v < 0 ? -1 : count++;
    }
    if (static_cast<std::size_t>(count) > kMaxValues || nodes.size() > kMaxNodes) {
        return std::nullopt;
    }
    Graph out = g;
    out.values.clear();
    out.nodes.clear();
    for (std::size_t v = 0; v < values.size(); ++v) {
        if (number[v] >= 0) {
            out.values.push_back(values[v]);
        }
    }
    const auto renumbered = [&](std::int32_t v) { return number[static_cast<std::size_t>(v)]; };
    for (Node n : nodes) {
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            n.inputs[k] = renumbered(n.inputs[k]);
        }
        n.output = renumbered(n.output);
        n.attr.recurrence.defines = n.attr.kind == AttrKind::Recurrence ? renumbered(n.attr.recurrence.defines) : n.attr.recurrence.defines;
        out.nodes.push_back(n);
    }
    for (NamedValue& io : out.inputs) {
        io.value = renumbered(io.value);
    }
    for (NamedValue& io : out.outputs) {
        io.value = renumbered(io.value);
    }
    return out;
}

} // namespace heinzel::rewrite::detail
