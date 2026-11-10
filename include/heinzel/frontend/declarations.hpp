#pragma once

// What a graph function declares: its boundary and its time.
//
//     Input x{"x", Signal{64}};         a signal: element axes, then its length in samples
//     Input w{"w", {64, 64}};           a tensor: the same numbers at every sample
//     Param gain{"gain"};               one number per run
//     Param q{"q", gt(0), le(10)};      a range, last: the numbers it may hold
//     Output y{"y"};  y[n] = x[n] + 0.5 * y[n - 3.0 * ms];  return y;
//
// A declaration records what was written and decides nothing: a tensor is its shape with a length-1 stream
// appended and marked `timeless`, a `Length` becomes the stream's symbol with its constraints, and the
// analysis works out each value's time.

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/frontend/ranged.hpp"
#include "heinzel/ir/ir.hpp"
#include "heinzel/support/fail.hpp"
#include "heinzel/support/fixed_vector.hpp"
#include "heinzel/units/named_units.hpp"

namespace heinzel {

// The id the frontend gives the one runtime dimension a graph may have: the length of its stream, a
// `Length` in a `Signal`. `Executor` accepts any one symbol and never reads the id.
inline constexpr std::int32_t kStreamLength = 0;

inline constexpr ElemType f32 = ElemType::F32;
inline constexpr ElemType f64 = ElemType::F64;

// The precision levels, a value's (`(a + b).precision(High)`) or a graph's (`finish(High, y)`).
using numerics::Precision;
inline constexpr Precision High = Precision::High;
inline constexpr Precision Normal = Precision::Normal;
inline constexpr Precision Fast = Precision::Fast;

// The units a lag is written in, so `using namespace heinzel;` brings `3.0 * ms` with it.
using units::ms;
using units::us;
using units::second;

// `n - 3.0 * ms` is a lag of a duration, folded to whole samples while compiling at the rate `n`
// carries (`constexpr Index n{48'000};`). `units::to_samples` refuses an unset rate, a negative duration
// and a fraction of a sample, each by name. `samples(n, d)` is the same count, for a coefficient.
constexpr std::int32_t samples(Index n, units::Quantity d) { return units::to_samples(d, n.rate); }
constexpr Past operator-(Index n, units::Quantity d) { return {samples(n, d)}; }

// `x[n - d.at_most(120.0) * ms]`: x as it was `d` ago, a distance in milliseconds that may change every sample (any unit
// of time: `us`, `second`), blended linearly between the two samples it falls between. The line keeps the bound's
// samples at the rate `n` carries, rounded up to a whole one, and holds `d` to them: below 0, and at a NaN, it reads x
// now; above the bound, as far back as it keeps. Over an input it reads a copy of it (`rewrite/copied_inputs.hpp`);
// inside feedback (a flanger's) it is not lowered yet.
namespace detail {

// The longest distance in samples, rounded up to a whole one; a bound that misses a whole number of samples only by the
// rounding of its product with the rate (`units::to_samples`'s 1e-6 of a sample) is that number. Its far tap reads one
// sample further.
constexpr std::int32_t longest_samples(double samples) {
    if (!(samples <= 2147483646.0)) {
        fail("heinzel: a delay's longest distance exceeds the int32 lag range");
    }
    const auto nearest = static_cast<std::int64_t>(samples + 0.5);
    const double off = samples - static_cast<double>(nearest);
    if (off <= 1e-6 && off >= -1e-6) {
        return static_cast<std::int32_t>(nearest);
    }
    const auto below = static_cast<std::int64_t>(samples);
    return static_cast<std::int32_t>(static_cast<double>(below) < samples ? below + 1 : below);
}

// Beside its type, where a call finds it with no `using namespace heinzel`.
constexpr DistanceInTime operator*(const AtMost& d, units::Scale unit) { return {d.distance, d.most, unit.si_factor}; }

} // namespace detail

constexpr detail::Unbounded operator*(Expr, units::Scale) { return {}; }

constexpr detail::VaryingPast operator-(Index n, const detail::DistanceInTime& d) {
    const double per_unit = d.seconds * n.rate;
    if (!(per_unit > 0.0)) {
        fail("heinzel: a delay that changes needs the sample rate: constexpr Index n{48'000}");
    }
    Expr samples = d.distance * per_unit;
    return {samples, detail::longest_samples(d.most * per_unit)};
}
template <std::same_as<Index> I>
constexpr Past operator-(I, detail::Unbounded) {
    static_assert(detail::kNeverCompiles<I>, "heinzel: a delay that changes needs its longest distance: x[n - d.at_most(120.0) * ms]");
    return {};
}
template <std::same_as<Index> I>
constexpr Past operator-(I, const detail::AtMost&) {
    static_assert(detail::kNeverCompiles<I>, "heinzel: a delay that changes is given in a unit of time: x[n - d.at_most(120.0) * ms]");
    return {};
}

// The stream's length, bound when the program runs: `Signal{2, frames}`. The defaults are
// `DimConstraints`'s, so a `Length{}` records symbol 0 with no constraint. `expect`, a typical length, is a
// hint that nothing reads yet; the graph does not keep it.
struct Length {
    std::int32_t multiple_of{1};
    std::int32_t min{0};
    std::int32_t expect{0};
};

namespace detail {

template <class E>
concept SignalExtent = (std::integral<E> && !std::same_as<E, bool>) || std::same_as<E, Extent> || std::same_as<E, Dim> || std::same_as<E, Length>;

// A number of samples, or of elements: at least one, and an extent's integer. (A negative one would read as a
// symbol in the extent encoding, `ir/vocab.hpp`.)
constexpr Extent as_extent(std::integral auto e) {
    if (std::cmp_less(e, 1) || std::cmp_greater(e, 2147483647)) {
        fail("heinzel: a signal's extent is a whole number of at least one");
    }
    return Extent{static_cast<std::int32_t>(e)};
}
constexpr Extent as_extent(Dim d) {
    if (d.id < 0) {
        fail("heinzel: a Dim's id is from 0 to kMaxDimDecls - 1");
    }
    if (d.id >= static_cast<std::int32_t>(kMaxDimDecls)) {
        fail("heinzel: a Dim's id is from 0 to kMaxDimDecls - 1; rebuild with -DHEINZEL_MAX_DIM_DECLS=N");
    }
    return Extent{d};
}
constexpr Extent as_extent(Extent e) {
    if (is_symbol(e)) {
        return as_extent(Dim{symbol_of(e)});
    }
    if (e.encoded < 1) {
        fail("heinzel: a signal's extent is a whole number of at least one");
    }
    return e;
}
constexpr Extent as_extent(Length) { return Extent{Dim{kStreamLength}}; }

// Mark a leaf as declared a tensor: no time of its own.
constexpr Expr timeless(Expr e) {
    e.catch_up();
    e.arena()->nodes[static_cast<std::size_t>(e.position())].timeless = true;
    return e;
}

} // namespace detail

// Element axes, then the length in samples: the last extent is time. An extent is an integer, a `Dim`
// or `Extent` (taken as it is, declaring nothing), or a `Length` (the stream's symbol, last only).
// `Signal{s}` takes a whole `Shape` value and declares nothing.
struct Signal {
    Shape shape{};
    DimDecl declared{};

