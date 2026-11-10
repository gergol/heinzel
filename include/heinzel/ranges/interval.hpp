#pragma once

// Arithmetic on ranges (`ir/range.hpp`; Plan/26-value-ranges/03), one interval of each operand at a time: a rule maps
// each piece of a one-operand range, or every pair of pieces of two, and the pieces it computed are joined. Where more
// than `kRangePieces` remain, the two closest are merged across their gap: the range only grows, so the proof stays
// sound. Ends are computed in double, rounded to nearest, and not widened: a range is the true one to within that.

#include <cstddef>
#include <cstdint>
#include <limits>

#include "heinzel/ir/range.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel::ranges {

inline constexpr double kInf = std::numeric_limits<double>::infinity();
inline constexpr double kLargest = std::numeric_limits<double>::max();
// π is irrational and its double is below it; the double above holds every angle a core can answer.
inline constexpr double kPiAbove = 3.1415926535897936;

// Neither infinite nor NaN. Asked by comparison: arithmetic that makes a NaN is no constant expression.
constexpr bool finite(double x) { return x == x && x != kInf && x != -kInf; }

constexpr Range whole() { return Range{}; }
constexpr Range nothing() {
    Range r{};
    r.count = 0;
    return r;
}
// One piece; an infinite end is open, since no number lies there.
constexpr Range span(double lo, double hi, bool lo_open = false, bool hi_open = false) {
    Range r{};
    r.pieces[0] = Interval{lo, hi, lo_open || lo == -kInf, hi_open || hi == kInf};
    return r;
}
// One number; an infinite or NaN one says nothing about a real number, so it is any number.
constexpr Range point(double c) { return finite(c) ? span(c + 0.0, c + 0.0) : whole(); }

// The pieces a rule computed, before they are a range: at most one per pair of two ranges' pieces.
struct Pieces {
    Interval at[kRangePieces * kRangePieces]{};
    std::size_t count{0};

    constexpr void add(const Interval& p) {
        if (count == kRangePieces * kRangePieces) {
            fail("heinzel::ranges: more pieces than two ranges' pairs");
        }
        at[count++] = p;
    }
};

namespace detail {

// A piece computed from numbers is never empty: where rounding met or crossed its two ends it is the closed piece
// between them, and where it overflowed to one infinity, the numbers beyond the largest double. Then -0 is 0, and an
// infinite end is open.
constexpr Interval rounded(Interval p) {
    p.lo = p.lo == kInf ? kLargest : p.lo;
    p.hi = p.hi == -kInf ? -kLargest : p.hi;
    if (p.lo >= p.hi) {
        p = Interval{p.hi, p.lo, false, false};
    }
    return Interval{p.lo + 0.0, p.hi + 0.0, p.lo_open || p.lo == -kInf, p.hi_open || p.hi == kInf};
}

// `n` pieces, none empty, as a range: sorted, touching pieces joined, and the two closest merged until they fit.
constexpr Range fitted(Interval* p, std::size_t n) {
    std::size_t kept = heinzel::detail::sort_and_join(p, n);
    while (kept > kRangePieces) {
        std::size_t at = 0;
        for (std::size_t i = 1; i + 1 < kept; ++i) {
            if (p[i + 1].lo - p[i].hi < p[at + 1].lo - p[at].hi) {
                at = i;
            }
        }
        p[at].hi = p[at + 1].hi;
        p[at].hi_open = p[at + 1].hi_open;
        for (std::size_t i = at + 1; i + 1 < kept; ++i) {
            p[i] = p[i + 1];
        }
        --kept;
    }
    Range r{};
    for (std::size_t k = 0; k < kept; ++k) {
        r.pieces[k] = p[k];
    }
    r.count = static_cast<std::int8_t>(kept);
    return r;
}

} // namespace detail

// The computed pieces as one range. An end that computed to NaN (∞ − ∞) says nothing, so the range is any number.
constexpr Range join(Pieces found) {
    for (std::size_t i = 0; i < found.count; ++i) {
        if (found.at[i].lo != found.at[i].lo || found.at[i].hi != found.at[i].hi) {
            return whole();
        }
        found.at[i] = detail::rounded(found.at[i]);
    }
    return detail::fitted(found.at, found.count);
}

// The numbers in both, as `intersect` has them, but widened to fit where it would refuse: a promise against a
// computed range may need a fifth piece, and only the proof's own ranges are widened.
constexpr Range narrowed(const Range& a, const Range& b) {
    Pieces found{};
    for (std::int8_t i = 0; i < a.count; ++i) {
        for (std::int8_t j = 0; j < b.count; ++j) {
            const Interval c = heinzel::detail::common(a.pieces[i], b.pieces[j]);
            if (!heinzel::detail::holds_nothing(c)) {
                found.add(c);
            }
        }
    }
    return detail::fitted(found.at, found.count);
}

