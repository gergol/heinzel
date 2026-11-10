#pragma once

// The reference interpreter (the walk): it runs a graph directly and is the tests' ground truth. It walks
// the node list in the written order, one sample at a time with time on the outside, and follows no plan:
// no chosen order, no shared storage, no fusion, no vectorisation, so it stays obviously correct. One step
// produces as many numbers as the value's shape has per sample. The arithmetic is `compute()`
// (`compute_reference.hpp`); which operand element each result element reads is `elements.hpp`.

#include <array>
#include <bit>
#include <cstddef>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/timing.hpp"
#include "heinzel/runtime/table_gather_element.hpp"
#include "heinzel/support/fail.hpp"

#include "collapse_element.hpp"
#include "compute_reference.hpp"
#include "contract_element.hpp"
#include "elements.hpp"
#include "framed_stream.hpp"
#include "iota_element.hpp"
#include "join_element.hpp"
#include "link_element.hpp"
#include "meaning_coverage.hpp"
#include "order_element.hpp"
#include "overlap_add_element.hpp"
#include "pad_element.hpp"
#include "rearrange_element.hpp"
#include "run_inputs.hpp"
#include "signals.hpp"

namespace heinzel {

namespace detail {

// One node at one sample, when the operation has a meaning per sample. The element loop is here, outside
// the arithmetic: `compute()` is asked once per element and handed three numbers.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void walk_per_sample_step(const Graph& g, const Node& node, std::size_t n, Signals<BlockSize, Planes>& out) {
    const std::int32_t id = node.output;
    const Elements result = elements_of(g.values[static_cast<std::size_t>(id)].shape);
    // The core this operation runs is set by its result's precision, which `infer` resolved at compile
    // time. An unmarked graph resolves every value to `Normal`, whose function accuracy is `Exact`.
    const FnAccuracy fn = numerics::numeric_setting_of(g.values[static_cast<std::size_t>(id)].precision).fn;
    const ElemType type = g.values[static_cast<std::size_t>(id)].type;

    // Worked out once per node rather than once per element, so the cost is linear in the elements
    // (`operand_elements` in `elements.hpp`).
    Elements operand[kMaxNodeInputs]{};
    operand_elements(g, node, operand);

    for (std::int32_t e = 0; e < result.count; ++e) {
        // Reads operand `k` `back` samples ago, at the element this element of the result reads.
        // Which distance is `operands_of` (`ir/timing.hpp`); which element is `element_read_by`
        // (`elements.hpp`). Before the start of the block there is silence.
        const auto read = [&](std::size_t k, std::int32_t back) -> double {
            const std::int32_t v = node.inputs[k];
            if (v == kNoValue) {
                return 0.0;
            }
            if (back != 0 && static_cast<std::int32_t>(n) < back) {
                return 0.0;
            }
            return sample_at(out, v, element_read_by(result, operand[k], e), n - static_cast<std::size_t>(back));
        };

        const Operands in = operands_of(node, read);
        // `fn` was resolved above from the value's precision: `compute` is handed a node, not the graph.
        const double rounded = as_type(type, compute(node, fn, in.a, in.b, in.c));
        write(out, id, e, n, rounded);

        // A recurrence and the value it defines are the same number, so they are written from the
        // same place. Writing the unrounded one to the variable would leave the tap reading a number
        // the output never had.
        if (node.attr.kind == AttrKind::Recurrence && node.attr.recurrence.defines != kNoValue) {
            // The variable and the expression defining it are the same value, so they are the same
            // width. Checked rather than assumed: if it were broken the write would land in the
            // NEXT value's planes and the symptom would be a plausible wrong number.
            if (elements_of(g.values[static_cast<std::size_t>(node.attr.recurrence.defines)].shape).count !=
                result.count) {
                fail("heinzel::run: a recurrence and the variable it defines are different widths");
            }
            write(out, node.attr.recurrence.defines, e, n, rounded);
        }
    }
}

// One node at one sample, when the operation COLLAPSES axes: one element of the result is a fold
// over many contributors, and one contributor is made out of the operands' elements.
//
// No operation is named here and no attribute case either. Which axes collapse is `collapse_of`
// (`collapse_element.hpp`), what one contributor is and what the fold does are `contribution` and
// `fold_of` (`compute_reference.hpp`), and which elements fold into which is `elements.hpp`. What is
// left here is the loop.
//
// Two operands, always, which lets one loop serve both a reduction and a contraction. The axes are named
// against the operands' join; a reduction's join is its one operand's own layout, and its second operand
// is `kNoValue` and reads as silence, which `contribution` ignores.
//
// Its cost is `result.count` folds of `fan_in` each, which multiply back to exactly the elements of the
// join: linear in the numbers it reads, and not quadratic when two axes collapse instead of one, since the
// fan-in grows and the result shrinks by the same factor.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void walk_folding_step(const Graph& g, const Node& node, std::size_t n, Signals<BlockSize, Planes>& out) {
    const std::int32_t id = node.output;
    Elements operand[kMaxNodeInputs]{};
    operand_elements(g, node, operand);
    const Elements joined = joined_elements(operand[0], operand[1]);
    const Elements result = elements_of(g.values[static_cast<std::size_t>(id)].shape);
    const Collapse c = collapse_of(node, joined);
    const ReduceOp fold = fold_of(node);
    const ElemType type = g.values[static_cast<std::size_t>(id)].type;