    template <detail::SignalExtent... E>
    explicit constexpr Signal(E... extents) : shape{detail::as_extent(extents)...} {
        static_assert(sizeof...(E) > 0, "a signal has a length");
        std::size_t at = 0;
        (declare(extents, ++at == sizeof...(E)), ...);
    }
    explicit constexpr Signal(Shape whole) : shape(whole) {
        if (whole.rank == 0) {
            fail("heinzel: a signal has a length");
        }
        for (std::int32_t k = 0; k < whole.rank; ++k) {
            detail::as_extent(extent_at(whole, k)); // the checks the extents one by one get
        }
    }

private:
    constexpr void declare(auto, bool) {}
    constexpr void declare(Length l, bool last) {
        if (!last) {
            fail("heinzel: a Length is a signal's last extent");
        }
        declared = DimDecl{kStreamLength, DimConstraints{l.multiple_of, l.min}};
    }
};

namespace detail {

// A tensor as it is recorded: its axes and a literal length-1 stream after them. The appended axis
// needs a slot of its own, so a tensor has at most `kMaxRank - 1` axes. Its extents are whole numbers, none of
// them bound late: a negative one reads as a symbol or as no extent in the encoding (`ir/vocab.hpp`).
constexpr Shape recorded_tensor(Shape tensor) {
    if (tensor.rank > static_cast<std::int32_t>(kMaxRank) - 1) {
        fail("heinzel: a tensor has at most kMaxRank - 1 axes");
    }
    for (std::int32_t k = 0; k < tensor.rank; ++k) {
        if (const Extent e = extent_at(tensor, k); is_symbol(e) || is_no_extent(e)) {
            fail("heinzel: a tensor's extent is a whole number");
        }
    }
    Shape recorded = tensor;
    recorded.rank = tensor.rank + 1;
    set_extent(recorded, tensor.rank, literal_extent(1));
    return recorded;
}

constexpr Expr tensor_input(std::string_view name, Shape tensor, ElemType type) {
    return timeless(input(name, recorded_tensor(tensor), type));
}

} // namespace detail

// A declared input: a tensor by default (`{64, 64}`, `{}` for one number), a signal when marked; its range, if any,
// last.
class Input : public Expr {
public:
    template <std::same_as<Range>... R>
    constexpr Input(std::string_view name, Shape tensor, R... range) : Input(name, tensor, f32, range...) {}
    template <std::same_as<Range>... R>
    constexpr Input(std::string_view name, Shape tensor, ElemType type, R... range)
        : Expr(detail::with_range(detail::tensor_input(name, tensor, type), range...)) {}
    template <std::same_as<Range>... R>
    constexpr Input(std::string_view name, Signal signal, R... range) : Input(name, signal, f32, range...) {}
    template <std::same_as<Range>... R>
    constexpr Input(std::string_view name, Signal signal, ElemType type, R... range)
        : Expr(detail::with_range(detail::input(name, signal.shape, type, signal.declared), range...)) {}
};

// An index ramp, counting along the element axes, the same at every sample: `iota({4})` is the tensor
// 0 1 2 3, `iota(Signal{4, frames})` that ramp at each sample of a signal (recording the `Length` as an
// input does). Not a ramp along time — `iota(Signal{T})` would be all zeros — so both need element axes.
constexpr Expr iota(Shape tensor, ElemType type = f32) {
    if (tensor.rank == 0) {
        fail("heinzel: an iota has element axes");
    }
    return detail::iota_node(detail::recorded_tensor(tensor), type, true, DimDecl{});
}
constexpr Expr iota(Signal signal, ElemType type = f32) {
    if (signal.shape.rank < 2) {
        fail("heinzel: an iota has element axes");
    }
    return detail::iota_node(signal.shape, type, false, signal.declared);
}

// A constant tensor: numbers known when the program compiles, written into the graph and read where they lie.
// `constant(kTaps)` is the tensor {N} of a `float` (f32) or `double` (f64) array; `constant(kWeights, {32, 32})` reads
// it as that shape, as an `Input` of the shape bound to it would; `constant({0.25, 0.5, 0.25})` is a braced list, f32
// unless `f64` is given. One number, `constant(0.5)`, is a literal; `constant({0.5}, {})` a tensor of one number, as
// `Input{"x", {}}` is. A graph holds up to `kMaxConstants` numbers in all (`docs/limitations.md`).
namespace detail {
template <class T>
concept ConstantNumber = std::same_as<T, float> || std::same_as<T, double>;
constexpr Shape whole(std::size_t count) { return Shape{literal_extent(static_cast<std::int32_t>(count))}; }
template <ConstantNumber T>
constexpr Expr constant_of(const T* numbers, std::size_t count, Shape tensor) {
    return constant_node(std::vector<double>(numbers, numbers + count), recorded_tensor(tensor), std::same_as<T, float> ? f32 : f64);
}
} // namespace detail

constexpr Expr constant(std::initializer_list<double> numbers) {
    return detail::constant_node(std::vector<double>(numbers), detail::recorded_tensor(detail::whole(numbers.size())), f32);
}
// (A template, so that a braced `{}` is never a type: `constant({0.5}, {})` is the shaped spelling below.)
template <std::same_as<ElemType> T>
constexpr Expr constant(std::initializer_list<double> numbers, T type) {
    return detail::constant_node(std::vector<double>(numbers), detail::recorded_tensor(detail::whole(numbers.size())), type);
}
constexpr Expr constant(std::initializer_list<double> numbers, Shape tensor, ElemType type = f32) {
    return detail::constant_node(std::vector<double>(numbers), detail::recorded_tensor(tensor), type);
}
template <detail::ConstantNumber T, std::size_t N>
constexpr Expr constant(const T (&numbers)[N]) {
    return detail::constant_of(numbers, N, detail::whole(N));
}
template <detail::ConstantNumber T, std::size_t N>
constexpr Expr constant(const T (&numbers)[N], Shape tensor) {
    return detail::constant_of(numbers, N, tensor);
}
template <detail::ConstantNumber T, std::size_t N>
constexpr Expr constant(const std::array<T, N>& numbers) {
    return detail::constant_of(numbers.data(), N, detail::whole(N));
}
template <detail::ConstantNumber T, std::size_t N>
constexpr Expr constant(const std::array<T, N>& numbers, Shape tensor) {
    return detail::constant_of(numbers.data(), N, tensor);
}

namespace detail {

constexpr ValueCategory category_of(Expr e) {
    e.catch_up();
    return e.arena()->nodes[static_cast<std::size_t>(e.position())].category;
}

// A spec's side names every axis of the value it reads, time included once the stream is appended. Where the
// frontend knows the value's rank (an input, an `iota`) it is checked at once; for any value, the rank is written on
// the first node of what the spec lowered to that reads the value — the fold, the product, the contraction, the
// transpose or `rearrange`'s split — and the analysis checks it once shapes settle (`OpAttr::named_ranks`).
constexpr void spec_names_every_axis(Expr e, std::int32_t named) {
    e.catch_up();
    const std::int32_t rank = e.arena()->nodes[static_cast<std::size_t>(e.position())].shape.rank;
    if (rank != 0 && rank != named) {
        fail("heinzel: the spec names a different number of axes than the value has");
    }
}
constexpr void record_spec_rank(Expr result, Expr operand, std::size_t k, std::int32_t named) {
    result.catch_up();
    operand.catch_up();
    std::int32_t at = result.arena() == operand.arena() ? result.position() : kNoValue;
    for (int step = 0; step < 3 && at != kNoValue; ++step) { // back: transpose, sum, product; merge, transpose, split
        BuildNode& node = result.arena()->nodes[static_cast<std::size_t>(at)];
        if (node.inputs[k] == operand.position()) {
            node.attr.named_ranks[k] = static_cast<std::int8_t>(named);
            return;
        }
        at = node.inputs[0];
    }
}

} // namespace detail

// A declared variable is a leaf with the declared shape, type and `Length`; the analysis holds its definition to it.
constexpr Variable::Variable(Signal declared, ElemType type) {
    if (type == ElemType::Unknown) {
        fail("heinzel: a declared length needs a type");
    }
    BuildNode n{};
    n.category = ValueCategory::Variable;
    n.shape = declared.shape;
    n.type = type;
    n.declared = declared.declared;
    node_ = detail::one_node(n);
}

// One number per run, broadcast over everything: an input of no axes; its range, if any, last.
class Param : public Expr {
public:
    template <std::same_as<Range>... R>
    constexpr explicit Param(std::string_view name, R... range) : Param(name, f32, range...) {}
    template <std::same_as<Range>... R>
    constexpr explicit Param(std::string_view name, ElemType type, R... range)
        : Expr(detail::with_range(detail::timeless(detail::input(name, Shape{}, type)), range...)) {}
};

// An output: `y = e`, or `y[n] = … y[n - k] …` for a recurrence that is itself the output. A single
// one converts to its graph (`return y;`); several are `finish(a, b, c)`. `z = y` gives z y's value;
// `Output z = y` does not compile, since it would copy y's name. Every refusal has a fixed text: a
// failure while compiling takes a literal, so the output's name shows in the compiler's note instead.
// `Expr`'s tensor members read its value as `Expr(y)` does: `y.max(1, keepdims)` (`ops.hpp`).
class Output : public forwarding::members<Output> {
public:
    constexpr explicit Output(std::string_view name) : name_(detail::output_name(name)) {}
    // A declared length (`Output y{"y", Signal{N}}`): the output is a recurrence of that shape, defined by `y[n] =`.
    constexpr Output(std::string_view name, Signal declared, ElemType type = f32)
        : name_(detail::output_name(name)), state_(declared, type), declared_(true) {}
    Output(const Output&) = delete;
    constexpr Output& operator=(const Output& other) { return *this = Expr(other); }
    constexpr Output& operator=(Expr e) {
        if (declared_) {
            fail("heinzel: a declared Output is defined by y[n] = ...; assign an undeclared one");
        }
        if (assigned_ || by_index_) {
            fail(by_index_ ? "heinzel: an Output is assigned by = or by [n] =, not both" : "heinzel: an Output is assigned once");
        }
        if (state_read_) {
            fail("heinzel: an Output was read before it was assigned; use [n] = for a recurrence");
        }
        if (detail::category_of(e) == ValueCategory::Const) {
            fail("heinzel: an output is computed or an input");
        }
        value_ = e;
        assigned_ = true;
        return *this;
    }

