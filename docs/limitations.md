# Limitations — the exclusion board

`heinzel` compiles small, fixed-structure numerical graphs to straight-line code. What is *in* works
across any axis; what is *out* is a **named, testable refusal at the point you would reach for it**,
plus a row here with the workaround. Nothing is silently absent.

This board is milestone **M5.9's** deliverable and it is built **incrementally**: a row is written
with the operation whose exclusion it records, not swept up at the end. The whole-board assertion —
every "excluded" row shown failing, and the board asserted so that a check which starts *passing*
fails the build — is the closing step of M5 (`plans/heinzel-milestones/02-MILESTONES.md` §M5.9). Until
then this file is the running record and each row's refusal has its own test.

## The exclusion table

| excluded | why | where it is refused | shown failing | workaround |
|---|---|---|---|---|
| Negative-stride **views** (`_slice` with `step < 1`, zero-copy flip) | a negative stride is a reversal, not a sub-range; every downstream stride/contiguity assumption is written for `step >= 1` | `sema/view_shape.hpp` `slice_shape`, its first refusal — a frontend `fail`-equivalent `diag_error` naming `_flip` | `tests/unit/inference_test.cpp` (`step = 0` refused, message names `_flip`); `tests/unit/rearrange_test.cpp` (the run refuses it too) | `_flip` — one materialising copy |
| **Reflect / edge pad modes** (`_padc` is constant-mode only) | a reflect pad mirrors the source and an edge pad replicates it — each a data-dependent index remap per element; the constant mode is a **guarded read** (one source element or one fixed fill), which is all `_padc` carries | UNREPRESENTABLE, not diagnosed: `_padc`'s attribute is a single fill (`ir/ir.hpp` `ViewAttr::pad_bits`) with no mode field, so a reflect/edge pad cannot be built to be refused — the same shape as the old tree's constant-only `pad()` (`git show main:docs/limitations.md` row `pad reflect/edge`) | n/a — no node spells the mode, so nothing rejects one; the **absence** is asserted at M5.9's whole-board step | mirrored `_flip` + `_slice` (reflect); an edge-row `_slice` broadcast then `_concat`'d with the body (replicate) |
| **Broadcast across `_concat` operands** | a join keeps every non-joined axis of both operands equal — numpy semantics — so it never broadcasts an axis of extent one against a wider one, the way an elementwise operation would; a join of `{2, 3}` and `{2, 1}` is a mistake, not an implicit widen | `sema/view_shape.hpp` `concat_shape` — a disagreeing non-joined axis is refused by name | `tests/unit/view_inference_concat_test.cpp` (operands disagreeing on a non-joined axis refused); `tests/unit/rearrange_concat_test.cpp` (the run refuses it too) | broadcast the narrow operand to the wide extent explicitly (a `_mul` by a wide one, or a `_reshape`) **before** the join |
| **Symbolic `_concat` joined axis** | the joined axis's output extent is the **sum of two** operand extents, not a term derived from **one** — so it is outside M5's derived-extent algebra (`(k·L + c)/d` over one source), which is why a symbolic joined axis has no literal size to write and no relation to record | `sema/view_shape.hpp` `concat_shape` — a symbolic joined axis is refused by name, the `_padc` literal-only precedent | `tests/unit/view_inference_concat_test.cpp` (a symbolic joined axis refused, naming the fix) | bind both operands' joined extents (or declare them literal); a symbolic axis **elsewhere** — a non-joined one the two agree on — passes straight through |
| **Spectral values** (`_fft` produces no numbers at M5) | the DFT arithmetic is an intrinsic backend deferred to **M13** in both trees; at M5 only the SHAPE `N → 2·(N/2+1)` and the static-length precondition are modelled, so a graph with an `_fft` is built, shape-inferred and planned but not RUN | `tests/reference/interpret.hpp` — the `ElementKind::Boundary` arm `fail`s "no spectral backend computes it yet", a refusal not a silent wrong number; the loop tree refuses the graph at planning | `tests/unit/spectral_fft_test.cpp` (the interpreted run refused, message named) | none until M13; the graph is still shape-checked and planned |
| **A symbolic `_fft` extent** (static N only) | the shape `2·(N/2+1)` needs a literal N and the M13 backend will be built from that length — the old tree's "static n, v1". The check covers the WHOLE transformed value, stricter than an innermost-only reading, so a symbolic OUTER axis (a batched transform bound later) is refused too | `sema/spectral.hpp` `fft_shape` — refused by name, `_fft`'s first refusal | `tests/unit/spectral_fft_test.cpp` (a symbolic innermost AND a symbolic outer axis both refused, the message naming the shape) | bind or declare every extent of the transformed value to a literal before the `_fft` |
| **User-specified `_ovadd` output length** (derived, not given) | the old tree GIVES `out_len` as an attribute; this tree DERIVES it as `(F-1)·hop + window` (the `_unfold` frame relation run backwards) so nothing can disagree with it (the M2.6 trap a given-and-then-checked length would be) — the cost is that a round-trip `_unfold`→process→`_ovadd` reconstructs `(F-1)·hop+window`, which for an original length that is not COLA-exact is shorter than the original | UNREPRESENTABLE, not diagnosed: `OverlapAddAttr` carries only `window`/`hop` (`ir/ir.hpp`), so a longer output length cannot be asked for to be refused | n/a — no attribute spells a length, so nothing rejects one; the derived shape is asserted in `tests/unit/spectral_ovadd_test.cpp` | `_padc` the reconstruction to the original length (or `_slice` it if the derived length overshoots), both built at S3h/S3b |
| **An unresolved symbolic `_ovadd` frame count** | `_ovadd` DERIVES its output length from the frame count F, so F must resolve to a literal by the width stage; unlike `_unfold`, which DECLARES its frame-count symbol and tracks it through the derived-extent algebra, an overlap-add is a CONSUMER of F and has no output length to derive while F is still a symbol | `sema/overlap_add.hpp` `overlap_add_shape` — a still-symbolic frame axis is refused by name, as an unfold over an unbound axis is | `tests/unit/spectral_ovadd_test.cpp` (a symbolic frame count refused, naming the two ways to give it a size) | bind the frame axis, or derive it from an `_unfold` whose own source is bound (`symbolic_test.cpp` graph six executes exactly this, bit-identically to its literal twin) |
| **A symbolic `_order` extent** (static element extents only) | the compare-exchange network is a fixed-capacity compile-time object sized from the ordered extent, so a symbolic one has no network to build — the old tree's static-extent rule. Every ELEMENT axis must be literal, while the streamed (last) axis may be a symbol, so a graph is compiled once for every signal length (P5, `Plan/archive/make-default/02` §3) | `sema/order.hpp` `order_shape` — refused by name, `_order`'s first refusal | `tests/unit/order_test.cpp` (a symbolic ordered extent refused, the message naming the shape; a symbolic stream accepted); `tests/unit/nest_symbolic_stream_test.cpp` (order statistics over a symbolic stream run on the nest path) | bind or declare every element extent of the ordered value to a literal before the `_order` |
| **An `_order` extent over the ceiling** (`kMaxOrderedLength` = 256) | the compare-exchange network is a fixed-capacity compile-time object, so the ordered extent is bounded; a ceiling, not a wall (standing rule S-6), and the diagnostic NAMES the override that raises it | `sema/order.hpp` `order_shape` — an extent over the ceiling is refused by name, naming `-D HEINZEL_MAX_ORDER_EXTENT` | `tests/unit/order_test.cpp` (an extent of 257 refused, the message naming the override) | raise the ceiling with `-D HEINZEL_MAX_ORDER_EXTENT=<n>` (set consistently across every translation unit — an inconsistent value would size the network differently in two TUs) |
| **Multi-axis order statistics** (`_order` runs along ONE axis) | ordering several axes at once is not a single whole-axis permutation and has no single network; the attribute carries ONE `axis`, so a multi-axis order cannot be asked — the old tree's single-axis contract | UNREPRESENTABLE, not diagnosed: `OrderAttr` carries a single `axis` (`ir/ir.hpp`), so a multi-axis order cannot be built to be refused | n/a — no attribute spells more than one axis; the single-axis shapes (Sort keeps, TopK resizes, Quantile drops one axis) are asserted in `tests/unit/order_test.cpp` | compose one `_order` per axis (each its own node), or reshape the axes to be ordered into one first |
| **Coupled multi-axis recurrence** (2-D IIR feedback, summed-area) | a recurrence feeds back along TIME — a run stands inside the sample loop and reads earlier samples (`TimeRule::SameSample` reaching `ReadsEarlierSamples`); a lag along an ELEMENT axis is a different, wavefront mechanism the old tree also refused | UNREPRESENTABLE, not diagnosed: `RecurrenceAttr` carries only `defines` (`ir/ir.hpp`), with no cross-axis-lag field and no multi-index left-hand side to write, so a 2-D feedback cannot be built to be refused | n/a — no attribute spells an element-axis lag; the recurrence-along-time meaning is asserted in `tests/unit/construction_test.cpp` / `nest_recurrence_test.cpp` | scan per axis in separate graphs, host transpose between (the old tree's own workaround) |
| **A loop through an `Expr`'s past** (`Expr r = x + 0.5 * v;` before `v[n] = 0.3 * r[n - 1]`) | the loop tree reads a computed value's past only after the value is made in the sample, and a loop needs it before: finishing takes that past for feedback, as a `Variable`'s, so the graph means what it says and the walk computes it, but no stage is lowered for a lag read before its value is made (Decision 76, B4's review) | `loops/graph/facts.hpp` `statements_of` — "heinzel::compile: feedback reads the past of an Expr; make it a Variable" | `tests/unit/index_notation_test.cpp` (`loop_through_an_expr` refused by name, and walked as `loop_through_a_variable` runs) | make the value a `Variable` (`Variable r; r[n] = x + 0.5 * v;`): a variable's past is feedback |
| **A chain of lags written before their definitions** (`Expr late = v[n - 1];` read by the output only and `Expr prev = w[n - 1];` read by `v`'s definition, both before `v[n] = x * 0.5 + prev;` and `w[n] = x * 2.0;`) | finishing moves a lag written before its variable's definition after it where nothing in the loops it stands in reads it, deciding every lag in one pass (`late_lags`, `frontend/reading_order.hpp`): `prev` is read by `v`'s definition, which is in a loop there only through `late`, so the pass moves `late` and keeps `prev`, which then stands before `w`'s definition with nothing in `w`'s loop reading it, a lag the loop tree does not lower. Moving the whole chain is backlog (a greatest fixpoint over the reads, one linear pass over their strongly connected parts); repeating the pass until nothing moves would be super-linear (Decision 76, B4's second review) | `loops/graph/feedback.hpp` — "heinzel::compile: a lag of a later value its definition does not read is not lowered yet" | `tests/unit/index_notation_test.cpp` (`chain_of_late_lags` refused with its text; `chain_with_the_lag_after` compiled and run as the walk runs it) | write the lag after its definition (`Expr late = v[n - 1];` after `v[n] = …`) |
| **A contraction of a tensor with a signal** (`einsum("ik,kj->ij", w, x)`, `w` a tensor, `x` a signal) | `_matmul` lines the two operands' streams up; a one-sample operand against an N-sample one is no broadcast it knows, and with the old check relaxed both runners computed wrong numbers — backlog B-16, after M4 | `sema/time_checks.hpp` `node_keeps_time` — "a tensor times a signal needs a multiply and sum, not a contraction" | `tests/unit/tensor_signal_test.cpp` (`tensor_times_signal`) | a multiply and a sum over the contracted axis, the operands' axes lined up (an unsqueeze each), which broadcasts a tensor against a signal as any elementwise pair does |
| **The past, the spectrum or the time axis of a tensor** | a tensor is the same numbers at every sample: it has no past to lag, no signal to transform, and its last recorded axis stands for time, not for an element axis (`Plan/archive/14-dsl/03` §2) | `sema/time_checks.hpp` — "a lag reads a tensor — declare it as a Signal to read its past", "an fft needs a Signal", "that axis is past the tensor's last" | `tests/unit/tensor_signal_test.cpp` (`lagged_tensor`, `fft_of_a_tensor`, `hidden_axis`) | declare the value a `Signal{…}` |
| **A gradient with respect to a `Param`** | a `Param` is one number for every element and sample, so its gradient sums the cotangent over all of them — over a tensor's hidden time axis too, which a fold may not name (and over a signal's time, which no runner folds, O-12) | `sema/time_checks.hpp`, in the analysis `gradients` runs on the graph it builds — "heinzel::gradients augmented-graph infer: heinzel: that axis is past the tensor's last" (before L4 the planner refused it: "this collapses the streamed axis") | `Plan/probes/14-dsl/l4-review/p8_grad.cpp` (the `Param` refused, the one-element tensor compiled) | declare the learned number as a one-element tensor, `Input k{"k", {1}}`: its gradient keeps the axis |
| **General `gather` / `scatter` / `index_select`** on tensor indices | data-dependent addressing is hardware-hostile on both first-class targets; the whole tree is affine iteration over static shapes, and a runtime index has no place in it | UNREPRESENTABLE, not diagnosed: no operation code spells a data-dependent tensor index (`ir/vocab.hpp` op list) — `_tblread` is the one interpolated read and it indexes a table by a phase, not a tensor by a tensor | n/a — no node spells the addressing; the **absence** is asserted at the whole-board step (`tests/unit/exclusion_board_test.cpp`) | `wavetable(table, phase)` or `lookup(table, x, lo, hi)` (`_tblread`, an interpolated read of a table, *Tables* below); restructure to contiguous access; `_ovadd` for a windowed accumulate |
| **`argsort` / sort-indices / topk-indices** | `_order` yields the VALUES (a compare-exchange network); the paired (value, index) network doubles the state and turns each lane's compare-exchange into a compare plus two blends — buildable, no consumer yet | UNREPRESENTABLE, not diagnosed: `OrderOp` is `{Sort, TopK, Quantile}` (`ir/vocab.hpp`), all value-producing; no index-output variant exists to be refused | n/a — no `OrderOp` spells an index output; the value-only shapes are asserted in `tests/unit/order_test.cpp` | sort the values in-graph and reproduce the permutation host-side; `argmax`/`argmin` cover k = 1 |
| **Runtime `q` (quantile) / runtime `k` (top-k)** | a runtime quantile needs a runtime rank, which is exactly the data-dependent addressing `gather` is excluded for | a C++ TYPE ERROR, not a diagnostic: `OrderAttr`'s `k` (`std::int32_t`) and `q` (`double`) are compile-time attribute fields, so a non-constant argument does not type-check at the builder (`ir/ir.hpp`) | n/a — the fields are literal by construction; that `OrderAttr` carries `k`/`q` as plain scalars is asserted in `tests/unit/exclusion_board_test.cpp` | build one graph per `q` / `k` |
| **`frac_delay` off the innermost (streamed) axis** | a fractional read position along a strided ELEMENT axis is a data-dependent gather (the row above); the streamed axis is time, where reading earlier samples is what a delay already does | UNREPRESENTABLE, not diagnosed: `DelayAttr` carries only `samples` (`ir/ir.hpp`) with no axis field by design, so an off-axis fractional delay cannot be built to be refused — the old tree's "the attr has no axis field by design" | n/a — no attribute spells an axis; that `_fdelay` reads earlier samples on the streamed axis only is asserted in `tests/unit/exclusion_board_test.cpp` | transpose/materialise so the streamed axis is innermost |
| **Symbolic extents on derived / kernel edges** | each such edge either exceeds the derived-extent algebra (an affine term over ONE source extent, `plan/M5-STEPS.md` S1 / M5.2) or a kernel's structural-stride requirement; a symbol there has no relation to record and no static object to size | `fft_shape` (`sema/spectral.hpp`) and `order_shape` (`sema/order.hpp`, its element axes), and the derived-extent **obligation** recorded at bind time for the algebra's own edges (M5.2) — these checks and obligations ARE the enumeration the old tree kept as a residual-edges list | `tests/unit/spectral_fft_test.cpp` (a symbolic transformed extent refused), `tests/unit/order_test.cpp` (a symbolic ordered extent refused), `tests/unit/symbolic_test.cpp` (an obligation that does not hold rejected at bind time, naming the operation) | give the edge a literal extent, or bind the symbol to one that satisfies the recorded obligation |
| **Rank > `kMaxRank`** (default 6) | the fixed-capacity discipline is load-bearing — no dynamic allocation in either compilation path — so a shape has a bounded number of axes; a ceiling, not a wall (standing rule S-6), raised only with measurement | `Shape` constructor `fail` naming the `-DHEINZEL_MAX_RANK` override (`ir/vocab.hpp:117`) — a `fail()` that stops the build, not a `Diagnostic`, because a shape that cannot be held cannot be handed to the pass at all | `tests/gates/refusals/rank_over_maximum.cpp` — a seven-axis `Shape` that must NOT compile, built as ctest `board_refuses_rank_over_maximum` and required to fail carrying the message | rebuild with `-DHEINZEL_MAX_RANK=N` (consistently across every TU), at most `kMaxAxes` (8): a stage's axes index arrays of eight (`static_assert`, `loops/band_layout.hpp`), and at 8 a contraction, whose domain has one axis more than its result, is refused by name |
| **Integer / bool / fixed-point element types** | a no-FPU embedded target is a trigger-gated backlog item; masks are float 0 / 1, not a bool type | UNREPRESENTABLE, not diagnosed: `ElemType` is `{Unknown, F32, F64}` (`ir/vocab.hpp:53`) — no integer member exists to be built and refused | n/a — no element type spells an integer; that the enum has no integer member is asserted in `tests/unit/exclusion_board_test.cpp` (fires the moment one is added) | float masks; a `_cast` at the boundary |
| **`mode`** (the most frequent value) | equality-run counting is a different mechanism from ranking, and no consumer asks for it — the old tree records "not asserted, no spelling exists" | UNREPRESENTABLE, not diagnosed: `OrderOp` is `{Sort, TopK, Quantile}` (`ir/vocab.hpp`), all rank statistics; no most-frequent-value variant exists to be built and refused | n/a — no `OrderOp` spells a mode; the recorded set is asserted in `tests/unit/exclusion_board_test.cpp` (fires if a variant is added) | a histogram plus `argmax` where the value set is known |

