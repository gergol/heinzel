#pragma once

// Subgraphs: a reusable piece of a graph, made under a name and applied once.
//
//     constexpr Expr gain_stage(Expr x) { Input gain{"gain", Signal{32}}; return x * gain; }
//     auto in_gain = subgraph("in_gain", gain_stage);
//     Expr s = in_gain(x);                                 // its input is "in_gain.gain"
//
// A body is an ordinary function (or a lambda that captures nothing) from expressions to an expression; its
// internal nodes splice into the caller's arena the way every composition does, so there is nothing to flatten and
// no subgraph operation. What a subgraph adds is the NAME: every input the body declared gets `name.` in front, and
// nesting composes (`outer.inner.k`). The body runs when the piece is applied, never when it is made, so a body that
// takes nothing (`noise()`) declares its inputs where it is applied.
//
// A piece is applied once: a second use would declare its names a second time, so each use makes its own piece. It
// remembers that it was applied, and a graph function's constant evaluation can change that only in an object it
// made itself: a piece lives inside its graph function, and a `constexpr` one at namespace scope cannot be applied.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/units/named_units.hpp"

namespace heinzel {

namespace detail {

template <class T>
struct array_of {
    static constexpr bool is = false;
    using element = void;
};
template <class T, std::size_t N>
struct array_of<std::array<T, N>> {
    static constexpr bool is = true;
    using element = T;
};

template <class T>
constexpr bool is_expr = std::is_same_v<T, Expr> || std::is_same_v<T, Input> || std::is_same_v<T, Param>; // (a class
// derived from `Expr` may hold another, unseen)

// A type that cannot hold an `Expr`: what a body may take besides expressions.
template <class T>
constexpr bool holds_no_expr() {
    if constexpr (array_of<T>::is) {
        return holds_no_expr<typename array_of<T>::element>();
    } else {
        return std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T, Shape> || std::is_same_v<T, Extent> ||
               std::is_same_v<T, Dim> || std::is_same_v<T, Axes> || std::is_same_v<T, Signal> || std::is_same_v<T, Length> ||
               std::is_same_v<T, Index> || std::is_same_v<T, Past> || std::is_same_v<T, keepdims_t> || std::is_same_v<T, units::Scale> ||
               std::is_same_v<T, units::Quantity> || std::is_same_v<T, std::string_view> || std::is_same_v<T, const char*> ||
               (std::is_array_v<T> && std::is_same_v<std::remove_cv_t<std::remove_extent_t<T>>, char>);
    }
}

template <class T>
constexpr bool is_argument() {
    if constexpr (array_of<T>::is) {
        return is_expr<typename array_of<T>::element> || holds_no_expr<T>();
    } else {
        return is_expr<T> || holds_no_expr<T>();
    }
}

template <class T>
struct result_of_exprs {
    static constexpr bool is = is_expr<T> || (array_of<T>::is && std::is_same_v<typename array_of<T>::element, Expr>);
};
template <class... T>
struct result_of_exprs<std::pair<T...>> {
    static constexpr bool is = (std::is_same_v<T, Expr> && ...);
};
template <class... T>
struct result_of_exprs<std::tuple<T...>> {
    static constexpr bool is = (std::is_same_v<T, Expr> && ...);
};

// How a subgraph knows which inputs the body declared, with no context passed in. Before the call each
// argument's arena is pinned at its first node, with its node count. An arena is only ever moved whole and
// appended to, so after the call each pinned block is still contiguous, wherever it lives now. Every input
// outside those blocks — in the result's arena or any argument's (a leaf may merge into an argument and not the
// result) — is the body's.
//
// An argument's arena, pinned at its first node: `count` nodes from there are the caller's.
struct Pin {
    Expr first;
    std::int32_t count;
};

constexpr void pin(std::vector<Pin>& pins, Expr e) {
    if (e.arena() == nullptr) {
        return; // a default `Expr{}` holds nothing
    }
    e.catch_up();
    pins.push_back(Pin{Expr{e.arena(), 0}, static_cast<std::int32_t>(e.arena()->nodes.size())});
}

template <class T>
constexpr void pin_argument(std::vector<Pin>& pins, const T& arg) {
    if constexpr (is_expr<T>) {
        pin(pins, arg);
    } else if constexpr (array_of<T>::is && is_expr<typename array_of<T>::element>) {
        for (const auto& e : arg) {
            pin(pins, e);
        }
    }
}

constexpr bool pinned(const std::vector<Pin>& pins, const Arena* arena, std::int32_t at) {
    for (const Pin& p : pins) {
        if (p.first.arena() == arena && at >= p.first.position() && at < p.first.position() + p.count) {
            return true;
        }
    }
    return false;
}

// `prefix.name`, refused past `kMaxNameLength` as any name is.
constexpr Name prefixed(std::string_view prefix, std::string_view name) {
    if (prefix.size() + 1 + name.size() + 1 > kMaxNameLength) {
        fail("heinzel: a name is longer than kMaxNameLength");
    }
    Name out{};
    std::size_t at = 0;
    for (const char c : prefix) {
        out.data_[at++] = c;
    }
    out.data_[at++] = '.';
    for (const char c : name) {
        out.data_[at++] = c;
    }
    return out;
}

// Every input of the arenas outside the pinned blocks gets `prefix.` in front — every new name measured first, so a
// refusal leaves no arena half renamed.
constexpr void prefix_inputs(const std::vector<Arena*>& arenas, std::string_view prefix, const std::vector<Pin>& pins) {
    for (const bool rename : {false, true}) {
        for (Arena* arena : arenas) {
            for (std::size_t i = 0; i < arena->nodes.size(); ++i) {
                BuildNode& node = arena->nodes[i];
                if (node.category == ValueCategory::Input && !pinned(pins, arena, static_cast<std::int32_t>(i))) {
                    const Name named = prefixed(prefix, node.name.view());
                    node.name = rename ? named : node.name;
                }
            }
        }
    }
}

constexpr void each_expr(const Expr& e, auto&& visit) {
    visit(e);
}
template <std::size_t N>
constexpr void each_expr(const std::array<Expr, N>& es, auto&& visit) {
    for (const Expr& e : es) {
        visit(e);
    }
}
template <class... T>
constexpr void each_expr(const std::pair<T...>& p, auto&& visit) {
    visit(p.first);
    visit(p.second);
}
template <class... T>
constexpr void each_expr(const std::tuple<T...>& t, auto&& visit) {
    std::apply([&](const auto&... e) { (visit(e), ...); }, t);
}

// After the body ran: every input it declared — outside the pinned blocks of the result's arenas and the
// arguments' — named `name.input`.
template <class Result>
constexpr Result name_inputs(std::string_view name, std::vector<Pin>& pins, Result result) {
    std::vector<Arena*> arenas;
    const auto add = [&](Arena* a) {
        for (const Arena* seen : arenas) {
            if (seen == a) {
                return;
            }
        }
        arenas.push_back(a);
    };
    for (Pin& p : pins) {
        p.first.catch_up();
        add(p.first.arena());
    }
    each_expr(result, [&](const Expr& e) {
        if (e.arena() != nullptr) {
            Expr live = e;
            live.catch_up();
            add(live.arena());
        }
    });
    prefix_inputs(arenas, name, pins);
    return result;
}

} // namespace detail

// A piece of a graph, made by `subgraph(name, body)`: applied, `piece(args...)` runs `body(args...)` and the inputs it
// declares come out `name.input`. Each refusal is the one error a misuse gives: the body's when the piece is made, the
// arguments' and the result's when it is applied; past a refused guard, the call is made where it still can be, so
// nothing else fails.
//
// Nothing may carry an `Expr` in or out unseen: the body captures nothing and takes its arguments by value or
// const reference (it is handed them `const`), an argument is an `Expr`, an `Input`, a `Param`, an `std::array` of
// them or a type that cannot hold one, and the result is an `Expr`, an array, a pair or a tuple of them. Each is
// checked while compiling. One way in stays open, and no check can see it: a body that reads an `Expr` kept at
// namespace scope captures it all the same — its inputs would be taken for the body's.
//
// Neither copied nor moved, so a piece is the one object it was made as and a second application is seen
// (`auto bass = subgraph(…)` is that object, by guaranteed copy elision).
template <class Body>
class Subgraph {
public:
    // Checked by `subgraph`: a failed `static_assert` here would take the class's members with it, a second error.
    static constexpr bool plain = std::is_function_v<std::remove_pointer_t<Body>> || std::is_empty_v<Body>;

