# Oracle O10 — what a function-accuracy tier costs, measured

Milestone M4.2. One row per operation that has coarse cores. The gate is
`tests/unit/tier_bounds_test.cpp`; this file is the record and the account of how the numbers were
taken. (The gate lived in `tiers_test.cpp` until that file passed amendment 06's six hundred lines
and was split along the seam its own opening paragraph named — graphs there, cores here.)

**Grown family by family, and now complete.** M4.2 covers fifteen transcendentals and all fifteen
are below: the exponential family's six, the periodic four, the inverse circular four, and `_pow`.
A row appeared here only once its coarse cores existed and had been measured — an empty row would
be a bound nobody could exceed.

## What is being measured, and against what

**The coarse core against this tree's exact core**, not against the platform's library. What a tier
costs is the distance from what this tree computes when it is asked for accuracy. The exact core's
own distance from the platform is a different number, measured where each core is written and gated
in `tests/unit/transcendental_test.cpp`. Adding the two together and calling the sum a tier's error
would charge the tier for something it did not do.

**Absolute or relative per operation, following the old tree's own `relative` flag.** Its
`OpBounds{approx, fast, relative}` marks `_exp`, `_exp2` and `_pow` relative and the logarithms, the
circular family and `_tanh` absolute. Reproducing that choice is what makes the two columns
comparable at all.

## The measurement

Taken with the shipped cores, at `-O2 -ffp-contract=off`, clang 22.1.8 on x86-64. The domains are
the ones each core's exact-tier measurement used:

| operation | domain swept | arguments |
|---|---|---|
| `_exp` | every argument whose answer is a normal double, steps of 1/100 | 141 801 |
| `_exp2` | the same | 204 601 |
| `_log` `_log2` `_log10` | every binade, 32 significands each | 66 912 each |
| `_tanh` | -25 to 25 in steps of a hundred-thousandth | 5 000 001 |
| `_sin` `_cos` | -1000 to 1000, then -1e12 to 1e12 | 400 001 + 200 001 |
| `_sinpi` | -200 to 200, then -2e15 to 2e15 | 400 001 + 200 001 |
| `_tan` | -1.5 to 1.5 | 300 001 |
| `_atan` | -400 to 400 in steps of 1/250, then every binade | 200 001 + 33 552 |
| `_asin` `_acos` | the whole domain, then the 10 000 doubles nearest one | 200 001 + 20 000 |
| `_atan2` | all four quadrants on a 601x601 grid, then far-apart exponent pairs | 361 201 + 71 100 |
| `_pow` | a 200x401 grid of positive bases and exponents, then negative bases at integral exponents, then the product placed near ±1024 | 80 200 + 16 214 + 27 456 |

The gate re-runs these at full density except for `_tanh`, which it runs at a hundredth of the
density — with the argument the maximum actually lands at, -0.75, exactly on the grid rather than
near it.

**Two ranges for the periodic ones, and it is not for coverage.** The interesting question about a
periodic function is not its accuracy near zero but whether the reduction still holds far out, which
is why the exact tier is measured the same way. `_sinpi`'s far range is where every double is an
integer and the answer is a signed zero — the range the operation exists for.

**`_tan` gets one range and it is narrow.** It has a pole at pi/2, so an absolute bound on it is
meaningless anywhere near one; [-1.5, 1.5] is where the exact tier's own gate lives for that reason
and the coarse gate follows it. The old tree's `_tan` row carries an absolute bound too and has the
same limitation.

## The table

`here` is this tree's recorded bound: the measured maximum, rounded up. `there` is the old tree's
bound for the same row and tier, from `kBuiltins` in its `include/heinzel/ir/vocab.hpp` at `c4012b8`.

| operation | unit | approx measured | approx bound | old tree | fast measured | fast bound | old tree |
|---|---|---|---|---|---|---|---|
| `_exp` | relative | 3.2421e-06 | 3.3e-06 | 3e-05 | 7.9443e-04 | 8.0e-04 | 6e-03 |
| `_exp2` | relative | 3.2422e-06 | 3.3e-06 | 4e-05 | 7.9445e-04 | 8.0e-04 | 7e-03 |
| `_log` | absolute | 1.1425e-06 | 1.2e-06 | 2e-05 | 5.6011e-05 | 5.7e-05 | 2e-03 |
| `_log2` | absolute | 1.6482e-06 | 1.7e-06 | 3e-05 | 8.0807e-05 | 8.2e-05 | 3e-03 |
| `_log10` | absolute | 4.9617e-07 | 5.0e-07 | 1e-05 | 2.4325e-05 | 2.5e-05 | 1e-03 |
| `_tanh` | absolute | 1.2223e-06 | 1.3e-06 | 1e-04 | 2.7643e-04 | 2.8e-04 | 3e-03 |
| `_sin` | absolute | 3.1161e-07 | 3.2e-07 | 5e-06 | 3.6264e-05 | 3.7e-05 | 3e-04 |
| `_cos` | absolute | 3.1158e-07 | 3.2e-07 | 5e-06 | 3.6262e-05 | 3.7e-05 | 3e-04 |
| `_tan` | absolute | 4.7532e-07 | 4.8e-07 | 1e-05 | 5.6326e-05 | 5.7e-05 | 6e-04 |
| `_sinpi` | absolute | 3.1161e-07 | 3.2e-07 | 5e-06 | 3.6265e-05 | 3.7e-05 | 3e-04 |
| `_atan` | absolute | 5.2965e-10 | 5.5e-10 | 2e-06 | 1.8997e-07 | 2.0e-07 | 5e-04 |
| `_asin` | absolute | 5.3007e-10 | 5.5e-10 | 2e-06 | 1.9008e-07 | 2.0e-07 | 5e-04 |
| `_acos` | absolute | 5.3007e-10 | 5.5e-10 | 2e-06 | 1.9008e-07 | 2.0e-07 | 5e-04 |
| `_atan2` | absolute | 5.2960e-10 | 5.5e-10 | 4e-06 | 1.8996e-07 | 2.0e-07 | 6e-04 |
| `_pow` | relative | 3.2419e-06 | 3.3e-06 | 2e-04 | 7.9439e-04 | 8.0e-04 | 3e-02 |

