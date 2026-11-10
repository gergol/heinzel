# Architecture

How Heinzel is built, as the code stands. What is not done yet is in `Plan/backlog.md`; why things are as they are
is in `docs/decisions.md`.

## What Heinzel is

Heinzel compiles a numerical graph while the program that uses it compiles. The graph can be a filter bank, a synth
voice, an image pipeline or a small neural-network layer. You write it as an ordinary `constexpr` function in a small
DSL. The C++ compiler then evaluates the library's passes as constant expressions: they analyse the graph, decide how
to run it, and turn it into a loop program. At run time only that program's loops run. The language is C++26, with
C++23 as a supported floor, and the compiler is clang.

```cpp
constexpr Index n{48'000};                    // the sample clock: 3 ms is 144 samples

constexpr Graph comb() {
    Input x{"x", Signal{64}};                 // a signal: 64 samples a run
    Output y{"y"};
    y[n] = x[n] + 0.5 * y[n - 3.0 * ms];      // defined by its own past
    return y;
}

Executor<comb> filter;                        // analysed, planned and lowered here, at compile time
filter.input("x") = x.data();
filter.output("y") = y.data();
filter.run();                                 // each run continues where the last one stopped
```

Audio and tensors share one language. A value is either a *signal*, whose last axis is time, or a *tensor*, which
holds the same numbers at every sample. A signal is processed in blocks, and its state (a recurrence, a delay, an
input read in its past) carries from one run to the next. So a signal run in blocks gives the same numbers as one run
over the whole of it.

Three goals shape the design:

- **Speed.** The yardstick is the competition: Eigen, JUCE, KFR, Faust, klang and hand-written loops. Results
  are compared like with like in precision, and bit-for-bit agreement with anything is not a goal in itself.
- **No run-time overhead.** Anything that can be computed at compile time is computed then. The aim is a binary
  within a few bytes of the hand-written loop's, and memory that is only what the program keeps.
- **No specialised kernels.** Matrix products, convolutions, attention, stencils and norms are all loop nests that
  the planner builds from the same few primitives. A fast Fourier transform is the one intrinsic planned.

## The pipeline

```
graph function     frontend/, notation/     a constexpr function that returns a Graph
   │ builds
Graph              ir/                      nodes and values: what is computed, never how
   │ infer         sema/                    shapes, element types, time, precision; or a refusal
analysed Graph
   │ proof         ranges/                  every operand inside its operation's domain; or a refusal
   │ rewritten     rewrite/                 the same results, in an order or form that runs faster
rewritten Graph
   │ schedule_of   loops/planner/           decisions, by a cost model and measured rules, for a Machine
Schedule           loops/schedule.hpp       stages, their loops, bands, splits: plain data
   │ lower_into    loops/lower*.hpp         statements, buffers and a tree; the legality check
Program            loops/program.hpp        the loop tree
   │ run           loops/run*.hpp, runtime/ one function template per tree node kind
numbers            executor.hpp             Executor binds the ports and owns the storage
```

**Building.** A graph function declares its boundary and its time with `Input`, `Param`, `Output` and `Variable`.
`Signal{…}` says that a value is a signal and how long it is, as a number or as a `Length` given at run time. A
recurrence that no signal drives takes the graph's length where the graph has exactly one, and otherwise declares its
own (`Variable phase{Signal{N}}`; Decisions 54 and 76). Index notation is the one way to read the past (`y[n - 1]`,
`x[n - 3.0 * ms]`, and a distance that changes, bounded, `x[n - d.at_most(25.0) * ms]`). Arithmetic operators, functions such as `where` and `sinpi`, members such as `x.sum(1)` or
`x.unfold(0, 3)`, and the `einsum` and `rearrange` notations add nodes. Statements may come in any order: a variable's
present read before its definition (`line[n] = x + fb * hp` before `hp[n] = …`) reads the definition, as a read
written after it does, and finishing puts the nodes in reading order (Decision 76). The loop tree refuses, for now, a
loop through an `Expr`'s past (make it a `Variable`) and a chain of lags written before their definitions, one needed
there only through another (write the lag after its definition; `docs/limitations.md`). The frontend records what was
written and decides nothing: it works out no shape and no element type.

**Analysis.** `infer(graph)` works out every value's shape, element type, time (signal or tensor) and precision, then
checks each operation's attribute. Symbolic extents, such as a block length given only at run time, are solved by a
union-find over axes. A graph that does not make sense is refused with a message that says why. The message names
an operation as the user wrote it where the DSL has one spelling for it (`einsum`, `+`, `unfold`), and by its
internal name (`_reduce`) otherwise.

**The proof of ranges** (Decision 74). A graph states what it knows of its numbers: a port's range in its declaration
(`Param gain{"gain", ge(0), le(1)}`, `Input x{"x", Signal{64}, ge(-1), le(1)}`), and a computed value's in a `promise`
(`(a + b).promise(gt(0), le(2))`). A range is a list of comparators (`gt`, `ge`, `lt`, `le`, and `ne` for a hole), held
as a union of up to four intervals. A composition states the range its result always has as a fact, where the walk,
which takes each operand's range alone, cannot see it: `frac` (`x - floor(x)`) lies in [0, 1], closed, since it
rounds to 1 for a tiny negative x; `abs`, an operation whose rule gives its range, states its at least 0 too, for a
variable's past. The proof trusts a fact as it does a port's range,
and nothing checks one; where a fact and the rules share no number, the walk keeps the rules' range. `ranges::proof` carries the
ranges through the analysed graph, node by node, and checks every operand against its operation's domain: no division by
a range that holds 0, no `sqrt` below 0, no logarithm of a number at or below 0, no `asin` outside [-1, 1]. A port
without a range may hold any number, and every graph is held to the proof: the first finding refuses it, by name (`/ may
divide by zero: the divisor is ge(0), le(2)`). A promise narrows its value's range. It is proven where the computed
range lies inside it, and on a recurrence's definition that is a proof by induction; otherwise a debug build checks it
while the graph runs (`Executor`, below). Then the graph's ranges are cleared: no rewrite, plan or program sees one, so
a release build plans the program of the graph written without them, but for the one mark the proof leaves the graph,
the same in every build: a table read whose phase lies in one turn, [0, 1], at every precision, which then takes no
wrap (Numerics, below). It is the one place where the proof makes a program faster, not only a graph safe: what it
rests on is a walk of its own, which trusts only what no rounding moves past 0 or 1 and nothing a caller states
(`ranges/in_every_rounding.hpp`). The proof runs between `infer` and the rewrites,
not inside `infer`, because the rewrites analyse graphs again whose promises a debug build has already turned into
checks.

