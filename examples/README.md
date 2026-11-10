# Examples

Two kinds of program live here. **The tour**, `01_…` to `15_…`, is a tutorial: fifteen small programs, read in
order, each adding a few ideas to the last. **The showcases** are complete programs: a reverb, a synthesizer voice, a
mixing console, an image pipeline, a transformer block, a filter learned by gradient descent.

Every one writes its computation in Heinzel's DSL, compiles it while the program itself compiles, runs it through
`Executor`, and checks the result against the same computation written as plain C++, exiting non-zero if the two
disagree. So each is also a test, and each reads in the same order: the graph, then `main`, then the plain C++ it is
checked against.

Build and run them (clang 22, as for the rest of the tree):

```
cmake -S . -B build/examples -G Ninja -DCMAKE_CXX_COMPILER=clang++-22 -DCMAKE_BUILD_TYPE=Release \
      -DHEINZEL_BUILD_TESTS=OFF -DHEINZEL_BUILD_EXAMPLES=ON
cmake --build build/examples
(cd build/examples && ctest)                  # every example, each checking itself
build/examples/examples/example_07_oscillators bell.wav
```

A graph is compiled by the C++ compiler, so a program takes seconds to build, not milliseconds; the larger
showcases take longer. The files an example writes land in the directory it runs in (`build/examples/examples/`
under ctest).

## The tour

**[01 A first graph](01_first_graph.cpp).** A graph is a `constexpr` function: `Input` and `Output` declare its
boundary, arithmetic connects them. `Executor<downmix>` compiles it while the program compiles; at run time you bind
buffers by name (`mixer.input("left") = left.data()`) and call `run()`. A misspelt port name stops the build.

**[02 Params and functions](02_params_and_functions.cpp).** A drive pedal. `Param drive{"drive"}` is one number a run,
set by the host without recompiling. The math is spelled as in C++ (`tanh`, `exp`, `log`, `log10`, `sin`, `cos`,
`sinpi`, `cospi`, `pow`, `abs`, `clamp`, …); `a > b` is a 0/1 mask, as are `<`, `>=` and `<=`, with an expression or a number on either side
(`x > 0.0`), and `where(mask, t, f)` blends without a branch (`t`, `f` or both may be numbers). A piece of a graph
is a `constexpr` function.

**[03 The past](03_the_past.cpp).** `constexpr Index n{48'000}` is the sample clock: `x[n - 2]` reads two samples
back, `x[n - 120.0 * ms]` a duration folded to samples while compiling. A C++ loop writes a FIR's taps into the graph;
a graph function can be a template. Each run continues where the last stopped, so blocks give the numbers of one run;
`reset()` returns to silence. A chorus reads x a distance ago that changes every sample,
`x[n - d.at_most(25.0) * ms]`, blended between two samples: the bound says how much of the past to keep, and `d` is
held to it.

**[04 Recurrences](04_recurrences.cpp).** `y[n] = y[n - 1] + a * (x[n] - y[n - 1])` defines a value by its own past.
A `Variable` holds state that is not an output (a direct form II biquad). A biquad's coefficients are Params the host
works out for a cutoff, so the filter is retuned between runs.

**[05 Composition](05_composition.cpp).** A three-band splitter. Filters are functions of `Expr`, each call a filter
with its own state; the cutoff is an expression, turned into a coefficient by the graph. Several results are
`finish(low, mid, high)`, and what they share is computed once.

**[06 Subgraphs](06_subgraphs.cpp).** A tone control. `auto bass = subgraph("bass", bass_shelf);` makes a piece of a
function that declares its own Params; applied once, `bass(x)` runs it and names them after the piece: `"bass.gain"`,
`"treble.gain"`. A piece lives inside its graph function.

**[07 Oscillators](07_oscillators.cpp).** An FM bell, written to `bell.wav`. A recurrence that no input drives takes
the graph's length, where the graph has exactly one: the clock declares it (`Variable t{Signal{kSamples}}`), and a
phase is `Variable p;`. A phase wrapped by `frac` and read through `sinpi` is a sine at any pitch, for as long as you
like. `audio::wav_writer` writes the file.

**[08 Streaming](08_streaming.cpp).** A stereo echo inside a host's audio callback. `Signal{2, Length{}}` is two
channels whose block length comes at run time: `prepare(512)` once, then `run(frames)` per block, blocks of any size,
in place. A tensor input (`{2}`, a gain per channel) broadcasts against the signal.

