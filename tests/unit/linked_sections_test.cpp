// The walk of sections linked in lanes (`docs/sections-in-lanes.md`). A link `u = _chain(x, V)` lays k sections along
// axis 0 (element 0 of `u` is `x`, element j is `V`'s element j - 1, both at this sample), and the walk takes each
// sample of a graph holding one in passes, pass p settling section p (`tests/reference/link_element.hpp`).
//
// Each linked graph of `support/linked_sections.hpp` is walked at `High` over 4096 samples of noise and gives, bit for
// bit, the numbers of its k-cycle graph, built apart with no link:
//   - a TPT pair (k = 2);
//   - LR8 as four RBJ biquads (k = 4, the poles repeated);
//   - five RBJ peaks whose coefficients are [5, 1] tensor inputs;
//   - eight tanh(... + 0.1) sections (k = 8): silence is not mapped to silence, so a section read before it settles
//     gives a number of its own;
//   - the TPT pair into the eight: two links in series, which take 2 + 8 - 1 passes, more than their largest k.
// The passes each graph takes are pinned (one for a graph with no link), and the link's three refusals by name. And
// walking a node again advances nothing: each walked graph of the both-ways corpus, a link set beside it, gives every
// number of every one of its values again.
//
// The facts: a link's operand 1 is read through to its variable's definition, as a lag's operand is, and closes a
// cycle there. Each graph with one link holds one cycle, over [k, time], where its k-cycle graph holds k; the pair into
// the eight holds two, the pair's planned first. The planner runs each link's stage skewed (the
// schedules pinned in `loops_plan_skewed_test`). A link of anything but the variable its own cycle defines is refused by
// name; one inside a larger cycle is refused there, or found in it and its stage refused by name: no skew runs a fold of
// the sections in their own cycle.

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/frontend/declarations.hpp"
#include "heinzel/ir/timing.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/planner/plan.hpp"
#include "heinzel/schedule/machine.hpp"
#include "heinzel/sema/analyze.hpp"

#include "both_ways.hpp"
#include "hand_built_graph.hpp"
#include "interpret.hpp"
#include "linked_sections.hpp"

using namespace heinzel;
using namespace heinzel::testing::linked_sections;

namespace {

constexpr std::size_t kSamples = static_cast<std::size_t>(kLength);
constexpr std::size_t kPlanes = 256;

// ---- the walk ------------------------------------------------------------------------------------------------------

std::vector<double> noise() {
    std::vector<double> x(kSamples);
    std::uint32_t state = 20261024u;
    for (double& v : x) {
        state = state * 1664525u + 1013904223u;
        v = static_cast<double>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.001;
    }
    return x;
}

// What input `name` is fed: noise into `x`, and a peak input its five numbers at every sample, sample-major.
std::vector<double> fed_to(const Name& name) {
    for (std::size_t i = 0; i < kPeakInputs.size(); ++i) {
        if (name == kPeakInputs[i]) {
            std::vector<double> repeated{};
            for (std::size_t n = 0; n < kSamples; ++n) {
                for (const Coefficients& peak : kPeaks) {
                    repeated.push_back(peak[i]);
                }
            }
            return repeated;
        }
    }
    return noise();
}

std::vector<double> output_of(const Graph& g, const Signals<kSamples, kPlanes>& walk) {
    std::vector<double> y(kSamples);
    for (std::size_t n = 0; n < kSamples; ++n) {
        y[n] = sample_at(walk, g.outputs[0].value, 0, n);
    }
    return y;
}

// `y`, walked by the `run` of many inputs.
std::vector<double> walked(const Graph& g) {
    std::vector<std::vector<double>> fed{};
    for (const NamedValue& in : g.inputs) {
        fed.push_back(fed_to(in.name));
    }
    Inputs feeds{};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        const std::int32_t width = elements_of(g.values[static_cast<std::size_t>(g.inputs[i].value)].shape).count;
        feeds.push_back(InputFeed{fed[i].data(), width});
    }
    const auto walk = std::make_unique<Signals<kSamples, kPlanes>>();
    run(g, feeds, *walk);
    return output_of(g, *walk);
}

// And by the `run` of one input of one number a sample.
std::vector<double> walked_alone(const Graph& g) {
    const std::vector<double> x = noise();
    std::array<double, kSamples> block{};
    for (std::size_t n = 0; n < kSamples; ++n) {
        block[n] = x[n];
    }
    const auto walk = std::make_unique<Signals<kSamples, kPlanes>>();
    run(g, block, *walk);
    return output_of(g, *walk);
}

