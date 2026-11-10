#pragma once

// The arena a graph is built into, and `Expr`, the handle a user holds. A leaf allocates its own
// one-node graph, so nothing has to hand it one; composing two expressions copies the smaller graph into
// the larger and leaves a forwarding note (union-find with path compression), so a stale handle is one
// hop from correct. An arena already is the flat node list, so finishing a graph is a copy, not a walk.
// See `docs/decisions.md`, Decision 1. The leaves and operations are `graph_builder.hpp`'s.

#include <concepts>
#include <cstdint>
#include <limits>
#include <vector>

#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fail.hpp"

namespace heinzel {

// ---- the arena ------------------------------------------------------------------------------------
//
// Nodes under construction live here, on the heap, only while a graph is being built. When the build
// runs during compilation, the language requires everything allocated to be freed before it ends, so a
// leak is a compile error.

struct BuildNode {
    OpCode op{};
    std::int32_t inputs[kMaxNodeInputs]{kNoValue, kNoValue, kNoValue};
    OpAttr attr{};
    ElemType type{ElemType::Unknown};
    Shape shape{};
    ValueCategory category{ValueCategory::Internal};
    double constant{0.0};
    Name name{};
    // A runtime-resolved dimension declared here, if any: on an `Input`, and on an `iota(Signal{…, frames})`
    // — the nodes that carry a shape the user wrote — and `kNoDimension` (the default) everywhere else.
    // Collected onto the graph when the arena is emitted.
    DimDecl declared{};

    // Compile-time numeric precision, carried onto the Value at emit.
    ::heinzel::numerics::Precision precision{::heinzel::numerics::Precision::Inherit};

    // Declared a tensor (`Value::timeless`), carried onto the Value at emit.
    bool timeless{false};

    // A constant tensor's numbers (`constant(…)`), moved into the graph's pool at emit. Held by the node, so arenas
    // merge by copying their nodes and no position into a pool moves.
    std::vector<double> numbers{};

    // The ranges this value was given, recorded onto the graph at emit (`Graph::ranges`): a port's in its declaration
    // (`declarations.hpp`), or the one a composition's result always has (`abs`, `frac`: a fact); and a computed value's
    // `promise`, which on a composition's result narrows its fact.
    Range range{};
    bool ranged{false};
    Range promise{};
    bool promised{false};

    constexpr bool operator==(const BuildNode&) const = default;
};

struct Arena {
    int handles{0};                 // how many expressions name this arena
    std::vector<BuildNode> nodes{};
    Arena* moved_to{nullptr};       // set once this arena's nodes were copied elsewhere
    std::int32_t moved_by{0};       // ...and by how much their positions shifted
};

constexpr void release(Arena* a);

constexpr void take_handle(Arena* a) {
    if (a != nullptr) {
        ++a->handles;
    }
}

constexpr void release(Arena* a) {
    if (a != nullptr && --a->handles == 0) {
        Arena* next = a->moved_to;
        delete a;
        release(next); // this arena was the last thing keeping the one it moved into alive
    }
}

// Follow the forwarding notes to where a node lives now, adjusting its position as we go.
constexpr Arena* live_arena(Arena* a, std::int32_t& position) {
    while (a->moved_to != nullptr) {
        position += a->moved_by;
        a = a->moved_to;
    }
    return a;
}

// ---- the sample index -------------------------------------------------------------------------------
// `x[n]` is x now and `x[n - 3]` is x three samples ago. `Index` carries the sample rate a duration
// needs (`n - 3.0 * ms`, in `declarations.hpp`); at 0 it takes whole-sample lags only. There is no
// `n + k`: the future is not something a sample can read.
struct Past {
    std::int32_t samples{0};
};

struct Index {
    double rate{0.0}; // Hz
};

constexpr Past operator-(Index, std::int32_t k) {
    if (k < 0) {
        fail("heinzel: n - k reaches back, so k is at least 0");
    }
    return {k};
}
template <std::floating_point F>
constexpr Past operator-(Index, F) = delete; // `n - 2.5` is no whole number of samples

// `x.sum(1, keepdims)`: the reduced axes stay, at extent 1, so the result broadcasts back against `x`.
struct keepdims_t {
    explicit constexpr keepdims_t() = default;
};
inline constexpr keepdims_t keepdims{};

namespace detail {

// False for every list of types: a `static_assert` on it fires only where its template is used, so a member that must
// never compile (`Now::promise`, `precision` on a named value) can say why in its own words, at the C++23 floor too.
template <class...>
inline constexpr bool kNeverCompiles = false;

struct AtMost;
struct VaryingPast;

} // namespace detail

// ---- the handle -------------------------------------------------------------------------------------
// What a user actually holds. A place in an arena, and a share in keeping it alive.
class Expr {
public:
    constexpr Expr() = default;
    constexpr Expr(Arena* a, std::int32_t position) : arena_(a), position_(position) { take_handle(arena_); }
    constexpr Expr(const Expr& o) : arena_(o.arena_), position_(o.position_) { take_handle(arena_); }
    // Only a named `Expr` is assigned to: `x[n] = e` or `y[n - 1] = e` would assign to a temporary
    // and define nothing.
    constexpr Expr& operator=(const Expr& o) & {
        if (this != &o) {
            take_handle(o.arena_);
            release(arena_);
            arena_ = o.arena_;
            position_ = o.position_;
        }
        return *this;
    }
    constexpr ~Expr() { release(arena_); }

