// Sections in lanes (`rewrite/sections_in_lanes.hpp`, `docs/sections-in-lanes.md`), the merge alone, unpriced
// (`detail::linked_runs`; the price that decides it is `lanes_price_test`'s). A maximal run of k >= 2 like
// feedback cycles, each reading the one before at this sample, becomes one cycle over [k, time], linked:
// `u = _chain(x, V)`, the first section's arithmetic over the sections, the last sliced out.
//
// The oracle, for every graph taken: its walk and the walk of what the rule makes are one, bit for bit, a real signal
// at every output; the links lay the sections pinned; `infer` changes nothing the rule made; a second pass finds
// nothing; and no output's range (`ranges/proven.hpp`) is looser, but where a section's range follows its input's: the
// proof reads a link's operand 1 as any number, so `min(x, 0.5 y[n - 1])` over x in [-1, 1] loses its bound 1.
//
// Taken, on zen2 unless named: Param cascades of 2, 3, 4 and 8 sections as `df1_param` writes them and as `bench_knobs`
// writes its RBJ peaks; constant and Param sections mixed (one Param all read stays, a Param each gathered, a constant
// each a [k, 1] constant, a Param each but one constant gathered with the constant, one number in all stays); the lags
// of `x` the same lags of the link; f32, f64 and f32 into f64 sections (the link f64), and an f32 input read in the
// past by f64 sections (the first stays); Fast; the default machine; 16 sections at four double lanes; saturating
// sections beside an output whose range is its marks'; sections reading one table, in the cycle and in the feed. The
// TPT pair, LR8 and eight tanh sections at Normal, written as k cycles, become the linked graphs, node for node; the
// TPT pair into the eight two links, as built by hand; and the priced Param cascade of 2 and of 8 its merged graph, the
// gather and all, unwidened. The maximal run: two unlike runs one after the other two links; a run broken by a cycle
// reading its middle section two links; a cascade whose first section reads two outside values the rest linked; a
// cascade of three read after its second the first two; the third reading the second at another place the last two; a
// gain before the cascade kept; a first section a constant drives (no voice) kept. Capacities: a graph the merge would
// take past kMaxNodes or kMaxConstants comes back as it was, one just inside is taken; of two runs, the second's merge
// past kMaxConstants, the first is linked and the second stays. And the merge is a constant expression.
//
// Declined, each alone: High; an input of two voices; a coefficient moving every sample (eq8sweep's band); a section
// read in the middle; a cascade inside a larger cycle (eq8fb); unlike cycles (JUCE's ladder); no lanes; 17 sections at
// four double lanes; sections reading the one before only in the past; lags that differ; a first section reading its
// own value where the second reads the first's (three like one-poles beside it linked).
//
// The other difference named: f32 constants that are no floats walk, linked, as rounded to float (a constant tensor's
// numbers are its type's; the walk reads a scalar constant as the double it holds).

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string_view>
#include <vector>

#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/ranges/proven.hpp"
#include "heinzel/rewrite/sections_in_lanes.hpp"
#include "heinzel/schedule/machine.hpp"

#include "interpret.hpp"
#include "same_graph.hpp"
#include "sections_in_lanes_graphs.hpp"

using namespace heinzel;
using lanes::Precision;
namespace ls = heinzel::testing::linked_sections;

namespace {

constexpr std::size_t kSamples = static_cast<std::size_t>(lanes::kLength);
constexpr std::size_t kPlanes = 1024;

// ---- the walk ------------------------------------------------------------------------------------------------------

// What input `in` of `g` is fed, sample-major: noise into `x` and `w`, a Param its number, each rounded to its type.
std::vector<double> fed(const Graph& g, const NamedValue& in) {
    const Value& v = g.values[static_cast<std::size_t>(in.value)];
    const std::string_view name = in.name.view();
    const bool signal = name == "x" || name == "w";
    const double held = signal ? 0.0 : lanes::param_number(name);
    std::vector<double> numbers(kSamples * static_cast<std::size_t>(elements_of(v.shape).count));
    std::uint32_t state = name == "w" ? 7u : 20261024u;
    for (double& number : numbers) {
        state = state * 1664525u + 1013904223u;
        const double noise = static_cast<double>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.001;
        const double raw = signal ? noise : held;
        number = v.type == ElemType::F32 ? static_cast<double>(static_cast<float>(raw)) : raw;
    }
    return numbers;
}

// Every output of `g` walked over `kSamples` samples, element by element.
std::vector<std::vector<double>> walked(const Graph& g) {
    std::vector<std::vector<double>> numbers{};
    Inputs feeds{};
    for (const NamedValue& in : g.inputs) {
        numbers.push_back(fed(g, in));
    }
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        feeds.push_back(InputFeed{numbers[i].data(), elements_of(g.values[static_cast<std::size_t>(g.inputs[i].value)].shape).count});
    }
    const auto walk = std::make_unique<Signals<kSamples, kPlanes>>();
    run(g, feeds, *walk);
    std::vector<std::vector<double>> out{};
    for (const NamedValue& o : g.outputs) {
        out.emplace_back();
        for (std::int32_t e = 0; e < elements_of(g.values[static_cast<std::size_t>(o.value)].shape).count; ++e) {
            for (std::size_t s = 0; s < kSamples; ++s) {
                out.back().push_back(sample_at(*walk, o.value, e, s));
            }
        }
    }
    return out;
}

