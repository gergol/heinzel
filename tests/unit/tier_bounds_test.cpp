// MILESTONE M4.2, ORACLE O10: what a function-accuracy tier costs in accuracy, measured and gated.
//
// Split from `tiers_test.cpp` when that file passed amendment 06's six hundred lines, and split
// along the seam that file's own opening paragraph named — it had TWO claims in it, and they are
// different in kind:
//
//   the seam    a node's setting reaches the arithmetic down every path that runs a graph, and the
//               three tiers are three different answers. That is about graphs, and it stayed there.
//   ORACLE O10  a measured maximum error per (operation, tier), gated, and checked against the old
//               tree's own bound for the same row. That is about cores, and it is here.
//
// Nothing here builds a graph. The subject is what `compute()` answers for one operation at one
// tier, so the file reaches the arithmetic directly and needs neither a planner nor a generator —
// which is also why it was the half worth moving.
#include "heinzel/runtime/compute.hpp"

#include <cmath>
#include <cstddef>
#include <string_view>

#include "one_operation.hpp"

using namespace heinzel;
using heinzel::testing::apply_at;

// ---- the special cases are answered before any series is reached ---------------------------------
//
// NaN, the infinities, the overflow and underflow edges, and the arguments where the answer is not a
// logarithm at all. Every one of these is decided by a branch taken BEFORE the polynomial, so a tier
// cannot reach it — and this is what lets the measurement below sweep an interior domain and still
// describe the whole of what a tier changes.
//
// Bitwise, and NaN asked about separately, because no two NaNs need share a bit pattern.
namespace {

constexpr bool answered_identically_at_every_tier(OpCode which, double x) {
    const double exact = apply_at(FnAccuracy::Exact, which, x);
    for (const FnAccuracy fn : {FnAccuracy::Approx}) {
        const double coarse = apply_at(fn, which, x);
        if (exact != exact) {
            if (coarse == coarse) {
                return false;
            }
            continue;
        }
        if (!heinzel::testing::same_bits(coarse, exact)) {
            return false;
        }
    }
    return true;
}

constexpr bool the_edges_do_not_move_with_the_tier() {
    using heinzel::testing::kInf;
    using heinzel::testing::kNaN;
    // `_exp` and `_exp2`: NaN, both infinities, and the two arguments just past the edges where the
    // answer stops being a normal double.
    for (const double x : {kNaN, kInf, -kInf, 800.0, -800.0, -1100.0, 1100.0}) {
        if (!answered_identically_at_every_tier(op::Exp, x)
            || !answered_identically_at_every_tier(op::Exp2, x)) {
            return false;
        }
    }
    // The logarithms: both zeros, a negative, and infinity — the four `logarithm_special_case`
    // answers, none of which is a logarithm.
    for (const double x : {kNaN, 0.0, -0.0, -1.0, kInf}) {
        if (!answered_identically_at_every_tier(op::Log, x)
            || !answered_identically_at_every_tier(op::Log2, x)
            || !answered_identically_at_every_tier(op::Log10, x)) {
            return false;
        }
    }
    // `_exp2` at an integer, which is not an edge case but a PROMISE, and one this file is the only
    // place to check across tiers: the whole part is split off before the series is reached, so an
    // integral argument leaves a zero remainder and the series returns its first term. Written as
    // `exp(x * ln2)` it would miss a power of two by an ulp — which is what the old tree's coarse
    // `exp2_approx_` does, since it is exactly that spelling.
    for (const double x : {0.0, 10.0, -3.0, 1023.0, -1074.0}) {
        if (!answered_identically_at_every_tier(op::Exp2, x)) {
            return false;
        }
    }
    // `_tanh`: the zero whose SIGN is the answer, and the saturation past 19.07.
    for (const double x : {kNaN, 0.0, -0.0, 25.0, -25.0, kInf, -kInf}) {
        if (!answered_identically_at_every_tier(op::Tanh, x)) {
            return false;
        }
    }
    return true;
}

} // namespace