    // One operand's element at one element of the JOIN. An operand the operation does not take reads
    // as silence, which is the same answer `walk_per_sample_step` gives for one.
    const auto contributor = [&](std::size_t k, std::int32_t at) -> double {
        if (node.inputs[k] == kNoValue) {
            return 0.0;
        }
        return sample_at(out, node.inputs[k], element_read_by(joined, operand[k], at), n);
    };

    for (std::int32_t e = 0; e < result.count; ++e) {
        double so_far = 0.0;
        for (std::int32_t j = 0; j < c.fan_in; ++j) {
            const std::int32_t at = folded_element(joined, c, result, e, j);
            // A fold starts at its FIRST contributor rather than at an identity — `Max` and `Min`
            // would need an infinity to start from. Nothing refuses an axis of no elements: such a
            // fold writes the zero it starts at (a `Mean` divides it by zero).
            const double x = contribution(node, contributor(0, at), contributor(1, at));
            so_far = j == 0 ? x : fold_step(fold, so_far, x);
        }
        write(out, id, e, n, as_type(type, fold_finish(fold, so_far, c.fan_in)));
    }
}

// One node at one sample, when the operation is a GENERAL contraction (`_matmul`): one element of the
// result is a sum over a contracted extent, its two operands read at different index maps.
// `walk_folding_step` reads both operands at one shared join index and cannot express it, so a general
// contraction is its own element kind rather than a flag in the fold, and no operation is named here.
//
// The fold is a sum, written here rather than read from `fold_of`: the contributor is the product of the
// two operands. Products accumulate in double and the result is rounded once at the end, as a fold's is.
//
// The index maps are `contracted_element` (`contract_element.hpp`), which reads the
// `[batch] ++ [a free] ++ [b free]` output layout of `sema/matmul.hpp` back to each operand's axes.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void walk_contract_step(const Graph& g, const Node& node, std::size_t n, Signals<BlockSize, Planes>& out) {
    const std::int32_t id = node.output;
    const Elements result = elements_of(g.values[static_cast<std::size_t>(id)].shape);
    const Elements opa = elements_of(g.values[static_cast<std::size_t>(node.inputs[0])].shape);
    const Elements opb = elements_of(g.values[static_cast<std::size_t>(node.inputs[1])].shape);
    const ElemType type = g.values[static_cast<std::size_t>(id)].type;
    const MatMulAttr& m = node.attr.matmul;

    const std::int32_t batch = m.batch_a.rank;                    // the shared batch block, at the front
    const std::int32_t a_free_base = batch;                       // a's free axes follow the batch block
    const std::int32_t b_free_base = batch + (opa.rank - 1 - batch); // b's follow a's (a's frees = its axes − K − batch)
    const std::int32_t contracted = opa.extent[m.a_contract];     // the summed extent; opb's must match (checked at infer)

    for (std::int32_t e = 0; e < result.count; ++e) {
        double so_far = 0.0;
        for (std::int32_t kc = 0; kc < contracted; ++kc) {
            const double av = sample_at(out, node.inputs[0],
                                        contracted_element(result, opa, m.a_contract, m.batch_a, a_free_base, kc, e), n);
            const double bv = sample_at(out, node.inputs[1],
                                        contracted_element(result, opb, m.b_contract, m.batch_b, b_free_base, kc, e), n);
            const double prod = av * bv;
            so_far = kc == 0 ? prod : fold_step(ReduceOp::Sum, so_far, prod);
        }
        write(out, id, e, n, as_type(type, fold_finish(ReduceOp::Sum, so_far, contracted)));
    }
}