## Capacities

A graph, a schedule and a program are fixed-capacity compile-time objects (`Plan/15-zero-overhead` ZO-4), so how much
one holds is bounded. Each bound a graph can reach by a refusal is a ceiling, not a wall: the refusal names its
override, `-DHEINZEL_MAX_<NAME>=N`, which raises it. The defaults sit a step above the largest graph and program the
tree builds, and tree nodes and buffers leave room for a contraction in each of the 16 stages (`Plan/15-zero-overhead/02`,
"Sweep result"), and inputs and buffers hold a plugin's 8-band EQ with three knobs a band (25 inputs, 75 buffers;
`docs/knob-math.md`). An override must be the same in every translation unit of a
program, because it changes the layout of `Graph` and `Program`; a mismatch is not detected, as for
`HEINZEL_MAX_RANK`. A CMake user sets it once, with `add_compile_definitions` or on a target's interface. Raising one
costs compile time and memory in every constant evaluation that makes a graph, a schedule or a program, element by
element (cut by four, planning ran 23 % faster in 60 % less memory: `Plan/probes/15-zero-overhead/capacities/`);
None sizes what an optimized binary holds: an `Executor` and a workspace hold arrays of exactly their program's entries.
So three defaults stay below a plugin-sized graph: the same EQ (465 values, 336 nodes, 381 statements) needs
`-DHEINZEL_MAX_VALUES=512 -DHEINZEL_MAX_NODES=384 -DHEINZEL_MAX_STMTS=384`, and as defaults those would cost every
compile, whatever its graph's size: the slowest test compiled 26 % slower in 39 % more memory, a small graph 21 % slower
in 28 % more (inputs and buffers cost nothing measurable). A compile that pays for its graph's size, not
the capacities, is backlog.
A graph over a capacity is one build error whose message is the refusal, as an analysis refusal is (on C++23 the fixed
text; `try_compile` and `compile` give the words): the frontend counts a graph when it finishes it, before it emits a
node, and a graph over a capacity says which it passed (`Graph::over`), which `infer` refuses first, in its words. A
capacity reached later, by `gradients` or a graph built by hand, stops the graph function where it is reached
(`push_value`, …), and an `Executor` of it is one error too, the words in its note (Decision 77). Three are reached
inside `try_compile`, after the graph function has run, and still stop the build with one error for each member read
off the compile result, the words in a note under the first: a parallel form's constants over `kMaxConstants`
(`rewrite/parallel_form.hpp`), a debug build's checks of promises over `kMaxIO` (`compile.hpp`; the backlog), and the
copy of an input a delay line reads over `kMaxValues` or `kMaxNodes` (`rewrite/copied_inputs.hpp`): an input that delay
lines read takes one value and one node more, its one copy, which the frontend does not count. Each override is shown
refusing under a small value of its own in `tests/gates/refusals/capacity.cpp` (ctest `build_refuses_capacity_*`;
`kMaxIO` and `kMaxStageStmts` at two of their places, `kMaxValues` and `kMaxNodes` at the copy too). The lowering's
stage refusals are pinned in `loops_lower` and `loops_primitives`; that of `gradients` no test reaches.
`tests/unit/capacities_raised_test.cpp` builds with every override raised and runs graphs just over each default.

