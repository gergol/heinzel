// The debug checks' numbers (Plan/26-value-ranges, item 5): a `MessageWriter` in `Digits::Shortest` writes the shortest
// digits that read back as the same double, a float as the double it is (a check compares it so: the review of Plan 26),
// laid out as `%g` lays them out: what `std::to_chars` writes in its general format. Beside it at run time, over random
// doubles and floats of every exponent and over the edges: subnormals, the largest finite numbers, powers of 2 and of 10
// and their neighbours, 0.1, 1 + 1 ulp. Under constant evaluation too, since a check's bounds are written while
// compiling. `%g`'s six digits are `ranges_walk_test`'s.
#include <bit>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>

#include "heinzel/support/fail.hpp"

using namespace heinzel;

namespace {

template <std::floating_point F>
constexpr fixed_string<kMaxMessageLength> written(F x) {
    MessageWriter out{MessageWriter::Digits::Shortest};
    out << x;
    return out.problem().message;
}

static_assert(written(1.0 + 1e-12).view() == "1.000000000001");
static_assert(written(0.1).view() == "0.1" && written(0.1f).view() == "0.10000000149011612");
static_assert(written(5e-324).view() == "5e-324" && written(std::numeric_limits<float>::denorm_min()).view() == "1.401298464324817e-45");
static_assert(written(std::numeric_limits<double>::max()).view() == "1.7976931348623157e+308");
static_assert(written(std::numeric_limits<float>::max()).view() == "3.4028234663852886e+38");
static_assert(written(123456.0).view() == "123456" && written(1e6).view() == "1e+06" && written(1234567.0).view() == "1.234567e+06");
static_assert(written(1e-4).view() == "0.0001" && written(1.5e-5).view() == "1.5e-05" && written(-2.5f).view() == "-2.5");
static_assert(written(-0.0).view() == "-0" && written(std::numeric_limits<double>::infinity()).view() == "inf");
// Six digits write all of these as the bound they lie beside.
static_assert(written(1.0000001f).view() == "1.0000001192092896" && written(0.30000000000000004).view() == "0.30000000000000004");

int g_wrong = 0;
long g_checked = 0;

// A float beside its double's digits.
template <std::floating_point F>
void beside_to_chars(F x) {
    char want[64]{};
    const char* end = std::to_chars(want, want + sizeof want, static_cast<double>(x), std::chars_format::general).ptr;
    const std::string_view expected{want, static_cast<std::size_t>(end - want)};
    const fixed_string<kMaxMessageLength> got = written(x);
    ++g_checked;
    if (got.view() != expected) {
        ++g_wrong;
        std::printf("  FAILED: %.17g written %s, to_chars %s\n", static_cast<double>(x), got.data_, want);
    }
}

// Every exponent: a power of 2, its neighbours, and a power of 10 near it.
template <std::floating_point F>
void edges() {
    using L = std::numeric_limits<F>;
    for (int p = L::min_exponent - L::digits; p < L::max_exponent; ++p) {
        const F x = std::ldexp(F{1}, p);
        beside_to_chars(x), beside_to_chars(std::nextafter(x, F{0})), beside_to_chars(std::nextafter(x, L::infinity()));
    }
    for (int p = L::min_exponent10 - L::digits10 - 2; p <= L::max_exponent10; ++p) {
        const F x = static_cast<F>(std::pow(10.0L, p));
        if (x > 0 && x <= L::max()) {
            beside_to_chars(x), beside_to_chars(std::nextafter(x, F{0}));
        }
    }
    for (const F x : {L::denorm_min(), L::min(), std::nextafter(L::min(), F{0}), L::max(), static_cast<F>(0.1), static_cast<F>(0.2),
                      static_cast<F>(0.3), F{1}, std::nextafter(F{1}, F{2}), std::nextafter(F{1}, F{0}), F{-1.5}}) {
        beside_to_chars(x);
    }
}

// Random bits: every exponent alike, and negative numbers, NaNs left out (`to_chars` may write "-nan").
std::uint64_t g_state = 0x26;
std::uint64_t next() {
    std::uint64_t z = (g_state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

void random_numbers(int count) {
    for (int i = 0; i < count; ++i) {
        const auto d = std::bit_cast<double>(next());
        const auto f = std::bit_cast<float>(static_cast<std::uint32_t>(next()));
        if (d == d) {
            beside_to_chars(d);
        }
        if (f == f) {
            beside_to_chars(f);
        }
    }
}

} // namespace

int main() {
    edges<double>();
    edges<float>();
    random_numbers(20000);
    if (g_wrong != 0) {
        std::printf("shortest_digits_test: %d of %ld wrong\n", g_wrong, g_checked);
        return 1;
    }
    std::printf("shortest_digits_test: %ld numbers written as std::to_chars writes them\n", g_checked);
    return 0;
}
