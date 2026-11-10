#pragma once

// rearrange and reduce: the einops verbs, a string over named element axes, lowered onto the existing
// reshape, transpose and reduce nodes (`frontend/ops.hpp`). A pure regroup (split or merge, labels in the
// same order) is one reshape, recorded as written and worked out by the analysis once its operand has a
// shape, so a computed value regroups as an input does; a pure reordering is one transpose; a spec that does
// both splits the left side's groups, reorders and merges the right side's, as einops lowers it. Labels are single
// letters and `(a b)` is a group. The streamed axis is never named: each side gets a last label no spec can write,
// so it passes through whole.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel::notation {

// A named axis size, for the one factor a split leaves unknown: `{"f", F}`. The name is a single letter
// (the first character is read); the size is the extent that axis takes.
struct AxisSize {
    std::string_view name;
    std::int32_t size{0};
};

namespace rearrange_detail {

// One parenthesised group of the notation: the label ids it flattens to, in order. A bare letter is a
// group of one.
struct Group {
    std::int32_t ids[kMaxRank]{};
    std::int32_t rank{0};
};

// One side of a spec (the lhs or the rhs): its groups, one per axis of the value that side describes.
struct Term {
    Group groups[kMaxRank]{};
    std::int32_t rank{0};
};

constexpr bool is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// Assign a letter its id on first appearance anywhere in the spec (`label_id` carries that map across the
// whole parse, both sides), and append it to the group under construction.
constexpr void push_label(Group& g, char c, std::int32_t (&label_id)[128], std::int32_t& n_ids) {
    if (!is_letter(c)) {
        fail("heinzel: a label in a spec is a single letter [A-Za-z]");
    }
    const auto u = static_cast<std::size_t>(static_cast<unsigned char>(c));
    if (label_id[u] < 0) {
        label_id[u] = n_ids++;
    }
    if (g.rank >= static_cast<std::int32_t>(kMaxRank)) {
        fail("heinzel: a group in a spec has more axes than kMaxRank");
    }
    g.ids[static_cast<std::size_t>(g.rank++)] = label_id[u];
}

constexpr void push_group(Term& t, const Group& g) {
    if (g.rank == 0) {
        fail("heinzel: a spec has an empty group ()");
    }
    if (t.rank >= static_cast<std::int32_t>(kMaxRank)) {
        fail("heinzel: a spec names more axes than kMaxRank");
    }
    t.groups[static_cast<std::size_t>(t.rank++)] = g;
}

// Parse one side into its groups. A `(` opens a group closed by `)`; a bare letter is a group of one.
// Nested or unbalanced parentheses are refused.
constexpr Term parse_term(std::string_view side, std::int32_t (&label_id)[128], std::int32_t& n_ids) {
    Term t{};
    Group open{};
    bool in_group = false;
    for (char c : side) {
        if (c == ' ' || c == '\t') {
            continue;
        }
        if (c == '(') {
            if (in_group) {
                fail("heinzel: a spec's groups do not nest ((...))");
            }
            in_group = true;
            open = Group{};
        } else if (c == ')') {
            if (!in_group) {
                fail("heinzel: a spec has a ')' without a '('");
            }
            in_group = false;
            push_group(t, open);
        } else if (in_group) {
            push_label(open, c, label_id, n_ids);
        } else {
            Group one{};
            push_label(one, c, label_id, n_ids);
            push_group(t, one);
        }
    }
    if (in_group) {
        fail("heinzel: a spec has a '(' without a ')'");
    }
    return t;
}

// The two sides of a parsed spec, the shared first-appearance id count, and the letter->id map (kept so a
// size named by letter can be matched to the id its extent is stored under).
struct Parse {
    Term lhs{};
    Term rhs{};
    std::int32_t n_ids{0};
    std::int32_t label_id[128]{};
};

constexpr Parse parse(std::string_view spec) {
    Parse p{};
    for (std::int32_t i = 0; i < 128; ++i) {
        p.label_id[i] = -1;
    }
    const std::size_t arrow = spec.find("->");
    if (arrow == std::string_view::npos) {
        fail("heinzel: a spec must contain \"->\" (the output is explicit)");
    }
    p.lhs = parse_term(spec.substr(0, arrow), p.label_id, p.n_ids);
    p.rhs = parse_term(spec.substr(arrow + 2), p.label_id, p.n_ids);
    return p;
}

// Flatten a term's groups into the label ids in order (the fully-split axis sequence).
constexpr Group flatten(const Term& t) {
    Group f{};
    for (std::int32_t g = 0; g < t.rank; ++g) {
        for (std::int32_t k = 0; k < t.groups[g].rank; ++k) {
            f.ids[static_cast<std::size_t>(f.rank++)] = t.groups[g].ids[k];
        }
    }
    return f;
}

constexpr std::int32_t index_of(const Group& f, std::int32_t id) {
    for (std::int32_t k = 0; k < f.rank; ++k) {
        if (f.ids[k] == id) {
            return k;
        }
    }
    return -1;
}

constexpr bool has_duplicate(const Group& f) {
    for (std::int32_t i = 0; i < f.rank; ++i) {
        for (std::int32_t j = i + 1; j < f.rank; ++j) {
            if (f.ids[i] == f.ids[j]) {
                return true;
            }
        }
    }
    return false;
}

constexpr bool same_ids_in_order(const Group& a, const Group& b) {
    if (a.rank != b.rank) {
        return false;
    }
    for (std::int32_t k = 0; k < a.rank; ++k) {
        if (a.ids[k] != b.ids[k]) {
            return false;
        }
    }
    return true;
}

constexpr bool is_permutation(const Group& a, const Group& b) {
    if (a.rank != b.rank) {
        return false;
    }
    for (std::int32_t k = 0; k < a.rank; ++k) {
        if (index_of(b, a.ids[k]) < 0) {
            return false;
        }
    }
    return true;
}

// Every label of `a` appears in `b` and vice versa — the axis SET is conserved. A rearrange never invents
// or drops an axis (that is `reduce`'s job); neither side repeats one (a diagonal).
constexpr void require_axes_conserved(const Group& la, const Group& lb) {
    if (has_duplicate(la) || has_duplicate(lb)) {
        fail("heinzel::rearrange: a repeated label (a diagonal) is not supported");
    }
    if (!is_permutation(la, lb)) {
        fail("heinzel::rearrange: the two sides name different axes (a rearrange conserves them)");
    }
}

// The size given for a label, or -1 if none was named. Names are single letters; a name of another length
// cannot match a label and is refused so a typo is not silently ignored.
constexpr std::int32_t size_for(std::int32_t id, const std::int32_t (&label_id)[128],
                                std::initializer_list<AxisSize> sizes) {
    for (const AxisSize& s : sizes) {
        if (s.name.size() != 1) {
            fail("heinzel::rearrange: an axis size names a single-letter label");
        }
        const auto u = static_cast<std::size_t>(static_cast<unsigned char>(s.name[0]));
        if (label_id[u] == id) {
            return s.size;
        }
    }
    return -1;
}

// Where each group of a side starts, one bit per label in the order `flatten` lays the labels out.
constexpr LabelMask group_starts(const Term& t) {
    LabelMask starts = 0;
    std::int32_t label = 0;
    for (std::int32_t g = 0; g < t.rank; ++g) {
        starts = static_cast<LabelMask>(starts | (1u << label));
        label += t.groups[g].rank;
    }
    return starts;
}

// The side with every label an axis of its own, in `f`'s order: where a reordering spec's regroups and its transpose
// meet.
constexpr Term split_out(const Group& f) {
    Term t{};
    for (std::int32_t k = 0; k < f.rank; ++k) {
        Group one{};
        one.ids[one.rank++] = f.ids[k];
        push_group(t, one);
    }
    return t;
}

// Each label's size where a split names it, and `no_extent()` where the operand decides it: a group of one takes its
// axis's extent, and a split's one unnamed factor (a negative size names none) is worked out by the analysis.
constexpr Shape label_sizes(const Term& lhs, const std::int32_t (&label_id)[128], std::initializer_list<AxisSize> sizes) {
    Shape given{};
    for (std::int32_t g = 0; g < lhs.rank; ++g) {
        const Group& group = lhs.groups[g];
        for (std::int32_t k = 0; k < group.rank; ++k) {
            const std::int32_t s = group.rank == 1 ? -1 : size_for(group.ids[k], label_id, sizes);
            set_extent(given, given.rank++, s < 0 ? no_extent() : literal_extent(s));
        }
    }
    return given;
}

// The permutation a pure transpose reads: output axis k comes from the source position of the label the
// output names there. The streamed (last) label is last in both, so it maps to itself.
constexpr Axes transpose_order(const Group& la, const Group& lb) {
    Axes order{};
    for (std::int32_t k = 0; k < lb.rank; ++k) {
        order.axes[static_cast<std::size_t>(order.rank++)] = index_of(la, lb.ids[k]);
    }
    return order;
}

// The permutation that reads the reduce's survivors (in the operand's own order) as the output asks;
// identity when they already agree, in which case the caller emits no transpose.
constexpr Axes survivors_order(const Group& survivors, const Group& out, bool& identity) {
    Axes order{};
    identity = survivors.rank == out.rank;
    for (std::int32_t k = 0; k < out.rank; ++k) {
        const std::int32_t pos = index_of(survivors, out.ids[k]);
        order.axes[static_cast<std::size_t>(order.rank++)] = pos;
        if (pos != k) {
            identity = false;
        }
    }
    return order;
}

// Neither side of a `reduce` regroups — a fold names whole axes and drops some. A group there is a mistake
// pointing at `rearrange`.
constexpr void require_no_groups(const Term& t) {
    for (std::int32_t g = 0; g < t.rank; ++g) {
        if (t.groups[g].rank != 1) {
            fail("heinzel::reduce: a reduce does not regroup an axis; use rearrange for that");
        }
    }
}

// The public spelling (`heinzel::rearrange`, `heinzel::reduce(spec, …)`) never names the stream: each side
// gets one group of one label no spec can write, last, and is then read as this notation reads the same spec
// with it written out. So a side names at most `kMaxRank - 1` labels in all, counted across its groups on the
// text before anything is parsed (`flatten` lays every label of a side out in one group).
constexpr Parse parse_with_the_stream(std::string_view spec) {
    std::int32_t labels = 0;
    for (const char c : spec) {
        labels = c == '-' || c == ',' ? 0 : labels + (is_letter(c) ? 1 : 0); // (a `,` is the parser's to refuse)
        if (labels > static_cast<std::int32_t>(kMaxRank) - 1) {
            fail("heinzel: a spec names at most kMaxRank - 1 axes");
        }
    }
    Parse p = parse(spec);
    Group stream{};
    stream.ids[stream.rank++] = p.n_ids++;
    push_group(p.lhs, stream);
    push_group(p.rhs, stream);
    return p;
}

// One reshape from `from`'s groups to `to`'s over the same labels in the same order, its target the analysis's.
constexpr Expr regroup_sides(Expr x, const Term& from, const Term& to, const Parse& p,
                             std::initializer_list<AxisSize> sizes) {
    const Shape given = label_sizes(from, p.label_id, sizes);
    const LabelMask operand_groups = group_starts(from);
    const LabelMask result_groups = group_starts(to);
    return detail::regroup(x, given, operand_groups, result_groups);
}

constexpr Expr lower_rearrange(const Parse& p, Expr x, std::initializer_list<AxisSize> sizes) {
    const Group la = flatten(p.lhs);
    const Group lb = flatten(p.rhs);
    require_axes_conserved(la, lb);
    if (same_ids_in_order(la, lb)) { // same order, only the grouping differs — one reshape
        return regroup_sides(x, p.lhs, p.rhs, p, sizes);
    }
    // The labels reordered: one transpose, after a split where the left side groups and before a merge where the
    // right side does.
    const bool splits = p.lhs.rank != la.rank;
    const bool merges = p.rhs.rank != lb.rank;
    const Expr split = splits ? regroup_sides(x, p.lhs, split_out(la), p, sizes) : x;
    const Expr moved = detail::transpose(split, transpose_order(la, lb));
    return merges ? regroup_sides(moved, split_out(lb), p.rhs, p, sizes) : moved;
}

constexpr Expr lower_reduce(const Parse& p, Expr x, ReduceOp how) {
    require_no_groups(p.lhs);
    require_no_groups(p.rhs);
    const Group la = flatten(p.lhs);
    const Group lb = flatten(p.rhs);
    if (has_duplicate(la) || has_duplicate(lb)) {
        fail("heinzel::reduce: a repeated label (a diagonal) is not supported");
    }
    for (std::int32_t k = 0; k < lb.rank; ++k) {
        if (index_of(la, lb.ids[k]) < 0) {
            fail("heinzel::reduce: the output names an axis absent from the input");
        }
    }
    Axes drop{};
    Group survivors{};
    for (std::int32_t k = 0; k < la.rank; ++k) {
        if (index_of(lb, la.ids[k]) >= 0) {
            survivors.ids[static_cast<std::size_t>(survivors.rank++)] = la.ids[k];
        } else {
            drop.axes[static_cast<std::size_t>(drop.rank++)] = k;
        }
    }
    Expr r = drop.rank > 0 ? detail::reduce(x, how, drop) : x;
    bool identity = false;
    const Axes order = survivors_order(survivors, lb, identity);
    return identity ? r : detail::transpose(r, order);
}

} // namespace rearrange_detail

} // namespace heinzel::notation

