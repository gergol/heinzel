# Heinzel rewrite — performance regression guard vs the old tree (ctg3)

> Superseded as the regression verdict by the generated `perf-regression-zen2.md` (`bench_report.py gate --out`).
> Kept as history: 35 of the 42 recorded `main_median_ns` in `benchmarks/baseline-zen2.json` come from here
> (the blurxy and harris rows were measured live on 2026-09-24, `ladder_bank` on 2026-09-20).

**Status: FINAL (2026-09-08).** The portable subset is complete — all eight primary workload groups
plus the tier-2 apps subset are ported, built, and measured on both trees. See the checklist at the
bottom (all boxes ticked) and the deferred list for what is out of scope and why.

## Verdict — NOT CLEAN (every ported workload regresses > +5%)

The regression guard is **NOT CLEAN**. Every one of the ~40 ported workloads is >5% slower on the
rewrite; the smallest regression measured is **+38%** (core rowsum C64, 1.38×) and there is no workload
within the +5% gate. This is a **single, consistent, architectural** finding, not a set of independent
codegen bugs, and per the task contract it is **recorded, not tuned**.

| workload group | ratio range (new ÷ old) | dominant cause |
|---|---|---|
| core elementwise (a*b+b) | 33–75× | width-1 stream, no across-buffer SIMD |
| core rowsum (reduce) | 1.4–1.8× | reduce-over-width, scalar |
| contraction (matmul) | 47–321× | untiled scalar contraction vs cache-tiled SIMD GEMM |
| channel_strip (tanh chain) | 25.9× | scalar tanh per sample vs SIMD-minimax fused |
| attention (softmax) | 14.6–29.0× | inherits contraction gap |
| oscillator FM (recurrence) | 112× (55× vs scalar) | scheduled-walk (no codegen path for 4M recurrence) |
| order_stats (sliding median) | 197–268× | scalar Order/window vs branchless SIMD selection net |
| fusion conv / vblur | 83–419× | scalar unfold-materialize + reduce, no fusion |
| lane_recurrence (C=16 one-pole bank) | 76.6× | scalar per-lane scheduled walk vs laned SIMD+ILP |
| apps tier-2 (bright/norm/linear/mlp2) | 6.1–290.5× | inherits the elem / reduce / contraction gaps |

**Root attribution (one sentence):** the rewrite at `936e5a0` is a CPU-only, header-only, M5/M6-era
compute core whose generated path emits a **scalar per-element kernel** (and whose recurrences at scale
fall back to a scheduled interpreted walk), while the old tree's Heinzel arm is a mature float **SIMD
backend** with a cache-tiled GEMM and branchless lane-parallel selection networks — so the gap is the
milestone distance between the two, exactly what a regression guard is meant to make visible before the
rewrite's own vectorization/tiling milestones (M10+) land. **Do not read this as a call to optimize:**
the task is to measure and report; optimization awaits Gerald's word.

This report accumulated one ported bench per porting step; rows are grouped by workload below.

## What this measures (interpretation A)

Per-workload, Heinzel-arm-vs-Heinzel-arm: the **rewrite** (`936e5a0`, branch `rewrite`) against the
**old tree** (`ctg3` `c4012b8`, branch `main`), same machine, same compiler, same flags. A workload
is flagged **FAIL** when the rewrite's median real-time is **> 5%** slower than the old tree's
(`ratio = new_median / old_median > 1.05`). External comparison arms (eigen/juce/kfr/faust/klang) and
GPU/FFT/spectral benches are out of scope (see deferred list).

**CLEAN = no ported workload exceeds +5%.** The result is **NOT CLEAN** (see the verdict above and the
per-workload findings). Per the task contract these are *recorded, not optimized* — no tuning of the
rewrite is done unless Gerald asks.

## Provenance

