#pragma once

// The rewrite layer's rule for a value written twice (Plan/33-knobs-that-move S1, backlog B-9): two nodes of one
// operation over the same values, of one attribute and one result type, shape and precision, are one node, where two
// literals of one type, shape and precision whose bits agree are the same value (-0 and +0, and NaNs of two payloads,
// are not, as their bits are not). A copy each reader makes of a knob's work (Freeverb's `1 - damp` in each comb)
// otherwise stands as a map of its own that moves every sample, and the planner closes the comb stage around each: 33
// stages against 11, 52.4 against 27.2 ms (`bench_compare_cmajor`, the M1). The merged node computes the same numbers,
// so it changes no rounding and fires at every precision. A literal two nodes read that are not one stays each node's:
// a literal is an immediate, nothing to compute once. It
// names no operation: whether two nodes are one value is the rule table's `Sharing` column, which keeps an output's
// materialisation and a variable's definition apart. A value with a range mark is never merged (two marks are two
// checks), nor an output into another value (two names, one buffer). One pass in the nodes' order, against a table of
// fixed size: linear. The last rule of `rewritten.hpp`, so that the rules see the graph as written: a lag two sections
// read, merged, hides a cascade from the parallel form (`dsp/cascade4`, whose second section reads the first's
// `y1[n-1]`), and a product two sums read fuses into neither (`product_adds.hpp`).

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"

namespace heinzel::rewrite {
namespace detail {

// The table's slots: twice the nodes and values a graph holds, so that a probe soon finds a free slot.
inline constexpr std::size_t kSeenSlots = 2 * (kMaxNodes + kMaxValues);

constexpr std::uint64_t mixed(std::uint64_t h, std::uint64_t x) {
    h = (h ^ x) * 0x9E3779B97F4A7C15ULL;
    return h ^ (h >> 29);
}

// What was seen, by its hash: each slot empty (-1) or the index of the first of its kind, probed in turn.
struct Seen {
    std::array<std::int32_t, kSeenSlots> slots{};

    constexpr Seen() { slots.fill(-1); }