    // Catch up with wherever this node has been moved to, so the position is valid right now.
    constexpr void catch_up() {
        if (arena_ == nullptr || arena_->moved_to == nullptr) {
            return;
        }
        std::int32_t p = position_;
        Arena* live = live_arena(arena_, p);
        take_handle(live);
        release(arena_);
        arena_ = live;
        position_ = p;
    }

    // `x[n]` is x itself; `x[n - k]` is x k samples ago (`detail::delayed`, in `graph_builder.hpp`); `x[n - d.at_most(120.0)
    // * ms]` is x a distance ago that changes, `d`, at most 120 ms (`declarations.hpp`).
    constexpr Expr operator[](Index) const { return *this; }
    constexpr Expr operator[](Past p) const;
    constexpr Expr operator[](const detail::VaryingPast& p) const;
    constexpr detail::AtMost at_most(double most) const;

    constexpr Arena* arena() const { return arena_; }
    constexpr std::int32_t position() const { return position_; }

    // Pin this value's numeric precision where it is made: `Expr y = (a + b).precision(High)`, a port where it is
    // declared, `Expr x = Input{"x", Signal{N}}.precision(High)`. The mark is written on the node and copied to the
    // Value at emit, so it survives finish() and any arena splice. A named value shares its node with every reader, so
    // `y = x.precision(Fast)` would make x Fast too: on a named value it does not compile, and a node some node
    // already reads is refused. The hole: a temporary naming a named value's node marks it while nothing reads it yet
    // (`Expr(x)`, `x[n]`, `std::move(x)`, `x.promise(…)`, an identity `einsum("ij->ij", x)`, a Variable's or an
    // Output's `Expr(v)`, a subgraph returning its argument); no value category tells that copy from a fresh result.
    constexpr Expr precision(::heinzel::numerics::Precision p) && {
        catch_up();
        for (const BuildNode& node : arena_->nodes) {
            for (const std::int32_t in : node.inputs) {
                if (in == position_) {
                    fail("heinzel: mark a value where it is made, before anything reads it");
                }
            }
        }
        arena_->nodes[static_cast<std::size_t>(position_)].precision = p;
        return *this;
    }
    // A const temporary (`f()` returning `const Expr`) is as fresh as any other.
    constexpr Expr precision(::heinzel::numerics::Precision p) const&& { return Expr(*this).precision(p); }
    template <class P>
    constexpr Expr precision(P) const& {
        static_assert(detail::kNeverCompiles<P>,
                      "heinzel: mark a value where it is made: Expr y = (a + b).precision(High)");
        return *this;
    }

    // Narrow this computed value to a range, `(a + b).promise(gt(0), le(2))`: a mark on its node, as `precision()`
    // is, and the same value back (`ranged.hpp`, beside the comparators).
    template <std::same_as<Range> First, std::same_as<Range>... More>
    constexpr Expr promise(First first, More... more) const;