static_assert(the_edges_do_not_move_with_the_tier(),
              "a tier changed an answer that no series produces");
// ---- oracle O10: what each tier costs, measured and gated ----------------------------------------
//
// One row per operation. `here` is what this tree measures — the maximum distance of the coarse core
// from the exact one over the domain swept below, rounded up — and `there` is the old tree's own
// bound for that row, transcribed from `kBuiltins` in its `include/heinzel/ir/vocab.hpp` at
// `c4012b8`. Two claims, checked separately:
//
//   the gate     the sweep finds nothing worse than `here`. A core that got worse fails here.
//   parity       `here` is inside `there`. What this tree calls `Approx` is at least as accurate as
//                what the old tree calls `Approx`, so a graph ported across at a named tier does not
//                quietly get a coarser answer than it had.
//
// AND A THIRD, WHICH IS THE ONE THAT ROTS: `here` is required to be within a factor of two of what
// the sweep actually measures. A bound nobody tightens is a bound that stops meaning anything, and a
// core that silently improved would leave one behind. `too_slack` below is that check.
namespace {

// ---- how far a coarse tier is from the exact one, which is what O10 records -----------------------
//
// Against the EXACT CORE rather than against the platform's library, and that is the honest
// comparison for a tier: what a tier costs is the distance from what this tree computes when it is
// asked for accuracy. The exact core's own distance from the platform is a separate number, measured
// where the core is and gated in `transcendental_test.cpp`. Adding the two together and calling the
// result a tier's error would charge the tier for something it did not do.
//
// Absolute or relative per operation, following the old tree's own `relative` flag on the row —
// `_exp`, `_exp2` and `_pow` carry relative bounds, the logarithms, the circular family and `_tanh`
// absolute ones. Reproducing that choice is part of what makes these numbers comparable to the old
// tree's at all.
constexpr double distance(double ours, double exact, bool relative) {
    const double difference = ours > exact ? ours - exact : exact - ours;
    if (!relative) {
        return difference;
    }
    const double magnitude = exact < 0.0 ? -exact : exact;
    return magnitude == 0.0 ? difference : difference / magnitude;
}

struct TierRow {
    OpCode op{};
    std::string_view name{};
    bool relative{false};
    double here_approx{0.0};
    double there_approx{0.0};  // the old tree's `Approx` bound for this row
};

// `_exp`, `_exp2` and `_pow` are relative, the rest absolute — the old tree's own choice per row,
// kept so the two columns below are comparable at all.
constexpr TierRow kTierTable[] = {
    {op::Exp, "_exp", true, 3.3e-6, 3e-5},
    {op::Exp2, "_exp2", true, 3.3e-6, 4e-5},
    {op::Log, "_log", false, 1.2e-6, 2e-5},
    {op::Log2, "_log2", false, 1.7e-6, 3e-5},
    {op::Log10, "_log10", false, 5.0e-7, 1e-5},
    {op::Tanh, "_tanh", false, 1.3e-6, 1e-4},
    // The periodic four. `_sin`, `_cos` and `_sinpi` share a number to three digits, and that is
    // the quadrant switch rather than a copied row: each of them reaches the sine polynomial on two
    // quadrants of four and the cosine polynomial on the other two, so each one's worst case is the
    // sine's. `_tan` is a little worse because it is a quotient of the two.
    {op::Sin, "_sin", false, 3.2e-7, 5e-6},
    {op::Cos, "_cos", false, 3.2e-7, 5e-6},
    {op::Tan, "_tan", false, 4.8e-7, 1e-5},
    {op::SinPi, "_sinpi", false, 3.2e-7, 5e-6},
    // The inverse circular four, which share ONE polynomial — every one of them forms a ratio and
    // hands it to the same arc tangent — so they share a number for a third reason again. The
    // coarse tier lands far inside the old tree's row, and why is a finding recorded where the core
    // is: this family's cost is in its eight-centre reduction, not in its series.
    {op::Atan, "_atan", false, 5.5e-10, 2e-6},
    {op::Asin, "_asin", false, 5.5e-10, 2e-6},
    {op::Acos, "_acos", false, 5.5e-10, 2e-6},
    {op::Atan2, "_atan2", false, 5.5e-10, 4e-6},
    // `_pow`, whose tier is the final exponential's series and nothing else — so its RELATIVE error
    // is `_exp`'s, at every exponent. That last clause is the point of the sweep below and is why
    // this row's number matches `_exp`'s to two digits.
    {op::Pow, "_pow", true, 3.3e-6, 2e-4},
};
constexpr std::size_t kTierRows = sizeof(kTierTable) / sizeof(kTierTable[0]);

constexpr double bound_of(const TierRow& row, FnAccuracy) {
    return row.here_approx;
}

// The worst distance found for one operation at one tier, over the arguments the caller feeds it.
struct Worst {
    double distance{0.0};
    double at{0.0};
};

// The second operand is there for `_atan2`, which is the only tiered operation that takes one. It
// defaults rather than being a separate helper because every sweep below then reads the same way,
// and a one-operand core ignores it exactly as `compute` does.
void note(Worst& worst, const TierRow& row, FnAccuracy fn, double x, double second = 0.0) {
    const double d = distance(apply_at(fn, row.op, x, second),
                              apply_at(FnAccuracy::Exact, row.op, x, second), row.relative);
    if (d > worst.distance) {
        worst = Worst{d, x};
    }
}

// The domain each family is swept over, and it is the domain that family's exact-tier measurement
// used. One function per family rather than one chain, because the domains have nothing in common
// and a chain of them would be a function about five subjects.

// Every argument whose answer is a normal double, in steps of a hundredth. A relative distance is
// not a useful unit on a denormal, where one ulp IS the value, so those are skipped rather than
// measured — the special-case assertions above are what covers the ends.
Worst sweep_exponential(const TierRow& row, FnAccuracy fn) {
    Worst worst{};
    const int lowest = row.op == op::Exp ? -74500 : -107500;
    const int highest = row.op == op::Exp ? 70900 : 102300;
    for (int i = lowest; i <= highest; ++i) {
        const double x = static_cast<double>(i) / 100.0;
        if (!std::isnormal(apply_at(FnAccuracy::Exact, row.op, x))) {
            continue;
        }
        note(worst, row, fn, x);
    }
    return worst;
}

// Every binade, 32 significands each — the sweep a logarithm's accuracy is a question about, since
// what its reduction has to hold is the significand and the exponent is exact.
Worst sweep_logarithm(const TierRow& row, FnAccuracy fn) {
    Worst worst{};
    for (int exponent = -1070; exponent <= 1020; ++exponent) {
        for (int k = 0; k < 32; ++k) {
            const double x = std::ldexp(1.0 + static_cast<double>(k) / 32.0, exponent);
            if (!std::isfinite(x) || x == 0.0) {
                continue;
            }
            note(worst, row, fn, x);
        }
    }
    return worst;
}

// Five million arguments at a hundred-thousandth is what the development sweep ran; this is the same
// shape at a hundredth of the density, with the argument the maximum lands at, -0.75, exactly on the
// grid rather than near it.
Worst sweep_hyperbolic(const TierRow& row, FnAccuracy fn) {
    Worst worst{};
    for (int i = -25000; i <= 25000; ++i) {
        note(worst, row, fn, static_cast<double>(i) / 1000.0);
    }
    return worst;
}

// THREE RANGES, because the interesting question about a periodic function is not its accuracy near
// zero but whether the reduction still holds far out — the same three the exact tier is measured
// over. `_tan` gets the narrow one only, and that is a statement about its domain rather than a
// convenience: it has a pole at pi/2, so an ABSOLUTE bound on it is meaningless anywhere near one,
// and [-1.5, 1.5] is where the exact tier's own gate lives for exactly that reason.
Worst sweep_periodic(const TierRow& row, FnAccuracy fn) {
    Worst worst{};
    if (row.op == op::Tan) {
        for (int i = -150000; i <= 150000; ++i) {
            note(worst, row, fn, static_cast<double>(i) / 100000.0);
        }
        return worst;
    }
    // `_sinpi` counts in half-turns, so its far range is chosen where every double is an integer and
    // the answer is a signed zero — which is the range the operation exists for.
    const double reach = row.op == op::SinPi ? 200.0 : 1000.0;
    for (int i = -200000; i <= 200000; ++i) {
        note(worst, row, fn, static_cast<double>(i) * reach / 200000.0);
    }
    const double far = row.op == op::SinPi ? 2e15 : 1e12;
    for (int i = -100000; i <= 100000; ++i) {
        note(worst, row, fn, static_cast<double>(i) * far / 100000.0);
    }
    return worst;
}

// `_atan` over its whole interesting range and then every binade beyond it; `_asin` and `_acos`
// over their whole domain and then the arguments closest to one, which is where a spelling that
// subtracted two nearly equal angles would lose its bits. The same three sweeps the exact tier is
// measured over, at a lower density.
Worst sweep_one_operand_arc(const TierRow& row, FnAccuracy fn) {
    Worst worst{};
    if (row.op == op::Atan) {
        for (int i = -400000; i <= 400000; i += 4) {
            note(worst, row, fn, static_cast<double>(i) / 1000.0);
        }
        for (int exponent = -1074; exponent <= 1023; ++exponent) {
            for (int k = 0; k < 8; ++k) {
                const double x = std::ldexp(1.0 + static_cast<double>(k) / 8.0, exponent);
                if (x == 0.0 || !std::isfinite(x)) {
                    continue;
                }
                note(worst, row, fn, x);
                note(worst, row, fn, -x);
            }
        }
        return worst;
    }
    for (int i = -400000; i <= 400000; i += 4) {
        note(worst, row, fn, static_cast<double>(i) / 400001.0);
    }
    double near_one = 1.0;
    for (int i = 0; i < 10000; ++i) {
        near_one = std::nextafter(near_one, 0.0);
        note(worst, row, fn, near_one);
        note(worst, row, fn, -near_one);
    }
    return worst;
}

// All four quadrants, and then pairs whose exponents are far enough apart that the quotient alone
// would be a denormal or an overflow — which is the case `ratio_of_magnitudes` exists for and the
// one a coarse tier must not be allowed to quietly break.
Worst sweep_two_operand_arc(const TierRow& row, FnAccuracy fn) {
    Worst worst{};
    for (int i = -300; i <= 300; ++i) {
        for (int j = -300; j <= 300; ++j) {
            note(worst, row, fn, static_cast<double>(i) / 7.0, static_cast<double>(j) / 13.0);
        }
    }
    for (int high = -1070; high <= 1020; high += 7) {
        for (int low = -1070; low <= 1020; low += 13) {
            const double y = std::ldexp(1.3, high);
            const double x = std::ldexp(1.7, low);
            note(worst, row, fn, y, x);
            note(worst, row, fn, -y, x);
            note(worst, row, fn, y, -x);
        }
    }
    return worst;
}

// `_pow`, whose sweep is chosen for the failure mode the tier decision turns on rather than for
// coverage of the domain, and which is therefore split in two: the ordinary domain, and THE PRODUCT
// NEAR ITS LIMIT. The second is the one that matters — base close to one and an exponent picked so
// that `y * log2(x)` lands near +/-1024 is where an error in the logarithm would arrive at the
// answer amplified by `y` — which the loop below drives from about 2 700 up to 7 million, against
// an ordinary grid whose exponent never exceeds ten. That amplification is what a tier taken on the
// pair arithmetic would have, and it is the reason this one is not taken there. This
// tier is taken on the final exponential instead, so the two halves must measure THE SAME, and that
// agreement is the evidence for the choice. `the_power_tier_does_not_grow_with_the_exponent` below
// is that comparison, checked rather than described.

// The ordinary domain: a grid over positive bases, then negative bases at integral exponents, where
// the answer's sign is the exponent's parity — a branch a coarse tier must not reach differently.
Worst sweep_power_ordinary(const TierRow& row, FnAccuracy fn) {
    Worst worst{};
    for (int i = 1; i <= 600; i += 3) {
        for (int j = -600; j <= 600; j += 3) {
            note(worst, row, fn, static_cast<double>(i) / 60.0, static_cast<double>(j) / 60.0);
        }
    }
    for (int i = -400; i <= -1; i += 3) {
        for (int j = -60; j <= 60; ++j) {
            const double x = static_cast<double>(i) / 40.0;
            const double y = static_cast<double>(j);
            if (std::isfinite(apply_at(FnAccuracy::Exact, row.op, x, y))) {
                note(worst, row, fn, x, y);
            }
        }
    }
    return worst;
}

// The product near its limit — AND AT A FRACTIONAL VALUE, which is the whole content of the loop.
//
// FINDING: the first draft of this sweep chose the exponent as `target / log2(x)` for an INTEGRAL
// target, and measured exactly zero at every argument, at both coarse tiers. It was not evidence
// that the tier does not amplify; it was a sweep that cannot see a tier at all. `2^p` is split into
// a whole part scaled in afterwards and a fraction handed to the series, and an integral product
// leaves that fraction at zero — the one argument where a series of four terms and a series of
// fourteen agree bit for bit. The sixteen offsets below carry the fraction across its whole range,
// and the sweep then measures 3.24e-06, which is the number the ordinary grid measures.
Worst sweep_power_near_limit(const TierRow& row, FnAccuracy fn) {
    Worst worst{};
    for (int i = 1; i <= 3000; i += 7) {
        const double x = 1.0 + static_cast<double>(i) * 1e-4;
        const double logarithm = std::log2(x);
        for (const double target : {-1020.0, -700.0, 700.0, 1020.0}) {
            for (int k = 0; k < 16; ++k) {
                const double product = target + static_cast<double>(k) / 16.0;
                note(worst, row, fn, x, product / logarithm);
            }
        }
    }
    return worst;
}

Worst sweep_power(const TierRow& row, FnAccuracy fn) {
    const Worst ordinary = sweep_power_ordinary(row, fn);
    const Worst near_limit = sweep_power_near_limit(row, fn);
    return near_limit.distance > ordinary.distance ? near_limit : ordinary;
}

Worst sweep(const TierRow& row, FnAccuracy fn) {
    if (row.op == op::Pow) {
        return sweep_power(row, fn);
    }
    if (row.op == op::Atan || row.op == op::Asin || row.op == op::Acos) {
        return sweep_one_operand_arc(row, fn);
    }
    if (row.op == op::Atan2) {
        return sweep_two_operand_arc(row, fn);
    }
    if (row.op == op::Exp || row.op == op::Exp2) {
        return sweep_exponential(row, fn);
    }
    if (row.op == op::Tanh) {
        return sweep_hyperbolic(row, fn);
    }
    if (row.op == op::Sin || row.op == op::Cos || row.op == op::Tan || row.op == op::SinPi) {
        return sweep_periodic(row, fn);
    }
    return sweep_logarithm(row, fn);
}

bool the_recorded_tier_bounds_hold() {
    for (std::size_t i = 0; i < kTierRows; ++i) {
        const TierRow& row = kTierTable[i];
        for (const FnAccuracy fn : {FnAccuracy::Approx}) {
            const Worst worst = sweep(row, fn);
            if (worst.distance > bound_of(row, fn)) {
                return false;
            }
            // The bound has not gone slack: it is at most twice what the sweep finds.
            if (worst.distance * 2.0 < bound_of(row, fn)) {
                return false;
            }
        }
    }
    return true;
}


// `_pow`'s tier does not grow with the exponent, and THAT is the claim the choice of where to spend
// its tier rests on. Two directions, two different mistakes:
//
//   near-limit much WORSE   a tier whose error is amplified by the exponent — what taking it on the
//                           pair arithmetic would have given, and a bound that depends on an operand
//                           is not a bound
//   near-limit much BETTER  a near-limit sweep that is not exercising the tier at all. That is not
//                           hypothetical: the first draft of it measured exactly zero, because an
//                           integral product leaves the series' argument at zero (see above)
//
// A factor of two either way, which is the same slack the recorded bounds are held to.
bool the_power_tier_does_not_grow_with_the_exponent() {
    for (std::size_t i = 0; i < kTierRows; ++i) {
        const TierRow& row = kTierTable[i];
        if (row.op != op::Pow) {
            continue;
        }
        for (const FnAccuracy fn : {FnAccuracy::Approx}) {
            const double ordinary = sweep_power_ordinary(row, fn).distance;
            const double near_limit = sweep_power_near_limit(row, fn).distance;
            if (!(ordinary > 0.0) || !(near_limit > 0.0)) {
                return false;
            }
            if (near_limit > ordinary * 2.0 || ordinary > near_limit * 2.0) {
                return false;
            }
        }
        return true;
    }
    return false;  // `_pow` left the table without this check leaving with it
}

} // namespace