**[09 Tensors](09_tensors.cpp).** A table of scores. Tensors are declared by shape, broadcast as in numpy, and folded
by named axes (`scores.mean(0, keepdims)`, an output's too: `z.max(1, keepdims)`); `constant(…)` and `iota(…)` are
tensors known while compiling. A tensor of N numbers is a table: `wavetable(table, phase)` reads it as one cycle of a
wave, the phase in turns, and `lookup(table, x, lo, hi)` as a function at N points from lo to hi, x clamped
(`docs/limitations.md`, "Tables").

**[10 Windows](10_windows.cpp).** A sensor trace despiked and smoothed, an image blurred. `trace.unfold(0, 5)` is every
window of five, read in place; `.median(1)` and `.mean(1)` make filters of them. `unfold2d`, `slice`, `transpose` and
`reshape` are views: they copy nothing.

**[11 Einsum](11_einsum.cpp).** A two-layer classifier in `einsum("bf,fh->bh", x, w1)`, a batch of matrix products,
and an interleaved image split into its colour planes with `rearrange` and averaged with `reduce`.

**[12 Ranges](12_ranges.cpp).** A level meter in dB, `10.0 * log10(power)`. Every graph is held to a proof that no
operation leaves its domain (no `log` of 0, and `log10` is a `log` times a constant, refused in the same words; no
division by a range holding 0); ports state their ranges (`ge(-1), le(1)`), a computed value its `promise`. A graph
that might fail is refused while compiling, in words. A checked `Executor` holds the host to the ranges and the
promises at run time.

**[13 Precision](13_precision.cpp).** `Normal` (float, sums reordered, as fast as the competition), `High` (double,
in written order, the tests' reference) and `Fast`, set per value where it is made (`(a + b).precision(High)`) or per
graph (`return finish(High, y);`); `f64` ports for doubles.

**[14 Learning](14_learning.cpp).** A linear model fitted by gradient descent. `gradients(loss, {Grad{"dw", w}, …})`
appends the backward pass; a plain C++ loop runs the one compiled program and steps the weights.

**[15 Under the hood](15_under_the_hood.cpp).** `print<smooth>()` (`heinzel/print.hpp`) shows what `Executor<smooth>`
runs: the graph as planned, one line a node, the schedule that puts the nodes into stages, and the loops they became;
`try_compile` returns a program or a refusal; the machine (`zen2`, `m1_pro`, `host` from `-DHEINZEL_AUTOTUNE=ON`)
changes how the loops are laid out, not what they mean (at `Normal` it may move a rounding, within `Normal`'s
accuracy).

## The showcases

| program | what it makes | what it shows |
|---|---|---|
| [`synth_voice`](synth_voice.cpp) | `synth_voice.wav`: an arpeggio, 3 s | an envelope, two oscillators, a state-variable filter and a feedback echo, each a short function of recurrences; times in milliseconds |
| [`reverb`](reverb.cpp) | `reverb.wav`: three drum hits in a room | Freeverb: two helpers, a comb and an allpass, build twenty feedback loops with delay lines up to 1617 samples |
| [`analog_ladder`](analog_ladder.cpp) | `ladder.wav`: a swept sawtooth, two saturators | a nonlinear four-pole ladder, its saturator a template argument (`ladder<tanh>`, `ladder<algebraic>`), a cutoff signal in Hz |
| [`mixing_console`](mixing_console.cpp) | `console.wav`: sixteen voices on a stereo bus | sixteen channel strips as one signal, per-channel settings as tensors, constant-power panning, a bus summed over channels, host blocks |
| [`image_pipeline`](image_pipeline.cpp) | `image_input.ppm`, `image_sharp.pgm`, `image_edges.pgm`, `image_relief.pgm` | an image as a tensor; `einsum`, `rearrange`, windows as views, a 3×3 median, kernels as constants, three results from one graph, `Executor<pipeline, zen2>` |
| [`transformer_block`](transformer_block.cpp) | the block's output, checked | attention and an MLP in `einsum`, RMS norm and softmax over `keepdims` reductions, a causal mask from `iota`, a shape error asserted at compile time |
| [`learn_a_filter`](learn_a_filter.cpp) | a loss curve, the learned taps | gradients through a window over a tensor; the same numbers read as a signal (a biquad's recurrence) and as a tensor (the FIR's windows) |

## Where your buffers are

A bound input is read where it lies, and a bound output is where its results land: the run writes it straight into
your buffer, unless the output reads its own past or your buffer overlaps an input the program reads or another
output (in place, `y == x`); then it is computed aside and copied in, so every binding means what it says. A graph
with no `Length` whose workspace takes at most 4 KiB keeps it inside the `Executor`, so an `Executor` on the stack
allocates nothing (`-DHEINZEL_MAX_INLINE_ARENA` moves the line); a checked one with a promise left to check allocates
the check's buffers when it is made.

Since the results land in your memory, where your buffers lie matters on x86: with its output 32 bytes above an input
modulo 4 KiB (4K aliasing), `elem` over 4,096 samples took 2,973 cycles, against 1,572 half a page apart
(`Plan/archive/14-dsl/11` §7). Allocating each buffer with a few cache lines of stagger avoids it.

## What does not run yet

The analysis knows more than the loop tree runs, and some operations have no spelling yet (`docs/limitations.md`,
`Plan/backlog.md`). A graph that holds what the loop tree does not run stops the build, rather than computing a wrong
number, naming the operation as you wrote it and, last, its operands' shapes ("heinzel::compile: unfold: a window over
no element axis, or with a hop below one: [64]"; "topk 3 is not one exact rank, which is not lowered yet: [6, 64]"):

- a window along a signal's time (a framed stream, and overlap-add after it): write a streaming FIR with `x[n - k]`
  (tour 03), or read the signal as a tensor (`learn_a_filter`);
- an `einsum` of a tensor with a signal: write a multiply and a sum, their axes lined up;
- sort, top-k, an even median and interpolated quantiles: one exact rank runs (an odd median, a minimum, a maximum);
- a loop that reads the past of an `Expr` (`Expr r = x + 0.5 * v;` before `v[n] = 0.3 * r[n - 1]`): make `r` a
  `Variable`;
- a delay that changes inside feedback (a flanger, `y[n] = x + 0.7 * y[n - d.at_most(5.0) * ms]`): a fixed lag in
  the loop runs;
- gradients through a matrix product or a recurrence.

Not spelled yet: the FFT; `asin`, `acos`, `atan`, `atan2` and `log2`. `exp`, `log`, `tanh`, `sin`, `cos` and `sinpi`
run on float cores, and `log10` and `cospi`, composed of them; `tan`, `pow` and `exp2` have no float core yet, so a
float runs their double cores one number at a time, more slowly. An `Executor` runs on one thread; only the lower-level
`loops::run` takes a thread pool.
