// The proof's walk (Plan/26-value-ranges/03): what it does beyond one operation's rule. A past read adds silence; a
// promise is proven, narrows, or is contradicted, on a constant by comparing it (a proven one narrowing an infinity's
// any number); a promise on a recurrence's definition is proven by induction, also for two recurrences reading each
// other and a delay of three samples, and a biquad's is not; a variable read at its own sample is any number; findings
// that follow from one root are grouped by it; `abs` and `frac` state their range as a fact (Plan/26-value-ranges, item
// 5), `frac`'s closed, and a fact never leaves a value no number. Then the wording, `%g`'s numbers, the monotone rows'
// cores, and that `try_compile` refuses a graph by its first finding (Plan/26-value-ranges/05).
// Checked while compiling and again at run time.
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/ranges/proven.hpp"
#include "heinzel/runtime/compute.hpp"

#include "ranged_graphs.hpp"

using namespace heinzel;
using namespace heinzel::testing;

namespace {

constexpr Index n{};

constexpr ranges::Verdict verdict_of(const Graph& g, std::size_t k) { return ranges::proof(g).promises[k].verdict; }

// ---- past reads -----------------------------------------------------------------------------------------------

constexpr Graph read_back(int which) {
    Input x{"x", Signal{8}, ge(1), le(2)};
    Output y{"y"};
    if (which == 0) {
        y = x[n - 1];
    } else if (which == 1) {
        y = 1.0 / x[n - 1];
    } else {
        y = sqrt(x[n - 1]);
    }
    return analysed(y);
}

// Before the first run the past is silence: x in [1, 2] reads back as 0 or [1, 2], so 1/x[n-1] divides by zero.
constexpr bool silence() {
    return result_of(read_back(0)) == made_of(closed(0, 0), closed(1, 2)) && findings_of(read_back(1)) == 1 &&
           findings_of(read_back(2)) == 0;
}

// ---- promises and induction -------------------------------------------------------------------------------------

constexpr Graph one_pole() {
    Input x{"x", Signal{8}, ge(-1), le(1)};
    Output y{"y"};
    y[n] = (x[n] + 0.5 * y[n - 1]).promise(ge(-2), le(2));
    return analysed(y);
}

// Two recurrences reading each other, one three samples back.
constexpr Graph coupled() {
    Input x{"x", Signal{8}, ge(-1), le(1)};
    Variable a;
    Variable b;
    a[n] = (0.5 * x[n] + 0.25 * b[n - 1]).promise(ge(-1), le(1));
    b[n] = (0.5 * x[n] + 0.25 * a[n - 3]).promise(ge(-1), le(1));
    Output y{"y"};
    y = a[n - 1] + b[n - 1];
    return analysed(y);
}

// A resonant biquad: an interval bound grows by a step (|a1| + |a2| > 1), so no promise is proven by induction.
constexpr Graph biquad() {
    Input x{"x", Signal{8}, ge(-1), le(1)};
    Output y{"y"};
    y[n] = (x[n] + 1.8 * y[n - 1] - 0.9 * y[n - 2]).promise(ge(-4), le(4));
    return analysed(y);
}

constexpr bool induction() {
    const ranges::Proof c = ranges::proof(coupled());
    return verdict_of(one_pole(), 0) == ranges::Verdict::Proven && result_of(one_pole()) == made_of(closed(-2, 2)) &&
           c.promises.size() == 2 && c.promises[0].verdict == ranges::Verdict::Proven && c.promises[1].verdict == ranges::Verdict::Proven &&
           c.findings.empty() && result_of(coupled()) == made_of(closed(-2, 2)) &&
           verdict_of(biquad(), 0) == ranges::Verdict::Narrowed && result_of(biquad()) == made_of(closed(-4, 4));
}

constexpr Graph narrowing() {
    Input x{"x", Signal{8}, ge(-1), le(1)};
    Input a{"a", Signal{8}, ge(0), le(1)};
    Input b{"b", Signal{8}, ge(0), le(1)};
    Output q{"q"};
    Output r{"r"};
    Output s{"s"};
    q = x / (a + b).promise(gt(0), le(2));         // narrowed: a check from step 3 on
    r = x / (a + b + 1.0).promise(gt(0), le(3));   // [1, 3] lies inside: proven
    s = (x - 2.0).promise(gt(0), le(2));           // [-3, -1]: contradicted
    return analysed(finish(q, r, s));
}

constexpr bool promises() {
    const Graph g = narrowing();
    const ranges::Proof p = ranges::proof(g);
    const std::size_t s = static_cast<std::size_t>(g.outputs[2].value);
    return p.promises.size() == 3 && p.promises[0].verdict == ranges::Verdict::Narrowed && p.promises[1].verdict == ranges::Verdict::Proven &&
           p.promises[2].verdict == ranges::Verdict::Contradicted && p.promises[2].value == g.outputs[2].value &&
           p.findings.size() == 1 && p.findings[0].operand == -1 && p.findings[0].range == made_of(closed(-3, -1)) &&
           p.values[s] == ranges::whole() && p.values[static_cast<std::size_t>(g.outputs[1].value)] == made_of(closed(-1, 1));
}

// A promise on a constant is decided by comparing it, as a check would: a NaN or an infinity is any number as a range,
// which the promise narrowed, and no check reads a constant (the review of Plan 26).
constexpr Graph promised_constant(double c, Range promised) {
    Input x{"x", Signal{8}, ge(-1), le(1)};
    Output y{"y"};
    y = x + constant(c).promise(promised);
    return analysed(y);
}

constexpr bool constants_compared() {
    const Range unit = intersect(ge(0), le(1));
    return refusal_of(promised_constant(-kInfinity, unit)).view() == "heinzel: promise(ge(0), le(1)) contradicts the proof: -inf" &&
           refusal_of(promised_constant(std::numeric_limits<double>::quiet_NaN(), ge(0))).view() ==
               "heinzel: promise(ge(0)) contradicts the proof: nan" &&
           refusal_of(promised_constant(2, unit)).view() == "heinzel: promise(ge(0), le(1)) contradicts the proof: 2" &&
           verdict_of(promised_constant(0.5, unit), 0) == ranges::Verdict::Proven &&
           verdict_of(promised_constant(kInfinity, ge(0)), 0) == ranges::Verdict::Proven; // as a check holds +inf in ge(0)
}

// A promise proven on an infinity narrows its any number, so `sqrt` of it is proven, as it was before constants were
// compared; a finite constant stays its number (the review of Plan 26, round 2).
constexpr Graph root_of_promised(double c) {
    Input x{"x", Signal{8}};
    Output y{"y"};
    y = sqrt(constant(c).promise(ge(0)) + x * 0.0);
    return analysed(y);
}

constexpr Range constant_range(const Graph& g, double c) {
    const ranges::Proof p = ranges::proof(g);
    for (std::size_t v = 0; v < g.values.size(); ++v) {
        if (g.values[v].category == ValueCategory::Const && g.values[v].constant == c) {
            return p.values[v];
        }
    }
    return ranges::nothing();
}

constexpr bool proven_constants_narrowed() {
    return findings_of(root_of_promised(kInfinity)) == 0 && verdict_of(root_of_promised(kInfinity), 0) == ranges::Verdict::Proven &&
           constant_range(root_of_promised(kInfinity), kInfinity) == made_of(Interval{0, kInfinity, false, true}) &&
           findings_of(root_of_promised(5)) == 0 && constant_range(root_of_promised(5), 5) == made_of(closed(5, 5));
}

// ---- a variable read at its own sample -------------------------------------------------------------------------

// The variable itself, read where its definition is computed: its promise would assume itself.
constexpr Graph same_sample() {
    Graph g{};
    const std::int32_t x = add_value(g, ElemType::F64, Shape{8}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    add_range(g, x, made_of(closed(1, 2)));
    const std::int32_t state = add_value(g, ElemType::F64, Shape{8}, ValueCategory::Variable);
    OpAttr back{};
    back.kind = AttrKind::Delay;
    back.delay.samples = 1;
    const std::int32_t past = add_node(g, op::FixedDelay, state, kNoValue, kNoValue, back);
    const std::int32_t next = add_node(g, op::Add, x, add_node(g, op::Mul, past, x));
    add_range(g, next, made_of(closed(0, 10)), true);
    OpAttr defines{};
    defines.kind = AttrKind::Recurrence;
    defines.recurrence.defines = state;
    const std::int32_t y = add_node(g, op::Recurrence, next, kNoValue, kNoValue, defines);
    g.outputs.push_back(NamedValue{Name{"y"}, add_node(g, op::Sqrt, add_node(g, op::Add, y, state))});
    return analysed(g);
}

constexpr bool own_sample() {
    const Graph g = same_sample();
    const ranges::Proof p = ranges::proof(g);
    // the past read takes the promise (or silence); the same-sample read is any number, so sqrt may see a negative
    return p.values[static_cast<std::size_t>(g.nodes[0].output)] == made_of(closed(0, 10)) && p.findings.size() == 1 &&
           p.findings[0].range == ranges::whole();
}

// ---- findings grouped by their root --------------------------------------------------------------------------

constexpr Graph rooted() {
    Input x{"x", Signal{8}, ge(-1), le(1)};
    Input u{"u", Signal{8}};
    Output a{"a"};
    Output b{"b"};
    Output c{"c"};
    const Expr root = sqrt(x - 2.0);
    a = 1.0 / root;            // follows from the root: its divisor is any number only because of it
    b = 1.0 / (root + u);      // its own root: u is any number of its own
    c = 1.0 / x;               // another root
    return analysed(finish(a, b, c));
}

constexpr bool roots() {
    const ranges::Proof p = ranges::proof(rooted());
    return p.findings.size() == 4 && p.findings[0].root == 0 && p.findings[1].root == 0 && p.findings[2].root == 2 &&
           p.findings[3].root == 3;
}

// ---- facts -----------------------------------------------------------------------------------------------------
// `frac` is `x - floor(x)`, operands the walk takes alone, so it states the range its result always has as a fact,
// trusted; `abs`, whose rule gives its range, states it too, for a variable's past. A promise on such a value narrows
// the fact: proven where the fact lies inside it.

constexpr Graph magnitudes() {
    Input x{"x", Signal{8}};
    Input u{"u", Signal{8}, ge(-1), le(1)};
    Output r{"r"};
    Output l{"l"};
    Output s{"s"};
    Output m{"m"};
    r = sqrt(abs(x));
    l = log(abs(x) + 1.0);
    s = x / (1.0 + abs(x));
    m = abs(u);
    return analysed(finish(r, l, s, m));
}

// A phase accumulator stepping down: in [0, 1] at every sample with no promise, and so is its past. `frac` of a number
// just below 0 is 1 (`-1e-9f + 1` in float), so a division by 1 - p[n - 1] is refused, and compiles once the definition
// is promised below 1, which a debug build checks (`ranges_checked_test.cpp`).
constexpr Graph phase(bool promised) {
    Input inc{"inc", Signal{8}, ge(-0.5), le(0)};
    Variable p;
    const Expr next = frac(p[n - 1] + inc[n]);
    p[n] = promised ? next.promise(lt(1)) : next;
    Output y{"y"};
    y = 1.0 / (1.0 - p[n - 1]);
    return analysed(y);
}

// A variable defined by `abs`: its past is read by its definition's fact, at least 0, so 1 + m[n - 1] is no zero. The
// rule alone would not reach it: the past is read before the walk reaches the definition (Plan/29 step 1).
constexpr Graph magnitude_past() {
    Input x{"x", Signal{8}};
    Variable m;
    const Expr next = abs(x[n] - 0.5 * m[n - 1]);
    m[n] = next;
    Output y{"y"};
    y = 1.0 / (1.0 + m[n - 1]);
    return analysed(y);
}

// The recurrence's value, its definition and the variable's past read, each `want`.
constexpr bool phase_held(const Graph& g, const Range& want) {
    const ranges::Proof q = ranges::proof(g);
    bool held = true;
    for (const Node& node : g.nodes) {
        const bool past = node.op == op::FixedDelay && g.values[static_cast<std::size_t>(node.inputs[0])].category == ValueCategory::Variable;
        if (node.attr.kind == AttrKind::Recurrence || past) {
            held = held && q.values[static_cast<std::size_t>(node.output)] == want;
        }
        if (node.attr.kind == AttrKind::Recurrence) {
            held = held && q.values[static_cast<std::size_t>(node.inputs[0])] == want;
        }
    }
    return held;
}

constexpr Graph promised_facts(bool contradicted) {
    Input x{"x", Signal{8}, ge(-1), le(1)};
    Output a{"a"};
    Output b{"b"};
    Output c{"c"};
    a = abs(x).promise(contradicted ? lt(0) : le(1)); // [0, 1] lies inside le(1): proven
    b = 1.0 / abs(x).promise(gt(0));                  // narrowed to (0, 1]: checked
    c = frac(x).promise(lt(0.5));                     // [0, 1] narrowed to [0, 0.5): checked
    return analysed(finish(a, b, c));
}

constexpr bool facts() {
    const Graph g = magnitudes();
    const ranges::Proof p = ranges::proof(g);
    const ranges::Proof f = ranges::proof(promised_facts(false));
    return p.findings.empty() && p.values[static_cast<std::size_t>(g.outputs[3].value)] == made_of(closed(0, 1)) &&
           phase_held(phase(false), made_of(closed(0, 1))) && phase_held(phase(true), made_of(closed_open(0, 1))) &&
           findings_of(magnitude_past()) == 0 &&
           refusal_of(phase(false)).view() == "heinzel: / may divide by zero: the divisor is ge(0), le(1)" && findings_of(phase(true)) == 0 &&
           verdict_of(phase(true), 0) == ranges::Verdict::Narrowed && f.findings.empty() && f.promises.size() == 3 &&
           f.promises[0].verdict == ranges::Verdict::Proven && f.promises[1].verdict == ranges::Verdict::Narrowed &&
           f.promises[2].verdict == ranges::Verdict::Narrowed &&
           f.values[static_cast<std::size_t>(promised_facts(false).outputs[2].value)] == made_of(closed_open(0, 0.5)) &&
           refusal_of(promised_facts(true)).view() == "heinzel: promise(lt(0)) contradicts the proof: ge(0), le(1)";
}

// The review's cases (`Plan/probes/review_p26/frac_fact.cpp`): for x just below 0, x - floor(x) rounds to 1, which the
// open fact [0, 1) met nowhere. It left no number, inside every domain and every promise, or lost the piece near 1.
constexpr Graph just_below_zero(int which) {
    Input x{"x", Signal{8}, f64, ge(-1e-17), le(-1e-18)};
    Output y{"y"};
    if (which == 0) {
        y = sqrt(frac(x) - 2.0);
    } else if (which == 1) {
        y = frac(x).promise(ge(0), le(0.5)) * 2.0;
    } else {
        Input b{"b", Signal{8}, f64, ge(0.25), le(0.5)};
        Input c{"c", Signal{8}, f64, ge(0), le(1)};
        y = sqrt(0.6 - frac(where(c > constant(0.5), x, b)));
    }
    return analysed(y);
}

// A fact the rules contradict, hand-built: -x for x in [1, 2], marked [5, 6]. The walk keeps the computed [-2, -1].
constexpr Graph contradicted_fact() {
    Graph g{};
    const std::int32_t x = add_value(g, ElemType::F64, Shape{8}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    add_range(g, x, made_of(closed(1, 2)));
    const std::int32_t minus = add_node(g, op::Neg, x);
    add_range(g, minus, made_of(closed(5, 6)));
    g.outputs.push_back(NamedValue{Name{"y"}, add_node(g, op::Sqrt, minus)});
    return analysed(g);
}

constexpr bool facts_never_empty() {
    return refusal_of(just_below_zero(0)).view() == "heinzel: sqrt may see a number below 0: its operand is -1" &&
           refusal_of(just_below_zero(1)).view() == "heinzel: promise(ge(0), le(0.5)) contradicts the proof: 1" &&
           refusal_of(just_below_zero(2)).view() == "heinzel: sqrt may see a number below 0: its operand is -0.4 or ge(0.1), le(0.35)" &&
           refusal_of(contradicted_fact()).view() == "heinzel: sqrt may see a number below 0: its operand is ge(-2), le(-1)";
}

// ---- a port of two marks -------------------------------------------------------------------------------------
// Hand-built: the proof and what a debug build checks read the same mark, the first (the review of Plan 26).

constexpr Graph marked_twice() {
    Graph g{};
    const std::int32_t x = add_value(g, ElemType::F64, Shape{8}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    add_range(g, x, made_of(closed(1, 2)));
    add_range(g, x, made_of(closed(-1, 1)));
    g.outputs.push_back(NamedValue{Name{"y"}, add_node(g, op::Sqrt, x)});
    return analysed(g);
}

constexpr bool first_mark() {
    Graph g = marked_twice();
    heinzel::detail::StatedRanges stated{};
    const bool proven = heinzel::detail::take_ranges(g, true, stated).ok;
    return proven && stated.inputs.size() == 1 && stated.inputs[0] == made_of(closed(1, 2)) &&
           ranges::proof(marked_twice()).values[static_cast<std::size_t>(marked_twice().inputs[0].value)] == made_of(closed(1, 2));
}

// ---- a port promised -------------------------------------------------------------------------------------------
// Hand-built, since the frontend refuses it: the proof refuses it in the frontend's words, where it narrowed by it and
// no check read it, so that `log(x)` was proven unchecked (the review of Plan 26, round 2).

Graph promised_port() {
    Graph g{};
    const std::int32_t x = add_value(g, ElemType::F64, Shape{8}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    add_range(g, x, made_of(Interval{0, kInfinity, true, true}), true);
    g.outputs.push_back(NamedValue{Name{"y"}, add_node(g, op::Log, x)});
    return analysed(g);
}

bool port_promised() {
    try {
        Graph g = promised_port();
        heinzel::detail::StatedRanges stated{};
        (void)heinzel::detail::take_ranges(g, true, stated);
    } catch (const heinzel::error& e) {
        return e.message() == kPromiseOnAPort && e.message() == "heinzel: a port's range goes in its declaration: Param p{\"p\", ge(0), le(1)}";
    }
    return false;
}

// ---- wording -------------------------------------------------------------------------------------------------

constexpr bool says(const Graph& g, std::string_view text) { return refusal_of(g).view() == text; }

constexpr std::string_view written(const Range& r, fixed_string<kMaxMessageLength>& into) {
    MessageWriter out;
    ranges::write_range(out, r);
    into = out.problem().message;
    return into.view();
}

constexpr bool range_text(const Range& r, std::string_view text) {
    fixed_string<kMaxMessageLength> into{};
    return written(r, into) == text;
}

constexpr bool wording() {
    const Range unit = made_of(closed(-1, 1));
    return says(analysed([]() -> Graph {
               Input x{"x", Signal{8}, ge(-1), le(1)};
               Input a{"a", Signal{8}, ge(0), le(1)};
               Input b{"b", Signal{8}, ge(0), le(1)};
               Output q{"q"};
               q = x / (a + b);
               return q;
           }()), "heinzel: / may divide by zero: the divisor is ge(0), le(2)") &&
           says(analysed([]() -> Graph {
               Input x{"x", Signal{8}, ge(-1), le(1)};
               Output s{"s"};
               s = (x - 2.0).promise(gt(0), le(2));
               return s;
           }()), "heinzel: promise(gt(0), le(2)) contradicts the proof: ge(-3), le(-1)") &&
           says(one_operation(op::Sqrt, unit), "heinzel: sqrt may see a number below 0: its operand is ge(-1), le(1)") &&
           says(one_operation(op::Log, made_of(closed(0, 1))), "heinzel: log may see a number <= 0: its operand is ge(0), le(1)") &&
           says(one_operation(op::Asin, made_of(closed(-2, 2))),
                "heinzel: _asin may see a number outside ge(-1), le(1): its operand is ge(-2), le(2)") &&
           says(one_operation(op::Tan, made_of(closed(1, 2))), "heinzel: tan may reach a pole: its operand is ge(1), le(2)") &&
           says(one_operation(op::Atan2, unit, unit), "heinzel: _atan2 may see 0 and 0: its operands are ge(-1), le(1) and ge(-1), le(1)") &&
           says(one_operation(op::Pow, made_of(closed(0, 1)), unit),
                "heinzel: pow may see a base <= 0 with a negative exponent: the base is ge(0), le(1)") &&
           says(one_operation(op::Pow, unit, made_of(closed(1, 2))), "heinzel: pow may see a negative base: the base is ge(-1), le(1)");
}

constexpr bool ranges_as_written() {
    return range_text(ranges::whole(), "any number") && range_text(ranges::nothing(), "no number") &&
           range_text(ne(0), "ne(0)") && range_text(gt(0), "gt(0)") && range_text(lt(0.5), "lt(0.5)") &&
           range_text(intersect(ge(-1), intersect(le(1), ne(0))), "ge(-1), le(1), ne(0)") &&
           range_text(made_of(Interval{-kInfinity, -1, true, false}, Interval{1, kInfinity, false, true}), "le(-1) or ge(1)") &&
           range_text(intersect(intersect(ge(0), le(10)), intersect(ne(1), ne(2))), "ge(0), le(10), ne(1), ne(2)") &&
           range_text(made_of(closed_open(0, 1), open_closed(2, 3)), "ge(0), lt(1) or gt(2), le(3)") &&
           range_text(made_of(closed(1e-6, 20000), closed(1e30, 1e30)), "ge(1e-06), le(20000) or 1e+30") &&
           range_text(made_of(closed(0, 0), closed(1, 2)), "0 or ge(1), le(2)") && range_text(made_of(closed(-1.5, -1.5)), "-1.5") &&
           range_text(made_of(closed(-1, 0), closed(1, 1), closed(2, 3)), "ge(-1), le(0) or 1 or ge(2), le(3)");
}

// `%g`'s six significant digits, beside the C library's own at run time.
constexpr double kNumbers[] = {0.1, 0.5, 1, 20000, 123456, 999999, 999999.5, 1e6, 1234567, 1e30, -1e-5, 1e-4, 0.00012345678,
                               3.14159265358979, -2.5, 1e-310, 5e-324, 1.7976931348623157e308, 100, 0.3};

constexpr bool numbers_written() {
    MessageWriter out;
    out << 0.1 << std::string_view{" "} << -0.0 << std::string_view{" "} << 1e30f << std::string_view{" "}
        << std::numeric_limits<double>::infinity() << std::string_view{" "} << -std::numeric_limits<double>::infinity()
        << std::string_view{" "} << std::numeric_limits<double>::quiet_NaN() << std::string_view{" "} << std::int64_t{-7};
    return out.problem().message.view() == "0.1 -0 1e+30 inf -inf nan -7";
}

// ---- the monotone rows' cores ------------------------------------------------------------------------------------

// Every row a monotone rule computes with has a writing: evaluated once under constant evaluation.
constexpr bool every_monotone_row_is_written() {
    for (std::uint8_t i = 0; i < kOpCount; ++i) {
        const RangeRule r = kOpTable[i].rules.range;
        if (r == RangeRule::Increasing || r == RangeRule::StrictlyIncreasing || r == RangeRule::StrictlyDecreasing ||
            r == RangeRule::EachIncreasing) {
            (void)compute_as<double>(OpCode{i}, FnAccuracy::Exact, 0.5, 0.25);
        }
    }
    return true;
}

constexpr double at(OpCode op, double x, double y = 0) { return compute_as<double>(op, FnAccuracy::Exact, x, y); }

// At the infinities, at a domain's edges and at the zeros, each core gives its limit exactly.
constexpr bool cores_at_their_edges() {
    constexpr double inf = std::numeric_limits<double>::infinity();
    return at(op::Exp, -inf) == 0 && at(op::Exp, inf) == inf && at(op::Exp, 0) == 1 && at(op::Log, 0) == -inf && at(op::Log, 1) == 0 &&
           at(op::Log, inf) == inf && at(op::Sqrt, 0) == 0 && at(op::Sqrt, inf) == inf && at(op::Rsqrt, 0) == inf && at(op::Rsqrt, inf) == 0 &&
           at(op::Tanh, 0) == 0 && at(op::Tanh, inf) == 1 && at(op::Tanh, -inf) == -1 && at(op::Floor, -inf) == -inf && at(op::Floor, inf) == inf &&
           at(op::Ceil, -inf) == -inf && at(op::Ceil, inf) == inf && at(op::Round, -inf) == -inf && at(op::Round, inf) == inf &&
           at(op::Trunc, -inf) == -inf && at(op::Trunc, inf) == inf && at(op::Sign, -inf) == -1 && at(op::Sign, inf) == 1 && at(op::Sign, 0) == 0 &&
           at(op::Min, -inf, 1) == -inf && at(op::Max, inf, 1) == inf;
}

static_assert(silence());
static_assert(induction());
static_assert(promises());
static_assert(constants_compared());
static_assert(proven_constants_narrowed());
static_assert(own_sample());
static_assert(roots());
static_assert(facts());
static_assert(facts_never_empty());
static_assert(first_mark());
static_assert(wording());
static_assert(ranges_as_written());
static_assert(numbers_written());
static_assert(every_monotone_row_is_written());
static_assert(cores_at_their_edges());

int g_wrong = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

void numbers_as_the_c_library_writes_them() {
    for (const double v : kNumbers) {
        for (const double sign : {1.0, -1.0}) {
            MessageWriter out;
            out << sign * v;
            char c[32]{};
            std::snprintf(c, sizeof c, "%g", sign * v);
            if (out.problem().message.view() != std::string_view{c}) {
                ++g_wrong;
                std::printf("  FAILED: %g written as %s\n", sign * v, out.problem().message.view().data());
            }
        }
    }
}

// ---- proven, so they compile --------------------------------------------------------------------------------
// An open bound proves 1/c; a hole proves 1/d and 1/(1/d); a feedback value proven by induction is read by a division.
// The graphs the proof refuses are in `gates/refusals/proof.cpp`.

constexpr Graph open_bound() {
    Input c{"c", Signal{8}, gt(0), le(2)};
    Output y{"y"};
    y = 1.0 / c;
    return y;
}

constexpr Graph holed() {
    Input d{"d", Signal{8}, ge(-1), le(1), ne(0)};
    Output y{"y"};
    y = 1.0 / (1.0 / d);
    return y;
}

constexpr Graph feedback_read() {
    Input x{"x", Signal{8}, ge(-1), le(1)};
    Variable v;
    v[n] = (x[n] + 0.5 * v[n - 1]).promise(ge(-2), le(2));
    Output y{"y"};
    y = 1.0 / (3.0 + v[n - 1]);
    return y;
}

// Each compiled into an Executor and run once: 1/c, d itself, and 1/(3 + v[n-1]) with v the one-pole of x.
void proven_compile() {
    std::vector<float> c{0.5f, 1, 2, 0.25f, 1, 1, 1, 1};
    std::vector<float> d{-1, -0.5f, 0.5f, 1, -1, 1, 0.25f, -0.25f};
    std::vector<float> x(8, 1.0f);
    std::vector<float> y(8);
    Executor<open_bound> a;
    a.input("c") = c.data(), a.output("y") = y.data();
    a.run();
    expect(y[0] == 2.0f && y[2] == 0.5f && y[3] == 4.0f, "an open bound proves 1/c");
    Executor<holed> b;
    b.input("d") = d.data(), b.output("y") = y.data();
    b.run();
    expect(y == d, "a hole proves 1/d and 1/(1/d)");
    Executor<feedback_read> f;
    f.input("x") = x.data(), f.output("y") = y.data();
    f.run();
    expect(y[0] == 1.0f / 3.0f && y[1] == 0.25f && y[2] == 1.0f / 4.5f, "a feedback value proven by induction, read by a division");
}

// Strict: a graph with a finding is refused by its first finding's words, in a debug and a release build alike.
void strict() {
    const auto divided = []() -> Graph {
        Input x{"x", Signal{8}, ge(-1), le(1)};
        Input a{"a", Signal{8}, ge(0), le(1)};
        Input b{"b", Signal{8}, ge(0), le(1)};
        Output q{"q"};
        q = x / (a + b);
        return q;
    };
    const fixed_string<kMaxMessageLength> checked = try_compile(divided(), default_machine, Bindings{}, true).refusal;
    const fixed_string<kMaxMessageLength> unchecked = try_compile(divided(), default_machine, Bindings{}, false).refusal;
    expect(checked.view() == "heinzel: / may divide by zero: the divisor is ge(0), le(2)" && unchecked == checked &&
               ranges::refusal(analysed(divided())).message == checked,
           "a finding refuses the graph");
}

} // namespace

int main() {
    expect(silence(), "a past read adds silence");
    expect(induction(), "induction");
    expect(promises(), "promises proven, narrowing, contradicted");
    expect(constants_compared(), "a promise on a constant, compared");
    expect(proven_constants_narrowed(), "a promise proven on an infinity narrows it");
    expect(own_sample(), "a variable at its own sample");
    expect(roots(), "findings grouped by their root");
    expect(facts(), "facts: abs and frac, and promises on them");
    expect(facts_never_empty(), "a fact never leaves a value no number");
    expect(first_mark(), "a port's first mark, in the proof and in what a debug build checks");
    expect(port_promised(), "a promise on a port, hand-built, refused by the frontend's words");
    expect(wording(), "wording");
    expect(ranges_as_written(), "ranges as written");
    expect(numbers_written(), "numbers written");
    expect(every_monotone_row_is_written() && cores_at_their_edges(), "the monotone cores");
    numbers_as_the_c_library_writes_them();
    proven_compile();
    strict();
    if (g_wrong != 0) {
        std::printf("ranges_walk_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("ranges_walk_test: silence, promises, induction and roots as 03 says, worded as the user writes ranges\n");
    return 0;
}
