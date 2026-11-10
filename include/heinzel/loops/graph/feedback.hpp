#pragma once

// Reads of the past, and feedback. A value read from an earlier sample is read out of storage: the buffer its maker
// wrote, some samples further back, with silence kept before the block. A lag and a recurrence's definition compute
// nothing; both hand their operand on. Which value such a read finds is the graph facts' (`Facts::read_as`); this file
// answers whether it has an address and whether it closes a cycle: a lag of a variable defined later is feedback, and
// everything from that lag to that definition runs sample by sample, in one loop nest — which may fold its voices every
// sample (Plan/23 10). So is a link of one (`kinds.hpp` `is_a_link`), which reads the element before at this sample.
// Refusals say `heinzel::compile:`.

#include <cstdint>
#include <string_view>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ir/timing.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/views.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel::loops::graph {

// If node `ni` reads the past of a value made later in the graph, that is feedback, and this returns the node that
// makes the value, where the cycle closes (-1: no such read). A link's operand 1 closes a cycle as a lag's operand does:
// the sections' outputs, a variable its cycle defines after it.
constexpr std::int32_t cycle_closes_at(const Graph& g, const Facts& facts, std::size_t ni) {
    const heinzel::Node& n = g.nodes[ni];
    const bool lag = graph::hands_its_operand_on(n) && samples_back(n) > 0;
    if (!lag && !graph::is_a_link(n)) {
        return -1;
    }
    const std::int32_t before = n.inputs[lag ? 0 : 1];
    const std::int32_t maker = facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(before)])];
    return maker > static_cast<std::int32_t>(ni) ? maker : -1;
}

// A link means one thing: k sections along axis 0, each reading the one before at this sample through operand 1, the
// variable over the sections' outputs, which a definition after the link makes from what the link makes. Only the
// rewrite makes one. A link of anything else (an input, a constant, a value made before it, a variable
// defined before it or never, or one whose definition does not read the link) closes no cycle of its own.
inline constexpr std::string_view kLinkOfNoCycle = "heinzel::compile: a link's operand 1 is not the variable its own cycle defines";

// A delay line in feedback is not lowered yet. Through its signal it would read the sample its cycle is making where
// its distance falls below one (the backlog's rule for it: clamp the distance to [1, max] there); through its distance
// it runs inside the cycle, where a stage runs maps, hand-ons and table reads (`runs_in_the_cycle`). The same words fit
// a line of a value defined later with no loop through it (a Variable's, written before its definition).
inline constexpr std::string_view kDelayLineInFeedback =
    "heinzel::compile: a delay line in feedback, or of a value defined later, is not lowered yet";

// Why the past read by node `ni` has no address here yet; empty when it has one. Silence before the block is a zeroed
// prefix of a workspace buffer; a caller's feed has none, so its first samples' past is read from silence kept
// elsewhere (head samples), and a delay line reads a copy of it (`rewrite/copied_inputs.hpp`). A window has no storage
// at all.
constexpr std::string_view why_no_past(const Graph& g, const Facts& facts, std::size_t ni) {
    const heinzel::Node& n = g.nodes[ni];
    const bool signalled = graph::gathers(n) == Gather::Back;
    if (!signalled && (!graph::hands_its_operand_on(n) || samples_back(n) == 0)) {
        return "";
    }
    const std::int32_t source = facts.read_as[static_cast<std::size_t>(n.inputs[0])];
    const std::int32_t maker = facts.producer[static_cast<std::size_t>(source)];
    const bool fed = g.values[static_cast<std::size_t>(source)].category == ValueCategory::Input;
    if (signalled && maker > static_cast<std::int32_t>(ni)) {
        return kDelayLineInFeedback;
    }
    if (signalled && maker < 0 && !fed) {
        return "heinzel::compile: a delay line over a constant is not lowered yet";
    }
    if (g.values[static_cast<std::size_t>(source)].shape.rank == 0) {
        return "heinzel::compile: a value of no axes has no time to be read back along";
    }
    if (signalled && maker < 0) {
        return "heinzel::compile: a delay line over an input reads a copy of it, which this graph lacks";
    }
    if (maker < 0) {
        return fed ? "" : "heinzel::compile: the past of a constant or of an undefined variable is not lowered yet";
    }
    return graph::folds_into_its_readers(g.nodes[static_cast<std::size_t>(maker)]) ? "heinzel::compile: the past of a window is not lowered yet" : "";
}