    // The first entry `same` finds equal to `id`, hashed `h`; `id` itself, recorded, where there is none.
    template <class Same>
    constexpr std::int32_t first(std::uint64_t h, std::int32_t id, Same same) {
        for (std::size_t s = static_cast<std::size_t>(h % kSeenSlots);; s = (s + 1) % kSeenSlots) {
            if (slots[s] < 0) {
                slots[s] = id;
                return id;
            }
            if (same(slots[s])) {
                return slots[s];
            }
        }
    }
};

constexpr std::uint64_t hash_of(const Value& v) {
    std::uint64_t h = mixed(static_cast<std::uint64_t>(v.type), static_cast<std::uint64_t>(v.precision));
    for (std::int32_t k = 0; k < v.shape.rank; ++k) {
        h = mixed(h, static_cast<std::uint32_t>(v.shape.dims[k]));
    }
    return mixed(h, std::bit_cast<std::uint64_t>(v.constant));
}

constexpr std::uint64_t mixed(std::uint64_t h, const Axes& a) {
    for (std::int32_t k = 0; k < a.rank; ++k) {
        h = mixed(h, static_cast<std::uint32_t>(a.axes[k]));
    }
    return mixed(h, static_cast<std::uint32_t>(a.rank));
}

constexpr std::uint64_t mixed(std::uint64_t h, const Shape& s) {
    for (std::int32_t k = 0; k < s.rank; ++k) {
        h = mixed(h, static_cast<std::uint32_t>(s.dims[k]));
    }
    return mixed(h, static_cast<std::uint32_t>(s.rank));
}

// Every field of an attribute, each at its default where its kind does not read it: else the nodes that differ only
// there (every lag of one value, every slice of it at another offset) share one hash, and the probe runs over all of
// them for each, quadratic (an FIR of 1000 taps, 7.6 ms at run time).
constexpr std::uint64_t hash_of(const OpAttr& a) {
    const auto u = [](std::int64_t x) { return static_cast<std::uint64_t>(x); };
    const ViewAttr& v = a.view;
    std::uint64_t h = mixed(u(static_cast<std::int64_t>(a.kind)), u(static_cast<std::int64_t>(a.cast.to)));
    for (const std::int64_t x : {std::int64_t{a.delay.samples}, std::int64_t{a.recurrence.defines}, std::int64_t{a.signalled_delay.max_delay},
                                 static_cast<std::int64_t>(a.reduce.op), std::int64_t{a.reduce.keep_axes}, static_cast<std::int64_t>(v.kind),
                                 std::int64_t{v.axis}, std::int64_t{v.offset}, std::int64_t{v.length}, std::int64_t{v.step}, std::int64_t{v.window},
                                 std::int64_t{v.hop}, std::int64_t{v.axis2}, std::int64_t{v.window2}, std::int64_t{v.hop2}, std::int64_t{v.pad_before},
                                 std::int64_t{v.pad_after}, std::int64_t{a.matmul.a_contract}, std::int64_t{a.matmul.b_contract}, std::int64_t{a.fft.n},
                                 static_cast<std::int64_t>(a.fft.dir), std::int64_t{a.overlap.window}, std::int64_t{a.overlap.hop},
                                 static_cast<std::int64_t>(a.order.op), std::int64_t{a.order.axis}, std::int64_t{a.order.k}, std::int64_t{a.table.size},
                                 static_cast<std::int64_t>(a.generator.type), std::int64_t{a.generator.first}}) {
        h = mixed(h, u(x));
    }
    h = mixed(mixed(mixed(mixed(h, a.reduce.axes), v.shape), v.order), a.contract.axes);
    h = mixed(mixed(mixed(h, a.matmul.batch_a), a.matmul.batch_b), a.generator.shape);
    return mixed(mixed(h, v.pad_bits), std::bit_cast<std::uint64_t>(a.order.q));
}

// Two literals that are one number: their bits, not `==`, so that -0 is not 0 and a NaN is itself.
constexpr bool same_literal(const Value& a, const Value& b) {
    return a.type == b.type && a.shape == b.shape && a.precision == b.precision && a.timeless == b.timeless &&
           std::bit_cast<std::uint64_t>(a.constant) == std::bit_cast<std::uint64_t>(b.constant);
}

// What a pass reads of `g`: per value, whether a range mark names it (1), which keeps it apart, or it is an output (2),
// which is merged into no other value, though another may be merged into it; and what an operand is to a node's
// equality, the first literal of its number for a literal (one not marked), itself otherwise.
struct Sameness {
    std::vector<char> held;
    std::vector<std::int32_t> number;
};

constexpr Sameness sameness_of(const Graph& g) {
    Sameness r{std::vector<char>(g.values.size(), 0), std::vector<std::int32_t>(g.values.size())};
    for (const NamedValue& io : g.outputs) {
        r.held[static_cast<std::size_t>(io.value)] = 2;
    }
    for (const RangeMark& m : g.ranges) {
        r.held[static_cast<std::size_t>(m.value)] = 1;
    }
    Seen seen{};
    for (std::size_t v = 0; v < g.values.size(); ++v) {
        const Value& value = g.values[v];
        const auto id = static_cast<std::int32_t>(v);
        const auto same_number = [&](std::int32_t w) { return same_literal(g.values[static_cast<std::size_t>(w)], value); };
        const bool literal = value.category == ValueCategory::Const && r.held[v] != 1;
        r.number[v] = literal ? seen.first(hash_of(value), id, same_number) : id;
    }
    return r;
}

constexpr bool same_node(const Graph& g, const Sameness& r, const Node& a, const Node& b) {
    bool same = a.op == b.op && a.attr == b.attr &&
                g.values[static_cast<std::size_t>(a.output)] == g.values[static_cast<std::size_t>(b.output)];
    for (std::size_t k = 0; k < rules_of(a.op).arity; ++k) {
        same = same && r.number[static_cast<std::size_t>(a.inputs[k])] == r.number[static_cast<std::size_t>(b.inputs[k])];
    }
    return same;
}

// The nodes of `g` with their operands read as `same` maps them, each node equal to one before it left out, its value
// mapped to that one's in `same`.
constexpr std::vector<Node> first_nodes(const Graph& g, const Sameness& r, std::vector<std::int32_t>& same) {
    std::vector<Node> kept;
    Seen seen{};
    for (Node n : g.nodes) {
        std::uint64_t h = mixed(n.op.index, hash_of(n.attr));
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            n.inputs[k] = same[static_cast<std::size_t>(n.inputs[k])];
            h = mixed(h, static_cast<std::uint32_t>(r.number[static_cast<std::size_t>(n.inputs[k])]));
        }
        const auto out = static_cast<std::size_t>(n.output);
        if (rules_of(n.op).sharing == Sharing::OneValue && r.held[out] != 1) {
            const auto id = static_cast<std::int32_t>(kept.size());
            const auto same_value = [&](std::int32_t j) { return same_node(g, r, kept[static_cast<std::size_t>(j)], n); };
            const std::int32_t first = seen.first(mixed(h, hash_of(g.values[out])), id, same_value);
            if (first != id && r.held[out] == 0) {
                same[out] = kept[static_cast<std::size_t>(first)].output;
                continue;
            }
        }
        kept.push_back(n);
    }
    return kept;
}

// Per value of `g`, kept as `nodes` read it: not one `same` maps to another, and not a literal that only a node left
// out read.
constexpr std::vector<char> kept_values(const Graph& g, const Sameness& r, const std::vector<Node>& nodes,
                                        const std::vector<std::int32_t>& same) {
    std::vector<char> read_after(g.values.size(), 0);
    for (const Node& n : nodes) {
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            read_after[static_cast<std::size_t>(n.inputs[k])] = 1;
        }
    }
    std::vector<char> read_before(g.values.size(), 0);
    for (const Node& n : g.nodes) {
        for (std::size_t k = 0; k < rules_of(n.op).arity; ++k) {
            read_before[static_cast<std::size_t>(n.inputs[k])] = 1;
        }
    }
    std::vector<char> kept(g.values.size(), 0);
    for (std::size_t v = 0; v < g.values.size(); ++v) {
        const bool dropped_literal = g.values[v].category == ValueCategory::Const && r.held[v] == 0 && read_before[v] != 0 && read_after[v] == 0;
        kept[v] = same[v] == static_cast<std::int32_t>(v) && !dropped_literal ? 1 : 0;
    }
    return kept;
}

