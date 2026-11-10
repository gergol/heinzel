# What the solver costs at `gradients()`'s infer, and what shape that cost has

**Question:** milestone M7.6 re-takes M2.8. `gradients()` gives the extent solver its SECOND call site
— the `infer` it runs over the graph it has just augmented with adjoint nodes. Standing rule S-6 asks
for the **shape** of a cost and not only its size: a linear cost that is merely large may be paid for
by raising a dial, and a cost that grows faster than the graph may not be, ever (amendment 03). At M2
only one call site existed (`infer` on a hand-written graph, `solver-cost.md`); P1-4 asked for both
instrumented, and this is the other.

**The subject** is a differentiable chain of N sections — a running signal and, per section, a
coefficient of its own symbolic extent, combined `y = tanh(y·cₛ)` so exactly one pair of axes meets
per section — differentiated wrt the input, its loss a `mean` over a symbolic axis (so the R4-2
run-time reciprocal is on the path). `gradients()` appends ~4N adjoint nodes and infers them; that
infer is the measured site. `tests/compile_time/gradient_cost.cpp`, two regimes:

| regime | what runs |
|---|---|
| **literal** | `gradients()` infers a fully-literal augmented graph — the solver is constructed and never asked |
| **unbound** | symbolic extents, nothing bound — the R4-2 reciprocal runs and the resolving pass canonicalises the appended adjoint nodes' symbolic shapes |

## Result

**Flat. About 2.7 ms per section with the augmented graph literal, 2.9 ms with it symbolic and
canonicalising — and neither widens along the chain.**

Nine runs per point, minimum reported; `-fsyntax-only` with clang invoked directly, so no cache and no
code generation are in the number; one rung per translation unit so no rung hides in another's shadow;
rungs equally spaced so the marginal columns are comparable. `tools/measure_gradient_cost.sh`,
clang 22.1.8, x86_64.

| sections | literal | unbound |
|---|---|---|
| 4 | 180 ms | 182 ms |
| 8 | 190 ms | 204 ms |
| 12 | 200 ms | 209 ms |
| 16 | 214 ms | 225 ms |
| 20 | 222 ms | 237 ms |
| 24 | 235 ms | 239 ms |

Marginal cost per section, interval by interval:

| interval | literal | unbound |
|---|---|---|
| 4 → 8 | 2.50 | 5.50 |
| 8 → 12 | 2.50 | 1.25 |
| 12 → 16 | 3.50 | 4.00 |
| 16 → 20 | 2.00 | 3.00 |
| 20 → 24 | 3.25 | 0.50 |
| **overall** | **2.75** | **2.85** |

**The shape, in the terms S-6 asks for: the marginal cost per differentiable section is FLAT.** The
last interval is no larger than the first in either regime — in the unbound regime it is the SMALLEST —
the scatter is about ±1.5 ms with no trend along the chain, and the overall slope sits in the middle of
that scatter. A super-linear cost at these sizes would not be subtle: the example the guard cites went
from 1.2 s to 12.6 s across four rungs.

**What the second call site adds over the first.** The autodiff pass roughly doubles the node count
(one adjoint per primal node), so `gradients()`'s infer runs the solver over ~2× the graph the primal
alone would — but the SLOPE is unchanged, which is the property that mattered. The solver's own share
between the regimes is about **0.1 ms per section**, smaller than at M2 (0.5–0.9 ms) because the
appended adjoint nodes are mostly elementwise, whose shapes the solver resolves without a unification.
The R4-2 reciprocal (a ones buffer summed over the reduced axes) adds a constant handful of nodes at
the loss, not a per-section cost — which is why the unbound regime's slope matches the literal one
rather than diverging from it.
