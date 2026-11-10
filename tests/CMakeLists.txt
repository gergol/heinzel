# One executable per meaningful test group (amendment 03 A3-2).
#
# The metric to hold is the SLOWEST SINGLE TRANSLATION UNIT, not the total. On the old tree, 89
# targets took 194 s wall against a 35 s parallel floor because ONE file (unit/compile_test.cpp)
# took 197.6 s and everything else finished in its shadow. A suite whose CPU total doubles but whose
# slowest TU halves builds twice as fast. Watch .ninja_log, not the wall clock.
#
# Separate executables rather than one linked binary: a group can then be built and run alone, which
# is what the inner development loop actually wants.

set(HEINZEL_TEST_GROUPS
    support              # fixed-capacity containers and the failure channel
    order_net            # M5 S5.1: the compare-exchange network generator behind _order (sort/topk/quantile)
    order_builder        # MO S1 (frontend growth, item b): the sort/topk/quantile/median members append exactly the hand-built _order node — the three shapes, the carried attribute, and the streamed-axis/out-of-range refusals shown rejecting through the builder
    vocab                # element types, shapes, the dimension encoding, operation identity
    op_rules             # the per-operation rule table and its coverage checks
    meaning              # one writing per operation, and the table knowing which rows owe one
    graph_value          # the graph as a value: structural, usable as a template argument
    construction         # arenas, forwarding handles, self-constructing leaves, recurrences
    solver               # which axes are the same axis, and what size each has
    symbolic             # a symbolic extent bound to N agrees with its literal-N twin
    inference            # the one pass that reads the rule columns; Plan/28-dsl-proposals/03 B2: a where of two numbers, and what is made of numbers alone, typed by its reader whatever its mask's (* x32 f32, * x64 f64), a where that is the output by its condition, each analysed twice equal; numbers only refused with its text
    idempotence          # M6.5: the analysis pass run twice over one graph gives an equal result (retires R-S5)
    view_inference       # M5: what the pass requires of a view's attribute — the structural set's refusals
    view_inference2d     # M5 S3f: what the pass requires of a 2-D unfold — its refusals and derived counts
    view_inference_pad   # M5 S3h: what the pass requires of a constant pad (_padc) — its refusals and grown shape
    view_inference_concat # M5 S3i: what the pass requires of a join (_concat) — its two-operand refusals and summed shape
    spectral_fft         # M5 S4a: what the pass makes of an _fft — its half-spectrum shape and its refusals
    spectral_ovadd       # M5 S4c: what the pass makes of an _ovadd — its collapsed shape, its refusals, and the windowed scatter it reads
    order                # M5 S5: what the pass makes of an _order — its sort/topk/quantile shapes, its refusals, and the ranks it reads
    tblread              # M5 S6: what the pass and the runners make of a _tblread — its shape, its refusals, and the interpolation it reads; Plan/28-dsl-proposals/05 D3: a NaN phase reads a NaN (at run time)
    first_sample         # Plan/33 S3: a run's first sample (`_first`), 1 at each run's start; the walk in runs, the Executor in runs at High bit for bit, the stage peeled, the refusals by name
    smoothed             # Plan/33 S3: dsp::smoothed, Cmajor's ramp on a knob, the gain example walked in runs (the ramp exactly) and on Executor in runs at High bit for bit, at Normal within rounding
    staging_mirror       # Plan/33 S2 review: the model's staging of feedback (stages_of) against the planner's, pair of cycles by pair, on graphs rewritten for zen2: one-poles in series at this sample and through lags, two rooms on one input
    iota                 # M5 S6: what the pass and the runners make of an _iota — its declared shape/type, its refusals, and the ramp it reads
    passthrough_audit    # M5.6 (S7): every passthrough-looking op carries its old-tree rejection, or is a declared identity — shown objecting to a bare passthrough
    exclusion_board      # M5.9 (S9) oracle O9: the twenty old-tree rows dispositioned, the board asserted whole so a vanished row or a refusal that stops rejecting fails the build
    subgraph             # Plan/archive/14-dsl L5, Plan/28-dsl-proposals/02 A5: subgraph(name, body) applied as piece(args...) with no context — graph_composition's strip, pinned by hash to the graph the BuildContext spelling built (deleted at L9), arguments from one arena or two, three-deep nesting, array/pair/tuple/Input/Variable results, function and lambda bodies, inputs never read or merged into an argument, a piece made before its inputs exist, one never applied adding nothing, a body that takes nothing run when applied (counted), a piece applied twice and an over-long name refused; the build failures are the build_refuses_subgraph_* fixtures
    timing               # which sample each of a node's operands is read from, and the lags and the tap delay line run so (the named lag is the fixed tap at the innermost axis; the tap against a hand reference, walked and on Executor)
    width                # M5: how many numbers one sample of a value is, a value's elements, which element of an operand an element of a result reads, the walk's planes, and its refusals (a recurrence narrower than its definition among them)
    collapse             # M5: an operation that collapses axes, run — which elements fold, and into what
    rearrange            # M5: an operation that rearranges axes, run — which element comes from where
    rearrange2d          # M5 S3f: the 2-D rearrangement (_unfld2d), run — two windows at once
    rearrange_flip       # M5 S3g: the reversal (_flip), run — output i of an axis reads (extent-1-i)
    rearrange_pad        # M5 S3h: the constant pad (_padc), run — a body element reads the source, a margin the fill
    rearrange_concat     # M5 S3i: the join (_concat), run — an element reads one of two operands, selected per element
    rearrange_reshape    # M5 S10b: the reshape rejection lifted into the pass — a non-conserving reshape refused by name; a regroup of a computed value (Plan/28-dsl-proposals/03 B3) equal to .reshape with the extents written, in a recurrence too, and one read while its operand's rank grows or before its symbol is bound, and run as the walk runs it
    compile_pipeline     # the pipeline entry compile(graph, machine, sizes): infer -> plan -> lower, same value at compile time and run time, no number moved against the walk, a graph that does not analyse stopped with infer's own message; try_compile hands the analysis's, the planner's or the lowering's refusal back as text
    conservation         # today's analysed graphs against the ones this tree recorded earlier
    both_ways            # M4.1: every operation reached by a corpus graph that is walked (all but _fft's) and, where the loop tree compiles it, run on Executor at High bit for bit; the coverage assertion shown objecting
    numeric_ops          # M4: what the numeric operations compute, where the answer is exact
    transcendental       # M4: the approximate ones, against the platform, to a measured bound
    tiers                # M4.2: the function-accuracy axis, from a node's setting to the core that runs
    tier_bounds          # M4.2 oracle O10: what each tier costs in accuracy, measured and gated
    nest_elementwise     # the elementwise slice end to end on the loop tree (plan -> lower -> run): fused chains, lanes with every remainder, broadcasting, F32 in float, F64, and High via double — against the arithmetic by hand
    nest_reduce          # folds on the loop tree against folds by hand: every reduction in lanes (held to the bound two orders of a sum can differ by) and strictly under High (held to the bits), maps fused into a fold and after it, a folded value read back broadcast, a non-contiguous fold axis, a fold of everything
    nest_contract        # contractions on the loop tree against the triple loop by hand, exactly (lanes run across outputs, so no sum is reordered): unrolled rows and lanes with every remainder, High via a double accumulator, a transposed and batched operand, and a bias + relu run in the contraction's own lanes
    nest_window          # sliding windows on the loop tree against loops by hand, exactly: a window is no statement and has no buffer (its readers read the source at frame*hop + tap), the fold over the taps runs in lanes ACROSS outputs from its first tap (conv, a vertical blur, High via a double accumulator), a hop of two runs every loop serially (no contiguous walk across outputs, five taps fill no lane), and a window that is itself a result is refused by name
    nest_grouping        # which nodes share a stage, against loops by hand: the planner visits a ready node RELATED to the open stage first, several folds of one domain over the same axes share a stage (dx/dy, a sum beside a max, harris in two stages), a map over a fold's domain that reads nothing after the fold runs inside its loops — and what is unrelated, reads a fold's result or keeps other axes does not (the refusals: nest_grouping_refusals)
    nest_grouping_refusals # split from nest_grouping (gate structure_limits): the hand-written schedules the lowering refuses by name — sibling folds over different axes of one domain, a contraction sharing its stage with a fold
    nest_grouping_review # adversarial review of sibling folds and the related-first visit: a map of a producer's input no longer pulled into the producer's stage of maps (which had cost it its place inside the window: +27 to +85 %), dx/dy over two window nodes siblings
    nest_band            # bands on the loop tree: what the lowering makes of every band case (leads, band buffers, kept rows), blurxy banded bit for bit against the same graph unbanded and a hand loop — chunks of one row to more than the whole, a band value that is also a result, two reaches — canaries between the buffers; every refusal of a band by name
    nest_band_workloads  # bands over harris, a chain of three blurs, conv with the band along its lanes, the oscillator and a one-pole bank over time (state carried between chunks and from run to run), each bit for bit against the same graph unbanded, canaries between the buffers (split from nest_band: D-6, Plan/archive/12 M4)
    nest_band_parallel   # P6: bands split among workers along their chunks (Schedule::Band::parallel), each worker with band buffers of its own and its first chunk started afresh — blurxy at chunks of 1 to more than the whole, harris, a chain of three blurs, conv, with the workspace's copies capping the split — bit for bit against the serial run, the gaps between copies untouched; the planner's 8 MB bands split, the oscillator's stream band not; a stream band and a result made ahead refused by name
    nest_band_plan       # which stages the planner bands (blurxy and harris at 8 / 25 MB, at a height half the L2 holds) and which not (256 KB, conv at 4 MB, the oscillator, contractions, rows that are not contiguous — refused by name, still lowered), each banded run bit for bit the run stored whole; a planner's band edited by hand laid out again by the lowering — refused by name, or run bit for bit as stored whole; blurxy past the L3 behind a gain worked out from a knob, a stage with no loop first, still outgrows the last-level cache where a map runs in its reader
    nest_recurrence      # feedback and lags on the loop tree against loops by hand and the reference walk, exactly: a lag is a Load some samples back out of a buffer that keeps that much of its past before the block (silence in a fresh workspace); a lag of a variable defined later makes its stage walk time a sample at a time (lanes across voices), what follows the cycle runs in lanes along time; where a sample fits one block of lane bodies time runs innermost and the state is handed on in a register (Role::State) — never for a lag that runs after its variable's definition; the past of an input and feedback through a fold refused by name
    nest_feedback_chain  # feedback cycles that feed each other at the present sample share one stage where the cost model prices it cheaper (Decision 65, Plan/23 04): cascades of 1 to 12 sections against loops by hand, exactly, and against the walk at High (eight as written split in two); a tanh between, a map of the feed alone, a bank's voice lanes and what follows the last cycle stay as they were
    knob_stages          # the work ahead first: work of no axis made from timeless values alone, gathered ahead of every unit with a loop: cascades of two to four RBJ peaks whose coefficients the graph works out from three Params a band, every stage with no loop first, each band's knob math a stage of its own, the stages with loops (as written and as rewritten) those of the cascade with its coefficients Params; on Executor at High in blocks of 1, 32 and 256 bit for bit the walk; a map over a declared tensor between two one-poles keeps its stage between them; and which stages are kept: each knob stage kept, watching its band's three knobs (a chain past a stage's room: the second stage its maker's Param too), in schedule and program; no stage with a loop kept, none that makes an output or reads what such a stage made
    cost_model           # the planner's cost model (Plan/23 04): what an operation costs by its rule-table class, and the corpus's measured pairs of stages put in the order zen2 ran them (a long cascade as written split, rewritten whole; maps between cycles in lanes; a division dearer in double)
    knob_price           # knob math: the price of a stage with no loop (its chain from its Params' load, and its issue) against checking its Params (two loads a watched Param and a branch: 1 to 40 Params pinned), and the decision, kept where the stage costs more: the probe's corpus as the probe wrote it, planned for zen2 and priced by the planner's own count, each of the 64 cases above the noise decided as zen2 measured, but onepole3 at B256, pinned as kept, as the model decides
    loops_kept           # knob math: a kept stage runs only where a Param it watches changed, bit for bit, since it last ran: one RBJ peak, three in series, a one-pole with exp and a gain of 1 / p, each program as a release Executor gets it against the same program with every kept cleared, bit for bit, on Executor in runs of 1, 32 and 256 (a Param kept, changed and set back; 0.0 and -0.0; two NaN payloads; reset; moved and moved back; prepare again with another length; a static program's inline arena moved, so copied, source and copy run on apart) and by hand with Params as bits, every workspace buffer compared, canaried; a mark in a kept stage's buffers survives a run where nothing it watches changed, a Param redoes exactly the stages watching it, and a fresh workspace runs them though every Param's bits are zero; and at High against the walk, bit for bit, its Params changed before some runs and kept in the others (the frame copy: the stages after a skipped one read its results through their copies)
    linked_sections       # sections in lanes: sections linked in lanes, u = _chain(x, V), walked in passes, pass p settling section p: a TPT pair, LR8 as four RBJ biquads, five RBJ peaks whose coefficients are [5, 1] tensor inputs, eight tanh(... + 0.1) sections and the pair into the eight (two links in series), each at High over 4096 samples of noise bit for bit its k-cycle graph built apart, by both runs where it has one input; the passes each takes (one with no link); a link refused by name where its operand 0 is more than one section, its operand 1 not laid out as its result, or it has no section axis. The facts: each link read through to its variable's definition, one cycle over [k, time] where the k-cycle graph holds k, the pair into the eight two in order, each planned skewed; a link of anything but the variable its own cycle defines refused by name, one inside a larger cycle refused there or, summed every sample, found in it and its stage refused by name, one read by a later cycle planned before it
    linked_price          # sections in lanes: the cost model reads a link as a lag of one step, its path afresh there with its own latency on it, over one step, no path through its operand 1: the linked graphs' LR8 and five peaks one cycle whose chain is the link, a multiply and four adds (18 on zen2 and the M1; the k-cycle twin's sections 9 each), its sections in the double lanes' bodies; K = 2, two RBJ biquads (their coefficients in inputs, or gathered) and LR4, merged as the merge and double state make them, priced as P prices them against today's cascade, which stays; the gather over [k, 1] leads into no cycle, its domain not the stage's (the inputs' price at K = 2), and eight RBJ biquads gathered take the lanes; at K = 2 and K = 8, the merged graph the two rules make is the one priced, node for node
    lanes_price           # P, the price that decides sections in lanes: the lanes where the cost model prices the graph's feedback stages below today's cascade by more than kTie, the cascade at a tie; the probe's corpus as it wrote it (Param cascades of K = 2 to 8 RBJ sections, LR4, LR8, sat4) after the parallel form on zen2, each run linked whole as one cycle, the choice against zen2's measured winner: K = 2 and LR4 the cascade, K >= 4, LR8 and sat4 the lanes, K = 3 a tie either way; and a lone section before a linked pair (KFR's EQ3 read after its first section), priced apart as the planner stages it, which keeps the cascade; and K = 2 behind work in front of the run over [time] (x * g, two knob peaks after x * g, x + x2), priced in a stage of maps of its own beside the lanes as the planner runs it, which keeps the cascade; and two channels of four Param one-poles sharing their Params, written section by section, priced as one stage as the planner runs them, alike to the chains written one after the other, which keeps the cascade, and two bands of four behind their gains priced apart, as the planner's cost call keeps them
    rewrite_sections_in_lanes # sections in lanes: a maximal run of like feedback cycles, each reading the one before at this sample, one cycle over [k, time], linked (the merge alone, unpriced): Param cascades of 2, 3, 4 and 8 sections, raw and through RBJ knob math, constants and Params mixed (one Param all read, the gather, a constant tensor, one number), the lags of x the link's, f32, f64 and f32 into f64, Fast, a table read, the linked graphs and the priced gathered graph node for node; each walked bit for bit as written, infer agreeing, a fixed point, no output's range looser but where a section's follows its input's (named); the maximal run (unlike runs in series, a middle reader, two outside values, other places, a gain kept, a constant drive); capacities; declined: High, two voices, a moving coefficient, a middle section read, eq8fb, JUCE's ladder, no lanes, 17 sections at four double lanes, a past-only read, lags that differ
    rewrite_double_state  # the rewrite layer's second rule (Plan/23 3.4): a one-voice cycle of cheap work at Normal in double, with the work between two such cycles; the conversions of a biquad, a cascade, a lag read aside, a cycle behind a tanh; a bank, High and Fast as written; fixed points; each program bit for bit against the walk of its graph; the low-passes against double; sections in lanes: four Param sections merged in lanes, a cycle holding a link one voice, promised double, one conversion after V, sliced, walked over 64 blocks of 256 samples of noise and a 440 Hz sine against the cascade in double, within 1e-5 and twice today's
    rewrite_parallel_form # the rewrite layer's third rule (Plan/23 3.7): a chain of second-order sections as the sum of its partial fractions, a bank in double; KFR's EQs and dsp/cascade4 recognised and expanded against long double; the bank's graph; High, one section, Linkwitz-Riley, a first-order section, a chain read in its middle, voices, a non-linear map as written; fixed points; the walk against the cascade's, the Executor against long double whole and in blocks
    rewrite_parallel_form_declines # the parallel form's declines since its review: chains whose partial fractions amplify past the bound (a 16th-order Butterworth, the review's sections at one frequency, four low-passes 0.1 Hz apart) and the program of one as accurate as the cascade; coefficients out of range compile; a value read twice is a signal of its own (linear time); a value pinned High inside a section keeps it as written
    rewrite_product_adds # an add of a product, outside feedback cycles, fused where the machine has the multiply-add, and a sum folding a product fused with it, only where the precision grants it; what fuses, what stays as written (a product read twice, in a cycle, added wider, no fma, High); fixed points; the numbers against the walk in double
    rewrite_equal_values  # equal values computed once (Plan/33 S1): eight combs, one 1 - damp; 0 and -0, NaN payloads apart; materialisations, variables, a promise, outputs, precisions apart; the both-ways corpus doubled, merged, walked to the bits as written
    equal_values_linear   # Plan/33 S1 review: equal values computed once stays linear in nodes that differ only in their attribute: 400 lags of one input merged within a step budget the old hash overran, every lag kept
    rewrite_feedback_sums # the rewrite layer's first rule (Decision 66): a feedback sum in the order its terms become ready, its last product fused where the machine has a multiply-add, only where the precision grants it; what reorders, what fuses, what stays as written; High untouched; fixed points of infer and of the rule; the numbers against the walk at most twice as far as as written; sections in lanes: the merged cascade's sum printed, in ready order, the link's term last and fused, the section's own newest state before it
    nest_recurrence_review # adversarial review of the recurrence slice: part A what held (cross-coupled and nested cycles, lags of lags, rank 3, narrow banks, a workspace run on over the next block, hand-written illegal schedules refused by name), part B the findings, fixed — a rank-0 value's past, an algebraic loop, a cycle found by what reads what and not by where its nodes stand, a long cycle's own message
    nest_v6_review       # adversarial review of the V6 slices and of state handed on in registers: part A 39 attacks that held, bit for bit (chains of views, views of symbolic values, lanes through views, the past of an input at every block length, banks of 1 to 64 voices, cycles whose span holds what is not theirs, pads and joins of views, ramps), part B the findings, fixed — a graph needing more loop nests than a program holds refused by name and by loops::lowers, a lag of a lag handed on, (and one left open: a view between a producer and its window switches inlining off)
    nest_symbolic_stream # graphs over a stream whose length is a symbol (one compile for every length), on the loop tree against the walk of the literal-length twin, bit for bit: order statistics (a median, the quantiles 0 and 1, a sliding median; _order asks only its element extents) and rearranges (a split and a merge of element axes, of an input and of a computed value; the stream passed through whole)
    nest_agreement       # the planner and lowering agree: over a corpus shaped like the graphs the surfaces built before they left the tree (DS-20), each built with a symbolic and a literal stream, try_compile hands back a program or a refusal by name and never a lowering failure; every verdict pinned
    nest_planner_holes   # Plan/17-simplify-more 06 § A: the graphs the planner once planned and the lowering refused, now planned and lowered — a·b + cᵀ (its lanes off the product's outputs), a product per sample beside a lag of its input (its rows untiled along time), a bank banded ahead of a window over its voices (the band not split) — each program run canaried and bit for bit against the walk at High
    nest_constant_views  # views of a SHAPED constant (autodiff's seeds) on the loop tree against the walk: a view of a constant with a statement stores the constant (a fill) and is read like any view's buffer by a map, a contraction, a lag, the caller; two gradient graphs; refused by name in the planner: a window over a constant, a contraction of one, an output no node makes
    nest_views           # views on the loop tree against the reference walk, bit for bit: a transpose, a slice, a reversal and new extents are an affine address composed once per program; one that something other than a view reads is a Load at that address in a stage over its own shape (fused with its readers), one that only views read is nothing; a view reads storage, so the stage making its source closes first; lanes only where they still walk memory; conversions and a materialisation barrier are Loads of a register; a pad whose every part is empty runs in lanes and split among workers; new extents over a window refused by name
    nest_gather          # reads whose address is a signal on the loop tree against the reference walk, bit for bit, at High (in double, the walk's arithmetic) and at Normal (in float, Decision 78's float reads, exact over these sixteenths): a delay line read as far back as a position says (a Gather out of a buffer keeping that much silence and a sample more; lanes along time, and across voices with a ragged end) and a table read between two of its cells where a phase says (the table a row of cells per sample, walked by time alone); a phase that is no number (a NaN, an infinity) reads a NaN on both runners, in lanes and not (Plan/28-dsl-proposals/05 D3); a delay line over an input planned as written, with no copy (compiling makes one, D4), refused by name
    nest_odd_shapes      # a contraction with a bias and a relu after it on odd shapes (one row, fewer rows than an unrolled block, a block and one, one column, a contracted axis of one), exactly against the triple loop: as planned on zen2, and on a machine with tiny caches and pages where the chunking, the relaid copy, the row and column tails and the fetch-ahead all happen at once — with the relayout taken back too, which may not change a number
    nest_review          # adversarial review of the fold (V2) and contraction (V3) slices: part A is regression coverage that held (odd/prime/one/below-a-lane/below-a-block extents through the tiled and relaid contraction, the lane axis on either operand, extra free axes, mixed F32/F64 through a relaid copy, a resumed fold re-run on a used workspace); part B the two divergences from the reference walk it found, kept as the tests of their fixes — a fold the schedule reassociates nothing in opens at its first element (Registers::opens_first), not at the identity (NaN swallowed by max/min, a sum of negative zeros turned positive), and a fold over the STREAMED axis is refused by name, as the walk refuses it
    nest_runner_review   # adversarial review of the runner's compile-time expansion (each<N>), of Registers::opens_first and of the streamed-axis refusal: a ragged contraction with a bias and a relu after it (the lane tail, the row remainder and the after-statements in one run, feeds allocated exactly at size), a TWO-axis serial fold against the reference walk bit for bit over NaN/-0.0/+-inf with and without a producer map inside it, a fold over an axis the operand DOES NOT HAVE (refused by name by the walk and by the planner), and the guards that keep `opens_first` sound: a strict fold is never chunked and never runs its own axis in lanes
    float_cores          # V5: `exp`, `tanh` and `sinpi` computed in FLOAT against the double core rounded to float, to a bound of 2 f32 ulp — every binade of both signs at 4096 points, the zeros, the subnormals, NaN, the infinities, the overflow and underflow thresholds and the 0.625 the tangent changes method at; for the half-turn sine every binade to 2^26, where a float is already an integer, plus oddness to the bit and the exact +-0/+-1 a whole and a half turn owe; and the lane form against the plain float, bit for bit
    nest_parallel        # P6: stages split among workers (Schedule::Stage::parallel) — each worker runs the whole stage over a range of one loop's axis — bit for bit against the serial run at 1, 2, 3, 7 and 16 in-order workers and on pools of 4 and 16: maps along rows and lanes, folds along rows and outputs (strict High too), a tiled and relaid contraction along its row Tile and its column lanes, windows (conv, a producer inside its window, a median), feedback along voices (time outer and inner), a FIR over a feed along time (its head samples worker 0's), two stages, a delay line, parts; every refusal by name
    nest_parallel_review # adversarial review of P6 M2: split_of on its edges; a FIR over a feed split along voices and along time, with feedback; views read by a split stage (transpose, strided slice, flip along the split axis, a producer inside its window through a flip, a pack with a flipped part), a ramp, a window over two axes, an F64 intermediate, a contraction of a transposed view; run-time extents down to 0; a fold over voices, a per-sample contraction, two results, a delay line, a table read; a workspace reused at changing worker counts — bit for bit the serial run; a fold's loop moved outside the inner loops a schedule names (inner_from) refused by name
    nest_parallel_plan   # P6: which stages the planner splits among workers and along which loop — the outermost over an axis where everything the stage makes moves and nothing reads its own past (an image's rows, a mono chain's time, a row fold's rows, a tiled contraction's row Tile, a conv's outputs, a bank's voices inside its time loop, a FIR's time), none in a band, none made in parts, none on a one-core machine — and planned programs run split, bit for bit the serial run
    nest_transcendental  # V5: the channel strip (gain -> 3x tanh -> gain -> clamp) and a bare tanh/exp/sinpi/sin/log (and log(abs(sin x)+1) as one stage) on the loop tree (plan -> lower -> run), in lanes with every remainder — under `Normal` in float within 2 ulp of the double core, under `High` via double EXACTLY as the reference walk computes it; and a graph holding a transcendental with no writing over the value type (an arc tangent) refused at planning, a cosine on its double core lowered (Plan/28-dsl-proposals/05 D2)
    native_f32_is_exact  # may an F32 value be computed in float? yes for every one-step Exact operation (the double-rounding theorem, measured), no for the ExactInDouble sequences — the column held honest both ways
    lane_ops             # Lane<T,W> moves no number: every lane operation against the scalar one, per lane, float and double at widths 2/4/8, bit for bit or NaN for NaN
    hardware_twins       # each hardware twin (sqrt, trunc, floor, ceil, roundeven) against its software core, bit for bit, over binade boundaries, ties and 4M random bit patterns
    numeric_contract     # the NumericContract column (what freedom an operation has) and numerics::grants (what a precision permits)
    ordering             # M4.2: the ordering axis — what `Reproducible` promises, and what `Free` does not
    adjoint_class        # M7: every operation classified for differentiation, exactly once, into its decided class
    gradient_fd          # M7 S1: gradients() over an elementwise+reduce graph, analytic vs finite differences
    gradient_mask_fd     # M7 S1c: the mask-routed adjoints — min/max/select and reduce max/min/prod, analytic vs FD
    gradient_structural_fd # M7 S2: the structural adjoints — transpose/reshape/squeeze/slice/pad/flip/concat/pack, analytic vs FD
    gradient_golden      # M7 S2b: the first numpy gradient golden — softmax composite, loss + dZS vs gen_grad_golden.py
    gradient_contract_fd # M7 S3: the contraction adjoint — dA/dB and the broadcasting case, analytic vs FD
    gradient_frame_fd    # M7 S4: the framing pair — unfold/overlap-add adjoints incl. the dropped-tail pad, analytic vs FD
    gradient_train       # M7 S5: an in-graph SGD step (M7.2 compose) run to numpy's converged loss_mlp_trained
    gradient_symbolic    # M7 S6: gradients over symbolic extents (M7.3, R4-2 mean run-time reciprocal), bound vs literal twin
    gradient_excluded    # M7 S7: excluded adjoints (M7.5) — named refusal on the diff path, clean off it, shown both ways
    autodiff_exclusions  # M7 S9: the residual autodiff exclusions (M7.6) — E5/E6/fft/order/recur, each shown failing
    notation             # M8 S2: the einsum notation lowers a spec to the same _contr/_reduce/_transpose the tests build by hand
    multi_input_interpret # MW S1: the walk fed N named inputs, one of them wide — every input bound, every element fed, a wrong-shaped feed refused
    image_surface        # MB S3: a rank-2 element value (a spatial image) with a degenerate length-1 stream runs on the walk and on Executor, broadcasting over rows, each against a hand reference
    matmul               # MB S4b/S4c: the general contraction RUNS — a transposing and a batched matmul, read at different index maps, walked against a hand reference and on Executor (default machine and zen2) against the walk
    rearrange_notation   # MB S5a: the einops rearrange/reduce notation lowers a spec to the same _reshape/_transpose/_reduce the tests build by hand — split/merge/transpose/fold, and the streamed-axis and regroup-and-reorder refusals shown objecting; over an input the analysed graph and its hash as before B3, the regroup's refusals the analysis's, and its own where a node reads it
    named_units          # M8 S5 (physical_dsp by-need): a named duration folds to an integer sample lag at compile time, and the fold objects — shown rejecting — to a sub-sample duration or an unset rate
    frac_sinpi_spellings # M8 S5 (oscillator by-need): the floor/frac/sinpi frontend spellings lower to their M4 cores (op::Floor, op::SinPi, and the composed x−floor(x)), each run and asserted bit-exact against the core
    frontend_outputs     # MG S1 (frontend growth, autodiff prerequisite): multi-output capture — finish(Output…) marks several already-computed values as outputs, a shared subexpression emitted once, both outputs run and read bit-exact; a bare input as the output is its identity (one _realize, read back bit for bit, Plan/archive/14-dsl L1), a bare literal refused
    frontend_gradients   # MG S2 (frontend growth, item a), and Plan/archive/14-dsl L5: gradients(loss, {…}, level) on a declared Output, pinned by hash to the graph finish_gradients built at either level (deleted at L9), every value at the level; it lowers to heinzel::autodiff::gradients, a frontend-built exp(a*x) graph run and its da/dx checked against finite differences and the closed form, an ignored target's zero gradient, and a non-input target, a repeated name and no target shown rejecting
    dynamic_extent       # MD S1 (frontend growth, item c): minting a runtime-resolved streamed extent — a Length builds a [2,N] graph that infers and is non-static with its constraints round-tripped on the graph, a static graph declares none and its hash is unmoved while a declared dimension moves it, one length shared across inputs is recorded once, two Lengths that disagree shown rejecting
    resolve_extent       # MD S2 (frontend growth, item c): the prepare()-time resolver — check_dimension_size validates a bound size against its DimConstraints and resolve_dimensions validates a whole graph's declared dimensions, with a non-positive size, a below-minimum size, a non-multiple size and an unbound dimension each shown rejecting with its own message, and a satisfying size accepted
    attention            # MA (reopen item b): single-head scaled dot-product self-attention (two einsum contractions around a stable softmax) at T == D (every temporary one width), Executor at High bit for bit against the walk, and an independent double oracle within atol=1e-4
    runtime_recurrence   # MU.2 (reopen item c, Q1=A): a single-sample recurrence along the RUNTIME-resolved streamed axis (tutorial 10's one-pole, offline path) — one compiled program on Executor, prepared and run at N=256/64/3, matching a hand oracle, pinning Decision 42's block-boundary/any-depth semantics
    framed_stream_interpret # MF.3 s1 (multi-rate, Decision 44 rep. A): the walk reads a framed stream — unfold of the streamed axis gathers x[f*hop+k] into each frame's window from an input fed WIDER than the block, checked element by element at two N; a framed source that is not an input, one also read unframed, and a too-short feed each shown rejecting (the reserved second-block tripwire)
    framed_stream_runtime # MF.6 s3c-pre (multi-rate, Decision 44 rep. A): a framed stream whose frame count is a SYMBOL, walked at two block lengths fixed when the test compiles, lands on its literal twin element by element; a block length that is not a literal frame count refused
    energy_fraction # MF.4 (multi-rate, Decision 44 rep. A): energy_fraction's per-block core — unfold(streamed axis)->square->mean over the window->sqrt = windowed RMS, and Lt(threshold,rms) = a 0/1 above-threshold mask — as one graph, its RMS and mask matched element-by-element to a hand reference at two N (masks shown mixed 0/1)
    fir_unfold # MF.5 (multi-rate, Decision 44 rep. A; lifts Decision 40's reserved FIR): tutorial-05 FIR-via-unfold — unfold(signal, K, hop1)*taps summed over the window == y[f]=sum_k x[f+k]*h[k], matched element-by-element to a summed-tap convolution reference at two N with DISTINCT asymmetric taps; taps ride a wide input (a K-wide constant vector has no spelling yet — MF.6 frontend growth)
    wav_roundtrip # MF.6 s1 (multi-rate, Decision 44 rep. A; the audio layer's buildable slice): the in-house WAV reader/writer round-trips a known stereo buffer — float32 read back BIT-EXACT (memcmp, the writer's verbatim promise), the format (channels, sample rate, frame count) survives the file (the file-level half of sample-rate bind), and s16 round-trips within one quantisation step across both signs; and the checks that pinned wav.hpp's fixes through the deleted CLI tests: a stream read through open(FILE*) from a file and a pipe, the placeholders of a stream written onto a file, a frame over 64 KiB and a wrapping chunk refused, s16 the nearest step and clamped, a header too wide refused with the file untouched, sizes patched in a file opened by path, a FIFO and a socket written whole, a failed write reported by write() or by close(); wav.hpp alone, no graph machinery
    frontend_framed_unfold # MF.6 s2 (multi-rate, Decision 44 rep. A; frontend growth item a): the FRONTEND spells an unfold of the STREAMED axis — energy_fraction's windowed-RMS-mask core written in the user-facing DSL (x.unfold(0, win, hop) on a rank-1 value) lowers to exactly the op::Unfold node MF.4 hand-built (frames defaults to -1), walked and matched element-by-element to the from-definition reference at two N, mask shown mixed 0/1; no new op, MF.2 already lifted the streamed-axis refusal into representation A
    precision_resolve       # rework M3b-1: infer() resolves per-value precision by data-flow (decision C) -- High inherits, select-higher wins a join, unmarked stays Normal; Plan/28-dsl-proposals/02 A4: High, Normal and Fast the enumerators, a mark where a value is made (a port in its declaration) the graph of the mark written on its node, finish(level, ...) the graph set_default_precision gives, one output or two; a mark on a named value or on a value something reads are the build_refuses_precision_* fixtures
    index_notation          # Plan/archive/14-dsl L1: x[n], x[n - k], x[n - d] for a duration, and a Variable defined by v[n] = … build exactly the graphs of delayed/previous/is/value, at compile time and at run time; a duration's refusals, a second definition refused; a present read before the definition (B-18) put in reading order when finished (Plan/28-dsl-proposals/03 B4) — it runs at High as the walk does and as the graph written in that order, a lag of it and a lag of a value made after it too, the benchmark's delay in either order is one program and gives one output, a division by it is proven, a graph that reads nothing ahead stays as built; a lag before its definition that no loop reads moved after it and run as the walk runs it, a loop through an Expr's past refused by name and walked as the loop through a Variable runs, a chain of lags before their definitions pinned refused and run written after; the spellings that must not compile are the build_refuses_index_notation_* fixtures
    declarations            # Plan/archive/14-dsl L1: Input as a tensor ({…}, marked timeless, its stream appended) or a Signal (element axes, then its length; a Length records the stream's DimDecl), Param, Output by = and by [n] =, finish(a, b, …), an input or a variable as its identity output, set_default_precision — each equal to the older spelling's graph (timeless cleared; the settings to the hash of the graph a BuildContext finished, since L9), at compile time and at run time; every refusal with its text (lengths, ranks, names, reads before =, both, twice, never, a literal, two ports of one name)
    ranges_recorded         # Plan/26-value-ranges/02: the ranges a graph states, a port's in its declaration and a value's promise, recorded as written on Graph::ranges; a list of comparators and intersect piece by piece; marks through merges and a subgraph; every refusal by its text; a graph with marks plans the program of the same graph without them
    ranges_rules            # Plan/26-value-ranges/03: the proof's rules, one column value at a time — each range rule and each domain on a small graph, its exact range and findings (open and closed ends, 0 times infinity, a NaN end, the square and limit rules, min and max on ties), ranges of several pieces, five merged to four across the smallest gap
    ranges_walk             # Plan/26-value-ranges/03: the proof's walk — a past read adds silence, a promise proven, narrowing or contradicted, induction (one pole; two recurrences reading each other, one three samples back; a biquad not proven), a variable at its own sample, findings grouped by their root; abs's and frac's facts (frac's closed: x - floor(x) rounds to 1 just below 0), and a fact the rules contradict leaving the rules' range, never no number; the wording, %g's numbers, the monotone rows' cores at their edges; strict: a finding refuses the graph by its words, checked or not, and the graphs proven compile and run (an open bound, a hole, a feedback value read by a division); the refused ones are the build_refuses_proof_* fixtures
    ranges_oracle           # Plan/26-value-ranges/03: the proof against the reference walk — 3000 random graphs over random port ranges with holes, 32 samples each from silence on inputs at the ranges' ends, beside their holes and inside: every number within its proven range (16 ulp), no NaN where the proof finds nothing; frac of numbers just below 0; each row's checks counted, the first draft's unsound rows (_dline, _tblread, _order, _ovadd, _matmul) required
    ranges_checked          # Plan/26-value-ranges/04: what a debug build checks — a Param when set (inside, outside, NaN, before it is rounded), an input's numbers before a run (a signal, a Length's frames, a tensor), each promise the proof did not prove after it as an output #k (a computed value, a feedback value), each failure in its words, the numbers found before the range (a tensor's promise by element, a long range cut after them), its numbers in the shortest digits of the double each was compared as (1 + 1e-12 against le(1), a float as its double, 0.1f against le(0.1)); a proven promise makes no output; the checks' buffers through moves and prepares; release plans the graph as written without ranges
    ranges_cascade          # Plan/26-value-ranges/04, split out of ranges_checked: a cascade's parallel form kept or declined by where its check reads — a promise on its first section keeps debug serial (the program of the cascade read in its middle, a float cascade's numbers), one on its last keeps the form in both (a double cascade's numbers)
    shortest_digits         # Plan/26-value-ranges, item 5: the debug checks' numbers — MessageWriter in Digits::Shortest writes the shortest digits that read back as the same double, a float as the double it is, laid out as %g, beside std::to_chars's general format: random doubles and floats of every exponent, subnormals, the largest finite, powers of 2 and of 10 and their neighbours, 0.1, 1 + 1 ulp; under constant evaluation too
    executor                # Plan/archive/14-dsl L2: Executor runs what loops::run runs over compile's program, bit for bit — corpus graphs, the comb, a Length at several lengths (each from prepare), a timeless output beside signals, F32 and F64 params, a lambda-built graph, a moved Executor, an unread input left unbound, one buffer as input and output; from L3 state across runs (reset, blocks of a Length equal one run, two runs in place); every run-time refusal with its text; a declared length driven by a knob, in blocks (L8); the build failures are the build_refuses_executor_* fixtures
    executor_state          # Plan/archive/14-dsl L3, split out of executor (Plan/15-zero-overhead/03): a run continues from the one before, from silence at every fresh start (prepare, reset); F64 over two voices; one buffer as the input and the output, run twice; a reset before prepare and after a move; Params and outputs of both types side by side, moves (set_initial gone, Plan/23 3.4)
    streaming               # Plan/archive/14-dsl L3: state carried from run to run — a signal run in blocks on one workspace gives one run's numbers bit for bit, at Normal and High: banks, poles, a coupled pair, echoes shallower and deeper than the block, a FIR and a biquad over an input (its tail), a delay line, a lagged ramp, an oscillator banded and not, a timeless output first; block sizes from 1 to past the whole, mixed (capacity mode) and read off each program; split along time, along voices and in a band, in order and on a pool; a literal stream in blocks; reset (exactly the carried regions), seed, and streams() both ways; a knob-driven oscillator, its length declared (L8), in a stream band; a knob-driven oscillator beside an input, its Length taken (Plan/28-dsl-proposals/03 B1)
    streams                 # Plan/archive/14-dsl L3, split out of streaming (Plan/15-zero-overhead/03): loops::streams refuses state one sample long broadcast over a longer signal (a recurrence, a lagged input), two literal streams that both carry state, and state in a stream band along an axis not the stream's — at compile time
    turned_operand          # Plan/20-score-product: Q . K^T's K read from a copy turned around (its lane-carrying axis made contiguous) by a stage of its own in 8 x 8 tiles, then relaid where asked; schedules edited to the register tile, bit for bit against the walk at High (blocks that do not divide, d below and above a lane, a batch axis, zen2, tiny caches and the default machine, K a constant turned and relaid when compiled; an operand carrying no lanes refused by name); the planner's own programs where K is read 32 times or more (not 13) on Executor: attention whole, turned and relaid on tiny caches at High, and tiled at Normal
    constant_tensor         # Plan/19-constants: constant(…) tensors against plain C++ and against their twins (the same graph with an Input bound to the numbers), bit for bit at High — broadcast along a signal, a constant's row sums, a 3-tap kernel over windows, a contracted weight, a table, an output, a gradient through a constant weight; a braced f32 list rounded where recorded, -0 and a NaN's payload kept apart in the pool, numbers that do not fill the shape refused
    constant_ramps          # Plan/15-zero-overhead/03 step 5: a ramp whose past nothing reads is a constant read where it lies — as a contraction's operand, relaid by the planner on tiny caches, as a table, as an output over a Length prepared at two lengths — on Executor against the walk, bit for bit
    tensor_signal           # Plan/archive/14-dsl L4: time is declared — a value is a signal or a tensor: the kinds (inputs, iota's seed, neutral constants, coupled recurrences), the refusals in their order (a tensor-only recurrence, a lag, an fft, a tensor times a signal, a tensor's hidden axis; squeeze and unsqueeze bounds, the rank guard), marked reshapes and short transpose orders completed (inside a recurrence, a rank-0 value, past kMaxRank), iota a tensor or a signal recording its Length, a tensor and a Param against a signal (the notation's graphs: tensor_signal_notation), run against a plain loop; the recurrences it refuses, declared (L8), accepted; the literal one undeclared takes x's length (Plan/28-dsl-proposals/03 B1), equal to it declared
    tensor_signal_notation  # split from tensor_signal (gate structure_limits): the public einsum / rearrange / reduce graph-equal to the old readings, a computed value's spec checked once its shapes settle, the label bound
    declared_length         # Plan/archive/14-dsl L8: a recurrence no signal drives declares its length (Variable{Signal}, Output{name, Signal}) — the knob-driven oscillator at 997 and over a Length at five lengths, the declared Output, a bank, constants only, a coupled pair with either declared, a declaration wider than its operands; each runs at Normal and High bit for bit against a plain loop; R1–R6 (wider, no past, f64, two lengths, a folded past, never defined) and the declared Output's = refused, each with its text; the declarations that are no Signal are the build_refuses_declared_length_* fixtures; undeclared in a graph of no stream, refused
    declared_length_taken   # split from declared_length (gate structure_limits): undeclared, a recurrence no signal drives takes the graph's one length (Plan/28-dsl-proposals/03 B1) — beside an input at 997 and over a Length, a bank, beside a clock, a ramp the only seed, a coupled pair, f64, a definition typed through a variable defined after it, one typed by a where's condition — each graph equal to it declared and idempotent, two run on loops::run and Executor at High bit for bit against the walk; two lengths (two inputs; a declared variable and an input), no past and a folded past refused
    members                 # Plan/archive/14-dsl L6: the tensor members (sum/mean/max/min/prod with and without keepdims, transpose, reshape, slice, unfold, unfold2d, squeeze, unsqueeze, realize, sort/topk/quantile/median) each building its old spelling's node (a detail:: builder since L9), graph for graph; the member reshape's element-only target completed by the analysis; x.max(0) a reduction and max(x, 0.0) elementwise; negative axes and a reshape target past kMaxRank - 1 refused; the float-axis build failures are the build_refuses_members_* fixtures; Plan/28-dsl-proposals/02 A2: the members on an Output and a Variable build what Expr(y)'s build, braced lists too, and an Output read by one before = is refused as Expr(y) is
    abs                     # Plan/29 step 1: abs(x) is one _abs node, the sign bit cleared (abs(+0) is +0, where max(x, -x) answered -0); over +-0, +-denormals, the least normals, +-infinity, NaNs of either sign and finite numbers, f32 and f64: on Executor at High bit for bit the walk, and at Normal and on the walk bit for bit std::fabs
    log10_cospi             # Plan/28-dsl-proposals/02 A6: log10 and cospi, composed — each building the graph of the operations it names (log(x) * log10e; sinpi(1/2 - r), r = |x| - 2 floor(|x|/2)), graph for graph; run on Executor at Normal and High over 4096 floats each against long double, held to the bounds the sweeps over every float set (log10 2.25 and 1.5 ulp, cospi 2.0 and 1.25), cospi exact at the integers and halves, the sweeps' worst points among them; log10 of a value not kept above 0 is the build_refuses_proof_a_log10_of_zero fixture
    cos_tan_pow_exp2        # Plan/28-dsl-proposals/05 D2, step 1: cos, tan, exp2 and pow spelled, each building the graph of its operation (pow over two values, a value and a number, a number and a value), graph for graph; none has a float core yet, so each runs its double core one number at a time: on Executor at High, Normal and Fast, over f32 and f64, on the default machine and zen2, 4096 numbers each equal to the walk's bit for bit (cos past 2^40 and at the infinities, exp2 at its subnormal results and overflow, tan to its branch's edges, pow's bases from 2^-40 to 2^40 and exponents in [-30, 30]); sin's gradient, which emits cos, on Executor at High as the walk runs it, against central differences and the closed form; a pow of a base that may be negative is the build_refuses_proof_a_pow_of_a_negative_base fixture
    tables                  # Plan/28-dsl-proposals/05 D3: wavetable(table, phase) and lookup(table, x, lo, hi), each building its graph (a _tblread of the table's N cells, after lookup's clamp of x's place), graph for graph; on Executor at High, Normal and Fast, over f32 and f64, on the default machine and zen2, every number equal to the walk's bit for bit at High and over f64, an f32 read at Normal and Fast (in float, Decision 78's float reads) within float_reads.hpp's bound of it, over an f64 table in double and the walk's, a NaN exactly where the walk's is: a wavetable of a constant and of an input (phases sweeping, negative, past and just below a whole turn, the floats beside 1 and every cell's place, a NaN and the infinities each a NaN), lookup over 64 cells and 100 (lo, hi, below, above, the floats beside every cell's x, the infinities, a NaN); lookup's edges: the first cell at and below lo, the last at and above hi (64 cells: exactly; 100: just short of it, never the first), a NaN at a NaN; the float tap inside its table, alone and in eight lanes, over tables of 1 to 2^30 cells; a lookup inside feedback (a saturated one-pole, one voice and three) at High on both machines as the walk reads it; a wavetable oscillator in blocks equal to one run at Normal on both machines; the proof: a constant table's read in its cells' hull, lookup's phase in [0, 63/64]; refused by name at run time, lookup's bounds, span and scale a double does not hold and a table of one cell; a computed table, lookup's crossed and infinite bounds and one cell are the build_refuses_table_* fixtures
    tables_in_one_turn      # Decision 78 after the close, the phase in one turn: a table read whose phase the proof keeps in [0, 1] at every precision (ranges/in_every_rounding.hpp: frac's fact, lookup's clamp by min and max, a choice of numbers, a recurrence and its past) is marked so and reads with no wrap; an input phase, declared or not, frac(p) * 1.5, a promise and a ranged Param are not (the proof keeps the declared, the promised and the Param's in [0, 1], which release checks nowhere); on Executor at High, Normal and Fast, over f32 and f64, on the default machine and zen2, a wavetable of frac(p) and of min(abs(p), 1) (abs(-0) is +0), a lookup (lo 0, so -0), an oscillator and a lookup in feedback: at High the walk's numbers bit for bit, at Normal and Fast those of the same read with its phase times 1 (read with the wrap), the phases at 0, -0, 1, beside them, every cell's place and a NaN; a false fact [0, 1] stated by push_range on a computed phase, read in one turn: phases below 0 over an input table between NaNs at every level, type and machine, every number within the table's cells (a wrong number, never a read outside); an oscillator in blocks of 1 to 100 equal to one run at High, Normal and Fast; frac's fact over a million floats (alone and in lanes) and doubles as the runner computes x - floor(x)
    delay_lines             # Plan/28-dsl-proposals/05 D4: x[n - d.at_most(b) * ms], a delay that changes, building a delay line of x, its position d times the samples a unit holds at n's rate, its longest distance the bound rounded up to whole samples (in ms, us and second; of an Input, a computed value, a Variable, an Output; at_most on a Variable and an Output), graph for graph; the copy of an input its lines read, one for two, a fixed point of infer and of itself; on Executor at High, Normal and Fast, over f32 and f64, on the default machine and zen2, a line over an input (read from the copy compiling makes) and over a computed value, its distance below 0, past the bound, whole samples and between them, a NaN and the infinities, every number equal to the walk's bit for bit at High and over f64, an f32 line at Normal and Fast (its blend in float, Decision 78's float reads) within 3.5 float steps of its larger tap of it, at an f64 distance in double and the walk's; the float tap inside its line, alone and in eight lanes, over lines of 0 to 2^31 - 1 samples; three voices at High; a Param distance against the float blend written out; a chorus at High as the walk reads it, at Normal on both machines in blocks shorter and longer than its line and a 120 ms line in blocks of 64 equal to one run, and after a reset; refused by name: a delay line in feedback (its signal, a flanger's; its distance) and of a value defined later with no loop through it, a delay of a constant, a line over an input planned with no copy; the spellings short of a bound, a unit, a finite bound at least 0 and a rate are the build_refuses_delay_* fixtures, the copy past kMaxValues and kMaxNodes the build_refuses_capacity_copy_* ones
    comparisons             # Plan/28-dsl-proposals/02 A1: <, >, <=, >= between two values or a value and a number (an integer too), where with a number first, clamp with an expression for either bound or both, and what converts to an expression compared (a Param, a past read, a Variable by name and by v[n], an Output) — each building the graph of the operations it names, graph for graph; the four run on Executor at Normal and High as C++ answers, the zeros' signs and a NaN on either side included; clamp's two numbers the wrong way round are the build_refuses_clamp_bounds_crossed fixture; Plan/28-dsl-proposals/03 B2: where with two numbers, graph for graph, and JUCE's peak follower (its coefficient a where of two numbers in the recurrence) beside a sign typed by its condition, at f32 and f64 on Executor at High bit for bit against the walk
    loops_ir                # loop tree M1.4: the program IR — hand-built trees printed and pinned, compared bit for bit, and well_formed refusing one ill-formed tree per rule of Plan/archive/13-loop-tree/02 §4.2
    loops_walks             # loop tree, M1 review: well_formed refusing, by name, each hand-built tree whose walks the runner would run wrong or past an end (lanes, relaid blocks, every point made, masked strips, nested loops, the past, splits, bands, kept stages, skewed stages), and accepting the trees it must
    loops_runner            # loop tree M1.8: the runner on trees built by hand, against hand loops bit for bit — sibling loops after a fold (its value splatted into lanes), a register tile jammed columns-first, a Clip loop inside a strip; and knob math's frame copy: a running total in memory read where it is stored, not through a copy, and a band whose second stage reads what its first writes at every row, copied anew each chunk
    loops_skewed            # sections in lanes: skewed stages built by hand against the walk of their linked graphs, bit for bit — the linked graphs' TPT pair, LR8, five peaks (two bodies, their coefficients [5, 1] feeds), eight tanh sections and the pair into the eight at High, and a Param cascade of 3 and 8 direct-form-I biquads at Normal (x and y float, the cycle double), its coefficients [k, 1] feeds or gathered by a stage from 5k Params: one run, blocks of 1, k - 1, k, 32, 256 and of uneven lengths, after reset, canaried, each block's input exactly its length, nothing stored past it; the coefficients moving before every run at blocks of 1, 32 and 256; a listing pinned
    fold_in_feedback        # Plan/23 10: a fold inside a feedback stage's sample — the walk takes a fold in a cycle (against the loop by hand); the shape built by hand (plain banks of 8, 3, 16 doubles and 5 floats, the loop read in the lanes, a lag of the fold's own result, TDF2's lanes after the fold) against the walk at High, bit for bit: one run, blocks of 1, 7, 8, 256 and of uneven lengths, after reset, canaried
    fold_in_feedback_planned # split from fold_in_feedback (gate structure_limits): the planned stage on Executor<…, zen2> at High against the trees, at Normal within rounding of the walk; the refusals by name; a kept axis never split; cheap maps never join an earlier feedback stage
    loops_workspace         # loop tree M1.6: the workspace over a corpus planned for zen2 and two edited schedules — where every buffer and split-band copy stands, and its size, pinned in tests/goldens/loops_workspace/ (recorded equal to the old workspace to the byte); a fresh workspace's bytes, a buffer's region, the silence
    loops_lower             # loop tree M2.3: the lowering on stages walked as they stand — a stage of every kind (maps, folds, contractions, siblings, a network, state, a cycle, a feed's past, a delay line, parts, an inlined producer, a ramp) as well-formed trees pinned in tests/goldens/loops_lower/, and every refusal of formation, operands and validation, by name, at compile time
    loops_primitives        # loop tree M2.4: what a schedule's loops and choices make of a stage — each as a well-formed tree pinned in tests/goldens/loops_primitives/ (lanes, blocks, a split, a register tile, a reorder, a relaid operand, a prefetch, bands, parts, a copy two readers share); what a schedule may not plan, and each choice's refusals, by name beside its legal twin
    loops_plan              # loop tree M3.4: the planner by itself — the corpus of Plan/archive/13-loop-tree/04 §4 but its edited schedules, and harris, attention, three blurs, an 8 MB unsharp, the oscillator's stream band, on zen2, the default machine and tiny caches, and each rule's decline case: every schedule printed and pinned in tests/goldens/loops_plan/, every schedule lowered; refusals as values at compile time
    loops_plan_skewed       # sections in lanes: a stage holding a link planned skewed in the voice layout, its cycle and what leads to its last section's slice, nothing else — the linked graphs and the K = 8 Param cascade as compile plans it (the gather's stage before the skewed one) planned on zen2, their schedules pinned (LR8's and the cascade's as rows of tests/goldens/loops_plan/), none split or banded; the refusals by name (no state, no lanes, past four lane bodies, sections read other than through the last one's slice, a band holding one)
    loops_lower_skewed      # sections in lanes: the lowering of a skewed stage — the planner's schedules (the linked graphs at High, the K = 8 Param cascade with the gather's stage before it) planned on zen2 lower to programs well_formed accepts, each skewed stage holding the program's skew facts (skewed, its time loop, the link shifted with no Open or Close, the fill at lane 0, the slice and its reshape at lane k - 1), LR8's and the cascade's programs rows of tests/goldens/loops_lower/; the refusals by name (a link in a stage not skewed, a skewed stage moving along time at no lane, lowered into more than one stage or with no Vector along its sections, a link's cycle cut between two stages, its lanes in two chunks)
    executor_skewed         # sections in lanes: the whole path — the linked graphs over a Length at High and the K = 8 Param cascade as compile plans it (merged by sections_in_lanes, given the rules after it) through compile(), which leaves each as written, and Executor<…, zen2>, each running skewed bit for bit as its walk over 4096 samples of noise: one run, blocks of 1, k - 1, k, 32 and 256, of uneven lengths, and after reset()
    param_arms              # sections in lanes: the benchmarks' Param arms at rest (biquad8 and biquad3 with their coefficients Params, whole and over a Length; lr8, its constants; one, two and eight RBJ peaks worked out from three knobs a band, and one and two Param biquads set from their design) on a release Executor<…, zen2> at Normal, rewritten, against the walk of the graph as written over 4096 samples of noise, in one run and in blocks of 256 and 32, each within its benchmark's bound; the lanes skewed where the price takes them (K = 3, 4, 8), the cascade where it keeps it
    param_moving            # sections in lanes: the knob moving — biquad8/heinzel-param-moving's graph on a release Executor<…, zen2> (the lanes) over 64 blocks of 256 samples of noise, band 4's coefficients set before every block from a sweep (the probes', 250 Hz to 4 kHz), against the cascade in double following the same coefficients: at most twice as far as the program of the graph rewritten without the lanes rule, planned by planned::
    lanes_past_held         # sections in lanes only where the skewed stage holds their past in registers — four RBJ band-passes (x[n-2] without x[n-1]), eight y = a s[n-d] + 0.5 s + p y[n-1] (d = 2, 3, 9, 64) and eight reading y[n-2] alone (a link fills no gap) declined, eight third-order sections (a skewed stage's runs of lags no longer cut) taken; and eight reading a lag of a lag of their input, of a feed map or three deep of their state declined, eight reading y1[n-1] of y1 = y[n-1] taken; each through compile() the graph rewritten, not as written, and on a release Executor<…, zen2> bit for bit the walk of its compiled graph and within the rules' bound of the cascade in double; the y[n-2] sections linked by hand refused by the lowering by name
    zero_copy               # Plan/18-zero-copy/02: outputs written straight into the caller's buffer — elem F32 and F64 over a Length in blocks, maps, a fold, contractions, rmsnorm, a median, a FIR over a feed, a pad, outputs read later, several outputs, two of one value, a recurrence's output (copied) — against the copy path bit for bit, caller buffers exactly sized and NaN behind a guard; in place, one float above, two outputs over each other: routed and exact
    inline_arena_off        # Plan/18-zero-copy/01: built with -DHEINZEL_MAX_INLINE_ARENA=0, no arena lies inside its workspace: a static program's is the page-aligned allocation placed with the stagger, and it runs as by hand
    switches_off            # built with -DHEINZEL_KNOB_MATH=0 and -DHEINZEL_LANES=0 (`docs/knob-math.md`, `docs/sections-in-lanes.md`, "Switch"), whatever the options: eight Param RBJ biquads and an RBJ peak from three knobs hold no skewed and no kept stage on zen2, and run on a release Executor in blocks of 256 and 32 within param_arms' bound of the walk
    graph_facts             # Plan/17-simplify-more 05: the graph facts table (loops/graph/facts.hpp) — every value's producer, what a read of it finds, what it is read through and how far back it is read, every node's statement — against their definitions written as scans, over the bench, operation, surface and band corpora and variables read through views
)