| override (default) | what it bounds | where it refuses, and its text |
|---|---|---|
| `HEINZEL_MAX_VALUES` (128) | `kMaxValues`: a graph's values (56 B each) | counted when the graph is finished (`frontend/graph_builder.hpp`, `capacity_passed`), and `push_value` (`ir/ir.hpp`) at each value `gradients` or a delay line's copy of an input adds: "heinzel: a graph has more values than kMaxValues; rebuild with -DHEINZEL_MAX_VALUES=N" |
| `HEINZEL_MAX_NODES` (128) | `kMaxNodes`: a graph's nodes (368 B each) | likewise, and `push_node` (`ir/ir.hpp`): "heinzel: a graph has more nodes than kMaxNodes; rebuild with -DHEINZEL_MAX_NODES=N" |
| `HEINZEL_MAX_IO` (32) | `kMaxIO`: a graph's named inputs, and its named outputs | inputs, counted when the graph is finished (`frontend/graph_builder.hpp`); outputs, a `static_assert` in `finish` (`frontend/declarations.hpp`); `gradients`, the loss and its gradients (`app/heinzel/gradients.hpp`); a debug build's checks of promises, outputs `#k` after the user's (`compile.hpp`, Plan/26-value-ranges/04): "…more inputs than kMaxIO…", "…more outputs than kMaxIO; rebuild with -DHEINZEL_MAX_IO=N", "heinzel: the checks of promises make more outputs than kMaxIO; rebuild with -DHEINZEL_MAX_IO=N" |
| `HEINZEL_MAX_DIM_DECLS` (8), at most `kMaxSymbols` (32) | `kMaxDimDecls`: a `Dim`'s id is below it; it sizes the table a run binds sizes in. A `Dim`'s id is a solver symbol, hence the ceiling (`static_assert`, `sema/solver.hpp`) | the `Dim`, where a `Signal` takes it (`frontend/declarations.hpp`): "heinzel: a Dim's id is from 0 to kMaxDimDecls - 1; rebuild with -DHEINZEL_MAX_DIM_DECLS=N" |
| `HEINZEL_MAX_CONSTANTS` (1024) | `kMaxConstants`: the numbers of all a graph's constant tensors (`constant(…)`). Only compiling pays for it: a binary holds just the numbers each constant reads | counted when the graph is finished (`frontend/graph_builder.hpp`), and `push_constant` (`ir/ir.hpp`) as a parallel form's constants are added (`rewrite/parallel_form.hpp`) or a graph is built by hand: "heinzel: a graph has more constants than kMaxConstants; rebuild with -DHEINZEL_MAX_CONSTANTS=N" |
| `HEINZEL_MAX_RANGES` (64) | `kMaxRanges`: the ranges a graph states, one per ranged port and promised value, and one per `abs` and `frac`, for the fact each states, so that 65 `abs` in a graph that states no range of its own pass it (`RangeMark`, 120 B each; Plan/26-value-ranges) | counted when the graph is finished (`frontend/graph_builder.hpp`), as the frontend records them: "heinzel: a graph has more ranges than kMaxRanges; rebuild with -DHEINZEL_MAX_RANGES=N" |
| `HEINZEL_MAX_STMTS` (128) | `kMaxStmts`: a program's statements (520 B each) | the lowering, as it appends a stage (`loops/normalize.hpp`): "heinzel::compile: more statements than kMaxStmts; rebuild with -DHEINZEL_MAX_STMTS=N" |
| `HEINZEL_MAX_TREE_NODES` (512) | `kMaxTreeNodes`: a program's tree nodes (60 B each) | the lowering, at each node it grows (`loops/normalize.hpp`): "heinzel::compile: more tree nodes than kMaxTreeNodes; rebuild with -DHEINZEL_MAX_TREE_NODES=N" |
| `HEINZEL_MAX_BUFFERS` (96) | `kMaxBuffers`: a program's buffers | the lowering, at each buffer (`loops/lower_stmts.hpp`): "heinzel::compile: more buffers than kMaxBuffers; rebuild with -DHEINZEL_MAX_BUFFERS=N" |
| `HEINZEL_MAX_STAGES` (16) | `kMaxStages`: a schedule's and a program's stages | the planner, counting loop nests (`loops/planner/plan.hpp`): "heinzel::compile: more loop nests than kMaxStages; rebuild with -DHEINZEL_MAX_STAGES=N"; the lowering (`loops/lower_form.hpp`, `loops/primitives/memory.hpp`): "…more stages than kMaxStages…" |
| `HEINZEL_MAX_STAGE_STMTS` (128), at least 2 | `kMaxStageStmts`: one stage's statements, its nodes and a Load per operand buffer; the planner keeps a stage's nodes to three quarters of it (`kMaxGroupStmts`, 96) | the lowering (`loops/lower_stmts.hpp`): "heinzel::compile: a stage over kMaxStageStmts; rebuild with -DHEINZEL_MAX_STAGE_STMTS=N"; a feedback cycle longer than `kMaxGroupStmts` (`loops/graph/feedback.hpp`): "heinzel::compile: a cycle over kMaxGroupStmts; rebuild with -DHEINZEL_MAX_STAGE_STMTS=N" |
| `HEINZEL_MAX_INLINE_ARENA` (4096) | the most bytes a static program's arena may take and lie inside its workspace (an `Executor`'s object), packed; above it, or 0, the arena is a page-aligned allocation | refuses nothing: it decides where an arena lies (`loops/layout.hpp`, `kInlineOf`) |

