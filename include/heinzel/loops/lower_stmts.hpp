#pragma once

// The statements and accesses of one stage. A statement finds each operand as a constant, in a register (a value made
// earlier in its stage), or in a buffer at an affine address, read once per stage through a `Load`. Each node becomes a
// statement, in its planned stage's order, and every refusal is a value. The lowering's working state is defined here,
// below its passes.

#include <array>
#include <bit>
#include <cstdint>
#include <expected>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ir/timing.hpp"
#include "heinzel/loops/graph/facts.hpp"
#include "heinzel/loops/graph/feedback.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/graph/places.hpp"
#include "heinzel/loops/graph/regions.hpp"
#include "heinzel/loops/graph/views.hpp"
#include "heinzel/loops/primitives/loops.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"
#include "heinzel/support/fixed_vector.hpp"
#include "heinzel/support/order_net.hpp"

namespace heinzel::loops::detail {

using Done = std::expected<void, Refusal>;

// A stage as formation leaves it: its statements in its nodes' order, each inside or after the fold, and its skeleton.
// The schedule's choices and normalization finish it. The lowering's state never leaves the lowering, so it grows as
// formation goes (`std::vector`): a fixed capacity here would be constructed whole, a stage's worth of statements
// sixteen times over, and constant evaluation pays for that element by element.
struct Formed {
    std::int32_t plan{-1};                     // its planned stage (-1: a relay copy)
    std::uint32_t collapsed{0};                // the axes of the domain its fold collapses (`graph::GroupShape`)
    fixed_vector<Extent, kMaxAxes> axes{};     // the domain
    std::vector<Stmt> stmts{};                 // at most kMaxStageStmts
    std::array<bool, kMaxStageStmts> inside{}; // each statement's side of the fold
    Skeleton skeleton{};
    std::int32_t inner{0};                     // where its inner loops start in `skeleton` (its size: none)
    bool resumes{false};                       // a chunk of its fold's own axis outside them
    // The loops split among workers and fetched ahead along (in `skeleton`; -1: none).
    std::int32_t split{-1};
    std::int32_t prefetch{-1};
    // The operand read from a relaid copy, and its block.
    std::int32_t relaid{-1};
    std::int32_t relaid_block{0};
    std::int32_t copy{-1};                     // ...the copy's buffer,
    bool makes_copy{false};                    // ...first read here: a copy stage before this one fills it
    // The operand read from a copy turned around, and that copy's buffer.
    std::int32_t turned{-1};
    std::int32_t turn{-1};
    bool makes_turn{false};                    // ...first read here: a stage before this one fills it
    // In a band: the axis it walks with the band, rows ahead of the last stage.
    std::int32_t band_axis{-1};
    std::int32_t lead{0};
    // The most samples of a value's past handed on in registers (its schedule's).
    std::int32_t state_depth{kMaxStateDepth};
};

// A planned stage's loops as its schedule spells them: its skeleton, where its inner loops start in it (its size when
// there are none), whether its fold resumes, and the loops its prefetch and its split run along, as indices into it
// (-1: none; a split of -2 names a loop the stage does not have).
struct Spelled {
    Skeleton skeleton{};
    std::int32_t inner{0};
    bool resumes{false};
    std::int32_t prefetch{-1};
    std::int32_t split{-1};
};

// The buffers are made in their final order, and that order sets the workspace layout (the workspace places them one
// after another): the feeds in input order, then the ramps, then for each planned stage its value made in parts, its
// relaid copy, the past its nodes keep and its stores.
struct Lowering {
    const Graph& g;
    const Schedule& schedule;
    Program& out;
    graph::Facts facts{};           // what the graph's values and nodes are (`graph::facts_of`)
    std::vector<Spelled> spelled{}; // each planned stage's loops
    std::vector<Formed> stages{};   // in run order, a value made in parts one stage per part (kMaxStages)
    graph::ValuePlaces at{};        // where the schedule makes each value and, as formation goes, its statement and its buffer
};

constexpr std::expected<std::int32_t, Refusal> add_buffer(Lowering& l, const Buffer& b) {
    if (l.out.buffers.size() == kMaxBuffers) {
        return std::unexpected("heinzel::compile: more buffers than kMaxBuffers; rebuild with -DHEINZEL_MAX_BUFFERS=N");
    }
    l.out.buffers.push_back(b);
    return static_cast<std::int32_t>(l.out.buffers.size() - 1);
}

constexpr std::expected<std::int32_t, Refusal> add_stmt(Formed& stage, const Stmt& st, bool inside) {
    if (stage.stmts.size() == kMaxStageStmts) {
        return std::unexpected("heinzel::compile: a stage over kMaxStageStmts; rebuild with -DHEINZEL_MAX_STAGE_STMTS=N");
    }
    stage.inside[stage.stmts.size()] = inside;
    stage.stmts.push_back(st);
    return static_cast<std::int32_t>(stage.stmts.size() - 1);
}

constexpr Access immediate(double x) { return Access{.source = Source::Immediate, .immediate = std::bit_cast<std::uint64_t>(x)}; }
constexpr Access local(std::int32_t s) { return Access{.source = Source::Local, .local = s}; }

// Is `st` a link (`graph::is_a_link`): u = _chain(x, V), element j of u V's element j − 1?
constexpr bool is_a_link(const Stmt& st) { return st.kind == StmtKind::Map && rules_of(st.op).time == TimeRule::ReadsTheElementBefore; }

// The affine address of a value of shape `held`, read from a statement of `region` out of `buffer`.
constexpr std::expected<Access, Refusal> buffer_access(std::int32_t buffer, const Shape& held, const graph::Region& region, const graph::Viewed& through) {
    const graph::Address at = graph::address_over(held, region, through);
    if (!at.why_not.empty()) {
        return std::unexpected(at.why_not);
    }
    Access a{.source = Source::Buffer, .buffer = buffer};
    for (std::size_t along = 0; along < kMaxAxes; ++along) {
        a.buffer_axis[along] = at.axis[along];
        a.step[along] = at.step[along];
    }
    for (std::size_t k = 0; k < kMaxRank; ++k) {
        a.offset[k] = at.offset[k];
    }
    return a;
}

// The Local holding a buffer operand: the `Load` that already reads it on this side of the fold, or a new one.
constexpr std::expected<std::int32_t, Refusal> loaded(Formed& stage, std::int32_t buffer, const Value& held, const graph::Region& region,
                                                      const graph::Viewed& through) {
    const std::expected<Access, Refusal> read = detail::buffer_access(buffer, held.shape, region, through);
    if (!read) {
        return std::unexpected(read.error());
    }
    for (std::size_t s = 0; s < stage.stmts.size(); ++s) {
        if (stage.stmts[s].kind == StmtKind::Load && stage.inside[s] == region.inside_fold && stage.stmts[s].in[0] == *read) {
            return static_cast<std::int32_t>(s);
        }
    }
    return detail::add_stmt(stage, Stmt{.kind = StmtKind::Load, .type = held.type, .in = {*read}}, region.inside_fold);
}

// Where a statement finds operand `read`: a constant, a run's start (computed where it is read), a register (a value made
// earlier in this stage), or a buffer read through a Load.
constexpr std::expected<Access, Refusal> operand_of(Lowering& l, std::int32_t read, const graph::Region& region, std::int32_t here, Formed& stage) {
    const std::int32_t v = l.facts.read_as[static_cast<std::size_t>(read)]; // a variable is read where its definition is
    const Value& operand = l.g.values[static_cast<std::size_t>(v)];
    if (operand.category == ValueCategory::Const) {
        return detail::immediate(operand.constant);
    }
    const std::int32_t maker = l.facts.producer[static_cast<std::size_t>(v)];
    if (maker >= 0 && graph::marks_a_run_start(l.g.nodes[static_cast<std::size_t>(maker)])) {
        if (!l.schedule.stages[static_cast<std::size_t>(here)].peeled) {
            return std::unexpected("heinzel::compile: a stage reads a run's first sample but is not peeled");
        }
        return Access{.source = Source::First};
    }
    if (l.at.stage[static_cast<std::size_t>(v)] == here) {
        const std::int32_t made_by = l.at.stmt[static_cast<std::size_t>(v)];
        if (made_by < 0) {
            return std::unexpected("heinzel::compile: a node reads a value its group has not made yet — the schedule's order is not the graph's");
        }
        // What is made inside a fold's loops changes with every turn of them; after they close only the fold is a value.
        const Stmt& maker = stage.stmts[static_cast<std::size_t>(made_by)];
        if (!region.inside_fold && stage.inside[static_cast<std::size_t>(made_by)] && maker.kind != StmtKind::Fold && maker.kind != StmtKind::Select) {
            return std::unexpected("heinzel::compile: a node after a fold reads a value that only exists inside the fold's loops");
        }
        // ...and inside them the fold is no value yet: its Finish makes it after them (a lane of a feedback stage's sample
        // reading the fold it feeds, Plan/23 10).
        if (region.inside_fold && (maker.kind == StmtKind::Fold || maker.kind == StmtKind::Select)) {
            return std::unexpected("heinzel::compile: a node inside a fold's loops reads the fold, which is made after them");
        }
        return detail::local(made_by);
    }
    // A view is read where its source is stored; a source computed in THIS stage, through the view, is a register.
    const graph::Viewed& through = l.facts.through[static_cast<std::size_t>(v)];
    if (through.source != v && l.at.stage[static_cast<std::size_t>(through.source)] == here) {
        if (l.at.stmt[static_cast<std::size_t>(through.source)] < 0 || !region.inside_fold) {
            return std::unexpected("heinzel::compile: a window's source computed in its reader's group must be made before it is read, inside the fold's loops");
        }
        return detail::local(l.at.stmt[static_cast<std::size_t>(through.source)]);
    }
    const std::int32_t stored_in = l.at.buffer[static_cast<std::size_t>(through.source)];
    if (stored_in >= 0) {
        const std::expected<std::int32_t, Refusal> load = detail::loaded(stage, stored_in, operand, region, through);
        if (!load) {
            return std::unexpected(load.error());
        }
        return detail::local(*load);
    }
    if (through.source != v) {
        return std::unexpected("heinzel::compile: a windowed value's source must be stored — an input, or an earlier group's result");
    }
    return std::unexpected("heinzel::compile: a node reads a value that has no place — not an input, a constant, or an earlier node's");
}

// What a lag reads: the buffer its source is stored in, `back` samples earlier. The source is an input, a ramp, or made
// by an earlier stage, or by this one, which is feedback; a later stage has not run yet when this one does.
constexpr std::expected<Access, Refusal> past_operand(Lowering& l, const heinzel::Node& n, const graph::Region& region, std::int32_t here) {
    const std::int32_t source = l.facts.read_as[static_cast<std::size_t>(n.inputs[0])];
    const std::int32_t made_in = l.at.stage[static_cast<std::size_t>(source)];
    const std::int32_t maker = l.facts.producer[static_cast<std::size_t>(source)];
    const bool fed = l.g.values[static_cast<std::size_t>(source)].category == ValueCategory::Input;
    const bool generated = maker >= 0 && graph::is_generated(l.g.nodes[static_cast<std::size_t>(maker)]);
    // (`operand_of` reads a run's start at the present sample wherever it is read: its past is no earlier sample's)
    if (maker >= 0 && graph::marks_a_run_start(l.g.nodes[static_cast<std::size_t>(maker)])) {
        return std::unexpected("heinzel::compile: a run's first sample has no past for a lag to read");
    }
    if ((made_in < 0 && !fed && !generated) || l.at.buffer[static_cast<std::size_t>(source)] < 0 || l.facts.through[static_cast<std::size_t>(source)].source != source) {
        return std::unexpected("heinzel::compile: the past of a value that is neither fed, a ramp, nor made by a group has no address");
    }
    if (made_in > here) {
        return std::unexpected("heinzel::compile: a lag reads a value a LATER group makes: feedback runs in one group");
    }
    std::expected<Access, Refusal> read = detail::buffer_access(l.at.buffer[static_cast<std::size_t>(source)], l.g.values[static_cast<std::size_t>(source)].shape,
                                                                region, graph::read_as_itself(source));
    if (read) {
        read->back = samples_back(n);
    }
    return read;
}

// What a link reads as its operand 1 (`u = _chain(x, V)`): V's buffer one element back along the sections,
// affine as a slice is. V is defined later in the link's stage, by the cycle the link closes (`keep_what_links_read`).
constexpr std::expected<Access, Refusal> linked_operand(Lowering& l, const heinzel::Node& n, const graph::Region& region, std::int32_t here) {
    const std::int32_t v = l.facts.read_as[static_cast<std::size_t>(n.inputs[1])];
    if (l.at.stage[static_cast<std::size_t>(v)] != here) {
        return std::unexpected("heinzel::compile: a link reads what its own group makes: its cycle runs in one group");
    }
    graph::Viewed back = graph::read_as_itself(v);
    back.offset[0] = -1;
    return detail::buffer_access(l.at.buffer[static_cast<std::size_t>(v)], l.g.values[static_cast<std::size_t>(v)].shape, region, back);
}

// Does view `n` of stage `here`, a skewed one, read one section of what the stage makes: a slice of one
// section along the sections, or a view keeping the elements in order over such a view (the slice's reshape)? It reads
// that section's lane of its operand's register (`Implied::lane`).
constexpr bool views_one_section(const Lowering& l, const heinzel::Node& n, std::int32_t here, const Formed& stage) {
    if (!graph::is_an_affine_view(n) || !l.schedule.stages[static_cast<std::size_t>(here)].skewed) {
        return false;
    }
    const std::int32_t v = l.facts.read_as[static_cast<std::size_t>(n.inputs[0])];
    const std::int32_t maker = l.at.stage[static_cast<std::size_t>(v)] == here ? l.at.stmt[static_cast<std::size_t>(v)] : -1;
    if (maker < 0) {
        return false;
    }
    const ViewAttr& view = n.attr.view;
    const Stmt& under = stage.stmts[static_cast<std::size_t>(maker)];
    const bool over_a_view = under.kind == StmtKind::Load && under.in[0].source == Source::Local && under.node >= 0 &&
                             graph::is_an_affine_view(l.g.nodes[static_cast<std::size_t>(under.node)]);
    return view.kind == ViewKind::Slice ? view.axis == 0 && view.length == 1 : graph::keeps_elements_in_order(n) && over_a_view;
}

// What a read whose address is a signal reads from: storage an earlier stage made. A delay line reads its first
// operand, walked as the stage walks it. A table is the second operand, of shape `{cells, time}`: the stage walks
// it along time, and its one element axis is indexed by the position, not by any loop.
constexpr std::expected<Access, Refusal> gathered_operand(Lowering& l, const heinzel::Node& n, const graph::Region& region, std::int32_t here) {
    const bool table = graph::gathers(n) == Gather::Table;
    const std::int32_t source = l.facts.read_as[static_cast<std::size_t>(n.inputs[table ? 1 : 0])];
    const Value& held = l.g.values[static_cast<std::size_t>(source)];
    const std::int32_t stored_in = l.at.buffer[static_cast<std::size_t>(source)];
    if (stored_in < 0 || l.at.stage[static_cast<std::size_t>(source)] == here || l.facts.through[static_cast<std::size_t>(source)].source != source) {
        return std::unexpected("heinzel::compile: a read whose address is a signal reads storage: an input, or what an EARLIER group made");
    }
    if (!table) {
        std::expected<Access, Refusal> read = detail::buffer_access(stored_in, held.shape, region, graph::read_as_itself(source));
        if (read) {
            read->indexed_axis = held.shape.rank - 1;
        }
        return read;
    }
    if (held.shape.rank != 2) {
        return std::unexpected("heinzel::compile: a table with axes besides its cells and time is not lowered yet");
    }
    Access read{.source = Source::Buffer, .buffer = stored_in, .indexed_axis = held.shape.rank - 2};
    for (std::size_t along = 0; along < kMaxAxes; ++along) {
        if (region.axis[along] == region.over.rank - 1 && extent_at(held.shape, 1) != literal_extent(1)) {
            read.buffer_axis[along] = 1;
            read.step[along] = region.step[along];
        }
    }
    return read;
}

// What a view with a statement reads: its source's buffer, at the view's address — storage, not its own stage's.
constexpr std::expected<Access, Refusal> view_operand(Lowering& l, const heinzel::Node& n, const graph::Region& region, std::int32_t here) {
    const graph::Viewed through = graph::through_view(l.g, n, l.facts.through[static_cast<std::size_t>(n.inputs[0])]);
    const Value& source = l.g.values[static_cast<std::size_t>(through.source)];
    if (source.category == ValueCategory::Const) { // every element of a view of a constant is that constant: a fill
        return detail::immediate(source.constant);
    }
    const std::int32_t stored_in = l.at.buffer[static_cast<std::size_t>(through.source)];
    if (stored_in < 0 || l.at.stage[static_cast<std::size_t>(through.source)] == here) {
        return std::unexpected("heinzel::compile: a view reads storage: an input, or what an EARLIER group made");
    }
    return detail::buffer_access(stored_in, l.g.values[static_cast<std::size_t>(n.output)].shape, region, through);
}

// A contraction's operand: a buffer read at the address its own axes give it in the stage's domain (not a broadcast
// alignment, so it is spelled out). A relaid operand's Load is redirected to its copy later (`relay`).
constexpr std::expected<Access, Refusal> contraction_operand(Lowering& l, const heinzel::Node& n, std::int32_t which, Formed& stage) {
    const std::int32_t v = n.inputs[static_cast<std::size_t>(which)];
    const Value& held = l.g.values[static_cast<std::size_t>(v)];
    if (l.at.buffer[static_cast<std::size_t>(v)] < 0) {
        return std::unexpected("heinzel::compile: a contraction's operand must be stored — an input, or an earlier group's result");
    }
    Stmt load{.kind = StmtKind::Load, .type = held.type};
    load.in[0] = Access{.source = Source::Buffer, .buffer = l.at.buffer[static_cast<std::size_t>(v)]};
    const auto axes = graph::contraction_axes(l.g, n, which);
    for (std::size_t k = 0; k < axes.size(); ++k) {
        load.in[0].buffer_axis[static_cast<std::size_t>(axes[k])] = static_cast<std::int32_t>(k);
    }
    const std::expected<std::int32_t, Refusal> s = detail::add_stmt(stage, load, true);
    if (!s) {
        return std::unexpected(s.error());
    }
    return detail::local(*s);
}

// One node as a statement of the stage being formed, reading its operands over `region` (`here`: its planned stage).
constexpr std::expected<Stmt, Refusal> statement_of(Lowering& l, const heinzel::Node& n, std::int32_t ni, const graph::Region& region, std::int32_t here,
                                                    Formed& stage) {
    const Value& made = l.g.values[static_cast<std::size_t>(n.output)];
    Stmt st{.op = n.op, .node = ni, .type = made.type, .computed_in = made.type, .precision = made.precision};
    for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
        st.computed_in = l.g.values[static_cast<std::size_t>(n.inputs[k])].type == ElemType::F64 ? ElemType::F64 : st.computed_in;
    }
    std::expected<Access, Refusal> in[kMaxNodeInputs]{Access{}, Access{}, Access{}};
    if (graph::is_a_contraction(n)) {
        st.kind = StmtKind::Fold, st.collapse = rules_of(n.op).attr, st.strict = !numerics::grants(made.precision).reorder;
        in[0] = detail::contraction_operand(l, n, 0, stage);
        in[1] = in[0] ? detail::contraction_operand(l, n, 1, stage) : in[1];
    } else if (graph::is_an_ordering(n)) {
        if (!graph::selects_one_rank(l.g, n)) {
            return std::unexpected("heinzel::compile: an order statistic that is not one exact rank of a literal axis is not lowered yet");
        }
        st.kind = StmtKind::Select;
        st.gathered = extent_at(l.g.values[static_cast<std::size_t>(n.inputs[0])].shape, n.attr.order.axis).encoded;
        st.rank = order::quantile_rank(st.gathered, n.attr.order.q).lo;
        in[0] = detail::operand_of(l, n.inputs[0], region, here, stage);
    } else if (graph::is_a_fold(n)) {
        st.kind = StmtKind::Fold, st.fold = n.attr.reduce.op, st.strict = !numerics::grants(made.precision).reorder;
        in[0] = detail::operand_of(l, n.inputs[0], region, here, stage);
    } else if (graph::is_a_plain_map(n) && made.shape == region.over) {
        for (std::size_t k = 0; k < rules_of(n.op).arity && (k == 0 || in[k - 1]); ++k) {
            in[k] = detail::operand_of(l, n.inputs[k], region, here, stage);
        }
    } else if (graph::gathers(n) != Gather::None && made.shape == region.over) {
        st.kind = StmtKind::Gather, st.gather = graph::gathers(n), st.reach = graph::reach_of(n);
        st.phase_in_one_turn = st.gather == Gather::Table && n.attr.table.phase_in_one_turn; // the proof's mark
        in[0] = detail::gathered_operand(l, n, region, here);
        in[1] = in[0] ? detail::operand_of(l, n.inputs[st.gather == Gather::Table ? 0 : 1], region, here, stage) : in[1]; // the position
    } else if (graph::is_a_link(n) && made.shape == region.over) {
        in[0] = detail::operand_of(l, n.inputs[0], region, here, stage); // the fill
        in[1] = in[0] ? detail::linked_operand(l, n, region, here) : in[1];
    } else if (detail::views_one_section(l, n, here, stage)) {
        st.kind = StmtKind::Load;
        in[0] = detail::operand_of(l, n.inputs[0], region, here, stage);
    } else if ((graph::hands_its_operand_on(n) || graph::is_an_affine_view(n)) && made.shape == region.over) {
        st.kind = StmtKind::Load; // nothing is computed: another place, an earlier sample, or its operand as it is
        in[0] = graph::is_an_affine_view(n) ? detail::view_operand(l, n, region, here)
                : samples_back(n) > 0       ? detail::past_operand(l, n, region, here)
                                            : detail::operand_of(l, n.inputs[0], region, here, stage);
    } else {
        return std::unexpected("heinzel::compile: a group holds a node that is neither its fold nor a map over the shape it runs at");
    }
    for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
        if (!in[k]) {
            return std::unexpected(in[k].error());
        }
        st.in[k] = *in[k];
    }
    return st;
}