    constexpr Subgraph(std::string_view name, Body body) : name_(detail::name_of(name)), body_(body) {}
    Subgraph(const Subgraph&) = delete;
    Subgraph& operator=(const Subgraph&) = delete;

    template <class... Args>
    constexpr auto operator()(const Args&... args) const {
        constexpr bool callable = std::is_invocable_v<const Body&, const Args&...>;
        static_assert((detail::is_argument<Args>() && ...),
                      "heinzel::subgraph: an argument is an Expr, an array of them, or a value that holds none");
        static_assert(!plain || callable, "heinzel::subgraph: the body takes its arguments by value or const reference");
        if (applied_) {
            fail("heinzel: a subgraph is applied once; make one for each use");
        }
        applied_ = true;
        if constexpr (!callable) {
            return Expr{};
        } else {
            using R = std::remove_cvref_t<std::invoke_result_t<const Body&, const Args&...>>;
            static_assert(detail::result_of_exprs<R>::is,
                          "heinzel::subgraph: a body returns an Expr, or an array, a pair or a tuple of them");
            if constexpr (std::is_void_v<R>) {
                return Expr{};
            } else if constexpr (!detail::result_of_exprs<R>::is) {
                return body_(args...);
            } else {
                std::vector<detail::Pin> pins;
                (detail::pin_argument(pins, args), ...);
                R made = body_(args...);
                if constexpr (detail::is_expr<R>) { // an `Input` or a `Param` returned: as the expression it is
                    return detail::name_inputs(name_.view(), pins, Expr(made));
                } else {
                    return detail::name_inputs(name_.view(), pins, made);
                }
            }
        }
    }

private:
    template <class Inner>
    friend constexpr Subgraph<Inner> subgraph(std::string_view name, const Subgraph<Inner>& inner);