**The capacities that decline** change a program instead of refusing it: a program that reaches one loses speed,
never a result, and no build fails. A stage closes when its nodes reach `kMaxGroupStmts`, and a producer is not
computed inside its window's reader when the two would hold more (`loops/planner/stages.hpp`, `inline.hpp`). How much
work a feedback stage merges is no capacity but `Machine::merge_work` (`loops/planner/chains.hpp`), which a raise
leaves alone. A
relaid operand's copy is dropped when no stage is left for it, and the operand is read as fed; a turned one's
(`Schedule::Stage::turned`) likewise, the stage then falling back to lanes along the contracted axis (where the
precision grants reordering, else none), unsplit
(`loops/planner/plan.hpp`). The buffers they take are not counted there: at `kMaxBuffers` such a graph is refused. The planner stops banding at `kMaxBands` (4) bands and `kMaxBandBuffers` (8) buffers a
band (`loops/planner/bands.hpp`). `kMaxValues` and `kMaxNodes` decline as well as refuse: a rewrite whose graph would
pass either is not made, and the graph is planned without it (`with` in `rewrite/rebuilt.hpp`, which the feedback
sums, the parallel form, the sections in lanes, the double state and the fused multiply-adds build their graph with). `kMaxStages` declines
a whole rewrite: a rewritten graph the planner refuses is planned as written (`detail::plan` in `compile.hpp`). Many
separate Param chains can reach it, each piece of knob math a stage (`docs/knob-math.md`, "Limits and stand-ins").
`kMaxBands`, `kMaxBandBuffers`, `kMaxAxes` (8), `kMaxStrips` (16) and `kMaxStageLoops` (24) have no override: they are small, the
planner keeps within them or declines, and their refusals are reached from a schedule written by hand (`kMaxAxes`'
also through a raised `HEINZEL_MAX_RANK`).