    // The tensor members (`frontend/ops.hpp`). Axes are counted from the front, never
    // negative; a signal's time is its last (framed by `unfold`, refused by a fold). An axis is an integer, so
    // `x.max(0.0)` does not compile — the elementwise `max(x, 0.0)` is what it would have meant. An `Output` and a
    // `Variable` forward every overload, the deleted ones too (`forwarding::members`): one added here goes there.
    constexpr Expr sum(std::int32_t axis) const;
    constexpr Expr sum(std::int32_t axis, keepdims_t) const;
    constexpr Expr sum(Axes axes) const;
    constexpr Expr sum(Axes axes, keepdims_t) const;
    constexpr Expr mean(std::int32_t axis) const;
    constexpr Expr mean(std::int32_t axis, keepdims_t) const;
    constexpr Expr mean(Axes axes) const;
    constexpr Expr mean(Axes axes, keepdims_t) const;
    constexpr Expr max(std::int32_t axis) const;
    constexpr Expr max(std::int32_t axis, keepdims_t) const;
    constexpr Expr max(Axes axes) const;
    constexpr Expr max(Axes axes, keepdims_t) const;
    constexpr Expr min(std::int32_t axis) const;
    constexpr Expr min(std::int32_t axis, keepdims_t) const;
    constexpr Expr min(Axes axes) const;
    constexpr Expr min(Axes axes, keepdims_t) const;
    constexpr Expr prod(std::int32_t axis) const;
    constexpr Expr prod(std::int32_t axis, keepdims_t) const;
    constexpr Expr prod(Axes axes) const;
    constexpr Expr prod(Axes axes, keepdims_t) const;
    constexpr Expr transpose(Axes order) const;     // the element axes' order; time stays last
    constexpr Expr reshape(Shape target) const;     // the element extents only
    constexpr Expr slice(std::int32_t axis, std::int32_t offset, std::int32_t length, std::int32_t step = 1) const;
    constexpr Expr unfold(std::int32_t axis, std::int32_t window, std::int32_t hop = 1) const;
    constexpr Expr unfold2d(std::int32_t axis, std::int32_t window, std::int32_t axis2, std::int32_t window2) const;
    constexpr Expr squeeze(std::int32_t axis) const;
    constexpr Expr unsqueeze(std::int32_t axis) const;
    constexpr Expr realize() const;
    constexpr Expr sort(std::int32_t axis) const;
    constexpr Expr topk(std::int32_t axis, std::int32_t k) const;
    constexpr Expr quantile(std::int32_t axis, double q) const;
    constexpr Expr median(std::int32_t axis) const;
    // An axis given as a floating-point number is a mistake (`x.max(0.0)`, `x.max(0.5f, keepdims)`): deleted.
    template <std::floating_point F, class... Rest> Expr sum(F, Rest...) const = delete;
    template <std::floating_point F, class... Rest> Expr mean(F, Rest...) const = delete;
    template <std::floating_point F, class... Rest> Expr max(F, Rest...) const = delete;
    template <std::floating_point F, class... Rest> Expr min(F, Rest...) const = delete;
    template <std::floating_point F, class... Rest> Expr prod(F, Rest...) const = delete;
    template <std::floating_point F, class... Rest> Expr slice(F, Rest...) const = delete;
    template <std::floating_point F, class... Rest> Expr unfold(F, Rest...) const = delete;
    template <std::floating_point F, class... Rest> Expr unfold2d(F, Rest...) const = delete;
    template <std::floating_point F> Expr squeeze(F) const = delete;
    template <std::floating_point F> Expr unsqueeze(F) const = delete;
    template <std::floating_point F> Expr sort(F) const = delete;
    template <std::floating_point F, class K> Expr topk(F, K) const = delete;
    template <std::floating_point F, class Q> Expr quantile(F, Q) const = delete;
    template <std::floating_point F> Expr median(F) const = delete;

private:
    Arena* arena_{nullptr};
    std::int32_t position_{kNoValue};
};

namespace detail {

// A delay that changes, spelled `x[n - d.at_most(120.0) * ms]` (`declarations.hpp`), in its three steps: what
// `d.at_most(120.0)` gives, the distance and its bound in a unit still to be named; that times a unit, `* ms`, so many
// seconds; and back from `n`, the position in samples and the longest it may be, in whole samples.
struct AtMost {
    Expr distance{};
    double most{0.0};
};
struct DistanceInTime {
    Expr distance{};
    double most{0.0};
    double seconds{0.0};
};
struct VaryingPast {
    Expr samples{};
    std::int32_t most{0};
};
// `d * ms` with no bound: refused where it is read back from, `n - d * ms`.
struct Unbounded {};

} // namespace detail

constexpr detail::AtMost Expr::at_most(double most) const {
    if (!(most >= 0.0 && most <= std::numeric_limits<double>::max())) {
        fail("heinzel: d.at_most(b) bounds a distance, so b is a finite number of at least 0");
    }
    return detail::AtMost{*this, most};
}

// Copy every node of `from` onto the end of `into`, shifting the positions they refer to, and leave
// `from` as a note saying where its nodes went.
//
// The cost is the size of `from`, and callers always move the SMALLER side, which keeps the total work
// near-linear instead of quadratic. Copying and renumbering both sides every time took 50 s on a
// 482-node graph where this takes 0.3 s.
constexpr void move_into(Arena* into, Arena* from) {
    const auto shift = static_cast<std::int32_t>(into->nodes.size());
    for (const BuildNode& n : from->nodes) {
        BuildNode moved = n;
        for (std::int32_t& in : moved.inputs) {
            if (in != kNoValue) {
                in += shift;
            }
        }
        if (moved.attr.kind == AttrKind::Recurrence && moved.attr.recurrence.defines != kNoValue) {
            moved.attr.recurrence.defines += shift;
        }
        into->nodes.push_back(moved);
    }
    from->nodes.clear();
    from->moved_to = into;
    from->moved_by = shift;
    take_handle(into); // the note itself keeps the destination alive
}

// Bring two expressions into one arena and return it, leaving both positions valid there.
constexpr Arena* bring_together(Expr& a, Expr& b) {
    a.catch_up();
    b.catch_up();
    if (a.arena() == b.arena()) {
        return a.arena();
    }
    if (a.arena()->nodes.size() >= b.arena()->nodes.size()) {
        move_into(a.arena(), b.arena());
        b.catch_up();
        return a.arena();
    }
    move_into(b.arena(), a.arena());
    a.catch_up();
    return b.arena();
}

namespace detail {

constexpr Expr one_node(BuildNode n) {
    Arena* a = new Arena{};
    a->nodes.push_back(n);
    return Expr{a, 0};
}

} // namespace detail

} // namespace heinzel
