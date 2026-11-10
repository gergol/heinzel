#pragma once

// The workloads `Plan/15-zero-overhead/01` measures an `Executor` on, without the library: each one's ports in its
// graph's order, its frames, the loop a user would write for it, and the bound that loop may stray
// within. Their graphs are in `workloads.hpp` (this tree's DSL) and `main_arm.cpp` (`main`'s). Data is `float`
// throughout; a Param is an input of one element. A hand loop keeps its state across calls the way a user would, in
// variables or a small buffer it owns, and sums in the program's order where its `hand_bound` is 0 (then it owes the
// program's bits), and otherwise strays no further than the bound a reordered sum may.

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace zo {

struct Port {
    const char* name;
    std::size_t floats;
    bool param = false;
};

using In = const float* const*;
using Out = float* const*;

} // namespace zo

namespace zo::w1 { // gain

inline constexpr std::array kIn{Port{"x", 64}, Port{"g", 1, true}};
inline constexpr std::array kOut{Port{"y", 64}};
inline constexpr std::int64_t kFrames = 64;

struct Hand {
    void run(In in, Out out, std::int64_t) {
        const float *x = in[0];
        const float g = in[1][0];
        for (std::size_t i = 0; i < 64; ++i) {
            out[0][i] = x[i] * g;
        }
    }
};

inline double hand_bound(In, std::size_t) { return 0.0; } // the program's bits

} // namespace zo::w1

namespace zo::w2 { // elem, a Length

inline constexpr std::array kIn{Port{"a", 4096}, Port{"b", 4096}};
inline constexpr std::array kOut{Port{"y", 4096}};
inline constexpr std::int64_t kFrames = 4096;

struct Hand {
    void run(In in, Out out, std::int64_t frames) {
        const float *a = in[0];
        const float *b = in[1];
        for (std::int64_t i = 0; i < frames; ++i) {
            out[0][i] = a[i] * b[i] + b[i];
        }
    }
};

inline double hand_bound(In, std::size_t) { return 0.0; } // the program's bits

} // namespace zo::w2

namespace zo::w3 { // biquad

inline constexpr std::array kIn{Port{"x", 64}};
inline constexpr std::array kOut{Port{"y", 64}};
inline constexpr std::int64_t kFrames = 64;
inline constexpr float kC1 = 2.0f * 0.9f * 0.70710678f;
inline constexpr float kC2 = -(0.9f * 0.9f);

struct Hand { // (the program's order, the newest state last, and its state in double: Decision 66)
    double y1 = 0.0;
    double y2 = 0.0;

    void run(In in, Out out, std::int64_t) {
        const float* x = in[0];
        double a = y1;
        double b = y2;
        for (std::size_t i = 0; i < 64; ++i) {
            const double v = (static_cast<double>(x[i]) + static_cast<double>(kC2) * b) + static_cast<double>(kC1) * a;
            out[0][i] = static_cast<float>(v);
            b = a, a = v;
        }
        y1 = a, y2 = b;
    }
};

inline double hand_bound(In, std::size_t) { return 0.0; } // the program's bits

} // namespace zo::w3

namespace zo::w4 { // comb

inline constexpr std::array kIn{Port{"x", 512}};
inline constexpr std::array kOut{Port{"y", 512}};
inline constexpr std::int64_t kFrames = 512;

struct Hand {
    std::array<float, 40> ring{};
    std::size_t at = 0;

    void run(In in, Out out, std::int64_t) {
        for (std::size_t i = 0; i < 512; ++i) {
            const float v = in[0][i] + 0.5f * ring[at];
            ring[at] = v;
            out[0][i] = v;
            at = at + 1 == ring.size() ? 0 : at + 1;
        }
    }
};

inline double hand_bound(In, std::size_t) { return 0.0; } // the program's bits

} // namespace zo::w4

namespace zo::w5 { // fir, 16 literal taps

inline constexpr std::array kIn{Port{"x", 512}};
inline constexpr std::array kOut{Port{"y", 512}};
inline constexpr std::int64_t kFrames = 512;
inline constexpr std::array<float, 16> kTaps{0.0125f, -0.025f, 0.0375f, 0.05f,  -0.0625f, 0.125f, 0.1875f, 0.25f,
                                             0.25f,   0.1875f, 0.125f,  -0.0625f, 0.05f,  0.0375f, -0.025f, 0.0125f};

struct Hand {
    std::array<float, 15 + 512> line{}; // the last 15 samples of x, then this block

    void run(In in, Out out, std::int64_t) {
        std::copy(in[0], in[0] + 512, line.begin() + 15);
        for (std::size_t i = 0; i < 512; ++i) {
            float sum = kTaps[0] * line[i + 15];
            for (std::size_t k = 1; k < 16; ++k) {
                sum = sum + kTaps[k] * line[i + 15 - k];
            }
            out[0][i] = sum;
        }
        std::copy(line.end() - 15, line.end(), line.begin());
    }
};

inline double hand_bound(In, std::size_t) { return 0.0; } // the program's bits

} // namespace zo::w5