## A compile result read at run time

An optimized `Executor`'s binary (`-O1` and up, `-Os`, asserts on or off) holds nothing of its compile result: what a
run needs is read off it while compiling, as tables of exactly their entries (Decision 55, gated by
`zero_overhead_binary`). An unoptimized one (`-O0`) keeps what it names, the whole compile result among it.
`compile(graph)` and `try_compile(graph)` stay public, for inspection and debugging, and a program that reads their
result at run time (prints it, looks into it at a run-time index, keeps a `Compiled` or a `CompileResult` in a variable
it reads) carries the whole of it: the `Graph` and the `Program`, about 197 KB at the default capacities, more with one
raised. Read it in a constant expression instead, or name the subobject a run needs: `loops::run<P>` takes the program
as `template <const loops::Program& P>`, and its binary holds none of it. To look at what an `Executor<g>` runs,
`print<g>()` (`heinzel/print.hpp`) compiles the graph again at run time and returns the graph as planned, its schedule
and its program as text: a program that calls it carries the compiler, and holds about 280 KB on the heap while it
runs; an `Executor` of the same graph holds none of it.

## Params and a run on another thread

`param("gain") = v` writes the Param's bytes in the `Executor`, and `run()` copies them once, before its first stage,
then compares them with what each kept stage last ran with (Decision 80). Neither is atomic: a `param()` while a `run()`
is under way on another thread is a data race, and the run may see half a number. Set Params between runs, on the
thread that runs, or hand them over with a lock or an atomic flag of the caller's. A host that delivers a change inside
a block (VST3's and CLAP's offsets) splits the block there: two runs, each with its own Params.

