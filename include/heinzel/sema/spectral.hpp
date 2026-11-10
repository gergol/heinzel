#pragma once

// The shape an `_fft` imposes on the innermost (streamed) axis it transforms, and its refusals. The output reads the
// attribute's `n`, not the source extent: a forward transform maps a real length n to `2*(n/2+1)`, but the inverse
// cannot be shaped from its input alone, because `2*(n/2+1)` is not injective (a real length of 4 and of 5 both give
// 6 planar numbers). So the real length it recovers has to be the declared `n`.

#include <cstdint>
#include <string_view>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/sema/refusal.hpp"

namespace heinzel {

// The half-spectrum length of a real transform of length n: n real numbers become `2*(n/2+1)` planar
// re/im numbers, `n + 2` for the even n an FFT requires.
constexpr std::int32_t half_spectrum_length(std::int32_t n) { return 2 * (n / 2 + 1); }

namespace detail {

// The innermost (transformed) axis becomes its half-spectrum length forward, or the real length `n` back; every
// other axis passes through, symbol and all. The transform is planned for one length, so every extent must be a
// number now, not a symbol bound later (a symbol bound to 64 satisfies it). A rank-0 value has no axis to transform.
// A real FFT length is a power of two of at least two. The transformed axis has to match it: a forward transform
// reads `n` real numbers, an inverse the `2*(n/2+1)` planar numbers a forward one produced.
constexpr Shape fft_shape(const Shape& in, OpCode op, const FftAttr& v, Diagnostic* why = nullptr) {
    if (why != nullptr && !is_static(in)) {
        return refused(in, why, op, " needs every extent known now, but was given ", in);
    }
    if (in.rank < 1) {
        return refused(in, why, op, " transforms a rank-0 value, which has no axis to transform");
    }
    if (v.n < 2 || (why != nullptr && (v.n & (v.n - 1)) != 0)) {
        return refused(in, why, op, " has length ", v.n, "; a real FFT length is a power of two of at least two");
    }
    const bool forward = v.dir == FftDir::Forward;
    if (why != nullptr) {
        const std::int32_t given = literal_of(extent_at(in, in.rank - 1));
        const std::int32_t expected = forward ? v.n : half_spectrum_length(v.n);
        if (given != expected) {
            return refused(in, why, op, forward ? std::string_view{" forward reads "} : std::string_view{" inverse reads "},
                           expected, " numbers on its innermost axis, but was given ", given);
        }
    }
    const std::int32_t inner = forward ? half_spectrum_length(v.n) : v.n;
    Shape shape{};
    for (std::int32_t k = 0; k < in.rank; ++k) {
        set_extent(shape, shape.rank++, k == in.rank - 1 ? literal_extent(inner) : extent_at(in, k));
    }
    return shape;
}

} // namespace detail

} // namespace heinzel
