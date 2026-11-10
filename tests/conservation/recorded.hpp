#pragma once

// The recorded analysed graphs: what this tree decided, at a named earlier commit of THIS tree.
//
// Conservation, and not consistency. A check that compares two paths of one build stays green when a
// change moves both; this is the one thing here that can say a DECISION of the analysis CHANGED — and
// only within one tree and one toolchain, which is why the commit and the toolchain are written down
// beside the numbers.
//
// THE RULE THAT KEEPS THIS HONEST. A change that deliberately alters the graph type breaks every hash
// at once. When that happens the corpus is re-recorded IN THE SAME COMMIT AS THE CHANGE, with the
// reason in the commit message. A wholesale break with no such re-record is a defect. A re-record with
// no stated reason is worse, because it is how a conservation oracle is quietly turned off.
//
// Use `tools/record_conservation_hashes.cpp` to produce the block below.

#include <cstdint>

#include "corpus.hpp"
#include "graph_hash.hpp"

namespace heinzel {

struct RecordedGraph {
    Name name{};
    std::uint64_t graph{};
};

// ---- the record ----------------------------------------------------------------------------------
//
//   recorded at   173f2fc  ("Every column that describes meaning has a reader; a price is not a
//                           meaning") — the commit that finished widening the graph type with a
//                           value's units
//   toolchain     clang 22.1.8, x86_64-unknown-linux-gnu
//   standard      C++26 (-std=c++2c), -fconstexpr-steps=268435456
//
// Entries 7-10 were appended when the corpus grew with the operation set, and entry 11 when the
// rewrite's decision was put under the oracle; each append left every earlier number byte for byte
// what it was. Entry 11 went with `opt/rewrite.hpp` (Plan/17 03), the others keeping their numbers.
//
// THE ONE RE-RECORD, of the four biquad entries (0, 1, 3, 6). `biquad` wrote `x + a*p1 + b*p2` inline,
// and the two products are the operands of `operator+`, whose evaluation order C++ leaves unsequenced.
// Each operand's evaluation is what inserts its const/mul nodes into the arena, so a compiler free to
// evaluate the right operand first lays the arena down in a different order — and gcc does exactly that
// on its runtime path while its constexpr path (and both of clang's) picks the other order. The arena
// order is IN the graph, so the hash saw it (`docs/decisions.md`, finding Q-2/GCC-ARENA-ORDER). The fix
// names each product before combining, which gives one order on every path; that order is a third one,
// so the four biquad entries moved together and the other eight did not.
//
// THE SECOND AND THIRD RE-RECORDS, of every entry, when the units check and then `Graph::sample_rate` went
// (Decision 59): the hash stopped mixing a value's units, then the graph's sample rate, so every number
// moved at once, twice. No graph did: each time, under the new hash, every entry built in the tree before
// the change hashed to the new number. Entry 4 was the units-annotated graph; it lost its units and kept
// its index.
//
// THE FOURTH, of entries 0-4 and 6, when `ValueCategory::Output` went (Plan/17 03): a result is named in the
// graph's outputs only, so a value the frontend used to mark `Output` keeps the category it had, and `Const`
// and `Variable` moved down one. The entries built by hand never marked one and have neither, so they kept
// their numbers. Under a hash that read the old graphs that way, every entry hashed to its new number.
//
// THE FIFTH, of entry 10 alone, when `_delay` folded into `_fdelay` (Plan/17 03): its lag that named its
// axis is the fixed tap it always computed. The old entry with that one node written as `_fdelay` hashed
// to the new number.

// Recorded by tools/record_conservation_hashes.cpp.
inline constexpr RecordedGraph kRecorded[kCorpusSize] = {
    {"biquad", 0x6bafe8b63e3b41cfull},
    {"biquad again", 0x6bafe8b63e3b41cfull},
    {"comb", 0xa64750aec0ad7e6aull},
    {"biquad, block bound to 64", 0x6bafe8b63e3b41cfull},
    {"an input read twice", 0xd177768558483704ull},
    {"a reduction over one axis", 0xf6f9089e15f216bfull},
    {"biquad once more", 0x6bafe8b63e3b41cfull},
    {"the elementwise set at M4", 0x79e6d0f50020f4cdull},
    {"the transcendental set at M4", 0xa6cc3c16adb399c3ull},
    {"a contraction of two operands", 0x936dc0befc33f955ull},
    {"the two lags that arrived at M4", 0x92081a4375d0eee0ull},
};

} // namespace heinzel