// The first sample at which the two differ in a bit, or `kSamples`.
std::size_t first_difference(const std::vector<double>& a, const std::vector<double>& b) {
    for (std::size_t n = 0; n < kSamples; ++n) {
        if (std::bit_cast<std::uint64_t>(a[n]) != std::bit_cast<std::uint64_t>(b[n])) {
            return n;
        }
    }
    return kSamples;
}

// A real signal: no NaN, and not one number throughout (two graphs that never move agree about anything).
bool moves(const std::vector<double>& s) {
    bool moved = false;
    for (const double v : s) {
        if (v != v) {
            return false;
        }
        moved = moved || v != s[0];
    }
    return moved;
}

// The linked graph, walked by both `run`s where it has one input, gives its k-cycle graph's numbers bit for bit, a real
// signal, in `passes` passes a sample; the k-cycle graph takes one.
bool holds(const char* what, const Graph& linked, const Graph& cycles, std::size_t passes) {
    const std::vector<double> want = walked(cycles);
    const std::size_t wide = first_difference(walked(linked), want);
    const std::size_t alone = linked.inputs.size() == 1 ? first_difference(walked_alone(linked), want) : kSamples;
    const bool ok = moves(want) && wide == kSamples && alone == kSamples && passes_of(linked) == passes &&
                    passes_of(cycles) == 1;
    if (!ok) {
        std::printf("linked_sections: %s: moves %d, first difference %zu (one input: %zu), passes %zu and %zu\n", what,
                    moves(want), wide, alone, passes_of(linked), passes_of(cycles));
    }
    return ok;
}

// ---- walked again --------------------------------------------------------------------------------------------------
//
// A link anywhere in a graph walks all of its nodes again each sample. With a link of four sections set beside it,
// each walked graph of the both-ways corpus (every operation the walk runs, lags, a delay line, a recurrence, a link
// among them) gives every number of every one of its values again, bit for bit: walking a node again advances nothing.

// `g` with a link of four sections beside it, closed by a variable of its own: three passes more.
constexpr Graph beside_a_link(Graph g) {
    const auto sections = variable(g, Shape{4, testing::kBlock});
    const auto in = testing::constant(g, 0.25);
    const auto u = add_node(g, op::Chain, in, sections);
    const auto half = testing::constant(g, 0.5);
    const auto next = add_node(g, op::Mul, u, half);
    settle(g, next, sections);
    return g;
}

constexpr std::size_t kCorpusSamples = static_cast<std::size_t>(testing::kBlock);

std::unique_ptr<Signals<kCorpusSamples>> walked_at_high(Graph g) {
    set_default_precision(g, numerics::Precision::High);
    if (const Diagnostic d = infer(g); !d.ok) {
        fail(d.message.view());
    }
    const std::vector<double> x = noise();
    std::array<double, kCorpusSamples> block{};
    for (std::size_t n = 0; n < kCorpusSamples; ++n) {
        block[n] = x[n];
    }
    auto walk = std::make_unique<Signals<kCorpusSamples>>();
    run(g, block, *walk);
    return walk;
}

// Whether a number of value `v`, `width` numbers a sample, differs in a bit between the two walks.
bool moved(const Signals<kCorpusSamples>& once, const Signals<kCorpusSamples>& again, std::int32_t v,
           std::int32_t width) {
    for (std::int32_t e = 0; e < width; ++e) {
        for (std::size_t n = 0; n < kCorpusSamples; ++n) {
            const double first = sample_at(once, v, e, n);
            const double second = sample_at(again, v, e, n);
            if (std::bit_cast<std::uint64_t>(first) != std::bit_cast<std::uint64_t>(second)) {
                return true;
            }
        }
    }
    return false;
}

// Every entry but the spectral one, whose `_fft` the walk does not run.
bool walked_again_holds() {
    bool ok = true;
    std::size_t walked = 0;
    for (std::size_t i = 0; i < testing::kCorpusSize; ++i) {
        const Graph g = testing::corpus_graph(i);
        if (!testing::is_runnable(g)) {
            continue;
        }
        ++walked;
        const auto once = walked_at_high(g);
        const auto again = walked_at_high(beside_a_link(g));
        for (std::size_t v = 0; v < g.values.size(); ++v) {
            if (moved(*once, *again, static_cast<std::int32_t>(v), elements_of(g.values[v].shape).count)) {
                std::printf("linked_sections: corpus entry %zu walked again: value %zu moved\n", i, v);
                ok = false;
            }
        }
    }
    if (walked != testing::kCorpusSize - 1) {
        std::printf("linked_sections: %zu corpus entries walked again\n", walked);
        ok = false;
    }
    return ok;
}