A kept stage needs the same floating-point environment from run to run (`docs/knob-math.md`, "Limits and stand-ins").

## Filter sections in lanes

A cascade of like feedback sections runs its k sections in SIMD lanes, each a sample behind the one before, where the
cost model prices that below the cascade (Decision 69). What it does not do, each in `docs/sections-in-lanes.md`,
"Limits and stand-ins":

- *A short run pays the lanes' edges*: three Param sections run 7–10 % slower at blocks of 32 and 64.
- *Sections over several voices stay a cascade.*
- *Sections whose past has a gap stay a cascade* (RBJ's band-pass; a lag of a lag of a value outside the cycle).
- *The price can take the lanes where the cascade is faster*, in rare shapes (1.45× to 1.94× slower).
- *Coefficients read through a slice stay a cascade*: give each section its coefficients as Params or constants.

## Tables

`wavetable(table, phase)` and `lookup(table, x, lo, hi)` (`frontend/graph_builder.hpp`, Decision 78) read a table that
is an `Input` with a shape or a `constant(…)`: the read is built with its number of cells, which a computed table
(`constant(k) * 2.0`) has only once the graph is analysed, so it is refused while the graph is built ("heinzel: a table
is an Input with a shape or a constant(...), its cells known where it is read"): compute the numbers into the
constant. A table is one axis of cells, the same at every sample or a row of them per sample (`Signal{N, frames}`); the
loop tree refuses one of more axes and a window as a table. A constant table's cells count against `kMaxConstants`
(1,024 numbers in all; a 2,048-cell wavetable raises it). A refusal names the read `_tblread`: `wavetable` is nine
characters, one more than an operation's name holds. `gradients` refuses a table read (its adjoint column).

`lookup`'s phase stops at the last cell's place, (N - 1)/N, which a float holds when N is a power of two; for any other
N it is rounded down, so x at and above hi reads at most N·2^-24 of a cell short of the last cell (N·2^-23 for an f32
read at `Normal` and `Fast`, whose place is a float too), and never blends in the first. At hi itself the read lands
on the last cell where (x - lo) times the phase's scale reaches its place, as it does for bounds a float holds and
whose difference it holds (-5 and 5 over 128 cells, 0 and 1 over 100); otherwise an ulp of the phase short of it. A
table read blends a cell with its neighbour even at a whole place (its weight zero), so an infinite cell makes its
neighbour's whole place a NaN.

`lookup` refuses, while the graph is built, a table of one cell (N points from lo to hi lie (hi - lo)/(N - 1) apart),
lo at or above hi, and bounds, a span hi - lo or a scale (N - 1)/N/(hi - lo) a double does not hold, each by name
("heinzel: lookup(table, x, lo, hi) needs lo, hi and hi - lo finite", …), where the read was a NaN. It checks them in
double, not knowing the value type: over floats, lo, hi and hi - lo must be finite in float too, as a bound past
3.4e38 is an infinity there, and every x reads a NaN.

An f32 read at `Normal` and `Fast` takes its place and blend in float, as a plain loop does (Decision 78), so it is no
longer the walk's number to the bit: its place, frac(phase)·N, is rounded once in float (once more where the phase lies
in (-1, 0)), so the read moves by that distance times the step between the cells it crosses, and the blend's roundings
add at most 3.5 float steps of the larger cell (`tests/support/float_reads.hpp`). Past 2^24 cells N's float is rounded
too, which moves the place by up to half a float step of N more, outside that bound; the read stays in its table. At
`High`, over f64 and over an f64 table it is the walk's to the bit.

A read wraps its phase, a floor and a subtraction a read, unless the proof keeps the phase in [0, 1] at every
precision (Decision 78, the phase in one turn): `frac`'s result, `lookup`'s clamp, `clamp(p, 0.0, 1.0)`,
`min(abs(p), 1.0)`, a choice of numbers in [0, 1], a `Variable` defined by one of those and a lag of any of them read
with no wrap. A `Variable`'s past is kept only by the range its definition always has (`frac`'s [0, 1], `abs`'s at
least 0), so `v[n - 1]` reads with no wrap where `v[n]` is a `frac`, but is wrapped where `v[n] = clamp(v[n - 1] + p,
0.0, 1.0)`, though `v` itself, at `n`, is not. A phase that is an input, one declared or promised in [0, 1], and one
computed by arithmetic (`0.5 + 0.5 * sinpi(x)`) are wrapped: release checks neither a declaration nor a promise, and
arithmetic may round past an end the proof computed in double.

## Delay lines

