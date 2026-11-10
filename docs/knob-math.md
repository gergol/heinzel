# Knob math

*Knob math* is the work that turns a plugin's knobs into the numbers its filters use. An RBJ peak, for example, works
out its five coefficients from three Params, `freq`, `gain` and `q` (two `sinpi`, an `exp`, seven divisions). A *Param*
is a named input of one number, set with `param("freq") = …` between runs. A *run* is one call of `run()`: one block.

## What it does

Knob math that reads nothing but Params and constants runs only in a run where one of its Params changed; in between,
its results stay in the workspace, the memory a program keeps from run to run. While the knobs rest, a plugin pays a
cheap check instead of the math.

## When it fires

Once the stages are final, the planner looks at each *stage* (one loop nest of the program) in order
(`keep_what_pays`, `include/heinzel/loops/planner/kept.hpp`). It marks a stage *kept* when both of these hold:

1. **It may be skipped.** It has no loop and stands in no band (a band runs several stages chunk by chunk). Every node
   in it is *work ahead of the loops*: a value of no axis made from Params and constants alone
   (`graph::ahead_of_the_loops`). Every input it reads is a Param: a declared tensor is read where it lies and may
   change with no call, so a stage that reads one runs every run. Every value it reads was made in the stage or by an
   earlier stage that may be skipped. It makes no output of the graph, since an output may live in the caller's memory,
   which a skipped stage would leave unwritten. It *watches* the Params it reads and those its makers watch.
