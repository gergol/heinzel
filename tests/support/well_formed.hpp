#pragma once

// What the runner accepts: a tree whose structure, references, chunks and register indices the runner can follow
// as written — and, `well_formed_walks.hpp`, whose walks keep what the runner relies on. Programs come only from the
// lowering, so this is the lowering's postcondition: the runner checks nothing, and the tests hold every program
// they make or run to it; the error names the first rule the tree breaks. Each check is linear in the nodes, or
// bounded per node by the depth of the tree.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

#include "heinzel/loops/program.hpp"
#include "heinzel/loops/tree.hpp"

#include "well_formed_closes.hpp"
#include "well_formed_kept.hpp"
#include "well_formed_restarts.hpp"
#include "well_formed_skew.hpp"
#include "well_formed_starts.hpp"
#include "well_formed_walks.hpp"

namespace heinzel::loops {
namespace detail {

// Every node but a root has a parent before it that lists it among its kids, in increasing order: no cycle, no node
// twice.
constexpr Checked check_links(const Program& p) {
    std::size_t kids = 0;
    std::size_t roots = 0;
    for (std::size_t n = 0; n < p.nodes.size(); ++n) {
        const Node& here = p.nodes[n];
        if (here.parent < -1 || here.parent >= static_cast<std::int32_t>(n)) {
            return std::unexpected("heinzel::loops: a node's parent comes before it");
        }
        roots += here.parent < 0 ? 1 : 0;
        std::int32_t last = static_cast<std::int32_t>(n);
        for (std::int32_t k = here.first_kid; k >= 0; k = node_at(p, k).next_sibling) {
            if (k <= last || static_cast<std::size_t>(k) >= p.nodes.size() || node_at(p, k).parent != static_cast<std::int32_t>(n)) {
                return std::unexpected("heinzel::loops: a node's kids follow it, in order, and name it their parent");
            }
            last = k;
            ++kids;
        }
    }
    if (kids + roots != p.nodes.size()) {
        return std::unexpected("heinzel::loops: every node but a root is a kid of its parent");
    }
    return {};
}

// A constant tensor's numbers lie in the program's pool, and it lies where they are: in read-only data (one of no
// number is a workspace buffer of no bytes).
constexpr Checked check_constants(const Program& p) {
    for (const Buffer& b : p.buffers) {
        if (b.first < 0) {
            continue;
        }
        const std::int64_t numbers = numbers_per_sample(b.shape);
        if (std::int64_t{b.first} + numbers > static_cast<std::int64_t>(p.constants.size()) || b.ramp ||
            (b.home != Home::Constant && numbers != 0)) {
            return std::unexpected("heinzel::loops: a constant's numbers lie in the program's pool, and it in read-only data");
        }
    }
    return {};
}

// Each stage's Stage node is a root, right after the nodes of the stage before, and its nodes stand in one run numbered
// depth first: following the links from it meets them in turn. Every node is some stage's, and a stage names a range of
// statements.
constexpr Checked check_stages(const Program& p) {
    std::int32_t next = 0; // where the next stage's nodes start
    for (const Stage& st : p.stages) {
        if (st.root != next || static_cast<std::size_t>(st.root) >= p.nodes.size() || node_at(p, st.root).kind != NodeKind::Stage ||
            node_at(p, st.root).parent != -1 || node_at(p, st.root).next_sibling != -1) {
            return std::unexpected("heinzel::loops: each stage's Stage node is a root, right after the nodes of the stage before");
        }
        for (std::int32_t at = st.root; at >= 0;) {
            if (at != next++) {
                return std::unexpected("heinzel::loops: nodes are numbered depth first");
            }
            std::int32_t up = at;
            for (at = node_at(p, at).first_kid; at < 0 && up != st.root; up = node_at(p, up).parent) {
                at = node_at(p, up).next_sibling;
            }
        }
        if (st.first < 0 || st.count < 0 || st.count > static_cast<std::int32_t>(kMaxStageStmts) ||
            static_cast<std::size_t>(std::int64_t{st.first} + st.count) > p.stmts.size()) {
            return std::unexpected("heinzel::loops: a stage names a range of statements");
        }
    }
    if (static_cast<std::size_t>(next) != p.nodes.size()) {
        return std::unexpected("heinzel::loops: every node stands in a stage's tree");
    }
    return {};
}

// A statement's accesses name buffers the program has, axes of the domain and of the buffer, and — a local —
// an earlier statement of the stage. A constant (one sample, in read-only data) is stored into by none and walked
// along its last axis by none: every read of it is its one sample.
constexpr Checked check_accesses(const Program& p, const Stage& stage, std::int32_t s) {
    const Stmt& st = p.stmts[static_cast<std::size_t>(stage.first + s)];
    if (st.kind == StmtKind::Gather && (st.in[0].source != Source::Buffer || st.in[0].indexed_axis < 0)) {
        return std::unexpected("heinzel::loops: a Gather reads a buffer along the axis its position indexes");
    }
    for (std::size_t k = 0; k <= kMaxNodeInputs; ++k) {
        const Access& a = loops::access_in(st, k);
        if (a.source == Source::Local && (a.local < 0 || a.local >= s || k == kMaxNodeInputs)) {
            return std::unexpected("heinzel::loops: a local operand is an earlier statement of the stage, and no store is one");
        }
        if (a.source != Source::Buffer) {
            continue;
        }
        if (a.buffer < 0 || static_cast<std::size_t>(a.buffer) >= p.buffers.size()) {
            return std::unexpected("heinzel::loops: an access names a buffer of the program");
        }
        const std::int32_t rank = p.buffers[static_cast<std::size_t>(a.buffer)].shape.rank;
        if (p.buffers[static_cast<std::size_t>(a.buffer)].home == Home::Constant) {
            if (k == kMaxNodeInputs) {
                return std::unexpected("heinzel::loops: no store is into a constant");
            }
            bool along_time = rank >= 1 && (a.indexed_axis == rank - 1 || a.back > 0);
            for (const std::int32_t along : a.buffer_axis) {
                along_time = along_time || (rank >= 1 && along == rank - 1);
            }
            if (along_time) {
                return std::unexpected("heinzel::loops: no access walks a constant along its last axis: it holds one sample");
            }
        }
        for (std::size_t axis = 0; axis < kMaxAxes; ++axis) {
            const bool walked = a.buffer_axis[axis] >= 0;
            if (walked && (axis >= stage.axes.size() || a.buffer_axis[axis] >= rank || a.step[axis] == 0)) { // (a flip steps back)
                return std::unexpected("heinzel::loops: an access walks buffer axes from axes of its stage's domain");
            }
        }
        if (a.indexed_axis >= rank || a.back < 0) {
            return std::unexpected("heinzel::loops: an access names buffer axes the buffer has");
        }
    }
    return {};
}

// A Loop's fields; an enumerator's governing strip and the chunks it is handed.
constexpr Checked check_walking(const Program& p, std::int32_t n, const Stage& stage, bool folds) {
    const Node& here = node_at(p, n);
    if (here.axis < 0 || static_cast<std::size_t>(here.axis) >= stage.axes.size() || here.step < 1) {
        return std::unexpected("heinzel::loops: a loop or enumerator walks an axis of its stage, by one or more");
    }
    if (here.kind == NodeKind::Loop) {
        const bool strip = here.tail != Tail::Clip;
        if (strip != (here.strip >= 0) || here.strip >= static_cast<std::int32_t>(kMaxStrips) ||
            (here.tail == Tail::LanesThenOnes ? here.rest < 1 || here.step % here.rest != 0 : here.rest != 1)) {
            return std::unexpected("heinzel::loops: a strip has an ordinal below kMaxStrips, and a rest only after its lanes");
        }
        if (here.chunks_fold && (strip || here.step < 2 || !folds)) {
            return std::unexpected("heinzel::loops: a loop that chunks a fold is a Clip loop of chunks of two or more, in a stage with a fold");
        }
        if (here.peel_first && strip) {
            return std::unexpected("heinzel::loops: a loop that peels its first iteration is a Clip loop");
        }
        return {};
    }
    if (here.kind == NodeKind::Turns) {
        if (!is_literal(stage.axes[static_cast<std::size_t>(here.axis)]) || here.step != 1) {
            return std::unexpected("heinzel::loops: Turns walk an axis of literal extent, one turn at a time");
        }
        return {};
    }
    const std::int32_t strip = governing_strip(p, n);
    if (strip < 0 || !is_strip(node_at(p, strip))) {
        return std::unexpected("heinzel::loops: an Unroll or Vector stands below a strip over its axis, and no other loop over it between");
    }
    const bool rests = node_at(p, strip).tail == Tail::LanesThenOnes;
    for (const bool rest : {false, true}) {
        const std::int64_t chunk = chunk_at(p, n, rest ? 1u : 0u);
        if (rest && !rests) {
            continue;
        }
        if (here.kind == NodeKind::Unroll && chunk % here.step != 0) {
            return std::unexpected("heinzel::loops: an Unroll's step divides the chunk it is handed");
        }
        if (here.kind == NodeKind::Vector && (chunk < 2 || !power_of_two(chunk) || (!rest && chunk != here.step))) {
            return std::unexpected("heinzel::loops: a Vector is as wide as the chunk it is handed, a power of two, two or more");
        }
    }
    return {};
}

// Registers hold a statement of their role and are sized by a chain below them; WhenLast / WhenNotLast stand
// below a loop that chunks a fold.
constexpr Checked check_scope(const Program& p, std::int32_t n, const Stage& stage) {
    const Node& here = node_at(p, n);
    if (here.kind != NodeKind::Registers) {
        if (ancestor(p, n, [](const Node& a) { return a.kind == NodeKind::Loop && a.chunks_fold; }) < 0) {
            return std::unexpected("heinzel::loops: WhenLast and WhenNotLast stand below a loop that chunks a fold");
        }
        return {};
    }
    const StmtKind kind = here.stmt >= 0 && here.stmt < stage.count ? p.stmts[static_cast<std::size_t>(stage.first + here.stmt)].kind : StmtKind::Map;
    const bool held = here.role == Role::Accumulator ? kind == StmtKind::Fold
                      : here.role == Role::Gathered  ? kind == StmtKind::Select
                                                     : kind != StmtKind::Select && (kind != StmtKind::Fold || here.link < 0);
    // (a fold's value is state where its Finish keeps it: one voice, outside every chain, Plan/23 10)
    if (here.stmt < 0 || here.stmt >= stage.count || !held || (here.opens_first && here.role != Role::Accumulator)) {
        return std::unexpected("heinzel::loops: Registers hold a statement of their stage in its role");
    }
    if (here.slots < 1 || here.slots > 64 || (here.role != Role::State && here.slots != 1)) {
        return std::unexpected("heinzel::loops: Registers hold one sample of a value; a State as many as its readers reach back");
    }
    if (ancestor(p, n, is_enumerator) >= 0) { // a body index counts the bodies of every chain above, not of its own
        return std::unexpected("heinzel::loops: Registers stand outside every enumeration");
    }
    const bool heads = here.link > n && static_cast<std::size_t>(here.link) < p.nodes.size() && encloses(p, n, here.link) &&
                       is_enumerator(node_at(p, here.link)) && chain_next(p, node_at(p, here.link).parent) != here.link;
    if (here.link >= 0 && !heads) {
        return std::unexpected("heinzel::loops: a Registers' link is the head of a chain below it");
    }
    return {};
}

// The enumerators between Registers `from` and leaf `to` are the Registers' link chain: the same levels in its
// order — the register index is the flat body index. None at all only where the leaf covers every register
// (`all`: an Init or a Finish of a fold whose lanes run along its own axis, a chain ending in lanes).
constexpr bool indexes_like(const Program& p, std::int32_t from, std::int32_t to, bool all) {
    std::array<std::int32_t, 2 * kMaxAxes> path{};
    std::size_t count = 0;
    for (std::int32_t at = node_at(p, to).parent; at != from; at = node_at(p, at).parent) {
        if (is_enumerator(node_at(p, at))) {
            if (count == path.size()) {
                return false;
            }
            path[count++] = at;
        }
    }
    const std::int32_t link = node_at(p, from).link;
    if (count == 0) {
        return link < 0 || (all && chain_has_lanes(p, link));
    }
    std::int32_t chain = link;
    for (std::size_t k = count; k-- > 0; chain = chain_next(p, chain)) {
        if (chain < 0 || node_at(p, chain).axis != node_at(p, path[k]).axis || node_at(p, chain).step != node_at(p, path[k]).step ||
            node_at(p, chain).kind != node_at(p, path[k]).kind) {
            return false;
        }
    }
    return chain < 0;
}

constexpr bool leaf_of(NodeKind leaf, StmtKind kind) {
    const bool computed = kind == StmtKind::Map || kind == StmtKind::Load || kind == StmtKind::Gather;
    if (leaf == NodeKind::Compute || leaf == NodeKind::Open || leaf == NodeKind::Close) {
        return computed;
    }
    if (leaf == NodeKind::Collect || leaf == NodeKind::Rank) {
        return kind == StmtKind::Select;
    }
    return kind == StmtKind::Fold;
}

// A Prefetch names a Loop enclosing the Registers it stands in, and the program a line.
constexpr Checked check_prefetch(const Program& p, std::int32_t n) {
    const Node& here = node_at(p, n);
    const std::int32_t regs = ancestor(p, n, [](const Node& a) { return a.kind == NodeKind::Registers; });
    const bool named = here.link >= 0 && static_cast<std::size_t>(here.link) < p.nodes.size() && regs >= 0 &&
                       node_at(p, here.link).kind == NodeKind::Loop && encloses(p, here.link, regs);
    if (!named || p.line_bytes < 1) {
        return std::unexpected("heinzel::loops: a Prefetch names a Loop enclosing its Registers, and a line");
    }
    return {};
}

// A leaf names a statement of its kind; the registers it reads or writes stand above it, indexed by its chain.
constexpr Checked check_leaf(const Program& p, std::int32_t n, const Stage& stage) {
    const Node& here = node_at(p, n);
    if (here.first_kid >= 0) {
        return std::unexpected("heinzel::loops: a leaf has no kids");
    }
    if (here.kind == NodeKind::Prefetch) {
        return check_prefetch(p, n);
    }
    if (here.stmt < 0 || here.stmt >= stage.count || !leaf_of(here.kind, p.stmts[static_cast<std::size_t>(stage.first + here.stmt)].kind)) {
        return std::unexpected("heinzel::loops: a leaf names a statement of its stage of its kind");
    }
    if (here.kind != NodeKind::Compute && here.kind != NodeKind::Open && here.kind != NodeKind::Close) {
        const Role role = here.kind == NodeKind::Collect || here.kind == NodeKind::Rank ? Role::Gathered : Role::Accumulator;
        const std::int32_t regs = registers_of(p, n, here.stmt, role);
        if (regs < 0 || !indexes_like(p, regs, n, (here.kind == NodeKind::Init || here.kind == NodeKind::Finish) && lanes_fold_together(p, regs))) {
            return std::unexpected("heinzel::loops: a fold's or a Select's leaf stands below its Registers, indexed by their chain");
        }
    }
    const bool opens = here.kind == NodeKind::Open || here.kind == NodeKind::Close; // (a Close mirrors an Open)
    const bool makes = here.kind == NodeKind::Compute || here.kind == NodeKind::Finish;
    if ((opens && (here.keeps < 0 || p.stmts[static_cast<std::size_t>(stage.first + here.stmt)].in[0].source != Source::Buffer)) ||
        (!makes && !opens && here.keeps >= 0) || (here.kind != NodeKind::Compute && here.reads >= 0) || (!makes && here.closes)) {
        return std::unexpected("heinzel::loops: only a Compute reads state; a Compute, a Finish or an Open keeps it, an Open from a buffer");
    }
    for (const std::int32_t state : {here.reads, here.keeps}) {
        const std::int32_t regs = state < 0 ? -1 : registers_of(p, n, state, Role::State);
        if (state >= 0 && (regs < 0 || !indexes_like(p, regs, n, false))) {
            return std::unexpected("heinzel::loops: a leaf reading or keeping state stands below that State's Registers, indexed by their chain");
        }
    }
    const std::int32_t slotted = opens ? here.keeps : here.shifted ? -1 : here.reads; // (whose state's slot it names; a shifted read's is the skew's)
    const std::int32_t lag = p.stmts[static_cast<std::size_t>(stage.first + here.stmt)].in[0].back;
    const Access& from = p.stmts[static_cast<std::size_t>(stage.first + here.stmt)].in[0];
    const Access& kept = slotted >= 0 && slotted < stage.count ? p.stmts[static_cast<std::size_t>(stage.first + slotted)].store : from;
    if (slotted >= 0 && (here.stmt >= slotted || from.source != Source::Buffer || kept.source != Source::Buffer || from.buffer != kept.buffer)) {
        return std::unexpected("heinzel::loops: a reader of state, and its Open, read the buffer its keeper stores, before the keeper");
    }
    if ((slotted < 0 && here.slot != 0) ||
        (slotted >= 0 && (here.slot != lag - 1 || here.slot < 0 || here.slot >= node_at(p, registers_of(p, n, slotted, Role::State)).slots))) {
        return std::unexpected("heinzel::loops: a reader of state, and its Open, take the slot of its lag; no other leaf names one");
    }
    if (here.kind == NodeKind::Close) {
        return check_close(p, n, stage);
    }
    if (here.kind == NodeKind::Collect) {
        const std::int32_t turns = ancestor(p, n, [](const Node& a) { return a.kind == NodeKind::Turns; });
        if (turns < 0 || literal_of(stage.axes[static_cast<std::size_t>(node_at(p, turns).axis)]) !=
                             p.stmts[static_cast<std::size_t>(stage.first + here.stmt)].gathered) {
            return std::unexpected("heinzel::loops: a Collect stands below the turns of its Select, as many as it gathers");
        }
    }
    return {};
}

// Where each node stands: the Stage nodes are the roots, everything else stands below one — and a Vector's kids are
// leaves. Returns each node's stage.
constexpr std::expected<std::vector<std::int32_t>, Refusal> place_nodes(const Program& p) {
    std::vector<std::int32_t> stage(p.nodes.size(), -1);
    for (std::size_t s = 0; s < p.stages.size(); ++s) {
        stage[static_cast<std::size_t>(p.stages[s].root)] = static_cast<std::int32_t>(s);
    }
    for (std::size_t n = 0; n < p.nodes.size(); ++n) {
        const Node& here = p.nodes[n];
        const bool root = here.parent < 0;
        if (root != (here.kind == NodeKind::Stage) || (!root && node_at(p, here.parent).kind == NodeKind::Vector && !is_leaf(here.kind))) {
            return std::unexpected("heinzel::loops: Stage nodes are the roots, everything else stands below one, only leaves below a Vector");
        }
        stage[n] = root ? stage[n] : stage[static_cast<std::size_t>(here.parent)];
    }
    return stage;
}

// Each node against the rules of its kind.
constexpr Checked check_node(const Program& p, std::int32_t n, const Stage& stage, bool folds) {
    const NodeKind kind = node_at(p, n).kind;
    if (kind == NodeKind::Loop || kind == NodeKind::Unroll || kind == NodeKind::Vector || kind == NodeKind::Turns) {
        return check_walking(p, n, stage, folds);
    }
    if (kind == NodeKind::Registers || kind == NodeKind::WhenLast || kind == NodeKind::WhenNotLast) {
        return check_scope(p, n, stage);
    }
    return is_leaf(kind) ? check_leaf(p, n, stage) : Checked{};
}

// What the one pass over the nodes learns about each statement (by global index): how many leaves of each kind
// it has, whether its Compute reads a State, and whether a loop that chunks it encloses its Registers.
struct Leaves {
    std::array<std::int32_t, 9> count{}; // Compute, Init, Accumulate, Finish, Partial, Open, Close, Collect, Rank
    std::int32_t reads{-1};              // the State its Compute reads
    std::int32_t opens{-1};              // the State its Open opens
    bool resumes{false};
    bool closes{false};
    bool shifted{false};                 // its Compute reads that State shifted, operand 0 its fill: no Open, no Close
    std::int32_t state{-1};              // the State its Compute reads, by global index (not shifted)
    std::int32_t maker{-1};              // its value-making leaf (Compute, Finish, Rank)
};

constexpr void note_leaf(const Program& p, std::int32_t n, std::int32_t first, std::vector<Leaves>& seen) {
    const Node& leaf = node_at(p, n);
    Leaves& of = seen[static_cast<std::size_t>(first + leaf.stmt)];
    ++of.count[static_cast<std::size_t>(leaf.kind) - static_cast<std::size_t>(NodeKind::Compute)];
    of.reads = leaf.kind == NodeKind::Compute ? leaf.reads : of.reads;
    of.opens = leaf.kind == NodeKind::Open ? leaf.keeps : of.opens;
    of.closes = leaf.kind == NodeKind::Compute || leaf.kind == NodeKind::Finish ? leaf.closes : of.closes;
    of.shifted = leaf.kind == NodeKind::Compute ? leaf.shifted : of.shifted;
    of.state = leaf.kind == NodeKind::Compute && leaf.reads >= 0 && !leaf.shifted ? first + leaf.reads : of.state;
    of.maker = makes_a_value(leaf.kind) ? n : of.maker;
    if (leaf.kind == NodeKind::Accumulate) {
        const std::int32_t regs = registers_of(p, n, leaf.stmt, Role::Accumulator);
        of.resumes = ancestor(p, regs, [](const Node& a) { return a.kind == NodeKind::Loop && a.chunks_fold; }) >= 0;
    }
}

// Every statement has exactly its leaves: a Compute (and an Open of the State it reads, where it reads one, and a Close
// where that State closes), a fold's Init, Accumulate, Finish (and a Partial where a loop that chunks it encloses its
// Registers), a Select's Collect and Rank. A shifted read has no Open: its operand 0 is its fill, and a real lane takes
// the lane below as the step before made it.
constexpr Checked check_counts(const Stmt& st, const Leaves& of, bool closed) {
    std::array<std::int32_t, 9> want{};
    const std::int32_t opened = of.shifted ? -1 : of.reads;
    if (st.kind == StmtKind::Fold) {
        want = {0, 1, 1, 1, of.resumes ? 1 : 0, 0, 0, 0, 0};
    } else if (st.kind == StmtKind::Select) {
        want = {0, 0, 0, 0, 0, 0, 0, 1, 1};
    } else {
        want = {1, 0, 0, 0, 0, opened >= 0 ? 1 : 0, closed ? 1 : 0, 0, 0};
    }
    if (of.count != want || of.opens != opened) {
        return std::unexpected("heinzel::loops: every statement has exactly its leaves");
    }
    return {};
}

// Every node against its kind's rules, one Vector axis and unique strip ordinals per stage, and what each
// statement's leaves are.
// Does an Open below State `n` fill each of its slots from the stored past? (A leaf's slot lies within its State's.) One
// only shifted reads read is opened by none: no real lane reads what an Open put there.
constexpr bool opens_every_slot(const Program& p, std::int32_t n) {
    std::uint64_t opened = 0;
    bool shifted = false;
    for (std::size_t k = static_cast<std::size_t>(n) + 1; k < p.nodes.size(); ++k) {
        const Node& leaf = p.nodes[k];
        const bool opens = leaf.kind == NodeKind::Open && leaf.keeps == node_at(p, n).stmt && encloses(p, n, static_cast<std::int32_t>(k));
        opened |= opens ? std::uint64_t{1} << leaf.slot : 0;
        shifted = shifted || (leaf.shifted && leaf.reads == node_at(p, n).stmt && encloses(p, n, static_cast<std::int32_t>(k)));
    }
    return std::countr_one(opened) >= node_at(p, n).slots || (opened == 0 && shifted);
}

constexpr Checked check_nodes(const Program& p, const std::vector<std::int32_t>& stage_of, std::vector<Leaves>& seen) {
    std::vector<std::int32_t> lanes(p.stages.size(), -1);
    std::vector<std::uint32_t> strips(p.stages.size(), 0);
    std::vector<bool> folds(p.stages.size(), false);
    for (std::size_t s = 0; s < p.stages.size(); ++s) {
        for (std::int32_t k = 0; k < p.stages[s].count; ++k) {
            folds[s] = folds[s] || p.stmts[static_cast<std::size_t>(p.stages[s].first + k)].kind == StmtKind::Fold;
        }
    }
    for (std::size_t n = 0; n < p.nodes.size(); ++n) {
        const std::int32_t s = stage_of[n];
        const Node& here = p.nodes[n];
        if (here.kind == NodeKind::Stage) {
            continue;
        }
        const Stage& stage = p.stages[static_cast<std::size_t>(s)];
        if (const Checked r = check_node(p, static_cast<std::int32_t>(n), stage, folds[static_cast<std::size_t>(s)]); !r) {
            return r;
        }
        std::int32_t& lane = lanes[static_cast<std::size_t>(s)];
        if (here.kind == NodeKind::Vector && lane >= 0 && lane != here.axis) {
            return std::unexpected("heinzel::loops: every Vector of a stage walks one axis, the stage's lane axis");
        }
        lane = here.kind == NodeKind::Vector ? here.axis : lane;
        std::uint32_t& used = strips[static_cast<std::size_t>(s)];
        if (is_strip(here) && (used >> here.strip & 1u) != 0) {
            return std::unexpected("heinzel::loops: strip ordinals are unique within a stage");
        }
        used |= is_strip(here) ? 1u << here.strip : 0u;
        if (is_leaf(here.kind) && here.kind != NodeKind::Prefetch) {
            note_leaf(p, static_cast<std::int32_t>(n), stage.first, seen);
        }
    }
    for (std::size_t st = 0; st < p.stmts.size(); ++st) {
        const std::int32_t state = seen[st].state;
        if (const Checked r = check_counts(p.stmts[st], seen[st], state >= 0 && seen[static_cast<std::size_t>(state)].closes); !r) {
            return r;
        }
    }
    for (std::size_t n = 0; n < p.nodes.size(); ++n) {
        if (p.nodes[n].kind == NodeKind::Registers && p.nodes[n].role == Role::State && !opens_every_slot(p, static_cast<std::int32_t>(n))) {
            return std::unexpected("heinzel::loops: every slot of a State is opened below it");
        }
    }
    return {};
}

// A local operand's maker is an earlier sibling of its reader or of one of the reader's ancestors in the stage:
// what a loop makes does not leave it.
constexpr bool reaches(const Program& p, std::int32_t maker, std::int32_t reader) {
    for (std::int32_t at = reader; at >= 0 && node_at(p, at).kind != NodeKind::Stage; at = node_at(p, at).parent) {
        if (node_at(p, maker).parent == node_at(p, at).parent && maker < at) {
            return true;
        }
    }
    return false;
}

// Within each run of siblings, the leaves that make a value come in statement order, each after every value
// already in reach (the runner indexes values by statement); the other leaves in statement order among their
// own kind. Every local an operand reads is in reach of its reader.
constexpr Checked check_order(const Program& p, const std::vector<std::int32_t>& stage_of, const std::vector<Leaves>& seen) {
    std::vector<std::int32_t> in_reach(p.nodes.size(), -1);
    for (std::size_t n = 0; n < p.nodes.size(); ++n) {
        std::int32_t made = p.nodes[n].kind == NodeKind::Stage ? -1 : in_reach[n];
        std::array<std::int32_t, 9> last{-1, -1, -1, -1, -1, -1, -1, -1, -1};
        for (std::int32_t k = p.nodes[n].first_kid; k >= 0; k = node_at(p, k).next_sibling) {
            const Node& kid = node_at(p, k);
            in_reach[static_cast<std::size_t>(k)] = made;
            if (!is_leaf(kid.kind) || kid.kind == NodeKind::Prefetch) {
                continue;
            }
            std::int32_t& before = makes_a_value(kid.kind) ? made : last[static_cast<std::size_t>(kid.kind) - static_cast<std::size_t>(NodeKind::Compute)];
            if (kid.stmt <= before) {
                return std::unexpected("heinzel::loops: leaves come in statement order, a value after every value in reach");
            }
            before = kid.stmt;
            const bool reads = (kid.kind == NodeKind::Compute && (kid.reads < 0 || kid.shifted)) || kid.kind == NodeKind::Accumulate || kid.kind == NodeKind::Collect;
            const std::int32_t first = p.stages[static_cast<std::size_t>(stage_of[static_cast<std::size_t>(k)])].first;
            for (const Access& a : p.stmts[static_cast<std::size_t>(first + kid.stmt)].in) {
                const std::int32_t maker = a.source == Source::Local ? seen[static_cast<std::size_t>(first + a.local)].maker : 0;
                if (reads && a.source == Source::Local && (maker < 0 || !reaches(p, maker, k))) {
                    return std::unexpected("heinzel::loops: a local operand's maker is an earlier sibling of its reader or of an ancestor");
                }
            }
        }
    }
    return {};
}

} // namespace detail

// Does the runner accept this tree? The error names the first rule it breaks.
constexpr std::expected<void, Refusal> well_formed(const Program& p) {
    if (const detail::Checked r = detail::check_links(p); !r) {
        return r;
    }
    if (const detail::Checked r = detail::check_constants(p); !r) {
        return r;
    }
    if (const detail::Checked r = detail::check_stages(p); !r) {
        return r;
    }
    const std::expected<std::vector<std::int32_t>, Refusal> stage_of = detail::place_nodes(p);
    if (!stage_of) {
        return std::unexpected(stage_of.error());
    }
    for (const Stage& stage : p.stages) {
        for (std::int32_t s = 0; s < stage.count; ++s) {
            if (const detail::Checked r = detail::check_accesses(p, stage, s); !r) {
                return r;
            }
        }
    }
    std::vector<detail::Leaves> seen(p.stmts.size());
    if (const detail::Checked r = detail::check_nodes(p, *stage_of, seen); !r) {
        return r;
    }
    if (const detail::Checked r = detail::check_order(p, *stage_of, seen); !r) {
        return r;
    }
    std::vector<std::int32_t> reads(p.stmts.size(), -1);
    std::vector<char> shifted(p.stmts.size(), 0);
    for (std::size_t st = 0; st < p.stmts.size(); ++st) {
        reads[st] = seen[st].reads;
        shifted[st] = seen[st].shifted ? 1 : 0;
    }
    if (const detail::Checked r = detail::check_closes(p, *stage_of, reads); !r) {
        return r;
    }
    detail::Checked r = detail::check_fields(p, *stage_of); // (each check below relies on those before it)
    r = r ? detail::check_bands(p) : r;
    const std::expected<detail::LaneAxes, Refusal> lanes = r ? detail::lane_axes(p, *stage_of) : std::unexpected(r.error());
    r = lanes ? detail::check_lanes(p, *lanes) : std::unexpected(lanes.error());
    r = r ? detail::check_blocks(p, *lanes, *stage_of) : r;
    r = r ? detail::check_nesting(p, *lanes, *stage_of) : r;
    r = r ? detail::check_restarts(p, *stage_of) : r;
    r = r ? detail::check_coverage(p, *stage_of) : r;
    r = r ? detail::check_skew(p, *lanes, *stage_of) : r;
    r = r ? detail::check_past(p, shifted) : r;
    r = r ? detail::check_splits(p) : r;
    r = r ? detail::check_starts(p) : r;
    return r ? detail::check_kept(p) : r;
}

// What an `Executor` needs beyond the runner: it copies each output out whole, so no output is a feed, a band's buffer
// or a relaid copy (a constant it copies from its one sample). The lowering makes none, and the `Executor` no longer
// asks (Plan/15-zero-overhead/03); the programs it holds are held to it here (`executed.hpp`, `executor_test`).
constexpr bool outputs_whole(const Program& p) {
    for (const std::int32_t out : p.outputs) {
        const Buffer& b = p.buffers[static_cast<std::size_t>(out)];
        if (b.home == Home::Feed || b.band >= 0 || b.blocked_axis >= 0) {
            return false;
        }
    }
    return true;
}

// For a test that runs `P`: a build holding a tree the runner does not accept stops here, naming the rule.
template <const Program& P>
consteval bool accepted() {
#if __cplusplus > 202302L
    static_assert(loops::well_formed(P).has_value(), loops::well_formed(P).error_or(""));
#else // (the C++23 floor: clang takes a computed message there only as an extension)
    static_assert(loops::well_formed(P).has_value(), "heinzel::loops: the program is not well formed (loops::well_formed names the rule)");
#endif
    return true;
}

} // namespace heinzel::loops
