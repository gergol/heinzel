// The ranges a graph states (Plan/26-value-ranges/02): a port's, last in its declaration, a computed value's `promise`,
// and a composition's fact (`abs`, `frac`), each recorded on `Graph::ranges` as a `RangeMark`: the value, its pieces,
// and whether it is a promise.
// What a list of comparators means, and `intersect` piece by piece; marks that survive arenas merging and a subgraph;
// every refusal by its text (what must not compile is in `gates/refusals/ranges.cpp`). The planner reads no mark: a
// graph with marks plans the program of the same graph without them, and hashes as it once they are cleared; so does
// `compile` in release, though the proof refuses that graph without them (in debug, its promises are checked:
// `ranges_checked_test.cpp`). Checked while compiling and again at run time.
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string_view>

#include "heinzel/compile.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/frontend/subgraph.hpp"
#include "heinzel/sema/analyze.hpp"

#include "graph_hash.hpp"
#include "hand_built_graph.hpp"

using namespace heinzel;

namespace {

constexpr Index n{};
constexpr double kInf = std::numeric_limits<double>::infinity();

constexpr Interval closed(double lo, double hi) { return Interval{lo, hi, false, false}; }
constexpr Interval open(double lo, double hi) { return Interval{lo, hi, true, true}; }

template <class... I>
constexpr Range made_of(I... piece) {
    Range r{};
    std::size_t k = 0;
    ((r.pieces[k++] = piece), ...);
    r.count = static_cast<std::int8_t>(k);
    return r;
}

// The mark on a value, or none (`kNoValue`).
constexpr RangeMark mark_of(const Graph& g, std::int32_t v) {
    for (const RangeMark& m : g.ranges) {
        if (m.value == v) {
            return m;
        }
    }
    return RangeMark{};
}
constexpr std::int32_t port(const Graph& g, std::string_view name) {
    for (const NamedValue& in : g.inputs) {
        if (in.name.view() == name) {
            return in.value;
        }
    }
    return kNoValue;
}

// ---- each spelling records its mark --------------------------------------------------------------

constexpr Graph declared() {
    Param gain{"gain", ge(0), le(1)};
    Param fc{"fc", f64, gt(0), le(20000)};
    Param plain{"plain"};
    Input x{"x", Signal{4, 8}, ge(-1), le(1)};
    Input w{"w", {4}, f32, ge(-1), le(1)};
    Output y{"y"};
    y = x * w * gain * fc * plain;
    return y;
}
static_assert(declared().ranges.size() == 4);
static_assert(mark_of(declared(), port(declared(), "gain")) == RangeMark{port(declared(), "gain"), made_of(closed(0, 1)), false});
static_assert(mark_of(declared(), port(declared(), "fc")).range == made_of(Interval{0, 20000, true, false}));
static_assert(mark_of(declared(), port(declared(), "x")).range == made_of(closed(-1, 1)) &&
              mark_of(declared(), port(declared(), "w")).range == made_of(closed(-1, 1)));
static_assert(mark_of(declared(), port(declared(), "plain")).value == kNoValue); // an empty list states no range

// A promise chained, and in place: one graph.
constexpr Graph chained() {
    Input a{"a", Signal{8}};
    Input b{"b", Signal{8}};
    Output q{"q"};
    const Expr c = (a + b).promise(gt(0), le(2));
    q = a / c;
    return q;
}
constexpr Graph in_place() {
    Input a{"a", Signal{8}};
    Input b{"b", Signal{8}};
    Output q{"q"};
    const Expr c = a + b;
    c.promise(gt(0), le(2));
    q = a / c;
    return q;
}
static_assert(chained() == in_place() && chained().ranges.size() == 1);
static_assert(chained().ranges[0] == RangeMark{chained().nodes[0].output, made_of(Interval{0, 2, true, false}), true});

// A hole, one side, a recurrence's definition.
constexpr Graph holes_and_sides() {
    Input x{"x", Signal{8}};
    Input y0{"y0", Signal{8}};
    Output d{"d"};
    Output env{"env"};
    Output y{"y"};
    d = 1.0 / (x - y0).promise(ge(-1), le(1), ne(0));
    env = max(x, 0.0).promise(ge(0));
    y[n] = (x[n] + 0.5 * y[n - 1]).promise(ge(-2), le(2));
    return finish(d, env, y);
}
constexpr const Node& producer(const Graph& g, std::int32_t v) {
    for (const Node& node : g.nodes) {
        if (node.output == v) {
            return node;
        }
    }
    return g.nodes[0];
}
static_assert(holes_and_sides().ranges.size() == 3);
static_assert(holes_and_sides().ranges[0].range == made_of(Interval{-1, 0, false, true}, Interval{0, 1, true, false}));
static_assert(holes_and_sides().ranges[1] == RangeMark{holes_and_sides().outputs[1].value, made_of(Interval{0, kInf, false, true}), true});
// The definition carries the promise; the recurrence's own value carries none.
static_assert(producer(holes_and_sides(), holes_and_sides().outputs[2].value).inputs[0] == holes_and_sides().ranges[2].value &&
              holes_and_sides().ranges[2].range == made_of(closed(-2, 2)) && holes_and_sides().ranges[2].promised);

// A composition states the range its result always has, as a fact: `abs` at least 0, `frac` in [0, 1). A promise on
// one is a mark of its own, after the fact.
constexpr Graph composed() {
    Input x{"x", Signal{8}};
    Output a{"a"};
    Output f{"f"};
    Output p{"p"};
    a = abs(x);
    f = frac(x);
    p = abs(x).promise(gt(0));
    return finish(a, f, p);
}
constexpr Range kAtLeastZero = made_of(Interval{0, kInf, false, true});
static_assert(composed().ranges.size() == 4);
static_assert(composed().ranges[0] == RangeMark{composed().outputs[0].value, kAtLeastZero, false} &&
              composed().ranges[1] == RangeMark{composed().outputs[1].value, made_of(closed(0, 1)), false});
static_assert(composed().ranges[2] == RangeMark{composed().outputs[2].value, kAtLeastZero, false} &&
              composed().ranges[3] == RangeMark{composed().outputs[2].value, made_of(open(0, kInf)), true});

// ---- a list of comparators ---------------------------------------------------------------------------

static_assert(detail::stated(le(1), ge(0)) == detail::stated(ge(0), le(1)) && detail::stated(ge(0), le(1)) == made_of(closed(0, 1)));
static_assert(detail::stated(ne(0), ge(-1), le(1)) == detail::stated(ge(-1), le(1), ne(0)) &&
              detail::stated(ge(-1), ne(0), le(1)) == made_of(Interval{-1, 0, false, true}, Interval{0, 1, true, false}));
static_assert(detail::stated(ge(0), ge(1), le(5), le(3)) == made_of(closed(1, 3)));           // the tighter bound wins
static_assert(detail::stated(ge(1), le(1)) == made_of(closed(1, 1)));                          // one number
static_assert(detail::stated(gt(0)) == made_of(Interval{0, kInf, true, true}) && detail::stated(lt(0)) == made_of(Interval{-kInf, 0, true, true}));
static_assert(detail::stated(ne(5), ge(0), le(1)) == made_of(closed(0, 1)));                   // a hole outside the bounds
static_assert(detail::stated(ne(1), ne(2), ne(3), ne(4), ge(0), le(3.5)).count == 4);          // holes after bounds, in any order
static_assert(ne(0) == made_of(open(-kInf, 0), open(0, kInf)));
static_assert(std::bit_cast<std::uint64_t>(ge(-0.0).pieces[0].lo) == 0 && ge(-0.0) == ge(0.0)); // -0 is 0

// ---- intersect, piece by piece -------------------------------------------------------------------------

static_assert(intersect(made_of(closed(-1, 1)), ne(0)) == made_of(Interval{-1, 0, false, true}, Interval{0, 1, true, false}));
static_assert(intersect(ne(0), ne(0)) == ne(0) && intersect(ne(0), ne(1)) == made_of(open(-kInf, 0), open(0, 1), open(1, kInf)));
static_assert(intersect(made_of(closed(0, 1), closed(1, 2)), Range{}) == made_of(closed(0, 2)));               // touching: one
static_assert(intersect(made_of(Interval{0, 1, false, true}, closed(1, 2)), Range{}) == made_of(closed(0, 2)));
static_assert(intersect(made_of(Interval{0, 1, false, true}, Interval{1, 2, true, false}), Range{}).count == 2); // a hole at 1
static_assert(intersect(made_of(closed(2, 3), closed(0, 2.5)), Range{}) == made_of(closed(0, 3)));            // sorted, joined
static_assert(intersect(made_of(closed(4, 5), closed(0, 1)), Range{}) == made_of(closed(0, 1), closed(4, 5)));
static_assert(intersect(gt(2), lt(1)) == made_of() && intersect(ge(1), lt(1)).count == 0 && intersect(gt(1), le(1)).count == 0);
static_assert(intersect(ge(1), le(1)) == made_of(closed(1, 1)));
static_assert(intersect(made_of(closed(0, 1), closed(2, 3)), made_of(closed(0.5, 2.5))) == made_of(closed(0.5, 1), closed(2, 2.5)));
static_assert(intersect(ne(3), made_of(closed(0, 1), closed(2, 4))) == intersect(made_of(closed(0, 1), closed(2, 4)), ne(3)));

// ---- marks survive merges ------------------------------------------------------------------------------

// A ranged Param's one-node arena and a promised value's three-node one join a larger arena.
constexpr Graph merged() {
    Input x{"x", Signal{8}};
    Expr big = x * 2.0 + 1.0;
    big = big * big - 0.5;
    Param gain{"gain", ge(0), le(1)};
    Input z{"z", Signal{8}};
    const Expr small = (z * 3.0).promise(ge(-3), le(3));
    Output y{"y"};
    y = big * gain + small;
    return y;
}
constexpr bool merged_marks_hold() {
    const Graph g = merged();
    const RangeMark promised = g.ranges[0].promised ? g.ranges[0] : g.ranges[1];
    const Node& made = producer(g, promised.value);
    return g.ranges.size() == 2 && mark_of(g, port(g, "gain")).range == made_of(closed(0, 1)) &&
           made.op == op::Mul && made.inputs[0] == port(g, "z") && promised.range == made_of(closed(-3, 3));
}
static_assert(merged_marks_hold());

// Inside a subgraph: a ranged Param, under its prefixed name, and a promise.
constexpr Expr stage(Expr x) {
    Param g{"g", ge(0), le(2)};
    return (x * g).promise(ge(-2), le(2));
}
constexpr Graph in_a_subgraph() {
    Input x{"x", Signal{8}, ge(-1), le(1)};
    Output y{"y"};
    auto s = subgraph("s", stage);
    y = s(x);
    return y;
}
static_assert(in_a_subgraph().ranges.size() == 3 && mark_of(in_a_subgraph(), port(in_a_subgraph(), "s.g")).range == made_of(closed(0, 2)) &&
              mark_of(in_a_subgraph(), port(in_a_subgraph(), "x")).range == made_of(closed(-1, 1)));
static_assert(mark_of(in_a_subgraph(), in_a_subgraph().outputs[0].value) ==
              RangeMark{in_a_subgraph().outputs[0].value, made_of(closed(-2, 2)), true});

// ---- nothing reads a mark yet --------------------------------------------------------------------------

constexpr Graph one_pole_ranged() {
    Input x{"x", Signal{64}, ge(-1), le(1)};
    Param a{"a", ge(0), lt(1)};
    Output y{"y"};
    Output e{"e"};
    y[n] = (x[n] + a * y[n - 1]).promise(ge(-2), le(2));
    e = 1.0 / (x - y).promise(ne(0));
    return finish(y, e);
}
constexpr Graph one_pole_plain() {
    Input x{"x", Signal{64}};
    Param a{"a"};
    Output y{"y"};
    Output e{"e"};
    y[n] = x[n] + a * y[n - 1];
    e = 1.0 / (x - y);
    return finish(y, e);
}
constexpr bool plans_as_without() {
    Graph with = one_pole_ranged();
    Graph without = one_pole_plain();
    if (with.ranges.size() != 4 || !infer(with).ok || !infer(without).ok) {
        return false;
    }
    const Planning a = planned_as_rewritten(with, default_machine);
    const Planning b = planned_as_rewritten(without, default_machine);
    Graph rewritten = a.graph;
    rewritten.ranges.clear();
    with.ranges.clear();
    return a.planned && b.planned && a.planned->schedule == b.planned->schedule && a.planned->program == b.planned->program &&
           rewritten == b.graph && with == without;
}
static_assert(plans_as_without());

// The hash folds the ranges in only when there are any, and sees each part of a mark.
constexpr bool hash_follows_ranges() {
    Graph g{};
    const std::int32_t x = add_value(g, ElemType::F32, Shape{8}, ValueCategory::Input);
    const std::int32_t y = add_node(g, op::Neg, x);
    const std::uint64_t none = hash_graph(g);
    Graph declared_x = g;
    Graph promised_y = g;
    Graph opened = g;
    Graph promised_x = g;
    add_range(declared_x, x, made_of(closed(-1, 1)));
    add_range(promised_y, y, made_of(closed(-1, 1)), true);
    add_range(opened, x, made_of(Interval{-1, 1, false, true}));
    add_range(promised_x, x, made_of(closed(-1, 1)), true);
    const std::uint64_t hashes[] = {none, hash_graph(declared_x), hash_graph(promised_y), hash_graph(opened), hash_graph(promised_x)};
    for (std::size_t i = 0; i < 5; ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (hashes[i] == hashes[j]) {
                return false;
            }
        }
    }
    declared_x.ranges.clear();
    return hash_graph(declared_x) == none && hash_graph(one_pole_ranged()) != hash_graph(one_pole_plain());
}
static_assert(hash_follows_ranges());

