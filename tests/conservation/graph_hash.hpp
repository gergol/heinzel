#pragma once

// Hashing an analysed graph.
//
// Oracle O5 asks whether today's analysed graph for a named graph is the one this tree produced at an
// earlier commit — a question no other check here can answer. A check that compares two paths of the
// same build stays green when a change moves both; only a recorded earlier graph can say that a decision
// of the analysis changed. (The loop tree's programs are pinned by the `tests/goldens/loops_*` listings.)
//
// The hash walks the fields rather than reading the bytes, which is what makes it work while
// compiling — there is no memcpy in a constant expression — and also what makes it honest: a field
// added to a graph and not added here would be silently outside the oracle, so the walk is written
// to be read against the type.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "heinzel/ir/ir.hpp"

namespace heinzel {

// FNV-1a, 64-bit. Chosen because it is four lines and needs no table, and because nothing here
// depends on it being cryptographic — this detects a changed decision, not a forged one.
inline constexpr std::uint64_t kHashSeed = 14695981039346656037ull;

constexpr std::uint64_t mix(std::uint64_t h, std::uint64_t value) {
    h ^= value;
    h *= 1099511628211ull;
    return h;
}

constexpr std::uint64_t mix(std::uint64_t h, double value) {
    // The bits, not the number: two doubles that compare equal but are not the same bits would be a
    // difference worth seeing, and a negative zero appearing where a positive one was is exactly
    // the kind of quiet change this oracle is for.
    return mix(h, std::bit_cast<std::uint64_t>(value));
}

constexpr std::uint64_t mix(std::uint64_t h, const Shape& s) {
    h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(s.rank)));
    for (std::int32_t k = 0; k < s.rank; ++k) {
        // The stored encoding, deliberately: a literal 64 and a symbol bound to 64 resolve to the
        // same extent and hash the same, which is correct — after binding they ARE the same shape.
        h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(extent_at(s, k).encoded)));
    }
    return h;
}

constexpr std::uint64_t mix(std::uint64_t h, const Axes& a) {
    h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(a.rank)));
    for (std::int32_t k = 0; k < a.rank; ++k) {
        h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(a.axes[k])));
    }
    return h;
}

constexpr std::uint64_t mix(std::uint64_t h, const Name& n) {
    const std::string_view text = n.view();
    h = mix(h, static_cast<std::uint64_t>(text.size()));
    for (const char c : text) {
        h = mix(h, static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
    }
    return h;
}

// A node, every field of it.
constexpr std::uint64_t mix(std::uint64_t h, const Node& n) {
    h = mix(h, static_cast<std::uint64_t>(n.op.index));
    for (std::size_t k = 0; k < kMaxNodeInputs; ++k) {
        h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(n.inputs[k])));
    }
    h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(n.output)));
    h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint8_t>(n.attr.kind)));
    h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint8_t>(n.attr.cast.to)));
    h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(n.attr.delay.samples)));
    h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(n.attr.recurrence.defines)));
    h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint8_t>(n.attr.reduce.op)));
    h = mix(h, n.attr.reduce.axes);
    h = mix(h, static_cast<std::uint64_t>(n.attr.reduce.keep_axes ? 1u : 0u));
    h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint8_t>(n.attr.view.kind)));
    h = mix(h, n.attr.view.shape);
    h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(n.attr.view.axis)));
    if (n.attr.view.operand_groups != 0) { // a regroup not yet analysed: mixed only when set, so no recorded hash moves
        h = mix(h, static_cast<std::uint64_t>(n.attr.view.operand_groups));
        h = mix(h, static_cast<std::uint64_t>(n.attr.view.result_groups));
    }
    if (n.attr.generator.first >= 0) { // (a constant's: mixed only when set, so no recorded hash moves)
        h = mix(h, static_cast<std::uint64_t>(n.attr.generator.first));
    }
    return h;
}