// ---- the refusals --------------------------------------------------------------------------------------------------

// The message a link over these shapes is refused with, walked; its row accepts each (it broadcasts as any elementwise
// operation does), and its meaning says nothing about them.
std::string refused(Shape x, Shape before, Shape result) {
    Graph g{};
    const auto in = add_value(g, ElemType::F64, x, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, in});
    const auto v = variable(g, before);
    const auto u = add_node(g, op::Chain, in, v);
    g.values[static_cast<std::size_t>(u)].type = ElemType::F64;
    g.values[static_cast<std::size_t>(u)].shape = result;
    const std::int32_t width = elements_of(x).count;
    const std::vector<double> silence(static_cast<std::size_t>(width) * 4, 0.0);
    Inputs feeds{};
    feeds.push_back(InputFeed{silence.data(), width});
    const auto walk = std::make_unique<Signals<4, 64>>();
    try {
        run(g, feeds, *walk);
    } catch (const heinzel::error& e) {
        return std::string(e.message());
    }
    return "";
}

bool refusals_hold() {
    const bool ok =
        refused(Shape{4}, Shape{4}, Shape{4}) ==
            "heinzel::linked_element: a link's result has no element axis to lay sections along" &&
        refused(Shape{2, 4}, Shape{3, 1, 4}, Shape{3, 2, 4}) ==
            "heinzel::linked_element: a link's operand 1 is not laid out as its result" &&
        refused(Shape{2, 4}, Shape{2, 4}, Shape{2, 4}) ==
            "heinzel::linked_element: a link's operand 0 is more than one section's worth" &&
        refused(Shape{4}, Shape{2, 4}, Shape{2, 4}).empty() && refused(Shape{1, 4}, Shape{3, 4}, Shape{3, 4}).empty();
    if (!ok) {
        std::printf("linked_sections: a link's refusals\n");
    }
    return ok;
}

// ---- the facts ------------------------------------------------------------------------------------------------------

inline constexpr std::string_view kFoldedInTheCycle = "heinzel::compile: sections linked in lanes run skewed, so no fold over them runs in their cycle";
inline constexpr std::string_view kNoCycleOfItsOwn = "heinzel::compile: a link's operand 1 is not the variable its own cycle defines";

// Each link reads through its operand 1 to that variable's definition, made after it, as a lag reads its operand.
bool links_read_their_definitions(const Graph& g) {
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    bool ok = facts.why_not.empty();
    std::size_t links = 0;
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        if (!loops::graph::is_a_link(g.nodes[i])) {
            continue;
        }
        ++links;
        const std::int32_t v = g.nodes[i].inputs[1];
        const std::int32_t maker = facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(v)])];
        const Node* definition = maker >= 0 ? &g.nodes[static_cast<std::size_t>(maker)] : nullptr;
        ok = ok && maker > static_cast<std::int32_t>(i) && definition->attr.kind == AttrKind::Recurrence &&
             definition->attr.recurrence.defines == v;
    }
    return ok && links > 0;
}

// The cycles the feedback facts plan, in the order they plan them, and whether that order is one: every node once, each
// after the makers of what it reads at this sample, and each cycle's nodes one after the other.
struct CyclesFound {
    std::string why_not{};
    std::vector<std::vector<std::int32_t>> cycles{}; // each cycle's nodes, ascending
    bool in_order{true};
};