# A feature switched off (the options HEINZEL_KNOB_MATH and HEINZEL_LANES, `docs/knob-math.md`, `docs/sections-in-lanes.md`)
# takes its own groups out of the suite: defined, so the lines below that set them up stand, but neither built nor run.
# knob_stages is in both: its knob cascade's stages are kept, and from three bands on its sections one linked cycle.
set(HEINZEL_KNOB_MATH_GROUPS loops_kept knob_stages knob_price)
set(HEINZEL_LANES_GROUPS linked_sections linked_price lanes_price rewrite_sections_in_lanes loops_skewed loops_plan_skewed
                         loops_lower_skewed executor_skewed lanes_past_held param_arms param_moving knob_stages)
set(_switched_off "")
if(NOT HEINZEL_KNOB_MATH)
    list(APPEND _switched_off ${HEINZEL_KNOB_MATH_GROUPS})
endif()
if(NOT HEINZEL_LANES)
    list(APPEND _switched_off ${HEINZEL_LANES_GROUPS})
endif()

foreach(group IN LISTS HEINZEL_TEST_GROUPS)
    add_executable(test_${group} unit/${group}_test.cpp)
    target_link_libraries(test_${group} PRIVATE heinzel)
    # Scaffolding shared between test translation units — building a graph by hand, and nothing
    # else. Deliberately not part of the library: it is how tests are written, not what they check.
    # `reference/` is the walk the tests check against.
    target_include_directories(test_${group} PRIVATE support conservation reference)
    if(group IN_LIST _switched_off)
        set_target_properties(test_${group} PROPERTIES EXCLUDE_FROM_ALL TRUE)
        continue()
    endif()
    add_test(NAME ${group} COMMAND test_${group})