// ---- feedback -------------------------------------------------------------------------------------------------
//
// A cycle is what runs sample by sample in one loop nest: the nodes that depend, at the same sample, on a lag or a link
// of something defined later, and that such a definition depends on. It is found by what reads what, not by where a node
// stands in the graph. Cycles whose spans overlap are found together and run as one. A cycle holds maps and hand-ons
// of one shape, no more than a stage holds; or of a fold's operand and its result, with that one fold (not over time)
// between them, where nothing over the operand reads the fold's result in its own sample: a feedback stage then folds
// its voices every sample (`loops/normalize.hpp` `sample_tree`).
struct Feedback {
    std::int32_t cycle[kMaxNodes]{};               // the cycle a node is part of, named by the node opening its span (-1: none)
    fixed_vector<std::int32_t, kMaxNodes> order{}; // every node in the order they are planned in: a cycle's consecutive
    std::string_view why_not{};                    // why this graph's feedback cannot run here; empty when it can
};

// The lane bodies of the block in which a feedback stage runs a sample's voices, time innermost (`planner/feedback.hpp`
// `plan_feedback_loops`): more voices keep time outermost, and a stage that folds its voices every sample, which takes
// that layout or none, is refused. The rewrite layer asks it too, so that it makes no such stage the planner refuses
// (`cycle_chains.hpp`).
inline constexpr std::int64_t kMostSampleBodies = 4;

// Do `voices` fit that block at `lanes` lanes: a literal count within `kMostSampleBodies` lane bodies? The planner's
// own question; its gathering asks it of every join into a stage that folds its voices (`planner/chains.hpp`
// `sample_fits`), since a double there halves a float bank's lanes: 17 voices fit four bodies of eight floats, not of
// four doubles.
constexpr bool fits_a_block(Extent voices, std::int64_t lanes) {
    return is_literal(voices) && voices.encoded >= 1 && voices.encoded <= lanes * kMostSampleBodies;
}

namespace detail {

// The node whose result `reader` reads through operand `k` (-1: none, an input, a constant), at this sample or, with
// `earlier`, at an earlier one. What a lag reads has to be in storage, so its maker runs in the lag's nest or before.
constexpr std::int32_t read_by(const Facts& facts, const heinzel::Node& reader, std::size_t k, bool earlier) {
    if (k >= rules_of(reader.op).arity || operand_comes_from_the_past(reader, k) != earlier) {
        return -1;
    }
    return facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(reader.inputs[k])])];
}
constexpr std::int32_t read_now(const Facts& facts, const heinzel::Node& reader, std::size_t k) { return detail::read_by(facts, reader, k, false); }

// The shapes cycle nodes may have: one (that of its span's first node); or, where the cycle holds one fold, not over
// time, the fold's operand and its result. `in_cycle`: a node of the cycle.
struct CycleShapes {
    Shape operand{};
    Shape result{};
    std::int32_t fold{-1};
};
constexpr CycleShapes cycle_shapes(const Graph& g, std::size_t from, std::size_t until, const bool* in_cycle) {
    const Shape& first = g.values[static_cast<std::size_t>(g.nodes[from].output)].shape;
    CycleShapes c{.operand = first, .result = first};
    std::int32_t folds = 0;
    std::int32_t fold = -1;
    for (std::size_t j = from; j <= until; ++j) {
        fold = in_cycle[j] && graph::is_a_fold(g.nodes[j]) ? static_cast<std::int32_t>(j) : fold;
        folds += in_cycle[j] && graph::is_a_fold(g.nodes[j]) ? 1 : 0;
    }
    if (folds == 1 && !graph::folds_over_time(g, g.nodes[static_cast<std::size_t>(fold)])) {
        c.fold = fold;
        c.operand = g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(fold)].inputs[0])].shape;
        c.result = g.values[static_cast<std::size_t>(g.nodes[static_cast<std::size_t>(fold)].output)].shape;
    }
    return c;
}