// One, bit for bit, and a real signal at every output: no NaN, not one number throughout.
bool same_numbers(const std::vector<std::vector<double>>& a, const std::vector<std::vector<double>>& b) {
    bool same = a.size() == b.size();
    for (std::size_t o = 0; o < a.size() && same; ++o) {
        bool moved = false;
        same = a[o].size() == b[o].size();
        for (std::size_t i = 0; i < a[o].size() && same; ++i) {
            same = std::bit_cast<std::uint64_t>(a[o][i]) == std::bit_cast<std::uint64_t>(b[o][i]) && a[o][i] == a[o][i];
            moved = moved || a[o][i] != a[o][0];
        }
        same = same && moved;
    }
    return same;
}

// ---- what the rule made ----------------------------------------------------------------------------------------

// The sections each link lays along its axis, in node order.
std::vector<std::int32_t> links_of(const Graph& g) {
    std::vector<std::int32_t> links{};
    for (const Node& n : g.nodes) {
        if (loops::graph::is_a_link(n)) {
            links.push_back(literal_of(extent_at(g.values[static_cast<std::size_t>(n.output)].shape, 0)));
        }
    }
    return links;
}

// The first link's result.
const Value& link_of(const Graph& g) {
    for (const Node& n : g.nodes) {
        if (loops::graph::is_a_link(n)) {
            return g.values[static_cast<std::size_t>(n.output)];
        }
    }
    fail("rewrite_sections_in_lanes_test: no link");
    return g.values[0];
}

// Each output's range as the proof finds it, in output order.
std::vector<Range> output_ranges(const Graph& g) {
    const auto proof = std::make_unique<ranges::Proof>(ranges::proof(g));
    std::vector<Range> found{};
    for (const NamedValue& o : g.outputs) {
        found.push_back(proof->values[static_cast<std::size_t>(o.value)]);
    }
    return found;
}

bool no_looser(const Graph& before, const Graph& after) {
    const std::vector<Range> was = output_ranges(before);
    const std::vector<Range> is = output_ranges(after);
    bool held = was.size() == is.size();
    for (std::size_t o = 0; o < was.size() && held; ++o) {
        held = ranges::inside(is[o], was[o]);
    }
    return held;
}

// The rule takes `g` on `m`: its links lay `sections` sections each, in node order; the walks are one, bit for bit; the
// rule's values are `infer`'s; a second pass finds nothing; and no output's range is looser (`ranged`: or the looser
// one is named).
bool taken(const char* what, const Graph& g, const std::vector<std::int32_t>& sections, const Machine& m = zen2, bool ranged = true) {
    const auto h = std::make_unique<Graph>(rewrite::detail::linked_runs(g, m));
    const auto again = std::make_unique<Graph>(*h);
    const bool inferred = infer(*again).ok && *again == *h;
    const bool fixed = rewrite::detail::linked_runs(*h, m) == *h;
    const bool linked = links_of(*h) == sections;
    const bool same = same_numbers(walked(g), walked(*h));
    const bool held = no_looser(g, *h) == ranged;
    const bool ok = inferred && fixed && linked && same && held;
    if (!ok) {
        std::printf("sections_in_lanes: %s: infer %d, fixed %d, %zu link(s) (first of %d), walks one %d, ranges %d\n", what, inferred, fixed,
                    links_of(*h).size(), links_of(*h).empty() ? 0 : links_of(*h)[0], same, held);
    }
    return ok;
}

