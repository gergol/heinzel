# Sections in lanes

Words used. A *section* is one feedback cycle of a cascade, for example one biquad of an EQ. A *lane* is one slot of
a SIMD register. A *stage* is one loop nest of the program. *Skewed*: while lane 0 is at sample n, lane j is at n − j.

## What it does

A cascade of like filter sections, such as an EQ whose eight bands' coefficients are Params, runs its k sections side by
side in SIMD lanes, section j one sample behind section j − 1, so no section waits for the one before within a sample.
Each section computes what it computed in the cascade, in its own order, so the numbers are the cascade's.

## When it fires

The rewrite `sections_in_lanes` (`include/heinzel/rewrite/sections_in_lanes.hpp`; `like_sections.hpp` finds the runs)
takes, after the parallel form:

> At a precision that grants reordering, on a machine with lanes, a maximal run of k ≥ 2 like feedback cycles, each
> reading the one before at the present sample, reading from outside only timeless values, the first reading one
> outside value of one voice, read outside only at its end, in no larger cycle, its past held in registers, and within
> `fits_a_block(k, the machine's double lanes)`, becomes one cycle over a section axis, linked.

- *Like*: two sections' nodes pair one for one, each pair one operation, attribute, type, shape and precision.
  *Timeless*: a constant, a Param, or a value worked out from them (a coefficient computed from knobs).
- *Its past held in registers*: each value a section reads back is read as one run of lags 1, 2, …, d with no gap, and a
  lag of a lag only of the cycle's own state (`y1 = y[n-1]`, then `y1[n-1]`); the lowering holds no other shape.
- *Linked*: by `_chain`, an internal operation users never write (code 60). In `u = _chain(x, V)`, element 0 of `u` is
  the cascade's input `x`, element j is section j − 1's value `V`, both at this sample. The skew is how a stage runs the
  graph, not part of the graph, so the graph keeps the cascade's exact meaning.
- *The price P*: the merge is kept only where the cost model prices the graph's feedback stages, after the rules that
  follow (the sums' order and double state, `sums_and_state`), below the cascade's by more than `kTie`, 3 %:
  `feedback_cycles(lanes) · 1.03 < feedback_cycles(cascade)`. At a tie the cascade stays. A graph's runs are priced
  together, taken or kept as one.
- *Coefficients*: one value every section reads stays as it is; constants become one `[k, 1]` constant tensor; anything
  else (Params, knob math) is gathered each run by a chain of `where` over one-hot constants, in a stage of maps of its
  own before the loops.

The planner runs a stage holding a link *skewed*: the sections along a vector of the machine's lanes, time innermost,
nothing in it but the cycle and what leads to the last section's value; no band holds it, no worker splits it. A block
of B samples takes B + k − 1 steps: head steps until every lane is real, plain steps, and k − 1 drain steps. In the head
and the drain, a lane whose sample lies outside the block keeps its state by a blend. So each run computes exactly its
own block, and a coefficient changed between runs applies exactly as in the cascade.

## Switch

A macro, `HEINZEL_LANES`, default 1, defined beside its one use in `include/heinzel/rewrite/sections_in_lanes.hpp`. At
0 `rewrite::lanes_sums_and_state` returns `rewrite::sums_and_state(g, m)` at once (an `if constexpr`, so both paths
always compile): no link is ever made, and the skewed code in the planner, the lowering and the runner is never reached.
The CMake option of the same name (ON by default), OFF, puts `HEINZEL_LANES=0` on the `heinzel` target and leaves the
feature's own groups unbuilt and unrun (`HEINZEL_LANES_GROUPS` in `tests/CMakeLists.txt`: the eleven test files below,
and `knob_stages`, whose knob cascade is one linked cycle from three bands on). Like `HEINZEL_MAX_IO`, every file of one
program must agree on it.

The test `switches_off`, built in every build with both switches at 0, holds that eight Param RBJ biquads
(`biquad8/heinzel-param`) take no skewed stage and run within `param_arms`' bound of the walk. With both switches off
the rest of the suite passes (374 tests, 2026-10-26), W12's zero-overhead rows among them, run as a cascade.

