#pragma once

// A string that lives inside a compile-time value. Names survive into the graph (an input "cutoff", an
// output "y"), and the graph is built in a constant expression and kept: a `std::string`'s memory cannot
// outlive the evaluation that allocated it. A fixed buffer, its capacity part of the type, can be kept like
// any other number.

#include <cstddef>
#include <string_view>

namespace heinzel {

// N counts the terminating '\0' as well, so fixed_string<4> holds three characters.
template <std::size_t N>
struct fixed_string {
    char data_[N]{};

    constexpr fixed_string() = default;

    // Any literal that fits is accepted, so a fixed_string<64> can be built from "x". The deduction
    // guide below still deduces the exact size when one is written directly as a template argument.
    template <std::size_t M>
        requires(M <= N)
    constexpr fixed_string(const char (&s)[M]) {
        for (std::size_t i = 0; i < M; ++i) {
            data_[i] = s[i];
        }
    }

    constexpr std::size_t length() const {
        std::size_t n = 0;
        while (n < N && data_[n] != '\0') {
            ++n;
        }
        return n;
    }

    constexpr std::string_view view() const { return {data_, length()}; }

    constexpr bool operator==(const fixed_string&) const = default;
};

template <std::size_t N>
fixed_string(const char (&)[N]) -> fixed_string<N>;

} // namespace heinzel