| item | value |
|---|---|
| compiler | clang 22.1.8 (`/home/user/.local/opt/llvm-22/bin/clang++`) |
| build type | Release, `-O3 -DNDEBUG -march=native -ffp-contract=off` (identical both trees) |
| linker / cache | `-fuse-ld=mold` (mold 2.40.4), ccache 4.12.3 |
| C++ standard | `HEINZEL_CXX_STANDARD=26` (`-std=c++2c`) |
| host | Ryzen (32 logical / 16 physical), L1d 32K, L2 512K, L3 16M, AVX2 (SIMD 16B×16) |
| CPU governor | `ondemand` (cannot set `performance` without root — mitigated by core-pinning + reps + median/min) |
| run | Google Benchmark JSON, `taskset -c 2`, `--benchmark_repetitions=7 --benchmark_min_time=0.30s` |
| metric | `real_time` (wall via `clock_gettime`; `perf_event_open` is EPERM-blocked here but does not affect wall timing) |
| rewrite HEAD | `936e5a011c1d83a55a8305526a89b6037cf7f67d` |
| ctg3 HEAD | `c4012b828897aa5c7eb86386eca4dad97eeb56d0` |

### Fairness — which path on each tree

- **new (rewrite):** the GENERATED run — `emit_wide` + the runtime-length
  `run_generated<Program>(feeds, RuntimeWorkspace&)` with `ScalarGenerator`. This is the rewrite's
  compiled/codegen path (no per-node dispatch at run time), the intended counterpart to the old tree's
  compiled backend. `emit`/`compile` happen once outside the timed loop; the workspace is allocated
  once outside the loop; only `run_generated` is timed.
- **old (ctg3):** the core bench's `hz::executor<g, kTarget, hz::simd_backend>` arm — the mature AVX2
  SIMD backend, which is the representative hz arm `run-benchmarks.py hz_baseline()` prefers.
- The rewrite computes in **double** (its mandatory compile==runtime bit-exactness model); the old
  hz arm is **float**. This is a real, contract-driven difference, attributed below — not a defect.

## Findings — per workload

### core_bench → benchmarks/bench_core.cpp

Old `benchmarks/core_bench.cpp` (`heinzel_bench`). Correctness gate passes on both (each ported
workload cross-checks the generated output against a hand double reference at sampled positions before
timing). Medians of 7 reps; min shown as the noise cross-check (median and min agree on every verdict
— none NOISY).

| workload | old median (ns) | new median (ns) | ratio | old min | new min | min ratio | verdict |
|---|--:|--:|--:|--:|--:|--:|:--|
| elem/hz/N4096      | 365.2      | 27302.5   | **74.76×** | 336.9      | 25825.0   | 76.66× | FAIL |
| elem/hz/N262144    | 45923.5    | 1779689.1 | **38.75×** | 43884.9    | 1735185.8 | 39.54× | FAIL |
| elem/hz/N1048576   | 217095.5   | 7209812.2 | **33.21×** | 213370.3  | 6865436.2 | 32.18× | FAIL |
| rowsum/hz/C64      | 1520816.0  | 2105028.0 | **1.38×**  | 1515591.0  | 2087765.0 | 1.38×  | FAIL |
| rowsum/hz/C256     | 2573569.2  | 4605187.2 | **1.79×**  | 2553718.4  | 4565181.4 | 1.79×  | FAIL |
| rowsum/hz/C1024    | 2799475.2  | 4547917.2 | **1.62×**  | 2776351.7  | 4528913.7 | 1.63×  | FAIL |

**Attribution (measure-and-report; not tuned):**
- The elementwise workloads (`y = a*b + b`, width-1 stream) regress **33–75×**. The rewrite's
  generated run at this milestone (M5/M6-era) streams sample-by-sample through a heap-backed
  `RuntimeWorkspace` (`read_at`/`store_at` into a flat `past`/`now` image); clang cannot auto-vectorize
  across that per-sample indirection, so the width-1 stream runs one `double` at a time. The old tree's
  `simd_backend` runs the whole buffer as AVX2 `float` (8-wide). There is **no across-buffer SIMD path**
  for a width-1 elementwise stream in the rewrite yet (`VectorLane` vectorizes a value's *width* axis,
  which is 1 here), so this is an architectural/milestone gap, not a codegen bug. The heap-workspace
  per-sample indirection is the suspected dominant term; root-causing (if wanted) needs
  `valgrind --tool=callgrind` or host-side `perf` (EPERM-blocked in this container).