`x[n - d.at_most(120.0) * ms]` (`frontend/declarations.hpp`, Decision 78) reads x as it was `d` ago, `d` a value that
may change every sample, between two samples the two blended. The line keeps the bound's samples, `b · rate`, rounded up
to a whole one (a bound that misses a whole number only by its product's rounding is that number), and one more for
the far tap; over an input, a copy of the input of the same depth besides (`rewrite/copied_inputs.hpp`). `d` is held
to the line: below 0, and at a NaN, the read is x now; above the bound, as far back as the line keeps. The bound needs
a unit of time and `n`'s rate; without a bound, a unit or a rate, or with a bound below 0 or not finite, the build stops
with one error that shows the spelling.

An f32 line at `Normal` and `Fast` blends its two samples in float (Decision 78): it reads the samples the walk reads,
the same distance between them (a float distance splits exactly), and lies within 3.5 float steps of the larger of them
of the walk's number. Past 2^24 samples, where the line's longest is no float, a distance beyond it is held to the
largest float below it, and the walk's to the longest itself: other samples, up to a float step of the longest nearer,
still in the line. At `High`, over f64 and with an f64 distance it is the walk's to the bit.

A run carries the line's whole depth to the next (B-17): a 120 ms line at 48 kHz in blocks of 64 moves its 5,761
samples every run, about 180 ns of a 472 ns run (`longdelay/heinzel/B64`; 4.55 ns a sample at blocks of 4,096, 7.4 at
64). A ring buffer moves nothing.

Not lowered yet, refused by name: a delay that changes inside feedback, through its signal (a flanger, a Karplus-Strong
string whose pitch moves) or through its distance (a delay its own output sets), "heinzel::compile: a delay line in
feedback, or of a value defined later, is not lowered yet"; a fixed lag there runs. The same words refuse a line of a
`Variable` written before the variable's definition with no loop through it, where a lag written there runs (finishing
moves a lag after the definition, not a line): write the line after it. A delay of a constant number, "a delay line
over a constant is not lowered yet", is the constant after the first samples. `gradients` refuses a delay line (its
adjoint column).

## Value ranges: what the proof refuses, and what to write instead

Every graph is held to the proof of ranges (Plan/26-value-ranges, Decision 74): an operation that may receive a value
outside its domain refuses the graph while it compiles (`compile.hpp` `take_ranges`; an `Executor`'s build error, the
text at C++26, `try_compile` at C++23). A port declared without a range may hold any number. A message holds 95
characters, so it names the operation as the user writes it and the operand's range as the proof computed it, and
the remedy is here. The refusals are pinned in `tests/gates/refusals/proof.cpp` (ctest `build_refuses_proof_*`), the
graphs proven in `tests/unit/ranges_walk_test.cpp`.

| the message (after `heinzel: `) | what it means | what to write |
|---|---|---|
| `/ may divide by zero: the divisor is …` | the divisor's range holds 0 | the ports' ranges in their declarations, away from 0 (`gt(0)`, a hole `ne(0)`); a `promise` on the divisor where the host keeps it away from 0; `x / max(d, eps)` where 0 can happen |
| `sqrt may see a number below 0: its operand is …` | the operand may be negative | a range `ge(0)` on what it is computed from, or a `promise(ge(0))` on it |
| `rsqrt`, `log`, `_log2`, `_log10` `may see a number <= 0: its operand is …` | the operand may be 0 or below (`log10(x)` is `log(x)` times a constant, so its refusal names `log`) | `gt(0)` on what it is computed from, or a promise |
| `_asin`, `_acos` `may see a number outside ge(-1), le(1): its operand is …` | the operand may leave [-1, 1] | the ranges that keep it inside, or `clamp(v, -1.0, 1.0)` |
| `tan may reach a pole: its operand is …` | a piece of the range crosses kπ ± π/2 | a range inside one branch |
| `_atan2 may see 0 and 0: its operands are … and …` | both operands may be 0 at once, where it is undefined | one operand's range away from 0 |
| `pow may see a base <= 0 with a negative exponent: the base is …`, `pow may see a negative base: …` | `pow` is defined here for a base ≥ 0, and > 0 where the exponent may be negative | the base's range `gt(0)` or `ge(0)`; a signed value squared as `x * x` |
| `promise(…) contradicts the proof: …` | the promise and the computed range share no number: the promise cannot hold | a promise the value can keep, or the graph it was meant for |

**Where the proof is too coarse** (false alarms), each with its workaround. A promise costs nothing in release; in
debug it is checked.

- *A division guarded by `where`.* `where(x > 0, 1 / x, 0)` computes both branches, and the proof does not narrow a
  branch by its condition: `1 / x` is refused where `x` may be 0. Write `1 / max(x, eps)`, or promise the divisor.
- *Correlated operands.* Each operand's range is taken alone. `x - x` for x in [-1, 1] is [-2, 2], not 0; only a
  value times itself is known to be a square, at least 0. `abs(x)` is at least 0 by its own rule, and `frac(x)`
  (`x - floor(x)`) states the range its result always has as a fact, which the proof trusts and nothing checks: it
  lies in [0, 1], so `sqrt(abs(x))` and `log(1 + frac(x))` are proven. `frac`'s fact is closed, since
  `x - floor(x)` rounds to 1 for a tiny negative x. Where x's range reaches the next integer up (`ge(-0.5), le(0)`),
  the proof sees `frac` reach 1 and refuses `1 / (1 - frac(x))`: promise `frac(x).promise(lt(1))` where the host keeps
  x from just below 0, and a debug build checks it. Where the range stops short of it (`le(-1e-10)`), the proof keeps
  `frac` below 1, which an f32 program need not (*What the proof does not cover*, below). The same written out
  (`max(x, -x)`) is not proven: write `abs`, or promise the result. A promise on `abs` or `frac` narrows the fact.
- *An unbounded feedback value.* A recurrence's value is any number unless its definition is promised:
  `y[n] = (x[n] + 0.5 * y[n - 1]).promise(ge(-2), le(2))`. The promise is proven by induction where one step of the
  filter maps the promised range inside itself (a one-pole), and checked in debug where it does not: resonant biquads,
  state-variable filters, envelope followers (`m + c * (env[n - 1] - m)` reads the magnitude `m` twice), Moog
  stages. A loop whose gain is exactly 1 in real numbers is not proven either: its float constants (`0.8f`, `0.2f`)
  sum to slightly more than 1. A phase accumulator needs no promise to lie in [0, 1]: `p[n] = frac(p[n - 1] + inc)`
  does by `frac`'s fact, and so does its past `p[n - 1]`. One stepping down reaches 1 (`frac(-1e-9f)` is 1), so a
  division by `1 - p[n - 1]` needs `p[n] = frac(p[n - 1] + inc).promise(lt(1))`, which a debug build checks.
