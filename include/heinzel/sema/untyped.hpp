#pragma once

// The values made of numbers alone (Plan/28-dsl-proposals/03 B2, Decision 76): what the sweep could not type, since
// nothing in it carries a type, and what then takes its type where it is read (`type_the_numbers`, `analyze.hpp`).

#include <cstddef>
#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"

namespace heinzel {

namespace detail {

// The operands an element-type rule reads, `first` up to `end`: all of them, all but the condition, or the first; none
// where the node states its type (a conversion, a generator).
struct TypedOperands {
    std::uint8_t first{0};
    std::uint8_t end{0};
};

constexpr TypedOperands typed_operands(const OpRules& rules) {
    switch (rules.type) {
    case TypeRule::FromOperands:
        return {0, rules.arity};
    case TypeRule::FromOperandsExceptFirst:
        return {1, rules.arity};
    case TypeRule::SameAsFirst:
        return {0, 1};
    case TypeRule::FromAttribute:
    case TypeRule::Declared:
    case TypeRule::Adapts:
    case TypeRule::Unclassified:
        break;
    }
    return {};
}

// Once the sweep has typed what it can: a literal no reader typed, a node that adapts as one (`_first`), or a node still
// untyped whose type-carrying operands are all such (`-0.5`, `where(c, 1.0, 2.0)`: the condition carries no type).
// Never a Variable: its type is its definition's, and a node that reads one is not made of numbers. Marked in node
// order, as an operand is made before its reader; returns whether any value is.
using Untyped = bool[kMaxValues];

constexpr bool mark_untyped(const Graph& g, Untyped& untyped) {
    bool any = false;
    for (std::size_t v = 0; v < g.values.size(); ++v) {
        untyped[v] = g.values[v].category == ValueCategory::Const && g.values[v].type == ElemType::Unknown;
        any = any || untyped[v];
    }
    for (const Node& n : g.nodes) {
        const TypedOperands read = typed_operands(rules_of(n.op));
        const auto out = static_cast<std::size_t>(n.output);
        bool numbers = (read.first < read.end || rules_of(n.op).type == TypeRule::Adapts) && g.values[out].type == ElemType::Unknown;
        for (std::uint8_t k = read.first; k < read.end; ++k) {
            numbers = numbers && untyped[static_cast<std::size_t>(n.inputs[k])];
        }
        untyped[out] = numbers;
        any = any || numbers;
    }
    return any;
}

} // namespace detail

} // namespace heinzel
