#pragma once

// A value's range: the numbers it may hold, as a union of up to `kRangePieces` intervals (Plan/26-value-ranges). A
// graph states ranges where it knows them, a port's in its declaration and a computed value's in its `promise`
// (`frontend/ranged.hpp`), a composition's result's as a fact (`abs`, `frac`), and records them in
// `Graph::ranges`. One interval is the common case. A hole takes two (`ne(0)` inside [-1, 1] is [-1, 0) or (0, 1]), and
// arithmetic on such ranges meets more: 1/x of that one is `le(-1)` or `ge(1)`.

#include <cstddef>
#include <cstdint>
#include <limits>

#include "heinzel/support/fail.hpp"

namespace heinzel {

inline constexpr std::size_t kRangePieces = 4;

// The numbers between two ends, each open or closed. An infinite end is open: no number lies there.
struct Interval {
    double lo{-std::numeric_limits<double>::infinity()};
    double hi{std::numeric_limits<double>::infinity()};
    bool lo_open{true};
    bool hi_open{true};

    constexpr bool operator==(const Interval&) const = default;
};

// Its pieces sorted, apart and not touching, and the pieces past `count` at their default, so that two ranges of one
// set compare equal. By default one piece, the whole line; no piece is the empty set.
struct Range {
    Interval pieces[kRangePieces]{};
    std::int8_t count{1};

    constexpr bool operator==(const Range&) const = default;
};

namespace detail {

constexpr bool holds_nothing(const Interval& p) { return p.lo > p.hi || (p.lo == p.hi && (p.lo_open || p.hi_open)); }

// Both at once: the later lower end and the earlier upper end, each open where an end that decides it is open.
constexpr Interval common(const Interval& a, const Interval& b) {
    Interval c{};
    c.lo = a.lo > b.lo ? a.lo : b.lo;
    c.lo_open = (a.lo == c.lo && a.lo_open) || (b.lo == c.lo && b.lo_open);
    c.hi = a.hi < b.hi ? a.hi : b.hi;
    c.hi_open = (a.hi == c.hi && a.hi_open) || (b.hi == c.hi && b.hi_open);
    return c;
}

constexpr bool starts_before(const Interval& p, const Interval& q) {
    return q.lo > p.lo || (q.lo == p.lo && !p.lo_open && q.lo_open);
}

// q, starting no earlier than p, is one piece with it: it starts inside p, or where p ends with one of the two ends
// closed. Both open there, the point between them is a hole, and they stay apart.
constexpr bool joins(const Interval& p, const Interval& q) {
    return q.lo < p.hi || (q.lo == p.hi && !(p.hi_open && q.lo_open));
}

// The first `n` pieces sorted and those that touch joined, in place; how many are left. Shared with the proof's join
// (`ranges/interval.hpp`), which widens a result of too many pieces where `intersect` refuses one.
constexpr std::size_t sort_and_join(Interval* p, std::size_t n) {
    for (std::size_t i = 1; i < n; ++i) {
        const Interval c = p[i];
        std::size_t at = i;
        for (; at > 0 && starts_before(c, p[at - 1]); --at) {
            p[at] = p[at - 1];
        }
        p[at] = c;
    }
    std::size_t kept = 0;
    for (std::size_t k = 0; k < n; ++k) {
        if (kept > 0 && joins(p[kept - 1], p[k])) {
            Interval& last = p[kept - 1];
            if (p[k].hi > last.hi || (p[k].hi == last.hi && !p[k].hi_open)) {
                last.hi = p[k].hi;
                last.hi_open = p[k].hi_open;
            }
            continue;
        }
        p[kept++] = p[k];
    }
    return kept;
}

} // namespace detail

// The numbers in both ranges: each piece of one against each of the other, sorted, and pieces that touch joined. A
// result of more pieces than a range holds is refused: widening it to fit would claim numbers neither range holds.
constexpr Range intersect(const Range& a, const Range& b) {
    Interval found[kRangePieces * kRangePieces]{};
    std::size_t n = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(a.count); ++i) {
        for (std::size_t j = 0; j < static_cast<std::size_t>(b.count); ++j) {
            const Interval c = detail::common(a.pieces[i], b.pieces[j]);
            if (!detail::holds_nothing(c)) {
                found[n++] = c;
            }
        }
    }
    const std::size_t kept = detail::sort_and_join(found, n);
    if (kept > kRangePieces) {
        fail("heinzel: a range is at most kRangePieces intervals: at most 3 ne inside its bounds");
    }
    Range r{};
    for (std::size_t k = 0; k < kept; ++k) {
        r.pieces[k] = found[k];
    }
    r.count = static_cast<std::int8_t>(kept);
    return r;
}

} // namespace heinzel
