#pragma once

// einsum: subscript notation for contraction, reduction and transposition, parsed and lowered onto the
// existing multiply, reduce, transpose and matmul nodes (`frontend/ops.hpp`); it adds no operation.
// Subscripts are single letters; the output is explicit (a spec without "->" is an error). Ellipsis,
// diagonals and more than two operands are refused: three operands need a pairing order, and choosing one
// is a cost decision a notation does not make.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel::notation {

// One operand's (or the output's) subscript: its axis label-ids, in axis order.
struct EinsumTerm {
    std::int32_t idx[kMaxRank]{};
    std::int32_t rank{0};
};

// A parsed spec. One or two operands only — the two the restricted contraction takes, plus the single
// operand a reduce/transpose takes; a wider chain is refused at parse (see the header note).
struct EinsumParse {
    EinsumTerm a{};
    EinsumTerm b{};
    EinsumTerm out{};
    std::int32_t n_operands{0};
    std::int32_t n_ids{0};
};

namespace einsum_detail {

constexpr bool is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

constexpr bool term_has(const EinsumTerm& t, std::int32_t id) {
    for (std::int32_t k = 0; k < t.rank; ++k) {
        if (t.idx[k] == id) {
            return true;
        }
    }
    return false;
}

constexpr std::int32_t term_pos(const EinsumTerm& t, std::int32_t id) {
    for (std::int32_t k = 0; k < t.rank; ++k) {
        if (t.idx[k] == id) {
            return k;
        }
    }
    return -1;
}

constexpr bool term_has_dup(const EinsumTerm& t) {
    for (std::int32_t i = 0; i < t.rank; ++i) {
        for (std::int32_t j = i + 1; j < t.rank; ++j) {
            if (t.idx[i] == t.idx[j]) {
                return true;
            }
        }
    }
    return false;
}

// Append one label character to `t`, assigning it the next id on its first appearance anywhere in the
// spec (`label_id` carries that first-appearance map across the whole parse).
constexpr void push_label(EinsumTerm& t, char c, std::int32_t (&label_id)[128], std::int32_t& n_ids) {
    if (!is_letter(c)) {
        fail("heinzel::einsum: a subscript is single letters [A-Za-z]");
    }
    const auto u = static_cast<std::size_t>(static_cast<unsigned char>(c));
    if (label_id[u] < 0) {
        label_id[u] = n_ids++;
    }
    if (t.rank >= static_cast<std::int32_t>(kMaxRank)) {
        fail("heinzel::einsum: a subscript has more axes than kMaxRank");
    }
    t.idx[static_cast<std::size_t>(t.rank++)] = label_id[u];
}

// Parse the operand side (everything before "->"): one term, or two split on a single ','.
constexpr void parse_inputs(std::string_view lhs, EinsumParse& p, std::int32_t (&label_id)[128]) {
    EinsumTerm cur{};
    bool have_second = false;
    for (char c : lhs) {
        if (c == ' ' || c == '\t') {
            continue;
        }
        if (c == '.') {
            fail("heinzel::einsum: ellipsis (...) is not supported — name the axes");
        }
        if (c == ',') {
            if (have_second) {
                fail("heinzel::einsum: more than two operands is not supported (the notation picks no pairing order)");
            }
            p.a = cur;
            cur = EinsumTerm{};
            have_second = true;
            continue;
        }
        push_label(cur, c, label_id, p.n_ids);
    }
    p.n_operands = have_second ? 2 : 1;
    (have_second ? p.b : p.a) = cur;
}

// Parse the output side (everything after "->").
constexpr void parse_output(std::string_view rhs, EinsumParse& p, std::int32_t (&label_id)[128]) {
    for (char c : rhs) {
        if (c == ' ' || c == '\t') {
            continue;
        }
        if (c == '.') {
            fail("heinzel::einsum: ellipsis (...) is not supported — name the axes");
        }
        if (c == ',') {
            fail("heinzel::einsum: the output subscript must not contain ','");
        }
        push_label(p.out, c, label_id, p.n_ids);
    }
}

constexpr EinsumParse parse(std::string_view spec) {
    EinsumParse p{};
    std::int32_t label_id[128];
    for (std::int32_t i = 0; i < 128; ++i) {
        label_id[i] = -1;
    }
    const std::size_t arrow = spec.find("->");
    if (arrow == std::string_view::npos) {
        fail("heinzel::einsum: the spec must contain \"->\" (the output subscript is explicit)");
    }
    parse_inputs(spec.substr(0, arrow), p, label_id);
    parse_output(spec.substr(arrow + 2), p, label_id);
    return p;
}

// The permutation that reads `survivors` (axes in the operand's own order, as a reduce or contract
// leaves them) as `out`: output axis k reads the survivor position of `out.idx[k]`. Identity when the
// two already agree, in which case the caller emits no transpose — the collapse alone is the answer.
constexpr Axes survivors_to_output(const EinsumTerm& survivors, const EinsumTerm& out, bool& identity) {
    Axes order{};
    identity = survivors.rank == out.rank;
    for (std::int32_t k = 0; k < out.rank; ++k) {
        const std::int32_t pos = term_pos(survivors, out.idx[k]);
        order.axes[static_cast<std::size_t>(order.rank++)] = pos;
        if (pos != k) {
            identity = false;
        }
    }
    return order;
}

// A one-operand spec: the kept axes (in the operand's own order) and the axis positions it drops. A
// label in the output but absent from the operand is an invented axis; a repeated label is a diagonal.
// Neither is expressible by a reduce or a transpose, the only two nodes this lowers to, so each is refused.
constexpr void split_unary(const EinsumTerm& a, const EinsumTerm& out, EinsumTerm& survivors, Axes& drop) {
    if (term_has_dup(a) || term_has_dup(out)) {
        fail("heinzel::einsum: a repeated index (a diagonal) is not supported");
    }
    for (std::int32_t k = 0; k < out.rank; ++k) {
        if (!term_has(a, out.idx[k])) {
            fail("heinzel::einsum: an output index is absent from the operand (no invented axis)");
        }
    }
    for (std::int32_t k = 0; k < a.rank; ++k) {
        if (term_has(out, a.idx[k])) {
            survivors.idx[static_cast<std::size_t>(survivors.rank++)] = a.idx[k];
        } else if (drop.rank < static_cast<std::int32_t>(kMaxRank)) {
            drop.axes[static_cast<std::size_t>(drop.rank++)] = k;
        }
    }
}

constexpr Expr lower_unary(Expr x, const EinsumTerm& a, const EinsumTerm& out) {
    EinsumTerm survivors{};
    Axes drop{};
    split_unary(a, out, survivors, drop);
    Expr r = x;
    if (drop.rank > 0) {
        r = detail::reduce(r, ReduceOp::Sum, drop);
    }
    bool identity = false;
    const Axes order = survivors_to_output(survivors, out, identity);
    return (identity || out.rank == 0) ? r : detail::transpose(r, order);
}

// Do the two operands carry the SAME labels in the SAME order? Then the contraction is their product summed:
// they line up elementwise and the joined shape is one operand's shape. Any other lining-up is the
// general `_matmul`'s.
constexpr bool elementwise_aligned(const EinsumTerm& a, const EinsumTerm& b) {
    if (a.rank != b.rank) {
        return false;
    }
    for (std::int32_t k = 0; k < a.rank; ++k) {
        if (a.idx[k] != b.idx[k]) {
            return false;
        }
    }
    return true;
}

// The elementwise-aligned contraction, built as `(a * b).sum(summed)` writes it (Decision 78): a summed label is
// one in the operands and not in the output. An output label in neither operand is refused, not silently mis-lowered.
constexpr Expr lower_elementwise(Expr a, Expr b, const EinsumTerm& ta, const EinsumTerm& out) {
    for (std::int32_t k = 0; k < out.rank; ++k) {
        if (!term_has(ta, out.idx[k])) {
            fail("heinzel::einsum: an output index is in neither operand");
        }
    }
    Axes summed{};
    EinsumTerm kept{};
    for (std::int32_t k = 0; k < ta.rank; ++k) {
        if (term_has(out, ta.idx[k])) {
            kept.idx[static_cast<std::size_t>(kept.rank++)] = ta.idx[k];
        } else if (summed.rank < static_cast<std::int32_t>(kMaxRank)) {
            summed.axes[static_cast<std::size_t>(summed.rank++)] = k;
        }
    }
    const Expr product = a * b;
    const Expr r = summed.rank > 0 ? detail::reduce(product, ReduceOp::Sum, summed) : product;
    bool identity = false;
    const Axes order = survivors_to_output(kept, out, identity);
    return (identity || out.rank == 0) ? r : detail::transpose(r, order);
}

// Read the axis binding of a transposing/batched contraction off the labels, and build the output order
// the shape rule produces: `[batch (a's order)] ++ [a's free axes] ++ [b's free axes] ++ [streamed]`.
// The streamed axis is the last label of every term, the one `parse_with_the_stream` appends and no spec can
// write, so a's element axes are `0 .. rank-2`. A label
// in a and b is batch when it is also in the output and contracted when it is not; a label in one operand
// and the output is that operand's free axis; a label summed from ONE operand alone is not a matmul and is
// refused, as is a diagonal, no summed axis, or more than one.
constexpr MatMulAttr read_matmul_binding(const EinsumTerm& a, const EinsumTerm& b, const EinsumTerm& out,
                                         EinsumTerm& canonical) {
    const std::int32_t streamed = out.idx[out.rank - 1];
    MatMulAttr binding{};
    std::int32_t contracted_labels = 0;
    for (std::int32_t k = 0; k + 1 < a.rank; ++k) { // a's element axes; the last is the streamed one
        const std::int32_t id = a.idx[k];
        const bool in_b = term_has(b, id);
        const bool in_out = term_has(out, id);
        if (in_b && in_out) { // a batch axis, kept once at the front in a's order
            binding.batch_a.axes[static_cast<std::size_t>(binding.batch_a.rank++)] = k;
            binding.batch_b.axes[static_cast<std::size_t>(binding.batch_b.rank++)] = term_pos(b, id);
            canonical.idx[static_cast<std::size_t>(canonical.rank++)] = id;
        } else if (in_b && !in_out) { // the contracted axis
            ++contracted_labels;
            binding.a_contract = k;
            binding.b_contract = term_pos(b, id);
        } else if (!in_b && in_out) { // a's own free axis
            canonical.idx[static_cast<std::size_t>(canonical.rank++)] = id;
        } else {
            fail("heinzel::einsum: an axis summed from one operand alone is not a contraction");
        }
    }
    for (std::int32_t k = 0; k + 1 < b.rank; ++k) { // b's element axes; its free ones follow a's
        const std::int32_t id = b.idx[k];
        const bool in_a = term_has(a, id);
        const bool in_out = term_has(out, id);
        if (!in_a && in_out) {
            canonical.idx[static_cast<std::size_t>(canonical.rank++)] = id;
        } else if (!in_a && !in_out) {
            fail("heinzel::einsum: an axis summed from one operand alone is not a contraction");
        }
    }
    if (contracted_labels != 1) {
        fail("heinzel::einsum: a general contraction sums exactly one shared axis");
    }
    canonical.idx[static_cast<std::size_t>(canonical.rank++)] = streamed;
    return binding;
}

// A transposing or batched contraction: the operands do not line up elementwise. Lower to
// one `op::MatMul` carrying the label binding, then a transpose only when the output axes are not already
// the `[batch]++[a free]++[b free]++[streamed]` order the shape rule produces.
constexpr Expr lower_matmul(Expr a, Expr b, const EinsumTerm& ta, const EinsumTerm& tb, const EinsumTerm& out) {
    EinsumTerm canonical{};
    const MatMulAttr binding = read_matmul_binding(ta, tb, out, canonical);
    for (std::int32_t k = 0; k < out.rank; ++k) {
        if (term_pos(canonical, out.idx[k]) < 0) {
            fail("heinzel::einsum: an output index is in neither operand");
        }
    }
    Expr r = detail::matmul(a, b, binding);
    bool identity = false;
    const Axes order = survivors_to_output(canonical, out, identity);
    return identity ? r : detail::transpose(r, order);
}

// A binary spec: a product and a sum when the operands line up elementwise, the general `_matmul` when they do not.
// A diagonal (a repeated index in either operand or the output) is neither and is refused first.
constexpr Expr lower_binary(Expr a, Expr b, const EinsumTerm& ta, const EinsumTerm& tb, const EinsumTerm& out) {
    if (term_has_dup(ta) || term_has_dup(tb) || term_has_dup(out)) {
        fail("heinzel::einsum: a repeated index (a diagonal) is not supported");
    }
    if (elementwise_aligned(ta, tb)) {
        return lower_elementwise(a, b, ta, out);
    }
    return lower_matmul(a, b, ta, tb, out);
}

constexpr Expr lower_one(const EinsumParse& p, Expr x) {
    if (p.n_operands != 1) {
        fail("heinzel::einsum: this spelling takes one operand — the spec names two");
    }
    return lower_unary(x, p.a, p.out);
}

constexpr Expr lower_two(const EinsumParse& p, Expr a, Expr b) {
    if (p.n_operands != 2) {
        fail("heinzel::einsum: this spelling takes two operands — the spec names one");
    }
    return lower_binary(a, b, p.a, p.b, p.out);
}

// The public spelling (`heinzel::einsum`) never names the stream: each term gets one label no spec can
// write, last, and is then read as this notation reads the same spec with it written out. So a term names
// at most `kMaxRank - 1` axes, counted on the text before anything is parsed.
constexpr EinsumParse parse_with_the_stream(std::string_view spec) {
    std::int32_t labels = 0;
    for (const char c : spec) {
        labels = c == ',' || c == '-' ? 0 : labels + (is_letter(c) ? 1 : 0);
        if (labels > static_cast<std::int32_t>(kMaxRank) - 1) {
            fail("heinzel: a spec names at most kMaxRank - 1 axes");
        }
    }
    EinsumParse p = parse(spec);
    const std::int32_t stream = p.n_ids++;
    for (EinsumTerm* t : {&p.a, &p.b, &p.out}) {
        if (t != &p.b || p.n_operands == 2) {
            t->idx[static_cast<std::size_t>(t->rank++)] = stream;
        }
    }
    return p;
}

} // namespace einsum_detail

} // namespace heinzel::notation

namespace heinzel {

// Einstein notation over the element axes: `einsum("ab->a", x)` sums axis 1, `einsum("ik,kj->ij", a, b)`
// multiplies two matrices. The stream is never named.
constexpr Expr einsum(std::string_view spec, Expr x) {
    const notation::EinsumParse p = notation::einsum_detail::parse_with_the_stream(spec);
    detail::spec_names_every_axis(x, p.a.rank);
    const Expr made = notation::einsum_detail::lower_one(p, x);
    detail::record_spec_rank(made, x, 0, p.a.rank);
    return made;
}
constexpr Expr einsum(std::string_view spec, Expr a, Expr b) {
    const notation::EinsumParse p = notation::einsum_detail::parse_with_the_stream(spec);
    if (p.n_operands == 2) {
        detail::spec_names_every_axis(a, p.a.rank);
        detail::spec_names_every_axis(b, p.b.rank);
    }
    const Expr made = notation::einsum_detail::lower_two(p, a, b);
    detail::record_spec_rank(made, a, 0, p.a.rank);
    detail::record_spec_rank(made, b, 1, p.b.rank);
    return made;
}

} // namespace heinzel