namespace zo::w6 { // rowsum

inline constexpr std::array kIn{Port{"x", 64 * 64}};
inline constexpr std::array kOut{Port{"y", 64}};
inline constexpr std::int64_t kFrames = 1;

struct Hand {
    void run(In in, Out out, std::int64_t) {
        for (std::size_t i = 0; i < 64; ++i) {
            float sum = 0.0f;
            for (std::size_t j = 0; j < 64; ++j) {
                sum += in[0][i * 64 + j];
            }
            out[0][i] = sum;
        }
    }
};

// (n - 1) · eps · Σ|x| over the row: how far two orders of one sum may differ.
inline double hand_bound(In in, std::size_t e) {
    double magnitude = 0.0;
    for (std::size_t j = 0; j < 64; ++j) {
        magnitude += std::fabs(static_cast<double>(in[0][e * 64 + j]));
    }
    return 63 * static_cast<double>(FLT_EPSILON) * magnitude;
}

} // namespace zo::w6

namespace zo::w7 { // matmul

inline constexpr std::array kIn{Port{"a", 64 * 64}, Port{"b", 64 * 64}};
inline constexpr std::array kOut{Port{"y", 64 * 64}};
inline constexpr std::int64_t kFrames = 1;

struct Hand {
    void run(In in, Out out, std::int64_t) {
        for (std::size_t i = 0; i < 64; ++i) {
            for (std::size_t j = 0; j < 64; ++j) {
                float sum = 0.0f;
                for (std::size_t k = 0; k < 64; ++k) {
                    sum += in[0][i * 64 + k] * in[1][k * 64 + j];
                }
                out[0][i * 64 + j] = sum;
            }
        }
    }
};

// (K - 1) · eps · Σ_k |a_ik · b_kj|.
inline double hand_bound(In in, std::size_t e) {
    const std::size_t i = e / 64;
    const std::size_t j = e % 64;
    double magnitude = 0.0;
    for (std::size_t k = 0; k < 64; ++k) {
        magnitude += std::fabs(static_cast<double>(in[0][i * 64 + k]) * static_cast<double>(in[1][k * 64 + j]));
    }
    return 63 * static_cast<double>(FLT_EPSILON) * magnitude;
}

} // namespace zo::w7

namespace zo::w8 { // blur, banded

inline constexpr std::size_t kH = 1024;
inline constexpr std::size_t kW = 2048;
inline constexpr std::array kIn{Port{"x", kH * kW}, Port{"wh", 3}, Port{"wv", 3}};
inline constexpr std::array kOut{Port{"y", (kH - 2) * (kW - 2)}};
inline constexpr std::int64_t kFrames = 1;

struct Hand {
    std::unique_ptr<float[]> bx{new float[kH * (kW - 2)]}; // the horizontal pass

    void run(In in, Out out, std::int64_t) {
        const float *x = in[0];
        const float *wh = in[1];
        const float *wv = in[2];
        for (std::size_t i = 0; i < kH; ++i) {
            for (std::size_t j = 0; j < kW - 2; ++j) {
                const float* at = x + i * kW + j;
                bx[i * (kW - 2) + j] = (at[0] * wh[0] + at[1] * wh[1]) + at[2] * wh[2];
            }
        }
        for (std::size_t i = 0; i < kH - 2; ++i) {
            for (std::size_t j = 0; j < kW - 2; ++j) {
                const float* at = bx.get() + i * (kW - 2) + j;
                out[0][i * (kW - 2) + j] = (at[0] * wv[0] + at[kW - 2] * wv[1]) + at[2 * (kW - 2)] * wv[2];
            }
        }
    }
};

inline double hand_bound(In, std::size_t) { return 0.0; } // the program's bits

} // namespace zo::w8

namespace zo::w9 { // voice: no hand loop

inline constexpr std::array kIn{Port{"gate", 512}, Port{"pitch", 512}}; // the graph meets gate first
inline constexpr std::array kOut{Port{"out", 512}};
inline constexpr std::int64_t kFrames = 512;

} // namespace zo::w9

namespace zo::w10 { // kernel, 3 constant taps

inline constexpr std::array kIn{Port{"x", 4096}};
inline constexpr std::array kOut{Port{"y", 4094}};
inline constexpr std::int64_t kFrames = 1;
inline constexpr float kSmooth[3] = {0.25f, 0.5f, 0.25f};

struct Hand {
    void run(In in, Out out, std::int64_t) {
        for (std::size_t i = 0; i < 4094; ++i) {
            out[0][i] = 0.25f * in[0][i] + 0.5f * in[0][i + 1] + 0.25f * in[0][i + 2];
        }
    }
};

// 2 · eps · Σ|k·x| over the window: how far two orders of one three-term sum may differ.
inline double hand_bound(In in, std::size_t e) {
    double magnitude = 0.0;
    for (std::size_t k = 0; k < 3; ++k) {
        magnitude += std::fabs(static_cast<double>(kSmooth[k]) * static_cast<double>(in[0][e + k]));
    }
    return 2 * static_cast<double>(FLT_EPSILON) * magnitude;
}

} // namespace zo::w10

namespace zo::w11 { // gain, ranged: W1's ports, frames, hand loop and bound

using w1::kIn, w1::kOut, w1::kFrames, w1::Hand, w1::hand_bound;

} // namespace zo::w11

namespace zo::w12 { // sections in lanes: no hand loop

inline constexpr std::array kIn{Port{"x", 512}};
inline constexpr std::array kOut{Port{"y", 512}};
inline constexpr std::int64_t kFrames = 512;

} // namespace zo::w12