Every bound is inside the old tree's for the same row and tier, which is the parity claim: a graph
ported across at a named tier does not quietly get a coarser answer than it had. That comparison is
a `static_assert` over the table rather than a note.

**Which old-tree path the `there` column describes, because it matters.** Its bounds are the ones
its FLOAT cores meet; its `double` tiers all forward to its exact core (finding S12-F64TIERS in
`docs/decisions.md`), so the float path is the only place its tiers exist to be compared against.
This tree computes in `double` everywhere, so its coarse cores differ at `double` where that tree's
do not — a deliberate deviation, and one the old tree's own rule accommodates: a bound is a ceiling
and over-delivering accuracy is contract-safe.

## Where a tier buys less, and why that is said out loud

**The inverse circular four land far inside the old tree's row — the approximate tier by a factor of
four thousand.** That is not a core that turned out unexpectedly good; it is a family whose cost is
not in its polynomial. The reduction brings every argument to `|u| <= 1/16` through an eight-centre
table and pair arithmetic, so the series is already short and shortening it further saves seven
multiply-adds out of a core that spends most of its time in `exact_product` and `careful_ratio`.

So the tier here is real, measured and distinct at all three rungs, and it buys less than in the
other two families. The deeper lever — doing the reduction in plain doubles and dropping the
argument correction, which is where the cost actually is — is deliberately not taken: the accuracy
it would cost is about 1e-17, seven orders below the truncation, so it would not be visible as a
tier at all, and it would put a second arithmetic path through a core whose entire argument is the
pair arithmetic.

## `_pow`, whose sweep is split for the failure mode rather than for coverage

`_pow` is the only row here built out of two other transcendentals — `x^y` is `2^(y · log2 x)` — so
it is the only one where a tier had two places it could go. It went on the final exponential's
series and not on the logarithm, because an error in the logarithm is MULTIPLIED BY `y` on its way
to the answer, and a bound that depends on an operand is not a bound. FINDING S12-POWLEVER in
`docs/decisions.md` is that argument in full, including the deviation in kind it makes from the old
tree, whose coarse `_pow` is `exp_fast_(b * log_fast_(a))`.

**Which is why the sweep is in two halves and why they must agree.** The ordinary grid, whose
exponent never exceeds ten, and a second sweep that places `y · log2 x` near ±1024 deliberately by
taking a base just above one — which drives the exponent itself from about 2 700 up to 7 million,
and an error amplified by it with them. They measure 3.2419e-06 and 3.2422e-06 at `Approx`, and
7.9439e-04 and 7.9445e-04 at `Fast`: the same number to four digits, six orders of magnitude of
exponent apart. `the_power_tier_does_not_grow_with_the_exponent()` requires that agreement within a factor of
two, and treats a near-limit half that is much BETTER as a failure too — because that is what a
sweep looks like when it is not exercising the tier at all.

**`_pow` measures what `_exp` measures, and that is the design showing through.** 3.2419e-06 against
3.2421e-06, 7.9439e-04 against 7.9443e-04. Once the amplified quantity is exact, the whole of the
tier is that one series, so the row cannot be anything but `_exp`'s row. A version of this table
where `_pow` was worse than `_exp` would mean the pair arithmetic had stopped carrying the logarithm
to more bits than a double holds.

## Four things worth reading rather than scrolling past

**No periodic operation is its own polynomial, which is why three of the four share a number.** The
quadrant switch sends `_sin` through the cosine polynomial on two quadrants of four and `_cos`
through the sine polynomial on the other two, so each one's worst case is the worse of the pair —
the sine's. `_sinpi` reaches its own pair the same way and lands on the same number, because its
polynomials are the radian ones in a rescaled variable: `pi^2/16` is 0.617, which is the radian
interval's `z` bound exactly. `_tan` is a little worse than the three because it is a quotient of
the two polynomials and carries both errors.

**`_exp` and `_exp2` measure identically to four digits at both tiers.** They share one series over
an interval of the same width and both reductions are exact, so there is nothing left for them to
differ about. A version of this table where they differed would mean one of the reductions had
stopped being exact.

**The three logarithms are one measurement scaled by base.** `_log2` is `_log` times 1/ln2 = 1.4427
and `_log10` is `_log` times 1/ln10 = 0.4343, and the measured numbers follow that to three digits
at both tiers. They share `logarithm_parts`, which is the hard half; a row that stopped following
would mean one of them had stopped sharing it.

**`_tanh`'s maximum is at -0.75 at both coarse tiers.** That is where the reduced argument reaches
the series bound of 1.5 — the last point the series covers before the fallback takes over, and so
where the dropped term is largest. The maximum landing exactly there is the evidence that what is
being measured is the truncation and not something else.

## What the numbers do not say

They are the distance between two cores of THIS tree, over the domain above, on this host and this
toolchain. They are not a claim about the platform's library, and they are not a claim about
arguments outside the swept domain — the special cases (NaN, the infinities, the overflow and
underflow edges) are answered identically at every tier, because those branches are taken before any
series is reached, and that is asserted where the cores are rather than measured here.