// ---- the parts a graph may not have ------------------------------------------------------------
// The runtime-resolved dimensions, and ONLY when there are any. A fully static graph declares none, so its hash is
// exactly what it was before this field existed and not one recorded hash moved — an oracle re-recorded on the commit
// that changes the representation it guards has been turned off for that commit. A declared dimension, or a change to
// one's constraints, DOES move this — and that is checked in `unit/dynamic_extent_test.cpp` by watching the hash follow
// a perturbed constraint, not asserted here. The constants' numbers likewise, only when there are any, the stated
// ranges (`unit/ranges_recorded_test.cpp` watches the hash follow one), and a capacity passed (`Graph::over`).
constexpr std::uint64_t mix(std::uint64_t h, const RangeMark& m) {
    h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(m.value)));
    h = mix(h, static_cast<std::uint64_t>(m.promised ? 1u : 0u));
    h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint8_t>(m.range.count)));
    for (std::size_t k = 0; k < static_cast<std::size_t>(m.range.count); ++k) {
        const Interval& p = m.range.pieces[k];
        h = mix(mix(h, p.lo), p.hi);
        h = mix(h, static_cast<std::uint64_t>((p.lo_open ? 1u : 0u) | (p.hi_open ? 2u : 0u)));
    }
    return h;
}

constexpr std::uint64_t mix_when_present(std::uint64_t h, const Graph& g) {
    if (g.dims.size() != 0) {
        h = mix(h, static_cast<std::uint64_t>(g.dims.size()));
        for (const DimDecl& d : g.dims) {
            h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(d.id)));
            h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(d.constraints.multiple_of)));
            h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(d.constraints.min)));
        }
    }
    if (g.constants.size() != 0) {
        h = mix(h, static_cast<std::uint64_t>(g.constants.size()));
        for (const std::uint64_t bits : g.constants) {
            h = mix(h, bits);
        }
    }
    if (g.ranges.size() != 0) {
        h = mix(h, static_cast<std::uint64_t>(g.ranges.size()));
        for (const RangeMark& m : g.ranges) {
            h = mix(h, m);
        }
    }
    if (g.over != Capacity::None) {
        h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint8_t>(g.over)));
    }
    return h;
}

// ---- the analysed graph ------------------------------------------------------------------------
// Every field of every part, so that adding a field to the graph and forgetting it here shows up as
// a walk that does not read like the type it is walking.
constexpr std::uint64_t hash_graph(const Graph& g) {
    std::uint64_t h = kHashSeed;

    h = mix(h, static_cast<std::uint64_t>(g.values.size()));
    for (const Value& v : g.values) {
        h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint8_t>(v.type)));
        h = mix(h, v.shape);
        h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint8_t>(v.category)));
        h = mix(h, v.constant);
        // Behind the identity the dims are: an unmarked, Normal or not-yet-analysed value mixes nothing,
        // so no recorded hash moved when precision arrived here; a High or a Fast one is seen.
        if (v.precision == numerics::Precision::High || v.precision == numerics::Precision::Fast) {
            h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint8_t>(v.precision)));
        }
        if (v.timeless) { // the same identity default: a value not declared a tensor mixes nothing
            h = mix(h, std::uint64_t{0x7e45'0a11});
        }
    }

    h = mix(h, static_cast<std::uint64_t>(g.nodes.size()));
    for (const Node& n : g.nodes) {
        h = mix(h, n);
    }

    h = mix(h, static_cast<std::uint64_t>(g.inputs.size()));
    for (const NamedValue& io : g.inputs) {
        h = mix(h, io.name);
        h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(io.value)));
    }
    h = mix(h, static_cast<std::uint64_t>(g.outputs.size()));
    for (const NamedValue& io : g.outputs) {
        h = mix(h, io.name);
        h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(io.value)));
    }

    h = mix_when_present(h, g);

    if (g.default_precision != numerics::Precision::Normal) {
        h = mix(h, static_cast<std::uint64_t>(static_cast<std::uint8_t>(g.default_precision)));
    }
    return h;
}

} // namespace heinzel
