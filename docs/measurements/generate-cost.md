# What generating code costs, and what shape that cost has

**Question:** milestone M3.6. The tree gained a generated path, and standing rule S-6 asks for the
**shape** of a cost and not only its size — because a linear cost that is merely large may be paid
for by raising a dial, and a cost that grows faster than the graph may not be, ever.

**Two costs, measured separately**, because they are different shapes of work and only one of them
could plausibly go super-linear:

| stage | what runs | why it is its own column |
|---|---|---|
| **emit** | `emit(graph, schedule, machine)` — a constexpr function that walks the schedule once and appends a `Step` | if this stopped being linear, something is wrong with `emit()` itself |
| **expand** | `run_generated<Program, Block, Gen>` — one template instantiation per step, each carrying a `Step` as a non-type argument, inside a function whose own argument is the whole `Program` | a mangled name that grows with the program, once per step, is the ordinary way a compile-time cost becomes quadratic without anybody deciding it should |

Measured together, a quadratic term could have hidden inside a linear one for exactly as long as the
graphs stayed small.

## Result

**Flat. About 1.1 ms per filter section to emit a program and 1.5 ms per section to expand one, and
neither widens along the chain.**

Nine runs per point, minimum reported; `-fsyntax-only` with clang invoked directly, so no cache and
no code generation are in the number; one rung per translation unit so no rung hides in another's
shadow; rungs equally spaced so the marginal columns are comparable; **each subject done eight times
per compilation and the total divided by eight** — see finding S7-B below, which is the reason that
last clause exists. `tools/measure_generate_cost.sh`, clang 22.1.8, x86_64.

| sections | stage 0 (baseline) | stage 1 (+emit) | stage 2 (+expand) | emit per program | expand per program |
|---|---|---|---|---|---|
| 4 | 249 ms | 354 ms | 435 ms | 13.1 ms | 10.1 ms |
| 8 | 328 ms | 475 ms | 580 ms | 18.4 ms | 13.1 ms |
| 12 | 448 ms | 635 ms | 797 ms | 23.4 ms | 20.2 ms |
| 16 | 607 ms | 829 ms | 1038 ms | 27.8 ms | 26.1 ms |
| 20 | 810 ms | 1057 ms | 1282 ms | 30.9 ms | 28.1 ms |
| 24 | 1060 ms | 1344 ms | 1658 ms | 35.5 ms | 39.2 ms |

Marginal cost per section, interval by interval:

| interval | emit | expand |
|---|---|---|
| 4 → 8 | 1.32 | 0.75 |
| 8 → 12 | 1.25 | 1.77 |
| 12 → 16 | 1.10 | 1.48 |
| 16 → 20 | 0.77 | 0.50 |
| 20 → 24 | 1.15 | 2.78 |
| **overall** | **1.12** | **1.46** |

**The shape, stated in the terms S-6 asks for: the marginal cost per graph section is FLAT for
both.** For emitting the scatter is about ±0.3 ms with no trend and the last interval is smaller
than the first. For expanding the scatter is wider — ±1 ms about a slope of 1.46 — and the last
interval is the largest of the five, which is the one number here worth looking at twice. It is
noise rather than a trend: a least-squares line through the six points leaves residuals of
+1.3, −1.3, +0.2, +0.5, −3.1, +2.4 ms, which alternate rather than curve. The decisive check is the
end-to-end one that does not depend on any single interval: **six times the chain length costs 2.7×
to emit and 3.9× to expand.** A quadratic term would have made those thirty-six times.

Both also carry a fixed cost — about 8 ms to emit and 4 ms to expand — which is what a graph of any
size pays for the machinery before the first section.

**The structural half of the same claim, which does not depend on a clock**, is asserted in
`tests/compile_time/guard.cpp`: a section emits the same number of steps at the end of a long chain
as at the start of a short one, and the same number of prologue constants — seven and two, pinned to
those values so that neither assertion can pass by both sides being zero. If a later section ever
produced more steps than an earlier one, the emitted program would be growing faster than the graph
and no budget would fix that.

## Finding S7-B — the first reading of this ladder measured nothing, and the reason generalises

The first version compiled each stage **once** and reported the difference between two whole
compilations. That produced this:

| interval | emit | expand |
|---|---|---|
| 4 → 8 | 1.00 | 0.25 |
| 8 → 12 | **−4.25** | 4.75 |
| 12 → 16 | 1.50 | 0.75 |
| 16 → 20 | 6.75 | 3.00 |
| 20 → 24 | **−0.75** | 6.50 |