    // `y[n] = …` defines the recurrence; read, `y[n]` is y's value now.
    struct Now {
        Output* o;
        constexpr void operator=(Expr definition) const { o->define(definition); }
        constexpr void operator=(const Now& other) const { o->define(Expr(other)); }
        constexpr operator Expr() const { return Expr(*o); }
        // A recurrence's promise goes on its definition, where the proof reads it (Plan/26-value-ranges/02).
        template <class... R>
        constexpr Expr promise(R...) const {
            static_assert(detail::kNeverCompiles<R...>, "heinzel: promise its definition: y[n] = (...).promise(...)");
            return Expr(*o);
        }
    };
    constexpr Now operator[](Index) { return {this}; }
    constexpr Expr operator[](Index) const { return Expr(*this); }
    constexpr Expr operator[](Past p) const {
        if (assigned_) {
            return value_[p];
        }
        state_read_ = true;
        return state_[p];
    }
    constexpr Expr operator[](const detail::VaryingPast& p) const {
        if (assigned_) {
            return value_[p];
        }
        state_read_ = true;
        return state_[p];
    }
    constexpr operator Expr() const {
        if (assigned_) {
            return value_;
        }
        if (!by_index_) {
            state_read_ = true;
        }
        return state_.value();
    }
    constexpr operator Graph() const;

private:
    template <class... More>
        requires(std::same_as<More, Output> && ...)
    friend constexpr Graph finish(const Output& first, const More&... more);

