#pragma once

// How this library reports that something is wrong. `fail()` throws: during constant evaluation, where
// a graph is built and checked, a throw is not a constant expression, so the build stops at the line that
// was wrong; at run time it is an ordinary throw. One code path, with no test for which of the two it is in.
// The error carries its message in a fixed buffer, so a message assembled on the spot is still readable
// after the stack has unwound.

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "heinzel/support/fixed_string.hpp"

namespace heinzel {

// Long enough for a message that names an operation and two disagreeing shapes.
inline constexpr std::size_t kMaxMessageLength = 96;

struct error {
    fixed_string<kMaxMessageLength> text{};

    constexpr std::string_view message() const { return text.view(); }
};

[[noreturn]] constexpr void fail(std::string_view message) {
#if defined(__cpp_exceptions) && __cpp_exceptions
    error e{};
    const std::size_t n = message.size() < kMaxMessageLength ? message.size() : kMaxMessageLength - 1;
    for (std::size_t i = 0; i < n; ++i) {
        e.text.data_[i] = message[i];
    }
    throw e;
#else
    // Exceptions turned off, as on a bare-metal target. A trap is not a constant expression either,
    // so a build still stops at the line; at run time it halts instead of unwinding.
    (void)message;
    __builtin_trap();
#endif
}

// ---- a checked answer, with a message that names what was wrong ---------------------------------
//
// `fail()` stops everything, which is right for a mistake inside the library. A pass checking a
// user's graph should instead finish looking and then say what disagreed with what, so it returns one
// of these. The message is built rather than chosen from a list, so that a shape conflict names the
// operation and both shapes.
struct Diagnostic {
    bool ok{true};
    fixed_string<kMaxMessageLength> message{};