template <class F>
constexpr Range each(const Range& a, F f) {
    Pieces found{};
    for (std::int8_t i = 0; i < a.count; ++i) {
        found.add(f(a.pieces[i]));
    }
    return join(found);
}

template <class F>
constexpr Range pairs(const Range& a, const Range& b, F f) {
    Pieces found{};
    for (std::int8_t i = 0; i < a.count; ++i) {
        for (std::int8_t j = 0; j < b.count; ++j) {
            found.add(f(a.pieces[i], b.pieces[j]));
        }
    }
    return join(found);
}

constexpr Range unite(const Range& a, const Range& b) {
    Pieces found{};
    for (std::int8_t i = 0; i < a.count; ++i) {
        found.add(a.pieces[i]);
    }
    for (std::int8_t j = 0; j < b.count; ++j) {
        found.add(b.pieces[j]);
    }
    return join(found);
}

// One piece from the first piece's lower end to the last one's upper end: what a blend of two of its numbers can be.
constexpr Range hull(const Range& a) {
    if (a.count == 0) {
        return a;
    }
    const Interval& first = a.pieces[0];
    const Interval& last = a.pieces[a.count - 1];
    return span(first.lo, last.hi, first.lo_open, last.hi_open);
}

constexpr bool piece_inside(const Interval& a, const Interval& b) {
    const bool lo = b.lo < a.lo || (b.lo == a.lo && (!b.lo_open || a.lo_open));
    const bool hi = a.hi < b.hi || (a.hi == b.hi && (!b.hi_open || a.hi_open));
    return lo && hi;
}