// ---- refusals ------------------------------------------------------------------------------------

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
        (void)build();
    } catch (const heinzel::error& e) {
        return e.message().find(text) != std::string_view::npos;
    }
    return false;
}

void bound_refusals() {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    expect(refused([&] { return gt(nan); }, "heinzel: a bound is a number, not NaN") && refused([&] { return ne(nan); }, "not NaN"),
           "a NaN bound");
    expect(refused([] { return le(kInf); }, "heinzel: a bound is a finite number; for no bound on a side, leave that side out") &&
               refused([] { return ge(-kInf); }, "a bound is a finite number") && refused([] { return ne(kInf); }, "a bound is a finite number"),
           "an infinite bound");
    expect(refused([] { return Param{"p", gt(2), lt(1)}; }, "heinzel: a range leaves no value: its comparators contradict, as gt(2), lt(1) do") &&
               refused([] { return Param{"p", ge(1), le(1), ne(1)}; }, "a range leaves no value"),
           "a list that leaves no value");
    expect(refused([] { return Param{"p", ge(0), le(10), ne(1), ne(2), ne(3), ne(4)}; },
                   "heinzel: a range is at most kRangePieces intervals: at most 3 ne inside its bounds") &&
               refused([] { return intersect(detail::stated(ge(0), le(10), ne(1), ne(2), ne(3)), ne(4)); }, "at most kRangePieces intervals") &&
               !refused([] { return Param{"p", ge(0), le(10), ne(1), ne(2), ne(3), ne(20)}; }, "heinzel"),
           "a fifth piece");
}