- The rowsum workloads (`Σ_c x[r,c]`, width-C reduce → width-1 stream) regress **1.4–1.8×**: the
  per-sample reduce over the C width axis vectorizes better (the C loop is inner and dependency-free),
  so the gap is closer to the double-vs-float + workspace-overhead floor than the elementwise case.
- Both trees were built with identical flags including `-ffp-contract=off` (the rewrite's mandatory
  correctness setting, applied to ctg3 too to isolate codegen as the only variable). The old tree's
  kernels still use explicit `fma`/`fma_rn` where spelled, which the flag does not suppress.

### contraction_bench → benchmarks/bench_contraction.cpp

Old `benchmarks/contraction_bench.cpp` (`heinzel_contraction_bench`). Workload: dense matmul
`C[M,N] = Σ_k A[M,K]·B[K,N]`. Mapped to the rewrite's streaming model as a length-1 stream —
`einsum("ikt,kjt->ijt", a, b)`, one sample per iteration = one full matmul (per
tests/parity/einsum_contractions_here.cpp and tests/unit/matmul_test.cpp). Only the Heinzel arm is
ported (naive/hand/Eigen baselines are out of scope; no Eigen fetch). Baseline = old `hz_simd` arm
(the register-blocked, M/N-tiled SIMD microkernel). Correctness-gated (double sum-of-products vs the
generated output at sampled `(i,j)`). Medians of 7 reps; median and min agree on every verdict.

| workload (M×K×N) | old median (ns) | new median (ns) | ratio | verdict |
|---|--:|--:|--:|:--|
| matmul/hz/128        | 40776.0    | 1922979.5     | **47.2×**  | FAIL |
| matmul/hz/256        | 308037.3   | 32400351.0    | **105.2×** | FAIL |
| matmul/hz/512        | 2197985.1  | 702334144.0   | **319.5×** | FAIL |
| matmul/hz/1024       | 17809900.1 | 5724336781.0  | **321.4×** | FAIL |
| matmul/hz/1024x256x1024 | 4353022.2  | 1398580504.0 | **321.3×** | FAIL |
| matmul/hz/2048x64x2048  | 5641704.9  | 733288648.0  | **130.0×** | FAIL |

**Attribution (measure-and-report; not tuned):** the old `hz_simd` arm sustains ~120 GFLOP/s (a
cache-tiled, register-blocked SIMD microkernel driven by `TargetInfo` cache sizes). The rewrite's
generated run has no tiled/blocked contraction microkernel at this milestone — `run_contract_step`
computes each output element's sum-of-products by reading operands element-by-element through the
(heap) workspace, in double, with no register blocking, N-panel reuse, or SIMD packing. The gap grows
with size (47× at 128 → ~321× at 512–1024) exactly as a cache-blind kernel loses to a tiled one once
the operands spill L2/L3. This is an architectural/milestone difference (the rewrite is CPU-only,
header-only, M5/M6-era with no cost-model-driven tiling), not a codegen bug — recorded, not optimized.

### channel_strip_bench → benchmarks/bench_channel_strip.cpp

Old `benchmarks/channel_strip_bench.cpp` (`heinzel_channel_strip_bench`). Workload: input gain → 3
cascaded `tanh` saturation stages → output gain → hard clamp, elementwise over a 1M-sample width-1
stream. Only the Heinzel arm ported (the `composed` / `hand_fused` C references are not Heinzel).
Baseline = old `strip/hz(fused-from-graph)`. Correctness-gated (scalar `std::tanh` reference, loose
tolerance since the rewrite resolves `tanh` at its own accuracy tier). Median of 7 reps.

| workload | old median (ns) | new median (ns) | ratio | verdict |
|---|--:|--:|--:|:--|
| strip/hz(fused-from-graph) | 6527564.5 | 168956134.5 | **25.9×** | FAIL |

**Attribution:** same width-1-stream story as core elem — the old hz arm fuses the whole chain into
one pass and keeps `tanh` on a SIMD minimax path (~153 M items/s); the rewrite's generated run streams
one sample at a time through the heap workspace with scalar `tanh` per sample (~6 M items/s). No
across-buffer SIMD for a width-1 stream at this milestone. Recorded, not tuned.