namespace heinzel {

using notation::AxisSize;

// einops over the element axes: `rearrange("(h w) c -> h w c", x, {{"h", 4}})` regroups, `rearrange("a b ->
// b a", x)` transposes, `rearrange("h a w b -> h w (a b)", x)` does both, `reduce("a b -> a", x, ReduceOp::Max)`
// folds. The stream is never named.
constexpr Expr rearrange(std::string_view spec, Expr x, std::initializer_list<AxisSize> sizes = {}) {
    const notation::rearrange_detail::Parse p = notation::rearrange_detail::parse_with_the_stream(spec);
    detail::spec_names_every_axis(x, p.lhs.rank);
    const Expr made = notation::rearrange_detail::lower_rearrange(p, x, sizes);
    detail::record_spec_rank(made, x, 0, p.lhs.rank);
    return made;
}
constexpr Expr reduce(std::string_view spec, Expr x, ReduceOp how) {
    const notation::rearrange_detail::Parse p = notation::rearrange_detail::parse_with_the_stream(spec);
    detail::spec_names_every_axis(x, p.lhs.rank);
    const Expr made = notation::rearrange_detail::lower_reduce(p, x, how);
    detail::record_spec_rank(made, x, 0, p.lhs.rank);
    return made;
}

} // namespace heinzel
