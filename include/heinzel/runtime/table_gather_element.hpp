#pragma once

// What a `_tblread` reads: the two table samples one output blends. `out[e] = table[frac(phase[e])*N]`,
// linearly interpolated, from a table of N samples read as one cycle: the sample after the last is the first.
// The address is a data-dependent position, so this is a gather and not an affine read. The reference walk and the
// loop tree at `High`, or over a double, both call `table_tap` and `lerp2` (also `_dline`'s blend), so their reads are
// bit-identical; an F32 read at `Normal` and `Fast` takes these steps in float (`loops/values.hpp`), within a bound.

#include <cstdint>

#include "heinzel/runtime/rounding_cores.hpp"

namespace heinzel {

// The linear blend of two taps, for both `_dline` and `_tblread`. `near + f*(far - near)` rather than
// `(1-f)*near + f*far`: the two differ in their last bits, and this one is exactly `near` when `f` is zero,
// so a whole-index position reads the table sample to the bit.
constexpr double lerp2(double near_tap, double far_tap, double fraction) {
    return near_tap + fraction * (far_tap - near_tap);
}

// Which two samples of the table one phase reads, and how far between them. `x = frac(phase)*N` lands in
// `[0, N)`, so its floor is a table index in `[0, N-1]`, and the next is that plus one, or the first after the last:
// the runner wraps the far tap, so a table holds its N samples and no copy of the first (the probe `wavetable-wrap` of
// Plan 28: a read 14 % faster than with the copy, an oscillator 4 % slower). `frac` is `phase - floor(phase)` with the
// tree's own floor (`round_toward_negative`), so a NEGATIVE phase wraps into the table rather than reading off its
// front, and the fraction agrees with every other floor here. A phase the proof of ranges keeps in [0, 1] at every
// precision (`Wraps` false: `TableAttr::phase_in_one_turn`) is read as it is, the same tap to the bit: below 1 the
// wrap hands the phase on (-0 as +0, and either reads the first cell at fraction +0), and at 1, whose frac is 0, x is
// N, which reads the first cell at fraction 0 too. A phase below 0, which only a false fact makes (a graph built by
// hand), reads the first cell: a wrong number, never memory before the table. The walk, which never asks the proof,
// always wraps.
struct TableTap {
    std::int32_t index{0};  // the lower of the two bracketing table samples: floor(frac(phase)*N)
    std::int32_t next{0};   // the sample after it: index + 1, the first after the last
    double fraction{0.0};   // how far from that sample toward the next, in [0, 1); a NaN where the phase is no number

    constexpr bool operator==(const TableTap&) const = default;
};

template <bool Wraps = true>
constexpr TableTap table_tap(std::int32_t n, double phase) {
    const double turn = Wraps ? phase - round_toward_negative(phase) : phase;
    const double x = turn * static_cast<double>(n); // frac(phase)*N, in [0, N]
    // A phase just below a whole one rounds frac to 1.0 (−1e-30: 1 − 1e-30 is 1.0), so x reaches N, which is the first
    // sample again, read as 0. A phase that is no number, a NaN or an infinity (whose frac is inf − inf), leaves x a
    // NaN, which fails `< N` too and so reads the first sample as well, never an index made of a NaN; `x - x`, zero for
    // any number, puts the NaN into the fraction, which the blend hands on: a NaN in, a NaN out (the same probe: +3.5 %
    // on a read, −2.4 % on an oscillator, the least of the spellings tried). Unwrapped, x below 0 is read as 0 too, so
    // that the read stays in its table whatever the proof was told: a max beside the compare, off the read's chain,
    // the cheapest of four spellings measured (Decision 78, the phase in one turn's review).
    const double at_least_zero = Wraps || x > 0.0 ? x : 0.0;
    const double place = x < static_cast<double>(n) ? at_least_zero : 0.0;
    const double whole = round_toward_negative(place);
    const auto index = static_cast<std::int32_t>(whole);
    return TableTap{index, index + 1 < n ? index + 1 : 0, (place - whole) + (x - x)};
}

} // namespace heinzel
