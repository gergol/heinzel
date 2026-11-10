#pragma once

// Which axes are the same axis: a union-find over symbolic extents (sizes supplied later).
//
// Axes that meet at an operation are one axis from then on, transitively. A class is pinned to a size
// when any member meets a literal; one class pinned to two sizes is refused, naming both sizes and the
// operations that set them. An axis may also be derived from another, worked out once the source is pinned.

#include <cstddef>
#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/support/fail.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel {

// How many distinct symbolic axes one graph may declare. Only room: nothing depends on 32.
inline constexpr std::size_t kMaxSymbols = 32;
static_assert(kMaxDimDecls <= kMaxSymbols, "HEINZEL_MAX_DIM_DECLS is at most kMaxSymbols (32): a Dim's id is a symbol");

// ---- what is known about an axis before its size is known ---------------------------------------
//
// Plain integers, not extents: a bound such as "at least 64" is a number, never a symbol.
//
// Zero means "nothing said" for every field but `divisible_by`, whose neutral value is 1 — every
// number is divisible by 1.
struct ExtentHint {
    std::int32_t divisible_by{1}; // every size this axis may take is a multiple of this
    std::int32_t at_least{0};     // 0: no lower bound stated
    std::int32_t at_most{0};      // 0: no upper bound stated
    // The size this axis usually is. A hint, never a promise, so two classes expecting different
    // sizes is not a contradiction, only a reason to stop expecting anything.
    std::int32_t expected{0};

    constexpr bool operator==(const ExtentHint&) const = default;
};

namespace detail {

constexpr std::int32_t greatest_common_divisor(std::int32_t a, std::int32_t b) {
    while (b != 0) {
        const std::int32_t r = a % b;
        a = b;
        b = r;
    }
    return a < 0 ? -a : a;
}

// The tightest divisibility both sides can promise. An axis that is a multiple of 4 and a multiple
// of 6 is a multiple of 12 — not of 24, which would promise more than either side said.
constexpr std::int32_t least_common_multiple(std::int32_t a, std::int32_t b) {
    if (a == 0 || b == 0) {
        return a == 0 ? b : a;
    }
    const std::int32_t scaled = a / greatest_common_divisor(a, b);
    // Two large, nearly coprime divisibilities multiply out of range: a compile error under constant
    // evaluation and silent overflow at run time. So it stops here, in both.
    if (b != 0 && (scaled > 0x7fffffff / (b < 0 ? -b : b))) {
        fail("heinzel::meet: two divisibility hints multiply out of range for an axis size");
    }
    return scaled * b;
}

} // namespace detail

// Can any size satisfy this hint at all? Two bounds that exclude each other cannot be met by any size
// that arrives later, so this is refused as soon as the two axes are proven equal.
constexpr bool hint_is_satisfiable(const ExtentHint& h) {
    return h.at_most == 0 || h.at_least <= h.at_most;
}

// Does a size satisfy what was declared about the axis?
constexpr bool size_satisfies(const ExtentHint& h, std::int32_t size) {
    if (h.divisible_by > 1 && size % h.divisible_by != 0) {
        return false;
    }
    if (h.at_least != 0 && size < h.at_least) {
        return false;
    }
    if (h.at_most != 0 && size > h.at_most) {
        return false;
    }
    return true;
}

// Meeting two hints. The result carries the tightest of each: the least common multiple of the
// divisibilities, the larger lower bound, the smaller upper bound.
//
// The expected value does not tighten, because it is not a constraint. It survives only when both
// sides expect the same size, or only one side expects anything, and that size satisfies the met
// bounds and divisibility. Otherwise the class expects nothing rather than picking a side.
constexpr ExtentHint meet(const ExtentHint& a, const ExtentHint& b) {
    ExtentHint out{};
    out.divisible_by = detail::least_common_multiple(a.divisible_by, b.divisible_by);
    out.at_least = a.at_least > b.at_least ? a.at_least : b.at_least;
    if (a.at_most == 0) {
        out.at_most = b.at_most;
    } else if (b.at_most == 0) {
        out.at_most = a.at_most;
    } else {
        out.at_most = a.at_most < b.at_most ? a.at_most : b.at_most;
    }

    std::int32_t expected = 0;
    if (a.expected == b.expected) {
        expected = a.expected;
    } else if (a.expected == 0 || b.expected == 0) {
        expected = a.expected == 0 ? b.expected : a.expected;
    }
    if (expected != 0 && size_satisfies(out, expected)) {
        out.expected = expected;
    }
    return out;
}

