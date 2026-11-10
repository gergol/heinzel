// Milestone M5.6, oracle O9's sibling — every operation with a PASSTHROUGH-LOOKING shape rule carries
// the rejection its old-tree implementation made, as an exclusion. An operation with a passthrough-looking
// shape rule and none is NOT FINISHED.
//
// THE TRAP THIS CATCHES (`01-DERIVATION.md` §1.5). `_flip`'s shape rule really is passthrough, so
// classifying it `Passthrough` and moving on DELETES the rejection its old shape function made, and the
// failure resurfaces as an assertion deep in the executor. The same holds for every operation whose old
// implementation rejected something inside a shape function that looked like identity. A fresh tree
// inherits the trap the moment someone reads the old rule and copies only the shape half. This file
// makes the sweep an explicit CHECK rather than an inspection satisfied inside another step — which is
// exactly why M5's plan gives it a step of its own (`plan/M5-STEPS.md` S7).
//
// WHY THIS LIVES IN `tests/`. The check has to name the one bare-passthrough identity (`op::Realize`),
// and `gate_one_writing_per_operation` / `gate_no_per_op_branches` forbid naming an operation under
// `include/`. A parity-style reference that names operations is exactly what a test may hold, so the
// allowlist and its justification live here, in step with the table it audits.
//
// THE PROXY, AND ITS BLIND SPOT, said plainly (as the other gates state theirs). "Carries an
// exclusion" is read as "carries a dedicated attribute" (`AttrKind != None`): a dedicated attribute is
// dispatched to a dedicated attribute or graph check that reproduces the old-tree rejection — the
// delays' `state_depth_is_a_constant` and axis check, `_recur`'s undelayed-cycle refusal in the
// planner, `_dline`'s reach bound, `_tblread`'s `check_table_attribute`. What the proxy does NOT prove
// is that the specific rejection is FAITHFUL — an attribute check that existed but rejected nothing
// would pass here. That faithfulness is each building milestone's own both-ways and shown-rejecting
// work (M4 for the delays and the recurrence, M5-S6 for `_tblread`); this gate proves only that no
// passthrough op is a BARE identity carrying no attribute unless it is declared one. It is the same class of blind spot `one_writing_per_operation` records for
// function-pointer dispatch.

#include <cstdint>

#include "heinzel/ir/op_rules.hpp"
#include "heinzel/ir/rule_columns.hpp"
#include "heinzel/ir/vocab.hpp"

using namespace heinzel;

namespace {

// How a passthrough-looking operation carries what its old implementation rejected.
enum class PassthroughDisposition {
    CarriesAttributeExclusion, // a dedicated attribute, hence a dedicated check reproducing the rejection
    DeclaredIdentity,          // a bare passthrough that rejected NOTHING in the old tree (allowlisted below)
    Unaccounted,               // the trap: a passthrough-looking rule carrying none
};

// The allowlist of bare-passthrough identities. An operation belongs here only if its shape rule really
// is the identity AND its old-tree implementation rejected nothing.
//
//   `_realize` — the old tree lists it among "pure data movement of a complex buffer is allowed"
//   (`sema/analyze.hpp:704-706` at `c4012b8`, `git show main:...`) and carries NO `docs/limitations.md`
//   row. It forces a value contiguous and hands every element straight back; there is nothing to
//   reject. This is §1.5's legitimate contrapositive — an operation whose old implementation rejected
//   nothing carries nothing, and is finished.
constexpr bool is_declared_identity(std::uint8_t code) {
    return OpCode{code} == op::Realize;
}

constexpr PassthroughDisposition disposition_of(std::uint8_t code, const OpRules& r) {
    if (r.attr != AttrKind::None) {
        return PassthroughDisposition::CarriesAttributeExclusion;
    }
    if (is_declared_identity(code)) {
        return PassthroughDisposition::DeclaredIdentity;
    }
    return PassthroughDisposition::Unaccounted;
}

// A shape rule that hands the operand's extents straight to the result, so a rejection inside it is
// invisible to the shape relation.
constexpr bool is_passthrough_looking(ShapeRule s) { return s == ShapeRule::Passthrough; }

// THE CHECK. Every passthrough-looking row is accounted for.
constexpr bool every_passthrough_op_is_accounted_for(const OpTableRow* rows, std::uint8_t count) {
    for (std::uint8_t i = 0; i < count; ++i) {
        if (!is_passthrough_looking(rows[i].rules.shape)) {
            continue;
        }
        if (disposition_of(i, rows[i].rules) == PassthroughDisposition::Unaccounted) {
            return false;
        }
    }
    return true;
}

} // namespace