    // The value `finish` names: the assigned one, the recurrence's, or — an input or a variable read by name —
    // its identity, one `_realize`. That is made at the first finish, after the graph is written (a variable
    // read by name before its definition is defined by then), and kept, so finishing twice gives one graph.
    constexpr Expr root() const {
        if (!assigned_) {
            return state_.value();
        }
        const ValueCategory category = detail::category_of(value_);
        if (category != ValueCategory::Input && category != ValueCategory::Variable) {
            return value_;
        }
        if (identity_.arena() == nullptr) {
            identity_ = detail::identity(value_);
        }
        return identity_;
    }

    constexpr void define(Expr definition) {
        if (assigned_ || by_index_) {
            fail(assigned_ ? "heinzel: an Output is assigned by = or by [n] =, not both" : "heinzel: an Output is assigned once");
        }
        state_[Index{}] = definition;
        by_index_ = true;
    }

    Name name_{};
    Expr value_{};
    mutable Expr identity_{};
    Variable state_{};
    mutable bool state_read_{false};
    bool by_index_{false};
    bool assigned_{false};
    bool declared_{false};
};

namespace detail {

// Every variable the graph reads is defined by a recurrence: one read and never defined (an `Output` read and
// left unassigned outside the outputs finished, a `Variable` never given `[n] =`) is a value nothing makes.
constexpr bool every_variable_defined(const Graph& g) {
    std::vector<char> defined(g.values.size(), 0);
    for (const Node& n : g.nodes) {
        if (n.attr.kind == AttrKind::Recurrence && n.attr.recurrence.defines != kNoValue) {
            defined[static_cast<std::size_t>(n.attr.recurrence.defines)] = 1;
        }
    }
    for (std::size_t v = 0; v < g.values.size(); ++v) {
        if (g.values[v].category == ValueCategory::Variable && defined[v] == 0) {
            return false;
        }
    }
    return true;
}

// Two ports of one graph with one name — two inputs, two outputs, or an input and an output.
constexpr bool names_repeat(const Graph& g) {
    const auto clash = [](const auto& ports, std::size_t i, const NamedValue& p) {
        for (std::size_t k = 0; k < i; ++k) {
            if (ports[k].name == p.name) {
                return true;
            }
        }
        return false;
    };
    for (std::size_t i = 0; i < g.inputs.size(); ++i) {
        if (clash(g.inputs, i, g.inputs[i])) {
            return true;
        }
    }
    for (std::size_t i = 0; i < g.outputs.size(); ++i) {
        if (clash(g.outputs, i, g.outputs[i]) || clash(g.inputs, g.inputs.size(), g.outputs[i])) {
            return true;
        }
    }
    return false;
}

} // namespace detail

// Finish a graph with these outputs, in this order. Every output shares one graph, so a value feeding
// two of them is computed once.
template <class... More>
    requires(std::same_as<More, Output> && ...)
constexpr Graph finish(const Output& first, const More&... more) {
    static_assert(1 + sizeof...(More) <= kMaxIO, "heinzel: more outputs than kMaxIO; rebuild with -DHEINZEL_MAX_IO=N");
    const Output* outs[] = {&first, &more...};
    fixed_vector<Expr, kMaxIO> roots{};
    for (const Output* o : outs) {
        if (!o->assigned_ && !o->by_index_) {
            fail("heinzel: an Output was never assigned");
        }
        roots.push_back(o->root());
    }
    // One arena, so the positions index one node list; merging moves nodes, so catch up at the end.
    for (std::size_t i = 1; i < roots.size(); ++i) {
        bring_together(roots[0], roots[i]);
    }
    for (Expr& root : roots) {
        root.catch_up();
    }

    Graph g{};
    emit_arena_nodes(g, roots[0].arena());
    if (g.over != Capacity::None) {
        return g; // over a capacity, which `infer` refuses first
    }
    for (std::size_t i = 0; i < roots.size(); ++i) {
        g.outputs.push_back(NamedValue{outs[i]->name_, roots[i].position()});
    }
    if (detail::names_repeat(g)) {
        fail("heinzel: two ports share a name");
    }
    if (!detail::every_variable_defined(g)) {
        fail("heinzel: a Variable or an Output is read but never defined");
    }
    return g;
}

constexpr Output::operator Graph() const { return finish(*this); }

// The level an unmarked value resolves to, on a finished graph (before its first analysis).
constexpr void set_default_precision(Graph& g, numerics::Precision level) { g.default_precision = level; }

// Finish a graph at a level: every value not marked otherwise resolves to it, `return finish(High, y);`.
template <class... More>
    requires(std::same_as<More, Output> && ...)
constexpr Graph finish(Precision level, const Output& first, const More&... more) {
    Graph g = finish(first, more...);
    set_default_precision(g, level);
    return g;
}

} // namespace heinzel