**Rewriting.** `rewrite::rewritten(graph, machine)` hands the planner a graph that computes the same results, its
ports by the same names, in an order or a form the processor runs faster. A rule that changes rounding fires only
where the value's precision grants it, so at `High` none does, and every rule reads the rule table's columns, never an
operation's name. First, made by compiling on the analysed graph so that the graph as written takes it too, a delay
line over an input reads a copy of the input that keeps its past, as a computed value's buffer does (Decision 78): a
caller's feed keeps none. Then the rules run, in this order. A linear filter, a chain of second-order sections, is computed in
its parallel form, a bank in double, where that keeps the cascade's accuracy (Decision 66), inside an enclosing loop
only where the loop's chain does not grow (Decision 71). A run of like feedback sections the parallel form leaves (its
coefficients Params, repeated poles, saturating sections), each reading the one before at the same sample, becomes one
cycle over a section axis, each section linked to the one before by an internal `_chain`, where the cost model prices
that below the cascade by more than 3 %: a stage runs it skewed in lanes, section j a sample behind section j − 1
(Decision 69, `docs/sections-in-lanes.md`). A feedback cycle's sum is ordered by when its terms are ready:
what does not wait on the cycle first, its newest term last, fused into an internal multiply-add (`_madd`, rounded as
the machine rounds) where the machine has one. Then a cycle at `Normal` keeps its state and the work between its cycles
in double, converted down once where it is read outside, where the cost model prices the stage it runs in no dearer so
(a tie within 3 % widens) or where Normal promises it (Decisions 66 and 68). Last, a value written twice is computed
once (Plan/33 S1): two nodes of one operation, attribute, type, shape and precision over the same values are one,
literals the same value where their type and bits agree (-0 and a NaN's payload apart), where the rule table's sharing
column says the operation computes nothing but its value (not `_realize`, not `_recur`); a value with a range mark
stays apart, and no output is merged into another value. Last, so that the rules see the graph as written: a lag two
sections read, merged, would hide a cascade from the parallel form.
`try_compile` rewrites before it plans; the tests' `planned::` helpers plan a graph as it was written (and, like
`planned_as_rewritten`, leave out the proof).

**Planning.** `loops::planner::schedule_of(graph, machine)` decides how the graph runs:

