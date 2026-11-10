#pragma once

// What a stage's loops imply, worked out before its tree grows (`normalize.hpp`) and held against the rules of loop
// shape (`validate.hpp`): where its inner loops start, whether they hold a fold, a network or lanes along the fold's own
// axis, which state is handed from one sample to the next and which of it closes, whether a fold opens at its first
// element or may resume, and in a skewed stage the lane each statement stands at.

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <expected>

#include "heinzel/ir/ir.hpp"
#include "heinzel/loops/lower_stmts.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/schedule.hpp"

namespace heinzel::loops::detail {

// What a stage's skeleton and statements imply. Where its inner loops start (the walk's size when it has none) and
// whether its fold resumes come from the schedule; the rest is what those loops hold, and which statement's state each
// reader reads (-1: none). A stage whose inner loop is its time loop and which holds a fold folds every sample
// (`sample`, Plan/23 10): its fold restarts inside the time loop, between the part before it and the part after.
struct Implied {
    std::int32_t inner{0};
    bool folds{false};
    bool selects{false};
    bool fold_lanes{false};
    bool opens_first{false};
    bool resumes{false};
    bool sample{false};
    std::int32_t fold{-1}; // its first Fold statement
    std::array<std::int32_t, kMaxStageStmts> carries{};
    std::array<std::int32_t, kMaxStageStmts> slot{};  // a reader: the slot of its state, its lag less one
    std::array<std::int32_t, kMaxStageStmts> slots{}; // a statement whose state is read: how many samples back (0: none)
    std::array<bool, kMaxStageStmts> closes{};        // ...stored once, after the loop, from its registers
    std::array<std::int32_t, kMaxStageStmts> lane{};  // in a skewed stage, the lane a statement stands at (-1: none)
};

constexpr bool serial(const Node& n) { return n.kind == NodeKind::Loop && n.tail == Tail::Clip && n.step == 1; }
constexpr bool chunks(const Node& n) { return n.kind == NodeKind::Loop && n.tail == Tail::Clip && n.step > 1; }

// The stage's fold node.
constexpr std::int32_t fold_node(const Formed& f) {
    for (std::size_t s = 0; s < f.stmts.size(); ++s) {
        if ((f.stmts[s].kind == StmtKind::Fold || f.stmts[s].kind == StmtKind::Select) && f.stmts[s].node >= 0) {
            return f.stmts[s].node;
        }
    }
    return -1;
}

// How many samples back statement `st` reads the state it may be handed: a lag's; a link's 1, since it reads its
// keeper's register at slot 0 (moved one lane up, `Node::shifted`), what a lag of one reads.
constexpr std::int32_t lag_of(const Stmt& st) { return detail::is_a_link(st) ? 1 : st.in[0].back; }

// The state handed from one sample to the next, in a stage whose inner loop is its time loop (no Select; a fold only
// where it folds every sample, whose Finish then may keep its value). A reader is a Load of some samples back along time
// that runs before a later statement storing the buffer it reads, or a link reading it through its operand 1.
// Where the stage reads that buffer 1, 2, …, d samples back, each before the statement, its last d values are state
// handed on in registers (at most the schedule's `state_depth`), the reader of k samples back taking slot k − 1; a lag
// past a gap, or past that depth, stays in memory, so that every slot has a reader to open it from the stored past. A
// link opens nothing (its lanes take the fill and the lane below, `normalize.hpp` `opens`), so it holds slot 0 alone
// and fills no gap: a lag of 2 past it would shift in a slot nothing opened (eight y = a s + b s[n-1] - q y[n-2]
// linked, 0.09 from the cascade in blocks of 256, bit for bit in one run).
constexpr void find_state(const Program& p, const Formed& f, Implied& shape) {
    if ((shape.folds && !shape.sample) || shape.selects || shape.inner == static_cast<std::int32_t>(f.skeleton.walk.size())) {
        return;
    }
    const auto time = static_cast<std::int32_t>(f.axes.size()) - 1;
    std::array<std::int32_t, kMaxStageStmts> storer{};
    std::array<std::uint64_t, kMaxStageStmts> backs{}; // per storing statement: the lags read before it, bit k − 1
    std::array<bool, kMaxStageStmts> linked{};         // ...and whether a link reads it
    storer.fill(-1);
    for (std::size_t r = 0; r < f.stmts.size(); ++r) {
        const bool link = detail::is_a_link(f.stmts[r]);
        const Access& read = f.stmts[r].in[link ? 1 : 0];
        const std::int32_t back = detail::lag_of(f.stmts[r]);
        if ((f.stmts[r].kind != StmtKind::Load && !link) || read.source != Source::Buffer || back < 1 || back > kMaxStateDepth ||
            read.buffer_axis[static_cast<std::size_t>(time)] != p.buffers[static_cast<std::size_t>(read.buffer)].shape.rank - 1) {
            continue;
        }
        for (std::size_t s = r + 1; s < f.stmts.size(); ++s) {
            storer[r] = f.stmts[s].store.source == Source::Buffer && f.stmts[s].store.buffer == read.buffer ? static_cast<std::int32_t>(s) : storer[r];
        }
        if (storer[r] >= 0) {
            const auto at = static_cast<std::size_t>(storer[r]);
            backs[at] |= link ? std::uint64_t{0} : std::uint64_t{1} << (back - 1);
            linked[at] = linked[at] || link;
        }
    }
    for (std::size_t s = 0; s < f.stmts.size(); ++s) {
        shape.slots[s] = std::min(std::max(std::countr_one(backs[s]), linked[s] ? 1 : 0), f.state_depth);
    }
    for (std::size_t r = 0; r < f.stmts.size(); ++r) {
        const std::int32_t s = storer[r];
        if (s >= 0 && detail::lag_of(f.stmts[r]) <= shape.slots[static_cast<std::size_t>(s)]) {
            shape.carries[r] = s;
            shape.slot[r] = detail::lag_of(f.stmts[r]) - 1;
        }
    }
}

// In a skewed stage, the lane each statement stands at: the fill a link reads (its operand 0) at lane 0; a
// view of one section of what the stage makes (`views_one_section`) at that section's, a slice at the one it takes and a
// view over it at its operand's.
constexpr void find_lanes(const Lowering& l, const Formed& f, Implied& shape) {
    if (f.plan < 0 || !l.schedule.stages[static_cast<std::size_t>(f.plan)].skewed) {
        return;
    }
    for (std::size_t s = 0; s < f.stmts.size(); ++s) {
        const Stmt& st = f.stmts[s];
        const Access& operand = st.in[0];
        if (operand.source != Source::Local) {
            continue;
        }
        if (detail::is_a_link(st)) {
            shape.lane[static_cast<std::size_t>(operand.local)] = 0;
        }
        const heinzel::Node* view = st.kind == StmtKind::Load && st.node >= 0 ? &l.g.nodes[static_cast<std::size_t>(st.node)] : nullptr;
        if (view != nullptr && graph::is_an_affine_view(*view)) {
            shape.lane[s] = view->attr.view.kind == ViewKind::Slice ? view->attr.view.offset : shape.lane[static_cast<std::size_t>(operand.local)];
        }
    }
}

// What the inner loops the schedule names hold: a fold, a Select, lanes along the fold's own axis, state handed on.
constexpr Implied shape_of(const Program& p, const Formed& f) {
    Implied shape{.inner = f.inner, .resumes = f.resumes};
    shape.carries.fill(-1);
    shape.lane.fill(-1);
    for (std::size_t s = 0; s < f.stmts.size(); ++s) {
        shape.fold = shape.fold < 0 && f.stmts[s].kind == StmtKind::Fold ? static_cast<std::int32_t>(s) : shape.fold;
        shape.selects = shape.selects || f.stmts[s].kind == StmtKind::Select;
    }
    shape.folds = shape.fold >= 0;
    const auto inner = static_cast<std::size_t>(shape.inner);
    shape.sample = shape.folds && f.stmts[static_cast<std::size_t>(shape.fold)].collapse == AttrKind::Reduce && inner + 1 == f.skeleton.walk.size() &&
                   detail::serial(f.skeleton.walk[inner]) && f.skeleton.walk[inner].axis + 1 == static_cast<std::int32_t>(f.axes.size()) &&
                   (f.collapsed >> f.skeleton.walk[inner].axis & 1u) == 0; // (a reduction's inner loop over time: no axis it folds)
    detail::find_state(p, f, shape);
    for (std::size_t k = static_cast<std::size_t>(shape.inner); k < f.skeleton.walk.size(); ++k) {
        shape.fold_lanes = shape.fold_lanes || f.skeleton.walk[k].kind == NodeKind::Vector;
    }
    return shape;
}

// Does a fold open at its first element rather than at its identity? A strict fold always does. A reduction does when
// all of its own loops are serial (step one, no tail) and no loop of the stage cuts chunks; a contraction does when
// every loop of its stage is serial.
constexpr bool opens_first(const Formed& f, const Implied& implied, const Stmt& st) {
    bool own = true;
    bool all = true;
    bool chunked = false;
    for (std::size_t k = 0; k < f.skeleton.walk.size(); ++k) {
        const bool plain = detail::serial(f.skeleton.walk[k]);
        own = own && (static_cast<std::int32_t>(k) < implied.inner || plain);
        all = all && plain;
        chunked = chunked || detail::chunks(f.skeleton.walk[k]);
    }
    return st.strict || (st.collapse == AttrKind::MatMul ? all : own && !chunked);
}

// A fold that resumes across chunks is refused when it is strict or is finished after its last element (a mean). Its
// partial results wait in the store that formation gave it (`store_if_read_outside`).
constexpr Done resume(const Formed& f) {
    for (const Stmt& st : f.stmts) {
        if (st.kind == StmtKind::Fold && (st.strict || st.fold == ReduceOp::Mean)) {
            return std::unexpected("heinzel::compile: a fold owed its strict order, or one that is finished (a mean), may not be chunked along its own axis");
        }
    }
    return {};
}

// Which statements keeping state store it only in its registers (`Node::closes`): where every read of the buffer one
// stores, in any stage, is a register of its state, its past within them, and no output is that buffer. Its buffer then
// holds its past alone (`Buffer::only_past`): the registers open from it before the time loop and close into it after,
// in place, in a run, a band's chunk or a head sample alike, and nothing carries it. Stored each sample, the rest was
// read by nothing: a one-voice biquad in double 3-5 % slower in blocks, 15-20 % over 1M samples (Decision 66); and
// closed at the end of a run-length buffer, a banded state touched a new line every chunk (klang's EQ 1.13x).
constexpr void find_closes(const Lowering& l, const Formed& f, Implied& shape) {
    for (std::size_t s = 0; s < f.stmts.size(); ++s) {
        const Access& kept = f.stmts[s].store;
        if (shape.slots[s] == 0 || kept.source != Source::Buffer || l.out.buffers[static_cast<std::size_t>(kept.buffer)].prefix > shape.slots[s]) {
            continue;
        }
        bool alone = true;
        for (const NamedValue& out : l.g.outputs) {
            alone = alone && l.at.buffer[static_cast<std::size_t>(out.value)] != kept.buffer;
        }
        for (const Formed& other : l.stages) {
            for (std::size_t r = 0; r < other.stmts.size(); ++r) {
                for (const Access& a : other.stmts[r].in) {
                    const bool reads = a.source == Source::Buffer && a.buffer == kept.buffer;
                    alone = alone && (!reads || (&other == &f && shape.carries[r] == static_cast<std::int32_t>(s)));
                }
            }
        }
        shape.closes[s] = alone;
    }
}

// What stage `f`'s loops imply: its inner loops, what they hold, and whether a fold opens at its first element. A fold
// that may not resume is refused here.
constexpr std::expected<Implied, Refusal> implied_by(const Lowering& l, const Formed& f) {
    Implied implied = detail::shape_of(l.out, f);
    detail::find_closes(l, f, implied);
    detail::find_lanes(l, f, implied);
    for (const Stmt& st : f.stmts) {
        implied.opens_first = implied.opens_first || (st.kind == StmtKind::Fold && detail::opens_first(f, implied, st));
    }
    if (implied.resumes) {
        if (const Done r = detail::resume(f); !r) {
            return std::unexpected(r.error());
        }
    }
    return implied;
}

} // namespace heinzel::loops::detail