// Does cycle node `j` run there: a map, a hand-on, a link or a table read over the operand (reading nothing its sample's
// fold makes, `after`) or over the result, or the fold itself? `after[j]` is set where it reads the fold's result in its
// sample. A table read is a map of its phase, its table read from storage made before the cycle runs.
constexpr bool runs_in_the_cycle(const Graph& g, const Facts& facts, std::size_t j, const CycleShapes& c, bool* after) {
    const heinzel::Node& n = g.nodes[j];
    after[j] = static_cast<std::int32_t>(j) == c.fold;
    for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
        const std::int32_t read = detail::read_now(facts, n, k);
        after[j] = after[j] || (read >= 0 && after[static_cast<std::size_t>(read)]);
    }
    const Shape& made = g.values[static_cast<std::size_t>(n.output)].shape;
    const bool map = graph::is_a_plain_map(n) || graph::hands_its_operand_on(n) || graph::is_a_link(n) || graph::gathers(n) == Gather::Table;
    return static_cast<std::int32_t>(j) == c.fold || (map && (made == c.result || (made == c.operand && !after[j])));
}

// Orders one span of the graph, `from` to `until`, that holds cycles. It marks which of its nodes are the cycle, in
// `f.cycle`, and appends the span's nodes to `f.order`: first what the cycle needs, then the cycle, then what needs it.
constexpr void order_a_span(const Graph& g, const Facts& facts, std::size_t from, std::size_t until, Feedback& f) {
    bool depends[kMaxNodes]{};
    bool needed[kMaxNodes]{};
    for (std::size_t j = from; j <= until; ++j) {
        const std::int32_t closes = graph::cycle_closes_at(g, facts, j);
        depends[j] = closes >= 0;
        needed[static_cast<std::size_t>(closes >= 0 ? closes : static_cast<std::int32_t>(j))] |= closes >= 0;
        for (std::size_t k = 0; k < kMaxNodeInputs; ++k) { // (what a lag inside the span reads the past of counts: it cannot run before it)
            const std::int32_t now = detail::read_now(facts, g.nodes[j], k);
            const std::int32_t before = detail::read_by(facts, g.nodes[j], k, true);
            const std::int32_t read = now >= 0 ? now : before < static_cast<std::int32_t>(j) ? before : -1;
            depends[j] = depends[j] || (read >= static_cast<std::int32_t>(from) && depends[static_cast<std::size_t>(read)]);
        }
    }
    for (std::size_t j = until + 1; j-- > from;) {
        for (std::size_t k = 0; needed[j] && k < kMaxNodeInputs; ++k) { // (...nor may what it reads run after it)
            const std::int32_t now = detail::read_now(facts, g.nodes[j], k);
            const std::int32_t before = detail::read_by(facts, g.nodes[j], k, true);
            const std::int32_t read = now >= 0 ? now : before;
            if (read >= static_cast<std::int32_t>(from) && read <= static_cast<std::int32_t>(until) && depends[static_cast<std::size_t>(read)]) {
                needed[static_cast<std::size_t>(read)] = true;
            }
        }
    }
    bool in_cycle[kMaxNodes]{};
    bool after[kMaxNodes]{};
    for (std::size_t j = from; j <= until; ++j) {
        in_cycle[j] = depends[j] && needed[j];
    }
    const CycleShapes shapes = detail::cycle_shapes(g, from, until, in_cycle);
    std::size_t held = 0;
    for (const std::int32_t part : {0, 1, 2}) {
        for (std::size_t j = from; j <= until; ++j) {
            if ((depends[j] ? (needed[j] ? 1 : 2) : 0) != part) {
                continue;
            }
            f.order.push_back(static_cast<std::int32_t>(j));
            if (part != 1) {
                continue;
            }
            f.cycle[j] = static_cast<std::int32_t>(from);
            if (!detail::runs_in_the_cycle(g, facts, j, shapes, after)) { // (a delay line there: its distance fed back)
                f.why_not = graph::gathers(g.nodes[j]) == Gather::Back ? kDelayLineInFeedback
                                                                       : "heinzel::compile: feedback through anything but maps of one shape is not lowered yet";
            } else if (++held > kMaxGroupStmts) {
                f.why_not = "heinzel::compile: a cycle over kMaxGroupStmts; rebuild with -DHEINZEL_MAX_STAGE_STMTS=N";
            }
        }
    }
    for (std::size_t j = from; j <= until; ++j) {
        if (graph::cycle_closes_at(g, facts, j) >= 0 && !needed[j]) {
            f.why_not = graph::is_a_link(g.nodes[j]) ? kLinkOfNoCycle
                                                     : "heinzel::compile: a lag of a later value its definition does not read is not lowered yet";
        }
    }
}

} // namespace detail

