#pragma once

// Checks that the rule table and the written arithmetic agree, in both directions. A row with no meaning
// would be a `fail()` the first time a graph holding that operation runs; a meaning with no row would read
// past the end of the table. The walk includes this file, so every build that can run the reference has
// passed these checks. They cost about 3 ms per translation unit (119 ms against 116 ms syntax-only), a
// fixed cost: the second predicate walks the one-byte operation code space, whatever the graph.

#include <cstdint>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"

#include "compute_reference.hpp"

namespace heinzel {

// Probing one operation code. A node with nothing else filled in is enough: the writing reads an
// attribute only in the branch that carries one, and no branch reads the operands' values to decide
// whether it has an answer.
//
// Out of the way in `detail` because it is not a question anybody should be asking: it answers for a
// fabricated node, and a caller who found it and used it to "compute" something would get a number
// for operands that were never theirs.
namespace detail {
constexpr Computed probe_meaning(std::uint8_t code) {
    Node probe{};
    probe.op = OpCode{code};
    // At the exact tier, because the question is whether a meaning is WRITTEN and not how accurate
    // it is. A probe at a coarse tier would answer the same for every operation the table covers,
    // and would additionally make the coverage check depend on the ladder having that rung.
    return compute_if_written(probe, FnAccuracy::Exact, 1.0, 1.0, 1.0);
}
} // namespace detail

// Every row that says it has a per-sample meaning has one written, in `runtime/compute.hpp` or
// `compute_reference.hpp`.
//
// Each check is a named predicate over an explicit argument rather than an inline assertion: a check that
// has only ever been seen to pass is no evidence that it would fail, and a predicate can be handed a
// deliberately broken table. `tests/unit/meaning_test.cpp` hands each one such a table, and
// `tests/gates/refusals/` requires the build to refuse.
constexpr bool every_per_sample_operation_has_a_meaning(const OpTableRow* rows, std::uint8_t count) {
    for (std::uint8_t i = 0; i < count; ++i) {
        if (rows[i].rules.meaning != MeaningRule::PerSample) {
            continue;
        }
        if (!detail::probe_meaning(i).written) {
            return false;
        }
    }
    return true;
}

static_assert(every_per_sample_operation_has_a_meaning(kOpTable, kOpCount),
              "an operation whose row says it has a per-sample meaning has none written in "
              "runtime/compute.hpp or compute_reference.hpp — write the arithmetic, or classify the row "
              "MeaningRule::AxisChanging");

// And nothing there answers for an operation code the table has no row for: one past its end, or a retired code
// inside it, whose row is empty (`is_retired`). The scan runs to the end of the code space rather than to a chosen
// ceiling: an operation code is one byte, so 256 IS the whole space and not a number somebody picked.
constexpr bool no_meaning_beyond(std::uint8_t count) {
    for (int code = 0; code < 256; ++code) {
        const bool rowless = code >= count || is_retired(OpCode{static_cast<std::uint8_t>(code)});
        if (rowless && detail::probe_meaning(static_cast<std::uint8_t>(code)).written) {
            return false;
        }
    }
    return true;
}

static_assert(no_meaning_beyond(kOpCount),
              "arithmetic is written in runtime/compute.hpp or compute_reference.hpp for an operation "
              "code that has no row in kOpTable, or a retired one — add the row and update kOpCount, or delete "
              "the arithmetic");

// And the other half of the meaning column: every row that says it changes axes owes an element meaning,
// and `element_kind_of` (`compute_reference.hpp`) says which kind it has. Asked through that function
// rather than against a list kept here, so this file names no operation and cannot drift from the walk.
constexpr int axis_changing_rows_without_an_element_meaning(const OpTableRow* rows, std::uint8_t count) {
    int owed = 0;
    for (std::uint8_t i = 0; i < count; ++i) {
        if (rows[i].rules.meaning == MeaningRule::AxisChanging &&
            element_kind_of(OpCode{i}) == ElementKind::None) {
            ++owed;
        }
    }
    return owed;
}

// The third claim the table makes about the writing, which `Approximate` rows have a core in the
// value's own type, is about `include/` and is checked there, in `runtime/compute.hpp`.

static_assert(axis_changing_rows_without_an_element_meaning(kOpTable, kOpCount) == 0,
              "an operation whose row says it changes axes has no element meaning written in "
              "compute_reference.hpp — write the fold or the rearrangement and say which kind it is in "
              "element_kind_of, or classify the row MeaningRule::PerSample and write its arithmetic");

} // namespace heinzel