// `g` with `nodes` and the values `kept`, every reference renumbered densely, in order.
constexpr void compact(Graph& g, const std::vector<Node>& nodes, const std::vector<char>& kept, const std::vector<std::int32_t>& same) {
    std::vector<std::int32_t> number(kept.size(), kNoValue);
    std::vector<Value> values;
    for (std::size_t v = 0; v < kept.size(); ++v) {
        if (kept[v] != 0) {
            number[v] = static_cast<std::int32_t>(values.size());
            values.push_back(g.values[v]);
        }
    }
    const auto renumbered = [&](std::int32_t v) {
        return v < 0 ? v : number[static_cast<std::size_t>(same[static_cast<std::size_t>(v)])];
    };
    g.values.clear();
    for (const Value& value : values) {
        g.values.push_back(value);
    }
    g.nodes.clear();
    for (Node n : nodes) {
        for (std::int32_t& in : n.inputs) {
            in = renumbered(in);
        }
        n.output = renumbered(n.output);
        n.attr.recurrence.defines = renumbered(n.attr.recurrence.defines);
        g.nodes.push_back(n);
    }
    for (NamedValue& io : g.inputs) {
        io.value = renumbered(io.value);
    }
    for (NamedValue& io : g.outputs) {
        io.value = renumbered(io.value);
    }
    for (RangeMark& m : g.ranges) {
        m.value = renumbered(m.value);
    }
}

} // namespace detail

// Each value of analysed graph `g` computed once: a node equal to one before it merged into that one, its readers
// reading that one's value, and a literal only such a node read dropped. A graph with no two equal is left as it is.
constexpr void compute_equal_values_once(Graph& g) {
    const detail::Sameness r = detail::sameness_of(g);
    std::vector<std::int32_t> same(g.values.size());
    for (std::size_t v = 0; v < same.size(); ++v) {
        same[v] = static_cast<std::int32_t>(v);
    }
    const std::vector<Node> nodes = detail::first_nodes(g, r, same);
    if (nodes.size() < g.nodes.size()) {
        detail::compact(g, nodes, detail::kept_values(g, r, nodes, same), same);
    }
}

} // namespace heinzel::rewrite
