#pragma once

// How far an F32 read may lie from the walk's where it is taken in float (`Normal` and `Fast`, Decision 78's float
// reads): the walk blends the two taps in double and rounds once; the float writing takes the tap in float and blends
// there (`loops/values.hpp`).
//
// The blend, `near + f (far - near)` in float, lies within 3.5 float steps of the larger tap of the walk's: its three
// roundings at most a step each (the difference and the product are at most twice the larger tap), the walk's half a
// step; with the fused multiply-add, 2.5. A delay line's tap is the walk's to the bit (a float distance clamped to a
// float bound, its whole part and fraction exact), so that is its bound. A table's place, frac(phase) N, is a float
// rounded once (half a step at the place), and once more first where the phase lies in (-1, 0), whose frac 1 + phase
// rounds (half a step at it, N times): the read is the interpolant at that place, so it moves by that distance times
// the steepest step between two cells it may have crossed, the place a turn round where it rounds up to N.
//
// Not counted, and neither arises below 2^24: past 2^24 cells N's float is rounded too (`table_tap_in_float`), which
// moves the place by up to half a step of N more; past 2^24 samples, where a line's longest is no float, a distance
// beyond it is held to the largest float below it, the walk's to the longest itself, so the two read other samples.
// Neither read leaves its table or its line.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace heinzel::testing {

// The spacing of floats at |x|: one step of the last bit.
inline double float_step(double x) {
    const float m = std::fabs(static_cast<float>(x));
    return static_cast<double>(std::nextafter(m, std::numeric_limits<float>::infinity()) - m);
}

inline double blend_bound(double near_tap, double far_tap) {
    return 3.5 * float_step(std::max(std::fabs(near_tap), std::fabs(far_tap)));
}

// A table read of `phase` (a float) over `n` cells: how far the float writing may lie from the walk's.
template <class Cell>
double table_bound(const Cell* cells, std::int32_t n, double phase) {
    if (!(phase - phase == 0.0)) {
        return 0.0; // a NaN or an infinity reads a NaN in both writings
    }
    const double wrapped = phase - std::floor(phase);
    const double place = wrapped * static_cast<double>(n);
    const double rounded = 0.5 * float_step(place);
    const double rounded_first = phase < 0.0 && phase > -1.0 ? 0.5 * float_step(wrapped) * n : 0.0; // 1 + phase rounds
    const double moved = rounded + rounded_first;
    double steepest = 0.0;
    double largest = 0.0;
    const auto first = static_cast<std::int64_t>(std::floor(place - moved));
    const auto last = static_cast<std::int64_t>(std::floor(place + moved));
    for (std::int64_t k = first; k <= last; ++k) { // the cells the place may have crossed, a turn round
        const double a = static_cast<double>(cells[((k % n) + n) % n]);
        const double b = static_cast<double>(cells[((k + 1) % n + n) % n]);
        steepest = std::max(steepest, std::fabs(b - a));
        largest = std::max({largest, std::fabs(a), std::fabs(b)});
    }
    return moved * steepest + 3.5 * float_step(largest);
}

// Is `got`, the float writing's number, within `bound` of the walk's `want`, a NaN exactly where it is one?
inline bool within(double got, double want, double bound) {
    if (got != got || want != want) {
        return got != got && want != want;
    }
    return std::fabs(got - want) <= bound;
}

} // namespace heinzel::testing
