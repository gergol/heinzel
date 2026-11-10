#pragma once

#include <cstdint>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/support/fail.hpp"

// A knob smoothed as Cmajor smooths one (`std::smoothing::SmoothedValueStream`), an opt-in DSP layer composed from
// frontend spellings and a run's start (`_first`, which users do not spell): where the knob changed, a ramp from where
// the output stands to the knob's value in `frames` equal steps,
//   out = goal - increment * remaining.
// `x` is a knob, the same through a run (a Param, or math on Params): a change is looked for at a run's first sample
// alone, `where(_first, x differs from the goal held, 0)`. The stage that reads it runs that sample apart, so in the
// samples after it the change is a 0 the compiler knows, and the blend and its division drop out (Plan/33 S3).
//
//   Input x{"x", Signal{Length{}}};
//   Param gain{"gain", ge(0.0), le(1.0)};
//   Output y{"y"};
//   y = x * dsp::smoothed(gain, 960); // a change glides over 20 ms at 48 kHz

namespace heinzel::dsp {

constexpr Expr smoothed(Expr x, std::int32_t frames) {
    if (frames < 1) {
        fail("heinzel::dsp::smoothed: a ramp takes one frame or more");
    }
    constexpr Index n{};
    Variable goal;
    Variable increment;
    Variable remaining;
    Variable out;
    const Expr held = goal[n - 1];
    const Expr differs = (x < held) + (held < x); // (`x != held`, which the DSL does not spell)
    const Expr changed = where(detail::first_sample(), differs, 0.0);
    const Expr step = (x - out[n - 1]) / static_cast<double>(frames);
    const Expr counted_down = max(remaining[n - 1] - 1.0, 0.0);
    goal[n] = where(changed, x, held);
    increment[n] = where(changed, step, increment[n - 1]);
    remaining[n] = where(changed, static_cast<double>(frames - 1), counted_down);
    out[n] = goal - increment * remaining;
    return out;
}

} // namespace heinzel::dsp
