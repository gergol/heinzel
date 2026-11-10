#pragma once

#include "heinzel/frontend/graph_builder.hpp"

// The phase-accumulator oscillator, an opt-in DSP layer composed from frontend spellings (no new operation):
//   phase[n] = frac(phase[n-1] + inc);   out = sinpi(2*phase) * amp
// `inc` is the per-sample phase increment in cycles (freq / sample_rate), so a constant `inc` gives a fixed
// pitch and a varying one gives FM, with the same graph. `frac` keeps the phase in [0,1) every sample, so it
// does not grow and stays exact over an arbitrarily long signal.

namespace heinzel::dsp {

// The phase accumulator, the piece every oscillator flavor shares: phase reads its own value one
// sample ago, adds the increment, and wraps to [0,1). `phase[n - 1]` reads the value before it is
// defined and `phase[n] = …` defines it — the recurrence shape the frontend exists for. The index is
// local: a library never defines the user's `n`.
constexpr Expr phase_accum(Expr inc) {
    constexpr Index n{};
    Variable phase;
    phase[n] = frac(phase[n - 1] + inc[n]);
    return phase;
}

// Waveforms over a phase in cycles ([0,1) per period). Only `sine` is spelled, because only the
// oscillator example needs it; a saw, square or triangle is an ordinary subgraph over `phase` the same way.
namespace wave {
constexpr Expr sine(Expr p) { return sinpi(p * 2.0); } // sin(2*pi*p), via the half-turns core
} // namespace wave

// The sine oscillator: the shared phase accumulator, its sine waveform, scaled by amp. `inc` and
// `amp` are value expressions; a user composing a different waveform calls `phase_accum` directly.
constexpr Expr oscillator(Expr inc, Expr amp) { return wave::sine(phase_accum(inc)) * amp; }

// The oscillator at a constant pitch. When `inc` is the same everywhere the phase is not a recurrence but
// frac(n·inc): an elementwise ramp with no loop-carried dependency, so it can run in vector lanes, unlike the
// recurrence in `oscillator` above. `iota(tensor)` is the index ramp n over the tensor's elements; `tensor`
// declares the output's element axes because `iota` has no operand to take a shape from.
constexpr Expr affine_oscillator(Shape tensor, Expr inc, Expr amp) {
    return wave::sine(frac(iota(tensor) * inc)) * amp;
}

} // namespace heinzel::dsp
