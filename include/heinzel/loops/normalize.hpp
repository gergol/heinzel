#pragma once

// Normalization appends a stage to the program as a tree in the normal form the runner expects. From what its inner
// loops imply (`implied.hpp`) it works out the register point and its registers, the chain of enumerators that every
// nest repeats, and the nests themselves (open, prefetch, init, update, after, or a resumed fold's two); a feedback stage
// whose sample holds a fold grows one shape of its own (`sample_tree`). It also finds a network's turns, the head
// samples a feed's past needs, and the strip numbers.

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <expected>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/graph/kinds.hpp"
#include "heinzel/loops/implied.hpp"
#include "heinzel/loops/lower_stmts.hpp"
#include "heinzel/loops/primitives/loops.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/tree.hpp"

namespace heinzel::loops::detail {

// The program's tree as it grows. Each node is appended as its parent's last kid, and a stage's strips are numbered in
// pre-order.
struct Tree {
    Program& p;
    std::vector<std::int32_t> last_kid{};
    std::int32_t strips{0};
};

constexpr std::expected<std::int32_t, Refusal> grow(Tree& t, Node n, std::int32_t parent) {
    if (t.p.nodes.size() == kMaxTreeNodes) {
        return std::unexpected("heinzel::compile: more tree nodes than kMaxTreeNodes; rebuild with -DHEINZEL_MAX_TREE_NODES=N");
    }
    if (n.kind == NodeKind::Loop && n.tail != Tail::Clip) {
        if (t.strips == static_cast<std::int32_t>(kMaxStrips)) {
            return std::unexpected("heinzel::compile: a stage needs more strips than kMaxStrips");
        }
        n.strip = t.strips++;
    }
    n.parent = parent;
    const auto at = static_cast<std::int32_t>(t.p.nodes.size());
    t.p.nodes.push_back(n);
    t.last_kid.push_back(-1);
    if (parent >= 0) {
        const std::int32_t before = t.last_kid[static_cast<std::size_t>(parent)];
        (before < 0 ? t.p.nodes[static_cast<std::size_t>(parent)].first_kid : t.p.nodes[static_cast<std::size_t>(before)].next_sibling) = at;
        t.last_kid[static_cast<std::size_t>(parent)] = at;
    }
    return at;
}

// The stage's tree as it grows: the tree node each skeleton node became, the chain of its outer enumerators, the node
// that sizes its registers (the link), and the Registers nodes made so far.
struct Growth {
    std::array<std::int32_t, kMaxStageLoops> node_of{};
    fixed_vector<Node, kMaxStageLoops> chain{};
    std::int32_t link{-1};
    fixed_vector<std::int32_t, 3 * kMaxStageStmts> registers{};
};

// Grows the chain under `parent`, each node inside the one before, and returns the innermost, under which the nest's
// leaves go. When `links` is set, the first node it makes becomes the link.
constexpr std::expected<std::int32_t, Refusal> chain_under(Tree& t, Growth& grown, std::int32_t parent, bool links) {
    std::int32_t at = parent;
    for (const Node& n : grown.chain) {
        const std::expected<std::int32_t, Refusal> made = detail::grow(t, n, at);
        if (!made) {
            return made;
        }
        grown.link = links && at == parent ? *made : grown.link;
        at = *made;
    }
    return at;
}

// Puts the outer enumerators into the chain in jam order: the Unrolls along other axes in loop order, then the Unroll
// along the lane axis, then the Vector.
constexpr void jam(const Formed& f, std::int32_t inner, Growth& grown) {
    std::int32_t lanes = -1;
    for (std::size_t k = 0; k < static_cast<std::size_t>(inner); ++k) {
        lanes = f.skeleton.walk[k].kind == NodeKind::Vector ? f.skeleton.walk[k].axis : lanes;
    }
    for (const int pass : {0, 1, 2}) {
        for (std::size_t k = 0; k < static_cast<std::size_t>(inner); ++k) {
            const Node& n = f.skeleton.walk[k];
            const bool unroll = n.kind == NodeKind::Unroll;
            if ((pass == 0 && unroll && n.axis != lanes) || (pass == 1 && unroll && n.axis == lanes) || (pass == 2 && n.kind == NodeKind::Vector)) {
                grown.chain.push_back(Node{.kind = n.kind, .axis = n.axis, .step = n.step});
            }
        }
    }
}

// Grows the Loops outside the inner ones, each inside the one before, and puts their enumerators into the chain.
// Returns the node the register point goes under.
constexpr std::expected<std::int32_t, Refusal> outer_loops(Tree& t, const Formed& f, const Implied& implied, std::int32_t at, Growth& grown) {
    for (std::size_t k = 0; k < static_cast<std::size_t>(implied.inner); ++k) {
        Node n = f.skeleton.walk[k];
        if (n.kind != NodeKind::Loop) {
            continue;
        }
        for (std::size_t in = static_cast<std::size_t>(implied.inner); detail::chunks(n) && implied.folds && in < f.skeleton.walk.size(); ++in) {
            n.chunks_fold = n.chunks_fold || f.skeleton.walk[in].axis == n.axis;
        }
        const std::expected<std::int32_t, Refusal> made = detail::grow(t, n, at);
        if (!made) {
            return made;
        }
        at = grown.node_of[k] = *made;
    }
    detail::jam(f, implied.inner, grown);
    return at;
}

// Grows the register point: a Registers node for each fold's accumulators, then for each value whose state is handed
// on, then for each network's gathered values, each inside the one before and in statement order. Returns the
// innermost.
constexpr std::expected<std::int32_t, Refusal> register_point(Tree& t, const Formed& f, const Implied& implied, std::int32_t at, Growth& grown) {
    for (const Role role : {Role::Accumulator, Role::State, Role::Gathered}) {
        for (std::size_t s = 0; s < f.stmts.size(); ++s) {
            const Stmt& st = f.stmts[s];
            const bool held = role == Role::Accumulator ? st.kind == StmtKind::Fold : role == Role::State ? implied.slots[s] > 0 : st.kind == StmtKind::Select;
            if (!held) {
                continue;
            }
            const bool first = st.kind == StmtKind::Fold && detail::opens_first(f, implied, st);
            const std::expected<std::int32_t, Refusal> made =
                detail::grow(t, Node{.kind = NodeKind::Registers, .stmt = static_cast<std::int32_t>(s), .role = role, .opens_first = first,
                                     .slots = role == Role::State ? implied.slots[s] : 1},
                             at);
            if (!made) {
                return made;
            }
            at = *made;
            grown.registers.push_back(at);
        }
    }
    return at;
}

// Grows the inner loops, each inside the one before, and returns the innermost. A fold's Loops are peeled where some
// fold opens at its first element. Lanes along the fold's own axis keep their enumerators, the first of which becomes
// the registers' link (the fold-lanes chain). A network's loop becomes its Turns.
constexpr std::expected<std::int32_t, Refusal> inner_loops(Tree& t, const Formed& f, const Implied& implied, std::int32_t at, Growth& grown) {
    for (std::size_t k = static_cast<std::size_t>(implied.inner); k < f.skeleton.walk.size(); ++k) {
        Node n = f.skeleton.walk[k];
        if (implied.selects) {
            n = Node{.kind = NodeKind::Turns, .axis = n.axis, .step = 1};
        } else if (n.kind == NodeKind::Loop && n.tail == Tail::Clip) {
            n.peel_first = implied.opens_first;
        }
        const std::expected<std::int32_t, Refusal> made = detail::grow(t, n, at);
        if (!made) {
            return made;
        }
        grown.link = grown.link < 0 && is_enumerator(n) ? *made : grown.link;
        at = grown.node_of[k] = *made;
    }
    return at;
}

// The leaves of the update nest (every statement inside the inner loops) or of the after nest.
constexpr Done leaves(Tree& t, const Formed& f, const Implied& implied, bool inside, std::int32_t at) {
    for (std::size_t s = 0; s < f.stmts.size(); ++s) {
        const Stmt& st = f.stmts[s];
        const auto index = static_cast<std::int32_t>(s);
        Node n{.stmt = index};
        if (inside && f.inside[s]) {
            n.kind = st.kind == StmtKind::Fold ? NodeKind::Accumulate : st.kind == StmtKind::Select ? NodeKind::Collect : NodeKind::Compute;
            n.reads = n.kind == NodeKind::Compute ? implied.carries[s] : -1;
            n.slot = n.reads >= 0 ? implied.slot[s] : 0;
            n.keeps = n.kind == NodeKind::Compute && implied.slots[s] > 0 ? index : -1;
            n.closes = n.keeps >= 0 && implied.closes[s];
            n.shifted = n.reads >= 0 && detail::is_a_link(st);
            n.lane = implied.lane[s];
        } else if (!inside && (st.kind == StmtKind::Fold || st.kind == StmtKind::Select || !f.inside[s])) {
            n.kind = st.kind == StmtKind::Fold ? NodeKind::Finish : st.kind == StmtKind::Select ? NodeKind::Rank : NodeKind::Compute;
        } else {
            continue;
        }
        if (const std::expected<std::int32_t, Refusal> made = detail::grow(t, n, at); !made) {
            return std::unexpected(made.error());
        }
    }
    return {};
}

// The after nest, which follows the inner loops and holds what comes after the fold. Where the fold resumes across
// chunks, that runs only after the last chunk (`WhenLast`); after every other chunk the fold's partial results are
// stored instead (`WhenNotLast`).
constexpr Done after_nest(Tree& t, const Formed& f, const Implied& implied, Growth& grown, std::int32_t regs) {
    bool any = false;
    for (std::size_t s = 0; s < f.stmts.size(); ++s) {
        any = any || f.stmts[s].kind == StmtKind::Fold || f.stmts[s].kind == StmtKind::Select || !f.inside[s];
    }
    if (!any) {
        return {}; // a nest with no leaf is omitted: state handed on has nothing after its loop
    }
    if (!implied.resumes) {
        const std::expected<std::int32_t, Refusal> at = detail::chain_under(t, grown, regs, false);
        return at ? detail::leaves(t, f, implied, false, *at) : Done{std::unexpected(at.error())};
    }
    const std::expected<std::int32_t, Refusal> last = detail::grow(t, Node{.kind = NodeKind::WhenLast}, regs);
    const std::expected<std::int32_t, Refusal> after = last ? detail::chain_under(t, grown, *last, false) : last;
    if (const Done r = after ? detail::leaves(t, f, implied, false, *after) : Done{std::unexpected(after.error())}; !r) {
        return r;
    }
    const std::expected<std::int32_t, Refusal> other = detail::grow(t, Node{.kind = NodeKind::WhenNotLast}, regs);
    const std::expected<std::int32_t, Refusal> partials = other ? detail::chain_under(t, grown, *other, false) : other;
    for (std::size_t s = 0; partials && s < f.stmts.size(); ++s) {
        if (f.stmts[s].kind == StmtKind::Fold) {
            if (const std::expected<std::int32_t, Refusal> made = detail::grow(t, Node{.kind = NodeKind::Partial, .stmt = static_cast<std::int32_t>(s)}, *partials); !made) {
                return std::unexpected(made.error());
            }
        }
    }
    return partials ? Done{} : Done{std::unexpected(partials.error())};
}

// Does reader r of state open its slot from the stored past (and close it back)? Each does but a link, whose register
// lane 0 takes from its fill and every other lane from the lane below (`Node::shifted`).
constexpr bool opens(const Formed& f, const Implied& implied, std::size_t r) {
    return implied.carries[r] >= 0 && !detail::is_a_link(f.stmts[r]);
}

// The close nest, after the inner loops, the open nest's mirror: each slot of a state that closes written back where its
// Open read it.
constexpr Done close_nest(Tree& t, const Formed& f, const Implied& implied, Growth& grown, std::int32_t regs) {
    const auto closed = [&](std::size_t r) { return detail::opens(f, implied, r) && implied.closes[static_cast<std::size_t>(implied.carries[r])]; };
    bool any = false;
    for (std::size_t r = 0; r < f.stmts.size(); ++r) {
        any = any || closed(r);
    }
    if (!any) {
        return {};
    }
    const std::expected<std::int32_t, Refusal> at = detail::chain_under(t, grown, regs, false);
    for (std::size_t r = 0; at && r < f.stmts.size(); ++r) {
        const std::int32_t keeper = implied.carries[r];
        if (closed(r) && !detail::grow(t, Node{.kind = NodeKind::Close, .stmt = static_cast<std::int32_t>(r), .keeps = keeper, .slot = implied.slot[r]}, *at)) {
            return std::unexpected("heinzel::compile: more tree nodes than kMaxTreeNodes; rebuild with -DHEINZEL_MAX_TREE_NODES=N");
        }
    }
    return at ? Done{} : Done{std::unexpected(at.error())};
}

// A state that closes holds its past alone: its buffer has no block, and neither its readers nor its keeper walk time
// through it, so its Opens and Closes stand at its past in every run, chunk and head sample. `first` is the stage's
// first statement in the program.
constexpr void held_in_place(Lowering& l, const Formed& f, const Implied& implied, std::size_t first) {
    const auto time = f.axes.size() - 1;
    for (std::size_t s = 0; s < f.stmts.size(); ++s) {
        if (!implied.closes[s]) {
            continue;
        }
        const std::int32_t b = f.stmts[s].store.buffer;
        l.out.buffers[static_cast<std::size_t>(b)].only_past = true;
        for (std::size_t k = first; k < l.out.stmts.size(); ++k) {
            for (std::size_t slot = 0; slot <= kMaxNodeInputs; ++slot) {
                Access& a = loops::access_in(l.out.stmts[k], slot);
                a.buffer_axis[time] = a.source == Source::Buffer && a.buffer == b ? -1 : a.buffer_axis[time];
            }
        }
    }
}

// A stage with inner loops grows its outer loops, then the register point, and inside it, in order: the open nest, a
// prefetch, the init nest, the inner loops with the update nest innermost, the after nest and the close nest.
constexpr Done nested_tree(Tree& t, const Formed& f, const Implied& implied, std::int32_t at, Growth& grown) {
    const std::expected<std::int32_t, Refusal> outer = detail::outer_loops(t, f, implied, at, grown);
    const std::expected<std::int32_t, Refusal> regs = outer ? detail::register_point(t, f, implied, *outer, grown) : outer;
    if (!regs) {
        return std::unexpected(regs.error());
    }
    bool hands_on = false;
    bool folds = false;
    for (std::size_t r = 0; r < f.stmts.size(); ++r) {
        hands_on = hands_on || detail::opens(f, implied, r);
        folds = folds || f.stmts[r].kind == StmtKind::Fold;
    }
    const std::expected<std::int32_t, Refusal> open = hands_on ? detail::chain_under(t, grown, *regs, false) : *regs;
    for (std::size_t r = 0; open && hands_on && r < f.stmts.size(); ++r) {
        if (detail::opens(f, implied, r) &&
            !detail::grow(t, Node{.kind = NodeKind::Open, .stmt = static_cast<std::int32_t>(r), .keeps = implied.carries[r], .slot = implied.slot[r]}, *open)) {
            return std::unexpected("heinzel::compile: more tree nodes than kMaxTreeNodes; rebuild with -DHEINZEL_MAX_TREE_NODES=N");
        }
    }
    if (f.prefetch >= 0) {
        const std::int32_t link = grown.node_of[static_cast<std::size_t>(f.prefetch)];
        if (!detail::grow(t, Node{.kind = NodeKind::Prefetch, .link = link}, *regs)) {
            return std::unexpected("heinzel::compile: more tree nodes than kMaxTreeNodes; rebuild with -DHEINZEL_MAX_TREE_NODES=N");
        }
    }
    const std::expected<std::int32_t, Refusal> init = !open ? open : !folds ? *regs : detail::chain_under(t, grown, *regs, false);
    for (std::size_t s = 0; init && folds && s < f.stmts.size(); ++s) {
        if (f.stmts[s].kind == StmtKind::Fold && !detail::grow(t, Node{.kind = NodeKind::Init, .stmt = static_cast<std::int32_t>(s)}, *init)) {
            return std::unexpected("heinzel::compile: more tree nodes than kMaxTreeNodes; rebuild with -DHEINZEL_MAX_TREE_NODES=N");
        }
    }
    const std::expected<std::int32_t, Refusal> innermost = init ? detail::inner_loops(t, f, implied, *regs, grown) : init;
    const std::expected<std::int32_t, Refusal> update = !innermost ? innermost : detail::chain_under(t, grown, *innermost, true);
    if (const Done r = update ? detail::leaves(t, f, implied, true, *update) : Done{std::unexpected(update.error())}; !r) {
        return r;
    }
    if (const Done r = detail::after_nest(t, f, implied, grown, *regs); !r) {
        return r;
    }
    if (const Done r = detail::close_nest(t, f, implied, grown, *regs); !r) {
        return r;
    }
    for (const std::int32_t r : grown.registers) {
        t.p.nodes[static_cast<std::size_t>(r)].link = grown.link;
    }
    return {};
}

// A stage without inner loops is its skeleton's walk, with every statement a Compute under the innermost node.
constexpr Done plain_tree(Tree& t, const Formed& f, std::int32_t at, Growth& grown) {
    for (std::size_t k = 0; k < f.skeleton.walk.size(); ++k) {
        const std::expected<std::int32_t, Refusal> made = detail::grow(t, f.skeleton.walk[k], at);
        if (!made) {
            return std::unexpected(made.error());
        }
        at = grown.node_of[k] = *made;
    }
    for (std::size_t s = 0; s < f.stmts.size(); ++s) {
        if (!detail::grow(t, Node{.kind = NodeKind::Compute, .stmt = static_cast<std::int32_t>(s)}, at)) {
            return std::unexpected("heinzel::compile: more tree nodes than kMaxTreeNodes; rebuild with -DHEINZEL_MAX_TREE_NODES=N");
        }
    }
    return {};
}

// ---- a feedback stage whose sample holds a fold (Plan/23 10) ------------------------------------------------------
//
// Its voices' strip and their chain outside; the State registers of what it hands on, a lane state's sized by the chain,
// a one-voice state's one body at width one; their Opens; the time loop, and in it each sample: the part before the
// fold at width one, the fold's Registers (its Init, the lanes in the chain with its Accumulate, its Finish, the part
// after it at width one); the Closes after the time loop. A statement's part is read off the planner's order: inside the
// fold's loops, the lanes (the fold among them); over its result, before the fold where it comes before it, else after.

// Statement s's leaf in a sample: a Compute (or the fold's Finish) reading the State it reads, keeping its value where
// a later sample reads it, and closing it where its registers alone hold it.
constexpr Node stated(const Implied& implied, std::size_t s, NodeKind kind) {
    Node n{.kind = kind, .stmt = static_cast<std::int32_t>(s), .reads = kind == NodeKind::Compute ? implied.carries[s] : -1};
    n.slot = n.reads >= 0 ? implied.slot[s] : 0;
    n.keeps = implied.slots[s] > 0 ? n.stmt : -1;
    n.closes = n.keeps >= 0 && implied.closes[s];
    return n;
}

// Is statement s's state a lane state (made in the lanes), not one voice's (made before or after the fold, or the fold's
// own value)?
constexpr bool in_the_lanes(const Formed& f, std::int32_t s) {
    return f.inside[static_cast<std::size_t>(s)] && f.stmts[static_cast<std::size_t>(s)].kind != StmtKind::Fold;
}

// The State registers, each inside the one before, in statement order. A lane state's join `grown.registers`, which
// the chain sizes; a one-voice state's have no link. Returns the innermost.
constexpr std::expected<std::int32_t, Refusal> sample_states(Tree& t, const Formed& f, const Implied& implied, std::int32_t at, Growth& grown) {
    for (std::size_t s = 0; s < f.stmts.size(); ++s) {
        if (implied.slots[s] == 0) {
            continue;
        }
        const std::expected<std::int32_t, Refusal> made =
            detail::grow(t, Node{.kind = NodeKind::Registers, .stmt = static_cast<std::int32_t>(s), .role = Role::State, .slots = implied.slots[s]}, at);
        if (!made) {
            return made;
        }
        at = *made;
        if (detail::in_the_lanes(f, static_cast<std::int32_t>(s))) {
            grown.registers.push_back(at);
        }
    }
    return at;
}

// The Opens of the states (`close`: the Closes of those that close) under `at`: a lane state's under a copy of the
// chain, then a one-voice state's outside every chain.
constexpr Done sample_ends(Tree& t, const Formed& f, const Implied& implied, Growth& grown, std::int32_t at, bool close) {
    for (const bool lanes : {true, false}) {
        const auto ends = [&](std::size_t r) {
            const std::int32_t keeper = implied.carries[r];
            return keeper >= 0 && detail::in_the_lanes(f, keeper) == lanes && (!close || implied.closes[static_cast<std::size_t>(keeper)]);
        };
        bool any = false;
        for (std::size_t r = 0; r < f.stmts.size(); ++r) {
            any = any || ends(r);
        }
        const std::expected<std::int32_t, Refusal> under = any && lanes ? detail::chain_under(t, grown, at, false) : at;
        for (std::size_t r = 0; under && any && r < f.stmts.size(); ++r) {
            const Node n{.kind = close ? NodeKind::Close : NodeKind::Open, .stmt = static_cast<std::int32_t>(r), .keeps = implied.carries[r], .slot = implied.slot[r]};
            if (ends(r) && !detail::grow(t, n, *under)) {
                return std::unexpected("heinzel::compile: more tree nodes than kMaxTreeNodes; rebuild with -DHEINZEL_MAX_TREE_NODES=N");
            }
        }
        if (!under) {
            return std::unexpected(under.error());
        }
    }
    return {};
}

// One sample, under the time loop: the part before the fold, then the fold's Registers holding its Init, the lanes in
// the chain (the Accumulate among them), its Finish and the part after it.
constexpr Done one_sample(Tree& t, const Formed& f, const Implied& implied, std::int32_t time, Growth& grown) {
    const auto fold = static_cast<std::size_t>(implied.fold);
    bool grew = true;
    for (std::size_t s = 0; s < fold; ++s) {
        grew = grew && (f.inside[s] || detail::grow(t, detail::stated(implied, s, NodeKind::Compute), time));
    }
    const Node registers{.kind = NodeKind::Registers, .stmt = implied.fold, .role = Role::Accumulator, .opens_first = detail::opens_first(f, implied, f.stmts[fold])};
    const std::expected<std::int32_t, Refusal> regs = grew ? detail::grow(t, registers, time) : std::unexpected(Refusal{});
    const std::expected<std::int32_t, Refusal> init = regs ? detail::grow(t, Node{.kind = NodeKind::Init, .stmt = implied.fold}, *regs) : regs;
    const std::expected<std::int32_t, Refusal> lanes = init ? detail::chain_under(t, grown, *regs, true) : init;
    for (std::size_t s = 0; lanes && grew && s < f.stmts.size(); ++s) {
        const Node n = s == fold ? Node{.kind = NodeKind::Accumulate, .stmt = implied.fold} : detail::stated(implied, s, NodeKind::Compute);
        grew = !f.inside[s] || detail::grow(t, n, *lanes);
    }
    grew = grew && lanes && detail::grow(t, detail::stated(implied, fold, NodeKind::Finish), *regs);
    for (std::size_t s = fold + 1; grew && s < f.stmts.size(); ++s) {
        grew = f.inside[s] || detail::grow(t, detail::stated(implied, s, NodeKind::Compute), *regs);
    }
    if (!grew) {
        return std::unexpected("heinzel::compile: more tree nodes than kMaxTreeNodes; rebuild with -DHEINZEL_MAX_TREE_NODES=N");
    }
    grown.registers.push_back(*regs);
    return {};
}

// The stage's tree: its outer loops, the State registers and their Opens, the time loop with one sample in it, the Closes.
constexpr Done sample_tree(Tree& t, const Formed& f, const Implied& implied, std::int32_t at, Growth& grown) {
    const std::expected<std::int32_t, Refusal> outer = detail::outer_loops(t, f, implied, at, grown);
    const std::expected<std::int32_t, Refusal> states = outer ? detail::sample_states(t, f, implied, *outer, grown) : outer;
    if (!states) {
        return std::unexpected(states.error());
    }
    const auto inner = static_cast<std::size_t>(implied.inner);
    const Done opened = detail::sample_ends(t, f, implied, grown, *states, false);
    const std::expected<std::int32_t, Refusal> time = opened ? detail::grow(t, f.skeleton.walk[inner], *states) : std::unexpected(opened.error());
    if (!time) {
        return std::unexpected(time.error());
    }
    grown.node_of[inner] = *time;
    if (const Done r = detail::one_sample(t, f, implied, *time, grown); !r) {
        return r;
    }
    if (const Done r = detail::sample_ends(t, f, implied, grown, *states, true); !r) {
        return r;
    }
    for (const std::int32_t r : grown.registers) {
        t.p.nodes[static_cast<std::size_t>(r)].link = grown.link;
    }
    return {};
}

// Head samples. A stage that reads a feed's past runs its first samples one at a time, as many as the deepest past it
// reads, so that those reads can reach into the feed's tail. They are walked by the outermost Loop whose axis walks the
// feed's streamed axis in the first such read.
constexpr Done head_samples(const Program& p, const Formed& f, const Growth& grown, Stage& stage) {
    for (const Stmt& st : f.stmts) {
        for (std::size_t slot = 0; slot <= kMaxNodeInputs; ++slot) {
            const Access& a = loops::access_in(st, slot);
            if (a.source != Source::Buffer || a.back == 0 || p.buffers[static_cast<std::size_t>(a.buffer)].home != Home::Feed) {
                continue;
            }
            const std::int32_t time = p.buffers[static_cast<std::size_t>(a.buffer)].shape.rank - 1;
            for (std::size_t k = 0; stage.time_loop < 0 && k < f.skeleton.walk.size(); ++k) {
                const Node& n = f.skeleton.walk[k];
                stage.time_loop = n.kind == NodeKind::Loop && a.buffer_axis[static_cast<std::size_t>(n.axis)] == time ? grown.node_of[k] : -1;
            }
            stage.head = a.back > stage.head ? a.back : stage.head;
        }
    }
    if (stage.head > 0 && stage.time_loop < 0) {
        return std::unexpected("heinzel::compile: a stage reads a feed's past along no loop of its own");
    }
    return {};
}

// Appends stage `f` as the program's next stage: its statements to `Program::stmts`, and its tree, in the normal form.
constexpr Done normalize(Lowering& l, Tree& t, const Formed& f, const Implied& implied) {
    if (l.out.stmts.size() + f.stmts.size() > kMaxStmts) {
        return std::unexpected("heinzel::compile: more statements than kMaxStmts; rebuild with -DHEINZEL_MAX_STMTS=N");
    }
    Stage stage{.first = static_cast<std::int32_t>(l.out.stmts.size()), .count = static_cast<std::int32_t>(f.stmts.size()), .band_axis = f.band_axis, .lead = f.lead};
    for (const Extent e : f.axes) {
        stage.axes.push_back(e);
    }
    for (const Stmt& st : f.stmts) {
        l.out.stmts.push_back(st);
    }
    detail::held_in_place(l, f, implied, static_cast<std::size_t>(stage.first));
    t.strips = 0;
    const std::expected<std::int32_t, Refusal> root = detail::grow(t, Node{.kind = NodeKind::Stage}, -1);
    if (!root) {
        return std::unexpected(root.error());
    }
    stage.root = *root;
    Growth grown{};
    grown.node_of.fill(-1);
    const bool inner = implied.inner < static_cast<std::int32_t>(f.skeleton.walk.size());
    const Done grew = !inner          ? detail::plain_tree(t, f, *root, grown)
                      : implied.sample ? detail::sample_tree(t, f, implied, *root, grown)
                                       : detail::nested_tree(t, f, implied, *root, grown);
    if (const Done r = grew; !r) {
        return r;
    }
    stage.split = f.split >= 0 ? grown.node_of[static_cast<std::size_t>(f.split)] : -1;
    if (const Done r = detail::head_samples(l.out, f, grown, stage); !r) {
        return r;
    }
    l.out.stages.push_back(stage);
    return {};
}

} // namespace heinzel::loops::detail
