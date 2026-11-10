#pragma once

// The half of a measured binary that names no library, shared by `arm.cpp` and `main_arm.cpp`: the workload
// -DWORKLOAD picks, its buffers, the hand loop as an arm, and the check of one arm against another.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "hand.hpp"

#if !defined(WORKLOAD)
#error "zero_overhead: -DWORKLOAD=w1 ... w12"
#endif

namespace {

namespace w = zo::WORKLOAD;

constexpr std::size_t kIns = w::kIn.size();
constexpr std::size_t kOuts = w::kOut.size();

template <class T>
T launder(T v) {
    asm volatile("" : "+r"(v));
    return v;
}

// Every port's buffer, its inputs the same numbers in every arm and every build.
struct Buffers {
    std::array<std::unique_ptr<float[]>, kIns> in;
    std::array<std::unique_ptr<float[]>, kOuts> out;

    Buffers() {
        std::uint32_t state = 2463534242u;
        for (std::size_t i = 0; i < kIns; ++i) {
            in[i].reset(new float[w::kIn[i].floats]);
            for (std::size_t e = 0; e < w::kIn[i].floats; ++e) {
                state = state * 1664525u + 1013904223u;
                in[i][e] = static_cast<float>(state >> 8) * 0x1p-23f - 1.0f; // in [-1, 1)
            }
        }
        for (std::size_t o = 0; o < kOuts; ++o) {
            out[o].reset(new float[w::kOut[o].floats]());
        }
    }
    [[gnu::noinline]] ~Buffers() = default; // (the harness's, in every arm alike: left to the inliner, it moved with the arm's size)
    std::array<const float*, kIns> ins() const {
        std::array<const float*, kIns> p{};
        for (std::size_t i = 0; i < kIns; ++i) {
            p[i] = launder<const float*>(in[i].get());
        }
        return p;
    }
    std::array<float*, kOuts> outs() const {
        std::array<float*, kOuts> p{};
        for (std::size_t o = 0; o < kOuts; ++o) {
            p[o] = launder(out[o].get());
        }
        return p;
    }
};

// A template, so that W9, which has no hand loop, never names one.
template <class Hand>
struct HandArm {
    Hand hand{};
    std::array<const float*, kIns> in{};
    std::array<float*, kOuts> out{};
    static constexpr std::size_t kObject = sizeof(Hand);

    explicit HandArm(std::int64_t) {}
    void bind(const Buffers& b) { in = b.ins(), out = b.outs(); }
    [[gnu::noinline]] void run(std::int64_t frames) { hand.run(in.data(), out.data(), frames); }
    const float* result(std::size_t o) { return out[o]; }
    std::size_t kept() { return 0; }
};

// Three runs of `Arm` against three of `Ref` on the same inputs: every output element in bits, or, where
// `bound(inputs, element, wanted)` is positive, within it.
template <class Arm, class Ref, class Bound>
int check(Bound bound) {
    Buffers mine;
    Buffers theirs;
    const std::int64_t frames = launder(w::kFrames);
    Arm arm(frames);
    Ref ref(frames);
    arm.bind(mine), ref.bind(theirs);
    std::size_t bits = 0;
    std::size_t beyond = 0;
    std::size_t total = 0;
    for (int r = 0; r < 3; ++r) {
        arm.run(launder(frames)), ref.run(launder(frames));
        for (std::size_t o = 0; o < kOuts; ++o) {
            for (std::size_t e = 0; e < w::kOut[o].floats; ++e, ++total) {
                const float got = arm.result(o)[e];
                const float want = ref.result(o)[e];
                const double within = bound(mine.ins().data(), e, want);
                const bool same = std::bit_cast<std::uint32_t>(got) == std::bit_cast<std::uint32_t>(want);
                bits += same ? 0 : 1;
                beyond += same || (within > 0.0 && std::fabs(static_cast<double>(got) - static_cast<double>(want)) <= within) ? 0 : 1;
            }
        }
    }
    std::printf("%s: %zu of %zu elements differ in bits, %zu beyond the bound\n", beyond == 0 ? "ok" : "FAILED", bits, total, beyond);
    return beyond == 0 ? 0 : 1;
}

} // namespace