### attention_bench → benchmarks/bench_attention.cpp

Old `benchmarks/attention_bench.cpp` (`heinzel_attention_bench`). Workload: single-head scaled
dot-product self-attention `o = softmax_k((Q·Kᵀ)·s)·V`, Q/K/V/o all `[N,D]`, `D=64`, `s=1/8`. The
rewrite graph is taken verbatim from `tests/unit/attention_test.cpp`: two `einsum` contractions
(`qdt,kdt->qkt` folds in the Kᵀ transpose; `qkt,kdt->qdt`) around the keepdims max/exp/sum softmax over
the key axis. Only the Heinzel arm ported (the textbook `hand` arm is not the rewrite path). Baseline =
old `attn/hz_simd/<N>x64`, the arm `run-benchmarks.py hz_baseline()` prefers. Correctness-gated against
an independent textbook double reference (atol 1e-4). Median of 7 reps; median and best agree.

| workload (N×D) | old median (ns) | new median (ns) | ratio | verdict |
|---|--:|--:|--:|:--|
| attn/hz/32x64  |      5644.2 |    82616.8 | **14.6×** | FAIL |
| attn/hz/64x64  |     21790.4 |   433849.8 | **19.9×** | FAIL |
| attn/hz/128x64 |     68985.6 |  1999885.7 | **29.0×** | FAIL |
| attn/hz/256x64 |    282159.3 |  7690102.3 | **27.3×** | FAIL |
| attn/hz/512x64 |   1114719.6 | 30966451.4 | **27.8×** | FAIL |

**Attribution:** the dominant cost is the two `N·N·D` contractions, so this inherits the same
contraction gap as `contraction_bench` — the old hz arm is a register-blocked SIMD matmul microkernel;
the rewrite's generated run does an untiled, scalar, double contraction per sample plus a scalar
softmax pre-pass. Ratio climbs from 14.6× (N=32, small enough to sit in L1) toward ~28× as the N×N
scores buffer grows, matching the cache-blind-vs-tiled story of the bare matmul. Recorded, not tuned.
**Deferred (expand later):** the old tree's simd-only large-N driver points (N=2048: 25.5 ms;
N=4096: 102.4 ms, old-arm figures) — on the rewrite's scalar path they cost ~seconds/iteration and add
nothing to the verdict the 32..512 sweep already establishes.

### oscillator_bench → benchmarks/bench_oscillator.cpp

Old `benchmarks/oscillator_bench.cpp` (`heinzel_oscillator_bench`). Workload: a sine audio oscillator
over N = 1<<22 (~4M) samples at 440 Hz / 48 kHz. **Only the FM/recurrence arm is ported** —
`phase[n] = frac(phase[n-1] + inc); out = sinpi(2·phase)·amp` (rewrite `dsp::oscillator`, graph pattern
from `tests/parity/oscillator_here.cpp` + the runtime-length recurrence of
`tests/unit/runtime_recurrence_test.cpp`). Baseline = old `osc_sine/fm/hz/simd`. Correctness-gated
against a hand double phase accumulator (atol 1e-4). Median of 7 reps; median and best agree.

| workload | old median (ns) | new median (ns) | ratio | verdict |
|---|--:|--:|--:|:--|
| osc_sine/fm/hz (vs old simd)   | 11784735.8 | 1322073417.0 | **112.2×** | FAIL |
| osc_sine/fm/hz (vs old scalar) | 24116228.3 | 1322073417.0 |    54.8×   | (context) |

**Path note (important for fair reading):** unlike the elementwise/contraction/attention benches — which
time the rewrite's **generated** run (`emit_wide`+`run_generated`) — this workload runs on the rewrite's
**scheduled `execute()`** path. That is *forced*, not a choice: a 4M-sample recurrence cannot use the
generated path here. The generated path emits a recurrence only for a **static** block length
(`biquad_generated.cpp` uses `emit` + a compile-time `kBlock`, stack `WorkspaceForProgram`); N = 1<<22
as a static block blows the constexpr plane budget, and the rewrite has **no cross-block streaming
driver** (no `set_streaming`/`reset`, established at ch04) to carry recurrence state across smaller
generated blocks. So the scheduled interpreted-walk `execute()` over a runtime-resolved axis
(`runtime_recurrence_test` pattern) is the ONLY path that runs this workload at scale. The 112× vs the
old compiled SIMD arm therefore conflates the milestone architectural gap with a scheduled-walk-vs-codegen
gap; even vs the old **scalar** arm it is 55×. Recorded, not tuned.

