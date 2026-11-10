// Subgraphs (Plan/archive/14-dsl/02 §7, Plan/28-dsl-proposals/02 A5): `subgraph(name, body)` makes a piece that,
// applied as `piece(args...)`, runs an ordinary function whose declared inputs come out `name.input`, with no context
// passed in.
//
//   strip      graph_composition's channel strip, spelled with declarations and `subgraph`, is the graph the
//              `BuildContext` spelling built (deleted at L9, its hash pinned) — siblings (`in_gain.gain`,
//              `out_gain.gain`) and nesting (`lpf.p1.a`) included
//   arguments  two from one arena, two from separate arenas, an array of them, and values that hold no Expr
//   nesting    three deep: `outer.mid.inner.k`
//   results    an array, a pair, a tuple, an `Input` returned by a lambda, a `Variable`'s value
//   bodies     a function name, a function pointer, a named lambda
//   leaves     an input declared inside and never read is absent, as at top level; one merged into an argument's
//              arena and left out of the result is kept, prefixed
//   pieces     one made before its inputs exist; one never applied adds nothing; a body that takes nothing runs when
//              applied, not when made
//   refusals   an over-long name and a piece applied twice at run time; what must not compile is in
//              `gates/refusals/subgraph.cpp`
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/frontend/subgraph.hpp"
#include "heinzel/sema/analyze.hpp"
#include "heinzel/support/fail.hpp"

#include "graph_hash.hpp"

using namespace heinzel;

