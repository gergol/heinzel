#pragma once

// A CANDIDATE, MEASURED BUT NOT ADOPTED. This is not part of the library and nothing in
// `include/` refers to it. It exists so that `plan/TRACEABILITY.md` Q-2 has a number instead of an
// estimate — the operator decided on 2026-08-25 not to change the mechanism, and to measure what
// changing it would have cost, for later reference.
//
// WHAT IT WOULD FIX. `finish()` copies the arena in emission order, so a value's identity is the
// order it was created in — which is the order the compiler EVALUATED the parts of an expression,
// not the order they were written. C++ does not fix that within one expression, and gcc 15.2 does
// not choose the same way while compiling as it does at run time. This walks the graph instead:
// depth-first from the outputs, operands before uses, operand slots in written order. That order
// comes from the SYNTAX — who is whose operand — which evaluation order cannot touch.
//
// The walk is ITERATIVE, deliberately. Spike D's recursive flattener hit the default
// `-fconstexpr-depth` of 512 at roughly eighty chained operations, which is inside the real corpus
// (`../workspace/plans/heinzel-dsl-study/OPTION-3-refcounted-dag.md` §3.1). Measuring a recursive
// one would measure a design that could not ship.
//
// KNOWN BEHAVIOUR CHANGE, and it is the reason this is a decision and not a tidy-up: a walk from the
// outputs reaches only what an output depends on, so any node that nothing needs is dropped. That is
// dead-node elimination, arriving as a side effect of renumbering rather than as a pass someone
// chose. Whether it is wanted is a separate question from whether the order should be canonical.

#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fail.hpp"

#include <cstddef>
#include <cstdint>

namespace heinzel::measurement {

namespace detail {

// Which node produces each value, and `kNoValue` for the ones nothing produces: inputs, literals,
// and a variable, whose whole point is that it exists before its definition does. Those are the
// sources the walk stops at, and they are also why it terminates — a recurrence's back edge reads a
// variable, so the value graph is acyclic even though the computation is not.
constexpr void producers_of(const Graph& src, fixed_vector<std::int32_t, kMaxValues>& producer) {
    for (std::size_t i = 0; i < src.values.size(); ++i) {
        producer.push_back(kNoValue);
    }
    for (std::size_t n = 0; n < src.nodes.size(); ++n) {
        const std::int32_t produced = src.nodes[n].output;
        if (produced != kNoValue) {
            producer[static_cast<std::size_t>(produced)] = static_cast<std::int32_t>(n);
        }
    }
}

// One frame per value being visited, holding which operand slot comes next. Post-order: a value is
// given its new identity only once every operand it reads already has one.
struct Frame {
    std::int32_t value{kNoValue};
    std::size_t next_slot{0};
};

// A node rewritten into the new numbering. Every operand must already have been placed — that is
// what post-order buys, and it is checked rather than assumed because the failure would otherwise be
// a graph that is quietly wrong rather than one that refuses to compile.
constexpr Node renumbered_node(const Node& was, const fixed_vector<std::int32_t, kMaxValues>& renamed,
                               std::int32_t new_output) {
    Node n{};
    n.op = was.op;
    for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
        const std::int32_t operand = was.inputs[k];
        if (operand == kNoValue) {
            n.inputs[k] = kNoValue;
            continue;
        }
        const std::int32_t moved = renamed[static_cast<std::size_t>(operand)];
        if (moved == kNoValue) {
            fail("canonical_renumber: an operand was used before it was placed");
        }
        n.inputs[k] = moved;
    }
    n.output = new_output;
    n.attr = was.attr;
    if (n.attr.kind == AttrKind::Recurrence && n.attr.recurrence.defines != kNoValue) {
        const std::int32_t defines = renamed[static_cast<std::size_t>(n.attr.recurrence.defines)];
        if (defines == kNoValue) {
            fail("canonical_renumber: a recurrence names a variable the walk never reached");
        }
        n.attr.recurrence.defines = defines;
    }
    return n;
}

// Depth-first from one output, operands before uses, operand slots in written order. The walk is
// ITERATIVE, deliberately — see the note at the top of this file about `-fconstexpr-depth`.
constexpr void walk_from(const Graph& src, std::int32_t root, const fixed_vector<std::int32_t, kMaxValues>& producer,
                         fixed_vector<std::int32_t, kMaxValues>& renamed, fixed_vector<bool, kMaxValues>& queued,
                         Graph& out) {
    fixed_vector<Frame, kMaxValues> stack{};
    const auto seed = static_cast<std::size_t>(root);
    if (queued[seed]) {
        return;
    }
    queued[seed] = true;
    stack.push_back(Frame{root, 0});

    while (!stack.empty()) {
        Frame& top = stack[stack.size() - 1];
        const auto here = static_cast<std::size_t>(top.value);
        const std::int32_t by = producer[here];

        if (by != kNoValue && top.next_slot < kMaxNodeInputs) {
            const std::int32_t operand = src.nodes[static_cast<std::size_t>(by)].inputs[top.next_slot];
            ++top.next_slot;
            if (operand != kNoValue && !queued[static_cast<std::size_t>(operand)]) {
                queued[static_cast<std::size_t>(operand)] = true;
                stack.push_back(Frame{operand, 0});
            }
            continue;
        }

        // Every operand is settled, so this value can take the next identity in the new order.
        renamed[here] = static_cast<std::int32_t>(out.values.size());
        out.values.push_back(src.values[here]);
        if (by != kNoValue) {
            out.nodes.push_back(renumbered_node(src.nodes[static_cast<std::size_t>(by)], renamed, renamed[here]));
        }
        stack.pop_back();
    }
}

// The named edges follow the values they name. Input order becomes discovery order, which is as
// canonical as everything else here — and is a second thing this pass would change.
constexpr void rename_named_edges(const Graph& src, const fixed_vector<std::int32_t, kMaxValues>& renamed,
                                  Graph& out) {
    for (const NamedValue& was : src.inputs) {
        const std::int32_t moved = renamed[static_cast<std::size_t>(was.value)];
        if (moved != kNoValue) {
            out.inputs.push_back(NamedValue{was.name, moved});
        }
    }
    for (const NamedValue& was : src.outputs) {
        out.outputs.push_back(NamedValue{was.name, renamed[static_cast<std::size_t>(was.value)]});
    }
}

} // namespace detail

constexpr Graph canonical_renumber(const Graph& src) {
    Graph out{};

    fixed_vector<std::int32_t, kMaxValues> producer{};
    detail::producers_of(src, producer);

    fixed_vector<std::int32_t, kMaxValues> renamed{};
    fixed_vector<bool, kMaxValues> queued{};
    for (std::size_t i = 0; i < src.values.size(); ++i) {
        renamed.push_back(kNoValue);
        queued.push_back(false);
    }

    for (const NamedValue& root : src.outputs) {
        detail::walk_from(src, root.value, producer, renamed, queued, out);
    }
    detail::rename_named_edges(src, renamed, out);
    return out;
}

} // namespace heinzel::measurement