A negative marginal cost is not a measurement of anything. `measure_solver_cost.sh` already records
one of these, from unequal rung widths; this one arrives from the other direction and is worth
recording separately because the remedy is different. Emitting a program costs on the order of
twenty milliseconds. The stage-0 baseline reaches **1060 ms** at the top rung, and its run-to-run
spread is around ±20 ms. Differencing two minima of that size cannot resolve a twenty-millisecond
term, and **more repeats cannot fix it** — the minimum of nine runs is already at the floor, so what
is missing is signal and not averaging.

The remedy is to multiply the subject and leave the baseline alone: each stage now does its work
**eight times** and the script divides. That is only legitimate if the compiler really does the work
eight times rather than folding it into one, so each copy is a distinct template instantiation and
**the scaling is checked rather than argued** — `tools/measure_generate_cost.sh --verify-amplification`:

| copies | emit per copy | expand per copy |
|---|---|---|
| 1 | **1.0 ms** | **40.0 ms** |
| 2 | 26.0 ms | 28.5 ms |
| 4 | 24.0 ms | 17.5 ms |
| 8 | 23.6 ms | 19.1 ms |
| 16 | 22.6 ms | 20.2 ms |

The per-copy cost is flat from two copies on, at 12 sections: about 23 ms to emit and about 20 ms to
expand. A compiler that had folded the copies together would show this column collapsing towards
zero as K grew, and it does not. The `1` row is the unamplified measurement, kept in the table
because it is the failure itself: it reads emitting as 1 ms and expanding as 40 ms, both wrong by
more than a factor of two, in opposite directions.

## Finding S7-A — the baseline under these numbers is NOT flat, and it is the planner

Stage 0 is everything M1 and M2 do: build the graph, analyse it, plan it. It grows **quadratically**
in the length of the chain, and that is a bigger fact than anything else on this page.

Attributed rather than guessed, by compiling the same translation unit with and without the planner
— `tools/measure_generate_cost.sh --attribute-baseline`:

| sections | build + analyse | build + analyse + plan | the planner's own share | marginal per section |
|---|---|---|---|---|
| 4 | 220 ms | 248 ms | 28 ms | — |
| 8 | 227 ms | 321 ms | 94 ms | 16.50 |
| 12 | 248 ms | 439 ms | 191 ms | 24.25 |
| 16 | 250 ms | 606 ms | 356 ms | 41.25 |
| 20 | 267 ms | 822 ms | 555 ms | 49.75 |
| 24 | 277 ms | 1059 ms | 782 ms | 56.75 |

*(This is a separate run from the ladder above, so its stage-0 column is a few milliseconds away from
that table's — 248 against 249 at four sections, 439 against 448 at twelve. The two were never meant
to be the same numbers; what is being compared here is the two columns beside each other.)*

Building and analysing is flat at about **2.9 ms per section**, which reproduces what the guard has
recorded since M1. Planning is not: its marginal cost per section climbs 16 → 24 → 41 → 50 → 57 ms
with no sign of settling, and from eight sections up the totals fit `1.36 × sections²` to within 8%
at every rung — 94/191/356/555/782 measured against 87/196/348/543/782 predicted. The smallest rung
is the exception and reads 28 ms against 22 ms predicted, which is the fixed cost a pure quadratic
with no intercept does not account for; it is about 7 ms, and it is invisible by eight sections.

**Where it comes from.** `choose_order` in `schedule/plan.hpp` is a list scheduler: for each of the
n steps it calls `pick_next_node`, which scans every node not yet placed. That is the classic
O(n²) shape, and `hand_back_finished_places` adds a second one by scanning the value table once per
step. Neither is a defect in the sense of computing a wrong answer — the schedule is correct, and
the conservation hashes have not moved.

**What is being done about it: nothing, yet, and deliberately.** Nothing in M3 caused this and
nothing in M3 depends on it. It is recorded here and reported to the operator because a cost of this
shape is the one kind that raising a ceiling never buys off (amendment 03), and because the point at
which it starts to hurt is predictable rather than mysterious: at twenty-four sections it is three
quarters of a second, and the milestones that widen the operation set (M4, M5) are the ones that
make graphs large enough for it to matter. A worklist keyed on the number of unsatisfied reads would
make `choose_order` linear, and that is a change to M2's planner rather than to M3's generator.

## What this does not say

It does not say generating is free — it says it is linear and small. It says nothing about a program
with many DISTINCT operations, because every section of this cascade emits the same seven; the
expansion cost per step could depend on which `compute()` branch a step selects, and this ladder
cannot see that. The operation set widens at M4 and M5, and that is the milestone at which a second
ladder — same chain length, varying operation mix — is worth having.

It also says nothing about the cost of generating in a REAL build rather than under `-fsyntax-only`.
Instantiating a template and emitting object code for it are different costs, and only the first is
measured here. That is the right half to measure for this question — the one that could go
quadratic is instantiation — but it is half.
