<p align="center">
  <img src="docs/logo/heinzel.svg" width="280" alt="Heinzel: a Heinzelmännchen on the night shift, carrying a lantern, above the wordmark heinzel&lt;&gt;">
</p>

# Heinzel

**A compile-time computational-graph compiler for DSP, in standard C++23.**

You write a filter, an oscillator or a whole channel strip the way the math reads. While your program compiles,
Heinzel analyses the graph, plans its loops for your machine and lowers them to the loop you would have written by
hand. Like the Heinzelmännchen of Cologne, who did the town's work at night while it slept, Heinzel does its work
before your program runs.

- **Runs inside the C++ compiler.** No code generator, no extra build step, no JIT: a graph is `constexpr` data, and
  clang compiles it with the rest of your program.
- **Graphs are values.** A graph is plain data built by ordinary functions, so filters compose like functions do, and
  subgraphs keep their parameters apart by name (`"bass.gain"`).
- **Plans for your machine.** A cost model chooses the schedule (which steps share a loop, how loops nest, what runs
  in vector lanes) for a described machine: `zen2`, `m1_pro`, or yours, measured with `-DHEINZEL_AUTOTUNE=ON`.
- **Zero overhead.** The binary holds the loops and nothing of the compiler. A test gate holds each gated workload's `Executor`
  within 77 bytes and 9 instructions a run (or 0.1 %) of the bare loop.
- **Checks while compiling.** A misspelt port, a shape that does not fit or a `log` that may see 0 stops the build,
  with a message that says so in words.

## What it looks like

A comb filter, declared and run. Each run continues where the last one stopped.

```cpp
#include <heinzel/dsl.hpp>   // links heinzel_app
#include <vector>

using namespace heinzel;

constexpr Index n{48'000};                    // the sample clock: 3 ms is 144 samples

constexpr Graph comb() {
    Input x{"x", Signal{64}};                 // a signal: 64 samples a run
    Output y{"y"};
    y[n] = x[n] + 0.5 * y[n - 3.0 * ms];      // defined by its own past
    return y;
}

int main() {
    std::vector<float> x(64, 1.0f), y(64);
    Executor<comb> filter;
    filter.input("x") = x.data();
    filter.output("y") = y.data();
    filter.run();
}
```

A level meter in decibels. `speed` is a `Param`, set between runs without recompiling. A port can say which numbers
it may hold, and the compiler proves that no operation sees a number outside its domain:

```cpp
constexpr Index n{};

constexpr Graph meter() {
    Input x{"x", Signal{4800}, ge(-1), le(1)};
    Param speed{"speed", gt(0), le(1)};
    Variable power;
    power[n] = ((1.0 - speed) * power[n - 1] + speed * (x * x)).promise(ge(0));
    Output db{"db"};
    db = 10.0 * log10(power + 1e-10);
    return db;
}
```

Without the `+ 1e-10`, silence would make the power 0, and the build stops with
`heinzel: log may see a number <= 0: its operand is ge(0)`. A checked build (`NDEBUG` not defined) checks the
promises it cannot prove after every run. A release build plans the graph without its ranges, so they cost nothing
there.

## Getting started

Heinzel is header-only. You need **clang 22** and **CMake 3.25** or newer. It is tested on Linux x86-64 and on macOS
arm64 with Homebrew's `llvm@22`. The default is C++26; `-DHEINZEL_CXX_STANDARD=23` builds for C++23. gcc and MSVC
are not supported.

With CMake's FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(heinzel GIT_REPOSITORY https://github.com/gergol/heinzel.git GIT_TAG main)
FetchContent_MakeAvailable(heinzel)