    Name name_;
    Body body_;
    mutable bool applied_ = false;
};

namespace detail {
template <class T>
inline constexpr bool is_piece = false;
template <class Body>
inline constexpr bool is_piece<Subgraph<Body>> = true;
} // namespace detail

// A piece named `name` that runs `body` when it is applied: `auto bass = subgraph("bass", bass_shelf);`, then
// `Expr b = bass(x);`. The name is refused here when it would not fit.
template <class Body>
    requires(!detail::is_piece<std::decay_t<Body>>)
constexpr Subgraph<std::decay_t<Body>> subgraph(std::string_view name, Body&& body) {
    static_assert(Subgraph<std::decay_t<Body>>::plain,
                  "heinzel::subgraph: the body is a function, or a lambda that captures nothing");
    return Subgraph<std::decay_t<Body>>(name, body);
}

// A piece made the body of another is refused (a piece is applied inside the outer one's body); past the refusal the
// inner one's body is made the piece, under the outer name, so nothing else fails.
template <class Inner>
constexpr Subgraph<Inner> subgraph(std::string_view name, const Subgraph<Inner>& inner) {
    static_assert(detail::kNeverCompiles<Inner>, "heinzel::subgraph: apply the inner piece inside the outer one's body");
    return Subgraph<Inner>(name, inner.body_);
}

} // namespace heinzel