constexpr Feedback find_feedback(const Graph& g, const Facts& facts) {
    Feedback f{};
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        f.cycle[i] = -1;
    }
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        if (const std::string_view why = graph::why_no_past(g, facts, i); !why.empty()) {
            f.why_not = why;
        }
        for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
            if (detail::read_now(facts, g.nodes[i], k) > static_cast<std::int32_t>(i)) {
                f.why_not = "heinzel::compile: a value defined in terms of its own PRESENT is an algebraic loop";
            }
        }
        if (graph::is_a_link(g.nodes[i]) && graph::cycle_closes_at(g, facts, i) < 0) {
            f.why_not = kLinkOfNoCycle;
        }
    }
    for (std::size_t i = 0; i < g.nodes.size() && f.why_not.empty(); ++i) {
        std::size_t until = i;
        for (std::size_t j = i; j <= until; ++j) { // the span grows while a lag inside it reaches further
            const std::int32_t closes = graph::cycle_closes_at(g, facts, j);
            until = closes > static_cast<std::int32_t>(until) ? static_cast<std::size_t>(closes) : until;
        }
        if (until == i) {
            f.order.push_back(static_cast<std::int32_t>(i));
        } else {
            detail::order_a_span(g, facts, i, until, f);
            i = until;
        }
    }
    return f;
}

inline constexpr std::int32_t kNoLag = 1 << 30; // (no cycle: no lag bounds how many samples run at once)

// Each node's cycle's nearest past: the shortest lag through which the cycle reads a value it makes (`kNoLag` for a
// node of no cycle). It bounds how many of the cycle's samples may run at once: one voice read no nearer than a
// vector's lanes back runs that many (a comb's, an allpass's), a one-sample state one at a time. The planner prices a
// stage by it (`planner/chains.hpp` `lanes_along_time`), and the rewrite layer a cycle's type (`cycle_chains.hpp`).
constexpr std::vector<std::int32_t> nearest_pasts(const Graph& g, const Facts& facts, const Feedback& feedback) {
    const std::size_t count = g.nodes.size();
    std::vector<std::int32_t> nearest(count, kNoLag);
    std::vector<std::int32_t> of(count, kNoLag);
    for (std::size_t l = 0; l < count; ++l) {
        const std::int32_t c = feedback.cycle[l];
        if (c < 0 || !graph::hands_its_operand_on(g.nodes[l]) || samples_back(g.nodes[l]) == 0) {
            continue;
        }
        const std::int32_t maker = facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(g.nodes[l].inputs[0])])];
        std::int32_t& near = nearest[static_cast<std::size_t>(c)];
        near = maker >= 0 && feedback.cycle[static_cast<std::size_t>(maker)] == c && samples_back(g.nodes[l]) < near ? samples_back(g.nodes[l]) : near;
    }
    for (std::size_t i = 0; i < count; ++i) {
        of[i] = feedback.cycle[i] >= 0 ? nearest[static_cast<std::size_t>(feedback.cycle[i])] : kNoLag;
    }
    return of;
}

// Does a node of the stage read the past of a caller's feed? The stage then runs its first samples apart, reading that
// past from the feed's tail: no band holds it, and no Tile cuts its time.
constexpr bool reads_a_feeds_past(const Graph& g, const Facts& facts, const Schedule::Stage& stage) {
    for (const std::int32_t ni : stage.nodes) {
        const heinzel::Node& n = g.nodes[static_cast<std::size_t>(ni)];
        if (graph::hands_its_operand_on(n) && samples_back(n) > 0 &&
            g.values[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(n.inputs[0])])].category == ValueCategory::Input) {
            return true;
        }
    }
    return false;
}

// Does a lag among the stage's nodes read one sample back of what a later node of the stage makes? That is state the
// lowering hands on in a register, and what a time loop run innermost needs to build a block of lanes.
constexpr bool hands_state_on(const Graph& g, const Facts& facts, const Schedule::Stage& stage) {
    const auto& nodes = stage.nodes;
    for (std::size_t k = 0; k < nodes.size(); ++k) {
        const heinzel::Node& n = g.nodes[static_cast<std::size_t>(nodes[k])];
        if (!graph::hands_its_operand_on(n) || samples_back(n) != 1) {
            continue;
        }
        const std::int32_t maker = facts.producer[static_cast<std::size_t>(facts.read_as[static_cast<std::size_t>(n.inputs[0])])];
        for (std::size_t later = k + 1; later < nodes.size(); ++later) {
            if (nodes[later] == maker) {
                return true;
            }
        }
    }
    return false;
}

} // namespace heinzel::loops::graph