// A value whose past is read gets its buffer, with silence before the block as deep as the deepest such read, before
// its stage's statements are formed: inside a cycle the lag is formed first and the node making what it reads after.
constexpr Done keep_its_past(Lowering& l, std::int32_t v) {
    const std::int32_t deepest = l.facts.deepest[static_cast<std::size_t>(v)];
    if (deepest > 0) {
        const Value& made = l.g.values[static_cast<std::size_t>(v)];
        const std::expected<std::int32_t, Refusal> b = detail::add_buffer(l, Buffer{.type = made.type, .shape = made.shape, .value = v, .prefix = deepest});
        if (!b) {
            return std::unexpected(b.error());
        }
        l.at.buffer[static_cast<std::size_t>(v)] = *b;
    }
    return {};
}

// A value a link of stage `ps` reads (`u = _chain(x, V)`, V defined later in the stage) gets its buffer, as its past
// does, where no lag reads it: the link reads it one element back, its keeper's register one lane up.
constexpr Done keep_what_links_read(Lowering& l, const Schedule::Stage& ps, std::int32_t here) {
    for (const std::int32_t ni : ps.nodes) {
        const heinzel::Node& n = l.g.nodes[static_cast<std::size_t>(ni)];
        const std::int32_t v = graph::is_a_link(n) ? l.facts.read_as[static_cast<std::size_t>(n.inputs[1])] : -1;
        if (v < 0 || l.at.stage[static_cast<std::size_t>(v)] != here || l.at.buffer[static_cast<std::size_t>(v)] >= 0) {
            continue;
        }
        const Value& made = l.g.values[static_cast<std::size_t>(v)];
        const std::expected<std::int32_t, Refusal> b = detail::add_buffer(l, Buffer{.type = made.type, .shape = made.shape, .value = v});
        if (!b) {
            return std::unexpected(b.error());
        }
        l.at.buffer[static_cast<std::size_t>(v)] = *b;
    }
    return {};
}