bool declined(const char* what, const Graph& g, const Machine& m = zen2) {
    const auto h = std::make_unique<Graph>(rewrite::detail::linked_runs(g, m));
    const bool ok = *h == g;
    if (!ok) {
        std::printf("sections_in_lanes: %s taken: %zu link(s)\n", what, links_of(*h).size());
    }
    return ok;
}

// ---- the linked and the priced graphs, node for node ------------------------------------------------------------

// The rule takes what `apart` makes (k cycles) to what `linked` makes, node for node, and the oracle holds of it. (The
// graphs made here, in a frame of their own: see `main`.)
template <class Apart, class Linked>
[[gnu::noinline]] bool reproduces(const char* what, Apart apart, Linked linked, const std::vector<std::int32_t>& sections) {
    const Graph before = apart();
    const Graph hand = linked();
    const auto h = std::make_unique<Graph>(rewrite::detail::linked_runs(before, zen2));
    const bool same = heinzel::testing::same_graph(*h, hand);
    if (!same) {
        std::printf("sections_in_lanes: %s: not the graph built by hand (%zu nodes, by hand %zu)\n", what, h->nodes.size(), hand.nodes.size());
    }
    return taken(what, before, sections) && same;
}

[[gnu::noinline]] bool hand_built_reproduced() {
    const auto tpt_apart = [](Graph& g, std::int32_t x) { return ls::cycles_tpt(g, x, ls::kTptGains); };
    const auto tpt_linked = [](Graph& g, std::int32_t x) { return ls::linked_tpt(g, x, ls::kTptGains); };
    const auto lr8_apart = [](Graph& g, std::int32_t x) {
        const auto of_section = [&g](std::int32_t j) { return ls::scalars(g, ls::kLr8[static_cast<std::size_t>(j)]); };
        return ls::cycles_biquads(g, x, of_section, 4);
    };
    const auto lr8_linked = [](Graph& g, std::int32_t x) {
        const ls::Biquad c = ls::constants_per_section(g, ls::kLr8);
        return ls::linked_biquads(g, x, c, 4);
    };
    const auto tanh_apart = [](Graph& g, std::int32_t x) { return ls::cycles_saturating(g, x, ls::kFeedback); };
    const auto tanh_linked = [](Graph& g, std::int32_t x) { return ls::linked_saturating(g, x, ls::kFeedback); };
    const auto series_apart = [&](Graph& g, std::int32_t x) {
        const auto pair = tpt_apart(g, x);
        return tanh_apart(g, pair);
    };
    const auto series_linked = [&](Graph& g, std::int32_t x) {
        const auto pair = tpt_linked(g, x);
        return tanh_linked(g, pair);
    };
    const auto made = [](auto body) { return [body] { return lanes::by_hand(body); }; };
    const auto apart = [](std::size_t k) { return [k] { return lanes::params_apart(k); }; };
    const auto gathered = [](std::size_t k) { return [k] { return lanes::params_linked(k); }; };
    bool ok = reproduces("B2's TPT pair", made(tpt_apart), made(tpt_linked), {2});
    ok = reproduces("B2's LR8", made(lr8_apart), made(lr8_linked), {4}) && ok;
    ok = reproduces("B2's eight tanh sections", made(tanh_apart), made(tanh_linked), {8}) && ok;
    ok = reproduces("B2's TPT pair into the eight", made(series_apart), made(series_linked), {2, 8}) && ok;
    ok = reproduces("B4's two RBJ biquads, gathered by G1", apart(2), gathered(2), {2}) && ok;
    ok = reproduces("B4's eight RBJ biquads, gathered by G1", apart(8), gathered(8), {8}) && ok;
    return ok;
}

// ---- taken -----------------------------------------------------------------------------------------------------

[[gnu::noinline]] bool param_cascades_taken() {
    bool ok = true;
    for (const std::size_t k : {2, 3, 4, 8}) {
        const std::vector<std::int32_t> one{static_cast<std::int32_t>(k)};
        ok = taken("a Param cascade as df1_param writes it", lanes::raw(k), one) && ok;
        ok = taken("a Param cascade as bench_knobs writes it", lanes::knobs(k), one) && ok;
    }
    return ok;
}

