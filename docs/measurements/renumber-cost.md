# What a canonical renumbering pass would cost

**Question:** `plan/TRACEABILITY.md` Q-2. `finish()` copies the arena in emission order, so a value's
identity is the order it was *evaluated* in, which C++ does not fix within one expression. Walking
the graph instead — depth-first from the outputs, operand slots in written order — would make
identity come from the syntax, which no compiler can permute.

**Decision this does not change.** The operator decided on 2026-08-25 to **leave the mechanism
alone** and record the number. Nothing in `include/` walks anything; the candidate lives in
`tests/compile_time/canonical_renumber.hpp`, under the same three exclusions as the compile-time
guard, and exists so that a later decision starts from evidence.

## Result

**About 1 ms per filter section, linear, and it does not change the shape of the curve.**

| sections | baseline | with renumbering | delta | factor |
|---|---|---|---|---|
| 3 | 266 ms | 268 ms | 2 ms | 1.01x |
| 6 | 272 ms | 277 ms | 5 ms | 1.02x |
| 12 | 295 ms | 304 ms | 9 ms | 1.03x |
| 18 | 305 ms | 325 ms | 20 ms | 1.07x |
| 24 | 327 ms | 351 ms | 24 ms | 1.07x |
| 30 | 342 ms | 374 ms | 32 ms | 1.09x |

Marginal cost per section: **2.8 ms building, 3.9 ms building and walking.** So the walk adds roughly
a third to what a section costs, and adds the *same* amount for the thirtieth section as for the
third. An independent run an hour earlier gave 9/7/18/17/23/**35** ms for the same rungs — same
shape, same magnitude at the top, which is the reproducibility this number needs to be worth
keeping.

**Read it against the rule the guard states.** A cost linear in the size of the graph and merely
larger may be paid for, or paid for by raising the budget. A cost growing faster than the graph may
not, ever. This one is linear: the delta per section is flat, so adopting the pass would be a
constant factor of about 1.1x at the largest graph the IR can currently hold — not a change of
asymptotic class.

## Method

`tools/measure_renumber_cost.sh`, clang 22.1.8, this host. Both columns are the **same translation
unit** compiled twice, differing only by `-DHEINZEL_MEASURE_RENUMBER`, so the ~260 ms of fixed
header cost is present on both sides and cancels out of the delta. `-fsyntax-only`, compiler invoked
directly so ccache is not involved, nine runs per point, **minimum** reported — on a shared host
noise only ever adds time, so the minimum is the better estimator and it is visibly steadier than
the median was.

The graph is the guard's cascade of second-order sections, unchanged, so these timings and
`tests/compile_time/guard.cpp`'s are comparable. The baseline here reads ~60 ms above the guard's
because this file also builds the two property graphs below and checks them.

## What the measurement is not

**The range is one decade, and the ceiling is a chosen number.** A graph is a fixed-size value
holding at most `kMaxValues` (320) values, and a section costs ten, so ~31 sections is the top.
Whether the delta stays linear at ten times this size is **not observable** with today's `Graph`,
and this measurement does not claim it — but the limit is a decision, not a property: `kMaxValues`
and the constant-evaluation budget may both be raised (operator, 2026-08-25), so extending this
measurement is a matter of choosing to, not of waiting for the design to allow it. Worth doing
before the linearity is relied on for anything.

It is also a measurement of *a* walk, not of the best possible one. The implementation is iterative
on purpose — spike D's recursive flattener hit the default `-fconstexpr-depth` of 512 at ~80 chained
operations, so timing a recursive one would time a design that could not ship.

## The property, checked rather than assumed

A fast pass that renumbers wrongly is worth nothing, so `renumber_cost.cpp` asserts the point of the
exercise at compile time. The same expression `x*2 + x*3` is built with its two products created in
either order — identical syntax, different emission order, which is exactly what gcc does to one
expression written one way:

- the two graphs **are not equal today** (this is Q-2, reproduced without needing a second compiler)
- they **are equal after renumbering**
- renumbering preserves value and node counts, is idempotent, leaves every operand before its use,
  and leaves every recurrence still naming its variable

## The cost that is not in the table

A walk from the outputs reaches only what an output depends on, so **anything unreachable is
dropped**. That is dead-node elimination arriving as a side effect of renumbering. It is probably
wanted, but it is a second decision wearing the first one's clothes, and adopting the pass without
naming it would be how a graph silently starts losing nodes. Input order also becomes discovery
order rather than arena order.

## Q-2 measured under gcc at the M2 boundary, 2026-08-25

The section above reproduces Q-2 without a second compiler, by building one expression two ways.
The M2 boundary run under **g++ 15.2.0** shows it happening on its own, to an expression written
once — which is the form the question is actually asked in.

`x + 1.27279221 * y.previous(1) + -0.81 * y.previous(2)`, the biquad, built by gcc twice: once
while compiling and once while running. Everything about the two graphs agrees except **who is
value number what**.

```
values: compile=11 run=11        nodes: compile=7 run=7
value 4 differs: category compile=Input run=Internal
value 5 differs: category compile=Internal run=Const
node 2 differs: op compile=_add reading [4,3]   run=_fdelay reading [0,-]
node 3 differs: op compile=_fdelay reading [0,-] run=_mul reading [5,4]
```

Both graphs are the same filter and both produce the same numbers — `parity_biquad` and
`parity_biquad_scheduled` pass under gcc. What differs is emission order, because
`y.previous(1)`, `y.previous(2)` and the two constants each append a node when they are evaluated,
and C++ does not fix the order in which the operands of `operator+` are evaluated. gcc happens to
choose one order in its constant evaluator and another in its generated code; clang happens to
choose the same order in both, which is why this is invisible under the required toolchain.

**Under amendment 01 this is recorded, not fixed:** gcc runs at milestone boundaries, and a
gcc-only failure is recorded with its diagnosis rather than holding a milestone. The decision is
Q-2, which is the operator's and becomes live at M6 with a hard deadline at M9.

**What M2 changed about the question.** Nothing about the mechanism, and one thing about the price.
At `dc1b57d` exactly one test failed under gcc for this reason (`execution`). At the M2 boundary
three do — `execution`, `symbolic` and `conservation` — because M2 added two more oracles that
compare a compile-time-built graph against a run-time-built one, which is precisely the comparison
Q-2 breaks. Every oracle this tree adds that rests on graph identity adds another. M3's generated-
against-interpreted check (O6) will rest on it too.

The candidate fix, and its measured cost, is the rest of this document.