// Statement `k` stores its result when something outside its stage reads it, when its past is read (its buffer exists
// already), or when it is a fold that resumes, whose partial results wait in the store between chunks. A fold's result
// is written over the region after its loops, which is the shape it has.
constexpr Done store_if_read_outside(Lowering& l, const graph::Region& written_at, Formed& stage, std::size_t k) {
    Stmt& st = stage.stmts[k];
    const std::int32_t v = l.g.nodes[static_cast<std::size_t>(st.node)].output;
    const bool resumes = stage.resumes && st.kind == StmtKind::Fold;
    if (!resumes && !graph::leaves_its_stage(l.at, v) && l.at.buffer[static_cast<std::size_t>(v)] < 0) {
        return {};
    }
    const Value& made = l.g.values[static_cast<std::size_t>(v)];
    if (l.at.buffer[static_cast<std::size_t>(v)] < 0) {
        const std::expected<std::int32_t, Refusal> b = detail::add_buffer(l, Buffer{.type = made.type, .shape = made.shape, .value = v});
        if (!b) {
            return std::unexpected(b.error());
        }
        l.at.buffer[static_cast<std::size_t>(v)] = *b;
    }
    const std::expected<Access, Refusal> store = detail::buffer_access(l.at.buffer[static_cast<std::size_t>(v)], made.shape, written_at, graph::read_as_itself(v));
    if (!store) {
        return std::unexpected(store.error());
    }
    st.store = *store;
    return {};
}

} // namespace heinzel::loops::detail