// Parity, and it is a compile-time claim because both numbers are constants. Every bound this tree
// records is inside the old tree's for the same row and the same tier.
constexpr bool every_bound_is_inside_the_old_trees() {
    for (std::size_t i = 0; i < kTierRows; ++i) {
        const TierRow& row = kTierTable[i];
        if (!(row.here_approx < row.there_approx)) {
            return false;
        }
    }
    return true;
}
static_assert(every_bound_is_inside_the_old_trees(),
              "a tier this tree records is coarser than the old tree's bound for the same row");

// ---- the numeric column says exactly what this file measured --------------------------------------
//
// `NumericContract::Approximate` claims that a coarser core exists for a row and that the accuracy
// setting selects it. This file is where that claim is checked, because it is the only place the difference
// between the tiers is actually measured. The two directions are different mistakes and both are
// caught:
//
//   labelled and not tiered   a row marked `Approximate` whose cores nobody made differ — the label that
//                             the finding S8-COST warned against, "a value indistinguishable from
//                             one somebody forgot"
//   tiered and not labelled   a coarse core added without the row being updated, so the back end at
//                             M6 would price it as if the setting changed nothing
//
// Read off the rule table by walking it, not from a list of six written out here. A list would be a
// list of six the day a seventh arrived.
constexpr bool the_tiered_rows_are_the_measured_ones() {
    for (std::uint8_t code = 0; code < kOpCount; ++code) {
        bool measured = false;
        for (std::size_t i = 0; i < kTierRows; ++i) {
            if (kTierTable[i].op.index == code) {
                measured = true;
            }
        }
        if ((kOpTable[code].rules.numeric == NumericContract::Approximate) != measured) {
            return false;
        }
    }
    return true;
}
static_assert(the_tiered_rows_are_the_measured_ones(),
              "a row's numeric column and the tiers this file measures disagree about which operations "
              "have a coarser core");

// Fifteen rows, which is M4.2's whole set: the six operations `exponential_of` serves, the four
// `periodic_of` does, the four `inverse_circular_of` does, and `_pow`. A sixteenth arriving without
// a row is caught by the set equality above rather than by this number, which is here so that a row
// DELETED alongside its cost value still shows up.
static_assert(kTierRows == 15);

int main() {
    return the_recorded_tier_bounds_hold() && the_power_tier_does_not_grow_with_the_exponent() ? 0 : 1;
}
