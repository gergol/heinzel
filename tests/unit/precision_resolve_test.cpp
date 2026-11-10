// infer() resolves each value's numeric precision by data flow.
// Compile-time proof over the REAL infer path: a .precision(High) mark inherits downstream, the
// select-higher join wins where High meets Normal, and an unmarked value stays Normal (the default).
// The planner reads the resolved level, and the lowering copies it into each statement for the
// runner; this test guards the resolution itself. And the spellings of Plan/28-dsl-proposals/02 A4: the
// levels' names, the mark where a value is made, `finish(level, …)`; a mark on a named value, or on one
// something reads, is the build_refuses_precision_* fixtures.
#include <concepts>
#include <cstddef>
#include <string_view>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

using namespace heinzel;
using numerics::Precision;

namespace {

constexpr std::size_t N = 64;

constexpr std::int32_t input_id(const Graph& g, std::string_view name) {
    for (const NamedValue& nv : g.inputs) {
        if (nv.name.view() == name) {
            return nv.value;
        }
    }
    return kNoValue;
}

constexpr Graph kG = [] {
    Expr x = Input{"x", Signal{N}}.precision(Precision::High); // marked where it is declared
    Input a{"a", Signal{N}};                                     // a unmarked -> Normal
    Output y{"y"};
    y = tanh(x) + a; // tanh(x) is High; +a is join(High, Normal) -> High
    Graph g = y;
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("precision_resolve: graph did not resolve");
    }
    return g;
}();

static_assert(kG.values[static_cast<std::size_t>(input_id(kG, "x"))].precision == Precision::High,
              "x stays pinned High");
static_assert(kG.values[static_cast<std::size_t>(input_id(kG, "a"))].precision == Precision::Normal,
              "unmarked a resolves to Normal (the default level)");
static_assert(kG.values[static_cast<std::size_t>(kG.outputs[0].value)].precision == Precision::High,
              "output inherits High through tanh and wins the select-higher join with Normal");

// The levels by their names in `heinzel` (Plan/28-dsl-proposals/02 A4): the enumerators themselves.
static_assert(std::same_as<heinzel::Precision, numerics::Precision> && High == Precision::High &&
                  Normal == Precision::Normal && Fast == Precision::Fast,
              "High, Normal and Fast are numerics::Precision's levels");

// A mark where a value is made writes the node a mark on the named value wrote before (A4), and adds no node: kG's
// port marked in its declaration, a sum marked as it is made.
constexpr void write_mark(Expr e, Precision level) { // what `x.precision(level)` on a named x did
    e.catch_up();
    e.arena()->nodes[static_cast<std::size_t>(e.position())].precision = level;
}
static_assert([] {
    Input x{"x", Signal{N}};
    Input a{"a", Signal{N}};
    write_mark(x, Precision::High);
    Output y{"y"};
    y = tanh(x) + a;
    Graph g = y;
    return infer(g).ok && g == kG;
}(), "a port marked where it is declared is the graph of the mark on the named port");
constexpr Graph sum_marked(bool where_made) {
    Input a{"a", Signal{N}};
    Input b{"b", Signal{N}};
    Output y{"y"};
    if (where_made) {
        Expr s = (a + b).precision(High);
        y = tanh(s);
        return y;
    }
    Expr s = a + b;
    write_mark(s, Precision::High);
    y = tanh(s);
    return y;
}
static_assert(sum_marked(true) == sum_marked(false), "a value marked as it is made: the mark on its node");
// A function returning `const Expr` hands out a temporary as fresh as any other (the `const&&` mark).
constexpr const Expr summed(Expr a, Expr b) { return a + b; }
constexpr Graph sum_marked_const() {
    Input a{"a", Signal{N}};
    Input b{"b", Signal{N}};
    Output y{"y"};
    Expr s = summed(a, b).precision(High);
    y = tanh(s);
    return y;
}
static_assert(sum_marked_const() == sum_marked(true), "a const temporary marked as it is made: the same graph");

// The graph-wide default: the level an unmarked value resolves to. It is how `Fast` reaches a whole
// graph (select-higher alone never goes below Normal), how `High` does without marking every input, and
// a pin still wins where it is higher.
constexpr Graph with_default(Precision level, Precision pin_on_x) {
    Expr x = Input{"x", Signal{N}}.precision(pin_on_x);
    Input a{"a", Signal{N}};
    Output y{"y"};
    y = tanh(x) + a;
    Graph g = y;
    set_default_precision(g, level);
    if (!infer(g).ok) {
        fail("precision_resolve: graph did not resolve");
    }
    return g;
}

