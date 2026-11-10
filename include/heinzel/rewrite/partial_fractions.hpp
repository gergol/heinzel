#pragma once

// The parallel form's mathematics: complex numbers and a root for the poles and residues of a chain of second-order
// sections (`linear_sections.hpp`), and the chain's partial fractions with how far they outgrow its result. Pure
// numbers, no graph (`parallel_form.hpp` builds the bank from them).

#include <cstddef>
#include <optional>
#include <vector>

#include "heinzel/rewrite/linear_sections.hpp"

namespace heinzel::rewrite::detail {

// ---- complex numbers and a root, for the poles and the residues ----------------------------------------------

struct Complex {
    double re{0};
    double im{0};
};
constexpr Complex plus(Complex a, Complex b) { return {a.re + b.re, a.im + b.im}; }
constexpr Complex minus(Complex a, Complex b) { return {a.re - b.re, a.im - b.im}; }
constexpr Complex times(Complex a, Complex b) { return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re}; }
constexpr double norm(Complex a) { return a.re * a.re + a.im * a.im; }
constexpr Complex over(Complex a, Complex b) {
    const double d = norm(b);
    return {(a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d};
}

// A non-negative number's square root, by Newton's method from above (it falls until it stops falling).
constexpr double root_of(double x) {
    if (!(x > 0)) {
        return 0;
    }
    double r = x > 1 ? x : 1;
    for (int k = 0; k < 2000; ++k) {
        const double next = 0.5 * (r + x / r);
        if (!(next < r)) {
            break;
        }
        r = next;
    }
    return r;
}

// ---- the partial fractions -----------------------------------------------------------------------------------

// The bank: each section's numerator `c0 + c1 z^-1` over its own denominator `1 + a1 z^-1 + a2 z^-2`, its feedback
// held negated (`-a1`, `-a2`: added products, so the feedback-sum rule fuses the newest), and the direct term (each
// section's b2 / a2, multiplied); its `amplification`, how far its terms outgrow its result (`partial_fractions`).
struct Expansion {
    std::vector<double> c0{};
    std::vector<double> c1{};
    std::vector<double> a1{};
    std::vector<double> a2{};
    double direct{1};
    double amplification{0};
};

constexpr bool tame(Complex a) { return tame(a.re) && tame(a.im); }

// a / b, none where b is zero or |a / b| would not be below 1e100 (so the quotient is `tame`; a and b about tame:
// nothing overflows on the way).
constexpr std::optional<Complex> ratio(Complex a, Complex b) {
    if (!(norm(b) > 0 && norm(a) * 1e-200 < norm(b))) {
        return std::nullopt;
    }
    return over(a, b);
}

// A section's two poles, the roots of z^2 + a1 z + a2 (the larger real root computed apart from the smaller).
constexpr void poles_of(const Section& s, std::vector<Complex>& poles) {
    const double a1 = s.a[1];
    const double a2 = s.a[2];
    const double disc = a1 * a1 - 4 * a2;
    if (disc >= 0) {
        const double r = root_of(disc);
        const double q = -0.5 * (a1 + (a1 >= 0 ? r : -r));
        poles.push_back({q, 0}), poles.push_back({a2 / q, 0});
    } else {
        const double r = root_of(-disc);
        poles.push_back({-0.5 * a1, 0.5 * r}), poles.push_back({-0.5 * a1, -0.5 * r});
    }
}

// The residue of the chain's transfer function at pole `i`: the numerators at 1/p over the product of (1 - q/p) for
// every other pole q; none where two poles (nearly) coincide or a product leaves `tame` (its poles inside the unit
// circle and off its centre, `partial_fractions`: |1/p| < 1e50).
constexpr std::optional<Complex> residue(const std::vector<Section>& sections, const std::vector<std::size_t>& chain, const std::vector<Complex>& poles,
                                         std::size_t i) {
    const Complex p = poles[i];
    const Complex w = over({1, 0}, p);
    const Complex one{1, 0};
    Complex numerator = one;
    Complex denominator = one;
    for (const std::size_t s : chain) {
        const Section& t = sections[s];
        numerator = times(numerator, plus({t.b[0], 0}, plus(times({t.b[1], 0}, w), times({t.b[2], 0}, times(w, w)))));
        if (!tame(numerator)) {
            return std::nullopt;
        }
    }
    for (std::size_t j = 0; j < poles.size(); ++j) {
        if (j == i) {
            continue;
        }
        const Complex apart = minus(one, over(poles[j], p));
        denominator = times(denominator, apart);
        if (norm(apart) < 1e-12 || !tame(denominator)) {
            return std::nullopt;
        }
    }
    return ratio(numerator, denominator);
}

// A first-order fraction r / (1 - p z^-1) at its peak, |r| / (1 - |p|), written (1 + |p|) |r| / (1 - |p|^2): never a
// division by zero where |p|^2 < 1.
constexpr double peak_of(Complex r, Complex p) { return root_of(norm(r)) * (1 + root_of(norm(p))) / (1 - norm(p)); }

// The chain's peak gain as the cascade has it, |H(z)| = |prod B(1/z) / A(1/z)|, over the frequencies its poles ring
// at (z = p / |p|), 0 and Nyquist; none where a product leaves `tame`.
constexpr std::optional<double> peak_gain(const std::vector<Section>& sections, const std::vector<std::size_t>& chain, const std::vector<Complex>& poles) {
    double peak = 0;
    for (std::size_t i = 0; i < poles.size() + 2; ++i) {
        const double size = i < poles.size() ? root_of(norm(poles[i])) : 1;
        const Complex w = i < poles.size() ? Complex{poles[i].re / size, -poles[i].im / size} : Complex{i == poles.size() ? 1.0 : -1.0, 0};
        Complex h{1, 0};
        for (const std::size_t s : chain) {
            const Section& t = sections[s];
            const Complex ww = times(w, w);
            const std::optional<Complex> q = ratio(plus({t.b[0], 0}, plus(times({t.b[1], 0}, w), times({t.b[2], 0}, ww))),
                                                   plus({1, 0}, plus(times({t.a[1], 0}, w), times({t.a[2], 0}, ww))));
            if (!q) {
                return std::nullopt;
            }
            h = times(h, *q);
            if (!tame(h)) {
                return std::nullopt;
            }
        }
        peak = norm(h) > peak * peak ? root_of(norm(h)) : peak;
    }
    return peak;
}

// The partial fractions of a chain of tame coefficients whose poles lie inside the unit circle, none where the
// expansion would leave double's range; and their `amplification`: each first-order fraction r / (1 - p z^-1) at its
// peak, |r| / (1 - |p|), and the direct term, summed, over the chain's own peak gain. The bank rounds relative to its
// terms, the cascade about relative to its result, so this is about the factor by which the bank's rounding exceeds the
// cascade's: large where the fractions cancel, as poles close together make them (`parallel_conditioning.cpp`).
constexpr std::optional<Expansion> partial_fractions(const std::vector<Section>& sections, const std::vector<std::size_t>& chain) {
    std::vector<Complex> poles{};
    Expansion e{};
    for (const std::size_t s : chain) {
        const Section& t = sections[s];
        if (!(tame(t.b[0]) && tame(t.b[1]) && tame(t.b[2]) && tame(t.a[1]) && tame(t.a[2]))) {
            return std::nullopt;
        }
        poles_of(t, poles);
    }
    for (const Complex& p : poles) {
        if (!(norm(p) > 1e-100 && norm(p) < 1)) {
            return std::nullopt; // (outside the unit circle, on it, or at its centre)
        }
    }
    for (const std::size_t s : chain) { // (|a2| = |p0 p1| > 1e-100: each quotient finite, and each product)
        e.direct *= sections[s].b[2] / sections[s].a[2];
        if (!tame(e.direct)) {
            return std::nullopt;
        }
    }
    const std::optional<double> peak = peak_gain(sections, chain, poles);
    if (!peak || !(*peak > 1e-100)) {
        return std::nullopt;
    }
    e.amplification = e.direct < 0 ? -e.direct : e.direct;
    for (std::size_t k = 0; k < chain.size(); ++k) {
        const std::optional<Complex> r0 = residue(sections, chain, poles, 2 * k);
        const std::optional<Complex> r1 = residue(sections, chain, poles, 2 * k + 1);
        if (!r0 || !r1) {
            return std::nullopt;
        }
        const Complex c0 = plus(*r0, *r1);
        const Complex c1 = plus(times(*r0, poles[2 * k + 1]), times(*r1, poles[2 * k]));
        const double scale = 1 + norm(c0) + norm(c1);
        if (c0.im * c0.im > 1e-18 * scale || c1.im * c1.im > 1e-18 * scale) {
            return std::nullopt; // (not real)
        }
        e.amplification += peak_of(*r0, poles[2 * k]) + peak_of(*r1, poles[2 * k + 1]);
        e.c0.push_back(c0.re), e.c1.push_back(-c1.re), e.a1.push_back(-sections[chain[k]].a[1]), e.a2.push_back(-sections[chain[k]].a[2]);
    }
    e.amplification /= *peak;
    return e;
}

// The most a bank's terms may outgrow its result: the filters of `parallel_sweep.cpp` reach 120 and the benches' 10.4,
// their float output as far from long double as the cascade's; past it the bank's rounding starts to show (a 12th-order
// high-pass at 5 Hz, 191: 1.15 times the cascade's error fed a chirp; 543: 1.97), and the cases the review found
// collapse (1.8e7 and more: 2.5e-5 to 2.5e-2). `Plan/probes/audio-iir/parallel_conditioning.cpp`.
inline constexpr double kMostAmplification = 150;

// The bank of a chain, none where it amplifies more than `kMostAmplification`.
constexpr std::optional<Expansion> expansion_of(const std::vector<Section>& sections, const std::vector<std::size_t>& chain) {
    const std::optional<Expansion> e = partial_fractions(sections, chain);
    return e && e->amplification <= kMostAmplification ? e : std::nullopt;
}

} // namespace heinzel::rewrite::detail