// A graph has no place to carry a hint and the DSL no spelling for "this axis is a multiple of 8".
// Only an exact `relate` records one (none does today); the tests declare them. The meet combines what
// two classes knew when they are proven equal.

// ---- what an axis is derived FROM ----------------------------------------------------------------
//
// `(mul * source + add) / div`, integer division rounding down, over one source symbol. Today only
// the unfold records one (`sema/framing.hpp`); the form also covers, rearranged:
//
//   unfold's frame count   (L - K)/H + 1   is   (1*L + (H - K)) / H
//   a strided slice        (stop-start+step-1)/step is (1*stop + (step-1-start)) / step
//   overlap-add's length   (F - 1)*H + N   is   (H*F + (N - H)) / 1
//   a reshape merging a symbolic axis with a literal one   is   (k*s + 0) / 1
//
// `exact` is the difference between a floor and an obligation. An unfold's frame count is a floor:
// a length that does not divide evenly yields fewer frames, which is the operation's meaning, not an
// error. A merge that must not lose elements is exact, which obliges the source to be divisible by
// `div`: recorded as the source's `divisible_by` hint and checked when the source is pinned.
//
// An `Extent` stays a literal or a symbol; a derived axis is a symbol of its own with a recorded
// relation, which keeps `Shape` small. A relation has one source, so a join's sum of two extents is
// outside the algebra (a symbolic joined axis is refused).
struct AffineRelation {
    std::int32_t source{-1};  // the symbol this axis is derived from; -1 means "not derived"
    std::int32_t mul{1};
    std::int32_t add{0};
    std::int32_t div{1};
    bool exact{false};        // the division must come out even, and that is an obligation
    OpName by{};              // the operation that recorded it, so the refusal can name it

    constexpr bool operator==(const AffineRelation&) const = default;
};

constexpr bool is_derived(const AffineRelation& r) { return r.source >= 0; }

// ---- binding a symbol to a size from outside the graph -------------------------------------------
//
// A symbolic extent says "this size arrives later"; a binding supplies it. Bindings are handed to the
// analysis pass rather than written into the graph, so the same graph can be used at several sizes,
// and bound to N it must produce exactly what its literal-N twin produces.
struct SymbolBinding {
    std::int32_t symbol{0};
    // A plain number: a symbol is always bound to a known size, never to another symbol.
    std::int32_t size{0};

    constexpr bool operator==(const SymbolBinding&) const = default;
};

struct Bindings {
    fixed_vector<SymbolBinding, kMaxSymbols> at{};

    constexpr Bindings& bind(Dim d, std::int32_t size) {
        at.push_back(SymbolBinding{d.id, size});
        return *this;
    }

    constexpr bool operator==(const Bindings&) const = default;
};

// ---- the solver ---------------------------------------------------------------------------------
//
// A union-find over symbol ids. Every symbol starts in a class of its own; `unify` merges classes;
// `find` names the class. A class carries the size it is pinned to (if any), the operation that
// pinned it, and the met hint of everything in it.
//
// `find` does not compress paths: that would make it mutate, and the solver's state would then depend
// on the order of the questions asked. Chains are at most `kMaxSymbols` (32) deep.
//
// It has private members, so it cannot be a template argument. That is fine: it is a working value of
// the analysis pass, not part of a graph or a program.
struct ExtentSolver {
    std::int32_t parent[kMaxSymbols]{};
    Extent size[kMaxSymbols]{};      // the size this class is pinned to, or no_extent()
    OpName pinned_by[kMaxSymbols]{}; // which operation pinned it; default-constructed means nobody
    ExtentHint hint[kMaxSymbols]{};
    // What each axis is derived from, per symbol rather than per class: two axes later proven equal
    // do not stop having been derived differently.
    AffineRelation from[kMaxSymbols]{};
    // Which operation declared this class's hint, so the bind-time refusal can name it. It differs
    // from `pinned_by`: an obligation may come from one operation and the size from a binding.
    OpName declared_by[kMaxSymbols]{};

