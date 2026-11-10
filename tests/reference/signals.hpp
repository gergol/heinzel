#pragma once

// The signal store the walk computes into: a block of samples per element of each value, one plane per
// number per sample, with no sharing and no plan.

#include <array>
#include <cstddef>

#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fail.hpp"
#include "heinzel/support/fixed_vector.hpp"

#include "elements.hpp"

namespace heinzel {

// How many planes a walked graph may hold by default: one per number per sample, summed over its values.
// It is a ceiling on a graph's size and buys nothing else, so it may be raised; the count grows linearly
// with a graph's nodes. The largest default-sized graphs in the tests need 322 planes (the both-ways
// corpus's axis-changing graph, `tests/support/both_ways.hpp`) and 603 (the softmax gradient golden,
// `tests/unit/gradient_golden_test.cpp`: the reverse pass roughly doubles a graph, and each broadcast
// spends a wide plane). A training graph needs far more (`fir_fit` 3205 planes, `mlp_train` about 7000),
// and the planes multiply with `BlockSize`, so such a store is made with its own count on the heap:
// `std::make_unique<Signals<B, 8192>>()`.
inline constexpr std::size_t kMaxPlanes = 768;

// One block of samples per ELEMENT of each value. A value's elements are consecutive planes starting
// at `first[value]`; a graph in which every value is one number wide therefore has `first[v] == v`.
//
// Fixed size, so this allocates nothing and works during constant evaluation as readily as at run
// time.
template <std::size_t BlockSize, std::size_t Planes = kMaxPlanes>
struct Signals {
    std::array<std::array<double, BlockSize>, Planes> at{};
    fixed_vector<std::int32_t, kMaxValues> first{};
};

// Where a value's elements begin. Filled by `run` from the graph alone: it is layout, not a plan.
template <std::size_t BlockSize, std::size_t Planes>
constexpr void lay_out(const Graph& g, Signals<BlockSize, Planes>& out) {
    // Cleared rather than appended to: `run` on a workspace that has already been run would
    // otherwise leave every value indexed past its own planes, which is a wrong number and not a
    // failure.
    out.first.clear();
    std::int32_t next = 0;
    for (const Value& v : g.values) {
        out.first.push_back(next);
        next += elements_of(v.shape).count;
    }
    if (static_cast<std::size_t>(next) > Planes) {
        fail("heinzel::run: this graph needs more planes than its Signals hold");
    }
}

// One element of one value at one sample. The reader every caller of `run` should use: indexing
// `at` directly is only right while every value is one number wide.
template <std::size_t BlockSize, std::size_t Planes>
constexpr double sample_at(const Signals<BlockSize, Planes>& out, std::int32_t value, std::int32_t element,
                           std::size_t n) {
    return out.at[static_cast<std::size_t>(out.first[static_cast<std::size_t>(value)] + element)][n];
}

namespace detail {

template <std::size_t BlockSize, std::size_t Planes>
constexpr void write(Signals<BlockSize, Planes>& out, std::int32_t value, std::int32_t element, std::size_t n,
                     double sample) {
    out.at[static_cast<std::size_t>(out.first[static_cast<std::size_t>(value)] + element)][n] = sample;
}

} // namespace detail

} // namespace heinzel