endforeach()
# The loop-tree tests' listings, read (and, with HEINZEL_REPIN set, rewritten) where they are kept under review.
target_compile_definitions(test_loops_lower PRIVATE HEINZEL_LOOPS_LOWER_GOLDENS="${CMAKE_CURRENT_SOURCE_DIR}/goldens/loops_lower")
target_compile_definitions(test_loops_primitives PRIVATE HEINZEL_LOOPS_PRIMITIVES_GOLDENS="${CMAKE_CURRENT_SOURCE_DIR}/goldens/loops_primitives")
target_compile_definitions(test_loops_plan PRIVATE HEINZEL_LOOPS_PLAN_GOLDENS="${CMAKE_CURRENT_SOURCE_DIR}/goldens/loops_plan")
target_compile_definitions(test_loops_workspace PRIVATE HEINZEL_LOOPS_WORKSPACE_GOLDENS="${CMAKE_CURRENT_SOURCE_DIR}/goldens/loops_workspace")
# The tests that sit on a capacity by design (a fan of 96 buffers, a chain of 150 values, twelve layers, a cycle as long
# as a stage holds) keep today's capacities, whatever the headers' defaults: a target's -D wins over the header's
# `#ifndef` and, coming after it, over CMAKE_CXX_FLAGS (`docs/limitations.md`, `Plan/15-zero-overhead/02`).
set(HEINZEL_TODAYS_CAPACITIES
    -DHEINZEL_MAX_VALUES=320 -DHEINZEL_MAX_NODES=320 -DHEINZEL_MAX_IO=32 -DHEINZEL_MAX_DIM_DECLS=8
    -DHEINZEL_MAX_STMTS=512 -DHEINZEL_MAX_TREE_NODES=3584 -DHEINZEL_MAX_BUFFERS=96 -DHEINZEL_MAX_STAGES=16
    -DHEINZEL_MAX_STAGE_STMTS=128)
target_compile_definitions(test_inline_arena_off PRIVATE HEINZEL_MAX_INLINE_ARENA=0)
target_compile_definitions(test_switches_off PRIVATE HEINZEL_KNOB_MATH=0 HEINZEL_LANES=0)
foreach(group loops_lower loops_primitives loops_plan nest_grouping nest_grouping_refusals nest_recurrence_review nest_v6_review nest_contract
              nest_feedback_chain knob_stages)
    target_compile_options(test_${group} PRIVATE ${HEINZEL_TODAYS_CAPACITIES})
endforeach()
# Five biquads as five cycles, each slicing its five coefficients out of the inputs, pass the default values.
target_compile_options(test_linked_sections PRIVATE -DHEINZEL_MAX_VALUES=320 -DHEINZEL_MAX_NODES=320)
# The linked graphs' five biquads as five cycles, as in linked_sections; eight RBJ biquads' 40 Params.
target_compile_options(test_linked_price PRIVATE -DHEINZEL_MAX_VALUES=320 -DHEINZEL_MAX_NODES=320 -DHEINZEL_MAX_IO=48)
# The probe's K = 8 Param cascade: eight RBJ biquads' 40 Params and the gather of them.
target_compile_options(test_lanes_price PRIVATE -DHEINZEL_MAX_VALUES=320 -DHEINZEL_MAX_NODES=320 -DHEINZEL_MAX_IO=48)
# Eight RBJ peaks through their knob math, two Param one-poles padded to a graph's nodes, eight RBJ biquads' 40 Params.
target_compile_options(test_rewrite_sections_in_lanes PRIVATE -DHEINZEL_MAX_VALUES=640 -DHEINZEL_MAX_NODES=512 -DHEINZEL_MAX_IO=48)
# The both-ways corpus doubled: its largest entry, 62 nodes over 120 values, twice.
target_compile_options(test_rewrite_equal_values PRIVATE -DHEINZEL_MAX_VALUES=256 -DHEINZEL_MAX_NODES=256)
# 400 lags of one input and their sum. Its budget comes after the library's (a source's options close the line): 1-2M
# steps merged with the attribute hashed, 6-8M without, which probed every lag before each (clang only, as the flag).
target_compile_options(test_equal_values_linear PRIVATE -DHEINZEL_MAX_VALUES=820 -DHEINZEL_MAX_NODES=820)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_source_files_properties(unit/equal_values_linear_test.cpp PROPERTIES COMPILE_OPTIONS -fconstexpr-steps=4000000)
endif()
# The K = 8 Param cascade, rewritten: eight RBJ biquads' 40 Params and the gather of them (planned, lowered, and run).
target_compile_options(test_loops_plan_skewed PRIVATE -DHEINZEL_MAX_VALUES=320 -DHEINZEL_MAX_NODES=320 -DHEINZEL_MAX_IO=48)
target_compile_options(test_loops_lower_skewed PRIVATE -DHEINZEL_MAX_VALUES=320 -DHEINZEL_MAX_NODES=320 -DHEINZEL_MAX_IO=48)
target_compile_options(test_executor_skewed PRIVATE -DHEINZEL_MAX_VALUES=320 -DHEINZEL_MAX_NODES=320 -DHEINZEL_MAX_IO=48)
# The Param arms: eight RBJ peaks through their knob math (`bench_knobs`' capacities), kEq8's 40 Params.
target_compile_options(test_param_arms PRIVATE -DHEINZEL_MAX_VALUES=512 -DHEINZEL_MAX_NODES=512 -DHEINZEL_MAX_STMTS=512
                                              -DHEINZEL_MAX_TREE_NODES=1024 -DHEINZEL_MAX_IO=48)
# kEq8's 40 Params, rewritten with the lanes and without, at `biquad8/heinzel-param`'s capacities.
target_compile_options(test_param_moving PRIVATE -DHEINZEL_MAX_VALUES=256 -DHEINZEL_MAX_NODES=256 -DHEINZEL_MAX_STMTS=256
                                                -DHEINZEL_MAX_TREE_NODES=1024 -DHEINZEL_MAX_STAGES=24 -DHEINZEL_MAX_STAGE_STMTS=256
                                                -DHEINZEL_MAX_IO=48)
# kEq8's 40 Params, rewritten without the lanes, at `biquad8/heinzel-param`'s capacities.
target_compile_options(test_switches_off PRIVATE -DHEINZEL_MAX_VALUES=256 -DHEINZEL_MAX_NODES=256 -DHEINZEL_MAX_STMTS=256
                                                 -DHEINZEL_MAX_TREE_NODES=1024 -DHEINZEL_MAX_STAGES=24
                                                 -DHEINZEL_MAX_STAGE_STMTS=256 -DHEINZEL_MAX_IO=48)
# Eight third-order sections' 32 Params, rewritten.
target_compile_options(test_lanes_past_held PRIVATE -DHEINZEL_MAX_VALUES=320 -DHEINZEL_MAX_NODES=320 -DHEINZEL_MAX_IO=48)
# ...and LR8's and the cascade's schedules and programs, rows of the loop tree's listings.
target_compile_definitions(test_loops_plan_skewed PRIVATE HEINZEL_LOOPS_PLAN_GOLDENS="${CMAKE_CURRENT_SOURCE_DIR}/goldens/loops_plan")
target_compile_definitions(test_loops_lower_skewed PRIVATE HEINZEL_LOOPS_LOWER_GOLDENS="${CMAKE_CURRENT_SOURCE_DIR}/goldens/loops_lower")
# A stage gathering eight biquads' 40 Params, one feed each.
target_compile_options(test_loops_skewed PRIVATE -DHEINZEL_MAX_IO=48)
# Three RBJ peaks in series pass the default values, nodes and statements.
target_compile_options(test_loops_kept PRIVATE -DHEINZEL_MAX_VALUES=320 -DHEINZEL_MAX_NODES=320 -DHEINZEL_MAX_STMTS=256)
# The price's corpus at its probe's capacities: one stage watching 40 Params, eight bands.
target_compile_options(test_knob_price PRIVATE -DHEINZEL_MAX_IO=48 -DHEINZEL_MAX_BUFFERS=128 -DHEINZEL_MAX_NODES=512 -DHEINZEL_MAX_VALUES=512
                                               -DHEINZEL_MAX_STAGES=24 -DHEINZEL_MAX_STAGE_STMTS=512 -DHEINZEL_MAX_STMTS=512
                                               -DHEINZEL_MAX_TREE_NODES=1024)
# KFR's graphic EQ is 112 nodes, and inside a loop one cycle of them (`eq8fb`); the declines' eight sections as many.
foreach(group rewrite_parallel_form rewrite_parallel_form_declines)
    target_compile_options(test_${group} PRIVATE -DHEINZEL_MAX_VALUES=320 -DHEINZEL_MAX_NODES=320 -DHEINZEL_MAX_STMTS=512
                                                 -DHEINZEL_MAX_TREE_NODES=3584 -DHEINZEL_MAX_STAGE_STMTS=192)
endforeach()
# How much a feedback stage merges is the machine's, not a stage's capacity (Plan/23 04 4.3c): one file, built at the
# default capacity and at the comparison targets' 256, plans the same stages.
foreach(_cap 128 256)
    add_executable(test_merge_bound_${_cap} unit/merge_bound_test.cpp)
    target_link_libraries(test_merge_bound_${_cap} PRIVATE heinzel)
    target_compile_options(test_merge_bound_${_cap} PRIVATE -DHEINZEL_MAX_VALUES=320 -DHEINZEL_MAX_NODES=320 -DHEINZEL_MAX_STAGE_STMTS=${_cap})
    add_test(NAME merge_bound_${_cap} COMMAND test_merge_bound_${_cap})
endforeach()
# A lost wake-up in a pool must fail these, not hang the suite.
set_tests_properties(nest_parallel nest_parallel_plan nest_parallel_review nest_band_parallel PROPERTIES TIMEOUT 300)
# A pipe, a FIFO or a socket that blocks must fail the WAV test, not hang the suite.
set_tests_properties(wav_roundtrip PROPERTIES TIMEOUT 60)

# `heinzel::host` (Plan 24): the corpus planned for the machine HEINZEL_AUTOTUNE measured gives the walk's bits at High.
# Only with the option, which writes the header: a measured machine can cost speed, never a bit.
if(HEINZEL_AUTOTUNE)
    add_executable(test_host_machine unit/host_machine_test.cpp)
    target_link_libraries(test_host_machine PRIVATE heinzel)
    target_include_directories(test_host_machine PRIVATE support conservation reference)
    add_test(NAME host_machine COMMAND test_host_machine)
endif()
# The header itself, in every build: written by HEINZEL_AUTOTUNE's writer with every field measured and compiled, so a
# field of `Machine` renamed or moved and not in `tools/tune/header.cmake` fails here, not only in tuned builds.
add_test(NAME host_machine_header
         COMMAND ${CMAKE_COMMAND} -DCXX=${CMAKE_CXX_COMPILER} -DSTD=${_std_flag} -DINC=${CMAKE_SOURCE_DIR}/include
                 -DTUNE=${CMAKE_SOURCE_DIR}/tools/tune -DOUT=${CMAKE_CURRENT_BINARY_DIR}/host_machine_header
                 -DTU=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/host_machine_header/tu.cpp
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/host_machine_header.cmake)

# ---- threads (P6, `Plan/archive/parallel/02`): the pool, and no thread without the option ----------------------
# The pool's own test has a timeout: a lost wake-up must fail it, not hang the suite.
if(HEINZEL_HAVE_THREADS)
    add_executable(test_thread_pool unit/thread_pool_test.cpp)
    target_link_libraries(test_thread_pool PRIVATE heinzel)
    add_test(NAME thread_pool COMMAND test_thread_pool)
    set_tests_properties(thread_pool PROPERTIES TIMEOUT 60)
endif()

# Programs the planner split, compiled WITHOUT `HEINZEL_HAVE_THREADS` and run serially and by in-order workers,
# name no thread in their object — and, shown objecting, the same translation unit with the option and a pool does.
set(_no_threads_check
    -DCXX=${CMAKE_CXX_COMPILER} -DSTD=${_std_flag} -DSTEPS=${HEINZEL_CONSTEXPR_BUDGET_FLAG}
    -DINC=${CMAKE_SOURCE_DIR}/include -DSUPPORT=${CMAKE_CURRENT_SOURCE_DIR}/support
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/no_threads/tu.cpp)
add_test(NAME gate_no_threads_without_the_option
         COMMAND ${CMAKE_COMMAND} ${_no_threads_check} -DOBJ=${CMAKE_CURRENT_BINARY_DIR}/no_threads_tu.o
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_threads_without_the_option.cmake)
add_test(NAME gate_no_threads_would_object
         COMMAND ${CMAKE_COMMAND} ${_no_threads_check} -DOBJ=${CMAKE_CURRENT_BINARY_DIR}/no_threads_pool_tu.o
                 "-DEXTRA=-DHEINZEL_HAVE_THREADS -DNO_THREADS_TU_WITH_A_POOL"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_threads_without_the_option.cmake)
# The undefined thread as `nm -C` spells it: `pthread_create`, `std::thread` (ELF, libstdc++) or `_pthread_create`,
# `std::__1::thread` (Mach-O's leading underscore, libc++'s inline namespace).
set_tests_properties(gate_no_threads_would_object PROPERTIES PASS_REGULAR_EXPRESSION
                     "no-threads check: the object references threading:.* U (_?pthread|std::(__1::)?thread)")

# ---- zero overhead (`Plan/15-zero-overhead/01`, "The gates"): an `Executor` over the run it wraps -------------------
# Each workload of `zero_overhead/` in each arm, twice: `zo_<w>_<arm>` for its bytes, `zo_<w>_<arm>_count` for its
# instructions per run. Only in the builds with no sanitizer, no `-march` and no build type (`rw-clang-26`, `-23`), on
# this machine's Linux (the counts read `perf_event_open`): the numbers mean nothing elsewhere. The limits are the worst
# row of both builds plus 32 B / 4 instructions (OQ-2; 128 B and 16 before): +45 B (W1) and +5 instructions (W1, W5,
# W7), on `49309303`; in instructions, 0.1 % of the run where that is more (Gerald, 2026-10-05). Each row's bytes are
# pinned besides (`_zo_bytes`: its delta, recorded; reviewed like a golden when it moves): at most 32 B either way.
set(_zo_bytes w1:-32 w2:0 w3:-32 w4:-156 w5:0 w6:-32 w7:-4 w8:20 w9:-4 w10:-44 w11:-32 w12:-28)
# The workloads every output of which is written in the caller's memory (`Plan/18-zero-copy/02`): their instructions
# are held to the bare arm, which copies nothing either. The gate checks the list against each `Executor`.
set(_zo_zero_copy w1 w2 w3 w5 w6 w7 w8 w9 w10 w11 w12)
# A workload the same as another in release, its `Executor` held to that one's (Plan/26-value-ranges/04: W11 is W1 with
# ranges, which cost nothing in release): its bytes equal, its instructions within the limit.
set(_zo_same w11:w1)
if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_BUILD_TYPE STREQUAL "" AND CMAKE_CXX_FLAGS STREQUAL ""
        AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING))
    message(STATUS "heinzel: the zero-overhead gates are not registered: they need clang on Linux, no build type and no flags")
else()
    set(_zo_workloads w1 w2 w3 w4 w5 w6 w7 w8 w9 w10 w11 w12)
    foreach(w IN LISTS _zo_workloads)
        foreach(arm hand bare bare_copy executor)
            if((w STREQUAL "w9" OR w STREQUAL "w12") AND arm STREQUAL "hand") # (W9 and W12 have no hand loop)
                continue()
            endif()
            string(TOUPPER ${arm} _arm)
            foreach(_t zo_${w}_${arm} zo_${w}_${arm}_count)
                add_executable(${_t} zero_overhead/arm.cpp)
                target_link_libraries(${_t} PRIVATE heinzel)
                target_include_directories(${_t} PRIVATE support zero_overhead)
                target_compile_definitions(${_t} PRIVATE WORKLOAD=${w} ARM=${_arm} NDEBUG)
                target_compile_options(${_t} PRIVATE -O2 -g0)
            endforeach()
            target_compile_definitions(zo_${w}_${arm}_count PRIVATE COUNT)
        endforeach()
        # ...and its `Executor` at -O1, at -Os and with asserts on, which must not hold its compile result either.
        foreach(_build o1 os checked)
            add_executable(zo_${w}_executor_${_build} zero_overhead/arm.cpp)
            target_link_libraries(zo_${w}_executor_${_build} PRIVATE heinzel)
            target_include_directories(zo_${w}_executor_${_build} PRIVATE support zero_overhead)
            target_compile_definitions(zo_${w}_executor_${_build} PRIVATE WORKLOAD=${w} ARM=EXECUTOR $<$<NOT:$<STREQUAL:${_build},checked>>:NDEBUG>)
            target_compile_options(zo_${w}_executor_${_build} PRIVATE
                                   $<$<STREQUAL:${_build},o1>:-O1> $<$<STREQUAL:${_build},os>:-Os> $<$<STREQUAL:${_build},checked>:-O2> -g0)
        endforeach()
    endforeach()
    # `llvm-size` beside the toolchain's `strip`.
    get_filename_component(_strip_dir "${CMAKE_STRIP}" DIRECTORY)
    get_filename_component(_strip_name "${CMAKE_STRIP}" NAME)
    string(REPLACE "strip" "size" _size_name "${_strip_name}")
    find_program(HEINZEL_LLVM_SIZE NAMES ${_size_name} llvm-size HINTS ${_strip_dir} REQUIRED)
    list(JOIN _zo_workloads " " _zo_list) # (a `;` in a test's command would split it)
    list(JOIN _zo_bytes " " _zo_base)
    list(JOIN _zo_zero_copy " " _zo_zero)
    list(JOIN _zo_same " " _zo_same_list)
    set(_zo_gate -DDIR=${CMAKE_CURRENT_BINARY_DIR} "-DWORKLOADS=${_zo_list}" -DSIZE=${HEINZEL_LLVM_SIZE} "-DBASE=${_zo_base}"
                 "-DZERO_COPY=${_zo_zero}" "-DSAME=${_zo_same_list}")
    add_test(NAME zero_overhead_binary
             COMMAND ${CMAKE_COMMAND} ${_zo_gate} -DKIND=binary -DLIMIT=77 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/zero_overhead.cmake)
    add_test(NAME zero_overhead_runtime
             COMMAND ${CMAKE_COMMAND} ${_zo_gate} -DKIND=runtime -DLIMIT=9 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/zero_overhead.cmake)
    set_tests_properties(zero_overhead_runtime PROPERTIES SKIP_REGULAR_EXPRESSION "zero_overhead_runtime: SKIPPED")
endif()

# What the sweep prints last says which twins it ran, and with a whole build configured OFF that is none.
if(HEINZEL_HARDWARE_TWINS)
    set(_twins_say "hardware twins: sqrt")
else()
    set(_twins_say "hardware twins: off")
endif()

# The same sweep with the twins turned off: `HEINZEL_HARDWARE_TWINS=0` must keep compiling, and with
# it every operation is its core, so the comparison is a core with itself.
add_executable(test_hardware_twins_off unit/hardware_twins_test.cpp)
target_link_libraries(test_hardware_twins_off PRIVATE heinzel)
target_compile_definitions(test_hardware_twins_off PRIVATE HEINZEL_HARDWARE_TWINS=0)
add_test(NAME hardware_twins_off COMMAND test_hardware_twins_off)
# The lane sweep too, and once for this machine's instruction set — where a lane IS a register.
target_compile_options(test_lane_ops PRIVATE -O2)
add_executable(test_lane_ops_native unit/lane_ops_test.cpp)
target_link_libraries(test_lane_ops_native PRIVATE heinzel)
target_compile_options(test_lane_ops_native PRIVATE -O2 -march=native)
add_test(NAME lane_ops_native COMMAND test_lane_ops_native)

# Optimised, all three: sixteen million comparisons are six seconds each otherwise.
target_compile_options(test_hardware_twins PRIVATE -O2)
target_compile_options(test_hardware_twins_off PRIVATE -O2)

# And once for this machine's own instruction set, which is what gives the fused multiply-add its twin.
add_executable(test_hardware_twins_native unit/hardware_twins_test.cpp)
target_link_libraries(test_hardware_twins_native PRIVATE heinzel)
target_compile_options(test_hardware_twins_native PRIVATE -O2 -march=native)
add_test(NAME hardware_twins_native COMMAND test_hardware_twins_native)
set_tests_properties(hardware_twins_native PROPERTIES PASS_REGULAR_EXPRESSION "${_twins_say}"
                                                      FAIL_REGULAR_EXPRESSION "disagreements")

# The float cores' multiply-adds are rounded as the machine rounds them — fused where it has the
# instruction, which the ordinary test build does not ask for. So the same bound is held once more for
# this machine's own instruction set: the numbers the benchmarks actually run on.
add_executable(test_float_cores_native unit/float_cores_test.cpp)
target_link_libraries(test_float_cores_native PRIVATE heinzel)
target_compile_options(test_float_cores_native PRIVATE -O2 -march=native)
add_test(NAME float_cores_native COMMAND test_float_cores_native)

# A pass expression replaces the exit code, so the failure line is named as well.
set_tests_properties(hardware_twins_off PROPERTIES PASS_REGULAR_EXPRESSION "hardware twins: off"
                                                   FAIL_REGULAR_EXPRESSION "disagreements")
set_tests_properties(hardware_twins PROPERTIES PASS_REGULAR_EXPRESSION "${_twins_say}"
                                               FAIL_REGULAR_EXPRESSION "disagreements")

# frontend_gradients uses the gradients() capture spelling, now the exported app-layer header
# (app/heinzel/gradients.hpp via the tests/support shim), so it links the application-layer interface.
target_link_libraries(test_frontend_gradients PRIVATE heinzel_app)
target_link_libraries(test_cos_tan_pow_exp2 PRIVATE heinzel_app)
target_link_libraries(test_nest_constant_views PRIVATE heinzel_app)
target_link_libraries(test_nest_agreement PRIVATE heinzel_app)
target_link_libraries(test_graph_facts PRIVATE heinzel_app) # (the surface corpus holds a gradient)

# The exported header is usable from the app layer WITHOUT any test include path — the whole point of
# shipping it (Q2 = option 1). `app_gradients_header_alone` compiles it with heinzel_app's include path
# and nothing else, so if the header ever grew a hidden dependence on test scaffolding, the build stops.
# `test_app_gradients_export` builds a gradient graph with it and checks the gradients on the walk, which
# is why it has `reference/`.
add_library(app_gradients_header_alone OBJECT app/gradients_header_alone.cpp)
target_link_libraries(app_gradients_header_alone PRIVATE heinzel_app)
add_executable(test_app_gradients_export app/gradients_export_test.cpp)
target_link_libraries(test_app_gradients_export PRIVATE heinzel_app)
target_include_directories(test_app_gradients_export PRIVATE reference)
add_test(NAME app_gradients_export COMMAND test_app_gradients_export)

# `print<g>()` (`app/heinzel/print.hpp`, Plan/28-dsl-proposals/04 C3) prints what `Executor<g>` runs: the compile it
# makes at run time equals the one made while compiling (its graph as pinned, the schedule, the program), on two
# machines, checked and not; a refused graph prints its refusal, a graph function that stops its error.
add_executable(test_app_print app/print_test.cpp)
target_link_libraries(test_app_print PRIVATE heinzel_app)
add_test(NAME app_print COMMAND test_app_print)

# ---- the conservation oracle's recorder ---------------------------------------------------------
# Not a test: it PRINTS the recorded hashes, which is what makes re-recording after a deliberate
# change to the graph type a single command rather than a hand-edit of hex. The check that compares
# against them is the `conservation` test group above.
add_executable(record_conservation_hashes ${CMAKE_CURRENT_SOURCE_DIR}/../tools/record_conservation_hashes.cpp)
target_link_libraries(record_conservation_hashes PRIVATE heinzel)
target_include_directories(record_conservation_hashes PRIVATE support conservation reference)

# ---- gates: properties that are checked rather than reviewed ------------------------------------
# Milestone M1's third condition is a NEGATIVE property — the analysis pass contains no branch about
# a particular operation — and a negative property is exactly the kind that erodes one convenient
# special case at a time unless something checks it. Every header of `sema/` is held to it: the shapes,
# the refusals and the checks are each keyed on a rule column, `AttrKind` or `ViewKind`, never on an
# operation code. Pointed at the directory, so a file split out or added cannot escape it.
add_test(NAME gate_no_per_op_branches
         COMMAND ${CMAKE_COMMAND}
                 -DPASS_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/sema
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_per_op_branches.cmake)

# The rewrite layer (`Plan/23-audio-scheduler/03`) rewrites sums and products, so it is where a rule asking "is this
# an add" would be easiest to write: what an operation is to a sum comes from the rule table's `Algebra` column.
add_test(NAME gate_rewrite_names_no_operation
         COMMAND ${CMAKE_COMMAND}
                 -DPASS_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/rewrite
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_per_op_branches.cmake)

# The proof of ranges (Plan/26-value-ranges/03) is a rule per operation, the easiest place to ask "is this a division":
# what an operation does to a range, and where its operands must lie, come from the rule table's range and domain columns.
add_test(NAME gate_ranges_name_no_operation
         COMMAND ${CMAKE_COMMAND}
                 -DPASS_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/ranges
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_per_op_branches.cmake)

# The schedule is where "no specialized kernels" (D-5) would erode: a scheduler that asks "is this a matmul".
# Whether a transform applies is asked of the rule table and of access maps, so this file may name no operation
# — GEMM's speed has to stay a consequence of generic loop transforms. (The loop tree, `loops/`, the planner, the
# lowering and the runner included, is held to the same by `gate_one_writing_per_operation`, which reads all of
# `include/`.)
add_test(NAME gate_schedule_machine_names_no_operation
         COMMAND ${CMAKE_COMMAND}
                 -DPASS_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/schedule/machine.hpp
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_per_op_branches.cmake)

# And the gate is pointed at a file that DOES name operations, to show it would object. A gate that
# has only ever been seen to pass is not evidence that it would ever fail — the inference test is a
# convenient offender because naming operations is exactly its job.
add_test(NAME gate_no_per_op_branches_would_object
         COMMAND ${CMAKE_COMMAND}
                 -DPASS_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/unit/inference_test.cpp
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_per_op_branches.cmake)
set_tests_properties(gate_no_per_op_branches_would_object PROPERTIES WILL_FAIL TRUE)
# The same over a directory, which reads each header: the fixtures hold one that writes arithmetic per operation.
# PASS_REGULAR_EXPRESSION, not WILL_FAIL (finding C-4): the expression names the offender and the operation, so the
# match can only have come from that header (CMake wraps the message near 76 columns, wherever the path ends it).
add_test(NAME gate_no_per_op_branches_would_object_in_a_directory
         COMMAND ${CMAKE_COMMAND}
                 -DPASS_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_per_op_branches.cmake)
set_tests_properties(gate_no_per_op_branches_would_object_in_a_directory PROPERTIES
                     PASS_REGULAR_EXPRESSION
                     "second_writing_of_arithmetic\\.hpp[^(]*\\(op::Add\\)")
# And an operation named by its string in the rule table, the spelling a rewrite that emits one would reach for.
add_test(NAME gate_no_per_op_branches_would_object_to_a_name
         COMMAND ${CMAKE_COMMAND}
                 -DPASS_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/op_by_name
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_per_op_branches.cmake)
set_tests_properties(gate_no_per_op_branches_would_object_to_a_name PROPERTIES
                     PASS_REGULAR_EXPRESSION
                     "emits_an_operation_by_name\\.hpp[^(]*\\(\"_fma\"\\)")

# Milestone M2.7: no extent is read as a plain integer. Pointed at BOTH the library and the test
# suite, because the condition says "anywhere outside the vocabulary header that defines the
# encoding" and a test that hand-decodes an extent is exactly as wrong as a header that does — it
# just fails later. `ir/vocab.hpp` is exempt because it IS the encoding.
add_test(NAME gate_extents_are_not_plain_integers
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../include
                 "-DEXEMPT=${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/ir/vocab.hpp"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/extents_are_not_plain_integers.cmake)