static_assert(every_passthrough_op_is_accounted_for(kOpTable, kOpCount),
              "an operation whose shape rule is Passthrough carries no dedicated attribute, and "
              "is not a declared identity — its old implementation's rejection has been deleted "
              "(01-DERIVATION 1.5). Give it its rejection back as an attribute check, or, if it truly "
              "rejected nothing, add it to is_declared_identity with the old-tree evidence.");

// ---- SHOWN REJECTING: the trap fabricated ------------------------------------------------------------
//
// A predicate seen only to pass is not evidence it would ever fail. Hand it a table with one
// passthrough row that carries no attribute and is not the declared identity — the exact shape §1.5
// names — and it must object. Code 0 is `op::Add`, which is not
// allowlisted, so the row is Unaccounted.
static_assert([] {
    OpRules trap{};
    trap.arity = 1;
    trap.attr = AttrKind::None;
    trap.shape = ShapeRule::Passthrough;
    trap.type = TypeRule::SameAsFirst;
    trap.time = TimeRule::SameSample;
    trap.meaning = MeaningRule::PerSample;
    trap.cost = CostClass::Cheap;
    const OpTableRow broken[] = {OpTableRow{OpName{}, trap}};
    return !every_passthrough_op_is_accounted_for(broken, 1);
}(), "the check must OBJECT to a bare passthrough that carries no attribute");

// The positive side — that a passthrough row IS accepted once accounted for — is carried by the real
// table passing above and by the direct per-operation assertions below, rather than by a second
// fabricated table (placing a row at `op::Realize`'s own code would need 53 padding rows whose
// unclassified columns are beside the point this file makes).

// ---- the dispositions, named directly ----------------------------------------------------------------
//
// Each passthrough-looking operation in the real table, and how it is accounted for. Grounded against
// the old tree (`git show main:include/heinzel/sema/analyze.hpp`) in `plan/M5-STEPS.md` S7 and
// `docs/decisions.md`.
static_assert(disposition_of(op::Realize.index, rules_of(op::Realize)) == PassthroughDisposition::DeclaredIdentity,
              "_realize is the bare passthrough that rejected nothing in the old tree");
static_assert(disposition_of(op::TableRead.index, rules_of(op::TableRead)) ==
                  PassthroughDisposition::CarriesAttributeExclusion,
              "_tblread's rejections ride AttrKind::Table (check_table_attribute), shown rejecting in tblread_test");
static_assert(disposition_of(op::FixedDelay.index, rules_of(op::FixedDelay)) ==
                  PassthroughDisposition::CarriesAttributeExclusion,
              "_fdelay's depth/axis rejections ride AttrKind::Delay");
static_assert(disposition_of(op::Recurrence.index, rules_of(op::Recurrence)) ==
                  PassthroughDisposition::CarriesAttributeExclusion,
              "_recur's undelayed-cycle rejection is keyed off AttrKind::Recurrence (refused in the planner)");
static_assert(disposition_of(op::DelayLine.index, rules_of(op::DelayLine)) ==
                  PassthroughDisposition::CarriesAttributeExclusion,
              "_dline's reach bound rides AttrKind::SignalledDelay");

// ---- the allowlist is MINIMAL and cannot go stale ----------------------------------------------------
//
// Every code the allowlist names must actually BE a bare passthrough: Passthrough shape, no attribute.
// If `_realize` ever gains an attribute, this fires and forces the
// allowlist entry to be re-decided rather than silently masking a real rejection — the O9 discipline
// (a check that starts passing fails the build), applied to the allowlist itself.
static_assert(is_declared_identity(op::Realize.index) &&
                  rules_of(op::Realize).shape == ShapeRule::Passthrough &&
                  rules_of(op::Realize).attr == AttrKind::None,
              "a declared identity must be a bare passthrough — Passthrough shape, no attribute; if "
              "_realize gained one, it would no longer need the allowlist and this entry is stale");

int main() { return 0; }
