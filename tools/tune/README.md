# Measuring a `Machine` (Plan 24)

The probes that measure the fields of `schedule/machine.hpp` on the machine they run on: moved here from
`Plan/probes/` (Plan/23 P6 wrote or ported them, Plan 24 turns them into tooling). Each is a program of its own. The
threshold probes plan their graphs for one machine — zen2, or the one `-DHEINZEL_BENCH_MACHINE=<name>` names — with
the field forced to its two ends or a sweep of values, time the programs through the runner, check that the arms
compute the same bits where they must, and print one machine-readable line a field, `tuned <field> <value>` (`none`
where the sweep shows no crossing; an `OpCost` as its latency, then its slots). How each decides is in its header.

| probe | field(s) | how it decides | zen2 |
|---|---|---|---|
| `latency.cpp` | `fp_per_cycle`, `add` … `root_double`, `core`, `core_double`, `shuffle`, `forwarded`, `loads_per_cycle`, `stores_per_cycle` | dependent and independent chains of each operation, the least of two rounds: a register's latency and issue (in slots, times `fp_per_cycle`) rounded, and so on (its header); the cores' median latency and issue rounded to the nearest ten | 2; 3 1, 5 1, 10 7, 13 10, 14 11, 20 17; core 60 60 (named: 70 60), core_double 110 100; 3 1; 8; 2, 1 |
| `memory.cpp` | `memory_bytes` | maps over buffers of four times the last-level cache (its argument, the L3's bytes), 256 MiB at least (macOS reports no cache past the L2), and in L1: a pass's cycles through memory against its bytes, least squares through the origin | 4 (4.2-4.3 bytes a cycle) |
| `relay.cpp` | `relay_pays_after` (recorded, not taken: below) | `A·B`, B 256 × 256 relaid or not, A of 2 to 64 blocks of rows: the fewest blocks from which on the relaid program is no longer clearly slower (median of 60 processes; 2 % or twice the standard error) | 12 (the probe reads 24: the gain stays at 0.96 from 10 to 16 blocks, 0.99 at 24) |
| `recompute.cpp` | `recompute_always`, `recompute_if_uncached` | conv's square inside the window or stored whole, 2 to 64 extra operations, at 2 MiB and at the L3's size: the most extra operations at which inside is clearly faster (6 %, the code layout's noise; median of 12 processes), and at every fewer | 8, 32 |
| `band_bytes.cpp` | `band_bytes` | blurxy and harris at 1024 and 8192 rows banded at five budgets (L1, L2 / 8 … L2): the fastest, the least worst ratio over the four points (median of 8 processes) | 256 KiB (L2 / 2) |
| `stream_rows.cpp` | `band_stream_rows` | the FM oscillator and the ladder bank in stream bands of 0 to 128 samples: the least worst ratio to each stream's best (D-6; median of 12 processes) | 16 |
| `turn.cpp` (was `Plan/probes/v5-score/reuse.cpp`) | `turn_pays_after` | `Q·Kᵀ`, K 512 × 64 turned or not, Q of 1 to 64 rows: the fewest rows from which on the turned program is at least 3 % faster (median of 40 processes) | 32 (the probe reads 16) |
| `parallel_work.cpp` | `parallel_work` | elementwise, a fold, conv and matmuls, split among 2, 4, 8 … workers: the power of two above every losing split's work a worker | 2^18 (16 workers) |
| `window.cpp` | `window` | a cascade of 2 to 4 biquad sections in both orders of its feedback sums (`rewrite/feedback_sums.hpp`), timed through the runner (median of 8 processes): of the windows at which the rule takes the faster order at every count, the one whose priced B/F is nearest the measured | 95 (named: 100) |

`cycles.hpp` is the clock `latency` and `memory` count cycles by: Linux's counter where the kernel allows it, else
nanoseconds times a clock calibrated on a chain of integer adds (macOS, a Linux that refuses the counter, or
`-DCYCLES_CALIBRATED`; on zen2 the two read the same fields). `probe.hpp` is what the threshold probes share: the machine they plan for, a program timed through the runner on a
workspace of its own, and `in_processes` (the layout of the address space moves a time by several percent, which no
number of rounds in one process averages out, so a probe that times a gain measures in child processes and decides by
their median).

Not measured here: the facts of the instruction set and the operating system (`vector_bytes`, `registers`, `fma`, the
cache sizes, `line_bytes`, `page_bytes`, `cores`), and the fields no probe decides in seconds (`contention`,
`stage_start`, `merge_work`; `Plan/24-machine-tuning/01-Fields.md` says why). The probes that only investigate stay in
`Plan/probes/`: `thresholds/caches.cpp` (the cache steps), `thresholds/band_when_over.cpp`, `m5-rows`, `m5-conv` and
the cost corpus.

## At configure time

`machine.cmake` (included by the top-level `CMakeLists.txt` with `-DHEINZEL_AUTOTUNE=ON`) runs these probes while CMake
configures and writes `heinzel::host` into `<build>/heinzel_tune/include/heinzel/host_machine.hpp`: the facts from the
compiler (its macros under the build's flags) and the operating system; stage 1, `latency` and `memory`, written as
`host`; stage 2, the threshold probes planned for that `host` (`-DHEINZEL_BENCH_MACHINE=host`: `probe.hpp` includes the
generated header where it finds it); then the header again with everything. Each probe is built with the build's
compiler and flags (`CMAKE_CXX_FLAGS` and its build type's; a target named in `CMAKE_CXX_COMPILER_TARGET` or
`CMAKE_OSX_ARCHITECTURES` is not followed; through ccache where the build uses it) and run pinned to `HEINZEL_TUNE_CORE`
(4, or the last CPU; unpinned, with one warning, where `taskset` cannot pin to it), `parallel_work` with
`HEINZEL_AUTOTUNE_WORKERS` workers (every core; written only when they are every core) on as many CPUs from there. On a
hybrid Intel processor CPU 4 may be an efficiency core (two performance cores are CPUs 0-3 with their second threads):
name a performance core in `HEINZEL_TUNE_CORE` (`lscpu -e` gives each CPU's MAXMHZ). A reading that fails or lies off
its range keeps its default, with a warning and a line in the header; `contention`, `stage_start`,
`merge_work` and `band_when_over` always do. So does `relay_pays_after` (Gerald, Plan 24 answer 11): `relay` runs and
its reading is kept (`relay.txt`, a line in `machine.txt` and the header), but its B (256 KiB) fits the L2, and the
planner's rule counts blocks of rows, not B's bytes (`loops/planner/contraction.hpp` `relay_where_it_pays`): planned for
its 24, `ml/linear_relu` ran 1.50x and `ml/mlp2` 1.25-1.30x on zen2, whose B does not fit the L2.
`<build>/heinzel_tune/` keeps `machine.txt`, measured again at a configure, and only when it is missing or its key
changed (the compiler, its flags, threads and workers, the code here but this README): a probe edited is measured at the
next configure, not by a build (the probes are no dependency of the build, and a measurement takes minutes; the `.cmake`
files and the header's template are CMake's own, so editing one makes the next build configure and measure), and each
probe's `<probe>.txt` and `<probe>.build.log`. Cross-compiling with the option on stops the configure. `header.cmake`
holds `Machine`'s fields in declaration order and writes the header; the test `host_machine_header` (in every build)
writes one with every field measured and compiles it, so a field renamed or moved in `machine.hpp` fails there. Each
field's rule and range: `Plan/24-machine-tuning/01-Fields.md`.

## Running by hand

    Plan/probes/thresholds/run.sh <out-dir> [probe ...]

builds the probes here (or the ones named: `relay`, or a path under `Plan/probes` without `.cpp`, `thresholds/caches`),
one at a time, each run as soon as it is built; `MACHINE=m1_pro` plans for the M1, `CXX` picks the compiler. On Linux
each runs pinned (`taskset -c $CORE`, `parallel_work` on `$PAR_CORES` with `$WORKERS` workers: 0-3 and 4 by default,
the box's limit); without `taskset` (macOS) unpinned. `<out-dir>/tuned.txt` collects the `tuned` lines.

On zen2 each must reproduce the value in the table above, within the grid's resolution; `parallel_work` with four
workers may read lower than 2^18, which was measured with sixteen.