2. **Skipping it pays.** The cost model (`include/heinzel/schedule/cost.hpp`) prices the stage above checking the
   Params it watches: `cost::once` (the stage's path and issue, priced as one sample of a loop) must exceed
   `cost::check` (two loads a watched Param and one branch). There is no margin.

Before a kept stage the runner asks one question (`Workspace::changed`, `include/heinzel/loops/workspace.hpp`): did a
watched Param change, bit for bit, since the stage last ran? Bits, not values: −0.0 against 0.0, and a NaN's payload,
count as changes. Each Param is loaded and compared with no branch of its own. The bits and a "has run" flag are stored
only when the stage runs, so a stage at rest stores nothing. A workspace just made (the first run, the first after
`prepare`) runs every kept stage; `reset()` keeps their results, which depend on Params alone.

Two more pieces came with knob math. They help every graph, and the switch below leaves them on:
- **The work ahead runs first.** It is gathered before the first stage with a loop, each piece (one band's knob math) a
  stage of its own (`find_the_work_ahead`, `next_unit` in `include/heinzel/loops/planner/stages.hpp`). Before, a band's
  knob stage stood between two filter sections, and the planner could not chain their feedback loops.
- **The frame copy.** A read that no loop of its stage moves, of a workspace buffer the stage does not store, is copied
  into the frame that places the stage (`still_slots`, `include/heinzel/loops/walk.hpp`), so the compiler loads it once
  instead of every sample. With it, the carry's one copy: after a run at least as long as a feed's tail, that tail is
  kept in one copy of a fixed size (`Workspace::carry_piece`), not two library calls.

## Switch

A macro, `HEINZEL_KNOB_MATH`, default 1, defined beside its one use in `include/heinzel/loops/planner/plan.hpp`. At 0
the planner keeps no stage (an `if constexpr` skips the call `planner::keep_what_pays`, so both paths always compile),
and knob math runs every run, as before the feature. The CMake option of the same name (ON by default), OFF, puts
`HEINZEL_KNOB_MATH=0` on the `heinzel` target and leaves the feature's own groups unbuilt and unrun
(`HEINZEL_KNOB_MATH_GROUPS` in `tests/CMakeLists.txt`: `loops_kept`, `knob_stages`, `knob_price`). Like
`HEINZEL_MAX_IO`, every file of one program must agree on it.

The test `switches_off`, built in every build with both switches at 0, holds that an RBJ peak from three
knobs (`knobs1/heinzel`) keeps no stage and runs within its bound of the walk. With both switches off the rest of the
suite passes (374 tests, 2026-10-26). Compiling `bench_knobs` costs the same with knob math off: 46.6 s and 583 MB with
both switches at 0, 46.5 s and 584 MB with the lanes alone off (`docs/sections-in-lanes.md`).

Off is not exactly "before knob math": the work ahead still runs first, and the frame copy and the carry's one copy
stay, for every graph.

## Where it lives

The removal list. Sections in lanes widen the frame copy (`docs/sections-in-lanes.md`): revert them first.

**Whole files it added:** `include/heinzel/loops/planner/kept.hpp` (which stages are kept, what each watches),
`tests/support/well_formed_kept.hpp` (what a kept stage must hold), `tests/gates/loops_alone/planner_kept.cpp`.

**Parts of shared files:**
- `include/heinzel/schedule/cost.hpp`: `once`, `check`, `kept`.
- `include/heinzel/loops/schedule.hpp`, `program.hpp`: `Stage::kept`, `Stage::watched` (a bit per input, `kWatchWords`).
- `include/heinzel/loops/planner/plan.hpp`: the call of `keep_what_pays`, and each stage's counts kept for it
  (`OpenGroup::counts` in `gathering.hpp`).
- `include/heinzel/loops/lower.hpp`: `copy_kept`, `params_alone` (a kept stage checked against the program).
- `include/heinzel/loops/workspace.hpp`: the last bits (`LastBits`, `NoneKept`, `kKeptOf`, `kWatchedOf`,
  `kFeedBytesOf`), `changed`, `bits_of`; the carry's one copy in `carry_piece`.
- `include/heinzel/loops/run.hpp`: the check before a kept stage, under `if constexpr`.
- `include/heinzel/loops/print.hpp`, `app/heinzel/print.hpp`: `kept: freq gain q` in the listings.
- The work ahead first: `loops/graph/facts.hpp` (`timeless_of`, moved there from `rewrite/feedback_sums.hpp`;
  `ahead_of_the_loops`), `loops/planner/stages.hpp` (`find_the_work_ahead`, `opened_over`, `joins`, `next_unit`),
  `gathering.hpp` (`Gathering::ahead`), `chains.hpp`, `in_the_reader.hpp` (`readers_of`).
- The frame copy: `loops/walk.hpp` (`still_slots`, `still_cell`, `StillHeld`, `place`), `run_stage.hpp`, `run_band.hpp`.

**Tests:** `tests/unit/loops_kept_test.cpp` (the oracle: each kept program against the same program with every `kept`
cleared, bit for bit, and at `High` against the reference walk), `knob_stages_test.cpp` (the work ahead first; which
stages are kept), `knob_price_test.cpp` (the price on the probe's corpus); kept, work-ahead and frame-copy cases in
`loops_walks_test`, `loops_primitives_test`, `loops_ir_test`, `loops_runner_test`, `nest_band_plan_test` and
`nest_feedback_chain_test`; the include in `tests/support/well_formed.hpp`; their lines in `tests/CMakeLists.txt`.

**Benchmarks:** `benchmarks/bench_knobs.cpp`: `knobs1` (one RBJ peak), `knobs2`, `knobs8` (eight bands, three knobs a
band), with `-moving` (knobs changed before every block), `-coefficients` (the same filter, its coefficients set as
Params) and `hand` arms, at blocks of 32, 64 and 256.

## What it gains and costs

zen2, core 4, ns a sample, old and new binaries interleaved, B32 = blocks of 32 samples:

| what | before | after |
|---|---|---|
| `knobs1/heinzel` B32 (one peak, knobs at rest): kept stages | 2.67 | 1.76 |
| the same: the carry's one copy, then the frame copy | 1.73 | 1.60, then 1.47 (its `-coefficients` arm: 1.46) |
| `knobs8/heinzel` B32 (eight bands at rest): the work ahead first | 26.4 | 12.1 (B256: 20.3 → 8.5) |
| the same: kept stages | 12.1 | 9.4 |
| the same: the frame copy | 9.31 | 9.05 |
| the same: the check storing nothing at rest | 8.46 | 8.31 |
| `knobs1/heinzel-moving` B32 (its knob moving every block) | 2.71 | 2.70 |
| `knobs8/heinzel-moving` B32 (all eight bands moving every block) | 12.20 | 14.08 (+15 %; B64 +10 %, B256 +3 %) |
| `additive/heinzel-params` (crv): the bench's lambda no longer inlined | 28.9 | 29.6 (+2.4 %; level when pinned) |
| compile time and memory | | not measured as a whole; a schedule and a program grow 16 B a stage |
| a workspace | | 8 B a watched Param and a flag a kept stage (`knobs1` +32 B, `knobs8` +200 B); nothing without one |
| lines | | library +605, tests +1,840, benchmarks +386 |

## Limits and stand-ins

- **Debug builds keep nothing.** A checked build makes the RBJ peak's unproven promise `a0 > 0` an output of its knob
  stage, so the stage may not be skipped: a debug build works the knob math out every run.
- **The stage's price is a stand-in.** `cost::once` misses what skipping saves by 0.8–6.4× on zen2; the decision is
  judged instead (the probe's 64 cases above the noise: 63 right). Backlog: "The price of a stage with no loop".
- **The check's price is not the check.** On `Executor`, which copies the Params into its frame, the check costs
  0.1–0.6 cycles a watched Param, priced at 1; through `loops::run`, which reads them through the caller's pointers,
  0.7–1.7 times the price. Kept as is: a refit would move the decisions.
- **Nothing pins that a stage at rest stores nothing:** no `measure.sh` row and no gated bench row has a kept stage.
- **All bands moving cost more:** eight bands moving every block run 15 % slower at B32, since the branch before each
  kept stage stops clang computing the eight bands' math side by side. Backlog: "Work ahead of the loops, a stage and a
  check a piece".
- **A cheap stage is kept too:** a Param's negation is priced above its check though skipping it saves nothing; twelve
  of them ran 3–5 % slower. Same backlog item.
- **Each piece is a stage:** many separate Param chains can pass `kMaxStages` (16; four chains of four two-pole sections
  need 18), and the graph then runs as written, its rewrites lost without a word. Same backlog item.
- **One output stops the skip:** an output anywhere in a band's knob math (a coefficient shown to a UI) makes the whole
  band run every run.
- **The floating-point environment:** the same bits give the same results only while flush-to-zero, denormals-are-zero
  and the rounding mode stay the same from run to run.
- **`param()` during `run()`** on another thread is a data race (`docs/limitations.md`, "Params and a run on another
  thread").
- **Capacities:** an 8-band EQ with three knobs a band needs `-DHEINZEL_MAX_VALUES=512 -DHEINZEL_MAX_NODES=384
  -DHEINZEL_MAX_STMTS=384`; as defaults they would cost every compile up to +26 % time and +39 % memory. Inputs (32) and
  buffers (96) were raised for it, at no measurable cost. Backlog: "Compile time follows the capacities".

## What the probes found

- **The skip** (blocks of 32): one RBJ peak 1.42× faster (69 → 49 ns a run), eight bands 1.54× (827 → 538 ns); one
  negation saves nothing. The check: at most 0.6 ns a run up to five Params a stage.
- **Knob math split the cascade:** with every knob stage skipped, the 8-band knob EQ still ran 19.1 ns a sample, against
  9.4 for the same cascade with its coefficients as Params: each band's knob stage stood between two sections. Hence
  the work ahead first.
- **The price** (41 cases, blocks of 32 and 256): how much of a stage the run around it hides varies (one `exp` saves 6
  cycles before a one-pole at B32, 49 before a map at B256). No form of price came within 30 % (at best 22 of 63
  cases), but the kept-or-not decision is rarely close, so it is judged: 83 of 84 decided as measured.
- **One peak's last 9.5 ns a block:** the carry's two library calls, and the coefficients reloaded every sample (the
  output's float store may alias them). Each fix alone takes part of one stall a block; the two together close it.
- **Cheap pieces sharing one stage, dropped:** four Param chains ran 14 % slower at rest (clang spilled two states), and
  an adversary 2.3× slower moving (one Param reran 24 squares). It won only with twelve negations or more, a rare shape.
- **The check's cost:** the library's check seemed to cost twice the probe's hand check, but the hand check kept its
  bits in registers, which no check across runs can. The real cost was storing the bits and the flag every run. Stored
  only when the stage runs, the check costs 0.1–0.6 cycles a watched Param on `Executor`, where it cost 0.6–2.1.

No probe was run on the M1 (Apple). The probe sources stay in the git history, on the branch `plan-32-history`.
