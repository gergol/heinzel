#pragma once

// The ranges of declarations: `gt`, `ge`, `lt`, `le`, `ne`, a port's `with_range` and `Expr::promise`. Apart from
// `declarations.hpp`, which includes it, to keep that file under the structure gate's lines.

#include <concepts>
#include <cstddef>
#include <limits>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel {

// ---- ranges -------------------------------------------------------------------------------------------
// The numbers a value may hold, where the graph knows them: last in a port's declaration
// (`Param gain{"gain", ge(0), le(1)}`), or in a computed value's `promise`. A range is a list of comparators, all of
// which hold. `gt` and `lt` leave their bound out, `ge` and `le` keep it, and `ne` makes a hole; a side left out has
// no bound, and a side given twice keeps the tighter bound. The graph records the list as one `Range`
// (`ir/range.hpp`, `Graph::ranges`), which the proof of ranges reads (`ranges/proven.hpp`, Plan/26-value-ranges).
namespace detail {

constexpr double bound(double b) {
    if (b != b) {
        fail("heinzel: a bound is a number, not NaN");
    }
    if (b > std::numeric_limits<double>::max() || b < -std::numeric_limits<double>::max()) {
        fail("heinzel: a bound is a finite number; for no bound on a side, leave that side out");
    }
    return b + 0.0; // -0 is 0, so a range has one spelling of it
}

constexpr Range one_piece(Interval i) {
    Range r{};
    r.pieces[0] = i;
    return r;
}

// A list's range. Its bounds are taken first and its holes after them, so that the order of a list decides nothing: a
// hole outside the bounds splits nothing, and each one inside makes one piece more.
template <std::same_as<Range>... R>
constexpr Range stated(R... list) {
    Range r{};
    ((r = list.count == 1 ? intersect(r, list) : r), ...);
    ((r = list.count == 1 ? r : intersect(r, list)), ...);
    if (r.count == 0) {
        fail("heinzel: a range leaves no value: its comparators contradict, as gt(2), lt(1) do");
    }
    return r;
}

// A declared port's range, on the node its declaration made; an empty list states none.
template <std::same_as<Range>... R>
constexpr Expr with_range(Expr e, R... list) {
    if constexpr (sizeof...(R) > 0) {
        e.catch_up();
        BuildNode& node = e.arena()->nodes[static_cast<std::size_t>(e.position())];
        node.range = stated(list...);
        node.ranged = true;
    }
    return e;
}

} // namespace detail

constexpr Range gt(double b) { return detail::one_piece(Interval{.lo = detail::bound(b), .lo_open = true}); }
constexpr Range ge(double b) { return detail::one_piece(Interval{.lo = detail::bound(b), .lo_open = false}); }
constexpr Range lt(double b) { return detail::one_piece(Interval{.hi = detail::bound(b), .hi_open = true}); }
constexpr Range le(double b) { return detail::one_piece(Interval{.hi = detail::bound(b), .hi_open = false}); }
constexpr Range ne(double b) {
    const double at = detail::bound(b);
    return Range{{Interval{.hi = at}, Interval{.lo = at}}, 2};
}

// A computed value's range, where the graph knows more than the proof can see. One statement has one place: a port's
// range is its declaration's, a recurrence's its definition's (`y[n] = (…).promise(…)`), and a value has one promise.
// A composition's fact (`abs(x)` is at least 0) is no promise of the user's: a promise on its result narrows it.
template <std::same_as<Range> First, std::same_as<Range>... More>
constexpr Expr Expr::promise(First first, More... more) const {
    Expr self = *this;
    self.catch_up();
    BuildNode& node = self.arena_->nodes[static_cast<std::size_t>(self.position_)];
    if (node.category == ValueCategory::Input) {
        fail(kPromiseOnAPort);
    }
    if (node.category == ValueCategory::Variable || node.attr.kind == AttrKind::Recurrence) {
        fail("heinzel: promise its definition: y[n] = (...).promise(...)");
    }
    if (node.promised) {
        fail("heinzel: a value has one promise");
    }
    node.promise = detail::stated(first, more...);
    node.promised = true;
    return self;
}

} // namespace heinzel