CyclesFound cycles_found(const Graph& g) {
    const loops::graph::Facts facts = loops::graph::facts_of(g);
    const loops::graph::Feedback feedback = loops::graph::find_feedback(g, facts);
    CyclesFound found{.why_not = std::string(feedback.why_not)};
    std::vector<std::int32_t> planned_at(g.nodes.size(), -1);
    std::vector<char> seen(g.nodes.size(), 0); // per cycle, by the node that names it: begun
    found.in_order = feedback.order.size() == g.nodes.size();
    std::int32_t open = -1;
    for (std::size_t p = 0; p < feedback.order.size(); ++p) {
        const auto i = static_cast<std::size_t>(feedback.order[p]);
        const std::int32_t cycle = feedback.cycle[i];
        found.in_order = found.in_order && planned_at[i] < 0 && (cycle == open || cycle < 0 || seen[static_cast<std::size_t>(cycle)] == 0);
        planned_at[i] = static_cast<std::int32_t>(p);
        if (cycle >= 0 && cycle != open) {
            seen[static_cast<std::size_t>(cycle)] = 1;
            found.cycles.emplace_back();
        }
        if (cycle >= 0) {
            found.cycles.back().push_back(static_cast<std::int32_t>(i));
        }
        open = cycle;
        for (std::size_t k = 0; k < rules_of(g.nodes[i].op).arity; ++k) {
            const std::int32_t read = g.nodes[i].inputs[k];
            const std::int32_t maker = facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(read)])];
            const bool now = !operand_comes_from_the_past(g.nodes[i], k);
            const bool earlier = maker < 0 || (maker != static_cast<std::int32_t>(i) && planned_at[static_cast<std::size_t>(maker)] >= 0);
            found.in_order = found.in_order && (!now || earlier);
        }
    }
    for (std::vector<std::int32_t>& cycle : found.cycles) {
        std::sort(cycle.begin(), cycle.end());
    }
    return found;
}

// The nodes that make a value over `over`, ascending.
std::vector<std::int32_t> nodes_over(const Graph& g, Shape over) {
    std::vector<std::int32_t> nodes{};
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        if (g.values[static_cast<std::size_t>(g.nodes[i].output)].shape == over) {
            nodes.push_back(static_cast<std::int32_t>(i));
        }
    }
    return nodes;
}

std::string refusal_on(const Graph& g, const Machine& m) {
    const std::unique_ptr<const CompileResult> compiled(new auto(try_compile(g, m)));
    return std::string(compiled->refusal.view());
}

// Compiling it is refused by name, on the default machine and on zen2.
bool refused_by_name(const Graph& g, std::string_view why) {
    return refusal_on(g, default_machine) == why && refusal_on(g, zen2) == why;
}

// The planner's schedule on the default machine and zen2 holds `links` skewed stages.
bool planned_skewed(const Graph& g, std::size_t links) {
    bool ok = true;
    for (const Machine& m : {default_machine, zen2}) {
        const std::expected<loops::Schedule, loops::Refusal> s = loops::planner::schedule_of(g, m);
        std::size_t skewed = 0;
        for (std::size_t k = 0; s && k < s->stages.size(); ++k) {
            skewed += s->stages[k].skewed ? 1 : 0;
        }
        ok = ok && s && skewed == links;
    }
    return ok;
}

// One cycle per graph holding one link, its nodes those over [k, time], planned in order, where the k-cycle graph holds
// k; each link reading through to its definition; the stage planned skewed.
bool one_cycle(const char* what, const Graph& linked, const Graph& cycles, std::int32_t k) {
    const CyclesFound found = cycles_found(linked);
    const CyclesFound apart = cycles_found(cycles);
    const bool ok = found.why_not.empty() && found.in_order && found.cycles.size() == 1 &&
                    found.cycles[0] == nodes_over(linked, Shape{k, kLength}) && apart.why_not.empty() && apart.in_order &&
                    apart.cycles.size() == static_cast<std::size_t>(k) && links_read_their_definitions(linked) && planned_skewed(linked, 1);
    if (!ok) {
        std::printf("linked_sections: %s: %zu cycles (%zu apart), \"%s\", refused \"%s\"\n", what, found.cycles.size(),
                    apart.cycles.size(), found.why_not.c_str(), refusal_on(linked, default_machine).c_str());
    }
    return ok;
}

// The pair into the eight: two cycles, the pair's over [2, time] first, then the eight's over [8, time]; ten apart.
bool two_cycles_in_series() {
    const Graph linked = in_series(Form::Linked);
    const CyclesFound found = cycles_found(linked);
    const CyclesFound apart = cycles_found(in_series(Form::Cycles));
    const bool ok = found.why_not.empty() && found.in_order && found.cycles.size() == 2 &&
                    found.cycles[0] == nodes_over(linked, Shape{2, kLength}) &&
                    found.cycles[1] == nodes_over(linked, Shape{8, kLength}) && apart.cycles.size() == 10 &&
                    links_read_their_definitions(linked) && planned_skewed(linked, 2);
    if (!ok) {
        std::printf("linked_sections: the pair into the eight: %zu cycles (%zu apart), \"%s\"\n", found.cycles.size(),
                    apart.cycles.size(), found.why_not.c_str());
    }
    return ok;
}