    constexpr bool operator==(const Diagnostic&) const = default;
};

// ---- the digits of a number, for `MessageWriter` -------------------------------------------------------------
namespace detail {

// A positive number's significant digits d0.d1… and its power of ten.
struct Decimal {
    char digits[17]{};
    std::int32_t count{0};
    std::int32_t exponent{0};
};

// An exact natural number, low word first, as wide as the shortest digits of a double need: their largest numbers are
// about 2^1080, ten times the scale of the least subnormal's.
inline constexpr std::size_t kNaturalWords = 36;
struct Natural {
    std::uint32_t words[kNaturalWords]{};
};

constexpr Natural shifted(std::uint64_t f, std::int32_t k) { // f · 2^k
    Natural n{};
    for (std::int32_t i = 0; i < 64; ++i) {
        if ((f >> i & 1) != 0) {
            n.words[(i + k) / 32] |= std::uint32_t{1} << ((i + k) % 32);
        }
    }
    return n;
}

constexpr Natural tenfold(Natural n) {
    std::uint64_t carry = 0;
    for (std::uint32_t& w : n.words) {
        carry += std::uint64_t{w} * 10;
        w = static_cast<std::uint32_t>(carry);
        carry >>= 32;
    }
    return n;
}

constexpr Natural plus(Natural a, const Natural& b) {
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < kNaturalWords; ++i) {
        carry += std::uint64_t{a.words[i]} + b.words[i];
        a.words[i] = static_cast<std::uint32_t>(carry);
        carry >>= 32;
    }
    return a;
}

constexpr Natural minus(Natural a, const Natural& b) { // a >= b
    std::uint64_t borrow = 0;
    for (std::size_t i = 0; i < kNaturalWords; ++i) {
        const std::uint64_t d = std::uint64_t{a.words[i]} - b.words[i] - borrow;
        a.words[i] = static_cast<std::uint32_t>(d);
        borrow = d >> 63; // a word smaller than what it gives wraps around
    }
    return a;
}

constexpr int compare(const Natural& a, const Natural& b) {
    for (std::size_t i = kNaturalWords; i > 0; --i) {
        if (a.words[i - 1] != b.words[i - 1]) {
            return a.words[i - 1] < b.words[i - 1] ? -1 : 1;
        }
    }
    return 0;
}

// The shortest digits that read back as the double v > 0, and of those the nearest to v, a tie to an even digit: Steele
// and White's free-format digits, as Burger and Dybvig write them, on exact naturals. v = f·2^e is r/s, and a number
// reads back as v within high/s above it and low/s below it, those bounds included where f is even (reading rounds a tie
// to even); where `closer_below`, a power of two, the gap below is half the one above. s grows, or r with the gaps,
// until v's first digit is the first after the point. Then each step takes one digit, until the digits so far, or with
// the last one more, read back as v.
constexpr Decimal shortest(double v) {
    const auto bits = std::bit_cast<std::uint64_t>(v);
    const auto biased = static_cast<std::int32_t>(bits >> 52);
    const std::uint64_t hidden = std::uint64_t{1} << 52;
    const std::uint64_t f = (bits & (hidden - 1)) | (biased != 0 ? hidden : 0);
    const std::int32_t e = (biased != 0 ? biased : 1) - 1075;
    const bool even = f % 2 == 0;
    const bool closer_below = biased > 1 && f == hidden;
    const std::int32_t half = closer_below ? 1 : 0;
    const std::int32_t up = e < 0 ? 0 : e;
    const std::int32_t down = e < 0 ? -e : 0;
    Natural r = shifted(f, 1 + half + up);
    Natural s = shifted(1, 1 + half + down);
    Natural high = shifted(1, half + up);
    Natural low = shifted(1, up);
    const auto reaches = [&](const Natural& a) { return compare(a, s) >= (even ? 0 : 1); };
    Decimal out{.exponent = -1};
    for (; reaches(plus(r, high)); ++out.exponent) {
        s = tenfold(s);
    }
    for (; !reaches(tenfold(plus(r, high))); --out.exponent) {
        r = tenfold(r), high = tenfold(high), low = tenfold(low);
    }
    for (;;) {
        r = tenfold(r), high = tenfold(high), low = tenfold(low);
        char d = '0';
        for (; compare(r, s) >= 0; ++d) {
            r = minus(r, s);
        }
        const bool below = compare(r, low) < (even ? 1 : 0);
        const bool above = reaches(plus(r, high));
        if (below || above) {
            const int nearer = compare(plus(r, r), s);
            const bool next = above && (!below || nearer > 0 || (nearer == 0 && (d - '0') % 2 == 1));
            out.digits[out.count++] = static_cast<char>(d + (next ? 1 : 0));
            return out;
        }
        out.digits[out.count++] = d;
    }
}

} // namespace detail

// Assembles a message a piece at a time, truncating at the buffer's end rather than running past it.
class MessageWriter {
public:
    // How it writes a number: as `%g` writes it, six significant digits; or the shortest digits that read back as the same
    // double, laid out as `%g` lays them out (`std::to_chars`'s general format). A debug build's checks write theirs so
    // (Plan/26-value-ranges): six digits would show a Param set 1e-12 past its bound as the bound.
    enum class Digits : std::uint8_t { Six, Shortest };

    constexpr MessageWriter() = default;
    constexpr explicit MessageWriter(Digits digits) : digits_(digits) {}

    constexpr MessageWriter& operator<<(std::string_view text) {
        for (char ch : text) {
            if (used_ + 1 < kMaxMessageLength) {
                message_.data_[used_++] = ch;
            }
        }
        return *this;
    }

    constexpr MessageWriter& operator<<(std::int64_t value) {
        if (value < 0) {
            *this << std::string_view{"-"};
        }
        // Written into a small buffer back to front, because the digits come out that way.
        char digits[20]{};
        std::size_t n = 0;
        std::uint64_t magnitude = value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1u : static_cast<std::uint64_t>(value);
        do {
            digits[n++] = static_cast<char>('0' + magnitude % 10);
            magnitude /= 10;
        } while (magnitude != 0);
        while (n > 0) {
            *this << std::string_view{&digits[--n], 1};
        }
        return *this;
    }