// One node at one sample, when the operation REARRANGES axes: one element of the result is one
// element of its operand, found somewhere else.
//
// No arithmetic happens here: `rearranged_element` (`rearrange_element.hpp`) says which element the
// number comes from and the loop moves it. Nothing is rounded, because the number is already stored at
// the type it is about to be stored at again (a rearrangement's row carries `TypeRule::SameAsFirst`).
// One operand, because a rearrangement has one; a join of two is `walk_join_step`. One read and one
// write per element of the result.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void walk_rearranging_step(const Graph& g, const Node& node, std::size_t n, Signals<BlockSize, Planes>& out) {
    const std::int32_t id = node.output;
    Elements operand[kMaxNodeInputs]{};
    operand_elements(g, node, operand);
    const Elements result = elements_of(g.values[static_cast<std::size_t>(id)].shape);

    for (std::int32_t e = 0; e < result.count; ++e) {
        const std::int32_t from = rearranged_element(node, result, operand[0], e);
        write(out, id, e, n, sample_at(out, node.inputs[0], from, n));
    }
}

// One node at one sample, when the operation is a GUARDED PAD: each element of the result is either one
// element of its operand (the body, shifted) or a constant fill (a margin). `padded_read` says which,
// and the loop copies a source element or writes the constant — the constant at the result's type,
// because it is the one number a view produces that was not already stored, exactly as a fold's is. One
// operand, because a pad has one.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void walk_padding_step(const Graph& g, const Node& node, std::size_t n, Signals<BlockSize, Planes>& out) {
    const std::int32_t id = node.output;
    Elements operand[kMaxNodeInputs]{};
    operand_elements(g, node, operand);
    const Elements result = elements_of(g.values[static_cast<std::size_t>(id)].shape);
    const ElemType type = g.values[static_cast<std::size_t>(id)].type;
    const double fill = pad_fill(node);

    for (std::int32_t e = 0; e < result.count; ++e) {
        const PadRead r = padded_read(node, result, operand[0], e);
        write(out, id, e, n,
              r.in_source ? sample_at(out, node.inputs[0], r.index, n) : as_type(type, fill));
    }
}

// One node at one sample, when the operation is a JOIN: each element of the result is one element of ONE
// of two operands, `concat_read` saying which operand and which element. No arithmetic and no rounding —
// the number is already stored at the type it is about to be stored at again, exactly as a rearrangement.
// TWO OPERANDS, and it SELECTS one rather than folding across them, which is what separates a join from
// the contraction that also reads two: `walk_folding_step` sums operand 0 and operand 1 at each
// contributor; this copies one or the other per output element.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void walk_join_step(const Graph& g, const Node& node, std::size_t n, Signals<BlockSize, Planes>& out) {
    const std::int32_t id = node.output;
    Elements operand[kMaxNodeInputs]{};
    operand_elements(g, node, operand);
    const Elements result = elements_of(g.values[static_cast<std::size_t>(id)].shape);

    for (std::int32_t e = 0; e < result.count; ++e) {
        const ConcatRead r = concat_read(node, result, operand[0], operand[1], e);
        const std::int32_t src = r.from_second ? node.inputs[1] : node.inputs[0];
        write(out, id, e, n, sample_at(out, src, r.index, n));
    }
}