// ---- the odd links --------------------------------------------------------------------------------------------------
//
// Links the rewrite never makes, each two sections over [2, time] whose last is the output: of an input, of a value
// made before, of one made after it (no variable), of a variable never defined, of one defined before it, and of one
// defined after it from what the link does not make.

enum class Operand1 : std::uint8_t { AnInput, MadeBefore, MadeAfter, NeverDefined, DefinedBefore, NotFromTheLink };

constexpr std::int32_t odd_link_of(Graph& g, std::int32_t x, Operand1 which) {
    const auto gains = per_section(g, kTptGains);
    std::int32_t v = kNoValue;
    if (which == Operand1::AnInput) {
        v = add_value(g, ElemType::F64, Shape{2, kLength}, ValueCategory::Input);
        g.inputs.push_back(NamedValue{Name{"v"}, v});
    } else if (which == Operand1::MadeBefore || which == Operand1::MadeAfter) {
        v = add_node(g, op::Mul, gains, x);
    } else {
        v = variable(g, Shape{2, kLength});
    }
    if (which == Operand1::DefinedBefore) {
        const auto next = add_node(g, op::Mul, gains, x);
        settle(g, next, v);
    }
    const auto u = add_node(g, op::Chain, x, v);
    const std::size_t link = g.nodes.size() - 1;
    if (which == Operand1::MadeAfter) {
        const auto half = scalar(g, 0.5);
        const auto after = add_node(g, op::Mul, u, half);
        g.nodes[link].inputs[1] = after;
    }
    if (which == Operand1::NotFromTheLink) {
        const auto next = add_node(g, op::Mul, gains, x);
        settle(g, next, v);
    }
    return section_of(g, u, 1, Shape{kLength});
}

constexpr Graph odd_link(Operand1 which) {
    return graph_of([which](Graph& g, std::int32_t x) { return odd_link_of(g, x, which); });
}

// The same, not analysed.
constexpr Graph unanalysed(Operand1 which) {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{kLength}, ValueCategory::Input);
    g.inputs.push_back(NamedValue{Name{"x"}, x});
    const auto y = odd_link_of(g, x, which);
    g.outputs.push_back(NamedValue{Name{"y"}, y});
    set_default_precision(g, numerics::Precision::High);
    return g;
}

// The TPT pair inside a larger cycle, which feeds its last section (`summed`: the sum of its two, a value over [time]
// as its input is) back to its input, a sample later.
constexpr Graph inside_a_larger_cycle(bool summed) {
    return graph_of([summed](Graph& g, std::int32_t x) {
        const auto around = variable(g, Shape{kLength});
        const auto back = lag(g, around, 1);
        const auto half = scalar(g, 0.5);
        const auto fed = add_node(g, op::Mul, half, back);
        const auto in = add_node(g, op::Add, x, fed);
        const auto outputs = variable(g, Shape{2, kLength});
        const auto states = variable(g, Shape{2, kLength});
        const auto u = add_node(g, op::Chain, in, outputs);
        const auto gain = per_section(g, kTptGains);
        const auto y = tpt(g, u, gain, states);
        settle(g, y, outputs);
        OpAttr both{};
        both.kind = AttrKind::Reduce;
        both.reduce.axes = Axes{0};
        const auto out = summed ? add_node(g, op::Reduce, outputs, kNoValue, kNoValue, both) : section_of(g, outputs, 1, Shape{kLength});
        settle(g, out, around);
        return out;
    });
}

// The TPT pair read by a one-pole cycle after it.
constexpr Graph read_by_a_later_cycle() {
    return graph_of([](Graph& g, std::int32_t x) {
        const auto pair = linked_tpt(g, x, kTptGains);
        const auto state = variable(g, Shape{kLength});
        const auto back = lag(g, state, 1);
        const auto half = scalar(g, 0.5);
        const auto fed = add_node(g, op::Mul, half, back);
        const auto next = add_node(g, op::Add, pair, fed);
        return settle(g, next, state);
    });
}

