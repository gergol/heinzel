# Decisions this tree is built on

Each entry states what was decided, what it rules out, and where the evidence is. Full reasoning
lives in `../workspace/plans/`; this file exists so someone reading the code can find out *why*
without leaving the tree.

## Decision 1 — graph construction: an arena with self-constructing leaves

**Decided 2026-08-24** (operator amendment 05). Chosen over three alternatives, all of which were
built and measured.

A leaf is not handed a graph to belong to. **A leaf allocates its own one-node graph** — because a
leaf does not need to *recover* a graph, it *is* one. Composition merges arenas: the smaller is
adopted into the larger and left as a forwarding stub carrying its offset, which is union-find with
path compression. A handle that has gone stale is therefore never wrong, only a hop from correct.

Measured against the alternatives (clang 22.1.8, same graph, 482 nodes):

| | design | 482 nodes |
|---|---|---|
| **chosen** | arena + forwarding handles | **317 ms** |
| | intrusive-refcounted DAG (the Halide/TVM ownership model) | 945 ms |
| | value-semantic fragments | 50 605 ms |
| | owned tree with `unique_ptr` children | does not compile — exponential on reuse |

The structural reason the arena wins: **it already is the flat node list, so flattening is a copy
rather than a traversal.**

**What this does NOT mean.** It removes the token from *leaves*, not from the language. Operations
that depend on where in the program they were written still need a context — see
`frontend/build_context.hpp`, whose header comment explains why, and Decision 2.

Evidence: `../workspace/plans/heinzel-dsl-study/00-SYNTHESIS.md` §3, spikes A–E.
The runner-up is written up in `OPTION-3-refcounted-dag.md` — parked, not rejected, and kept partly
as material for explaining how it *could* be done.

## Decision 2 — settings travel as an explicit value, not as ambient state

Forced by Decision 1, and confirmed by two measurements rather than argued (spikes F and G):
attaching settings to the arena loses one of them whenever two arenas merge, and marking a region
and stamping what was added cannot distinguish nodes *created* in the region from nodes *adopted*
into it. Generalised: **the "when" of an emission cannot be recovered from arena structure at all.**

Cost: about two dozen call sites — the boundary declarations, the operations that carry an accuracy
tier, and the sample-rate helpers. Ordinary arithmetic is untouched.
Saving: no guard objects and no restore-on-unwind obligation. Leaving a region is dropping a value.

## Decision 3 — operations declare their rules as data

Shape, element type, physical units and cost are columns of a per-operation table, interpreted by
one pass, rather than branches written once per operation inside each pass. The old tree already
does this for units and cost and guards it with a coverage assertion that makes a missing row a
*compile* error; this tree extends the same mechanism to shape and element type.

Evidence: `../workspace/plans/heinzel-dsl-study/00-SYNTHESIS.md` §4, and §4.1a for why Halide's
visitor-based alternative was measured and not taken.

## Decision 4 — accuracy tiers, gcc, and the conference

- clang is the required toolchain. gcc is nice-to-have and is checked only at milestone boundaries
  (amendment 01).
- The conference is not a scheduling constraint. This tree exists precisely so the old
  implementation stays demoable, so no meaningful refactoring is dropped for it (amendment 02).
- The constant-evaluation budget is a dial and may be raised — but it buys a constant factor, never
  an asymptotic class, so it is never raised to rescue a design that grows super-linearly
  (amendment 03).
- **Both ceilings are chosen numbers and may be raised whenever wanted or needed** (operator,
  2026-08-25): the constant-evaluation budget (`HEINZEL_CONSTEXPR_STEPS`, `CMakeLists.txt`) and the
  graph's capacity (`kMaxValues` / `kMaxNodes`, `ir/ir.hpp`). Neither is load-bearing — the budget
  costs nothing to translation units below it, and the capacity buys mangled-name length. So
  "the graph does not fit" and "the budget ran out" are decisions to take, not walls to design
  around, and they should not silently shape what gets built. This does not soften the line above;
  it sharpens what it is about. Raising a ceiling to admit a bigger graph is ordinary. Raising one
  because a cost went super-linear is the mistake amendment 03 names, and telling the two apart is
  why the measurements here report the shape of a cost and not only its size.

### A floating-point overflow is not a constant expression, and only one of the two compilers says so

**FINDING S5c-GCC, found at the M4 boundary and fixed there** — which is what amendment 01's
boundary gcc run exists for.

The tree stopped compiling under gcc 15.2 during M4 and nobody noticed for nine commits, because
clang is the required toolchain and the failing thing is legal under clang. One `static_assert`
pinned `_fma`'s single stated divergence from a hardware fused multiply-add — that a product past
the largest double answers infinity where hardware would recover a finite sum — and reaching that
case means overflowing on purpose:

    max * 2   // clang 22.1.8: folds to infinity.   gcc 15.2.0: 'is not a constant expression'

**gcc is right.** Producing a value outside the representable range is undefined behaviour, and a
constant evaluator may not fold undefined behaviour. The assertion was never a constant expression;
it was a constant expression *under one compiler*, and a green suite under the required toolchain
says nothing about that.

The property is real and stays checked — it moved to a run-time check in the same file, beside the
sweep that compares this core against the platform's. What it cost is one world, and the note beside
it says which and why.

**The general consequence, since this tree evaluates so much while compiling.** Arithmetic that
overflows cannot happen during constant evaluation under a strict compiler, so a graph whose
constant-folded arithmetic reaches infinity is buildable under clang and not under gcc. That is not
a defect in either of them; it is the price of the leniency clang extends, and it is recorded here
so the next occurrence is recognised in one reading rather than nine commits.

## Decision 5 — the schedule is a separate value, and M1 builds one

**Decided 2026-08-24, in the course of the work**, and recorded here because it settled a question
the milestone's wording left genuinely open rather than because it was planned.

Milestone M1's fourth condition asks for *"a compiled artifact for at least one non-trivial graph,
runnable, with its numeric output compared against the old tree"*. **"Compiled artifact" is a term
of art in this project**: in the old tree, `compile<G, T>()` runs a pipeline and produces a value
that says how to run a graph. That leaves two readings of the condition, and they lead to
materially different work:

- a graph built and analysed while compiling, then run — which was already true; or
- the output of a schedule step, run — which was not.

**Both are now satisfied, by building the second.** The reasoning is that the readings are not
symmetric: the stricter one contains the looser one, so doing the work settles the question instead
of arguing about the word. It is also wanted for its own sake — separating what a program means from
how it is run is amendment 05's "op semantics from ISA lowering", and it is the talk's own narrative
(amendment 02, A2-1).

What was NOT built, so this is not read as more than it is: no fusion, no loop nests, no
vectorisation, no code generation. The schedule holds an order and a storage layout, and an
interpreter runs it. The full pipeline in the old tree's `runtime/compile.hpp` has six stages;
`compile<G, T>()` here names one, and says so.

**What the separation immediately bought**, which is the honest test of whether it was worth it:
the same graph can now be scheduled two ways — sharing storage and not — and the two must produce
the same numbers. That is *decision conservation*, and it was explicitly unavailable while there was
only one way to run a graph. The cross-tree numeric check cannot see it: two different schedules
computing the same numbers both pass. This one can.

## Decision 6 — the accuracy default is the old tree's default

**Decided 2026-08-25 by the operator** (Q-1), after M1 surfaced it.

The tier was declared `Exact` while the skeleton was being written, on the grounds that the strictest
value is the safe one to pick when nothing reads it yet. That was wrong in a way worth recording:
the old tree defaults to `Precise`, which permits reassociation, so `Exact` was not conservative —
it was a **silent behaviour change** dressed as caution. The numeric-parity test pins the tier
explicitly on both sides, which is why nothing failed and why the difference could sit unnoticed.
`Precise` is back as a value and is the default.

**Left open at the time, and since CLOSED.** A tier bundled two independent things — which
transcendental implementation runs, and whether the optimiser may reassociate. One ladder reached
four of the six combinations those span; "coarse functions, reproducible ordering" was not
expressible at all. **The operator decided on 2026-08-25 to split the tier into those two axes, and
it was split during M4**, before the transcendental operations were written: `FnAccuracy`,
`Ordering` and the `NumericSetting` pair in `ir/vocab.hpp`, with `Accuracy` kept as the four-name
convenience over them. The trigger named here — the first transcendental — is what arrived.

The full record of that decision, its reason and what it cost is **Decision 9**, written at M4.3 as
the milestone requires. It said "Decision 8" from `f14b236` until M4.7, by which time Decision 8 had
been written about something else — Decision 9 opens by recording how that happened. This paragraph is kept rather than deleted because the reasoning it contains was
right, and a decisions file that quietly loses the "we left this open, and here is why" step stops
being a record of how the tree got here.

## Decision 7 — every column that describes meaning has a reader, and `CostClass` deliberately has none

Milestone M2.6. The rule table declares six columns per operation. Five of them describe what an
operation MEANS, and by this milestone every one of them has a pass that reads it:

| column | read by |
|---|---|
| shape | `sema/analyze.hpp` — the shape rule, since M1 |
| element type | `sema/analyze.hpp` — the type rule, since M1 |
| precondition | `sema/analyze.hpp` — checked after the fixpoint, because it is a question about the *resolved* shape |
| timing | `schedule/plan.hpp` — what may share storage, and where a loop is legal |
| units | `sema/analyze.hpp` — new here: the physical-dimension check |

**`CostClass` gains no reader, and that is a decision rather than an omission.** A cost is a
*price*, not a meaning. A pass reads a price in order to CHOOSE between alternatives, and there is
no pass in this tree that has alternatives to choose between: there is one way to lower an
operation, one order-selection rule, and one storage policy. The first thing that will genuinely
want it is the back end at M6, which picks among several implementations of the same operation.

This is written down because **a column that is complete, asserted by the coverage check, and
interpreted by nothing looks exactly like a column somebody forgot**. The coverage check will keep
demanding a `CostClass` for every new operation, which is right — the price is real and stating it
at the moment the operation is written is cheaper than reconstructing it later — but nothing today
consults it, and a reader who went looking for the consumer should find this instead of a puzzle.

### The consumer arrives at M6.4 — the cost-priced lane choice

**As of milestone M6.4, `CostClass` has a reader, exactly the one this decision predicted.** The back
end at M6 picks between two implementations of a per-sample operation — lane it, or run it scalar —
and prices that choice from the cost column. A `Cheap` operation is laned by a wide back end; an
expensive one (`Software`, `Tiered`) runs scalar even under a wide back end, because this tree's
expensive cores are long scalar sequences (a Newton square root, some twenty operations of Dekker
splitting for `_fma`) that a lane cannot widen. The reader is `step_may_lane` in
`codegen/program.hpp`; the choice is recorded as `Program::may_lane` (one flag per step) and consulted
by `run_steps` in `codegen/generate.hpp`, which hands each step's flag to its generated body. It lives
on the program rather than on `Step` for the reason finding M6.4-NTTP records, below.

**Finding M6.4-NTTP — a field cannot be added to `Step` on clang 22.1.8.** The natural home for a
per-step flag is `Step` itself. It cannot go there: `Step` is materialised as a non-type template
argument (`run_step<P.steps[I], ...>`), and adding *any* field to it — even an unread trailing `bool` —
miscompiles the generated run. The symptom is a run-time `fail()` from `element_read_by` ("an operand
has more axes than the result it feeds"), because a wrong `Elements::rank` reaches it at run time while
the constant-evaluated path stays correct; a graph of unit widths that has always agreed bit for bit
suddenly throws. Bisected to the field addition on a clean tree, reproduced at `-O0` and `-O3`, and
narrowed to `Step` specifically — the same field added to `Program` (materialised whole, never indexed
element-wise as a template argument) is fine. So `may_lane` is a `Program`-level parallel array. This
constrains every later milestone that would grow a step: put the datum on the program, not the step,
until the toolchain moves. Recorded here rather than in `docs/limitations.md`, which is the old-tree
exclusion board and is asserted whole.

The choice is **part of the emitted program**, so oracle O5 sees a cost answer change: adding the field
re-recorded exactly the two corpus entries that carry an expensive per-sample step (the elementwise and
transcendental sets), with the reason in `conservation/recorded.hpp`, and `conservation_test.cpp`
perturbs `may_lane` to show it is inside the program hash. It **moves no number**: the scalar and lane
paths are the same `compute()`, which is what lets `unit/lane_cost_test.cpp` show a wide back end
agreeing with the scalar back end and the interpreter bit for bit on both a cheap and an expensive
graph, while a `MisLane` back end is caught on the cheap graph (it lanes) and not on the expensive one
(it does not) — the choice changing behaviour, shown both ways.

This does not disturb the deviations recorded below: they are about which VALUE a row carries, and are
as true now as when written. What changed is only that the column is read.

**Decision 7 still stands in substance** — a cost is a price, not a meaning — but the price now has the
one reader a price is for: a pass that chooses between alternatives.

### `CostClass::Software`, and a row that deliberately differs from the old tree's (milestone M4)

`_fma` arrived at M4 and did not fit either value. The old tree's row calls it `Cheap` and is right
to: at `c4012b8` it lowers to a platform `fma_`, one machine instruction. **Here it is around twenty
floating-point operations of Dekker splitting**, because `std::fma` cannot be called in a constant
expression under clang and every graph in this tree must be evaluable while it is compiled. Copying
the old row across would have carried a fact about the OLD tree's implementation into a tree where
it is false — and a cost that reads "one multiply-add's worth" for twenty of them is wrong in the
one direction a scheduler would care about.

`Hardware` did not fit either, and the reason is worth naming because it is the same shape as Q-1:
it bundles TWO facts — expensive, and one instruction with no coarser version — and only the first
is true of `_fma` here.

**FINDING S4-B, one commit later: that argument applied to `_sqrt` as well, sitting two rows up, and
it had not been noticed.** This tree's square root is a Newton iteration and a correction, in both
worlds; it is no more a machine instruction than `_fma` is. `Hardware` therefore described no row it
labelled — a value that can never be true of anything here, which is exactly what M2.6 objects to in
the other direction. So `Hardware` was not joined by a third value; it was **replaced** by
`Software`, and `_sqrt`, `_rsqrt` and `_fma` all carry it. Two values, and each describes something.

This is a deliberate deviation from the old tree's row and is recorded as M4.4 requires. It does not
change Decision 7's substance: `CostClass` still has no reader, and gaining a value it does not have
a reader for is exactly as true of `Software` as of the two beside it.

### Two deliberate deviations in the periodic family (milestone M4.4)

**`_sinpi` is a real half-turn sine here and is not one in the old tree.** That tree's vocabulary
comment calls it the periodic DSP primitive whose argument reduction is exact in a way `sin(pi*x)`
is not — and then `op_eval.hpp` at `c4012b8` implements it as
`sin_rn(3.14159265358979323846 * a)`, which is exactly that spelling and inherits exactly that
problem. Here the coefficients have pi folded into them, so nothing multiplies the argument by pi at
run time and the reduction is a subtraction of an integer: exact at every double. `sinpi(1)` is zero
here and 1.22e-16 there; `sinpi(x)` for x above 1e14 was measured exact at 200000 arguments here and
is meaningless there. Values will differ, and this side is the correct one.

A second, smaller decision came with it: the zero at an integer carries the **argument's** sign, so
`sinpi(-x) == -sinpi(x)` holds everywhere including at the zeros. Writing it as `-sin(0)` gives
negative zero for both `1` and `-1`, which is not odd.

**`_sin`, `_cos` and `_tan` answer NaN beyond 2^40 radians, where the old tree keeps answering.**
Reducing an argument that large modulo pi/2 needs bits of pi/2 that no finite expansion in this file
carries; the platform's library reduces against a stored expansion of 2/pi (Payne and Hanek) and the
old tree gets that for free by calling it. This tree does not implement Payne-Hanek and will not
return a number it cannot stand behind, so it says NaN — and the operation for that range is
`_sinpi`, whose reduction is exact everywhere.

The bound is measured rather than chosen: the exact-product reduction was checked at 2 ulp out to
1e12, and 2^40 is where the argument's own ulp starts to swamp what is being asked of it. An earlier
Cody-Waite arrangement was silently wrong from about 2^23 upward — five trillion ulps at an argument
of 1e9 — which is why the bound is now stated and gated rather than assumed.

### The inverse circular family, and one rule where the old tree has two (milestone M4.4)

`_atan` `_asin` `_acos` `_atan2` arrived together, and three of the four differ from the old tree's
rows in ways worth recording rather than discovering.

**The old tree's `Compare` and `SameDimless` are one rule here, named `AgreeingToPlain`.** At
`c4012b8` `Compare` is documented as "in0 ~ in1 (const-poly); out is a dimensionless mask" and
`SameDimless` as "in0 ~ in1 (const-poly); result dimensionless (atan2)". The constraint is
identical; only the prose about what the answer is USED for differs, and the two arms that read
them would have identical bodies. A relation with two names is two places to change and one of them
to forget, so there is one value, and it is named for the relation rather than for either family of
readers — `_lt` `_le` `_eq` answer a mask, `_atan2` answers an angle, and the units question about
all four is the same.

**`_atan2`'s five infinite answers are computed rather than tabulated.** `atan2(±inf, ±inf)` is
±pi/4 or ±3pi/4, `atan2(±inf, x)` is ±pi/2, `atan2(y, ±inf)` is ±0 or ±pi. Each infinite operand is
replaced by a unit of its own sign and each finite operand beside an infinity by a zero of its own
sign, and the ordinary finite code then produces exactly those five values — verified against the
platform's `atan2` bit for bit. Five more constants would have been five more chances to write one
down wrong, and the old tree does not face the question at all because it calls libm.

**All four are hand-written cores measured against libm, where the old tree simply IS libm.**
`atan_rn`, `asin_rn`, `acos_rn` and `atan2_rn` at `c4012b8` are `::atan` and friends, so the old
tree's exact tier is the platform's answer by definition. Here it is a written core, and the
distance is a measured quantity: **at most one ulp** for every one of the four, over a linear sweep,
every binade a double has, the hundred thousand arguments closest to one from either side, and
twenty million random bit patterns each. That bound is gated in `transcendental_test.cpp`.

One defect found by that measurement and worth keeping: `_atan2` of two DENORMAL operands was 9638
ulps out, because the compensated division divides its residual by the denominator and
`exact_product` does not split a denormal exactly — so the "correction" was amplified by 10^300
instead of correcting anything. The division is now done on the significands, where both operands
are normal by construction, and the exponent difference put back afterwards. The plain quotient
alone would have been within one ulp the whole time, which is the useful shape of the lesson: a
compensation that cannot be computed is worse than no compensation at all, and a guard that says so
is part of the compensation.

### The hyperbolic tangent, and where its row agrees with the old tree (milestone M4.4)

`_tanh` is the one row of the family that matches the old tree's in every column that both trees
have. At `c4012b8` it is `{1, CostClass::Trans, UnitsRule::Dimensionless}`; here it is arity 1,
`CostClass::Software`, `UnitsRule::Dimensionless` — the cost value is the rename recorded above and
not a different answer. There is nothing to deviate about, and saying so is the point of the sweep:
a check that only ever reports differences cannot be distinguished from one that is not running.

Two things about it are still worth recording, because neither is visible in a row.

**The old tree's exact tier for `_tanh` is libm; this one is a written core, measured at TWO ulps.**
Two rather than the one every other transcendental here reaches, and the reason is structural: the
answer is a quotient of two quantities the exponential series produced, so it carries that series'
error twice. Measured against `std::tanh` over five million arguments from -25 to 25 and every
binade below 16 — worst case 2 ulps, 4884259 of the five million bit-identical. Gated in
`transcendental_test.cpp` at the measured bound.

**Its saturation above an argument of 20 is arithmetic, not a shortcut.** `tanh` reaches the largest
double below one at about 19.07, so every argument past that has exactly one as its correctly
rounded answer. The branch exists because `e^2x` overflows a double long before `tanh` stops being
worth computing, and answering one there is the right answer rather than a cheap one. The old tree
never faces the question because libm does.

### `_pow`, and the old tree's two answers for it (milestone M4.4)

The old tree's row is `{2, CostClass::Trans, UnitsRule::PowLike}` with the comment *"exp(b·log a)
core; a<=0 -> NaN on the SIMD path"*. Three things follow from it, and each is a deliberate decision
here rather than an inherited one.

**`PowLike` is not a separate rule here; `Dimensionless` is asked of both operands.** The old tree's
`PowLike` is documented as "base, exponent and result all dimensionless" and its `Dimensionless` as
"argument must be dimensionless; result dimensionless". That is one relation asked of one operand and
of two, not two relations — so this tree has one value and the arity decides how many operands it
reaches. The check on an operand past the arity costs nothing: nobody has stated units for it, and a
claim nobody has made is never a disagreement. Both halves are shown rejecting in `units_test.cpp`,
the exponent's separately, because that half did not exist before `_pow` arrived.

**AT `c4012b8` `_pow` HAS TWO ANSWERS THAT DISAGREE OVER A QUARTER OF ITS DOMAIN, and this tree keeps
the right one.** The SIMD path answers NaN for every `x <= 0`; the scalar path calls libm, which
answers `pow(-2, 3) = -8`. A negative base raised to an integral exponent has a real answer, and the
sign of that answer is the exponent's parity — so this tree computes it, everywhere, by construction.
Preserving the parity of the SIMD path would mean preserving a defect of exactly the kind milestone
M3 exists to make impossible: one meaning, two consumers, and the consumers agreeing because there is
only one meaning to agree about.

**The exact tier is a written core measured at ONE ulp, where the old tree's is libm.** That it is
one ulp and not three is the content of `power_cores.hpp` and not a property of the obvious spelling:
`2^(y * log2 x)` written with this tree's own `_log2` and `_exp2` is out by hundreds, because the
logarithm's error is multiplied by `y` and `y * log2(x)` runs to 1024 before the answer stops being a
double. The logarithm is therefore carried in a pair to about 2^-65, which is ten bits more than a
double holds and is what the amplification demands.

Two defects found by measuring rather than by reading, both invisible outside that regime and both
recorded because the shape of each is the useful part:

- the series for `log(m)` was truncated ten terms too early. Over the reduced interval the first
  dropped term is 2.4e-17 relative — 2^-55, which a double cannot even represent as a correction to
  itself, so every ordinary sweep called it exact. It surfaced only as 146 ulps in `pow` at an
  exponent near a thousand.
- the squared series argument dropped its own low half, which put the leading term of the correction
  at 2^-60 where the budget is 2^-63.

Both say the same thing: an intermediate's accuracy has no meaning on its own, only against what the
consumer multiplies it by, and a sweep that never reaches the amplifying case measures the wrong
thing confidently.

**One case where this tree is an ulp from the platform and could be exact.** `pow(2, 0.5)` answers
one ulp below the correctly rounded square root of two, because it goes through the logarithm like
every other argument. Routing a half-integral exponent through `_sqrt` would fix it and is
deliberately not done: it would make the answer depend on whether an exponent happens to land on a
representable half, which is a discontinuity in the operation's meaning bought for an ulp.

### What giving `UnitsRule` a reader actually cost

*(Reversed by Decision 59: the units check, the `UnitsRule` column and `Value::units` went on 2026-10-04.)*

Not "add a reader". `Value` carried an element type, a shape, a category and a constant, and **no
units at all**, so the column described a relation between things the graph could not express. A
value had to be able to hold a physical dimension first — which is the one change M2 makes to the
graph type, and the reason the conservation corpus (M2.5) is recorded after this landed rather than
before it.

The dimension is a vector of rational exponents over a base set of one, TIME, which is the old
tree's design (`units/dimension.hpp` at `c4012b8`) and carried across deliberately. Rational rather
than integer exponents are load-bearing: a square root halves an exponent, so the root of a rate is
`1/s^(1/2)` and integers cannot say it. What did NOT come across is the named-unit surface —
`seconds`, `ms`, `hertz`, the SI scale factors, `to_samples`. Those are a layer (M8), and the
algebra is in the core only because a core pass has to read it. Deleting the layer would not change
`ir/units.hpp`.

One thing the check is careful about: **"nobody said" and "this is a plain number" are different
claims.** Only the second is a claim. Collapsing them would make every unannotated value assert it
was dimensionless, and then a graph with one annotated input would be rejected everywhere that
input touched anything else. And a *literal* never states a dimension at all — it adapts to what it
is used with, exactly as it does for element types, so `x + 0.5` on a duration stays legal.

The check is opt-in: a graph in which nothing states a unit skips it entirely. That is what keeps
it from taxing every constant evaluation in the tree rather than only the graphs that use it.

**One case the table could not state at M2.6, and how it was settled at M4.** `_reduce` was
classified `Structural`, meaning the dimension passes through unchanged — correct for a sum, a
maximum, a minimum and a mean, and *not* correct for `ReduceOp::Prod`: a product over an axis of
extent N raises the dimension to the Nth power, a dimension that depends on an extent and which no
row of a per-operation table can hold. No graph used a product reduction, so nothing was wrong; it
was parked here for M4's reduction work with a guess at the answer — *"likely a precondition — a
product reduction over a dimensioned operand is refused"*.

**Answered at M4, and the guess was right about the outcome and wrong about the mechanism.** It is
not a precondition. The `Precondition` column is a question about a resolved SHAPE, and this is a
question about a dimension — putting it there would have been the conflation that column's own
comment warns against.

What it is instead is **the second key, which this tree already had for a different column**. A rule
cannot always be looked up by operation alone: `_reshape` is one operation carrying several
rearrangements with different shape rules, and `_reduce` is one operation carrying five folds whose
dimension rules are not all the same. So `_reduce`'s units column reads `ByReduceOp`, and
`ir/attribute_rules.hpp` holds the table it defers to — `PassThrough` for a sum, a maximum, a minimum
and a mean, `MustBePlain` for a product. A product reduction over a dimensioned operand is refused
with a message that says what actually went wrong: *"_reduce: a product of s raises it to an extent's
power"*. A product of plain numbers is a plain number and is accepted.

That is the honest thing the table CAN say. Computing the dimension would mean raising it to an
extent's power, which a symbolic extent could not answer at all — and no graph asks for it.

It comes with the coverage check that makes the second key worth having: **a sixth fold added without
a dimension rule does not build**, exactly as a fourth rearrangement without a shape rule does not.
The pattern was already there and `_reduce` had been carrying its neighbours' answer since M1 for
want of using it.

### `_contr`, whose old row is wrong in two columns (milestone M4.4)

Both differences are **defects corrected**, not deviations chosen, and each was settled by reading
the old tree rather than by preferring this tree's taste.

**Arity.** `ir/vocab.hpp:162` at `c4012b8` declares `{"_contr", {1, CostClass::Cheap,
UnitsRule::Structural}}` — arity **one**. The only site that builds a contraction passes **two**
operands (`frontend/builder.hpp:2805`, `g.emit(op::Contract, a.type, a.value, b.value)`) and the
units arm reads operand `b` (`sema/check_units.hpp:186`). Two independent readers contradict the
row, so the row is the thing that is wrong. This tree writes arity 2.

**Units.** The old row says `Structural`. There that value means *"the `AttrKind` arm decides"*, and
the arm for a contraction answers `add_rel` — the dimensions **add**, because a contraction is a sum
of products. In this tree's vocabulary that relation is `UnitsRule::Product`, which already exists
and already has a reader, so the answer goes in the column instead of deferring to an arm this tree
does not have. `Structural` here means *"the dimension passes through unchanged"*, which for a
contraction is simply false: contracting seconds with hertz gives a plain number, and a row claiming
pass-through would have said seconds.

**What is deliberately NARROWER than the old tree, and why that is not a defect.** The old
`ContractAttr` is `{std::int32_t spec}`, an index into a `Graph.contracts` side-table of
`ContractSpec` einsum bindings — three lists of small index ids, one per operand and one for the
output, able to express batch and transposing contractions. This tree's `ContractAttr` names the
axes that collapse and nothing else. The general form is not copied across because **nothing here
would construct one**: a spec with no builder is the same thing M2.6 refuses in the column of a rule
table, and the narrower attribute covers exactly the case the old tree's own comment gives as its
example — *"an id in both A and B but not the output is contracted (summed)"*. The batch and
transposing cases arrive with M5's shape algebra, which is what they need.

The **cost** column is the old tree's unchanged: `Cheap`, because a contraction is multiply-adds.

### `_delay` against `_fdelay`, and which of the two trees' delays does the work (milestone M4.4)

*(`_delay` folded into `_fdelay` in Decision 60.)*

**Every column of `_delay`'s row matches the old tree's** — `ir/vocab.hpp:158` says `{1,
CostClass::Cheap, UnitsRule::Structural}` and so does this one. There is no deviation to record. What
there is to record is why the two rows are two rows and not one, because the answer is not the one
the old tree's documents give.

**In the old tree, `op::FixedDelay` is declared and never constructed.** Every mention of it outside
`Plan/` prose at `c4012b8`: the code itself (`ir/vocab.hpp:84`), its row (`:186`), the lockstep
assertion (`:224`), two lines of `frontend/autodiff.hpp` that classify it and exclude it from
gradients (`:66`, `:96`), two of `ir/op_eval.hpp` listing it among the ops the sequential path
realizes (`:606`, `:610`), and one row of a documentation table. **No emitter, no attribute variant
of its own, no test.** `op::Delay` is the one that works: it is what `x[n-k]` lowers to
(`frontend/builder.hpp:545`), what a feedback tap into a recurrence is, and what the stage classifier
keys on.

**The intended distinction was a scheduling one and was never built.** The archived plans
(`Plan/archive/convolution/00-Overview.md:53-55`) say `op::Delay` forces the scalar recurrence path
and `op::FixedDelay` was to be the vectorizable feed-forward tap. Nothing in the shipped tree acts on
it: `autodiff.hpp` treats the two identically, `op_eval.hpp` lists them together, no pass separates
them. An intent recorded in prose and never built is not a difference to copy.

**So the difference this tree takes is the one the old tree's CODE supports: the axis.** Its
`DelayAttr` is `{depth, axis}` (`ir/ir.hpp:139`) and belongs to `op::Delay`; its `_fdelay` has no
attribute variant and is described as `out[n] = in[n-D]`, with no axis in it. Here `_delay` carries
`AxisDelayAttr{samples, axis}` and `_fdelay` carries the axis-less `DelayAttr`, and the paired check
in `every_row_is_complete` holds each row to its own. That is a difference **the table states**,
which is what keeps this from being one meaning under two codes — the thing M2.6 exists to refuse.

**The names cross over, and that is a rename rather than a copy** (amendment 02). The old tree's
`DelayAttr` is the one WITH an axis; this tree's `DelayAttr` is the one without, because it was
written for `FixedDelay` at M1 and `FixedDelay` is the tap that has no axis. The recorded
conservation hashes are keyed on it, so renaming it would move every one of them for a change that
altered no behaviour. The new payload is `AxisDelayAttr`, which says what it is.

**THE LIMIT, STATED AND CHECKED RATHER THAN SILENT.** A run in this tree computes one number per
value per sample and the block index IS the streamed axis — there is no second index to step back
along. So the only lag that has anywhere to run is one along the innermost axis, and
`sema/analyze.hpp`'s `check_attributes` refuses any other by name: *"_delay reaches back along axis
0; a run here steps back along the innermost axis only"*. Shown rejecting against a rank-2 value with
`axis = 0`. The innermost has two spellings — `kInnermostAxis`, and the operand's last axis by number
— and both are accepted because both are the same axis.

This is the outcome M4's step plan named in advance for exactly this case: *"if the axis cannot be
expressed, `Delay` gets a stated and checked limit rather than a silent one."* A lag along an outer
axis needs shape machinery that reaches the runtime, which is M5's, and `_delay` is the operation
that will carry it when it exists. Until then the two lags agree sample for sample at the innermost
axis — asserted in `tests/unit/execution_test.cpp` — because one is the other's restriction.

### `_dline`, and the seam widening it forced (milestone M4.4)

**Every column matches the old tree's row** — `ir/vocab.hpp:185` says `{2, CostClass::Cheap,
UnitsRule::Structural}` and so does this one, and its `Structural` is this tree's exactly: the result
follows the signal and the position is unconstrained, which is what `units_pass_through` already does
and what the old tree's arm says in as many words (`sema/check_units.hpp:182`). There is no deviation
to record in units, cost or arity.

**What it added is a timing answer no earlier row could state, and that is the thing worth
recording.** `_dline`'s distance is its second operand's VALUE at this sample. It is therefore
neither `SameSample` nor a number the row could carry, and it broke an assumption that had been true
of every operation in the tree: **that a step reads all of its operands from one sample**.

Until milestone M4 that assumption lived in `samples_back()` — one number belonging to the step,
which each of the four runners applied to all three operands itself. Three consequences, each of
which is now a change rather than a defect avoided:

- **`TimeRule` gained `ReadsAtSignalledDepth`**, and its comment carries the caller's contract: fetch
  operand 1 at this sample, hand it to `signalled_tap()`, then fetch operand 0 at `whole` and at
  `whole + 1` samples back. A distance that lands between two samples is two reads and a blend.
- **The timing question is now asked per OPERAND** (`operand_comes_from_the_past`), not per node. That
  is not tidiness. A tap delay line reads its signal from the past and its position from the present,
  so a planner that asked the node would have found no present dependency at all and been free to
  schedule the delay line before its own position — reading the previous sample's, for a plausible
  wrong answer that no comparison between two runs of this tree could see. `execution_test.cpp` pins
  what the planner is told rather than where the delay line landed in one order, because the greedy
  walk happens to pick a good order here even when told nothing.
- **The fetch schedule became one shared piece of code**, `operands_of()` in the new
  `ir/timing.hpp`, handed a reader. All four runners use it. Written per runner it would have been
  four chances to disagree about which samples a program reads, which is the same class of mistake
  M3.1 exists to prevent about what it computes. Generated code passes a reader whose every index is
  a literal, so each operand's source and slot stay compile-time constants.

**`ir/timing.hpp` is a file split, under amendment 06**, made because `ir/op_rules.hpp` reached 665
lines. It is the seam that file already had: what is left there describes what a TABLE says about an
operation in terms of nothing but the vocabulary, and what moved out describes what one column means
for a particular NODE. `ir/op_rules.hpp` now needs only `ir/vocab.hpp`.

**The interpolation is the old tree's**: `lerp(x[n-i0], x[n-i0-1])` at `i0 = floor(clamp(pos))`, zero
before the start of the block. Written `near + f*(far - near)` rather than `(1-f)*near + f*far`
because the two differ in their last bits and this one is exactly `near` when `f` is zero, which is
what makes a whole-sample position agree with a fixed delay of the same distance to the bit.

**Two things are checked rather than assumed.** The position is DATA, so it is clamped to
`[0, ceiling]` with the comparison written `!(p > 0)` — a position that is not a number lands at zero
instead of surviving into a cast, which has no defined answer. And a negative ceiling is refused by
`check_attributes`, because it is not a smaller clamp but a range with nothing in it.

**`SignalledDelayAttr` is a rename** (amendment 02). The old tree calls it `FracDelayAttr` with a
field spelled `fdelay`, one letter from the NAME of its fixed delay, `"_fdelay"` — two different
things sharing an abbreviation. It deliberately has no axis, and the old tree says why: *"the
fractional delay is a data-dependent gather along the STREAMED axis — a non-innermost fractional tap
has no driver, so the innermost anchoring is permanent, not a legacy convention."* That is why
`_dline` never gains what `_delay` has.

### The sweep — all twenty-nine rows against `c4012b8`, and the one difference nobody had recorded (M4.4)

The sections above record each family's deviations as the family was written. This is the sweep
M4.4 actually asks for: **every one of the twenty-nine new rows, all three columns, in one place.**
M4.4 is the one M4 condition the milestone document deliberately makes *a review with an artifact
rather than a gate* — so what it produces is this table, and no `ctest` entry.

Both sides were extracted mechanically rather than transcribed, because a hand-typed table of
twenty-nine rows by three columns is a table with a typo in it: this tree's from
`include/heinzel/ir/op_rules.hpp`, the old tree's from `git show main:include/heinzel/ir/vocab.hpp`
at `c4012b8`, each parsed out of the initialiser rather than read by eye. Anyone can redo it.

**A word about the timing column, because the old tree has none.** There an operation's timing is
implicit in which `AttrKind` arm routes it, and exactly two arms reach into the past. Its
`sema/analyze.hpp:1269` names both of them and nothing else — *"the tap readers"* — and its
`AttrKind::Delay` arm at `sema/analyze.hpp:1001` refuses a depth below one with *"a delay must read
the past (depth >= 1)"*, which is that arm saying outright what the column here states. So "checked
against the old tree's timing" means checked against which operations those two arms cover, and
"no arm" below means the old tree has no arm claiming that operation reads anything but the current
sample.

| op | units (old -> here) | cost (old -> here) | timing (old -> here) |
|---|---|---|---|
| `_min` | SamePoly | Cheap | no arm -> SameSample |
| `_max` | SamePoly | Cheap | no arm -> SameSample |
| `_le` | **Compare -> AgreeingToPlain** | Cheap | no arm -> SameSample |
| `_eq` | **Compare -> AgreeingToPlain** | Cheap | no arm -> SameSample |
| `_sign` | ToDimless | Cheap | no arm -> SameSample |
| `_floor` | Same | Cheap | no arm -> SameSample |
| `_ceil` | Same | Cheap | no arm -> SameSample |
| `_round` | Same | Cheap | no arm -> SameSample |
| `_trunc` | Same | Cheap | no arm -> SameSample |
| `_fma` | ProductAddend | **Cheap -> Software** | no arm -> SameSample |
| `_rsqrt` | **NegHalf -> MinusHalf** | **Hw -> Software** | no arm -> SameSample |
| `_exp` | Dimensionless | **Trans -> Software** | no arm -> SameSample |
| `_log` | Dimensionless | **Trans -> Software** | no arm -> SameSample |
| `_exp2` | Dimensionless | **Trans -> Software** | no arm -> SameSample |
| `_log2` | Dimensionless | **Trans -> Software** | no arm -> SameSample |
| `_log10` | Dimensionless | **Trans -> Software** | no arm -> SameSample |
| `_sin` | Dimensionless | **Trans -> Software** | no arm -> SameSample |
| `_cos` | Dimensionless | **Trans -> Software** | no arm -> SameSample |
| `_tan` | Dimensionless | **Trans -> Software** | no arm -> SameSample |
| `_sinpi` | Dimensionless | **Trans -> Software** | no arm -> SameSample |
| `_atan` | Dimensionless | **Trans -> Software** | no arm -> SameSample |
| `_asin` | Dimensionless | **Trans -> Software** | no arm -> SameSample |
| `_acos` | Dimensionless | **Trans -> Software** | no arm -> SameSample |
| `_atan2` | **SameDimless -> AgreeingToPlain** | **Trans -> Software** | no arm -> SameSample |
| `_tanh` | Dimensionless | **Trans -> Software** | no arm -> SameSample |
| `_pow` | **PowLike -> Dimensionless** | **Trans -> Software** | no arm -> SameSample |
| `_contr` | **Structural -> Product** | Cheap | no arm -> SameSample |
| `_delay` | Structural | Cheap | **Delay arm -> ReadsEarlierSamples** |
| `_dline` | Structural | Cheap | **FracDelay arm -> ReadsAtSignalledDepth** |

Bold is a difference. Every one of them is already recorded above with its reason — `Compare` and
`SameDimless` merging into `AgreeingToPlain`, `PowLike` becoming `Dimensionless`, `_fma` and
`_rsqrt` becoming `Software`, `_contr`'s corrected units, and the two lags' timing — with two
exceptions, which is what a sweep is for.

**`_contr`'s other corrected column is its arity, and it is not in this table because M4.4 names
three columns and arity is not one of them.** It is recorded in full above: the old row says 1, two
independent readers there pass and read two operands, and this row says 2. Said here so that a
reader comparing the two tables side by side does not find a fourth difference the sweep appears to
have missed.

**`_rsqrt`: `NegHalf` is spelled `MinusHalf` here.** One rule, one meaning, one spelling changed:
the tree spells `Half` out and an abbreviated negative beside a spelled-out half read as two
conventions. Added to the naming table below. Nothing else about the row differs.

**FINDING S8-COST — the old tree has THREE cost values and this tree has two, and until now nothing
said why.** At `c4012b8` the enum is `{Cheap, Hw, Trans}` and its own comment states exactly what
the two derived facts are: *"expensive ⇔ cost != Cheap, tiered coarse core ⇔ cost == Trans"*. So
`Hw` and `Trans` are not two flavours of expensive — they answer a second question, **does function
accuracy choose among cores of different cost**, and `Hw` is its "no" (`_sqrt`, `_rsqrt`, `_order`).

Here all eighteen expensive rows carry `Software`, whose comment says *"expensive, and
tier-invariant"*. **That is true of every one of them today** — this tree implements one core per
transcendental and `FnAccuracy::Approx` and `FnAccuracy::Fast` select nothing yet, so `_exp` really
does cost the same at every setting. The row is right about the tree it describes. What was missing
is that the old tree draws a distinction here and this one does not, which M4.4 requires recorded
whether it is deliberate or not.

It is deliberate, and the reason is the rule the tree already follows for `UnitsRule`: **a column
value arrives with the thing that needs it, never before.** A third value added today would label
fifteen rows with a property no code implements — which is M2.6's objection in its other direction,
a value indistinguishable from one somebody forgot.

**So the trigger is named rather than left to be rediscovered: the third value arrives with the
coarse cores, at M4.2.** The moment a second core exists for any operation, `Software` becomes a
false claim on that row and the column gains the value that says so. Two things about it are already
decided by the way `Hardware` failed (finding S4-B, above): it will be named for the FACT and not
for the family — `Transcendental` would bundle "is a transcendental function" with "has tiered
cores", and the old tree's own row for `_rsqrt` already anticipates them coming apart (*"tier-invariant
full precision (estimate tiers: measured later)"*) — and it does not resurrect `Hardware`, because
nothing here is one instruction.

**A second thing M4.2 will need somewhere, noted here while the old row is open.** That tree carries
the measured bounds IN the table, as an `OpBounds{approx, fast, relative}` per row. This tree has no
bounds column and does not need one yet for the same reason. When M4.2 records a measured maximum
error per function-accuracy tier per transcendental, that is where the question of a column
against a separate table gets answered — and it is a real choice, because the old tree's own numbers
are `float` bounds on a `double` core.

## Decision 8 — structure is gated, and here is what the gate cannot see

Operator amendment 06, 2026-08-25, mid-M3: *"it is very important that the code is well structured
and we avoid the humongous multi thousand line long headers and hundreds of lines long functions of
the previous implementation"*.

The old tree at `c4012b8` has 102 headers totalling 44,236 lines, ten of them over a thousand, a
2,944-line `frontend/builder.hpp`, and 38 functions of 60 lines or more with a 1,064-line one at the
top. Nobody decided that; it arrived one convenient addition at a time. An instruction to write
shorter functions is exactly the kind of thing that decays the same way, so this is a gate:
`gate_structure_limits` measures every `.hpp` and `.cpp` under `include/`, `tests/` and `tools/`, and
fails naming the file, the construct, its measured size and the limit. **600 lines a file, 60 lines a
function.** They are limits and not targets — a 58-line function that is one coherent step is fine,
and shredding something coherent into six 10-line functions called once each makes the code less
readable while making this gate pass.

**What the gate cannot see**, written down rather than left implied, because a gate whose blind spots
are undocumented is one somebody later trusts too far:

- **"A file declares one concern" is not gated, and cannot be.** Two of the amendment's three limits
  are mechanical and the third is a judgement. It is a review obligation, not a number anybody can
  point at a CI run. The test the amendment gives is whether the sentence its header comment opens
  with needs an "and"; that is a question for a reader.
- **A long function assembled out of short ones is invisible to it**, and to any line-counting gate.
  The limit is a floor under readability, not a definition of it.
- **Braces within a line are counted but not ordered.** `} else {` and one-line bodies are measured
  by net depth, which is right for every case in this tree and would not be for a construct that
  closed and reopened a scope in an order the net does not capture.
- **A macro that opens or closes a brace would desynchronise the depth.** None exists here. The gate
  does not silently absorb it if one appears: it refuses to report a file whose braces do not balance
  at the end, because reporting that file as clean would be a false pass.
- **Raw string literals (`R"(...)"`) are not understood by the literal stripper.** None exists here.
  A brace inside one would be counted.
- **A nested lambda is not measured separately.** The enclosing function's length includes it, which
  is deliberate: the reader still has to hold it.
- **`.cmake` and other non-C++ sources are not measured at all.** The gate globs `.hpp` and `.cpp`.

**One thing it does NOT do, and the reason is worth keeping.** It never turns a source file into a
CMake list. The obvious spelling — `string(REPLACE "\n" ";" ...)` and `foreach(IN LISTS)` — was the
first implementation, and it is wrong in the direction that matters. A C++ line ends in `;` more
often than not, so the text has to be escaped before it is split, and the escaping does not survive
real source: measured on `sema/analyze.hpp`, it collapsed thirteen consecutive lines into one
element, called a 525-line file 513 lines, and put `infer()` at line 166 when its signature is at
178. `file(STRINGS)` fails from the other side, splitting on semicolons within a line, and called the
same file 530. Both are silent, and a collapsed line **shortens** whatever function contains it — so
the failure mode of the obvious spelling is a false pass. Lines are peeled off the front of the
string with `FIND` and `SUBSTRING` instead.

That defect surfaced because the reported length for `infer()` matched the amendment's independent
measurement of 346 while the reported line number did not; the length agreed only because the twelve
lost lines happened to fall before the function and shift its start and end together. Two tools
sharing one flaw look exactly like corroboration. The gate is cross-checked against an independent
implementation of the same algorithm, and the two agree on every line number, every length, every
file line count and the brace balance.

The would-object fixtures pin the measured size, and the function one also pins the **line number**,
for that reason — an expression matching only "is 73 lines" would have passed the bug above.

### FINDING M4-OPTIN — the opt-in exclusion is a blind spot too, and it caught a real defect

Found at M4's boundary, by the check success condition 3 asks for: build with
`HEINZEL_BUILD_COMPILE_TIME_GUARD=ON` and run the opt-in targets. `heinzel_generate_cost` **did not
compile**, and had not since M4.2's seam widening three commits earlier.

Its `GeneratorCopy<Copy>` — a distinct generator type per copy, which is what makes the expansion
happen again rather than being reused — still had `template <Node N> step(a, b, c)`. When the tier
reached the arithmetic, a generator's contract became `template <Node N, FnAccuracy Fn>`, and eleven
places were changed together for the reason `operands_of()` exists. **This was the twelfth**, and it
was missed because it is `EXCLUDE_FROM_ALL` and deliberately outside `ctest` (amendment 03 A3-2): the
suite could not see it, a clean configure could not see it, and every commit in between was honestly
green.

**So the exclusion the guard needs for speed is a blind spot of the same kind Decision 8 lists for
the structure gate, and it is written down here for the same reason.** Two consequences:

1. **The milestone-boundary check is what covers it**, and it must build and run ALL of the opt-in
   targets — `heinzel_compile_time_guard`, `heinzel_generate_cost`, `heinzel_renumber_cost`,
   `heinzel_solver_cost` — not the two the M3 boundary happened to record. That is now what the
   boundary log carries.
2. **A seam count is a count of what the suite reaches**, not of what the tree contains. M3.4's
   record of how many places an operation touches moved to eleven at the seam widening; the true
   number was twelve, and the twelfth was invisible to every check that ran that day.

It is the second time a boundary check has found something a green suite could not — amendment 01's
gcc run found the first, at M1 — and it is the argument for keeping both.

## Decision 9 — the accuracy setting is two axes, and this is what splitting it cost

Milestone M4.3, which asks that Q-1 be answered and **written here with its reason**. **Decided
2026-08-25 15:47 by the operator**, in one sentence, answering the M3-boundary report's
recommendation: *split it now*. Implemented at `f14b236`.

**FOUND MISSING AT M4.7, and the way it went missing is the useful part.** Decision 6 has said since
`f14b236` that the full record "is Decision 8, written at M4.3 as the milestone requires" — and
`ir/vocab.hpp` said the same. Decision 8 was then written, a milestone later, about amendment 06's
structure gate. **The number was taken and the forward pointer went dangling**, and nothing noticed,
because a cross-reference to a heading that exists is exactly as valid to a reader skimming as one to
the heading they wanted. The record M4.3 requires simply was not here. Written now, as Decision 9,
with both pointers corrected. A forward reference to a number nobody has allocated is a promise that
depends on nobody else getting there first.

### What was decided, and why it could not wait for its own deadline

The setting answered TWO questions at once — which transcendental implementation runs, and whether
the optimiser may reassociate — and four rungs cannot reach the six combinations two axes span.
*Coarse functions with reproducible ordering* could not be said in this tree at all. That it was
unreachable is the whole finding; a tidier spelling was never the point.

The split is additive on any day it is taken, so this was never about capability. It is about
**price, and the price was about to rise for one specific reason**: M4.2 records a measured maximum
error per tier per transcendental and gates it, and fifteen of M4's new codes are transcendental. A
table keyed on a conflated rung has to be **re-measured after a split rather than re-labelled**. So
the split was taken before a single one of those numbers existed. The same argument put M3 ahead of
the operation set, and it has now been right twice.

### The spelling, which the decision left to this side

Two axes, plus the ladder kept as a convenience over them:

    enum class FnAccuracy  { Inherit, Exact, Approx, Fast };
    enum class Ordering    { Inherit, Reproducible, Free };
    struct NumericSetting  { FnAccuracy fn; Ordering ordering; };
    Accuracy               the four rungs, each naming a pair

The ladder is kept rather than retired because two of the decision's own conditions require it:
nothing expressible before may become unnameable, and parity against the old tree must stay a
comparison rather than a translation — that tree has one ladder, `Numerics`, and `Accuracy::Precise`
has to go on naming its default directly.

It is `NumericSetting` and not `Numerics`, which the step plan had drafted. The old tree's `Numerics`
is its four-rung ENUM; parity work reads the two trees side by side, and one name for two different
shapes is how a comparison turns into a misreading.

### What it cost, measured rather than estimated

**281 lines added and 102 removed across thirteen files**, of which 123 added are `ir/vocab.hpp` —
the two enums, the pair, the packing and its two proofs. **Eight of the thirteen files are tests**,
and that ratio is the honest shape of this change rather than a footnote: four headers moved, and the
rest of the work was pinning what the move must not have altered. Nothing outside the enum, the sites
that read it, the tables that key on it and the tests that pin it was touched; the decision said
explicitly that it licenses no rename spree, and none was taken.

**The recorded conservation hashes did not move and were not re-recorded.** That is the decision's
third condition — *default behaviour unchanged, and the hashes are the check* — and here it is an
observation rather than an assertion: `tests/conservation/recorded.hpp` is untouched by `f14b236`.
The pair travels as one packed byte whose numbering leaves the five ladder points at the five codes
they already had and **appends** the seven the ladder could not reach — the same argument
`op::Min{13}` makes for appending an operation code rather than inserting it.

Two obligations arrived with a packing and both were discharged as checks **shown objecting**: the
packing is injective over all twelve combinations, and each axis is separately inside the
conservation oracle. And the first condition — both axes independently settable — has teeth rather
than a tidy shape: `numerics_for_call()` resolves each axis on its own, and its test was pointed at a
deliberate pair-wise resolution and observed to reject it in both directions.

### What was deliberately NOT built, and what that leaves open

No `allows_reorder()` predicate, no scheduler consumer, no reverse mapping from a pair back to a
rung. **Nothing in this tree reassociates anything**, so the ordering axis is honoured vacuously
today; it has a name and a place on a node because a node must CARRY a setting before a pass can ask.
Its first reader arrives with the first pass that would reorder, which is M6's. The old tree's two
consumers — `frontend/einsum.hpp:375` gating contraction-path reordering and `schedule/policy.hpp:37`
gating loop transforms on reduction axes — are recorded in the enum's comment as the evidence of what
that name will eventually be asked to do.

The consequence for M4.2 is not a loophole and is worth stating where the decision is recorded: the
ordering axis today promises nothing observable, and the milestone anticipates exactly that case and
asks for it to be said out loud rather than left as a tier claiming a freedom it never takes.

## Naming — what was renamed on the way across, and what it used to be called

Amendment 02 makes readable naming a deliverable, and the design documents in `../workspace/plans/`
cite the old spellings. So a rename is only worth making if it earns its keep, and every one is
recorded here — otherwise the plans stop being readable against the code.

| here | old tree | why |
|---|---|---|
| `OpCode` | `OpRef` | it is a code stored in a node, not a reference to anything |
| `OpName` | `OpId` | it is the operation's short spelled-out name; "id" suggested a number |
| `Accuracy` | `Numerics` | it says what the setting controls — how much accuracy may be traded for speed. Since the Q-1 split it is the four-name convenience; what a node CARRIES is `NumericSetting`, a pair with no old-tree counterpart (Decision 6) |
| `symbol_of` / `encode_symbol` | `sym_of` / `dim_ref` | spelled out, and `dim_ref` did not say it was an encoding |
| `kMaxMessageLength` | `kMaxDiagLen` | matches `kMaxNameLength`; one abbreviation style in the tree |
| `Target` | `TargetInfo` | it is the target; "info" said nothing |
| `Placement` / `Storage` | `ArenaLayout` | see below — `arena` already means something else here |
| `Units` / `base_units` | `Dimension` / `base_dim` | `Dimension` collided with `Dim`, which is a symbolic axis size — two meanings for one word is what this tree exists to avoid |
| `plain()` | `units::dimensionless` | it is what a person says: a gain is a plain number |
| `SignalledDelayAttr` | `FracDelayAttr` | its field is spelled `fdelay` there, one letter from the NAME of that tree's fixed delay, `"_fdelay"`. What actually distinguishes it is that the distance arrives as a signal, so that is what it is called |
| `AxisDelayAttr` | `DelayAttr` | the names cross over: the old tree's `DelayAttr` carries an axis and this tree's does not, because this one was written for `FixedDelay`. Renaming the existing one would move every recorded conservation hash for no change in behaviour, so the payload that names an axis is the one that got the new name |
| `UnitsRule::MinusHalf` | `UnitsRule::NegHalf` | one rule, one meaning; the tree spells `Half` out two rows above it, and an abbreviated negative beside a spelled-out half reads as two conventions |

One rename made to keep a word meaning one thing, which is the rule amendment 02 asks for. The old
tree calls the storage a schedule assigns "the arena". Here **`arena` already means the block a graph
is built in** (Decision 1), and those are different things at different times — so the storage plan
is spelled with `Placement`, `Storage` and `slot` instead. Two meanings for one word is the specific
failure this tree exists to avoid.

Not renamed, on purpose: `fixed_vector`, `fixed_string`, `Shape`, `Extent`, `Dim`, `Axes`,
`ElemType`, `AttrKind`, `ViewKind`. They already say what they are, and a rename that buys nothing
costs the ability to read the plans against the code.

**`_xpose` was kept, and under amendment 02 that is a decision rather than a copy (milestone M5, the
structural set).** The amendment governs abbreviations, and it has been applied both ways already:
`_dline`, `_fdelay` and `_contr` were kept, `FracDelay` became `SignalledDelay` because two different
things shared one abbreviation. `_xpose` collides with nothing, it is the old tree's own row name
(`git show main:include/heinzel/ir/vocab.hpp`), and M5's row-by-row comparison against that table is
a comparison only while the names line up. Its C++ spelling is `op::Transpose` — also the old tree's
— so the readable name is readable and the row's name is the one being compared. The abbreviation
that would have earned a rename is one that read as two things; this one does not.

One deliberate change of meaning rather than of spelling: `ElemType::Unknown` is now the **zero**
value. The old tree appended it last to keep existing template manglings stable, which is a
constraint a fresh tree does not have — and the ordering matters, because it makes "nobody has said
what type this is yet" the honest default instead of single precision.

## Decision 10 — the tier reaches the arithmetic, and where it is grounded

Milestone M4.2, and the first half of it: until this step **nothing in the tree read a node's
`numerics`**. The axis existed, it was carried on every node, it was hashed into the conservation
record, and no code anywhere asked it a question. `compute()` did not take it, so no core could have
branched on it if it had wanted to. A setting nothing reads is a promise, and M4.2's condition — a
measured maximum error per tier per transcendental — cannot be met by a tree that computes the same
number at every tier.

**The resolution has one writing, in `ir/vocab.hpp`.** `resolved_against(inner, outer)` takes an
expressed answer over `Inherit`, **per axis**, and it is what the frontend uses to resolve a call
against its build context and what all three runners use to resolve a node against its graph.
`BuildContext::numerics_for_call` had been doing exactly this in two lines of its own; three copies
of an inheritance rule are three chances to differ about the case nobody tests.

**The ground is `Exact`, and it is a decision rather than a fallthrough.** `running_function_accuracy`
never answers `Inherit`, because the arithmetic has to run something. A graph has nothing above it,
so a graph whose own default still says `Inherit` has expressed no preference anywhere — and such a
graph got the exact cores before this axis existed, because they were the only ones. Grounding
anywhere else would make the arrival of the coarse cores change what an unconfigured graph computes,
which is the operator's Q-1 condition 3 in as many words. `Graph::default_numerics` is `Precise` out
of the box, so the ground is reached only by a graph that deliberately cleared it.

**The ordering axis is deliberately NOT grounded.** Nothing reads it, so there is no arithmetic to
hand a ground to, and inventing one now would be deciding what "reproducible" means on behalf of the
first pass that reassociates. That pass is where it gets decided.

**`Inherit` is refused rather than treated as `Exact`.** Three runners resolve; `compute()` tells a
fourth that forgets, at its first call, and so does `terms_at()` for a core reached directly. The
alternative — quietly taking the most accurate tier — would give a graph the right numbers while its
setting was being ignored, which is the hardest kind of wrong to find.

### What it cost, because M3.4 asks for that number

Eleven headers, and the two that are worth naming are not the obvious ones. **Twelve places, not
eleven** — corrected at M4's boundary: a twelfth was a generator copy in an `EXCLUDE_FROM_ALL`
measurement target that no green suite could reach. FINDING M4-OPTIN above is that story, and the
correction is here rather than only there because this is the number M3.4 asks for.

`codegen/program.hpp` gained a field. The header said — and still says — that a program "carries its
steps' nodes BY VALUE, so nothing needs the graph once the program exists". That was true only while
nothing read a node's `numerics`: `Inherit` resolves against the graph's default, which a program did
not have. So `Program::default_numerics` is not an extension, it is a repair, and generated code can
now resolve a tier from the program alone. **It moved the recorded program hashes, once**; every one
of the three artifact numbers is unchanged for all eleven corpus entries, which is the check that
the change altered nothing it had no business altering. `tests/conservation/recorded.hpp` carries the
account.

`codegen/generate.hpp` widened the generator seam: a generator is now handed one step's node AND the
resolved tier, both as compile-time constants. A generated run that chose a core while it ran would
be dispatching, which is the one thing `00-OVERVIEW.md` §0.4 says a generated run does not do.

### Finding S8-COST's trigger fired, exactly as it was written

`CostClass` had two values against the old tree's three, and Decision 7's finding named the moment
the third would arrive: *"the trigger is named rather than left to be rediscovered: the third value
arrives with the coarse cores, at M4.2"*. It has. `CostClass::Tiered` means **expensive, and not
tier-invariant: a coarser core exists and the function-accuracy setting selects it**, and six rows
carry it — `_exp` `_exp2` `_log` `_log2` `_log10` `_tanh`.

Named for the fact and not for the family, which that finding also fixed in advance. `_sin` is every
bit as transcendental as `_exp` and stays `Software` until its coarse core is written; `_rsqrt` is
`Software` permanently, for the reason the old tree's own row gives.

**Which rows carry it is checked rather than reviewed.** `tests/unit/tiers_test.cpp` requires the set
of rows marked `Tiered` to be exactly the set of operations it has MEASURED giving three different
answers at the three tiers, walked off the rule table rather than listed. Both directions fail: a row
labelled without tiered cores, and tiered cores added without the row.

### What a tier is spent on, and what it is not

A coarse tier buys exactly one thing: **fewer terms of the polynomial**. The reduction is shared by
all three tiers and is exact, so cutting it would give up the one part that costs nothing to keep —
and, more to the point, would stop the coarse core being a comparison against the exact one.
`runtime/series_length.hpp` is where a tier becomes a number, and the three lengths of each series
are declared beside the series with the first dropped term as the argument for the bound.

**What this step is not allowed to do**, and it is worth writing down because it is the easy way out:
record the same number three times and call the tiers observable. A tier whose measured error equals
the exact tier's is a tier that does not exist. So every operation is required to give three
different answers, and the bounds are additionally required to be within a factor of two of what the
sweep measures — a bound nobody tightens stops meaning anything.

### FINDING S12-F64TIERS — the old tree's coarse tiers exist only at `float`

Found while checking, rather than assuming, that the lever this tree spends a tier on is the lever
that tree spends one on. The first half of the check passed outright: `include/heinzel/simd/math.hpp`
at `c4012b8`, above its coarse cores, says *"Tiered coarse cores ... siblings of the Exact cores
above. **Same branchless range reduction; only the polynomial degree drops.**"* That is this design,
in that tree's own words, and it is why shortening a series is parity rather than invention.

The second half did not. Two hundred lines further down, every `double` tier forwards to the exact
core:

    inline vec<double> exp_fast_(vec<double> x)   { return exp_(x); }
    inline vec<double> exp_approx_(vec<double> x) { return exp_(x); }

with the reason stated: *"f64 tiered lanes are correctness-only here: a coarse tier is a ceiling, so
forward to the Exact f64 cores. Over-delivering accuracy is always contract-safe."* All twelve
`double` tiered lanes do this.

**So the old tree's `OpBounds` are float bounds on float cores, and at `double` its tiers collapse to
one.** Three consequences, none of them a reason to change course:

1. **The parity comparison is against the only place its tiers exist**, which is the float path.
   That is the right comparison and the numbers land inside it; the sentence saying so now says
   which path it means.
2. **This tree does something that tree does not**: coarse cores that actually differ at `double`.
   Recorded as a deliberate deviation under M4.4's standard. It follows from a difference already
   recorded — this tree computes in `double` everywhere and calls no math library, so "forward to
   the exact core" would leave M4.2 with nothing to measure.
3. **It is contract-compatible in that tree's own terms.** Its rule is that a bound is a CEILING and
   over-delivering is safe; this tree's coarse cores are inside its bounds, so a graph ported across
   at a named tier gets an answer that tree would accept.

**And one place the two now differ in kind rather than in degree.** That tree's coarse `_exp2` is
`exp_approx_(x * ln2)` — the spelling its own exact core avoids, because it misses a power of two by
an ulp at every integer. This tree splits the whole part off before the series at every tier, so
`_exp2` is exact at every integer at `Approx` and `Fast` as well as at `Exact`. Asserted in
`tests/unit/tiers_test.cpp` rather than left as a property somebody might notice.

### FINDING S12-F32HIDES — a tier that a graph's element type erases

Found at M4.2 while adding the inverse circular family to the tier gate: four assertions that had
been drafted exactly like the ten already passing turned red, and the cores were not the reason.

Every runner rounds a value to its own element type on the way out, and `input()` builds an `F32`
value unless told otherwise. A float ulp is about 6e-8 relative. The inverse circular family's
approximate tier differs from its exact one by about 5.3e-10 — **below a float ulp, so at `F32` the
two tiers produce bit-identical output.** Not hard to see: gone.

The gate would then have reported that `_atan` computes the same thing at every tier, and been right
about the graph while wrong about the cores. It is the exact inverse of the failure the distinctness
assertion exists to catch, and it would have been read as evidence that the routing was broken.

**The tier graphs are therefore built at `F64`**, with the reason written where they are built: what
that file asks is a question about arithmetic, so the graph is built in the type the arithmetic runs
in. Two consequences worth carrying forward:

1. **A coarse tier on an `F32` graph may be free of charge.** If a tier's error is below a float ulp
   over the whole domain, the coarse core and the exact one give the same `F32` answer — so the
   setting costs nothing and buys nothing. That is a fact about this tree worth knowing before
   anybody tunes a graph, and it is a stronger statement for the inverse circular family than for
   the other two, whose coarse tiers are far above a float ulp.
2. **An accuracy test must not inherit its element type from a convenience default.** Anything that
   measures how two computations differ has to be run in a type that can hold the difference.

### FINDING S12-POWLEVER — where `_pow`'s tier is spent, and the deviation in kind that follows

`_pow` is the only one of the fifteen where the choice of lever was not obvious, because it is the
only one built out of two other transcendentals: `x^y` is `2^(y · log2 x)`, so there are two series
to shorten and they are not equivalent choices.

**A note in `power_cores.hpp` predicted the wrong one.** It said the tiering would have to be taken
on the pair arithmetic as a whole, since a coarse tier that keeps the pair while cutting the final
series pays the whole cost of the pair for a coarser answer. That objection is true. It is also not
the deciding one.

**The logarithm's error is multiplied by an operand.** `y · log2 x` runs to 1024, so an error made
in the logarithm arrives at the answer amplified by `y`. A tier taken there has an error that GROWS
WITH AN OPERAND — and a bound that depends on an operand is not a bound. Cutting the final
exponential's series instead leaves the amplified quantity exact and makes the answer's relative
error the truncation of that one series, uniformly: **the same number `_exp` records, at every
exponent**. 3.2419e-06 and 7.9439e-04 measured, against `_exp`'s 3.2421e-06 and 7.9443e-04.

**That makes this a deviation in kind from the old tree, and a second one for the same family.** Its
float coarse core is `pow_fast_(a, b) = exp_fast_(b * log_fast_(a))` — the tier taken on the
logarithm, exactly the shape rejected here — which is why its `_pow` row carries the widest bounds
in its table (2e-4 and 3e-2, about seven and five times its own `_exp` row) and why those numbers
hold only over a modest exponent. Recorded as a deliberate deviation under M4.4's standard. It is
contract-compatible in that tree's own terms for the reason S12-F64TIERS gives: a bound is a
ceiling, and this tree's numbers are inside it over the whole domain rather than over part of it.

**The claim is gated rather than argued.** `the_power_tier_does_not_grow_with_the_exponent()` in
`tests/unit/tier_bounds_test.cpp` sweeps `_pow` twice — an ordinary grid, and arguments where
`y · log2 x` is deliberately placed near ±1024 — and requires the two to agree within a factor of
two, the same slack the recorded bounds are held to. Both directions are a real failure:

- **near-limit much worse** is the amplified tier, the thing this decision rejects.
- **near-limit much better** is a sweep that is not exercising the tier at all, which is not
  hypothetical. The first draft of it chose the exponent as `target / log2(x)` for an INTEGRAL
  target and measured exactly zero at every argument at both coarse tiers. It read as proof that
  the tier does not amplify; it was a sweep that could not see a tier. `2^p` splits the whole part
  off and scales it in afterwards, so an integral product leaves the series' argument at zero — the
  one place where a series of four terms and one of fourteen agree bit for bit. Sixteen fractional
  offsets per target fixed it, and the sweep then measures the same 3.24e-06 the ordinary grid does.

**A general lesson, and it is the one worth keeping.** When an operation is composed of others, the
question "where does a tier go" is not a matter of taste: it is decided by whether the error made
there is amplified by an operand on its way to the answer. Put the tier where the amplification has
already happened.

## Decision 11 — the ordering axis: one rung makes a promise, the other takes no freedom

Milestone M4.2's second half, and it is two claims with two different fates. Written out here
because the condition explicitly asks for this branch to be RECORDED rather than papered over.

### `Ordering::Reproducible` promises something, and it is gated

An expression that could be contracted into a fused multiply-add is required to equal its unfused
reference bit for bit. That is a promise with no escape clause, and `tests/unit/ordering_test.cpp`
is the gate: a graph computing `x * x + c` at `Reproducible`, at `x = 1 + 2^-27` and `c = -1`, must
answer `2^-26` in all three runners — the walk, the schedule-following run, and generated code.

**Its teeth are in the same file**, because without them the gate is green on a tree that contracts
everything as long as the reference contracts too. The same arithmetic spelled with `_fma` — which
promises a single rounding, in software, and is exactly what a contracting pass would leave behind
— is required to answer `2^-26 + 2^-54` at that same input. So "the unfused reference" is a claim
about the ANSWER and not about the spelling, and the input is demonstrably one where the two forms
differ.

**This is not the subject `tests/two_worlds/fusion_probe.cpp` has**, and the two look alike enough
to be worth separating. That one asks whether `-ffp-contract=off` does its work when THIS TREE'S OWN
SOURCE is compiled — a question about C++, answered on a target that has the instruction. This one
asks what a GRAPH at a named ordering answers, and it would still need answering on a tree compiled
with contraction turned on.

### `Ordering::Free` promises nothing observable today, and that is the finding

M4.2 asks for a graph whose `Exact` and `Precise` answers differ in bits — *"if no such graph can be
produced, because nothing in the tree ever reassociates, then the permissive tiers promise nothing
observable, and that is itself the finding: record it, and say so in `docs/decisions.md` rather than
leaving a tier that claims a freedom it never takes."*

**That is the branch this tree is on, and the evidence is three-fold rather than an absence of
effort:**

1. **Nothing reads the axis.** `ir/vocab.hpp` says so where `Ordering` is declared, and names the
   two places the OLD tree reads it: `frontend/einsum.hpp:375`, gating contraction-path reordering,
   and `schedule/policy.hpp:37`, gating loop transforms on reduction axes. Neither has an equivalent
   here yet.
2. **It was measured, not assumed** — FINDING S3-A at M3.2. The difference between the two rungs is
   zero, and the test that measured it says in its own message why that is weaker than it looks.
3. **The permissive rung demonstrably REACHES the graph while changing no number.** M4.1's corpus
   asserts `Precise` beside `Exact` with the graphs first shown to differ, and
   `every_operation_orders()` in the ordering test requires each node to carry the ordering it was
   built with — shown objecting by dropping the axis in `numerics_for_call`, at which point that one
   assertion turns red and every equality in the file stays green, which is precisely the failure it
   exists to catch.

**So the record is: at `Free` the answer is the unfused one; if this tree ever contracted it would
be the fused one; the two differ at this input by 2^-54, in bits the test can see.** The assertion
carrying it is written to go RED the day a contraction or reassociation pass arrives — that is the
moment `Free` begins promising something observable and the moment this section has to be rewritten,
and its message says so.

**That day came with Decision 73** (`rewrite/product_adds.hpp`, 2026-10-11): at `Free`, on a machine with a fused
multiply-add, the loop tree's rewritten graph holds `_madd`, rounded as the machine rounds. The assertion did not go red
on the x86 test builds, which have no `-mfma` and so round `_madd` twice, the unfused answer; it did on arm64, which
rounds once (2026-10-20). `ordering_test` now holds what is true on both: the walk answers the unfused value at `Free`;
the rule fuses only at `Free` on a machine with the instruction (not at `Reproducible`, not on `default_machine`); and
the loop tree then answers the build's rounding (`kMultiplyAddIsOneInstruction`), the fused value on arm64.

### Reconciliation against 5.960e-08, and why that number is not between this tree's two tiers

M4.2 asks for the measured difference to be reconciled against the 5.960e-08 M1 measured. **That
number is a gap to the OLD TREE's baseline, not a gap between this tree's own two rungs** — the
condition says so outright — and it is already recorded and attributed, at
`tests/CMakeLists.txt:378` and in `TRACEABILITY.md` under M3.3: the old tree rewrites `a*b + c` into
a single fused multiply-add before generating code (its `opt/optimize.hpp` `contract_fma`) and
counts that as part of its exact baseline, so the whole gap is one fused multiply-add's worth of
rounding at samples reaching 1.273 — one single-precision step at that magnitude, which is one
rounding, as the explanation predicts.

**The two numbers are consistent, and the mechanism is the same one.** Both are the difference
between a rounded product and an exact one: 5.960e-08 is that difference at `float` and at a
magnitude near 1.273; 2^-54 is that difference at `double` and at a magnitude near 2^-26. **The old
tree contracts and this tree does not, which is the same fact seen from two sides** — across the
tree boundary it is a parity allowance, and inside this tree it is a freedom `Free` declines to
take. The allowance goes to zero on the day the assertion above goes red, and both places say so.

### What was deliberately not built

A reassociating pass, in order to have something to measure. That is a milestone's worth of
optimiser built to satisfy a condition that anticipates its absence in as many words — and it would
be building an optimisation because a test asked for one, which is the wrong order.

### The 2-D unfold windows two OUTER axes and interleaves the window axes (milestone M5, S3f)

`_unfld2d` is the old tree's `Window2d`: a 2-D sliding window. The old tree windows the **innermost
two** axes and lays the result out grouped — `[.., H, W] → [.., H_out, W_out, Kh, Kw]`, the two frame
counts together and the two window axes together at the very innermost. This tree cannot copy either
choice, for the reason `_slice` and the 1-D `_unfold` could not (finding S3e-CRUX): **the innermost
axis here is the streamed axis (time), and it stays whole.** A window over time would make an output
sample read a different input sample — a cross-sample read this tree's same-time width model has no way
to express, and the plan (M5-B) forbids as a second execution model.

So `_unfld2d` here windows **two distinct OUTER element axes** `ax1 < ax2`, each framed exactly as the
single `_unfold` frames its one — extent `L_i` becomes `N_i = (L_i - K_i)/H_i + 1` frames with a window
axis of `K_i` beside it. Because each window axis is inserted immediately after its own frame axis, the
two are **interleaved**: `[.., N1, K1, .., N2, K2, .., streamed]`, not the old tree's grouped
`[.., N1, N2, K1, K2]`. This is the same divergence `_unfold`/`_slice` made, applied to two axes at
once — a window axis sits beside its own frame axis, which is this tree's 1-D convention generalised
rather than the old tree's spatial-window layout copied. A run reads output element `(f1, k1, f2, k2)`
from source `(f1·H1 + k1)` along `ax1` and `(f2·H2 + k2)` along `ax2`, recomposed on the source extents.

**M5.7's per-axis rejection is native to this shape.** `_unfld2d` rejects only the axis it cannot
frame — a symbolic axis it cannot yet size, a window wider than its extent — never a graph that merely
declares a symbolic dimension somewhere else, because each axis is framed on its own.

**LITERAL ONLY at S3f-1.** A symbolic source on either windowed axis is refused ("bind it first"), the
`_slice`/S3e-1 precedent; the DERIVED 2-D frame count — two `relate()` calls, one per axis, reusing
S3e-2's machinery — is S3f-2. Extends **finding S3e-1-HASH**: the new payload fields `axis2`, `window2`
and `hop2` join `window`/`hop`/`frames` as view fields not yet conservation-hashed; the deliberate full
re-record before M5's boundary closes them together.

### `_fft` is an intrinsic boundary: its shape is modelled, its arithmetic is deferred to M13 (milestone M5, S4a)

The plan (S4) says this tree owes the FFT decision explicitly — *what is inside the boundary, and what
a graph is allowed to assume about it*. Decided:

- **Inside the boundary, NOT modelled at M5:** the DFT arithmetic — the numbers a transform produces.
  Deferred to **M13** exactly as the old tree defers it (its `check_complex_compute` gates native
  complex on FFT-as-node/M13). At M5 there is no `compute()` arm and no runner arm that produces a
  spectral value.
- **Modelled at M5:** the SHAPE the transform imposes — a forward real transform sends an innermost
  real length `N` to its half-spectrum `2·(N/2+1)`, an inverse recovers `N` — and the static-length
  precondition. A graph carrying an `_fft` may be built, shape-inferred, unit-checked and PLANNED; it
  may not be RUN to numbers.

**Therefore the element meaning is `ElementKind::Boundary`, a DECISION and not the half-done `None`.**
`None` is how `runtime/compute.hpp` says a row's writing has not arrived yet; `Boundary` says the
writing is a backend this tree defers, the same stance `ShapeRule::Opaque` takes for a shape. It
satisfies the meaning-coverage gate (`Boundary != None`), and the three runners' `Boundary` arm
`fail()`s with a clear refusal rather than a silent wrong number. That a run refuses is shown from the
interpreted walk in `spectral_fft_test.cpp`; that the graph still compiles and plans in both worlds is
`both_ways_test.cpp`'s reading R2 — legitimate for `_fft` because M5.3's numeric twin is `_ovadd`
(S4c), which meets the strong reading with real numbers, so R2 here is not the M5-B-forbidden dodge.

**rfft/irfft only, and `n` is the real length.** The attribute carries the real transform length and a
direction, no `real` flag — this tree does real transforms only at M5, as the old v1 does. The length
is load-bearing on the INVERSE: `2·(N/2+1)` is not injective in N (a real 4 and a real 5 both give 6
planar numbers), so the length a `_fft` recovers has to be the declared `n`, not read back from the
input extent. `fft_axis_shape` reads `n` in both directions for symmetry.

**Its precondition is the WHOLE transformed value (`ExtentsKnownNow`), a deliberate strictly-stronger
reading.** The plan names `Precondition::ExtentsKnownNow`, whose reader (`check_preconditions`) holds
the whole operand shape static — stricter than the "innermost extent literal" the shape strictly needs.
The two readings are ordered (whole-shape contains innermost-only), so the task's rule takes the
stronger one, and it is the honest one: the M13 backend is built from a static length and a symbolic
OUTER axis (a batched transform bound later) is a graph shape this tree does not yet run. It is refused
by name, and recorded as an exclusion row (`docs/limitations.md`) with the workaround — bind the extents
— rather than left as a silent narrowing. `_fft`'s units are `Structural` (the shape machinery decides,
the relation is not consulted), matching the old row's `Structural`.

### `_pack` is a join of identical operands, and it reuses the join's shape rule rather than minting its own (milestone M5, S4b)

*(`_pack` folded into `_concat` in Decision 60.)*

`_pack` stacks two equal planes along one outer axis, `[.,N]+[.,N] -> [.,2N]` — the old tree's `pack`,
which lays a spectrum's real and imaginary halves side by side. It is `_concat` restricted to IDENTICAL
operands, and that restriction, not a new shape law, is the whole of what makes it its own operation.

**It reuses `ViewShapeRule::ConcatAxis` — the one place two `ViewKind`s share a rule, and a deliberate
one.** A join's output axis is the SUM of its two operands' extents; with equal operands that sum is
`2N`. So a pack's shape is a concat's shape, computed by the same `concat_axis_shape`. Minting a
`ViewShapeRule::PackedPlanes` that routed to the same computation would be a column value with no
distinct reader — the M2.6 trap this tree writes coverage checks against, and the deeper principle
("one meaning, one reader", M3) outranks the incidental pattern that every prior view happened to have
its own rule. Each earlier view had a genuinely different shape law; `_pack` is the first that does not.
`op_rules_test.cpp` asserts the sharing directly (`view_shape_rule(ViewKind::Pack) == ConcatAxis`) so it
reads as intended, not as an oversight.

**What is genuinely `_pack`'s own is a single refusal, and it lives where refusals live — a check, not a
rule column.** `_concat` lets its joined axis differ between the two operands; `_pack` doubles ONE plane,
so it requires the joined axis equal too. `sema/concat_checks.hpp`'s `pack_operands_match_on_the_joined_axis`
adds exactly that one check on top of `check_concat_attribute`, and `check_join_attribute` dispatches the
two joins by kind. Its element meaning is `ElementKind::Joined`, the concat reader `concat_read` verbatim
(the split falls at `N` because the operands are equal), so no runner arm changes and the three runners
agree bit for bit on the corpus's packed contribution (`both_ways_test.cpp`).

**Its units are `Structural`, the old row's `None`.** The old `pack` row carries `None`; this tree spells
that `Structural`, meaning the shape machinery decides and the dimension relation is not consulted — a
separate `None` value would be a column nobody reads, the same M2.6 trap. A symbolic joined axis is
literal-only at S4b (the `_concat`/`_padc` precedent), recorded as an exclusion row in `docs/limitations.md`;
a symbolic axis ELSEWHERE, the same symbol in both operands, passes straight through.

### `_ovadd` is a scatter-add with its own element-meaning kind, and its output length is derived, not carried (milestone M5, S4c)

`_ovadd` is the ADJOINT of `_unfold`: it reassembles a windowed signal, taking `[.., F, K]` — F frames of
K taps — back to one axis `[.., out_len]` with `out_len = (F-1)*hop + window`, each output position the SUM
of the overlapping frames' taps. It exists to serve the spectral path (reassembling frames after an inverse
transform) but is a pure reindex-and-sum and, unlike `_fft`, runs to numbers here.

**It is a full operation with `ShapeRule::OverlapAddAxes`, not a view.** Its two innermost element axes
COLLAPSE into one — a shape law no `ViewShapeRule` expresses (every view keeps or resizes axes one at a
time; none folds two into one) — so it carries its own attribute (`OverlapAddAttr{window, hop}`) and its
own shape rule, paired the way `_fft`'s `SpectralAxis`/`Fft` are.

**Its element meaning is a NEW kind, `ElementKind::Scatter` — not a reuse of `Fold`.** The plan's first
note suggested reusing `Fold`. On grounding it against the tree, the fold machinery
(`collapse_of`/`fold_of`/`contribution`/`folded_element`) is a FIXED-fan-in collapse over NAMED axes;
overlap-add is a WINDOWED, position-dependent scatter-add whose contributor set VARIES per output element.
Forcing it into `Fold` would need either operation-code branches inside the three runners
(`gate_no_per_op_branches`/the runner discipline forbids them) or bending `folded_element` to a relation it
was not built for. So it got its own kind — exactly as `_fft` got `Boundary` rather than deform an existing
kind. The three runners switch on `ElementKind` and name no operation; a fifth case is exhaustive-switch
-checked. Its indexing lives in `runtime/overlap_add_element.hpp` (compute.hpp and elements.hpp were both
at their 600-line limit, and an overlap-add is a concern of its own — the unfold adjoint).

**Its output length is DERIVED by the solver, carried by no attribute.** The old tree GIVES `out_len` as an
attribute; this tree derives it. When the frame count F is a literal, `overlap_add_shape` computes
`out_len = (F-1)*hop + window` directly. When F is a SYMBOL (an `_unfold` derived it from a bound-later
length), the shape function hands the source shape back — exactly as `padded_axis_shape` does for a
still-symbolic padded axis — and the solver resolves F to a literal the moment its source is bound; the next
sweep then computes a literal `out_len`. So nothing in interpret/execute/generate ever sees a symbol, which
is the whole of what M5.4's "symbolic frame count executed" needs, satisfied by the SAME resolution
`_unfold` already relies on rather than by a fresh `relate()` call. Carrying no `out_len` attribute means
nothing can disagree with the derived value — the M2.6 trap a given-and-then-checked length would be.

**An UNBOUND frame count is refused, not deferred — the one asymmetry with `_unfold`.** `_unfold` DECLARES
its frame-count symbol and the derived-extent algebra tracks it, so an unfold over an unbound axis is a
well-formed symbolic graph. `_ovadd` is a CONSUMER of a frame count, not its declarer, and needs F resolved
to derive `out_len` — so an `_ovadd` whose frame count never resolves has no output length and is refused
by name (`sema/overlap_add_checks.hpp`, "frame count is not known yet; bind it or derive it from an
`_unfold`"), the `unfold_fits_the_source` precedent. A BOUND symbolic F resolves and executes (the M5.3/M5.4
twin, `symbolic_test.cpp` graph six). The consequence for output length: a round-trip
`_unfold`->process->`_ovadd` reconstructs `(F-1)*hop + window`, which for an original length that is not
COLA-exact is shorter than the original — recorded as a limitation with the `_padc`/`_slice` workaround.

**The cost is LINEAR.** Each output element iterates ONLY the `<= ceil(window/hop)` frames that overlap it,
never all F; the source elements read sum back to the operand's F*K. A scan of every frame per output
element would be `out_len * F`, super-linear in `out_len` — the cost SHAPE amendment 03 names — so the
reader computes the overlapping frame range in O(1) instead.

**Structure (amendment 06).** Adding a fifth `ElementKind` and its branch pushed `runtime/compute.hpp` past
600, so `fold_step`/`fold_finish` moved to `runtime/fold_arithmetic.hpp`. This is a genuine concern split,
not number-shaving: it is the SAME split `math_cores.hpp` already is — `compute.hpp` NAMES which fold a node
carries (`fold_of`/`collapse_of`/`contribution` read the attribute), and the arithmetic of folding, keyed on
`ReduceOp` and naming no operation, lives beside the other arithmetic kernels, the way the exponential
series does rather than sitting in `exponential_of`. `shape_of_node`'s `ByViewKind` arm — the one shape rule
whose case carried logic rather than a route — became the named helper `view_node_shape` for the same
reason, keeping the dispatch uniform: every non-trivial rule routes to a shape helper.

## `_order` — the compare-exchange order statistic (M5, S5)

**`_order` is a compare-exchange NETWORK, not a monoid fold, and it earns its own `ElementKind`.** Order
statistics (sort, top-k, quantile) were long excluded as "data-dependent ordering that fits neither the
affine nest nor any fixed schedule" — true of comparison sorting with data-dependent control flow, FALSE of
a sorting network. For a compile-time extent L a Batcher odd-even mergesort is a FIXED sequence of branchless
compare-exchanges (`support/order_net.hpp`): a schedule-as-data object, identical on every input, addressing
still affine. So `_order` runs the SAME comparators in the SAME order interpreted and generated, bit-exact —
the property M3 keeps. Its output at one rank is a joint function of ALL L operand elements along the axis,
produced by the network — not a fixed-fan-in monoid collapse (`Fold`), not a windowed scatter-add
(`Scatter`), not a spectral boundary. So it is a NEW `ElementKind::Ordered`, exactly as `_ovadd` earned
`Scatter`. Its indexing seam is `runtime/order_element.hpp` (compute.hpp/elements.hpp both at 600); the step
iterates GROUPS (one choice of every non-ordered axis), gathering each group's L values once and running the
pruned network once, so cost is linear per group rather than the L× a re-sort-per-output would be
(amendment 03's shape argument, the reduce path's own batch decomposition reused).

**`CostClass::Software`, not `Cheap` and not the old tree's `Hw` (finding S8-COST).** An order network is
expensive — O(L·log²L) comparators — and it is COMPUTED BY THIS TREE (no library, either world), so no
accuracy setting changes what it costs: it is tier-invariant, which is `Software`, joining `_sqrt`/`_rsqrt`.
It is not `Cheap` (a copy-shaped view's price) and it is not `Tiered` (no coarser core selects in). The old
tree's `Hw` conflated "one machine instruction" with "no coarser version"; nothing here is one instruction,
so `Software` is the honest label — the exact misread S8-COST exists to prevent.

**`median` is `Quantile{q = 0.5}` sugar, resolved in the frontend, not a fourth `OrderOp`.** The
numpy-'linear' quantile at 0.5 IS the median — an exact rank for odd L, the lerp of the two middles for even
L — so a `Median` enumerator would be a value nobody's shape or store routes on distinctly. Three `OrderOp`s
(Sort, TopK, Quantile) are the whole set; `order_keeps_axis` routes shape and store, never a hand ternary.

**The output contract matches the old tree, confirmed by reading it at `c4012b8`.** Sort is ASCENDING and
shape-preserving. TopK keeps the k LARGEST sorted DESCENDING (output rank 0 is the max), the ordered axis →
k. Quantile DROPS the ordered axis (rank falls by one) to one value: the exact rank when the interpolation
weight is zero (bit-exact, no arithmetic) else `(1-w)·asc[lo] + w·asc[hi]`. A descending sort is
`flip(sort(x))` — `_flip` exists (S3g) — so no descending `OrderOp` is needed. The one arithmetic the family
does is the quantile lerp; it is one writing (`order_output_value`), so the three runners cannot select
differently.

**The ordered-extent ceiling is an override the diagnostic NAMES (standing rule S-6).** The network is a
fixed-capacity compile-time object, so L is bounded by `kMaxOrderedLength` (= `HEINZEL_MAX_ORDER_EXTENT`,
256). That is a decision, not a wall: over the ceiling, `order_checks.hpp` refuses by name and the message
names `-D HEINZEL_MAX_ORDER_EXTENT`, so a graph that wants a bigger network raises it deliberately (and
consistently across every TU — an inconsistent value would size the network differently in two of them).
`kMaxValues`/`kMaxNodes` stay 320; a group of ≤256 ordered values fits the element budgets.

**A symbolic ordered extent is EXCLUDED, unlike `_ovadd`'s executed-symbolic frame count — and that is
parity, not a regression.** `_ovadd`'s M5.4 headline was a specific one: a frame count derived by an
`_unfold` resolves at the width stage and executes. Order statistics have no such analogue in the old tree
and no M5 condition asks for one, so `_order` carries `Precondition::ExtentsKnownNow` (`_fft`'s) and a
symbolic extent anywhere on the node is refused first, by the precondition — the conservative reading the
milestone wants, and a comparison the old tree can be checked against.

**Structure (amendment 06).** Adding a 53rd operation pushed `sema/analyze.hpp` and
`sema/attribute_checks.hpp` — both at exactly 600 — over the limit, so the VIEW SHAPES (`viewed_shape` and
the axis-resizing helpers `strided_range_shape`/`padded_axis_shape`/`concat_axis_shape`, plus the
`is_a_permutation_of_the_rank` predicate they share with the view checks) moved to the new
`sema/view_shape.hpp`. This is a genuine concern split, not number-shaving: the view shapes were the LAST
operation family still shaped inline while every other has its own `_shape.hpp`
(`spectral_shape`/`overlap_add_shape`/`order_shape`), and moving them completes that pattern. `view_shape.hpp`
is added to `gate_no_per_op_branches` (like `framing.hpp` before it), so the split does not narrow that gate
to the half that stayed in `analyze.hpp` — the same reasoning the attribute-checks split recorded. The
both-ways corpus's wide contributions moved to `tests/support/axis_changing_contributions.hpp`, one concern
of their own separate from the base graph and the runnable entries. `vocab.hpp` and `op_rules.hpp` (at 580
and 592) came back under 600 by tightening the new prose, not by splitting coherent code.

**What the structure gate cannot measure, restated for `_order` (amendment 06, S6-4).** The brace-depth line
scan is approximate; it counts every line including comments, and it cannot tell a genuine concern split from
a number-shaving one — that judgement is the reviewer's, recorded here. The view-shape split above is the
former; the prose-tightening of `vocab.hpp`/`op_rules.hpp` touched only text added for `_order`, not existing
code.

## `_realize` — a materialisation barrier modelled as the identity (M5, S6a)

`_realize` is the first of M5's tail (`_realize`/`_tblread`/`_iota`) and the first of M5's fifteen that is
NOT axis-changing. The old tree's `Realize{13}` is a real O(buffer) copy that forces a value contiguous —
`Plan/archive/19` Lever 3b's transposed-input copy, and the barrier before an `_fft` or a reshape-of-a-view.

**At M5 it is modelled as the IDENTITY it computes, and the barrier role is deferred to M8 — on purpose.**
Value for value `out[e] == in[e]`, so its shape is its operand's (`ShapeRule::Passthrough`), its type its
operand's (`SameAsFirst`), its units `Same` (the old tree's row — a copy preserves the dimension), its
meaning `PerSample` (it hands its operand straight back in `runtime/compute.hpp`, adding a NAME to the
hand-back branch the delays share, not a branch — the M3.4 record). Because it is per-sample it needed no
change in any of the three runners: `walk_step`/`emit`/`execute` dispatch a `PerSample` row through the
generic per-sample path, and the identity is what `compute()` returns. So it is RUN both ways, bit-exact —
the strongest reading of M5.1, not R2 — reached in the both-ways corpus's arithmetic entry.

**What is NOT built here, and why it is a deferral rather than a gap.** A realize that could be fused away
would be pointless, so the scheduler must treat it as a barrier that may not be elided. That is a fact the
SCHEDULER reads, and the layers that would read it — relayout, fusion — are M8's ("the layers above the
core"), not M5's. M5 owes the operation: a row, a shape, a meaning, and a both-ways run. It has all four.
The barrier semantics arrive with the pass that would otherwise elide it, which is also the first moment a
test could show the barrier doing anything. Modelling it as the identity now makes that later pass a
strengthening, not a rewrite — nothing expressible today becomes unnameable. No exclusion-board row: the old
tree's `docs/limitations.md` has none for `realize`, and neither does this one.

**Cost `Cheap`, not `Software`.** One copy per element — a byte move, the price a copy-shaped view carries —
unlike `_order`'s computed network. `Precondition::None`: a copy works on any shape, symbolic extents and
all, so nothing about it requires extents known now.

**Structure (amendment 06).** The 53rd operation needed headroom `vocab.hpp` and `op_rules.hpp` did not
have — both were at exactly 600 — so S6-pre first split two genuine concerns out ahead of it:
`ir/numeric_setting.hpp` (the accuracy/ordering setting and its one-byte packing) from `vocab.hpp`, and
`ir/rule_columns.hpp` (the rule-column enums) from `op_rules.hpp`. Both are concern splits the files had held
since M4, not number-shaving: the setting model is orthogonal to the shape/op vocabulary, and the columns a
row is spelled in are separate from the table of rows and its coverage assertions. `_realize` itself added
only its code, row, hand-back line and tests, all comfortably under the limits.

## `_tblread` — a wavetable gather with its own element-meaning kind (M5, S6b)

`_tblread` is the second of M5's tail (`_realize`/`_tblread`/`_iota`) and the wavetable read the old tree
spells `table_read(phase, table, N)`: `out[e] = table[frac(phase[e])*N]`, linearly interpolated between the
two bracketing samples of an `[N+1]` buffer whose trailing sample is the wrap guard `table[N]==table[0]`.

**It is `MeaningRule::AxisChanging` with a NEW element-meaning kind, `Gathered` — not a per-sample row.**
The result has the phase's shape (`ShapeRule::Passthrough` of operand 0), so a per-sample row was the
tempting reading. It is the wrong one: one output element is a blend of TWO of the table's elements at a
DATA-DEPENDENT index, and `compute()`'s contract is three already-fetched numbers — it cannot fetch the two
table samples, because which two they are is not known until the phase is read. That is exactly the codebase's
own definition of `AxisChanging` ("one result element assembled out of many operand elements, no
sample-at-a-time answer"), so `_tblread` joins that family with its own kind, as `_ovadd` earned `Scatter`
and `_order` earned `Ordered`. `element_kind_of(op::TableRead) == Gathered`; `compute()` refuses it; the
three runners reach a dedicated `walk_gather_step` / `run_gather_step` / `Gathered` arm through the same
`element_kind_of` switch the other axis-changers use.

**An AxisChanging MEANING with a Passthrough SHAPE is not a contradiction.** `AxisChanging` is a claim about
what `compute()` can do; `Passthrough` is a claim about the result's extents. The pairings check
(`row_attribute_pairings_agree`) ties neither to the other, and `AttrKind::Table` is UNPAIRED — no shape rule
reads it, because the shape is the phase's — exactly as `AttrKind::Reduce` is unpaired. This is the first row
where the two columns are deliberately not the same fact, and it is worth stating so a future reader does not
"fix" a passthrough shape into an axis-changing one.

**The seam considered and rejected: widening the per-sample reader.** `_dline` is the SAMPLE-axis twin of
this — a data-dependent read resolved by `operands_of` in `ir/timing.hpp`, which pre-fetches near/far/fraction
and hands them to `compute()`'s `tapped_of`. Keeping `_tblread` per-sample the same way would have needed the
shared reader `at(k, back)` to also take a data-dependent ELEMENT override, a change felt across all three
runners' reader closures for the one operation that wants it. The `Gathered` classification disturbs the
shared per-sample seam not at all and fits the extension point the tree already has, so it was chosen.

**The blend is `_dline`'s, factored to `lerp2` and written once.** `table_lerp` on the old side spells the
interpolation `table[k]*(1-f) + table[k+1]*f`; `lerp2` (`runtime/table_gather_element.hpp`) spells it
`near + f*(far-near)`, the same last-bit divergence `_dline` already made and recorded — chosen so a
whole-index position reads the table sample to the bit, and so this tree's interpreted, scheduled and
generated gathers stay bit-exact with EACH OTHER (M5.1), which is what the both-ways corpus
(`a_table_read_contribution`) checks. `tapped_of` (for `_dline`) calls the same `lerp2`, so a table read and
a fractional delay cannot drift apart. The index derivation `table_tap` (`frac(phase)*N`, split into whole
and fraction, `frac` using the tree's own floor so a negative phase wraps into the table) is likewise the ONE
writing all three runners call — the `overlap_read` / `order_group_input` pattern.

**Given vs derived: `N` is given, the table's size is checked.** The attribute carries `N` (the resolution);
the `[N+1]` buffer size is not carried but CHECKED — `check_table_attribute` (`sema/table_checks.hpp`) refuses
`N < 1` and a table whose width is not exactly `N+1`, and a table whose size is a symbol resolved later (a
wavetable's extent must be known now, though the PHASE may be symbolic — hence `Precondition::None` on the row
and a dedicated size check rather than a whole-node extents gate). Each refusal is shown rejecting a
deliberately broken node in `tblread_test.cpp`, and the runners carry the same size guard for a hand-built
node that reaches them without the pass.

**No exclusion-board row.** `_tblread` is RUNNABLE (it computes numbers both ways), so like `_realize` it is
an INCLUDED operation, and neither this tree's `docs/limitations.md` nor the old tree's has a row for it.

**Structure (amendment 06).** The gather's arithmetic went into a new `runtime/table_gather_element.hpp`
(`table_tap` + the shared `lerp2` + the `table_size` attribute seam), and the sema refusals into a new
`sema/table_checks.hpp`, both for the reason `_order`'s and `_fft`'s did: `runtime/compute.hpp` and
`sema/attribute_checks.hpp` were near their 600-line limit, and a wavetable gather and its refusals are each a
concern of their own. `compute.hpp` gained only the `Gathered` enum value, its `element_kind_of` case, and a
call to the shared `lerp2`; it stays under the limit.

## `_iota` — the first arity-0 generator, and what it cost the rule-column vocabulary (milestone M5, S6c)

`_iota` is `out[e] = e`, an index ramp — the old tree's `iota(Shape, ElemType)` (`ir/ir.hpp:181` there,
`out[i]=i along the iterated axis`; `iota_lane(i)=i`, `backends/scalar_backend.hpp:53`). It is this tree's
FIRST arity-0 operation, and it extends the rule-column alphabet rather than only adding a row.

**A THIRD meaning shape: a generator, not per-sample and not a fold/rearrangement.** `compute()` answers
one element out of operand NUMBERS and is handed no index; `_iota` has no operands and its value IS the
position. So it has no sample-at-a-time meaning in `compute()`'s terms — classified `MeaningRule::AxisChanging`
— but it is neither a fold (nothing collapses) nor a rearrangement of an operand (there is none). It got a new
element meaning, `ElementKind::Indexed` (`runtime/iota_element.hpp`, `iota_value(e)=e`), the sixth runnable
kind, joining the three runners' element-kind dispatch beside `Gathered`.

**Arity 0 relaxed the coverage guard, and the enum sentinels prove it left no hole.** `every_row_is_complete`
rejected `arity == 0` because zero was the OpRules default standing in for "someone forgot to write the
arity". `_iota`'s arity really IS zero, so the lower bound is gone (`arity > 3` remains). A forgotten row is
still caught: it is default-constructed, so every OTHER column is at its `Unclassified` sentinel and rejected
there — arity was simply the one column that never had a dedicated sentinel. `op_rules_test.cpp` shows both: a
fully-default row still rejected, and a well-formed arity-0 row accepted.

**Declared shape and type: the pair only a generator needs.** No operand means neither shape nor element type
can be derived from anything flowing in, so both are stated in a new `IotaAttr{ shape, type }`. Two new rule
columns read it — `ShapeRule::Declared` (reads `iota.shape`) and `TypeRule::Declared` (reads `iota.type`) —
each paired with `AttrKind::Iota` in `row_attribute_pairings_agree`, in both directions like every other
attribute-reading rule. `TypeRule::Declared` is distinct from `FromAttribute` (which is hardwired to
`CastAttr::to`); a cast and a generator both state their type, but from different attributes.

**The extents-known refusal lives in the attribute check, not the precondition column.**
`Precondition::ExtentsKnownNow` is checked against an operation's OPERANDS (`check_preconditions` loops
`arity`), and a generator has none — so that column would be DEAD for `_iota`, a rejection that never fires.
The requirement is real all the same: an index loop needs the element count known, and a generator has no
source that could ever bind a symbol into its declared extents, so a symbolic one is unresolvable rather than
merely unresolved. It is refused by name against the declared shape in a new `sema/iota_checks.hpp` (with the
rank>=1 check, the old tree's own builder guard), shown rejecting a rank-0 and a symbolic-extent ramp in
`iota_test.cpp`. The row therefore carries `Precondition::None`.

**The value is the flat element index — a modelling difference from the old tree, recorded.** This tree puts
time on the OUTSIDE of the element loop (the streamed axis is innermost and iterated by the walk, not by the
element loop), so a `_iota` ramps over its ELEMENTS: `e` runs `0 .. numbers_per_sample-1`, the same at every
sample. For the rank-1-outer DSP case that is exactly the old tree's `[0, 1, 2, ...]`. The old tree's OTHER
use — a time ramp `frac(iota·inc)` for a constant-pitch oscillator — is NOT a spatial iota here: a running
sample counter is a different construct (time is outside the element loop), so that use is a modelling
difference rather than a value this iota reproduces. For two or more outer axes the flat index is this tree's
own element order (`runtime/elements.hpp`), a defined extension the old tree — which only ramps one axis —
gives no ground truth for.

**No exclusion-board row.** `_iota` is RUNNABLE (it generates numbers both ways), so like `_realize`/`_tblread`
it is an INCLUDED operation; neither tree's `docs/limitations.md` has a row for it.

**Structure (amendment 06), two commits.** `runtime/compute.hpp` stood at 599/600, so `_iota`'s
`ElementKind::Indexed` enum value and `element_kind_of` case would have overrun the gate. S6c-pre lifted
`rearranged_element` (the `Rearrange` meaning) into `runtime/rearrange_element.hpp` — the per-kind-header split
`order_element`/`overlap_add_element` already made — dropping `compute.hpp` to 558. The generator's value went
into a new `runtime/iota_element.hpp` and its refusals into a new `sema/iota_checks.hpp`, each a concern of its
own beside its peers; `compute.hpp` gained only the enum value and its dispatch case, and
`sema/attribute_checks.hpp` (also at its limit) only the include and a one-line dispatch.

## M5.6 — every passthrough-looking operation carries its rejection, made an explicit check (M5, S7)

M5.6 asks that every operation classified `Opaque` carry a precondition or an exclusion row reproducing
what the old tree rejected, and names the trap `01-DERIVATION.md` §1.5 records: an operation with a
passthrough-looking shape rule and NEITHER is not finished, because a rejection that lived inside an
old shape function is deleted the moment someone copies only the shape half, and resurfaces as an
assertion deep in the executor.

**The set, against the real table.** `ShapeRule::Opaque` has zero rows — the column exists so the first
unclassifiable operation is a decision, not an omission — so the Opaque clause is vacuously held and the
live half is the passthrough trap. `ShapeRule::Passthrough` has six rows, each grounded against
`c4012b8` (`git show main:include/heinzel/sema/analyze.hpp`):

- `_fdelay` / `_delay` (`AttrKind::Delay` / `AttrKind::AxisDelay`) — the old tree rejects depth < 1 and
  an axis out of range (`:1000-1005` there); here `state_depth_is_a_constant` and the lag-axis check in
  `sema/attribute_checks.hpp`.
- `_recur` (`AttrKind::Recurrence`) — the old tree rejects an undelayed (lag-0) self-dependence
  (`:1253`); here the "feedback loop with no delay in it" refusal in `schedule/plan.hpp`.
- `_dline` (`AttrKind::SignalledDelay`) — carries `signalled_ceiling_is_a_distance` (a negative reach
  ceiling refused) in `sema/attribute_checks.hpp`. The old tree's frac-delay rejection was a different
  one — the two operands must share the element type (`:720` there); `_dline`'s faithfulness to the old
  tree was established at M4, and S7 asserts only that it is not a bare passthrough.
- `_tblread` (`AttrKind::Table`) — carries `check_table_attribute` (N ≥ 1, an N+1-wide table, a literal
  table extent), already shown rejecting deliberately broken inputs in `tblread_test.cpp`; its old-tree
  grounding is S6b's.
- `_realize` (`AttrKind::None`) — the ONE bare passthrough. The old tree lists it among "pure data
  movement of a complex buffer is allowed" (`:704-706`) and carries no `docs/limitations.md` row: it
  rejects nothing. This is §1.5's legitimate contrapositive — an operation that rejected nothing
  carries nothing and is finished.

**Why it is a check and not an inspection.** Every passthrough operation except `_realize` carries a
dedicated attribute (`AttrKind != None`), and a dedicated attribute is dispatched to a dedicated
attribute or graph check reproducing the old rejection. So the DATA already separates "carries a
rejection" from "bare identity": a passthrough row with `Precondition::None` AND `AttrKind::None` is the
trap shape, and its only member is `_realize`, declared an identity on an allowlist naming it directly.
`tests/unit/passthrough_audit_test.cpp` static-asserts the whole table is accounted for, is SHOWN
objecting to a fabricated bare passthrough, asserts the allowlist is minimal and cannot go stale (if
`_realize` gained an attribute or precondition the entry fires), and records that the `Opaque` count is
zero so the first such row is a deliberate act.

**The proxy's blind spot, stated plainly** (as the other gates state theirs). "Carries an exclusion" is
read as "carries a dedicated attribute". The proxy proves an operation is not a BARE passthrough; it
does NOT prove the specific rejection is FAITHFUL — an attribute check that existed but rejected nothing
would pass. That faithfulness is each building milestone's own both-ways and shown-rejecting work (M4
for the delays and the recurrence, M5-S6 for `_tblread`), not this gate's. It is the same class of blind
spot `one_writing_per_operation` records for function-pointer dispatch. The audit lives in `tests/`
because it must name `op::Realize`, which `gate_one_writing_per_operation` forbids under `include/`.

## `_reshape`'s refusal is lifted from the executor into the analysis pass (milestone M5, S10b)

Operator decision 2026-09-01 14:08 (`operator-decision-reshape-lift.md`), in reply to the S8 finding:
the rewrite accepted an element-count-changing `_reshape` at analysis time and refused it only later, as
a hard `fail()` deep in the executor (`same_elements_in_order`, `runtime/elements.hpp`). The old tree
refuses it AT ANALYSIS with a clean diagnostic (`git show main:include/heinzel/sema/analyze.hpp` around
"reshape changes the element count"). §1.5 / R3 warns against exactly this — a rejection living as an
executor assertion instead of a pass diagnostic. **Decision: lift it.**

**What the check is.** The innermost axis is TIME (the sample loop); a value's ELEMENTS are the numbers
OUTSIDE it (`numbers_per_sample` = product of extents except the last). A valid reshape re-cuts the
element axes among themselves, keeping BOTH the element count and the streamed axis whole. The new
`sema/reshape_checks.hpp` `check_reshape_attribute`, routed from `check_view_attribute` on
`ViewKind::Reshape`, refuses the two ways it can fail:
- **count-changing** (`{2,3,T}` -> `{6T}`, six numbers per sample becoming one) — a reframing over time,
  what `_unfold`/`_ovadd` are for. "`_reshape` changes how many numbers a sample has, from N to M".
- **samples-changing / streamed-axis re-cut** (`{8,4}` -> `{8,3}`, eight both ways) — the narrower
  sub-case the S8 finding flagged as unverified. `numbers_per_sample` is UNCHANGED, so the executor
  count check never saw it; it re-cuts the streamed axis, which is time. Now refused BY NAME in the pass:
  "`_reshape` re-cuts the streamed axis; that is time and stays whole". This RESOLVES the sub-case — it
  is a pass diagnostic, not a "may or may not".

A symbolic width on either side is not this check's to refuse — `numbers_per_sample` returns
`kWidthNotKnown` and `everything_is_known` answers for such a value by name (the check defers).

**The executor assertion STAYS as the hand-built-graph backstop.** `same_elements_in_order`'s `fail()`
still refuses the same bad reshape built by hand and never run through the pass — the `permuted_element`
precedent (pass refuses by name AND the run refuses a graph that skipped it, since three runners reach
it). The pinning test (`rearrange_reshape_test.cpp`, split out of `rearrange_test.cpp` at S10b to hold
reshape's own concern under amendment 06) asserts BOTH: the pass names `_reshape`, and the executor
still throws for the hand-built graph.

**Blind spot.** The check reads `numbers_per_sample` and the streamed-axis extent of the resolved
operand and result shapes. It proves a reshape that reaches the pass conserves its elements; it does not
prove a reshape assembled directly against the executor did — that is what the retained backstop is for.

**Two incidental collateral test updates.** `inference_test.cpp` (attribute-dispatch shape resolution)
used `{4,8}` -> `{32}` and `units_test.cpp` (units propagation, and a units-disagreement rejection) used
`{4}` -> `{2,2}`; both are count-changing reshapes the lift now refuses at analysis before those tests'
own subject runs. Each was moved to a VALID reshape that still exercises its actual subject (`{4,8}` ->
`{2,2,8}`; `{4}` -> `{1,4}`) rather than converted to a rejection — the rejection is covered in
`rearrange_reshape_test.cpp`. This surfaced a latent contradiction: those tests only passed pre-lift
because analysis had not yet refused a reshape the executor already did.

## M5.9 — the exclusion board asserted as a whole, oracle O9 (milestone M5, S9)

`tests/unit/exclusion_board_test.cpp` closes M5.9: each of the old tree's twenty data rows
(`git show main:docs/limitations.md` at `c4012b8`, the table body `:18-37`, counted rather than quoted)
is dispositioned as exactly one of **excluded** (failing at a named point), **unrepresentable** (a kind
of excluded: no node spells the excluded thing, so the guard is that the spelling stays absent), or
**not applicable at M5** (the mechanism it attaches to is not built here yet, with the milestone that
makes it live named). There is no **implemented** row — every one is a genuine exclusion the old tree
also made, which is itself the finding that nothing here quietly re-admitted an excluded operation.

The board is asserted as a WHOLE: the count is fixed at twenty (a row that silently vanishes drops it
below twenty and fails the build — the single most dangerous parity break, because nothing else in a
build notices a rejection that quietly stops rejecting), every row must carry a disposition, and each
excluded row's refusal is re-pointed at its deliberately-broken input so the guard flips the moment the
pass starts accepting it. Both directions were shown objecting before the commit: a dropped row fails
the count assertion, and a refusal "made to pass" (a valid slice handed to the negative-stride guard)
fails row 2's assertion.

**Blind spot.** The gate proves a rejection EXISTS and still fires; it does NOT prove the rejection is
FAITHFUL to the old tree's exact behaviour — that faithfulness is each operation's own both-ways and
shown-rejecting work (`view_inference_test`, `order_test`, `spectral_fft_test`, `symbolic_test`). For
the unrepresentable rows the guard is a structural assertion that the spelling stays absent — a changed
field type (`OrderAttr::k`/`q`), a grown attribute (`RecurrenceAttr`, `DelayAttr` by `sizeof`), or a
newly named operation (`_gather`/`_scatter`/`_index`, scanned in `kOpTable`) fires it; an enumerator
APPENDED past the recorded maximum of an enum (`ElemType`, `OrderOp`) is not detected here, and the
operation tests that switch exhaustively on those enums are that backstop.

### FINDING S9-NEST — board row 14 (contraction axes over kMaxAxes) is not applicable at M5, deferred to M6

The old tree refuses a contraction whose output rank plus contracted-axis count exceeds `kMaxAxes` in
`lower_stage_nest` (`nest_lower.hpp`) — a nest-LOWERING refusal. No nest lowering exists at M5 (there is
no `lower_stage_nest`/`nest_lower` in the tree). What IS enforced here is the shape-level cap: a
contraction whose RESULT exceeds `kMaxRank` is refused by the `Shape` constructor (board row 16). The
nest-capacity refusal is a code-generation object that **M6** — the generated code and the machine model
— builds, so row 14 is dispositioned not-applicable-at-M5 with M6 named, not forced into a shown-failing
check for a site that cannot exist yet. The plan's S9 disposition table expected an existing site to
cite; verification found it absent, and the honest disposition follows the verification.

### FINDING S9-EDGES — board row 15 (symbolic derived/kernel edges) has no single residual-edges list here

The old tree kept a single "residual-edges list" enumerating every derived or kernel edge on which a
symbolic extent is refused. This tree does not: its derived-extent algebra (`plan/M5-STEPS.md` S1 /
M5.2) DEFINES what a symbolic edge may be — an affine term over one source extent — and everything
outside it is refused by the `Precondition::ExtentsKnownNow` rows (`_fft`, `_order`) and by the
derived-extent OBLIGATION recorded at bind time and checked when the symbol is bound (M5.2, shown both
ways in `symbolic_test`). Those preconditions and obligations ARE the enumeration; row 15 is excluded
and shown failing through them (`spectral_fft_test`, `order_test`, `symbolic_test`), not through a
residual-edges list this tree never grew.

## Decision 12 — the lane back ends: two shapes, one of them parameterised, not four back ends

Milestone M6.2 asks for a number of lane back ends: "four, matching `05-Parity-Inventory.md` §5.4,
**or** fewer with the reduction recorded here." Two things are recorded here, because verification of
the exit condition against the tree turned up a finding as well as a decision.

**The count is two shapes, not four.** A lane back end is a generator that declares a width and is
handed a whole lane of that many result elements at once (`codegen/generate.hpp`, `codegen/lane.hpp`).
There are exactly two GENUINELY DISTINCT shapes at M6:

- the **scalar** shape (`ScalarGenerator`, and every generator predating M6): width one, one `step`
  per element;
- the **vector** shape (`VectorLane<W>`): width W, a `lane` handed all W elements at once.

`VectorLane<2>`, `VectorLane<4>` and `VectorLane<8>` are the SAME shape at three lane sizes, not three
back ends. Minting them as separate back ends to reach four would be exactly the manufacture the plan's
S3 note and amendment 06 warn against — "splitting something coherent merely to lower [or raise] a
number is itself a finding." The width is a parameter of one vector shape, and `tests/unit/lanes_test.cpp`
is the evidence rather than the assertion: on an eight-wide graph the scalar back end and the vector
shape at widths two, four and eight all produce the same workspace bit for bit, and each width's
`MisLane` twin is shown differing, so each width's lane path is proven to have run rather than fallen
wholly to the scalar remainder.

**What would make a third shape genuine.** Not a fourth width — a different IMPLEMENTATION: arithmetic
that is not `compute()` per lane (a real SIMD core, a fused reduction), whose difference from the scalar
answer is then attributed at the looser tiers exactly as the fold-precision and old-side differences
already are. That is the instruction-set registration surface M6.3 (S4) exercises — a back end supplied
as a type, in a test, with no shared-header edit. The reference `VectorLane` deliberately takes the
seam without taking that difference, so it is the fixed point O11 measures the others against.

### FINDING M6-PARITY-INVENTORY-ABSENT — §5.4's "four" has no anchor in this plan set

M6.2 cites `05-Parity-Inventory.md` §5.4 for the number four. That document is **not present** in the
authoritative plan set (`/home/user/workspace/plans/heinzel-milestones/`), which carries `05-RELEASE.md`
and no parity inventory; the reference is inherited from the older plan the milestone plan was derived
from. So the "four" it names cannot be checked against anything here, and the exit condition's own
alternative — "fewer, recorded in `docs/decisions.md`" — is the branch that applies. Recorded rather
than silently taken: the reduction to two shapes stands on the reasoning above, not on a spec row this
plan set never carried. The "parity row updated in the same commit" clause has no row in this tree to
update; if a parity inventory is ever added at release (M10, `05-RELEASE.md` §5.4 leaves the CI/matrix
shape to M10), §5.4 should record two shapes and the width parameter, not four back ends.

**Blind spot.** O11 here shows the two shapes agree over the parity corpus's elementwise steps at four
widths total (one scalar, three vector). It does NOT exercise a lane over a recurrence-defining step
(`lanes_test.cpp` says so) — that path reuses the scalar path's `put_per_sample` and is correct by
construction, not shown — and it does not exercise a back end whose arithmetic actually differs from
`compute()`, because none exists until the instruction-set seam is used at M6.3.

## Decision 13 — the first rewriting pass, and why it is canonicalisation and not an optimiser (M6.5, M6.6)

*(The rewrite went in Decision 60.)*

M6.6 asks for "a pass that rewrites the graph before scheduling, with its decisions in O5's corpus."
M6.5 asks that analysis be idempotent, because M6 is the first milestone with a pass that re-runs it.
Both are built; what is recorded here is what the rewrite IS, what it deliberately is not, and a
finding about the frontend that verifying M6.6 turned up.

**The rewrite is commutative-operand canonicalisation, and nothing more.** `opt/rewrite.hpp` puts the
lower-numbered value first in every commutative operation. That is the smallest change that is still a
rewrite, and the choice is deliberate against two larger ones:

- **Not fusion or reassociation.** The old tree rewrites `a*b + c` into a fused multiply-add
  (`opt/optimize.hpp` `contract_fma`), and "What was deliberately not built" under Decision 11 already
  records why this tree does not: fusing MOVES numbers — it is a `Free`-tier freedom, one rounding's
  worth at the biquad's magnitude — and building the reassociating pass to have something to measure is
  a milestone's worth of optimiser anticipating a condition that names its own absence. Canonicalisation
  moves NO number, which is M6's governing invariant: the emitted program may change, the numbers may
  not. `_add` and `_mul` are marked commutative only because IEEE addition and multiplication are
  commutative bit-for-bit; associativity is not, so the pass says nothing about `a+(b+c)`.
- **Not common-subexpression elimination with renumbering.** CSE that drops a node needs the graph
  rebuilt in a new numbering — which is exactly `tests/compile_time/canonical_renumber.hpp`, the
  walk-from-outputs candidate measured but NOT adopted for open question Q-2, and whose dead-node
  elimination is a behaviour change reserved to the operator. Adopting it here would be deciding Q-2.
  Canonicalisation is in place: it changes `node.inputs`, never the value count, so no reference dangles.

**It is table-driven, so it names no operation.** `gate_one_writing_per_operation` allows only three
files under `include/` to spell `op::Something`, and the rewrite is not one of them. Which operations
commute is therefore the operand-order column of the rule table (`ir/rule_columns.hpp`,
`Commutativity {Unclassified, Ordered, Commutative}`), sentinel-checked in `every_row_is_complete`
like every other column — a forgotten row reads as `Unclassified` and fails the build, not as
`Ordered` by the enum's zero. This is the same discipline the cost column's own zero-value trap taught
(Decision 7).

**M6.5 — analysis is idempotent, and it retires R-S5.** `unit/idempotence_test.cpp` asserts
`infer(infer(g)) == infer(g)`: the strong form on a pre-inference graph (including a bound-symbol
biquad, which exercises `resolve_through_solver` where a residue would make the second run differ),
identity on every already-resolved corpus graph, and the check shown objecting against a pass that
drifts on every run. This is what makes re-running analysis after a rewrite well-defined; for THIS
rewrite the re-analysis is in fact a no-op, because exchanging commutative operands changes no shape,
type or unit — which `rewrite_test.cpp` shows rather than assumes.

### FINDING M6-EVALORDER — the frontend's operand order is evaluation-order-dependent, and this is a face of Q-2

Verifying M6.6 against the corpus turned up that the frontend-built filter graphs carry NON-canonical
commutative operand order: the biquad has three `_mul`/`_add` nodes whose operands are the higher value
first, the comb two (measured by a throwaway probe over `corpus_entry`). The cause is that `finish()`
numbers a value by the order the compiler EVALUATED the parts of an expression, so `c * y.previous(1)`
becomes `_mul(c, yp1)` or `_mul(yp1, c)` at the compiler's whim. That is precisely open question Q-2
(construction order depending on evaluation order) seen at the granularity of a single node, and the
canonicalisation rewrite is a local, checked mitigation of it — two spellings of one commutative
expression become the same graph.

**The rewrite is deliberately NOT in the default `compile()` pipeline.** Turning it on would
renormalise the operand order of those filter artifacts, which is a genuine change of a recorded
decision and would require re-recording entries 0, 1, 2, 3 and 6 of the conservation corpus in the same
commit, with the reason stated (their graph and program numbers would move; order and placement would
not). That is a deliberate act, not one to smuggle into a milestone whose job was to build the pass —
and Q-2's cross-translation-unit face, which the same evaluation-order dependence attacks across a
compile boundary, is a decision the operator reserved (Q-2, M6.7). So the pass exists and is proven
under the oracle on a dedicated entry (11, `rewritten(a non-canonical sum)`), and the default pipeline
is left alone until enabling it is decided on purpose.

**Blind spot.** The rewrite normalises operand ORDER within a node; it does not merge two nodes that
compute the same thing (no CSE), does not touch associativity, and does not address Q-2's cross-TU
identity question at all. Its conservation coverage is one entry whose sum is deliberately
non-canonical; a commutative operation this tree never builds out of canonical order would not exercise
it, which is why `rewrite_test.cpp` builds one on purpose rather than waiting for the corpus to happen
to contain one.

## M6.7 — Q-2 disposition: re-deferred, with a reason and the named deadline M8/M9

M6.7 requires open question Q-2 (construction order depending on evaluation order) to be answered, or
re-deferred with a reason and a named deadline. It is **re-deferred**, and this records the operator's
standing decision of 2026-08-25 to leave the mechanism alone — it is not a decision taken here. Q-2 is
a design call the operator reserved; M6.7 only obliges the tree to record the re-deferral with its
deadline, and it was reported to the origin at M6 entry (2026-09-02).

**Reason it may still be deferred.** Under clang — the only supported compiler — construction order is
consistent between compile time and run time, so the numbers are unaffected: zero differing samples
(`04-OPEN-QUESTIONS.md` Q-2). The failure has a *mechanism* at M6 (generated code is keyed on a graph's
identity) but no *observable* under the supported toolchain. Doing neither of Q-2's two fixes — the
canonical walk, or the enforced naming discipline — remains legitimate exactly as long as clang stays
the only supported compiler, which is the condition M10 removes.

**Deadline, named as M6.7 requires.**

- **M8 — the integrations.** A plugin adapter instantiates a graph in its own translation unit, which
  is the two-translation-unit case the M1 trigger names. Q-2 should be resolved here.
- **M9 — the compile cache. HARD deadline.** The cache keys on a graph, and M9.3 requires the keying
  shown both ways, so **M9 cannot close with Q-2 open.** If it is not resolved at M8 it must be at M9.

**What M6.6 changed about the picture, and what it did not.** The M6.6 canonicalisation rewrite
(Decision 13) is a *local* mitigation of Q-2 at the granularity of one node: it makes two spellings of
one commutative expression the same graph, which is why finding M6-EVALORDER records that the
frontend's operand order is itself evaluation-order-dependent. But it does not resolve Q-2. It is
deliberately not in the default pipeline, it addresses only commutative operand order (not the node
NUMBERING a canonical walk would fix, nor unreachable-node or input-order questions), and it says
nothing about whether two translation units agree on a graph's identity — which is Q-2's actual
failure mode. So Q-2 stays a reserved decision with the deadline above; the rewrite narrows the
surface, it does not close the question.

**Not decided here.** Which of the two fixes to adopt — the canonical walk (affordable at ~1.09x but
carrying the unreachable-node and input-order decisions with it) or the enforced naming discipline — is
the operator's call, live at M8 and forced at M9.

## Decision 14 — `gradients()` is a graph-to-graph transformation, and an opt-in feature is a leaf (M7)

Reverse-mode differentiation arrives at M7 as a feature under `include/heinzel/autodiff/`, and four of
its decisions are worth recording because they are places this tree's answer is not the old tree's.

**It is a Graph→Graph transformation, not a capture-time helper (finding F-M7-GRAPH2GRAPH).** The old
tree's `gradients()` runs while an arena is being built, differentiating `Expr` nodes as they are
captured. This tree has no such capture point: every structural operation is a hand-constructed
`Graph` node (`tests/support/hand_built_graph.hpp`), and the frontend spells only arithmetic, delay and
recurrence. So `gradients(Graph&, loss, {wrt…})` infers the graph, walks its node list in reverse
appending each node's adjoint in the SAME `add_node` vocabulary, and returns the gradient value ids —
ordinary graph values the caller keeps building with. That is what makes M7.2 (an optimizer step
in-graph) true by construction rather than by a special affordance: `w ← w − lr·dw` is three more
nodes on the returned ids. It also is why M7 depends on M5 and not M6 (`03-DEPENDENCIES.md` §3.3): the
pass needs shapes and operations, not lowering.

**The contraction adjoint is re-derived against the restricted attribute (finding F-M7-CONTRACT).** This
tree's `ContractAttr` carries `Axes axes` — the summed axes of the operands' joined shape — deliberately
NOT the old tree's einsum `ContractSpec{a_idx,b_idx,o_idx}` ("the generality has no reader here"). The
old tree's contract VJP permutes those three index lists and does not port. But a contraction in this
tree IS `reduce_sum(A⊙B, axes)` over the broadcast joined shape (`runtime/compute.hpp`), so its adjoint
needs no rule of its own: it composes the reduce-sum adjoint and the multiply adjoint —
`dP = bcast_back(dY over the summed axes)`, `dA = unbroadcast(dP⊙B)`, `dB = unbroadcast(dP⊙A)`. A
matmul's `dX = dY·Wᵀ`, `dW = Xᵀ·dY` fall out of that with no transpose special-case
(`rules_contract.hpp`). Anything the restricted attribute cannot express is not silently widened; it is
a residual (a symbolic contracted extent refuses by name).

**fft/pack differentiate by claim, not by a checked number (finding F-M7-FFT-NOTRUN).** `_fft`/`_pack`
are classified `Differentiable` — the claim that a linear-transpose adjoint rule exists — but this
tree's fft is shape-only until a backend at M13, so an fft graph builds, infers and plans but runs no
numbers. The autodiff goldens (`gen_grad_golden.py`) cover MLP, conv1d and softmax; none is spectral.
So the spectral adjoint has NO runnable numeric check here, and rather than emit an unverifiable
gradient the pass FAILS on an `_fft` reached on the differentiated path (an honest "not written yet,
runs no numbers" refusal, shown on the residual board). The old tree lifted its fft exclusion because
its fft ran; that reason is absent here, so the exclusion stays until M13 gives fft numbers.

**E7 is lifted for the reduction, and the residual is named.** M7.3 requires gradients over symbolic
extents. The one place it is not free is a mean, whose adjoint scales by `1/N`: when `N` is a symbol
that reciprocal cannot be a compile-time constant, so on the symbolic branch `rules_reduce.hpp` builds
`N` as a run-time value — a ones buffer of the operand shape summed over the reduced axes — and divides
by it (R4-2). Elementwise and contract rules were already symbolic-safe (they compare extents, never
read them as literals). What remains excluded: a slice/pad/unfold over a symbolic OPERATED axis, but
that is refused at the FORWARD infer (M5.7's per-axis precondition), so the adjoint never sees it — it
is a core exclusion, not an autodiff residual.

**The two gates, and what they cannot see.** M7.4's leaf gate (`gates/autodiff_is_a_leaf.cmake`) proves
no core header includes an autodiff header, so deleting the feature changes no core header — held by
construction, checked by scanning the include edge. Its blind spot: it proves the EDGE is absent, not
that a physical `rm -r include/heinzel/autodiff` recompiles, which is unnecessary precisely because the
edge is what such a build would exercise. The residual board (`autodiff_exclusions_test.cpp`) proves each
exclusion still FIRES; its blind spot is the M5.9 one restated — it proves a refusal exists and objects,
not that the refusal is faithful to some external oracle (there is none for a gradient this tree cannot
take). Both are stated here as Decision 8 requires a gate's blind spot to be.

## Decision 15 — the layers above the core are a triage, not a port; and `core_no_frontend` (M8)

M8 in `02-MILESTONES.md` is written against the *old* tree's shape, where `dsp/ rt/ audio/ units/
integrations/ backends/ simd/` are seven sibling include subtrees and O12 (§M8.1) protects six named
boundaries between them and the core. This tree is a re-derivation, not a port, and it has already
folded several of those "layers" into the core. So M8.1's own escape clause is the governing
instrument — *"a boundary this tree does not have, because it did not build the layer the gate
protects, is a recorded decision, not a silently missing gate"* — and M8.6's is its twin for the
build-option list. This decision is that record; each layer's final disposition is confirmed at the
step that builds (or records the absence of) it.

**The triage.** Each old-tree layer is exactly one of: **(a)** ported as an opt-in subtree with its
O12 gate; **(b)** already in the core, so its boundary is the existing structure/opt-in gates, not a
new subtree; **(c)** deliberately absent at M8, recorded with the milestone that would add it.

| old-tree layer | files @ `c4012b8` | disposition (draft, confirmed at its M8 step) |
|---|---|---|
| `units/` | 1 (`dimension.hpp`) | **(b) in core** — `ir/units.hpp` (data-as-IR) + `sema/units_check.hpp` (the checking pass). Units-annotated graphs are already a named exclusion (E8, M4/M5). No `units/` subtree exists or should. |
| `audio/` | 4 | **(a) port by need** — the wav/decode/cli subset iff a runnable M8.3 example exercises it; otherwise **(c)** deferred with reason. |
| `dsp/` | 17 | **(a) partial** — only the subset an example/tutorial runs (the freestanding-safe fixed-N FFT/windows/complex first); the rest **(c)** by need. |
| `rt/` | 9 | **(a/c) by need** — realtime scheduling that no M8 example runs defers to M11/M13. |
| `integrations/` | 2 (juce, vamp) | **(a) port** — M8.5 requires plugin-parity checks; needs the M6 target + back end. |
| `backends/` | 5 | **(c) mostly deferred** — backend codegen beyond the M6 machine model is M13 (the same deferral F-M7-FFT-NOTRUN records for the DFT). |
| `simd/` | 3 | **(b/c)** — vectorisation lives in `codegen` (M6 `VectorLane`); the scalar-minimax embedded path is **(c)** by need. |

A layer is built only when a *runnable, output-compared* example (M8.3) or a parity check (M8.5)
actually exercises it. Building a layer no example runs would be cargo-cult; recording it absent with
its reason is the mechanism M8.1/M8.6 provide. This triage IS the M8.6 difference-from-twenty-two list,
finalised at S8.

**`core_no_frontend`.** `02-Target-Architecture.md` §2.4 adds this boundary to the O12 family; it was
RED in the old tree. Here the frontend builds ON the core (`include/heinzel/frontend/` includes `ir/`,
`sema/`), and no core header includes the frontend — the gate `core_no_frontend` (over `include/`,
frontend exempt) holds it, shown objecting to a deliberate offender fixture. Its machinery is the
general `gates/no_include_into.cmake`, parameterised by the protected include prefix; it is the same
leaf-edge property M7.4's `gates/autodiff_is_a_leaf.cmake` checks. That autodiff gate is the specific
ancestor of this general form and MAY be folded into it in a later structure pass; it is left standing
now so M7.4's just-closed gate is not disturbed (respecting the "no wild refactor" rule). Its blind
spot is Decision 14's, restated: it proves the include EDGE is absent, not that a physical removal
recompiles — unnecessary because the edge is what such a build would exercise. The remaining O12 gates
(`core_no_dsp`, `core_no_rt`, `core_no_audio`) are registered through the same machinery at the step
that ports each layer (S4); a layer disposition (b)/(c) has no such subtree, so it carries no such gate
— which is the recorded decision above, not a hole.

## Decision 16 — the einsum notation is a capture layer that lowers to three existing nodes and consults no cost model (M8.2)

`02-MILESTONES.md` §M8.2 owes the einsum/einops notation, and `01-DERIVATION.md` §1.3 fixes its one
hard property: it is *capture*, and **capture consults no cost model**. In the old tree
`frontend/einsum.hpp` included `schedule/cost.hpp` and called `cost::contraction_path` to GEMM-reorder
a contraction at build time — a cost-model consultation this rewrite must not reproduce (P2-2). Here
nothing has been built to move, so it is a rule on new code, not a refactor.

**What it is.** `include/heinzel/notation/` is an opt-in layer above the frontend, in namespace
`heinzel::notation`:

- `structural_ops.hpp` — `reduce(x, ReduceOp, Axes)`, `transpose(x, Axes order)`, `contract(a, b, Axes)`:
  the user-facing way to build the three attributed structural nodes that until now only
  `tests/support/hand_built_graph.hpp` built by hand. Each appends exactly that node to the frontend's
  arena, the same way `graph_builder.hpp`'s `delayed()` appends a delay. It writes NO arithmetic — the
  reduce/contract/transpose meanings were settled and measured at M4/M5 and still run through the one
  writing in `runtime/compute.hpp`. It is exempt from `gate_one_writing_per_operation` for the identical
  reason `frontend/graph_builder.hpp` is: it names `op::` to CONSTRUCT nodes, not to compute.
- `einsum.hpp` — the string parser and lowering. `einsum("cn,cn->n", a, b)` is a contraction,
  `einsum("cn->n", x)` a reduction, `einsum("abn->ban", x)` a transposition, `einsum("abcn->cbn", x)` a
  reduction then a transposition. It names no `op::` at all — it calls the three builders.

**Why the notation is narrower than numpy's einsum, and why that is faithful, not a shortcut.** This
tree's `_contr` (`sema/analyze.hpp` `ShapeRule::ContractAxes`) is a broadcast and a collapse: it lines
its two operands up ELEMENTWISE and sums the named axes. It is not a stride-walking contraction, so a
transposing or batched einsum (`"ik,kj->ij"`) is not expressible by it — that is M5's deferred shape
algebra, recorded there. The notation therefore lowers only what the nodes can compute: a binary spec's
two operands must carry the same labels in the same order, and the output is those labels minus the
summed ones. A transposing/batched contraction, a diagonal (repeated index), an invented output axis,
an ellipsis, or three-or-more operands are each REFUSED with the reason, at compile time (`fail()`),
never silently mis-lowered. Three-or-more operands are refused specifically because an n-ary chain would
have to CHOOSE a pairing order — the cost-model decision M8.2 forbids the capture layer. So the
no-cost-model property is structural: the lowering makes no ordering, pairing or layout decision, and
there is nothing here a cost model could inform.

**The gate — `notation_no_cost_model` (O12).** The cost model in this tree is the PLANNER,
`heinzel/schedule/` (it chooses an order and where values live — the role `cost::contraction_path` filled
in the old tree). The gate scans `include/heinzel/notation/` and forbids any `#include` of
`heinzel/schedule/`, shown OBJECTING to a committed fixture that includes `schedule/plan.hpp`, pinned by
file:line (`PASS_REGULAR_EXPRESSION`, not `WILL_FAIL` — finding C-4). It reuses `gates/no_include_into.cmake`
the OTHER way round from `core_no_frontend`: there ROOT is `include/` and the forbidden prefix is a layer
(the leaf edge, M8.1); here ROOT is the notation subtree and the forbidden prefix is the cost model (the
capture edge, M8.2). Both are the same include-edge check with ROOT and FORBIDDEN swapped, so the shared
gate's messages were made direction-neutral ("a header under the scanned root reaches into the forbidden
prefix") rather than duplicating the file — one spelling, both boundaries.

**Blind spots, stated.** (1) The gate proves the include EDGE to `heinzel/schedule/` is absent; it does
not prove the notation touches no cost anywhere by other means (it names no `op::` cost column and calls
no planner, but the gate's claim is specifically the include edge). This is the same edge-not-removal
blind spot Decision 14/15 record, and it is sufficient for the same reason — an `#include` is what any
such consultation would need. (2) The gate is pointed at the cost model = the planner. `codegen/` (the
back end / machine model) is downstream and the notation reaches into it no more than into the planner,
but the boundary this condition names is the cost model, so that is what the gate protects. (3) The
notation's own refusals are compile-time `fail()`s demonstrated by construction here; a `compile_fail_*`
harness for them (the old tree's `compile_fail_einsum*` parity rows) belongs with the examples at M8.3,
recorded there.

**einops.** The multi-character, space-separated einops verbs (`rearrange`, a named-op `reduce`, `repeat`)
are NOT landed at S2. einsum already gives a real user-facing spelling for the three capabilities S2
names (reduce/contract/transpose). einops grows by need at M8.3 if the `einops_pipeline` example is in the
selected eighteen and needs a construct einsum cannot spell — the evidence-driven rule the M8 plan sets
(a surface is built when a runnable example exercises it), never a silent frontend widening.

## Decision 17 — the run-surface widens to N named inputs, any of them wide, as an additive superset (Milestone MW)

**Why this milestone exists.** M8.3 asks that the eighteen example programs, twelve tutorial programs
and twenty benchmarks each be RUN and output-compared. Sizing that surface surfaced
`F-M8-RUNTIME-IS-ONE-SCALAR-INPUT`: all three of this tree's run-surfaces — the walk
(`runtime/interpret.hpp`), the scheduled run (`runtime/execute.hpp`) and the generated run
(`codegen/program.hpp` + `codegen/generate.hpp`) — took EXACTLY ONE input and it had to be one number
per sample, while the old tree's examples run programs with several named inputs and wide inputs. The
operator decided (2026-09-04 10:33, `operator-decision-multi-input.md`) to build a wide/multi-input
run-surface as its own milestone NOW, before finishing M8.3 and before M9 (M9's Python surface needs
it independently). Reading A (the programs actually run) is the target; Reading B (recorded exclusion)
was rejected.

**What was already there, and what was missing.** The frontend and IR have always modelled N named
inputs of any shape (`Graph::inputs` is `fixed_vector<NamedValue, kMaxIO>`; `input(where, name, Shape,
type)` takes any shape), and the internal machinery already carries WIDE values everywhere (`Signals`
keeps one plane per element; a constant is filled element-by-element; `write`/`write_place`/`store_at`
address any element). The single-scalar wall was ONLY at the three run-surface ENTRY points, which
rejected `inputs.size() != 1` and a wide input and then fed element zero of the one input. So the
milestone widened only the feeding.

**The feed representation (`runtime/run_inputs.hpp`).** A feed is a view over the caller's storage —
`InputFeed{const double* data; std::int32_t width}` — laid out SAMPLE-MAJOR: `data[n*width + e]` is
element `e` of that input at sample `n`. `Inputs` is a `fixed_vector<InputFeed, kMaxIO>`, one per named
input in `Graph::inputs` order. Sample-major was chosen for two reasons: a real audio/frame input
arrives interleaved (element fastest within a sample), and the width-1 case is then bit-for-bit the
scalar entry's `block[n]`. `feeds_are_this_graph_s` checks the feed count and each width against the
graph before any number is written, so a feed of the wrong shape is a clear refusal, not a half-filled
input; it is ONE spelling shared by the walk and the scheduled run, named after the check rather than
after a surface — the same shape and reason as `widths_are_this_graph_s`.

**Additive, not a rewrite (the key design choice).** The scalar entry points are UNCHANGED — same
signatures, same behaviour, same refusal messages. A `std::array<double, BlockSize>` genuinely carries
one number per sample and cannot fill a wide input, so the scalar entry's wide-input refusal is CORRECT
and stays (it is what `tests/unit/runners_at_width_test.cpp` pins, and that test is untouched and green).
The wide capability is a NEW entry beside each: `run(g, const Inputs&, out)`, `execute(g, plan, const
Inputs&, work)`, and — because `emit` takes no input argument to overload on — a separate `emit_wide`
generator plus `run_generated(const Inputs&, work)`. Every one of the 105 prior tests stayed green by
construction; nothing was regressed.

**The generated run and the conservation oracle.** `Program::input` (one `Placement`) became
`Program::inputs` (a `fixed_vector<InputPlace, kMaxIO>`, each a place and a width). The program hash
gained a `mix(InputPlace)` overload behind the same IDENTITY the literal width and the lane flag use: a
single input one number wide mixes exactly the one placement the lone field mixed, so widening the field
to a list RE-RECORDED NOTHING — the whole corpus is single-scalar-input, its hashes are unchanged, and
`conservation_test.cpp` now perturbs both the input place and the input width and requires the hash to
follow. This is the M4.2 `default_numerics` precedent applied without even needing the re-record, because
the identity holds.

**`_delay` was NOT forced to change.** The innermost-axis restriction on a lag
(`sema/attribute_checks.hpp`: "a lag along an outer axis has nowhere to run: there is no second index to
step back along") is a rule about a VALUE's own axes, orthogonal to how many inputs are bound or how wide
one is. Widening the feeding touched no analysis pass — `sema/analyze.hpp` and `sema/attribute_checks.hpp`
are byte-unchanged across MW. The operator reserved a deeper `_delay` change as a further decision; it was
not reached, and this records that it was checked, not merely skipped.

**Blind spots.** (1) All inputs still share one `BlockSize` — the run computes one block, and every input
supplies `width * BlockSize` numbers over that same block. Per-input block lengths are not a thing here
and were not asked for. (2) A feed is a view: the caller owns the storage and its lifetime, exactly as the
scalar entry's `const std::array&` is. (3) The widths a generated program checks its feeds against are the
ones baked in at `emit_wide` from the graph; `feeds_match_program` checks the caller's feeds against the
program's recorded inputs, since a generated run has no graph left. (4) The examples/tutorials/benchmarks
of M8.3 are what this unblocks; porting them onto the widened surface is M8.3's remaining work, not MW's.

## Decision 18 — M8.6: the build-option list, each declared option configures and builds, and the twenty-two accounted for

`02-MILESTONES.md` §M8.6 asks two things: **every build option this tree declares configures and
builds**, and **where the option set differs from the old tree's twenty-two, each difference is a
recorded decision rather than a silent absence**. Decision 15 is the layer-level half of that record;
this is the option-level half, and it names the check that keeps it honest.

**What this tree declares** (`CMakeLists.txt`) — two `option()` toggles and two `set(... CACHE STRING)`
knobs:

| option | values | configures | builds |
|---|---|---|---|
| `HEINZEL_BUILD_TESTS` | ON (default) / OFF | both | ON → the 108-test suite; OFF → an INTERFACE library with no targets, for a consumer that wants only the headers |
| `HEINZEL_BUILD_COMPILE_TIME_GUARD` | OFF (default) / ON | both | OFF → nothing (guard out of the default build **and** out of ctest, success condition 3); ON → the `heinzel_compile_time_guard` target on demand, and **still zero ctests registered** |
| `HEINZEL_CXX_STANDARD` | 26 (default) / 23 / (else rejected) | 26 and 23 configure+build the full suite; any other value is refused by a `FATAL_ERROR` | 23 is the declared portable floor and builds 108/108 |
| `HEINZEL_CONSTEXPR_STEPS` | any integer (default 2^28) | a raised value (2^29 checked) configures; the compiler-specific flag it feeds is chosen in `CMakeLists.txt` | exercised by every default build |

**The check.** `tools/configure_matrix.sh` drives cmake over each declared value and fails loudly on
any that does not configure (or, for the artifact-producing ones, does not build). It is run by hand,
not wired into ctest — a matrix of clean configures does not belong in the per-commit inner loop, the
same reasoning that keeps the compile-time guard and the `measure_*.sh` subjects out of it. It is
**shown objecting**: `HEINZEL_CXX_STANDARD=99` is pointed at the tree and observed to be *rejected at
configure* (the pass is the rejection), and the guard-ON path asserts the guard registers **zero
additional ctests** — measured as the registered-test *count* under `guard=ON` matching a `guard=OFF`
baseline (both 142 at `fd6bc21`), which objects by arithmetic whenever the counts differ. That
count-equality replaced an earlier name grep (`guard|_cost`) that false-positived once the suite grew
ordinary tests carrying "cost" in their names (`build_refuses_row_with_no_cost`,
`parity_cost_model_flip`): the property is "the option adds no test", which is a count, not a name.
Measured (S8, `fd6bc21`, clang 22.1.8): `6 passed, 0 failed`.

**The twenty-two, each accounted for.** The old tree's `CMakeLists.txt` at `c4012b8` declares exactly
twenty-two `option()`s (plus the same two cache STRINGs, which carried over unchanged). Each absent one
is a recorded decision, not a silent hole; the layer-level reasons are Decision 15's, referenced here.

- **Carried over:** `HEINZEL_BUILD_TESTS` (kept), and the two cache STRINGs `HEINZEL_CXX_STANDARD`,
  `HEINZEL_CONSTEXPR_STEPS`.
- **New here, not among the twenty-two:** `HEINZEL_BUILD_COMPILE_TIME_GUARD` — amendment 03 A3-2's
  separate, slow, opt-in guard kept out of the default suite. A deliberate addition, recorded.
- **Deferred to the M8 step that builds its surface** (each will *add* its option there, and this record
  updates when it does — that scheduled addition IS the recorded decision, per §M8.6):
  - `HEINZEL_BUILD_EXAMPLES` → S3, with the first ported example. Currently held behind the reserved
    frontend-DSL scope decision (`F-M8-EXAMPLES-NEED-A-BUILD-SURFACE`, reported to Gerald 2026-09-04);
    the option lands with the examples, not before.
  - `HEINZEL_BUILD_BENCHMARKS`, and the five `HEINZEL_BENCH_COMPARE_{KFR,PFFFT,FAUST,ACCEL,KLANG}`
    third-party comparison toggles → S6, with the benchmarks (the COMPARE_* ones additionally need the
    dsp FFT layer, so they are gated twice).
  - `HEINZEL_BUILD_DSP_TESTS`, `HEINZEL_DSP_FFT_POCKETFFT`, `HEINZEL_DSP_FFT_KFR` → the `dsp/` layer,
    ported by need at S4 (Decision 15, `dsp` disposition (a) partial).
  - `HEINZEL_BUILD_RT_TESTS` → the `rt/` layer, by need at S4 (Decision 15, `rt` (a/c) by need).
  - `HEINZEL_AUDIO_FLAC`, `HEINZEL_AUDIO_MP3`, `HEINZEL_AUDIO_OPUS` → the `audio/` layer, by need at S4
    (Decision 15, `audio` (a) port by need).
  - `HEINZEL_BUILD_JUCE`, `HEINZEL_BUILD_VAMP` → the integrations, at M8.5 / S7 (Decision 15,
    `integrations` (a) port; needs the M6 target + back end).
- **Deferred to a later milestone, deliberately absent at M8** (Decision 15's disposition (c)):
  - `HEINZEL_HAVE_THREADS` — the executor's thread / freestanding seam. The rewrite executor has not
    grown that seam (`F-M8-LAYERS-ARE-NOT-A-PORT`); the embedded/freestanding surface is Decision 15's
    `simd`/embedded (c) and lands no earlier than M11/M13. Absent, recorded.
  - `HEINZEL_ENABLE_CUDA`, `HEINZEL_ENABLE_CUDNN` — GPU offload, an M13 backend concern (the same
    deferral `F-M7-FFT-NOTRUN` and Decision 15's `backends` (c) record for the DFT). Absent, recorded.
  - `HEINZEL_AUTOTUNE`, `HEINZEL_AUTOTUNE_PARALLEL` — host calibration of the default `TargetInfo`. The
    M6 machine model uses a fixed, named `TargetInfo`; calibrating it to the build host is a later
    concern (M11/M13), not an M8 one. Absent, recorded.

That is all twenty-two. **M8.6 as it binds today** — every *declared* option configures and builds, and
every difference from the twenty-two is a recorded decision — is met and mechanically re-checkable. It
is **not frozen**: S3/S4/S6/S7 each add the option named against them above, and the M8 boundary
re-confirms the list against this record once those steps have run (or recorded their own absence). The
freeze the plan's S8 asks for is the boundary's, when the surface those steps build is present; this
decision is the accounting that makes that freeze a diff against a written baseline rather than a guess.

## Decision 19 — M8.3 begins: the first ported example (`custom_op`) and the `log` frontend spelling

`02-MILESTONES.md` §M8.3 asks for the eighteen examples RUN and output-compared. Operator decision
2026-09-04 11:58 (Option A, scoped) resolves how: grow the user-facing frontend so each example becomes
expressible, **one example per commit**, each new spelling LOWERING to an already-tested M4/M5 op (no new
op semantics), streaming subset first, additive-only. This decision records the first such port and the
convention the rest follow.

**Realization mechanism.** An M8.3 example is realized as a parity `_here` / `_there` pair on the
existing harness (`tests/parity/compare.cmake`, Decision at M1.4 / M4.5): a `_here` program written in
THIS tree's frontend that RUNS and prints samples, a `_there` program that builds the same computation
on the old tree at c4012b8 and prints the same format, and a `compare_samples` check with a NAMED,
MEASURED tolerance and its cause. This is the same instrument the biquad / delay-line / reduce /
contract families already use; it is what makes "run and output-compared" a comparison of numbers
against the old tree rather than a self-check. (`HEINZEL_BUILD_EXAMPLES`, the old tree's standalone
examples/ build option, remains the separate concern Decision 18 files against S3; the run-and-compare
obligation of M8.3 is met by the parity realization, which does not need it.)

**`custom_op` — first ported (commit "M8 S3a").** The old `examples/custom_op.cpp` makes one point:
Heinzel has no custom-op registration seam; you extend the vocabulary by composing a `constexpr` helper
(`log10(x) = log(x) * 1/ln10`) from built-ins. That point is true of this tree by construction (a leaf
allocates its own graph), so the port needs exactly ONE new frontend spelling — `log`, a free function
found by ADL on `Expr` that names the already-checked M4 code `op::Log` and adds no operation. Both
sides spell the same helper in the same order with the same constants; the only accounted difference is
the natural-log core, which differs by ≤1 ULP of the double result and tips the single-precision
rounding of the F32 output on 16 of 64 samples — every difference exactly one f32 ULP at the output's
magnitude (max 1.9e-06 near 17 dB). ALLOWED=2.0e-06 is that bound, measured not fitted, and goes to zero
the day the two trees share a log core.

**Membership recording — incremental, by disposition.** Under Option A ("one example per commit") the
18-of-26 membership is fixed AS EACH example is ported or deferred, recorded here, not guessed up front.
The dispositions already fixed (Decision 15 / M8-STEPS S3 sizing): plugins (juce×5, vamp×2) → M8.5/S7;
`12_python.py` → M9.1; embedded/esp32 → deferred layer (c); the batch/broadcast-over-time examples
(channel_scale_3d, image_elementwise_2d, stencil_indexing_2d, mlp_train, and any transposing/batched
contraction such as einsum_contractions) → the RESERVED batch-model decision (operator 2026-09-04 11:58),
STOP-and-report, not ported here. `custom_op` is the first admitted to the S3 set. The set closes when
every example is either ported (with its parity pair) or recorded deferred with its reason.

## Decision 20 — M8.3: a runtime scalar "Param" is an ordinary named input; `conditionals` ported

The old tree has a first-class `Param` — a runtime scalar knob, set once per run via `ex.param("name")`,
constant across the block, addressable hierarchically. This tree has no `Param` category
(`ValueCategory` is Internal/Input/Output/Const/Variable) and does not add one. A Param is modelled as
an **ordinary named input fed a constant-valued stream** on the MW multi-input surface: the host writes
the knob's value into every sample of that input, `run(g, Inputs, out)` binds it beside the signal
inputs, and the graph never treats it as anything but one number per sample.

**Why this is the right reading and in scope.** It adds no operation and no value category — additive,
per Option A (2026-09-04 11:58). It is NOT the reserved batch/broadcast-over-time axis model: nothing
in the graph broadcasts a value across the time axis; the input simply carries a constant signal, which
`fill_constants` already does for literals and which an ordinary input is free to carry. For the
interpret run surface M8.3 examples use, a constant-fed input and a hoisted knob compute identical
numbers (verified below). A first-class Param (loop-invariant, hoisted in codegen) would be new surface;
it is deferred until an example needs the hoisting itself rather than the value, and recorded here so
that deferral is a decision, not a gap.

**`conditionals` — second ported (commit "M8 S3b").** Its three graphs — a per-lane noise gate
`select(|x|>thresh, x, 0)` with `thresh` a knob, a soft limiter `x/(1+|x|)`, a hard limiter
`min(max(x,-1),1)` — exercise the Param-as-input modelling and add the exact choose/compare/clamp
spellings `max`, `min`, `abs` (composed as `max(x,-x)`), `>` (a mask via `op::Lt` with swapped
operands), and `select` (`op::Select`). Every one names an already-checked M4 code; none rounds beyond
the F32 output. Measured: all 192 samples (64 per section) agree with the old tree BIT FOR BIT — the
masks and clamps cannot round, and the soft limiter's single division did not hit the double-rounding
the contraction finding turns on. ALLOWED=0.0, no cause owed; a difference of any size is a regression.
*(`select` went for `where` in Decision 59.)*

S3 examples admitted so far: custom_op, conditionals, graph_composition (commit "M8 S3c": composed
channel strip; validates recurrence-in-helper + BuildContext::inside() namespacing; adds only `tanh`;
filter matches bit for bit, tanh core differs by one f32 ULP on 17/32 samples, ALLOWED=6.0e-08). Newly recorded dispositions from S3 sizing:
`fast_convolution` → DEFERRED (rfft/irfft/cmul; the spectral backend is M13, `ElementKind::Boundary`);
`oscillator` → S4 (needs the `dsp/` layer: `dsp::oscillator`, `frac`, band-limited waveforms).

## Decision 21 — M8.3 example membership fixed by disposition; the batch/broadcast axis-model binds the 18-count

With the streaming subset admitted (custom_op, conditionals, graph_composition) the S3 sizing gate
("fix the 18 of 26 by disposition, recorded, not guessed") is completable: every `examples/*.cpp` at
`c4012b8` was read and dispositioned against what {frontend + notation} can spell today, the closed
M4/M5/M6/M7 op set, and the two standing reserved boundaries (the batch/broadcast-over-time axis model,
operator 2026-09-04 11:58; and any deeper `_delay`/runtime-extent change, operator 2026-09-04 10:33).
A disposition is EVIDENCE-DRIVEN — each cites the construct in the old source that fixes it — never a
guess. The table (26 programs; the plugin subdirs count as their integration example):

| example | disposition | why (the construct that fixes it) |
|---|---|---|
| `custom_op` | **PORTED** S3a `fc5af8c` | composed `log10` helper; only new spelling `log` → `op::Log` (M4). |
| `conditionals` | **PORTED** S3b `3160ac0` | gate/soft/hard; `max`/`min`/`abs`/`>`/`select` → M4 codes; Param-as-input. |
| `graph_composition` | **PORTED** S3c `4bd476b` | channel strip; recurrence-in-helper + `inside()`; only new spelling `tanh`. |
| `fir_fit` | **REACHABLE** (Option A, future step) | unfold + einsum `fk,k->f` + `gradients` — all lower to closed M5/M7 ops; needs frontend spellings `unfold`/`gradients`/`as_output` (none new-semantic, F-M7-GRAPH2GRAPH). Not batch. Multi-spelling + recorded golden — sized before its step. |
| `physical_dsp` | **REACHABLE** (Option A, future step) | feedback comb recurrence (runnable now) whose delay depth is a physical duration folded at compile time. Needs a units **Quantity** (magnitude+`Units`) type and a `samples(duration)` fold → a constant delay depth (`op::Delay`, closed). Units are in-core (Decision 15b). Not batch. |
| `energy_fraction` | **S4** (audio layer, by-need) | `heinzel::audio::run_cli` + `unfold` windows + `mean`/`sqrt`. Needs the opt-in `audio/` cli subtree (S4) + `unfold` spelling. Streaming, not batch. |
| `oscillator` | **S4** (dsp layer, by-need) | `dsp::oscillator`, `frac`, band-limited waveforms → opt-in `dsp/` subtree (S4). |
| `channel_scale_3d` | **RESERVED batch-model** | per-channel scale broadcast across a batched 3-D buffer — broadcast over a non-time axis. |
| `image_elementwise_2d` | **RESERVED batch-model** | 2-D image broadcast; a non-streamed spatial axis pair. |
| `stencil_indexing_2d` | **RESERVED batch-model** | 2-D neighbourhood stencil; non-streamed spatial indexing. |
| `mlp_train` | **RESERVED batch-model** | matmul (`ik,kj->ij`, transposing/batched contraction, Decision 16) + batched training. |
| `einsum_contractions` | **RESERVED batch-model** | transposing/batched einsum the streaming `_contr` cannot spell (Decision 16). |
| `einops_pipeline` | **RESERVED batch-model** (old-tree "M14") | `rearrange("b (c f) t -> b c f t")` + named reduce over batch axes — the batch einops axis model. |
| `interleaved_planar_audio` | **RESERVED batch-model** (old-tree "M11") | `input_layout("c t","(t c)")` + a `[C,1]` gain broadcast over t — an interleaved layout and a broadcast over the time axis. |
| `dsl_tour` | **DEFERRED composite** | a whole-DSL coverage tour (matmul, FFT, units-quantity, dsp biquad, einops, STFT) — not a single-concern parity example; expressible only once every reserved/S4 surface above exists. Recorded as an inventory exclusion at M8.3 (its coverage is met piecewise by the ported examples + unit suites). |
| `dynamic_length` | **RESERVED runtime-extent** | extent `N` resolved at `prepare()`; the rewrite's extents are compile-time (`check_iota_attribute` "a generator's shape must be known now"; `state_depth_is_a_constant` "only a scan length may be symbolic", both in `sema/attribute_checks.hpp`). A runtime-resolved dim is a reserved-architecture capability (the same class as the deferred `_delay`/axis questions), not a thin spelling that lowers to a closed op. |
| `fast_convolution` | **DEFERRED M13** | rfft/irfft/cmul; spectral backend is M13 (`ElementKind::Boundary`). (recorded above) |
| `embedded_biquad` | **DEFERRED (c) embedded** | freestanding/embedded surface — Decision 15 disposition (c). |
| `esp32-bench/` | **DEFERRED (c) embedded** | ESP32 target — Decision 15 disposition (c). |
| `juce_plugin/` ×5 | **M8.5 / S7** | plugin-parity integration; needs the M6 target+back end. |
| `vamp_plugin/` ×2 | **M8.5 / S7** | plugin-parity integration. |

**The finding this fixes (F-M8-BATCH-MODEL-BINDS-THE-18): the batch/broadcast-over-time axis model is
now the binding constraint on M8.3's 18-example count.** Nine of the nineteen non-plugin example
programs (channel_scale_3d, image_elementwise_2d, stencil_indexing_2d, mlp_train, einsum_contractions,
einops_pipeline, interleaved_planar_audio, plus dsl_tour and dynamic_length which need it and/or the
reserved runtime-extent) cannot be ported without a reserved decision; three more (fast_convolution,
embedded_biquad, esp32-bench) are deferred to later milestones by disposition already recorded. Three
are ported; four (fir_fit, physical_dsp, energy_fraction, oscillator) are reachable under the
already-granted Option A frontend growth + S4 by-need layers, each a multi-commit step sized before it
runs. So the streaming frontend, as authorized, tops out at seven ported+reachable — short of the
plan's eighteen, whose remainder requires the
batch/broadcast axis-model the frontend-DSL decision (2026-09-04 11:58) explicitly kept reserved:
*"when an example first needs a batched/transposing contraction, a broadcast over the time axis, or
any non-streamed axis model, stop and bring it back to Gerald with a recorded batch-model
recommendation."* This decision IS that stop, recorded; the recommendation is carried to the origin.
The batch examples are deferred cleanly (not ported, not failed) meanwhile; M8.4/M8.6 stay closeable
in parallel; nothing here crosses the reserved boundary.

## Decision 22 — the batch/broadcast axis-model is minted as its own milestone (MB); axes trend toward general, no baked-in semantics

**Operator decision 2026-09-04 13:29** (Gerald, topic "heinzel"): *"mint the batch/broadcast axis as
own milestone. And generally, remember, that we actually didn't want to have any specific
axis-semantics. at least in the long run."* This RESOLVES the reserved boundary Decision 21 carried to
the origin (the batch/broadcast-over-time model that binds nine of M8.3's examples) and the STOP the
2026-09-04 11:58 frontend-DSL decision installed at the first batched/transposing example.

**Minted as milestone `MB`** (own plan `plan/MB-STEPS.md`, own exit conditions, own fresh audit),
exactly as `MW` was minted for the wide/multi-input surface — not a decimal, because renumbering the
closed M8.x/M9 scheme would disturb traceability. Scope: unblock the nine — channel_scale_3d,
image_elementwise_2d, stencil_indexing_2d, mlp_train, einsum_contractions, einops_pipeline,
interleaved_planar_audio, dsl_tour, dynamic_length.

**Capabilities, smallest-first, each additive and one per commit** (grounded in the tree at `4e5c823`):
1. **operand broadcast** in elementwise binary ops (trailing-axis align, extent-1/absent axis stretch,
   streamed axis unchanged) — unblocks channel_scale_3d. The keep-axis broadcast already exists
   (`ReduceAttr::keep_axes`, `ir/ir.hpp:132`); operand broadcast is its missing twin.
2. a **2-D element-axis run surface** (spatial image, degenerate streamed axis) — image_elementwise_2d,
   stencil_indexing_2d (with the existing `Unfold2d`, `ir/ir.hpp:170`).
3. the **general / transposing / batched contraction (matmul)** the streaming `_contr` refuses today
   (`notation/einsum.hpp` header; Decision 16) — einsum_contractions, mlp_train forward.
4. **general rearrange over outer axes** (einops verbs, planar/interleaved layout), lowering to
   transpose + reshape + keep-axis reduce — interleaved_planar_audio, einops_pipeline.
Each lowers to / extends an already-tested M4/M5 op's shape algebra; **no new numeric meaning** (M4/M5
closed). dsl_tour runs piecewise once these land.

**Two reserved carve-outs MB does NOT take on:**
- **dynamic_length's runtime-resolved extent** stays a separate reserved item — do its batch layout,
  but STOP-and-ask before adding runtime-extent resolution (extents are compile-time today:
  `check_iota_attribute`, `state_depth_is_a_constant`).
- **full removal of axis-semantics** (the M1 streamed-axis model + the innermost-axis `_delay` rule) is
  a recognised LARGE future item, surfaced as its own reserved decision when a milestone naturally takes
  it on — not undertaken silently here.

**ARCHITECTURAL NORTH STAR (standing, applies to all future design).** Axes should carry no baked-in
semantics in the long run; "innermost axis IS time / the streamed axis" should eventually be a
*configuration*, not a hard assumption ("at least in the long run"). Applied to MB as a **design steer,
not a rip-out**: build the batch axis-model toward **general axes** (an axis is just an axis; extend the
uniform outer-axis shape machinery, do not fork a second special "batch" kind); keep the streamed-axis
special-casing **contained and factorable** so a later milestone can parameterise "which axis is
streamed" without unwinding MB. Fully removing axis-semantics is the reserved large item above.

Boundaries unchanged: additive (every green test stays green; 111/111 at entry, re-verified this tick
under clang 22.1.8); amendment-06 structure gate over every new MB file from the first commit; ctg3
read-only `c4012b8`/main; nothing pushed; A6-1 (one delegated agent); Q-2 deferred (hard M9); M10
excluded. This decision is a record and a milestone mint; it introduces no code and leaves the suite
unchanged.

## Decision 23 — MB.S5: the einops rearrange/reduce notation, and two reserved run-surface/optimiser items

MB.S5 gives the general rearrange over outer axes (capability 4 of Decision 22). It is a **capture-layer
notation** (`notation/rearrange.hpp`), the einops discipline `notation/einsum.hpp` already keeps: parse a
string, emit already-tested nodes, consult no cost model (the O12 `notation_no_cost_model` gate holds over
the new file). No new op and no new meaning.

- **`rearrange`** lowers a SPLIT or MERGE of the outer axes (same order) to one `_reshape`, and a
  TRANSPOSE (a permutation, no regroup) to one `_transpose`. A spec that would do BOTH at once is refused
  — write it as two rearranges — rather than growing a reshape-then-transpose lowering no example needs
  ("no speculative DSL", the M8.3 frontend-DSL line). `reduce` folds the dropped axes then transposes the
  survivors only when reordered.
- **Convention (recorded here):** the named axes are the OUTER element axes and the streamed (innermost)
  axis is named LAST and stays whole and last — the same streamed-axis-last convention `einsum` keeps
  (`einsum_contractions` names its streamed axis `t` the same way). A spec that would move or regroup the
  streamed axis is refused by name, the `ir/ir.hpp:140` restriction caught early. Ported examples carry a
  length-1 streamed axis for a whole-tensor value, the `image_elementwise` discipline.
- Ported and RUN-compared: **einops_pipeline** (S5b — split + mean, ALLOWED 9.6e-07, the reduction-
  rounding ULP) and **interleaved_planar_audio**'s PLANAR core (S5c — per-channel gain + a `c t -> t c`
  transpose round-trip, ALLOWED 0.0, bit-exact; the round-trip shown to compose to the identity
  numerically).

**Two reserved inventory items surfaced by interleaved_planar_audio, recorded and NOT built** (they are a
new capability, not a layout of existing ops — the MB §S5 "record it, do not widen" clause):

1. **Interleaved physical input (`input_layout(... "(t c)")`).** The old example declares a logical `[C,T]`
   view over a physically INTERLEAVED `(t c)` buffer, folding the interleave into the read addressing. This
   is a **run-surface memory-layout feature**, not a shape-algebra rearrange: the MW run surface
   (`runtime/run_inputs.hpp`) feeds inputs sample-major and has no interleaved-physical-input declaration.
   Porting it faithfully would ADD a run-surface capability, outside S5's "rearrange over outer axes"
   grant. **Reserved run-surface item** (a layout/IO by-need), like the units/audio by-need layers.
2. **"Inverse rearrange pair cancels to zero nodes."** The old example asserts `(c t)->(t c)->(c t)`
   compiles to the SAME node count as the direct graph — a graph **canonicalisation/optimiser** property.
   The capture layer emits the nodes it is told to (M8.2, no cost model); it does not fold inverse
   transposes. **Reserved optimiser item.** The S5c port asserts only what is TRUE — the transposes compose
   to the identity numerically and the numbers are right — and does not claim the node-count cancellation.

## Decision 24 — MB.S6: stencil_indexing_2d idiom 2 ported; idiom 1 reserved

The old `stencil_indexing_2d` shows two idioms for a 2-D stencil.

- **Idiom 2 (the windowed VIEW) is PORTED and run-compared** (`parity/stencil_unfold_*`): a 3-tap
  vertical Laplacian `sum(unfold(x, 0, 3, 1) * taps, tap_axis)` over the interior rows of an 8×12 image.
  It is the 2-D element-axis surface (MB.S3) + the existing `_unfold` (M5, closed) — an `unfold` builder
  added to `structural_ops.hpp` beside `reshape`/`transpose`, lowering to the closed op, no new meaning.
  ALLOWED 2.4e-07 (one f32 ULP at [2,4), the reduction-fold rounding). This is the second witness for MB
  exit condition 2 (a 2-D element value runs), image_elementwise_2d being the first.
- **Idiom 1 (the per-axis subscript `x[i-1,j] + x[i,j-1] - 2*x[i,j]`) is RESERVED, not built.** It needs
  two things this tree does not have and MB does not grant: (a) a **multi-axis subscript frontend surface**
  (`indices<2>()`, `x[i-1,j]`), a whole DSL surface, not a layout of existing ops; and (b) each lagged tap
  is a **SHIFT of an ELEMENT (non-streamed) axis** — a spatial lag — which brushes the reserved
  innermost-axis `_delay` question the operator kept separate (a shift on an element axis could lower to
  pad+slice, but the subscript surface that drives it is the reserved part). Recorded as a reserved
  frontend-surface item; porting it is a future decision, not smuggled into MB.

## Decision 25 — MB.S6: dynamic_length reaches its reserved STOP; dsl_tour dispositioned

The last two of MB's nine examples reach a clean stop rather than a port, each for a reason the plan
anticipated.

**dynamic_length — STOP at the runtime-resolved extent (the reserved carve-out).** Its batch/broadcast
LAYOUT is a trivial elementwise `y = x*gain + bias` over a stereo block — already covered by
channel_scale (broadcast) and conditionals (a runtime scalar as a named input, Decision 20), so there is
no new batch-model work to do. Its DEFINING feature is a **runtime-resolved extent**: `g.dim("N",
{multiple_of=8, expect=256})`, an `Input x{N,2}`, then `ex.dim("N")=frames; ex.prepare()` fixes the
length at run time. This tree's extents are resolved at COMPILE time (the solver binds symbols to
literals; `check_iota_attribute`, `state_depth_is_a_constant`), and there is no `prepare()`/runtime-dim
surface. Per the 2026-09-04 13:29 operator decision, the runtime-resolved extent is a **SEPARATE reserved
item**: do the batch layout, STOP-and-ask before adding runtime-extent resolution. So dynamic_length is
STOPPED here and recorded — not a failure, the expected clean stop. Building a runtime-dim/prepare surface
is its own future decision.

**dsl_tour — dispositioned, not ported wholesale.** dsl_tour is not one example; it is a
DOCUMENTATION-COVERAGE program that exercises nearly the WHOLE old-tree frontend so `docs/dsl-overview.md`
is known to compile and run. Its legs map to many milestones, not to MB:

- **Legs MB (and M4/M5/M7/M8.2) grant are already RUN and compared** by dedicated tests, so re-porting
  them in one monolithic tour adds coverage of nothing: elementwise arithmetic + promotion, min/max/abs/
  frac, the transcendental family, comparisons + select, reductions (sum/mean/max, keepdims) — `numeric_ops`,
  `transcendental`, `both_ways`, `tiers`, `ordering`; contraction/matmul/unary einsum — `notation`, `matmul`,
  `einsum_contractions`; rearrange split/merge + named reduce — `rearrange_notation`, `einops_pipeline`.
- **Legs that need a frontend surface MB does not grant are reserved frontend-surface items**, each its
  own future work, NOT smuggled into MB: multi-axis subscript indexing (Decision 24); the recurrence/
  streaming, view-op (slice/realize/iota), units, complex/FFT, gain_db/mix, named-subgraph `instantiate`,
  dsp::biquad, accuracy-tier-selection, and STFT/ISTFT **frontend spellings** (several of the underlying
  OPS exist and are tested in-core — units Decision 15b, spectral M5, tiers M4.2 — but their user-facing
  DSL spellings are separate surfaces); and interleaved buffer layout (the reserved run-surface item,
  Decision 23).

So dsl_tour is a DISPOSITION like Decision 21's example-membership call: its MB-granted legs are verified
by the dedicated tests, and the rest are named reserved frontend surfaces. Porting the grand tour whole
would be a speculative frontend build-out — exactly the "no speculative DSL" line the M8.3 frontend-DSL
decision drew — so it is recorded, not built.

**MB example tally:** ported + run-compared — channel_scale_3d (S2), image_elementwise_2d (S3),
einsum_contractions + mlp_train forward (S4), einops_pipeline + interleaved_planar_audio PLANAR (S5),
stencil_indexing_2d unfold idiom (S6). Reserved/stopped with reasons — dynamic_length (runtime extent),
dsl_tour (whole-frontend disposition), and the recorded inventory items (interleaved-physical IO,
inverse-pair cancellation, stencil idiom 1, mlp_train backward = F-M7-GRAPH2GRAPH).

## Decision 26 — MB CLOSED: the batch/broadcast axis-model as built, and what stays factorable

Milestone MB is closed (`plan/TRACEABILITY.md`, MB section). What was built and how the north star was
honoured:

- **The axis-model is shape algebra over ORDINARY outer axes, no new axis kind.** Broadcast
  (pre-existing, F-MB-BROADCAST-ALREADY-EXISTS), the general/transposing/batched contraction
  (`op::MatMul`), and the einops rearrange (`notation/rearrange.hpp`) all extend the uniform outer-axis
  machinery (`vocab.hpp` Shape/Extent/Axes, the `sema/*_shape.hpp` rules). A "batch" axis is just an
  outer element axis that broadcasts or is contracted — there is no special "batch" `AxisKind` bolted
  beside "time". This is the north star's "an axis is just an axis" applied as a design steer.
- **Which streamed-axis special-casing was KEPT, and kept FACTORABLE (not removed).** The convention
  "the innermost axis is the streamed axis (time), whole and last" still holds, and MB deliberately did
  not remove it (removal is the reserved large item, Decision 22). MB kept the coupling CONTAINED: the
  rearrange notation names the streamed axis last and refuses to move or regroup it (one check,
  `require_streamed_axis_fixed`); the matmul contracts only element axes and passes the streamed axis
  through; the unfold windows an outer axis only. None of these DEEPEN the streamed-axis assumption — a
  later milestone that makes "which axis is streamed" a parameter can do so without unwinding MB's
  additions, because each MB op localises the assumption to one guard rather than threading it through.
- **Blind spots, named not hidden.** MB adds no numeric meaning and no runtime-extent resolution, no
  multi-axis subscript / element-axis-shift frontend, no interleaved-physical IO, no inverse-transpose
  optimiser, no autodiff frontend, and none of the other frontend spellings dsl_tour exercises. Each is a
  recorded reserved item (Decisions 22–25), its own future decision.
- **The two reserved carve-outs remain OPEN and untouched:** dynamic_length's runtime-resolved extent
  (STOPPED at MB.S6, Decision 25) and the full removal of axis-semantics (Decision 22). `_delay`'s
  innermost-axis rule was not forced and is byte-unchanged. Q-2 stays deferred, hard deadline M9.

## Decision 27 — M8.3 tutorial-chapter disposition (size-before-S5); chapter 01 ported

M8.3 asks the **twelve tutorial chapter programs** be RUN and output-compared, not merely built. At
`c4012b8` `tutorial/code/` holds eleven `.cpp` chapters plus `12_python.py`; the plan states this at
length because "twelve, each executed" is otherwise satisfiable by eleven — the twelfth is a Python
program covered by **M9.1**, not a C++ program, and is recorded here as that split. This is the
"size before the step" gate for the tutorial half of M8.3: every chapter read at `c4012b8` and
dispositioned against what {frontend + notation} can spell after MW (multi-input) and MB
(batch/broadcast) closed, the closed M4/M5/M6/M7 op set, and the standing reserved boundaries. Each
disposition cites the construct in the old chapter that fixes it — never a guess.

| chapter | disposition | why (the construct that fixes it) |
|---|---|---|
| `01_getting_started` | **PORTED** (this decision) | `y = a*x + 1`; `a` a runtime knob = named input fed constant (Decision 20). Pure `op::Mul`/`op::Add` (M4). MEASURED bit-exact. |
| `02_the_dsl` | **REACHABLE** (Option A, future step) | elementwise math (`tanh`/`sqrt`/`abs`/`log`/`clamp`), compare+`where` (= `select`, ported in conditionals), broadcasting `{C}` over `{R,C}` (MB operand broadcast), views (transpose/reshape/slice/unfold/iota — M5). Needs the `clamp`/`where` spellings; no new op. Multi-graph, one commit per graph. |
| `03_reductions_and_einsum` | **REACHABLE** (Option A, future step) | reduce (`sum`/`max`/`mean` keepdims), `softmax` helper, matmul einsum (MB.S4), einops rearrange/reduce (MB.S5). All lower to closed M5/MB ops; needs the reduce-method + `softmax` frontend spellings. |
| `04_recurrences_and_streaming` | **PARTIAL — reachable core + dsp by-need** | the leaky/two-pole recurrences and streaming are core (M1, runnable now); the RBJ biquad cascade needs the opt-in `dsp/` subtree (S4 by-need, Decision 15/21). Port the recurrence graphs; the biquad section waits on `dsp/`. |
| `05_shape_generic_and_dynamic` | **RESERVED runtime-extent** | `g.dim`/`g.bind` deferred shapes bound at `prepare()` — the same runtime-resolved extent STOPPED for `dynamic_length` (Decision 25). Shape-generic (compile-time) parts are expressible; the runtime dimension is reserved. |
| `06_debugging_and_numerics` | **PARTIAL — reachable tiers + inspection API differs** | the numerics tiers (Precise/Fast/Approx) exist (Q-1 split, Decision A6-3); `run_pipeline`/`Pipeline` inspection and `simd_backend` are old-tree structural API this tree factors differently — the tier graphs are portable, the inspection walk is a re-expression, not a port. Sized before its step. |
| `07_autodiff` | **RESERVED autodiff-frontend** | `grad()`/`gradients()` at capture time — F-M7-GRAPH2GRAPH: M7's `gradients()` is graph->graph and hand-built, there is NO frontend autodiff spelling. Widening the frontend with an autodiff surface is new user-facing semantics MB's line forbids; reserved (recorded MB.S4f). |
| `08_autoscheduling_and_deployment` | **REACHABLE — two machine models + compile<>** | the cost model, two `TargetInfo`s, byte-identical parallel result, explicit `compile<>` are M6 content (scheduled+generated runners exist, three-way agreement tested). Re-expressed in this tree's schedule/codegen spelling; no reserved surface. |
| `09_gpu_offload` | **DEFERRED backend** | the GPU PLACEMENT decision is inspectable without CUDA, but the chapter's subject (device offload) is the spectral/accelerator backend deferred to M13 (`ElementKind::Boundary`). The placement-print part is reachable; execution is deferred. |
| `10_audio_cli` | **RESERVED + by-need** | `heinzel::audio::run_cli` (opt-in `audio/` subtree, S4 by-need) over a runtime length `N` (reserved runtime-extent, Decision 25). Two reserved/by-need dependencies; not portable without both. |
| `11_plugins` | **M8.5 + reserved runtime-extent** | the hand-built prepare/process/set wrapper over a dynamic `N` — the plugin shape is M8.5 (juce/vamp integration parity) and the dynamic block length is the reserved runtime-extent. |
| `12_python.py` | **M9.1 (not a C++ program)** | the Python surface; covered by the Python suite at M9.1. This is the eleven-vs-twelve split, recorded so "twelve executed" is not satisfiable by eleven. |

**What this fixes.** The tutorial half of M8.3 tops out, on the frontend as authorized (Option A
spellings that lower to closed ops + the S4 by-need layers), at chapters 01, 02, 03, 08 fully and
04/06/09 in part; chapters 05, 07, 10, 11 each need a reserved decision (runtime-extent, autodiff
frontend) or a later milestone (M8.5 plugins, M13 backend) already recorded. No chapter is ported by
widening the frontend past Option A or crossing a reserved boundary; a gap is a recorded disposition,
never a silent surface growth. Chapter 01 is ported by this decision (`tests/tutorial/tutorial_01_*`,
test `tutorial_01_getting_started`, MEASURED bit-exact). The remaining reachable chapters are each a
multi-commit step sized before it runs, in the S5 order 02 → 03 → 08 → the reachable parts of 04/06.

### Decision 27 addendum — chapters 02, 03, 08, 04 ported; ch04's two re-expressed runtime knobs

Chapters 02, 03, 08 ported (progress.md tallies the commits). Chapter 04's reachable core is now
ported too — section (1)'s leaky one-pole and section (3)'s internal-Variable two-pole chain, each
run and cross-compared to the same recurrence on the old tree (test `tutorial_04_recurrences_streaming`,
ALLOWED 4.0e-07 = the leaky pole's 1/(1-a) f32-accumulation bound, MEASURED max 1.193e-07). Section
(4)'s RBJ biquad cascade stays deferred to the opt-in `dsp/` subtree (S4 by-need), as Decision 27's
row states.

**Two of the old chapter's runtime knobs have no counterpart in this tree, and are re-expressed, not
dropped — the M8.1 escape mechanism (a boundary/feature this tree did not build is a recorded
decision).** The old executor exposes `set_initial("y", history)` (seed a recurrence's past before
sample 0) and `set_streaming(true)` + `reset()` (carry recurrence state ACROSS separate `run()`
calls, so block-by-block equals whole-buffer). This tree's runtime runs a whole block in one `run()`
and holds a recurrence's past in per-value HISTORY slots WITHIN that block (`codegen/program.hpp`:
the generated program is "the streaming program this tree has"); there is no host-seeded initial
state and no cross-`run()` carry. So: (1)'s seeded-history sub-part is re-expressed as the
whole-buffer, zero-initial-history run (the recurrence's past reads back to zero before sample 0, the
FixedDelay clamp); (2)'s "block-wise streaming equals whole-buffer" is re-expressed as this tree's
own streaming invariant — the leaky recurrence run interpreted == scheduled == generated, bit for
bit, proved at compile time in `tutorial_04_here.cpp` (output unchanged), exactly as ch08's
byte-identity claim was re-expressed. A cross-`run()` streaming runner with host-seeded state is a
later runtime concern (it would live where the realtime/plugin surface does, M8.5/M11), recorded here
so its absence at M8 is a decision and not a hole.

### Decision 27 addendum 2 — chapter 06 (debugging and numerics) ported; run_pipeline re-expressed

Chapter 06's three parts, ported as a single self-contained run-and-assert program
(`tests/tutorial/tutorial_06_here.cpp`, test `tutorial_06_debugging_numerics`), NOT gated on the old
tree — because its numeric content has no cross-tree golden (below). This is the ch08 internal-check
discipline rather than the ch04 cross-tree-spine discipline, and the reason is recorded here.

- **Part (3), the numerics tiers — the chapter's numeric subject.** sin(x) at Precise / Approx / Fast,
  each asserted inside the bound THIS tree declares for it (`runtime/periodic_cores.hpp`: Approx 4
  terms worst 3.1e-7, Fast 3 terms worst 3.7e-5; Precise runs the exact core, ~machine precision).
  Measured 1.11e-16 / 1.86e-07 / 2.43e-05 — each under its declared bound — and the tiers asserted
  DISTINCT so a tier silently not taking effect cannot pass vacuously. **No cross-tree compare**: the
  coarse tiers are each tree's own minimax/series cores, so a Fast-tier sin here and on the old tree
  are two different approximations by design — there is no shared golden, and comparing them would
  measure the difference between two approximations rather than either's correctness. Q-1 is NOT
  re-opened — the accuracy-tier split is decided and implemented; ch06 only USES the existing tiers.
  Needed a `sin` frontend spelling (op::Sin, closed M4; two forms — inherit and per-call tier), added
  under Option A, and F64 storage so the tiers separate by CORE accuracy not f32 storage noise.
- **Part (2), the named diagnostic.** A deliberately shape-mismatched graph (x{4}+y{8}) refused by
  `infer()` at RUNTIME (the dual-mode point: the same pass that runs under consteval runs as plain
  data) with the message "shapes disagree at _add: [4] and [8]". SHOWN REJECTING — pointed at a
  broken input and observed to object, message asserted to name the op.
- **Part (1), run_pipeline — re-expressed, not ported.** The old `run_pipeline(g,t)` returns a Pipeline
  with stages/transforms/workers and an arena size. This tree has no cost-model autoscheduler with
  those (schedule.hpp: Target = {share_storage, Machine}; the same fact ch08 records), so the
  run_pipeline/Pipeline shape does not port. Its SUBJECT — the compiler's decisions are inspectable
  VALUES, not a hidden consteval-only step — re-expresses two ways this tree already has: `infer()`
  returns a Diagnostic value (part 2), and `compile<G>()`/`emit()` produce a Schedule/Program whose
  step and arena facts are compile-time CONSTANTS (shown on the sin graph: steps=1 momentary=1
  history=1). The old part (1)'s RMS-normalize graph is the same graph ch08 already built and
  cross-compared to the old tree, so it is not rebuilt here — recorded, not duplicated.

**Tutorial S5 status after ch06.** Chapters 01, 02, 03, 08 (full cross-tree spine) and 04 (recurrence
core), 06 (self-contained) are ported. Remaining reachable: none on the frontend as authorized —
chapters 05, 07, 10, 11 each await a reserved decision (runtime-extent, autodiff frontend) or a later
milestone (M8.5 plugins, M13 backend); ch12 → M9.1. The tutorial half of M8.3 is now at its Option-A
ceiling; the examples (S3-by-need), benchmarks (S6), M8.4/M8.5/M8.6 remain for M8.

## Decision 28 — M8.4: one spelling everywhere — the mechanical gate, and the alias review (2026-09-05)

`02-MILESTONES.md` §M8.4 asks that every example, tutorial and test graph use the same spelling of the
language, with **a check for whatever part is mechanical and the rest recorded as reviewed with the
date**. Standing rule S-2 (amendment 02 A2-2) has carried this by fresh audit since M1; M8 is where the
user-facing surface first exists at scale, so it earns a gate.

**The mechanical half — `gate_one_spelling` (`tests/gates/one_spelling.cmake`).** There is one way to
add a node or a value to a graph *by hand*, and it is `tests/support/hand_built_graph.hpp` — which says
so itself: "two copies of 'how a test adds a node' drift, and then two tests that look like they are
testing the same thing are not." A user-facing program does not add nodes by hand at all: it writes the
language (`frontend/graph_builder.hpp`'s `Expr` spellings, or the `notation/` headers), which lowers to
nodes; a test that needs a graph the language cannot spell yet calls the shared helper. So the raw
graph-mutation idiom — `g.nodes.push_back(...)` / `g.values.push_back(...)` — belongs in exactly one
file across the whole test tree. The gate scans `tests/` (174 files at this commit) and confines that
idiom to `hand_built_graph.hpp`. It is **shown objecting**: `gate_one_spelling_would_object` points it
at `gates/fixtures/one_spelling/adds_a_node_inline.hpp`, a program that builds a graph inline, and the
gate is observed to name the offending file and idiom (WILL_FAIL). What it deliberately does not catch:
a throwaway `Node{op::Add}` fed to a predicate (`lane_cost_test.cpp`, `step_may_lane`) is not adding to
a graph; registering a named input/output (`g.inputs.push_back`) is metadata, not a node or value.

**EXEMPT — the recorded non-mechanical judgement (three files where the raw IR is the SUBJECT under
test, not a graph spelled a second way):**
- `unit/graph_value_test.cpp` — the graph-as-a-value property (a whole `Graph` usable as a template
  argument) is asserted directly over the raw `Value`/`Node` aggregates; routing through the helper
  would hide the very types it is checking are plain fixed-size values.
- `unit/inference_test.cpp` — injects one `Const` value carrying a payload (`0.5`) that `add_value` does
  not set, to test that a multiply-by-const infers `F32`; the const machinery is the subject.
- `compile_time/canonical_renumber.hpp` — a measured-but-not-adopted renumber candidate (Q-2 reference
  number, operator 2026-08-25) that copies a graph node by node; guard-only, outside the default build.

*(`select` went in Decision 59: `where` is the one spelling of the blend.)*

**The alias half — reviewed 2026-09-05, no change required.** "Same spelling of the language" at the
level of *which synonym* is not mechanical: the frontend offers deliberate surface aliases — `where`
≡ `select` (`graph_builder.hpp:377`), `clamp` ≡ `min(max(...))`, `abs` ≡ `max(x,-x)` — provided so a
numpy-facing program and a native one each read naturally. Reviewed across every user-facing program
(the tutorial chapters and the parity `_here` programs): **no single program mixes two synonyms of the
same operation** (checked mechanically as a review aid — `conditionals_here` uses `select` throughout,
`tutorial_02_here` uses `where` throughout; the apparent overlap was a `BuildContext` variable named
`where` and prose in comments, not a second call). The cross-program split (`where` in the teaching
chapter, `select` in the ports) is the intended-alias choice, not drift, and no program reaches for the
`apply(op::…)` / `one_node(...)` primitive under the named spellings. This is the recorded reviewed
half; if a future port introduces a genuine same-operation double-spelling, the fix is to standardise
that program, and this record is where the review is renewed with its new date.

**M8.4 as it binds:** the mechanical idiom-confinement is a gate in ctest, shown objecting; the
alias-level consistency is reviewed and dated here. Blind spot stated for `docs/decisions.md`'s M8
blind-spot list: the gate proves how a graph is *built* (one hand-builder), not which *synonym* a
correctly-built graph uses — that is the reviewed half by construction, because a machine cannot read
`where` and `select` as the same intent without the semantic model M8.4 declines to require.

## Decision 29 — M8.5: the plugin adapters — Vamp arm built here, JUCE arm host-blocked

M8.5 asks for plugin adapters with checks that compare a plugin's output against the same graph run
directly, registered so they run WITHOUT the test suite enabled — the shape the old tree's two plugin
parity checks (`examples/{juce,vamp}_plugin/parity_check.cpp`) take. Gerald decided 2026-09-05 01:12:
*build BOTH JUCE and Vamp parity; install/fetch what's missing.*

**The Vamp arm is built and closed here.** `include/heinzel/integrations/vamp/{plugin_traits,graph_plugin}.hpp`
wrap a compiled graph as a `Vamp::Plugin` over THIS tree's run surface (`run(g, feeds, out)` +
`sample_at`), and `integrations/vamp/parity_check.cpp` is its OWN executable, registered under
`HEINZEL_BUILD_VAMP` **independent of `HEINZEL_BUILD_TESTS`** (verified: `-DHEINZEL_BUILD_VAMP=ON
-DHEINZEL_BUILD_TESTS=OFF` still builds and runs it via ctest). It drives the plugin through the
`Vamp::Plugin` interface over three graphs — a stateless all-knob graph (also fed a stream in
non-N-aligned host chunks reframed to N-blocks), a recurrent one-pole (a single N-block), and an
`fs`-using graph (auto-fed the host rate) — and asserts every emitted feature **bit-identical** to the
same graph run directly, read at the block's last sample: 36 blocks, all identical. The `vamp_sdk` is
vendored (`deps/vamp-plugin-sdk/`, BSD, see PROVENANCE.md), opt-in, out of the default build/ctest.

> Reversed by Decision 48 (P5): the adapter runs `run_nest` since P5 M6 (the walk is its parity oracle).

**The rewrite adapter is a faithful re-expression, NOT a port** (F-M8-LAYERS-ARE-NOT-A-PORT / the
Decision 15/18 disposition mechanism — a layer-disposition within remit, not one of the two reserved
questions):
- a **parameter** is an ordinary named input fed one constant number per block — there is no runtime
  `param` category here, and Vamp fixes parameters from `initialise()` until destruction, so a
  per-block constant is exactly right;
- `fs`/`sample_rate` is auto-fed the host input rate, never exposed as a knob (the host-set/knob split
  the old tree draws);
- there is **no cross-`run()` streaming state** (Decision 27), so a wrapped graph's faithful unit is
  **one fixed-N block per `process()`** — which is precisely what "a plugin's output vs the same graph
  run directly" asks. A stateless graph processes successive independent N-blocks; a recurrent graph's
  faithful unit is the single N-block (multi-block recurrent carry is the Decision-27 non-feature, not
  a parity gap).

**The JUCE arm is host-blocked and reported, not silently re-dispositioned.** `juce_audio_processors`
needs ALSA and (via `juce_gui_basics`) X11/freetype/GL to build even headless; on this host all of
alsa/x11/freetype2/gl are absent (pkg-config + `/usr/include`) and there is no sudo — the exact
"genuinely cannot build without root" case the 01:12 decision names as STOP-and-report. Reported to
the origin (telegram topic 8) with the recommendation: the JUCE adapter has the same substance as the
Vamp one (route named IO/params, reframe the block, add no DSP) and is buildable wherever those libs
exist; Gerald decides whether to build it on a JUCE-capable host before release. **M8.5's JUCE arm is
therefore an open host-dependent item, tracked here** — its exit is met by the Vamp arm on this host
and by the recorded, buildable-elsewhere JUCE disposition.

**O12 boundary (M8.1 family, extended).** `core_no_integrations`: `heinzel/integrations/` is the
outermost layer — it builds ON the core and pulls in an external SDK — so no core / dsp / notation /
frontend header may include one of its headers. The gate scans `include/`, forbids
`heinzel/integrations/`, exempts the integrations subtree, and is **shown objecting** to a fixture core
header that reaches into it (PASS_REGULAR_EXPRESSION pinning file:line, per finding C-4).

**Blind spots for the M8 list.** The Vamp parity proves the bridge adds no DSP over the *interpret* run
surface; it does not re-run the same graph through *execute*/*codegen* (those surfaces are M8.3's cross-
tree parity subject, not the adapter's). And the JUCE arm's *substance* (block reframing over a variable
host block) is proven here only through the Vamp path's harness-side reframe, not through a JUCE
`processBlock` — because the JUCE SDK will not build on this host.

## Decision 30 — Q-2 RESOLVED: name each subexpression; enforcement is the milestone gcc run, not a brittle gate

Q-2 (`04-OPEN-QUESTIONS.md` §Q-2), reserved since 2026-08-25 with a hard deadline of M9, is **decided,
not deferred** — Gerald, 2026-09-05 01:12: adopt the coordinator's recommended fix, *make naming each
sub-expression the standing discipline.*

**The mechanism, and why naming fixes it.** An `Expr` carries an arena (`frontend/graph_builder.hpp`),
and combining two expressions merges their arenas — appending one node list onto the other. Within a
single statement, the order in which C++ evaluates the operands of `a + b` is unspecified, so the merge
order (hence the final node order) can differ between compilers. Under gcc 15.2 a filter recurrence
written as one expression builds one node order while compiling and a permutation of it while running;
clang chooses consistently, so only a second compiler sees it at all. **The numbers are unaffected
(zero differing samples)** — what it touches is a graph used as a *template argument*, where two
translation units could mangle the same filter differently (M6 generated code, M8 plugin adapters, M9
compile cache). Naming each subexpression as its own statement **sequences** the merges, so both
compilers agree in both modes. The operator chose this over the canonical-walk option deliberately: the
walk would also silently drop unreachable nodes and turn input order into discovery order (two further
decisions), whereas the discipline costs nothing at run time.

**Enforcement — the design call the operator left to the coordinator.** A mechanical
"each subexpression is named" gate would have to read C++ expression structure from source text; a regex
approximation would be false-positive noise and could not actually verify the property — a cargo-cult
gate (SOUL). The property is *cross-compiler node-order stability*, and the tool that actually reveals a
violation is a **second compiler** — which this project already runs at **every milestone boundary**
(S-5 / amendment 01, gcc). A graph whose node order depends on evaluation order is exactly what the gcc
boundary run surfaces. So the enforcement is: the discipline is the standing rule (stated here and
applied to new frontend graphs, including the M8.5 parity graphs), and the mandated gcc boundary run is
the cross-compiler check that keeps it honest. No new default-suite gate is added, because under clang
alone such a test is a tautology and under gcc the boundary run already is the test.

**Separate from the gcc-only runtime divergence.** The flagged gcc-only divergence in the three core
tests (conservation/execution/symbolic) is its OWN bug to chase at the M8 boundary — it is a runtime
number difference, not the template-argument identity question Q-2 names. Q-2 is closed; that bug is
not, and Decision 29's blind-spot list does not absorb it.

## Decision 31 — the gcc-only 3-test divergence WAS Q-2 (finding Q-2/GCC-ARENA-ORDER); fix applied, conservation re-recorded

Decision 30's closing paragraph called the gcc-only divergence in the three core tests
(conservation/execution/symbolic) "its OWN bug ... not the template-argument identity question Q-2
names." **That was wrong, and this decision corrects it** (operator decision 2026-09-05 10:46, "close
M8 & M9 via the no-sudo achievable path", item 3).

**What was actually observed.** The three gcc-failing tests each compared a graph built at *runtime*
against its *constexpr*-built twin and found them unequal under g++ 15.2. Direct inspection showed the
coefficients (`1.27279221`, `-0.81`, `0.5`) **bit-identical** — only their const/mul nodes landed in
**permuted arena slots**. `biquad` was written `x + 1.27279221*y.previous(1) + -0.81*y.previous(2)`:
the two products are the operands of `operator+`, whose evaluation order C++ leaves unsequenced, and
each operand's evaluation is what inserts its nodes into the arena. gcc picks one order while constant-
evaluating and a different one in generated code; clang happens to pick the same order on both paths,
which is the only reason clang never saw it. There is no computed float and no transcendental in the
construction, so "excess intermediate precision on float coefficients" (the characterization carried
since M3) cannot be the cause. **This is exactly Q-2 — construction order depending on evaluation
order — and the numbers are provably unaffected.** It is a latent portability defect the discipline of
Decision 30 was made to prevent; clang merely hid it.

**Fix applied (this commit).** The Decision 30 name-each-subexpression discipline, applied to the two
diverging builders in `tests/support/filters.hpp` (`biquad`, `comb`). Each product is named before
combining, which sequences the arena insertions so every compiler agrees on one order in both modes.
Verified: the three tests pass under **both** clang 22.1.8 and g++ 15.2 — full suite 146/146 on each.
gcc == clang is the check, not the good-case assertion.

**Why it is not hash-neutral, and the deliberate re-record.** Naming moves the arena to a *third*
order, different from the one the conservation corpus recorded. `biquad` is corpus entries 0, 1, 3, 6,
and the conservation hash is a structural walk over the arena, so those four entries' hashes move —
including the **graph** number, which no prior re-record touched, because this is the first re-record
whose change is a graph's construction. `comb` (entry 2) and the other seven entries are byte-for-byte
unchanged (comb took the same edit but clang's arena order for it was already the named order). The
relations the corpus exists to check all survive: entry 3 == entry 0 in all four numbers; entry 6
shares entry 0's three artifact numbers and differs in the program; entry 1 differs from entry 0 in
placement and program only. Per the operator authorization, the conservation hashes are **re-recorded
in the same commit** as the fix, with the reason stated here and in `tests/conservation/recorded.hpp`:
the representation (arena order) changed while behaviour is bit-for-bit identical. This is the
deliberate, documented re-record the corpus rule permits, not the routine kind taken because the
numbers went red.

**Q-2 stays closed** (Decision 30). This decision does not reopen it; it corrects the
mischaracterization that the runtime divergence was a separate float bug, and records that the closing
discipline, once applied to the graph that was actually diverging, eliminates it.

## Decision 32 — M8/M9 recorded absences: the Python surface and two M8.3 examples close on the streaming subset

Operator decision 2026-09-05 10:46 ("close M8 & M9 via the no-sudo achievable path"), items 1 and 2,
answering the M9 parity-modules finding (plan/M9-FINDING-parity-test-modules-need-reserved-frontend.md)
and the last two M8.3 examples: **Reading (A) — recorded absence.** The Python frontend closes on the
streaming-scalar subset; the modules and examples that need reserved frontend growth are deliberate,
recorded absences, NOT shortfalls. M9.1 explicitly permits "absence is a recorded decision", so this is
a legitimate close. No frontend is grown for any of them (the large frontend growth stays a future
EXPLICIT decision, minted like MW/MB — see the 10:46 boundary note).

**The Python surface is the streaming-scalar subset, by design across M9.** `python/heinzel/dsl.py`
`__all__` and `frontend/graph_builder.hpp` spell only arithmetic, the transcendentals, max/min/clamp/
select/where/delayed, and `Variable` — no reduction, contraction, transpose, order statistic, gradient
entry, or wide element axis. The structural surface (`notation/structural_ops.hpp`) is opt-in C++ that
the Python package does not import; order statistics (`_order`) have M5 analysis but no user-facing
builder in any header. So from Python there is no way to construct these, and that scoping was audited
into the dsl/_jit docstrings deliberately.

**M9 parity modules recorded as deliberate absences (M9.1 / M9.2 / R-S6):**
- `test_order_stats` — needs an `_order` builder AND a wide element axis, neither of which the Python
  surface exposes. R-S6's diagnostic ("needs every extent known now …", the `ExtentsKnownNow`
  precondition at sema/analyze.hpp — this tree's analogue of the old "must be static in v1") is pinned
  at the **C++** test layer where `_order` actually lives; it is unreachable from Python because every
  op carrying that precondition is structural/spectral with no Python spelling.
- `test_autodiff` — **[C++ side SUPERSEDED by Decision 33 / MG — the C++ autodiff examples fir_fit,
  mlp_train and tutorial ch07 are now BUILT; the PYTHON `test_autodiff` stays absent until the Python
  gradient surface is grown (item b).]** `gradients()` is M7 with no Python gradient entry; the headline
  autodiff examples need reductions + matmul + batch (reserved MB axis-model + frontend growth).
- `test_layout_golden` — layout is a compile-time/codegen concern; host target is compile-time
  (prior decision), so there is no Python layout knob to golden.
- `test_gpu` — no GPU back end and no Python GPU surface; recorded absence, consistent with the above.
- the non-streaming examples/doc-snippets (dense_layer, layernorm, attention, spectrogram_einops, and
  any snippet using `x.mean(axis)` / reductions) — all need the reserved batch/broadcast axis-model
  (MB) plus a Python spelling for it.

The streaming-subset parity modules remain present and green: test_basic, test_params, test_streaming,
test_multi_io, test_dsl, test_jit, test_cache, test_errors, test_audio, and the streaming-only
test_examples / test_doc_snippets (relu, gain, one-pole, biquad).

**Two M8.3 examples recorded as reserved absences (item 2):**
- `fir_fit` — **[SUPERSEDED for the C++ side by Decision 33 / MG — now BUILT.]** needs the autodiff
  **frontend** surface (`gradients`/`unfold`/`as_output` spellings). Reachable in principle (all lower
  to closed M5/M7 ops, F-M7-GRAPH2GRAPH — no new op semantics), but growing the frontend for it is the
  reserved frontend-growth work, not authorized by the 10:46 steer. Recorded absence.
- `energy_fraction` — needs `dynamic_length`'s **runtime-extent** (a runtime-resolved block length),
  which is a SEPARATE reserved item (stop-and-ask, per the batch-axis-model decision). Recorded absence.

Both are recorded rather than built for the same reason as item 1: the 10:46 decision closes the
achievable-without-frontend-growth path and explicitly does not authorize the reserved frontend growth.

## Decision 29 addendum — M8.5 JUCE arm confirmed buildable-elsewhere by the operator (item 4)

The 10:46 decision, item 4, confirms Decision 29's disposition as the operator's final word for this
task: **M8.5 closes on the Vamp arm** (built here, adapter parity bit-identical, runs without the
suite), and the **JUCE arm is recorded as blocked-on-host-system-libs / buildable-elsewhere** — no
ALSA/X11/freetype/GL and no sudo on this host, same adapter shape, builds unchanged wherever those libs
exist, built on a JUCE-capable host before release (M10, Gerald's alone). This is the "without sudo"
case the steer names; it is not a silent re-disposition.

## Decision 33 — MG (frontend growth, item a): the C++ autodiff frontend + three examples are BUILT (revises Decision 32)

Operator decision 2026-09-05 10:55 "Grow the frontend", reopened 19:01 ("I authorize the frontend
growth"). Milestone MG builds the user-facing autodiff/gradients capture spelling and the C++ examples
that were recorded as absences under Decision 32's Reading (A). Those specific absences are **revised
from "recorded absent" to BUILT** — the reopen mandate's instruction in as many words.

**What MG built (each additive, every prior test still green, each spelling lowering to an already-tested
op — no new op or numeric meaning):**

- **MG S1 — multi-output capture** (`include/heinzel/frontend/capture.hpp`): a graph may declare several
  named outputs, `finish(where, {Output...})`, sharing one forward so a value feeding two outputs is
  computed once. Test group `frontend_outputs`.
- **MG S2 — the `gradients` capture spelling** (`tests/support/gradients_capture.hpp`): `finish_gradients`
  composes the frontend graph with the closed M7 `heinzel::autodiff::gradients`, naming the loss and each
  gradient as outputs. It lives in the application/leaf layer, ABOVE the O12 `core_no_frontend` and A4-2
  `autodiff-is-a-leaf` boundaries (a spelling needing BOTH leaves cannot be a core header) — shipping it
  to library users as an exported header is a packaging decision reserved for Gerald (recorded in the
  header, reported to the origin). Test group `frontend_gradients`.
- **MG S3 — `fir_fit`** (`tests/parity/fir_fit_{here,there}.cpp`): the differentiable FIR coefficient
  fit, trained 400 SGD steps and output-compared to the old tree; ALLOWED 4.1e-07 (measured).
- **MG S4 — `mlp_train`** (`tests/parity/mlp_train_{here,there}.cpp`): the 2-layer ReLU MLP trained 600
  SGD steps and compared; ALLOWED 3.0e-08 (measured, one ULP).
- **MG S5 — tutorial ch07** (`tests/tutorial/tutorial_07_{here,there}.cpp`): the autodiff chapter's
  direct FIR fit, 4000 steps, compared; ALLOWED 2.8e-06 (measured).

**Two implementation choices, recorded because they matter to a future reader:**

1. **The matmul/contraction backward uses `_contract` (op 39), never the general `_matmul` (op 56).**
   `mlp_forward`'s einsum lowered each layer to `_matmul`, whose adjoint is deliberately not written
   (rules_contract.hpp: a transposing/batch contraction "is not reachable"). fir_fit and ch07 spell the
   FIR as the old `dsp::fir` weight-and-reduce (`reduce(unfold(x,K,1)*w, Sum, tap axis)`), and mlp_train
   spells each layer with `notation::contract` (the restricted contraction) — every op the M7 autodiff
   already differentiates (`adjoint_reduce`, `adjoint_structural`, `adjoint_contract`). The general
   `_matmul` adjoint stays a future backend-shaped item, not part of MG.
2. **The three examples run on the SCHEDULED runner (compile/execute), not the walk.** A forward+backward
   graph is wide (fir_fit 3205 interpret planes, mlp_train ~6988); the walk's `Signals` is fixed at
   `kMaxPlanes` and would multiply that with `BlockSize` on the stack (a BlockSize-256 tutorial would
   overflow at kMaxPlanes 4096). The scheduled workspace is plan-sized with buffer reuse, so the wide
   graphs run small and **kMaxPlanes is unchanged (768)** — the amendment-03 lesson applied: the cost is
   a bigger graph's data footprint, but the remedy is the right runner, not a global stack multiplier.

**What STAYS absent (unchanged from Decision 32 — future items b/c, minted like MW/MB, or reserved):**
- The **Python** `test_autodiff` and the whole Python gradient surface (item b — no Python gradient entry
  yet).
- `test_order_stats`, `test_layout_golden`, non-streaming Python examples (item b — tensor/order-stats
  Python surface).
- `energy_fraction`, `dynamic_length`, tutorials 05 & 10 (item c — runtime-resolved extents + audio
  CLI/WAV).
- `test_gpu`, spectral/FFT (M13 backend, not a frontend spelling); M10 (Gerald's alone).

**Verified at the MG boundary:** the full suite **151/151** green from a clean clang-22.1.8 configure AND
from a clean gcc-15.2.0 configure (S-5 / amendment 01 — no gcc divergence on the three new examples);
MG.1's and MG.2's refusals shown rejecting (bare input/literal output; loss-not-computed, non-input
target); the amendment-06 structure gate green over the new files (every function ≤60, files <600); the
ctg3 boundary clean throughout (empty, c4012b8, main); nothing pushed; A6-1 held (zero delegated agents).

## Decision 34 — MT (frontend growth, item b — tensor Python surface, part 1): wide inputs + element-axis reductions + layernorm are BUILT (revises Decision 32)

The operator reopened the frontend growth (2026-09-05 19:01, re-confirming the 10:55 "grow the
frontend"). Item (b) — the tensor/order-stats Python surface — was recorded under Decision 32 (and
carried forward by Decision 33) as absent. Milestone **MT** grows the first part of it. These absences
are **revised from absent to BUILT**:

- **The Python DSL takes a WIDE input** (MT S1, `dsl.py` `Graph.input(name, width)`): `width>1` emits
  `input(where, name, Shape{W, hzpy_block})` — the element axis outer, the streamed (time) axis
  innermost — the same wide input the C++ frontend and the multi-input run-surface (MW) already take.
  `width==1` emits the byte-identical `Shape{hzpy_block}`, so a scalar graph's cache identity is
  unchanged. No wrapper change: the shim already reports each input's per-sample width
  (`elements_of().count`) and the wrapper feeds/reads `width*block` doubles per named boundary.
- **Element-axis reductions** (MT S2, `heinzel.reduce(x, how, axes, keepdims)`): the generated TU now
  includes the opt-in `notation/structural_ops.hpp`, and the spelling emits
  `notation::reduce(v, ReduceOp::How, Axes{...})` — lowering to the **M5 `_reduce` op, no new numeric
  meaning**. `how` is one of sum/mean/prod/max/min. Axes count the C++ way: the streamed axis is the
  innermost/last, the element axes lead. Agrees with numpy on both the interpreter and generated (O6)
  paths; a reduction aimed at the streamed axis is shown rejecting.
> Reversed by Decision 48 (P5): Python runs the nest path only (shim ABI 3); there is no interpreter or
> generated (O6) path beside it.
- **`layernorm`** (MT S3, `python/examples/layernorm.py`): the old tree's deferred example, ported to
  this tree's functional surface — x/gamma/beta as width-D inputs, the feature mean/var as keepdims
  reductions over the element axis, F32 to match the old float32. Run and output-compared to the same
  numpy layernorm the old tree compared against (max abs err 4.1e-07, old tolerance 1e-4); the generated
  path matches the interpreted. Registered in `test_examples.py`.

**A recorded finding (MT S2), surfaced not fixed here.** Reducing the streamed axis is caught by a
constexpr `fail` at the compute/emit layer (`runtime/elements.hpp` `collapse_over: this collapses the
streamed axis, which a run here cannot do`), NOT by the analysis pass — `check_attributes` has clauses
for `_order`/`_matmul`/view/fft/overlap/table/iota but none for `_reduce`. The rejection is genuine
(the graph does not compile), but it reaches Python as a generic `HeinzelCompileError`, not a verbatim
analysis diagnostic the way an out-of-range `_order` axis does. Adding a clean `_reduce` axis clause to
`check_attributes` is a small, worthwhile follow-up; it is not required for MT's conditions (shown
rejecting holds) and was left for a later step rather than folded speculatively into this milestone.

**What STAYS absent after MT (unchanged, future milestones or reserved):**
- `transpose`/`reshape`/`contract` Python spellings and the examples that need them (dense_layer,
  attention need a contraction; einops/stencil need transpose/reshape/unfold) — added on demand with the
  example that first needs them (the operator's "each new spelling lowers to a tested op; no
  speculation" discipline), the next tensor-surface milestone.
- The **Python** `test_autodiff` and gradient surface (item b, gradient half).
- `test_order_stats` — the `_order` op has NO C++ frontend builder at all (M9-FINDING); it needs one
  first, a separately minted milestone.
- `test_layout_golden` (layout is a compile-time/codegen concern, no Python knob).
- `energy_fraction`, `dynamic_length`, tutorials 05 & 10 (item c — runtime-resolved extents + audio
  CLI/WAV).
- `test_gpu`, spectral/FFT (M13 backend); M10 (Gerald's alone).

**Verified at the MT boundary:** the full suite **152/152** green from a clean clang-22.1.8 configure
with `HEINZEL_BUILD_PYTHON=ON` (88 pytest inside) AND from a clean gcc-15.2.0 configure (S-5 / amendment
01); MT.1's wrong-length-feed refusal and MT.2's streamed-axis refusal shown rejecting; the amendment-06
structure gate green; the ctg3 boundary clean throughout (empty, c4012b8, main); nothing pushed; A6-1
held (zero delegated agents).

## Decision 35 — ME (frontend growth, item b — einsum/contraction Python surface, part 2): multi-axis inputs + einsum + dense_layer/einsum_contractions are BUILT (revises Decisions 32/34)

Milestone **ME** grows the second part of item (b): the einsum contraction surface. These items,
recorded absent under Decision 32 and still open after MT (Decision 34), are **revised to BUILT**:

- **A multi-axis element input** (ME S1, `dsl.py` `Graph.input(name, shape=[a, b, ...])`): emits
  `Shape{a, b, ..., hzpy_block}` — several leading element axes with the streamed axis appended
  innermost, the operand shape an einsum contraction takes. The `width=` paths are unchanged and
  byte-identical (scalar cache identity holds); `width` and `shape` together are refused.
- **`heinzel.einsum(spec, *operands)`** (ME S2): emits `notation::einsum("spec", v...)`, lowering to
  `structural_ops`' contract/matmul — **no new op, no new meaning**. `spec` is validated to the einsum
  alphabet and required to carry `->` (which also forecloses smuggling characters into the C++ literal).
  A matmul `"ikt,kjt->ijt"` and a batched matmul `"bikt,bkjt->bijt"` agree with numpy on both run paths;
  the three-operand chain (M8.2 cost-model exclusion — an n-ary chain must choose a pairing order) and a
  spec omitting the streamed-axis label are shown rejecting.
- **`einsum_contractions`** (ME S3) and **`dense_layer`** (ME S4): the old tree's deferred contraction
  examples ported to Python. A whole-tensor (timeless) contraction is spelled at block=1 (its length-1
  streamed axis). Compared to numpy — matmul 4.3e-07, batched 4.5e-07, dense_layer 5.9e-08 (all under
  1e-5); the generated (O6) path matches the interpreted. A weight matrix is just another input; no
  constant-tensor spelling was needed. The three-operand chain stays the M8.2 recorded exclusion.

> Reversed by Decision 48 (P5): there is no `block` since P5 — one build serves every length, the stream
> bound per call; a timeless tensor is fed one sample.

**The mapping, recorded for a future reader.** This tree keeps the streamed axis innermost, so a
timeless whole-tensor contraction gets a length-1 streamed axis and the einsum spec names it LAST
(`"ikt,kjt->ijt"`); the operand shapes are `Shape{..., 1}` compiled at block=1. This is exactly the
mapping the C++ parity program `tests/parity/einsum_contractions_here.cpp` uses.

**What STAYS absent after ME (unchanged, future milestones or reserved):**
- `transpose`/`reshape` Python spellings and the einops/rearrange/stencil examples that need them
  (added on demand with the example that first needs them).
- `attention` (needs softmax composed over a contraction — more than one layer's plumbing).
- The **Python** `test_autodiff`/gradient surface (item b, gradient half).
- `test_order_stats` — the `_order` op has NO C++ frontend builder (M9-FINDING); a separately minted
  milestone must build one first.
- `test_layout_golden`; item (c) runtime-resolved extents + audio CLI/WAV; `test_gpu`, spectral/FFT
  (M13); M10.

**Verified at the ME boundary:** the full suite **152/152** green from a clean clang-22.1.8 configure
with `HEINZEL_BUILD_PYTHON=ON` (100 pytest inside) AND from a clean gcc-15.2.0 configure (S-5 / amendment
01); ME.1's wrong-length-feed refusal, ME.2's three-operand and missing-stream-label refusals shown
rejecting; the amendment-06 structure gate green; the ctg3 boundary clean throughout (empty, c4012b8,
main); nothing pushed; A6-1 held (zero delegated agents).

## Decision 36 — MR (frontend growth, item b — rearrange/einops Python surface, part 3): rearrange + einops_pipeline are BUILT (revises Decisions 32/34/35)

Milestone **MR** grows the einops rearrange verb. Revised from absent to BUILT:

- **`heinzel.rearrange(x, spec, sizes)`** (MR S1): emits `notation::rearrange("spec", v, {{...}})`,
  lowering to `structural_ops`' reshape/transpose — **no new op, no new meaning**. spec validated to the
  einops alphabet (letters, parens, `->`) with a required `->`; split-size names alphabetic, sizes
  positive ints. A split `"(c f) s -> c f s"` and a transpose `"c f s -> f c s"` agree with numpy on both
  run paths; a spec that moves the streamed axis is shown rejecting (the streamed axis passes through
  whole).
- **`einops_pipeline`** (MR S2, `python/examples/einops_pipeline.py`): x{B,C*F,T} split then mean-folded
  over f and t at block=1, vs numpy — max abs err 3.8e-07 (under 1e-5); generated path matches. Uses the
  MT axes-based `reduce` for the fold, so MR did not need the string-`reduce` overload.

**Still absent after MR (unchanged, future milestones or reserved):** `attention` (softmax over a
contraction, more than one layer's plumbing); the **Python** `test_autodiff`/gradient surface;
`test_order_stats` (needs a NEW C++ `_order` builder — none exists); `test_layout_golden`; item (c)
runtime-resolved extents + audio CLI/WAV; `test_gpu`, spectral/FFT (M13); M10.

**Verified at the MR boundary:** the full suite **152/152** green from a clean clang-22.1.8 configure
with `HEINZEL_BUILD_PYTHON=ON` (105 pytest inside) AND from a clean gcc-15.2.0 configure (S-5 / amendment
01 — no divergence); MR.1's streamed-axis-move refusal shown rejecting; the amendment-06 structure gate
green; the ctg3 boundary clean throughout (empty, c4012b8, main); nothing pushed; A6-1 held.

## Decision 37 — MO (frontend growth, item b — order-statistics surface): the C++ `_order` builder + the Python median/quantile/sort/topk/median_filter surface + `test_order_stats` are BUILT (revises Decisions 32/34/35/36)

Milestone **MO** builds the order-statistics surface. This closes the `test_order_stats` gap that
Decisions 32/34/35/36 recorded as absent "needs a NEW C++ `_order` builder — none exists": the builder
now exists, and the Python surface above it is built. Revised from absent to BUILT:

- **The C++ `notation::order` builder** (MO S1, `include/heinzel/notation/order.hpp`):
  `order/sort/topk/quantile/median` append exactly the `op::Order` node the hand-built `OrderAttr` path
  builds (`tests/unit/order_test.cpp`, `tests/parity/order_stats_here.cpp`) — **no new op, no new
  meaning**; the M5 `sema/order_checks.hpp` validates the same attribute. `order()`'s `k` defaults to 0,
  the value the hand path carries for every non-TopK op, so `sort`/`quantile`/`median` produce the SAME
  `OrderAttr` under the defaulted `operator==` (pinned for all four in `order_builder_test.cpp`). The
  file is exempted from the `gate_one_writing_per_operation` gate for the identical reason
  `structural_ops.hpp` is: it names `op::Order` to CONSTRUCT the node, not to write arithmetic.
- **The Python surface** (MO S2, `heinzel.sort/topk/quantile/median`, `dsl.py`): each emits
  `notation::{sort,topk,quantile,median}(v, axis, …)` and lowers through the S1 builder to `_order` — no
  new meaning. median/quantile over each element axis, sort ascending, topk descending, and the
  `topk(k=1) == max(keepdims)` anchor agree with numpy on BOTH the interpret and generated (O6) paths;
  ordering the streamed axis and an out-of-range axis are shown rejecting. `argsort` is a named
  exclusion (`_order` produces ordered values, not an index permutation; no stable-sort permutation op
  exists to lower it to — the same exclusion the old test pins).

> Reversed by Decision 48 (P5): on the nest path, the only path, sort, topk, a quantile between two ranks
> and an even median are refused by name until P7 (D-1); the interpret/generated agreement is history.
- **`heinzel.median_filter`** (MO S3): composes `notation::unfold` (frames the element axis into
  `(frames, window)`) + `median` (folds the window axis) — no new op; the axis extent `L` becomes
  `L - window + 1`, matching `np.median(sliding_window_view(v, window), axis=-1)`.
- **`order_stats`** (MO S3, `python/examples/order_stats.py`): the old tree's order-statistics surface
  ported and run/compared to numpy on both paths; registered in `test_examples.py`.

**THE AXIS MODEL (recorded blind spot).** A Python `shape=[a,…,z]` input becomes C++
`Shape{a,…,z,hzpy_block}`: the Python element axes map to the SAME C++ element-axis indices, and the
streamed (time) axis is appended innermost and is never user-named. So a Python `axis` counts element
axes only and — as with `heinzel.reduce` — is explicit and **non-negative**: the Python layer tracks no
rank, so a negative axis cannot be resolved, and a bare `-1` would land on the streamed axis rather than
the last element axis. Ordering the streamed/time axis stays reserved (the `_delay` rule); MO never
touches it.

**Still absent after MO (unchanged, future milestones or reserved):**
- **keepdims on an order statistic** — this tree's `_order` carries no keepdims (only `_reduce` does, via
  `keep_axes`), and a frontend keepdims would need an axis-insert op that does not exist as a tested op;
  adding keepdims to `_order` is a backend change, not a frontend spelling. A descending sort is `topk`
  at the full extent, so no `flip` verb was added.
- The **Python** `test_autodiff`/gradient surface (item a's Python half); `attention` (softmax over a
  contraction); `test_layout_golden`; item (c) runtime-resolved extents + audio CLI/WAV (which the old
  `test_order_stats`' symbolic-`g.dim` rejection case belongs to); `test_gpu`, spectral/FFT (M13); M10.

**Verified at the MO boundary:** the full suite **153/153** green from a clean clang-22.1.8 configure
with `HEINZEL_BUILD_PYTHON=ON` (121 pytest inside) AND the C++ suite **152/152** from a clean gcc-15.2.0
configure (S-5 / amendment 01 — no divergence; `order_builder`/`order_net`/`parity_order_stats` green
under gcc); the builder's streamed-axis + out-of-range refusals and the Python surface's streamed-axis +
out-of-range refusals shown rejecting; the amendment-06 structure gate green (with `order.hpp` under the
600/60 limits and exempted from the one-writing gate for the constructs-a-node reason); the ctg3 boundary
clean throughout (empty, c4012b8, main); nothing pushed; A6-1 held (zero delegated agents).

## Decision 38 — ML (frontend growth, item b): the `test_layout_golden` parity module is BUILT (revises Decisions 32/33/37)

Milestone **ML** builds `test_layout_golden`, one of the 15 parity modules (05-Parity-Inventory §5.10),
recorded absent under Decision 32 and carried forward by Decisions 33/34/35/36/37. It is a **test-only**
module — it introduces NO new spelling, NO new op, NO new numeric meaning — so it is one focused commit,
not a new-capability milestone. Revised from absent to BUILT:

- **`python/tests/test_layout_golden.py`** (ML S1): ported from the old tree's module to this tree's
  functional object DSL. The old module drove the C++-string `heinzel.compile(...)` surface with method
  spellings (`transpose(x,{1,0})`, `x.sum(1)`); this tree's Python surface is the functional builder, so
  the three row-major-layout checks lower to already-tested free-function spellings:
  - **non-square transpose** `{3,4}→{4,3}` via `rearrange(x, "a b s -> b a s")` (MR) — the falsifying
    anchor: a column-major mistake would give a wrong-transposed buffer, so the value compare against
    `m.reshape(3,4).T.reshape(-1)` falsifies a layout bug rather than asserting only the good case;
  - **row-major elementwise 2-D** `{2,3}` `x*2` (M9);
  - **last-element-axis reduction** `{2,3}→{2}` via `reduce(x,"sum",axes=[1])` (MT).
  Each check is proven on BOTH the interpreter and the generated (O6) path against numpy (numpy is the
  golden, exactly as the old module used it). Auto-collected by `python_bindings_suite` (no CMake change).
> Reversed by Decision 48 (P5): Python runs the nest path only (shim ABI 3); there is no interpreter or
> generated (O6) path beside it.

**Layout contract recorded.** (Its `block=1` reversed by Decision 48: there is no `block` since P5.) A timeless 2-D element tensor compiles at `block=1`; an input `shape=[a,b]`
is `Shape{a, b, hzpy_block}` (element axes leading, streamed axis innermost). The boundary buffer is
row-major — element fastest within a sample — so a numpy C-order `reshape(-1)` fed flat, and the output
`reshape(-1)`, line up. This is the same layout the wide/multi-axis input tests (MT/ME/MR) already read
back; ML pins it as the named golden module with the non-square anchor.

**Still absent after ML (unchanged, future milestones or reserved):**
- The **Python** `test_autodiff`/gradient surface (item a's Python half) — **needs the reserved
  gradients-capture packaging decision** (MG boundary finding: the `finish_gradients` spelling composes
  the frontend AND autodiff leaves, so it cannot be a core header and today lives in
  `tests/support/gradients_capture.hpp`; the generated Python TU includes only `include/` headers, so
  exposing it to Python needs either a dedicated application-layer export header the package ships, or a
  relaxed leaf boundary — a packaging/architecture decision reserved for Gerald). Surfaced again at the
  ML boundary report.
- `attention` (softmax over a contraction — needs verification the two einsums are expressible in the
  streaming/batch axis model without a reserved relaxation).
- item (c): runtime-resolved extents + audio CLI/WAV (`energy_fraction`, `dynamic_length`, tutorials
  05 & 10); the old `test_order_stats`' symbolic-`g.dim` rejection case belongs here.
- `test_gpu`, spectral/FFT (M13 backend, not a frontend spelling); M10 (Gerald's alone).

**Verified at the ML boundary:** the full suite **153/153** green from a clean clang-22.1.8 configure
with `HEINZEL_BUILD_PYTHON=ON` (python subsuite **124** pytest inside, was 121) AND **153/153** from a
clean gcc-15.2.0 configure (S-5 / amendment 01 — no divergence; ML adds zero C++, so the C++ suite is
byte-identical and the Python arm skips cleanly under a non-clang compiler as designed); the non-square
transpose anchor confirmed falsifying (value compare, not shape-count alone); the amendment-06 structure
gate green; the ctg3 boundary clean throughout (empty, c4012b8, main); nothing pushed; A6-1 held (zero
delegated agents, audit in-context).

## Decision 39 — MD S1 (frontend growth, item c — runtime-resolved extents, part 1): the frontend can mint a symbolic streamed dimension, statelessly

Milestone **MD** (in progress) grows the frontend for a **runtime-resolved streamed extent**: a block
length `N` declared symbolically at build time and bound to an actual size later, at `prepare()` — the
audio-plugin `prepareToPlay()` shape. This unblocks `dynamic_length` and the shape-generic half of
tutorial 05. Authorized by the 2026-09-05 10:55 grow-the-frontend decision (item c) and the 19:01 reopen,
which lift Decision 25's reservation of the dynamic_length carve-out. MD's later steps add the
prepare()-time resolver (S2) and the runtime-sized executor (S3); **this decision records S1 only** — the
frontend minting surface and the design fork it turned on.

**The design fork, and how it was settled.** The old tree minted a dimension from a *stateful* builder
(`GraphBuilder g; auto N = g.dim("N", {…})`) — a builder-to-leaf channel this tree deliberately removed
(Decision 1: a leaf builds its own one-node graph and is handed nothing). MD may not reintroduce that
channel silently. Three shapes were considered (`plan/MD-STEPS.md`): (A) stateless — the caller owns the
id, constraints ride to the graph at the `input` site the way names and units do; (B) a minimal dim-only
registry object (a partial walk-back of Decision 1); (C) constraints on the input value's attribute only.
**(A) is implemented**, because a `Dim` is already just `{id}` (`ir/vocab.hpp`) and an `Extent` already
encodes a symbol id, so nothing stateful is required: `declare_dim(id, constraints)` returns a plain
`DimDecl`, `extent_of(N)` places the symbol on the innermost axis, and the `input` that first carries it
takes the `DimDecl` as an optional trailing argument. (C) was rejected because a dimension spans several
values, so its constraints belong to the dimension, not to one value; (B) was rejected as a walk-back to
be surfaced, not taken, if (A) ever proved unworkable — it did not. A fresh audit of the real diff
confirmed (A) forces no stateful leaf: `input` records, `finish` collects (deduped by id, conflicting
constraints refused), and nothing persists between calls.

**What is BUILT (MD S1):**
- **`include/heinzel/frontend/dynamic_extent.hpp`** (new, one concern): `DimConstraints{multiple_of, min,
  expect}` and `DimDecl{id, constraints}` (carried in `ir/ir.hpp` so the graph can hold them, the way
  `Units` is), `declare_dim`, `extent_of`, and `record_dim` (dedup + conflict refusal).
- **`ir/ir.hpp`**: `Graph` gains a `fixed_vector<DimDecl, kMaxDimDecls> dims{}` — empty for a static
  graph. Additive: `Graph` stays an NTTP and its defaulted `==` compares the new (empty) table equal.
- **`frontend/graph_builder.hpp`**: `input(...)` takes an optional trailing `DimDecl`; `emit_arena_nodes`
  collects it onto the graph, so both `finish` paths get it. No new operation; the frontend still records
  and decides nothing semantic (its gate stays green).
- **`tests/conservation/artifact_hash.hpp`**: `hash_graph` folds the dims table in **only when
  non-empty** — the identity-default discipline `packed_code` and the widths already use. A fully static
  graph declares none, so **not one recorded conservation hash moved** (no re-record); a declared
  dimension, or a change to one's constraints, does move the hash, checked in the new test.
- **`tests/unit/dynamic_extent_test.cpp`** (new group `dynamic_extent`): a `[2,N]` frontend graph infers,
  is non-static, and its constraints round-trip; a static graph declares none and hashes unmoved while a
  declared dimension moves the hash; one length shared across two inputs is recorded once; a conflicting
  re-declaration and a symbol on a WIDTH axis (`[N,2]`) are both **shown rejecting** — the latter by the
  planner's existing `plan_schedule` objection, which is what keeps MD to the streamed-axis-only scope (a
  symbolic width is the reserved general-axis work, not built here).

**Verified:** the full suite **153/153** green from a clean clang-22.1.8 configure (the new group is the
153rd; the analysis pass already accepts a symbolic streamed length, so no `sema` change was needed);
`extents_are_not_plain_integers`, `frontend_decides_nothing_semantic`, the structure gate and the
conservation oracle all green; ctg3 clean (empty, c4012b8, main); nothing pushed; A6-1 held (zero
delegated agents). MD's boundary (S6) will revise Decision 32's dynamic_length/tutorial-05 absence to
BUILT once the resolver (S2) and runtime executor (S3) land.

## Decision 40 — MD boundary (frontend growth, item c — runtime-resolved extents): dynamic_length and tutorial 05's runtime-dimension section are BUILT; the tutorial's FIR sections are recorded reserved-`_delay` (revises Decisions 25/32)

Milestone **MD** grew the frontend for a **runtime-resolved streamed extent** (Decision 39 recorded S1;
S2 added the prepare()-time resolver, S3 the runtime-sized executor for both the scheduled and generated
paths). This decision records the boundary: the two surfaces that were reserved for exactly this
capability are now BUILT and compared to the old tree at c4012b8, and it revises the reservations that
held them.

**`dynamic_length` — BUILT (revises Decision 25 and Decision 32).** Decision 25 STOPPED `dynamic_length`
at its runtime-resolved extent (the reserved carve-out); Decision 32 carried it as an absence. The
2026-09-05 10:55 grow-the-frontend decision (item c) + the 19:01 reopen lifted that reservation, and MD
built the surface it needed. `dynamic_length` is ported (MD S4): `tests/parity/dynamic_length_here.cpp`
builds `y = x*gain + bias` over a stereo block `[2, N]` (streamed axis innermost — the old tree's
`{N,2}`), N declared with `declare_dim`, validated with `resolve_dimensions`, run at N=256 and N=64
through ONE compiled graph via the runtime-sized executor; `dynamic_length_there.cpp` is the old example
on c4012b8. Both are value-self-checking, so parity is `same PASS + same arena story`
(`dynamic_length_selfcheck.cmake`) — both recover the block effect, and the cross-tree arena-SIZE integer
is deliberately not compared (the arena models differ). The knobs are named inputs fed a constant across
the block (this tree has no runtime Param). Registered: `parity_dynamic_length`.

**tutorial 05's runtime-dimension section — BUILT (revises Decision 32).** Decision 32 recorded
`05_shape_generic_and_dynamic` reserved-runtime-extent. Its "dynamic dimensions" section (`y = x*gain`
over a 1-D block whose length N is resolved at prepare(), run at N=64 and N=96) is ported (MD S5):
`tests/tutorial/tutorial_05_here.cpp` on the MD runtime-extent surface, `tutorial_05_there.cpp` the old
tree's own section. Both value-self-checking (same-PASS via `parity/selfcheck_pair.cmake`); both recover
`y = 2.5*x` at both lengths (max_err 0.000e+00). Registered: `tutorial_05_shape_generic_and_dynamic`.

**tutorial 05's two shape-generic FIR sections — RECORDED RESERVED (reserved-`_delay`, NOT built).** The
"declare now, bind at the end" and "one shape-generic graph, several signatures" sections build a
STREAMING FIR `(x.unfold(4,1) * h).sum(1)` over a 1-D signal. In the old tree the outer axis is the
stream, so that unfold is a moving window ALONG THE STREAM — a lag along the streamed axis, which in this
tree (streamed axis innermost) is the **reserved innermost-axis `_delay` question** the operator kept
separate. Re-casting it as a static outer-axis stencil with a length-1 stream (the `stencil_unfold`
convention, Decision for that example) would change a streaming filter into a batch stencil — a semantics
shift, not a faithful port. So these two sections are reserved (reserved-`_delay`, **not** symbolic-width,
**not** built here); a faithful port awaits the reserved innermost-axis `_delay` decision. This is a
scope finding, not a milestone blocker — MD.5's named half (the runtime dimension) is delivered.
Disposition detail: `plan/MD-S5-NOTES.md`.

**Blind spots (stated, as the structure gate's own discipline requires).** The `dynamic_length` parity's
STORY line is a hardcoded identical string on both sides, so its equality is not independently
discriminating; the SUBSTANTIVE parity is the exit codes (both self-checks pass at both lengths), which is
real. `tutorial_05`'s parity is same-PASS (both recover the same hand reference), not a sample dump — a
dump would only re-derive what each self-check already verified, and would need a tolerance where the
section spans f32(there) vs f64(here).

**Verified:** the full suite **158/158** green from a clean clang-22.1.8 configure, and **158/158** from a
clean **gcc-15.2.0** configure — no divergence at the boundary (amendment 01 / S-5). The structure gate
(`gate_structure_limits` + `_in_tests`) passes over the new files (every new function < 60 lines). The
compile-time guard remains outside the default build and ctest — re-verified, not assumed: the default
configure registers 0 guard tests and builds 0 guard objects, while a guard-ON configure makes the guard
target available yet registers the identical 158-test set (0 guard tests). ctg3 clean (empty, c4012b8,
main); nothing pushed; A6-1 held (zero delegated agents).

## Decision 41 — MA (reopen item b): the generated-path in-place-aliasing bug is FIXED and `attention` is BUILT (revises Decisions 32/33/37)

Authorized by the operator decision **2026-09-06 08:45 (Q3 = "fix it")**: edit the closed,
amendment-06-gated M6 buffer planner to correct a real generated-path miscomputation, then ship the
`attention` example. Both are done.

**The bug.** `schedule/plan.hpp`'s `hand_back_finished_places` handed a momentary value's place back at
the same step its last read happened, on the stated assumption that "running one operation reads all of
its operands before it writes anything, so a result may take the place its own operand has just vacated."
That is true for an **in-place-safe** operation — one whose output element depends only on the
same-position input element — and FALSE for a **structural** operation (a reduction, contraction, view,
gather or transform), whose output reads many input positions. With storage sharing on, `take_slot`
reuses a freed run of EXACTLY the same width, so when `T == D` an attention softmax's `[T,T]` score/attn
temporaries and the `[T,D]` output are one width, and a structural op's output aliased an operand it was
still reading — the scheduled/generated numbers were corrupt (err ~1e0), the interpreter always correct.
Latent because no recorded graph put a same-width structural op on the shared path; every `T != D` case
dodged it by width mismatch.

**The fix.** In-place safety is derived from the rule table's **meaning** column: `MeaningRule::PerSample`
(elementwise and passthrough) is safe, `MeaningRule::AxisChanging` is not. The meaning column (not the
shape column) is the right discriminator because the shape-**passthrough** gather `_tblread` is
`Passthrough`/`AxisChanging` — shape-preserving yet a gather, so unsafe; a shape-derived rule would
misclassify it. An unsafe last-reader's hand-back is deferred by one step, so a structural op's output can
never take a slot it is still reading. This changes buffer LAYOUT only for structural-op graphs and
NUMBERS only for the previously-wrong ones. It asks the table rather than naming an operation, so it keeps
the `gate_planner_names_no_operation` discipline.

**No protected anchor moved, so nothing was re-recorded.** The full suite was **158/158** green (including
`conservation` and `schedule`) after the fix and BEFORE the new test was added — every conservation and
layout hash bit-for-bit unchanged, as the Q3 safety condition required. Had one moved, the standing order
was to stop and report, not re-record.

**`attention` — BUILT (revises Decisions 32/33/37).** Single-head scaled dot-product self-attention —
`out = softmax_k((Q Kᵀ)/√D) V` — is now expressible with **no new op and no new spelling**: two
`notation::einsum` contractions (`"qdt,kdt->qkt"` then `"qkt,kdt->qdt"`, MB's general contraction) around
the numerically-stable softmax (M5 keepdims max/sum, M4 exp). Built in this tree's **functional
free-function DSL** (the port MA-STEPS specifies) as `tests/unit/attention_test.cpp` at `T == D == 4` —
the aliasing case — run **walked, scheduled (shared and unshared) and generated (places and immediates)**,
all required to agree at compile time, plus an independent double oracle within the old example's
`atol=1e-4`. Registered as the `attention` ctest group; prints `attention ok`.

**Shown rejecting the bug (the audit discipline, not merely the good case).** With the `plan.hpp` fix
reverted, the shared-scheduled and both generated `static_assert`s fail to compile while the unshared one
does NOT — exactly as the root cause predicted (the unshared path never aliased). The test is therefore a
real gate on the fix, not a tautology.

**Blind spots (stated, as the structure gate's discipline requires).** (1) The port is the C++
functional-DSL spelling, not a literal `python/examples/attention.py` on the Python tensor surface — the
correctness content (expressible, run every way, O6 lands on the interpret numbers within tolerance) is
identical and stronger for pinning the fix (a compile-time cross-path assert), but a Python-surface
`attention.py` wrapper is not added here; it is optional packaging on the MT/ME/MR tensor surface. (2) The
double oracle and the walk agree because both are correct; the value type defaults to F32, so the gap is
F32 rounding (measured well under `1e-4`), not a second independent arithmetic. (3) The fix is proven on
same-width structural graphs up to attention's size; the ceilings were not raised.

**Verified:** the full suite **159/159** green from a clean **clang-22.1.8** configure and **159/159** from
a clean **gcc-15.2.0** configure — no divergence at the boundary (amendment 01 / S-5), no gcc-only defect.
The structure gate (`gate_structure_limits` + `_in_tests`) passes over the changed files
(`op_is_in_place_safe` and `hand_back_finished_places` both < 60 lines; `plan.hpp` 432 lines < 600).
ctg3 clean (empty, c4012b8, main) before and after; nothing pushed; A6-1 held (zero delegated agents).


## Decision 42 — MU.1 (reopen item c): the semantics of a lag/window along the streamed/runtime axis (Q1 = A, reserved content DEFINED)

Operator decision `operator-decision-final-three.md` (2026-09-06 08:45): **Q1 = A** lifts the reserved
innermost-axis `_delay` rule **for a lag or sliding window along the streamed (and runtime-resolved) axis,
and for that only**. He asked that the semantics be DEFINED and fresh-audited BEFORE the audio layer is
built — this decision is that definition; MU.2..MU.6 build on it. It is NOT the full axis-semantics
removal (the north star), which stays a separate reserved decision.

> Reversed in part by Decision 52 (`Plan/archive/14-dsl` L3): a workspace reused for the next run continues the stream, so
> a lag reads the run before's last samples; silence is where a fresh or `reset()` workspace starts.

**The model this tree already has, read from the code, not assumed.** The streamed axis is innermost and
time; **one run computes exactly one block of `block` samples** (`runtime/execute.hpp`, the `for (n = 0;
n < block; ++n)` loop). A lag of depth `D` along the streamed axis reads sample `n-D`: `operand_of`
returns the stored sample when `n-D >= 0`, and **silence `0.0` when `n-D < 0`, BEFORE consulting any
storage** (`execute.hpp` — *"before the start of the block there is silence"*). A value a lag reads the
past of is placed in `Storage::History`, which keeps that value's samples for THIS block so a later sample
in the same block can read back; `runtime_workspace_for` zero-fills `now` and `past` at every
`prepare()`. **There is no cross-block state carry in the runtime today** — `begin_sample` fills only the
constants and the input, and nothing seeds one block's `past` from the previous block. `ir/ir.hpp`'s
`AxisDelayAttr{samples, axis=kInnermostAxis}` can already NAME the streamed axis; only the SEMANTICS of a
lag/window there were reserved, not the representation.

### What "block boundary" and "re-prepare()" therefore mean

**Offline processing is ONE block.** The whole signal — length `N`, runtime-resolved (MD) — is a single
`execute` call, so a recurrence (an IIR such as tutorial 10's one-pole) or any fixed lag is **exact
end-to-end**, with silence only before sample 0. This is the faithful, always-available path for every
recurrent graph and needs nothing new. The only "block boundary" a one-block-per-signal run has is its own
start, which is silence.

**`re-prepare()` resets to silence.** `prepare()` sizes and zero-fills a fresh workspace, so a new run — a
new stream, or a new `N` — begins from silence identically to a stream's very start. State does NOT
persist across a `prepare()`. This is the same "silence before the start" rule M1 fixed, now stated to
hold at the prepare boundary.

> Reversed by Decision 48 (P5): `--stream`, `--block` and the overlap below are gone (D-6); a file is
> run whole.

**`--stream` (constant-memory, many blocks) splits into two cases, and the split is the crux of Q1:**

- **FIR / a sliding WINDOW (energy_fraction, tutorial 05 FIR) — additive, no model change.** A window has
  finite context `window - hop`. `run_cli` (MU.5) feeds an **input overlap of `window - hop` samples**
  between consecutive blocks (the old tree's `.overlap = kWin - kHop`), so every frame a block emits is
  complete from that block's own overlapped input. No graph state crosses the boundary; the windowed run
  stays a pure per-block map, and offline == `--stream` is **count-exact** (sums of 0/1 masks are
  integer-exact — the invariant energy_fraction is built on). This needs NO change to `execute`/`operand_of`.

- **IIR / an unbounded RECURRENCE in `--stream` (tutorial 10 one-pole) — the reserved deeper case.** True
  constant-memory streaming of an IIR needs the previous block's tail seeded into this block's `n-D < 0`
  reads, which `operand_of` deliberately does NOT do (it returns silence for `n < back`, never reaching
  across `execute` calls). Providing that carry is a change to the M1 streaming model. **Therefore
  tutorial 10 ports on the OFFLINE (one-block) path, where it is exact; its `--stream` constant-memory
  mode for an IIR is the Q1 tripwire.** If MU.5 can add the carry additively — a `prepare()`-seeded
  history that leaves the one-block/offline numbers bit-identical and every green test green — that stays
  inside Q1's grant. If it cannot be done without reopening the deeper M1 streaming model, **STOP and
  report**; do not silently redo the model. Recorded so the boundary decision is explicit, not discovered.

### Layout rule for windowing the streamed axis (binds MU.3)

`sema/framing.hpp`'s `framed_window_shape` today refuses `ax >= rank-1` (the streamed axis stays whole).
MU.3 lifts that for the streamed axis ONLY, with this layout: unfolding the streamed axis of extent `N`
yields a frame axis of extent `W = (N-window)/hop + 1` and a static window axis of extent `window`; **the
window axis is the inner, static, per-block gather and the frame axis `W` is what streams** at the hop
rate. In every named use the window axis is immediately reduced (`mean`/`sum` — RMS for energy_fraction,
the tap-sum for FIR), leaving `{.., W}` whose innermost axis `W` is the streamed output. The intermediate
`{.., W, window}` is a materialised per-block tensor (a static window axis beside a streamed frame axis —
MB axis-model territory), never itself a delay line. This keeps "the innermost RUN axis is the stream"
true of every value that actually streams, while admitting framing as a within-block operation.

**Tripwire (stop-and-report), the same as the IIR case.** If MU.3 finds this layout cannot be reconciled
with the M1 streaming model without reopening it — e.g. a windowed graph that needs the frame axis and a
residual time axis to stream simultaneously — STOP and report. The named MU deliverables all reduce the
window and stream a single derived frame axis, so on the evidence they stay inside the layout above.

### A lag depth larger than the block — no refusal is owed

A history place keeps the value's WHOLE block, not `D` samples (`runtime/workspace.hpp`'s
`array<array<double, BlockSize>, HistoryPlaces>`; `runtime_workspace.hpp`'s `past` sized
`history_slots * block`). A lag therefore reads silence for `n-D < 0` and the value's own stored samples
for `n-D >= 0`, for ANY depth `D`, with no out-of-bounds: a read only ever touches index `n-D` in
`[0, N)`. A depth `D >= N` simply reads silence at every sample — which is the CORRECT one-block answer
(a delay longer than the whole signal produces silence), not an error. So **no depth-vs-`N` refusal is
owed**, at compile time or at `resolve_dimensions()`. (An earlier draft of this decision claimed MU.2
would add such a refusal; MU.2's test disproves the premise — `runtime_recurrence` runs a depth-1
recurrence exactly at `N = 3`, and the whole-block history is depth-independent.) Single-sample
recurrences (`D = 1`) are valid for every `N >= 1`; this was verified, not assumed
(`tests/unit/runtime_recurrence_test.cpp`, N ∈ {256, 64, 3}, max_err 0 to 1e-9).

### Scope, blind spots
- Lifts `_delay`/framing along the **time/runtime axis only**. Outer-axis framing is unchanged (MB). Full
  axis-semantics removal (north star) is untouched and still reserved.
- Offline (one block) is the exact path and is always available; the only mode with a genuine cross-block
  boundary is `--stream`, and only the IIR sub-case of it touches the reserved model. Recorded above.
- A graph that windows the stream and is run through raw `execute` (not `run_cli`, so no overlap) sees
  edge effects in the first `window-hop` samples of each block — correct for "each block is a fresh
  stream"; offline==stream is a `run_cli` property (overlap supplied) asserted there, not a graph
  invariant. MU.5's parity check pins it.

## Decision 43 — Q2 (reopen item a, = option 1): gradients() is an exported app-layer header and the Python test_autodiff surface is BUILT (revises Decisions 32/33)

Operator decision `operator-decision-final-three.md` (2026-09-06 08:45), **Q2 = option 1**: package the
gradients() capture spelling as a dedicated application-layer export header OUTSIDE `include/`, without
relaxing the O12/A4-2 leaf boundaries, and build the Python `test_autodiff` surface on it. This resolves
the packaging decision MG.2 reserved (Decision 33) and revises the "Python autodiff stays absent" line of
Decisions 32/33 to BUILT on the streaming/element subset.

**Q2.1 — the exported header (`app/heinzel/gradients.hpp`).** The `gradients()` capture spelling composes
the two core leaves (frontend + autodiff) and so cannot be a core header under O12 (`core_no_frontend`)
and A4-2 (`autodiff_is_a_leaf`). It now lives in a NEW top-level `app/` tree — the application layer,
above both boundaries. `tests/support/gradients_capture.hpp` became a nine-line shim including it, so the
already-built consumers (fir_fit, mlp_train, tutorial 07, frontend_gradients) use one definition. A
separate `heinzel_app` INTERFACE library exposes `app/` (linking `heinzel`), so the core never advertises
an app header. `test_app_gradients_export` proves the header is self-contained — it includes ONLY
`heinzel/gradients.hpp` (no test-support path), builds a gradient graph, and checks da/dx against the
closed form of exp(a·x). Amendment 06's structure gate now scans `app/` too
(`gate_structure_limits_in_app`) — a new source root outside the gate would be a blind spot. **The
O12/A4-2 leaves are NOT relaxed**: both gates still scan `include/` and pass.

**Q2.2 — the Python `gradients()` surface (`python/heinzel/`).** `dsl.gradients(loss, wrt_list, names)` /
`grad(loss, wrt)` put the graph in gradient-finish mode; `render()` then emits `finish_gradients(...)`.
The JIT includes the exported header (`_codegen`) and adds `-Iapp` alongside `-Iinclude` (`_compile`,
`resolve_app_dir`). It records the request only — the numbers are the closed M7 transform's, no new op or
meaning. `python/tests/test_autodiff.py`: **FOUR of the six old sub-tests built (Q2's "~4/6") plus two
guards** — render-body emission, wrt-must-be-a-list, finishes-once, an end-to-end linear regression
(da/db vs the hand closed form, rtol 1e-6), a transcendental gradient exp(a·x) (da vs the closed form),
and the recurrence-on-the-diff-path named error (the excluded-adjoint refusal `fail_excluded`, reached
through `gradients`, in the compiler diagnostic).

**RESERVED (recorded absences, unchanged reasons).** `test_grad_native_graph_matmul` needs the general
`_matmul` adjoint — an unwritten future BACKEND item, NOT authorized by Q2 (the same disposition MG made,
Decision 33: the restricted `_contract` differentiates, the general `_matmul` does not). `test_grad_
spectral_loss` needs the FFT adjoint → M13 (a backend, not a frontend spelling). Both are `pytest.skip`
with their reasons.

**Blind spots (the structure gate's own discipline).** (1) The Python surface builds F32 graphs (the DSL
`input` sets no element type), so a finite-difference gradient oracle is NOT usable on it (an eps
difference of two F32 losses is rounding noise); the numeric proofs are against HAND closed forms, which
are independent of the adjoint and exact at F32 tolerance. (2) The gradient tests use whole-tensor losses
at `block=1` (reversed by Decision 48: fed one sample since P5) (element axes carry the data, the streamed axis is length 1) — the same convention
`test_order.py` uses; a streamed (per-sample) gradient is the C++ frontend_gradients test's domain. (3)
The Python autodiff arm runs under the clang configure only (the bindings require clang ≥ 22; the gcc
boundary build's Python arm raises its own clear error by design), so the gcc boundary covers the C++
side (the exported header, its export test, the structure gate), not the pytest arm.

**Verified at the Q2 boundary:** the C++ suite **162/162** green from a clean **clang-22.1.8** configure
AND from a clean **gcc-15.2.0** configure (S-5 / amendment 01 — no divergence on the app header or its
export test); the Python bindings suite **130 passed / 2 skipped** via `uv` (clang++), up from 124 (+6
autodiff), no regression; O12 (`core_no_frontend`, `autodiff_is_a_leaf`) and every structure gate
(including the new `app/` root) pass; ctg3 clean (empty, c4012b8, main) throughout; nothing pushed; A6-1
held (zero delegated agents).

## Decision 44 — MF.1 (multi-rate / frame-stream): the runtime representation of framing the streamed axis is **A** — the frame count becomes the block length (revises/extends Decision 42)

> Reversed by Decision 48 (P5): framing the streamed axis is refused on every surface until P7 (D-1).

Operator decisions `operator-decision-multirate.md` (2026-09-06 21:18: mint the multi-rate/frame-stream
model as its own milestone, option 1 — an `unfold` over the streamed axis is now allowed) and
`operator-decision-frame-model-A.md` (2026-09-06 21:33: **representation A**). Decision 42 (MU.1) defined
the *semantics* of a lag/window along the streamed axis and stated the layout rule ("the frame axis `W`
is what streams at the hop rate"); it left the *runtime representation* of that framing as a genuine fork
— surfaced to Gerald as A vs B — because Decision 42's own words admit two readings. **This decision
records the representation Gerald chose (A) so the fork is closed before any code lands.**

**The fork, and why it is Gerald's.** `unfold(x{N}, axis 0, win, hop)` frames the streamed axis of a
stream of length `N` into `W = (N-win)/hop + 1` frames and a window of `win` taps. Read from the trees:

- **A (chosen).** The frame count `W` becomes the run's **block length**. The graph runs `W` times; frame
  `f`'s output reads `input[f·hop + k]` for window tap `k` — the exact source index
  `runtime/elements.hpp:350` already computes for an outer-axis unfold (`f*hop + k`), now with `f` the
  streamed frame and `k` a static element. The window tap `k` is a genuine **static element axis** of
  extent `win` that enters the element `Shape` (rank+1); the frame count `W` stays **implicit** — the
  streamed length is never an element axis (`run_inputs.hpp`, `runtime_workspace.hpp`: a value's width is
  the product of its OUTER axes, the innermost/streamed extent is the block) — replacing `N` as the loop
  bound, with the runtime relation `W = (N-win)/hop + 1` recorded on the streamed length the way
  Decision 39 (MD) records a runtime-resolved streamed `Dim`. **Accepted cost:** the input is fed *wider*
  than the block — `N = (W-1)·hop + win` input samples are read to produce `W` output frames — a genuine
  but bounded rate change on the *input* side, not new element logic. This is the literal reading of
  Decision 42 ("`W` is what streams at the hop rate") and the more additive-to-the-executor reading (the
  index math is verbatim; only the block bound and the input width change).
- **B (rejected).** Keep the block at `N`, materialise all `W` frames as one wide `{W}` value in a single
  pass, and let the CLI wrapper sum across host blocks — literally how the old tree at `c4012b8` is built
  (a *derived* `W` dim, `audio/cli.hpp`'s `resolve_axis` marks the frame axis `derived`, not
  user-settable; the framer overlaps host blocks). This materialises a non-per-sample value across the
  whole block — a larger departure from "every value streams over the block".

A and B give **identical numbers** for both named deliverables (energy_fraction windowed-RMS, tutorial-05
FIR-via-unfold); neither recurs *along* the frame axis. They diverge only for a future graph that smooths
or recurs along the frame axis — north-star territory Gerald keeps reserved. That is why it was his call
(a genuine architecture fork), not an exit-condition ambiguity where one reading contains the other.

**The tied consequence Gerald fixed — the conservation oracle.** Under A the frames ARE the stream, at
the hop rate. So the conservation oracle's per-sample law **EXTENDS across the rate change**: a frame
value is a first-class streamed value, conserved per-frame like any per-sample value — it is **NOT a
recorded blind spot.** The oracle is applied at the frame rate for a framed segment.

**Guardrails (binding, from both operator decisions).**
- **Static / single-rate graphs stay byte-identical.** No existing recorded conservation / layout /
  schedule hash may move for a graph that does not use the frame-stream. The rate change is a NEW
  capability beside the M1 streaming model, not a rewrite of it. If extending the model (or the oracle
  across the rate change) genuinely MOVES a protected anchor for an existing graph, **STOP and report
  before any re-record** — the same rule as the Q-2 / Q-3 fixes.
- **The other stream guards stay.** `sema/framing.hpp:64-68` is lifted for the streamed axis ONLY;
  `sema/attribute_checks.hpp` (transpose that moves the streamed axis) and `sema/order_checks.hpp`
  (ordering the streamed axis) still refuse **by name**. Note `runtime/elements.hpp`'s `unfolded_element`
  needs no change under A: the window tap is a static ELEMENT axis, exactly what it already gathers with
  `f*hop+k` — the new behaviour lives at the block-loop / input-read level (the block bound becomes `W`,
  the input is read across `[0,N)`), which is MF.3's concern. Do not just delete the streamed-axis
  guards; extend them faithfully to admit the frame-stream and keep refusing what is still illegal.
- **North-star territory stays reserved.** If MF's build path reaches a graph that needs the frame axis
  AND a residual time axis to stream simultaneously — i.e. a *second, independent* runtime block length
  coexisting with `N` — that is the north-star tripwire: **STOP and ask.** The named deliverables reduce
  the window in-graph and stream a single derived frame axis, so on the evidence they stay inside A.

**Scope / blind spots.** A lifts framing of the **time/runtime axis only** (outer-axis framing is
unchanged, MB). Full axis-semantics removal (the north star) is untouched and still reserved. b1's true
`W`-streaming as a *second* block length beside `N` (the reserved divergence above) is deferred with the
b2 divergence recorded. flac/mp3/opus decode and a Python `audio` module are recorded reserved absences
(host has no sudo; WAV is the buildable slice) — see MF-STEPS. This decision is docs-only (MF.1); MF.2
lifts the analysis refusal, MF.3 makes the framed segment run == interpret, MF.4/MF.5 port the two
examples, MF.6 adds the WAV/CLI layer and the run-and-compare to `c4012b8`. Recorded and fresh-audited
before any MF code, per the operator decision.

## Decision 45 — MF boundary (multi-rate / frame-stream): the WAV/CLI audio layer is BUILT and energy_fraction is run-and-compared BIT-EXACT to c4012b8; tutorial-05's FIR-via-unfold is BUILT (revises Decision 40)

Milestone **MF** (Decision 44, representation A) framed the streamed axis, ran the framed stream ==
interpret at two N (MF.2/MF.3), built energy_fraction's windowed-RMS-mask core and tutorial-05's
FIR-via-unfold as graphs matched to from-definition references (MF.4/MF.5), and added the WAV/CLI audio
layer (MF.6): `wav.hpp` (s1), the frontend streamed-axis unfold spelling (s2), `cli.hpp` flag
generation + `--help` (s3b), the runtime-sized framed executor for literal and symbolic W (s3c-pre/-pre-2),
and the `run_cli` pump — offline (s3c) and block-wise `--stream` (s3d). This records the boundary.

**tutorial 05's FIR-via-unfold — BUILT (revises Decision 40).** Decision 40 recorded tutorial-05's two
shape-generic FIR sections as **reserved-`_delay`, NOT built**, because a streaming FIR
`(x.unfold(4,1)*h).sum(1)` is a moving window ALONG the streamed axis — the reserved innermost-axis
`_delay` question. Q1=A (Decision 42) and the multi-rate model (Decision 44) lifted exactly that: framing
the streamed axis is now representation A. MF.5's `fir_unfold` builds the FIR-via-unfold as a graph and
matches it element-by-element to a summed-tap convolution reference at two N with distinct asymmetric taps
(0.5,-1,0.25), walk/scheduled/generated held to each other. So Decision 40's FIR reservation is **revised
to BUILT**; the reserved-`_delay` carve-out it named is closed by the multi-rate model.

**energy_fraction — run-and-compared BIT-EXACT to c4012b8 (exit condition 6).** The old tree's
`examples/energy_fraction` (the ONLY `run_cli`-based CLI in either tree — see the finding below) was
exported from `main` (== ctg3 `c4012b8`) into a THROWAWAY dir (ctg3 untouched) and built WAV-only. Over a
48128-frame (94·hop, no trailing-block drop) 300 Hz amplitude-ramp signal it printed
0.924731/0.849462/0.688172/0.526882 at thresholds 0.05/0.1/0.2/0.3 (above counts 86/79/64/49 of W=93). The
rewrite pump running the same energy_fraction graph (symbolic N, win=1024/hop=512), **both offline and
`--stream`, reproduces those fractions bit-for-bit.** Pinned in `cli_run_test.cpp`
`matches_old_tree_energy_fraction` (offline==`--stream` exact within a build; within one window of the
recorded old-tree count across the old-float/new-double + clang/gcc gap); live comparison recorded in
`verification/mf-s3d-parity/README.md`.

> Reversed by Decision 48 (P5): the CLI runs the nest path, which refuses the framed energy_fraction by
> name; `matches_old_tree_energy_fraction` is gone (`energy_fraction_test` still tests the walk).

**offline == `--stream` count-exact.** The `--stream` pump reads a framed graph block by block, blocks
overlapping by `win-hop`; each block consumes `Wb·hop` input and keeps the rest (`< win`) as the next
block's carry, so the global frame partition is identical to the offline single run — hence for a
count-style output (0/1 masks summed) `--stream == offline`, shown at block sizes forcing overlapping
blocks (`cli_run_test.cpp` `streaming_is_count_exact_with_offline`).

> Reversed by Decision 48 (P5): there is no `--stream`, and `streaming_is_count_exact_with_offline` is gone.

**FINDING — exit-6's "three examples run-and-compared" maps to ONE `run_cli` CLI.** `energy_fraction` is
the only example in the old tree that uses `run_cli` (it links `heinzel_audio`; every other example is a
standalone program on the executor/frontend). tutorial-05 FIR-via-unfold and tutorial-10's one-pole are
NOT `run_cli` CLIs in either tree, so there is no old-tree CLI binary to diff their stdout against; their
"run-and-compare to c4012b8" is satisfied by their in-tree comparison to the old-tree computation — the
summed-tap convolution reference (MF.5 `fir_unfold`, exit condition 5) and the recurrence oracle (MU.2
`runtime_recurrence`, tutorial-10 one-pole offline). The single `run_cli` CLI (energy_fraction) is
compared as an actual binary, which is the strongest available form.

**Reserved absences recorded at this boundary (with reasons).**
- **IIR frame-stream (`--stream` of a framed graph carrying a recurrence/delay across frame blocks)** —
  NOT built. The block-wise pump gives each block a fresh workspace (`RuntimeWorkspace` history is indexed
  within a block, zero-init, with no cross-`execute()` continuation API), correct only when the frames
  carry no cross-sample state. Such a graph is **refused by name** in `--stream` (detected on the
  lag/recurrence ATTRIBUTE, not `plan.history_slots` — a framed value uses history STORAGE for its
  within-block frames, so history_slots > 0 for a windowed graph that streams perfectly). No named MF
  deliverable needs it; a state-carrying stream needs a runtime carry API (a separate, larger item).
- **`-o` audio-output writing** — the offline/`--stream` pump is analysis-only; a graph with an
  `audio_out` is refused by name. Writing a processed-audio WAV (effects: overlap-0 + executor state
  carry) is deferred; no named MF deliverable produces audio out.

> Reversed by Decision 48 (P5): no `--stream` means no IIR frame-stream to refuse, and `-o` is built (D-4):
> a whole file's audio output written as a WAV.
- **flac/mp3/opus decode, and a Python `audio` module** — recorded absences (host has no sudo for the
  decode libs; the Python surface is optional packaging). WAV is the buildable slice.

**Structure.** The pump is `include/heinzel/audio/cli_run.hpp` — its own file, distinct concern from
`cli.hpp`'s flag-generation/help (amendment 06). Structure gate green over all new files; full suite
175/175 from a clean configure under clang 22.1.8 AND g++ 15.2.0 (the S-5 gcc boundary run — no gcc-only
divergence; the parity test's one-window tolerance held under gcc). ctg3 read-only `c4012b8`/main; the old
tree built OUTSIDE it; nothing pushed; A6-1 held (all audits in-context).

## Decision 46 — the hardware twins: one file may branch on constant evaluation, and `-fno-math-errno` joins the interface flags (revises the M2.2 gate's "nowhere")

**Decided** (Gerald, 2026-09-18, plan decision D-3): an operation IEEE 754 requires to be *correctly
rounded* may use the processor's instruction at run time while constant evaluation runs the software
core, in exactly one file — `runtime/hardware_twins.hpp` — which `gate_no_constant_evaluation_branch`
exempts by path. The gate's reason stands everywhere else: two bodies selected by which world a call is
in cannot be compared. These can, and `unit/hardware_twins_test.cpp` does — core against instruction,
bit for bit, over every binade boundary, every half-integer tie near zero, and four million random bit
patterns.

**What has a twin**: `square_root`, `truncate_toward_zero`, `round_toward_negative`,
`round_toward_positive`, `round_to_nearest_even`, `multiply_add_rounding_once`.

**Same bits, except which NaN.** Where the answer is a NaN, core and instruction both answer one but not
the same one (x86 gives the root of a negative the negative quiet NaN, quiets a signalling NaN, and picks
among two NaN operands by position), and the lane sweep showed the same between a scalar expression and
its vector form, because the compiler commutes operands freely. IEEE 754 leaves a NaN's sign and payload
open and no two builds of this tree ever agreed on them. So every comparison of results reads "the same
bits, or both a NaN" — in the twin and lane tests now, and in the nest path's `memcmp` oracle when it
arrives — and the twins carry no guard, which keeps each a single instruction and lets a loop of them
vectorize.

**The fused multiply-add, and the finding on the way to its twin.** The same sweep, first run against
the emulation this tree had (`multiply_add_rounding_once` built on `exact_product`/`exact_sum`), showed
that core was NOT correctly rounded: it rounded twice on some ordinary inputs
(`fma(1+2^-52, 1+2^-52, 3)` answered 4; correct is 4+2^-50), differed among the subnormals (a partial
product's low half stops being exact there), and answered NaN for an infinite `c` and for a sum that
overflows. Its own comment had said "evidence and not a proof … no input exhibiting it was found"; ten
million random inputs had not found what a sixteen-value table of edge cases found at once. It was
replaced (`runtime/fused_multiply_add.hpp`) by an integer algorithm — the exact 106-bit product, `c`
aligned under it in 128 bits with a sticky bit, one rounding at the position the result's exponent
dictates — which agrees with the instruction, and with glibc's `fma`, on all sixteen million inputs of
the sweep, including the class the old core documented as its one divergence (`max*2 - max`, which is
now `max`). Only then did the instruction become its twin, and only where the target has one
(`__FP_FAST_FMA`, or `__FMA__` / `__ARM_FEATURE_FMA` since clang does not define the portable macro) —
the one instruction-set question under `include/`, exempted by path in `gate_no_instruction_set_macros`.

**Which twins are on.** The root wherever `HEINZEL_HARDWARE_TWINS` is. The four roundings and the fused
multiply-add only where the target has the instruction (`__SSE4_1__` / `__ARM_FEATURE_DIRECTED_ROUNDING`;
`__FMA__` / `__ARM_FEATURE_FMA` / `__FP_FAST_FMA`): without it the builtin is a libm call, which is
correct but measured SLOWER than the core on baseline x86-64 (trunc 2.65 ns against 1.21, floor 2.65
against 1.68), and would make the default build depend on `roundeven` (glibc 2.25). So the default
`-march`-less build has the root's twin only; `hardware_twins_test` is also built with `-march=native`,
where all six are on.

**`-fno-math-errno`** is now an INTERFACE flag beside `-ffp-contract=off`. It changes no value the
library computes: nothing under `include/` or `app/` reads `errno` or `<cfenv>`. What it buys: without
it the compiler keeps a never-taken `call sqrt` on the error path of every `__builtin_sqrt`, and cannot
vectorize a loop of bare builtins at all (measured, clang 22: 8 × `vsqrtss` without, one `vsqrtps`
with). Like the contraction flag it reaches a consumer's own
sources through the interface: code there that checks `errno` after a libm call stops seeing it set.
`HEINZEL_HARDWARE_TWINS=OFF` (CMake) / `=0` (macro) turns every twin off for a freestanding build.

## Decision 47 — bands: groups sharing an outer loop, the carry instead of a ring (`Plan/archive/shared-outer-loop/`)

**What.** A nest schedule may say that consecutive groups run under ONE loop over an axis of the last one,
in chunks of `step` rows (`NestSchedule::bands`). Per chunk every group runs over the rows it has to have
made by then — a producer runs `lead` rows ahead of the band's last group, the reach of the windows between
them — and a value made and read only inside the band is kept per chunk (a band buffer of `step + lead`
rows), not whole. Between chunks the rows the next chunk reads again are COPIED to the front of the buffer:
the carry.

**Why a carry and not a ring.** A ring (row `r` at `r mod rows`, as the hand-fused loops do) makes an
address non-affine, and every part of the nest path — access maps, strides worked out once per run,
`moving_slots`, lanes — rests on affine addresses. The carry keeps them affine: per chunk the runner only
moves each slot's starting pointer. It costs a copy of a few rows per chunk, measured within 2 % of a ring
(`Plan/probes/band_rows.cpp`), and faster than recomputing the reach per chunk at every point measured. It
is also the only form a band around FEEDBACK can take: a recurrence cannot recompute its past, only carry
it — here the prefix of silence a lag reads is what is carried, along time.

**One derivation.** `band_layout` (`schedule/band_reads.hpp`) derives the axes, leads and kept values, or
refuses by name: a band holds no contraction, no value made in parts, no relaid copy, reads no feed's past;
it walks an axis no fold of it collapses; a later group reads a band value at step 1, from no offset, along
the band, never through a lag or a gather; extents agree (`E_m = E_last + lead`); a band buffer keeps a
prefix only along time, and its rows are contiguous; at most `kMaxBandBuffers`. The planner proposes a band
only where `band_layout` accepts it, lowering copies it and checks every access it wrote against the same
derivation — so `nest_lowers` cannot say yes to a band lowering refuses.

**The rule** (`schedule/plan_bands.hpp`). A run of consecutive groups is banded where each stores only what
the next reads, along one unfolded axis, and the run's WORKING SET stored whole (everything it reads and
writes) exceeds `Machine::band_when_over = l3_bytes`; the height keeps `band_bytes = l2_bytes / 2` per
chunk. A run whose reads are all at the point (a stream) is banded in chunks of `band_stream_rows = 16`
samples, so the next group runs while the first one's latency chain still does. Measured on zen2 (`Plan/archive/
shared-outer-loop/00-Overview.md` §8): blurxy and harris from 8 MB up level with or ahead of the hand-fused
loops and `main`, the oscillator 0.90× of `main`, the ladder bank 11 % faster; a working set that fits the
last-level cache is slower banded, and is not.

**The runner** places a band group per CHUNK, in locals, as it places any group: a placement kept across
chunks in memory lost the constant strides of literal extents and ran banded harris at 2.3× the
instructions.

**The prefix is zeroed before chunk 0 of every run** — today's meaning of a prefix, "silence before the
block". O-3's cross-block state (P7.3) will want the prefix to carry the previous block's tail instead; at
that point this zeroing becomes "zero on `reset()`". Recorded, not built.

**Amended (2026-10-10, Plan/23, Gerald's yes): the working set is weighed against 15/16 of the last-level cache.** A
program never has the whole of it: `Machine::band_when_over` is `l3_bytes / 16 * 15` (zen2 15 MiB, `m1_pro` 22.5 MiB by
the same rule, the default machine 7.5 MiB). Measured where the working set alone decides
(`Plan/probes/thresholds/band_when_over.cpp`: five streams the L2 rule bands none of, 1/4 to 3/2 of the L3, banded
and whole, two runs alike): KFR's EQ as a bank in double runs banded 1.05-1.08x at 7/8 of the L3, 0.98-1.00x at 15/16,
0.91-0.94x at the L3; eq8fb 1.01x, 1.00x, 0.99-1.00x. JUCE's ladder, whose working set is exactly zen2's L3, is
banded again: `ladder/heinzel` 0.96-0.98x (alternated, controls 0.99-1.01x); no other bench program, none of the
differential's 15,730 and nothing of `measure.sh` moved. The probe also shows the ladder, the voice and the ladder over
16 voices 3-9 % faster banded at every size, a quarter of the L3 too: a stream band's overlap, which no working-set
threshold decides (open).

## Decision 48 — P5: the nest path is the default on every surface; the walk is an oracle only, until P9 (`Plan/archive/make-default/`)

**Decided** (Gerald, 2026-09-25, O-13: "Change everything. There are no users yet. Fail loudly."). Python,
the audio CLI and the Vamp adapter compile a graph with `compile()` (`heinzel/compile.hpp`: infer →
rewrite → `plan_nest` → `lower`) and run it with `run_nest`. There is no fallback. A graph the nest path
cannot run fails when it is compiled, with the refusal by name: Python reads it from a `static_assert`
over `try_compile`'s refusal (the analysis's or the planner's); a CLI tool or a plugin fails its own
build, where clang's note quotes a planner's refusal (a literal; the analysis's message is assembled, and
is read from a run-time catch, `compile.hpp`). The reference walk (`run(g, feeds, out)`) and the old `Program` / `run_generated` path stay
as the differential oracle in tests until P9 retires them; no surface calls them.

**What P5 reverses** (each marked where it was decided):
- Decision 45's energy_fraction through the CLI, and the `cli_run_test` case that pinned it
  (`matches_old_tree_energy_fraction`). A window over the streamed axis is refused on the nest path
  (D-1), so the CLI refuses the framed graph by name. `energy_fraction_test` and
  `frontend_framed_unfold_test` stay: they test the walk.
- Decision 42's `--stream` and its overlap: `--stream`, `--block`, `spec.overlap` and `spec.block_frames`
  are gone (D-6). A file, or stdin, is decoded whole and run as one run.
- Decision 44's framing of the streamed axis (the frame count as the block length): refused on every
  surface until P7 (D-1).
- Python's `block=` / `block=1`: one build serves every length, the stream's length bound per call
  (`hzpy_block` is a symbol). A timeless tensor is fed one sample; a generator is called `g(n)`.
- ABI 2's `heinzel_run_generated`: shim ABI 3 runs `run_nest`, every port in its value's own type.
- The Vamp adapter's `run` + `sample_at`: it runs `run_nest` with a workspace made in `initialise()`,
  and feeds the host's F32 block without a copy.

**The refusals.** New in P5, each by name in the planner and in `nest_agreement_test`'s corpus:
- D-1: a sort, the k largest, a quantile between two ranks, an even median ("an order statistic that is
  not one exact rank");
- an output that no node makes: a gradient its loss does not reach, or one that IS the loss's seed (an
  un-reduced `x + p`, a bias);
- a window over a constant; a contraction of a constant or a window; a table read over a constant or a
  window, or of more than two axes.

A framed stream (D-1) is refused before the planner: over a symbolic stream (every Python graph) by the
analysis, which asks for the frame count (`_unfold unfolds axis 0, whose size is not known yet`), over a
literal one by the view rules ("a window over an axis that is no element axis"). Older than P5 but reached
on every surface now, with no fallback: a delay line over an input or a constant, or in feedback; a window
that is itself a result; an operation with no core in the value's type (`cos`, `tan`, `pow`, `atan`: "the
graph holds an operation the nest path does not lower yet"); the past of a window or of a constant; a
value of no axes; a pad or join along a symbolic axis; a fold over the stream; the planner's shape and
size limits for feedback and groups.

**What P5 built so that no graph a test or example ran before is refused** outside D-1: a view or a part
of a shaped constant (autodiff's seeds) is stored as a fill; `_order` runs over a symbolic stream
(`ElementExtentsKnownNow`); `rearrange` passes a symbolic axis through; `sin` and `log` have float cores
(Cephes polynomials; `sin` at most 2 ulp for |x| ≤ 8192 with a five-part reduction, `log` 1 ulp over every
positive float — each measured over every float in its range by the M2 review and its re-review;
`float_cores_test` samples 4096 points a binade).

**The known difference N-5.** Under High, a view or a part (a pad, a join) of an F32 constant holds the
constant rounded to float, where the walk keeps it in double: a High graph multiplying by a view of 0.1
differs from the walk in the last place (`nest_constant_views_test` pins it). It goes when the analysis
rounds every F32 constant to its type.

**Every number crosses in its own type.** Python feeds float32 to an F32 port and float64 to an F64 one.
The CLI parses a param as a double and feeds it in its input's type, reads each output in its own type
(summing in double), and narrows an F64 audio output only for the WAV. A WAV is read as float, a 64-bit
file too.

**What the reviews found**, and the tree now guards (six milestone reviews, six rounds of re-review):
- a delay line over a ramp read outside its buffer: a ramp keeps silence now, and lowering checks a delay
  line's silence;
- the planner accepted contractions and table reads of windows that lowering refused;
- Python's cache served binaries built from old headers, and a quote in a name wrote C++;
- the WAV reader overflowed its stack on a frame over 64 KiB;
- the Vamp adapter read a rank-0 output past its buffer.
The sanitizer builds (`rw-asan`, `rw-asan-O2`, green since P5 M8) found two test bugs.

## Decision 50 — the loop tree: the nest program a tree, the schedule a primitive list, the runner one template per node kind (`Plan/archive/13-loop-tree/`)

**Decided** (Gerald, 2026-09-26/27, `Plan/archive/13-loop-tree/00` §3 D-1–D-10; switched at M4, 2026-10-01). The nest path —
schedule and executor — is restructured the way TVM and Halide are built. What it replaces had one fixed loop skeleton
per group whose meaning depended on position (`folds_from`, `through`, `relaid`, a flag per transform), planned by
`schedule/plan_nest.hpp`, lowered by `lower/` and run by `codegen/run_nest.hpp`; all of it, and the two bridges below,
is deleted. After M4 the pipeline is DSL → graph → `sema` → `loops/` (plan, lower, run), plus the shared meaning of an
operation (`runtime/compute.hpp`, the cores, `Lane<T,W>`).

**The IR** (`loops/program.hpp`, `Plan/archive/13-loop-tree/02`). Buffers say what storage exists, statements what is computed
(affine accesses), stages the loop nest over each domain, bands the stages run chunk by chunk — and a TREE of nodes says
in which loops, in which order, at which width and with which registers each statement runs: walking nodes (`Loop`,
`Unroll`, `Vector`), scopes (`Registers`, `WhenLast`), leaves (`Compute`, `Init`, `Accumulate`, `Finish`, …). Plain
`constexpr` data with `operator==`, fixed capacities (`kMaxStmts` 512, `kMaxTreeNodes` 3584, `kMaxSteps` 256: O-4;
*cut, and raisable by name, since Decision 55; `kMaxSteps` went with the step layer*), held to `well_formed` before any run, printed by `loops/print.hpp`.
What the old skeleton implied by position the tree states: the probe found the order of jammed unrolled bodies worth
2.2× (rows outside columns), which the old `Block` hard-coded.

**The schedule** (`loops/schedule.hpp`) is a list of primitives — `Reorder`, `Split`, `Vectorize`, `Unroll`, `Jam`,
`Contract`, `Prefetch`, `Parallel`, `CacheRead`, `Band`, `BandParallel` — each naming a stage and its loops by axis and
level, applied by the lowering (`loops/lower.hpp`) to each stage's default nest in step groups, then normalized and
validated: the lowering is the final word on legality, every refusal a value (`std::expected`). Some structure is
DERIVED, never scheduled: a fold's inner loops follow from its loops (no step expresses a fold's loop outside them).

**The planner** (`loops/planner/`) is a PORT of the old one, pass by pass and in its order (D-10): per stage an ordered
list of loops — the draft (`planner/draft.hpp`) — spelled as primitives by one emitter (`planner/emit.hpp`). The graph
facts it asks (views, regions, feedback, places, band layout) were adopted by copy into `loops/graph/` and
`loops/band_layout.hpp`. Replacing its legality predicates by trying steps is left for later: it changes decisions
that could not be measured while D-8 held.

**The runner** (`loops/run*.hpp`, `walk.hpp`, `registers.hpp`, `values.hpp`) has one template per node kind and decides
nothing: every loop, width, register and address arrives in the program. Split stages and split bands run on what the
caller brings (`size()`, `run(count, task)`): `loops/` names no thread.

**Why it could be switched** — the oracles, each deleted with what it compared against: bridge 1 (`bridge/from_nest`,
the old program as a tree) held the new runner equal, bit for bit and buffer by buffer, at every run of the nest tests
(M1, `both_runners`); bridge 2 (`bridge/from_schedule`, the old schedule as steps) held the new lowering equal to bridge
1's tree at every lowering (M2, `checked_lower`); the new planner's schedule equalled bridge 2's spelling of the old one
at all 199 planning sites, and its refusals the old texts (M3, `checked_plan`, `loops_agreement`). At M4 the tests moved
onto the `loops` API with every assertion's meaning kept (or listed, `Plan/archive/13-loop-tree/07` §2), placement pinned by
`loops_workspace`'s goldens recorded while it equalled the old workspace to the byte.

**Measured.** Runtime: the probe (nine programs) at parity in time and instructions; at M3's close the tree against the
old path over the 42 gated rows a geometric mean of 1.002 (0.958–1.042, the outliers placement: equal instruction
counts), against `main` 0.73× its time. Compile time of the new path's own costs (D-6, judged against the old path on
the gated programs): the runner 1.28× (M1), the lowering 1.17× (M2, after O-4 shrank the capacities; 1.91× before),
planning and lowering 1.24× (M3; about 1.28× with `kMaxSteps` 256); at M4's close 1.35× and 1.30× against M3.5's
records. The whole suite against B0 (`b916a1bd`, the tree before this plan), from clean: 1.22× for the link targets
both have, the slowest test TU 1.07× — after two decisions of Gerald's: the tests hold the programs they run as
subobjects (`planned::Held`), since clang's code generation rebuilds the initializer of a whole `constexpr` program
named as the runner's template argument over and over (one TU 69 s against 28 s), and `nest_band` was split in two.

**One refusal prefix** (O-1, Gerald, 2026-10-01): the planner's, the lowering's and their graph facts' refusals say
`heinzel::compile:`; `try_compile` hands back the lowering's too. The public surface is `Executor`; `compile()` returns
`Compiled{Graph, loops::Program}`; the `loops::` names are used as they are, no re-export.

## Decision 51 — the DSL surface restored: index notation, declarations, `Executor` (`Plan/archive/14-dsl/`; reverses `Plan/archive/00` O-6)

**Decided** (Gerald, 2026-09-29 and 2026-09-30, DS-1–DS-22). The user writes `main`'s DSL without its
`GraphBuilder`: `Input x{"x", Signal{64}}; Output y{"y"}; y[n] = x[n] + 0.5 * y[n - 3.0 * ms]; return y;`.
Index notation is the one recurrence and delay spelling, which reverses O-6 (`.define` / `.previous` as the
one spelling). `BuildContext`, `.previous()`, `.is()`, `delayed` and `finish(where, …)` leave the public
surface; they stay underneath until every graph has moved (L9). Decisions 49 and 50 are reserved by P6 and
the loop tree's M4. This record grows with the plan: L1 below, then `Executor` (L2), subgraphs and settings
(L5), members (L6), the old spellings retired (L9).

**Vamp, the audio CLI and Python left the tree** (DS-20, L0), each to come back on the settled `Executor` by
a plan of its own (`Plan/archive/14-dsl/06` §2 says what each needs); `audio/wav.hpp` stays, and every check of it
the CLI's tests held moved into `wav_roundtrip`. Decision 48's surfaces are, until then, the examples.

**L1: index notation and the declarations, beside the old spellings.**
- `Index n{rate}` and `Past` (`frontend/arena.hpp`): `x[n]` is x and creates nothing; `x[n - k]` is
  `delayed(x, k)`; `x[n - 3.0 * ms]` folds a duration at `n`'s rate while compiling (`samples(n, d)` for a
  coefficient). `n - 2.5` does not compile, and there is no `n + k`.
- `Variable v; v[n] = …` defines v by its own past (`v[n - k]`), once: a second definition is refused, and a
  `Variable` cannot be copied, since two copies would share one node. `Expr`'s copy assignment is
  ref-qualified, so `x[n] = e` and `y[n - 1] = e` no longer compile to an assignment of a temporary.
- `Input` is a tensor by default: `{64, 64}` is recorded as `Shape{64, 64, 1}` and marked `Value::timeless`
  (new, last in `Value`, hashed only when true, so no recorded hash moved; `sizeof(Value)` stays 64 bytes).
  `Signal{…}` is a signal, its last extent time; a `Length` in it is the stream's symbol (`kStreamLength`,
  0) and records its constraints as `declare_dim(0, …)` did. `Param` is a timeless input of no axes.
- `Output`: `y = e`, or `y[n] = …` for a recurrence that is itself the output; `y[n - k]` of an assigned
  output is its value's past. Each misuse is refused where it happens with a fixed text (a read before `=`,
  both spellings, either twice) or at `finish` (never assigned). `finish(a, b, …)` is `capture.hpp`'s
  multi-output body — `capture.hpp` is deleted, and its 40 calls were respelled graph-equal, checked site by
  site against the old spelling's artifact hash — and adds: an input or a variable read by name as an output
  is its identity (one `_realize`; Plan/archive/10 §2.2 item 6), a literal is refused, and two ports of one name are
  refused.
- `input(where, …)` and `finish(where, …)` are now `detail::input` / `detail::finish` under a context:
  the declarations build on the context-free bodies, which become the only ones at L9.
- The gates: `frontend_decides_nothing_semantic` scans all of `frontend/` (its "pending" no longer trips on
  "appending") and has a `_would_object` twin; the spine lets `frontend` read `units`.
- Tests: `index_notation_test` and `declarations_test` build each spelling and the older one to the same
  graph, at compile time and at run time; eight build-failure fixtures show what does not compile.

**L2: `Executor`, the call site** (`include/heinzel/executor.hpp`, `Plan/archive/14-dsl/04`).
- `Executor<build, target>` takes the graph as its function (or a lambda that captures nothing) and compiles it
  with `try_compile` while the program compiles; it departs from `main`'s `executor<build_comb(), kTarget,
  scalar_backend>` because a grown class-type template argument is never passed by value, there is one spelling,
  and there is one runner. A graph the analysis or the planner refuses is one build error whose message is the
  reason under C++26 (a fixed text on the C++23 floor): every later check is written `refused || …`, the port
  lookup succeeds without looking, and the workspace and the runner call are under `if constexpr (!refused)`, so
  bindings and runs add no error of their own. The graph-level checks sit in the constructor, so a moved one or
  one in an `std::optional` shows the same single error.
- Ports are looked up while compiling (`PortName`, consteval): a misspelt name, `input()` on a `Param` and
  `param()` on anything else stop the build. A buffer's element type is checked when it is bound. A run needs a
  buffer for every input the program reads — an input in the graph that no node reads needs none; one read by a
  node no output needs (the planner computes it) does — and a value for every `Param`, whose feed is pointed at
  the object's own slot on every run, so a moved `Executor` reads its own. *(Since Decision 55 a run's checks are
  debug-only, the Params are pointed at once and again after a move, and `Executor::graph` is gone.)*
- Each bound output is copied out of the workspace after the run: its buffer is asserted whole and plain. The copy
  is what L12 removes (zero-copy, DS-21); the API is already that one.
- A program has at most one symbol, and only on time: the last axis of a buffer, or a loop along which a store
  walks its buffer's last axis and no access another axis (a contraction's summed axis comes after the result's;
  a symbol only on a fold's axis is refused). `has_length` is read off the program: `prepare(max_frames)`, validated
  against the symbol's declaration (or at least one sample), then `run(frames)`; `run()` otherwise. Until L3
  carries state, `run(frames)` makes a fresh workspace whenever the length changes.
- `examples/synth_voice.cpp` is spelled in index notation and runs through `Executor`; `executor_test` holds
  `Executor` to `run_nest` bit for bit, and fourteen build-failure fixtures show each refusal as one error.

**L5: subgraphs and gradients with no context** (`frontend/subgraph.hpp`, `app/heinzel/gradients.hpp`,
`Plan/archive/14-dsl/02` §6–§8).
- `instantiate(name, body, args...)` runs an ordinary function as a subgraph whose declared inputs come out
  `name.input`, nesting to `outer.mid.inner.k`. With no context passed in, it finds the body's inputs by pinning
  each argument's arena at its first node before the call: an arena is only moved whole and appended to, so each
  pinned block is still contiguous afterwards, and every input outside the blocks — in the result's arenas and the
  arguments' (a leaf may merge into an argument and not the result) — is the body's. So nothing may carry an
  `Expr` in or out unseen, and each is refused while compiling, one error each: a lambda that captures or a
  member function as the body, a body that takes an argument by non-const reference (it is handed them `const`),
  an argument that could hold an `Expr` but is none (an `Output`, a class derived from `Expr`), a result other
  than an `Expr`, an array, a pair or a tuple of them. A body reading an `Expr` kept at namespace scope is the one
  capture no check sees. A prefixed name past `kMaxNameLength` is refused as any name is, before anything is
  renamed (the L5 review found a body's input handed out through a reference, and a derived argument). `graph_composition`'s strip spelled this way
  equals the `BuildContext` spelling node for node.
- `gradients(loss, {Grad{name, wrt}…}, level)` takes a declared `Output` and the level as an argument: the
  gradients are built by analysing the graph, so the level cannot be set on it afterwards. Same graph as
  `finish_gradients` with a context carrying that level, wherever that takes the loss; `gradients` also takes an
  output that is an input, through its identity. A default `Expr{}` target is refused in both spellings.
- The sample rate and the default precision a `BuildContext` carried are `set_sample_rate` and
  `set_default_precision` on the finished graph. The `BuildContext` spellings stay until L9 deletes them. *(`set_sample_rate` and `Graph::sample_rate` went
  in Decision 59.)*

**L6: the tensor members** (`frontend/ops.hpp`, `Plan/archive/14-dsl/03` §5). The structural builders moved from
`notation/structural_ops.hpp` and `notation/order.hpp` into `frontend/ops.hpp` under `detail` — each appends the
node it appended before, so every graph is the same — and `notation::` forwards to them until L9. `Expr` has the
members `x.sum(a)`, `mean`, `max`, `min`, `prod` (an axis or `Axes{…}`, `, keepdims`), `transpose`, `reshape`,
`slice`, `unfold`, `unfold2d`, `squeeze`, `unsqueeze`, `realize`, `sort`, `topk`, `quantile`, `median`. Axes count
from the front: a negative one is refused at the member, any integer is an axis, and a floating-point one does not
compile (`x.max(0.0)` meant the elementwise `max(x, 0.0)`; a deleted template catches it with and without
`keepdims`). The member `reshape` and `transpose` name element axes only and carry L4's mark, completed by the
analysis; a reshape target past `kMaxRank - 1` axes is refused at the member, and a full-length transpose order
by the analysis (03 §3.4, for Gerald). `image_pipeline`, `transformer_block` and `learn_a_filter` are rewritten in
the declared spelling and run through `Executor`, each still checked against its plain C++ reference.

**L9: the old spellings retired** (`Plan/archive/14-dsl/07` §5). Every graph moved in L7, so what stayed underneath goes.
- `Variable`'s `previous`, `is` and `value` are private, `Output` and `detail::variable_access` (a test's steps) its
  friends, and its name parameter is gone (a name never reached the graph). `delayed` is `detail::delayed`;
  `input(where, …)` and `finish(where, …)` are gone, their bodies `detail::input` / `detail::finish`; `record_dim` is
  in `detail`, and `declare_dim` and `extent_of`, which nothing called once the tests wrote `DimDecl{…}`, are gone. Deleted: `input(name)`, `finish(name, e)`, `input(g, name)` and
  `InputAnnot`; `frontend/build_context.hpp`, with `subgraph.hpp`'s context overload; `finish_gradients`;
  `notation/structural_ops.hpp`, `notation/order.hpp` and the stream-labelled `notation::` readings. The public
  `einsum`, `rearrange` and `reduce(spec…)` lower through the same parsers, the lowerings call `frontend/ops.hpp`'s
  builders, and their refusals name the function a user calls (`heinzel::einsum: …`, `heinzel::reduce: …`; a spec's
  syntax, shared by `rearrange` and `reduce`, as `heinzel: a spec …`). The stream checks the lowerings made could no
  longer fire, since no spec names the stream, and are deleted (OQ-10).
- What no declaration can say (DS-18: a symbol on an element axis or numbered 1, a rank-0 past, a cascade past
  `kMaxDimDecls`, a signal's element axis of no elements) is a test's graph on `detail::input`, graph-equal.
- The tests that compared a new spelling with an old one keep an oracle: through `detail::` where the old spelling
  lives on beneath; through the old readings' bodies for the notation (`einsum_detail::lower_two(parse(spec), …)`),
  since `hash_graph` reads only some attribute kinds and would not see a matmul's binding; and pinned to the
  artifact hash recorded at `22b7c9ef` where nothing is beneath (`BuildContext`'s settings and `inside`,
  `finish_gradients` at both levels — each graph checked to use only attributes the hash reads, and every gradient
  value to resolve to its level, which the hash does not tell from an unmarked one). The frontend's own tests are
  rewritten on the declared surface (`context_free_test`, whose subject was the deleted `input(name)`, deleted);
  the notation's refusals moved to the public spelling, and those of a spec that moves or drops the stream went
  with the old readings, since no spec can name it.
- `gate_public_spelling`, with its `_would_object` twin: the examples, the `_here` ports and the benchmarks name
  no retired spelling and no `detail` (whole words, blanks around a `(` allowed, `using namespace detail` and an
  alias of it caught, C++ comments stripped in one pass that steps over strings, prose elsewhere counted).

## Decision 52 — state carried across runs (`Plan/archive/14-dsl/05`; reverses Decision 42, as `Plan/archive/00` O-3 decided)

**Decided** (Gerald, 2026-09-18, O-3: "State is runtime storage outside graph semantics: the `t` prefix of a
buffer read with `back > 0` carries the previous block's tail; `work.reset()` clears it"; DS-4, 2026-09-29). A
workspace reused for the next run continues the stream: a recurrence, a delay, an input read in its past pick up
where the run before stopped, so a signal run in blocks gives the numbers of one run over the whole, bit for bit.
Carrying is the default; `reset()` starts over from silence. No IR field, planner, lowering or bridge changes: the
carry derives from `Buffer::prefix`, the band fields, `home`/`feed` and the accesses' `back`, identically in both
runners (`codegen/`, `loops/`), and `both_runners` compares them after every run, tails included.

**What carries** (both workspaces, `carry(feeds)` as the run's epilogue):
- every buffer outside a band that keeps a prefix: its last `prefix` samples moved in front of its block (a
  `memmove`; right for a prefix deeper than the block, which then keeps part of its own past);
- every feed read `D` samples into its past: a TAIL of `D` samples the workspace keeps after all its buffers
  (so no buffer moves, `loops_workspace_test`), which the head reads instead of silence at the slot's own offset;
- a stream band's buffers: their last rows carried at the end of `run_band`, where the per-run re-zeroing was.
Split stages and bands carry nothing of their own (a split never reads its own past along the split axis).

**The workspace API** (both): `carry`, `reset` (every carried region zeroed, nothing else), `seed(buffer,
history)` (a buffer's whole prefix: the history, then silence), `set_length(n)` (capacity mode: a workspace built
for `max` runs `n ≤ max` samples; per-sample strides do not depend on `n`). `Executor` adds `reset()`,
`set_initial` (a seed that takes effect at a fresh start: before the first run, at `prepare`, at `reset`) and runs
`run(frames)` through `set_length`. *(Revised by Decision 55: the workspace is a template on its program, `set_length`
and `built_for` belong to a stream program's alone, `seed_unchecked` beside `seed`, and the arena is exact.)*

**What cannot stream is refused.** `streams(P)` (`codegen/nest_streams.hpp`, `loops/streams.hpp`; the two forms
agree on every program `both_runners` runs): every carried region has the stream's length as its last extent, and
every carried band buffer is in a band along it. One-sample state broadcast over a longer signal would step once a
run, not a sample; two literal streams of different lengths that both carry state are refused conservatively
(DS-14). `Executor` refuses such a graph while compiling ("this graph's state does not run along the stream").

**What it cost** (`Plan/archive/12`, L3): the carry moves each carried region's whole past every run. A 375 ms echo at
48 kHz — 18 000 samples, 72 KB — costs 664 ns per 64-sample block, of which the block's arithmetic is 8 ns: 0.05 %
of the block's real-time budget (1.33 ms). A sliding origin that would amortise the move is backlog B-17. The bench
gate stayed CLEAN, the loop-tree runner within its factor of today's, and the runners' compile time moved by at most
2 %.

## Decision 53 — time is declared: a value is a tensor or a signal (`Plan/archive/14-dsl/03`; `Plan/archive/00` O-7 as DS-7 decided it)

> **Interim** (Gerald, 2026-10-01): the signal/tensor split is a band-aid, kept for now. The direction is one kind of
> value, a tensor whose axes are named — one named `time` makes it a signal, an index is defined along a named axis
> (`Index{"time"}`), and images and video name theirs the same way: backlog B-19 (`Plan/archive/13-loop-tree/11`).

**Decided** (Gerald, 2026-09-29, DS-7): the declaration says whether a value has time. A tensor is the default —
`Input w{"w", {64, 64}}`, the same numbers at every sample — and a signal is marked, `Input x{"x", Signal{2, 512}}`,
its last extent its length in samples. This departs from O-7's letter ("the analysis appends the length-1 stream
axis only when a value reaches a time-ruled op"), which cannot work: feeds are sample-major, so inferring time from
the ops downstream would transpose a caller's input when a delay is added; a weight `W{C, C}` contracted into a
recurrence reaches the recurrence without being a signal; and a memoryless audio graph (a gain, a bias) would lose
its time. So: inputs say which, and everything else follows.

**The representation does not change below the analysis.** A tensor is recorded with a literal length-1 axis
appended and `Value::timeless` set (hashed only when set); after the analysis every value has its stream axis as
before, and to the planners, runners, bridges and workspaces a tensor is a one-sample signal. Nothing below `sema`
reads `timeless`. Graphs that spell `{…, 1}` keep working unchanged as one-sample signals; declaring such a value a
tensor gives the same analysed graph but for the flag, so the programs stay equal — except across a contraction,
where a tensor meeting a one-sample signal is refused, so a contraction's operands migrate together.

**The analysis derives each value's kind** (`sema/time_checks.hpp`), right after the shapes settle and before any
other check words a refusal: neutral < tensor < signal, seeded by the inputs' and the generators' records (a literal
is neutral), joined over operands and handed from a recurrence's definition to its `Variable`, to a fixpoint. The
kinds are the pass's own and never written back, so a second analysis computes the same ones: idempotence holds by
construction. Each refusal is keyed on a rule column, never an operation, and has a fixed text: a recurrence whose
definition is not a signal (checked over every node first, so a tensor-only `Variable` reads that and not the lag
refusal); a lag or an fft of a tensor; a general contraction of a tensor with a signal (both runners computed wrong
numbers when only the old check was relaxed: backlog B-16); an axis past a tensor's last element axis, which is
its hidden time; and, for every value, a squeeze of anything but an element axis of extent 1, an unsqueeze past the
last element axis or past `kMaxRank` axes (whose shape helpers now guard their indices).

**A reshape may name element axes only** (`ViewAttr::elements_only`, set by the member `x.reshape({…})` from L6):
the analysis reads the target with the operand's last extent appended while shapes settle — keeping the operand's
shape while it is still rank 0, as a recurrence's own past is in the first sweep — and then writes the full target
back and clears the mark, once. The member's transpose order carries the same mark and is completed with the last
axis only when it is exactly one short of the operand's rank: inside a recurrence the rank grows between sweeps,
and until it fits the operand's shape stands; an order that never fits is refused. An unmarked order is read as
written (the L4 review: completing any order one short refused a short order in a recurrence and let a public
spec that named too few axes read its stream label as an element axis). The analysed graph then equals the one
built with the full target and order (analysed without bindings: with them, the appended extent is the bound one).

**The public notation never names the stream** (`heinzel::einsum`, `rearrange`, `reduce(spec, …)`, `iota`): each
term or side gets one label no spec can write, last, and is lowered as `notation::` lowers the same spec with it
written out — so each builds the graph its old twin builds once analysed, and a spec names at most
`kMaxRank - 1` axes, and every axis of the value it reads: at once where the rank is known while building (an
input, an `iota`), and for any value through `OpAttr::named_ranks`, the ranks the spec named, written on the node
it lowered to, checked once shapes settle and then cleared (the L5 review: a computed value went unchecked).
`iota({…})` is a tensor; `iota(Signal{…, frames})` a signal that records its `Length`; neither is a ramp along
time, so both need element axes. `notation::` keeps its meaning until L9 deletes it.

Amended by Decision 76 (B1): a recurrence no signal drives takes the graph's one length, inferred from a declared one.

## Decision 54 — declared lengths: a recurrence no signal drives says its own (`Plan/archive/14-dsl/10`, DS-22)

**Decided** (Gerald, 2026-09-30, DS-22, was OQ-9): a length may be declared on a `Variable` and on an `Output`.
Time is declared, not inferred (Decision 53), and a recurrence driven only by knobs or constants has none — the
knob-driven oscillator `phase[n] = frac(phase[n - 1] + f / kRate)` was refused from L4 ("a recurrence needs a Signal
in it"). `main` had `Output y{g, "y", {N}}`.

**The spelling.** `Variable phase{Signal{N}}` (or `Signal{C, frames}`, and `, f64`) and `Output y{"y", Signal{N}}`,
the latter defined by `y[n] = …`. The type defaults to `f32`, as `Input`'s does; a declared `Output` assigned with
`=` (or given another output with `=`) is refused at the assignment ("a declared Output is defined by y[n] = ...;
assign an undeclared one") — broadcasting a value up to a stated shape needs an operation no `ViewKind` is, and a
check would need a declared shape stored beside each output. A declaration is a `Signal`: `Variable v{{4}}`,
`Output y{"y", {64}}` and `Variable v{Signal{}}` do not compile.

**No new IR field.** The frontend records the declared shape, type and `Length` on the variable's leaf, and a
variable with a shape when the analysis starts is a declared one (a snapshot, `Declared`, taken before the sweep, as
`resolve_precision` takes its pins). After one analysis every defined variable has a shape, so a second keeps what
the first derived: idempotent. The sweep leaves a declared leaf as declared (`propagate_recurrence` skips it); right
after the sweep `declared_variables_hold` (`sema/declared_variables.hpp`, beside `write_shape`, moved there for
`analyze.hpp`'s line limit) requires each declared variable's definition to come out at exactly its shape and type
— exact, since the planners treat a variable and its definition as one value: "a Variable is defined as [4, 997]
f32 but declared [997] f32", and "a declared Variable is never defined" for a graph built by hand (the declarations'
`finish` refuses a variable read and never defined first, since L1; the old `finish(name, e)`, deleted at L9, did not).
A declaration names its type: `ElemType::Unknown` is refused ("a declared length needs a type"). A declared variable seeds the time kinds as a
signal, and the time refusal names the fix: "a recurrence needs a Signal in it, or a declared length".

**What it runs.** The positive path needed no change: the knob-driven oscillator at 997 samples and over a `Length`,
the declared `Output`, a bank, constants alone and a coupled pair with either one declared run on both runners at
`Normal` and `High` bit for bit against plain loops (`declared_length_test`); over a `Length` the oscillator sits in a
stream band whose blocks equal one run (`streaming_test`), and `Executor` runs it in blocks with the knob set once
(`executor_test`).

Amended by Decision 76 (B1): where the graph has exactly one length, such a recurrence takes it undeclared.

## Decision 55 — zero run-time overhead: a right-sized `Executor` (`Plan/15-zero-overhead/`; revises Decisions 50, 51, 52 and DS-12)

**Decided** (Gerald, 2026-10-02 and 2026-10-04, ZO-1–ZO-7, OQ-1–OQ-6). Nothing computable at compile time is computed
at run time, per run or when an `Executor` is made, and no byte of the compile result reaches an optimized binary
(`-O1` and up or `-Os`, with asserts on or off; an unoptimized one keeps what it names). Before, run-time
code read `Executor<…>::result` (the `Graph` and the `Program`, 664 KB), so one biquad built to 682 KB against a hand
loop's 14.5 KB, and another 6.5 KB of code worked out at run time what the program fixes (the layout, the carry list,
the ramps, the port checks on every run).

**Nothing of the compile result at run time.** The program appears only in constant expressions. What a run or a
construction needs is a `constexpr` table of exactly its entries, read at constant indices: `loops/layout.hpp` derives
the buffer layout (`kKindOf<P>`: `Static`, `Stream` or `Symbolic`), the pieces and their placement (`kPiecesOf`,
`kPlacementOf<P, C>` for a static program, the same `place()` at `prepare` for a stream), the carried regions, the band
pasts and the ramps; `executor.hpp` the ports' types, the Params' and the seeds' places and the `Length`'s constraints.
The runner takes slots, strides and starts as constants (`kStridesOf`, `kStartsOf`), so a static program's walk
folds, and it is inlined whole into the caller of `loops::run`: its code no longer depends on the program's linkage
(W8's bare arm ran 1.25 M instructions a run over its hand loop with the linkage users get, now 0.77 M).
- **`Executor::graph` is gone** (ZO-7; `build_refuses_executor_graph_gone`), and `result`, `graph` and `program` are
  private. `compile()` stays public, for inspection: reading a compile result at run time costs its size
  (`docs/limitations.md`). `try_compile` is unchanged (Q-1).
- **The run-time checks are debug-only** (OQ-1, `assert`'s rule): every input the program reads bound, every Param
  set, `prepare` before a run and before `reset`, a static program's `Executor` not moved from (its arena gone), the
  frames in range and within the `Length`'s constraints, `set_initial`'s history whole samples and no deeper than the
  output keeps. The element-type checks of the bindings
  stay in every build. The "set" flags exist in every build, so no layout depends on `NDEBUG`; there is no "moved"
  flag, and a moved-from `Executor` that can run, runs. Compile-time checks all stay. Revises DS-12's "a `Param` never
  set is refused at `run()`" and Decision 51's checks: in a release build such a run reads what the Param holds (0).
- **`set_initial`'s port is checked while compiling** (OQ-3): on an output that keeps no past (none, or no bytes of
  it), or with a history of the other element type, it is one compile error each (`Port::InitialF32`,
  `Port::InitialF64`; `build_refuses_executor_set_initial_*`).
- **The members** are each of their own count: the workspace by value, a pointer per input and per output, the Params
  in one byte array (F64s first), the "set" bits, the seeds (inside the object up to 64 B, else allocated at the first
  `set_initial`), and `fresh_` only where an output keeps a past. A run is the debug-only checks, `set_length`,
  `loops::run`, and a copy of each bound output: its whole, a sample per frame of a streamed one, a constant's one
  sample as often. Moves are `noexcept`.

**The workspace API** (revises Decision 52's, ZO-5). `loops::Workspace<P, Copies>` is a template on its program, an
alias of `detail::Workspace<P, C>` with `C` 1 unless the program splits a band, so a serial program has one workspace
type; `loops::GuardedWorkspace<P, C, Guard>` keeps `Guard` free bytes around every region for a test's canaries.
- A static program's workspace is made by its default constructor; a stream program's by `Workspace(n)`, for runs of
  up to `n` samples, and only it has `set_length(n)` and `built_for()` (`build_refuses_workspace_set_length_without_a_stream`);
  a symbolic one's by `Workspace(sizes)`. Its range check is debug-only.
- `seed(b, history)` checks; `seed_unchecked(integral_constant<B>, history)` is for a caller that checked while it
  compiled. `region(b)` and `tail(f)` are `ByteRange`s; `arena()` is the allocation, null for a program that keeps
  nothing.
- **The arena is exact**: one page-aligned allocation of what the program keeps plus the 4K-aliasing stagger (the
  placement probe kept it: packed blocks ran up to 1.26 times slower), no silence page, no trailing page, and one
  `memset` at construction (Q-3), so that the first run takes no page fault in it. W1's arena went from 10,496 to
  2,304 B for its 256 kept, W8's from 16.9 to 8.5 MB.
- **A ramp whose past nothing reads is a constant** (`Home::Constant`): its one sample, `kConstantOf<P, B>`, lies in
  read-only data, shared by equal ramps, and is read where it lies; it has no piece and no fill. `well_formed` refuses a
  store into a constant, a lag of one, and a walk or a gather along its last axis. A relaid ramp is still copied by its
  copy stage every run (OQ-6, until L11).
- `loops::run<P>` takes any `std::array<const void*, N>`, `N` at least the program's feeds.

**Capacities** (revises Decision 50's O-4; ZO-3, OQ-4, Q-2). Each is behind `#ifndef HEINZEL_MAX_<NAME>`, named in
its refusal and in `docs/limitations.md`, and cut by a sweep to Gerald's values: 128 values, 128 nodes, 24 IO, 128
statements, 512 tree nodes, 64 buffers, 16 stages, 128 stage statements (from 320, 320, 32, 512, 3,584, 96, 16, 128).
They cost no byte of an optimized binary, only compile time and memory.

**The gates** (ZO-6, OQ-2). `zero_overhead_binary` and `zero_overhead_runtime`, in `rw-clang-26` and `rw-clang-23`,
hold each `Executor` of `tests/zero_overhead/` to its bare run plus the copy of its outputs: at most 77 B
(`text + data + bss`) and 9 instructions a run, the worst row of both builds plus 32 B / 4 (from OQ-2's 128 B and 16).
Each failed on its own mutation: the compile result read through a laundered index, by 160,688 B and more on every row;
each port tested eight times a run, by 60 to 86 instructions. A count that perf refuses is a skip, never a pass. A
regression in the runner or the workspace shows in both arms and cancels out: the bare arm's row, compared with the
baseline at each review, catches it. *After the close's review:* a binary holds the harness's code too, laid out by the
inliner differently in each arm, so one limit left W3 700 B of room (a 500-character string only its `Executor` pulled
in passed); each row's bytes are now also pinned to its recorded delta plus 32 B (the W3 mutation fails, +423 against
at most −272). And the compile result reached an `-Os` binary, and one with asserts on: `run_band`'s carry handed a
reference into the program to a function left out of line, and at `-O1` the stage's extents were read in a loop over
the program's array. Both read constants now, and the gate holds every `Executor` built at `-Os` and with asserts on
under 32 KiB (the old carry restored: W8 at `-Os`, 173,659 B).

**Measured** (`Plan/15-zero-overhead/01`, clang 22, zen2, no `-march`): an `Executor` is 3.6–6.8 KB (173–178 KB
before), −628 to +45 B over bare + copy at `c1133c0d` (−604 to +45 at `8ebbaf8b`, the harness's buffers freed out of
line in every arm), 0.6–1.7 KB over the hand loop, and smaller than `main`'s on every row both run (9.0–11.7 KB).
It runs 2–5 instructions a run over bare + copy (10–48 before); making one costs 670–1,630 instructions on W1–W7
(2,239–3,994; W8 467,294, its 8.5 MB arena zeroed); its object is 32–80 B (1,872). Against `main` like with like
(`-march=native`) it is behind on W1 (80 against 28 instructions: the runner's 25 over the hand loop, and the copy,
which L12 removes), W7 (the contraction) and W8 (the band's code), both in `Plan/backlog.md`.

## Decision 56 — constant tensors in the graph, read where they lie (`Plan/19-constants/`; L11 of `Plan/archive/14-dsl`, DS-8)

**Decided** (Gerald, 2026-09-29, DS-8; 2026-10-05, the spelling and the pool). A kernel, a weight or a table the user
knows when the program compiles is written into the graph, not bound as an `Input` the caller copies in:
`constant(kTaps)` is the tensor {N} of a `float` (f32) or `double` (f64) array, `constant(kWeights, {32, 32})` reads
the array as that shape exactly as an `Input` of the shape bound to it would, and `constant({0.25, 0.5, 0.25})` is a
braced list, f32 unless `f64` is given. `constant(0.5)`, one number, stays a literal; `constant({0.5})` is a tensor of
one number, and so is `constant({0.5}, {})`, as `Input{"x", {}}` is (the type overload takes only an `ElemType`).

**In the IR** a constant is `_const`, the other generator beside `_iota`: the same rule columns, its own attribute
kind (`AttrKind::Constant`, appended so no kind's number moves), and `GeneratorAttr::first`, where its numbers start
in `Graph::constants`. The pool holds each number's `double` bits, so a NaN's payload and -0 compare; the frontend
rounds each number to the constant's type as it records it, so the pool holds exactly what every runner and the
reference walk read. Its capacity, `HEINZEL_MAX_CONSTANTS`, is 1,024 (OQ-8): a compile-time ceiling only, since
`Graph` and `Program` are fixed-size `constexpr` values. An empty pool costs no compile time (`arm_loops` 24.8 s,
`arm_executor` 33.1–33.6 s at 0 and at 1,024); a graph that fills it pays +0.14 s and +4.6 MB against its twin with an
`Input`. The analysis checks a constant's range in the pool; the analysis and the planner name no operation, keying on
the attribute kind and the row. Autodiff needs nothing: the row's adjoint class is `ZeroDerivative`.

**On the loop tree** a constant is `Home::Constant`, as a ramp whose past nothing reads is: one sample in a
`static constexpr` array of exactly its numbers (`kConstantOf`), built from the program's copy of the pool at
constant indices, read where it lies and broadcast along time, with no workspace bytes, no fill and no copy. Nothing
else of the graph or the program reaches a binary: W10 (a 3-tap constant kernel over 4,096 numbers) builds at -O1 to
3,989 B, at -Os to 10,363 B and with asserts to 4,597 B. **A relaid constant is made when the program compiles**
(OQ-6 of `Plan/15`): a contraction operand the planner relays, when it is a constant tensor or a ramp, gets a relaid
copy that is a constant too, laid out in blocks with zero padding, and no stage copies it. Its bytes equal what the
copy stage wrote; a run saves 0.04–0.1 % of its instructions and the arena loses the block (a 128 × 128 weight on zen2:
133,440 → 67,584 B). A constant's sample lies on a cache line (64 B), as a workspace block does: the review found the
relaid array 4-byte aligned, and that 128 × 128 run 7 % slower than with its copy stage (3.57 against 3.34 ms, min of
three); aligned, the two are level within the run-to-run noise (3.40 ms).

**What it does not enable** (`Plan/archive/14-dsl/09` §3): a contraction over a window (B-3) and a tensor contracted
with a signal (B-16) stay refused; a constant kernel over a window keeps the multiply-and-sum spelling
(`(x.unfold(0, 3) * constant(kSmooth)).sum(1)`). The old paths `09` refused constants on were deleted by Decision 58.

**Measured** (clang 22, zen2, no `-march`): W10's `Executor` is 3,921 B against bare + copy's 3,933 and runs 10,000
instructions against the bare run's 9,998, every arm bit for bit the hand loop with literal taps (10,812). W1–W9 are
unchanged in bytes and instructions. `image_pipeline` takes its kernels and colour weights as constants.

## Decision 57 — outputs written in the caller's buffer, and a small static arena inside its workspace (`Plan/18-zero-copy/`; L12 step A of `Plan/archive/14-dsl`, DS-21)

**Decided** (Gerald, 2026-09-30, DS-21; 2026-10-05, ZC-1–ZC-5). Two costs an `Executor` added over a hand loop: every
run copied each output out of the workspace into the caller's buffer, and a static program that keeps a few hundred
bytes allocated a page-aligned arena on the heap, so a binary referenced `operator new` and `delete` for it.

**A static program's arena of at most 4 KiB lies inside its workspace** (ZC-1), so an `Executor` on the stack keeps
it there. Its pieces are packed at 64 B, with no page stagger: within one page no two blocks can 4K-alias each other,
and where the arena lies against the caller's buffers is luck, as for any buffer of the caller's.
`-DHEINZEL_MAX_INLINE_ARENA=N` moves the line, the same in every translation unit (0: every arena on the heap, as
before; `inline_arena_off`). An inline arena is left as it is found, as a hand loop's buffers are: only the pasts and
the relaid blocks, which a run reads before it writes, are zeroed. `canaried` fills every other block with NaN, so a
run that read one first would show; none did. A moved-from `Executor` with an inline arena keeps a copy of it and runs
(OQ-1). Four placement goldens were packed; nothing else moved.

**An output is written straight into the buffer bound to it** (step A; ZC-2, ZC-4): when its buffer is a workspace
buffer that keeps no past, is neither a ramp, a band buffer nor relaid, and no earlier output names it
(`loops::kInPlaceOf`), and when its bytes overlap no input the program reads and no other bound output. Overlap is
decided at binding and at `prepare` (a stream program's bytes at its most frames), not per run: an overlapping output
is routed, written into the workspace and copied after the run as before, so every binding means what it meant, in
place (`y == x`) and "the last copy wins" included. The runner is unchanged: the workspace's `bytes_of` returns the
bound block (`bind`, which refuses an ineligible buffer while compiling), and a run tests one routed bit per output.
An output that reads its own past (W3, W4) is still copied: step B waits for Gerald (ZC-3). **The run is inlined into
its caller** (Gerald, 2026-10-05: speed first): one function shared by every call cost each run about 1.2 ns more (a
64-sample graph, 5.1 ns against 3.9; `elem` at 4,096 alike, 367 against 365) and a one-site binary 60–90 B, while
inlined each call site carries the runner (0.5–2.4 KB a site; the review of L12), so a program that runs one graph from
several places wraps `run` once. **The instruction limit is 9 or 0.1 % of the run**, whichever is more (Gerald,
2026-10-05): W7's +14 (+4 for the routed test, about 10 a padding `nop` that moved into an outer loop) is about 1.5 ns
of a 25 µs run, noise; an output copied again costs +36 to +784,166 and still fails. Against the copy path, with
page-aligned buffers: the 64-sample graph 10.6 → 3.9 ns, `elem` at 4,096 584 → 365 ns.

**Measured** (`Plan/18-zero-copy/`, clang 22, zen2, no `-march`): the zero-copy rows run 2–5 instructions over the
bare run (W1 93 against 91, W2 6,696 against 6,691, W8 11.91 M, 0.78 M fewer than with the copy) but W7 (+14: one
more callee-saved register for the routed test and a loop-alignment `nop` in an outer loop), which the gate holds at 9
and which went to Gerald. The bytes over bare + copy are −288 to +68 (the routing at binding is most of the growth).
The minimal program (`y = x * 0.5` over 64 floats against its hand loop's 2,562 B) is 3,331 B before, 2,699 with the
inline arena and no heap symbol, and 3,015 with zero-copy (its routing and routed copy; `memcpy` again).
`elem/heinzel-executor/N262144` runs 46,460 ns against 55,113 before (the bare control 32,289): the rest is the
benchmark's own placement, 4K aliasing between its vectors, now the caller's (30.5–32.4 K ns page-aligned).

## Decision 58 — the old runners deleted; the interpreter stays as the reference (`Plan/16-simplify/01`; 55–57 are taken by `Plan/15` and `Plan/archive/14-dsl` L11/L12)

**Decided** (Gerald, 2026-10-03): the generated runner (`codegen/`) and the scheduled runner
(`runtime/execute.hpp`, its workspaces, the old `compile<G, T>()` and `schedule/plan.hpp`) are deleted. They
served no user: the DSL, `Executor`, `compile` and every example run the loop tree only, and every operation
they ran the interpreter runs too, so no graph lost its runner — with one narrowing: a graph only the walk
runs (a framed stream, an overlap-add, `_contr`, …) runs at block lengths fixed at compile time only, since the
walk's block is a template constant and the old runtime-sized run is gone. This closes `Plan/archive/00` P9, except that the
interpreter is not retired: it is the reference every planned run is checked against (Decision 48's "until P9"
ends here), and `Plan/16-simplify/02` moves it into `tests/reference/`.

What went with them: the old `Schedule`, `Placement` and `Storage`; the tests whose subject was an old runner; the
benchmarks' `heinzel-old` arms. `Target::share_storage` and `Machine::immediate_constants` stay, read by nothing
now, because a user may write them. *(They went with `Target` in Decision 59.)* Tests whose subject is a feature run on
`Executor` where the loop tree compiles the graph (at `High` where they compare with the walk bit for bit) and on
the walk where it does not; `Signals` takes its plane count as a parameter, so the wide training graphs walk on
the heap.

**Conservation keeps the analysed-graph hash only.** The order, placement and emitted-program hashes recorded the
old schedule's and the generator's decisions, which no longer exist; the loop tree's programs are pinned by the
`tests/goldens/loops_*` listings. The twelve graph hashes did not move.

## Decision 59 — the decided API of 2026-10-04 (`Plan/17-simplify-more/02`)

**Decided** (Gerald, 2026-10-04), each a change a user sees.

**The units dimensional check goes** (reverses Decision 7's `UnitsRule` reader). It caught adding a duration to a
rate, in graphs that stated units on an input, and no example, benchmark or user graph stated any. What went: the
check (`sema/units_check.hpp`), the dimension algebra (`ir/units.hpp`), the `UnitsRule` column, `_reduce`'s
`ReduceUnitsRule` table, `Value::units`, `Input`'s fourth argument, and autodiff's refusal of a graph with units
(exclusion E8). What stays is the duration: `units::ms`, `second`, `us`, `to_samples`, `samples(n, d)` and
`n - d`. A `Quantity` is a duration in seconds; `units::hertz` went with the algebra, so a rate passed where a
duration belongs is a type error instead of a refusal. The conservation corpus's units entry (4) lost its units
and keeps its index; every recorded hash moved once, because the hash no longer mixes a value's units.

**`Graph::sample_rate` and `set_sample_rate` go.** Nothing read the field: a duration is folded at the rate the
sample clock carries (`constexpr Index n{48'000}`), and the rate moves to the time axis with named axes (backlog
B-19). Callers drop the call. Every conservation hash moved once more, because the hash no longer mixes the
rate; no graph moved.

**`Executor<build, Machine>` replaces `Executor<build, Target>`.** `Target` held a `Machine` and a field nothing read,
so `Executor`, `compile` and `try_compile` take the `Machine` itself (`Executor<comb, zen2>`, `compile(g, zen2)`).
`Target`, `default_target`, `Target::share_storage`, `Machine::immediate_constants` and `schedule/schedule.hpp` went
(reverses Decision 58's "stay"). A `Machine` is passed by reference, as the program is.

**`select` goes; `where` stays.** Two spellings of one blend; `where` is the one the tutorial teaches and numpy
users know. The operation keeps its internal name, `_select`, and the lane primitive `select` in `runtime/` is
not a spelling of the DSL. The ports that wrote `select` (`conditionals_here`) write `where`.

**Refusals name what the user wrote**, where that is mechanical. Each row of the rule table carries, beside its
internal name, the one spelling the DSL has for it, if it has exactly one, and a refusal names that: "einsum's
contracted extents disagree" for `_matmul`, "shapes disagree at +" for `_add`, "unfold windows 9 of an axis of
extent 8". 22 operations have one: the four arithmetic operators and unary minus, `sqrt`, `rsqrt`, `exp`, `log`,
`sin`, `sinpi`, `tanh`, `floor`, `min`, `max`, `where`, `slice`, `unfold`, `unfold2d`, `iota`, and einsum for
`_contr` and `_matmul`; a composed helper (`abs`, `frac`, `clamp`) reuses the spellings it is written in. An
operation with several spellings, with none, or whose spelling swaps its operands (`a > b` is `_lt(b, a)`) keeps
its internal name; `Plan/16-simplify/08-Later.md` lists them. Programs and the printed listings still use the
internal names.

## Decision 60 — the IR's tables, one per fact (`Plan/17-simplify-more/03`)

**Two operations were encodings of others, and folded into them** (reversing M4.4's two lags and M5 S4b's `_pack`).
`_pack` was `_concat` whose operands also had to be equal on the joined axis: the same shape law, element reader
and adjoint, and one more refusal. `_delay` named the axis its lag ran along, and only the innermost was allowed,
the axis `_fdelay` always runs along. A planar pack is a `_concat` of two equal operands, and a lag is `_fdelay`;
the refusals that were theirs alone went with them. Their codes, 40 and 50, are **retired** rather than reused,
and `AttrKind`'s 7 with `_delay`: the rule table keeps an empty row for each (`is_retired`), so no later code
moves and a graph that used neither keeps its encoding and its conservation hash. Checked by the differential
(701 graphs on 4,379 machines): only the three graphs that used `_pack` and the one that used `_delay` changed,
in their op encoding; their programs and their reference-walk numbers did not.

**`opt/rewrite.hpp` goes** (decided by Gerald, 2026-10-04; reverses Decision 13). Run by `compile` and
`try_compile` between analysis and planning, it put a commutative operation's operands in one order, so that
two evaluation orders of one C++ expression gave one graph. IEEE add and multiply commute bit for bit and the
planner plans either order, so no number depended on it. The pass, the `Commutativity` column it was the only
reader of, and conservation entry 11, which recorded its decision, went. Programs moved where a sum or a product
arrived with its higher-numbered operand first: 24 of the agreement corpus's graphs, each statement reading its
operands the other way round; no refusal moved, no golden listing moved, and the bench gate passed.

**The rule table lost what no reader needed, and took in a table of its own.** The `Precondition` column (two
rows) became the first refusal of `_fft`'s and `_order`'s attribute checks, in the same words. It now runs node by
node with the other attribute checks, not in a pass before them, so a symbolic element axis that reaches a view
(a pad, a join or an unfold of it) before an order or an fft is refused by the view ("… not known yet"), not by the
order or fft. No declaration spells a symbolic element axis, so only a hand-built graph does this, and
`order_test.cpp` and `spectral_fft_test.cpp` pin which refusal wins. `CostClass::Tiered` marked exactly the rows
`NumericContract::Approximate` marks, and the numeric column keeps the fact;
`ShapeRule::Opaque` had no row; `ir/attribute_rules.hpp` renamed `ViewKind` case for case. `autodiff/classify.hpp`'s
per-code table is the adjoint column, and the lockstep assertion names every code. Write-only state went too:
`ValueCategory::Output` (the graph's outputs say what a result is), `Grants::own_type_accumulator`,
`DimConstraints::expect` (`Length::expect` stays, a hint the graph does not keep), and `reassociate` and
`contract`, always equal, are one grant, `reorder`.

## Decision 61 — the analysis says each condition once (`Plan/17-simplify-more/04`)

**One function per family shapes and refuses.** Each structural family (slice, pad, join, flip and transpose in
`sema/view_shape.hpp`; unfold and unfold2d in `sema/framing.hpp`; `sema/order.hpp`, `sema/spectral.hpp`,
`sema/overlap_add.hpp`) tests its conditions in the order its refusals are reported. The sweep calls it without a
`Diagnostic`, and it hands its operand's shape back where it cannot make one; `check_attributes` calls it once more
with one, and the first condition that fails is worded. The shape function and the check used to test the same
conditions twice. A placeholder is not a refusal: a condition the shape does not need (a slice's step, a pad's
amounts, the streamed axis of a slice, pad, join or flip, an order past the ceiling, an fft of length 6, a window up
to `hop - 1` too wide) is asked only when wording, so the sweep makes the shapes it made before, and no refusal
moves to a downstream `shapes disagree`. Refusals are still reported after the time checks, in node order, so a graph
with two faults names the one it named. `matmul.hpp` keeps its shape and its check apart, since the shape refuses
nothing; `attribute_checks.hpp` holds the checks that make no shape (the lags, iota, table, and reshape, which reads
the resolved output). `sema/refusal.hpp` words a refusal.

**Files: 21 to 13.** For the decisions above that cite the old names: `concat_checks.hpp` and `pad_checks.hpp` are
`concat_shape` and `pad_shape` in `view_shape.hpp`; the unfold checks of `attribute_checks.hpp` are `unfold_shape`,
`unfold2d_shape` and `frame_axis` in `framing.hpp`; `order_shape.hpp` and `order_checks.hpp` are `order.hpp`,
`spectral_shape.hpp` and `spectral_checks.hpp` are `spectral.hpp`, `overlap_add_shape.hpp` and
`overlap_add_checks.hpp` are `overlap_add.hpp`, `contract_shape.hpp` and `matmul_checks.hpp` are `matmul.hpp`; and
`iota_checks.hpp`, `table_checks.hpp` and `reshape_checks.hpp` are in `attribute_checks.hpp`. Of the functions,
`strided_range_shape` is `slice_shape`, `padded_axis_shape` is `pad_shape`, `concat_axis_shape` is `concat_shape`,
`framed_window_shape` and `framed_stream_shape` are `unfold_shape`, `fft_axis_shape` is `fft_shape`, and
`order_axis_shape` is `order_shape`. `gate_no_per_op_branches` reads every header of `sema/`.

Checked by the differential: 701 graphs on 4,379 machines, the 255 distinct graphs the analysis tests analyse, and
243,186 attribute mutations of both. Every analysed graph, program and refusal text is identical. One graph outside
it changed, for the better: a hand-built rank-0 operand unfolded at axis -1 stopped constant evaluation
(`extent_at(in, -1)`) and is now refused as "unfold unfolds axis -1, which this value of rank 0 does not have". Only a
hand-built graph reaches it, since `from_the_front` fails on a negative axis.

## Decision 62 — an operand striding along a product's lanes is read from a copy turned around (`Plan/20-score-product`; V5 of `Plan/archive/00`)

**Decided** (Gerald, 2026-10-05: V5 before M5.1; 2026-10-06: `Machine::turn_pays_after` stays a `Machine` setting, as
`relay_pays_after` is). Attention's score product `Q·Kᵀ` reads K with its rows contiguous
along `d`, the summed axis. Lanes across the output's columns `k` would read K a row apart, so the planner ran its
lanes along `d` instead, each of the N² outputs ending in a horizontal reduction: at 512 × 64 the score product took
841 of attention's 1,298 µs, and attention ran 1.22–1.25× of `main`'s time, the gated rows furthest behind.

**The rule**: where one operand of a contraction strides along the output's lane axis, is read as itself and has each
of its numbers read `Machine::turn_pays_after` (32) times or more, the planner plans the register tile as for any
product and asks for that operand turned (`Schedule::Stage::turned`). The lowering reads it from a copy turned around,
its axis carrying the lanes swapped with its contiguous one (`Buffer::turned`), filled by a stage of its own one element
at a time, in tiles of 8 × 8 of the two axes (a blocked buffer is filled in lanes along its blocked axis, which would
read K a row apart; untiled, a 512 × 1024 K's copy ran 4–5× slower, its rows a page apart, every read a new line of one
cache set). The usual relay blocks the turned copy where that pays. A constant (or ramp) is turned, and relaid, when the
program compiles. The register tile reorders no sum, so `High` takes it too. Where a program has no stage left for the
copy (`kMaxStages`), the stage falls back to today's plan, unsplit: lanes along the contracted axis where the precision
grants reordering and the axis holds a lane, else none; the copy's buffer is not
counted there, so at `kMaxBuffers` such a graph is refused rather than declined.

**The reuse bound** is measured (`Plan/probes/v5-score/reuse.cpp`): a product reading K once ran 10× slower turned and
a dense layer per sample 1.6×; 16 reads break even. At 32 reads, tiled, K 512 × d: 0.49 / 0.52 / 0.61 / 0.69 / 0.76× of
the time not turned at d = 64 / 128 / 256 / 512 / 1024. Measured on one core; the copy runs on one worker while a split
product's other workers wait (parallel runs are parked, P6).

**Measured** (zen2, one core): the score product 3.08 / 12.6 / 54 / 222 / 840 µs → 1.8 / 5.7 / 18.9 / 73 / 278 µs
at 32 / 64 / 128 / 256 / 512 rows (K given turned by the caller: 1.35 / 4.7 / 17.3 / 69 / 266); attention at 512 × 64
741 µs. The bench gate: `attn/*` 0.65–0.73× of `main`'s time (512 × 64: 752 against 1,064 µs, before the tiling). The differential moves `attention<128,64>` alone; every
other program, the zero-overhead workloads included, is unchanged. A faster turned copy (a register transpose) would
lower the bound and close most of the rest of the gap to K given: not built.

## Decision 63 — rows kept near the row by bands (`Plan/21-row-bands`; M5.1 of `Plan/archive/13-loop-tree/08` as option (c))

**Decided** (Gerald, 2026-10-05: option (c), no new IR). M5.1 asked to compute a producer at a loop of its consumer
(row-level fusion), with a new representation first: (a) one stage with sibling nests after a fold, or (b) a stage
placed under a loop of another. The probe (`Plan/probes/m5-rows/`) found today's bands already give it: a band of a few
rows over a fold and the map that reads the same row again keeps the chunk's rows in the first-level cache between the
two reads, bit for bit the same numbers. It also found the targets that would have needed (a) or (b) gain nothing:
softmax −1 %, attention's softmax ±0, whose gap was its score product (Decision 62).

**The rule**: a stream run is banded also where two of its stages read one value that outgrows the second-level cache
(`Machine::l2_bytes`) and the second of them is cheap (every node `CostClass::Cheap`), in chunks of
`Machine::band_stream_rows` (16) rows, as any stream band. Behind a costlier stage (an `exp`) the second read is
hidden, so the cost condition keeps softmax and attention as they were.

**Measured** (zen2, one core): rownorm 32.5–33.0 → 25.3–26.8 µs (−20 %), rmsnorm 44.6–46.5 → 43.0–44.4 µs (−4 %; its
best height in the probe, 8 rows, gave −15 %, but the height is the machine's for every stream). `ml/rownorm` 0.15× of
`main`'s time. No other graph of the differential's corpus moves; outside it the rule also bands column norms on
machines of 16-byte vectors, norms over the last axis of a 3-D value, and literal-length signals (each sample less its
mean over the voices, with and without feedback) — each checked by the review bit for bit against the run stored whole
and, at `High`, against the walk.

**Amended by the review**: a stream bands by this rule only where `band_stream_rows` rows of the value fit `band_bytes`.
Wide rows gained nothing banded (rownorm 40 × 32768 and 17 × 65536: ±2 % on one core), and a band split among workers
lost most of its ranges (24 to 3).

## Decision 64 — a value two cheap stages hand on past the L2 bands the run (`Plan/22-conv-bands`)

**Decided** (2026-10-06, under Decision 63, which Gerald chose as M5.1's option (c): planner rules, no new IR). `conv/*`
at N1048576 ran 1.04–1.20× of `main`'s time. `perf` on both binaries: `main`'s fused arm computes the square per tile
and runs its taps from the first-level cache; ours stored the square whole (4 MB) and read it back, because the planner
banded only a run whose working set outgrows the last-level cache (conv's: 12 MB). Its tap loop also keeps 4
accumulators against `main`'s ~9, a latency chain at 32 taps; a fused multiply-add alone gains nothing there (±0, as in
September), with 8 accumulators 1–2 %.

**The rule**: Decision 63's, widened to every run and to a value handed on: two stages of a run touching one value of
more than `Machine::l2_bytes` — the first to make or read it, a later one reading it — band the run, the later stage
cheap (every node `CostClass::Cheap`) and, where the first made it, that one too; in a stream only a value read twice
counts (a value a recurrence hands on, chunked into 16 samples, ran 5–17 % slower). A stream at `band_stream_rows` rows
where that many rows of the value fit `band_bytes` (the review of Decision 63), any other run at the height
`band_bytes` gives. The maker's condition keeps softmax and attention as they were: their `exp(x - max)`, banded, ran
±0; requiring every stage cheap would have lost rmsnorm (a `sqrt` per row).

**And with workers** (the review of this plan): a band split among workers hands each whole chunks, so a band of 17
chunks on 16 workers left one making two while the rest waited — newly banded runs, just past the L2, have few chunks.
Where `Machine::cores` is more than one, a band's chunks are made at most a whole multiple of it, each a little lower
(the height rounded up to whole lanes blocks: rounded down, the first version of this added a chunk and brought the 17
back for sizes not a power of two — the second review), unless the lower chunks would no longer pay. Measured on 4 workers (the box allows no more), a zen2 program: conv at
1M 159 → 122–135 µs (unbanded 151), at 32 taps 316–347 → 248–254 (unbanded 261), at 256K 62 → 31 (47), blurxy at 512
rows 74 → 62 (84–89); on one core level or 1–3 % faster.

**Measured** (zen2, one core): conv N1048576 −5 to −9 %, blurxy at 512 rows −20 to −27 %, harris at 232 rows
(9.6 MB) +0.5 to +1.3 % (noise-level; the one loss). A sample less its mean over 8 voices (Decision 63's rule on a narrow
stream) −25 %. In the differential's corpus the rule moves nothing; the evening moves blurxy<1024> on 55 machines and
harris<1024> on 18 (heights only). The bench gate in Plan/22's "As built".

**Not taken**: 8 accumulators for a fold across outputs from 24 taps on (−4 to −8 % at 24–64 taps, +3 to +10 % at 12–16
taps, where clang writes out a 4-accumulator tap loop and not an 8-accumulator one) — for Gerald.

## Decision 65 — feedback cycles that feed each other share one stage (`Plan/23-audio-scheduler/02`)

**Decided** (2026-10-07, Plan/23 P2, under Gerald's go for the plan). The planner gave every feedback cycle a stage of
its own (`planner/stages.hpp` `joins`: a node of another cycle closed the stage), and put the cheap maps between two
cycles in a vector stage of their own. A cascade of k biquads so ran as 2k loops, one after another over the block. A
feedback loop is bound by latency, each sample waiting for the last: alone, the sections' latencies add up; in one loop
the processor overlaps them, and the cheap maps between them fill slots it would leave idle anyway (in lanes they bought
nothing at audio block sizes, as Faust's `-vec` split pays only over thousands of samples: `Plan/probes/audio-iir/`).

**The rule**: a feedback cycle that reads a cycle of the open stage at the present sample, directly or through cheap
maps only (a plain map of `CostClass::Cheap`, or a hand-on), joins that stage with those maps, when both walk the same
domain and the whole cycle fits what the stage has left (below). A map joins only when it reads what the stage makes
and leads into a later cycle (`find_chains`: one pass forward for the depths, one back for the marks), and only when that
cycle could follow it. The stage keeps its first cycle's id; the passes after it see a feedback stage as before.

**And**: in such a stage, a one-sample lag that is no node of a cycle and reads what the stage makes runs just before
its maker (`lags_before_their_makers`), so the lowering hands the value on in a register as it hands on a cycle's own
state. Read after its maker it went through memory, and over a run-time length clang joined two such loads of adjacent
samples (`s[n-2]`, `s[n-1]`) into one wider load, which the store of the sample before cannot forward to: a
three-section cascade 6.7 ns a sample merged, 2.9 with the lags moved (3.0 with clang's SLP vectorizer off, which
confirmed the cause), bit for bit. (Amended 2026-10-08, Plan/23 3.2a: a cycle's own one-sample lag of what its stage made earlier moves too, so a lag
of a lag, `y[n-1][n-1]`, is handed on in a register: `ladder_bank` 3.15 → 3.10 ms, and the biquad of Decision 66 needs
it. A lag that closes a cycle reads a node after it and stays.)

**Bounded** (the review's probes, which found no wrong result but three ways to lose time): a merge stops where
- its work a sample, nodes times lane bodies (its voices over the lanes the machine's vector holds), would pass a stage
  (`kMaxGroupStmts`), or its handed-on states (one-sample lags of what a stage makes) times its bodies would pass the
  machine's registers: unbounded, a bank of 32 voices in eight sections ran 1.16–1.26× its split time, spilling; bounded
  (two sections a stage) 0.70×; allowing twice the registers changed nothing (0.98–1.04×);
- a run of cheap maps between two cycles is more than 10 dependent operations deep (`kChainDepth`): merged, the maps
  run a sample at a time instead of in lanes, which pays while the cycles' latency hides them (9 and 10 deep
  0.84–0.91×, 12 the same, 16 1.35×, 32 2.4–2.8×; 15 maps summed as a tree 4 deep the same);
- the cycle a map leads into waits on something outside the chain (`could_follow`): the map gave up its lanes for
  nothing, 1.07–3.1×;
- its statements, nodes and the loads of what it reads from outside, would pass `kMaxStageStmts` (reachable only with
  raised capacities, where such a graph was refused: fifteen sections, 90 nodes and 46 loads, now take two stages).

**Stays as before**: the part before the first cycle that reads only feeds (it keeps its lanes: 3.32 against 4.23 ns
a sample with it merged too, `edited.cpp`); maps after the last cycle; a map that is not cheap (a `tanh`, a `sin`)
between two cycles; a map of the feed alone that a later cycle reads (the chain breaks there); cycles over other
domains. Twelve sections outgrow one stage and take a second; the maps before the cycle that no longer fits may then
straddle the boundary, their values stored. Every program of the differential's corpus (15,730) and every golden is
unchanged; so are `measure.sh`'s bytes and counts. The same operations run in the same order, so the rule holds at
every precision: `nest_feedback_chain` checks cascades of 1 to 12 sections, a bank of 16 voices, a `tanh` between and
a map of the feed against loops by hand, bit for bit, and against the walk at `High`. The review's harness, rerun on
the bounded rule: 56 graphs (stranded maps, deep chains, wide banks, many loads, lags between and of lags, outputs
mid-chain) and 45 random ones against the walk at `High` and bit for bit against the planner before at `Normal`, 12
through `Executor`, worker splits byte for byte the serial run; of 10,000 random graphs on each of four machines, none
the planner before planned is refused, and 232–552 it refused are planned.

**Measured** (zen2, core 4): `dsp/cascade4` 2.75–2.81× `main`'s time → 0.98× (the gate, 44 rows CLEAN; its factor back
to the rule, 1.05); at blocks of 32 to 256 samples 2.1–2.3× its hand loop → 0.90–0.96×. The three-section EQ of the
comparisons: over a run-time length 148 → 332–345 M samples/s (KFR 261 at 256 samples, 374 at 16384; FAUST's vector
mode 175), whole over 1M samples 124 → 279 (KFR 370); the eight-section EQ 48 → 73 (KFR 316: its sections in lanes,
`Plan/23` P5). Rows without chained cycles unchanged: the strip, the FM oscillator, the one-pole and ladder banks.
Against the planner before this decision, bounded (the review's harness, 256 and 4096 samples): one voice of four
sections 0.38–0.40×, of eight 0.52–0.55×, sixteen voices of four 0.69–0.71×, 24 and 32 voices of eight 0.66–0.75×, a
soft-clipped bank 0.93–0.96×; deep or stranded chains 0.96–1.03× (planned as before).

**Amended (Plan/23 P4 4.3a, 2026-10-09): the merge is the cost model's call.** `kChainDepth` (ten maps) is gone: the
planner joins a cycle, and the cheap work leading into it, to the open feedback stage where the cost model
(`schedule/cost.hpp`) prices the stage cheaper joined than closed before them, and a map leads into the nearest cycle
it reaches (an allpass's `-in` skips its cycle; marked with the farthest, the whole chain of allpasses had waited on
the last). Its window term is what ten maps deep measured: one sample's path overlaps the next ones' only as far as
the out-of-order window holds them. So a cascade as written past it splits (eight sections 38.7 → 29.5 cycles a
sample), and the reverbs' combs and allpasses join where it pays: `apchain` 0.65×, `maiverb` 0.90×, `reverb` 0.84–0.85×.
The register bound (`still_fits`) stays. Of the differential's 15,730 programs, none moved.

## Decision 66 — the rewrite layer, and its first rule: a feedback sum in the order its terms are ready (`Plan/23-audio-scheduler/03`)

**Decided** (2026-10-08, Plan/23 P3, under Gerald's go for the plan; Q5: `Compiled::graph` is the graph as
rewritten). Decision 60 deleted `opt/rewrite.hpp`, which won nothing measurable; the layer comes back because its first
rule does.

**The layer**: `include/heinzel/rewrite/`, `rewritten(graph, machine)`, between `infer` and the planner in `compile()`
and `try_compile()` and in the benchmarks' `kProgramOf`, so an arm runs what an `Executor` would. It reads `ir/`,
`sema/`, `schedule/` and the graph facts of `loops/graph/`, nothing of the runtime (since 3.6; it read whether the build
had the fused multiply-add before); nothing in `loops/` includes it (the spine gate). A rule names no operation (`gate_rewrite_names_no_operation`,
which now also refuses a rule-table name such as `"_fma"`), changes rounding only where a value's precision grants
reordering (never at `High`), and leaves the graph a fixed point of `infer` and of itself.

**The rule**: in a feedback cycle, at a precision that grants reordering, a signed sum of three or more terms, or of
two whose last would fuse, is evaluated in the order its terms become ready around the cycle — what reads nothing of
it, its older states (a lag of a lag counting as older), its newer, what it makes this sample — and where the machine
has the multiply-add and so does this build, its last term, a product (one factor timeless if it is subtracted, which is negated: a constant as it is copied,
a Param by a `_neg` off the chain; Plan 25 step 2), is fused: a biquad's newest state waits on one multiply-add instead of a multiply, an add and a subtract. What an
operation is to a sum is the rule table's `Algebra` column. It declines a sum with a term that reads its cycle's past
two or more samples back through a lag that closes the cycle: the lowering hands only a one-sample lag on in a
register, and in ready order that store-to-load path became the chain (the bench biquad 1.04–1.06× and the cascade
1.09–1.12× slower without the check). With it, the planner also runs a cycle's own lag of a lag before its maker
(Decision 65, amended), so both states of a biquad written `a = y[n-1]; b = a[n-1]` are handed on in registers.

**Measured** (zen2): that biquad 2.08–2.20 → 1.42–1.48 ns a sample; `onepole_bank` 0.08× → 0.07× `main`;
`ladder_bank` 0.97×; an SVF 4.1 → 3.15; the biquad and the cascade as written (`y[n-2]`) unchanged, so `dsp/biquad`'s
expected 1.5× is not met. Accuracy, against the same filter in double, fed noise: the rewritten low-passes at 20 Hz,
1 kHz and 10 kHz have 0.36–0.74× the error as written fused and 0.54–0.73× reordered only (a build without the
instruction), the bench cascade 1.24× and 1.32×. A measurement, not a bound: an
ill-conditioned filter fed a tone goes either way (an RBJ peak at 10 Hz, Q 30, −6 dB, fed 15 kHz: 29× the written
error over 4096 samples, 396× over 65536; fed 300 Hz, 0.3×). `Normal` grants the reordering; `High` keeps the order.

**Tried and withdrawn**: reading a two-sample lag as a lag of a lag in the graph (a probe of that form, its schedule
edited, ran the biquad at 1.41 ns). `Executor::set_initial` seeds an output's own stored past, and split, `y` kept one
sample: the seed of `y[-2]` reached nothing (`executor_state` aborted).

**The review** found no wrong number, and four faults, fixed: built without the instruction, the rule's `_fma` ran in
software (a one-pole bank 17× slower; now the rule fuses only where the build does, and reorders elsewhere); it was not
a fixed point on 152 of 839 random graphs it changed (now each settles in one pass); a step over a bank's mono terms
took the bank's shape; its sort was quadratic.

**The second review** found three more, fixed: a double sum's float terms, first in ready order, were added in float
(the rule now declines a sum with a term narrower than itself); a graph the rewrite made unplannable (one stage past
`kMaxStages`) is planned as written (`planned_as_rewritten`, in `compile`, `try_compile` and `kProgramOf`); the fused
chain is run by the test in every build. Fusing only where the build has the instruction makes `-march` change the
program: two translation units built with different flags give different numbers from one `Executor<G, M>`. The
float cores at `Normal` already round as the machine rounds, so this was already so: a program is built for one
instruction set, as for one set of capacities.

**Open, for Gerald**: two-sample state in registers in the lowering (the biquad as written, the cascade, the parallel
form all wait on it; built, Decision 67); the parallel form (3.3: 2.5× the cascade by hand, at twice its error — a form change, his
numerics call, Q4); double state (3.4), which as designed breaks `set_initial` the same way; an internal operation for
a multiply-add rounded as the machine rounds, the cleaner fix of the software `_fma` (and of the same, older cliff in
contractions the planner fuses). *Since then* (P3's close): the two-sample state is Decision 67, the multiply-add
and double state are the amendments below; the parallel form, approved (Q4), waits on P4, its bank's feed-forward
placement being the cost model's call (3.7).

**Amended (3.6, Gerald's yes of 2026-10-08): `_madd`.** The rule fuses into `_madd`, `a * b + c` rounded as the machine
rounds: once where the build has the instruction, twice where it does not (`multiply_add_as_the_machine_rounds`, now
in double and over lanes of either type too). A new rule-table row (code 58, `Cheap`, no DSL spelling) and a new
numeric contract, `MachineRounded`: the rounding is the build's, a freedom only where the value's precision grants
reordering, so only a rule that asks the grant emits one. The rule fuses wherever the *machine* has the multiply-add,
so the program no longer depends on `-march`: the differential over 15,730 (graph, machine) pairs gives the same
programs built plain and with `-march=native` (495 differed before). In `-march` builds each of those 495 moved by
`_fma` → `_madd` alone, the same instruction, no stage or band moved (a `_madd` is `Cheap`, so 3.2's band watch is
moot); in plain builds the same 495 now fuse, a multiply and an add before, the same two roundings. `_fma` keeps its
promise of one rounding and is no part of a sum (`Algebra::None`); the role is `ProductAdd`. Adding the row tipped
the inliner: `compute_as`'s chain went out of line in every binary (+5.6 KB each, the zero-overhead pins moved), so
`compute_as` and its families are now forced inline where the build optimizes (`HEINZEL_COMPUTE_INLINE`), and every
binary is back at its size.

**Amended (3.4, Gerald's Q1 (b) and his "set_initial is not needed" of 2026-10-08): double state.** `Executor::set_initial`
is gone; the state for the next block is the Executor's. A second rule, `rewrite/double_state.hpp`: at `Normal`, a
feedback cycle over one voice whose nodes are cheap, whose type follows its operands, and whose past is all held in
registers (every lag one of a run 1, 2, ... of a value's), computes in double, with the cheap one-voice work carrying
one such cycle's value into another (a cascade's sections pass doubles: casts around each cycle alone were 1.6x slower
by hand and no more accurate). Nothing is converted up (a float operand beside a double one is read as double, a
constant at its double value); what leaves, a result or a later stage's lag, is converted down once, and the planner
runs that conversion in the cycle's stage. RBJ low-passes as users write them: 20 Hz -68.8 -> -144.9 dB, 1 kHz -112.1
-> -145.7 dB. A bank, a comb, an echo and a comb network stay float (lanes would halve, or delay lines double: echo375
2.4x, MAI_verb 1.44x slower when they did). Its conditions stand in for the latency model (P4): a single filter pays its
result's conversion each sample (`dsp/biquad` 1.04x whole), a division on the chain more (the Padé ladder 1.05x).

With it, a state kept in its registers stores nothing each sample (`Node::closes`): its buffer holds its past alone
(`Buffer::only_past`, no block, no carry), its registers open from it before the time loop and close into it after,
in place, in a run, a band's chunk or a head sample alike. Arenas shrink (`ladder_bank` 35 to 19 KB), and banks whose
inner states were stored each sample gain: `svf16` 0.62x, `ladder16` 0.64x; `biquad3` 0.85x, `dsp/cascade4` 0.94x of
their time before. Its limit: the feed-forward before a chain's first cycle stays float (it may run in lanes over
time), and where that section is a low-frequency high-pass, its float rounding before poles near 1 sets the error
(KFR's graphic EQ: 3.1e-4 against 8.4e-8 computed as a whole in double); the linear-filter rewrite (3.7) is the cure.

**P3 closed** (2026-10-09): the matrix green (`rw-clang-26` and `-23` 302 of 302, `rw-asan` and `-O2` 300 of 300), the
bench gate 44 of 44 against `main`. The third review (3.4 and 3.6) found no wrong number: random graphs, 3,036 built at
run time and about 50 at compile time (High bit for bit the walk, every block size the numbers of one run, voices
split among threads, `reset` byte for byte) and the Executor aliased in place. It found four smaller things, fixed:
`well_formed` let a Close stand before its time loop; a `_madd` built by hand (through `apply`) was accepted at
`High`, where its bits would follow the build (the analysis now refuses an operation rounded as the machine rounds
where its precision grants no reordering); two sentences of the architecture said more than the code keeps; two
comments were stale. A graph that compiles with a `Length` but is not on time, older than P3, went to the backlog.

**Amended (3.7, Gerald's Q4 yes of 2026-10-08, built as P4 step 4.3d, 2026-10-09): the parallel form.** A third
rule, first in `rewritten`: `rewrite/parallel_form.hpp`, with `rewrite/linear_sections.hpp` recognising a section. A
chain of second-order sections, each a feedback cycle linear in one signal (now, one and two samples back) and in its
own past (one and two back), every coefficient a constant, each reading the one before, is computed as the sum of its
partial fractions: a bank of the sections' own denominators over `[sections, time]` in double, numerators from the
residues of the chain's poles, summed over the sections, the direct term added, converted to the output's type. The
expansion is done at compile time in double (within 1.4e-13 of long double's). It fires at a precision that grants
reordering, over one voice, where nothing outside reads the chain but its end, where each section is its own cycle,
and where no two poles coincide. A section is read by the rule table's `Algebra` column, its input being the signal
its lags read, so `0.996 x` is a term of `x` (the first version took it for a signal of its own and left KFR's first
section out of the bank). The bank's feedback is held negated and added, so the feedback-sum rule fuses its newest
term. Declined: `High`, one section, repeated poles (a Linkwitz-Riley pair), a first-order section, a chain read in
its middle, voices, a map that is not linear between sections, and a chain inside an enclosing loop (`u = x - 0.6
clamp(y[n-1])`, `y` the chain of `u`). That last one would put the bank's sum on the outer cycle, feedback through
two shapes and a fold, which the lowering has no loop shape for. (Lifted by Plan/23 P10, Decision 71: a feedback stage
now folds its voices every sample, and the chain inside a loop becomes a bank on it.)

Accuracy: with the output in float, the program is exactly as far from long double as the cascade in double rounded to
float (3.1e-8 to 5.1e-8 on KFR's EQs, `dsp/cascade4`, an 8th-order Butterworth at 30 Hz). In a graph wholly in double,
an ill-conditioned filter loses a little: that Butterworth 7.2e-12 against the cascade's 1.1e-12, within what `Normal`
grants a reordering.

The planner bands a stream over a bank narrower than a lane where a run of known length outgrows the last-level cache
(`loops/planner/bands.hpp` `known_to_spill`). The veto on such bands was measured in the caches only (3-5 voice banks
2-7 % slower banded); past the last-level cache, KFR's EQ as a three-voice bank ran 4.35 ns a sample unbanded and 2.61
banded (`Plan/probes/audio-iir/bank_bands.cpp`). A `Length` run stays unbanded: in blocks the band costs it (1.88 ->
2.45). The bank's sum stays a stage of its own, which in the caches beats a sum inside the bank's stage (1.88 ns a
sample against the hand probe's 2.7).

Measured on zen2 against the tree before: `biquad8` 9.31 -> 3.64 ms (0.39x; KFR 3.33), `biquad3` 3.08 -> 2.77 ms
(0.90x; KFR 2.86), `biquad3dyn` 0.65-0.70x, `dsp/cascade4` 0.63x whole and 0.72-0.77x in blocks; klang's `eq` and
`dsp/biquad` unchanged. The bench gate against `main` 44 of 44. The matrix green (`rw-clang-26` and `-23` 304 of 304,
`rw-asan` and `-O2` 302 of 302), no golden, hash or zero-overhead pin moved, and the differential unchanged (no chain
of sections in its corpus). Stand-ins for P4's model: the form taken wherever it applies, and the stream band's height
(16 rows; the three-voice bank runs 1.9 from 64 rows on).

**Amended (3.7's review, 2026-10-09): the parallel form weighs what its fractions amplify.** The review found the
rule took ill-conditioned chains and their banks' accuracy collapsed (eight sections at one frequency with Qs
0.50-0.67: a low-pass at 50 Hz 9.9e-6 from long double, a high-pass at 20 Hz 2.4e-4, at 10 Hz 8e-4; four low-passes at
30.0-30.3 Hz 2.5e-5). The rule now declines *a chain whose partial fractions, each at its peak (|r| / (1 - |p|)) and
the direct term, sum to more than 150 times the chain's own peak gain* (the cascade's |H| at the frequencies its poles
ring at, 0 and Nyquist). One numbers guard, no operation named: the bank rounds relative to its terms, the cascade about
relative to its result, so the ratio is about the factor by which the bank's rounding exceeds the cascade's. The
threshold is the data's (`Plan/probes/audio-iir/parallel_conditioning.cpp`, fed noise and a chirp): the sweep's
filters amplify 1.8-120, the benches' 3.98-10.4 (KFR's EQs 3.99 and 10.4, `dsp/cascade4` 3.98, klang's 6.86), the
review's cases 1.8e7-6.4e7; with the output in float the bank keeps the cascade's accuracy (1.00-1.04 times its error)
up to 128, 1.04-1.23 for high-passes of order 10-12 at 5-30 Hz (137-370), 1.97 at 543, up to 16 from 1.5e3; 150 lies
in a gap of the data. Butterworth designs fold up to order 10 (but at 10 Hz, 326-343), not from order 12 (191 and up).
The review's low-pass at 50 Hz is the cascade again: 5.6e-8 in float (the double cascade 3.5e-8), 2.3e-13 in double.

Three more faults, fixed with it: a small or huge coefficient failed the compile (a NaN under constant evaluation:
a2 = 1e-160, `a1 * a1` past 1e154), so every number the expansion goes on with now stays below 1e100, a quotient is
taken only where it does, the poles must lie inside the unit circle and off its centre, and a section's linear form
refuses a coefficient past 1e100; the recognition walked paths, not nodes (`t = t + t` 16 times, then a section: 104
s), so a linear step is opened only where one node reads it (as the feedback-sum rule's `take`), with the cheap
conditions first: 12/16/20 doublings 1.45/1.51/1.55 s against 1.36/1.40/1.44 before 3.7, the difference constant (the
two headers and the pass); a value pinned `High` inside a section was rewritten, so every node a section consumes must
grant reordering or it is no section, and its chain splits around it.

And a chain whose result is double stays a cascade: its result must be narrower than the type the bank computes in
(`kBankType`). Wholly in double the bank's error follows its sections' noise gains more than the amplification (in a
cascade a later section filters an earlier one's rounding, in a bank nothing does), and broke the rule's own oracle,
twice the double cascade's error: the sweep's 8th-order Butterworth high-pass at 30 Hz 15 times fed noise and 148
fed a chirp, high-passes at 2-5 Hz 60-400 times. Only a rounding to a narrower type hides the bank's. That high-pass
in double is now 9.5e-13 from long double (the double cascade 1.4e-12); over float it still folds, and so do the
benches' filters, all float. This overrides 3.7's amendment above, where a filter wholly in double "loses a little"
within what `Normal` grants a reordering.

**Amended (Plan/23 P4 4.3a, 2026-10-09): where the conversion down runs is the cost model's call.** 3.4 ran it in the
stage making what it converts, a rule standing in for the model. Now it is work after the cycle like any other
(`planner/trail.hpp`): it joins the stage as far as the model prices that cheapest, against it and what follows it in
lanes; a single filter's conversion still joins, four issue-bound sections with at most one map after them leave it
in lanes (within 3 %), and on machines of 64-byte vectors a lone biquad's leaves for lanes of eight doubles.

**Amended (Plan/23 P4 4.3b, 2026-10-09): double state by the cost model.** A cycle at `Normal` computes in double where
the stage it runs in costs no more so by the cost model, within its resolution (3 %, the corpus's bar: a tie widens,
double being the more accurate): each cycle alone first, its stage priced from the graph (`graph::cycle_stages`: its
chain in its type's latencies, its voices in lane bodies, a comb's samples at once as its nearest past allows,
`graph::nearest_pasts`, a conversion's slot for each float it reads and each value converted down), then the cycles
the region links as one set, while the graph's feedback stages, gathered as the planner gathers them (all merged or
each apart, the cheaper), stay within 3 % of their cost in float. `Machine` gains `core_double` (110 / 100: the exact
double series, 70-181 cycles on a chain). Now in double: a comb damped by a one-sample state (JUCE's `delay` 0.95x), a
comb whose samples fill double's lanes and not float's, and the one-voice part of a loop around a bank (`eq8fb`: its
conversions leave the loop's chain, 0.85-0.88x, 8.0 ms whole and 7.5 ns a sample in blocks); still float: a core on
the chain, combs merged into a stage their conversions load (Freeverb's eight 1.43x slower in double), a comb in lanes.
One voice stays a condition for what widens, a stand-in: a voice bank in double costs what the model does not see (its
lanes masked to its voices; JUCE's EQ over 2 and 4 channels 1.12-1.18x slower, priced alike). The accuracy contract, as
read: Normal promises the float accuracy of the libraries it is compared against, and 3.4 added, for its class (one
voice, cheap nodes, the past in registers), double "at no cost" (Q1 (b), the accuracy). The model may add cycles where
double is free; it does not take the promise away, which would trade the accuracy Q1 chose for a few percent below the
model's resolution (a declined 16th-order low-pass at 50 Hz priced 4-9 % dearer in double, its error 3.5e-8 in double
and 2.2e-4 in float). So where the model prices the class dearer — a division on the chain (1.14x, the Padé ladder's
1.05x), bands of one input summed (1.16-1.22x), six sections (1.03x) — it keeps double; dropping that floor is Gerald's
call. The form inside a loop follows (Decision 71): a bank from four sections. `Plan/probes/audio-iir/cost_corpus17.cpp`.

## Decision 67 — a run of lags handed on in registers, and the feedback sum's ready order corrected (`Plan/23-audio-scheduler/08`)

**Decided** (2026-10-08, Gerald: "keep `y[n-2]` in registers … as many things in the registers as possible … clean,
not special solutions for biquad filters"). Closes Decision 66's first open item.

**The rule**: a run of lags 1, 2, …, d of one stored value, each read before the statement storing it, is handed on
in d registers: the lag of k samples reads slot k − 1, and the storing statement moves the slots on before it writes
slot 0. A lag past a gap (a comb's `y[n-1000]` alone, a `y[n-4]` beside `y[n-1]` and `y[n-2]`) stays in memory, so
every slot has a reader that opens it from the stored past at the start of a run: streaming and seeds are unchanged.
How deep is the planner's (`Schedule::Stage::state_depth`, from the review below); merging (Decision 65) counts a
register for each lagged value, at its maker, so that two nodes lagging one value count once. IR: `Node::slot` (a reader's and its Open's), `Node::slots` (a State's depth); both
print only where they differ from one sample, so no listing without a run moved. The runner's `State` holds
`was[slot][body]`.

**The feedback sum rule, three corrections** (each found by measuring, each probed by hand,
`Plan/probes/cascade-order`): with `y[n-2]` in a register the rule reached the cascades and lost (`dsp/cascade4`
1.11× slower) because it misjudged when terms are ready. (1) It opens the part of a sum that reads nothing of its
cycle (`c0 s + c1 s[n-1]`, a node of no cycle that the sum alone reads): the sum as written. (2) What any cycle makes
this sample, or reads of it through cheap work (as the planner chains cycles), is made this sample: in a stage holding
both, the previous section's output arrives last, so it is added last and fused (four sections by hand: as written
3.60, the old order 3.90, the new 2.95 ns a sample). Behind an expensive map the cycles are stored apart, and the
value is ready at once. (3) A product or a negation is ready with its later factor; anything else a cycle computes
this sample (a sum, a `tanh`) is made this sample: a TPT filter's `bp = g hp + s1[n-1]` fuses `g hp` (two stages by
hand 5.33 → 5.04; the second stage alone fused, 5.50). The rule now declines only a closing lag past a gap.

**Measured** (zen2, before P8 → after, the same box, alternated): `dsp/biquad` 2.40 → 1.58 ms (0.66×, its hand loop
2.25), `dsp/cascade4` 3.83 → 3.37 (0.88×, hand 3.72), each block size alike; `biquad8` 14.5 → 11.0 ms (0.76×; KFR
3.35), 9.9 (0.68×) with the review's state depth, `biquad3` 0.95–0.97×, `biquad3dyn` 0.95×, klang's `eq` 0.72×, JUCE's `svf` 0.92× and `linkwitz` 0.97×; every
other comparison row the same program or within its noise (rows whose program did not change moved up to 6 % with
the binary's layout). Accuracy against the same filters in double (`rewrite_feedback_sums_test`): rewritten and fused
0.36–0.76× the written error, the bench cascade 0.76× and 0.66× (was 1.24× and 1.32×). W3's `Executor` stays 36
instructions over its bare run; the bare run is 591 instructions for 64 samples (was 494, its hand loop 560): the
slot moves and the reordered sum, cheaper in time.

**The review** found no wrong number, and two faults, fixed. Banks of 24–32 voices spilled: two sections' slots
times four lane bodies filled all 16 registers (32 voices of four biquads 17.5 → 18.5–20.5 ns a sample). Probed
(`Plan/probes/state-depth`): a second slot pays only in a light stage, where `y[n-2]` from memory would be on the
chain; in a heavy one the load has slack and the register is wanted (eight mono sections 9.15 → 7.62 with one slot,
32 voices of four 18.5 → 15.6). So the planner decides how deep each feedback stage keeps a value's past
(`Schedule::Stage::state_depth`): the largest depth at which all of its state, times its lane bodies, fits in half the
registers, the rest for the sample's working set; at least the newest sample. Merging counts one register a lagged
value, at its maker. And `well_formed` now refuses a state reader after its keeper, or of another buffer.

**Open**: the biquad cascades still trail KFR (`biquad3` 1.27×, `biquad8` 2.94×): KFR runs the sections in lanes,
P5's job. How much working set the merge bound should leave (24 voices of four: 2 + 2 sections a stage ran 11.8 ns a
sample, the bound's 3 + 1 12.7) is the latency model's question (P4).

## Decision 68 — one cost model for feedback stages (`Plan/23-audio-scheduler/04`)

**Decided** (2026-10-08 to 10, Plan/23 P4, under Gerald's north star of 2026-10-08, "a general scheduling algorithm, not
a micro-solution per case"; Q2: `Machine` gains fields). What the planner chose about stages that run a sample at a time
were rules with measured thresholds, each a guess at one number of what a recurrence costs (`main`'s cache model needed
fitted constants and still picked wrong). One model now prices those choices from the processor's figures, and the rules
standing in for it moved onto it one at a time, each only where a corpus measured both ways said the model picks at
least as well; where it did not, the rule stays, named a stand-in. A model that is wrong is not better than a rule that
is measured.

**The model** (`schedule/cost.hpp`: a few `constexpr` functions over the rule table and a `Machine`, which the planner
and the rewrite layer ask alike, each counting its own chain and slots from the graph). A stage costs a sample, all its
voices, the most of its *chain* (the longest path, in latencies, from a state read to the state it becomes, over the
lag; `loops/graph/cycle_chains.hpp`) and its *issue* (its slots times its lane bodies, over `fp_per_cycle`), taken
together as their p-norm (`cost::busy`, p = `contention`: where one dwarfs the other, about it; where they meet, more
than either, since the chain's operations wait behind the work issued beside them); its *loads* and *stores* over what
the machine issues of each a cycle; and the *out-of-order window*: one sample's own path (`depth`) overlaps the next
samples' only as far as the window holds their slots, so a stage run a sample at a time costs at least `depth · slots /
window` (what `kChainDepth`'s ten maps measured). A stage in lanes along time runs `lanes` samples at once, its chain 0;
and every stage costs at least the bytes it moves through memory past the last-level cache over `memory_bytes`, where a
run stored whole outgrows the cache. A value handed between stages costs a store and a load per element and sample; a
state read from memory waits on the store-to-load forwarding on its chain; a fold of a stage's voices in its sample
costs its bodies − 1 adds and log2(lanes) steps of a shuffle and an add on the sample's path (`cost::lanes_folded`); a
stage of its own in a stream band pays its start each chunk. An operation costs its rule-table class (the new column
`Latency`: add-like, conversions among them, multiply-add, divide, root, a load, a core; the type matters for divide,
root and a core), never its name.

**`Machine`'s fields** (zen2's are the defaults; `m1_pro` carries them until P6 measures its own), as latency / issue
slots at two a cycle:
- `add` 3 / 1, `multiply_add` 5 / 1, `divide` 10 / 7 and `divide_double` 13 / 10, `root` 14 / 11 and `root_double` 20 /
  17, `core` 70 / 60 and `core_double` 110 / 100, `fp_per_cycle` 2, `loads_per_cycle` 2, `stores_per_cycle` 1:
  `Plan/probes/audio-iir/latency.cpp`, a dependent chain then eight independent ones, float and double, scalar and
  lanes, on core 4 (add, multiply 3 / 0.5 cycles in either type, multiply-add 5 / 0.5, divide 10 / 3.5 and 13 / 5, root
  14 / 5.5 and 20 / 8.5, a conversion pair 6 / 2, the float cores 46 to 91 on a chain, the double cores' exact series
  70-181 on a chain and 27-131 of issue; loads 1.87 and stores 1.00 a cycle);
- `forwarded` 8, a store-to-load forwarding on a chain (corpus slice 3); `window` 100, zen2's floating-point scheduler
  and queue (36 + 64, its data sheet), which slice 1 confirmed;
- `shuffle` 3 / 1: a fold's step less its add, 6 cycles a step for four doubles and eight floats alike (`latency.cpp`);
- `contention` 8: probed by hand (`contention.cpp`: beside a recurrence an operation waiting on the chain's value delays
  it about a quarter cycle, one waiting on nothing costs nothing until the issue bound), its p fitted on the corpus (75
  of 81 points right, 72 without it; 6 and 7 alike, 8 moves the fewest programs);
- `stage_start` 6: a one-pole's maps in a stage of their own, banded against whole, 5-6 cycles a chunk
  (`stage_start.cpp`); `memory_bytes` 4: maps over 64 MB buffers against the same in L1, 4.1 bytes a cycle fitted (loads
  alone 6.2, stores alone 4.4; `memory.cpp`).

**The corpus** (`Plan/probes/audio-iir/cost_corpus*.cpp`, slices 1-18: each point a graph measured under each candidate,
cycles a sample, pinned, built one translation unit at a time, one block of 256 in cache unless named; a pick is right
within 3 % of the best, the bar 90 % of the points). Slices 1-3 (maps or a `tanh` between two biquads, double state, the
state depth): 17 of 18. Slices 1-10, 1-7 rerun against the trees they were measured with (the feed, a cascade whole or
split, allpasses and one-poles, maps after and beside 1-4 sections, a bank's sum, eq8mix's `u`): 72 of 81 without the
contention term, 75 with it, 76 with the memory term. Then a slice a step: the trailing decision 39 of 40 (12; 3.4's
rule 24), a fold's stage after its fold 7 of 8 (13; all joined 3), independent recurrences merged 0.30-0.71× each time
(14), a bank's sum in its sample (15), store or compute where read 11 of 12 (16), double state 10 of 10 where the model
alone decides (17), the state depth and the merge bound (18, below); 11 the cascades the contention term moved. The
misses that stand: three taps beside two biquads at B256 (merged 10.0, the pick 10.4: a stage's totals do not say the
feed waits on no chain); a biquad in double, a tie it widens by design (6.1 against 5.8); two maps after three sections
(12.0 against 11.4 joined); the sum after a bank of two or three voices (joined 8.6 against 6.9; the stand-in below); a
float `exp` after `biquad8`'s bank, 5 % faster in the sum's stage, priced alike. Every point is in `04` with both times;
the model is off in magnitude more often than in pick (a core's window occupancy, the per-run cost, a 32-voice bank's
spills).

**What the model decides** (bench rows against the tree before each step, alternated, three rounds of three):
- which cycles share a feedback stage, with the cheap work leading into them (4.3a, 4.3g): `kChainDepth` gone, a map
  leading into its nearest cycle, a cycle joining the open stage where that costs no more than closing it, whether it
  reads the stage or hands it nothing (`planner/chains.hpp` `chains_on`; `unit_related` gone). An eight-section cascade
  as written splits (38.7 → 29.5 cycles a sample); `apchain` 0.65×, `maiverb` 0.90×, `reverb` 0.84-0.85×, then
  0.46-0.47×, klang's `eq` 0.79×;
- the feed before the first cycle, weighed whole as the stage closes (`as_it_closes`; slice 6, 8 of 8): P2's rule (a map
  of the feed alone keeps its lanes) gone; `bankmix16` 0.51×, `envfollow` 0.87×, klang's `eq` 0.92×, `dsp/biquad`
  0.91-0.96×;
- **the trailing decision** (4.3a): *what follows a feedback stage's last cycle joins it as far as the model prices that
  cheapest, the stage with the first k of that work against it and the rest in lanes handed what crosses*
  (`planner/trail.hpp`, every cut priced in one pass, a tie the most). 3.4's conversion rule is gone, a conversion being
  work after the cycle like any other; `allpass` 0.24×, `maiverb` 0.87×, `delay` 0.95×; `ladder` 1.04×, its run's
  working set now exactly the L3, which the band rule's "more than" no longer bands (banded 0.97×; the band threshold's,
  for Gerald);
- what joins a fold's stage after its fold (4.3e; all of it did): the trailing decision for a stage folding a signal's
  voices, priced as it runs, with its start: `biquad8` 0.91×, `eq8mix` 0.86× whole, `eq8sweep` with its phase an input
  40.4 → 14.9 ns a sample; and from a lane's worth of voices a bank's sum in the bank's sample (4.3g, `trail.hpp`
  `fold_joins`): `eq8sweep` 10.45 / 10.39 ns a sample whole / in blocks (KFR 11.66 / 11.28), `bankmix16` 0.53×;
- store or compute where read (4.3f, `planner/in_the_reader.hpp`): a map that one later stage of maps alone reads runs
  there where the two stages cost less so: `eq8mix` 0.877× whole (3.09 ns a sample, KFR 3.26-3.30), `eq8sweep` 0.954× in
  blocks;
- double state (4.3b, `rewrite/double_state.hpp` over `graph::cycle_stages` and `graph::nearest_pasts`): Decision 66's
  conditions "cheap nodes" and "the past in registers" are the model's now, a cycle widening where its stage, gathered
  as the planner gathers cycles, costs no more in double within 3 % (a tie widens): `eq8fb` 0.85-0.88×, JUCE's `delay`
  0.95×, a `tanh` on the chain float (2.55× in double). **Normal's promise is a floor**: where the model prices Decision
  66's class dearer in double (a division on the chain 1.14×, bands of one input summed 1.16-1.22×, six sections 1.03×)
  it keeps double, an accuracy contract and not a cost rule; dropping it is Gerald's call;
- inside an enclosing loop, the parallel form where the loop's chain does not grow (Decision 71, amended).

**Stand-ins that stay**, named in their comments and in `00-Overview.md`'s North star, each where the model measured
worse or cannot see what decides:
- the parallel form outside a loop, taken wherever it applies (faster on every set probed, so no rule asks);
- a fold after a bank narrower than a lane stays apart (`fold_joins`): the model prices it joined, which runs 1.25-1.3×
  (slices 9 and 15); a stage that folds its voices every sample takes its cycle and joins nothing;
- the state depth's half of the registers (`planner/chains.hpp`): within 3 % at 16 of 19 depth choices, a spill term at
  15 (slice 18). At 12-16 registers of state the compiler's allocation decides: JUCE's EQ over 16 channels and each half
  of 16 voices of six hold as many, and a second slot makes the one 0.90×, the other 1.05-1.07×; where a program lands
  moves it by up to half;
- the merge bound (`still_fits`: the work a sample within `kMaxGroupStmts`, the handed-on states within the registers):
  the model alone merges wide banks 1.12-1.14× past where the work bound cuts them. The bound reads the capacity knob,
  so a raised `HEINZEL_MAX_STAGE_STMTS` plans wide banks worse (for Gerald: the bound as a `Machine` figure);
- the window thresholds `recompute_always` and `recompute_if_uncached` (`planner/inline.hpp`): their stored buffers live
  in L2 and L3, where the model has no figure;
- eq8mix's `u` stored once: a value several stages read is not made again in each, which needs a graph rewrite (the
  lowering keeps one stage a value), and copies made by hand were gathered into one stage and stored anyway (14.1-14.7
  cycles a sample whole against 13.0-13.6);
- one voice for what widens to double: a bank in double, its lanes masked to its voices, costs what the model does not
  see (JUCE's EQ over 2 and 4 channels 1.12-1.18×, priced alike);
- and smaller ones: the 3 % tie, a stream band's height (16 rows), the stages a graph's cycles gather into priced by
  double state only all merged or each apart.

**At the close** (zen2, core 4): the bench gate CLEAN, 44 of 44 against `main` (`blur` 0.87-0.88×, `ladder_bank` 0.78×);
of the differential's 15,730 programs 366 moved, all feedback stages as P4 changed them (`fm_oscillator`, a lag of two
fed back, `ladder_bank`); of `measure.sh` only W9 (`voice`: bare 6675 → 6875 B, its arena 88,424 → 94,568 B, 98,689 →
98,516 instructions a run, the `Executor`'s overhead over the bare run the same 3 instructions).

**The review** found two bugs, each fixed with its repro as a regression test. *A join broke the sample it joined*: a
bank's sum in the bank's sample lays it out in four lane bodies or none, and a double joining after it (a gain after the
sum, a damped comb chaining on, a one-pole widened at `Normal`) halved the lanes, so the planner refused its own stage
(on zen2 17-32 voices, on 16-byte vectors 9-16), and at `Normal` `compile` planned the graph as written, without any
rewrite; before P4 all compiled. Now the planner asks its own question, `graph::fits_a_block`, of every way into a stage
that folds its voices, at the lanes after the join (`sample_fits`: a cycle chaining on, the work after its cycles, its
feed), and `fold_joins` at the lanes with the fold; the refusal says what it is ("a sample folding its voices takes a
literal count, four lane bodies at most"). *Normal's promise was dropped near capacity*: double state widens every
candidate to price them, and where that outgrew `kMaxValues` or `kMaxNodes` it returned the graph as written, promised
cycles too; there the promised cycles now widen alone, as before 4.3b. Two small points: a dead branch of `fold_joins`
went, and a feed folded into a stage adds its raw counts (its priced ones took a once-a-sample adjustment twice in a
figure no feedback stage reads). The review's fuzz after (five machines, three precisions, float and double inputs,
32,000 seeds): of 284 refusals and fallbacks where the tree before P4 compiled none left, no program ill-formed, 4
moved, each now holding Normal's promise. `measure.sh`'s binaries and every `rw-bench` binary are byte for byte those
before the fixes; the matrix green on them (`rw-clang-26` and `-23` 306 of 306, `rw-asan` and `-O2` 304 of 304, nothing
"did not run").

**Amended (2026-10-10, Gerald's yes): the merge bound is a `Machine` figure.** The work bound (`still_fits`: a feedback
stage's work a sample, nodes times lane bodies) was `kMaxGroupStmts`, three quarters of a stage's statements: a capacity
read as a cost, so a build that raised `HEINZEL_MAX_STAGE_STMTS` (the comparison targets' 256, for `eq8fb`) merged wide
banks further, into stages 1.12-1.14x slower (slice 18). It is now `Machine::merge_work`, 96 on zen2 (the corpus's
points), and the capacity only caps it: `still_fits` asks the work within `merge_work` and the nodes within
`kMaxGroupStmts` apart, and a symbolic count of voices counts as `merge_work` voices. The other readers of
`kMaxGroupStmts` count a stage's nodes and stay. Nothing moved at the default capacity (the differential, `measure.sh`'s
binaries and every `rw-bench` binary, the comparison targets' among them, byte for byte); at 256 wide banks plan as at
128 (`merge_bound_test`, built at both). Still a stand-in for the model, now named where it belongs.

**Amended (2026-10-10, Gerald's yes): the band threshold** is 15/16 of the L3 (Decision 47, amended), where a band
starts to pay when the working set alone decides; `ladder` is banded again, 0.96-0.98x.

## Decision 69 — sections in lanes: a cascade of like sections run skewed in SIMD lanes (P5 of `Plan/23-audio-scheduler/05`)

**Decided** (Gerald, 2026-10-24). An EQ whose coefficients are Params ran as a serial cascade, 3.6× the same EQ with
constants and 2.2× KFR's (`biquad8/heinzel-param`, 9.6 ns a sample at B256 on zen2). Of the two ways measured, the
parallel form's bank worked out at run time was "way too complicated for a tiny gain": with a knob moving every block it
ran 3.7× slower than the lanes, it needs a fallback where two bands come close, and while a knob moves it is not the
cascade. So a maximal run of like feedback sections, each reading the one before at the same sample, becomes one cycle
over a section axis, linked by an internal `_chain`, and a skewed stage runs it in lanes, section j a sample behind
section j − 1: KFR's method, but with the lanes held at a run's edges, so each run computes exactly its own block and
the numbers are the cascade's. It is taken where the cost model prices it below the cascade by more than `kTie` ("a
cost model would be preferable" to a fixed number of sections), constant chains the parallel form does not take
included. This builds P5 of Plan 23, which had stopped at its go/no-go on constant coefficients (2026-10-09: LR4 slower
in lanes, LR8 7–9 % faster). `biquad8/heinzel-param` B256 9.61 → 3.33 ns a sample (KFR 4.26). Details:
`docs/sections-in-lanes.md`.

## Decision 71 — a feedback stage whose sample folds its voices (`Plan/23-audio-scheduler/10`; 68–70 are reserved by P4, P5, P6)

**Decided** (2026-10-09, Plan/23 P10, the orchestrating session while Gerald was away: decided, not asked; no DSL or
API change, no dependency). `eq8fb` (KFR's EQ in a loop: `u = x − 0.6 clamp(y[n−1])`, `y` the EQ of `u`) only tied
the hand loop, because the parallel form (Decision 66) was declined inside an enclosing loop: its bank's sum would lie
on the loop, a fold across a feedback stage's lanes inside its sample, which no loop shape ran.

**The shape**: one time loop, a sample at a time, each sample in three parts — before the fold, at width one (work over
the fold's result: the clip, `u`); the lanes, the stage's voices in lane bodies as a voice bank runs them, each body's
value its accumulator (the fold opens at its first element); after the fold, at width one (the fold finished: bodies
added, then across the lane; `+ D u`, the conversion, `y`). A value made before is read in the lanes as a splat; a lane
value reaches after only through the fold. State stays in registers of its part's width: a body each for the lanes, one
scalar for one voice (`y[n−1]`, `u[n−1]`). **The program gains no field**: the fold's Registers stand right below the
time loop, inside one whole chunk of the voices' strip (`well_formed_restarts.hpp`), so they restart every sample; a
State with no link is a one-voice state; a Finish may keep (and close) the fold's value. The runner changed in `finish`
alone: where the strip stands outside the fold's Registers it adds no rest, takes the identity for a masked tail's
missing lanes, and a strict fold (`High`) adds every real element in element order, from the first, in double, as the
reference walk does. Every other program compiles to the same bytes (`measure.sh`'s binaries, the differential's
15,730 programs).

**Where a fold runs, one rule**: a fold over a feedback stage's voices runs in that stage's sample wherever the cost
model prices it cheaper there than in a stage of its own with the folded values handed on; where its result feeds the
cycle back there is no stage of its own, so it always does. Built: the second case (`eq8fb`: one candidate, nothing
priced). The first (a fold after a lone bank: `biquad8`, `eq8mix`, `eq8sweep`'s bank) is P4's trailing decision; until
the model has its contention term, a fold after a lone bank stays apart.

**Who decides what.** The graph facts admit a cycle holding one fold (not over time) between nodes over its operand and
over its result, where nothing over the operand reads the fold's result in its own sample; anything else keeps the old
refusal ("feedback through anything but maps of one shape"), two folds in one loop among it. The planner makes such a
cycle one stage over the fold's operand, laid out before, lanes, after (a stable partition after the lags move before
their makers), with today's voice layout (voices in lanes, time innermost) where the voices are a literal count within
four lane bodies, and refuses it by name otherwise (its cycle handing no state of one sample back on, no lanes, or the
voices' count), so that `compile` plans the graph as written; no split along an axis a stage's fold collapses. The
lowering builds the tree from the schedule (`normalize.hpp` `sample_tree`), each statement's part read off the
planner's order, and refuses by name voices not in one chunk of their strip's lanes, a second fold, a node over the
fold's result among the lanes, a lane reading the fold or the part after it, time not innermost and time cut into
chunks outside the sample. The parallel form now fires inside a loop (`own_cycles` gone) where the loop's chain does
not grow and a sample holds the bank (amended below); a chain whose result is double still stays a cascade.

**Measured** (zen2, core 4, the binaries before against after, alternated, three rounds of three, medians):
`eq8fb/heinzel` 11.82 → 9.38 ns a sample (0.79×), `/B256` 11.65 → 9.40 (0.81×), in the KFR and the JUCE targets alike
(JUCE's filters 28.9; KFR has no arm: its engine cannot close a loop); the hand probe's ceiling 8.3, the rest about the
two conversions on the chain (`u` up, `y` down), P4 4.3b's to take. No other whole-picture program moved; the bench gate
against `main` 44 of 44; the matrix green (`rw-clang-26` and `-23` 306 of 306, `rw-asan` and `-O2` 304 of 304), `measure.sh`'s
binaries byte for byte the same through every step. Accuracy: the rewritten loop walked is 6.2e-8 from the loop in long double (the float cascade as
written 3.4e-4); the program 1.1e-7 from its walk.

**Stand-ins handed to P4** (named in `00-Overview.md`'s North star list): the parallel form taken wherever it applies —
inside a loop it pays from six sections on and costs 1.2–1.8× below (two sections 4.1 → 7.4 ns, three 5.5 → 8.9,
`Plan/probes/audio-iir/fold_in_a_loop.cpp`), the bank's chain being about fixed and the cascade's one multiply-add a
section (inside a loop no longer a stand-in: amended below); such a stage takes its cycle and nothing else, and joins
nothing (`chains_on`, `fold_in_the_feed` and the conversion rule skip it); a fold after a lone bank kept apart. And a
corpus point: `eq8sweep` with {its LFO's phase, the bank, its sum} in one such stage, by an edited schedule through
the real lowering, 12.5 → 10.5 ns a sample in blocks of 256 (`sweep_one_sample.cpp`), the outputs the same bits: two
sample-at-a-time stages that hand each other nothing sharing one (P4 4.3g).

**The review** found no wrong number, and four faults, fixed: a fold that keeps its axis in a loop did not compile on a
machine of many cores (the split pass offered the voices; it now skips every axis a fold collapses); a lane reading the
fold it feeds, and time cut into chunks outside the sample, were lowered and then refused by `well_formed` (both now
refused by name in the lowering); the planner's refusal named one cause for three.

**Amended (2026-10-09, `10` 10.7, Gerald away: decided, not asked): the form inside a loop by the model.** Inside a
loop the loop's period is its chain, so one principle replaces the stand-in there: *a rewrite that changes a value on
a feedback cycle is taken only where the cycle's chain, priced in latencies, does not grow* (`rewrite/priced.hpp`).
The chain is the cost model's measure (P4 4.3a: a cycle's longest path, in latencies, from a lag of a value it makes to
its maker, over the lag), moved from the planner into the graph facts (`loops/graph/cycle_chains.hpp`) so that the
planner and the rewrite layer, which may not read the planner, measure alike; it now prices a fold in a cycle (its lane
bodies added, then log2(lanes) steps of a shuffle and an add; `Machine::shuffle`, 3 cycles on zen2, measured as a fold
step of 6 in `latency.cpp`) and an operand converted to another type (the conversion row's class). Each graph is
priced as the planner will be given it (the rules after the form applied), and the loop a rewrite changes may not
grow, cycle by cycle: every chain inside a loop is decided once, all banks built into one graph and priced together
against the graph before (the k-th cycle after is the k-th before; where they cannot be priced together, each alone),
so the rule's compile time stays within 1.3× of the one before it (the second review's probe; it grew with chains
times passes when each chain was rebuilt and priced in every pass). On zen2 the cascade's chain is 11 + 5 a section
cycles and the bank's 37 in one lane body, 40 in two: a cascade at 2–5 sections, a bank from 6 — each the faster form
measured (at six the two within 4 %). Measured after (`fold_in_a_loop.cpp`, B256): 2–5 sections 4.2, 5.5–5.7, 6.9–7.0,
7.8–8.0 ns a sample (their cascades), 6 and 8 their banks as before; `eq8fb` and every other whole-picture program the
same bytes; the differential 0 of 15,730 moved. The planner reads the new prices for no decision: a stage that folds
each sample joins nothing, and since the second review neither does the cheap work leading into it (`chains_on` asked
only a node's own cycle, so maps leading into a folding cycle joined an earlier feedback stage, priced by that cycle's
chain, and gave up their lanes: a one-pole and 13 to 17 maps 1.25–1.39× slower after 10.7, 12 maps 1.2× since 10.3;
now 10.3–10.7 ns a sample at every count, `fold_in_feedback_test` pins 15). Outside a loop the form is still taken wherever it applies, the stand-in. And the
bank must fit the sample the planner lays out (the review's finding: where the planner refused it, `compile` planned
the graph as written and lost every other rewrite — nine sections in the loop on `m1_pro`, eight with no lanes): a fold
whose voices no sample holds (not a literal count, no lanes, more than `graph::kMostSampleBodies`, the planner's four
lane bodies) has no end to its chain, so such a bank never pays and the cascade stays, planned as rewritten.

**Amended (Plan/23 P4 4.3b, 2026-10-09): the form inside a loop after double state.** The loop's one-voice part (its
clip and `u`) now computes in double with the bank (Decision 66, 4.3b), so no conversion lies on the loop's chain: 31
cycles in one lane body, 34 in two (37 and 40 before). A bank from four sections, the chains tied there: as a bank
2 sections 1.39x the cascade's time, 3 1.34-1.39x, 4 0.99-1.05x, 5 1.06x (a miss: priced by its chain alone, its
second body of one lane costs more), 6 0.82x, 8 0.67x (`cost_corpus17.cpp`).

## Decision 72 — measuring the machine at configure time: `heinzel::host` (`Plan/24-machine-tuning`; 69 and 70 are reserved by P5, P6)

**Decided** (2026-10-10, Gerald; Plan 24). A `Machine` (`schedule/machine.hpp`) is the facts about a processor that the
planner reads, and `zen2`'s were measured by hand, probe by probe (Plan/23 P4, P6). `main` had an optional step that
measured its target while CMake configured (`HEINZEL_AUTOTUNE`, July 2026); this tree now has one for `Machine`.

**What `main` did, and what went wrong.** One program (`tools/calibrate.cpp`) timed the cost model's rates, the best of
five runs; the numbers became cache variables and a header whose target became the *default* of `compile` and
`executor`, so the tests needed a switch (`HEINZEL_NO_AUTOTUNE`) to undo it, and every benchmark directory measured at
its first configure whatever the option said. Three things went wrong: a benchmark directory configured on a busy box
kept a speed of 28 instead of 132, and a ladder row that moved 3x was a decision flipped by drifting measurements, not
by code (`9fd9a446`); on the M1 the compiler removed the copy the probe timed, so the speed came out infinite,
overflowed and broke the benchmark build (`048187a8`); a 16 % drop blamed on the measured numbers was layout luck, and
on zen2 they moved no decision at all (`d55ce5f3`). What worked: off by default, measured once per build directory, a
failed measurement falling back to a default field by field.

**Gerald's answers** (`00-Overview.md`): (1) `main`'s option name, `HEINZEL_AUTOTUNE`; the header
`heinzel/host_machine.hpp`, the machine `heinzel::host`; (2) off by default; (3) the default machine does not change:
`Executor<g>` plans for `default_machine` in every build directory and code names `host` where it wants it, so the same
source makes the same program everywhere unless it says `host` (at `Normal` a machine's lanes and fused multiply-adds
order sums), and the tests need nothing; (4) a reading that fails or is implausible keeps its field's default, with a
warning; (5) cross-compiling with the option on stops the configure (a probe under an emulator measures the emulator);
(6) one mode, minutes once per build directory; (7) built before the Mac run, so that run is "configure with tuning
on"; (8) on this box every tuned configure passes `-DHEINZEL_AUTOTUNE_WORKERS=4` (`CLAUDE.md`'s load limits); (9) the
cores' rule rounds to the nearest ten, not up (a tenth of a cycle decided "up"); (10) `band_bytes` takes the fastest
budget, not the smallest within the noise; (11) `relay_pays_after` stays 12 and is not taken from its probe (below).

**What is measured** (`tools/tune/machine.cmake`; each field's source, rule and range in `01-Fields.md`):
- *facts*, in under a second: `vector_bytes`, `registers` and `fma` from the compiler's macros under the build's flags
  (`CMAKE_CXX_FLAGS` and its build type's; a target named in `CMAKE_CXX_COMPILER_TARGET` or `CMAKE_OSX_ARCHITECTURES` is
  not followed; what the code is compiled for; the gate `no_instruction_set_macros` forbids asking in `include/` only);
  the caches, line, page and cores from sysfs, `getconf` or `sysctl` (one CCX's L3 on Zen; an Apple processor reports no
  L3, so `l3_bytes` = `l2_bytes`, `main`'s rule, until P6 decides whether the system-level cache counts; an Intel Mac's
  `hw.l3cachesize` is read);
- *stage 1*: `latency.cpp` (the `OpCost`s, `fp_per_cycle`, loads and stores, `forwarded`, `shuffle`, the cores) and
  `memory.cpp` (`memory_bytes`), written as `host`;
- *stage 2*, the threshold probes planned for stage 1's `host` (`-DHEINZEL_BENCH_MACHINE=host`): `recompute`,
  `band_bytes`, `stream_rows` and `turn`, and `parallel_work` where threads are on and the workers are every core;
- *defaults by design*, each named in the header with its reason: `window`, `contention`, `stage_start` and
  `merge_work` (no probe decides them in seconds: a data sheet, corpus fits, a hand-cut schedule), `band_when_over`
  (its probe takes 11.5 minutes, twice the tuned set; the 15/16-of-the-L3 rule follows the measured L3),
  `relay_pays_after` (answer 11: `relay` runs and its reading is recorded, not taken), and `parallel_work` with fewer
  workers than cores (the count matters: four workers read 2048 here, sixteen 2^18).

The probes moved from `Plan/probes/` to `tools/tune/` and print `tuned <field> <value>`; the five that time a gain
between arms (`relay`, `recompute`, `band_bytes`, `stream_rows`, `turn`) time them interleaved in child processes and
decide by the median over the processes (a program's time moves by several percent with where its pages land, which no
number of rounds in one process averages out), with a tolerance where they look for a crossing or a plateau
(`band_bytes` takes the fastest budget, answer 10); `parallel_work` times its serial and split runs in one process, the
median of 11 each, a split losing past 3 %; `latency` and `memory` have no arms (the least of two rounds of chains; a
least-squares fit). They are built with the build's compiler and flags (through ccache), run one at a time, pinned with
`taskset` where it can pin to the core (unpinned, with a warning, where it cannot). Against `main`'s faults: the
one-minute load is read before each probe and recorded, with a warning above 2 (the configure's own thread counts one);
a reading off its probe's grid or a plausible range keeps the default with a warning (a probe edited to print `inf`
did); `machine.txt` keeps the readings under a key (compiler and version, flags, threads and workers, a hash of
`tools/tune/`'s code), so a configure measures again only when it changed (a probe edited is measured at the next
configure, not by a build; an edit to the `.cmake` files or the header's template, CMake's own inputs, by the next build), and the planner's headers are left out of it (they change daily, a measurement takes
minutes).

**Where it reaches**: code that includes the header (`heinzel`'s interface carries its directory with the option on);
the benchmarks with `HEINZEL_BENCH_MACHINE=host` (an error without the option; the report's header lists `host`'s
values; `zen2` stays their default, so no benchmark picks up a measurement it was not asked for); and one test, built
only with the option, `host_machine_test`: nine gated rows' graphs on `Executor<…, host>` at `High`, bit for bit against
the walk (a measured machine can cost speed, never a bit). The Mac run (`Plan/23-audio-scheduler/m1/run.sh`) configures
`rw-bench` with the option on and `host`, so its benchmarks plan for what it measured in one run, and archives
`heinzel_tune/` and the test's result (built alone in `build/rw-host`, handed the measurement); `m1_pro` takes the
header's values by hand (Decision 70).

**The readings on zen2** (`build/rw-tune`, Release, `-march=native`, clang++-22, four workers; `01-Fields.md`): `host`
is `zen2` field by field, but `turn_pays_after` 16 (the named `zen2` keeps 32, Gerald) and `core`'s latency 60 (70),
and the defaults by design. Step 1 read each probe three times by hand; in step 2's eight tuned configures three fields
read another value once each (`recompute_always` 16, `recompute_if_uncached` 64, `band_bytes` 512 KiB at a load of
1.89), after which `recompute` measures in 12 child processes with a tolerance of 6 % (the code layout moved one
unchanged program 6 % between builds) and has read 8 and 32 every time since; `relay` read 32 once in thirteen.

**What a measured value cost.** Each gated row's program planned for `host` and for `zen2` (42 programs), printed and
diffed: three differ, all by `relay_pays_after` 24 against 12, and measured both ways `matmul/heinzel/128` ran 1.02x,
`ml/linear_relu/heinzel` 1.50x and `ml/mlp2/heinzel` 1.25-1.30x. The probe's B (256 KiB) fits the L2; theirs (1 MiB,
512 KiB a layer) does not, and read in place across 128 rows it loses far more than the probe's does, because the
planner's rule counts blocks of rows, not the operand's bytes (`planner/contraction.hpp` `relay_where_it_pays`). Hence
answer 11: `host` keeps 12, the move of the named `zen2` to 24 (Plan/23) is withdrawn, and the rule's missing term is
the cost model's (`Plan/backlog.md`). `turn_pays_after` 16 and `core` 60 move no gated program.

**The time**: the first tuned configure 6.0 minutes (158 s building the probes, 198 running them), measured again with
ccache warm 3.2-3.4 minutes; with the key unchanged 0.3 s and the header untouched; with the option off 0.31 s and
nothing compiled. At the close, measured again after its probe edits: 377 s (155 building, 222 running; the
load 0.68-1.68 before each probe).

**At the close** (step 5): from the Mac dry run, three faults a bare tuned configure on a Mac would meet, fixed: the
probes get CMake's SDK (`-isysroot`; `run.sh` had hidden it by exporting `SDKROOT`), the load is read in the C locale
(a German one prints a decimal comma), and `memory`'s buffers are 256 MiB at least (with the L2's 12 MiB as `l3_bytes`
they were 48 MiB, only twice the M1 Pro's 24 MB system-level cache; zen2 reads 4.18-4.21 bytes a cycle with the floor,
4). The matrix green (`rw-clang-26` and `-23` 308 of 308, `rw-asan` and `-O2` 306 of 306, nothing "did not run").
`include/` changed in comments alone (the probes' new paths, `relay_pays_after`'s reason), so neither the bench gate nor
`measure.sh` ran: the differential's 15,730 programs and refusals are the same as at the package's start (`61ee2d1a`),
and `rw-bench` (`zen2`) builds every arm.

## Decision 73 — lanes as wide as a share of the registers, and multiply-adds outside the cycles (`Plan/probes/map-unroll`; 69 and 70 are reserved by P5, P6)

**Decided** (2026-10-11, an agent, on Gerald's request to fix the M1 run's gate without moving zen2's numbers). The M1
Pro's run (`build/m1-rerun-20261010-162730`, planned for `host`) had ten gated rows over their factor against `main`,
the same in two runs: `elem` N4096 1.32×, `strip` 1.39×, `img/brightness` 1.28×, `conv` 1.38-1.54×, `blurxy` H32 1.18×,
`harris` H8192 1.16×, `osc_sine/fm` 1.03×. Planned for `zen2` on the same Mac, five of them were level, and `m1_pro`
with `vector_bytes` 32 alone matched that: the narrow vector was most of it (16 bytes, one NEON register a lane). Lanes
of 32 bytes everywhere were not the answer (matmul 1.2-1.4× slower, harris, attention); the rest was the fused
multiply-add, which `main`'s loops had and this tree's lacked outside contractions and feedback sums.

**The rules.**
- *A map's lanes* (`put_the_lanes`): one vector of an eighth of the registers (zen2 two, the M1 four), a sixteenth where
  the stage runs a core (its dozen or so values live take the rest: zen2 one, the M1 two); *each of a fold's four
  blocks* a sixteenth (zen2 one register, the M1 two). Never wider than a literal axis holds, and a band's chunk along
  the lanes narrows a widened map back (`band_where_it_pays`: a stream's chunk of 8 samples would otherwise be all
  tail). One vector of several registers, which the compiler interleaves, and not blocks of one register (`unroll`):
  blocks ran the strip 1.2× and conv at 16 taps 0.75×, the chains one after the other.
- *An add of a product* outside the feedback cycles and the sums they read, the product read by nothing else and laid
  out and rounded as the add, becomes `_madd` where the machine has the multiply-add (`rewrite/product_adds.hpp`, last in
  `rewritten`), as Decision 66's rule fuses a feedback sum's last product. A feedback sum's feed-forward terms stay that
  rule's: fused too, they moved the cost model's cut of an eight-section cascade (`merge_bound_test`), and the audio
  programs of Plan 23 stay as they were.
- *A sum folding a product* of its stage that nothing else reads, its operands of the product's type, folds each point
  in one multiply-add where the machine has the instruction and issues four adds a cycle (`sums_a_product`,
  `Schedule::Stage::contract` on a fold's stage; the lowering points the fold at the product's two operands, `fused`,
  and leaves the product's statement to the compiler to drop). On zen2, whose two adds have pipes of their own beside its
  two multiply-adds (`fp_per_cycle` 2), the fused add only lengthened the chain: conv at 32 taps 1.1× slower. `m1_pro`'s
  `fp_per_cycle` is the run's reading, 4.

All three change rounding only where the precision grants reordering (`Normal`; never at `High`): a map's lane count not
at all, a fold's only where its lanes run along its own axis, which a precision has to grant already.

**Measured.** The M1 (`m1_pro`, `main` live, one process each): every gated row within its factor; `elem` N4096 0.98×,
`strip` 0.97×, `conv` 0.91-0.96×, `blurxy` H32 1.07×, `harris` 0.99×, `osc_sine/fm` 0.97×, `img/brightness` 0.99×, `blur`
0.49-0.60× (from 0.64-0.73×); no contraction row moved. `img/brightness` reads 18-22 µs from process to process in both
trees (where the input lies on a 16 KiB page; the workspace's stagger is laid out for 4 KiB pages), so one reading of
it is luck. Planned for `host` (the run's own machine), the same but `osc_sine/fm`, 1.004× over 16 processes against its
factor 1.0, and conv at N1048576 1.14× (banded: `host` takes the L2 for the L3, `06-M1.md`). zen2 (A/B against the tree
before, three rounds, core 4): `blurxy` H32 and `img/brightness` 0.95×, `ml/rownorm` and conv K16 0.98×, `elem` N4096
1.04×, every other gated row within 1.5 % (`osc_sine/fm` 1.005×: two registers had spilled its `sinpi`, 1.11×; the fused
fold had cost conv at 32 taps 1.11×).

**Checked**: `rw-clang-26`, 310 of 310. `rewrite_parallel_form`'s run-time `compile` had needed a little over 8 MB of
stack in that unoptimized build, the tree before as well; with `compile`'s result off its frame and
`planned_as_rewritten`'s plans in a function of their own (2.2 MB less) it runs in 7. `measure.sh`: the `Executor` 2-5 instructions over the bare run,
14 on W7 (0.005 %); W2's bytes re-pinned, 68 → 32 B over its reference (its map in lanes of two registers on the default
machine). Every plan golden that moved is a map's vector doubled, none a fold's, none fused.

**Given up for now**: the strip on zen2 ran 0.48× with its `tanh` four registers wide, but the same width spills the
oscillator's `sinpi`; a core's live values are not in the rule table, so cores keep a sixteenth (backlog).

## Decision 74 — value ranges: a compile-time proof that no operation leaves its domain (`Plan/26-value-ranges`)

**Decided** (2026-10-13/14, Gerald; Plan 26, his idea from Heinzel's predecessors). A graph states what it knows of its
numbers, and the compiler proves from it that **no operation receives a value outside its domain**: no division by a
range that holds 0, no `sqrt` below 0, no `rsqrt` or logarithm at or below 0, no `asin` or `acos` outside [-1, 1], no
`tan` across a pole, no `atan2(0, 0)`, no `pow` of a base below 0 (or at 0 with an exponent that may be negative).
Ranges are about the true, real-number values; overflow and the program's rounding are not covered. The proof computes
in double, with each number as the graph writes it, so an f32 program can round past what it proved: `frac` of an f32
just below 0 is 1, and a scalar literal f32 cannot hold (1e-46) is 0 (`docs/limitations.md`).

**The spelling** (approved 2026-10-14). A range is a list of comparators that all hold, `gt`, `ge`, `lt`, `le` and `ne`
for a hole, inside a union of up to four intervals; it goes last in a `Param`'s or an `Input`'s declaration, or into
`v.promise(…)`, an in-place mark on a computed value like `.precision()`. A promise on a port, a second promise on one
value and a promise on a recurrence's variable (`y[n].promise(…)`: "promise its definition") are refused; outputs named
`#…` are reserved; `compile` and `try_compile` take a trailing `checked`, and `CompileResult` gains `ranges`.

**Strict, for every graph.** A port without a range may hold any number, and the first finding refuses the graph by
name (`/ may divide by zero: the divisor is ge(0), le(2)`), the remedy in `docs/limitations.md` (a message holds 95
characters). Built report-only first (step 2) and made strict once every graph of ours passed (step 4): the probe's
82 graphs had 12 findings as written, all missing port ranges, in two places, and none real. Of the tests' graphs,
five were refused for a missing port range (the operation corpus's three entries, two one-operation graphs) and three
because `abs`, `max(x, -x)`, hides that a magnitude is at least 0 (fixed by `abs(x).promise(ge(0))`); of the
benchmarks', one (`bench_transcendental`'s log); of the examples', none. None was a real bug. The real catches are
gradients at a domain's edge (the derivative of `sqrt` at 0 is infinite), which a declaration away from the edge
answers.

**Debug and release plan different programs.** A promise the proof cannot prove is checked by a debug build while the
graph runs (an output `#k` the `Executor` scans after each run), and vanishes in release, as `assert()` does; a ranged
`Param` is checked when it is set and a ranged input before each run. `Executor<build, M, Checked>` takes `Checked`
from `NDEBUG` (`detail::kChecked`), so a debug and a release unit never share one `Executor` type: a mix is a link
error, not a silent mix-up of two programs. A check is a reader of the value it checks, so a rewrite that needs the
value to have no other reader declines on its own, with no rule of its own: a check on a cascade's first section keeps
debug serial, and at `Normal` a serial float cascade rounds as one (KFR's three-section EQ: 8.2e-5 from long double in
debug, 4.6e-8 in release's bank). Release plans the program of the graph written without ranges: the proof clears the
graph's ranges before the rewrites, and the release bench objects are those of the tree before Plan 26, function by
function.

**Where it lives.** `ranges/` (`interval.hpp`, `proven.hpp`), a layer after `runtime` that reads `ir` and the
operations' own writing (`compute_as<double>`), and two rule-table columns, `RangeRule` and `Domain`, so that the proof
names no operation; `compile.hpp`'s `take_ranges` between `infer` and the rewrites; the checks in `executor.hpp` and
`ports.hpp`. The walk is linear: a port's range or a constant's number, each node's operands against its domain and its
result by its rule, silence (0) at a past read, a promise proven, narrowing or contradicted (on a recurrence's
definition, a proof by induction), the square rule (a value times itself is at least 0) and the limit rule (an end
whose image is a function's limit at an infinity stays open), both from the probe.

**After step 4** (Gerald, 2026-10-14). `abs` (`max(x, -x)`) and `frac` (`x - floor(x)`) state the range their result
always has, at least 0 and [0, 1), as a fact: a mark on a computed value that is no promise, which the proof trusts as
it does a port's range and nothing checks, so neither costs a debug build anything. A user's promise on such a value is
a second mark, which narrows the fact; the "one promise" rule is about the user's. The step-4 workarounds,
`abs(x).promise(ge(0))` in three tests' graphs, went. The checks' messages write their numbers in the shortest digits
that read back as the same number of its type (a Param set to 1 + 1e-12 against `le(1)` read as the bound in six), and
a range of one number is written as that number ("the divisor is 0 or ge(1), le(2)").

**After the review** (Gerald, 2026-10-15). `frac`'s fact is [0, 1], closed: `x - floor(x)` rounds to 1 for a tiny
negative x, and the open fact had left such a value no number, inside every domain, so that everything after it
counted as proven. A fact that shares no number with the rules' range now leaves that range. A broken promise writes the
numbers it found before the range ("promise #0 broken at sample 2: … is outside ge(-4), le(4)"), a value the same at
every sample by element, and every number in a check's message is the double it was compared as, so that 0.1f never
reads "0.1 is outside le(0.1)". Only output names starting with `#` are reserved. A promise on a constant is decided by
comparing the constant, so a promise on a NaN or an infinity no check would read is contradicted. `take_ranges` reads a
port's first mark, as the proof does.

**After review round 2** (2026-10-15). A broken promise names its element where its value has no time axis (a tensor's,
a Param's: its time kind), and its sample otherwise, also where a run holds one sample. A Param's or an input's message
cuts a long name so that the index and the number always fit. A constant's promise proven by comparing it narrows its
range, so a promised +inf is no longer any number. Every path by which an output's name enters a graph refuses `#`
(`gradients` too), and the proof refuses a hand-built graph's promise on a port, in the frontend's words.

**Rejected.**
- *Keeping the checks in release*: a scan of every checked value each run, and a reader that holds the parallel form
  back, against the zero-overhead goal (Decision 55); release trusts its caller, as `assert` does.
- *An opt-in check*, by a graph or by a build: a proof most graphs skip proves nothing of them, and strict costs our
  graphs a declaration or a promise in a few places.
- *Finiteness as the guarantee*, for now: it needs a bound on every feedback value, and one-step induction proves
  about half (71 of the probe's 147 recurrences, with a uniform promise); resonant sections, state-variable filters and
  phase accumulators are never proven.
- *Promises on ports*: one spelling per meaning; a port's range goes in its declaration, where a debug build checks the
  number as it comes in.
- *A proof inside `infer`*: the rewrites analyse graphs again whose promises a debug build has turned into checks and
  whose ranges are cleared, so a proof there would refuse what was already proven.

## Decision 75 — Plan 28 package A: the frontend's spellings (`Plan/28-dsl-proposals/02`)

**Decided** (Gerald, 2026-10-16: "looks all good, apply the proposals", `Plan/27-examples/dsl-proposals.md`). Package
A spells in the frontend what writing the examples met as awkward or missing. Each spelling builds operations that
exist; a graph that keeps its spelling builds node for node the same graph, so no golden, conservation hash or
zero-overhead row moves. Each step of the package adds its section here.

**A1: comparisons, `where` with a number first, `clamp` with expressions** (proposals 1, 2's first half and 15).
- *The comparisons* `<`, `>`, `<=` and `>=`, each between two expressions, or an expression and a number on either
  side. No operation is added: `a < b` is `_lt(a, b)`, `a > b` `_lt(b, a)`, `a <= b` `_le(a, b)`, `a >= b`
  `_le(b, a)`. A number is the `constant` it is, as in `max(x, 0.5)` and the arithmetic, so `x > 0.0` builds the graph
  `x > constant(0.0)` built, and an integer is a number (`x > 0`). Before, only `>` between two expressions was
  spelled; it builds the same node as it did.
- *The overloads' rule*: an `Expr` or a `double` on either side. Whatever converts to an `Expr` compares as one (an
  `Input`, a `Param`, an assigned `Output`, a `Variable` by name or as `v[n]`, a past read `x[n - 1]`), and no type
  converts to both an `Expr` and a `double`, so for any two operands at most one overload fits: nothing that compiled
  before is ambiguous now or means something else.
- *A NaN on either side gives 0* in all four, as in C++: they are the ordered comparisons of IEEE 754, which the cores
  (`runtime/compute.hpp`, `p < q ? 1 : 0`) and the lanes already compute, at every precision and lane width. So, as in
  C++, `x >= y` is not `1 - (x < y)` where a NaN may come, and `where(x > 0.0, t, f)` takes `f` for a NaN.
- *`==` and `!=` are not spelled* (`_eq` keeps none): an exact equality of floats is rarely what is meant, and
  `y == x` typed for `y = x` would compile into a mask nothing reads, without a word.
- *`where(c, number, e)`*, beside `where(c, e, number)`, the number a `constant`. Two numbers
  (`where(c, 0.877, 0.9987)`) wait for package B: neither branch has an element type to give the other (B-6).
- *`clamp(x, lo, hi)` with an expression for either bound or both*, `min(max(x, lo), hi)` as with two numbers. Two
  numbers the wrong way round are refused while the graph compiles ("heinzel: clamp(x, lo, hi) has lo above hi"); an
  expression's value is not known while the graph is built, and where lo is above hi the result is hi.
- The examples write `x > 0.0` (`02_params_and_functions`, whose note that `constant(0.0)` makes 0 an expression
  went), `where(c, -1.0, …)` (`09_tensors`) and `where(c, -1e30, s)` (`transformer_block`); each binary is byte for
  byte what it was, and so is what it prints. Checked: `rw-clang-26`, 366 of 366 (`comparisons`: every spelling's
  graph against the operations it names, the four run on `Executor` at `Normal` and `High` as C++ answers them;
  `build_refuses_clamp_bounds_crossed`); `measure.sh` over the bare, bare + copy and `Executor` arms of `include/`
  before and after: all 88 binaries byte for byte the same.

**A2: the tensor members on an `Output` and a `Variable`** (proposal 3).
- *What forwards*: every tensor member of `Expr` works on an `Output` and a `Variable` as on any value: `sum`,
  `mean`, `max`, `min` and `prod` (by an axis or by axes, with `keepdims` or without), `transpose`, `reshape`,
  `slice`, `unfold`, `unfold2d`, `squeeze`, `unsqueeze`, `realize`, `sort`, `topk`, `quantile` and `median`.
  `z.max(1, keepdims)` is what `Expr(z).max(1, keepdims)`, the spelling it replaces, was: the value is read as that
  conversion reads it, then `Expr`'s own member builds the node, so the graph is node for node the same. An `Output`
  read so before it is assigned is marked read as `Expr(z)` marks it, and a later `=` is refused ("an Output was read
  before it was assigned"); one read before its `[n] =`, or a `Variable` before its definition, reads the value the
  recurrence will define, as it does by name.
- *One empty base, one forwarder for each overload*: `forwarding::members<Self>` (`frontend/ops.hpp`), which
  `Output` and `Variable` inherit, holds 32 forwarders with `Expr`'s signatures one for one (`Axes`, `Shape`,
  `keepdims_t`, the defaults of `slice`'s step and `unfold`'s hop) and the 14 deleted templates that refuse a
  floating-point axis, so `z.max(0.0)` is refused in the words `x.max(0.0)` is ("call to deleted member function
  'max'"); without them `z.max(0.0)` would convert to the forwarder's axis 0 and fold. Not a variadic pack: a pack
  deduces no type for a braced list, and members take one about 80 times in the tree (`y.reshape({2, 3})`,
  `y.transpose({1, 0})`, `y.sum({0, 1})`). A member or an overload added to `Expr` is added there too, as the note
  at `Expr`'s list says. The base is empty: `sizeof(Output)` stays 136 bytes and `sizeof(Variable)` 32, and both live
  only while a graph is built.
- *Not forwarded*: `promise` and `precision` mark the node a value is made at, and an `Output` or a `Variable` names
  a value made elsewhere: a recurrence's promise goes on its definition (Plan/26-value-ranges/02, as `Now::promise`
  says), the precision mark only where a value is made (A4). `operator[]` keeps its own meaning on both (`y[n] =`
  defines, `y[n - 1]` reads the past); E2's `c(k, i)` joins the base when it lands. `y[n]` on an `Output` or a
  `Variable` that is not `const` is still the proxy of `y[n] =` and takes no member: write `y.sum(0)`.
- *Nothing that compiled changes meaning*: neither class had a member of these names, nothing inside them or their
  `Now` calls one unqualified, and the base holds nothing. A base's namespace joins every argument-dependent lookup on its class, so the base
  lives in a namespace of its own, `heinzel::forwarding`, which holds nothing else: in `detail`, a user's call with an
  `Output` or a `Variable` in it would have looked into `heinzel::detail` too (a first cut did; review, 2026-10-16).
- `09_tensors` names the z-scores once, `z = centred / spread;`, and reads them as `z.max(1, keepdims)` and
  `row_top > z` (its `Expr standard` went); its binary and what it prints are byte for byte what they were.
  `bench_compare_juce_heinzel` writes `y.sum(0)` for `hz::Expr(y).sum(0)`. The tests that write `Expr(v).sum(0)` keep
  it: they are no model for users. Checked: `rw-clang-26`, 367 of 367 (`members`: an assigned `Output`, one read before
  its `[n] =`, a `Variable` after and before its definition, each member's graph against `Expr(y)`'s, braced lists
  too, and an `Output` read by a member, then assigned, refused as after `Expr(y)`;
  `build_refuses_members_a_float_axis_on_an_output`); `measure.sh` over the bare, bare + copy and `Executor` arms of
  `include/` before and after: all 88 binaries byte for byte the same.

**A3: the units from `heinzel`** (proposal 11).
- *What*: `heinzel::ms`, `heinzel::us` and `heinzel::second` are using-declarations of `units::ms`, `units::us` and
  `units::second` (`frontend/declarations.hpp`, beside `f32` and `f64`), so a program that writes
  `using namespace heinzel;` writes `x[n - 120.0 * ms]` with no `using units::ms;` after it. They are the same
  objects, not copies: `heinzel::ms` and `units::ms` are one variable, and a file that opens both namespaces sees no
  ambiguity. Nothing in `heinzel` was named `ms`, `us` or `second` (the `second` of the solver's parameters is a
  local); `units::Quantity`, `Scale` and `to_samples` stay in `units`, as no new public name for them is approved.
- *The long names retire*: `units::millisecond` and `units::microsecond` are deleted, `ms` and `us` are the
  definitions (`Scale ms{1e-3}`, `Scale us{1e-6}`); nothing in the tree used the long names. Gate `public_spelling`
  refuses `units::millisecond` and `units::microsecond` in the files a user reads. The token carries `units::` so
  that the gate's whole-word match catches the retired spelling (`units::millisecond`, `hz::units::microsecond`, a
  `using units::millisecond;`) and nothing else: prose such as "three milliseconds back" and `std::chrono::milliseconds`
  stay free, as the bare word would have been refused in them. The fixture holds one offence for each (39 in all,
  from 37) and a line of look-alikes that must pass.
- *Examples*: `using units::ms;` goes from `03_the_past`, `08_streaming`, `synth_voice`, `README.md`, the zero-overhead
  workloads and `index_notation_test`; each example prints and writes byte for byte what it did. No graph moves.
  Checked: `rw-clang-26`, with `named_units` showing `heinzel::ms`, `us` and `second` the very objects of `units`;
  `measure.sh` over the bare, bare + copy and `Executor` arms of `include/` before and after: all 88 binaries byte for
  byte the same.

**A4: the precision's names, the mark where a value is made, `finish(level, …)`** (proposals 5 and 12).
- *The names*: `heinzel::Precision` is a using-declaration of `numerics::Precision`, and `High`, `Normal` and `Fast`
  are constants equal to its enumerators (`frontend/declarations.hpp`, beside `f32` and `f64`), so a program that
  writes `using namespace heinzel;` writes `(a + b).precision(High)` and `template <Precision P>` with no
  `using numerics::Precision;`. `Inherit` gets no name: it is no level to pick, and the analysis refuses it as a
  graph's default. Nothing in `heinzel`, nor in the tests, examples, benchmarks and tools that open it, was named
  `Precision`, `High`, `Normal` or `Fast` (a test's template parameter `bool High` hides the constant where it is
  declared, a scoped `Decline::High` is looked up in its enumeration); a file that writes `using numerics::Precision;`
  after `using namespace heinzel;` names one type twice, which is no ambiguity.
- *The mark where a value is made*: `.precision(P)` writes the level on the node a value is made at, and a named value
  shares that node with every reader of it, so `y = x.precision(Fast)` made x `Fast` too (the backlog's line). Now
  `Expr::precision` has two overloads. On a temporary (`&&`) it marks; on a named value (`const&`) it is a template
  that never compiles, one error in words that show the fix: "heinzel: mark a value where it is made: Expr y =
  (a + b).precision(High)" (`detail::kNeverCompiles`, which `Now::promise` uses too, moved from `graph_builder.hpp` to
  `arena.hpp`, before `Expr`). A temporary whose node some node already reads is refused while the graph is built,
  "heinzel: mark a value where it is made, before anything reads it": one pass over the arena's nodes a mark, and a
  graph holds a handful of marks. A port is marked where it is declared,
  `Expr x = Input{"x", Signal{N}}.precision(High)` (a `Param` the same way); a named `Input` or `Param` is a named
  value. `Output` and `Variable` have no `precision`
  (A2): the value they name is made by its definition, which is where the mark goes (`y[n] = (…).precision(High)`).
  The overloads pick by the value category alone: `&&` is the better match for a temporary (a `const&&` one, for a
  function returning `const Expr`, marks through a copy, added after the package's review) and the template the only
  one for a named value, so a mark on a temporary marks the node it marked before, unless something reads it already
  (none in the tree did).
- *The hole that stays*: a temporary that names a named value's node marks it while nothing reads it yet, and a
  reader written later sees it: `Expr(x)`, `x[n]`, `std::move(x)`, `x.promise(…)` (it returns the same node), an
  identity `einsum("ij->ij", x)`, `Expr(v)` or `std::as_const(v)[n]` on a Variable, `Expr(y)` on an Output, a
  subgraph whose body returns its argument (the review found all but the first two). No value category tells that
  copy from a fresh result; the backlog's line on the shared mark names this hole. Whether `promise` on a named
  value should be refused as `precision` is goes to Gerald.
- *`finish(level, first, more…)`*: finishes as `finish(first, more…)` does, then sets the graph's default
  (`Graph::default_precision`) by calling `set_default_precision`, so `return finish(High, y);` is the graph
  `Graph g = y; set_default_precision(g, High); return g;` was. The level comes first; no `finish` took a `Precision`
  before, and nothing converts a `Precision` to an `Output`.
- *`set_default_precision` retires from what a user reads*: gate `public_spelling` refuses it in the examples, the
  benchmarks and the tutorials' and parity ports' `_here` programs (fixture: one offence and three look-alikes,
  40 in all, from 39), and its message names `finish(High, y)` among what to write. The tests, the tools, the test
  support and `app/heinzel/gradients.hpp` keep it: the gate does not scan them, and `gradients` takes its level as an
  argument. `tutorial_04_here` and `tutorial_08_here` write `return finish(High, Output{"y"} = …);`.
- *The tests' marks on named values go where the value is made*: `precision_resolve` (two ports,
  `Expr x = Input{"x", Signal{N}}.precision(…)`), `rewrite_parallel_form_declines` (`b0 x` marked as it is made where
  `high`), `tiers` (the operation's result marked as it is made). Each graph is the one the old spelling built: a
  throwaway built every rewritten graph, the tutorials' and `13_precision`'s too, both ways against the new headers,
  the old way as the old mark did it (the level written on the node), and found them equal; `precision_resolve` keeps
  that for its port and a sum.
- *Examples*: `13_precision` writes `High`, `Normal` and `Fast`, `template <Precision P>` and `return finish(P, y);`
  (its `using numerics::Precision;` went), and its header tells the mark where a value is made; its binary and what
  it prints are byte for byte what they were. `examples/README.md` and `docs/architecture.md`'s Numerics say the same.
  Checked: `rw-clang-26`, 369 of 369 (`precision_resolve`: the names, the marks where made against the mark on the
  node, `finish(level, …)` against `set_default_precision` with one output and two;
  `build_refuses_precision_a_named_value` and `build_refuses_precision_a_value_read`); `measure.sh` over the bare,
  bare + copy and `Executor` arms of `include/` before and after: all 88 binaries byte for byte the same.

**A5: `subgraph`, a piece made first and applied later** (proposal 20, Gerald's own: "maybe we should rather name it
`subgraph` or so? also I think it shouldn't run with an empty function immediately. the double-instantiation should
probably be an error").
- *What*: `subgraph(name, body)` makes a piece, a `Subgraph<Body>`; applied, `piece(args…)` does what
  `instantiate(name, body, args…)` did: it pins each argument's arena, runs the body, and puts `name.` in front of
  every input the body declared, so nesting composes (`outer.inner.k`). `auto bass = subgraph("bass", bass_shelf);`,
  then `Expr s = bass(x);`. The piece holds the name as a `Name`, a copy (a `string_view` could dangle once the string
  it was made from is gone), the body (a function pointer, or the lambda that captures nothing) and
  `mutable bool applied_`. A name that would not fit is refused when the piece is made, in the words of any name;
  before, only when the body declared an input.
- *The body runs when the piece is applied, never when it is made*: making a piece builds nothing. A piece made and
  never applied leaves nothing in the graph, one made before the inputs it is applied to exist builds the graph of
  one made just before it is applied, and a body that takes nothing (`noise()`) runs where it is called.
- *A piece is applied once*: a second application is refused while the graph is built, "heinzel: a subgraph is applied
  once; make one for each use", since it would declare the same names twice (an error, not shared knobs, Gerald). The
  call is `const` and the flag `mutable`, so a `const` piece applies too. A constant evaluation changes only an object
  whose life began in it, so a piece lives inside its graph function: a `constexpr` piece at namespace scope does not
  compile when applied ("read of mutable member 'applied_' is not allowed in a constant expression"), as the header
  and the tour say. Copy and move are deleted, so a piece is the one object it was made as and no copy of it applies a
  second time: `auto bass = subgraph(…)` is that object by guaranteed copy elision (C++17, so at the C++23 floor too),
  and `auto copy = bass;` does not compile.
- *Each misuse one error, where it is written*: the body's check (a function, or a lambda that captures nothing) fires
  at `subgraph(…)`, in `subgraph` itself and not in the class, as clang drops a class's members when a `static_assert`
  in it fails and the application after it would be a second error ("does not provide a call operator"). The
  arguments' and the result's checks fire at the application, with the fallbacks that keep each misuse to one error,
  as before. The messages begin `heinzel::subgraph:`. A piece made the body of another (`subgraph("outer", inner)`)
  gave three errors; since the package's review it is one, "heinzel::subgraph: apply the inner piece inside the outer
  one's body", from an overload that then makes the inner body a piece under the outer name, so applying it adds none
  (`build_refuses_subgraph_a_piece_of_a_piece`, built while compiling).
- *`instantiate` is retired*: deleted, and gate `public_spelling` refuses `instantiate(` in the files a user reads.
  The token carries its `(` so that the whole-word match catches a call (`instantiate(`, `hz::instantiate (`) and
  nothing else: prose ("instantiated") and look-alikes (`my_instantiate(`) stay free, as `units::` did for A3. The
  fixture holds one offence and a line of look-alikes (41 in all, from 40); the gate's message names `subgraph` among
  what to write. Since the review, a member call (`dsp->instantiate(48000)`, Faust's) is not matched, and the root
  `README.md` is scanned.
- *Moved uses, each graph the same*: `subgraph_test` (the strip's pinned hash `0x47a6dbefe8261f43` unmoved),
  `construction_test`, `ranges_recorded_test` (its `instantiated()` now `in_a_subgraph()`),
  `parity/graph_composition_here` and `06_subgraphs`. A throwaway built every moved graph both ways against the new
  headers, the old way through `instantiate` copied verbatim, and found them equal with `Graph`'s `==`. The refusal
  fixture's seven cases move with their messages.
- *Tests*: `subgraph`: a piece made before its inputs exist against one made just before it is applied, a body that
  takes nothing made first and made late, pieces never applied against the graph without them; at run time a body
  that takes nothing counted (no run when its piece is made, one when it is applied), a piece applied twice and a
  piece's name too long refused by their texts. `build_refuses_subgraph_a_piece_applied_twice`: one error, its text in
  the compiler's note (only this case is built while compiling; there, the other cases' fallbacks would fail twice).
- *Examples*: `06_subgraphs` makes its pieces first (`auto bass = subgraph("bass", bass_shelf);`, the treble too),
  then `Expr s = bass(x); s = treble(s); y = volume * s;`, and its header says what a piece is, that it is made once
  and applied once, and why it lives inside its graph function. Its binary and what it prints are byte for byte what
  they were, and `parity_graph_composition_here` prints the same samples. `examples/README.md`, `docs/architecture.md`
  (the frontend) and the backlog's line on redundant spellings follow. Checked: `rw-clang-26`, 370 of 370; `measure.sh`
  over the bare, bare + copy and `Executor` arms of `include/` before and after: all 88 binaries byte for byte the
  same.

**A6: `log10` and `cospi`, composed** (proposal 4, its first half).
- *What*: `log10(x)` is `log(x) * std::numbers::log10e`; `cospi(x)` is `sinpi(0.5 - r)` with `a = abs(x)` and
  `r = a - 2.0 * floor(a * 0.5)`. Both are composed helpers beside `frac`, `abs` and `clamp`
  (`frontend/graph_builder.hpp`) and add no operation, so the loop tree runs them wherever it runs `log` and `sinpi`,
  at every precision. No composed `exp2` or `cos`: package D makes them operations.
- *The constant*: `std::numbers::log10e` is 1/ln 10 correctly rounded (0x1.bcb7b1526e50ep-2, checked in 200-bit
  arithmetic); `1.0 / std::numbers::ln10`, which `12_ranges` wrote as `10.0 / std::numbers::ln10`, is one ulp low.
  Both round to the same float, the correctly rounded one, 0x1.bcb7b2p-2.
- *The reduction takes |x|*: cos is even, and for a >= 0, `a - 2 floor(a/2)` is exact at every float (a itself below
  2, Sterbenz's lemma from 2 up). The plan's first draft reduced x itself, which is exact only for x >= 0: for a
  negative x, `x + 2k` rounds to r's ulp. It answered -0 at -0.49999997, whose cosine is 9.4e-8 (1.3e7 ulp), and over
  every negative float down to -2^25 at High 7.5 million answers were more than 2 ulp off; the absolute error stayed
  at about 2e-7 at most, so a waveform looked right while the zeros were wrong. With |x| it costs two more nodes (a
  negation and a max). `1/2 - r` is exact for r >= 1/4 and rounds only below, where the answer is above 0.7; that is
  where the worst errors lie. `cospi` is exact at the integers and the halves; where r = 3/2 (`cospi(1.5)`,
  `cospi(3.5)`) the answer is `sinpi(-1)`, -0, which compares equal to 0 (IEEE 754's cosPi gives +0), accepted. The
  spelling it replaces, `sinpi(x + 0.5)`, answered 0 for every float from 2^23 up and was 1.3e7 ulp off at 0.49999997.
- *Accuracy*, measured over every float on `Executor` (f32 ports, the default machine, against long double, 2026-10-16):
  `log10` over every positive float at most 2.03 ulp at `Normal` (at 0.350355953; 40 of 2.14e9 over 2) and 1.37 at
  `High` (at 6.04565479e-29; none over 2). A composed `log10` cannot reach 1 ulp at `High`: the log rounded to f32
  first costs up to 0.87 ulp of the result where log10 has the finer exponent, the f32 constant 0.34 and the product
  0.5 (`log(x) / ln10` measured no better there). `cospi` over every non-negative float (a negative one is the same
  computation after the exact |x|) at most 1.70 ulp at `Normal` (at 0.249258772) and 1.06 at `High` (at 0.249794945),
  none over 2. The plan's figures (about 2 and 1, about 2.6) are corrected there. The `High` figure is a `log10`
  whose log runs at `High`: precision flows forward, so a mark on `log10(x)`'s result pins only its multiply and the
  log runs at the graph's level (the package's review, by probe); its operand marked where made or `finish(High, …)`
  pins both. The comment at `log10`, `docs/architecture.md` and the backlog's transcendental line say so; `log10` as
  an operation of its own (that line) would end it.
- *The proof of ranges* takes each part's rule: `log10(p)` with a p not kept above 0 is refused in `log`'s words,
  "heinzel: log may see a number <= 0: its operand is …", as the message names the node's operation and `log10` is
  `log` times a constant. The domain is the same, so the message stays; `docs/limitations.md`'s table and the
  comment at `log10` say so. `cospi` needs nothing: `abs` (with its fact), `floor`, the products and `sinpi` are
  total.
- *Left as they are*: `eq8sweep` (`benchmarks/eq8_graph.hpp`) keeps `sinpi(half_turns + 0.5)`, a gated benchmark
  whose graph would move; `tests/parity/custom_op_here.cpp` keeps its own `myops::log10`, `main`'s helper with its
  f32 literal, compared with `main` bit for bit.
- *Tests*: `log10_cospi`: each spelling's graph against the operations it names, graph for graph; on `Executor` at
  `Normal` and `High` over 4096 floats each (powers of ten, the floats beside 1, a band where log10 has the finer
  exponent, floats of every exponent; the integers and halves, a sweep of [-4, 4], the floats beside each half, big
  floats to 3e38 and their negatives; the sweeps' worst points in both), held to 2.25 and 1.5 ulp (`log10`) and 2.0
  and 1.25 (`cospi`), and `cospi` exact at the integers and halves. `build_refuses_proof_a_log10_of_zero`: one
  error, in `log`'s words.
- *Examples*: `12_ranges` writes `10.0 * log10(power + 1e-10)`, and `10.0 * log10(power)` without the floor, whose
  `static_assert` text is unchanged. Its graph has one multiply more (10 times log10's product, where `10 / ln10` was
  one constant): 3667 of its 4800 numbers move, by at most 2 ulp, and its distance from the plain loop, which it
  prints, goes from 8.4e-6 to 7.2e-6 dB (its comment says so). `examples/README.md` (the math's names, tour 12, what
  is not spelled), `docs/architecture.md` (the transcendentals), `docs/limitations.md` and the backlog's line on the
  transcendentals follow. Checked: `rw-clang-26`, 372 of 372 (`log10_cospi`, at the sweeps' worst on its own points:
  2.03, 1.37, 1.70 and 1.06 ulp; `build_refuses_proof_a_log10_of_zero`); `measure.sh` over the bare, bare + copy and
  `Executor` arms of `include/` before and after: all 88 binaries byte for byte the same.

## Decision 76 — Plan 28 package B: the analysis (`Plan/28-dsl-proposals/03`)

**Decided** (Gerald, 2026-10-16: "looks all good, apply the proposals", `Plan/27-examples/dsl-proposals.md`). Package
B makes the analysis accept graphs it refused where writing the examples met the refusal as needless. Each step
changes only graphs refused before, so no accepted graph, golden, conservation hash or zero-overhead row moves (B3
moves one attribute in a corner no recorded graph holds, written in its section; the review's fix to B1 retypes graphs
B1 had accepted, written in B1's). Each step of the package adds its section here, and the review its findings.

**B1: a recurrence takes the graph's one length** (proposal 6). Amends Decisions 53 and 54: a length is inferred from
a declared one, never time from operations.
- *What*: a recurrence no signal drives (a knob's phase, a clock, one of numbers alone) had to declare its length
  (`Variable p{Signal{N}}`, Decision 54), also where the graph's one stream had that length already. Where the graph
  has exactly one length it now takes it: `Variable p;`, so `phase(hz)` is written once for any graph. With none (no
  stream at all) it is refused as before, "heinzel: a recurrence needs a Signal in it, or a declared length"; with two
  or more, "heinzel: a recurrence no signal drives takes the graph's length, but it has two; declare it" (91
  characters: the extents would not fit the 96 a message holds). A declared one is unchanged.
- *The graph's lengths* are the last extents of the values `time_kinds` seeds as signals: a timed input, a timed
  generator (`iota(Signal{…})`), a declared variable or output; told apart by `same_extent`. Every `Length` is the one
  symbol `kStreamLength`, so `Signal{Length{}}` and `Signal{2, Length{}}` are one length. They are told apart before
  the solver: a symbol bound to 512 and a literal 512 are two, so such a graph declares its recurrence's length.
- *How* (`undriven_take_the_length`, `sema/time_checks.hpp`): `infer` works out the time kinds once before the sweep,
  from the declarations alone (`time_kinds` reads categories, the `timeless` flag and edges, no shape), so they are the
  kinds `check_time` works out after it. A variable that declared nothing and whose recurrence comes out no signal is
  undriven; with one length, `Declared` marks it (`takes`, beside `at`) and keeps the length (`length`). In the sweep
  `propagate_recurrence` gives it its definition's shape with the length last (a tensor's recorded 1 replaced, so a
  bank driven by `Input f{"f", {5}}` is `[5, L]`; a value of no axes, numbers and Params only, gains it as its one
  axis) and its definition's type, or f32 where only numbers define it, as a declaration's default; `time_kinds` seeds
  it as a signal, as it seeds a declared one. After one analysis it has its shape and reads as declared, so a second
  analysis is equal (Decision 54).
- *Held as a declaration*: the planner treats a variable and its definition as one value, so `declared_variables_hold`
  holds one that took the length to it as it holds a declared one. One whose definition misses it (it reads no past,
  `v[n] = f * 2.0`, or folds it, `v[n - 1].sum(0)`; a fold over the stream is not refused yet, backlog O-12) had no
  signal in it after all, and is refused in the words it met before ("a recurrence needs a Signal in it, or a declared
  length", now `kNeedsASignal`, shared); declared, the same graph reads "a Variable is defined as [] f32 but declared
  [997] f32". Not in the plan's text, which said nothing of this case.
- *Accepted graphs do not move*: in a graph accepted before every recurrence is a signal by `check_time`'s kinds,
  which are the ones worked out before the sweep, so no variable takes a length and every later step runs as it did.
  The cost is one more `time_kinds` an analysis. Where a variable takes the length, the analysed graph is the one
  built with it declared, as long as each literal has one reader (each number written in an expression is its own);
  a literal named once and read by two nodes is typed by the first reader that types it, which may be another while
  the variable's type is still unknown, as for any undeclared variable a signal drives (a throwaway probe: with
  `Expr c = constant(0.5)` read in the recurrence and times an f64 Param, the variable came out f64 taken and f32
  declared, and so did one an input drives).
- *Why no further*: time is still never inferred from operations (Decision 53's reasons stand: a delay added
  downstream must not transpose a caller's input). Only a length is, and only from a declared one; a graph of two
  leaves the choice to its author.
- *Tests*: `declared_length` (each graph undeclared equal to it declared, and idempotent: the knob's phase beside an
  input at 997 samples and over a `Length`, a bank becoming `[5, 997]`, beside a declared clock as in
  `07_oscillators`, an `iota(Signal{4, frames})` the only seed, a coupled pair beside two inputs of one `Length` in two
  spellings, f64; two lengths refused, from two inputs and from a declared variable and an input; no past and a folded
  past refused; the phase beside an input, at 997 and over a `Length` of 64, run on `loops::run` and `Executor` at
  `High` bit for bit as the walk runs it), `tensor_signal` (`literal_only_recurrence` analyses to
  `literal_only_declared`; `tensor_only_recurrence`, no stream, stays refused), `streaming` (a knob-driven oscillator
  beside a `Signal{Length{}}` input, in blocks equal to one run, at `Normal` and `High`).
- *Examples*: `07_oscillators` declares its length on the clock only (`Variable t{Signal{kSamples}}`); its `phase` is
  `Variable p;`, and its header says why: a clock declares how long the sound is, and everything else takes that
  length. Its binary, what it prints and `bell.wav` are byte for byte what they were. `examples/README.md` and
  `docs/architecture.md` (building, the analysis) follow. Checked: `rw-clang-26`, 373 of 373 (no test added: the cases
  joined `declared_length`, `tensor_signal` and `streaming`); `measure.sh` over the bare, bare + copy and `Executor`
  arms of `include/` before and after: all 88 binaries byte for byte the same.
- *The review: typed after the sweep.* The f32 default was given at the variable's first visit in the sweep, so a
  definition typed only in a later sweep, through a variable defined after it, found its literals already adapted to
  f32: with `a[n] = a[n - 1] * 0.999 + 0.5 * b[n - 1]` before `b[n] = frac(b[n - 1] + f / 48000.0)`, `f` an f64
  Param, both variables came out f64 and `0.999` f32, unlike the graph declared f64; and `w[n] = where(f > 0.5, 1.0,
  0.0) + 0.5 * w[n - 1]` came out f32, its `where` typed by its condition only after the default (B2). Now the default
  is given where the sweep and the numbers' step leave a variable that took the length untyped
  (`undriven_default_to_f32`, `declared_variables.hpp`), as B2 takes its marks only after the sweep, and both run once
  more; until its definition is typed, the sweep keeps the variable's type as it is. Both graphs analyse to the ones
  declared f64. No graph accepted before B1 has a variable that takes the length, and of those B1 accepted, only one
  whose definition was untyped at its first visit can move (none in `measure.sh`'s rows or the examples: those
  binaries are byte for byte what they were but tour 15's, which finishes and analyses its graph at run time, and what
  the examples print and write is too). The B1 cases moved to `declared_length_taken` (`declared_length` was at 596
  lines), beside the two graphs.

**B2: `where` over numbers** (proposal 2, its second half; backlog B-6).
- *What*: `where(c, 0.877, 0.9987)` is spelled (`where(Expr, double, double)`, beside package A's `(Expr, double,
  Expr)`), and a `where` of two numbers, plain or `constant(a)`, is typed instead of refused ("the element type of
  value N could not be worked out"). Its numbers take the type of what reads the `where` (`* x32` f32, `* x64` f64,
  whatever its condition's); a `where` that nothing reads with a type, an output say, takes its condition's. With
  neither: "heinzel: where: its element type could not be worked out; type one of its values" (80 characters; the word
  is the rule table's spelling of the operation, written by `write_op`, so no branch names it). The overload makes its
  two numbers on named lines, so the graph does not hang on the order a compiler evaluates arguments in.
- *The rule* (`type_the_numbers` in `analyze.hpp`, `sema/untyped.hpp`): a value made of numbers alone is a literal no
  reader typed, or a node still untyped whose type-carrying operands (those its `TypeRule` reads: all, all but the
  condition, or the first; `typed_operands`, which `type_of_node` reads too) are all such. Never a Variable, nor a node
  that reads one: a variable's type is its definition's. Such a value adapts as a literal does: the first reader that
  types it gives it its type, and it hands the type on to the numbers it is made of. Then a node of
  `TypeRule::FromOperandsExceptFirst` still untyped takes its condition's type, the operand the rule skips, readers
  first, so a `where` that another reads takes that one's; one more sweep settles the rest. No line names an
  operation.
- *Accepted graphs do not move*: the sweep runs by the old rules first, and the values made of numbers are marked only
  where it left something untyped, a graph `everything_is_known` refused before. So every graph accepted before is
  typed by the old rules alone, as it was. Not as the plan said, which marked before the sweep and adapted in it: then
  a literal named once and read by two nodes could be typed through a value made of numbers by another reader than
  before. The cost for an accepted graph is one pass over its values and nodes.
- *What else it accepts*: any value of numbers alone that a typed value reads, computed per sample as written:
  `x * sqrt(constant(2.0))`, `x + constant(1.0) / 3.0`, a `where` whose condition is numbers too (a throwaway probe ran
  these on `Executor` at `High` bit for bit as the walk does). A `where` in a recurrence's definition that its condition
  types gives the variable that type through its definition, as any definition does
  (`w[n] = where(x > w[n - 1], 1.0, 0.0) + 0.5 * w[n - 1]`: f32, from `x`).
- *The refusal* is worded last, where `everything_is_known` was (an untyped `where` is named before any value number),
  so a graph refused before for another reason keeps its words.
- *Tests*: `inference` (`* x32` f32 and `* x64` f64 through `0.5 * picked + 0.25`, the mask of the other type; a
  `where` that is the output takes its condition's type, f32 and f64; each analysed twice, equal; numbers only refused
  with its text), `comparisons` (`where(c, 1.0, -1.0)` builds the graph of its two constants in the order written;
  JUCE's peak follower, its coefficient a `where` of two numbers in the recurrence, beside a sign typed by its
  condition, at f32 and f64 on `Executor` at `High` bit for bit against the walk). The run is in `comparisons_test`,
  and the marker in a header of its own: `inference_test` and `analyze.hpp` would have reached the 600-line gate.
- *Benchmark*: the peak follower of `bench_compare_juce_heinzel.cpp` writes `where(xa > env[n - 1], f(at), f(rl))`, no
  typed `constant({…}, {})`. `envfollow/heinzel` on core 4, three runs of seven repetitions, medians 2.281, 2.277 and
  2.282 ms before, 2.272, 2.268 and 2.266 ms after (JUCE's arm 6.27 ms in both): the same within noise.
- *Examples*: tour 02's header and `examples/README.md` say either or both of `t` and `f` may be a number; no example
  changes its code (`09_tensors`' `where(row_top > z, -1.0, iota(…))` and `transformer_block`'s were no workaround),
  and the binaries of 02, 09 and `transformer_block`, and what they print, are byte for byte what they were.
  `docs/architecture.md` (the analysis) follows. Checked: `rw-clang-26`, 373 of 373 (no test added: the cases joined
  `inference` and `comparisons`); `measure.sh` over the bare, bare + copy and `Executor` arms of `include/` before and
  after: all 88 binaries byte for byte the same.

**B3: `rearrange` regroups a computed value** (proposal 17; backlog B-7).
- *What*: `rearrange("(c f) -> c f", x * 2.0, {{"f", 4}})`, a split or a merge of any value, a recurrence's own past
  included. Before, the regroup was worked out while the graph was built, from the operand's shape then, which only a
  value made from a declaration has (an input, a declared variable, a constant tensor, an `iota`); on a computed value
  it was refused, "heinzel::rearrange: the left side names a different number of axes than the operand", though the
  spec was right, and `.reshape` with the extents written out was the way round (`image_pipeline`).
- *How*: the frontend records the regroup as written, a `_reshape` whose `ViewAttr` marks where each side's groups
  start (`operand_groups`, `result_groups`, a bit per label) and holds in `shape` the sizes a split names
  (`no_extent()` for the rest). `as_read` completes it once the operand has a rank its groups fit, as it completes a
  member's `x.reshape({…})`: a group of one takes its axis's extent, a symbol too; a split's unnamed factor is the
  axis's extent over the named ones, literal only; a merged axis is the product of its labels', literal only
  (`regroup_target`, `sema/view_shape.hpp`). `complete_views` writes the whole target back and clears the marks, so an
  analysed graph has none set; `graph_hash.hpp` mixes them only while set. The frontend does no regroup arithmetic.
- *The refusals* that read the operand's shape are the family's, worded where `check_time` finds a regroup still
  marked, as it refuses a member's reshape still marked. They keep the words `rearrange` used while building ("a split
  needs the operand's extent known now, not a symbol", "a merge needs its extents known now, not a symbol", "the split
  sizes do not divide the axis extent", "… do not multiply to the axis extent"), without the operation's name before
  them: `_reshape` is not what the user wrote. Not as the plan said: a sixth moved with them, a split that leaves two
  factors unnamed, so no condition is checked in two places. A spec that names a different number of axes than its
  operand has meets the rank it recorded first, "heinzel: the spec names a different number of axes than the value
  has", as before for a value whose rank was known while building; "the left side names …" is left for a regroup read
  without that rank. No text moved in `tensor_signal_test` (the plan expected some): its refusals are the spec's own,
  made while building.
- *The node does not grow*: a mask is a byte at the default rank (`LabelMask`; a word above eight axes, so
  `HEINZEL_MAX_RANK` keeps its range), and the two sit in the padding beside `elements_only`. `Node` is 368 bytes before
  and after (the plan expected 4 to 8 more).
- *Accepted graphs*: a regroup that was worked out while building is worked out from the same shape now, so its
  analysed graph is the one built before. One attribute can move: where the solver pins or merges the symbol of a
  one-label group's axis (`Signal{12, Dim{0}}` added to an input of 64 samples), the target holds the solver's extent,
  64, where it held `Dim{0}`, as `.reshape`'s target always did. The values' shapes are the same, nothing after the
  analysis reads the attribute, and no recorded hash or golden holds such a graph (a throwaway probe printed both).
- *Tests*: `rearrange_notation` (four graphs over inputs hash as they did at `2bc62564`, the split equals its target
  written; the refusals over an input and over computed values, with their words), `rearrange_reshape` (a computed
  value's split, a merge over a `Length`, a recurrence that regroups its own past, and `image_pipeline`'s windows
  merged, each analysed equal to `.reshape` with the extents written, the recurrence equal analysed twice; a computed
  value split, folded, scaled and merged on `Executor` at `High`, bit for bit as the walk runs it). The recurrence is
  held by equality only: the loop tree does not lower feedback through a reshape yet.
- *Examples*: `image_pipeline` merges each window's nine values with a second `rearrange`, `"h w a b -> h w (a b)"`,
  instead of `.reshape({kHeight - 2, kWidth - 2, 9})`; one spec that reorders and regroups at once is still refused
  (write two rearranges). Its binary, what it prints and the four images it writes are byte for byte what they were,
  and `11_einsum`'s too. `docs/architecture.md` (`notation/`, the analysis) follows. Checked: `rw-clang-26`, 373 of
  373 (no test added: the cases joined `rearrange_notation`, `rearrange_reshape` and `nest_symbolic_stream`, which runs
  a split of a sum and a merge of a product over a `Length` on the loop tree against the walk); `measure.sh` over the
  bare, bare + copy and `Executor` arms of `include/` before and after: all 88 binaries byte for byte the same.
- *The review: a regroup read where the sweep refuses.* A regroup still marked handed its operand's shape on, so a
  node reading it could be refused first, in its words, for a shape that is not the regroup's:
  `rearrange("(a b) -> a b", x, {{"a", 3}}) * y`, `x` `[8, 64]` and `y` `[3, 2, 64]`, read "shapes disagree at *:
  [8, 64] and [3, 2, 64]" (before B3, and with no reader still, "the split sizes do not divide the axis extent"). As
  the review fixed it, where the sweep refused a node that read such a regroup, or a value made from one, the
  regroup's own refusal was reported (`unfinished_regroup_read_by`, `time_checks.hpp`), worded as `check_time` words
  it: the rank its spec named, then the family's (since replaced: the second review, below). Tests in
  `rearrange_notation`: over an input, over a computed value, read through a scale, and a spec of the wrong rank.
- *The second review: a regroup still marked hands on no shape.* The first fix worded two valid graphs' refusals as
  the regroup's, one of them falsely, since a regroup still handed its operand's shape on in a sweep where it could
  not be completed yet. With the operand's rank still growing (`fed = w * v[n - 1]`, `w` `[6, 16]`, then
  `rearrange("c (a b) -> c a b", fed, {{"a", 2}}) * z`, `z` `[2, 2, 3, 16]`, merged back into `v`), it read "the spec
  names a different number of axes than the value has", where `.reshape({2, 2, 3})`, or `v` declared, was accepted;
  with a symbol the solver binds only after the first sweep (`rearrange("(a b) -> a b", x, {{"a", 4}}) * z`, `x`
  `[Dim{0}, 16]` bound to 8, `z` `[4, 2, 16]`), "a split needs the operand's extent known now, not a symbol", where
  the regroup alone was accepted. Now a regroup still marked hands on no shape (`viewed_shape`), as a recurrence's
  past does in the first sweep, so a reader neither disagrees with nor tells the solver of axes that are not the
  regroup's; `check_time` words one that cannot be completed, as it did before the first fix, which is gone. Both
  graphs are accepted and analyse equal to `.reshape` with the extents written (`rearrange_reshape`), and the four
  refusals of the first fix keep their words. At the fixpoint no regroup of an accepted graph is still marked, so the
  last sweep makes the shapes it made; only what a reader told the solver from the operand's shape in an earlier sweep
  could differ, and nothing pinned moved: every hash and golden of the suite, `measure.sh`'s 88 binaries, and the
  examples' output and binaries (but tour 15's, which analyses its graph at run time). `rw-clang-26`: 374 of 374.

**B4: statements in any order** (proposal 14; backlog B-18).
- *What*: a variable's present may be read before the statement that defines it, `line[n] = x + fb * hp` before
  `hp[n] = …`: a delay line written in the order its signal flows. Before, the read was made while `hp` was still the
  variable itself, so it stood before the recurrence that defines `hp`. The loop tree took it for a loop through the
  present ("heinzel::compile: a value defined in terms of its own PRESENT is an algebraic loop"), and the reference
  walk, which runs the nodes in the order they stand, read the variable before this sample's value was in it: silently
  wrong. Now the graph means what it means written in reading order.
- *How* (`frontend/reading_order.hpp`, the last step of `emit_arena_nodes`, so of both `finish`es): a stable
  topological order. A node waits for the node that makes what it reads; a variable read in the present waits for the
  recurrence that defines it, and a variable's past waits for nothing: that is feedback. A depth-first walk from each
  node in the order it stands places what it waits for first, its path held in a vector (constant evaluation allows
  512 frames of recursion); linear in nodes and reads. A lag (it reads the past and nothing of this sample: the rule
  table's timing column, `ir/timing.hpp`) is placed just before its first reader, after whatever else that reader
  waits for, so a lag of a variable written before its definition stands after the definition where its reader does.
  A read so moved (one that stood before what it reads; only a variable's present can) reads the value made there,
  the definition's, as a read written after the definition does. Value numbers stay (`app/heinzel/gradients.hpp`
  names values by them), and no line names an operation.
- *Not as the plan said*: a computed value's past waits for its maker too; only a variable's past is feedback. The loop
  tree reads a computed value's past only after it is made ("a node reads what it or a later node makes",
  `loops/graph/facts.hpp`), as the proof of ranges takes its range where it is made: with present reads alone, a lag of
  a value made after the read ahead (`Expr dry = x * 0.5;` and `fb[n] = dry[n - 5] * 0.7` after `y = x + 0.3 * fb`)
  stood before that value and was refused (`lag_of_a_later_value` in the test). And a moved read reads the definition
  rather than the variable: the proof of ranges takes a variable read in the present for any number, no node making it,
  so `x / (3.0 + g)` written before `g[n] = (…).promise(ge(0), le(1))` was refused (the divisor's range holds 0) where
  written after it is proven; now proven either way (with the rewrite left out, refused).
- *Accepted graphs do not move*: a graph whose nodes wait only for nodes before them is returned as built, which is
  checked first (a pass over the nodes, and one over their reads). Every graph that reads ahead was refused by the
  loop tree, so no accepted graph, golden, conservation hash or zero-overhead row moves. An algebraic loop
  (`y[n] = x + 0.5 * y[n - 0]`) is a node waiting for itself: the depth-first walk meets a node on its own path, and
  the graph keeps its order and its message. So, as built, did a cycle through a computed value's past (the review,
  below).
- *The reference walk* runs the nodes as they stand, so on a reordered graph a read before the definition reads this
  sample's value: it is the oracle for such graphs, where before it was silently wrong.
- *Left as it was*: an `Expr` taken from a variable before its definition and read after it (`Expr v0 = v;`) reads
  the variable itself, as before: any number to the proof (`docs/limitations.md`). As built, a lag of a variable
  before its definition, in a graph that reads nothing ahead, kept its place and its refusal ("a lag of a later value
  its definition does not read is not lowered yet"; the review, below).
- *Tests*: `index_notation` (B-18's graph runs on `Executor` at `High` bit for bit as the walk does and as the graph
  written in reading order; a lag and the present of a variable before its definition, and a lag of a value made
  after the read ahead, run as the walk does; the benchmark's delay in either order compiles to one program but for
  the graph value each buffer holds, and over the benchmark's block at `Normal` on zen2 gives one output, bit for
  bit; the division proven in both orders; a graph that reads nothing ahead stands as built, its nodes' values
  numbered in order: a lag before its definition (moved since, by the review), a recurrence, the coupled pair, the
  comb, an alias, the delay as the benchmark wrote it). The algebraic loops of `nest_recurrence_review` and
  `loops_plan` keep their message.
- *Benchmark*: `bench_compare_juce_heinzel.cpp`'s delay writes the line first, then its high-pass; the object file is
  byte for byte the one of the other order (both built in `rw-bench`), so its row needs no A/B, and its "defined
  after" comment goes. `docs/limitations.md`'s "a variable read at the same sample" goes: a read after the definition
  reads the definition's range (the test's division written after), and now one before it does too.
  `docs/architecture.md` (building, `frontend/`) follows. Checked: `rw-clang-26`, 373 of 373, none skipped (no test
  added: the cases joined `index_notation`); `measure.sh` over the bare, bare + copy and `Executor` arms of
  `include/` before and after: all 88 binaries byte for byte the same. The examples print and write byte for byte what
  they did; tour 15's binary, which finishes its graph at run time (`try_compile(smooth())`), carries the pass. The
  heaviest translation unit, `rewrite_parallel_form`, alone: 97.5 and 96.9 s, against 97.2 and 96.6 s before.
- *The review: a loop through a computed value's past.* `Expr r = x + 0.5 * v;` before `v[n] = 0.3 * r[n - 1]` was
  refused as an algebraic loop, which it is not: the cycle is there only because a computed value's past waits for its
  maker. Where the walk meets a value waiting for itself, it now walks once more with a computed value's past taken
  for feedback, as a variable's. Then the lag stands before what makes it, the reference walk computes the graph, and
  the loop tree, which reads a computed value's past only after it is made, refuses it in words that say what to
  write: "heinzel::compile: feedback reads the past of an Expr; make it a Variable" (72 characters; `statements_of`
  in `loops/graph/facts.hpp`, for a past read of a later node, beside "a node reads what it or a later node makes" for
  a present one, which only a graph edited by hand has). The refusal is the loop tree's, not the frontend's: the graph
  means what it says, and only the lowering lacks a lag read before its value is made (`docs/limitations.md`). A loop
  through the present stays an algebraic loop, with its message: walked again, it still waits for itself. The way
  round runs: with `r` a `Variable` it runs on `Executor` at `High` bit for bit as the walk runs the `Expr`'s graph.
- *The review: a lag before its definition, with no read ahead.* Whether one was accepted hung on an unrelated read
  ahead: alone, `Expr late = v[n - 1];` before `v[n] = x * 0.5` with `y = v + late` kept its place and its refusal;
  beside a present read ahead elsewhere, the walk put it before its first reader, after the definition, and it
  compiled. Now, once the graph is in reading order, a lag of a variable that closes a loop which nothing in that loop
  reads waits for the recurrence it closes at (`late_lags`): in each stretch the lags' loops span, as the loop tree
  reads them (`loops/graph/feedback.hpp`), what the recurrences they close read is followed back, linear in nodes and
  reads. That is exactly the lag the loop tree refuses so, so no graph accepted before moves; and a read in a graph in
  reading order reaches only back into its stretch, so waiting closes no loop. The lag goes after its definition with
  its readers (`v + late`, and a reader standing before the definition, `late * 2.0`), and a lag only another
  variable's loop reads, `u[n] = prev + 0.5 * u[n - 1]` with `Expr prev = y[n - 1];` before `y[n] = x * 2.0`, goes
  too; each runs as the walk does. `loops_plan`'s case for the refusal moves the lag back to the front by hand, as
  finishing no longer builds it. One pass decides every lag, so a chain of them is not moved whole (the second review,
  below).
- *The second review: a chain of late lags, a documented limit.* A lag needed before its definition only through
  another such lag stays where it was written: with `Expr late = v[n - 1];` (read by the output only) and `Expr prev =
  w[n - 1];` before `v[n] = x * 0.5 + prev;` and `w[n] = x * 2.0;`, and `y = v + late + w`, `prev` is read by `v`'s
  definition, which is in a loop there only through `late`; the pass moves `late`, and the loop tree refuses `prev`
  ("a lag of a later value its definition does not read is not lowered yet"), where `late` written after `v`'s
  definition compiles. Repeating the pass until nothing moves would be super-linear, which the design rules forbid,
  and the linear way, a greatest fixpoint over the reads by their strongly connected parts, is not worth building now:
  the limit and its workaround (write the lag after its definition) are in `docs/limitations.md`, the fix in the
  backlog. `index_notation` pins the refusal, so the day the chain is moved it fails, and runs the graph with the lag
  written after as the walk runs it; no code changes, and `rw-clang-26`: 374 of 374 (the cases joined
  `index_notation`). Also corrected: the loop-through-an-`Expr` message is 72 characters, not 71.

**After the close: one spec that reorders and regroups** (Gerald, 2026-10-18: "Let's do the reordering / grouping
rearrange"). Amends B3 and Decision 23, which refused it ("write it as two rearranges"): no example needed it then.
- *What*: `rearrange("h a w b -> h w (a b)", windows)`, `"(h a) (w b) -> (h w) (a b)"` (an image cut into patches)
  and `"(b c) f -> f b c"` are one spec each. Each was refused: "heinzel::rearrange: a spec that both regroups and
  reorders is not supported — write it as two rearranges".
- *How* (`lower_rearrange`, `notation/rearrange.hpp`), as einops lowers it: a split of the left side's groups into
  an axis per label, a transpose from the left side's order to the right side's, a merge into the right side's groups;
  the split only where the left side groups, the merge only where the right side does. Two of B3's regroups, recorded
  as written and worked out by the analysis, and a transpose: no new node or operation. The split takes the sizes the
  spec gives, the merge none; the spec's rank is recorded on the first node that reads the value, the split or else
  the transpose, as before.
- *Accepted graphs do not move*: a pure regroup and a pure transpose lower as they did.
- *Refusals* are the regroup's, as for any split or merge: sizes that do not divide, a merge of a symbol, a spec of
  another rank than its value.
- *Tests*: `rearrange_notation` (the patches equal to the three views written out; a split then a transpose and a
  transpose then a merge, each with only the nodes it needs; the three refusals over a computed value; the old
  refusal's case gone), `rearrange_reshape` (`image_pipeline`'s windows in one spec analysed equal to its two
  `rearrange`s; a computed value cut into patches and each patch summed, on `Executor` at `High`, bit for bit as the
  walk runs it).
- *Examples*: `image_pipeline` lines its windows up with one `rearrange`; what it prints and the four images it writes
  are byte for byte what they were. Tour 11 says one spec may do both. `docs/architecture.md` (`notation/`) follows.
- *Checked*: `rw-clang-26` 394 of 394 (no test added: the cases joined `rearrange_notation` and `rearrange_reshape`);
  `measure.sh` over the bare, bare + copy and `Executor` arms of `include/` before (`0ff40eb5`) and after: all 88
  binaries byte for byte the same; `rw-clang-23` 372, `rw-asan` and `rw-asan-O2` 370.
- *The review* (an agent at `31a9e2b9`): no real bug. The elements held bit for bit against einops' index maps
  written by hand, for seven specs over an input, computed values, a tensor, a `Length` and a recurrence's past (on
  the walk); the gradient through one, the recorded ranks and the refusals' words held too. A gap older than this
  commit, now met by more specs: a size the lowering never reads is accepted unchecked (`{{"a", 5}}` for an axis of 2
  that only a transpose or a merge reads, a letter the spec does not name); it is the backlog's open choice "unused
  `rearrange` sizes", for Gerald. A recurrence through a split, a transpose and a merge of its own past is refused by
  the loop tree by name, as a transpose of a past is. Fixed: `record_spec_rank`'s comments
  (`frontend/declarations.hpp`) name the three-node lowering. No second round.

## Decision 77 — Plan 28 package C: messages and looking inside (`Plan/28-dsl-proposals/04`)

**Decided** (Gerald, 2026-10-16: "looks all good, apply the proposals", `Plan/27-examples/dsl-proposals.md`). Package
C makes what the build says of a wrong graph, and what a program can show of what it compiled, readable: one error for
a graph function that stops (proposal 8), the loop tree's refusal naming the operation (9) and `print<g>()` (7). No
spelling changes. Each step of the package adds its section here, and the review its findings.

**C1: a failure inside the graph function is one error** (proposal 8).
- *What*: a graph function that stopped while it ran (a frontend `fail`, as for an `Output` assigned twice; a graph over
  a capacity; the constant-evaluation step limit, reached while it ran) stopped the build with about 20 errors in some
  340 lines (`reverb` without its raised capacities: 20 in 341), the first "constexpr variable 'result' must be
  initialized by a constant expression", the reason only in a note under it: `fail` throws, a throw is no constant
  expression, and each member of `Executor` read off `result` failed again. Now it is one error. A graph over a capacity
  is the analysis's refusal, the capacity's words the error's message (C++26; on C++23 the floor's fixed text, as for
  every refusal). Any other stop is "static assertion expression is not an integral constant expression", whose notes
  give `fail`'s words (`in call to 'fail({35, &"heinzel: an Output is assigned once"[0]})'`) and each call down to the
  user's line, on C++23 and C++26 alike. The step and the depth limit count each constant evaluation on its own, and
  `built`'s is the graph function's alone: either limit reached inside `try_compile`, while it plans, still gives the
  cascade (the review: `Executor<smooth>`, tour 15's graph, under `-fconstexpr-steps=150000` 20 errors in 362 lines,
  under `-fconstexpr-depth=20` 20 in 360; under `-fconstexpr-depth=12`, which the graph function itself reaches, one
  error in 52).
- *How, part 1* (`executor.hpp`): `Executor` first asks whether the graph function runs to its end, `built = requires
  { typename std::bool_constant<(Build(), true)>; }`, false where it is no constant expression: a substitution
  failure, never a hard error. Where it is false, `result` is a refusal ("heinzel::Executor: the graph function
  stopped while it ran; the error below says why", 84 characters), so every member read off it computes, and
  `checks()` runs the function once more, `if constexpr (!built) static_assert((Build(), true));`, the one error; the
  refusal's own `static_assert` holds `!built || !refused`, so it is not a second.
- *How, part 2* (`frontend/graph_builder.hpp`, `ir/ir.hpp`): finishing counts before it emits (`capacity_passed`: a
  value for each build node, a node for each operation, the inputs, the constants' numbers, the ranges stated). Over a
  capacity the graph holds only `Graph::over`, which one (`Capacity`, a byte), whose words `words_of` gives: the
  literals `push_value` and the rest still fail with where `gradients`, the rewrite or a graph built by hand adds past
  one (`kOverValues`, …, `ir/ir.hpp`). Not the spec's `string_view` of the literal: that would make `Graph` no
  structural type, and a whole graph is a template argument (`graph_value_test`). `finish` and `gradients` hand it on
  as it is, and `infer` refuses it first. `compile` fails with `words_of(g.over)` itself, not with `infer`'s copy,
  since clang's note prints a `string_view` of a literal as the literal and one of a buffer as an address: so
  `tests/gates/refusals/capacity.cpp` reads the words on C++23 too.
  At run time a graph function over a capacity now returns where it threw, and `try_compile` and `compile` refuse its
  graph in the same words. The graph hash folds `over` in only when it is set, so no pinned hash moves.
- *The probe* (`Plan/probes/28-dsl-proposals/graph-fails/`, clang++-22): `built` is false for a `fail`, a capacity
  overrun and the step limit, at `-std=c++23` and `-std=c++2c`; the `static_assert` route is one error with the same
  text at both, the step limit reading "constexpr evaluation hit maximum step limit; possible infinite loop?" at the
  user's loop; the class in this shape gives one error in 16–31 lines where the old shape gave five in 85–100.
- *The cost*: an `Executor` runs its graph function twice while compiling (three times where it stops). Measured,
  user seconds of the example's compile with rw-clang-26's flags, five runs interleaved against the old headers:
  `reverb` 8.53 → 8.72 s (median, +0.19 s, +2.2 %), `transformer_block` 5.16 → 5.24 s (+0.08 s, +1.6 %); alone, one
  run of `reverb`'s `room()` takes 0.19 s and of `block()` 0.055 s, so the growth is that run. Accepted: the plan
  foresaw it and had the probe measure it, the price of one error instead of 20, and a compile-time cost only. No route
  runs the function once: a failure can be tested only where it is a substitution failure (a `requires`), which hands
  out a yes or no, or a small number, never the result (197 KB as a template argument would be a grown class-type
  NTTP, which the design rules forbid), and a `const` variable of class type that is not `constexpr` cannot be read in
  a constant expression. A constexpr `try`/`catch` would run it once; clang 22 defines no `__cpp_constexpr_exceptions`
  (the backlog).
- *Left*: a `fail` inside `try_compile`, not in the graph function, still fails each member read off the result (a
  debug build's checks of promises over `kMaxIO`, a parallel form's constants over `kMaxConstants`), and so does the
  step or depth limit reached while `try_compile` plans, which is no throw, so a constexpr `try` would not catch it;
  the same backlog line.
- *Tests*: `build_refuses_executor_over_a_capacity` (the comb under `-DHEINZEL_MAX_VALUES=4`, the capacity's words,
  C++26, the fixed text, C++23), `_output_assigned_twice` and `_graph_function_fails` (a graph function's own `fail`),
  each "1 error generated" with the reason, `fail`'s literal in its note for the two; `_a_gradient_named_hash` (case
  21, a gradient's `fail`) is now an `Executor` of the graph, which its comment said would repeat the error in each
  member. The four forbid "initializer of 'result' is not a constant expression" and "must be initialized by a
  constant expression" (`FAIL_REGULAR_EXPRESSION`). `capacity.cpp` unchanged, its words read through `compile`'s
  note at both standards; `constant_tensor`'s pool over its capacity, at run time, is a graph that says so, which
  `infer` refuses in the words, where it threw.
- *Docs*: `executor.hpp`'s comment, `docs/limitations.md` ("Capacities": where each is counted, one error),
  `docs/architecture.md` (`Executor`), tour 15's last bullet, `examples/CMakeLists.txt`'s line on Freeverb's
  capacities, the backlog's line, plan 04's C1 (its count as measured, and as built); `examples/README.md` names no
  count and is unchanged.
- *Checked*: `rw-clang-26`, 377 of 377 (three tests added), and `rw-clang-23`, 355 of 355, none skipped;
  `measure.sh` over the bare, bare + copy and `Executor` arms of `include/` before (`44aa81bc`) and after: all 88
  binaries byte for byte the same (`built` and `result` are read only while compiling); the examples print and write
  byte for byte what they did. With both parts built, the compile time again, five interleaved runs: `reverb` 8.51 →
  8.70 s (+0.19 s), `transformer_block` 5.18 → 5.24 s (+0.06 s).

**C2: the loop tree's refusal names the operation** (proposal 9).
- *What*: a node no stage of the loop tree runs was refused in words that named neither the operation nor where:
  an aligned `einsum` (`"ij,ij->i"`) and `cos` said "heinzel::compile: the graph holds an operation the nest path does
  not lower yet", a sort, a top-k, an even median and an interpolated quantile "…an order statistic that is not one
  exact rank is not lowered yet". Now the refusal names the operation as it was written, then the case, then its
  operands' shapes as the analysis recorded them, last, so that a message cut at its 95 characters (`MessageWriter`
  cuts silently) loses shapes, never the reason: "heinzel::compile: einsum is not lowered yet: [64, 32, 1] and
  [64, 32, 1]", "heinzel::compile: _cos is not lowered yet: [?0]", "heinzel::compile: topk 3 is not one exact rank,
  which is not lowered yet: [6, 64]", "heinzel::compile: unfold: a window over no element axis, or with a hop below
  one: [64]" (a framed stream), "heinzel::compile: einsum: a contraction of a constant or a window is not lowered yet:
  [4, 3, 64" (cut). No source line: every value of a graph would hold one, and equal graphs would differ. No spelling
  changes.
- *How* (`loops/planner/stages.hpp`): `gather_groups` words a node's refusal where it has the node (`worded`):
  "heinzel::compile: ", the operation (`write_node`, `ir/op_rules.hpp`, beside `shown_name`: `write_op`'s words, and
  an order statistic, one code for four spellings, by its attribute's member: "sort", "topk 3", "median", "quantile
  0.25"; which node is one comes from the rule table's attribute column, no code is named), the case
  `why_it_cannot_run` gives, and the operands' shapes (`write_shape`, moved with `write_extent` from
  `sema/refusal.hpp` to `ir/vocab.hpp`: `loops/` may not include `sema/`). A case is a sentence of its own, which
  follows after a colon (the views', which the lowering shares, and the other planner cases'), or the words that go
  on from the operation's name: " is not lowered yet", the nest path's, and " is not one exact rank, which is not
  lowered yet", the order statistics', reworded so that "quantile 0.25" keeps its reason. Two views' sentences are
  shortened for the same reason (they are the lowering's too, which a schedule by hand reaches): "a window over no
  element axis, or with a hop below one" (was "…over an axis that is no element axis, …", 96 characters with the
  operation) and "new extents, a symbolic one not outermost, have no address yet" (was 82 characters, cut at 95
  already before C2).
- *`loops::Refusal`* (`loops/program.hpp`) was a `string_view`. It is now a literal, kept as a view of it, or a text
  it holds (`fixed_string<96>`), which only `worded` makes; it reads as at most 95 characters (`view()`, `empty()`,
  `data()` and `size()`, which a computed `static_assert` message and `%.*s` read), and `==` with a text compares the
  95 characters it holds of it, so the lowering's literals longer than that, which tests pin whole, compare as before
  (and as `try_compile` and `fail` cut them). It converts to a `string_view` only by `view()`: a view of a
  temporary refusal cannot be taken unseen (eighteen test sites that returned or kept one now keep the refusal), and
  `fail` takes `view()`.
- *Departure from the spec*: it planned one owning text for every refusal. Built so, first, the loop tree's six
  capacity refusals ("…more statements than kMaxStmts; rebuild with -DHEINZEL_MAX_STMTS=N", tree nodes,
  buffers, loop nests, a stage's statements, a cycle), which `compile` fails with in a constant expression, showed
  in clang's note as an address (`fail({87, &p.planned…_M_unex.text.data_[0]})`), no longer as their words, and
  `build_refuses_capacity_*` failed: at C++23 no computed message can be shown at all. So a literal stays a literal,
  as before C2, and its words stay in the note; only a node's refusal, assembled, is a held text, and `compile`'s
  note shows it as an address, as the analysis's messages (`compile.hpp`'s comment says so). The cost is 16 bytes:
  a refusal is 112 bytes, not 96 (it was 16). `empty()`, which passes ask in loops (`address_over` once an axis),
  reads the two members, not `view()`: through `view()` it cost an example's heaviest evaluation 1–1.7 % more steps.
- *Sizes*: `Program` 118,376 B, `CompileResult` 197,048 B and `Planning` 207,296 B are unchanged: a `Program` holds no
  refusal, a `CompileResult` held its 96-byte text already, and a plan's `std::expected` is as large as the plan. A
  refusal holds a text only where a node is refused, and a literal costs what it cost. Constant-evaluation steps,
  the fewest each example's translation unit compiles under (its heaviest evaluation, bisected to 0.1 %):
  `transformer_block` 943,616 → 945,664 (+0.2 %), `reverb` 6,766,592 → 6,787,072 (+0.3 %), tour 15 299,520 →
  299,776 (+0.1 %).
  Compile time of `rewrite_parallel_form`, the heaviest test, no ccache, three runs interleaved: 97.71 → 97.88 s (median, user; 97.34–98.15 before, 97.65–97.96
  after: noise), its peak memory 577 → 570 MB.
- *Tests*: `loops_plan_test` pins the aligned `einsum` (the spec's text, verbatim), `sort`, `topk 3` and `quantile
  0.25` over `[6, 64]`, and a quantile over five axes whose shape is cut at 95 characters after its reason
  (`kCut`, held exactly to its first 95); `build_refuses_executor_not_lowered` (case 25 of `executor.cpp`) builds an
  `Executor` of the aligned `einsum`, one error whose message, at C++26, is the refusal (the floor's fixed text at
  C++23). Moved: `loops_plan_test`'s sliding median and cases 0–4 and 12, `compile_pipeline_test`'s long refusal
  (now 105 characters, its shape cut), `nest_constant_views_test`, the agreement corpus's prefixes
  (`agreement_graphs.hpp`, each order statistic by its own word), `parity/contract_here.cpp` ("einsum is not lowered
  yet: [3, 64] and [3, 64]") and `parity/mlp_train_here.cpp` ("…: [16, 4, 1, 1] and [1, 4, 8, 1]").
  `loops_lower_test`'s order statistic is the lowering's own literal, for a schedule by hand, and stays.
- *Docs*: `examples/README.md` ("What does not run yet": the operation and its shapes), `docs/architecture.md` (the
  lowering's refusals, the transcendentals' message, `compile`'s note), `compile.hpp`'s comment, plan 04's C2 as
  built, pointers moved in plans 04, 05 and 07; `docs/limitations.md` quotes none of the old words and is unchanged.
- *Left*: `_reduce` is shown as itself (`x.sum(1)`, `einsum` and `reduce` write it), though its member could name it
  as an order statistic's does ("sum"); `_tblread` has no spelling before package D; the analysis's refusals still
  name an order statistic `_order` (`write_op`).
- *Checked*: `rw-clang-26`, 378 of 378 (one test added), none skipped; the new case at the C++23 floor alone
  (`rw-clang-23`'s `executor_not_lowered`: one error, the fixed text); `measure.sh` over the bare, bare + copy and
  `Executor` arms of `include/` before (`f7988695`) and after: all 88 binaries byte for byte the same (a refusal is
  read only while compiling); the examples print and write byte for byte what they did.

**C3: `print<g>()`** (proposal 7).
- *What*: to look at what an `Executor` runs, a program compiled the graph again at run time with `try_compile`,
  held its 197,048-B `CompileResult` on the heap and printed its program with `loops::print`, an internal (tour 15);
  `try_compile` kept no schedule, and nothing printed a graph, though the schedule names its nodes (`s1 = n8`) and so
  do the program's statements (`node 8`). Now `print<smooth>()` returns, as text, the graph as planned, the schedule
  and the program; it takes `Executor`'s template head, `template <auto Build, const Machine& M = default_machine, bool
  Checked = detail::kChecked>`, so `print<smooth, zen2>()` shows what `Executor<smooth, zen2>` runs, and a checked
  build's checks with it (`output #0 = v2`). A refused graph prints its refusal, and a graph function that stops while
  it runs the error it threw ("heinzel: an Output is assigned once"), each with a newline. Its own header,
  `app/heinzel/print.hpp`, which `dsl.hpp` does not include; `Executor` never names it. The one new spelling.
- *The graph listing*, decided here: one line a node, `n8 v11 = tanh v10 [256] f32`, the node's number, the value it
  makes, its operation as the user wrote it (`write_node`, as C2's refusals name it: `tanh`, `topk 3`, `_fdelay`),
  its operands, a constant as its number (`0.1 * v4`), and the value's shape and type as the analysis gave them
  (`write_shape`, `[64, 32, 1]`). An operation of one character stands between its two operands (`v3 - v2`); a lag
  and the variable a recurrence defines, which no operand shows, follow the operands (`_fdelay v0 back 1`, `_recur v7
  defines v0`). The inputs come first (`v3 = input x [256] f32`, a Param `[]`), the outputs last (`output y = v11`).
  Sections are headed `-- graph`, `-- schedule` and `-- program`, as the goldens head theirs.
- *How* (`compile.hpp`): `try_compile`'s steps are one function, `detail::attempt` (the analysis, the proof of
  ranges, the plan as rewritten), which `try_compile` and `print` take. It returns the refusal, the ranges an
  `Executor` checks, and the plan, a `Planning`, held by a `std::unique_ptr` (constexpr since C++23):
  `new Planning(planned_as_rewritten(g, machine))`, the call's result the heap's object, under constant evaluation
  freed before `try_compile` returns. `print` holds the graph on the heap too (`new Graph(Build())`), runs the steps
  inside one `try` (`heinzel::error`; under `-fno-exceptions` a failure traps, as `fail` does) and prints the
  plan's three listings, the schedule and the program with `loops::print`.
- *No 200 KB through the stack* (the spec's check). `detail::plan` assigned each plan from `loops::plan_and_lower`'s
  returned value, a 133,728-B `std::expected<Planned, Refusal>` temporary in its frame; now
  `loops::plan_and_lower_into` makes it in the `Planning` itself (`emplace`, then the schedule and the program in
  place; `plan_and_lower` stays, returning one, for the tests). `try_compile` held its 207,296-B `Planning` in its
  frame and assigned `Compiled{p.graph, p.planned->program}`, a 191,944-B temporary; now it copies the graph and the
  program member by member from the heap. Frames by `-fstack-usage`, clang 22 at `-O0`: `try_compile` 400,168 →
  5,768 B, `detail::plan` 267,496 → 40 B; `attempt` 536 B, `planned_as_rewritten` 280 B (its `p` is the caller's
  object), `print` 152 B and its body 5,608 B (the ranges). The deepest frames are the rewrite's own (`widen`
  221,880 B), unchanged. Tour 15 now runs under 602 KB of stack, where it needed 1,137 KB (`ulimit -s`, bisected).
  `compile` is unchanged and still holds its `Planning` in its frame: it fails with a refusal's literal, which
  clang's note shows by its words, and does not take `attempt`'s copy.
- *Departures*: not in the spec, the lag and the recurrence's variable on a node's line, and the member-by-member copy
  in `try_compile`. Tour 15 keeps `try_compile` for its `static_assert` on `mismatched()`, a refusal stated in words
  while compiling, which `print` cannot do. `docs/limitations.md`'s size of a `Compiled`, 178 KB, was stale: 192 KB.
- *Left*: the other attributes are not shown (a reduce's or a contraction's axes, a view's kind and numbers, a
  table's, an FFT's direction), nor a value's precision, which the program's statements show (`normal`); in
  `loops::print`'s listings a stage's statements (`s0`) and the schedule's stages (`s0 = …`) share a letter, and the
  program's tree nodes (`n0`) and the graph's nodes do, as before; tour 15's header says that the program's are its
  own (the review).
- *Tests*: `app_print` (`tests/app/print_test.cpp`): `print<smooth>()` equals the three prints of the compile
  `try_compile` makes while the test compiles, in `print`'s layout (its graph listing, `loops::planner::schedule_of` of
  the graph as planned, the program), its graph section equal to a pinned listing; on `zen2` too; a graph with a
  promise the proof cannot prove, checked and not, each its own, `output #0` in the checked one only; the analysis's
  refusal (`mismatched`, equal to `try_compile`'s), the loop tree's (the aligned `einsum`, C2's words), and an `Output`
  assigned twice, its `fail`'s words. `public_spelling` refuses `loops::print` in the files a user reads (its fixture:
  42 offences, a look-alike `my_loops::print`).
- *Docs*: tour 15 (one `print<smooth>()` call, its header), `examples/README.md` (tour 15), `docs/limitations.md` ("A
  compile result read at run time"), `docs/architecture.md` (`compile.hpp`'s steps, `print`, `plan_and_lower_into`,
  `app/`), plan 04's C3 as built, the pointer in plan 05 moved.
- *Cost*: compile time, five runs interleaved against the old headers, user seconds with rw-clang-26's flags: tour 15
  4.22 → 4.25 s (median; it compiles `print`'s run-time pipeline where it compiled `try_compile`'s, and its three
  `Executor`s as before), peak memory 280 → 275 MB; `reverb`, whose `Executor` takes the shared steps while
  compiling, 8.72 → 8.74 s, 293 → 294 MB: noise.
- *Checked*: `rw-clang-26`, 379 of 379 (one test added), none skipped; `measure.sh` over the bare, bare + copy and
  `Executor` arms of `include/` before (`7ae87b99`) and after: all 88 binaries byte for byte the same (`attempt`,
  its heap and `plan_and_lower_into` run only while compiling); the examples print and write byte for byte what
  they did but tour 15, whose program listing is the one it printed, now after the graph and the schedule.
- *The review: a graph `Executor` refuses past `try_compile`.* `print` took `try_compile`'s steps only, but
  `Executor` refuses two more kinds of graph in `checks()`: a second symbol, or one off the time axis
  ("heinzel::Executor: at most one symbol, on the time axis"), and state that does not run along the stream
  ("heinzel::Executor: this graph's state does not run along the stream"). `symbol_off_time` (`Signal{Dim{0}, 4}`)
  and `one_sample_recurrence` (one-sample state beside a 16-sample signal), cases 12 and 15 of
  `tests/gates/refusals/executor.cpp`, pass `try_compile`, so `print` listed a graph, a schedule and a program for
  each, where an `Executor` of either is a build error. Both checks are now one function, `detail::why_not_run`
  (`ports.hpp`), the words or nothing, which `Executor` and `print` call, so the two cannot drift: `print` prints
  `Executor`'s words, with a newline. `Executor`'s errors are as they were, one each: at C++26 one `static_assert`
  whose message is the function's words, on the C++23 floor the two literals, each asserted against its words'
  constant (`kOffTime`, `kOffStream`), since a floor message must be a literal. `app_print` prints both cases in
  those words, `try_compile` passing each (asserted). Checked: `rw-clang-26`, 379 of 379, none skipped; at the C++23
  floor (`rw-clang-23`) `app_print` and the 25 `build_refuses_executor_*`; `measure.sh` before (`180a5ebc`) and
  after: all 88 binaries byte for byte the same; the examples print and write byte for byte what they did.

## Decision 78 — Plan 28 package D: the engine (`Plan/28-dsl-proposals/05`)

**Decided** (Gerald, 2026-10-16: "looks all good, apply the proposals", `Plan/27-examples/dsl-proposals.md`). Package
D makes the loop tree run what the analysis already knows: an aligned `einsum` (proposal 10, first half), `cos`, `tan`,
`pow` and `exp2` (4), tables and a variable delay (16), gradients through a matrix product (19, with B-5) and a tensor
contracted with a signal (10, second half). Each transform ships with its eligibility rule, the walk as its oracle and
a benchmark. Each step of the package adds its section here, and the review its findings. Gerald, 2026-10-17: the
gradients through a matrix product (D5, with B-5) and the tensor contracted with a signal (D6) move to the backlog
(B-4, B-5, B-16); the package is D1–D4.

**D1: an aligned `einsum` is a multiply and a sum** (proposal 10, first half).
- *What*: an `einsum` whose two operands carry the same labels in the same order (`"bf,bf->b"`) built `_contr`, which
  the loop tree does not lower: the build stopped with "heinzel::compile: einsum is not lowered yet: [64, 32, 1] and
  [64, 32, 1]", and tour 11 told the user to write `(a * b).sum(1)`. Now `einsum` builds that graph, node for node,
  and it runs. No spelling changes.
- *How* (`notation/einsum.hpp`): `lower_elementwise` builds the product, `a * b`, then `detail::reduce` summing the
  labels the output leaves out (none: the product alone), then the transpose as before: the notation choosing its
  nodes, as `lower_unary` builds `_reduce`; no pass, no rule column. The eligibility rule is the notation's own: the
  two operands' labels equal, in order (`elementwise_aligned`); any other pairing is one `_matmul`, as before.
  `record_spec_rank` (`frontend/declarations.hpp`), which writes the ranks a spec named on the first node that reads
  each operand, now walks three nodes back from the result, a transpose, the sum and the product, where it walked two.
- *Not in the spec*: the frontend's `detail::contract` (`frontend/ops.hpp`), which nothing calls now, is deleted;
  `_contr` stays, its row, adjoint and walk, for the graphs built by hand that hold it, and its retirement is a backlog
  line. The rule table's `spelled` column for `_contr` is empty, as the column says for an operation no spelling
  builds, so a refusal names it `_contr`, not `einsum` (`inference_test`'s contraction built by hand: "shapes disagree
  at _contr: [4, 8] and [4, 3]").
- *What it runs*: `print<g, zen2>()` of `"bf,bf->b"` over `[256, 256]` is one stage, the product mapped inside the
  fold along `f` in 8 lanes, no buffer for the product.
- *At High* each product is now rounded to its f32 result and the products summed in double, rounded once, where
  `_contr` summed the exact products. The parity allowances against `main`, row by row:
  - `contract`: 3.0e-08 → 1.91e-06. 16 of 64 samples differ (2 before), each by exactly one f32 step at its
    magnitude, the largest 2^-19 at 26.46. Hand references reproduce all 64 samples of each side: here each product
    rounded, then one rounding of their sum; `main` a fused multiply-add per term in f32, each product exact and each
    partial sum rounded. The old note's "rounds each product and each partial sum" for `main` reproduces 48 of its 64;
    corrected. The pair was the walk of a `_contr` built by hand; it is now written in the language (`einsum("c,c->",
    spread, doubled)`, the halves a constant tensor) and runs on `Executor`, the walk's numbers (`executed`).
  - `einsum_contractions`: unchanged at 0.0. Its two graphs are `_matmul`s; its output is byte for byte as before.
  - `mlp_train`: unchanged at 3.0e-08. 4 of 6 samples differ (5 before), each by one f32 step, the largest still
    sample 0's 2.98e-08, the loss before any update; sample 1 now agrees, sample 5's gap is one step where it was two.
    The graph ran on the walk alone; now on `Executor`, every one of its 601 runs checked against the walk bit for bit
    (as `fir_fit_here`), its host in float. Its program runs 30 loop nests where `kMaxStages` is 16, since `gradients`
    sweeps once for each of its four targets (B-5), so its target is built with `-DHEINZEL_MAX_STAGES=32`.
- *Moved*: C2 and C3 pinned the aligned `einsum` as the node no stage runs. `loops_plan_test`'s now plans, and a
  `_contr` built by hand keeps the two-operand words ("heinzel::compile: _contr is not lowered yet: [64, 32, 1] and
  [64, 32, 1]"); `build_refuses_executor_not_lowered` (case 25) and `app_print` take a top-k ("heinzel::compile: topk 3
  is not one exact rank, which is not lowered yet: [6, 64]"; one error, the refusal at C++26, the floor's fixed text at
  C++23), which no step of package D lowers.
- *Tests*: the oracle, `notation_test`: `einsum("c,c->", …)`, `"bf,bf->b"` and `"bfc,bfc->cb"`, analysed, equal the
  graphs of `(a * b).sum(0)`, `(a * b).sum(1)` and the latter turned (`detail::transpose`), one `_mul`, one `_reduce`
  and no `_contr`; the walk, through the two parity pairs above; `tensor_signal_notation_test` cases 11 and 12, a
  computed operand's rank named wrong refused past the sum and the transpose, named right not.
- *Benchmark* (`rowdot/{heinzel,naive,eigen}`, `bench_contraction.cpp`; zen2, core 4, 7 repetitions, the medians of
  two runs): 64×64 222 and 213 ns against Eigen's 238 and 232 and naive's 2,105 and 2,094; 256×256 5,050 and
  4,832 ns against 4,888 and 4,635 and 44,120 and 44,339; 1024×1024 89.6 and 85.1 µs against 90.0 and 86.0 and 743
  and 740. So Heinzel runs as Eigen does (7 % faster at 64×64, 3–4 % slower at 256×256, the same at 1024×1024, the
  last two bound by the caches and memory), and 9.5–9.9 times the naive loop, whose float sum the compiler may not
  reassociate. Not gated: the bench gate measures its baseline's rows, which hold none for it; the step adds a
  capability, not a speed transform.
- *Left*: a mismatched aligned `einsum` is refused at the multiply it is built as, "shapes disagree at *: [64, 32, 1]
  and [64, 31, 1]", where it said "…at einsum: …": the product is an ordinary `_mul`, and naming the spelling would
  need the node to carry it, which no other composition does. Retiring `_contr` (the backlog); `mlp_train`'s raised
  capacity until B-5 sweeps once.
- *Docs*: tour 11's caveat, `examples/README.md` ("What does not run yet": the aligned `einsum`'s line gone, the example
  refusal a framed stream's), `docs/architecture.md` (the loop tree's refusal, `notation/`), the comments in
  `einsum.hpp`, `declarations.hpp`, `op_rules.hpp`, `autodiff/gradients.hpp` and the CMake notes of the two pairs,
  the backlog (ticked; `_contr`'s retirement), plan 05's D1 as built, pointers moved in plans 05 and 06;
  `docs/limitations.md` describes no aligned `einsum` (its M5 board row on `einsum`'s diagonal is history).
- *Checked*: `rw-clang-26`, 379 of 379, none skipped; `measure.sh` over the bare, bare + copy and `Executor` arms of
  `include/` before (`366f5f9c`) and after: all 88 binaries byte for byte the same (no measured row holds an aligned
  `einsum`); the examples print and write byte for byte what they did.

**D2, step 1: `cos`, `tan`, `pow` and `exp2` run, on their double cores** (proposal 4, second half; step 2, a float
core for `cos`, follows).
- *What*: the loop tree ran an approximate operation only where it had a float core (`exp`, `tanh`, `sinpi`, `sin`,
  `log`), at every precision, and none of the four was spelled: a graph holding one, built by its code, stopped the
  build ("heinzel::compile: _cos is not lowered yet: [64]"). Now they are spelled `cos(x)`, `tan(x)`, `exp2(x)` and
  `pow(x, y)`, `pow` also with a number for either operand (`pow(x, 2.5)`, `pow(10.0, y)`), as `max` and `min` take
  one, and they run. Spelling them names them in a refusal as written: the four rows' `spelled` column.
- *Eligibility*: an `Approximate` elementwise operation is a plain map where the rule table says it has a writing over
  the value type (`has_a_writing_over_the_value_type`, `ir/op_rules.hpp`: the float-core list, now
  `has_a_core_in_the_values_own_type`'s, plus the four), which `is_a_plain_map` (`loops/graph/kinds.hpp`) asks. Every
  other transcendental (`asin`, `acos`, `atan`, `atan2`, `log2`, `log10` as operations) is refused as before.
- *How* (`runtime/compute.hpp`): a family of the four, `transcendental_in_double_as(op, fn, a, b)`, whose two operands
  carry `pow`'s. Its `*_over` functions (`runtime/float_cores.hpp`, beside the float cores' choice) run the double
  core through `EachNumber<V>::of`, one number at a time, and a two-number `of` beside the one-number one
  (`value_type.hpp`, `lane_value.hpp`) gives `pow` its pairs. A float is widened, its core run at the tier its level
  grants, the answer rounded back: what the walk computes for an F32 value, so the two agree bit for bit at High,
  Normal and Fast. The static check splits by predicate: the float-core list against the float cores, the writing
  list against both families, each its own `static_assert`.
- *Measured first, then reached from `transcendental_as`*: the family first sat beside `transcendental_as` in
  `compute_as`'s chain, one more call that folds away with the operation. That alone, even with its body emptied, moved
  W5's code (the FIR, which holds no transcendental) in every arm: its run 1,820 → 1,941 B, 8,015 → 8,293 instructions a
  run (3.5 %), its `-Os` `Executor` 5,260 → 5,402 B, its checked one 5,989 → 5,993; the program was the same, the code
  the optimizer made of it not. Reached instead from `transcendental_as`'s end (its last line hands the operation on,
  and it takes `b` now), `compute_as` makes as many calls as before and every binary is as it was: `measure.sh` over the
  bare, bare + copy and `Executor` arms of `include/` before (`5572d96b`) and after, all 88 byte for byte the same, and
  the 33 `-Os`, `-O1` and checked `Executor`s of their sizes before. The `-Os` dispatch the backlog feared (every
  operation a call out of a switch, 14–17 KB) was gone already: `cbad46fb` forced `compute_as` and its families inline
  where the build optimizes (2026-10-08). The `-Os` `Executor`s are 3,136–7,574 B, none holds `compute_as`, so the
  operation as a template argument was not needed; the backlog line is ticked.
- *Reversed*: the recorded rule said these were "declined rather than widened": under `Normal` a float is computed in
  float, and a double core on widened lanes would be slower and more accurate than the code it is compared against
  (`op_rules.hpp`, `value_type.hpp`, `docs/architecture.md`). Gerald put the capability first
  (`Plan/16-simplify/08-Later.md`: none of these may go, first on the double cores): they run, slowly, and a float core
  is written where a benchmark asks for one, `cos`'s next. The comments and the architecture's paragraph say so.
- *A named stand-in*: the cost model prices `Latency::Core` as a float core in a float (`schedule/cost.hpp`, the
  comment there), though the four's double cores cost many times that lane by lane: a graph holding one plans
  correctly, its schedule not chosen by its real cost. It stays until a float core or a benchmark asks.
- *The proof's words*: `pow`'s base must be kept at or above 0 (above 0 where the exponent may be negative); a base it
  cannot keep there is refused in `pow`'s name, `Executor` of `pow(x, 2.0)` over an undeclared `x` one error,
  "heinzel: pow may see a negative base: the base is any number" (`print<g>()` says the same). The spelling's comment
  and `docs/limitations.md` name the remedy: a range on the base, or a signed value squared as `x * x`. The fixture
  `build_refuses_proof_a_pow_of_a_negative_base` (x in [-1, 1]: "…the base is ge(-1), le(1)"); `ranges_walk_test`'s
  pinned words read `tan` and `pow` where they read `_tan` and `_pow`.
- *What it runs*: `print<g, zen2>()` of `cos(x)` over `Signal{256}` f32 is one stage in 8 lanes, `map f32 _cos`, each
  lane's numbers through the double core.
- *Tests*: `cos_tan_pow_exp2`: the spellings build their operations' graphs, graph for graph; each of the four on
  `Executor` at High, Normal and Fast, over f32 and f64, on the default machine and on zen2, 4,096 numbers equal to the
  walk's bit for bit (`cos` over [-10, 10] and magnitudes 2^-30 to 2^30, the zeros, 1e9, ±2^41, past 2^40 a NaN,
  and the infinities; `exp2` from -1,100 to 1,100, the subnormal results and the overflow of either type; `tan` to
  its branch's edges; `pow` of bases 2^-40 to 2^40 to exponents in [-30, 30]): none of the 196,608 differs. And
  `sin`'s gradient, whose adjoint emits `cos`, runs on `Executor`: dloss/dw of mean_k sin(w x_k) at High equal to the
  walk, bit for bit (`testing::executed`), within 1.6e-11 of the central difference and the closed form's to the 17
  digits printed. `tiers_test` runs `cos`, `exp2` and `pow` on both
  runners at Fast and High (`tan`'s input there crosses a pole).
- *Moved*: the refusals pinned to `_cos` take an arc tangent, which has no writing: `loops_plan_test`'s case 0
  ("heinzel::compile: _atan is not lowered yet: [?0]") and `nest_transcendental`'s refusal by name, where a cosine
  now lowers; `loops_plan_test` plans each of the four.
- *Benchmark* (`cos/{heinzel,naive,eigen}` in `bench_transcendental.cpp` beside `sin`'s rows; zen2, core 4, 7
  repetitions, the medians of two runs): `cos/heinzel` 48.4 and 48.5 µs at 4,096, 3.12 and 3.11 ms at 262,144, 12.5
  and 12.5 ms at 1,048,576 (84 M a second), against `std::cos` per float 11.7 µs, 758 and 753 µs, 3.03 and 3.01 ms
  (348 M a second) and Eigen's 2.6 µs, 169 and 168 µs, 674 and 677 µs (1.55 G a second); `sin/heinzel`'s float
  core 3.9 µs, 250 µs, 1.00 ms. So the double core lane by lane is 4.1 times slower than libm's `cosf` and 12.5
  times slower than the float sine: the capability, not a speed claim, and the row step 2 moves. Its error against
  the float64 answer equals `std::cos`'s, 5.75e-8. Not gated: no baseline row.
- *Not in the spec*: `pow(double, Expr)` beside `pow(Expr, double)` (the neighbours take a number on either side; dB to
  a gain is `pow(10.0, db / 20.0)`; approved by Gerald, 2026-10-17); tour 02 lists the math spelled (`exp2`, `log10`,
  `cos`, `tan`, `cospi`, `pow` added). No example changed its spelling: the benchmarks' sweeping EQ (`eq8sweep`) keeps
  `sinpi(w/π + 1/2)` and `exp(v ln 2)`, which run on float cores, where `cos` and `exp2` would run their double cores in
  a benchmarked row and round otherwise; `mixing_console`'s pan, `sinpi(0.5 * (1 - pan))`, is the half-turn form, and a
  `cos` there would change `console.wav`.
- *Left*: step 2, a float core for `cos`; float cores for `tan`, `pow` and `exp2` where a benchmark asks; the cost
  model's stand-in; `asin`, `acos`, `atan`, `atan2`, `log2` and `log10` as operations (the backlog).
- *Docs*: `docs/architecture.md` (the Numerics section), `docs/limitations.md` (the proof's table, its conservative
  domains), `examples/README.md` (tour 02, "What does not run yet": `sin`'s gradient runs, the transcendentals not
  spelled), tour 02's header, the comments in `op_rules.hpp`, `value_type.hpp`, `compute.hpp`, `float_cores.hpp`,
  `kinds.hpp`, `cost.hpp` and `graph_builder.hpp`, the backlog (the transcendentals' line, `sin`'s half of the
  gradients' line, the `-Os` line ticked), plan 05's D2 as built, pointers moved in plans 05 and 06.
- *Checked*: `rw-clang-26`, 381 of 381, none skipped; `measure.sh`'s 88 binaries before (`5572d96b`) and after byte
  for byte the same, the 33 `-Os`, `-O1` and checked `Executor`s of the same sizes; the examples (`rw-clang-26` and
  `rw-examples`) print and write byte for byte what they did.

**D2, step 2: a float core for `cos`** (proposal 4, second half).
- *What*: `cos` ran its double core one number at a time at every precision, a lane of floats too: 48 µs for 4,096
  floats, 4.1 times libm's `cosf`. Now an F32 cosine at `Normal` and `Fast` runs a float core, as the sine does; F64,
  and every value at `High`, still run the double core, so the precisions mean what they mean for `sin`.
- *How* (`runtime/float_cores.hpp`): the radian sine's body is now `sine_turned_in_float<QuarterTurns>(a)`,
  `sin(a + QuarterTurns π/2)` of a magnitude: the hold at 2^24, the five-part reduction, Cephes' two polynomials and the
  quadrant's choice, the shift added to the quarter-turn count `m` after the reduction, so the remainder is the sine's
  and `m + 1` exact (`m` is below 2^24·2/π). `sine_in_float` is it at 0, its sign and its zero put back as before;
  `cosine_in_float` is it at 1 of `|x|`: even, so no sign is put back, and no case for a zero (the cosine polynomial is
  exactly one at `r = 0`); an infinity and a NaN are `x - x`. `cosine_over` takes the pattern of the other float cores
  (a float runs the float core, any other type the double core). `_cos` moves from `transcendental_in_double_as` to
  `transcendental_as` (`compute.hpp`) and, in the rule table, onto `has_a_core_in_the_values_own_type`'s list, which
  the static check holds against the two families; the cost model's stand-in now covers `tan`, `pow` and `exp2` only.
- *Eligibility*: as before, `has_a_writing_over_the_value_type`; which core runs is chosen by the value type, in
  `float_cores.hpp`, as for every float core.
- *Measured*: every one of the 2^32 floats (a probe, not committed), built without and with the fused multiply-add: at
  most 2 ulp of the double core rounded to float on |x| ≤ 8192 (8,600 and 8,434 arguments over 1 ulp, the worst at
  0x1.3e5a6p+4 and 0x1.c17f6ep+7), every finite argument's answer in [-1, 1], an infinity's and a NaN's a NaN, even to
  the bit, a lane of eight equal to the plain float at every 1,024th: the sine's bounds (`float_cores.hpp`: about 8.7
  thousand over 1).
- *Tests*: `float_cores_test` sweeps the cosine where it sweeps the sine: ≤ 2 ulp on |x| ≤ 8192 (worst 2 ulp, at
  0x1.47ap+12, in the plain and the native build), in [-1, 1] past it, the lane equal to the plain float, even to the
  bit in every binade, `cos(±0)` exactly 1, the exhaustive sweep's two worst arguments, and 0x1.fa4df0p-3, where the
  fused and the unfused multiply-add differ, in the check that a core evaluated while compiling gives its run's bits.
  `cos_tan_pow_exp2`: an F32 `cos` at `Normal` and `Fast` no longer equals the walk bit for bit, for the reason the
  sine's float core does not (`nest_transcendental_test` holds that one to 2 ulp): the walk computes in double. So
  there it is held to 2 ulp of the walk at `Normal` where the core claims it, |x| ≤ 8192, past it to [-1, 1], and to a
  NaN at an infinity; at `Fast` against the walk at `Normal` too, since the float cores do not read the tier, and the
  walk at `Fast` runs the coarse double core (6 ulp from it at most, 98 of the 4,096 numbers over 2). It measures at
  most 1 ulp, 977 of 4,096 numbers different from the walk, none beyond the claim, at both levels and on both
  machines. At `High` and over F64 every number equals the walk's, as before; `sin`'s gradient (F64, `High`) and
  `tiers_test` (F64 graphs) are unchanged.
- *Same code elsewhere*: the branch moves from one family to the other, so `compute_as` makes the calls it made:
  `measure.sh`'s 88 binaries of `include/` before (`2d6603a7`) and after byte for byte the same, the 33 `-Os`, `-O1` and
  checked `Executor`s of their sizes before (the `-Os` ones 3,136–7,574 B); `sin/heinzel` runs as before.
- *Benchmark* (`cos/{heinzel,naive,eigen}`, `bench_transcendental.cpp`; zen2, core 4, 7 repetitions, the medians of two
  runs): `cos/heinzel` 48.4 and 48.5 µs → 3.30 and 3.27 at 4,096 (14.7 times faster), 3.11 and 3.16 ms → 211 and 211
  µs at 262,144, 12.42 and 12.45 ms → 843 and 846 µs at 1,048,576 (84 M → 1.24 G a second); `std::cos` per float 11.7
  to 12.0 µs, 751 to 759 µs and 3.00 to 3.02 ms, 3.6 times slower; Eigen's 2.61 and 2.62 µs, 168 and 168 µs, 673 and
  677 µs, 1.25 times faster. Its error against the float64 answer is Eigen's, 1.10e-7 (`std::cos` 5.75e-8).
  `sin/heinzel` 3.86 and 3.88 µs (3.87 and 3.86 before); the cosine is 15 % faster than the sine, which puts back a
  sign and a zero. Not gated: no baseline row.
- *Left*: float cores for `tan`, `pow` and `exp2` where a benchmark asks, and the cost model's stand-in for them (the
  backlog); Eigen's lead on the radian sine and cosine (1.47 and 1.25 times), which this step does not chase.
- *Docs*: `docs/architecture.md` (Numerics: the float cores' list), `examples/README.md` ("What does not run yet"),
  the backlog's transcendentals line, plan 05's D2 as built, the comments in `float_cores.hpp`, `cost.hpp`,
  `value_type.hpp`, `graph_builder.hpp`, `bench_transcendental.cpp`, `tiers_test` and `nest_transcendental_test`.
- *Checked*: `rw-clang-26`, 381 of 381, none skipped; `measure.sh` and the 33 sizes as above; the examples
  (`rw-clang-26` and `rw-examples`) print and write byte for byte what they did.

**D3, step 1: a table read of a NaN phase is a NaN** (proposal 16, first half; step 2, the tables' spellings, follows).
- *What*: `table_tap` (`runtime/table_gather_element.hpp`), which the walk and the loop tree share, turned a phase into
  the cell it reads by casting the floor of `frac(phase)·N` to an `int32`. A NaN phase, and an infinite one, whose
  `frac` is `inf − inf`, left that floor a NaN, and the cast of a NaN is undefined: ASan found the walk reading outside
  its table in Plan 26's oracle (the backlog). Now such a phase reads a NaN, on both runners: a NaN in, a NaN out, no
  read out of bounds.
- *How*: the select that already caught `frac` rounding to 1 (a phase just below a whole one, where `x = N`) catches
  the NaN too: `place = x < N ? x : 0` (a NaN fails `< N`), its floor the index, and the fraction `(place − floor) +
  (x − x)`, where `x − x` is zero for every number and a NaN for a NaN, which the blend hands on. For every number the
  tap is the one it was (`x` lies in `[0, N]`, and at `N` both forms read cell 0 at fraction 0).
- *Measured first* (`Plan/probes/28-dsl-proposals/wavetable-wrap`, a 2,048-cell constant table, 4,096 samples, zen2,
  core 4, the median of 15 timings of 2,000 runs, interleaved): the plain guard, `whole > 0 ? whole : 0` after the
  floor, cost a compare and an `and` per four lanes, +4.5 % on a read of an input ramp (6,080 → 6,350 ns) and +4.6 % on
  an oscillator (17,020 → 17,810 ns); the guard folded into the existing select, as built, 5,903–6,063 → 6,174–6,222 ns
  (+3.5 %) on the ramp and 17,026–17,046 → 16,623–16,642 ns (−2.4 %) on the oscillator. A NaN cannot be made in a
  constant expression (clang refuses arithmetic that makes or meets one), so the NaN cases are checked at run time.
- *Tests*: `tblread_test` (the walk: a NaN phase taps the first cell's place with a NaN fraction, reads a NaN, the
  phases beside it as before); `nest_gather_test` (both runners: every fourth phase a NaN, an infinity or a negative
  one, over a table that is a row of cells per sample and one constant over time, in lanes; each reads a NaN, the rest
  the walk's numbers bit for bit).
- *Same code elsewhere*: no measured row reads a table: `measure.sh`'s 88 binaries of `include/` before (`28e5c1ea`)
  and after byte for byte the same.
- *Docs*: the backlog's line ticked, plan 05's D3 as built, the comments in `table_gather_element.hpp` and
  `ranges_oracle_test`, the two tests' notes in `tests/CMakeLists.txt`; plan 06's pointer into the backlog moved (it
  read 289 since `2ade1553` moved its line to 292; 293 now).
- *Checked*: `rw-clang-26`, 381 of 381, none skipped; the examples print and write byte for byte what they did.

**D3, step 2: tables, `wavetable(table, phase)` and `lookup(table, x, lo, hi)`** (proposal 16, first half).
- *What*: `_tblread` existed, on both runners, with no spelling: a table read was built by hand, over a table of N + 1
  cells whose last repeated its first. Now a table, whatever the DSL already has for a tensor of numbers (an `Input` of a
  shape, `constant(…)`), is read two ways, in Gerald's words (2026-10-16): `wavetable(table, phase)` takes its N cells
  as one cycle, the phase in turns and wrapped, every whole turn the first cell, the last blended into the first; and
  `lookup(table, x, lo, hi)` takes them as a function at N evenly spaced points from lo to hi, both ends included
  (JUCE's `LookupTableTransform`), x clamped, the first cell at and below lo, the last at and above hi, a NaN at a NaN.
  No third word: a `table(…)` helper is not in this plan. Written so:

```cpp
constexpr std::array<float, 2048> kCycle = …;            // one cycle of a wave
Param inc{"inc"};
Variable phase;
phase[n] = frac(phase[n - 1] + inc);
Output y{"y"};
y = wavetable(constant(kCycle), phase);                   // or an Input table{"table", {2048}}

constexpr std::array<float, 128> kTanh = …;              // tanh at 128 points from -5 to 5
Expr warm = lookup(constant(kTanh), drive * x, -5.0, 5.0);
```

- *The wrap cell is the runner's* (the probe, `Plan/probes/28-dsl-proposals/wavetable-wrap`): `table_tap` names the far
  cell (`TableTap::next`, the first after the last), and both runners read it there; the table holds its N cells. Against
  a copy of the first kept after the last (step 1's headers), a 2,048-cell constant table on zen2, core 4: the read of
  an input ramp 6,184–6,286 → 5,319–5,340 ns (14 % faster), an oscillator whose phase is a recurrence 16,648–16,665 →
  17,316–17,360 ns (4.1 % slower). Neither faster on both, so the simpler: one table of N cells for an input and a
  constant alike (an input's table could not be given a cell where it is built), no copy, no N + 1 contract. The
  attribute check holds a table to N samples ("_tblread of resolution 3 needs a table of 3 samples, but was given 4").
- *`lookup`'s top edge: its phase is clamped below 1 by one cell*, not a guard cell: the phase is
  `min(last, max(0, (x - lo) · last/(hi - lo)))`, `last` the last cell's place, (N − 1)/N, rounded down to a float where
  it is none (`detail::last_cell`, `frontend/ops.hpp`), so no value's type rounds it past the last cell: x ≥ hi reads
  the last cell with weight one and the first with weight zero. Why not a guard: it works for an input table too, costs
  no cell, and is what the proof reads. Exact where (N − 1)/N is a float (N a power of two); else at most N·2^-24 of a
  cell short of the last (100 cells: −0.9999924 for a last cell of −1, never near the first's 1000). At hi itself the
  last cell where (hi − lo)·scale reaches its place, as for bounds a float holds whose difference it holds (−5 and 5
  over 128, −1 and 3 over 64, 0 and 1 over 100); else an ulp of the phase short. The bound goes first in `max` and
  `min`, which answer their second operand when one is a NaN, so a NaN x stays a NaN (`clamp`'s order answers lo).
- *A constant table already lowered*: `constant(…)` tables ran since Plan 19 (`constant_tensor_test`); the stage refusal
  the plan meant to remove (`stages.hpp`, "a table read over a constant or a window") is for a literal, one number held
  nowhere, which no spelling builds (`wavetable` asks for a table's cells, which a literal has not), and a window: both
  stay.
- *What a table is to the frontend*: the read is built with its cells (`TableAttr::size`), so `wavetable` takes them
  from the leaf where it is read (`detail::cells_of`: an `Input` of a shape, a `constant(…)`, an `iota`); a computed
  table has a shape only once analysed and is refused by name while the graph is built ("heinzel: a table is an Input
  with a shape or a constant(...), its cells known where it is read"), as is `lookup` with lo at or above hi (the
  `build_refuses_table_*` fixtures). The `spelled` column stays empty: `wavetable` is nine characters, one over an
  `OpName`, so a refusal names the read `_tblread`.
- *The proof of ranges*: no new rule. A read covers its table's range (`CoveringTable`), and a constant's range is its
  cells' hull (`Generated`), so a `wavetable` or `lookup` of a constant table is proven in [its least cell, its
  greatest]; an input table's is its declared range. `lookup`'s clamp proves its phase in [0, (N − 1)/N].
- *Tests*: `tables_test` (new): the spellings build their graphs, node for node; on `Executor` at High, Normal and Fast,
  over f32 and f64, on the default machine and zen2, a wavetable of a constant and of an input (phases sweeping,
  negative, past and just below a whole turn, a NaN and the infinities) and a lookup over 64 cells and 100 (lo, hi,
  below, above, ±1e30, the infinities, a NaN): every number equal to the walk's bit for bit, 49,152 of them, a NaN
  exactly where the read takes no number; lookup's edges at Normal and High; a wavetable oscillator in blocks of 8, 16,
  12 and 4 equal to one run of 40; the proof's ranges (a static_assert). Moved to N-cell tables: `tblread_test` (N = 3:
  0.75 reads the last cell toward the first, 25), `nest_gather_test` (eight cells; `table_tap(8, -0.0625)` is `{7, 0,
  0.5}`), the both-ways corpus, `agreement_graphs`, `loops_lower_test`, `nest_parallel_review_test`, `ranges_rules_test`,
  `ranges_oracle_test`; `constant_tensor_test` and `constant_ramps_test` spell their reads `wavetable`.
- *A table read inside feedback* (not in the spec): JUCE's ladder reads its table in its feedback loop, and the loop tree
  ran a cycle of maps and hand-ons only ("feedback through anything but maps of one shape is not lowered yet"). A table
  read now runs there as a map of its phase (`loops/graph/feedback.hpp` `runs_in_the_cycle`: `gathers(n) ==
  Gather::Table`, from the rule table), its table read from storage made before the cycle, which the lowering already
  requires by name. Oracle: a one-pole whose input passes through a `lookup`, its own past among what it reads, one
  voice and three, on `Executor` at High on both machines, equal to the walk bit for bit (`tables_test`); a delay
  line in feedback stays refused (D4's "not in this plan").
- *Benchmarks* (zen2, core 4, 7 repetitions, the medians of two runs): `wavetable/{heinzel,naive}` (`bench_wavetable.cpp`,
  new: a 2,048-cell sine cycle read at 440 Hz for 2^20 samples, each checked within 1e-3 of the sine): Heinzel 4.43
  and 4.43 ms, the plain loop 2.73 and 2.73 ms, 1.63 times faster. Heinzel's read is `ExactInDouble`: the phase cast
  to float and widened again, its `frac` taken again, the cells widened, the blend in double, six conversions and
  three roundings a sample, 18.8 cycles and 38.6 instructions a sample against the plain loop's 11.5 and 30.5 (`perf
  stat`), whose place and blend are float; a float writing at `Normal` is a backlog line. `ladder/heinzel-table`
  (`bench_compare_juce_heinzel_filters.cpp`: JUCE's 128 points of `tanh` from -5 to 5, read by `lookup` in both
  saturators, one constant): 23.9 and 24.0 ms against JUCE's own ladder's 18.4 and 18.4 (1.30 times), Heinzel's exact
  `tanh` 28.2 and 28.2, the Padé form 14.6 and 14.6; within 2e-2 of the exact-`tanh` ladder, JUCE's own bound. Not
  gated (no baseline row). The gated `ladder_bank/heinzel` (`bench_lane_recurrence`) holds no table: its object file
  built at `28e5c1ea` and after is byte for byte the same.
- *Same code elsewhere*: `measure.sh`'s 88 binaries of `include/` before (`ae9674b5`) and after byte for byte the
  same; no measured row reads a table.
- *Examples*: none changed. A table would read well in `analog_ladder` (`ladder<table>`) or tour 07, but each would
  change what it prints and writes (a third render, another sound), so tour 09's paragraph names the two reads in
  `examples/README.md` instead.
- *Left* (the backlog): a float writing of the table read at `Normal`; a computed table, which the analysis could
  count; `wavetable` in refusals (an `OpName` holds 8 characters); a `table(…)` helper, Gerald's.
- *Docs*: `examples/README.md` (tour 09: tables), `docs/limitations.md` (a "Tables" section, the exclusion table's
  workaround), `docs/architecture.md` (the frontend: tables), the backlog (the spelling's line ticked; the leftovers;
  the float writing), plan 05's D3 as built, the probe's README, the comments in `table_gather_element.hpp`, `ir.hpp`,
  `vocab.hpp`, `op_rules.hpp`, `attribute_checks.hpp`, `lower_stmts.hpp`, `feedback.hpp`, `interpret.hpp` and
  `compute_reference.hpp`, and `bench_compare_juce_heinzel_filters.cpp`'s header.
- *Checked*: `rw-clang-26`, 384 of 384 (`tables` and the two `build_refuses_table_*` fixtures new), none skipped;
  the examples (`rw-clang-26`) print and write byte for byte what they did.

**D4: a delay that changes, `x[n - d.at_most(120.0) * ms]`** (proposal 16, second half).
- *What*: `_dline(signal, position)` ran on both runners with no spelling; the loop tree ran it over a computed value and
  refused it over an input, over a constant and in feedback ("a delay line over an input or constant, or in feedback, is
  not lowered yet"). Now it is spelled in Gerald's words (2026-10-16), `x[n - d.at_most(120.0) * ms]`: x as it was `d`
  ago, `d` any value, in milliseconds (or `us`, `second`), between two samples the two blended; and it runs over an
  input. Written so (tour 03):

```cpp
constexpr Index n{48'000};
Variable phase;
phase[n] = frac(phase[n - 1] + 1.0 / 65'536);
Expr wobble = sinpi(2.0 * phase);
Expr d = 15.0 + 10.0 * wobble;            // 5 to 25 ms
Expr wet = x[n - d.at_most(25.0) * ms];
y = 0.5 * x + 0.5 * wet;
```

- *The spelling* (`frontend/arena.hpp`, `declarations.hpp`, `ops.hpp`): `d.at_most(b)`, the one public name (on an
  `Output` and a `Variable` too, `forwarding::members`), gives a step no user names, `detail::AtMost`; times a unit a
  `DistanceInTime`; back from `n` a `VaryingPast`: the position, `d · (unit · rate)`, a multiply in the graph, and the
  longest distance, `b · unit · rate` rounded up to a whole sample (`detail::longest_samples`), not `units::to_samples`,
  which refuses 2.5 ms at 44.1 kHz: a bound that misses a whole number of samples only by its product's rounding (that
  function's 1e-6 of a sample) is that number, 120 ms at 48 kHz 5,760 and not 5,761; any other rounds up, 2.5 ms at
  44.1 kHz 111. `x[…]` builds the `_dline` (`detail::delay_line`) of an `Expr`, an `Input`, an `Output` (its value, or
  its state as a lag reads it) and a `Variable` (the variable itself, as its lags read it). Each step's operator stands
  beside its type, so a program without `using namespace heinzel` finds it (the benchmark's arm found that).
- *A distance outside [0, b]*: `_dline` holds the position to [0, its longest]: below 0, and at a NaN, the read is x now;
  above b, as far back as the line keeps (b rounded up). The spelling's comment, tour 03 and `docs/limitations.md` say so.
- *Refused while the graph is built, one error each* (the `build_refuses_delay_*` fixtures, `gates/refusals/delay.cpp`):
  `x[n - d * ms]`, a distance with no bound: `operator*(Expr, units::Scale)` gives `detail::Unbounded`, whose
  `operator-(Index, …)` is a `kNeverCompiles` template, "heinzel: a delay that changes needs its longest distance: x[n -
  d.at_most(120.0) * ms]"; `x[n - d.at_most(25.0)]`, no unit (only Gerald's spelling: no bounded distance in samples),
  "heinzel: a delay that changes is given in a unit of time: x[n - d.at_most(120.0) * ms]"; a bound below 0, a NaN or
  an infinity, "heinzel: d.at_most(b) bounds a distance, so b is a finite number of at least 0" (the review's words,
  below); an `Index` with no rate, "heinzel: a delay that changes needs the sample rate: constexpr Index n{48'000}".
  The compiler's first line for the first:

```
include/heinzel/frontend/declarations.hpp:97:19: error: static assertion failed due to requirement 'detail::kNeverCompiles<heinzel::Index>': heinzel: a delay that changes needs its longest distance: x[n - d.at_most(120.0) * ms]
```

- *Over an input, a copy: decided by the probe* (`Plan/probes/28-dsl-proposals/dline-input`, zen2, core 4, the median of
  15 timings, two runs). The plan's way was the feed's kept tail, the stage's first `reach + 1` samples run apart, as a
  fixed lag of an input runs them; a tap whose distance changes lands on either side of the run's start, so the tail had
  to keep a copy of the run's first `reach + 1` samples beside it too (`tail.patch`). Against a copy of the input into a
  buffer that keeps `reach + 1` samples before its block, as a computed value's buffer does: over a position that is an
  input, the tail the same at 4,096 samples and 1,200 deep (8,955–8,966 ns against 8,899–9,014) and 5 to 9 % slower on
  every other row (64 samples: 194 against 178–179 ns); in a chorus, whose oscillator joined the line's stage and ran a
  sample at a time through the head, 1.5 to 2.3 times slower (4,096 samples: 29,647–29,660 ns against 19,342–19,436). So
  the copy, with no head copy, no second head rule, and the path the loop tree already ran. A fixed lag of an input keeps
  its tail, which won for it (4,096 samples, 1,200 deep: 346–354 ns against 666–674).
- *How* (`rewrite/copied_inputs.hpp`): a delay line whose signal is an input of time reads a copy of it, one copy an
  input, made just before its first such line: the rule table's one row that hands an operand on unchanged (`_realize`,
  found by its columns; the rule names no operation, as `gate_rewrite_names_no_operation` holds). Eligibility: a node the
  rule table times `ReadsAtSignalledDepth` whose operand 0 is an input with an axis. The planner plans the copy as any
  node: in the chorus merged into the oscillator's stage, its buffer keeping `reach + 1` samples before its block,
  carried from run to run. Compiling makes it in place on the analysed graph (`compile.hpp`: `attempt` and `compile`),
  before `rewritten`: its rules read the graph's feedback, and made last, the copy left them a graph with no plan (the
  chorus then lost its phase in double and its fused multiply-adds); so made, the planner's fallback to the graph as
  written has it too, with no graph more on the stack. A graph planned as written (the tests' `planned::`) keeps its
  line over the input and is refused by name: "heinzel::compile: a delay line over an input reads a copy of it, which
  this graph lacks" (`loops_plan_test` case 9, `nest_gather_test`).
- *Over a constant*: refused, "heinzel::compile: a delay line over a constant is not lowered yet": a constant's past is
  the constant, after the run's first samples' silence, and no one writes it. A constant tensor's is the analysis's: "a
  lag reads a tensor — declare it as a Signal to read its past".
- *In feedback*: refused, as the plan keeps it (its rule when built, the distance held to [1, max], is the backlog's), in
  words that also fit the other graphs refused there (the review's words, below): "heinzel::compile: a delay line in
  feedback, or of a value defined later, is not lowered yet". A flanger, `y[n] = x + 0.7 * y[n - d.at_most(5.0) * ms]`
  on `Executor`:

```
include/heinzel/executor.hpp:291:23: error: static assertion failed due to requirement '!built || !refused': heinzel::compile: a delay line in feedback, or of a value defined later, is not lowered yet
```

  The other graph: a line of a `Variable` written before its definition with no loop through it. A lag written there
  is moved after the definition (B4's `late_lags`) and runs; a line reads its distance now, so it is no lag to that pass
  and stays before it (the backlog; `docs/limitations.md`: write it after).
- *Tests*: `delay_lines_test` (new): the spelling builds its graph, graph for graph (in ms, us and second; of an Input,
  a computed value, a Variable and an Output; `at_most` on a Variable and an Output), and its longest distances (5,760,
  1,200, 0, 111, 45, 5, 1), at compile time; spelled with every name qualified. On `Executor` at High, Normal and Fast,
  over f32 and f64, on the default machine and zen2, a line over an input and one over a computed value, distances below
  0, past the bound, between samples, a NaN and the infinities: none of the 24,576 numbers differs from the walk; three
  voices at High on both machines (`executed`); a Param distance, 1.01 ms, against the blend written out. A chorus at
  High as the walk reads it, on both machines; in blocks of 1, 7, 64, 100, 3, 1,225 and 600, and a 120 ms line in blocks
  of 64 over 12,000 samples: one run's numbers, and again after a reset. Refused by name: the flanger, the line before
  its definition, a line of a number, the line planned with no copy. `loops_plan_test`: case 9's words, cases 14
  (feedback) and 15 (a constant) new. `parity/dline_here.cpp`, walked only behind a pinned refusal, is written in the
  language at 1 kHz (a millisecond one sample, the position times one, exact) and runs on `Executor`, the walk's
  numbers: `parity_dline` passes at its allowance as before.
- *Benchmark* (`bench_compare_juce.cpp` and `bench_compare_juce_heinzel.cpp`; zen2, core 4, 7 repetitions, the medians
  of two runs): `chorus/{heinzel,juce,naive}`, x and x d ago half and half, d swept from 5 to 25 ms by a triangle whose
  phase steps 2^-16 of a turn a sample, exact in float, so every arm reads at the same position (checked within 1e-5 of a
  double reference); JUCE's `dsp::DelayLine<float, Linear>`, push then pop, a sample at a time; `naive` a ring of a power
  of two samples. Over 2^20 samples: Heinzel 4.77 and 4.78 ms (4.55 ns a sample), JUCE 11.43 and 11.41 (2.4 times
  slower), the plain loop 4.14 and 4.13 (1.15 times faster). In blocks of 64 (`/B64`): 328 and 329 ns, 694 and 693, 252
  and 252. Heinzel's oscillator phase, a recurrence, runs in a stage of its own and its read in double (the probe's read
  alone, a position input: 2.2 ns a sample); the plain loop's blend is float, a backlog line beside the table's. Not
  gated (no baseline row).
- *Long lines at small blocks* (B-17): `longdelay`, the same over 10 to 120 ms, 5,761 samples kept. In blocks of 4,096,
  Heinzel 18.63 and 18.66 µs (4.55 ns a sample), JUCE 42.96 and 42.86, the plain loop 16.16 and 16.12; in blocks of 64,
  472 and 472 ns (7.4 ns a sample), JUCE 670 and 668, the plain loop 252 and 252: moving the history every run costs about
  180 ns of Heinzel's 472, where a ring moves nothing. A sliding origin stays B-17's, with this row as its benchmark.
- *Examples*: tour 03 gains a chorus, a section and a printed line of its own, run whole and in four blocks against its
  plain loop within 1e-4, its phase step a float holds exactly; its first line and every other example's output are as
  they were. `examples/README.md` (tour 03; "What does not run yet": a delay that changes in feedback).
- *Not in the spec*: the refusals of a missing unit, a bound below 0 and a missing rate; a Param distance runs (a delay
  set once a run).
- *Left* (the backlog): a delay that changes in feedback and over a constant, Gerald's; a line before its definition; its
  read in float at `Normal`, with the table's; B-17's sliding origin.
- *Docs*: `docs/limitations.md` (a "Delay lines" section), `docs/architecture.md` (Building, Rewriting, `frontend/`,
  `rewrite/`), `examples/README.md`, the backlog (the delay lines' line, the line before its definition, the float
  writing, B-17's measurement), plan 05's D4 as built, the probe's README, the comments in `feedback.hpp` and
  `compile.hpp`, the tests' notes in `tests/CMakeLists.txt`; pointers moved: plan 06's `graph_builder.hpp:477 -> 479`,
  `declarations.hpp:288-296 -> 337-345`, `Plan/backlog.md:299 -> 309`; plan 05's `declarations.hpp:259-271 -> 308-320`,
  `feedback.hpp:124-137 -> 132-145`, `docs/architecture.md:428-431 -> 434-437`, `Plan/backlog.md:220-223 -> 230-233`,
  D4's own `feedback.hpp:46-47 -> 50-61`, and D2's `Plan/backlog.md:147-151 -> 170-175` (stale since the line moved).
- *Checked*: `rw-clang-26`, 389 of 389 (`delay_lines` and the four `build_refuses_delay_*` fixtures new), none skipped;
  `measure.sh` over the bare, bare + copy and `Executor` arms of `include/` before (`b513b3bd`) and after: all 88
  binaries byte for byte the same (no measured row holds a delay line, and the copy leaves a graph without one as it
  is); the examples (`rw-clang-26`, against the same built at `b513b3bd`) print and write byte for byte what they did,
  tour 03 one line more, its chorus's.

**The package's review** (an adversarial review at `12c7ce19`: no wrong number, no crash, no undefined behaviour; gaps
in what is refused and in its words, fixed at their source).
- *A copy past a capacity*: where the copy of an input a delay line reads did not fit under `kMaxValues` or `kMaxNodes`,
  `copy_inputs_of_delay_lines` made none, and the planner said "a delay line over an input reads a copy of it, which
  this graph lacks", naming no capacity (the reviewer's probe: a line over an input and 123 squares, 128 values). It
  adds the copy through `push_value` and `push_node` now, which refuse in the capacity's own words, as Decision 77
  promises for every capacity: the third reached inside `try_compile` (`docs/limitations.md`, Capacities). Fixtures
  `build_refuses_capacity_copy_values` and `_copy_nodes`: a line over an input at `-DHEINZEL_MAX_VALUES=5` (its five
  values, asserted) and `-DHEINZEL_MAX_NODES=2`, one error each; against `12c7ce19`'s headers they get the old words.
- *`lookup`'s bounds*: only lo ≥ hi was refused; an infinite bound or a span past a double (`[-1e308, 1e308]`) read a
  NaN at every x, and a table of one cell a NaN at ±inf (`(x - lo) · 0`). `detail::lookup_scale` (`frontend/ops.hpp`)
  refuses, while the graph is built, a table of fewer than 2 cells, "heinzel: lookup(table, x, lo, hi) needs a table of
  at least 2 cells" (N points lie (hi - lo)/(N - 1) apart; `wavetable` of one cell is a constant cycle and stays);
  bounds or a span a double does not hold, "heinzel: lookup(table, x, lo, hi) needs lo, hi and hi - lo finite"; lo at
  or above hi as before; and a scale past a double, "heinzel: lookup(table, x, lo, hi) has hi - lo too small to space
  its cells". Each is compared before anything is computed from it: a NaN made in a constant expression stops it in
  clang's words, not these. Checked in double: over floats lo, hi and hi - lo must also be finite in float, which the
  frontend cannot check, not knowing the value type (`docs/limitations.md`, Tables). Fixtures
  `build_refuses_table_lookup_bound_infinite` and `_of_one_cell`; a NaN, each infinity, a span past a double, one too
  small, lo at +inf (crossed), and a wide and a narrow span that build, at run time in `tables_test`.
- *A distance fed back*: `v[n] = 0.5 * x[n - (0.5 + 0.25 * v[n - 1]).at_most(1.0) * ms]`, an input's line whose
  distance is in the cycle, was refused as "feedback through anything but maps of one shape". A delay line inside a
  cycle (`gathers(n) == Gather::Back`, the rule table's, no operation named) is a delay line in feedback now
  (`loops/graph/feedback.hpp`, `order_a_span`), and one wording covers it, the flanger's signal fed back and a line of a
  `Variable` before its definition with no loop: "heinzel::compile: a delay line in feedback, or of a value defined
  later, is not lowered yet" (`kDelayLineInFeedback`, 91 characters; D4's was "a delay that changes, of a value defined
  later (feedback), …", which did not fit the distance). `delay_lines_test` pins the three; `loops_plan_test` case 14.
- *`at_most(inf)`*: refused as "so b is a number of at least 0", which infinity is; the check is b in [0, the largest
  double] now (it was 1e300; a bound above it is refused by the line's int32 range), "… so b is a finite number of at
  least 0", the `build_refuses_delay_below_zero` fixture's words.
- *Pointers*: plan 05's D2 step 1 (`compute.hpp:144-159`, `:161-188`, `:189-230`; `float_cores.hpp:399-406,417-434`,
  stale since step 2), and those these fixes moved (D3's `ops.hpp:146-200`, `feedback.hpp:138-151`; D4's
  `feedback.hpp:56-67`, `arena.hpp:264-292`, `ops.hpp:202-214`).
- *Docs*: `docs/limitations.md` (Capacities, Tables, Delay lines), plan 05 (D3's and D4's review notes), the backlog's
  delay lines line, the tests' notes in `tests/CMakeLists.txt`, the comments in `copied_inputs.hpp`, `feedback.hpp`,
  `ops.hpp` and `graph_builder.hpp`.
- *Checked*: `rw-clang-26`, 393 of 393 (the four fixtures new), none skipped; `measure.sh` over the bare, bare + copy
  and `Executor` arms of `include/` before (`12c7ce19`) and after: all 88 binaries byte for byte the same (every change
  runs while a graph is built or compiled).

**After the close: the float reads** (Gerald, 2026-10-17: "only make the table read faster. float is fine here", then
"make the delay line also faster").
- *What*: both reads whose address is a signal took their tap and blend in double, a lane at a time (`loops/values.hpp`
  `gathered`), at every precision: an f32 read widened its position and cells and rounded its answer back, six
  conversions and three roundings a sample, and lost to the plain loops, whose place and blend are float:
  `wavetable/heinzel` 1.61 times `naive`'s time, `ladder/heinzel-table` 1.30 times JUCE's ladder, `chorus/heinzel` 1.15
  times the plain ring's. Now an f32 read at `Normal` and `Fast` takes them in float.
- *The rule* (`made_by`): a gather computes in the type a map computes in, double at `High`, else its operands' type
  (`Stmt::computed_in`, double where one is a double); where that is float, its tap and blend are float
  (`gathered_in_float`). So `High`, f64, and an f32 read with a double operand (an f64 table, an f64 distance) keep the
  walk's double, bit for bit. The rule table's `ExactInDouble` stays: it says what the read is defined as, which `High`
  computes, as `_fma`'s and `_rsqrt`'s, which `Normal` also computes in float. No operation is named.
- *How*: `table_tap`'s and `signalled_tap`'s steps on the float the position is, over every lane at once
  (`table_tap_in_float`, `signalled_tap_in_float`, one template for a float and a `Lane`): the phase wrapped, its place,
  the select that reads the first cell for a place rounded up to N and for a NaN, the floor, the fraction and `x − x`;
  the distance held to [0, the largest float at most the line's longest] (`largest_float_at_most`: past 2^24 samples a
  longest may round up) and split by truncation. Only the two loads are a lane at a time; the blend is
  `fraction · (far − near) + near` by the machine's multiply-add, which `Normal` grants. A table read's type is its
  phase's (`SameAsFirst`), so an f32 read's phase is a float: an oscillator kept in double at `Normal` (Decision 66)
  hands it the phase rounded to a float, as the plain loop does.
- *In bounds, in float*: the floor of a place below N's float is at most N − 1 whatever N is (a float past 2^24 is
  whole, N's float its nearest), a place rounded up to N and a NaN read the first cell, and a distance at most the
  line's float bound truncates to at most its longest. Checked directly: the float tap of 11,512 phases over tables of
  1 to 2^30 cells (beside every cell's place and a whole turn, tiny, huge, no number) and of 192 distances over lines
  of 0 to 2^31 − 1 samples (at, beside and past the longest and its float), alone and in eight lanes: each inside,
  whole, its fraction in [0, 1) or a NaN where the phase is none, every lane the number's.
- *The bound* (`tests/support/float_reads.hpp`): the float blend lies within 3.5 float steps of its larger tap of the
  walk's (its three roundings at most a step each, the difference and the product being at most twice the larger tap,
  and the walk's half a step; 2.5 where the machine fuses). A delay line's tap is the walk's to the bit (a float
  distance clamped to a float bound splits exactly), so that is its bound. A table's place is rounded once in float,
  half a step at the place, and first once more where the phase lies in (−1, 0), whose frac 1 + phase rounds (N half
  steps of it); the read, the interpolant at the place, moves by that distance times the steepest step between the
  cells it may cross, a turn round where it rounds up to N. So `lookup` at and above hi reads at most N·2^-23 of a cell
  short of the last cell at `Normal` (N·2^-24 at `High`), never the first: over 100 cells −0x1.fffe8p-1 against High's
  −0x1.ffff04p-1, the last cell −1.
- *Tests*: `tables_test` and `delay_lines_test` hold every f32 read at `Normal` and `Fast`, on both machines, to the
  bound around the walk's own phase or taps, a NaN exactly where the walk's is, and every other number to the bit, as
  before; their sweeps gain the floats beside 1 and beside every cell's place (a phase and lookup's x) and whole-sample
  distances. Measured: wavetables 74 of 1,024 numbers differ from the walk, at most 0.48 of the bound; lookup over 64
  cells 9, 0.17; over 100 cells 990, 0.95; the delay lines 92, 0.57. A Param distance against the float blend written
  out with the machine's multiply-add, every number. The blocks tests run at `Normal` on both machines: a wavetable
  oscillator, a chorus and a 120 ms line in blocks give one run's numbers. `nest_gather_test` runs its reads at `High`
  and at `Normal`, bit for bit at both (its sixteenths blend exactly in float, so the float tap's every cell and lane,
  a ragged end of three voices among them, is checked to the bit). An f32 read over an f64 input table and an f32 line
  at an f64 distance compute in double at `Normal` and `Fast`, every number the walk's. `parity_dline` and
  `tblread_test` run at `High` and the walk, as before.
- *Measured first* (`Plan/probes/28-dsl-proposals/float-reads`, zen2, core 4, 4,096 samples a run, the median of 15
  timings of 2,000 runs, two runs each, interleaved): the float writing a lane at a time, the tap over every lane at
  once, and that with a hardware gather (`vgatherdps`). An oscillator read a sample at a time 17,306–17,362 → 11,958–
  11,991 ns (its plain loop 11,155–11,198), a lookup inside feedback 63,754–63,758 → 53,727–53,901 (54,615–54,888), a
  chorus 18,462–18,510 → 13,294–13,314 (18,729–18,815), a delay line over an input distance 9,235–9,248 → 4,015–4,022
  (9,552–9,771). In lanes the two float writings make nearly the same code, the compiler vectorizing the one written a
  lane at a time; every lane at once was the steadier and 4.5 % faster on a wavetable of an input ramp (5,217 against
  5,460–5,474), so it is built. The gather was 10 % slower on the chorus and 26 % on the line. The ramp itself stays
  1.38 times its plain loop: moving each lane's two addresses out of the vector registers, which no writing saves.
- *Benchmarks* (zen2, core 4, 7 repetitions, the medians of two runs, before and after interleaved; every arm's check
  passing as before, none loosened): `wavetable/heinzel` 4.44 and 4.45 → 3.07 and 3.09 ms, `naive` 2.75 and 2.76 (1.61
  → 1.12 times); `ladder/heinzel-table` 23.8 and 24.0 → 21.0 and 21.1 ms, JUCE's ladder 18.4 and 18.4 (1.30 → 1.14);
  `chorus/heinzel` 4.76 and 4.74 → 3.50 and 3.52 ms, `naive` 4.13 and 4.13, JUCE 11.3 and 11.4 (1.15 → 0.85 of the plain
  ring); `chorus/heinzel/B64` 329 and 328 → 263 and 266 ns, `naive` 251; `longdelay/heinzel/B64` 472 and 468 → 407 and
  406 ns, `naive` 251, JUCE 668 (B-17's history move stays); `longdelay/heinzel/B4096` 18.6 and 18.6 → 14.3 and 14.4 µs,
  `naive` 16.1, JUCE 42.8. Not gated (no baseline row).
- *Same code elsewhere*: `measure.sh`'s 88 binaries of `include/` before (`6d9267d0`) and after byte for byte the same;
  no measured row reads a table or a delay that changes.
- *Examples*: tour 03's chorus runs in float now; its numbers move within the check it prints (1e-4 of its plain
  loop), so it prints and writes byte for byte what it did (built at `6d9267d0` and now, compared); no other example
  reads a table or a delay that changes.
- *Not in the spec*: the rule asks the statement's `computed_in`, not the value type alone, so an f32 read over an f64
  table or distance computes in double, as a map with a double operand does.
- *Left* (the backlog): a table read whose phase the proof keeps in [0, 1] (`frac`'s, `lookup`'s clamp) needs no wrap,
  the same cells to the bit: unwrapped, the probe's oscillator 11,857–11,971 → 10,955–11,081 ns (its plain loop's) and
  its lookup in feedback 53,049–53,109 → 47,328–47,368, a rule of its own.
- *Docs*: `docs/architecture.md` (Numerics: the two reads), `docs/limitations.md` (Tables, Delay lines), the backlog,
  plan 05's note after the close and its pointer `values.hpp:137-149 -> 138-150`, the probe's README, the comments in
  `values.hpp`, `table_gather_element.hpp` and `timing.hpp`, the tests' notes in `tests/CMakeLists.txt`.
- *Checked*: `rw-clang-26`, 393 of 393, none skipped.

**After the close: the phase in one turn** (Gerald, 2026-10-17: "Let's fix that phase wrap, it's an awesome idea to use
the proof mechanism for that").
- *What*: a table read wrapped its phase at every sample, `phase − floor(phase)`, a floor and a subtraction, on the
  chain of a read made a sample at a time: an oscillator's (`frac(p[n − 1] + inc)`) and a lookup's inside feedback
  (JUCE's ladder's saturator), whose phases lie in [0, 1] already. Unwrapped they ran 8 % and 11 % faster (the float
  reads' probe, `no-wrap.patch`). Now a read whose phase the proof keeps in one turn at every precision takes no wrap.
- *The rule*: a table read (the rule table's `Gather::Table`) whose phase, operand 0, the proof keeps in [0, 1] at every
  precision reads it as it is. Decided while compiling, on the analysed graph and before the rewrites (`compile.hpp`
  `mark_phases_in_one_turn`, in `take_ranges`, the proof's place); carried as `TableAttr::phase_in_one_turn`,
  which the rewrites keep with the node; copied by the lowering into `Stmt::phase_in_one_turn`; taken by the runner,
  `table_tap<false>` in double, `table_tap_in_float<false>` in float (`loops/values.hpp`). A mark a graph brings is
  replaced, so a graph cannot claim one. No operation is named. It is the first time the proof reaches a program:
  before, it refused or passed a graph and was cleared.
- *Exactness*: for a phase in [0, 1) the wrap is the identity (floor 0; −0 becomes +0, and either reads the first cell
  at fraction +0, `(±0 − ±0) + (±0 − ±0)`); at 1 the wrap gives 0, the first cell at fraction 0, and unwrapped x = N
  fails `x < N`, the first cell at fraction `(0 − 0) + (N − N)` = +0; a NaN reaches the same select and the same NaN
  fraction. So the same cells, next cell and fraction, bit for bit, at every precision and in float and in double, the
  float neighbours of 0 and 1 among them. `High` stays the walk's, which keeps its wrap: the walk is the definition, and
  the oracle never asks the proof.
- *What the program may rest on* (`ranges/in_every_rounding.hpp`, new): the proof's ranges are of the real numbers,
  its ends computed in double (Decision 74), and a program rounds past them: `frac` of an f32 just below 0 is 1 where
  the proof keeps it below 1, a float constant is not its double, and a product of two may pass 1 where theirs do not.
  Without the wrap a phase below 0 floors to cell −1, so the mark rests on a walk of its own, the proof's rules kept
  only where no rounding moves a number past 0 or 1, which every type holds and every monotone rounding leaves on
  their sides: a constant's number; the rules that make no number (`Same`: a view, a lag, a recurrence, a conversion;
  `EachIncreasing`: `min` and `max`, which pick an operand; `Choice`; `Truth`, 0 or 1; `Union`; `Generated`); silence,
  the 0 a past read reads before the first run and after a reset; and the facts of `abs` and `frac`, true of their
  numbers as computed: `x − floor(x)` lies in [0, 1] for every finite float and double (exact from 0 up, Sterbenz's
  lemma from 1 up; below 0 a monotone rounding of a number in [0, 1)), a NaN otherwise. The subtraction reads x twice,
  so no rule of `rewrite/` takes x apart into a sum (a sum is opened only where it has one reader); the feedback rule
  may swap its two terms and the double-state rule compute it in double, either of which keeps it in [0, 1]. Every
  other rule is any number there. So a `frac`'s phase, `lookup`'s clamp `min(last, max(0, along))` (its last place a
  float below 1), `clamp(p, 0, 1)`, `min(abs(p), 1)`, a choice of numbers in [0, 1], a variable these define and a lag
  of any of them are in one turn; a variable's past is kept only by its definition's fact (`frac`'s [0, 1], `abs`'s at
  least 0), so `ph[n − 1]` is in one turn where `ph[n]` is a `frac`, and `ph[n] = clamp(ph[n − 1] + p, 0, 1)` is in one
  turn at n, not at n − 1. `frac(p) · 1.5` and `0.5 + 0.5 sinpi(x)` are not.
- *What it may not rest on*: a port's declared range, an input's or a Param's, and a promise. A debug build checks them
  (an input before each run, a Param when it is set, a promise after the run), release nowhere ("release trusts its
  caller", Decision 74), so a phase that is an input, one declared in [0, 1], one promised there (proven, too, from a
  declared range) and a choice of a ranged Param keep the wrap, though the proof keeps each in [0, 1]. A NaN lies in
  no range: a value kept in one turn may be one (`frac` of an infinity, `lookup` of a NaN through `max` and `min`), and
  the select reads it as the wrap does. A fact is the frontend's (`abs`, `frac`), and must hold of the numbers as
  computed, in every rounding at every precision; a graph built by hand that marks a false one on a computed value is
  trusted for its numbers, but no more for memory: its read is a wrong number, never one outside the table (the
  review, below).
- *Where it lives*: `TableAttr` holds no spare byte, so its mark grows a node from 368 to 376 bytes and a graph from
  73,568 to 74,592 (`tensor_signal_test`'s pin moved); `Stmt`'s sits in the padding after `gather`, 520 bytes still. The
  program's print shows `in-one-turn`.
- *Tests*: `tables_in_one_turn_test` (new). On `Executor` at High, Normal and Fast, over f32 and f64, on the default
  machine and zen2: a wavetable of `frac(p)` (p at 0, −0, 1, −1e-30 and the least number below 0, which `frac` makes
  1, the numbers beside 0 and 1, every cell's place and beside it, a NaN, the infinities, a sweep), `lookup` with lo 0
  (x = −0 makes a phase of −0; a NaN through the clamp), an oscillator (steps to 0, to 1 from just below 0, back) and a
  lookup inside feedback, each in one turn by its statement's mark: at High every number the walk's, bit for bit; at
  Normal and Fast every number that of the same read with its phase times 1, which the walk at every precision does not
  see into, so read with the wrap (its mark checked off), bit for bit (48 reads of 1,024 numbers, none apart). The
  marks: an oscillator's past, a choice of two numbers and `clamp(p, 0, 1)` in one turn; an input phase, declared in
  [0, 1] or not, `frac(p) · 1.5`, a promise and a choice of a ranged Param not, the proof keeping each of the last
  three in [0, 1] (static asserts). An oscillator in blocks of 1, 7, 64, 3, 100, 1 and 24 equals one run at High,
  Normal and Fast. `frac`'s fact as the runner computes it, `compute_as` of floor and subtraction, over 1,048,576
  floats (alone and in eight lanes) and as many doubles: none outside [0, 1], a NaN only of a NaN or an infinity.
  A mutation reading the last cell where an unwrapped place reaches N fails 4 of the 1,024 wavetable numbers and 1 or 2
  of the oscillator's at every level, in both types, on both machines (tried, not committed). Every existing table
  test as before (`tables_test`: High and f64 the walk's to the bit, an f32 read at Normal and Fast within its bound);
  its blocks test now reads in one turn.
- *Measured* (the probe, `Plan/probes/28-dsl-proposals/float-reads`, zen2, core 4, 4,096 samples a run, two runs each,
  interleaved): `osc` 11,933–11,945 → 11,053–11,057 ns (its plain loop 11,143–11,248), `pole` 53,692–53,699 →
  47,845–47,915 (54,518–54,708); `ramp`, an input phase, wrapped as before (5,192–5,219 and 5,209–5,211).
- *Benchmarks* (zen2, core 4, 7 repetitions, the medians of two runs, before and after interleaved; every check passing
  as before): `wavetable/heinzel` 3.07 and 3.07 → 2.82 and 2.83 ms, `naive` 2.75 (1.12 → 1.03 times);
  `ladder/heinzel-table` 21.0 and 21.0 → 19.4 and 19.5 ms, JUCE's ladder 18.4 (1.14 → 1.06). Not gated. No other
  benchmark reads a table.
- *Not built* (measured only): the delay line's clamp of its distance to [0, the longest] could go the same way where
  the distance is kept in [0, b]. Unclamped (`no-clamp.patch`) the probe's chorus ran 1.3 % faster (13,292–13,298 →
  13,111–13,143 ns) and its line at an input distance 6.2 % (4,011–4,021 → 3,766); but the line's distance is an input
  and the chorus's arithmetic on an oscillator's phase, neither kept there at every precision.
- *Same code elsewhere*: `measure.sh` (bare, bare + copy and `Executor` arms) over `include/` before (`3f18fe43`) and
  after: 83 of the 88 binaries byte for byte the same; the counting builds of W2, W7, W8, W9 and W10's `Executor` differ
  only in their symbol table, every loaded section the same: a symbol names the workspace's deleter by the program's
  place inside the compile result, 73,664 → 74,688 bytes along, which the graph's growth moved. No measured row reads a
  table. `zero_overhead_binary` and `zero_overhead_runtime` pass.
- *Examples*: none reads a table, so none prints or writes anything else; `rw-clang-26` builds them all.
- *Docs*: `docs/architecture.md` (the proof's paragraph: the one mark it leaves the graph, the first place it makes a
  program faster; `ranges/`; Numerics: the read in one turn), `docs/limitations.md` (Tables: what reads with no wrap
  and what does not), the backlog line ticked, plan 05's note after the close, the probe's README and `no-clamp.patch`,
  the comments in `compile.hpp`, `ir.hpp`, `program.hpp`, `lower_stmts.hpp`, `values.hpp` and
  `table_gather_element.hpp`, the tests' notes in `tests/CMakeLists.txt`.
- *Pointers moved*: plan 05's `docs/architecture.md:434-437 -> 442-445`, `table_gather_element.hpp:36-47 -> 40-53` and
  `:22-47 -> 22-53`, `values.hpp:138-150 -> 139-151` and `:166-253 -> 167-255`; D3's plan text before it was built
  (`table_gather_element.hpp:22-44`, `:34-44`) is history. Plans 06 and 07 and the backlog point before every change
  (the backlog line keeps its two lines).
- *Checked*: `rw-clang-26`, 394 of 394 (`tables_in_one_turn` new), none skipped.

**After the close: the review of the float reads and the phase in one turn** (an adversarial review of `f4b52ce8` and
`7c2bec39`: one real bug, five gaps between the docs and the code).
- *A false fact read off the table* (the bug): the walk at every precision trusts every mark that is no promise and no
  port's (`in_every_rounding.hpp`), and `push_range` is public, so a graph built by hand that marked [0, 1] on a
  computed phase (`wavetable(t, p * 1.0)`) had its read marked, and p = −1/128 read cell −1, p = −0.25 the heap 16
  cells before the table. Now the tap with no wrap reads a place below 0 at the first cell (`table_tap<false>`,
  `table_tap_in_float<false>`: `max(x, 0)` beside the compare `x < N`), so a read stays in its table whatever a fact
  says: a false fact makes a wrong number, never a read outside it. The skip stays.
- *Exactness*: on [−0, 1] and at a NaN the read is the wrapped one's to the bit: −0 and +0 leave the max as +0 (the
  first cell at fraction +0, as the wrap reads −0), 1 fails `x < N`, a NaN fails both (the first cell, the NaN fraction
  from `x − x`). A probe (not committed) held the tap with no wrap against the wrapped one over every float in [+0, 1]
  (1,065,353,217), −0 and NaNs, alone and in eight lanes, over tables of 1, 2, 3, 64, 100, 2,048, 2^24 + 1, 2^24 + 3
  and 2^30 cells, and in double over every third of those floats and 2^24 doubles in [0, 1): none apart; a sample of
  the floats below 0, each index inside.
- *The spellings* (zen2, core 4, 7 repetitions, the medians, before and after interleaved): `max(x, 0)` beside the
  compare, one `vmaxss` off the read's chain, the loop otherwise instruction for instruction the same:
  `ladder/heinzel-table` 19.45–19.51 (one 19.71) → 19.70–19.84 ms over eleven pairs, 1.6 % (JUCE 18.30–18.50; 1.06 →
  1.07 times), its one operation more presumably queued behind the ladder's multiplies; `wavetable/heinzel` 2.81–2.84 →
  2.82–2.83 ms over eight (`naive` 2.72–2.75). Both compares, `0 ≤ x < N` (one more on the chain), two pairs: 19.94 and
  19.88 ms, 2.84 and 2.84. |phase|, its sign cleared: 20.06 and 20.05, 2.86 and 2.85 (the compiler made lookup's
  `min(last, |y|) · N` a compare and a blend). |x|: 19.66 and 19.63, 2.85 and 2.86. The max is built: level on the
  wavetable, the least in all, and the plainest rule (a place below 0 reads the first cell, as one at N and a NaN do).
- *Tests*: `tables_in_one_turn_test` reads a false fact: `wavetable(t, p * 1.0)` over an input table between NaNs, the
  product marked [0, 1] by `push_range`, read in one turn, at phases −1/128, −0.01, −0.25, −0.75 and 1,020 more in
  (−2, −0], at High, Normal and Fast, f32 and f64, on both machines: every number within the table's cells (the first
  cell's), none a NaN. Without the guard 166 to 327 of the 1,024 numbers fall outside, at every level, type and machine
  (tried, not committed); its static asserts on the tap fail first. `min(abs(p), 1)` joins the reads held everywhere
  (`abs(+0)` is −0 under `select(a > b, a, b)`, read as the wrap reads it): 12 reads of 1,024 numbers, none apart.
- *The docs*: a variable's past is kept only by its definition's fact, not by the clamp a recurrence makes (this
  section, `docs/limitations.md`, `docs/architecture.md`); a fact must hold in every rounding at every precision, not
  only of the real numbers (`detail::with_fact`, `in_every_rounding.hpp`); past 2^24 cells N's float is rounded, up to
  half a step of N more on the place, and past 2^24 samples a distance beyond a longest that is no float is held to the
  largest float below it where the walk holds it to the longest: neither in `float_reads.hpp`'s bound, neither leaving
  its table or line (`docs/limitations.md`, the bound's comment); plan 05's pointer `docs/architecture.md:442-445`, a
  paragraph early, is `445-455`.
- *Not changed*: the node's 376 bytes (compile-time memory only; the binaries are the same).
- *Same code elsewhere*: `measure.sh` (bare, bare + copy and `Executor` arms) over `include/` before (`7c2bec39`) and
  after: the 88 binaries byte for byte the same; no measured row reads a table.
- *Docs*: besides those above, the comments in `table_gather_element.hpp` and `values.hpp`, plan 05's note after the
  close, the tests' notes in `tests/CMakeLists.txt`.
- *Pointers moved*: plan 05's `docs/architecture.md:442-445 -> 445-455`, `values.hpp:167-255 -> 167-257`,
  `table_gather_element.hpp:40-53 -> 41-57` and `:22-53 -> 22-57`, `graph_builder.hpp:278-298 -> 280-300` and
  `:319,366 -> 321,368`; plan 06's `graph_builder.hpp:479 -> 481`. Plan 07 and the backlog point before every change.
- *Checked*: `rw-clang-26`, 394 of 394, none skipped.

## Decision 79 — Plan 29: `abs` as an operation, and a subtracted product fused (`Plan/29-abs-and-fused-subtract`)

**Decided** (Gerald, 2026-10-20: the crv probe on the M1 Pro puts `additive/heinzel` at 54.3 µs against `hand-fma`'s
40.1, "ideally it would match the hand-fma"; the plan approved with both changed answers, `abs(+0)` is `+0` and the
gradient of `abs` at 0 is 0). Each step adds its section here.

**Step 1: `_abs`.**
- *What*: `hz::abs(x)` was `max(x, -x)`, two nodes, and `_max` is `select(a > b, a, b)`: not `fabs` (it answers −0
  for +0), so clang kept a negation, a compare and a blend (`fneg`, `fcmgt`, `bif`) for each `abs`, where x86's
  `vmaxpd` is the select and hid it. Now one operation, `_abs` (code 59, appended): the sign bit cleared, `Cheap`,
  `Exact`, `Differentiable`, latency of an add, spelled `abs`. The spelling is unchanged.
- *How*: `magnitude_of` beside `select` (`runtime/value_type.hpp`), the bits of a float or a double with the sign bit
  masked off, a constant expression and one `fabs` at run time; a lane's is `__builtin_elementwise_abs`
  (`runtime/lane_value.hpp`). No hardware twin: no rounding, the same code under constant evaluation. The walk shares
  the writing (`arithmetic_of`), as it does `_neg`'s.
- *Range*: a new rule, `Magnitude` (`ir/rule_columns.hpp`, `ranges/interval.hpp`): a piece with no negative number as
  it is, one with no positive number negated, one across 0 from a closed 0 to the larger end's magnitude, open where
  that end is (on a tie, where both are), the pieces then joined. It makes no number (only the sign changes), so the
  walk at every precision keeps it (`in_every_rounding.hpp`).
- *The fact stays*: the plan dropped `abs`'s fact (at least 0) as the rule now gives it, but a variable's past is read
  by its definition's fact, not by the rule that made it (the proof's rule 3), so `v[n] = abs(...)` would lose the
  `v[n - 1]` ≥ 0 that `docs/limitations.md` promises a table read. `abs` keeps stating it; at its own node the rule
  gives as much or more (`abs(x)` for x in [−1, 2] was [0, 2] by the fact, and is by the rule). `ranges_walk_test`
  holds it: `1 / (1 + m[n - 1])` for `m[n] = abs(x[n] - 0.5 m[n - 1])` is proven; with the fact dropped its `facts()`
  fails (tried, not committed).
- *Gradient*: dY · sign(x), so 0 at ±0 (PyTorch's; it was −dY there, the tie going to `-x`).
- *Moved*: nothing. No conservation hash, loop golden or pinned output holds `abs` (the corpus and the goldens have
  none; the tests that run `abs`, parity `conditionals`, tutorial 02, `nest_transcendental`, `nest_agreement`,
  `tables_in_one_turn`, `log10_cospi` and example 12, print what they printed); `ranges_oracle`'s generator draws `_abs`
  among its one-operand rows, so its graphs and counts are new ones.
- *Tests*: `abs_test` (new): one node; over ±0, ±denormals, the least normals, ±∞, NaNs of either sign and finite
  numbers, f32 and f64, on `Executor` at High bit for bit the walk, and at Normal and on the walk bit for bit
  `std::fabs`. `ranges_rules_test` (`magnitudes`), `numeric_ops_test` (±0, a denormal, ∞, NaN),
  `gradient_mask_fd_test` (abs against finite differences away from 0, and 0 at ±0), `lane_ops_test` and
  `native_f32_is_exact_test` (lane against number, float against double), `both_ways` (the corpus reaches `_abs`).
- *Benchmark* (M1 Pro, `Plan/probes/crv`, 7 repetitions, the medians, the old binary run beside the new):
  `additive/heinzel` 54.7 → 41.8 µs, `heinzel-params` 54.9 → 41.8, `hand-fma` 40.4 and 40.3, `hand` 55.5 and 55.6;
  `off` 825.587a as before. The loop: 15 floating-point instructions a vector where it had 19, one `fabs.2d` and one
  `fabd.2d` (clang folds `|a - b|`), no `fcmgt` or `bif`. zen2 and `measure.sh` (Linux) at the package's close.
- *Checked* (the Mac, `rw-clang-26`): 383 of 391; the 8 that fail fail at HEAD too, the same way (`transcendental`,
  `rewrite_parallel_form_declines`, `ordering`, `gate_no_threads_would_object`, three `build_refuses_executor_*`,
  `parity_mlp_forward`).

**Step 2: a subtracted product.**
- *What*: `fuse_product_adds` (`rewrite/product_adds.hpp`) fused an add of a product, not a subtraction: crv's wrap
  `x - floor(…) 2π` was `fmul` + `fsub` where `hand-fma` has one fused instruction. Now a `Subtract` whose minuend or
  subtrahend is a `Product` fuses under the add's conditions (read by nothing else, laid out and rounded as the
  subtraction, outside feedback cycles and their sums, the machine has `fma`, the precision grants reordering):
  `x - a*b` as `_madd(-a, b, x)`, `a*b - x` as `_madd(a, b, -x)`. The rule reads the `Algebra` column only.
- *How*: the negation is `feedback_sums.hpp`'s `negated` (shared, now taking the list the `_neg` goes into): a constant
  negated as it is copied, no node; anything else a `_neg`, put right before the multiply-add (its operands are made
  by then; one node in, one product out, the graph's size unchanged). Of `x - a*b` a constant factor first, then a
  declared timeless one. The negation is exact, so the rounding is the add case's. The subtrahend is taken before the
  minuend where both are products (`fmls` folds it). Subtractions inside a feedback sum stay the feedback rule's
  (Decision 66), as the adds do.
- *What clang makes of it* (objdump, M1 and an x86 `znver2` cross build): a factor's `_neg` folds, `fmls.4s` /
  `vfnmadd213ps`; a minuend's folds on x86 (`vfmsub213ps`), but NEON has no vector instruction for `a*b - x`: `fneg` +
  `fmla`, as many as `fmul` + `fsub` were. Kept, not restricted: level or better on the M1 (crv 38.2 µs either way,
  `maps_subtract.cpp` 380 → 372 ns), one instruction fewer on x86, one rounding fewer everywhere.
- *Moved*: nothing. The goldens, conservation hashes and pinned outputs plan graphs as written (`planned::`), not
  rewritten; the four tests failing at HEAD print what they printed (`transcendental`, `rewrite_parallel_form_declines`,
  `ordering`, `parity_mlp_forward`, compared output for output).
- *Tests*: `rewrite_product_adds_test`: `c - 2π a` (the constant negated, no node), `c - a*b` (`_madd(_neg(a), b, c)`),
  `1.2a - 0.05` (`_madd(1.2, a, -0.05)`), `a*b - c` (`_madd(a, b, _neg(c))`), each operand checked; a subtracted
  product read twice, High, a machine without `fma` and a subtracting one-pole (the feedback rule's) as written; each a
  fixed point of `infer` and the rule; the oracle: each rewritten program at Normal against the reference walk at
  High, within the add case's bound (1.2e-7 of the terms at most).
- *Benchmark* (M1 Pro, 7 repetitions, the medians, step 1's binary run beside): `additive/heinzel` 41.4 → 38.2 µs,
  `heinzel-params` 41.7 → 38.2, `hand-fma` 39.9-40.3; `off` 825.587a as before. The loop: 14 floating-point
  instructions a vector where it had 15 (`fmla` 24 → 40, `fmul` 48 → 32, `fsub` 16 → 0, `fneg` 0 → 8 for crv's
  `y |y| - y`; the `fabd` of `|x - k 2π|` is a `fabs` of the fused `r`). `Plan/probes/map-unroll/maps_subtract.cpp`
  (L1-sized, before and after): `clamp(0.95 - 1.2x, 0, 1)` 328 → 254 ns, `clamp(c - a*b, 0, 1)` 380 → 366,
  `clamp(a*b - c, 0, 1)` 380 → 372; at 512 x 512 the loads bound every form, level. zen2 and the bench gate at the
  package's close.
- *Checked* (the Mac, `rw-clang-26`): 383 of 391, the same 8 failing as at HEAD.
- *Review fix*: a `_neg` of a timeless value that is not a constant (a Param) is made once a run; the loop reloads it
  from the workspace every vector (it cannot prove the output apart), and NEON's `fmla` overwrites its addend, so a
  shared `-k` costs a `mov.16b` a vector. The rule now: of `x - a*b` a constant factor is negated, else a factor the
  sum's own loop makes (`timeless` as the sum, of its shape; `feedback_sums.hpp`'s `timeless_of`, shared); of
  `a*b - x` the addend where it is a constant or made so; anything else stays as written (`negated_at`). M1, 2048
  samples (`maps_subtract.cpp`, before and after back to back, medians of 7): `clamp(x*w - k, 0, 1)` 188 → 183 ns
  (unfused 182), `clamp(x - k*w, 0, 1)` 168 → 163 (`fmls.4s` by element, no reload; `x + k*w` 163), `x - a*w` 162
  level; crv `heinzel`, `heinzel-params` level (38.4-38.6 µs, the same loop code), `hand-fma` 40.5.

## Decision 80 — knob math: values worked out from Params only when a Param changed

**Decided** (Gerald, 2026-10-23: recomputing what depends on Params is a common cost in audio DSP; the cost model
chooses, across blocks only). A stage with no loop that works out values from Params alone, such as a filter's
coefficients from its knobs, is *kept*: it runs only in a run where a Param it watches changed, bit for bit, and its
results stay in the workspace in between. A stage is kept where the cost model prices it above checking its Params, with
no margin. No form of that price fits the measured cases within 30 %, since how much of a stage the run around it hides
varies, but the decision is rarely close, so "the decision is judged, not the price" (63 of 64 cases as measured). The
work ahead of the loops runs before them, each band's knob math a stage of its own, and a value its stage cannot change
is read from a copy in the stage's frame. `knobs1/heinzel` (one RBJ peak, knobs at rest) B32 2.67 → 1.47 ns a sample,
level with the same filter's coefficients set as Params. Details: `docs/knob-math.md`.