**Affine arm DEFERRED (finding):** the old bench's headline `osc_sine/affine` arm (loop-invariant pitch,
`phase = frac(iota·inc)`, vectorized) has no runnable equivalent at N = 1<<22. In the rewrite
`dsp::affine_oscillator(shape, inc, amp)` is a width-materialized `notation::iota(shape)` generator (see
`tests/parity/oscillator_affine_here.cpp`: "the N-sample buffer is the no-time image x{N,1}"), so N is a
**compile-time width** bounded by the plane budget (the affine parity test sits at N=96). 4M cannot be a
compile-time width. Listed as deferred; the FM arm above is the genuine recurrence workload.

### order_stats_bench → benchmarks/bench_order_stats.cpp

Old `benchmarks/order_stats_bench.cpp` (`heinzel_order_stats_bench`). Workload: a sliding median filter
over an N=1<<20 signal at window W ∈ {3,5,9}. Rewrite graph verbatim from
`tests/parity/order_stats_here.cpp`: `notation::unfold(x, axis=0, W, hop=1)` → {N-W+1, W, 1}, then an
`op::Order` Quantile q=0.5 over the window axis (bit-exact middle-element selection at odd W). Timed on
the generated run path (the large (N-W+1)×W unfold buffer lives on the heap RuntimeWorkspace — the
compile-time plane budget that caps the *interpreted* `run()` path does not bind the generated path,
confirmed by building at N=1<<20). Baseline = old `median_filter/W<k>/hz_simd`. Correctness-gated
against `std::nth_element` per window (atol ~0). Median of 7 reps; median and best agree.

| workload | old median (ns) | new median (ns) | ratio | verdict |
|---|--:|--:|--:|:--|
| median_filter/W3/hz | 113064.3 |  30254705.4 | **267.6×** | FAIL |
| median_filter/W5/hz | 190560.0 |  48844341.3 | **256.3×** | FAIL |
| median_filter/W9/hz | 616898.0 | 121546460.7 | **197.0×** | FAIL |

**Attribution:** the old hz arm is a pruned Batcher/Paeth **branchless selection network**, lane-parallel
and SIMD-vectorized (compare-exchange chains, no data-dependent branches); the rewrite runs a scalar
`Order` Quantile per window streamed one output frame at a time through the heap workspace. This is the
same width-1-stream no-across-buffer-SIMD story as core/channel_strip, amplified by the per-window
selection cost. Recorded, not tuned.

### fusion_bench → benchmarks/bench_fusion.cpp

Old `benchmarks/fusion_bench.cpp` (`heinzel_fusion_bench`). Two workloads: a 1-D conv `p=x*x;
y[i]=Σ_k p[i+k]·h[k]` and a vertical blur `p=x*x; y[i,j]=Σ_k p[i+k,j]·w[k]`. Graphs verbatim from
`tests/parity/fusion_here.cpp` and `tests/parity/vblur_here.cpp` (unfold the data/row axis, weight,
reduce Sum over the window axis). Generated run path. Baseline = old `fused` arm (headline Heinzel
`compute_at` path); the old materialized-Heinzel `hz_mat` arm is shown for context. Correctness-gated
against a hand stencil (atol 1e-9). Median of 7 reps; median and best agree.

**Fuse-vs-materialize arms DEFERRED (finding):** the old bench's whole point is a *scheduling* sweep —
the roofline model deciding to fold the `x*x` producer into the stencil vs materialize it to RAM
(`TargetInfo.compute_rate/mem_bandwidth/l3_bandwidth`, an uncalibrated target pinning "always fuse").
The rewrite `Target={share_storage,Machine}` has no roofline and its `Schedule` has no fuse↔materialize
decision, so there is nothing to sweep; one plain graph is compared hz-vs-hz.