    // A number in the writer's `Digits`: an exponent below 1e-4 and from 1e6 on, no trailing zeros; `inf`, `-inf`, `nan`.
    // A template, because a plain `double` overload would make `out << int32` ambiguous. A float is written as the double
    // it is, as a check compares it (0.1f reads 0.10000000149011612).
    template <std::floating_point F>
    constexpr MessageWriter& operator<<(F number) {
        double v = static_cast<double>(number);
        if (v != v) {
            return *this << std::string_view{"nan"};
        }
        if (std::bit_cast<std::uint64_t>(v) >> 63 != 0) {
            *this << std::string_view{"-"};
            v = -v;
        }
        if (v > std::numeric_limits<double>::max()) {
            return *this << std::string_view{"inf"};
        }
        if (v == 0) {
            return *this << std::string_view{"0"};
        }
        write_digits(digits_ == Digits::Six ? six_digits(v) : detail::shortest(v));
        return *this;
    }

    constexpr Diagnostic problem() const { return Diagnostic{false, message_}; }

private:
    fixed_string<kMaxMessageLength> message_{};
    std::size_t used_{0};
    Digits digits_{Digits::Six};

    // v's six significant digits, rounded. Its exponent is estimated, and corrected where rounding or the estimate put
    // the digits outside [100000, 999999].
    static constexpr detail::Decimal six_digits(double v) {
        std::int32_t exponent = 0;
        for (double s = v; s >= 10; s /= 10) {
            ++exponent;
        }
        for (double s = v; s < 1; s *= 10) {
            --exponent;
        }
        for (;;) {
            double x = v;
            std::int32_t shift = 5 - exponent;
            for (; shift > 22; shift -= 22) {
                x *= 1e22;
            }
            for (; shift < -22; shift += 22) {
                x /= 1e22;
            }
            double power = 1;
            for (std::int32_t k = shift < 0 ? -shift : shift; k > 0; --k) {
                power *= 10; // exact up to 1e22
            }
            x = shift < 0 ? x / power : x * power;
            auto digits = static_cast<std::int64_t>(x + 0.5);
            if (digits < 100000) {
                --exponent;
            } else if (digits > 999999) {
                ++exponent;
            } else {
                detail::Decimal d{.count = 6, .exponent = exponent};
                for (std::int32_t i = 6; i > 0; --i, digits /= 10) {
                    d.digits[i - 1] = static_cast<char>('0' + digits % 10);
                }
                while (d.count > 1 && d.digits[d.count - 1] == '0') {
                    --d.count;
                }
                return d;
            }
        }
    }

    // Digits d0.d1… times 10^exponent, as `%g` lays them out; past the last digit, zeros.
    constexpr void write_digits(const detail::Decimal& d) {
        const auto digits = [&](std::int32_t from, std::int32_t to) {
            for (std::int32_t i = from; i < to; ++i) {
                *this << std::string_view{i < d.count ? &d.digits[i] : "0", 1};
            }
        };
        if (d.exponent < -4 || d.exponent >= 6) {
            digits(0, 1);
            if (d.count > 1) {
                *this << std::string_view{"."};
                digits(1, d.count);
            }
            *this << std::string_view{d.exponent < 0 ? "e-" : "e+"};
            if (d.exponent > -10 && d.exponent < 10) {
                *this << std::string_view{"0"};
            }
            *this << static_cast<std::int64_t>(d.exponent < 0 ? -d.exponent : d.exponent);
        } else if (d.exponent >= 0) {
            digits(0, d.exponent + 1);
            if (d.count > d.exponent + 1) {
                *this << std::string_view{"."};
                digits(d.exponent + 1, d.count);
            }
        } else {
            *this << std::string_view{"0."};
            for (std::int32_t k = -1; k > d.exponent; --k) {
                *this << std::string_view{"0"};
            }
            digits(0, d.count);
        }
    }
};

} // namespace heinzel
