# FAUST comparison sources (`heinzel_faust_compare_bench`)

Heinzel vs [FAUST](https://faust.grame.fr) — the dedicated audio-DSP compiler — on four shared workloads
(Moog ladder mono + 16-voice, 3-section biquad cascade, Freeverb, sine + tanh strip). Design rationale,
fairness model, and results discussion: [`Plan/archive/20-faust-compare-bench.md`](../../Plan/archive/20-faust-compare-bench.md).

## Why the generated C++ is committed (and `faust` is not a build dependency)

FAUST codegen is a **pure function of `(.dsp, flags, faust version)`** — it does *not* probe the host
(no CPU-feature or cache detection). Architecture-specialization is delegated to the downstream
`clang++ -O3 -march=native` that compiles the generated code, exactly the stage where Heinzel / JUCE / KFR
get theirs. So the generated `.cpp` is portable: we generate once, commit `generated/*.cpp`, and the
build compiles them per-host. **No `faust` binary is required to build or run the benchmark**, and CI
needs nothing extra. `faust` is only needed to *regenerate* (a manual developer step).

## Files

- `*.dsp` — the FAUST sources, one per workload.
- `faust_dsp.h` — a vendored minimal FAUST runtime base (`dsp` / `UI` / `Meta` / `FAUSTFLOAT`), so the
  generated code compiles with no faust headers installed. Force-included into each generated TU by
  CMake (`-include …/faust/faust_dsp.h`).
- `faust_arms.hpp` — declares the per-arm factories the bench calls.
- `generated/<name>_{scal,vec}.cpp` — committed FAUST output (raw, **+ one appended factory line** so
  the bench can instantiate the class). Each is its own translation unit.
- `generated/arms.cmake` — the source list, emitted by the generator and `include()`d by the build.

## Regenerating

Generation-time only. Install faust (e.g. `brew install faust`), then from the repo root:

```
tools/gen-faust.sh
```

This emits both `-scal` (FAUST's default/idiomatic) and `-vec -vs 32` (its autovectorizer) for every
`.dsp`, appends the `heinzel_make_<name>_<mode>()` factory, and rewrites `generated/arms.cmake`. The `-vec`
arm uses FAUST's **default loop variant `-lv 0` ("fastest")** — a fixed inner vector size plus a
remainder loop, which the FAUST manual documents as the fastest. We deliberately do NOT pass `-lv 1`
("simple", variable trip count) — it vectorizes worse, so using it would under-report FAUST.

**Pinned version:** generated with **FAUST 2.85.5**. A version bump changes the output — regenerate and
re-measure (and update this line). Build flags per file: `-scal -light -cn heinzel_<name>_scal` and
`-vec -vs 32 -light -cn heinzel_<name>_vec`.

If a `.dsp` spec changes, the biquad coefficients in `biquad3.dsp` are Heinzel's own RBJ values
(`dsp::highpass/peaking/lowpass`); recompute them from heinzel if the EQ spec changes (see the comment in
`biquad3.dsp`).

## License

FAUST grants an explicit exception for its compiler output (the generated code is the user's, not
GPL-encumbered); the FAUST standard library is permissively licensed. The generated files here are
**benchmark-only** and never enter the shipped `heinzel` library — the same posture as the GPL'd KFR arm.
