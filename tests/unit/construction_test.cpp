// Building a graph.
//
// The assertions run during compilation, and that is not incidental here. Building allocates, and
// the language requires everything allocated during a constant evaluation to be released before it
// ends — so a leak is a hard compile error. **This file passing is the compiler certifying that the
// ownership model is right**, which is a stronger statement than any test could make by checking.
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/frontend/subgraph.hpp"

#include <string_view>

using namespace heinzel;

constexpr Index n{};

// ---- a leaf is a graph -----------------------------------------------------------------------------
// No graph is handed to a leaf. This is the whole point of the design, so it is the first assertion.
static_assert([] {
    const Input x{"x", Signal{64}};
    return x.arena() != nullptr && x.arena()->nodes.size() == 1 && x.position() == 0;
}(), "an input allocates its own one-node graph");

static_assert([] {
    const Expr k = constant(2.0);
    return k.arena()->nodes[0].category == ValueCategory::Const && k.arena()->nodes[0].constant == 2.0;
}());

// A literal leaves its type unresolved on purpose: it adapts to what it is used with rather than
// forcing double precision on an expression because someone wrote a coefficient as a double.
static_assert(constant(0.5).arena()->nodes[0].type == ElemType::Unknown,
              "a literal adapts to its neighbours; it does not drive the type");

// ---- composing merges, and reuse stays one node -------------------------------------------------------
static_assert([] {
    Input x{"x", Signal{64}};
    Expr y = x * 2.0 + x;
    y.catch_up();
    // input, literal, multiply, add — `x` appears twice in the expression and once in the graph.
    return y.arena()->nodes.size() == 4;
}(), "an expression that uses a value twice does not duplicate it");

// Sharing is not a size optimisation. Duplicating and cleaning up afterwards cannot be relied on:
// the pass that removes duplicates deliberately leaves delays alone, because the same read is a
// feedback tap in one place and an ordinary delayed read in another, and merging them drops the tap.
static_assert([] {
    Input x{"x", Signal{64}};
    Expr d = x[n - 1];
    Expr y = d + d;
    y.catch_up();
    return y.arena()->nodes.size() == 3; // input, delay, add
}(), "a delayed read used twice is one read");

// ---- a stale handle is never wrong, only one hop from correct -------------------------------------------
static_assert([] {
    Input a{"a", Signal{4}};
    Input b{"b", Signal{4}};
    Input c{"c", Signal{4}};
    // Build up one side so the other is the smaller and gets moved.
    Expr big = a + b;
    big = big * a;
    big = big - b;
    const Expr joined = big + c;   // `c`'s arena is moved, so `c` is now stale
    Expr stale = c;
    stale.catch_up();
    return stale.arena() == joined.arena() && stale.arena()->nodes[static_cast<std::size_t>(stale.position())].category ==
                                                  ValueCategory::Input;
}(), "a handle whose nodes were moved still finds them");

// ---- the case the design exists for: a value defined by its own past ---------------------------------------
// `y[n] = x[n] - 0.5*y[n-1]` reads y's past before y has been defined.
constexpr Graph one_pole() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    y[n] = x[n] - 0.5 * y[n - 1];
    return y;
}

static_assert([] {
    const Graph g = one_pole();
    // Every recurrence must still name a real value after all the moving around. This is the
    // assertion the design was built to be able to make.
    for (const Node& n : g.nodes) {
        if (n.attr.kind != AttrKind::Recurrence) {
            continue;
        }
        const std::int32_t named = n.attr.recurrence.defines;
        if (named < 0 || named >= static_cast<std::int32_t>(g.values.size())) {
            return false;
        }
        if (g.values[static_cast<std::size_t>(named)].category != ValueCategory::Variable) {
            return false;
        }
        return true;
    }
    return false; // there was no recurrence at all
}(), "a recurrence still names its own variable after the arenas have been merged");

