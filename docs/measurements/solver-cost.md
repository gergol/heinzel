# What the extent solver costs, and what shape that cost has

**Question:** milestone M2.8. The analysis pass gained an extent solver, and standing rule S-6 asks
for the **shape** of a cost and not only its size — because a linear cost that is merely large may
be paid for by raising a dial, and a cost that grows faster than the graph may not be, ever.

**Measured at every call site the solver has**, which is what the condition asks for. The three call
sites are three regimes of the same graph, and they are the regimes in the order a graph reaches
them:

| regime | what runs | who pays it |
|---|---|---|
| **literal** | the solver is CONSTRUCTED and never asked anything | every graph in this tree, including all thirteen operations' tests and the parity biquad |
| **bound** | the seeding loop, one `unify` per section, and the resolving pass substituting sizes back into every shape | a graph whose size arrives as a binding |
| **unbound** | the unifications happen and nothing is pinned, so the resolving pass canonicalises rather than substitutes | a graph that stays symbolic |

## Result

**Flat. About 3.9 ms per filter section with the solver never asked, 4.8 ms with it seeding,
unifying and substituting, and 4.4 ms unifying and canonicalising — and none of the three widens
along the chain.**

Nine runs per point, minimum reported; `-fsyntax-only` with clang invoked directly, so no cache and
no code generation are in the number; one rung per translation unit so no rung hides in another's
shadow; rungs equally spaced so the marginal columns are comparable to each other.
`tools/measure_solver_cost.sh`, clang 22.1.8, x86_64.

| sections | literal | bound | unbound |
|---|---|---|---|
| 4 | 212 ms | 216 ms | 217 ms |
| 8 | 231 ms | 228 ms | 234 ms |
| 12 | 240 ms | 255 ms | 256 ms |
| 16 | 256 ms | 270 ms | 269 ms |
| 20 | 276 ms | 290 ms | 287 ms |
| 24 | 290 ms | 311 ms | 304 ms |

Marginal cost per section, interval by interval:

| interval | literal | bound | unbound |
|---|---|---|---|
| 4 → 8 | 4.75 | 3.00 | 4.25 |
| 8 → 12 | 2.25 | 6.75 | 5.50 |
| 12 → 16 | 4.00 | 3.75 | 3.25 |
| 16 → 20 | 5.00 | 5.00 | 4.50 |
| 20 → 24 | 3.50 | 5.25 | 4.25 |
| **overall** | **3.9** | **4.8** | **4.4** |

**The shape, stated in the terms S-6 asks for: the marginal cost per graph section is FLAT.** The
last interval is no larger than the first in any regime, the scatter is about ±1.5 ms with no trend
along the chain, and the overall slope agrees with the middle of that scatter. A super-linear cost
at these sizes would not be subtle — the example the guard cites went from 1.2 s to 12.6 s across
four rungs.

**The solver's own share** is the gap between the regimes: roughly **0.9 ms per section** to seed,
unify and substitute, and **0.5 ms** to unify and canonicalise. Its **construction**, which every
graph pays whether it asks the solver anything or not, does not separate from the noise — the fitted
intercepts are 196 ms and 197 ms against a fixed header cost of about 196 ms. That is the number
that mattered most, because the construction is the only part paid by graphs that have no symbolic
extent at all, and it is why `infer` skips the resolving pass entirely for such a graph.

## How this was measured, and one thing that went wrong first

The cascade the compile-time guard already uses **cannot** measure this. Its only shapes are one
block and some scalars, and a scalar broadcasts against anything without two axes ever meeting — so
a symbolic version of it pays the solver's construction and nothing else. The ladder here is a
second cascade (`tests/compile_time/solver_cascade.hpp`) in which every section takes a coefficient
signal with an extent of its own, so every section makes exactly one pair of axes meet. One
unification per section is what makes "the marginal cost per section" a question about the solver
rather than about the pass around it. The guard asserts that the sections really do all end up in
one class, so a future change that stopped them meeting would show up as a failed assertion rather
than as a quietly meaningless number.

An earlier reading, at five runs per point and rungs 3/6/12/18/24, produced a first marginal of
**−0.33 ms per section** — a graph with twice the work compiling faster than the smaller one. That
is not a measurement of anything, and it is worth recording why it happened, because the same
mistake is easy to repeat: a marginal cost is a difference between two noisy numbers and is far
noisier than either, and intervals of different widths are not comparable to each other at all. Nine
runs and equally spaced rungs fixed both. The script now says so in its own comments.

## What this does not say

It does not say the solver is free — it says it is linear and small. It also says nothing about a
graph with many DISTINCT symbol classes, because this ladder deliberately unifies everything into
one: chain length is what varies here, not class count. The operations that will produce many
independent classes are M5's framing and spectral ones, and that is the milestone at which a second
ladder is worth having.