// Every piece of `a` inside one piece of `b`: a piece is connected, so it cannot straddle two.
constexpr bool inside(const Range& a, const Range& b) {
    for (std::int8_t i = 0; i < a.count; ++i) {
        bool found = false;
        for (std::int8_t j = 0; j < b.count && !found; ++j) {
            found = piece_inside(a.pieces[i], b.pieces[j]);
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

constexpr bool holds(const Range& a, double x) { return inside(span(x, x), a); }

constexpr bool is_whole(const Range& a) { return a == whole(); }

// ---- one piece at a time ----------------------------------------------------------------------------------------

constexpr Interval negated(const Interval& a) { return Interval{-a.hi, -a.lo, a.hi_open, a.lo_open}; }

constexpr Interval sum(const Interval& a, const Interval& b) {
    return Interval{a.lo + b.lo, a.hi + b.hi, a.lo_open || b.lo_open, a.hi_open || b.hi_open};
}

constexpr Interval difference(const Interval& a, const Interval& b) { return sum(a, negated(b)); }

namespace detail {

struct End {
    double at{};
    bool open{};
};

// One corner of a product. Its end is reached when both factors' ends are, or when a closed 0 factor makes it 0 whatever
// the other is. 0 times an infinite end counts as 0 (IEEE 1788): reached when the 0 end is.
constexpr End corner(End a, End b) {
    const bool a_zero = a.at == 0;
    const bool b_zero = b.at == 0;
    if ((a_zero && (b.at == kInf || b.at == -kInf)) || (b_zero && (a.at == kInf || a.at == -kInf))) {
        return End{0.0, a_zero ? a.open : b.open};
    }
    const bool reached = (!a.open && !b.open) || (!a.open && a_zero) || (!b.open && b_zero);
    return End{a.at * b.at, !reached};
}

} // namespace detail

// The least and the largest corner product, each end closed where some corner reaching it is.
constexpr Interval product(const Interval& a, const Interval& b) {
    const detail::End c[4] = {detail::corner({a.lo, a.lo_open}, {b.lo, b.lo_open}), detail::corner({a.lo, a.lo_open}, {b.hi, b.hi_open}),
                              detail::corner({a.hi, a.hi_open}, {b.lo, b.lo_open}), detail::corner({a.hi, a.hi_open}, {b.hi, b.hi_open})};
    Interval r{c[0].at, c[0].at, true, true};
    for (const detail::End& e : c) {
        r.lo = e.at < r.lo ? e.at : r.lo;
        r.hi = e.at > r.hi ? e.at : r.hi;
    }
    for (const detail::End& e : c) {
        r.lo_open = r.lo_open && !(e.at == r.lo && !e.open);
        r.hi_open = r.hi_open && !(e.at == r.hi && !e.open);
    }
    return r;
}

// One value times itself: the piece through x², since the value lies in one piece at a time.
constexpr Interval square(const Interval& a) {
    if (a.lo >= 0) {
        return Interval{a.lo * a.lo, a.hi * a.hi, a.lo_open, a.hi_open};
    }
    if (a.hi <= 0) {
        return Interval{a.hi * a.hi, a.lo * a.lo, a.hi_open, a.lo_open};
    }
    const double lo = a.lo * a.lo;
    const double hi = a.hi * a.hi;
    return Interval{0.0, lo > hi ? lo : hi, false, lo > hi ? a.lo_open : (hi > lo ? a.hi_open : a.lo_open && a.hi_open)};
}

// |x| of a piece: itself where it holds no negative number, negated where it holds no positive one, else [0, the
// larger end's magnitude], its lower end reached (0 lies inside) and its upper end open where the end that gives it is.
constexpr Interval magnitude(const Interval& a) {
    if (a.lo >= 0) {
        return a;
    }
    if (a.hi <= 0) {
        return negated(a);
    }
    const double below = -a.lo;
    return Interval{0.0, below > a.hi ? below : a.hi, false,
                    below > a.hi ? a.lo_open : (a.hi > below ? a.hi_open : a.lo_open && a.hi_open)};
}

// 1/b of a piece without 0 (its domain was checked). An open end at 0 goes to the infinity of the piece's side.
constexpr Interval reciprocal(const Interval& b) {
    return Interval{b.hi == 0 ? -kInf : 1.0 / b.hi, b.lo == 0 ? kInf : 1.0 / b.lo, b.hi_open, b.lo_open};
}

// c terms of a piece, c >= 1.
constexpr Interval scaled(const Interval& a, double c) { return Interval{c * a.lo, c * a.hi, a.lo_open, a.hi_open}; }

// A monotone f of one piece: [f(lo), f(hi)], or [f(hi), f(lo)] where f decreases. A non-strict f closes both ends (a
// rounding maps an open end onto a number it reaches); a strict one keeps them, and a finite end whose image is f's
// limit on that side (`below`, `above`; NaN where its domain does not reach that infinity) stays open: the true value
// lies strictly inside, and only rounding reached the limit (exp(-1e30) is 0.0).
template <class F>
constexpr Interval mapped(const Interval& a, F f, bool strict, bool decreasing, double below, double above) {
    const double lo = f(a.lo);
    const double hi = f(a.hi);
    const bool lo_open = strict && (a.lo_open || lo == below);
    const bool hi_open = strict && (a.hi_open || hi == above);
    return decreasing ? Interval{hi, lo, hi_open, lo_open} : Interval{lo, hi, lo_open, hi_open};
}

namespace detail {

// Whether an end of f(x, y), for an f increasing in each operand, is reached. It is f of the operands' ends, reached
// when the one operand that decides it reaches its own. On a tie, either one reaching it is enough where that one alone
// holds f there (a minimum's lower end, a maximum's upper end), and both must otherwise.
template <class F>
constexpr bool end_open(F f, double end, End a, End b, double inward) {
    if (end == a.at && end != b.at) {
        return a.open;
    }
    if (end == b.at && end != a.at) {
        return b.open;
    }
    if (end == a.at && f(end, inward) == end) {
        return a.open && b.open;
    }
    return a.open || b.open;
}

} // namespace detail

// min and max: each end taken from the operand that decides it (a corner rule is unsound here: max([0, 1], (-1, 2])
// reaches 0 at y = -0.5, a corner of no closed ends).
template <class F>
constexpr Interval each_increasing(const Interval& a, const Interval& b, F f) {
    const double lo = f(a.lo, b.lo);
    const double hi = f(a.hi, b.hi);
    // A number inside each end; an infinite end is open whatever is asked.
    const double above = !finite(lo) ? 0.0 : lo + (lo == 0 ? 1.0 : (lo < 0 ? -lo : lo));
    const double below = !finite(hi) ? 0.0 : hi - (hi == 0 ? 1.0 : (hi < 0 ? -hi : hi));
    return Interval{lo, hi, detail::end_open(f, lo, {a.lo, a.lo_open}, {b.lo, b.lo_open}, above),
                    detail::end_open(f, hi, {a.hi, a.hi_open}, {b.hi, b.hi_open}, below)};
}

// ---- domains ------------------------------------------------------------------------------------------------

constexpr bool may_be_negative(const Range& a) { return a.count > 0 && a.pieces[0].lo < 0; }

// tan's: each piece finite and inside one open branch (kπ − π/2, kπ + π/2), its own branch found from its lower end.
constexpr bool within_one_branch(const Range& a) {
    constexpr double pi = 3.141592653589793;
    for (std::int8_t i = 0; i < a.count; ++i) {
        const Interval& p = a.pieces[i];
        // Wider than a branch, or so far out that a double no longer tells the branches apart.
        if (!(p.hi - p.lo < pi) || p.lo < -1e15 || p.hi > 1e15) {
            return false;
        }
        double turns = (p.lo + pi / 2) / pi;
        const double whole_turns = static_cast<double>(static_cast<std::int64_t>(turns));
        turns = whole_turns > turns ? whole_turns - 1 : whole_turns;
        const double left = turns * pi - pi / 2;
        if (!piece_inside(p, Interval{left, left + pi, true, true})) {
            return false;
        }
    }
    return true;
}

} // namespace heinzel::ranges