// One node at one sample, when the operation is an OVERLAP-ADD: one element of the result is the SUM of the
// operand's elements a sliding window scatters onto it — the adjoint of the rearrangement `_unfold` is.
//
// `overlap_span` says which frames overlap this output element and `overlap_read` which source element each
// reads (`overlap_add_element.hpp`, which reads the window and hop); the loop sums them. Its sum starts at
// zero, unlike `walk_folding_step`'s fold (which starts at its first contributor for `Max`/`Min`): an
// overlap-add is a plain sum whose identity is zero, and an output position no frame overlaps is
// legitimately that zero. Rounded once at the result's type, as a fold's result is.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void walk_scatter_step(const Graph& g, const Node& node, std::size_t n, Signals<BlockSize, Planes>& out) {
    const std::int32_t id = node.output;
    Elements operand[kMaxNodeInputs]{};
    operand_elements(g, node, operand);
    const Elements result = elements_of(g.values[static_cast<std::size_t>(id)].shape);
    const ElemType type = g.values[static_cast<std::size_t>(id)].type;

    for (std::int32_t e = 0; e < result.count; ++e) {
        const OverlapSpan span = overlap_span(node, result, operand[0], e);
        double so_far = 0.0;
        for (std::int32_t j = 0; j < span.count; ++j) {
            so_far += sample_at(out, node.inputs[0], overlap_read(node, result, operand[0], span, j, e), n);
        }
        write(out, id, e, n, as_type(type, so_far));
    }
}

// One node at one sample, when the operation is an ORDER STATISTIC: each group — one choice of every
// non-ordered element axis — is gathered along the ordered axis, sorted by the compare-exchange network, and
// its kept ranks written (Sort every rank, TopK the k largest descending, Quantile one interpolated value).
//
// `order_plan` (`order_element.hpp`) reads the attribute once, so nothing here names an operation or an
// attribute case. The network is built here, at run time, from the resolved extent (L is a literal: the
// attribute check makes it so) and pruned to the ranks the statistic keeps, then applied by
// `order_compare_exchange`.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void walk_order_step(const Graph& g, const Node& node, std::size_t n, Signals<BlockSize, Planes>& out) {
    const std::int32_t id = node.output;
    Elements operand[kMaxNodeInputs]{};
    operand_elements(g, node, operand);
    const Elements from = operand[0];
    const Elements to = elements_of(g.values[static_cast<std::size_t>(id)].shape);
    const ElemType type = g.values[static_cast<std::size_t>(id)].type;
    const OrderPlan ord = order_plan(node, from);
    const order::NetScratch net = order::prune_net(order::make_sort_net(ord.length), ord.length, ord.r0, ord.r1);

    for (std::int32_t group = 0; group < ord.groups; ++group) {
        double buf[order::kMaxOrderedLength]{};
        for (std::int32_t t = 0; t < ord.length; ++t) {
            buf[t] = sample_at(out, node.inputs[0], order_group_input(from, ord.axis, group, t), n);
        }
        order_compare_exchange(net.ce.data(), net.n, buf);
        for (std::int32_t s = 0; s < order_output_count(ord); ++s) {
            write(out, id, order_group_output(to, from, ord.axis, ord.kept, group, s), n,
                  as_type(type, order_output_value(ord, buf, s)));
        }
    }
}