// ---- finishing ------------------------------------------------------------------------------------------
static_assert(one_pole().inputs.size() == 1 && one_pole().inputs[0].name.view() == std::string_view{"x"});
static_assert(one_pole().outputs.size() == 1 && one_pole().outputs[0].name.view() == std::string_view{"y"});

// Nothing that is not an operation became one: an input, a literal and a variable are values.
static_assert([] {
    const Graph g = one_pole();
    std::size_t values_without_a_node = 0;
    for (const Value& v : g.values) {
        if (v.category == ValueCategory::Input || v.category == ValueCategory::Const ||
            v.category == ValueCategory::Variable) {
            ++values_without_a_node;
        }
    }
    return g.values.size() == g.nodes.size() + values_without_a_node;
}(), "every value is either produced by exactly one operation or is not produced at all");

// Every operand refers to a value that exists, and to one written before it.
static_assert([] {
    const Graph g = one_pole();
    for (const Node& n : g.nodes) {
        for (std::int32_t in : n.inputs) {
            if (in == kNoValue) {
                continue;
            }
            if (in < 0 || in >= static_cast<std::int32_t>(g.values.size())) {
                return false;
            }
            if (in >= n.output) {
                return false; // an operand defined after its use
            }
        }
    }
    return true;
}(), "operands refer to values that exist, and that came earlier");

// ---- names carry their enclosing subgraphs ----------------------------------------------------------------
constexpr Expr eq(Expr v) {
    Input cutoff{"cutoff", Signal{1}};
    return v * cutoff;
}
constexpr Expr mixer(Expr v) {
    auto piece = subgraph("eq", eq);
    return piece(v);
}
static_assert([] {
    Input x{"x", Signal{1}};
    auto piece = subgraph("mixer", mixer);
    Expr y = piece(x);
    y.catch_up();
    for (const BuildNode& b : y.arena()->nodes) {
        if (b.category == ValueCategory::Input && b.name.view() == std::string_view{"mixer.eq.cutoff"}) {
            return true;
        }
    }
    return false;
}(), "a declared name composes the subgraphs it was written inside");

// ---- three operands, from three separate graphs ---------------------------------------------------------
// The three-operand path merges twice, so every handle involved may move more than once. It is the
// least obvious code in the layer, which is exactly why it is asserted rather than assumed.
static_assert([] {
    Input a{"a", Signal{4}};
    Input b{"b", Signal{4}};
    Input c{"c", Signal{4}};
    Expr chosen = apply(op::Select, a, b, c);
    chosen.catch_up();
    a.catch_up();
    b.catch_up();
    c.catch_up();
    const Arena* live = chosen.arena();
    if (a.arena() != live || b.arena() != live || c.arena() != live) {
        return false;
    }
    const auto& n = live->nodes[static_cast<std::size_t>(chosen.position())];
    // Each operand slot points at its own input, and no two at the same one.
    return n.inputs[0] == a.position() && n.inputs[1] == b.position() && n.inputs[2] == c.position() &&
           a.position() != b.position() && b.position() != c.position() && a.position() != c.position();
}(), "three operands from three separate graphs all end up in one, each still pointing at itself");

// ---- the same graph, built twice, is the same graph -----------------------------------------------------------
// Order is deterministic because finishing copies the arena in the order things were written, rather
// than choosing an order by walking a structure.
static_assert(one_pole() == one_pole(), "building the same expression twice gives the same graph");

// ---- compiling and running agree ------------------------------------------------------------------------------
// The graph the compiler produced, compared against the graph the same code produces when it runs.
// This is the check that catches a compiler disagreeing with itself between the two paths — which
// the old tree records as something actually observed, not a hypothetical.
namespace {

bool compile_time_and_run_time_agree() {
    constexpr Graph built_while_compiling = one_pole();
    const Graph built_while_running = one_pole();
    return built_while_compiling == built_while_running;
}

} // namespace

int main() { return compile_time_and_run_time_agree() ? 0 : 1; }