namespace {

constexpr std::int32_t kBlock = 32;

constexpr bool has_input(const Graph& g, std::string_view name) {
    for (const NamedValue& in : g.inputs) {
        if (in.name.view() == name) {
            return true;
        }
    }
    return false;
}

// ---- graph_composition's strip ----------------------------------------------------------------------------------

namespace declared {

constexpr Index n{};
constexpr Expr gain_stage(Expr x) {
    Input gain{"gain", Signal{kBlock}};
    return x * gain;
}
constexpr Expr onepole_stage(Expr x) {
    Input a{"a", Signal{kBlock}};
    Variable y;
    y[n] = (1.0 - a) * x + a * y[n - 1];
    return Expr(y);
}
constexpr Expr twopole_stage(Expr x) {
    auto p1 = subgraph("p1", onepole_stage);
    auto p2 = subgraph("p2", onepole_stage);
    Expr first = p1(x);
    return p2(first);
}
constexpr Expr saturate_stage(Expr x) {
    Input drive{"drive", Signal{kBlock}};
    return tanh(x * drive);
}
constexpr Graph strip() {
    auto in_gain = subgraph("in_gain", gain_stage);
    auto lpf = subgraph("lpf", twopole_stage);
    auto sat = subgraph("sat", saturate_stage);
    auto out_gain = subgraph("out_gain", gain_stage);
    Input x{"x", Signal{kBlock}};
    Expr s = in_gain(x);
    s = lpf(s);
    s = sat(s);
    Output y{"y"};
    y = out_gain(s);
    return y;
}

} // namespace declared

// The graph the `BuildContext` spelling built (`instantiate(where, …)`, `where.inside`), recorded at 22b7c9ef,
// re-pinned when the hash stopped mixing a value's units and the sample rate (Decision 59), and again when
// `ValueCategory::Output` went (Decision 60), the graph field for field the same.
static_assert(hash_graph(declared::strip()) == 0x47a6dbefe8261f43ull, "the declared strip is the context's, node for node");
static_assert(has_input(declared::strip(), "in_gain.gain") && has_input(declared::strip(), "out_gain.gain") &&
              has_input(declared::strip(), "lpf.p1.a") && has_input(declared::strip(), "lpf.p2.a") && has_input(declared::strip(), "x"));

// ---- arguments, nesting, results, bodies, leaves -----------------------------------------------------------

constexpr Expr knob(Expr x) {
    Input k{"k", Signal{4}};
    return x * k;
}
constexpr Expr two(Expr a, Expr b) {
    Input k{"k", Signal{4}};
    return (a + b) * k;
}
constexpr Expr scaled(Expr x, double by, Shape, std::string_view, std::array<Expr, 2> more) { return knob(x * by + more[0] * more[1]); }

constexpr Graph arguments() {
    Input a{"a", Signal{4}};
    Input b{"b", Signal{4}};
    Expr shared = a * 2.0;
    Output p{"p"};
    Output q{"q"};
    Output r{"r"};
    auto one_arena = subgraph("shared", two);
    auto two_arenas = subgraph("apart", two);
    auto more = subgraph("more", scaled);
    p = one_arena(a, shared);
    q = two_arenas(a, b);
    r = more(a, 0.5, Shape{4}, "label", std::array<Expr, 2>{a, b});
    return finish(p, q, r);
}
static_assert(has_input(arguments(), "shared.k") && has_input(arguments(), "apart.k") && has_input(arguments(), "more.k") &&
              has_input(arguments(), "a") && has_input(arguments(), "b") && [] { Graph g = arguments(); return infer(g).ok; }(),
              "an input passed in keeps its name; one made inside the body, however deep in plain calls, is prefixed");

constexpr Expr inner(Expr x) {
    auto piece = subgraph("inner", knob);
    return piece(x);
}
constexpr Expr mid(Expr x) {
    auto piece = subgraph("mid", inner);
    return piece(x);
}
constexpr Graph three_deep() {
    auto outer = subgraph("outer", mid);
    Input x{"x", Signal{4}};
    Output y{"y"};
    y = outer(x);
    return y;
}
static_assert(has_input(three_deep(), "outer.mid.inner.k"));

constexpr Graph results() {
    Input x{"x", Signal{4}};
    auto pair = subgraph("pair", [](Expr v) { return std::pair{knob(v), v * 2.0}; });
    auto array = subgraph("array", [](Expr v) { return std::array<Expr, 2>{knob(v), v}; });
    auto tuple = subgraph("tuple", [](Expr v) { return std::tuple<Expr>{knob(v)}; });
    auto leaf = subgraph("leaf", [](Expr) { return Input{"k", Signal{4}}; });
    auto state = subgraph("state", [](Expr w) -> Expr {
        Variable acc;
        acc[Index{}] = knob(w) + 0.5 * acc[Index{} - 1];
        return acc;
    });
    const auto [s, t] = pair(x);
    const std::array<Expr, 2> both = array(x);
    const auto [u] = tuple(x);
    const Expr k = leaf(x);
    const Expr v = state(x);
    Output y{"y"};
    y = s + t + both[0] + both[1] + u + k + v;
    return y;
}
static_assert(has_input(results(), "pair.k") && has_input(results(), "array.k") && has_input(results(), "tuple.k") &&
              has_input(results(), "leaf.k") && has_input(results(), "state.k"));

constexpr auto named_lambda = [](Expr x) { return knob(x); };
constexpr Expr (*pointer)(Expr) = knob;
constexpr Graph bodies() {
    Input x{"x", Signal{4}};
    auto by_name = subgraph("name", knob);
    auto by_pointer = subgraph("pointer", pointer);
    auto by_lambda = subgraph("lambda", named_lambda);
    Output y{"y"};
    y = by_name(x) + by_pointer(x) + by_lambda(x);
    return y;
}
static_assert(has_input(bodies(), "name.k") && has_input(bodies(), "pointer.k") && has_input(bodies(), "lambda.k"));

// Declared and never read: not in the graph, as at top level. Merged into an argument's arena and left out of a
// result that lives in an arena of its own: still the body's, so prefixed — it reaches the graph when the caller
// uses the argument again.
constexpr Graph leaves() {
    Input x{"x", Signal{4}};
    Output y{"y"};
    auto eq = subgraph("eq", [](Expr v) {
        Input unread{"unread", Signal{4}};
        Input dropped{"dropped", Signal{4}};
        const Expr merged = v * dropped;
        Input fresh{"fresh", Signal{4}};
        return fresh * 2.0;
    });
    y = x + eq(x);
    return y;
}
static_assert(!has_input(leaves(), "eq.unread") && !has_input(leaves(), "unread") && has_input(leaves(), "eq.dropped") &&
              !has_input(leaves(), "dropped") && has_input(leaves(), "eq.fresh"));

// Every element of a result is scanned, each in an arena of its own; an array argument's elements are pinned, a null
// one skipped; a result whose arena moved after it was made is caught up.
constexpr Graph results_apart() {
    Input x{"x", Signal{4}};
    Input a{"a", Signal{4}};
    Input b{"b", Signal{4}};
    auto pa = subgraph("pa", [](Expr v) { return std::pair<Expr, Expr>{v * 2.0, Input{"k", Signal{4}}}; });
    auto ar = subgraph("ar", [](Expr v) { return std::array<Expr, 2>{v * 2.0, Input{"k", Signal{4}}}; });
    auto tu = subgraph("tu", [](Expr v) { return std::tuple<Expr, Expr>{v * 2.0, Input{"k", Signal{4}}}; });
    auto m_piece = subgraph("m", [](std::array<Expr, 3> v) { return v[0] + v[1]; });
    auto st = subgraph("st", [](Expr) {
        Input k{"k", Signal{4}};
        const Expr made = k * 2.0;
        Input j{"j", Signal{4}};
        const Expr big = ((j * 3.0) * 4.0) * 5.0 + made; // `made`'s arena moves into `big`'s
        return made;
    });
    const auto [p, q] = pa(x);
    const std::array<Expr, 2> r = ar(x);
    const auto [t, u] = tu(x);
    const Expr m = m_piece(std::array<Expr, 3>{a, b, Expr{}});
    const Expr stale = st(x);
    Output y{"y"};
    y = p + q + r[0] + r[1] + t + u + m + stale;
    return y;
}
static_assert(has_input(results_apart(), "pa.k") && has_input(results_apart(), "ar.k") && has_input(results_apart(), "tu.k") &&
              has_input(results_apart(), "a") && has_input(results_apart(), "b") && has_input(results_apart(), "st.k") &&
              has_input(results_apart(), "st.j"));

// ---- pieces: made first, applied later, once ------------------------------------------------------------------

// Made before the inputs it is applied to exist: the graph of a piece made just before it is applied.
constexpr Graph made_first() {
    auto eq = subgraph("eq", knob);
    Input x{"x", Signal{4}};
    Output y{"y"};
    y = eq(x);
    return y;
}
constexpr Graph made_late() {
    Input x{"x", Signal{4}};
    Output y{"y"};
    auto eq = subgraph("eq", knob);
    y = eq(x);
    return y;
}
static_assert(made_first() == made_late() && has_input(made_first(), "eq.k"));

// A body that takes nothing: a source with a knob of its own.
constexpr Expr noise() {
    Input level{"level", Signal{4}};
    return level * 0.5;
}
constexpr Graph noise_made_first() {
    auto hiss = subgraph("hiss", noise);
    Input x{"x", Signal{4}};
    Output y{"y"};
    Expr hissing = hiss();
    y = x + hissing;
    return y;
}
constexpr Graph noise_made_late() {
    Input x{"x", Signal{4}};
    Output y{"y"};
    auto hiss = subgraph("hiss", noise);
    Expr hissing = hiss();
    y = x + hissing;
    return y;
}
static_assert(noise_made_first() == noise_made_late() && has_input(noise_made_first(), "hiss.level"));

// Made and never applied, a body that takes nothing too: the graph has nothing of either.
constexpr Graph never_applied() {
    Input x{"x", Signal{4}};
    auto eq = subgraph("eq", knob);
    auto hiss = subgraph("hiss", noise);
    Output y{"y"};
    y = x * 2.0;
    return y;
}
constexpr Graph without_pieces() {
    Input x{"x", Signal{4}};
    Output y{"y"};
    y = x * 2.0;
    return y;
}
static_assert(never_applied() == without_pieces());

} // namespace