// One node at one sample, when the operation is a wavetable GATHER (`_tblread`): each output element is
// `lerp2` of the two table samples the phase brackets. The phase (operand 0) is read at the element this
// output reads (`element_read_by`, the same per-sample rule everything else uses); the TABLE (operand 1) is
// read at a DATA-DEPENDENT element index — `table_tap` turns the phase into which two samples and how far
// between — which is the whole reason this is not a per-sample step. `table_tap` and `lerp2`
// (`runtime/table_gather_element.hpp`) and `table_size` (`compute_reference.hpp`) name no operation.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void walk_gather_step(const Graph& g, const Node& node, std::size_t n, Signals<BlockSize, Planes>& out) {
    const std::int32_t id = node.output;
    Elements operand[kMaxNodeInputs]{};
    operand_elements(g, node, operand);
    const Elements result = elements_of(g.values[static_cast<std::size_t>(id)].shape);
    const ElemType type = g.values[static_cast<std::size_t>(id)].type;
    const std::int32_t size = table_size(node);
    // The table holds N samples, read as a cycle. A hand-built node reaching the walk without the analysis
    // pass could carry a table of the wrong width; reading past it would land in the next value's planes and
    // answer a plausible wrong number, so it is refused rather than read.
    if (operand[1].count != size) {
        fail("heinzel::run: a table read's table operand must hold N samples");
    }

    for (std::int32_t e = 0; e < result.count; ++e) {
        const double phase = sample_at(out, node.inputs[0], element_read_by(result, operand[0], e), n);
        const TableTap tap = table_tap(size, phase);
        const double near_tap = sample_at(out, node.inputs[1], tap.index, n);
        const double far_tap = sample_at(out, node.inputs[1], tap.next, n);
        write(out, id, e, n, as_type(type, lerp2(near_tap, far_tap, tap.fraction)));
    }
}

// One node at one sample, when the operation is an index RAMP (`_iota`): each output element IS its own
// index, generated with no operand read. `iota_value` (`iota_element.hpp`) turns the element index into
// the number, rounded to the result's type. An arity-0 node, so `operand_elements` is not asked.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void walk_indexed_step(const Graph& g, const Node& node, std::size_t n, Signals<BlockSize, Planes>& out) {
    const std::int32_t id = node.output;
    const Elements result = elements_of(g.values[static_cast<std::size_t>(id)].shape);
    const ElemType type = g.values[static_cast<std::size_t>(id)].type;

    for (std::int32_t e = 0; e < result.count; ++e) {
        write(out, id, e, n, as_type(type, iota_value(e)));
    }
}

// One node at one sample, when the operation is a constant tensor (`_const`): each output element IS the graph's
// number at `first + e`, rounded to the result's type when the frontend recorded it.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void walk_pooled_step(const Graph& g, const Node& node, std::size_t n, Signals<BlockSize, Planes>& out) {
    const std::int32_t id = node.output;
    const Elements result = elements_of(g.values[static_cast<std::size_t>(id)].shape);
    const ElemType type = g.values[static_cast<std::size_t>(id)].type;
    const auto first = static_cast<std::size_t>(node.attr.generator.first);

    for (std::int32_t e = 0; e < result.count; ++e) {
        write(out, id, e, n, as_type(type, std::bit_cast<double>(g.constants[first + static_cast<std::size_t>(e)])));
    }
}

