// The many-channel rows of the KFR comparison (bench_compare_kfr, Plan/23 09 (b)): biquad3's EQ (`kEq`) on 2, 8 and 16
// channels, blocks of 256 frames, planar and interleaved; channels_common.hpp has the rows, their layouts and the
// check. This file:
//   naive — per channel the textbook DF-I cascade, one channel after the other, its frames C apart when interleaved.
//   kfr   — an `iir_state<float, 3>` a channel (its lanes hold sections wired into a cascade, so a channel each), run
//           one after the other; interleaved data goes through KFR's `deinterleave` and `interleave` (plain loops)
//           inside the timed run.
// The Heinzel arm is bench_compare_kfr_heinzel_channels.cpp's; this file is compiled as KFR is used, without Heinzel's
// `-ffp-contract=off`.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <benchmark/benchmark.h>

#include <kfr/base.hpp>
#include <kfr/dsp.hpp>

#include "channels_common.hpp"
#include "kfr_compare_common.hpp"
#include "kfr_compare_refs.hpp"

#include "page_vector.hpp"

using heinzel_bench::PageVector;
using heinzel_bench::OutputVector;

namespace {

using namespace kfrcmp;
using channels::kFrames;
using channels::Layout;

// A channel's two blocks through the textbook DF-I cascade: the reference.
void eq_reference(const float* x, float* y, std::size_t n) { df1_cascade<3>(kEq, x, y, static_cast<std::int32_t>(n)); }

// ---- naive ---------------------------------------------------------------------------------------------------------
struct Df1 {
    float x1 = 0;
    float x2 = 0;
    float y1 = 0;
    float y2 = 0;
};

// One channel's block through the textbook DF-I cascade, its frames `stride` apart, the state carried. The coefficients
// and the state are copied into the loop, so that no store to `y` can touch them.
void df1_channel(std::array<Df1, 3>& st, const float* x, float* y, std::size_t stride) {
    const std::array<BiquadCoeffs, 3> eq = kEq;
    std::array<Df1, 3> s = st;
    for (std::size_t t = 0; t < kFrames; ++t) {
        float v = x[t * stride];
        for (std::size_t k = 0; k < 3; ++k) {
            const BiquadCoeffs& c = eq[k];
            const float o = c.b0 * v + c.b1 * s[k].x1 + c.b2 * s[k].x2 - c.a1 * s[k].y1 - c.a2 * s[k].y2;
            s[k].x2 = s[k].x1;
            s[k].x1 = v;
            s[k].y2 = s[k].y1;
            s[k].y1 = o;
            v = o;
        }
        y[t * stride] = v;
    }
    st = s;
}

void bench_naive(benchmark::State& state, Layout layout, std::size_t C) {
    OutputVector<std::array<Df1, 3>, 1> st(C);
    const PageVector<float> xi = channels::interleaved(g_sine_in.data(), C);
    const std::vector<float> ref = channels::reference(g_sine_in.data(), C, eq_reference);
    OutputVector<float> y(C * kFrames);
    channels::time_arm(state, layout, C, ref, y.data(), [&](std::size_t b) {
        for (std::size_t c = 0; c < C; ++c) {
            if (layout == Layout::planar) {
                df1_channel(st[c], g_sine_in.data() + (b + c) * kFrames, y.data() + c * kFrames, 1);
            } else {
                df1_channel(st[c], xi.data() + b * kFrames * C + c, y.data() + c, C);
            }
        }
    });
}

// ---- KFR -----------------------------------------------------------------------------------------------------------
// kEq as KFR's biquad_section(a0, a1, a2, b0, b1, b2): a1 and a2 the values the difference equation subtracts.
kfr::iir_params<float, 3> eq_params() {
    std::array<kfr::biquad_section<float>, 3> s{};
    for (std::size_t k = 0; k < s.size(); ++k) {
        s[k] = kfr::biquad_section<float>(1.0f, kEq[k].a1, kEq[k].a2, kEq[k].b0, kEq[k].b1, kEq[k].b2);
    }
    return kfr::iir_params<float, 3>(s.data(), s.size());
}

// A block of planar channels through their engines, channel after channel: each the call biquad3dyn's arm makes, which
// runs 3.9 ns a sample there and 4.9 here at the same instructions (IPC 1.31 against 0.96; not the output's placement,
// moved 256 bytes: code layout, Plan/probes/audio-iir/kfr_channel_outputs.cpp has both speeds from like spellings).
void eq_block(std::vector<kfr::iir_state<float, 3>>& st, const float* from, float* to) {
    for (std::size_t c = 0; c < st.size(); ++c) {
        auto y = kfr::make_univector(to + c * kFrames, kFrames);
        y = kfr::iir(kfr::make_univector(from + c * kFrames, kFrames), std::ref(st[c]));
    }
}

void bench_kfr(benchmark::State& state, Layout layout, std::size_t C) {
    std::vector<kfr::iir_state<float, 3>> st(C, kfr::iir_state<float, 3>(eq_params()));
    const PageVector<float> xi = channels::interleaved(g_sine_in.data(), C);
    const std::vector<float> ref = channels::reference(g_sine_in.data(), C, eq_reference);
    OutputVector<float> y(C * kFrames); // ours, at half a page (Plan/30): KFR's calls take pointers, eq_block its views
    OutputVector<float, 1> in(C * kFrames);
    OutputVector<float, 2> out(C * kFrames);
    std::array<float*, channels::kMaxChannels> ins{};
    std::array<const float*, channels::kMaxChannels> outs{};
    for (std::size_t c = 0; c < C; ++c) {
        ins[c] = in.data() + c * kFrames;
        outs[c] = out.data() + c * kFrames;
    }
    channels::time_arm(state, layout, C, ref, y.data(), [&](std::size_t b) {
        if (layout == Layout::planar) {
            eq_block(st, g_sine_in.data() + b * kFrames, y.data());
            return;
        }
        kfr::deinterleave(ins.data(), xi.data() + b * kFrames * C, C, kFrames);
        eq_block(st, in.data(), out.data());
        kfr::interleave(y.data(), outs.data(), C, kFrames);
    });
}

const bool g_registered = [] {
    for (const Layout layout : {Layout::planar, Layout::inter}) {
        for (const std::size_t c : channels::kCounts) {
            benchmark::RegisterBenchmark(channels::name_of(layout, "naive", c), bench_naive, layout, c)->UseRealTime();
            benchmark::RegisterBenchmark(channels::name_of(layout, "kfr", c), bench_kfr, layout, c)->UseRealTime();
        }
    }
    return true;
}();

} // namespace
