# Cmajor comparison sources (`bench_compare_cmajor`)

Heinzel vs [Cmajor](https://cmajor.dev) on two of Cmajor's own example patches, unchanged: **Freeverb**
(`examples/patches/Freeverb`) and **Filter** (`examples/patches/Filter`, `std::filters::tpt::svf`, stereo). Build with
`-DHEINZEL_BENCH_COMPARE_CMAJOR=ON`; the arms and the fairness model are in `../bench_compare_cmajor.cpp`'s header.

## Files

- `patches/<Name>/` — the patches, verbatim from `cmajor-lang/cmajor` at tag `1.0.3209` (`2d6ceb6`).
- `generated/<Name>.cpp` — `cmaj generate --target=cpp` of each patch, verbatim and committed: no `cmaj` is needed to
  build. Cmajor's `-O` level does not change this output (checked: `-O0` to `-O4` give the same bytes); it only picks
  the compiler line below.
- `arms.inc` — the host: knobs sent as the patch's `init` values, frames copied in, `advance`, copied out, a
  `maxFramesPerBlock` (512) block at a time. `arms_O3.cpp` / `arms_O4.cpp` compile it once per compiler line.
- `cmajor_arms.hpp` — the entry points the bench calls.

## Compiler lines

Cmajor's documentation names no flags for the generated C++. Its own cpp engine (`cmaj render --engine=cpp`) compiles
it with these, read off by passing a logging `--overrideCompiler` (cmaj 1.0.3209):

| arm          | `cmaj` level           | line                                                                                                          |
|--------------|------------------------|---------------------------------------------------------------------------------------------------------------|
| `cmajor`     | default, `-O2`*, `-O3` | `-O3 -std=c++17 -fPIC -Wno-#pragma-messages -Wno-parentheses-equality -Wno-deprecated-declarations -Wno-tautological-compare -Werror -Wall -Wextra` |
| `cmajor-O4`  | `-O4`                  | the same with `-O3 -ffast-math`                                                                               |

\* `-O2` passes `-O2`; not an arm. The plugin projects `cmaj generate --target=clap` writes set C++17 and leave
optimisation to the build type. There is no `-march`: Cmajor's arms run on the baseline instruction set while every
other arm of the suite has `-march=native` (on the M1 that is NEON either way; on x86 it is SSE2 against AVX2).

`benchmarks/CMakeLists.txt` runs these lines through a custom command, so none of the build's flags reach the arms.
One addition: the `-O4` line also gets `-Wno-nan-infinity-disabled`. Clang 22 rejects the generated code under
`-ffast-math -Werror` because its `infinity()` constants come before the pragma block that silences this warning. The
flag changes no code generation.

## Regenerating

With `cmaj` from a [release](https://github.com/cmajor-lang/cmajor/releases), from the repo root:

```
for p in Freeverb Filter; do
    cmaj generate --target=cpp --output=benchmarks/cmajor/generated/$p.cpp benchmarks/cmajor/patches/$p/$p.cmajorpatch
done
```

A new Cmajor version changes the output: copy in its patches, regenerate, re-measure, and update the tag above.

## License

Cmajor is GPLv3 or commercial. Its LICENSE.md says C++ generated from your *own* Cmajor code is yours; these patches
are Cmajor's own examples, so the generated files keep the examples' license. They are benchmark-only and never enter
the shipped `heinzel` library, the same treatment as the GPL'd KFR arm.