// Which kind of step a node is. The rule table says whether it has a sample-at-a-time meaning at all;
// `element_kind_of` (`compute_reference.hpp`) says which kind of element meaning it has if it does not.
// Neither question is answered here, so this file names no operation.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void walk_step(const Graph& g, const Node& node, std::size_t n, Signals<BlockSize, Planes>& out, bool starts) {
    if (rules_of(node.op).meaning != MeaningRule::AxisChanging) {
        walk_per_sample_step(g, node, n, out);
        return;
    }
    switch (element_kind_of(node.op)) {
    case ElementKind::Fold:
        walk_folding_step(g, node, n, out);
        return;
    case ElementKind::Rearrange:
        walk_rearranging_step(g, node, n, out);
        return;
    case ElementKind::Guarded:
        walk_padding_step(g, node, n, out);
        return;
    case ElementKind::Joined:
        walk_join_step(g, node, n, out);
        return;
    case ElementKind::Scatter:
        walk_scatter_step(g, node, n, out);
        return;
    case ElementKind::Ordered:
        walk_order_step(g, node, n, out);
        return;
    case ElementKind::Gathered:
        walk_gather_step(g, node, n, out);
        return;
    case ElementKind::Indexed:
        walk_indexed_step(g, node, n, out);
        return;
    case ElementKind::Pooled:
        walk_pooled_step(g, node, n, out);
        return;
    case ElementKind::Started:
        // Where the run stands (`_first`): 1 at its first sample, else 0, in its type.
        write(out, node.output, 0, n, as_type(g.values[static_cast<std::size_t>(node.output)].type, starts ? 1.0 : 0.0));
        return;
    case ElementKind::Boundary:
        // An intrinsic boundary (`_fft`): its shape and precondition are modelled, its arithmetic is
        // left to a backend that does not exist yet. A clear refusal, never a silent wrong number.
        fail("heinzel::run: an intrinsic boundary (_fft); no spectral backend computes it yet");
    case ElementKind::Contracted:
        walk_contract_step(g, node, n, out);
        return;
    case ElementKind::Linked:
        // A link reads the section before at this sample: right once the `run`s have taken their passes (`passes_of`).
        walk_link_step(g, node, n, out);
        return;
    case ElementKind::None:
        // A row that says it changes axes and for which no element meaning is written yet. Said in
        // its own words rather than reaching `collapse_of` and failing about an attribute — which
        // would be true and misleading.
        fail("heinzel::run: this operation changes axes and has no element meaning written yet");
    }
    fail("heinzel::run: an element meaning of a kind this walk does not know");
}

// ...in a walk of one run, the block's first sample its start.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void walk_step(const Graph& g, const Node& node, std::size_t n, Signals<BlockSize, Planes>& out) {
    walk_step(g, node, n, out, n == 0);
}

// The constants of a graph, filled into every element of every sample — the same repeat rule an
// operand of extent one gets from `element_read_by`: a constant is one number and it fills every
// element of the value it stands for. Shared by the scalar and the wide `run`.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void fill_constants(const Graph& g, Signals<BlockSize, Planes>& out) {
    for (std::size_t i = 0; i < g.values.size(); ++i) {
        if (g.values[i].category != ValueCategory::Const) {
            continue;
        }
        for (std::int32_t e = 0; e < elements_of(g.values[i].shape).count; ++e) {
            for (std::size_t n = 0; n < BlockSize; ++n) {
                write(out, static_cast<std::int32_t>(i), e, n, g.values[i].constant);
            }
        }
    }
}

// One framing node at one frame `f`: every element of the framed value is one number of its source
// input's FEED, read WIDER than the block (`framed_feed_index`). No arithmetic and no rounding — the
// number is copied at the type it was fed at, exactly as a rearrangement copies one already stored.
// The frames are materialised here rather than in `walk_step` because the source is read from the feed
// across `[0, N)`, not from a per-sample plane the block loop has filled (`framed_stream.hpp`).
template <std::size_t BlockSize, std::size_t Planes>
constexpr void walk_framed_stream_step(const Graph& g, const Node& node, const Inputs& feeds,
                                       std::size_t f, Signals<BlockSize, Planes>& out) {
    const std::int32_t id = node.output;
    const std::int32_t src = node.inputs[0];
    const std::int32_t i = feed_index_of(g, src);
    const std::int32_t window = node.attr.view.window;
    const std::int32_t hop = node.attr.view.hop;
    const std::int32_t w_src = feeds[static_cast<std::size_t>(i)].width;
    const std::int32_t required = framed_feed_samples(window, hop, static_cast<std::int32_t>(BlockSize));
    if (feeds[static_cast<std::size_t>(i)].samples < required) {
        fail("heinzel::run: a framed input's feed holds fewer samples than its frames read");
    }
    const std::int32_t count = elements_of(g.values[static_cast<std::size_t>(id)].shape).count;
    for (std::int32_t e = 0; e < count; ++e) {
        const std::int32_t idx = framed_feed_index(window, hop, w_src, e, static_cast<std::int32_t>(f));
        write(out, id, e, f, feeds[static_cast<std::size_t>(i)].data[static_cast<std::size_t>(idx)]);
    }
}

} // namespace detail

