#pragma once

// The Cmajor arms' entry points (bench_compare_cmajor): each generated patch driven as a host drives it (arms.inc),
// compiled twice with Cmajor's own compiler lines (README.md): `kO3`, its cpp engine's default, and `kO4`, its `-O4`,
// which adds `-ffast-math`. Nothing here sees Heinzel.

#include <cstddef>

namespace cmajor_arms {

// One patch at 48 kHz, its knobs sent as the patch declares them and settled (`make`), or just initialised (`start`);
// `knobs` sends one value per knob as events, in the patch's order; `run` takes `n` frames.
struct Patch {
    void* (*make)();
    void* (*start)();
    void (*knobs)(void* patch, const float* values);
    void (*run)(void* patch, const float* x, float* y, std::size_t n);
    void (*destroy)(void* patch);
};

// Freeverb: mono in, stereo out, interleaved. Filter: stereo in and out, interleaved.
struct Arms {
    Patch freeverb;
    Patch filter;
};

extern const Arms kO3;
extern const Arms kO4;

} // namespace cmajor_arms
