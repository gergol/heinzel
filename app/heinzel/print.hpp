#pragma once

// heinzel/print.hpp: `print<smooth>()`, what `Executor<smooth>` runs, as text; `print<smooth, zen2>()` what
// `Executor<smooth, zen2>` runs. Three listings: the graph as planned, one line a node (`n8 v11 = tanh v10 [256] f32`),
// the schedule, which puts the nodes into stages (`s1 = n8`) and gives each its loops, and the loop program: its
// buffers, its stages' statements and the tree of loops that runs them (`loops/print.hpp`). Or else the reason the
// graph is refused, by the compiler or by `Executor` (a symbol off time, state that does not run along the stream:
// `detail::why_not_run`, which both ask, in `Executor`'s words), or the error its graph function stopped with. It
// compiles at run time, taking `try_compile`'s steps, so a program that calls it carries the compiler. `dsl.hpp` does
// not include it and no `Executor` names it, so a program that only runs graphs pays nothing for it.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

#include "heinzel/compile.hpp"
#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/loops/print.hpp"
#include "heinzel/ports.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel {
namespace detail {

// What a message's writer wrote (`write_node`, `write_shape`, a number), as text.
inline std::string written(const MessageWriter& m) { return std::string(m.problem().message.view()); }

// An operand: a constant as its number, any other value as `v3`.
inline std::string shown_operand(const Graph& g, std::int32_t v) {
    MessageWriter m;
    if (is_a_constant(g, v)) {
        m << g.values[static_cast<std::size_t>(v)].constant;
    } else {
        m << std::string_view{"v"} << v;
    }
    return written(m);
}

// A value's shape and type, as the analysis gave them: `[256] f32`.
inline std::string shown_value(const Value& v) {
    MessageWriter shape;
    write_shape(shape, v.shape);
    return written(shape) + ' ' + std::string(loops::detail::spelled(v.type));
}

// What a node computes: its operation as the user wrote it, then its operands, or an operation of one character between
// its two (`v3 - v2`).
inline std::string shown_expression(const Graph& g, const Node& n) {
    MessageWriter op;
    write_node(op, n);
    const std::string name = written(op);
    if (name.size() == 1 && n.inputs[1] != kNoValue && n.inputs[2] == kNoValue) {
        const std::string left = shown_operand(g, n.inputs[0]);
        const std::string right = shown_operand(g, n.inputs[1]);
        return left + ' ' + name + ' ' + right;
    }
    std::string out = name;
    for (const std::int32_t v : n.inputs) {
        if (v != kNoValue) {
            out += ' ';
            out += shown_operand(g, v);
        }
    }
    return out;
}

// One node: `n8 v11 = tanh v10 [256] f32`, after its operands a lag (`back 1`) and the variable a recurrence defines
// (`defines v0`), which no operand shows.
inline void put_graph_node(std::string& out, const Graph& g, std::size_t index) {
    const Node& n = g.nodes[index];
    const std::string expression = shown_expression(g, n);
    out += 'n' + std::to_string(index) + " v" + std::to_string(n.output) + " = " + expression;
    if (n.attr.kind == AttrKind::Delay) {
        out += " back " + std::to_string(n.attr.delay.samples);
    } else if (n.attr.kind == AttrKind::Recurrence) {
        out += " defines v" + std::to_string(n.attr.recurrence.defines);
    }
    const Value& made = g.values[static_cast<std::size_t>(n.output)];
    out += ' ' + shown_value(made) + '\n';
}

// A graph as text: its inputs (`v3 = input x [256] f32`), its nodes in order, its outputs (`output y = v11`).
inline std::string graph_listing(const Graph& g) {
    std::string out;
    for (const NamedValue& port : g.inputs) {
        const Value& fed = g.values[static_cast<std::size_t>(port.value)];
        const std::string name{port.name.view()};
        out += 'v' + std::to_string(port.value) + " = input " + name + ' ' + shown_value(fed) + '\n';
    }
    for (std::size_t k = 0; k < g.nodes.size(); ++k) {
        put_graph_node(out, g, k);
    }
    for (const NamedValue& port : g.outputs) {
        const std::string name{port.name.view()};
        out += "output " + name + " = v" + std::to_string(port.value) + '\n';
    }
    return out;
}

// The three listings of a plan, each after its heading.
inline std::string listings(const Planning& p) {
    const std::string graph = graph_listing(p.graph);
    const std::string schedule = loops::print(p.planned->schedule, p.graph);
    const std::string program = loops::print(p.planned->program);
    return "-- graph\n" + graph + "-- schedule\n" + schedule + "-- program\n" + program;
}

template <auto Build, const Machine& M, bool Checked>
std::string printed() {
    // The graph on the heap, as its plan is (`Attempt`): about 70 KB, and the rewrite's frames below need the stack.
    const std::unique_ptr<Graph> g(new Graph(Build()));
    const Attempt a = attempt(*g, M, Bindings{}, Checked);
    if (!a.refusal.view().empty()) {
        return std::string(a.refusal.view()) + '\n';
    }
    const std::string_view not_run = why_not_run(a.planning->planned->program);
    if (!not_run.empty()) {
        return std::string(not_run) + '\n';
    }
    return listings(*a.planning);
}

} // namespace detail

// What `Executor<Build, M, Checked>` runs, as text, compiled now; or why it does not run.
template <auto Build, const Machine& M = default_machine, bool Checked = detail::kChecked>
std::string print() {
    static_assert(std::is_same_v<decltype(Build()), Graph>, "heinzel::print: the graph function returns a Graph");
#if defined(__cpp_exceptions)
    try {
        return detail::printed<Build, M, Checked>();
    } catch (const error& e) {
        return std::string(e.message()) + '\n';
    }
#else
    return detail::printed<Build, M, Checked>(); // (a failure traps, as `fail` does)
#endif
}

} // namespace heinzel