Compiling `bench_knobs` (`rw-bench`'s command; wall, max RSS): 84.5 s and 795 MB as is, 46.5 s and 584 MB with
`HEINZEL_LANES=0`, 46.6 s and 583 MB with both switches at 0.

Off is not exactly "before the lanes": what they changed in the cost model (work leading into a cycle priced where the
planner runs it; cycles chained in the price as the planner chains them; a cycle of another shape staged apart) and in
the feedback sums' order (a sum whose first ready term is subtracted) stays on for every graph. Only the order moved a
program: `linkwitz/heinzel` +5.7 %, `measure.sh`'s W9 1 % fewer instructions.

The lanes build on knob math's frame copy (`docs/knob-math.md`; `still_slots` in `include/heinzel/loops/walk.hpp`, which
the lanes widen to a skewed stage's k sections), so reverting knob math needs the lanes reverted first, or switched off.

## Where it lives

**Whole files it added:** `include/heinzel/rewrite/sections_in_lanes.hpp` (the merge, its price,
`lanes_sums_and_state`), `rewrite/like_sections.hpp` (which runs are like), `tests/reference/link_element.hpp` (the
reference walk of a link, in passes), `tests/support/linked_sections.hpp`, `same_graph.hpp`, `skewed_sections.hpp`,
`well_formed_skew.hpp`.

**Parts of shared files:**
- The operation: `ir/vocab.hpp` (`op::Chain`), `ir/rule_columns.hpp` (`TimeRule::ReadsTheElementBefore`),
  `ir/op_rules.hpp` (the `_chain` row), `ir/timing.hpp`, `loops/graph/kinds.hpp` (`is_a_link`).
- The facts and the price: `loops/graph/feedback.hpp` (a link closes its cycle: `cycle_closes_at`, `find_feedback`),
  `loops/graph/cycle_chains.hpp` (a link a lag of one step, `read_now`; `leads_into`, `staged`, `stages_of`).
- The planner: `planner/gathering.hpp` (`runs_skewed`), `feedback.hpp` (the voice layout skewed, `sections_leave`),
  `trail.hpp` (`find_the_slices`), `chains.hpp` (`state_depth`), `stages.hpp`, `parallel.hpp`, `band_layout.hpp`
  (`why_not_in_any_band`), `schedule.hpp` (`Schedule::Stage::skewed`).
- The lowering: `lower.hpp` (`copy_skewed`), `lower_stmts.hpp` and `lower_form.hpp` (`keep_what_links_read`,
  `views_one_section`), `implied.hpp` (`find_state`, `Implied::lane`), `normalize.hpp`, `validate.hpp`.
- The program: `program.hpp` (`Stage::skewed`, `Node::shifted`, `Node::lane`), `print.hpp`.
- The runner: `run_nodes.hpp` (`run_skewed`, `run_registers`, bodies last first), `walk.hpp` (`Skewed<Inner, Steps>`,
  `kAtAnEdge`, `lane_slots`, the coefficients' copy: `copied_of`, `still_cells`), `registers.hpp` (`shifted_in`,
  `linked`, the blends), `values.hpp` (`lane_real`, `at_lane`, `made_at_lane`, `stored_at_lane`).
- The rewrites: `rewritten.hpp` (calls `lanes_sums_and_state`), `double_state.hpp` (a cycle holding a link one voice),
  `feedback_sums.hpp` (`stands_upright`: stays for every graph); `rebuilt.hpp` only holds `detail::with`, moved.

**Tests:** `tests/unit/linked_sections_test.cpp`, `linked_price_test.cpp`, `lanes_price_test.cpp`,
`rewrite_sections_in_lanes_test.cpp` (with `sections_in_lanes_graphs.hpp`), `loops_skewed_test.cpp`,
`loops_plan_skewed_test.cpp`, `loops_lower_skewed_test.cpp`, `executor_skewed_test.cpp`, `lanes_past_held_test.cpp`,
`param_arms_test.cpp` (with `param_arms_graphs.hpp`), `param_moving_test.cpp`; the golden rows `linked_lr8` and
`linked_params_8` of `tests/goldens/loops_plan` and `loops_lower`; linked cases in `tests/reference/interpret.hpp`,
`compute_reference.hpp`, `tests/support/both_ways.hpp`, `well_formed.hpp`, `well_formed_walks.hpp` and the unit tests
`adjoint_class`, `both_ways`, `knob_stages`, `loops_walks`, `meaning`, `merge_bound`, `native_f32_is_exact`,
`op_rules`, `rewrite_double_state`, `rewrite_feedback_sums`, `timing`; `measure.sh`'s W12 (`tests/zero_overhead/`).

**Benchmarks:** `biquad8/heinzel-param`, `-moving`, `biquad8/naive-moving` and `lr8/{heinzel,naive,kfr}`
(`bench_compare_kfr_heinzel_eq8.cpp`, `bench_compare_kfr_eq8.cpp`, `eq8_common.hpp`, `eq8_graph.hpp`, `iir_arms.hpp`,
the IO override in `benchmarks/CMakeLists.txt`). They run without the lanes, slower.

## What it gains and costs

zen2, core 4, ns a sample, three interleaved rounds of five; B256 = blocks of 256 samples, whole = one run of 2^20:

| what | before | after |
|---|---|---|
| `biquad8/heinzel-param` B256 (eight bands, 40 Params) | 9.61 | 3.33 (KFR 4.26) |
| the same, whole | 10.05 | 3.05 (KFR 3.12) |
| `biquad8/heinzel-param-moving` B256 (one band moving every block) | 9.49 | 3.31 (the hand loop 9.12) |
| its error (`max_err`; the linked state is double) | 3.7e-6 | 3.0e-8 |
| ÷ JUCE (8 `IIR::Filter` in a `ProcessorChain`, B256, run-time coefficients, double state: 29.50) | 0.33× | 0.112× |
| `knobs8/heinzel` B32 / B256 (eight bands from their knobs, at rest) | 8.23 / 7.42 | 5.29 / 3.37 |
| `lr8/heinzel` B256 / whole (constants) | 3.16 / 3.46 | 2.86 / 2.81 (KFR 4.24 / 3.13) |
| `biquad3dyn/heinzel-param/256` (three sections) | 2.83 | 2.88: no gain |
| three Param sections at blocks of 32 / 64; four at 32 | | +10 % / +7 %; +6 % |
| `linkwitz/heinzel` (LR4, not linked: the sums' order fix) | 4.80 | 5.07 (+5.7 %) |
| compile `bench_knobs` | 47 s, about 580 MB | 85 s, about 800 MB |
| lines | | library +2,050 (−290), tests +6,450 and 380 golden lines, benchmarks +330 |

## Limits and stand-ins

- **`kTie`**: within 3 % the cascade stays; three sections tie, so they gain nothing.
- **`kEarlyMargin`** (10 %, `rewrite/feedback_sums.hpp`) stands in until zen2's issue model is recalibrated: LR4's
  second stage, priced 9.1 % cheaper early, is kept late, +5.7 %. Backlog: "Recalibrate zen2's issue model".
- **Short blocks pay the edges:** the k − 1 drain steps and the gather cost a run, the price is a sample's, so neither
  enters it. Three sections run 7–10 % slower at blocks of 32 and 64; a run of one sample pays k steps.
- **A skewed step costs about 11.6 cycles on zen2 whatever its sections:** two runs of four Param one-poles with a gain
  between are taken in lanes, 1.67–1.94× slower. Backlog: "Fit a skewed step's cost on zen2".
- **Stereo 3 + 3** (two channels of three sections) is priced as two stages, run as one: lanes, 1.09–1.11× slower.
- **A lag of a lag** of a value outside the cycle is declined, so the sections stay a cascade (eight: 4.8 ns a sample,
  2.95 written as `s[n-2]`). Backlog: "Fold a lag of a lag".
- **The cascade is priced up to 1.5× what runs** where zen2's issue bound decides it: two sections behind `x * g + 0.25`
  are taken in lanes, 1.45× slower. Rare shapes like these are picked wrong.
- **Stay a cascade:** several voices; a past with a gap (a band-pass); a slice's coefficients; `x * b` beside `b * x`.
- **A checked build cuts a run at a promised middle section:** release links {4}, checked {2, 2}.
- **Compile time:** a skewed stage inlines its step three times. Backlog: "Compile time of run sites".
- **W12 with 20 Params** misses both zero-overhead gates (+2,462 B). Backlog: "Params set by name cost bytes".

## What the probes found

- **The bank at run time against the lanes** (eight bands, B256): the parallel form's bank, worked out from the Params
  each run, ran 2.6 ns a sample at rest (the lanes 3.3), but 12.7 with a knob moving every block, 3.7× the lanes' 3.4:
  its numbers cost 2.5 µs a block. It breaks where two bands come close (0.14 % of random settings), and while a knob
  moves it departs from the cascade by −31 to −58 dB, up to +31 dB after a jump; the lanes are the cascade.
- **Where the lanes win** (Param cascades of K RBJ sections): the cascade at K = 2 (by 27–52 %), a tie at K = 3, the
  lanes from K = 4 (8–12 %), 2.1–3.2× at K = 8; LR4 stays a cascade (16–26 %), LR8 and `sat4` take the lanes (9–11 %,
  39–51 %). The gather costs 0–4 ns a run up to K = 6, 14–16 at K = 8 (B32), and flips no winner.
- **The model** picks every winner, K = 2 only by the tie; it prices the cascade 1.26–1.68× above what runs at K = 2–4.
- **The runner** at first read the coefficients from memory every step (the output's float store may alias them): K = 8
  11–18 % over the hand lanes. Read from a copy in the frame: 0.97–0.98 of them at B256.
- **The edge steps** out of line save 12 s of `bench_knobs`' 85 but cost 20–50 ns a run; one blended loop runs
  1.12–1.43× slower. Neither was built.

No probe was run on the M1 (Apple). The probe sources stay in the git history, on the branch `plan-32-history`.