target_link_libraries(my_plugin PRIVATE heinzel_app)
```

As a dependency, Heinzel builds none of its own tests. `heinzel_app` gives you `<heinzel/dsl.hpp>`, `<heinzel/gradients.hpp>` and `<heinzel/print.hpp>` on top of the core
target `heinzel`. Linking it also sets the flags Heinzel needs on your target: `-std=c++2c`, a raised
`-fconstexpr-steps`, `-ffp-contract=off` and `-fno-math-errno`.

A graph has fixed capacities (128 values and nodes, 32 inputs and outputs, …). Raise one with `-DHEINZEL_MAX_<NAME>=N`, the same
in every translation unit. A graph that outgrows one says which one to raise (`docs/limitations.md`, "Capacities").

### Planning for your machine

`Executor<comb>` plans for `default_machine`. `Executor<comb, zen2>` and `Executor<comb, m1_pro>` plan for those
machines. With `-DHEINZEL_AUTOTUNE=ON`, CMake measures the machine it configures on, once per build directory (about
6 minutes). It writes `heinzel::host`, a description of that machine:

```cpp
#include "heinzel/host_machine.hpp"
heinzel::Executor<comb, heinzel::host> filter;
```

A machine changes how the loops are laid out, not what they compute (at `Normal`, a rounding may differ within the
tier's accuracy).

## The examples

[`examples/`](examples/README.md) holds a tour of fifteen small programs, read in order: a first graph, Params and
functions, the past, recurrences, composition, subgraphs, oscillators, streaming, tensors, windows, einsum, ranges,
precision, learning and, last, what happens under the hood. Beside them are complete programs: a synth voice, a
Freeverb reverb, an analog ladder filter, a mixing console, an image pipeline, a transformer block and a filter
learned by gradient descent. Each one checks itself against the same computation written as plain C++, and the audio
ones write `.wav` files.

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-22 -DCMAKE_BUILD_TYPE=Release \
      -DHEINZEL_BUILD_TESTS=OFF -DHEINZEL_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build
build/examples/example_07_oscillators bell.wav
```

A plain `cmake -S . -B build` builds the test suite. Its parity tests, which compare against an older implementation
that is not part of this repository, switch themselves off.

## What else is in it

- **Precision tiers.** `Normal` (float, the default), `High` (double, bit for bit with the reference interpreter) and
  `Fast`, chosen per graph (`finish(High, y)`) or per value (`(a + b).precision(High)`).
- **Tensors.** `einsum("bf,fh->bh", x, w)`, `rearrange`, `reduce` and windows over tensors: an image pipeline and a
  transformer block are graphs like any filter.
- **Gradients.** `gradients(loss, {…})` builds the backward graph while compiling (`learn_a_filter`).
- **A look inside.** `print<comb>()` shows the nodes, the schedule and the loops. `try_compile(g)` returns the
  program or the reason it was refused, so a refusal can be tested with `static_assert`.

## Performance

Heinzel's speed-up over each library: the geomean, over that library's benchmark rows, of its time divided by
Heinzel's. Above 1 means Heinzel is faster.

| against | zen2 | M1 Pro | zen4 | rows |
|---|---|---|---|---|
| Eigen | 0.90× | 0.92× | 0.86× | 18 |
| Cmajor | 1.9× | 1.5× | 2.1× | 4 |
| KFR | 3.0× | 2.7× | 2.3× | 29 |
| hand-written loop | 3.0× | 2.6× | 2.8× | 34 |
| naive loop | 4.4× | 3.0× | 4.5× | 55 |
| Faust | 4.5× | 3.4× | 4.8× | 10 |
| JUCE | 5.8× | 4.7× | 4.6× | 31 |

The rows were measured on 2026-11-08, in float at the `Normal` tier, with clang 22 `-O3 -march=native`, medians of 7
repetitions. zen2 is a Ryzen 9 3950X, zen4 a Ryzen 9 7950X and M1 Pro an Apple M1 Pro, which was busy during its run.
Faust is its scalar build, Cmajor its `-O4` and JUCE its plain arm; FFT rows are left out. Every row, including the
ones Heinzel loses, is in [`docs/performance-zen2.md`](docs/performance-zen2.md),
[`docs/performance-zen4.md`](docs/performance-zen4.md) and [`docs/performance-m1.md`](docs/performance-m1.md),
generated by `tools/bench_report.py`.

## Status

Heinzel is new, and its API may still change. Some things do not run yet: a window along a signal's time (framed
streams and overlap-add), the FFT, `asin`/`acos`/`atan`, a delay that changes inside feedback, gradients through a
matrix product or a recurrence, and more than one thread per `Executor`. A graph that needs one of them stops the
build with a message naming it; it never computes a wrong number. The list is in
[`examples/README.md`](examples/README.md#what-does-not-run-yet) and [`docs/limitations.md`](docs/limitations.md).

## Documentation

- [`docs/architecture.md`](docs/architecture.md): the pipeline from graph to loops, the design rules and how
  correctness is checked.
- [`docs/limitations.md`](docs/limitations.md): what is refused and why, the capacities, and how to work around them.
- [`docs/knob-math.md`](docs/knob-math.md): math on Params alone runs only when a Param changes.
- [`docs/sections-in-lanes.md`](docs/sections-in-lanes.md): cascades of filter sections in vector lanes.

## License

Heinzel is released under the [MIT License](LICENSE).

The benchmarks fetch the libraries they compare against when you turn them on; none of them is part of this
repository. `benchmarks/faust/generated/` and `benchmarks/cmajor/` hold C++ generated by Faust and Cmajor, used
only by the benchmarks, under the terms described in the READMEs beside them.