// Compute every value over one block. `input_block` supplies the samples for the graph's one input.
//
// Time runs on the outside and the node list on the inside, which is what makes a value defined by
// its own past work at all: by the time sample n is being computed, sample n-1 of everything is
// already there. A graph holding a link goes over the node list more than once a sample (`passes_of`).
// Walking a node again is safe: it writes only this sample's numbers, worked out from earlier samples,
// which no pass writes, and from this sample's, so nothing advances.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void run(const Graph& g, const std::array<double, BlockSize>& input_block, Signals<BlockSize, Planes>& out) {
    if (g.inputs.size() != 1) {
        fail("heinzel::run: this interpreter takes exactly one input");
    }
    lay_out(g, out);
    const std::int32_t input_id = g.inputs[0].value;
    // `input_block` is one number per sample, so the value it fills must be one number per sample.
    // A wider input is a different signature rather than a wider loop, and saying so here is what
    // keeps a wide input from silently arriving with only its first element filled.
    if (elements_of(g.values[static_cast<std::size_t>(input_id)].shape).count != 1) {
        fail("heinzel::run: this interpreter takes an input of one number per sample");
    }

    detail::fill_constants(g, out);
    const std::size_t passes = passes_of(g);

    for (std::size_t n = 0; n < BlockSize; ++n) {
        detail::write(out, input_id, 0, n, input_block[n]);

        for (std::size_t pass = 0; pass < passes; ++pass) {
            for (const Node& node : g.nodes) {
                detail::walk_step(g, node, n, out, n == 0);
            }
        }
    }
}

// Compute every value over one block, for a graph of N named inputs any of which may be wide: the same
// walk as the scalar `run` above, with more than one number bound per input. `feeds[i]` supplies
// `g.inputs[i]`, laid out sample-major (`feeds[i].data[n * width + e]`, see `run_inputs.hpp`).
// `run_length`: the block walked as the runs of a program called on it one after another, each that many samples (the
// last what is left), a run's first sample the only one where `_first` is 1; a Param's feed changes where a run starts
// as a host sets a knob between calls. One walk, so its state crosses from run to run as a program's does.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void run(const Graph& g, const Inputs& feeds, Signals<BlockSize, Planes>& out, std::size_t run_length = BlockSize) {
    feeds_are_this_graph_s(g, feeds);
    framed_inputs_are_well_formed(g, BlockSize);
    lay_out(g, out);
    detail::fill_constants(g, out);

    // Which inputs are framed sources, once. A framed input is not bound per sample: the framing node
    // below materialises its frames from the whole feed, and binding it as `data[n*width + e]` would fill
    // only `W` of its `N` samples.
    fixed_vector<bool, kMaxIO> is_framed_source{};
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        is_framed_source.push_back(feeds_a_framed_stream(g, g.inputs[i].value));
    }
    const std::size_t passes = passes_of(g);

    for (std::size_t n = 0; n < BlockSize; ++n) {
        for (std::size_t i = 0; i < g.inputs.size(); ++i) {
            if (is_framed_source[i]) {
                continue;
            }
            const std::int32_t id = g.inputs[i].value;
            for (std::int32_t e = 0; e < feeds[i].width; ++e) {
                detail::write(out, id, e, n, feeds[i].data[n * feeds[i].width + static_cast<std::size_t>(e)]);
            }
        }
        for (std::size_t pass = 0; pass < passes; ++pass) {
            for (const Node& node : g.nodes) {
                if (frames_the_stream(g, node)) {
                    detail::walk_framed_stream_step(g, node, feeds, n, out);
                    continue;
                }
                detail::walk_step(g, node, n, out, run_length > 0 && n % run_length == 0);
            }
        }
    }
}

} // namespace heinzel
