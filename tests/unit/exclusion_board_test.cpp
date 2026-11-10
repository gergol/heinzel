// Milestone M5.9, ORACLE O9 — the exclusion board asserted as a WHOLE.
//
// The old tree's `docs/limitations.md` carries twenty data rows (`git show main:docs/limitations.md`
// at `c4012b8`, the table body `:18-37`, counted rather than quoted). M5.9 asks that each is
// dispositioned here as exactly one of: IMPLEMENTED (an exclusion that silently disappeared would be a
// parity break in the other direction), EXCLUDED (failing at a named point, its message carrying the
// decision), or NOT APPLICABLE (with the reason and the milestone that makes it live). Every "excluded"
// row has a check SHOWN FAILING, and the board is asserted so that a check which quietly stops
// rejecting — the single most dangerous parity break, because nothing else in a build notices it —
// fails the build.
//
// The per-operation tests already show each individual refusal failing against its exact message
// (`view_inference_test` for the negative-stride slice, `order_test` for the order-statistic refusals,
// `spectral_fft_test`/`symbolic_test` for the symbolic-edge refusals). THIS file is the WHOLE-BOARD
// step the others are not: it fixes the count at twenty, gives every row a disposition so one cannot
// vanish unnoticed, and re-points each representable refusal so the guard flips the moment the pass
// starts accepting what it must reject.
//
// WHY THIS LIVES IN `tests/`. The board names operation codes and vocabulary types directly — the
// `passthrough_audit_test` precedent — which `gate_one_writing_per_operation` and
// `gate_no_per_op_branches` forbid under `include/`. A parity-style reference that names operations is
// exactly what a test may hold.
//
// ITS BLIND SPOT, said plainly (as the other gates state theirs, and recorded in `docs/decisions.md`).
// The gate proves a rejection EXISTS and still fires; it does NOT prove the rejection is FAITHFUL to
// the old tree's exact behaviour — that faithfulness is each operation's own both-ways and
// shown-rejecting work. For the UNREPRESENTABLE rows (no node spells the excluded thing) the guard is
// a structural assertion that the spelling still does not exist; an enumerator APPENDED past the
// recorded maximum of an enum is not detected here (the operation tests that switch exhaustively on the
// enum are that backstop), but a changed field type, a grown attribute, or a newly named operation is.

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "heinzel/ir/ir.hpp"
#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ir/vocab.hpp"
#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"

using namespace heinzel;