// The mixed coefficients as 12 merges them: b0's one Param read as it is, b1's Params and a1's (one a constant)
// gathered by two selects each over two masks, b2's three constants one [3, 1] constant, a2's one number as it is.
[[gnu::noinline]] bool mixed_taken() {
    const Graph g = lanes::mixed();
    const auto h = std::make_unique<Graph>(rewrite::detail::linked_runs(g, zen2));
    const auto named = [&](const NamedValue& io) { return io.name.view() == "g"; };
    const std::int32_t shared = std::find_if(h->inputs.begin(), h->inputs.end(), named)->value;
    std::size_t selects = 0;
    std::size_t pooled = 0;
    bool param = false;
    bool number = false;
    for (const Node& n : h->nodes) {
        const bool over = h->values[static_cast<std::size_t>(n.output)].shape.rank == 2;
        const Value& first = h->values[static_cast<std::size_t>(n.inputs[0] < 0 ? 0 : n.inputs[0])];
        selects += rules_of(n.op).type == TypeRule::FromOperandsExceptFirst ? 1 : 0;
        pooled += rules_of(n.op).attr == AttrKind::Constant ? 1 : 0;
        param = param || (over && n.inputs[0] == shared);
        number = number || (over && first.category == ValueCategory::Const && first.constant == 0.25);
    }
    const bool merged = selects == 4 && pooled == 3 && param && number;
    if (!merged) {
        std::printf("sections_in_lanes: mixed: %zu selects, %zu constant tensors, the Param read %d, the number %d\n", selects, pooled, param, number);
    }
    return taken("constant and Param sections mixed", g, {3}) && merged;
}

// DF1's x[n - 1] and x[n - 2]: the same lags of the link.
[[gnu::noinline]] bool lags_of_the_link() {
    const auto h = std::make_unique<Graph>(rewrite::detail::linked_runs(lanes::raw(3), zen2));
    std::int32_t u = kNoValue;
    std::vector<std::int32_t> back{};
    for (const Node& n : h->nodes) {
        u = loops::graph::is_a_link(n) ? n.output : u;
        if (n.attr.kind == AttrKind::Delay && n.inputs[0] == u) {
            back.push_back(n.attr.delay.samples);
        }
    }
    const bool ok = back == std::vector<std::int32_t>{1, 2};
    if (!ok) {
        std::printf("sections_in_lanes: %zu lags of the link\n", back.size());
    }
    return ok;
}

// The link's type is its operands' (`FromOperands`): f32 sections, f64 ones, an f32 input into f64 sections; and where
// an f32 input is read in the past by f64 sections, the first section's lag is not its successors' type: it stays.
[[gnu::noinline]] bool types_hold() {
    const auto f64_pole = [](Expr s, std::size_t j) { return lanes::one_pole_param(s, j, f64); };
    const auto f64_df1 = [](Expr s, std::size_t j) { return lanes::df1_param(s, j, f64); };
    const Graph narrow = lanes::raw(3);
    const Graph wide = lanes::raw(3, Precision::Normal, f64);
    const Graph into = lanes::series(3, f64_pole);
    const Graph past = lanes::series(3, f64_df1);
    bool ok = taken("f32 sections", narrow, {3}) && taken("f64 sections", wide, {3}) && taken("an f32 input into f64 sections", into, {3}) &&
              taken("an f32 input read in the past by f64 sections", past, {2});
    const ElemType types[4] = {
        link_of(rewrite::detail::linked_runs(narrow, zen2)).type, link_of(rewrite::detail::linked_runs(wide, zen2)).type,
        link_of(rewrite::detail::linked_runs(into, zen2)).type, link_of(rewrite::detail::linked_runs(past, zen2)).type};
    ok = ok && types[0] == ElemType::F32 && types[1] == ElemType::F64 && types[2] == ElemType::F64 && types[3] == ElemType::F64;
    if (!ok) {
        std::printf("sections_in_lanes: the link's types\n");
    }
    return ok;
}

[[gnu::noinline]] bool precision_and_machines() {
    bool ok = declined("High", lanes::raw(3, Precision::High));
    ok = taken("Fast", lanes::raw(3, Precision::Fast), {3}) && ok;
    ok = taken("on the default machine", lanes::raw(3), {3}, default_machine) && ok;
    ok = declined("no lanes", lanes::raw(3), Machine{.vector_bytes = 8}) && ok;
    ok = taken("16 sections at four double lanes", lanes::series(16, lanes::one_pole_constant), {16}) && ok;
    ok = taken("saturating sections beside a ranged output", lanes::saturating_beside_a_knob(), {3}) && ok;
    ok = taken("saturating sections through one table", lanes::saturating_through_a_table(), {3}) && ok;
    ok = declined("17 sections at four double lanes", lanes::series(17, lanes::one_pole_constant)) && ok;
    return ok;
}

