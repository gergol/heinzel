#pragma once

// A reference wider than double on every machine: a double-double, hi + lo (Dekker's and Knuth's exact sums and
// fma's exact products, about 106 bits), and the sine and cosine of one by their series.
//
// `long double` was the tests' wider type, and on Apple's arm64 it is double: a reference in it there is no more precise
// than what it checks, so an error measured from it reads 0 and a bound in that error proves nothing. Deliberately not
// the library's pair arithmetic (`runtime/exact_arithmetic.hpp`): a reference shares no code with what it judges.

#include <cmath>

namespace heinzel::testing {

struct DoubleDouble {
    double hi = 0;
    double lo = 0;
    DoubleDouble() = default;
    DoubleDouble(double v) : hi(v) {}
    DoubleDouble(double h, double l) : hi(h), lo(l) {}
    explicit operator double() const { return hi + lo; }
};

// The exact a + b as hi + lo, a's magnitude at least b's.
inline DoubleDouble quick_sum(double a, double b) {
    const double s = a + b;
    return DoubleDouble(s, b - (s - a));
}

// The exact a + b as hi + lo.
inline DoubleDouble exact_sum(double a, double b) {
    const double s = a + b;
    const double b_in = s - a;
    return DoubleDouble(s, (a - (s - b_in)) + (b - b_in));
}

inline DoubleDouble operator+(DoubleDouble x, DoubleDouble y) {
    DoubleDouble s = exact_sum(x.hi, y.hi);
    const DoubleDouble t = exact_sum(x.lo, y.lo);
    s = quick_sum(s.hi, s.lo + t.hi);
    return quick_sum(s.hi, s.lo + t.lo);
}

inline DoubleDouble operator-(DoubleDouble x) { return DoubleDouble(-x.hi, -x.lo); }

inline DoubleDouble operator-(DoubleDouble x, DoubleDouble y) { return x + -y; }

inline DoubleDouble operator*(DoubleDouble x, DoubleDouble y) {
    const double p = x.hi * y.hi;
    const double e = std::fma(x.hi, y.hi, -p) + (x.hi * y.lo + x.lo * y.hi);
    return quick_sum(p, e);
}

// x / d: the quotient, then the remainder's (exact through fma) over d.
inline DoubleDouble operator/(DoubleDouble x, double d) {
    const double q = x.hi / d;
    const double r = (std::fma(-q, d, x.hi) + x.lo) / d;
    return quick_sum(q, r);
}

// pi to about 2^-106.
inline const DoubleDouble kPiWide(0x1.921fb54442d18p+1, 0x1.1a62633145c07p-53);

// sin(y) (`odd`) or cos(y), |y| <= pi/4, by its series: 16 terms reach y^31/31!, below 2^-106 of the answer.
inline double wide_sin_or_cos(DoubleDouble y, bool odd) {
    const DoubleDouble square = y * y;
    DoubleDouble term = odd ? y : DoubleDouble(1.0);
    DoubleDouble sum = term;
    for (int k = odd ? 2 : 1; k < 32; k += 2) {
        term = -(term * square / static_cast<double>(k * (k + 1)));
        sum = sum + term;
    }
    return static_cast<double>(sum);
}

} // namespace heinzel::testing
