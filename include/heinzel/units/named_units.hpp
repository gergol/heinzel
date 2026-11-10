#pragma once

// Durations (`3.0 * ms`): a named unit of time is its length in seconds, and `to_samples` turns a duration into
// an exact integer lag at compile time: 3 ms at 2 kHz is a lag of 6, the same graph as one with the 6 written by
// hand. Only units of time exist, so whatever reaches `to_samples` is a time.

#include <cstdint>

#include "heinzel/support/fail.hpp"

namespace heinzel::units {

// A named unit of time: its length in seconds.
struct Scale {
    double si_factor{1.0};

    constexpr bool operator==(const Scale&) const = default;
};

// A duration, in seconds: `3.0 * ms` is `{si_value = 3e-3}`.
struct Quantity {
    double si_value{0.0};

    constexpr bool operator==(const Quantity&) const = default;
};

constexpr Quantity operator*(double v, Scale u) { return {v * u.si_factor}; }
constexpr Quantity operator*(Scale u, double v) { return {v * u.si_factor}; }

inline constexpr Scale second{1.0};
inline constexpr Scale ms{1e-3};
inline constexpr Scale us{1e-6};

// Convert a duration to an integer number of samples at `sample_rate` Hz, at COMPILE TIME. Fails the build
// (a throw is not a constant expression) if the rate is unset, or if the duration does not land on an integer
// sample count at this rate. The range check is on the double before the narrowing, so a multi-hour delay
// cannot wrap to a wrong lag, and the tolerance is absolute in samples, so a large delay cannot round a
// fraction of a sample away.
constexpr std::int32_t to_samples(Quantity dur, double sample_rate) {
    if (sample_rate <= 0.0) {
        fail("heinzel::units::to_samples: sample rate is unset or non-positive");
    }
    if (dur.si_value < 0.0) {
        fail("heinzel::units::to_samples: negative duration");
    }
    const double s = dur.si_value * sample_rate;
    if (s > 2147483647.0) {
        fail("heinzel::units::to_samples: sample count exceeds the int32 lag range");
    }
    const long long r = static_cast<long long>(s + 0.5);
    const double err = s - static_cast<double>(r);
    constexpr double tol = 1e-6;
    if (err > tol || err < -tol) {
        fail("heinzel::units::to_samples: duration is not an integer number of samples at this sample rate");
    }
    return static_cast<std::int32_t>(r);
}

} // namespace heinzel::units