[[gnu::noinline]] bool maximal_runs() {
    bool ok = taken("a run broken by a cycle reading its middle section", lanes::broken_in_the_middle(), {2, 2});
    ok = taken("a cascade whose first section reads two outside values", lanes::first_reads_two(), {2}) && ok;
    ok = taken("three sections read after the second", lanes::read_in_the_middle(3, 1), {2}) && ok;
    ok = taken("three sections, the third reading the second at another place", lanes::read_at_other_places(), {2}) && ok;
    ok = taken("a cascade over a gain of its input, the gain kept", lanes::over_a_gain(), {3}) && ok;
    ok = taken("a cascade whose first section a constant drives, the first kept", lanes::driven_by_a_constant(), {2}) && ok;
    return ok;
}

[[gnu::noinline]] bool declines_hold() {
    const auto f32_df1 = [](Expr s, std::size_t j) { return lanes::df1_param(s, j, f32); };
    bool ok = declined("an input of two voices", lanes::series(3, f32_df1, Precision::Normal, f32, 2));
    ok = declined("a coefficient moving every sample", lanes::moving_band()) && ok;
    ok = declined("a section read in the middle", lanes::read_in_the_middle(2, 0)) && ok;
    ok = declined("a cascade inside a larger cycle", lanes::inside_a_loop()) && ok;
    ok = declined("unlike cycles", lanes::ladder()) && ok;
    ok = declined("sections reading the one before only in the past", lanes::only_in_the_past()) && ok;
    ok = declined("sections whose lags differ", lanes::lags_differ()) && ok;
    return ok;
}

// A run's first section reads its input from outside the run. Where the second of a pair reads the first's value, the
// first reads its own `q`, which the pair makes: not alike, so not taken, and three like one-poles beside the pair
// are linked all the same.
[[gnu::noinline]] bool input_from_outside() {
    bool ok = declined("a first section reading its own value where the second reads the first's", lanes::own_value_read());
    ok = taken("three one-poles beside that pair", lanes::one_poles_beside_own_value(), {3}) && ok;
    return ok;
}

// The other difference named: a scalar constant the walk reads as the double it holds, a constant tensor as its type's
// number (as the program reads both, an immediate broadcast to the statement's type). So two f32 one-poles whose poles
// are no floats, linked, hold them as floats in the pool, and walk as the same with their poles rounded to float.
[[gnu::noinline]] bool constants_named() {
    const Graph g = lanes::poles_no_floats(false);
    const Graph twin = lanes::poles_no_floats(true);
    const auto h = std::make_unique<Graph>(rewrite::detail::linked_runs(g, zen2));
    const std::vector<std::vector<double>> linked = walked(*h);
    std::vector<double> poles{};
    for (const Node& n : h->nodes) {
        for (std::int32_t e = 0; rules_of(n.op).attr == AttrKind::Constant && e < numbers_per_sample(n.attr.generator.shape); ++e) {
            poles.push_back(std::bit_cast<double>(h->constants[static_cast<std::size_t>(n.attr.generator.first + e)]));
        }
    }
    const std::vector<double> floats{static_cast<double>(0.3f), static_cast<double>(0.7f)};
    const bool named = links_of(*h) == std::vector<std::int32_t>{2} && poles == floats && same_numbers(linked, walked(twin)) &&
                       !same_numbers(linked, walked(g));
    if (!named) {
        std::printf("sections_in_lanes: poles that are no floats\n");
    }
    return named;
}

// The difference named: the proof reads a link's operand 1 as any number, so sections whose range follows their
// input's lose it, (-inf, 1] before, any number after.
[[gnu::noinline]] bool ranges_named() {
    const Graph g = lanes::minimums(3);
    const Range before = output_ranges(g)[0];
    const Range after = output_ranges(rewrite::detail::linked_runs(g, zen2))[0];
    const bool named = before.count == 1 && before.pieces[0].hi == 1.0 && !ranges::finite(before.pieces[0].lo) && ranges::is_whole(after);
    if (!named) {
        std::printf("sections_in_lanes: minimums' ranges\n");
    }
    return taken("sections whose range follows their input's", g, {3}, zen2, false) && named;
}