bool odd_links_hold() {
    bool ok = true;
    for (const Operand1 which : {Operand1::AnInput, Operand1::MadeBefore, Operand1::DefinedBefore, Operand1::NotFromTheLink}) {
        const Graph g = odd_link(which);
        const bool held = cycles_found(g).why_not == kNoCycleOfItsOwn && refused_by_name(g, kNoCycleOfItsOwn);
        if (!held) {
            std::printf("linked_sections: odd link %d: \"%s\", refused \"%s\"\n", static_cast<int>(which),
                        cycles_found(g).why_not.c_str(), refusal_on(g, default_machine).c_str());
        }
        ok = held && ok;
    }
    // Neither a variable never defined nor a value made after the link gets past the analysis; were they to, the facts
    // would refuse them by name, the first as these, the second as any read of the past of a value made later.
    const Graph undefined = unanalysed(Operand1::NeverDefined);
    const Graph made_after = unanalysed(Operand1::MadeAfter);
    const bool unanalysable = cycles_found(undefined).why_not == kNoCycleOfItsOwn &&
                              refused_by_name(undefined, "heinzel: a declared Variable is never defined") &&
                              loops::graph::facts_of(made_after).why_not == "heinzel::compile: feedback reads the past of an Expr; make it a Variable" &&
                              !refusal_on(made_after, default_machine).empty();
    if (!unanalysable) {
        std::printf("linked_sections: a link of a variable never defined: \"%s\", refused \"%s\"; of a value made after it: \"%s\"\n",
                    cycles_found(undefined).why_not.c_str(), refusal_on(undefined, default_machine).c_str(),
                    std::string(loops::graph::facts_of(made_after).why_not).c_str());
    }
    ok = unanalysable && ok;
    // Inside a larger cycle: refused where the cycle's shapes differ; found as one cycle, the link and the sum in it,
    // where it sums the sections every sample, and the stage refused by name.
    const Graph sliced = inside_a_larger_cycle(false);
    const Graph summed = inside_a_larger_cycle(true);
    const CyclesFound around = cycles_found(summed);
    const std::string_view shapes = "heinzel::compile: feedback through anything but maps of one shape is not lowered yet";
    const bool larger = cycles_found(sliced).why_not == shapes && refused_by_name(sliced, shapes) && around.why_not.empty() &&
                        around.in_order && around.cycles.size() == 1 && around.cycles[0].size() == summed.nodes.size() - 1 && // (all but the gains)
                        refused_by_name(summed, kFoldedInTheCycle);
    // Read by a later cycle: two cycles, the link's first.
    const Graph later = read_by_a_later_cycle();
    const CyclesFound two = cycles_found(later);
    const bool read = two.why_not.empty() && two.in_order && two.cycles.size() == 2 &&
                      two.cycles[0] == nodes_over(later, Shape{2, kLength}) &&
                      two.cycles[1].back() == static_cast<std::int32_t>(later.nodes.size()) - 1 && planned_skewed(later, 1);
    if (!larger || !read) {
        std::printf("linked_sections: inside a larger cycle \"%s\", summed \"%s\" (%zu cycles), read by a later cycle %zu cycles\n",
                    cycles_found(sliced).why_not.c_str(), around.why_not.c_str(), around.cycles.size(), two.cycles.size());
    }
    return ok && larger && read;
}

} // namespace

int main() {
    try {
        bool ok = holds("a TPT pair", tpt_pair(Form::Linked), tpt_pair(Form::Cycles), 2);
        ok = holds("LR8 as four RBJ biquads", lr8(Form::Linked), lr8(Form::Cycles), 4) && ok;
        ok = holds("five peaks, their coefficients [5, 1] inputs", peaks(Form::Linked), peaks(Form::Cycles), 5) && ok;
        ok = holds("eight tanh sections", saturating_chain(Form::Linked), saturating_chain(Form::Cycles), 8) && ok;
        ok = holds("the TPT pair into the eight", in_series(Form::Linked), in_series(Form::Cycles), 9) && ok;
        ok = walked_again_holds() && ok;
        ok = refusals_hold() && ok;
        ok = one_cycle("a TPT pair", tpt_pair(Form::Linked), tpt_pair(Form::Cycles), 2) && ok;
        ok = one_cycle("LR8 as four RBJ biquads", lr8(Form::Linked), lr8(Form::Cycles), 4) && ok;
        ok = one_cycle("five peaks", peaks(Form::Linked), peaks(Form::Cycles), 5) && ok;
        ok = one_cycle("eight tanh sections", saturating_chain(Form::Linked), saturating_chain(Form::Cycles), 8) && ok;
        ok = two_cycles_in_series() && ok;
        ok = odd_links_hold() && ok;
        return ok ? 0 : 1;
    } catch (const heinzel::error& e) {
        std::printf("linked_sections: %.*s\n", static_cast<int>(e.message().size()), e.message().data());
        return 1;
    }
}
