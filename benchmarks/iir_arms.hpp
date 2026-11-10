#pragma once

// The Heinzel arms of the comparison benches' IIR rows (`bench_compare_kfr`, `bench_compare_faust`,
// `bench_compare_klang`, `bench_compare_juce`): a biquad cascade written as the textbook direct form I, as a graph. The
// KFR, FAUST and JUCE files' hand references run that form; klang's runs transposed direct form II, klang's own (the
// same response, another form). Its coefficients are `constexpr` copies of the sets those files design at run time
// (with `std::cos`, or JUCE's own designs), printed once as hex floats; each arm checks its copy bit for bit against the
// file's set before it times anything, so a copy that drifted from its source skips the arm instead of timing another
// filter.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"

namespace iir_arms {

namespace hz = heinzel;

struct Section {
    // y = b0 x + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
};

// `kEq` of `kfr_compare_common.hpp` and `faust_compare_common.hpp`: high-pass 80 Hz, peaking 1 kHz +6 dB, low-pass
// 8 kHz, at 48 kHz.
inline constexpr std::array<Section, 3> kEq{{
    {0x1.fc38e2p-1f, -0x1.fc38e2p+0f, 0x1.fc38e2p-1f, -0x1.fc355p+0f, 0x1.f878e4p-1f},
    {0x1.0b4082p+0f, -0x1.e533bcp+0f, 0x1.bc4618p-1f, -0x1.e533bcp+0f, 0x1.d2c71cp-1f},
    {0x1.3d86bap-3f, 0x1.3d86bap-2f, 0x1.3d86bap-3f, -0x1.3d86bap-1f, 0x1.ec35d2p-3f},
}};

// `kEq8` of `kfr_compare_common.hpp`: an eight-section graphic EQ, 40 Hz to 12 kHz.
inline constexpr std::array<Section, 8> kEq8{{
    {0x1.fe1b8ap-1f, -0x1.fe1b8ap+0f, 0x1.fe1b8ap-1f, -0x1.fe1aa4p+0f, 0x1.fc38dep-1f},
    {0x1.00c51p+0f, -0x1.fc349ap+0f, 0x1.f6ff3p-1f, -0x1.fc349ap+0f, 0x1.f8894ep-1f},
    {0x1.fcd68p-1f, -0x1.f4e78cp+0f, 0x1.ed81fcp-1f, -0x1.f4e78cp+0f, 0x1.ea587ap-1f},
    {0x1.032206p+0f, -0x1.efbf68p+0f, 0x1.db5b6p-1f, -0x1.efbf68p+0f, 0x1.e19f7p-1f},
    {0x1.f422a6p-1f, -0x1.d357aep+0f, 0x1.ba9d6ep-1f, -0x1.d357aep+0f, 0x1.aec014p-1f},
    {0x1.0b65e2p+0f, -0x1.b92dcep+0f, 0x1.7ab028p-1f, -0x1.b92dcep+0f, 0x1.917beep-1f},
    {0x1.dae2e8p-1f, -0x1.4d5a2cp+0f, 0x1.26f52cp-1f, -0x1.4d5a2cp+0f, 0x1.01d814p-1f},
    {0x1.2be764p-2f, 0x1.2be764p-1f, 0x1.2be764p-2f, 0x1.b7df4cp-25f, 0x1.5f3b2cp-3f},
}};

// `kLr8` of `eq8_common.hpp`: an eighth-order Linkwitz-Riley low-pass at 2 kHz, 48 kHz, two Butterworth fourth-order
// cascades, so four RBJ low-passes with Qs 0.5412, 1.3066, 0.5412, 1.3066.
inline constexpr std::array<Section, 4> kLr8{{
    {0x1.c28a2p-7f, 0x1.c28a2p-6f, 0x1.c28a2p-7f, -0x1.8f1e2ep+0f, 0x1.3a64fep-1f},
    {0x1.fbf5cep-7f, 0x1.fbf5cep-6f, 0x1.fbf5cep-7f, -0x1.c1fc24p+0f, 0x1.a3b7a4p-1f},
    {0x1.c28a2p-7f, 0x1.c28a2p-6f, 0x1.c28a2p-7f, -0x1.8f1e2ep+0f, 0x1.3a64fep-1f},
    {0x1.fbf5cep-7f, 0x1.fbf5cep-6f, 0x1.fbf5cep-7f, -0x1.c1fc24p+0f, 0x1.a3b7a4p-1f},
}};

// `kBq3` of `klang_compare_common.hpp`: high-pass 80 Hz, low-pass 8 kHz, low-pass 3 kHz, Q 1/√2, at 48 kHz.
inline constexpr std::array<Section, 3> kKlangBq3{{
    {0x1.fc3906p-1f, -0x1.fc3906p+0f, 0x1.fc3906p-1f, -0x1.fc3574p+0f, 0x1.f8792cp-1f},
    {0x1.3d8b64p-3f, 0x1.3d8b64p-2f, 0x1.3d8b64p-3f, -0x1.3d8b64p-1f, 0x1.ec5b22p-3f},
    {0x1.eac6acp-6f, 0x1.eac6acp-5f, 0x1.eac6acp-6f, -0x1.74494ep+0f, 0x1.25eb72p-1f},
}};

// `kLpfEq` and `kHpfEq` of `klang_compare_common.hpp`, the EQ example's two parallel sections: low-pass 200 Hz,
// high-pass 4 kHz, Q 1/√2, at 48 kHz.
inline constexpr std::array<Section, 2> kKlangEq{{
    {0x1.60c854p-13f, 0x1.60c854p-12f, 0x1.60c854p-13f, -0x1.f685dep+0f, 0x1.ed63ecp-1f},
    {0x1.60ecbep-1f, -0x1.60ecbep+0f, 0x1.60ecbep-1f, -0x1.4795fcp+0f, 0x1.e90dfp-2f},
}};

// `juce_eq()` of `juce_compare_common.hpp`, JUCE's own design of `kEq`'s spec (`IIR::Coefficients::makeHighPass`,
// `makePeakFilter`, `makeLowPass`), read out of the library on x86; checked within 4 ULPs (`near_bits`), since the
// M1's low-pass comes out 1-2 ULPs away.
inline constexpr std::array<Section, 3> kJuceEq{{
    {0x1.fc38ep-1f, -0x1.fc38ep+0f, 0x1.fc38ep-1f, -0x1.fc354ep+0f, 0x1.f878e2p-1f},
    {0x1.0b4082p+0f, -0x1.e533bep+0f, 0x1.bc4618p-1f, -0x1.e533bep+0f, 0x1.d2c71ep-1f},
    {0x1.3d86bep-3f, 0x1.3d86bep-2f, 0x1.3d86bep-3f, -0x1.3d86bap-1f, 0x1.ec35d6p-3f},
}};

// Whether `copy` holds exactly the bits of `set`, an array of anything with the five coefficients.
template <std::size_t K, class C>
bool same_bits(const std::array<Section, K>& copy, const std::array<C, K>& set) {
    for (std::size_t k = 0; k < K; ++k) {
        const float a[] = {copy[k].b0, copy[k].b1, copy[k].b2, copy[k].a1, copy[k].a2};
        const float b[] = {set[k].b0, set[k].b1, set[k].b2, set[k].a1, set[k].a2};
        for (std::size_t i = 0; i < 5; ++i) {
            if (std::bit_cast<std::uint32_t>(a[i]) != std::bit_cast<std::uint32_t>(b[i])) {
                return false;
            }
        }
    }
    return true;
}

// Whether `copy` is within `ulps` of `set`, coefficient by coefficient: for a copy of a design whose last bits vary with
// the platform's libm and contraction (JUCE's, compiled as JUCE is used), so it stays the same filter for timing.
template <std::size_t K, class C>
bool near_bits(const std::array<Section, K>& copy, const std::array<C, K>& set, std::int64_t ulps) {
    for (std::size_t k = 0; k < K; ++k) {
        const float a[] = {copy[k].b0, copy[k].b1, copy[k].b2, copy[k].a1, copy[k].a2};
        const float b[] = {set[k].b0, set[k].b1, set[k].b2, set[k].a1, set[k].a2};
        for (std::size_t i = 0; i < 5; ++i) {
            const std::int64_t d = std::int64_t{std::bit_cast<std::int32_t>(a[i])} - std::bit_cast<std::int32_t>(b[i]);
            if ((a[i] < 0) != (b[i] < 0) || d > ulps || d < -ulps) {
                return false;
            }
        }
    }
    return true;
}

inline constexpr hz::Index n{};

// A float coefficient as the graph's constant, exactly.
constexpr double f(float v) { return static_cast<double>(v); }

// One section over `x`, the written direct form I of the hand references.
constexpr hz::Expr df1(hz::Expr x, const Section& c) {
    hz::Variable y;
    y[n] = f(c.b0) * x + f(c.b1) * x[n - 1] + f(c.b2) * x[n - 2] - f(c.a1) * y[n - 1] - f(c.a2) * y[n - 2];
    return y;
}

// K sections in series over a signal of `extents`: its channels, if any (in lanes, frame-major), then its length (a
// number, or a `Length` given at run time); the last section is the result `y`.
template <std::size_t K>
constexpr hz::Graph df1_cascade(const std::array<Section, K>& c, auto... extents) {
    hz::Input x{"x", hz::Signal{extents...}};
    hz::Expr s = x;
    for (const Section& section : c) {
        s = df1(s, section);
    }
    return hz::finish(hz::Output{"y"} = s);
}

// The coefficients as Params, known only at run time (as a plugin's are): section k's are `b0_k` … `a2_k`.
inline constexpr const char* kCoefficient[8][5] = {
    {"b0_0", "b1_0", "b2_0", "a1_0", "a2_0"},
    {"b0_1", "b1_1", "b2_1", "a1_1", "a2_1"},
    {"b0_2", "b1_2", "b2_2", "a1_2", "a2_2"},
    {"b0_3", "b1_3", "b2_3", "a1_3", "a2_3"},
    {"b0_4", "b1_4", "b2_4", "a1_4", "a2_4"},
    {"b0_5", "b1_5", "b2_5", "a1_5", "a2_5"},
    {"b0_6", "b1_6", "b2_6", "a1_6", "a2_6"},
    {"b0_7", "b1_7", "b2_7", "a1_7", "a2_7"},
};

constexpr hz::Expr df1_param(hz::Expr x, std::size_t k) {
    const auto* name = kCoefficient[k];
    hz::Param b0{name[0]};
    hz::Param b1{name[1]};
    hz::Param b2{name[2]};
    hz::Param a1{name[3]};
    hz::Param a2{name[4]};
    hz::Variable y;
    y[n] = b0 * x + b1 * x[n - 1] + b2 * x[n - 2] - a1 * y[n - 1] - a2 * y[n - 2];
    return y;
}

// `df1_cascade` with the coefficients as Params, set by `bind_coefficients`.
template <std::size_t K>
constexpr hz::Graph df1_cascade_param(auto... extents) {
    static_assert(K <= std::size(kCoefficient));
    hz::Input x{"x", hz::Signal{extents...}};
    hz::Expr s = x;
    for (std::size_t k = 0; k < K; ++k) {
        s = df1_param(s, k);
    }
    return hz::finish(hz::Output{"y"} = s);
}

// Sets a `df1_cascade_param` executor's coefficients to `set`'s (the names are compile-time, hence the fold).
template <std::size_t K, class C>
void bind_coefficients(auto& run, const std::array<C, K>& set) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        const auto value = [&](std::size_t i) {
            const C& c = set[i / 5];
            const float v[] = {c.b0, c.b1, c.b2, c.a1, c.a2};
            return static_cast<double>(v[i % 5]);
        };
        ((run.param(kCoefficient[I / 5][I % 5]) = value(I)), ...);
    }(std::make_index_sequence<5 * K>{});
}

} // namespace iir_arms