// A graph the merge would take past a capacity comes back as it was; one just inside it is taken. Two Param one-poles
// merge into one node more (a mask, two selects, the link, the slice and the reshape for one section's five nodes);
// two constant one-poles add two numbers to the pool, their poles (their gains, one number, stay as they are).
[[gnu::noinline]] bool capacities_hold() {
    const std::size_t pads = kMaxNodes - 10;
    const Graph at = lanes::padded_one_poles(pads);
    const Graph inside = lanes::padded_one_poles(pads - 1);
    const Graph full = lanes::pooled_one_poles(kMaxConstants - 1);
    const Graph room = lanes::pooled_one_poles(kMaxConstants - 2);
    const std::vector<std::int32_t> two{2};
    const bool nodes = at.nodes.size() == kMaxNodes && declined("one node past kMaxNodes", at) &&
                       links_of(rewrite::detail::linked_runs(inside, zen2)) == two;
    const bool pool = declined("one number past kMaxConstants", full) && links_of(rewrite::detail::linked_runs(room, zen2)) == two &&
                      rewrite::detail::linked_runs(room, zen2).constants.size() == kMaxConstants;
    if (!nodes || !pool) {
        std::printf("sections_in_lanes: capacities: nodes %d, the pool %d\n", nodes, pool);
    }
    return nodes && pool;
}

// Each run's merge is counted against the pool the runs before it left: of two runs of two constant one-poles, the pool
// three numbers short of kMaxConstants, the first (over `x`) is linked, the pool then one short, and the second stays
// as written. (Not walked, as the other capacities: the pool's tensor takes more planes than the walk holds.)
[[gnu::noinline]] bool pool_per_run() {
    const Graph g = lanes::pooled_runs(kMaxConstants - 3);
    const auto h = std::make_unique<Graph>(rewrite::detail::linked_runs(g, zen2));
    const auto named = [](const NamedValue& io) { return io.name.view() == "x"; };
    const std::int32_t x = std::find_if(h->inputs.begin(), h->inputs.end(), named)->value;
    bool over_x = false;
    for (const Node& n : h->nodes) {
        over_x = over_x || (loops::graph::is_a_link(n) && n.inputs[0] == x);
    }
    const bool first = links_of(*h) == std::vector<std::int32_t>{2} && over_x && h->constants.size() == kMaxConstants - 1;
    if (!first) {
        std::printf("sections_in_lanes: two runs and the pool: %zu link(s), over x %d, %zu numbers\n", links_of(*h).size(), over_x,
                    h->constants.size());
    }
    return first;
}

// The merge is a constant expression, as `rewritten` needs it: two constant one-poles linked.
constexpr bool linked_while_compiling() {
    const Graph g = lanes::series(2, lanes::one_pole_constant);
    const Graph h = rewrite::detail::linked_runs(g, zen2);
    std::size_t links = 0;
    for (const Node& n : h.nodes) {
        links += loops::graph::is_a_link(n) ? 1 : 0;
    }
    return links == 1 && h.nodes.size() == 9;
}
static_assert(linked_while_compiling(), "two constant one-poles, linked: a [2, 1] constant, the link, five nodes, the slice, the reshape");

} // namespace

// Each check, and `reproduces`, is `[[gnu::noinline]]`: under ASan at -O2 no two graphs share a stack slot, and inlined
// into `main` the checks' graphs made a frame of 12.5 MB, past the 16 MB stack with the rule's frames below it.
int main() {
    try {
        bool ok = param_cascades_taken();
        ok = mixed_taken() && ok;
        ok = lags_of_the_link() && ok;
        ok = types_hold() && ok;
        ok = precision_and_machines() && ok;
        ok = maximal_runs() && ok;
        ok = declines_hold() && ok;
        ok = input_from_outside() && ok;
        ok = ranges_named() && ok;
        ok = constants_named() && ok;
        ok = hand_built_reproduced() && ok;
        ok = capacities_hold() && ok;
        ok = pool_per_run() && ok;
        return ok ? 0 : 1;
    } catch (const heinzel::error& e) {
        std::printf("sections_in_lanes: %.*s\n", static_cast<int>(e.message().size()), e.message().data());
        return 1;
    }
}