constexpr bool every_value_is(const Graph& g, Precision level) {
    for (const Value& v : g.values) {
        if (v.precision != level) {
            return false;
        }
    }
    return true;
}

static_assert(every_value_is(with_default(Precision::Fast, Precision::Inherit), Precision::Fast),
              "a Fast default reaches every unmarked value");
static_assert(every_value_is(with_default(Precision::High, Precision::Inherit), Precision::High),
              "a High default makes the whole graph bit-exact without a mark");
static_assert([] {
    const Graph g = with_default(Precision::Fast, Precision::High);
    return g.values[static_cast<std::size_t>(input_id(g, "a"))].precision == Precision::Fast &&
           g.values[static_cast<std::size_t>(g.outputs[0].value)].precision == Precision::High;
}(), "under a Fast default a High pin still wins every join it reaches, and nothing else moves");
static_assert(with_default(Precision::Normal, Precision::High) == kG, "Normal is the default's default");

// Set on a finished graph before its first analysis, the default reaches every value of it, one output or several.
static_assert([] {
    Input x{"x", Signal{N}};
    Output y{"y"};
    y = tanh(x);
    Graph g = y;
    set_default_precision(g, Precision::High);
    return g.default_precision == Precision::High && infer(g).ok && every_value_is(g, Precision::High);
}(), "a default precision set before the analysis reaches the graph");
static_assert([] {
    Input x{"x", Signal{N}};
    Output a{"a"};
    Output b{"b"};
    a = tanh(x);
    b = x + x;
    Graph g = finish(a, b);
    set_default_precision(g, Precision::High);
    return infer(g).ok && every_value_is(g, Precision::High);
}(), "and the multi-output finish()'s");

// `finish(level, …)` (A4) is the graph `set_default_precision` gives the finished one, one output or several.
constexpr Graph one_output(Precision level, bool at_finish) {
    Input x{"x", Signal{N}};
    Output y{"y"};
    y = tanh(x);
    if (at_finish) {
        return finish(level, y);
    }
    Graph g = y;
    set_default_precision(g, level);
    return g;
}
constexpr Graph two_outputs(Precision level, bool at_finish) {
    Input x{"x", Signal{N}};
    Output a{"a"};
    Output b{"b"};
    a = tanh(x);
    b = x + x;
    if (at_finish) {
        return finish(level, a, b);
    }
    Graph g = finish(a, b);
    set_default_precision(g, level);
    return g;
}
static_assert(one_output(High, true) == one_output(High, false) && one_output(Fast, true) == one_output(Fast, false) &&
                  one_output(Normal, true) == one_output(Normal, false),
              "finish(High, y) is finish(y) with set_default_precision(g, High)");
static_assert(two_outputs(High, true) == two_outputs(High, false) &&
                  two_outputs(Fast, true) == two_outputs(Fast, false),
              "finish(High, a, b) is finish(a, b) with set_default_precision(g, High)");
static_assert([] {
    Graph g = one_output(High, true);
    return g.default_precision == Precision::High && infer(g).ok && every_value_is(g, Precision::High);
}(), "a graph finished at High resolves at High throughout");

// A default that arrives after the analysis could reach nothing (resolved levels read as pins), so
// analysis refuses it by name instead of ignoring it; and `Inherit` is no level to default to.
constexpr bool refused_mentioning(Graph g, std::string_view words) {
    const Diagnostic d = infer(g);
    return !d.ok && d.message.view().find(words) != std::string_view::npos;
}
static_assert([] {
    Graph late = kG;
    late.default_precision = Precision::High;
    return refused_mentioning(late, "changed after the graph was analysed");
}(), "a default precision set after analysis is refused, not ignored");
static_assert([] {
    Graph again = kG;
    return infer(again).ok && again == kG;
}(), "while re-analysing under the same default stays the no-op it was");
static_assert([] {
    Input x{"x", Signal{N}};
    Output y{"y"};
    y = tanh(x);
    Graph g = y;
    g.default_precision = Precision::Inherit;
    return refused_mentioning(g, "must be a level");
}(), "Inherit is not a level a graph can default to");

} // namespace

int main() { return 0; }