void promise_refusals() {
    expect(refused([] { Param p{"p"}; return p.promise(ge(0)); }, "heinzel: a port's range goes in its declaration: Param p{\"p\", ge(0), le(1)}") &&
               refused([] { Input x{"x", Signal{8}, ge(-1)}; return x.promise(le(1)); }, "a port's range goes in its declaration") &&
               refused([] { Input x{"x", Signal{8}}; return Expr(x).promise(le(1)); }, "a port's range goes in its declaration"),
           "a promise on a port");
    expect(refused(
               [] {
                   Input x{"x", Signal{8}};
                   const Expr v = (x * 2.0).promise(ge(-2));
                   return v.promise(le(2));
               },
               "heinzel: a value has one promise") &&
               refused([] { Input x{"x", Signal{8}}; return abs(x).promise(gt(0)).promise(le(1)); }, "heinzel: a value has one promise"),
           "a second promise, also on a fact");
    const auto defined = [] {
        Input x{"x", Signal{8}};
        Output y{"y"};
        y[n] = x[n] + 0.5 * y[n - 1];
        return Expr(y).promise(ge(-2), le(2));
    };
    const auto read_const = [] {
        Input x{"x", Signal{8}};
        Output y{"y"};
        y[n] = x[n] + 0.5 * y[n - 1];
        const Output& seen = y;
        return seen[n].promise(ge(-2), le(2));
    };
    const auto undefined = [] {
        Variable v;
        return Expr(v).promise(ge(-2), le(2));
    };
    expect(refused(defined, "heinzel: promise its definition: y[n] = (...).promise(...)") && refused(read_const, "promise its definition") &&
               refused(undefined, "promise its definition"),
           "a promise on a recurrence's variable");
    expect(refused([] { return Output{"#0"}; }, "heinzel: an output's name starting with # is kept for the checks of promises") &&
               refused([] { return Output{"#y", Signal{8}}; }, "starting with # is kept") && !refused([] { return Output{"y#"}; }, "heinzel") &&
               refused([] { Input x{"x", Signal{8}}; return detail::finish("#y", x * 2.0); }, "starting with # is kept"),
           "an output named #, also by finish");
}

} // namespace