add_test(NAME gate_extents_are_not_plain_integers_in_tests
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
                 "-DEXEMPT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/extents_are_not_plain_integers.cmake)

# And pointed at a file written to offend it, so it is observed OBJECTING. Unlike the per-operation
# gate, this one has no honest offender in the tree to borrow, so the offender is a committed
# fixture that nothing includes — one file per pattern the gate names.
add_test(NAME gate_extents_are_not_plain_integers_would_object
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/extents_are_not_plain_integers.cmake)
set_tests_properties(gate_extents_are_not_plain_integers_would_object PROPERTIES WILL_FAIL TRUE)

# Milestone M2.2: no source under include/ branches on whether it is being constant-evaluated. Two
# bodies selected by which world a call is in cannot be compared against each other, and comparing
# the compile-time and run-time construction paths is this tree's central check.
get_filename_component(_hardware_twins "${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/runtime/hardware_twins.hpp" REALPATH)
add_test(NAME gate_no_constant_evaluation_branch
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../include
                 -DEXEMPT=${_hardware_twins}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_constant_evaluation_branch.cmake)

# The reference walk is held to the same: it is one side of every comparison between the two worlds.
add_test(NAME gate_no_constant_evaluation_branch_in_reference
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/reference
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_constant_evaluation_branch.cmake)

add_test(NAME gate_no_constant_evaluation_branch_would_object
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_constant_evaluation_branch.cmake)
set_tests_properties(gate_no_constant_evaluation_branch_would_object PROPERTIES WILL_FAIL TRUE)

# Milestone M3.1: an operation's arithmetic is written in exactly one place. Pointed at the whole of
# `include/`, with the one file that may write it named on the command line. Two headers are exempt
# for reasons that are not arithmetic: the rule table's lockstep assertion has to name every code,
# and the frontend names operations to RECORD what the user wrote, which is its job.
#
# This does NOT replace `gate_no_per_op_branches` above. That one says the analysis pass and the
# planner may name NO operation at all, which is a stronger claim about two particular files.
# `autodiff/` is exempt for the reason `frontend/graph_builder.hpp` is: it names operations to
# CONSTRUCT nodes, not to write any operation's arithmetic. `gradients()` (M7) classifies each
# operation's adjoint story and appends adjoint nodes in the existing vocabulary — every `op::` it
# spells is a node it builds or a class it decides, and each such node still calls the one writing in
# `compute.hpp`. The gate would otherwise read the classification table and the adjoint rules as a
# second writing, which they are not — the same false positive the builder's exemption avoids.
# `frontend/ops.hpp` is exempt for the identical reason as `frontend/graph_builder.hpp`: it names
# `op::Reduce`/`op::Transpose`/`op::MatMul` to CONSTRUCT those nodes from what the user spelled, not to write
# any operation's arithmetic — each such node still runs through the one writing in `compute.hpp`.
# `notation/einsum.hpp` and `notation/rearrange.hpp` need no exemption: they name no `op::` at all, calling
# the frontend's builders and operators instead.
add_test(NAME gate_one_writing_per_operation
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../include
                 -DWRITING=${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/runtime/compute.hpp
                 "-DEXEMPT=${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/ir/op_rules.hpp;${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/frontend/graph_builder.hpp;${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/frontend/ops.hpp;${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/autodiff"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/one_writing_per_operation.cmake)

# And pointed at a file written to offend it, so it is observed OBJECTING: a "fast path" covering the
# two operations that happened to matter that day, which is how a second writing really appears.
add_test(NAME gate_one_writing_per_operation_would_object
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures
                 -DWRITING=${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/runtime/compute.hpp
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/one_writing_per_operation.cmake)
set_tests_properties(gate_one_writing_per_operation_would_object PROPERTIES WILL_FAIL TRUE)

# ---- one spelling everywhere, test graphs included (milestone M8.4, standing rule S-2 / A2-2) -----
# The mechanical half of M8.4: there is one way to add a node or value to a graph by hand, and it is
# `support/hand_built_graph.hpp`. A user-facing program (tutorial, example, parity `_here`) does not
# add nodes by hand — it writes the language, which lowers to nodes. So the raw idiom
# `g.nodes.push_back` / `g.values.push_back` lives in exactly one file across the whole test tree.
# The alias-level half of "one spelling" (`clamp` vs `min(max(…))`, operator vs named form) is not
# mechanical and is recorded as reviewed with its date in docs/decisions.md (Decision 28).
#
# EXEMPT is the recorded non-mechanical judgement: three files where the raw IR is the SUBJECT under
# test rather than a graph spelled a second way — `unit/graph_value_test.cpp` (the graph-as-a-value
# property is asserted over the raw aggregates), `unit/inference_test.cpp` (injects a `Const` value
# with a payload `add_value` does not set), `compile_time/canonical_renumber.hpp` (a measured,
# not-adopted renumber candidate that copies a graph node by node; guard-only). `gates/fixtures` is
# exempt because the would-object file below is a deliberate offender.
# `support/hand_built_tree.hpp` builds loop trees by hand (`Program::nodes.push_back`): a loop tree is not a
# graph, and that header is its one spelling (Plan/archive/13-loop-tree/04 §3).
add_test(NAME gate_one_spelling
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
                 -DWRITING=${CMAKE_CURRENT_SOURCE_DIR}/support/hand_built_graph.hpp
                 "-DEXEMPT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures;${CMAKE_CURRENT_SOURCE_DIR}/unit/graph_value_test.cpp;${CMAKE_CURRENT_SOURCE_DIR}/unit/inference_test.cpp;${CMAKE_CURRENT_SOURCE_DIR}/compile_time/canonical_renumber.hpp;${CMAKE_CURRENT_SOURCE_DIR}/support/hand_built_tree.hpp"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/one_spelling.cmake)

# And pointed at a file written to offend it, so it is observed OBJECTING: a program that adds to a
# graph inline instead of writing the language or calling the shared hand-built helper.
add_test(NAME gate_one_spelling_would_object
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/one_spelling
                 -DWRITING=${CMAKE_CURRENT_SOURCE_DIR}/support/hand_built_graph.hpp
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/one_spelling.cmake)
set_tests_properties(gate_one_spelling_would_object PROPERTIES WILL_FAIL TRUE)

# The spelling a user reads (Plan/archive/14-dsl L9, DS-19): the root README, the examples, the tutorials' and parity
# ports' `_here` programs and the benchmarks use none of the spellings L9 retired, nor the internals (`detail::`).
# `examples/esp32-bench` is no source of this tree (an ignored local build of `main`'s ESP32 bench).
add_test(NAME gate_public_spelling
         COMMAND ${CMAKE_COMMAND}
                 "-DFILES=${CMAKE_CURRENT_SOURCE_DIR}/../README.md;${CMAKE_CURRENT_SOURCE_DIR}/tutorial/*_here.cpp;${CMAKE_CURRENT_SOURCE_DIR}/parity/*_here.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../benchmarks/*.cpp;${CMAKE_CURRENT_SOURCE_DIR}/../benchmarks/*.hpp"
                 -DTREES=${CMAKE_CURRENT_SOURCE_DIR}/../examples
                 -DEXEMPT=${CMAKE_CURRENT_SOURCE_DIR}/../examples/esp32-bench
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/public_spelling.cmake)

# And pointed at a file written to offend it: every retired name in code — blanks around a `(` (a newline too), `detail`
# reached by a `using` and an alias, a `/*` in a string, a raw string or a prefixed one (`u8R"`), a `'"'` and an `L'"'`,
# escaped quotes, a string spliced across lines, a digit separator, a raw string's delimiter, an `R"` that opens none,
# a URL in a block comment, `loops::print` — none counted in a comment (a `//` one continued by a backslash too), inside
# a longer word (`kfr::detail::`, `my_loops::print` among them) or as another library's member (`dsp->instantiate(…)`):
# 42 offences, three of them pinned by line. Its root is spelled with a `..`, as the real gate's is, and an exempted
# directory beside it holds one more offence, which counts only if the exemption fails.
add_test(NAME gate_public_spelling_would_object
         COMMAND ${CMAKE_COMMAND}
                 -DTREES=${CMAKE_CURRENT_SOURCE_DIR}/gates/../gates/fixtures/public_spelling
                 -DEXEMPT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/public_spelling/exempt
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/public_spelling.cmake)
set_tests_properties(gate_public_spelling_would_object PROPERTIES
                     PASS_REGULAR_EXPRESSION "old_spellings_here.cpp:5: BuildContext.*:33: delayed\\(.*:42: delayed\\(.*\\(42 in all\\)")

# ---- no preprocessor branch on an instruction set (milestone M3.5, amendment 05 A5-2) ------------
# What the hardware can do is data — a `Machine` the planner reads — and not a preprocessor branch: two
# sides of an `#ifdef __AVX2__` cannot be compared against each other in one build, because only one of
# them exists in any given build.
#
# Pointed at `tests/` as well as `include/`. The condition names `include/`, but a fast path that
# appeared in a test would be the same mistake arriving through a door the gate was not watching, and
# the offenders' directory is exempt for the usual reason.
# One file asks, for one question: whether the target has the instruction (a fused multiply-add, a
# rounding) that `runtime/hardware_twins.hpp` would use as a core's twin (`runtime/instruction_set.hpp`);
# the twins are the one file that reads the two macros it derives, which are refused everywhere else like
# the ones it reads.
get_filename_component(_instruction_set "${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/runtime/instruction_set.hpp" REALPATH)
add_test(NAME gate_no_instruction_set_macros
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../include
                 "-DEXEMPT=${_instruction_set};${_hardware_twins}"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_instruction_set_macros.cmake)

add_test(NAME gate_no_instruction_set_macros_in_tests
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
                 "-DEXEMPT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_instruction_set_macros.cmake)

# Shown OBJECTING, and to both spellings: `#ifdef __AVX2__`, which carries trailing underscores, and
# `#if defined(__ARM_NEON)`, which does not. The first draft of the gate's pattern assumed they were
# uniform and silently missed the second, so the expression below names it specifically.
add_test(NAME gate_no_instruction_set_macros_would_object
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/instruction_set
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_instruction_set_macros.cmake)
set_tests_properties(gate_no_instruction_set_macros_would_object PROPERTIES
                     PASS_REGULAR_EXPRESSION "a_fast_path_for_avx2\\.hpp:23: a preprocessor conditional on .__ARM_NEON.")

# ---- autodiff is a leaf: the core does not know it exists (milestone M7.4 / amendment 04 A4-2) ----
# The `gradients()` feature under `include/heinzel/autodiff/` includes the core; no core header may
# include one of ITS headers. That edge's absence is what makes "delete the feature and every core
# header still compiles" true by construction. The scan is `include/` with the autodiff directory
# exempt — an autodiff header including a sibling or a core header is the leaf-on-trunk direction, not
# an offence. Tests may name autodiff freely, so this gate is pointed at `include/` alone.
add_test(NAME gate_autodiff_is_a_leaf
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../include
                 "-DEXEMPT=${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/autodiff"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/autodiff_is_a_leaf.cmake)

# Shown OBJECTING to a core header that reaches into autodiff. PASS_REGULAR_EXPRESSION, not WILL_FAIL,
# for finding C-4's reason: WILL_FAIL cannot tell "the gate objected" from "the gate crashed". The
# expression pins the offender's file AND line, so the match can only have come from that one include.
add_test(NAME gate_autodiff_is_a_leaf_would_object
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/autodiff_leaf
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/autodiff_is_a_leaf.cmake)
set_tests_properties(gate_autodiff_is_a_leaf_would_object PROPERTIES
                     PASS_REGULAR_EXPRESSION
                     "a_core_header_that_reaches_into_autodiff\\.hpp:7: a core header includes an autodiff header")

# ---- the spine's include order ----------------------------------------------------------------------
# ir -> sema -> schedule -> runtime -> loops, and the authoring layers reach the spine through `ir`
# only. The allowed edges are the table in the gate script. It exists because a cycle it refuses
# existed: `schedule/schedule.hpp` once included the generator's header for `Machine`.
add_test(NAME spine_include_order
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/spine_include_order.cmake)

# Shown OBJECTING to each fixture: an IR header reading analysis, a schedule header reading the
# generator (after a comment holding `;` and `[`, which must not hide it), and a relative include.
add_test(NAME spine_include_order_would_object
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/spine_include_order
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/spine_include_order.cmake)
set_tests_properties(spine_include_order_would_object PROPERTIES
                     PASS_REGULAR_EXPRESSION
                     "an_ir_header_that_reads_analysis\\.hpp: ir/ has #include \"heinzel/sema.*a_schedule_header_that_reads_the_generator\\.hpp: schedule/ has #include \"heinzel/codegen.*a_relative_path\\.hpp: schedule/ has #include \"\\.\\./codegen/program\\.hpp.*a_rewrite_header_that_reads_the_cores\\.hpp: rewrite/ has #include \"heinzel/runtime/hardware_twins.*a_rewrite_header_that_reads_the_planner\\.hpp: rewrite/ has #include \"heinzel/loops/planner")

# ---- M8.1 / O12: the core does not include the frontend --------------------------------------------
# `02-Target-Architecture.md` §2.4 adds `core_no_frontend` to the old tree's O12 family (it was RED
# there). The frontend builds ON the core — `include/heinzel/frontend/` includes `ir/`, `sema/` — so no
# core header may include one of ITS headers, the same leaf-edge property `gate_autodiff_is_a_leaf`
# checks for autodiff. The general form of that gate is `no_include_into.cmake`, parameterised by the
# protected prefix; the autodiff gate is its specific ancestor and may be folded into it in a later
# structure pass (kept separate now so M7.4's just-closed gate is not disturbed — recorded in
# docs/decisions.md §M8). The frontend subtree is exempt: a frontend header including a sibling is the
# leaf-on-trunk direction. The `notation/` subtree is exempt for the same reason — it is a LAYER above the
# frontend (S2), not the core; it includes the frontend to build on it, which is the allowed direction,
# and no core header includes notation (that edge is the `notation_no_cost_model` gate's neighbour concern).
# The `dsp/` subtree is exempt on the SAME footing (M8.3, oscillator by-need): an application layer above
# the frontend — `dsp/oscillator.hpp` composes the recurrence frontend and the frac/sinpi spellings into an
# oscillator, exactly as the old tree's `dsp/oscillator.hpp` includes `frontend/builder.hpp`. That is the
# allowed layer-on-trunk direction and no core header includes dsp; the protected boundary (core
# ir/sema/runtime/schedule free of the frontend) is untouched, those subtrees stay scanned.
add_test(NAME core_no_frontend
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../include
                 -DFORBIDDEN=heinzel/frontend/
                 "-DLABEL=core_no_frontend gate (M8.1 / O12)"
                 "-DEXEMPT=${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/frontend;${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/notation;${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/dsp"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_include_into.cmake)

# Shown OBJECTING to a core header that reaches into the frontend. PASS_REGULAR_EXPRESSION, not
# WILL_FAIL (finding C-4): the expression pins the offender's file AND line, so the match can only have
# come from that one include.
add_test(NAME core_no_frontend_would_object
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/core_no_frontend
                 -DFORBIDDEN=heinzel/frontend/
                 "-DLABEL=core_no_frontend gate (M8.1 / O12)"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_include_into.cmake)
set_tests_properties(core_no_frontend_would_object PROPERTIES
                     PASS_REGULAR_EXPRESSION
                     "a_core_header_that_reaches_into_frontend\\.hpp:9: includes heinzel/frontend/")

# ---- MF.6 / O12: the core does not include the audio layer ------------------------------------------
# The old tree kept `heinzel/audio/` a strictly-separated leaf (`cmake/check-core-no-audio.cmake`): the
# freestanding/embedded core references no I/O facility, so nothing outside `heinzel/audio/` may include
# one of ITS headers. The audio layer (WAV read/write, MF.6) is a leaf beside the core: it may build ON
# the core — the allowed layer-on-trunk direction — and the forbidden edge is a core header naming
# audio. Same leaf-edge check as core_no_frontend: scan
# `include/`, forbid `heinzel/audio/`, exempt the audio subtree. The edge's absence is what makes "delete
# the audio layer and every core header still compiles" true by construction — decisions.md §M8
# anticipated this gate joining the O12 family once an M8.3/MF example ported the audio subset.
add_test(NAME core_no_audio
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../include
                 -DFORBIDDEN=heinzel/audio/
                 "-DLABEL=core_no_audio gate (MF.6 / O12)"
                 "-DEXEMPT=${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/audio"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_include_into.cmake)

# Shown OBJECTING to a core header that reaches into the audio layer. PASS_REGULAR_EXPRESSION, not
# WILL_FAIL (finding C-4): the expression pins the offender's file AND line, so the match can only have
# come from that one include and not from the gate crashing for another reason.
add_test(NAME core_no_audio_would_object
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/core_no_audio
                 -DFORBIDDEN=heinzel/audio/
                 "-DLABEL=core_no_audio gate (MF.6 / O12)"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_include_into.cmake)
set_tests_properties(core_no_audio_would_object PROPERTIES
                     PASS_REGULAR_EXPRESSION
                     "a_core_header_that_reaches_into_audio\\.hpp:10: includes heinzel/audio/")

# ---- M8.2 / O12: the notation consults no cost model while capturing -------------------------------
# `02-MILESTONES.md` §M8.2 and `01-DERIVATION.md` §1.3: the einsum/einops notation is a capture layer,
# and a capture layer must not reach into the cost model — in the old tree `einsum.hpp` included
# `schedule/cost.hpp` for `cost::contraction_path`, the GEMM-reorder that consulted a cost model at build
# time, which this rewrite must not reproduce. The cost model here is the machine model, `heinzel/schedule/`
# (what the planner reads). This runs the same include-leaf gate the other way round:
# ROOT is the notation subtree, and the forbidden prefix is the cost model, so a notation header reaching
# into `heinzel/schedule/` is the offence. The property is structural — the lowering makes no ordering,
# pairing or layout decision, so it never needs a cost model, and this keeps it that way.
add_test(NAME notation_no_cost_model
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/notation
                 -DFORBIDDEN=heinzel/schedule/
                 "-DLABEL=notation_no_cost_model gate (M8.2 / O12)"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_include_into.cmake)

# Shown OBJECTING to a notation header that reaches into the cost model. PASS_REGULAR_EXPRESSION, not
# WILL_FAIL (finding C-4): the expression pins the offender's file AND line, so the match can only have
# come from that one include and not from the gate crashing for another reason.
add_test(NAME notation_no_cost_model_would_object
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/notation_no_cost_model
                 -DFORBIDDEN=heinzel/schedule/
                 "-DLABEL=notation_no_cost_model gate (M8.2 / O12)"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_include_into.cmake)
set_tests_properties(notation_no_cost_model_would_object PROPERTIES
                     PASS_REGULAR_EXPRESSION
                     "a_notation_header_that_consults_the_cost_model\\.hpp:9: includes heinzel/schedule/")

# ---- the reference walk does not run what it checks --------------------------------------------------
# `tests/reference/` is what the loop tree is checked against, so none of its headers may include the loop
# tree or the machine model the planner reads: a walk that ran either would check the loop tree against
# itself. Direct edges are enough. The reference includes only `ir/`, `runtime/` and `support/`, whose
# closure the spine's order keeps out of `loops/` and `schedule/`. Each gate is shown OBJECTING to a fixture
# that includes both, its expression pinning the offending line.
foreach(_case "loops|10" "schedule|11")
    string(REPLACE "|" ";" _parts "${_case}")
    list(GET _parts 0 _layer)
    list(GET _parts 1 _line)
    add_test(NAME reference_no_${_layer}
             COMMAND ${CMAKE_COMMAND}
                     -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/reference
                     -DFORBIDDEN=heinzel/${_layer}/
                     -DLABEL=reference_no_${_layer}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_include_into.cmake)
    add_test(NAME reference_no_${_layer}_would_object
             COMMAND ${CMAKE_COMMAND}
                     -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/reference_independent
                     -DFORBIDDEN=heinzel/${_layer}/
                     -DLABEL=reference_no_${_layer}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/no_include_into.cmake)
    set_tests_properties(reference_no_${_layer}_would_object PROPERTIES
                         PASS_REGULAR_EXPRESSION
                         "a_reference_header_that_runs_what_it_checks\\.hpp:${_line}: includes heinzel/${_layer}/")
endforeach()

# ---- the loop tree's boundary (Plan/archive/13-loop-tree/01 §2, §5) ------------------------------------------
# `loops/` must not silently depend on the rest of the tree: the transitive include closure of every file
# under it stays inside `loops/` and the allowlist of `01` §2 — the shared core, what an operation computes
# (`runtime/compute.hpp` and exactly its closure), `Lane`, and what the hardware can do (`schedule/machine.hpp`, which
# the planner reads from M3 on; it includes nothing). The direct-edge gates see one include at a time;
# this walks the closure, so an allowed header that reaches a forbidden one is caught with its chain.
# Adding to ALLOW is a reviewed change to `01` §2 with a line of reason, never done to get a build green.
set(_loops_allow
    heinzel/support/ heinzel/ir/
    heinzel/runtime/compute.hpp heinzel/runtime/exact_arithmetic.hpp heinzel/runtime/exponential_cores.hpp
    heinzel/runtime/float_bits.hpp heinzel/runtime/float_cores.hpp heinzel/runtime/fold_arithmetic.hpp
    heinzel/runtime/fused_multiply_add.hpp heinzel/runtime/hardware_twins.hpp
    heinzel/runtime/instruction_set.hpp # (the twins' question of the instruction set)
    heinzel/runtime/inverse_circular_cores.hpp heinzel/runtime/math_cores.hpp heinzel/runtime/periodic_cores.hpp
    heinzel/runtime/power_cores.hpp heinzel/runtime/root_cores.hpp heinzel/runtime/rounding_cores.hpp
    heinzel/runtime/series_length.hpp heinzel/runtime/table_gather_element.hpp heinzel/runtime/value_type.hpp
    heinzel/runtime/lane_value.hpp heinzel/schedule/machine.hpp heinzel/schedule/cost.hpp)
add_test(NAME loops_closure
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/loops
                 -DINCLUDE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/../include
                 -DLABEL=loops_closure
                 "-DALLOW=${_loops_allow}"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/closure_stays_inside.cmake)

# Shown OBJECTING to a `loops/` header that reaches a forbidden header through an allowed one — the case the
# direct-edge gates miss. The expression pins the whole chain.
add_test(NAME loops_closure_would_object
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/loops_closure/heinzel/loops
                 -DINCLUDE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/loops_closure
                 -DLABEL=loops_closure
                 -DALLOW=heinzel/runtime/allowed.hpp
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/closure_stays_inside.cmake)
set_tests_properties(loops_closure_would_object PROPERTIES
                     PASS_REGULAR_EXPRESSION
                     "heinzel/loops/offender\\.hpp -> heinzel/runtime/allowed\\.hpp -> heinzel/codegen/forbidden\\.hpp")

# `loops/` compiles by itself (Plan/archive/13-loop-tree/01 §5.6): every `loops/` header first and alone in its own
# translation unit (`gates/loops_alone/`, globbed, so a new header's TU compiles as soon as it exists), and the
# loop tree's own tests, whose closure — through the test targets' include folders — stays in `loops/`, its
# allowlist, the DSL and the graph builders. A name `loops/` uses but forgot to declare then fails to compile instead
# of being found through whatever a test included first.
file(GLOB _loops_alone_tus CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gates/loops_alone/*.cpp)
add_library(loops_alone OBJECT ${_loops_alone_tus})
target_link_libraries(loops_alone PRIVATE heinzel)
set(LOOPS_ALONE_FILES tests/unit/loops_ir_test.cpp tests/unit/loops_walks_test.cpp tests/unit/loops_runner_test.cpp tests/unit/loops_lower_test.cpp
    tests/unit/loops_primitives_test.cpp tests/unit/loops_plan_test.cpp)
list(TRANSFORM _loops_allow PREPEND "include/" OUTPUT_VARIABLE _loops_alone_allow)
list(APPEND _loops_alone_allow include/heinzel/loops/ include/heinzel/frontend/ include/heinzel/notation/
     include/heinzel/sema/ include/heinzel/units/ include/heinzel/dsp/
     tests/support/hand_built_graph.hpp tests/support/hand_built_tree.hpp tests/support/small_trees.hpp tests/support/corpus_graphs.hpp
     tests/support/loops_schedules.hpp tests/support/well_formed.hpp tests/support/well_formed_walks.hpp
     tests/support/well_formed_closes.hpp # (well_formed's rules for a state stored after its loop, split out for the line gate)
     tests/support/well_formed_kept.hpp # (and for a kept stage, docs/knob-math.md)
     tests/support/well_formed_skew.hpp # (and for a skewed stage, docs/sections-in-lanes.md)
     tests/support/well_formed_starts.hpp # (and for a run's first sample apart, Plan/33 S3)
     tests/support/well_formed_restarts.hpp tests/support/fold_in_feedback.hpp) # (a fold in a feedback stage's sample: its rules, its trees by hand)
add_test(NAME loops_alone
         COMMAND ${CMAKE_COMMAND}
                 -DBASE=${CMAKE_CURRENT_SOURCE_DIR}/..
                 "-DFILES=${LOOPS_ALONE_FILES}"
                 "-DINCLUDE_DIRS=include;tests/support;tests/conservation;tests/reference"
                 -DHEADERS=include/heinzel/loops
                 -DTUS=tests/gates/loops_alone
                 -DLABEL=loops_alone
                 "-DALLOW=${_loops_alone_allow}"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/closure_stays_inside.cmake)

# Shown OBJECTING, three times in one fixture laid out like the tree: a `loops/` header with no translation unit, a
# test that reaches a support header it may not through `<…>`, and a test whose support header (allowed) reaches
# outside the allowlist.
add_test(NAME loops_alone_would_object
         COMMAND ${CMAKE_COMMAND}
                 -DBASE=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/loops_alone
                 "-DFILES=tests/unit/a_loops_test.cpp;tests/unit/b_loops_test.cpp"
                 "-DINCLUDE_DIRS=include;tests/support"
                 -DHEADERS=include/heinzel/loops
                 -DTUS=tests/gates/loops_alone
                 -DLABEL=loops_alone
                 "-DALLOW=include/heinzel/loops/;tests/support/offender.hpp"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/closure_stays_inside.cmake)
set_tests_properties(loops_alone_would_object PROPERTIES
                     PASS_REGULAR_EXPRESSION
                     "include/heinzel/loops/lonely\\.hpp: no translation unit.*tests/unit/b_loops_test\\.cpp -> tests/support/hidden\\.hpp.*tests/unit/a_loops_test\\.cpp -> tests/support/offender\\.hpp -> include/heinzel/codegen/forbidden\\.hpp")

# ---- structure: how long a file and a function may be (operator amendment 06) --------------------
# "It is very important that the code is well structured and we avoid the humongous multi thousand
# line long headers and hundreds of lines long functions of the previous implementation" — the
# operator, 2026-08-25, mid-M3. An instruction to write shorter functions is exactly the kind of
# thing that decays, so it is a gate: 600 lines a file, 60 lines a function, measured over every
# `.hpp` and `.cpp` in the tree.
#
# Three roots. `include/` and `tests/` are the two the amendment names; `tools/` is added because it
# is the third place C++ is written here and leaving it out would be a hole the amendment plainly
# does not intend. That is the stronger reading, and it contains the weaker one.
#
# The offenders' directory is exempt from the `tests/` run for the reason S1 recorded when it moved
# the refusal fixtures: a deliberate offender sitting in the scanned tree makes every pass report it
# instead of the file it is pointed at.
# Declared once. Five tests pass these, and two committed fixtures are sized against them — a limit
# spelled out in five places is one that drifts in four of them.
set(HEINZEL_MAX_FILE_LINES 600)
set(HEINZEL_MAX_FUNCTION_LINES 60)

add_test(NAME gate_structure_limits
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../include
                 -DMAX_FILE_LINES=${HEINZEL_MAX_FILE_LINES} -DMAX_FUNCTION_LINES=${HEINZEL_MAX_FUNCTION_LINES}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/structure_limits.cmake)

add_test(NAME gate_structure_limits_in_tests
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
                 -DMAX_FILE_LINES=${HEINZEL_MAX_FILE_LINES} -DMAX_FUNCTION_LINES=${HEINZEL_MAX_FUNCTION_LINES}
                 "-DEXEMPT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/structure_limits.cmake)

add_test(NAME gate_structure_limits_in_tools
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../tools
                 -DMAX_FILE_LINES=${HEINZEL_MAX_FILE_LINES} -DMAX_FUNCTION_LINES=${HEINZEL_MAX_FUNCTION_LINES}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/structure_limits.cmake)

# The application-layer export tree (app/, Q2 = option 1) is a source root like include/ and tools/,
# so amendment 06's structure gate scans it too — a new tree outside the gate is exactly the blind
# spot the gate exists to close.
add_test(NAME gate_structure_limits_in_app
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../app
                 -DMAX_FILE_LINES=${HEINZEL_MAX_FILE_LINES} -DMAX_FUNCTION_LINES=${HEINZEL_MAX_FUNCTION_LINES}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/structure_limits.cmake)

# The examples (examples/) are a source root as well, and scanned whether or not they are built.
# `examples/esp32-bench` is no source of this tree: an ignored local build of `main`'s ESP32 bench.
add_test(NAME gate_structure_limits_in_examples
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../examples
                 -DEXEMPT=${CMAKE_CURRENT_SOURCE_DIR}/../examples/esp32-bench
                 -DMAX_FILE_LINES=${HEINZEL_MAX_FILE_LINES} -DMAX_FUNCTION_LINES=${HEINZEL_MAX_FUNCTION_LINES}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/structure_limits.cmake)

# And shown OBJECTING, once per limit, each fixture in its own directory so that each test points the
# gate at exactly one offender and the message it matches can only have come from that one.
#
# PASS_REGULAR_EXPRESSION rather than WILL_FAIL, which is S1 audit finding C-4 applied on purpose:
# WILL_FAIL passes whenever the run fails, including when the gate crashed or when CMake could not
# find the script, so it cannot tell "the gate objected" from "the gate broke".
#
# Each expression pins the MEASURED SIZE, and the function one also pins the LINE NUMBER. That is
# not decoration. While this gate was being written its first implementation split the file into a
# CMake list, silently lost twelve lines of `sema/analyze.hpp`, and reported the right length at the
# wrong line — and the right length was a coincidence of the lost lines falling before the function.
# An expression matching only "is 73 lines" would have passed that bug.
add_test(NAME gate_structure_limits_would_object_to_a_long_file
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/structure/file_limit
                 -DMAX_FILE_LINES=${HEINZEL_MAX_FILE_LINES} -DMAX_FUNCTION_LINES=${HEINZEL_MAX_FUNCTION_LINES}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/structure_limits.cmake)
set_tests_properties(gate_structure_limits_would_object_to_a_long_file PROPERTIES
                     PASS_REGULAR_EXPRESSION
                     "one_addition_at_a_time\\.hpp: the file is 641 lines, and the limit is 600")

add_test(NAME gate_structure_limits_would_object_to_a_long_function
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/structure/function_limit
                 -DMAX_FILE_LINES=${HEINZEL_MAX_FILE_LINES} -DMAX_FUNCTION_LINES=${HEINZEL_MAX_FUNCTION_LINES}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/structure_limits.cmake)
set_tests_properties(gate_structure_limits_would_object_to_a_long_function PROPERTIES
                     PASS_REGULAR_EXPRESSION
                     "a_case_at_a_time\\.hpp:24: .*a_dispatcher_that_grew_a_case_at_a_time.*is 73 lines, and the limit is 60")

# ---- one declaration per line (Plan 28 step 0.1, Gerald 2026-10-16) -------------------------------
# No statement declares two names (`T a, b;`, `T a = 1, b{2};`, wrapped or not) and no line holds two declarations of
# one scope (`Input x{…}; Output y{…};`, a one-line class or body with two); a `for` header, a parameter list, a
# capture, template arguments and a structured binding are no second name. How the gate reads a file, and what it
# cannot see: its header. Every source root; its own offenders, `public_spelling`'s (whose lines another test pins)
# and `examples/esp32-bench` (an ignored local build of `main`'s ESP32 bench) are exempt.
add_test(NAME gate_one_declaration_per_line
         COMMAND ${CMAKE_COMMAND}
                 "-DTREES=${CMAKE_CURRENT_SOURCE_DIR}/../include;${CMAKE_CURRENT_SOURCE_DIR}/../app;${CMAKE_CURRENT_SOURCE_DIR};${CMAKE_CURRENT_SOURCE_DIR}/../benchmarks;${CMAKE_CURRENT_SOURCE_DIR}/../tools;${CMAKE_CURRENT_SOURCE_DIR}/../examples"
                 "-DEXEMPT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/one_declaration_per_line;${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/public_spelling;${CMAKE_CURRENT_SOURCE_DIR}/../examples/esp32-bench"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/one_declaration_per_line.cmake)

# Shown OBJECTING to every form, each line once: the expression pins the count and the lines whose reading is subtle
# (a declaration wrapped at its comma, a lambda's body inside a call, the line after an all-caps macro's call, a `/*`
# in a string), so a gate that broke or stopped reading early cannot pass it. And shown ALLOWING every form that is
# one declaration or none, the tree's trickiest among them.
add_test(NAME gate_one_declaration_per_line_would_object
         COMMAND ${CMAKE_COMMAND}
                 -DTREES=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/one_declaration_per_line/offends.cpp
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/one_declaration_per_line.cmake)
set_tests_properties(gate_one_declaration_per_line_would_object PROPERTIES
                     PASS_REGULAR_EXPRESSION
                     "offends\\.cpp:7: int a, b.*offends\\.cpp:33: .*offends\\.cpp:39: .*offends\\.cpp:57: .*offends\\.cpp:59: .*\\(41 in all\\)")
add_test(NAME gate_one_declaration_per_line_allows
         COMMAND ${CMAKE_COMMAND}
                 -DTREES=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/one_declaration_per_line/allowed.cpp
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/one_declaration_per_line.cmake)

# ---- the build refuses a half-done operation (milestone M3.4) -----------------------------------
# The two ways of getting an operation half-done are opposites: a table row with no meaning written,
# and a meaning written for an operation with no row. Both must fail to BUILD, which is a thing no
# ordinary test can show — `unit/op_rules_test.cpp` says so directly: a test that fails to compile is
# not a test.
#
# So each fixture is a translation unit that must not compile, held out of the default build, and the
# test is an attempt to build it that is required to fail. The message naming what is missing is the
# static_assert's own, which is why each fixture carries the same words the real check does.
#
# They live in `gates/refusals/` rather than in `gates/fixtures/` because they are not offenders
# against any source-scanning gate — they configure a table, which means naming an operation — and
# leaving them beside the offenders would make the one-writing gate's would-object report name them
# instead of the file it is actually pointed at.
#
# WILL_FAIL would be the obvious way to say "this must not build", and it is the wrong one: it passes
# whenever the build fails, including when it fails because somebody broke an unrelated header. So
# each test requires the build to fail FOR ITS OWN REASON, by matching the static_assert's message.
# When a PASS_REGULAR_EXPRESSION is set, ctest judges on the output and ignores the return code.
#
# RESOURCE_LOCK because these two are the only tests that run a build, and two ninja invocations in
# one build directory at the same time is not something ninja promises to survive. Without it,
# `ctest -j` would be a flaky-test generator.
set(HEINZEL_REFUSAL_row_with_no_meaning "has none written in runtime/compute.hpp")
set(HEINZEL_REFUSAL_meaning_with_no_row "has no row in kOpTable")
# The third, added at M4. Unlike the other two it closes a hole rather than demonstrating a check
# that always worked: `CostClass` had no sentinel, so an unstated cost read as `Cheap`.
set(HEINZEL_REFUSAL_row_with_no_cost "never states its cost is half-filled")
# The fourth (Plan/26-value-ranges/03): the proof of ranges reads a range rule and a domain for every operation.
set(HEINZEL_REFUSAL_row_with_no_range "never states its range rule and domain is half-filled")

foreach(refusal row_with_no_meaning meaning_with_no_row row_with_no_cost row_with_no_range)
    add_executable(incomplete_${refusal} EXCLUDE_FROM_ALL gates/refusals/${refusal}.cpp)
    target_link_libraries(incomplete_${refusal} PRIVATE heinzel)
    target_include_directories(incomplete_${refusal} PRIVATE support reference)

    add_test(NAME build_refuses_${refusal}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target incomplete_${refusal})
    set_tests_properties(build_refuses_${refusal} PROPERTIES
                         PASS_REGULAR_EXPRESSION "${HEINZEL_REFUSAL_${refusal}}"
                         RESOURCE_LOCK heinzel_build_tree)
endforeach()

# ---- index notation's spellings that must not compile (Plan/archive/14-dsl/02 §2) --------------------------
# One source, one case per target (`-DCASE=<k>`), each required to fail FOR ITS OWN REASON — the same
# mechanics and the same RESOURCE_LOCK as above. The messages are clang's, the same at C++26 and C++23;
# only the case's own lines differ between the targets, so the message names the case's error.
set(HEINZEL_INDEX_NOTATION_REFUSALS
    "lag_of_a_fraction|overload resolution selected deleted operator '-'"
    "lag_into_the_future|invalid operands to binary expression \\('const Index' and 'int'\\)"
    "now_of_an_expr_assigned|no viable overloaded '='"
    "lag_assigned|no viable overloaded '='"
    "read_only_variable_assigned|no viable overloaded '='"
    "variable_copied|call to deleted constructor of 'Variable'"
    "signal_of_no_length|static assertion failed.*: a signal has a length"
    "output_copied|call to deleted constructor of 'Output'")
set(_case 0)
foreach(_refusal IN LISTS HEINZEL_INDEX_NOTATION_REFUSALS)
    math(EXPR _case "${_case}+1")
    string(REPLACE "|" ";" _parts "${_refusal}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _says)
    add_executable(index_notation_${_name} EXCLUDE_FROM_ALL gates/refusals/index_notation.cpp)
    target_link_libraries(index_notation_${_name} PRIVATE heinzel)
    target_compile_definitions(index_notation_${_name} PRIVATE CASE=${_case})
    add_test(NAME build_refuses_index_notation_${_name}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target index_notation_${_name})
    set_tests_properties(build_refuses_index_notation_${_name} PROPERTIES
                         PASS_REGULAR_EXPRESSION "error: ${_says}"
                         RESOURCE_LOCK heinzel_build_tree)
endforeach()

# ---- `Executor`'s uses that must not compile (Plan/archive/14-dsl/04 §9) -------------------------------------------
# One source, one case per target, each required to fail for its own reason with exactly ONE error: a refused graph
# shows its refusal and nothing else, whatever is bound and run (04 §2). The analysis's refusal is its reason under
# C++26 and a fixed text on the C++23 floor.
if(HEINZEL_CXX_STANDARD STREQUAL "23")
    set(_refused_graph "the graph does not compile. try_compile\\(graph\\) says why") # (a `;` would split the list below)
    set(_over_a_capacity "${_refused_graph}")
    set(_not_lowered "${_refused_graph}")
else()
    set(_refused_graph "static assertion failed.*: shapes disagree at \\+")
    set(_over_a_capacity "static assertion failed.*: heinzel: a graph has more values than kMaxValues. rebuild with -DHEINZEL_MAX_VALUES=N")
    set(_not_lowered "static assertion failed.*: heinzel::compile: topk 3 is not one exact rank, which is not lowered yet: .6, 64.")
endif()
# A graph function that stops while it runs: the reason is `fail`'s argument in clang's note, the string_view's members
# in the standard library's order: `{<length>, &"<text>"[0]}` with libstdc++, `{&"<text>"[0], <length>}` with libc++.
set(_stopped "static assertion expression is not an integral constant expression.*in call to 'fail\\({([0-9]+, )?&")
set(HEINZEL_EXECUTOR_REFUSALS
    "misspelt_input|no port by that name"
    "misspelt_output|no port by that name"
    "misspelt_param|no port by that name"
    "input_on_a_param|a Param: use param\\(\\)"
    "param_on_a_signal|not a Param"
    "graph_refused|${_refused_graph}"
    "prepare_without_a_length|this graph has no Length: run\\(\\)"
    "frames_without_a_length|this graph has no Length: run\\(\\)"
    "run_with_a_length|this graph has a Length: prepare, then run\\(frames\\)"
    "a_second_symbol|at most one symbol, on the time axis"
    "a_symbol_on_a_fold|at most one symbol, on the time axis"
    "a_symbol_off_time|at most one symbol, on the time axis"
    "refused_graph_bound|${_refused_graph}"
    "refused_graph_held|${_refused_graph}"
    "one_sample_recurrence|this graph's state does not run along the stream"
    "lagged_one_sample_input|this graph's state does not run along the stream"
    "two_literal_streams|this graph's state does not run along the stream"
    "output_to_a_const_buffer|an output buffer is written to, so it is not const"
    "graph_gone|'graph' is a private member"
    "a_check_bound|no port by that name"
    "a_gradient_named_hash|${_stopped}\"heinzel: an output's name starting with # is kept for the checks of promises\""
    "over_a_capacity|${_over_a_capacity}"
    "output_assigned_twice|${_stopped}\"heinzel: an Output is assigned once\""
    "graph_function_fails|${_stopped}\"my filter: kTaps is at least 1\""
    "not_lowered|${_not_lowered}")
set(_case 0)
foreach(_refusal IN LISTS HEINZEL_EXECUTOR_REFUSALS)
    math(EXPR _case "${_case}+1")
    string(REPLACE "|" ";" _parts "${_refusal}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _says)
    add_executable(executor_${_name} EXCLUDE_FROM_ALL gates/refusals/executor.cpp)
    target_link_libraries(executor_${_name} PRIVATE heinzel_app)
    target_include_directories(executor_${_name} PRIVATE support reference)
    target_compile_definitions(executor_${_name} PRIVATE CASE=${_case})
    add_test(NAME build_refuses_executor_${_name}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target executor_${_name})
    set_tests_properties(build_refuses_executor_${_name} PROPERTIES
                         PASS_REGULAR_EXPRESSION "${_says}.*1 error generated\\."
                         RESOURCE_LOCK heinzel_build_tree)
endforeach()
# Cases 21–24 never show the cascade a graph function that stops used to start, each member read off the compile
# result failing again (Decision 77).
target_compile_options(executor_over_a_capacity PRIVATE -DHEINZEL_MAX_VALUES=4)
foreach(_name a_gradient_named_hash over_a_capacity output_assigned_twice graph_function_fails)
    set_tests_properties(build_refuses_executor_${_name} PROPERTIES FAIL_REGULAR_EXPRESSION
                         "initializer of 'result' is not a constant expression;must be initialized by a constant expression")
endforeach()

# ---- `loops::Workspace`'s uses that must not compile (Plan/15-zero-overhead/03): one error each ----------------------
set(HEINZEL_WORKSPACE_REFUSALS
    "set_length_without_a_stream|invalid reference to function 'set_length': constraints not satisfied"
    "bind_not_in_place|only an output kInPlaceOf names is written in the caller's memory")
set(_case 0)
foreach(_refusal IN LISTS HEINZEL_WORKSPACE_REFUSALS)
    math(EXPR _case "${_case}+1")
    string(REPLACE "|" ";" _parts "${_refusal}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _says)
    add_executable(workspace_${_name} EXCLUDE_FROM_ALL gates/refusals/workspace.cpp)
    target_link_libraries(workspace_${_name} PRIVATE heinzel)
    target_compile_definitions(workspace_${_name} PRIVATE CASE=${_case})
    add_test(NAME build_refuses_workspace_${_name}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target workspace_${_name})
    set_tests_properties(build_refuses_workspace_${_name} PROPERTIES
                         PASS_REGULAR_EXPRESSION "${_says}.*1 error generated\\."
                         RESOURCE_LOCK heinzel_build_tree)
endforeach()

# ---- `subgraph`'s uses that must not compile (Plan/archive/14-dsl/02 §7, Plan/28-dsl-proposals/02 A5) ---------------
# The first seven let an `Expr` in or out unseen, the eighth applies one piece twice, the ninth makes a piece another's
# body; each must fail with its own message and one error. (The `.` in the eighth stands for its `;`, which a CMake list
# cannot hold.)
set(HEINZEL_SUBGRAPH_REFUSALS
    "a_capturing_lambda|heinzel::subgraph: the body is a function, or a lambda that captures nothing"
    "an_output_argument|heinzel::subgraph: an argument is an Expr, an array of them, or a value that holds none"
    "a_struct_result|heinzel::subgraph: a body returns an Expr, or an array, a pair or a tuple of them"
    "a_reference_argument|heinzel::subgraph: the body takes its arguments by value or const reference"
    "a_derived_argument|heinzel::subgraph: an argument is an Expr, an array of them, or a value that holds none"
    "a_member_function|heinzel::subgraph: the body is a function, or a lambda that captures nothing"
    "a_body_returning_nothing|heinzel::subgraph: a body returns an Expr, or an array, a pair or a tuple of them"
    "a_piece_applied_twice|heinzel: a subgraph is applied once. make one for each use"
    "a_piece_of_a_piece|heinzel::subgraph: apply the inner piece inside the outer one's body")
set(_case 0)
foreach(_refusal IN LISTS HEINZEL_SUBGRAPH_REFUSALS)
    math(EXPR _case "${_case}+1")
    string(REPLACE "|" ";" _parts "${_refusal}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _says)
    add_executable(subgraph_${_name} EXCLUDE_FROM_ALL gates/refusals/subgraph.cpp)
    target_link_libraries(subgraph_${_name} PRIVATE heinzel)
    target_compile_definitions(subgraph_${_name} PRIVATE CASE=${_case})
    add_test(NAME build_refuses_subgraph_${_name}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target subgraph_${_name})
    set_tests_properties(build_refuses_subgraph_${_name} PROPERTIES
                         PASS_REGULAR_EXPRESSION "${_says}.*1 error generated\\."
                         RESOURCE_LOCK heinzel_build_tree)
endforeach()

# ---- a declared length that is no Signal (Plan/archive/14-dsl/10 §2): one error ----------------------------------------
set(HEINZEL_DECLARED_LENGTH_REFUSALS
    "a_tensor_variable|no matching constructor for initialization of 'Variable'"
    "a_tensor_output|no matching constructor for initialization of 'Output'"
    "a_signal_of_no_length|a signal has a length")
set(_case 0)
foreach(_refusal IN LISTS HEINZEL_DECLARED_LENGTH_REFUSALS)
    math(EXPR _case "${_case}+1")
    string(REPLACE "|" ";" _parts "${_refusal}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _says)
    add_executable(declared_length_${_name} EXCLUDE_FROM_ALL gates/refusals/declared_length.cpp)
    target_link_libraries(declared_length_${_name} PRIVATE heinzel)
    target_compile_definitions(declared_length_${_name} PRIVATE CASE=${_case})
    add_test(NAME build_refuses_declared_length_${_name}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target declared_length_${_name})
    set_tests_properties(build_refuses_declared_length_${_name} PROPERTIES
                         PASS_REGULAR_EXPRESSION "${_says}.*1 error generated\\."
                         RESOURCE_LOCK heinzel_build_tree)
endforeach()

# ---- a floating-point axis given to a member (Plan/archive/14-dsl/03 §5): a deleted overload, one error ---------------
set(HEINZEL_MEMBERS_REFUSALS
    "a_float_axis|call to deleted member function 'max'"
    "a_float_axis_kept|call to deleted member function 'max'"
    "a_float_slice|call to deleted member function 'slice'"
    "a_float_sum|call to deleted member function 'sum'"
    "a_float_mean|call to deleted member function 'mean'"
    "a_float_min|call to deleted member function 'min'"
    "a_float_prod|call to deleted member function 'prod'"
    "a_float_unfold|call to deleted member function 'unfold'"
    "a_float_unfold2d|call to deleted member function 'unfold2d'"
    "a_float_squeeze|call to deleted member function 'squeeze'"
    "a_float_unsqueeze|call to deleted member function 'unsqueeze'"
    "a_float_sort|call to deleted member function 'sort'"
    "a_float_topk|call to deleted member function 'topk'"
    "a_float_quantile|call to deleted member function 'quantile'"
    "a_float_median|call to deleted member function 'median'"
    "a_float_axis_on_an_output|call to deleted member function 'max'")
set(_case 0)
foreach(_refusal IN LISTS HEINZEL_MEMBERS_REFUSALS)
    math(EXPR _case "${_case}+1")
    string(REPLACE "|" ";" _parts "${_refusal}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _says)
    add_executable(members_${_name} EXCLUDE_FROM_ALL gates/refusals/members.cpp)
    target_link_libraries(members_${_name} PRIVATE heinzel)
    target_compile_definitions(members_${_name} PRIVATE CASE=${_case})
    add_test(NAME build_refuses_members_${_name}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target members_${_name})
    set_tests_properties(build_refuses_members_${_name} PROPERTIES
                         PASS_REGULAR_EXPRESSION "${_says}.*1 error generated\\."
                         RESOURCE_LOCK heinzel_build_tree)
endforeach()

# ---- clamp's two numbers the wrong way round (Plan/28-dsl-proposals/02 A1): one error, the refusal's text ------------
add_executable(clamp_bounds_crossed EXCLUDE_FROM_ALL gates/refusals/clamp.cpp)
target_link_libraries(clamp_bounds_crossed PRIVATE heinzel)
add_test(NAME build_refuses_clamp_bounds_crossed
         COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clamp_bounds_crossed)
set_tests_properties(build_refuses_clamp_bounds_crossed PROPERTIES
                     PASS_REGULAR_EXPRESSION "heinzel: clamp\\(x, lo, hi\\) has lo above hi.*1 error generated\\."
                     RESOURCE_LOCK heinzel_build_tree)

# ---- a table a read cannot count, lookup's bounds crossed or infinite, one cell (Plan/28-dsl-proposals/05 D3) --------
set(HEINZEL_TABLE_REFUSALS
    "a_computed_table|heinzel: a table is an Input with a shape or a constant[(][.][.][.][)], its cells known where it is read"
    "lookup_bounds_crossed|heinzel: lookup[(]table, x, lo, hi[)] has lo at or above hi"
    "lookup_bound_infinite|heinzel: lookup[(]table, x, lo, hi[)] needs lo, hi and hi - lo finite"
    "lookup_of_one_cell|heinzel: lookup[(]table, x, lo, hi[)] needs a table of at least 2 cells")
set(_case 0)
foreach(_refusal IN LISTS HEINZEL_TABLE_REFUSALS)
    math(EXPR _case "${_case}+1")
    string(REPLACE "|" ";" _parts "${_refusal}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _says)
    add_executable(table_${_name} EXCLUDE_FROM_ALL gates/refusals/table.cpp)
    target_link_libraries(table_${_name} PRIVATE heinzel)
    target_compile_definitions(table_${_name} PRIVATE CASE=${_case})
    add_test(NAME build_refuses_table_${_name}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target table_${_name})
    set_tests_properties(build_refuses_table_${_name} PROPERTIES
                         PASS_REGULAR_EXPRESSION "${_says}.*1 error generated\\."
                         RESOURCE_LOCK heinzel_build_tree)
endforeach()

# ---- a delay that changes, spelled short of what it needs (Plan/28-dsl-proposals/05 D4): one error each, its text -----
set(HEINZEL_DELAY_REFUSALS
    "unbounded|heinzel: a delay that changes needs its longest distance: x\\[n - d[.]at_most[(]120[.]0[)] [*] ms\\]"
    "no_unit|heinzel: a delay that changes is given in a unit of time: x\\[n - d[.]at_most[(]120[.]0[)] [*] ms\\]"
    "below_zero|heinzel: d[.]at_most[(]b[)] bounds a distance, so b is a finite number of at least 0"
    "no_rate|heinzel: a delay that changes needs the sample rate: constexpr Index n[{]48'000[}]")
set(_case 0)
foreach(_refusal IN LISTS HEINZEL_DELAY_REFUSALS)
    math(EXPR _case "${_case}+1")
    string(REPLACE "|" ";" _parts "${_refusal}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _says)
    add_executable(delay_${_name} EXCLUDE_FROM_ALL gates/refusals/delay.cpp)
    target_link_libraries(delay_${_name} PRIVATE heinzel)
    target_compile_definitions(delay_${_name} PRIVATE CASE=${_case})
    add_test(NAME build_refuses_delay_${_name}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target delay_${_name})
    set_tests_properties(build_refuses_delay_${_name} PROPERTIES
                         PASS_REGULAR_EXPRESSION "${_says}.*1 error generated\\."
                         RESOURCE_LOCK heinzel_build_tree)
endforeach()

# ---- a precision mark where no value is made (Plan/28-dsl-proposals/02 A4): one error each, its text ----------------
set(HEINZEL_PRECISION_REFUSALS
    "a_named_value|heinzel: mark a value where it is made: Expr y = [(]a [+] b[)][.]precision[(]High[)]"
    "a_value_read|heinzel: mark a value where it is made, before anything reads it")
set(_case 0)
foreach(_refusal IN LISTS HEINZEL_PRECISION_REFUSALS)
    math(EXPR _case "${_case}+1")
    string(REPLACE "|" ";" _parts "${_refusal}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _says)
    add_executable(precision_${_name} EXCLUDE_FROM_ALL gates/refusals/precision.cpp)
    target_link_libraries(precision_${_name} PRIVATE heinzel)
    target_compile_definitions(precision_${_name} PRIVATE CASE=${_case})
    add_test(NAME build_refuses_precision_${_name}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target precision_${_name})
    set_tests_properties(build_refuses_precision_${_name} PROPERTIES
                         PASS_REGULAR_EXPRESSION "${_says}.*1 error generated\\."
                         RESOURCE_LOCK heinzel_build_tree)
endforeach()

# ---- a range that is no list of comparators, last (Plan/26-value-ranges/02): one error -------------------------------
set(HEINZEL_RANGES_REFUSALS
    "a_promise_of_nothing|no matching member function for call to 'promise'"
    "a_promise_of_a_number|no matching member function for call to 'promise'"
    "a_param_bound_by_a_number|no matching constructor for initialization of 'Param'"
    "a_type_after_the_range|no matching constructor for initialization of 'Input'"
    "a_promise_on_now|heinzel: promise its definition: y.n. = [(][.][.][.][)][.]promise[(][.][.][.][)]"
    "a_range_of_no_value|a range leaves no value"
    "a_promise_on_a_variable_now|heinzel: promise its definition: y.n. = [(][.][.][.][)][.]promise[(][.][.][.][)]")
set(_case 0)
foreach(_refusal IN LISTS HEINZEL_RANGES_REFUSALS)
    math(EXPR _case "${_case}+1")
    string(REPLACE "|" ";" _parts "${_refusal}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _says)
    add_executable(ranges_${_name} EXCLUDE_FROM_ALL gates/refusals/ranges.cpp)
    target_link_libraries(ranges_${_name} PRIVATE heinzel)
    target_compile_definitions(ranges_${_name} PRIVATE CASE=${_case})
    add_test(NAME build_refuses_ranges_${_name}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ranges_${_name})
    set_tests_properties(build_refuses_ranges_${_name} PROPERTIES
                         PASS_REGULAR_EXPRESSION "${_says}.*1 error generated\\."
                         RESOURCE_LOCK heinzel_build_tree)
endforeach()

# ---- a graph in which an operation may leave its domain (Plan/26-value-ranges/05): one error, the finding ----------
# An `Executor` of each, so that the message is the finding's words at C++26; on the C++23 floor it is the fixed text
# every refused graph gives (`_refused_graph`, above), which the cases share.
if(HEINZEL_CXX_STANDARD STREQUAL "23")
    set(HEINZEL_PROOF_REFUSALS
        "a_division_by_zero|${_refused_graph}" "a_root_below_zero|${_refused_graph}" "a_log_of_zero|${_refused_graph}"
        "an_asin_past_one|${_refused_graph}" "a_contradiction|${_refused_graph}" "a_past_read_of_silence|${_refused_graph}"
        "a_log10_of_zero|${_refused_graph}" "a_pow_of_a_negative_base|${_refused_graph}")
else()
    set(HEINZEL_PROOF_REFUSALS
        "a_division_by_zero|heinzel: / may divide by zero: the divisor is ge\\(-1\\), le\\(1\\)"
        "a_root_below_zero|heinzel: sqrt may see a number below 0: its operand is ge\\(-1\\), le\\(1\\)"
        "a_log_of_zero|heinzel: log may see a number <= 0: its operand is ge\\(0\\), le\\(1\\)"
        "an_asin_past_one|heinzel: _asin may see a number outside ge\\(-1\\), le\\(1\\): its operand is ge\\(-2\\), le\\(2\\)"
        "a_contradiction|heinzel: promise\\(gt\\(0\\), le\\(2\\)\\) contradicts the proof: ge\\(-3\\), le\\(-1\\)"
        "a_past_read_of_silence|heinzel: / may divide by zero: the divisor is 0 or ge\\(1\\), le\\(2\\)"
        "a_log10_of_zero|heinzel: log may see a number <= 0: its operand is ge\\(0\\), le\\(1\\)"
        "a_pow_of_a_negative_base|heinzel: pow may see a negative base: the base is ge\\(-1\\), le\\(1\\)")
endif()
set(_case 0)
foreach(_refusal IN LISTS HEINZEL_PROOF_REFUSALS)
    math(EXPR _case "${_case}+1")
    string(REPLACE "|" ";" _parts "${_refusal}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _says)
    add_executable(proof_${_name} EXCLUDE_FROM_ALL gates/refusals/proof.cpp)
    target_link_libraries(proof_${_name} PRIVATE heinzel)
    target_compile_definitions(proof_${_name} PRIVATE CASE=${_case})
    add_test(NAME build_refuses_proof_${_name}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target proof_${_name})
    set_tests_properties(build_refuses_proof_${_name} PROPERTIES
                         PASS_REGULAR_EXPRESSION "${_says}.*1 error generated\\."
                         RESOURCE_LOCK heinzel_build_tree)
endforeach()

# ---- the exclusion board's one compile-time refusal (milestone M5.9, oracle O9) ------------------
# Board row 16 — a rank over `kMaxRank` — is a `Shape`-constructor `fail()`, not a `Diagnostic`, so it
# cannot be shown rejecting from a `static_assert` that evaluates the pass: a throw in a constant
# expression is ill-formed, not a `false` value. `exclusion_board_test.cpp` asserts the ceiling still
# exists; this proves the guard actually stops a build. Same mechanics as the half-done refusals above —
# a translation unit that must NOT compile, held out of the default build, required to fail FOR ITS OWN
# REASON by matching the ctor's message rather than by WILL_FAIL (which passes on any build failure,
# including an unrelated one). RESOURCE_LOCK for the same reason: it runs a build in the shared tree.
add_executable(board_offender_rank_over_maximum EXCLUDE_FROM_ALL gates/refusals/rank_over_maximum.cpp)
target_link_libraries(board_offender_rank_over_maximum PRIVATE heinzel)

add_test(NAME board_refuses_rank_over_maximum
         COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target board_offender_rank_over_maximum)
set_tests_properties(board_refuses_rank_over_maximum PROPERTIES
                     PASS_REGULAR_EXPRESSION "more axes than kMaxRank"
                     RESOURCE_LOCK heinzel_build_tree)

# ---- a capacity reached is refused by name, with its override (Plan/15-zero-overhead/02) ---------------------------
# One case per override (two for kMaxIO, inputs and outputs, and for kMaxStageStmts, a stage and a cycle), built with
# a small override of its own through target_compile_options and required to fail with the refusal's words through
# `=N`. Never the bare macro name: ninja's FAILED line echoes the command, `-DHEINZEL_MAX_…=2` included. A `;` is
# written `[;]`, which CMake does not split a list at. The copy of an input a delay line reads past the values and the
# nodes (Plan/28-dsl-proposals/05 D4, its review) asks for one error too: its graph, asserted at the capacity, fits.
set(HEINZEL_CAPACITY_REFUSALS
    "values|-DHEINZEL_MAX_VALUES=4|more values than kMaxValues[;] rebuild with -DHEINZEL_MAX_VALUES=N"
    "nodes|-DHEINZEL_MAX_NODES=2|more nodes than kMaxNodes[;] rebuild with -DHEINZEL_MAX_NODES=N"
    "inputs|-DHEINZEL_MAX_IO=2|more inputs than kMaxIO[;] rebuild with -DHEINZEL_MAX_IO=N"
    "outputs|-DHEINZEL_MAX_IO=2|more outputs than kMaxIO[;] rebuild with -DHEINZEL_MAX_IO=N"
    "dim_decls|-DHEINZEL_MAX_DIM_DECLS=1|to kMaxDimDecls - 1[;] rebuild with -DHEINZEL_MAX_DIM_DECLS=N"
    "stmts|-DHEINZEL_MAX_STMTS=4|more statements than kMaxStmts[;] rebuild with -DHEINZEL_MAX_STMTS=N"
    "tree_nodes|-DHEINZEL_MAX_TREE_NODES=4|more tree nodes than kMaxTreeNodes[;] rebuild with -DHEINZEL_MAX_TREE_NODES=N"
    "buffers|-DHEINZEL_MAX_BUFFERS=2|more buffers than kMaxBuffers[;] rebuild with -DHEINZEL_MAX_BUFFERS=N"
    "stages|-DHEINZEL_MAX_STAGES=1|more loop nests than kMaxStages[;] rebuild with -DHEINZEL_MAX_STAGES=N"
    "stage_stmts|-DHEINZEL_MAX_STAGE_STMTS=4|a stage over kMaxStageStmts[;] rebuild with -DHEINZEL_MAX_STAGE_STMTS=N"
    "cycle|-DHEINZEL_MAX_STAGE_STMTS=2|a cycle over kMaxGroupStmts[;] rebuild with -DHEINZEL_MAX_STAGE_STMTS=N"
    "constants|-DHEINZEL_MAX_CONSTANTS=2|more constants than kMaxConstants[;] rebuild with -DHEINZEL_MAX_CONSTANTS=N"
    "ranges|-DHEINZEL_MAX_RANGES=1|more ranges than kMaxRanges[;] rebuild with -DHEINZEL_MAX_RANGES=N"
    "checks|-DHEINZEL_MAX_IO=1|the checks of promises make more outputs than kMaxIO[;] rebuild with -DHEINZEL_MAX_IO=N"
    "copy_values|-DHEINZEL_MAX_VALUES=5|more values than kMaxValues[;] rebuild with -DHEINZEL_MAX_VALUES=N.*1 error generated[.]"
    "copy_nodes|-DHEINZEL_MAX_NODES=2|more nodes than kMaxNodes[;] rebuild with -DHEINZEL_MAX_NODES=N.*1 error generated[.]")
set(_case 0)
foreach(_refusal IN LISTS HEINZEL_CAPACITY_REFUSALS)
    math(EXPR _case "${_case}+1")
    string(REPLACE "|" ";" _parts "${_refusal}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _override)
    list(GET _parts 2 _says)
    add_executable(capacity_${_name} EXCLUDE_FROM_ALL gates/refusals/capacity.cpp)
    target_link_libraries(capacity_${_name} PRIVATE heinzel)
    target_compile_definitions(capacity_${_name} PRIVATE CASE=${_case})
    target_compile_options(capacity_${_name} PRIVATE ${_override})
    add_test(NAME build_refuses_capacity_${_name}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target capacity_${_name})
    set_tests_properties(build_refuses_capacity_${_name} PROPERTIES
                         PASS_REGULAR_EXPRESSION "${_says}"
                         RESOURCE_LOCK heinzel_build_tree)
endforeach()

# ---- and raised: every override at once, graphs just over each default run on Executor (Plan/15-zero-overhead/02) ---
# Three targets of one file, so that no translation unit is the suite's slowest by far.
foreach(_part 1 2 3)
    add_executable(test_capacities_raised_${_part} unit/capacities_raised_test.cpp)
    target_link_libraries(test_capacities_raised_${_part} PRIVATE heinzel)
    target_compile_definitions(test_capacities_raised_${_part} PRIVATE PART=${_part})
    target_compile_options(test_capacities_raised_${_part} PRIVATE
                           -DHEINZEL_MAX_VALUES=256 -DHEINZEL_MAX_NODES=192 -DHEINZEL_MAX_IO=128 -DHEINZEL_MAX_DIM_DECLS=9
                           -DHEINZEL_MAX_STMTS=256 -DHEINZEL_MAX_TREE_NODES=768 -DHEINZEL_MAX_BUFFERS=128
                           -DHEINZEL_MAX_STAGES=48 -DHEINZEL_MAX_STAGE_STMTS=256 -DHEINZEL_MAX_CONSTANTS=2048)
    add_test(NAME capacities_raised_${_part} COMMAND test_capacities_raised_${_part})
endforeach()

# The frontend records what was written and decides nothing semantic: it inserts no conversions, and
# it has no machinery for deferring operations. Both are properties that erode quietly.
add_test(NAME gate_frontend_decides_nothing_semantic
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/../include/heinzel/frontend
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/frontend_decides_nothing_semantic.cmake)

# Shown OBJECTING to a frontend header that keeps operations for later, and to one that inserts a conversion, and
# shown PASSING one whose code says "appending" and "depending". PASS_REGULAR_EXPRESSION, not WILL_FAIL: the
# expression names the file and the property, so a match cannot come from the gate crashing.
foreach(_case "defers|a_frontend_that_defers\\.hpp[ \n]+contains deferred-operation machinery"
              "queues|a_frontend_that_queues\\.hpp[ \n]+contains deferred-operation machinery"
              "converts|a_frontend_that_converts\\.hpp[ \n]+emits a conversion"
              "appends|gate passed: the frontend records")
    string(REPLACE "|" ";" _parts "${_case}")
    list(GET _parts 0 _fixture)
    list(GET _parts 1 _says)
    add_test(NAME gate_frontend_decides_nothing_semantic_on_${_fixture}
             COMMAND ${CMAKE_COMMAND}
                     -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/gates/fixtures/frontend_decides_nothing_semantic/${_fixture}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/frontend_decides_nothing_semantic.cmake)
    set_tests_properties(gate_frontend_decides_nothing_semantic_on_${_fixture} PROPERTIES PASS_REGULAR_EXPRESSION "${_says}")
endforeach()

# ---- the two worlds, where a fused multiply-add would separate them --------------------------------
# The library sets `-ffp-contract=off` and the reason is in the root CMakeLists. Here it is checked,
# in the only way a flag like that CAN be checked: on a target that has the fused instruction, since
# baseline x86-64 does not and a check on a target without one would pass for the wrong reason.
#
# Two tests, and the pair is the point. One compiles the probe as the library is compiled and
# requires the two worlds to agree; the other compiles the same source with contraction deliberately
# on and requires them to DIFFER. Without the second, the first would be green on every machine in
# the world and would be evidence of nothing.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-mfma HEINZEL_HAS_FUSED_MULTIPLY_ADD_FLAG)

if(HEINZEL_HAS_FUSED_MULTIPLY_ADD_FLAG)
    add_executable(fusion_probe two_worlds/fusion_probe.cpp)
    target_link_libraries(fusion_probe PRIVATE heinzel)     # ...which is where -ffp-contract=off comes from
    target_compile_options(fusion_probe PRIVATE -mfma)      # ...and this is what makes the question real
    add_test(NAME two_worlds_agree_under_fusion COMMAND fusion_probe)

    add_test(NAME two_worlds_would_separate_without_the_flag
             COMMAND ${CMAKE_COMMAND}
                     -DCOMPILER=${CMAKE_CXX_COMPILER}
                     -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/two_worlds/fusion_probe.cpp
                     -DARCH_FLAG=-mfma
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/gates/fusion_would_separate_the_worlds.cmake)
else()
    # Said out loud rather than silently skipped: on a machine where the question cannot be asked,
    # the claim is untested here, and a reader deserves to know which of those two they are looking
    # at. The library still sets the flag.
    message(STATUS "heinzel: no -mfma on this compiler, so the two-worlds fusion check is not registered")
endif()

# ---- parity against the old tree ------------------------------------------------------------------
# Milestone M1's fourth condition: the same filter, built here and built on the old implementation
# at c4012b8, producing the same numbers.
#
# The old tree's headers are extracted from the SHARED OBJECT DATABASE rather than read out of
# /home/user/ctg3's working directory. Two reasons, and the second one is why it is worth the extra
# step: extraction reads one pinned COMMIT rather than whatever happens to be checked out or whatever
# `main` has moved on to, so the reference cannot drift (and survives `rewrite` being merged into
# `main`; the commit is also tagged `parity-oracle-v1`); and nothing here ever opens that directory,
# which keeps it read-only in fact and not merely in intention.
# Not a cache entry: a build dir that never overrode it (-DHEINZEL_PARITY_REF=...) follows this line.
# A commit SHA or a tag, never a branch: the ref names the extraction directory, and a name that moves
# would leave stale objects behind it.
if(NOT DEFINED HEINZEL_PARITY_REF)
    set(HEINZEL_PARITY_REF "c4012b828897aa5c7eb86386eca4dad97eeb56d0")
endif()
find_package(Git QUIET)
# On by default only where the ref is in the repository: the public repository has none of the old tree's history,
# and a plain configure there must work. Asked for explicitly and missing, it still stops the configure (below).
set(_parity_default OFF)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} -C "${CMAKE_CURRENT_SOURCE_DIR}/.." cat-file -e "${HEINZEL_PARITY_REF}^{commit}"
        RESULT_VARIABLE _parity_ref_rc OUTPUT_QUIET ERROR_QUIET)
    if(_parity_ref_rc EQUAL 0)
        set(_parity_default ON)
    endif()
endif()
option(HEINZEL_PARITY "Build the parity pairs against the old tree at HEINZEL_PARITY_REF" ${_parity_default})
# The ref is part of the directory name, so changing it changes every `_there` compile line: the
# extracted headers keep their commit's mtime, and a build would otherwise see nothing newer.
set(HEINZEL_OLD_TREE "${CMAKE_CURRENT_BINARY_DIR}/old-tree-${HEINZEL_PARITY_REF}")

# FINDING M5-S2e-HARNESS. What the old side is compiled WITH, and it is not a detail.
#
# This tree's targets get `-ffp-contract=off` through the `heinzel` interface library, so nothing
# here lets the compiler fuse a multiply and an add on its own account. The old side does not link
# that library — deliberately, it must see only its own headers — and until this was measured it
# therefore got no such flag. At `-O3` the compiler then fuses arithmetic inside the REFERENCE, and
# the difference that shows up in a parity comparison is the two COMPILE LINES rather than the two
# implementations.
#
# Measured, on the two fixtures milestone M4.5 carried into M5: a reduction differs by 2.390e-07 with
# fusion allowed on the old side and by EXACTLY ZERO with it off; a contraction by 1.900e-06 against
# 2.980e-08. Both differences were being attributed to the trees.
#
# It changes nothing already recorded, which is why it went unnoticed: the biquad's old-side samples
# are bit for bit identical with the flag and without it (its 5.960e-08 really is the old tree's own
# source-level `contract_fma`, as its allowance says), and so are the delay line's. A fixture whose
# reference has a multiply-add chain the compiler can fuse is what makes the flag visible, and a fold
# is exactly that.
#
# LIBM ON THE OLD SIDE. The old tree's scalar path evaluates exp/log/sin/cos/tanh/pow through libm at
# run time (its ir/op_eval.hpp), and libm's float functions are not correctly rounded: their last bit
# differs between glibc versions. This tree's cores are its own and do not move. So for the pairs
# whose old-side graph reaches one of those ops the allowance is a measurement on ONE libm, and a
# failure by one f32 ULP on another host can be the reference moving rather than this tree.
# Re-measure and restate it there; do not widen it blind. Found by counting libm calls per `_there`
# binary; measured 2026-09-18, glibc 2.39, as largest difference / allowance:
#   custom_op (logf) 0 / 0                       graph_composition (tanhf) 5.970e-08 / 6.0e-08
#   channel_strip (tanhf) 5.970e-08 / 6.0e-08   softmax (expf) 2.980e-08 / 3.0e-08
#   oscillator (sinf) 1.937e-07 / 2.5e-07       oscillator_affine (sinf) 1.714e-07 / 2.5e-07
#   tutorial_02 (tanhf, logf) and tutorial_03 (expf): 1.900e-06 / 1.91e-06 each, but that number is an
#   fma difference in another of their graphs; their libm graphs measure bit-exact here.
# No other old-side graph calls libm. A second, weaker dependence: several pairs generate their INPUT
# with host libm (sinf; mlp_train also tanhf) — identically on both sides, so it cannot make the trees
# disagree, but a different glibc feeds different numbers and a largest difference measured within one
# ULP of its allowance (mlp_forward, mlp_train, tutorial_04, tutorial_07) or at zero (fir_fit) may land
# elsewhere.
set(HEINZEL_OLD_SIDE_OPTIONS -std=c++2c -ffp-contract=off)
if(HEINZEL_PARITY AND GIT_FOUND)
    # Removed first: extraction overwrites but never deletes, so a tree left by an earlier ref would
    # keep its headers — and the sentinel checked below — alive under a new one.
    file(REMOVE_RECURSE "${HEINZEL_OLD_TREE}")
    file(MAKE_DIRECTORY "${HEINZEL_OLD_TREE}")
    # Two steps rather than a pipeline: `cmake -E tar` cannot read an archive from standard input,
    # and `git -C` is what lets the archive be produced from the repository while it is written into
    # the build directory.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} -C "${CMAKE_CURRENT_SOURCE_DIR}/.."
                archive -o "${HEINZEL_OLD_TREE}/old-tree.tar" ${HEINZEL_PARITY_REF} include
        RESULT_VARIABLE _extract_rc
        ERROR_VARIABLE _extract_err)
    if(_extract_rc EQUAL 0)
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar x "${HEINZEL_OLD_TREE}/old-tree.tar"
            WORKING_DIRECTORY "${HEINZEL_OLD_TREE}"
            RESULT_VARIABLE _extract_rc
            ERROR_VARIABLE _extract_err)
    endif()
endif()

if(HEINZEL_PARITY AND GIT_FOUND AND _extract_rc EQUAL 0 AND EXISTS "${HEINZEL_OLD_TREE}/include/heinzel/frontend/builder.hpp")
    message(STATUS "heinzel: parity reference ${HEINZEL_PARITY_REF}")
    # WHY THE BIQUAD'S ALLOWANCE IS 6.0e-08, AND WHY IT IS NOT A FUDGE. Kept here, beside the number
    # it justifies, rather than in `compare.cmake`, which is about the mechanism and not about any
    # one family. The plan expected bit-exact agreement at the exact tier; measurement refuted that
    # and identified the cause completely:
    #
    #   * this tree evaluates  x + c1*y[n-1] + c2*y[n-2]  in plain single precision, and reproduces
    #     a hand-written plain-float reference EXACTLY;
    #   * the old tree rewrites `a*b + c` into a single fused multiply-add before generating code
    #     (its opt/optimize.hpp `contract_fma`), and reproduces a hand-written FUSED reference
    #     EXACTLY.
    #
    # So the entire difference is one fused multiply-add. Fusing is MORE accurate, not less, and the
    # old tree counts it as part of its exact baseline rather than as an accuracy trade — which is
    # worth knowing, because it means setting both sides to "exact" does not make them bit-identical.
    # The measured largest difference is 5.960e-08 against samples reaching 1.273: one
    # single-precision step at that magnitude, which is exactly one rounding, as the explanation
    # predicts. **M4.2 reconciles its own tier measurements against that number**, so it is recorded
    # here as a measurement and not only as a bound.
    #
    # The allowance goes to zero when this tree gains the same contraction, and that is the moment to
    # revisit it.
    add_executable(parity_biquad_here parity/biquad_here.cpp)
    target_link_libraries(parity_biquad_here PRIVATE heinzel)

    # The old side is deliberately NOT linked against this tree's library: it is the other
    # implementation, and it must see only its own headers.
    add_executable(parity_biquad_there parity/biquad_there.cpp)
    target_include_directories(parity_biquad_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_biquad_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_executable(compare_samples parity/compare_samples.cpp)

    add_test(NAME parity_biquad
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_biquad_here>
                     -DTHERE=$<TARGET_FILE:parity_biquad_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=walked
                     -DALLOWED=6.0e-08
                     "-DBECAUSE=the old tree fuses a*b+c into a single multiply-add before generating code (its opt/optimize.hpp contract_fma) and counts that as part of its exact baseline. This tree does not contract, so the whole gap is one fused multiply-add worth of rounding at these magnitudes"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- milestone M4.5: one graph per family, and the delay line is the third of them ------------
    # The biquad above covers arithmetic and state. This covers the tap delay line — the operation
    # whose read position is a SIGNAL, so it is the one family whose parity says anything about the
    # seam M4 widened. Its two graphs are the same graph in the two trees' own languages, and the
    # position they compute is exact in both, so what the comparison measures is the interpolation
    # and not a disagreement about which pair of samples to read.
    #
    # THE ALLOWANCE IS ONE SINGLE-PRECISION STEP AT THE MAGNITUDE THIS SIGNAL REACHES, and both
    # halves of that sentence are measured rather than chosen. The signal reaches 4.82 — the index
    # (n*5) mod 23 visits every residue, so the largest tap is 0.31*22 - 2 — which lives in [4, 8),
    # where a float step is 2^-21 = 4.768372e-07. The largest difference over the block is exactly
    # that step, at sample 12, and 4.8e-07 is it rounded up in the third digit. So the bound is the
    # step and not a margin widened until the test went green, which is the distinction the named
    # cause exists to make checkable.
    #
    # The harness will REPORT 4.760e-07 rather than 4.768372e-07, and that is the comparison's own
    # resolution rather than a second difference: both sides print nine significant digits and are
    # read back as doubles, so a float gap arrives slightly rounded. Said here because a reader who
    # checks the message against this comment would otherwise find two numbers where there is one.
    #
    # This tree's half of that difference is not a guess: a hand reference computing
    # near + f*(far - near) with one rounding reproduces all sixty-four of its samples bit for bit.
    # The old tree's own source states its form in as many words (emit/emit.hpp frac_delay_read).
    add_executable(parity_dline_here parity/dline_here.cpp)
    target_link_libraries(parity_dline_here PRIVATE heinzel)

    add_executable(parity_dline_there parity/dline_there.cpp)
    target_include_directories(parity_dline_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_dline_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_dline
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_dline_here>
                     -DTHERE=$<TARGET_FILE:parity_dline_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=dline
                     -DALLOWED=4.8e-07
                     "-DBECAUSE=the two trees write the same linear blend two ways and therefore round at different points. This tree computes near + f*(far - near) and rounds once, at the node output (tests/reference/compute_reference.hpp tapped_of), while the old tree computes near*(1 - f) + far*f in single precision throughout (its emit/emit.hpp frac_delay_read). The forms are algebraically equal, the arithmetic is not, and 25 of the 64 samples differ"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)
    # ---- milestone M4.5's CARRIED HALF: the reduction and the contraction ------------------------
    # These are the two families M4.5 named that could not be compared when M4 closed, because an
    # operation the table classifies `AxisChanging` could not be RUN here at all — asking one for a
    # sample-at-a-time meaning was a `fail()` by design. M5's S2b and S2d gave the three runners the
    # other kind of meaning, so the condition's own words — compared numerically, each difference
    # above zero attributed to a named cause — are finally answerable.
    #
    # Both fixtures reduce or contract over axis 0, the CHANNEL axis, because the streamed axis is the
    # innermost one in BOTH trees. This tree settled that at M5 on its own committed evidence (a lag is
    # refused along any axis but the innermost; `Elementwise` broadcasts from the innermost outwards);
    # the old tree says it outright in `integrations/juce/graph_processor.hpp` and `opt/relayout.hpp`.
    # Worth recording, because it is what makes these comparisons and not translations.
    #
    # THE REDUCTION IS BIT-EXACT — allowance zero, no cause to name. Every one of the sixty-four
    # samples agrees, which is the strongest form this condition can take and is why it is stated
    # first: it establishes that the two trees agree about WHICH axis collapses and about the fan-in,
    # so whatever the contraction's allowance turns out to be cannot be hiding either of those.
    add_executable(parity_reduce_here parity/reduce_here.cpp)
    target_link_libraries(parity_reduce_here PRIVATE heinzel)

    add_executable(parity_reduce_there parity/reduce_there.cpp)
    target_include_directories(parity_reduce_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_reduce_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_reduce
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_reduce_here>
                     -DTHERE=$<TARGET_FILE:parity_reduce_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=reduce
                     -DALLOWED=0.0
                     "-DBECAUSE="
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # The allowance below is a measurement, not a margin. The contraction, `einsum("c,c->", …)`, differs on 16 of
    # sixty-four samples, each by exactly one single-precision step at its magnitude, the largest 1.91e-06 at 26.46
    # (2^-19). Hand references reproduce every sample of both sides:
    #
    #   * this tree builds the contraction as a multiply and a sum (Decision 78), so at High each product is rounded
    #     to f32, the multiply's own result, and the three are summed in double and rounded ONCE when the fold
    #     finishes;
    #   * the old tree's scalar backend runs a per-term fused multiply-add in f32: each product exact, each partial
    #     sum rounded.
    #
    # Until Decision 78 this tree summed the exact products in double and rounded once (FINDING M5-S2e-FOLDPRECISION):
    # two samples differed, by 2.98e-08 at 0.3456, and that note's "rounds each product and each partial sum" for the
    # old tree reproduces 48 of its 64 samples, the fused form all 64. The allowance goes to zero the day the two
    # trees round a contraction the same way.
    add_executable(parity_contract_here parity/contract_here.cpp)
    target_link_libraries(parity_contract_here PRIVATE heinzel)

    add_executable(parity_contract_there parity/contract_there.cpp)
    target_include_directories(parity_contract_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_contract_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_contract
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_contract_here>
                     -DTHERE=$<TARGET_FILE:parity_contract_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=contract
                     -DALLOWED=1.91e-06
                     "-DBECAUSE=this tree builds the aligned contraction as a multiply and a sum (Decision 78): at High each product is rounded to f32 and the products are summed in double and rounded once, while the old tree's scalar backend runs a per-term fused multiply-add in f32, each product exact and each partial sum rounded. Hand references for both forms reproduce their own tree's samples bit for bit. Sixteen of the sixty-four samples differ, each by exactly one single-precision step at its magnitude, the largest 2^-19 at 26.46"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- M8.3 example: `custom_op` — a composed helper, run and output-compared ------------------
    # The first of the eighteen examples ported onto the frontend (operator decision 2026-09-04 11:58,
    # Option A). It exercises the point the example exists to make — extending the vocabulary by
    # composing a `constexpr` helper (`log10 = log * 1/ln10`), not by a registration seam — and needs
    # exactly one new frontend spelling, `log`, which names the already-checked M4 code `op::Log`.
    # Both sides spell the same helper in the same order with the same f32 constant; the pair measures
    # bit-identical (BECAUSE has the measurement and what the earlier 1.9e-06 really was).
    add_executable(parity_custom_op_here parity/custom_op_here.cpp)
    target_link_libraries(parity_custom_op_here PRIVATE heinzel)

    add_executable(parity_custom_op_there parity/custom_op_there.cpp)
    target_include_directories(parity_custom_op_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_custom_op_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_custom_op
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_custom_op_here>
                     -DTHERE=$<TARGET_FILE:parity_custom_op_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=custom_op
                     -DALLOWED=0.0
                     "-DBECAUSE=both sides spell log10(x)=log(x)*(1/ln10) with the same f32 constant, then multiply by 20, in the same order. Measured on glibc 2.39: all 64 samples bit-identical — the old side's libm logf and this tree's log core round alike on this input. History: 1.9e-06 was recorded here as 'the two log cores', and it was neither — the `_here` graph carried 1/ln10 as a double where the old side rounds the literal to f32 (a literal's value is not rounded to its adapted type). The old side's logf is libm and may move on another glibc; re-measure there, do not widen blind"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- M8.3 example: `conditionals` — gate / soft limiter / hard limiter, run and compared -------
    # The second ported example (operator decision 2026-09-04 11:58, Option A). It establishes that a
    # runtime scalar knob ("Param" in the old tree) is, here, an ordinary named input fed a
    # constant-valued stream on the MW multi-input surface, and it adds the exact choose/compare/clamp
    # spellings (max/min/abs/>/where), each naming an already-checked M4 code. Three sections are
    # printed in one stream (gate, soft, hard). All three compare EXACTLY across the two trees on these
    # sixty-four inputs — the gate and hard limiter are integer masks and clamps that cannot round, and
    # the soft limiter's one division was measured to agree bit for bit here (the double-rounding that
    # the contraction's finding turns on does not bite on any of these samples). So ALLOWED is 0.0 and
    # no cause is owed; a difference of any size is a real regression, not an accounted tolerance.
    add_executable(parity_conditionals_here parity/conditionals_here.cpp)
    target_link_libraries(parity_conditionals_here PRIVATE heinzel)

    add_executable(parity_conditionals_there parity/conditionals_there.cpp)
    target_include_directories(parity_conditionals_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_conditionals_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_conditionals
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_conditionals_here>
                     -DTHERE=$<TARGET_FILE:parity_conditionals_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=conditionals
                     -DALLOWED=0.0
                     "-DBECAUSE="
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- M8.3 example: `graph_composition` — a composed channel strip, run and compared -----------
    # The third ported example (Option A). It composes reusable fragments the way this tree does —
    # `constexpr` functions over Expr, applied as subgraphs under the same dotted parameter names
    # the old tree's `instantiate` produces — and reuses the one-pole recurrence fragment twice in
    # series. Adds only the `tanh` spelling (names op::Tanh). ALLOWED is measured and its two causes are
    # named in BECAUSE. History worth keeping: this pair failed on glibc 2.39 at 1.192e-07, and the cause
    # was neither tree — the `_here` program fed the knobs as the doubles 0.3/0.8/0.7 where the old side
    # feeds the f32 literals. With the same numbers fed, the first measurement (6.0e-08) holds here too.
    add_executable(parity_graph_composition_here parity/graph_composition_here.cpp)
    target_link_libraries(parity_graph_composition_here PRIVATE heinzel)

    add_executable(parity_graph_composition_there parity/graph_composition_there.cpp)
    target_include_directories(parity_graph_composition_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_graph_composition_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_graph_composition
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_graph_composition_here>
                     -DTHERE=$<TARGET_FILE:parity_graph_composition_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=graph_composition
                     -DALLOWED=6.0e-08
                     "-DBECAUSE=both sides are fed the same f32 knob values and the same input, and two things differ. The old tree fuses each one-pole's (1-a)*x + a*y into one fused multiply-add (its contract_fma) where this tree rounds the product and the sum separately; and the old tree's tanh is libm tanhf at run time, which is not correctly rounded and varies by glibc, where this tree runs its own core. Both are reproduced exactly, all 32 samples each, by two hand-written float models (fma + tanhf for the old side, plain arithmetic + a correctly rounded tanh for this one). Measured on glibc 2.39: 14 of 32 samples differ, largest 5.970e-08 at sample 4, one f32 ULP near 0.51. The fma half goes to zero when this tree contracts; the tanhf half is the old side's libm"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- MB example: `channel_scale_3d` — N-D elementwise + per-channel broadcast, run and compared --
    # The first of milestone MB's nine batch examples (operator decision 2026-09-04 13:29). MB.S2 was
    # written to ADD operand broadcast; the tick's finding F-MB-BROADCAST-ALREADY-EXISTS established it
    # already exists in both layers, so this is a PORT on the existing surface: y = x{C,H,W} * scale{C,1}
    # + bias{C,1}, with W taken as the streamed axis and scale/bias broadcasting over H and constant
    # across the stream. The only arithmetic is one multiply then one add on exact f32 operands, so both
    # trees produce the IEEE f32 result and the two agree BIT FOR BIT — the there side pins
    # set_fast_math(false) so it cannot contract into a once-rounded FMA. So ALLOWED is 0.0 and no cause
    # is owed; a difference of any size is a real regression, not an accounted tolerance. The rejection
    # half of MB.S2's condition (an incompatible broadcast shown refused) is inference_test.cpp's
    # elementwise `_add` disagreement, on the same op family this example runs.
    add_executable(parity_channel_scale_here parity/channel_scale_here.cpp)
    target_link_libraries(parity_channel_scale_here PRIVATE heinzel)

    add_executable(parity_channel_scale_there parity/channel_scale_there.cpp)
    target_include_directories(parity_channel_scale_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_channel_scale_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_channel_scale
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_channel_scale_here>
                     -DTHERE=$<TARGET_FILE:parity_channel_scale_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=channel_scale
                     -DALLOWED=0.0
                     "-DBECAUSE="
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- MB example: `image_elementwise_2d` — the 2-D spatial surface, run and compared -------------
    # The second of milestone MB's nine batch examples (MB.S3). An H×W image put through a smooth
    # saturator y = t/(1+t*t), t = gain*x + bias, with bias{W} broadcast over rows and gain a runtime
    # scalar — whole-tensor and index-free, no sample stream. It rests on the 2-D element-axis run
    # surface (a degenerate length-1 stream over a rank-2 element value) that image_surface_test.cpp
    # verifies three ways; here it is RUN on Executor and output-compared to the old tree. The ALLOWED
    # below is one FMA-contraction difference, measured (see BECAUSE), NOT a fitted margin: the two
    # outputs are each pinned to an exact evaluation, so the whole gap is accounted.
    add_executable(parity_image_elementwise_here parity/image_elementwise_here.cpp)
    target_link_libraries(parity_image_elementwise_here PRIVATE heinzel)

    add_executable(parity_image_elementwise_there parity/image_elementwise_there.cpp)
    target_include_directories(parity_image_elementwise_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_image_elementwise_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_image_elementwise
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_image_elementwise_here>
                     -DTHERE=$<TARGET_FILE:parity_image_elementwise_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=image_elementwise
                     -DALLOWED=6.0e-08
                     "-DBECAUSE=the graph is add/mul/div only on exact f32 operands, so with -ffp-contract=off on both sides the arithmetic would match bit for bit — except the old tree's op-eval CONTRACTS a*b+c into a single-rounded fused multiply-add (fmaf) at evaluation, which -ffp-contract=off does not suppress because it is explicit, not a compiler contraction. It does so twice: t = fma(gain,x,bias) and denom = fma(t,t,1). This tree evaluates a separately-rounded multiply then add (it has no auto-fusion; op::Fma is spelled explicitly or not at all). The here output is exactly the non-contracted evaluation and the there output exactly the twice-contracted one (both matched against a hand reference), so the whole difference is those two intermediate-product roundings — one f32 ULP at the output's magnitude on 22 of the 96 pixels (max 5.96e-08 near 0.5). The allowance is that one ULP and goes to zero the day this tree contracts a*b+c the same way"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- MB example: `einsum_contractions` — the general contraction spelled as einsum, run & compared -
    # MB.S4e. The old example spells a matmul, a batched matmul and a THREE-operand chain with
    # einsum("...->..."). MB.S4a–S4d make the first two expressible (the notation lowers a
    # transposing/batched contraction to op::MatMul); the three-operand chain is the M8.2-reserved refusal
    # (a capture layer must not choose a pairing order) and is NOT ported — the notation refuses it by
    # name (notation_test.cpp). So the here/there pair runs the matmul and the batched matmul only.
    #
    # ALLOWED is 0.0, MEASURED not fitted: all 45 outputs (M*N + Bt*M*N) agree BIT FOR BIT. That is worth
    # a note because — unlike channel_scale's power-of-two exactness — it is NOT structural. The two trees
    # accumulate the contraction's sum DIFFERENTLY: the old tree's scalar backend does a per-term
    # `fma_rn(A,B,acc)` in f32 (exec_contract.hpp, rounding once per K-step), while this tree sums the
    # exact f32 products in double and rounds ONCE at the end (at High). For these operands and
    # K=4/5 the two land on the same f32 bits at every cell, so the allowance is zero and any difference a
    # future arithmetic change introduced would be caught — a different contraction could diverge by that
    # reduction rounding, which would then be an accounted tolerance like image_elementwise's FMA ULP.
    add_executable(parity_einsum_contractions_here parity/einsum_contractions_here.cpp)
    target_link_libraries(parity_einsum_contractions_here PRIVATE heinzel)

    add_executable(parity_einsum_contractions_there parity/einsum_contractions_there.cpp)
    target_include_directories(parity_einsum_contractions_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_einsum_contractions_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_einsum_contractions
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_einsum_contractions_here>
                     -DTHERE=$<TARGET_FILE:parity_einsum_contractions_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=einsum_contractions
                     -DALLOWED=0.0
                     "-DBECAUSE="
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- MB example: `mlp_train` FORWARD — matmul composed in an MLP, run & compared ------------------
    # MB.S4f. The old mlp_train example TRAINS a 2-layer ReLU MLP: forward, then gradients() appends a
    # backward pass, then the host runs SGD. MB.S4 makes the FORWARD expressible — two contractions
    # ("bi,ih->bh" and "bh,ho->bo"), a broadcast bias add, a ReLU — so the forward is ported and
    # output-compared; the training is the `mlp_train` pair below, through gradients(…). ALLOWED is set
    # from the MEASURED difference (see BECAUSE): the
    # forward chains two contractions, each a SUM the two trees round differently (old: per-term fma_rn
    # in f32; here: exact products in double, one final rounding).
    add_executable(parity_mlp_forward_here parity/mlp_forward_here.cpp)
    target_link_libraries(parity_mlp_forward_here PRIVATE heinzel)

    add_executable(parity_mlp_forward_there parity/mlp_forward_there.cpp)
    target_include_directories(parity_mlp_forward_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_mlp_forward_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_mlp_forward
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_mlp_forward_here>
                     -DTHERE=$<TARGET_FILE:parity_mlp_forward_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=mlp_forward
                     -DALLOWED=0x1p-23
                     "-DBECAUSE=the forward is two contractions with a broadcast bias add and a ReLU between them; the biases are zero and ReLU/add are exact, so the whole difference is how the two trees round the contraction SUM. The old tree accumulates each cell with a per-term fma_rn in f32 (exec_contract.hpp, rounding once per K-step); this tree sums the exact f32 products in double and rounds ONCE at the end (at High). The two disagree by at most one f32 ULP at the output's magnitude — measured 1.19e-07 near 1.11 (the [1,2) ULP is 2^-23 = 1.192e-07) on 14 of the 32 outputs, zero on the other 18 (on the m1, with Apple's sinf making the input, near 1.15 on 16 of them). The allowance is that one ULP, written exactly; both sides print in hex, so the comparison sees the f32 values and not their decimal roundings, which could put one ULP's gap up to 1e-9 above it and goes to zero the day the two trees accumulate a contraction identically; it is NOT a fitted margin (the outputs are each a fixed deterministic evaluation)"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- MG example: `fir_fit` — differentiable FIR coefficient fit, TRAINED and compared ------------
    # MG.S3. The first ported example whose graph carries a BACKWARD pass: `gradients(…)` (MG S2)
    # composes the frontend graph with the closed M7 autodiff, so loss + dloss/dw + dloss/dgain are named
    # outputs of one forward. Both trees run 400 SGD steps to the SAME reference kernel (the fixed point
    # the loss defines), so the effective taps `gain*w` converge to `1.5*kRef` on both sides; each program
    # prints those K taps and nothing else (compare_samples reads numbers only). ALLOWED is the MEASURED
    # gap between the two converged fits — see BECAUSE — not a fitted margin.
    add_executable(parity_fir_fit_here parity/fir_fit_here.cpp)
    target_link_libraries(parity_fir_fit_here PRIVATE heinzel heinzel_app)

    add_executable(parity_fir_fit_there parity/fir_fit_there.cpp)
    target_include_directories(parity_fir_fit_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_fir_fit_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_fir_fit
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_fir_fit_here>
                     -DTHERE=$<TARGET_FILE:parity_fir_fit_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=fir_fit
                     -DALLOWED=0.0
                     "-DBECAUSE=both trees fit the same K-tap reference kernel by 400 SGD steps through the compiled forward+adjoint graph, with the host update in float on both sides. Measured on glibc 2.39: all 5 effective taps gain*w bit-identical. The per-step arithmetic of the two trees is NOT known to be identical (tutorial_07's trajectories differ at the f32 ULP level); the descents land on the same f32 fixed point. History: 4.027e-07 was recorded here as 'float-vs-double accumulation and reduction order', and it was the `_here` host loop updating the taps in double with lr=0.2 where the old side uses float and 0.2f, feeding numbers no f32 pipeline holds. The input is generated with host libm sin on both sides, so another glibc descends from different data; re-measure there, do not widen blind"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- MG example: `mlp_train` — a 2-layer ReLU MLP TRAINED by SGD, and compared ------------------
    # MG.S4. The second ported example carrying a backward pass. Each layer is an einsum whose operands
    # carry the same labels (`"bih,bih->bh"`: a multiply and a sum, Decision 78) rather than the `_matmul`
    # `mlp_forward` used, so the M7 autodiff differentiates it; `gradients(…)` names loss + four
    # gradients. Both trees run the same deterministic 600-step SGD from the same init and print the loss
    # at the example's six sampled steps (0,120,…,600). ALLOWED is the MEASURED trajectory gap.
    # Its program runs 30 loop nests where kMaxStages is 16: `gradients` sweeps the graph once for each of its four
    # targets (B-5, Plan/backlog.md).
    add_executable(parity_mlp_train_here parity/mlp_train_here.cpp)
    target_link_libraries(parity_mlp_train_here PRIVATE heinzel heinzel_app)
    target_compile_definitions(parity_mlp_train_here PRIVATE HEINZEL_MAX_STAGES=32)

    add_executable(parity_mlp_train_there parity/mlp_train_there.cpp)
    target_include_directories(parity_mlp_train_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_mlp_train_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_mlp_train
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_mlp_train_here>
                     -DTHERE=$<TARGET_FILE:parity_mlp_train_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=mlp_train
                     -DALLOWED=3.0e-08
                     "-DBECAUSE=both trees run the SAME deterministic 600-step SGD from the SAME init through the compiled forward+backward MLP, with the host update in float on both sides; each prints the loss at the six sampled steps. Measured on glibc 2.39: 4 of 6 samples differ, each by one f32 step, and the largest, 2.98e-08, is sample 0 — the loss BEFORE any update — so it is the forward graph and not the descent: the old tree's matrix products fuse each product into an f32 partial sum, while this tree rounds each product to f32 and sums in double, rounding once (Decision 78; before it, 5 of 6 with the same largest), and the mean likewise. Each sampled loss is a fixed deterministic result, not a tuned margin. The allowance is the measured max over the six samples and goes to zero the day the two trees accumulate the contractions and the mean identically"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- MB example: `einops_pipeline` — rearrange split + mean reduce, run & compared ---------------
    # MB.S5b. The old example packs `[B, C*F, T]`, splits `(c f)` back apart with rearrange + sizes, then
    # takes an einops `mean` over f and t to a per-channel `[B, C]`. MB.S5a's notation lowers the split to
    # one `_reshape` and the mean to one `_reduce`; here the whole pipeline is RUN and output-compared. The
    # mapping carries a length-1 streamed axis (the whole-tensor discipline image_elementwise established).
    # ALLOWED is set from the MEASURED difference (see BECAUSE): a mean is a SUM then a divide, and the two
    # trees round the sum differently.
    add_executable(parity_einops_pipeline_here parity/einops_pipeline_here.cpp)
    target_link_libraries(parity_einops_pipeline_here PRIVATE heinzel)

    add_executable(parity_einops_pipeline_there parity/einops_pipeline_there.cpp)
    target_include_directories(parity_einops_pipeline_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_einops_pipeline_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_einops_pipeline
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_einops_pipeline_here>
                     -DTHERE=$<TARGET_FILE:parity_einops_pipeline_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=einops_pipeline
                     -DALLOWED=9.6e-07
                     "-DBECAUSE=the pipeline's only reduction is a mean over f and t — a SUM then a divide — and the split, being a reshape, moves no numbers and rounds nothing. So the whole difference is how the two trees round the reduction sum: the old tree accumulates it in f32 (the scalar backend rounds as it goes), while this tree's _reduce sums in double and rounds ONCE at the end. The two disagree by at most one f32 ULP at the output's magnitude — measured 9.53e-07 near 9.45 (the [8,16) ULP is 2^-20 = 9.537e-07), one ULP on the outputs that differ and zero on the rest. The allowance is that one ULP and goes to zero the day the two trees accumulate a reduction identically; it is NOT a fitted margin (each output is a fixed deterministic evaluation)"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- MB example: `interleaved_planar_audio` PLANAR core — per-channel gain + transpose round-trip --
    # MB.S5c. Ports the rearrange-grantable half of the old example: a per-channel gain over a logical
    # [C,T] value, reached directly AND through a `c t -> t c` / `t c -> c t` transpose round-trip that
    # MB.S5a's notation lowers to two inverse `_transpose`s. The here side prints both; the there side
    # prints the old tree's planar gain twice, so the round-trip is shown to compose to the identity
    # NUMERICALLY (every number returns home). Two of the old example's points are RESERVED inventory,
    # recorded in docs/decisions.md and NOT ported: the interleaved-physical `input_layout` (a run-surface
    # memory-layout feature) and the "inverse pair cancels to zero nodes" claim (an optimiser property the
    # capture layer does not have). ALLOWED is 0.0: the gains {0.5, 2.0} are exact powers of two so the
    # products are exact, and a transpose moves numbers without changing them, so both sides are bit-exact.
    add_executable(parity_interleaved_planar_here parity/interleaved_planar_here.cpp)
    target_link_libraries(parity_interleaved_planar_here PRIVATE heinzel)

    add_executable(parity_interleaved_planar_there parity/interleaved_planar_there.cpp)
    target_include_directories(parity_interleaved_planar_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_interleaved_planar_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_interleaved_planar
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_interleaved_planar_here>
                     -DTHERE=$<TARGET_FILE:parity_interleaved_planar_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=interleaved_planar
                     -DALLOWED=0.0
                     "-DBECAUSE="
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- MB example: `stencil_indexing_2d` UNFOLD idiom — 2-D element axes + _unfold, run & compared ---
    # MB.S6. Ports idiom 2 of the old example (the windowed view): a 3-tap vertical Laplacian
    # sum(unfold(x, 0, 3, 1) * taps, tap_axis) over the interior rows of an 8x12 image. It rests on the
    # 2-D element-axis surface (MB.S3) and the existing _unfold (M5, closed) — an unfold builder beside
    # reshape/transpose, today the member `x.unfold(…)`. Idiom 1 (per-axis subscript indexing) is RESERVED
    # (docs/decisions.md): it needs a multi-axis subscript frontend surface and an element-axis shift that
    # brushes the reserved innermost-axis _delay question. ALLOWED is set from the MEASURED reduction ULP.
    add_executable(parity_stencil_unfold_here parity/stencil_unfold_here.cpp)
    target_link_libraries(parity_stencil_unfold_here PRIVATE heinzel)

    add_executable(parity_stencil_unfold_there parity/stencil_unfold_there.cpp)
    target_include_directories(parity_stencil_unfold_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_stencil_unfold_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_stencil_unfold
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_stencil_unfold_here>
                     -DTHERE=$<TARGET_FILE:parity_stencil_unfold_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=stencil_unfold
                     -DALLOWED=2.4e-07
                     "-DBECAUSE=the stencil is a weighted SUM over 3 taps ({1,-2,1}); the unfold is a view, moving numbers and rounding nothing, so the whole difference is how the two trees round that 3-tap sum. The old tree folds it ascending-tap with fma_rn in f32; this tree's _reduce sums the products. The two disagree by at most one f32 ULP at the output's magnitude — measured 2.39e-07 near -2.99 (the [2,4) ULP is 2^-22 = 2.384e-07), on the taps that differ and zero on the rest. The allowance is that one ULP and goes to zero the day the two trees fold a reduction identically; it is NOT a fitted margin (each output is a fixed deterministic evaluation)"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- benchmark: `fusion` — a squaring producer fused into a 1-D conv, run and compared ---------
    # M8.3, S6 (fusion). benchmarks/fusion_bench.cpp's headline graph p = x*x ; y[i] = Σ_k p[i+k]·h[k].
    # A benchmark is a CORRECTNESS corpus here (amendment 03: measure shape, not timing). The here side
    # builds it in this tree's stencil idiom (unfold the data axis → weight → _reduce the window axis);
    # the there side is the bench's `fused()` verbatim (transpose(unfold)·reshape(h) summed over the tap
    # axis). Same conv, same squaring producer, same f32 inputs; the only freedom is the reduction ORDER
    # over the K taps — so ALLOWED is the measured one-ULP reduce-order gap with that cause named.
    add_executable(parity_fusion_here parity/fusion_here.cpp)
    target_link_libraries(parity_fusion_here PRIVATE heinzel)

    add_executable(parity_fusion_there parity/fusion_there.cpp)
    target_include_directories(parity_fusion_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_fusion_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_fusion
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_fusion_here>
                     -DTHERE=$<TARGET_FILE:parity_fusion_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=fusion
                     -DALLOWED=6.0e-08
                     "-DBECAUSE=the graph is p=x*x fed into a K=5-tap weighted sum; the square and the unfold are exact (a product and a view — rounding nothing), so the whole difference is how the two trees fold that 5-tap sum. The old tree folds it over the tap axis; this tree's _reduce folds the products over the window axis. The two disagree by at most one f32 ULP at the output magnitude — measured 5.96e-08 near -0.529 (the [0.5,1) ULP is 2^-24 = 5.96e-08), on the frames whose fold order matters and zero on the rest. The allowance is that one ULP and goes to zero the day the two trees fold a reduction identically; it is NOT a fitted margin (each frame is a fixed deterministic evaluation, fast-math off on both sides)"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- benchmark: `order_stats` — a sliding median, run and compared ------------------------------
    # M8.3, S6 (order_stats). benchmarks/order_stats_bench.cpp's graph median(unfold(x,W),-1). The here
    # side unfolds the data axis and takes `_order` Quantile(0.5) over the window axis; the there side is
    # the bench's median_filter(x,W) verbatim. W is ODD (5), so the median is the sorted-middle element —
    # an exact selection, no reassociable arithmetic — so ALLOWED is 0.0 and any difference is a real
    # regression, not an accounted tolerance.
    add_executable(parity_order_stats_here parity/order_stats_here.cpp)
    target_link_libraries(parity_order_stats_here PRIVATE heinzel)

    add_executable(parity_order_stats_there parity/order_stats_there.cpp)
    target_include_directories(parity_order_stats_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_order_stats_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_order_stats
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_order_stats_here>
                     -DTHERE=$<TARGET_FILE:parity_order_stats_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=order_stats
                     -DALLOWED=0.0
                     "-DBECAUSE=a sliding median over an ODD window (W=5) is the sorted-middle element, an exact selection. This tree resolves Quantile(0.5) as numpy does — p=(W-1)*0.5=2.0 is integral, so the interpolation weight is zero and it returns the middle element bit-for-bit, no arithmetic; the old tree's median(unfold(x,W),-1) picks the same middle element. A selection network reassociates nothing, so the two sides return the identical number and ALLOWED is 0.0. It stays 0.0 as long as both trees select the same rank; a non-zero difference would mean one tree changed which element the median is"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- benchmark: `channel_strip` — a saturating gain chain, run and compared ---------------------
    # M8.3, S6 (channel_strip). benchmarks/channel_strip_bench.cpp's g_strip(): in-gain -> three
    # cascaded tanh(d·s + b) drive stages -> out-gain -> hard clamp, pure elementwise. The here side
    # builds it with `tanh` (the M4 op::Tanh, same core graph_composition's single saturator uses) and
    # min/max; the there side is g_strip() verbatim. All gains and biases are the old bench's f32
    # literals on both sides, so the ONLY accounted difference is the two trees' tanh cores compounded
    # over THREE stages (graph_composition's single stage measured one f32 ULP). MEASURED below.
    add_executable(parity_channel_strip_here parity/channel_strip_here.cpp)
    target_link_libraries(parity_channel_strip_here PRIVATE heinzel)

    add_executable(parity_channel_strip_there parity/channel_strip_there.cpp)
    target_include_directories(parity_channel_strip_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_channel_strip_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_channel_strip
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_channel_strip_here>
                     -DTHERE=$<TARGET_FILE:parity_channel_strip_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=channel_strip
                     -DALLOWED=6.0e-08
                     "-DBECAUSE=every gain and bias is the old bench's f32 literal on both sides, so the arithmetic constants match, and two things differ. The old tree fuses each saturator's d*s+b into one fused multiply-add (its contract_fma) where this tree rounds the product and the sum separately; and the old tree's tanh is libm tanhf at run time, which is not correctly rounded and varies by glibc, where this tree runs its own core. Both sides are reproduced exactly, all 64 frames each, by a hand-written float model (fmaf + tanhf for the old side, plain arithmetic + a correctly rounded tanh for this one); on its own the fma changes 10 frames and tanhf 27. Measured on glibc 2.39: 29 of 64 frames differ, largest 5.970e-08 at frame 14, one f32 ULP near 0.505. The fma half goes to zero when this tree contracts; the tanhf half is the old side's libm. Not a fitted margin (each frame is a fixed deterministic evaluation, fast-math off on both sides)"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- benchmark: `cost_model_flip` — a separable blur, run and compared -------------------------
    # M8.3, S6 (cost_model_flip). benchmarks/cost_model_flip_bench.cpp's g_sep_blur: a Kp-tap horizontal
    # windowed reduce feeding a Kc-tap vertical windowed reduce. The bench's headline (the roofline
    # flipping fuse-vs-materialize) is a SCHEDULING property carried by the schedule gates; the ported
    # numeric content is the blur OUTPUT, schedule-invariant. The here side unfolds the W axis then the
    # row axis and reduces each window; the there side is g_sep_blur verbatim (unfold + unfold_rows). No
    # new op (M5 _unfold + _reduce). Input image and taps are varied/non-uniform so the blur isn't a
    # vacuous constant. The only freedom is the reduction ORDER over the taps → MEASURED reduce-order gap.
    add_executable(parity_cost_model_flip_here parity/cost_model_flip_here.cpp)
    target_link_libraries(parity_cost_model_flip_here PRIVATE heinzel)

    add_executable(parity_cost_model_flip_there parity/cost_model_flip_there.cpp)
    target_include_directories(parity_cost_model_flip_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_cost_model_flip_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_cost_model_flip
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_cost_model_flip_here>
                     -DTHERE=$<TARGET_FILE:parity_cost_model_flip_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=cost_model_flip
                     -DALLOWED=3.0e-08
                     "-DBECAUSE=the separable blur is the same set of Kp*Kc weighted products on both sides; the input image and both tap sets are identical f32 values, so the only freedom is the ORDER in which each window's products are folded (the old tree's ascending-tap f32 sum vs this tree's _reduce of the products, and the f32-executor-vs-this-tree-at-High (via double) rounding). MEASURED max 2.98e-08 near 0.353 (the [0.25,0.5) ULP is 2^-25 = 2.98e-08), on 16 of the 24 outputs and exactly zero on the rest -> discriminating, not vacuous. ALLOWED is that one ULP; it goes to zero the day the two trees fold a reduction identically, and is not a fitted margin (fixed deterministic inputs, fast-math off on both sides). The cost model's fuse-vs-materialize FLIP the bench headlines is a scheduling property (schedule gates), numerically neutral, so it does not appear in this numeric parity"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- benchmark: `image_pipeline` / vblur — squared producer + vertical conv, run and compared ---
    # M8.3, S6 (image_pipeline, vblur family). image_pipeline_bench.cpp's g_vblur_fused: p = x*x feeding
    # a K-tap vertical (row) windowed reduce. The bench's headline (fusing the square into the conv) is a
    # scheduling property; the ported content is the numeric output. Here: op::Mul (M4) + M5 _unfold on
    # the row axis + _reduce Sum; there g_vblur_fused verbatim (unfold_rows). No new op. Varied image and
    # non-uniform taps. The only freedom is the reduction ORDER over the taps → MEASURED reduce-order gap.
    # (The image_pipeline families hconv and blurxy are already covered — hconv is the fusion graph, blurxy
    # is cost_model_flip's g_sep_blur — see plan/M8-S6-NOTES.md; only vblur/unsharp/harris are new.)
    add_executable(parity_vblur_here parity/vblur_here.cpp)
    target_link_libraries(parity_vblur_here PRIVATE heinzel)

    add_executable(parity_vblur_there parity/vblur_there.cpp)
    target_include_directories(parity_vblur_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_vblur_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_vblur
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_vblur_here>
                     -DTHERE=$<TARGET_FILE:parity_vblur_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=vblur
                     -DALLOWED=2.5e-07
                     "-DBECAUSE=the squared producer p=x*x is exact (a product rounds nothing beyond its own f32 round) and identical on both sides, so each output is the same set of K weighted products; the only freedom is the ORDER in which the window's products are folded (the old tree's ascending-tap f32 fma sum vs this tree's _reduce, plus f32-executor-vs-this-tree-at-High (via double) rounding). MEASURED max 2.39e-07 near 2.105 (the [2,4) ULP is 2^-22 = 2.384e-07) — one f32 ULP at the output magnitude, on 16 of 48 outputs and zero on the rest. The magnitude is larger than the other blur ports because squaring makes the summed values larger, so one ULP is a larger absolute number; it is still exactly one ULP and goes to zero the day the trees fold identically. Not a fitted margin (fixed deterministic inputs, fast-math off both sides)"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- benchmark: `image_pipeline` / unsharp — vertical blur + unsharp combine, run and compared --
    # M8.3, S6 (image_pipeline, unsharp family). image_pipeline_bench.cpp's g_unsharp: a K-tap vertical
    # blur b, then y = (1+alpha)*center(x) - alpha*b, where center(x) is x cropped to the K/2 row band
    # that aligns with the valid blur output. Here: M5 _unfold/_reduce for the blur, M5 _slice on the row
    # axis for the center crop, M4 mul/sub for the combine; there g_unsharp verbatim (unfold_rows + a flat
    # slice+reshape crop). No new op. Taps sum to 1 so both the crop and blur terms matter — a wrong crop
    # offset would move y everywhere. The freedom is the blur's reduction ORDER → MEASURED reduce-order gap.
    add_executable(parity_unsharp_here parity/unsharp_here.cpp)
    target_link_libraries(parity_unsharp_here PRIVATE heinzel)

    add_executable(parity_unsharp_there parity/unsharp_there.cpp)
    target_include_directories(parity_unsharp_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_unsharp_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_unsharp
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_unsharp_here>
                     -DTHERE=$<TARGET_FILE:parity_unsharp_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=unsharp
                     -DALLOWED=2.5e-07
                     "-DBECAUSE=the center crop and the (1+alpha)*xc - alpha*b combine are exact and identical on both sides, so the only freedom is the blur b's reduction ORDER (the old tree's ascending-tap f32 fma sum vs this tree's _reduce, plus f32-executor-vs-this-tree-at-High (via double) rounding). MEASURED max 2.39e-07 near 2.123 (the [2,4) ULP is 2^-22 = 2.384e-07) — one f32 ULP at the output magnitude, on 4 of 32 outputs and bit-identical on the rest. The close match on the crop-dominated values also confirms the center-crop ALIGNMENT (a wrong K/2 offset would move y by whole pixels, not one ULP — as an off-by-one crop did before the corpus was sized). ALLOWED is that one ULP; it goes to zero the day the trees fold the blur identically. Not a fitted margin (fixed deterministic inputs, fast-math off both sides)"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- benchmark: `image_pipeline` / harris — 2-D corner response, run and compared --------------
    # M8.3, S6 (image_pipeline, harris family — the last new one). image_pipeline_bench.cpp's g_harris:
    # two 3x3 gradient stencils dx/dy -> products ixx/ixy/iyy -> three 3x3 box sums sxx/sxy/syy -> the
    # Harris response sxx*syy - sxy*sxy - 0.04*(sxx+syy)^2. It is the one family that exercises the 2-D
    # windowed view op::Unfold2d (M5, runtime + rearrange2d_test/view_inference2d_test) under numeric
    # pressure, twice nested. The here side emits Unfold2d with a local helper (the contract_here
    # precedent — op under pressure before the notation surface reaches it), folds the two window axes at
    # {1,3}, and reuses one unfold2d(x) for dx/dy; the rest is ordinary frontend. there is g_harris
    # verbatim. No new op. Sobel-like wx/wy so dx/dy differ (a real corner measure). The response's
    # subtraction of near-equal products amplifies the reduce-order rounding → MEASURED gap.
    add_executable(parity_harris_here parity/harris_here.cpp)
    target_link_libraries(parity_harris_here PRIVATE heinzel)

    add_executable(parity_harris_there parity/harris_there.cpp)
    target_include_directories(parity_harris_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_harris_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_harris
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_harris_here>
                     -DTHERE=$<TARGET_FILE:parity_harris_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=harris
                     -DALLOWED=2.5e-03
                     "-DBECAUSE=every windowed sum (the two gradient stencils and the three box sums) is the same set of products on both sides; the freedom is the reduction ORDER at each stage plus f32-executor-vs-this-tree-at-High (via double) rounding. The Harris response sxx*syy - sxy*sxy - 0.04*(sxx+syy)^2 then SUBTRACTS large near-equal products — sxx*syy and sxy*sxy are each ~1e6 here and cancel to an output ~6e3 — so a per-stage rounding of a few units in the last place is amplified by that cancellation into the output's low bits. MEASURED max 1.954e-03 at 6168 (a RELATIVE 3.17e-07, about 4 ULP of the [4096,8192) result whose ULP is 2^-11 = 4.88e-04), on 3 of the 4 outputs and bit-identical on the fourth. ALLOWED 2.5e-03 covers it with a small margin; it is a reduction-order+cancellation rounding, not a disagreement about the operation (a wrong kernel/fold/response moves the output by thousands, as an early mis-wired version did). Not a fitted margin (fixed deterministic inputs, fast-math off both sides)"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- benchmark: `apps` / softmax — a stable row softmax, run and compared ----------------------
    # M8.3, S6 (apps, softmax arm). apps_bench.cpp's g_softmax: e=exp(x-max_c(x)); y=e/sum_c(e) over the
    # columns of an [M,K] matrix, max/sum with keepdims. The one apps arm that combines the exp
    # transcendental with row reductions and is not already covered (biquad is parity_biquad, the MLP is
    # mlp_forward). Here: M4 op::Exp + M5 _reduce Max/Sum with keepdims + M4 sub/div; there g_softmax
    # verbatim. No new op. The max-shift and division are exact; the freedom is the two trees' exp cores
    # and the length-K sum's reduction ORDER → MEASURED gap.
    add_executable(parity_softmax_here parity/softmax_here.cpp)
    target_link_libraries(parity_softmax_here PRIVATE heinzel)

    add_executable(parity_softmax_there parity/softmax_there.cpp)
    target_include_directories(parity_softmax_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_softmax_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_softmax
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_softmax_here>
                     -DTHERE=$<TARGET_FILE:parity_softmax_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=softmax
                     -DALLOWED=3.0e-08
                     "-DBECAUSE=the max-shift (x - max_c) and the final divide are exact and identical on both sides, so the accounted difference is the two trees' exp cores (this tree's op::Exp vs the old tree's scalar exp) and the length-K sum's reduction ORDER, plus f32-executor-vs-this-tree-at-High (via double) rounding. MEASURED max 2.98e-08 near 0.251 (the [0.25,0.5) ULP is 2^-25 = 2.98e-08) — one f32 ULP at the output magnitude, on 18 of 24 outputs and bit-identical on the rest. Every row sums to 1 (a genuine distribution). ALLOWED is that one ULP; it goes to zero the day the two trees share an exp core and fold the sum identically. Not a fitted margin (fixed deterministic inputs, fast-math off both sides)"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- benchmark: `apps` / rmsnorm — RMS normalization, run and compared -------------------------
    # M8.3, S6 (apps, rmsnorm arm). apps_bench.cpp's g_rmsnorm: y = x / sqrt(mean_c(x^2)+eps) over the
    # columns of an [M,K] matrix, mean with keepdims. A DIFFERENT transcendental core from softmax's exp
    # (M4 op::Sqrt) over a row mean of squares. Here: M4 op::Sqrt + M5 _reduce Mean with keepdims + M4
    # square/add/div; there g_rmsnorm verbatim. No new op. The square/add/divide are exact; the freedom is
    # the two trees' sqrt cores and the length-K mean's reduction ORDER → MEASURED gap.
    add_executable(parity_rmsnorm_here parity/rmsnorm_here.cpp)
    target_link_libraries(parity_rmsnorm_here PRIVATE heinzel)

    add_executable(parity_rmsnorm_there parity/rmsnorm_there.cpp)
    target_include_directories(parity_rmsnorm_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_rmsnorm_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_rmsnorm
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_rmsnorm_here>
                     -DTHERE=$<TARGET_FILE:parity_rmsnorm_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=rmsnorm
                     -DALLOWED=1.5e-07
                     "-DBECAUSE=the square, the eps add and the final divide are exact and identical on both sides, so the accounted difference is the two trees' sqrt cores (this tree's op::Sqrt vs the old tree's scalar sqrt) and the length-K mean's reduction ORDER, plus f32-executor-vs-this-tree-at-High (via double) rounding. MEASURED max 1.20e-07 near 1.219 (the [1,2) ULP is 2^-23 = 1.19e-07) — one f32 ULP at the output magnitude, on 10 of 24 outputs and bit-identical on the rest. ALLOWED 1.5e-07 is that one ULP with a small margin; it goes to zero the day the two trees share a sqrt core and fold the mean identically. Not a fitted margin (fixed deterministic inputs, fast-math off both sides)"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- benchmark: `apps` / cascade4 — a four-section biquad cascade, run and compared -------------
    # M8.3, S6 (apps, cascade4 arm). apps_bench.cpp's g_cascade4: four CHAINED second-order recurrences
    # y1←x, y2←y1, y3←y2, y4←y3, only y4 an output (y1..y3 internal `variable()` states). The distinct
    # S6 content is a chained recurrence scan — one section's current sample feeds the next within the
    # same step — which biquad/dline (a single recurrence) do not exercise. Here: `Variable` chain via
    # the frontend; there: g_cascade4 verbatim via `x.variable()`. Every coefficient is the old bench's
    # f32 literal on both sides; fast-math off both sides. MEASURED below.
    add_executable(parity_cascade4_here parity/cascade4_here.cpp)
    target_link_libraries(parity_cascade4_here PRIVATE heinzel)

    add_executable(parity_cascade4_there parity/cascade4_there.cpp)
    target_include_directories(parity_cascade4_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_cascade4_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_cascade4
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_cascade4_here>
                     -DTHERE=$<TARGET_FILE:parity_cascade4_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=cascade4
                     -DALLOWED=7.0e-08
                     "-DBECAUSE=every coefficient is the old bench's f32 literal on both sides and the four sections are accumulated in the same left-to-right order, so there is no reassociation freedom (fast-math off both sides); the accounted difference is that this tree at High computes the recurrences in f64 while the old tree's scalar executor runs them in f32, and that per-sample rounding COMPOUNDS through four CHAINED second-order feedback loops (each section reads the current sample of the one before it, so an early-section rounding propagates forward). MEASURED max 5.96e-08 at the y4 output magnitude ~0.193 (about four ULP at that magnitude, the accumulation of the four loops' f32 roundings), on 40 of 64 samples and bit-identical on the rest — discriminating, not vacuous: a misordered or mis-fed section shows gross diffs, not this bounded ULP-scale gap. ALLOWED 7.0e-08 is the measured max with a small headroom; it goes to zero the day both trees compute the cascade in the same precision. Not a fitted margin (fixed deterministic input, fast-math off both sides, single-threaded scalar — no nondeterminism to average out)"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- example: `physical_dsp` — a delay written as a physical duration, run and compared --------
    # M8.3, S5 (physical_dsp c2). A feedback comb y[n] = x[n] + 0.5·y[n−D] whose delay D is the
    # duration 3 ms, folded to the integer lag 6 at 2 kHz at compile time — the here side by the named
    # durations' `to_samples` (physical_dsp c1), the there side by the old tree's own `units/` layer and
    # `g.samples`. So the compared thing is the duration delay feature end to end: both folds must reach 6
    # or the graphs are not the same recurrence. The impulse response is 0.5^k at n = 6k — exact powers of
    # two, representable identically in this tree's arithmetic and the old tree's f32 executor — so
    # ALLOWED is 0.0 and MEASURED: a difference of any size is a real regression, not an accounted
    # tolerance. The refusals (a sub-sample duration, an unset rate) are named_units_test.cpp, on the same
    # `to_samples` fold this example rests on.
    add_executable(parity_physical_dsp_here parity/physical_dsp_here.cpp)
    target_link_libraries(parity_physical_dsp_here PRIVATE heinzel)

    add_executable(parity_physical_dsp_there parity/physical_dsp_there.cpp)
    target_include_directories(parity_physical_dsp_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_physical_dsp_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_physical_dsp
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_physical_dsp_here>
                     -DTHERE=$<TARGET_FILE:parity_physical_dsp_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=physical_dsp
                     -DALLOWED=0.0
                     "-DBECAUSE="
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- example: `oscillator` — a phase-accumulator sine, run and compared ------------------------
    # M8.3, S5 (oscillator c2). The audio oscillator is a RECURRENCE: phase[n] = frac(phase[n-1] + inc)
    # with a waveform applied to the wrapped phase, so one graph covers fixed pitch (constant inc, as
    # here) and FM. The here side builds it with the opt-in `heinzel::dsp::oscillator` layer over this
    # tree's recurrence frontend and the `frac`/`sinpi` spellings (oscillator c1); the there side is the
    # old tree's own `dsp::oscillator` with the sine waveform. The old example renders it block-by-block
    # with streaming AND once over the whole buffer and checks the two agree; this tree has no streaming
    # (ch04), so the ported form is the whole-buffer run — the old example's OWN `whole` reference — and
    # this cross-tree compare stands in for that internal check plus the old example's analytic one.
    #
    # WHY THE ALLOWANCE IS 2.5e-07 AND WHAT ACCOUNTS FOR IT. Both sides store the phase in single
    # precision and feed the identical float increment, so the graphs are the same recurrence; the
    # residual is that the two trees compute the sine with DIFFERENT cores — this tree's op::SinPi exact
    # core (runtime/periodic_cores.hpp) against the old tree's dsp sine — and the phase wrap `x-floor(x)`
    # accordingly rounds a hair differently, and the recurrence accumulates those per-sample roundings
    # over the 512-sample buffer. Both cores are machine-accurate at the Precise tier, so each sample
    # agrees to within a couple of single-precision ulp; MEASURED largest difference 1.937e-07 at these
    # magnitudes (amplitude 0.5). Unlike ch06's coarse tiers — deliberately NOT cross-compared because a
    # Fast core here and there are two different approximations — the exact tier IS comparable: two
    # machine-accurate renderings of the true oscillator must agree to machine precision, and this checks
    # they do. The allowance goes to zero only if the two trees ever share one sine core.
    add_executable(parity_oscillator_here parity/oscillator_here.cpp)
    target_link_libraries(parity_oscillator_here PRIVATE heinzel)

    add_executable(parity_oscillator_there parity/oscillator_there.cpp)
    target_include_directories(parity_oscillator_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_oscillator_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_oscillator
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_oscillator_here>
                     -DTHERE=$<TARGET_FILE:parity_oscillator_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=oscillator
                     -DALLOWED=2.5e-07
                     "-DBECAUSE=both sides store the phase in single precision and feed the identical float increment, so the recurrence is the same; the residual is the two trees' different sine cores (this tree's op::SinPi exact core in runtime/periodic_cores.hpp vs the old tree's dsp sine) and the phase-wrap rounding that accumulates over 512 samples. Both are machine-accurate at the Precise tier, so each sample agrees to a couple of single-precision ulp; measured largest difference 1.937e-07 at amplitude 0.5"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- benchmark: `oscillator` / affine — the loop-invariant-pitch arm, run and compared ---------
    # M8.3, S6 (oscillator, affine arm). oscillator_bench.cpp's g_affine_sine: sine(frac(iota·inc))·amp
    # with a SCALAR inc — a pure elementwise phase ramp, no recurrence. Distinct from the FM/recurrence
    # arm the `oscillator` S5 parity already covers: the recurrence accumulates inc over n additions
    # while the affine path forms n·inc with one multiply, so the two phases (equal in exact arithmetic)
    # round differently — the contrast the bench keeps both arms for. Here: `dsp::affine_oscillator`
    # (added to the ported dsp layer, composing iota/frac/sinpi — no new op); there: g_affine_sine
    # verbatim via the old tree's own `dsp::affine_oscillator`. inc a constant (= the bench's loop-
    # invariant param) so both sides are input-free generators. fast-math off both sides. MEASURED below.
    add_executable(parity_oscillator_affine_here parity/oscillator_affine_here.cpp)
    target_link_libraries(parity_oscillator_affine_here PRIVATE heinzel)

    add_executable(parity_oscillator_affine_there parity/oscillator_affine_there.cpp)
    target_include_directories(parity_oscillator_affine_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_oscillator_affine_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_oscillator_affine
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_oscillator_affine_here>
                     -DTHERE=$<TARGET_FILE:parity_oscillator_affine_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=oscillator_affine
                     -DALLOWED=2.5e-07
                     "-DBECAUSE=both sides form the phase as frac(n·inc) with the identical f32 increment and take the sine of it scaled by 0.5 — no recurrence, no reassociation (fast-math off both sides). The accounted difference is two things the affine path exposes: n·inc is formed by ONE multiply, rounded in f32 by the old executor and in f64 by this tree at High (inc = 1234/48000 is not a power of two, so the product genuinely rounds), and the two trees apply DIFFERENT sine cores (this tree's op::SinPi exact core vs the old tree's dsp sine). MEASURED max 1.71e-07 at output magnitude 0.064 (about 3 ULP of the 0.5 amplitude scale, the same order as the recurrence `oscillator` parity's 1.94e-07), on 53 of 96 samples and bit-identical on the rest — discriminating, and it spans ~2.5 frac wraps so the wrap is exercised, not skipped. ALLOWED 2.5e-07 matches the sibling oscillator parity's single-precision two-sine-core allowance; it goes to zero the day the two trees share a sine core. Not a fitted margin (fixed deterministic generator, fast-math off both sides)"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- dynamic_length: a runtime-resolved streamed extent, ported and self-checked (MD.4) ------
    # The old `examples/dynamic_length.cpp` runs an elementwise block effect `y = x*gain + bias` over a
    # stereo block whose length N is a RUNTIME value fixed once per prepare() — the audio-plugin
    # prepareToPlay() shape. The `_here` port builds the same effect in this tree's grown frontend
    # (a `Length` mints the symbol, `Executor`'s prepare validates the bound size and runs the
    # N-independent program for N samples) at N=256 and N=64 through ONE compiled graph. Both files are VALUE-SELF-CHECKING, so MD.4's parity is `same PASS + same arena story`, not
    # a sample dump: `dynamic_length_selfcheck.cmake` requires both exit 0 and both print the same STORY
    # line. The two trees' arena-size integers are not compared — the arena models differ, as
    # compare.cmake explains for cross-tree artifacts.
    add_executable(parity_dynamic_length_here parity/dynamic_length_here.cpp)
    target_link_libraries(parity_dynamic_length_here PRIVATE heinzel)

    add_executable(parity_dynamic_length_there parity/dynamic_length_there.cpp)
    target_include_directories(parity_dynamic_length_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(parity_dynamic_length_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME parity_dynamic_length
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:parity_dynamic_length_here>
                     -DTHERE=$<TARGET_FILE:parity_dynamic_length_there>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=dynamic_length
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/dynamic_length_selfcheck.cmake)

    # ---- the tutorial chapter programs, each RUN and output-compared (M8.3, S5) ------------------
    # M8.3 asks the twelve tutorial chapter programs be RUN, not merely built — the eleven .cpp
    # chapters here plus 12_python.py in M9.1. Each ported chapter is a `_here` program in this
    # tree's language and a `_there` program on the old tree at c4012b8, run and compared numerically
    # exactly as the examples are (the tutorials ARE the widest cross-tree corpus, extending O1). The
    # per-chapter disposition — which are portable now, which lean on a by-need layer, a reserved
    # boundary, or the autodiff-frontend gap — is docs/decisions.md Decision 27.
    #
    # Chapter 01 (getting started): y = a*x + 1. Pure elementwise; `a` is a runtime knob spelled as a
    # named input fed a constant, as the conditionals port established. The whole difference is the
    # old tree's fma contraction of a*x+1, the same cause the biquad names; ALLOWED is MEASURED.
    add_executable(tutorial_01_here tutorial/tutorial_01_here.cpp)
    target_link_libraries(tutorial_01_here PRIVATE heinzel)

    add_executable(tutorial_01_there tutorial/tutorial_01_there.cpp)
    target_include_directories(tutorial_01_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(tutorial_01_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME tutorial_01_getting_started
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:tutorial_01_here>
                     -DTHERE=$<TARGET_FILE:tutorial_01_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=tutorial_01
                     -DALLOWED=0.0
                     "-DBECAUSE=MEASURED bit-exact over all 256 samples (largest difference 0.000e+00). y = a*x + 1 is spelled with the same constants in the same order on both sides, and with fast-math off the old tree's optimizer does not contract a*x+1 into an fma on the scalar backend here — so there is no accounted difference to allow, and the allowance is 0.0. Should this tree or the old tree's scheduling ever fold the mul-add differently the difference would be one f32 ULP at the output magnitude; today it is nothing"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # Chapter 02 (the DSL): four small graphs. Grown one graph-family at a time (S5). Graph 1 is the
    # elementwise math graph — the transcendental family (tanh/sqrt/log) is the only accounted
    # difference between the two trees' scalar backends; ALLOWED is MEASURED.
    add_executable(tutorial_02_here tutorial/tutorial_02_here.cpp)
    target_link_libraries(tutorial_02_here PRIVATE heinzel)

    add_executable(tutorial_02_there tutorial/tutorial_02_there.cpp)
    target_include_directories(tutorial_02_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(tutorial_02_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    # Chapter 03 (reductions and einsum): three graph-families, grown one at a time (S5). Graph 1a is
    # the axis reductions (rowsum/rowmax/cent) — the only accounted difference is the mean's reduction
    # rounding; ALLOWED is MEASURED.
    add_executable(tutorial_03_here tutorial/tutorial_03_here.cpp)
    target_link_libraries(tutorial_03_here PRIVATE heinzel)

    add_executable(tutorial_03_there tutorial/tutorial_03_there.cpp)
    target_include_directories(tutorial_03_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(tutorial_03_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME tutorial_03_reductions_einsum
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:tutorial_03_here>
                     -DTHERE=$<TARGET_FILE:tutorial_03_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=tutorial_03
                     -DALLOWED=1.91e-06
                     "-DBECAUSE=MEASURED. Graph 1 (axis reductions rowsum/rowmax/cent + softmax) is bit-exact — the mean's reduction rounding and softmax's exp/sum/divide match the old tree's scalar backend exactly. Graph 2 (the einsum contractions: c = ik,kj->ij matmul, ct = transposed output, arow = ik->i unary reduce) carries ONE accounted difference, the einsum_contractions cause: a contraction is a sum of products, and this tree accumulates the exact products in double and rounds ONCE to f32 while the old tree accumulates in f32 (rounding at each addition). That is a reduction-order rounding difference, one f32 ULP at the output magnitude — measured 1.90e-06 on 4 of the arow/matmul rows near magnitude 16.8, where a ULP is 2^-19. The allowance is that one ULP (1.91e-06), NOT a fitted margin, and goes to zero the day the two accumulate the same way. Graph 3 (einops: rearrange split 'b (c f) t -> b c f t' + mean over f,t) carries a SMALLER reduction-rounding difference of the same class (measured max ~4.8e-07), well within the graph-2 allowance"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    add_test(NAME tutorial_02_the_dsl
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:tutorial_02_here>
                     -DTHERE=$<TARGET_FILE:tutorial_02_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=tutorial_02
                     -DALLOWED=1.91e-06
                     "-DBECAUSE=MEASURED, the full chapter (4 graph-families, 227 samples). Streams: graph 1 (elementwise math), graph 2a (relu_like = where(x>thr,x,0)), graph 2b (the 0/1 mask), graph 3 (broadcasting, y = gain*x + bias with per-column bias broadcast over rows), graph 4a (views: xt = materialised transpose, xs = reshape+slice, ramp = iota*0.5), graph 4b (the FIR: unfold+kernel+sum). Graphs 1-2, 4a and 4b are bit-exact (transcendentals measured to round identically; where/select/mask/views/the 3-tap reduction all exact here) (transcendentals tanh/sqrt/log measured to round identically on the two scalar backends; where/select and the mask exact by construction). Graph 3 carries ONE accounted difference, the same cause as image_elementwise: the old tree contracts gain*x+bias into a single-rounded fmaf at op-eval while this tree evaluates a separately-rounded multiply then add (no auto-fusion; op::Fma is explicit or not at all). That is one f32 ULP at the output magnitude — measured 1.90e-06 on 1 of the 32 pixels near magnitude 27.8 (bias up to 70), where a ULP is 2^-19. The allowance is that one ULP (1.91e-06) and goes to zero the day this tree contracts a*b+c the same way. NOT a fitted margin"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # Chapter 08 (autoscheduling and deployment): the row RMS-normalize the old chapter pins under its
    # two machine models. The SUBJECT re-expresses in this tree's own spelling — two machines, Executor
    # (Decision 27, S5c); the c1 step here is the numeric spine. The accounted difference is the mean accumulated in f32 on the old tree
    # versus in double here; ALLOWED is MEASURED.
    add_executable(tutorial_08_here tutorial/tutorial_08_here.cpp)
    target_link_libraries(tutorial_08_here PRIVATE heinzel)

    add_executable(tutorial_08_there tutorial/tutorial_08_there.cpp)
    target_include_directories(tutorial_08_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(tutorial_08_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME tutorial_08_autoscheduling_deployment
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:tutorial_08_here>
                     -DTHERE=$<TARGET_FILE:tutorial_08_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=tutorial_08
                     -DALLOWED=1.25e-07
                     "-DBECAUSE=MEASURED (48 samples, the row RMS-normalize y = x * rsqrt(mean(x*x over the columns) + 1e-6f), the same f32 epsilon on both sides). The one accounted difference is the accumulator of the mean over the 8 columns: the old tree sums in f32, this tree sums in double and rounds the mean once. Each side is reproduced exactly, all 48 samples, by a hand-written float model that differs from the other only in that accumulator, with the reciprocal root rounded once, (float)(1/sqrt((double)t)), on both sides. Measured on glibc 2.39: 8 of 48 samples differ, all in the last of the six rows, largest one f32 ULP on [1,2) (1.19e-07, which the ten-digit sample dump shows as 1.200e-07) at sample 46 near 1.06. The allowance is that one ULP as dumped and goes to zero when both trees accumulate the mean alike. The chapter's SUBJECT (one graph scheduled for two machine models, byte-identical across runners, the pinned compile<> deployment) has no cost-model autoscheduler to compare across the trees; it is re-expressed in this tree's own spelling on the here side (S5c c2/c3), which does not change these printed samples"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- tutorial chapter 04 — recurrences and streaming (M8.3, S5) --------------------------------
    # The reachable core of the chapter: section (1)'s leaky one-pole recurrence, run whole-buffer and
    # output-compared to the same recurrence on the old tree. The old chapter's seeded-initial-history
    # and cross-run() block-streaming knobs have no counterpart in this tree's whole-block runtime and
    # are re-expressed on the here side (Decision 27); the RBJ biquad cascade (section 4) waits on the
    # opt-in dsp/ subtree (S4 by-need). The accounted difference is the recurrence evaluated in f32 on
    # the old tree versus f64 here; ALLOWED is MEASURED.
    add_executable(tutorial_04_here tutorial/tutorial_04_here.cpp)
    target_link_libraries(tutorial_04_here PRIVATE heinzel)

    add_executable(tutorial_04_there tutorial/tutorial_04_there.cpp)
    target_include_directories(tutorial_04_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(tutorial_04_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME tutorial_04_recurrences_streaming
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:tutorial_04_here>
                     -DTHERE=$<TARGET_FILE:tutorial_04_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=tutorial_04
                     -DALLOWED=1.2e-07
                     "-DBECAUSE=MEASURED (96 samples: 48 of the section-(1) leaky one-pole y[n] = (1-a)*x[n] + a*y[n-1] with a=0.85f, then 48 of the section-(3) internal-Variable two-pole chain s[n]=0.7f*x+0.3f*s[n-1]; y[n]=0.6f*s+0.4f*y[n-1]; the same f32 coefficients on both sides). The one accounted difference: the old tree fuses each recurrence's multiply-add into one fused multiply-add (its contract_fma) where this tree rounds the product and the sum separately, per operation, in f32. Both sides are reproduced exactly, all 96 samples each, by a hand-written float model (with fmaf for the old side, plain for this one). Through the feedback loop a one-ULP difference persists and decays by the pole. Measured on glibc 2.39: 22 of 96 samples differ; leaky max 2.98e-08, two-pole max 1.190e-07 at sample 53 (one f32 ULP near 1.06). The allowance is that one ULP and goes to zero the day this tree contracts a*b+c the same way. History: 4.0e-07 was recorded here with a '1/(1-a) accumulation of f32-vs-f64 evaluation' bound; this tree does not evaluate in f64 and round at the print (it rounds per operation), and most of the old difference was the `_here` graph carrying 0.85/0.7/0.3/0.6/0.4 as doubles. The chapter's seeded-initial-history and cross-run() block-streaming knobs have no counterpart in this tree's whole-block runtime and are re-expressed on the here side (Decision 27); the RBJ biquad cascade (section 4) waits on the opt-in dsp/ layer"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # Chapter 07 (autodiff): fir_fit's sibling — the differentiable FIR without the redundant gain, so
    # the K taps recover the reference DIRECTLY. Ported under MG (MG.5): each layer's backward is the
    # M7 autodiff via gradients(…); the FIR is the weight-and-reduce lowering (not the _matmul
    # einsum), run on Executor at High. Both trees descend to the SAME wref with the host update in
    # float on both sides; ALLOWED is MEASURED and BECAUSE says what was excluded as its cause.
    add_executable(tutorial_07_here tutorial/tutorial_07_here.cpp)
    target_link_libraries(tutorial_07_here PRIVATE heinzel heinzel_app)

    add_executable(tutorial_07_there tutorial/tutorial_07_there.cpp)
    target_include_directories(tutorial_07_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(tutorial_07_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME tutorial_07_autodiff
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:tutorial_07_here>
                     -DTHERE=$<TARGET_FILE:tutorial_07_there>
                     -DCOMPARE=$<TARGET_FILE:compare_samples>
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}
                     -DTAG=tutorial_07
                     -DALLOWED=1.2e-08
                     "-DBECAUSE=both trees fit the same K=8 FIR reference kernel wref by 4000 SGD steps through the compiled forward+backward graph, with the host update in float on both sides. Measured on glibc 2.39: 5 of 8 taps differ, largest 1.117e-08 at tap 5 (3 f32 ULP near 0.05). The two trajectories already differ at the f32 ULP level after 40 steps (measured, at most 7.5e-09), so the per-step arithmetic differs. Excluded by switching each off on the old side with the result unchanged to the last digit: its contract_fma and reciprocal_div graph passes, fast-math, and its SIMD backend. What remains, and is NOT isolated further, is accumulation inside the old tree's contraction and reduction executors against this tree's strict left-to-right double accumulator. History: 2.772e-06 was recorded here, and nearly all of it was the `_here` host loop updating the taps in double with lr=0.04 where the old side uses float and 0.04f"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/compare.cmake)

    # ---- tutorial chapter 05 — the RUNTIME-DIMENSION section, ported and self-checked (MD.5) ------
    # Tutorial 05 has three sections. The two shape-generic FIR sections build a STREAMING FIR whose
    # unfold is a lag along the streamed axis — the reserved innermost-axis `_delay` question — so they
    # are recorded reserved (plan/MD-S5-NOTES.md + docs/decisions.md Decision 32 revision), NOT built.
    # This ports the third — "dynamic dimensions": `y = x*gain` over a 1-D block whose length N is
    # declared with constraints and resolved at prepare(), run at N=64 and N=96. Pure elementwise scale,
    # no lag, so streaming along the runtime-resolved innermost N is clean — the MD runtime-extent
    # surface. Both sides are value-self-checking (the tutorial's own check()), so parity is same-PASS:
    # both recover y = 2.5*x at both lengths. `gain` is a named input fed a constant (this tree has no
    # runtime Param), as the dynamic_length port established.
    add_executable(tutorial_05_here tutorial/tutorial_05_here.cpp)
    target_link_libraries(tutorial_05_here PRIVATE heinzel)

    add_executable(tutorial_05_there tutorial/tutorial_05_there.cpp)
    target_include_directories(tutorial_05_there PRIVATE "${HEINZEL_OLD_TREE}/include")
    target_compile_options(tutorial_05_there PRIVATE ${HEINZEL_OLD_SIDE_OPTIONS} ${HEINZEL_CONSTEXPR_BUDGET_FLAG})

    add_test(NAME tutorial_05_shape_generic_and_dynamic
             COMMAND ${CMAKE_COMMAND}
                     -DHERE=$<TARGET_FILE:tutorial_05_here>
                     -DTHERE=$<TARGET_FILE:tutorial_05_there>
                     -DTAG=tutorial_05
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/parity/selfcheck_pair.cmake)

elseif(HEINZEL_PARITY)
    # Fatal rather than a warning: a suite that drops its only external check and still goes green
    # reads as if it had run one. A tarball or a shallow clone without the commit says so explicitly.
    if(NOT GIT_FOUND)
        set(_why "git was not found")
    elseif(NOT _extract_rc EQUAL 0)
        set(_why "${HEINZEL_PARITY_REF} could not be extracted: ${_extract_err}")
    else()
        set(_why "${HEINZEL_PARITY_REF} has no include/heinzel/frontend/builder.hpp, so it is not the old tree")
    endif()
    message(FATAL_ERROR "heinzel: no parity reference — ${_why}. Configure with -DHEINZEL_PARITY=OFF "
                        "to build without the parity pairs.")
else()
    message(STATUS "heinzel: HEINZEL_PARITY is OFF — the parity pairs are NOT part of this build")
endif()

# ---- tutorial chapter 06 — debugging and numerics, part (3): the numerics tiers (M8.3, S5) -------
# Self-contained, NOT gated on the old tree: the coarse tiers are each tree's own minimax/series cores
# (this tree's live in runtime/periodic_cores.hpp), so a Fast-tier sin here and there are two DIFFERENT
# approximations by design — there is no cross-tree golden for them. The chapter's subject is that each
# tier stays inside the bound THIS tree declares for it, and that the tiers genuinely differ; the
# program run-and-asserts exactly that and exits non-zero on any violation (the ch08 internal-check
# discipline). Part (1)'s RMS graph overlaps ch08's already-cross-compared norm; parts (1) inspection
# and (2) diagnostic land in later ch06 steps.
add_executable(tutorial_06_here tutorial/tutorial_06_here.cpp)
target_link_libraries(tutorial_06_here PRIVATE heinzel)
add_test(NAME tutorial_06_debugging_numerics COMMAND tutorial_06_here)

# Every ported program (`_here`, parity and tutorial) checks its `Executor` run against the reference walk, or
# walks a graph the loop tree refuses (`support/executed.hpp`).
get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
list(FILTER _targets INCLUDE REGEX "_here$")
foreach(_here IN LISTS _targets)
    target_include_directories(${_here} PRIVATE reference support)
endforeach()

# ---- the compile-time regression guard ----------------------------------------------------------
# Deliberately NOT part of the default build and NOT registered with ctest: it exists to fail loudly
# when constant-evaluation cost regresses, and it must not tax every run of the inner loop.
# Build it explicitly:  cmake --build build --target heinzel_compile_time_guard
if(HEINZEL_BUILD_COMPILE_TIME_GUARD)
    add_executable(heinzel_compile_time_guard EXCLUDE_FROM_ALL compile_time/guard.cpp)
    target_link_libraries(heinzel_compile_time_guard PRIVATE heinzel)
    # The guard's ladder now includes the extent solver's, which lives beside it.
    target_include_directories(heinzel_compile_time_guard PRIVATE compile_time)
    # Its top rung, about 240 values, sits near `kMaxValues` by design: it keeps today's capacities.
    target_compile_options(heinzel_compile_time_guard PRIVATE ${HEINZEL_TODAYS_CAPACITIES})

    # The Q-2 measurement, under the same option and the same three exclusions. It is not a guard —
    # it measures a pass the library deliberately does NOT have, so there is nothing here to
    # regress. Timings come from tools/measure_renumber_cost.sh, which invokes the compiler
    # directly; this target exists so the subject stays compilable and its correctness assertions
    # keep being checked.
    add_executable(heinzel_renumber_cost EXCLUDE_FROM_ALL compile_time/renumber_cost.cpp)
    target_link_libraries(heinzel_renumber_cost PRIVATE heinzel)
    target_include_directories(heinzel_renumber_cost PRIVATE compile_time)
    target_compile_options(heinzel_renumber_cost PRIVATE ${HEINZEL_TODAYS_CAPACITIES}) # its default rung is the guard's top

    # The M2.8 measurement, under the same option and the same three exclusions, and for the same
    # reason the renumbering one is here: timings come from tools/measure_solver_cost.sh, which
    # invokes the compiler directly, so without a target the subject would rot unnoticed until the
    # next person ran the script.
    add_executable(heinzel_solver_cost EXCLUDE_FROM_ALL compile_time/solver_cost.cpp)
    target_link_libraries(heinzel_solver_cost PRIVATE heinzel)
    target_include_directories(heinzel_solver_cost PRIVATE compile_time)

    # The M7.6 re-take of M2.8 — the solver's SECOND call site, the infer inside gradients(). Under the
    # same option and the same three exclusions as the others, and here for the same reason: timings
    # come from tools/measure_gradient_cost.sh, which invokes the compiler directly, so without a target
    # the subject would rot unnoticed until the next person ran the script. It needs the test support
    # dir for hand_built_graph.hpp, which the frontend-built cascades above do not.
    add_executable(heinzel_gradient_cost EXCLUDE_FROM_ALL compile_time/gradient_cost.cpp)
    target_link_libraries(heinzel_gradient_cost PRIVATE heinzel)
    target_include_directories(heinzel_gradient_cost PRIVATE compile_time support)
endif()