    constexpr ExtentSolver() {
        for (std::size_t s = 0; s < kMaxSymbols; ++s) {
            parent[s] = static_cast<std::int32_t>(s);
        }
    }

    constexpr bool operator==(const ExtentSolver&) const = default;

    // The class this symbol belongs to, named by its representative — the lowest-numbered symbol
    // reachable by following parents.
    constexpr std::int32_t find(std::int32_t symbol) const {
        if (symbol < 0 || static_cast<std::size_t>(symbol) >= kMaxSymbols) {
            fail("heinzel::ExtentSolver: symbol id out of range; raise kMaxSymbols");
        }
        std::int32_t s = symbol;
        while (parent[static_cast<std::size_t>(s)] != s) {
            s = parent[static_cast<std::size_t>(s)];
        }
        return s;
    }

    // Are these two axes known to be the same axis?
    constexpr bool same_class(std::int32_t a, std::int32_t b) const { return find(a) == find(b); }

    // What is known about this symbol's class.
    constexpr ExtentHint hint_of(std::int32_t symbol) const { return hint[static_cast<std::size_t>(find(symbol))]; }

    // What this symbol resolves to: the size its class is pinned to if there is one, otherwise the
    // class's representative symbol. Canonicalising an unpinned symbol to its representative is
    // what makes transitive equality visible: `a`, `b` and `c` all come back as one extent even
    // though nothing ever related `a` and `c`.
    constexpr Extent resolve(Extent e) const {
        if (!is_symbol(e)) {
            return e;
        }
        const std::int32_t root = find(symbol_of(e));
        const Extent pinned = size[static_cast<std::size_t>(root)];
        if (!is_no_extent(pinned)) {
            return pinned;
        }
        // Not pinned directly, but it may still have a size if it is derived from an axis that does.
        if (const Extent derived = derived_size(symbol_of(e)); !is_no_extent(derived)) {
            return derived;
        }
        return Extent{Dim{root}};
    }

    // The size a derived axis works out to, or `no_extent()` when its source has none yet.
    //
    // A chain: an axis derived from an axis derived from a bound one. A chain longer than the number
    // of symbols is a cycle. A graph cannot produce one (a derivation's source already exists when it
    // is recorded), but it is refused here rather than looped over.
    constexpr Extent derived_size(std::int32_t symbol) const {
        std::int32_t at = symbol;
        std::int32_t hops = 0;
        // The relations passed through, innermost first, so the arithmetic can be applied on the
        // way back out. Applying it on the way in would need the source's size before it is known.
        std::int32_t chain[kMaxSymbols]{};
        std::int32_t depth = 0;
        Extent base = no_extent();
        while (true) {
            if (++hops > static_cast<std::int32_t>(kMaxSymbols)) {
                fail("heinzel::ExtentSolver: a derived extent is derived from itself");
            }
            const std::int32_t root = find(at);
            if (!is_no_extent(size[static_cast<std::size_t>(root)])) {
                base = size[static_cast<std::size_t>(root)];
                break;
            }
            const AffineRelation& r = from[static_cast<std::size_t>(at)];
            if (!is_derived(r)) {
                return no_extent();
            }
            chain[static_cast<std::size_t>(depth++)] = at;
            at = r.source;
        }
        std::int32_t value = literal_of(base);
        while (depth > 0) {
            const AffineRelation& r = from[static_cast<std::size_t>(chain[static_cast<std::size_t>(--depth)])];
            value = apply(r, value);
        }
        return literal_extent(value);
    }

    // One relation applied to one size. Floor division, and `div` is at least 1 by construction —
    // `relate` refuses anything else, so this is arithmetic and not a place to re-check.
    static constexpr std::int32_t apply(const AffineRelation& r, std::int32_t source_size) {
        const std::int64_t scaled = static_cast<std::int64_t>(r.mul) * source_size + r.add;
        // Named rather than clamped. A derived axis of zero or fewer elements is a graph that
        // cannot run, and saying so where the arithmetic happened beats an empty buffer later.
        if (scaled < r.div) {
            fail("heinzel::ExtentSolver: a derived extent works out to zero or fewer elements");
        }
        return static_cast<std::int32_t>(scaled / r.div);
    }

    // What this axis is derived from, if anything.
    constexpr AffineRelation relation_of(std::int32_t symbol) const {
        return from[static_cast<std::size_t>(symbol)];
    }

