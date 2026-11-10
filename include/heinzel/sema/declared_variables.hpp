#pragma once

// A `Variable` that declares its length (`Variable phase{Signal{N}}`, `Output y{"y", Signal{N}}`): its
// shape and type are said before the analysis, and its recurrence's definition must come out at exactly
// them. A variable recorded with a shape is a declared one; the frontend gives no other variable a shape,
// and after one analysis every defined variable has its derived shape, so a second analysis keeps what
// the first found. One that no signal drives, in a graph of one length, takes that length (Decision 76):
// held as a declared one is, and declared after one analysis.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/sema/refusal.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel {

namespace detail {

struct Declared {
    bool at[kMaxValues]{};
    bool takes[kMaxValues]{}; // no signal drives it: it takes `length`, the graph's one (`time_checks.hpp`)
    Extent length{};
};

// Taken before the sweep: which variables say their shape themselves.
constexpr Declared declared_variables(const Graph& g) {
    Declared d{};
    for (std::size_t v = 0; v < g.values.size(); ++v) {
        d.at[v] = g.values[v].category == ValueCategory::Variable && g.values[v].shape.rank > 0;
    }
    return d;
}

// The shape a variable that takes the graph's length gets from its definition's: the length last, where a tensor's
// recorded 1 stands, or as the one axis of a value of none (numbers and Params only).
constexpr Shape with_length(Shape definition, Extent length) {
    definition.rank = definition.rank == 0 ? 1 : definition.rank;
    set_extent(definition, definition.rank - 1, length);
    return definition;
}

// A variable that took the graph's length and that nothing typed, the sweep and the numbers' step done, is f32, as a
// declaration's default. Only then: given at its first visit, it typed the literals its readers adapt before a
// definition typed in a later sweep (through a variable defined after it) could, unlike the graph declared.
constexpr bool undriven_default_to_f32(Graph& g, const Declared& declared) {
    bool given = false;
    for (std::size_t v = 0; v < g.values.size(); ++v) {
        if (declared.takes[v] && g.values[v].type == ElemType::Unknown) {
            g.values[v].type = ElemType::F32;
            given = true;
        }
    }
    return given;
}

// A recurrence that no signal drives and no length reaches (`time_checks.hpp`).
inline constexpr std::string_view kNeedsASignal = "heinzel: a recurrence needs a Signal in it, or a declared length";

constexpr std::string_view type_word(ElemType t) { return t == ElemType::F64 ? " f64" : " f32"; } // Unknown: a literal's f32

// Every declared variable is defined, at exactly its declared shape and type. A definition that reads the
// variable's past broadcasts to it; one wider, narrower (it reads no past, or folds it) or of another type is
// refused: the planner treats a variable and its definition as one value. One that took the graph's length and
// missed it reads no past or folds it, so no signal is in it: refused in the words it was before it could take one.
constexpr Diagnostic declared_variables_hold(const Graph& g, const Declared& declared) {
    bool defined[kMaxValues]{};
    for (const Node& n : g.nodes) {
        if (n.attr.kind != AttrKind::Recurrence || n.attr.recurrence.defines == kNoValue) {
            continue;
        }
        const auto v = static_cast<std::size_t>(n.attr.recurrence.defines);
        defined[v] = true;
        const Value& is = g.values[static_cast<std::size_t>(n.output)];
        const Value& said = g.values[v];
        const bool holds = is.shape == said.shape && is.type == said.type;
        if (declared.takes[v] && !holds) {
            return (MessageWriter{} << kNeedsASignal).problem();
        }
        if (declared.at[v] && !holds) {
            MessageWriter m;
            m << std::string_view{"heinzel: a Variable is defined as "};
            write_shape(m, is.shape);
            m << type_word(is.type) << std::string_view{" but declared "};
            write_shape(m, said.shape);
            m << type_word(said.type);
            return m.problem();
        }
    }
    for (std::size_t v = 0; v < g.values.size(); ++v) {
        if (declared.at[v] && !defined[v]) {
            return (MessageWriter{} << std::string_view{"heinzel: a declared Variable is never defined"}).problem();
        }
    }
    return Diagnostic{};
}

} // namespace detail

} // namespace heinzel