int main() {
    // The same functions at run time.
    expect(declared().ranges.size() == 4 && mark_of(declared(), port(declared(), "fc")).range == made_of(Interval{0, 20000, true, false}),
           "the declarations' ranges");
    expect(chained() == in_place() && holes_and_sides().ranges[0].range.count == 2, "promises");
    expect(composed().ranges.size() == 4 && composed().ranges[1].range == made_of(closed(0, 1)) && composed().ranges[3].promised,
           "facts, and a promise on one");
    expect(intersect(made_of(closed(4, 5), closed(0, 1)), Range{}) == made_of(closed(0, 1), closed(4, 5)) && intersect(gt(2), lt(1)).count == 0,
           "intersect");
    expect(merged_marks_hold() && in_a_subgraph().ranges.size() == 3, "marks through merges and a subgraph");
    expect(plans_as_without(), "a graph with marks plans as the same graph without them");
    Graph plain = one_pole_plain();
    expect(infer(plain).ok && compile(one_pole_ranged(), default_machine, {}, false).program ==
                                  std::unique_ptr<const Planning>(new auto(planned_as_rewritten(plain, default_machine)))->planned->program,
           "compile in release, with and without marks");
    expect(hash_follows_ranges(), "the hash");
    bound_refusals();
    promise_refusals();
    if (g_wrong != 0) {
        std::printf("ranges_recorded_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("ranges_recorded_test: each range is recorded as written, and the planner reads none\n");
    return 0;
}