- which nodes share a *stage*, that is one loop nest. Element-wise work is fused, a fold takes its producers inside
  its loops, and a feedback cycle runs a sample at a time. What joins a feedback stage is the cost model's call
  (Decisions 65 and 68): cycles, with the cheap maps leading into them, share one stage where that costs no more than
  closing it, whether they feed each other or hand each other nothing, so the processor overlaps their latency chains
  (a cascade of filter sections, a reverb's combs), as far as the machine's registers hold their state; the feed
  before the first cycle is weighed whole as the stage closes; and the work after the last cycle joins as far as the
  model prices that cheapest, the rest in lanes. A one-sample read of what the stage makes runs before its maker, so it
  is handed on in a register. A cycle holding one fold over its voices (a bank whose sum the loop reads back) is one
  stage that folds the voices every sample: the part before the fold at width one, the voices in lanes, the fold
  finished and the part after it at width one, the voices' state in registers a body each and the one-voice state in
  scalars (Decision 71); a bank's sum, from a lane's worth of voices, runs in the bank's sample the same way where the
  model prices that no dearer. A cycle holding a link (sections in lanes) is a *skewed* stage: its sections along a
  vector of the machine's lanes, time innermost, each lane a sample behind the one before. It holds its cycle and what
  leads from it to its last section's slice, nothing else (no feed, no work after the slice: a named stand-in for the
  trailing rule's price), and no band holds it nor does a worker split it (Decision 69);
- each stage's loops, outermost first: serial loops, vector lanes, unrolled bodies and cache tiles;
- for a contraction, whether its multiply and add fuse, whether a strided operand is read from a copy laid out for
  the lanes, and whether to fetch ahead;
- whether a cheap producer read through a window is computed inside its reader instead of being stored, and whether a
  map one later stage alone reads runs in that stage (the cost model's call);
- which consecutive stages run as a *band*, chunk by chunk under one outer loop, so that a pipeline larger than the
  cache keeps only a few rows of each intermediate;
- which stages and bands may be split among worker threads;
- which stage runs a run's first sample apart (*peeled*): one that reads a run's start, `_first`, an internal
  operation that is 1 at a run's first sample and 0 at every other, stored nowhere and computed where it is read (the
  rule table's run-start column; Plan/33 S3). No band holds it and no worker splits it, and the lowering refuses it in
  lanes, until a benchmark asks.

Every rule is measured, or priced by the cost model where Decision 68 moved it there; a rule kept in the model's place
says so in its comment (a named stand-in). Its constants are fields of `Machine` (vector width, registers, cache sizes,
cores, fused multiply-add, measured thresholds, the model's latencies and issue rates) or measured literals beside the
rule. The result is a `loops::Schedule`: plain data that can be printed, compared, and written or edited by hand in
tests.

**Lowering.** `lower_into(graph, schedule, program)` builds the program the schedule describes. It forms each stage's
statements and their affine accesses, makes the buffers, places the bands, and normalises each stage into a tree of
nodes. It decides nothing. What follows from the schedule it works out again rather than trusting it, so a schedule
that is not legal for the graph is refused *by name*: with a message that says which rule it breaks.
`loops::plan_and_lower` runs both steps and returns the schedule and the program (`plan_and_lower_into` makes them
in its caller's object); a refusal from either is a value
that starts with `heinzel::compile:` (`loops::Refusal`: a literal, or a text it holds; 95 characters at most). A node no stage can run
is refused with its operation as the user wrote it (an order statistic by its member's word: `topk 3`, `median`), the
case, and its operands' shapes, last, so a cut loses shapes, never the reason: "heinzel::compile: topk 3 is not one
exact rank, which is not lowered yet: [6, 64]" (`planner/stages.hpp`, Decision 77).

**The program.** A `loops::Program` holds:

- the buffers: element type, shape, how many past samples each keeps, and, for a band, the rows it keeps per chunk;
- the statements: a map, a load, a gather, a fold or a rank selection, each over affine accesses;
- the stages: a domain and its statements, in the order they run;
- the bands: runs of consecutive stages;
- for each stage, a tree that says in which loops, in which order, at which width and in which registers each of its
  statements runs;
- for a skewed stage, three facts: `Stage::skewed` (its lanes are its sections, lane j standing j samples behind lane 0,
  so its time loop runs k − 1 steps past the block), `Node::shifted` on the link's Compute (it reads its state register
  moved one lane up, lane 0 taking the cascade's input, the fill) and `Node::lane` on a Compute that
  stands at one lane (the fill's Load at lane 0, the last section's slice at k − 1: that lane's value, read and stored
  at that lane's sample).

Each fact is stored once: a band's buffers are the buffers that name it, a stage's tree is rooted at its `Stage` node,
and the order of the stages is their order in the program. Its capacities are fixed. It travels as
`template <const loops::Program& P>`, never by value.

**Running.** `loops::run<P>(feeds, workspace)` runs the stages and bands in order. The *feeds* are the input buffers the
caller owns, read where they lie. The runner has one function template per kind of tree node, and reaches every node by
index at compile time. So the loops it instantiates are ordinary code: pointers stay in registers, strides are constants
where extents are literal, and lanes are compiler vector types. A read that no loop of its stage moves (a filter's
coefficient, say), of a workspace buffer the stage does not store, is copied into the frame that
places the stage and read there, so the compiler loads it once where a store of its type might otherwise change it
(`walk.hpp` `still_slots`, Decision 80); in a skewed stage so is a read that moves only along its lanes, its k sections
copied (the coefficients a filter's bands gather, Decision 69). A skewed stage runs a block of B samples in B + k − 1
steps, as three loops (`run_nodes.hpp` `run_skewed`): the head steps, until every lane is real, the plain steps, and
the k − 1 drain steps after the block. In the head and the drain a lane whose sample lies outside the block keeps its
state by a blend, so a run computes exactly its own block and leaves the state any other stage leaves; in each step
the bodies run last first, so each reads the one below as the step before left it. A peeled stage is handed its walk
at the run's first sample marked as such (`AtStart`, `walk.hpp`), and its time loop's first iteration keeps the mark
while the others take it off (`run_clip`, the peel a fold's first element takes too), so a read of a run's start is a
constant 1 in the first sample and a constant 0 after: what only a start computes (`dsp::smoothed`'s blend and its
division) is code the compiler drops from the samples after it. A run is one call: `Executor` makes one `loops::run`
of every `run(frames)`. The runner decides nothing and checks nothing. It asks one question, of a *kept* stage alone (one with no loop that works out values from Params, as
a filter's coefficients from its knobs): whether a Param it watches changed, bit for bit, since it last ran
(`Workspace::changed`). Where none did, the stage does not run, and its results are still in their buffers, which no
other stage writes; a workspace just made runs every kept stage, and `reset()` leaves their results, which depend on Params alone (Decision 80, `docs/knob-math.md`). A
workspace is one allocation. Its buffers are placed with a stagger against 4K aliasing, which `loops_workspace`'s
goldens pin. A run leaves the state the next run reads (`loops/streams.hpp`): the last samples of each buffer whose past
is read, the tail of each feed read in its past, and the last rows of each *stream band*, a band that runs its stages a
few samples at a time. A state handed on in registers that nothing else reads (no output, no other statement) and whose
past they hold is stored nowhere else: its buffer holds its past alone, which its registers open from before the time
loop and close into after, in place. Given a thread pool, `run(feeds, workspace, pool)` may split the stages and bands
the schedule marked, when the work is large enough; the result is bit for bit the serial one.

**`Executor`.** `Executor<build>` runs the whole pipeline at compile time (`try_compile`). Its optional second template
argument is the `Machine` to plan for (`Executor<comb, zen2>`; `default_machine` if omitted). A refused graph is a
single build error whose message is the refusal (on C++23, a fixed text; `try_compile` gives the reason), a graph over
a capacity too: finishing counts it first and hands on a graph that says which capacity it passed (`Graph::over`),
which `infer` refuses first, in that capacity's words. A graph function that stops while it runs (a `fail`, or the
step or depth limit reached by the function alone) is asked about first (`built`, a `requires` that is false where the
function is no constant expression), so that the compile result is a refusal and not some 20 errors, one for each
member read off it; the one error is the function run once more in a `static_assert`, whose notes give `fail`'s words
and the line (Decision 77). A limit reached later, while `try_compile` plans, still gives the 20 errors. Its third,
`Checked`, is `true` unless `NDEBUG` is defined (`detail::kChecked`, `compile.hpp`), as `assert` is. A checked
`Executor` holds a ranged `Param` to its range when it is set, a ranged input's numbers before each run, and after it
each promise the proof did not prove, which the program writes as an output `#k` of its own: the first number outside
fails, in words (`promise #0 broken at sample 3: 0 is outside gt(0), le(2)`; a value with no time axis, as a weight
tensor's or a Param's, `at element 2`), the numbers it found before the range, each in the shortest digits of the
double it was compared as (a float's 0.1 reads 0.10000000149011612 beside `le(0.1)`); a long port name is cut so that
they always fit. The checks' outputs are other readers of the values they check, so a debug build may plan another
program than a release one (a filter cascade checked in its middle stays serial); the two are different types, so a
debug and a release unit never share one. Port names are checked at compile time. A user binds `input("x")`,
`output("y")` and `param("gain")`, then calls `run()`. A graph with a `Length` calls `prepare(max_frames)` once, then
`run(frames)`. Each run continues the state the run before left, so a signal run in blocks gives the numbers of one run;
`reset()` starts it over from silence. An output is written straight into the buffer bound to it, unless it keeps a past
or its buffer overlaps an input the program reads or another output: then it is written into the workspace and copied
out after the run (`Plan/18-zero-copy/02`).

`compile` and `try_compile` (`compile.hpp`) are the same pipeline, public for inspection and debugging; they take the
`Machine` as their second argument, and `checked` last (`detail::kChecked` by default). They run at compile time, or at
run time for a graph whose sizes arrive late. A `compile` that stops while compiling shows a refusal written as a
literal (a capacity's, most of the loop tree's) in clang's note, and an assembled one (the analysis's, the proof's, a
node's) as an address: `try_compile` gives its words. `CompileResult::ranges` holds what a checked `Executor` checks.
`try_compile`'s steps are one function, `detail::attempt`, which holds the plan (a `Planning`, about 200 KB) on the
heap (under constant evaluation freed before it ends) and makes each plan in place (`loops::plan_and_lower_into`): at
run time no frame of `try_compile` or `print` holds a plan or a program. `print<g, M, Checked>()`
(`app/heinzel/print.hpp`) takes the same steps at run time and prints what `Executor<g, M, Checked>` runs: the graph
as planned, one line a node (`n8 v11 = tanh v10 [256] f32`: node 8 makes value 11), the schedule (`loops/print.hpp`;
`s1 = n8`, the stage that runs node 8, then its loops) and the program; or else the refusal, the compiler's or
`Executor`'s (`detail::why_not_run`, `ports.hpp`, which both ask), or the error the graph function threw.

## The directories of `include/heinzel/`

The core layers, in order, are `ir`, then `sema`, then `schedule`, `runtime`, `loops` and `rewrite`, with `ranges`
beside the last two. A layer includes only layers before it (and `support/`): `sema` includes `ir`, `loops` includes
`runtime`, `schedule` and `ir`, `rewrite` reads of `loops` its graph facts alone and nothing of `runtime`, so that it
cannot reach the planner it runs before and emits the same graph in every build, and `ranges` reads `ir` and of
`runtime` the operations' own writing alone. The
authoring layers (`frontend`, `notation`, `dsp`) include `ir` and none of the later core layers. Gates hold both.

**`ir/`: the graph.** `ir.hpp` defines the `Graph`: values, nodes, named inputs and outputs, and the run-time
dimensions it declares, all plain data of fixed size. `vocab.hpp` defines element types, shapes, extents (literal or
symbolic) and the operation codes. `op_rules.hpp` is the *rule table*: one row per operation code (61 codes, 59
operations: an operation folded into another retires its code, whose row stays empty) and one column per fact a
pass needs, such as its shape rule, element-type rule, timing, element meaning (which operand elements each result
element reads), cost, numeric contract and how it differentiates; beside them, its internal name and, where the DSL
has exactly one, its user spelling, which refusals use. The columns are spelled in `rule_columns.hpp`. Passes read
columns instead of branching on the operation, so adding an operation means adding a row, and static checks make a
missing or half-filled row a build error. A rearrangement's shape follows its view's kind (`ViewKind`) rather than a
column, `timing.hpp` says which operands are read in the past, and `numeric_setting.hpp` says what a precision means
for accuracy and freedom of order.

**`frontend/`: the DSL.** `arena.hpp` defines `Expr`, the handle a user holds. A leaf owns a one-node graph, and
composing two expressions merges the smaller graph into the larger (`docs/decisions.md`, Decision 1).
`graph_builder.hpp` holds the leaves, the operations, recurrences and finishing a graph, which puts its nodes in
reading order where a variable's present, or a lag no loop needs there, stands before its definition
(`reading_order.hpp`). `declarations.hpp` holds
`Input`, `Param`, `Output`, `Variable`, `Signal` and `Length`, and the steps of a delay that changes, `d.at_most(b) * ms`
back from `n`, each a private type of `detail` (`arena.hpp`) that only the next step takes, the last a `_dline` of the
position in samples and its longest distance rounded up. `ops.hpp` holds the structural operations and the
members that spell them. `subgraph.hpp` holds `subgraph`, which makes a reusable function a named piece, applied
once, and `dynamic_extent.hpp` records a length known only at `prepare()`. A table (an `Input` of a shape, a
`constant(…)`) is read by `wavetable(table, phase)`, its N cells one cycle, and `lookup(table, x, lo, hi)`, a
`wavetable` of x's clamped place: one `_tblread`, built with the cells the leaf states, its far cell the first after
the last (`runtime/table_gather_element.hpp`, Decision 78); inside a feedback cycle too, a map of its phase.

**`notation/`: `einsum`, `rearrange` and `reduce`.** These parse their spec strings and build nodes through the
frontend; they add no operation (an aligned `einsum`, `"bf,bf->b"`, is the graph of `(a * b).sum(1)`, Decision 78). The
streamed axis is never named in a spec. A regroup (`"(c f) -> c f"`) is recorded as written, its labels' sizes and
where each side's groups start, and the analysis works its target out once the operand has a shape, as it completes
`x.reshape({…})`, so a computed value regroups as an input does. A spec that reorders and regroups at once is a split, a
transpose and a merge, each only where its side groups, as einops lowers it.

**`units/`: durations.** Named units of time, `ms`, `us` and `second` (also spelled `heinzel::ms` and so on, so
`using namespace heinzel;` brings them), and `to_samples`, which turns a duration into an exact number of samples at
compile time. A duration is the only quantity: there is no dimension algebra.

**`sema/`: the analysis.** `analyze.hpp` holds `infer`. It gives a recurrence no signal drives the graph's one length
(`time_checks.hpp`), sweeps shapes and element types together until nothing changes, types what is made of numbers
alone by its reader, else a `where` by its condition (`untyped.hpp`), makes a variable that took the length and that
nothing typed f32 and does both again, holds declared variables to their declarations, decides which values have
time, resolves precision, then checks each operation's attribute and operands, and requires every value to be known.
`solver.hpp` is the union-find over symbolic extents. Each family of structural operations has one function that
tests its conditions once, in the order its refusals are reported (`view_shape.hpp`, `framing.hpp`, `order.hpp`,
`spectral.hpp`, `overlap_add.hpp`): in the sweep it hands its operand's shape back where it cannot make one (a
regroup not completed yet hands on none, so its reader waits for it), and `attribute_checks.hpp` calls it once more,
node by node, to word the first refusal (`refusal.hpp`). A condition the shape does not need is asked only then.
`matmul.hpp` shapes a general contraction and checks it apart, and `attribute_checks.hpp` also holds the checks that
make no shape. `time_checks.hpp` completes the views a member or a regroup left marked, decides signal or tensor and
what each forbids, before any attribute is checked; a regroup it could not complete is worded there.
`resolve_extent.hpp` checks a run-time size against a `Length`'s constraints.

**`schedule/`: the target.** `machine.hpp` defines `Machine`, the facts about a processor that the planner reads, with
`zen2`, `m1_pro` (its thresholds still zen2's until measured, Plan/23 P6) and `default_machine`. With
`-DHEINZEL_AUTOTUNE=ON` a fourth, `host`, is measured at configure time on the machine CMake runs on and written into
the build directory (`heinzel/host_machine.hpp`, `tools/tune/machine.cmake`, Decision 72): plain `constexpr` data like
the others, which code names where it wants it; it is never the default, and the tests never see it. A build for a
machine passes one of these; the planner asks the compiler nothing about the instruction set. A wrong value costs speed,
never correctness, and at `High` never a bit (at `Normal`, the lanes and fused multiply-adds it implies decide how sums
are ordered). `compile`, `try_compile` and `Executor` take one. `cost.hpp` is the planner's model of what a stage costs
a sample (Decision 68): the most of its chain and its issue taken together (their p-norm: work beside a chain is free
only while the chain dwarfs it), its loads and stores, and the out-of-order window's bound, and at least what it moves
through memory past the last-level cache; each operation priced by its rule-table class (`Latency`) and the machine's
figures, validated against measured programs (`Plan/probes/audio-iir/cost_corpus*.cpp`).

**`loops/`: the loop tree.** It has four parts.

- **Graph facts** (`graph/`) are what the planner and the lowering both read off the graph. They say how the rule
  table classifies each node (a map, a fold, a view, a gather and so on), how a view becomes an affine address instead
  of a copy, what shapes a stage's nodes run at and which axes its fold collapses, which reads of the past close a
  feedback cycle, and where each value is made and which stages read it. What the passes ask of the graph again and
  again is worked out once per graph, in linear time (`facts.hpp`): each value's producer, what a read of it finds,
  what it is read through and how far back it is read, and whether each node is a statement. The planner and the
  lowering each build that table once and hand it on. Both read the nodes in the order the frontend builds them, and
  refuse a graph edited so that a node reads what it or a later node makes.
- **The planner** (`planner/`) runs one pass per file. `stages.hpp` decides which nodes share a stage, asking
  `gathering.hpp` (the stage still open), `chains.hpp` (cycles chained onto it, and what bounds a merge) and `trail.hpp`
  (what follows a feedback stage's last cycle); `in_the_reader.hpp` moves a map into the one later stage that reads it,
  and `loops.hpp`, `contraction.hpp` and `feedback.hpp` decide each stage's loops. `inline.hpp` decides what a window
  recomputes, `bands.hpp` the bands and `parallel.hpp` the splits; `kept.hpp`, last, which stages with no loop that work
  out values from Params alone run only in a run where a Param they watch changed (`docs/knob-math.md`). `plan.hpp` holds
  `schedule_of`, `plan_and_lower` (and `_into`) and `lowers`. The schedule itself is `schedule.hpp`. Lanes, splits and
  whether a band splits are decided before any statement exists, so the planner asks of the graph what the lowering
  will check of the accesses, as strictly or more so.
- **The lowering** (`lower.hpp`, `lower_form.hpp`, `lower_stmts.hpp`, `primitives/`, `normalize.hpp`, `validate.hpp`)
  builds and checks the program. `band_layout.hpp` works out a band's layout, and the planner and the lowering ask it
  the same question, so they cannot disagree. `program.hpp` defines the program, `tree.hpp` answers questions about
  its tree, and `print.hpp` prints a program or a schedule as text.
- **The runner** (`run.hpp`, `run_stage.hpp`, `run_band.hpp`, `run_nodes.hpp`, `walk.hpp`, `registers.hpp`,
  `values.hpp`) runs a program. `workspace.hpp` owns its storage, and `streams.hpp` says what a run carries to the
  next.

**`rewrite/`: graph to graph, before the planner.** `copied_inputs.hpp` gives a delay line over an input a copy of
it to read, a `_realize`, made in place before the other rules (`compile.hpp`). `rewritten.hpp` runs the rules in
order, last `equal_values.hpp`, which merges the nodes written twice, one pass with a table of fixed size. `parallel_form.hpp`
(its algebra in `partial_fractions.hpp`) computes a linear filter, a chain of second-order sections
(`linear_sections.hpp` recognises one), as the sum of its partial fractions: a bank of sections over the chain's input,
in double, where the filter's result is narrower than double and its fractions, each at its peak, sum to at most 150
times the filter's own gain (elsewhere the bank's
rounding would show); inside an enclosing loop too, the bank's sum then on the loop (Decision 71).
`sections_in_lanes.hpp` links a run of like sections (`like_sections.hpp` recognises one) into one cycle over a section
axis where the cost model prices it below the cascade, a coefficient the sections read from Params gathered into one
`[k, 1]` value by a chain of `where` (Decision 69). `feedback_sums.hpp` orders a feedback
cycle's sum by when its terms are ready and fuses its newest term; `double_state.hpp` widens a cycle to double where
the cost model prices its stage no dearer so (`loops/graph/cycle_chains.hpp` `cycle_stages`) or Normal promises it,
with a conversion down where it is read outside. Each rule works out the
shape, type and time of every value it makes as `infer` would (the tests hold it to that), and returns its graph
unchanged where it finds nothing or where the result would not fit the graph's capacities.

**`ranges/`: the proof of ranges.** `interval.hpp` is arithmetic on ranges, piece by piece: sums, products (a value
times itself is a square, so at least 0), reciprocals by side, min and max by the operand that decides each end, and a
monotone operation's ends computed by its own writing (`compute_as<double>`), an end whose image is the operation's
limit at an infinity kept open. More than four pieces are merged across the smallest gap, which only widens a range.
`proven.hpp` holds the walk (`ranges::proof`): a port takes its declared range and a constant its number; each node
checks its operands against the rule table's `Domain` column and computes its result by the `RangeRule` column, so it
names no operation, and a fact narrows the result it is stated on; a read of the past adds silence (0); a promise
proves, narrows or contradicts; after a finding the value is any number, and the findings that follow from one are
grouped under it. `ranges::message` words a finding as the user writes ranges, within the 95 characters a message holds;
the remedies are in `docs/limitations.md`. `in_every_rounding.hpp` keeps of the proof what holds at every precision,
whatever the program rounds: a constant's number, the rules that make no number (a view, a lag, a recurrence, a
conversion, `min`, `max`, `abs`, a selection, a comparison, a join), silence and the facts of `abs` and `frac`, never a
port's range or a promise; `compile.hpp` marks by it each table read whose phase lies in [0, 1]
(`TableAttr::phase_in_one_turn`), which the rewrites carry with the node and the lowering copies into its statement.

**`runtime/`: what operations compute.** `compute.hpp` holds `compute_as<V>`, the one writing of every operation's
arithmetic. `V` is `float`, `double` or a `Lane` of either. The cores (`*_cores.hpp`) compute roots, logarithms,
exponentials, trigonometric functions and roundings in double. They are written so that they work in a constant
expression, and the same code runs at run time, so no number depends on the platform's math library.
`float_cores.hpp` computes `exp`, `tanh`, `sinpi`, `sin` and `log` in the value's own type, for float lanes.
`lane_value.hpp` defines `Lane<T, W>` over the compiler's vector extension. `hardware_twins.hpp` swaps in the
processor's instruction at run time where IEEE 754 makes it bit-identical. `fold_arithmetic.hpp` and
`table_gather_element.hpp` hold arithmetic that names no operation. `thread_pool.hpp` is a caller-owned pool, inert
unless `HEINZEL_HAVE_THREADS` is defined.

**`autodiff/`: reverse-mode differentiation.** `gradients(graph, loss, targets)` is a Graph → Graph transform: it
appends the nodes of the gradient, and the result compiles and runs like any graph. The rule table's adjoint column says
how each operation differentiates, and the `rules_*.hpp` files hold the adjoints per family. No core header includes
`autodiff/`.

**`support/`: the plain building blocks.** `fail.hpp` is the one way to report an error: a throw, which stops the
build under constant evaluation and is an ordinary exception at run time. `fixed_vector.hpp` and `fixed_string.hpp`
are containers whose capacity is part of the type, so a graph built in a constant expression can be kept.
`order_net.hpp` builds the compare-exchange networks behind sort, top-k and quantile.

**`dsp/` and `audio/`.** `dsp/oscillator.hpp` is an oscillator composed from DSL spellings; `dsp/smoothed.hpp` a knob
smoothed as Cmajor smooths one, a ramp restarted where a run starts with the knob changed (its change asked of a run's
start, `_first`, which users do not spell). `audio/wav.hpp` is a WAV
reader and writer for host programs. No core header includes either.

**Top level.** `compile.hpp` holds `compile` and `try_compile`, with `take_ranges` (the proof's place, after `infer`)
and `detail::kChecked`, and `executor.hpp` holds `Executor`; `ports.hpp` its ports and the words of its checks.
`app/heinzel/dsl.hpp` is the one include for users, and `app/heinzel/gradients.hpp` spells `gradients(loss, {…})`
over the frontend. Both live in `app/` because they join what the core keeps apart: the frontend with `Executor`,
and the frontend with autodiff. `app/heinzel/print.hpp`, `print<g>()`, is a user's include beside them, which
`dsl.hpp` leaves out, so that a program that only runs graphs compiles none of it.

## Design rules

- **The IR is data.** The graph, the schedule and the program are plain structs and arrays with a generated `==`,
  not nested template types. Passes are ordinary `constexpr` functions. They run at compile time and at run time,
  and can be debugged and tested at run time.
- **Nothing allocated escapes constant evaluation.** A pass may use a `std::vector` while it works, but what it
  returns has fixed capacity. Capacities live with their data: the graph's (`kMaxNodes`, …) in `ir/`, the
  program's (`kMaxStages`, `kMaxStmts`, …) in `loops/program.hpp`. Passes stay linear in the graph. Raising a
  capacity to rescue a pass that is not linear is not allowed.
- **A large value is never a template argument by value**, because clang miscompiles that. The program is passed as
  a reference (`template <const Program& P>`), and `Executor` takes the graph as its function and builds it inside.
- **No pass names an operation.** Whether a transform applies is asked of the rule table and of access maps. "Is
  this a matrix product" is not a question a pass may ask.
- **One writing per operation.** Within `include/`, an operation's arithmetic is written in `runtime/compute.hpp`
  only; the gate covers `include/`. The loop tree calls it. The reference walk calls it too, but dispatches the
  transcendentals, conversions and lags itself (`tests/reference/compute_reference.hpp`), over the same cores.
- **Tests hold a program as a subobject** (`planned::Held`) and name that, never a whole `constexpr` variable:
  clang rebuilds a whole variable's initializer each time, which made one test file take 69 s instead of 28 s.
- **One constant-evaluation branch.** Only `runtime/hardware_twins.hpp` may ask whether it is being evaluated at
  compile time. It does so for square root, fused multiply-add and the four roundings, which IEEE 754 defines
  exactly, and a test proves each one bit-identical. Everything else runs the same code at compile time and at run
  time, which is what lets the two be compared.
- **No instruction-set macros**, but one question: `runtime/hardware_twins.hpp` asks whether the target has the
  instructions its twins use (fused multiply-add, rounding). Everything else about the target is a `Machine` value,
  and lanes are compiler vector types.
- **Every transform ships with three things:** an eligibility rule that fits in one sentence, a correctness oracle,
  and a benchmark that shows the win. Without a win it is not merged.

The rules are enforced by *gates*: CMake scripts in `tests/gates/` that run with the test suite. They cover file and
function length (600 and 60 lines), the include order of the core layers, the closure of `loops/`, `loops/` headers
compiling alone, the reference not including what it checks, autodiff and the frontend kept out of the core, no branch
on an operation code, one writing per operation, one constant-evaluation branch, no instruction-set macros, extents
(literal or symbolic) read through their accessors and never as plain integers, one way for tests to build a graph by
hand, no retired spelling in user-facing code, and no thread without the option. Each kind of gate is also run against
a planted offender (its `_would_object` twin), so a gate that silently checks nothing fails.

## Numerics

Every value has a precision, resolved during analysis. A `.precision(…)` mark, written where a value is made
(`(a + b).precision(High)`, a port in its declaration), pins it. Every other value takes the highest of the graph's
default (`Normal` unless finished at another level, `finish(High, y)`) and its operands' levels, so the default is a
floor: in a `Normal` graph, a value computed from a `Fast` pin is `Normal`. A precision grants freedoms
(`numerics::grants`) that a schedule may take or leave.

- **`High`** grants nothing. Every operation is computed in double, in the written order, and rounded once to its
  result type. A fold accumulates in double from its first element. This is what the reference walk computes, so
  the loop tree at `High` agrees with it bit for bit. It is the tests' reference and makes no claim on speed.
- **`Normal`** is the default and the benchmarked level. A `float` value is computed in float, and its transcendentals
  use the float cores where they have one (below), held to within 2 ulp of the double core; but a feedback cycle keeps
  its state in double where the cost model prices that no dearer, as fast and far more accurate (one filter, a cascade,
  a damped comb, a loop around a bank: a core on the chain, a voice bank, a comb whose samples run in lanes and cycles
  merged into a stage their conversions would load stay float), and always a one-voice cycle of cheap work whose past
  the registers hold, its promise (Decision 66). A fold may reassociate (lanes, tiles), and `a * b + c` may become one
  fused multiply-add. It matches the precision of the libraries it is compared against: neither sloppier nor paying for
  accuracy they do not deliver.
- **`Fast`** also lets an operation trade accuracy for speed: the double cores evaluate shorter series. The float
  cores do not use it yet; the planner choosing faster, less accurate versions is planned.

**What the proof of ranges guarantees** (Decision 74): no operation receives a value outside its domain, for the true,
real-number values the graph describes, given its ports within their declared ranges and its promises kept. It does not
cover overflow (`exp(1000)` is infinite, and a sum of large numbers may be), nor the program's own rounding: a value is
computed in its type, so a number the proof keeps just inside a domain may round onto its edge (an `f32` Param declared
`gt(0)` and set to 1e-46 holds 0). The proof computes in double, with each number as the graph writes it: an f32
`frac` of a number just below 0 is 1 where the proof keeps it below 1, and a scalar literal is judged as its double
(1e-46 above 0, which f32 holds as 0). A NaN fed in lies outside every declared range and an infinity outside every
bounded one, and a checked `Executor` refuses both there; release trusts the caller. `docs/limitations.md` lists where
the proof is too coarse and what to write instead.

The loop tree runs a transcendental where the rule table says it has a writing over the value type. `exp`, `log`,
`tanh`, `sin`, `cos` and `sinpi` have a core in the value's own type, a float core for a float (the cosine's is the
sine's, a quarter turn along). `tan`, `pow` and `exp2` have only their double cores: a float is widened, its core run
and the answer rounded back, one number at a time, which is what the walk computes, at every precision, and slow
(Decision 78; Gerald: none of these capabilities may go). Their float cores come where a benchmark asks; until then the
cost model prices them as float cores, a stand-in. A graph that holds any other (the inverse circular functions, `log2`
and `log10` as operations) is refused at every precision, with a message that names it ("heinzel::compile: _atan is not
lowered yet: [64]"). The reference walk runs them all. `log10` and `cospi` are spelled as compositions of these
(`log(x) * log10e`; `sinpi(1/2 - r)` with `r = |x| mod 2`, an exact reduction), so they run: over every f32 value at
most 2.03 and 1.70 ulp at `Normal`, 1.37 and 1.06 at `High` (Decision 75). A mark on `log10`'s result pins only its
product (precision flows forward); its log takes `High` from a marked operand or `finish(High, …)`.

The two reads whose address is a signal, a table read and a delay line, compute where a map would: at `High`, and over
any double operand, in double, a lane at a time, the walk's steps (`table_tap`, `signalled_tap`, `lerp2`) and its
numbers; an f32 read at `Normal` and `Fast` in float, as a plain loop does, the same steps on the float its position is
over every lane at once and only its two loads a lane at a time (`loops/values.hpp`, Decision 78). The float read stays
in its table and its line, and within a bound of the walk's (`tests/support/float_reads.hpp`, `docs/limitations.md`).
A table read whose phase the proof keeps in [0, 1] at every precision (`frac`'s, `lookup`'s clamp, a variable they
define, a lag of them; a variable's past only by its definition's fact, `frac`'s) reads it with no wrap
(`table_tap<false>`): the same cells and fraction to the bit, at 1, -0 and a NaN too, so at `High` it is still the
walk's, which always wraps; a place below 0, which only a false fact of a graph built by hand makes, reads the first
cell, so the read stays in its table. A phase only declared or promised in [0, 1] is wrapped: release checks neither.

## How correctness is checked

- **The reference walk** (`tests/reference/`) interprets a graph directly: one sample at a time, in the written order,
  in double, with no plan. It is the ground truth. Tests run a graph on `Executor` at `High` and on the walk, and
  require every bit to agree (`tests/support/executed.hpp`). A corpus holds every operation (`both_ways.hpp`): each is
  walked, the ones the loop tree compiles also run on `Executor`, and `_fft`, whose backend is not written, is only
  analysed. A graph the loop tree still refuses runs on the walk alone, with its refusal pinned, so that the test
  fails on the day the loop tree learns it.
- **Every program a test makes is held to what the runner accepts** (`tests/support/well_formed.hpp`). Production code
  does not check this: the lowering is the only source of programs. Canaried runs (`tests/support/canaried.hpp`) also
  fill the workspace outside every buffer with a pattern and check it afterwards.
- **Two worlds.** The same graph is built and compiled under constant evaluation and at run time, and the results must
  be equal. The one constant-evaluation branch is what makes that comparison meaningful. The library is compiled with
  `-ffp-contract=off`, so a multiply and an add fuse only where the schedule says so.
- **Parity against `main`.** Each `tests/parity/*_here.cpp` has a `_there.cpp` twin, built against the previous
  implementation at a pinned commit (`HEINZEL_PARITY_REF`). Their samples are compared numerically. Any allowance
  above zero has to name its cause.
- **Goldens.** The printed listings of planned and lowered programs and of workspace layouts (`tests/goldens/loops_*`)
  are compared as text, so any change of structure is reviewed line by line. The analysed graphs of a corpus are
  pinned by hash (`tests/conservation/`). Neither is ever re-recorded without a reason named in the commit.
- **The bench gate.** `tools/bench_report.py gate` runs the gated benchmark rows against `main` built and measured in
  the same session, and each row must stay within its recorded factor of `main`'s time. It runs after any change to
  the runner, because it catches correct-but-slow code that every unit test passes. The tool's subcommands are `run`
  (the benchmarks), `report` (`docs/performance-<arch>.md` from their results), `record` (a new baseline), `gate`, and
  `all` (run, then report).

## Where decisions are recorded

- `docs/decisions.md`: each decision with what it rules out and where the evidence is, numbered. A reversal is a
  new entry, and the old one gets a note that points to it.
- `docs/limitations.md`: what is refused, by which check, which test shows it, and the workaround.
- `docs/knob-math.md`, `docs/sections-in-lanes.md`: one page for each feature that a switch turns off: what it does,
  when it fires, its switch, where it lives, what it gains and costs, its limits.
- `docs/performance-<arch>.md` and `docs/perf-regression-<arch>.md`: generated by `tools/bench_report.py`.
- `docs/measurements/`: recorded measurements, such as compile-time costs and the accuracy of the tiers.
- `Plan/backlog.md`: what is open. Finished plans are in `Plan/archive/`, kept as history.