    // Record that one axis is an affine function of another. Three refusals rather than assumptions,
    // because each is a caller mistake that would otherwise become a wrong size much later.
    constexpr bool relate(std::int32_t derived, std::int32_t source, std::int32_t mul, std::int32_t add,
                          std::int32_t div, bool exact, OpName at, Diagnostic& why) {
        if (mul < 1 || div < 1) {
            why = relation_is_degenerate(at);
            return false;
        }
        if (derived == source || same_class(derived, source)) {
            why = relation_is_circular(at);
            return false;
        }
        const AffineRelation existing = from[static_cast<std::size_t>(derived)];
        if (is_derived(existing)) {
            // Twice is fine when it is the same relation — the analysis pass may reach one node by
            // two paths — and is a contradiction otherwise.
            const AffineRelation fresh{source, mul, add, div, exact, at};
            if (existing == fresh) {
                return true;
            }
            why = relation_disagrees(existing.by, at);
            return false;
        }
        // An exact division obliges the source to be a multiple of `div`; a floor obliges nothing,
        // because fewer frames is the operation's meaning rather than an error.
        if (exact && div > 1) {
            ExtentHint obligation{};
            obligation.divisible_by = div;
            if (!declare_at(source, obligation, at, why)) {
                return false;
            }
        }
        from[static_cast<std::size_t>(derived)] = AffineRelation{source, mul, add, div, exact, at};
        return true;
    }

    // Declare what is known about an axis. Rejects a declaration whose bounds cannot both hold, and
    // one that contradicts a size the class is already pinned to.
    constexpr bool declare(std::int32_t symbol, const ExtentHint& h, Diagnostic& why) {
        return declare_at(symbol, h, OpName{}, why);
    }

    // The same, remembering which operation said it, so the bind-time refusal can name the
    // operation that recorded an obligation.
    constexpr bool declare_at(std::int32_t symbol, const ExtentHint& h, OpName at, Diagnostic& why) {
        const std::size_t root = static_cast<std::size_t>(find(symbol));
        const ExtentHint met = meet(hint[root], h);
        if (!hint_is_satisfiable(met)) {
            why = impossible_bounds(met);
            return false;
        }
        const Extent pinned = size[root];
        if (!is_no_extent(pinned) && !size_satisfies(met, literal_of(pinned))) {
            why = size_breaks_hint(literal_of(pinned), met, at);
            return false;
        }
        hint[root] = met;
        // The first operation to state something about this class is the one named. A later one
        // that only re-states what is already true has not added the constraint that breaks.
        if (!(at == OpName{}) && declared_by[root] == OpName{}) {
            declared_by[root] = at;
        }
        return true;
    }