- *A variable taken as an `Expr` before its definition.* `Expr v0 = v;` written before `v[n] = (…).promise(…)` is the
  variable itself, and a read of it at this sample after the definition (`1.0 / (3.0 + v0)`) is any number: the proof
  takes a variable's range from its definition only in its past, or where the read is of `v` itself (`3.0 + v`, before
  the definition or after it, reads the definition). Read `v`, or promise the expression (`(3.0 + v0).promise(gt(0))`).
- *A sum over a run-time length.* With its count unknown until `prepare`, a sum of terms at least 0 is [0, ∞) and one
  of terms of both signs any number, so `1 / x.sum(…)` is refused. Promise the sum.
- *Conservative domains.* `pow` with a negative base and an integer exponent, and `atan2(0, 0)`, have a value, but the
  proof refuses them: write a whole power of a signed value as products (`x * x`); the loop tree refuses `atan2`
  anyway.
- *A gradient at a domain's edge* is no false alarm: the derivative of `sqrt` at 0, of `asin` at ±1 and `pow`'s at a
  base of 0 are infinite, so `gradients` through `sqrt` over [0, 1] is refused unless the input is declared `gt(0)`.

**What the proof does not cover.** It is about the true, real-number values. Overflow is not covered: `exp(1000)` is
infinite, and so may a large sum be. Nor is the program's rounding (Gerald, 2026-10-14). The proof computes in double,
with each number as the graph writes it, and an f32 program rounds them:

- A value is computed in its own type, so an `f32` Param declared `gt(0)` and set to 1e-46 holds 0 (the check reads
  the double as given, which is above 0), and a value the proof keeps just inside a domain may round onto its edge.
- `frac(x)` is 1 in f32 for an x less than about 3e-8 below 0 (`frac(-1e-9f)`), where the proof, in double, keeps it
  below 1: with x in `ge(-0.5), le(-1e-10)`, `1 / (1 - frac(x))` is proven and divides by 0, and
  `frac(x).promise(lt(1))` is proven too, so nothing checks it.
- A scalar literal is judged as the double written: in an f32 graph `log(constant(1e-46) + x * 0)` is proven, where
  the program's 1e-46 is 0. A tensor constant (`constant({1e-46}, {})`) is judged at its f32 numbers, and refused.

What is refused is a range that reaches a domain's edge as the proof computes it; what a debug build checks is every
promise the proof cannot prove, against the numbers the program computed. So near an edge, let the range reach it
(x in `ge(-0.5), le(0)`: `frac` then reaches 1, the division is refused, and `frac(x).promise(lt(1))` is checked),
keep a margin the type holds (a literal above f32's least number, 1.4e-45), or guard the operation
(`1 / max(1 - frac(x), eps)`).

**Debug and release compute different programs.** A checked `Executor` (no `NDEBUG`) writes each promise the proof did
not prove as an output `#k`, an extra reader of the promised value. A rewrite that needs the value to have no other
reader declines there: a promise on the first section of a filter cascade keeps the cascade serial in debug, while
release computes its parallel form, a bank in double. At `Normal` a serial float cascade has a float cascade's
rounding, so the two builds' numbers differ: KFR's three-section EQ is 8.2e-5 from long double in debug and 4.6e-8 in
release (`tests/unit/ranges_cascade_test.cpp`). A promise on the last section (the `_recur`'s value, which the bank's
sum carries) keeps the parallel form in both.

## Not applicable at M5 — dispositioned, milestone named

Rows the old tree carries that this milestone neither implements nor refuses, because the mechanism
they would attach to is not built here yet. Each is a disposition with its reason, not a gap — the
whole-board gate (`tests/unit/exclusion_board_test.cpp`) records every one so none can be quietly
forgotten, and names the milestone that makes it live.

| not applicable at M5 | why it is not applicable HERE | where it becomes live |
|---|---|---|
| **Order statistics on the GPU** | there is no `map_*` device-placement machinery at M5 — the whole tree is CPU-only — so an order statistic is never *placed* on a GPU to be refused | a later placement tranche (device eligibility); not an M2–M9 milestone |
| **einsum diagonal / ellipsis / implicit output** | there is no einsum frontend in this tree; `_contr` is spelled by naming its axes directly, so the notation whose diagonal would be a gather does not exist | never planned; contraction is the axis-named `_contr` |
| **Contraction: output rank + contracted-axis count > `kMaxAxes`** | this is a nest-LOWERING refusal (`lower_stage_nest` in the old tree), and no nest lowering exists at M5. The shape-level cap IS enforced here (a contraction whose result exceeds `kMaxRank` is refused by the `Shape` ctor, the row above) | **M6** — the generated code and machine model, where the fixed-capacity nest is built |
| **Runtime graph-*structure* mutation** | the entire architecture is a `constexpr build()` producing a compile-time artifact; there is no API that changes ranks, ops or connectivity at runtime (dynamic *extents* are a separate, supported thing) | never — structural by design |
| **Autodiff through an order statistic** | `gradients()` does not exist at M5; the adjoint of a rank routes each cotangent to whichever input won that rank — a data-dependent scatter — and is a named refusal *inside* `gradients()` | **M7** — `gradients()` (`03-DEPENDENCIES.md`, `04-OPEN-QUESTIONS.md`) |
| **Autodiff through stateful / dynamic paths** | same — `gradients()` is M7; recurrences, delay taps, `frac_delay`/`_tblread`, strided slices, non-innermost `_unfold` and dynamic extents are its refusals, none of which can be made before the pass they live in exists | **M7** — `gradients()` |

## The board is asserted as a whole

Every one of the old tree's twenty data rows (`git show main:docs/limitations.md` at `c4012b8`,
`:18-37`, counted rather than quoted) now has exactly one disposition above: **implemented** (none —
each row is a genuine exclusion the old tree also made), **excluded** (shown failing at a named point),
or **not applicable at M5** (with the milestone that makes it live). `tests/unit/exclusion_board_test.cpp`
is the M5.9 whole-board gate: it enumerates the twenty, asserts the count is exactly twenty and each
carries a disposition — so a row that silently disappears fails the build — and for every excluded row
holds the standing guard M5.9 names, that a refusal which quietly stops rejecting fails the build too.
Its blind spot is stated in `docs/decisions.md`.