namespace {

// ---- the board as a value ---------------------------------------------------------------------------
//
// One disposition per row. `Unspecified` is the default and the sentinel: a row left at it is a row
// nobody dispositioned, and `board_is_complete` refuses it — the "every row has a disposition" half of
// M5.9, shown objecting below with a fabricated board.
enum class Disposition : std::uint8_t {
    Unspecified,
    Excluded,        // failing at a named point — a pass diagnostic or a `fail()`, shown below
    Unrepresentable, // excluded because no node spells it; the guard is that the spelling stays absent
    NotApplicable,   // the mechanism it attaches to is not built at M5; a milestone names where it lives
};

struct BoardRow {
    std::string_view row;        // the old-tree row, named so a reader can line it up with :18-37
    Disposition disposition;
    std::string_view milestone;  // non-empty only for NotApplicable: where the row becomes live
};

// The twenty data rows of `git show main:docs/limitations.md` at `c4012b8`, IN ORDER (`:18-37`). The
// count twenty is the old tree's, recorded from a frozen commit that is read-only and never changes, so
// the constant is stable; the `kOldTreeRowCount` assertion below is what makes a row silently dropped
// from this array a build failure. There is no IMPLEMENTED row: every one is a genuine exclusion the
// old tree also made, which is itself the M5.9 finding that nothing here quietly re-admitted an
// excluded operation.
constexpr BoardRow kBoard[] = {
    {"coupled multi-axis recurrence", Disposition::Unrepresentable, {}},        // 1
    {"negative-stride views", Disposition::Excluded, {}},                       // 2
    {"gather / scatter / index_select", Disposition::Unrepresentable, {}},      // 3
    {"argsort / sort-indices / topk-indices", Disposition::Unrepresentable, {}},// 4
    {"symbolic order extent", Disposition::Excluded, {}},                       // 5
    {"ordered extent over the ceiling", Disposition::Excluded, {}},             // 6
    {"runtime q / runtime k", Disposition::Unrepresentable, {}},                // 7
    {"mode", Disposition::Unrepresentable, {}},                                 // 8
    {"multi-axis order statistics", Disposition::Unrepresentable, {}},          // 9
    {"order statistics on the GPU", Disposition::NotApplicable, "device placement tranche"}, // 10
    {"pad reflect / edge modes", Disposition::Unrepresentable, {}},             // 11
    {"frac_delay off the innermost axis", Disposition::Unrepresentable, {}},    // 12
    {"einsum diagonal / ellipsis / implicit output", Disposition::NotApplicable, "no einsum frontend"}, // 13
    {"contraction axes over kMaxAxes (nest capacity)", Disposition::NotApplicable, "M6"}, // 14
    {"symbolic extents on derived / kernel edges", Disposition::Excluded, {}},  // 15
    {"rank over kMaxRank", Disposition::Excluded, {}},                          // 16
    {"integer / bool / fixed-point element types", Disposition::Unrepresentable, {}}, // 17
    {"runtime graph-structure mutation", Disposition::NotApplicable, "structural by design"}, // 18
    {"autodiff through an order statistic", Disposition::Excluded, {}},         // 19 — live at M7 (M7.6)
    {"autodiff through stateful / dynamic paths", Disposition::Excluded, {}},    // 20 — live at M7 (M7.6)
};

// The old tree's row count, recorded from `c4012b8`. A row dropped from `kBoard` fails HERE.
constexpr std::size_t kOldTreeRowCount = 20;

// A row is dispositioned when it is not the sentinel AND, if it is NotApplicable, names the milestone
// or reason that makes it live — an unexplained "not applicable" is the quiet gap M5.9 forbids.
constexpr bool row_is_dispositioned(const BoardRow& r) {
    if (r.disposition == Disposition::Unspecified) {
        return false;
    }
    if (r.disposition == Disposition::NotApplicable) {
        return !r.milestone.empty();
    }
    return true;
}

// The whole board: exactly the old tree's count, every row dispositioned. Takes the count so a
// fabricated short board can be handed to it below and shown rejected.
constexpr bool board_is_complete(const BoardRow* rows, std::size_t n) {
    if (n != kOldTreeRowCount) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!row_is_dispositioned(rows[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

// THE WHOLE-BOARD ASSERTION. Twenty rows, each dispositioned. A row that silently disappears drops the
// count below twenty and fails here; a row added without a disposition fails here.
static_assert(std::size(kBoard) == kOldTreeRowCount,
              "the exclusion board must carry exactly the old tree's twenty data rows "
              "(git show main:docs/limitations.md :18-37 at c4012b8); a row has been dropped or added");
static_assert(board_is_complete(kBoard, std::size(kBoard)),
              "every one of the twenty rows must carry a disposition — Excluded, Unrepresentable, or a "
              "NotApplicable that names the milestone that makes it live");

// ---- SHOWN OBJECTING: the whole-board assertion, fabricated broken ----------------------------------
//
// A predicate seen only to pass is not evidence it would ever fail. Two ways the board goes wrong, each
// handed to `board_is_complete` and required to be rejected.

// A row silently vanished: nineteen where there must be twenty.
static_assert([] {
    return !board_is_complete(kBoard, kOldTreeRowCount - 1);
}(), "the board must OBJECT when the count is not twenty (a row silently disappeared)");

// A row present but undispositioned — the sentinel left in place, the gap M5.9 names.
static_assert([] {
    BoardRow broken[kOldTreeRowCount];
    for (std::size_t i = 0; i < kOldTreeRowCount; ++i) {
        broken[i] = BoardRow{"x", Disposition::Excluded, {}};
    }
    broken[7] = BoardRow{"x", Disposition::Unspecified, {}};
    return !board_is_complete(broken, kOldTreeRowCount);
}(), "the board must OBJECT when a row carries no disposition");

// A NotApplicable row that does not say where it becomes live — "not applicable" with no reason is the
// quiet gap, not a disposition.
static_assert([] {
    BoardRow broken[kOldTreeRowCount];
    for (std::size_t i = 0; i < kOldTreeRowCount; ++i) {
        broken[i] = BoardRow{"x", Disposition::Excluded, {}};
    }
    broken[9] = BoardRow{"x", Disposition::NotApplicable, {}};
    return !board_is_complete(broken, kOldTreeRowCount);
}(), "the board must OBJECT when a NotApplicable row names no milestone");

// ---- the UNREPRESENTABLE rows: the spelling still does not exist -------------------------------------
//
// For each row excluded because no node spells the excluded thing, the standing guard is that the
// vocabulary has not grown to spell it. A changed field type, a grown attribute, or a newly named
// operation fires the assertion and forces the row to be re-dispositioned.

// Row 1 — a coupled multi-axis recurrence. `RecurrenceAttr` carries only `defines`; a recurrence feeds
// back along TIME (its `TimeRule`), never along an element axis, and there is no cross-axis-lag field
// and no multi-index left-hand side to build one. A field added here would be a new spelling.
static_assert(sizeof(RecurrenceAttr) == sizeof(std::int32_t),
              "RecurrenceAttr has grown a field — if it can now spell a cross-axis (2-D IIR) lag, board "
              "row 1 (coupled multi-axis recurrence) must be re-dispositioned from Unrepresentable");

// Row 12 — a fractional delay off the innermost axis. `DelayAttr` carries only `samples`, with no axis
// field by design, so `_fdelay` reads earlier samples on the streamed axis only.
static_assert(sizeof(DelayAttr) == sizeof(std::int32_t),
              "DelayAttr has grown a field — if _fdelay can now name an axis, board row 12 (frac_delay "
              "off the innermost axis) must be re-dispositioned from Unrepresentable");

// Row 4 — argsort / index outputs — and row 8 — `mode`. `OrderOp` is `{Sort, TopK, Quantile}`, every
// one a value-producing rank statistic: no index-output variant (row 4) and no most-frequent-value
// variant (row 8). Row 7 — runtime q / k. `OrderAttr`'s selection is compile-time: `k` an `int32`, `q`
// a `double`, so a non-constant argument does not type-check at the builder.
static_assert(static_cast<std::uint8_t>(OrderOp::Sort) == 0 &&
                  static_cast<std::uint8_t>(OrderOp::TopK) == 1 &&
                  static_cast<std::uint8_t>(OrderOp::Quantile) == 2,
              "OrderOp's recorded set {Sort, TopK, Quantile} changed — a variant that outputs indices "
              "(board row 4, argsort) or counts a mode (board row 8) is now representable and the row "
              "must be re-dispositioned from Unrepresentable");
static_assert(std::is_same_v<decltype(OrderAttr::k), std::int32_t> &&
                  std::is_same_v<decltype(OrderAttr::q), double>,
              "OrderAttr's k/q are no longer compile-time scalars — a runtime rank makes board row 7 "
              "(runtime q / k) representable and it must be re-dispositioned");

// Row 9 — multi-axis order statistics. `OrderAttr` carries ONE `axis`, so several axes at once cannot
// be asked. (Its detailed shown-failing lives in `order_test`; here is the structural half.)
static_assert(std::is_same_v<decltype(OrderAttr::axis), std::int32_t>,
              "OrderAttr::axis is no longer a single scalar — if a multi-axis order can now be spelled, "
              "board row 9 (multi-axis order statistics) must be re-dispositioned");

// Row 17 — integer / bool / fixed-point element types. `ElemType` is `{Unknown, F32, F64}`; no integer
// member exists to be built. Row 11 — pad reflect/edge: `ViewAttr::pad_bits` is a single fill with no
// mode field (its detailed absence is asserted in `view_inference_pad_test`; recorded here as part of
// the whole board).
static_assert(static_cast<std::uint8_t>(ElemType::Unknown) == 0 &&
                  static_cast<std::uint8_t>(ElemType::F32) == 1 &&
                  static_cast<std::uint8_t>(ElemType::F64) == 2,
              "ElemType's recorded set {Unknown, F32, F64} changed — an integer/bool member makes board "
              "row 17 (integer element types) representable and it must be re-dispositioned");

// Row 3 — gather / scatter / index_select. No operation code spells a data-dependent tensor index. The
// guard scans the whole table for the names the old tree stubbed, so a code added under any of them
// fires. `_tblread` (an interpolated table read indexed by a phase, not a tensor by a tensor) is the
// in-tree workaround and is deliberately NOT one of these names.
namespace {
constexpr bool no_op_is_named(OpName forbidden) {
    for (std::uint8_t i = 0; i < kOpCount; ++i) {
        if (kOpTable[i].name == forbidden) {
            return false;
        }
    }
    return true;
}
} // namespace
static_assert(no_op_is_named(OpName{"_gather"}) && no_op_is_named(OpName{"_scatter"}) &&
                  no_op_is_named(OpName{"_index"}),
              "an operation spelling a data-dependent tensor index now exists — board row 3 (gather / "
              "scatter / index_select) must be re-dispositioned from Unrepresentable to a real refusal");

// ---- the EXCLUDED-by-diagnostic rows: re-pointed here, so the guard flips if a refusal disappears ----
//
// Each builds the deliberately-broken input its old-tree row names and requires the pass to OBJECT
// (`!ok`). The exact message is the per-operation test's business; here the standing guard is only that
// the refusal still FIRES — if the pass ever starts accepting one of these, this file fails, which is
// the M5.9 property nothing else in a build has.

// Row 2 — a negative (here zero) stride is a reversal, which is `_flip`. (`view_inference_test` pins the
// message; this is the whole-board standing guard.)
constexpr bool negative_stride_slice_refused(std::int32_t step) {
    Graph g{};
    const auto x = add_value(g, ElemType::F32, Shape{4, 3, 8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Slice;
    k.view.axis = 0;
    k.view.offset = 0;
    k.view.length = 2;
    k.view.step = step;
    add_node(g, op::Slice, x, kNoValue, kNoValue, k);
    return !infer(g).ok;
}
static_assert(negative_stride_slice_refused(0),
              "board row 2: a stride below one (a reversal) must still be refused by the pass");

// Row 5 — a symbolic ordered extent has no static network to build. Row 6 — an ordered extent over the
// ceiling. Both are refused by `_order`'s attribute check.
constexpr bool order_on_shape_refused(Shape s) {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, s, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Order;
    k.order = OrderAttr{OrderOp::Sort, 0, 1, 0.0};
    add_node(g, op::Order, in, kNoValue, kNoValue, k);
    return !infer(g).ok;
}
static_assert(order_on_shape_refused(Shape{Dim{0}, 8}),
              "board row 5: a symbolic ordered extent must still be refused");
static_assert(order_on_shape_refused(Shape{257, 8}),
              "board row 6: an ordered extent over the ceiling (256) must still be refused");

// Row 15 — a symbolic extent on a derived / kernel edge. `_fft`'s extents-known-now check refuses
// a symbolic transformed extent; that check (with `_order`'s, and the derived-extent obligation
// checked at bind time in `symbolic_test`) is this tree's enumeration of the residual edges.
static_assert([] {
    Graph g{};
    const auto in = add_value(g, ElemType::F32, Shape{Dim{0}}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::Fft;
    k.fft.n = 8; // a valid transform length: the ONLY thing wrong is the symbolic extent
    add_node(g, op::Fft, in, kNoValue, kNoValue, k);
    return !infer(g).ok;
}(), "board row 15: a symbolic extent on a derived/kernel edge (here an _fft) must still be refused");

// ---- SHOWN OBJECTING: a refusal "made to pass" would break the guard --------------------------------
//
// The plan asks the gate be shown firing when a row's check is deliberately made to pass. The same
// builder handed a GOOD input is ACCEPTED — so the `!ok` guards above track the refusal exactly, and if
// an operation started admitting the broken input (the refusal "made to pass"), `!ok` would become
// false and the corresponding static_assert would fire. A valid slice (`step == 1`) is not refused, so
// `negative_stride_slice_refused` returns false for it:
static_assert(!negative_stride_slice_refused(1),
              "a VALID slice must be accepted — this is the guard shown firing: were a negative stride "
              "ever accepted too, board row 2's assertion above would flip and fail the build");
// And a static ordered extent within the ceiling is accepted, so rows 5/6 track the refusal likewise.
static_assert(!order_on_shape_refused(Shape{8, 8}),
              "a VALID static order is accepted — board rows 5/6 fail the build if the refusal vanishes");

// Row 16 — rank over kMaxRank — is a `Shape`-constructor `fail()`, not a pass diagnostic, so it cannot
// be shown from a static_assert (a throw in a constant expression is ill-formed, not `false`). Its
// shown-failing check is the translation unit `tests/gates/refusals/rank_over_maximum.cpp`, built as
// ctest `board_refuses_rank_over_maximum` and required to fail carrying "more axes than kMaxRank". The
// standing guard here is only that the ceiling still exists and is six:
static_assert(kMaxRank == 6,
              "kMaxRank is no longer six — board row 16's fixture (a seven-axis Shape) is sized against "
              "it and its refusal message must be re-checked");

// Rows 19 and 20 — the two AUTODIFF rows — go live at M7 (M7.6), re-dispositioned NotApplicable →
// Excluded above. Their refusals come from `gradients()`, which `fail()`s (throws) rather than
// returning a `Diagnostic`, so — like row 16 — they cannot be shown rejecting from a `static_assert`
// that evaluates the pass. Their shown-failing check is the ctest `autodiff_exclusions`
// (`autodiff_exclusions_test.cpp`): row 19 is the order-statistic refusal, row 20 the recurrence /
// delay refusal, each built on a differentiated path and required to object. The standing guard here
// is only that the two operations are still classified `Excluded`, so a row silently readmitted flips
// this assertion; the ctest is what proves the refusal still stops a differentiation.
static_assert(rules_of(op::Order).adjoint == AdjointClass::Excluded && rules_of(op::Recurrence).adjoint == AdjointClass::Excluded &&
                  rules_of(op::FixedDelay).adjoint == AdjointClass::Excluded &&
                  rules_of(op::TableRead).adjoint == AdjointClass::Excluded,
              "board rows 19/20: an autodiff-excluded operation was reclassified — its refusal in "
              "`autodiff_exclusions` no longer proves what the board says it does");

int main() { return 0; }