// ---- run time: an over-long name, a piece applied twice, a body that runs when applied ---------------------------

namespace {

int g_wrong = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

template <class Build>
bool refused(Build build, std::string_view text) {
    try {
        build();
    } catch (const error& e) {
        return e.message().find(text) != std::string_view::npos;
    }
    return false;
}

void long_names() {
    const std::string too_long(kMaxNameLength, 'p'); // with its terminator, one byte too many
    expect(refused([&] { const auto piece = subgraph(too_long, knob); }, "heinzel: a name is longer than kMaxNameLength"),
           "a piece's name past kMaxNameLength, refused when the piece is made");
    const std::string prefix(kMaxNameLength - 2, 'p'); // "p…p.k" and its terminator: one byte too many
    expect(refused([&] {
               Input x{"x", Signal{4}};
               auto piece = subgraph(prefix, knob);
               return piece(x);
           },
                   "heinzel: a name is longer than kMaxNameLength"),
           "a prefixed name past kMaxNameLength");
    const std::string fits(kMaxNameLength - 3, 'p');
    bool fitted = false;
    try {
        Input x{"x", Signal{4}};
        Output y{"y"};
        auto piece = subgraph(fits, knob);
        y = piece(x);
        fitted = has_input(Graph(y), fits + ".k");
    } catch (const error&) {
    }
    expect(fitted, "a prefixed name that just fits");
    // Refused, it renames nothing: every new name is measured before any is written (`k` would fit, `kk` not).
    Input x{"x", Signal{4}};
    const bool refused_whole = refused([&] {
        auto piece = subgraph(fits, [](Expr v) {
            Input k{"k", Signal{4}};
            Input kk{"kk", Signal{4}};
            return v * k * kk;
        });
        return piece(x);
    }, "longer than kMaxNameLength");
    Output y{"y"};
    y = x * 1.0;
    expect(refused_whole && has_input(Graph(y), "k") && has_input(Graph(y), "kk"), "a refused prefix renames nothing");
}

void applied_twice() {
    expect(refused([] {
               Input x{"x", Signal{4}};
               auto eq = subgraph("eq", knob);
               const Expr once = eq(x);
               return eq(once);
           },
                   "heinzel: a subgraph is applied once; make one for each use"),
           "a piece applied twice");
}

// Counted at run time: the body runs once, when its piece is applied, and not when the piece is made.
int g_runs = 0;
Expr counted() {
    ++g_runs;
    Input level{"level", Signal{4}};
    return level * 0.5;
}
void runs_when_applied() {
    auto hiss = subgraph("hiss", counted);
    const int when_made = g_runs;
    Input x{"x", Signal{4}};
    Output y{"y"};
    Expr hissing = hiss();
    y = x + hissing;
    expect(when_made == 0 && g_runs == 1 && has_input(Graph(y), "hiss.level"), "a body that takes nothing runs when applied");
}

} // namespace

int main() {
    long_names();
    applied_twice();
    runs_when_applied();
    if (g_wrong != 0) {
        std::printf("subgraph_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("subgraph_test: every input a subgraph declares carries its name\n");
    return 0;
}