| workload | old fused (ns) | old hz_mat (ns) | new (ns) | ratio vs fused | verdict |
|---|--:|--:|--:|--:|:--|
| conv/N65536_K16   |   29042.3 |   32245.7 |  11448963.2 | **394.2×** | FAIL |
| conv/N262144_K16  |  118179.8 |  128480.5 |  48540458.7 | **410.7×** | FAIL |
| conv/N1048576_K16 |  470145.2 |  568295.6 | 196876151.0 | **418.8×** | FAIL |
| conv/N1048576_K32 |  995040.1 | 1025953.3 | 390052217.0 | **392.0×** | FAIL |
| blur/H16384_W256  | 2441859.8 | 4045900.0 | 225911789.0 |  **92.5×** | FAIL |
| blur/H8192_W512   | 2477709.0 | 4090293.1 | 221513335.5 |  **89.4×** | FAIL |
| blur/H4096_W1024  | 2594852.1 | 4091936.2 | 216476217.5 |  **83.4×** | FAIL |

**Attribution:** the old fused arm keeps the squared producer cache-resident per consumer tile and runs
a SIMD stencil kernel; the rewrite generates a scalar path that materializes the `unfold` window buffer
and reduces it scalar, no fusion and no across-buffer SIMD. conv's ~400× dwarfs blur's ~90× because
conv's per-output work (16–32 taps, width-1 stream) is tiny so the per-element scalar overhead
dominates, whereas blur amortizes the same overhead across the 256–1024-wide rows. Recorded, not tuned.

### lane_recurrence_bench → benchmarks/bench_lane_recurrence.cpp