    // Two axes have met at an operation, so from now on they are one axis. Either may be a size or
    // a symbol; the four combinations are the whole of the method.
    constexpr bool unify(Extent a, Extent b, OpName at, Diagnostic& why) {
        if (!is_symbol(a) && !is_symbol(b)) {
            // Two sizes. Agreeing is nothing to record; disagreeing is the plain case the pass
            // already reports with both shapes, so it is left to say so.
            return literal_of(a) == literal_of(b);
        }
        if (!is_symbol(a)) {
            return pin(symbol_of(b), a, at, why);
        }
        if (!is_symbol(b)) {
            return pin(symbol_of(a), b, at, why);
        }

        const std::size_t ra = static_cast<std::size_t>(find(symbol_of(a)));
        const std::size_t rb = static_cast<std::size_t>(find(symbol_of(b)));
        if (ra == rb) {
            return true;
        }

        // Both pinned, to different sizes: the contradiction this file exists to name. Each side
        // remembers the operation that pinned it, so the message can say which is which.
        if (!is_no_extent(size[ra]) && !is_no_extent(size[rb]) && literal_of(size[ra]) != literal_of(size[rb])) {
            why = sizes_disagree(size[ra], pinned_by[ra], size[rb], pinned_by[rb]);
            return false;
        }

        const ExtentHint met = meet(hint[ra], hint[rb]);
        if (!hint_is_satisfiable(met)) {
            why = impossible_bounds(met);
            return false;
        }

        // The lower-numbered representative wins, so the same graph always produces the same
        // classes whatever order the operations were visited in. The tests compare the answers given
        // at compile time with those given at run time (`tests/unit/solver_test.cpp`), so they must not
        // depend on the order.
        const std::size_t keep = ra < rb ? ra : rb;
        const std::size_t drop = ra < rb ? rb : ra;
        const Extent pinned = is_no_extent(size[keep]) ? size[drop] : size[keep];
        const OpName pinner = is_no_extent(size[keep]) ? pinned_by[drop] : pinned_by[keep];

        if (!is_no_extent(pinned) && !size_satisfies(met, literal_of(pinned))) {
            why = size_breaks_hint(literal_of(pinned), met,
                                   declared_by[keep] == OpName{} ? declared_by[drop] : declared_by[keep]);
            return false;
        }

        parent[drop] = static_cast<std::int32_t>(keep);
        hint[keep] = met;
        size[keep] = pinned;
        pinned_by[keep] = pinner;
        if (declared_by[keep] == OpName{}) {
            declared_by[keep] = declared_by[drop];
        }
        return true;
    }

private:
    // Pin a symbol's class to a size, remembering which operation did it.
    constexpr bool pin(std::int32_t symbol, Extent to, OpName at, Diagnostic& why) {
        const std::size_t root = static_cast<std::size_t>(find(symbol));
        if (!is_no_extent(size[root])) {
            if (literal_of(size[root]) != literal_of(to)) {
                why = sizes_disagree(size[root], pinned_by[root], to, at);
                return false;
            }
            return true;
        }
        if (!size_satisfies(hint[root], literal_of(to))) {
            why = size_breaks_hint(literal_of(to), hint[root], declared_by[root]);
            return false;
        }
        size[root] = to;
        pinned_by[root] = at;
        return true;
    }

    // The messages name both things that disagree and where each came from.
    static constexpr Diagnostic sizes_disagree(Extent first, OpName by_first, Extent second, OpName by_second) {
        MessageWriter m;
        m << std::string_view{"one axis is given two sizes: "} << literal_of(first) << std::string_view{" at "};
        write_op_name(m, by_first);
        m << std::string_view{" and "} << literal_of(second) << std::string_view{" at "};
        write_op_name(m, by_second);
        return m.problem();
    }

    static constexpr Diagnostic impossible_bounds(const ExtentHint& h) {
        MessageWriter m;
        m << std::string_view{"this axis is declared at least "} << h.at_least
          << std::string_view{" and at most "} << h.at_most << std::string_view{", which cannot both hold"};
        return m.problem();
    }

    // Names the part of the hint that broke, not just "breaks what was declared".
    static constexpr Diagnostic size_breaks_hint(std::int32_t given, const ExtentHint& h, OpName by) {
        MessageWriter m;
        m << std::string_view{"this axis is given the size "} << given << std::string_view{", but it was declared "};
        if (h.divisible_by > 1 && given % h.divisible_by != 0) {
            m << std::string_view{"a multiple of "} << h.divisible_by;
        } else if (h.at_least != 0 && given < h.at_least) {
            m << std::string_view{"at least "} << h.at_least;
        } else {
            m << std::string_view{"at most "} << h.at_most;
        }
        // Name the operation that recorded the obligation: for a derived axis the user never wrote
        // "a multiple of 4" (it is a hop, or a merge that must not lose elements). Omitted when
        // nobody signed it.
        if (!(by == OpName{})) {
            m << std::string_view{" at "};
            write_op_name(m, by);
        }
        return m.problem();
    }

    static constexpr Diagnostic relation_is_degenerate(OpName at) {
        MessageWriter m;
        m << std::string_view{"a derived extent needs a positive multiplier and divisor, at "};
        write_op_name(m, at);
        return m.problem();
    }

    static constexpr Diagnostic relation_is_circular(OpName at) {
        MessageWriter m;
        m << std::string_view{"an axis cannot be derived from itself, at "};
        write_op_name(m, at);
        return m.problem();
    }

    static constexpr Diagnostic relation_disagrees(OpName first, OpName second) {
        MessageWriter m;
        m << std::string_view{"one axis is derived two different ways: at "};
        write_op_name(m, first);
        m << std::string_view{" and at "};
        write_op_name(m, second);
        return m.problem();
    }
};

} // namespace heinzel
