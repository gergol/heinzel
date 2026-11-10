// The vocabulary: element types, shapes, the dimension encoding, and operation identity.
//
// All of it is asserted during compilation, because all of it exists to be usable during
// compilation. The encoding assertions are the ones worth reading twice: a shape axis holds either
// a literal size or a symbol in the same integer, and every claim about which is which is checked
// here rather than trusted.
#include "heinzel/ir/vocab.hpp"

#include <string_view>

using namespace heinzel;

// ---- element types -----------------------------------------------------------------------------
static_assert(elem_size(ElemType::F32) == 4);
static_assert(elem_size(ElemType::F64) == 8);

// Unknown is the zero value, so a value nobody has annotated starts out honestly unresolved rather
// than silently single-precision.
static_assert(ElemType{} == ElemType::Unknown, "an unannotated element type is Unknown, not F32");

// ---- the dimension encoding --------------------------------------------------------------------
static_assert(!is_symbol(0) && !is_symbol(256), "a literal extent is not a symbol");
static_assert(!is_symbol(-1), "-1 is reserved for the infer-this-axis marker, never a symbol");
static_assert(is_symbol(-2) && is_symbol(-3));

// Round-tripping is the property everything else leans on.
static_assert(symbol_of(encode_symbol(0)) == 0);
static_assert(symbol_of(encode_symbol(7)) == 7);
static_assert(is_symbol(encode_symbol(0)), "symbol 0 must encode clear of the reserved -1");

// ---- shapes ------------------------------------------------------------------------------------
static_assert(Shape{}.rank == 0, "no axes is a scalar");
static_assert(Shape{4, 8}.rank == 2 && literal_of(extent_at(Shape{4, 8}, 0)) == 4 &&
              literal_of(extent_at(Shape{4, 8}, 1)) == 8);
static_assert(Shape{4, 8} == Shape{4, 8});
static_assert(!(Shape{4, 8} == Shape{8, 4}), "axis order is part of a shape");

static_assert(is_static(Shape{}) && is_static(Shape{4, 8}));

// A symbolic axis writes the encoding, and the shape knows it is no longer fully known.
static_assert([] {
    constexpr Dim n{3};
    const Shape s{n, 8};
    return s.rank == 2 && is_symbol(extent_at(s, 0)) && symbol_of(extent_at(s, 0)) == 3 &&
           literal_of(extent_at(s, 1)) == 8 && !is_static(s);
}(), "a shape mixes symbolic and literal axes in one array");

// ---- reading an extent through the helpers -----------------------------------------------------
//
// The helpers milestone M2.7's gate exists to force the rest of the tree through. Their whole
// purpose is that no other file knows the encoding, so the encoding is asserted against them here.

// The one conversion from a stored extent, and it must NOT go through Extent's constructor: the
// constructor takes a size, so handing it an encoded symbol would read as an enormous negative one.
static_assert(is_symbol(extent_from_encoded(encode_symbol(3))) && symbol_of(extent_from_encoded(encode_symbol(3))) == 3);
static_assert(is_literal(extent_from_encoded(64)) && literal_of(extent_from_encoded(64)) == 64);
static_assert(literal_of(literal_extent(64)) == 64);

// `is_literal` and `is_symbol` answer the same question from opposite sides, for both kinds.
static_assert(is_literal(literal_extent(0)) && !is_symbol(literal_extent(0)), "an extent of zero is a size");
static_assert(is_symbol(Extent{Dim{0}}) && !is_literal(Extent{Dim{0}}), "symbol 0 is a symbol");

// `same_extent` is "the same extent", not "the same number". Two DIFFERENT symbols are not the same
// extent even though they may later turn out to name one size — deciding that is the solver's job,
// and a helper that guessed would decide it silently and wrongly.
static_assert(same_extent(literal_extent(8), literal_extent(8)));
static_assert(!same_extent(literal_extent(8), literal_extent(4)));
static_assert(same_extent(Extent{Dim{1}}, Extent{Dim{1}}));
static_assert(!same_extent(Extent{Dim{0}}, Extent{Dim{1}}), "two symbols are not the same extent yet");
static_assert(!same_extent(Extent{Dim{0}}, literal_extent(8)));

// Reading and writing an axis of a shape, with no file outside the vocabulary touching `dims`.
static_assert([] {
    Shape s{4, 8};
    set_extent(s, 1, Extent{Dim{2}});
    return is_symbol(extent_at(s, 1)) && symbol_of(extent_at(s, 1)) == 2 && literal_of(extent_at(s, 0)) == 4;
}(), "set_extent and extent_at are the only way in and out of a shape's axes");

// Shown REFUSING, not merely shown working: asking a symbol for its size is a mistake in the caller
// and it stops at the line that made it rather than returning something plausible. Under constant
// evaluation that refusal is a compile error by construction, which is precisely why it cannot be a
// static_assert here — the same reason support_test.cpp gives for fail(). This covers the runtime
// half, and with it the message the caller actually reads.
namespace {

bool asking_a_symbol_for_its_size_is_refused() {
    try {
        (void)literal_of(Extent{Dim{0}});
    } catch (const error& e) {
        return e.message() ==
               std::string_view{"heinzel::literal_of: this extent is a symbol resolved later, so it has no size yet"};
    }
    return false;
}

} // namespace

// ---- axis sets ---------------------------------------------------------------------------------
static_assert(Axes{}.rank == 0);
static_assert(Axes{0, 2}.rank == 2 && Axes{0, 2}.axes[1] == 2);

// ---- operation identity ------------------------------------------------------------------------
static_assert(OpName{"_add"}[0] == '_' && OpName{"_add"}[1] == 'a');
static_assert(OpName{"_add"} == OpName{"_add"});
static_assert(!(OpName{"_add"} == OpName{"_sub"}));

// The stored identity is one byte, because every byte of a graph appears in a mangled symbol name.
static_assert(sizeof(OpCode) == 1);
static_assert(op::Add == OpCode{0} && op::Reshape == OpCode{12});
static_assert(!(op::Add == op::Sub));

// ---- names -------------------------------------------------------------------------------------
// One name type across the tree: this is the same type the build context composes into.
static_assert(Name{"cutoff"}.view() == std::string_view{"cutoff"});
static_assert(Name{}.length() == 0);

int main() {
    if (!asking_a_symbol_for_its_size_is_refused()) return 1;
    return 0;
}