Old `benchmarks/lane_recurrence_bench.cpp` (`heinzel_lane_recurrence_bench`). Workload: a bank of C=16
independent one-pole voices, `y[c][n] = x[c][n] + a·y[c][n-1]`, N=1<<16 samples/voice, a=0.95. The C
voices are the WIDTH axis; the recurrence runs along the runtime-resolved streamed axis (the
`runtime_recurrence_test` pattern widened from mono to width-C, `y.previous(1)` tapping each lane's prior
sample). Runs on the SCHEDULED `execute()` path (same forced choice as oscillator: an N=1<<16 recurrence
exceeds the generated path's static-block limit and there is no cross-block streaming driver). Baseline =
old `onepole_bank/hz_calibrated` (realistic deployment target); the pinned laned/relayout/strided arms
are shown for context. Correctness-gated against the per-voice scalar recurrence. Median of 7 reps.

**Positive finding:** the width-C recurrence — flagged UNPROVEN in the plan (all rewrite recurrence
tests are rank-1 mono) — **builds and verifies** here at C=16: the per-voice correctness gate passes, so
a rank-2 voice bank IS expressible and correct on the rewrite's runtime-recurrence path. It is only
slow, not absent.

| workload | old (ns) | new (ns) | ratio | verdict |
|---|--:|--:|--:|:--|
| onepole_bank/hz (vs old calibrated) | 1585568.6 | 121375030.3 | **76.6×** | FAIL |
| onepole_bank/hz (vs old relayout)   | 1635328.8 | 121375030.3 |    74.2×   | (context) |
| onepole_bank/hz (vs old strided)    | 4953837.4 | 121375030.3 |    24.5×   | (context) |

**Attribution:** the old tree lanes the 16 independent voices across SIMD registers and advances
independent lane blocks to fill the serial-feedback latency (ILP), optionally relaying out to a
contiguous [time][ch] layout; the rewrite runs a scheduled scalar per-lane recurrence — no laning, no
ILP, no relayout, and the scheduled interpreted-walk rather than codegen. Even against the old *strided*
(relayout-disabled) arm it is 24.5×. Recorded, not tuned. **Deferred (expand later):** the heavy 5-state
**ladder bank** arm, and the laned-ILP / relayout / calibrated SCHEDULING arms (rewrite `Target` has no
roofline lane-vs-serial gate or `disable_relayout` to pin).

### apps_bench (tier-2 subset) → benchmarks/bench_apps.cpp

Old `benchmarks/apps_bench.cpp` (`heinzel_apps_bench`). The pruned tier-2 subset — kernels with distinct
coverage beyond the eight primary groups (redundant ones — gemm/softmax/box/sharpen/biquad/cascade —
not ported; `logmel` deferred, needs the absent `dsp::logmel`). Spellings from
`tests/parity/rmsnorm_here.cpp` and `tests/parity/mlp_forward_here.cpp`. Generated run path. Baseline =
each kernel's old `hz_simd` arm. Correctness-gated against hand references. Median of 7 reps; median and
best agree.

| workload | old median (ns) | new median (ns) | ratio | verdict |
|---|--:|--:|--:|:--|
| img/brightness (clamp(1.2x+.05,0,1), 512²)     |  28303.2 |   2888876.4 | **102.1×** | FAIL |
| ml/rownorm (x−rowmean, 1024×256)               | 177102.1 |   1299185.1 |   **7.3×** | FAIL |
| ml/rmsnorm (x/√(mean(x²)+ε), 1024×256)         | 285745.2 |   1750158.1 |   **6.1×** | FAIL |
| ml/linear_relu (relu(x·W+b), 128×512·512×512)  | 606156.8 | 176056445.5 | **290.5×** | FAIL |
| ml/mlp2 (2-layer MLP, 128×512→256→512)         | 599218.6 | 114922319.0 | **191.8×** | FAIL |

**Attribution:** the matmul-epilogue kernels (linear_relu 290×, mlp2 192×) inherit the untiled-scalar
contraction gap (contraction_bench); the elementwise brightness (102×) inherits the width-1-stream
no-SIMD gap (core elem); the row-reduce norms (rownorm 7.3×, rmsnorm 6.1×) inherit the modest
reduce-over-width gap (core rowsum, 1.4–1.8×) plus the broadcast/sqrt epilogue. Recorded, not tuned.

## Deferred (not portable to the rewrite's M2–M9 surface — findings, not faked)

From `plan/01-classification.md` (evidence-anchored). These have no runnable rewrite equivalent yet:

| bench | reason |
|---|---|
| cost_model_flip_bench | roofline fuse↔materialize flip; rewrite `Target` has no roofline, `Schedule` no fuse/materialize |
| dsp_fft_bench, fft_graph_bench | FFT/spectral reserved to M13; the graph `_fft` op refuses at execute |
| image_pipeline_bench | segment/tile/parallel cost-model decisions absent from the rewrite |
| mai_verb_bench | `dsp::reverb`/`dsp::allpass` helpers absent (rewrite `dsp/` = oscillator.hpp only) |
| apps_bench `logmel` | `dsp::logmel`/`mel_filterbank` absent |
| materialize_break_bench | REF-ONLY: old tree builds no heinzel arm; nothing to re-express |
| juce/klang/kfr/faust/accel `_compare_bench` | REF-ONLY: external headers `#include`d unconditionally, libs fetch from network / Apple-only |

## Porting checklist (measured vs pending)

- [x] core_bench (elem, rowsum) — MEASURED, all FAIL
- [x] contraction_bench (matmul) — MEASURED, all FAIL
- [x] attention_bench (softmax attention) — MEASURED, all FAIL (14.6–29.0×; large-N 2048/4096 deferred)
- [x] oscillator_bench — FM/recurrence MEASURED, FAIL (112× vs simd; scheduled-path forced); affine arm DEFERRED (plane-budget)
- [x] channel_strip_bench (tanh chain) — MEASURED, FAIL (25.9×)
- [x] order_stats_bench (sliding median) — MEASURED, all FAIL (197–268×)
- [x] fusion_bench (conv + vertical blur) — MEASURED, all FAIL (conv 392–419×, blur 83–93×; fuse-vs-materialize arms deferred)
- [x] lane_recurrence_bench (one-pole C=16 bank — bank VERIFIED in rewrite) — MEASURED, FAIL (76.6× vs calibrated); ladder + scheduling arms deferred
- [x] tier-2 apps (rmsnorm, rownorm, brightness, linear_relu, mlp2) — MEASURED, all FAIL (6.1–290.5×)
