#pragma once

// An `Executor`'s ports: what a name is looked up as, and the name itself, looked up while compiling (`PortName`);
// the small types its members are made of; the words of a debug build's checks; and which compiled programs it runs.

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "heinzel/compile.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/streams.hpp"
#include "heinzel/ranges/interval.hpp"
#include "heinzel/ranges/proven.hpp"
#include "heinzel/sema/declared_variables.hpp"
#include "heinzel/sema/time_checks.hpp"
#include "heinzel/support/fail.hpp"
#include "heinzel/support/fixed_string.hpp"
#include "heinzel/support/fixed_vector.hpp"

namespace heinzel {

// What a name is looked up as: an input, a Param or an output.
enum class Port : std::uint8_t { Input, Param, Output };

namespace detail {

// A Param is an input of no axes: one number per run.
constexpr bool is_param(const Graph& g, const NamedValue& port) { return g.values[static_cast<std::size_t>(port.value)].shape.rank == 0; }

template <class V>
constexpr ElemType element_type_of() {
    static_assert(std::is_same_v<V, float> || std::is_same_v<V, double>, "heinzel::Executor: a buffer holds float or double");
    return std::is_same_v<V, float> ? ElemType::F32 : ElemType::F64;
}

// Which named inputs the program reads: those, and only those, need a buffer (or a value).
constexpr std::array<bool, kMaxIO> inputs_read(const loops::Program& p) {
    std::array<bool, kMaxIO> read{};
    for (const loops::Stmt& s : p.stmts) {
        for (const loops::Access& x : s.in) {
            if (x.source == loops::Source::Buffer && p.buffers[static_cast<std::size_t>(x.buffer)].home == loops::Home::Feed) {
                read[static_cast<std::size_t>(p.buffers[static_cast<std::size_t>(x.buffer)].feed)] = true;
            }
        }
    }
    return read;
}

// An executor's inputs, or its outputs, as the types of their values.
template <std::size_t N>
constexpr std::array<ElemType, N> value_types(const Graph& g, const fixed_vector<NamedValue, kMaxIO>& ports) {
    std::array<ElemType, N> type{};
    for (std::size_t i = 0; i < N; ++i) {
        type[i] = g.values[static_cast<std::size_t>(ports[i].value)].type;
    }
    return type;
}

// How many of the first `inputs` inputs are Params.
constexpr std::size_t param_count(const Graph& g, std::size_t inputs) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < inputs; ++i) {
        n += is_param(g, g.inputs[i]) ? 1 : 0;
    }
    return n;
}

// The Params: each one's ordinal among them and its place in `params_`, the F64s first so that each stands aligned.
template <std::size_t N>
struct ParamPlaces {
    std::array<std::size_t, N> ordinal{};
    std::array<std::size_t, N> at{};
    std::size_t bytes{0};
};
template <std::size_t N>
constexpr ParamPlaces<N> param_places(const Graph& g, const std::array<ElemType, N>& type) {
    ParamPlaces<N> p{};
    std::size_t n = 0;
    for (const ElemType t : {ElemType::F64, ElemType::F32}) {
        for (std::size_t i = 0; i < N; ++i) {
            if (is_param(g, g.inputs[i]) && type[i] == t) {
                p.ordinal[i] = n++;
                p.at[i] = p.bytes;
                p.bytes += static_cast<std::size_t>(loops::bytes_of(t));
            }
        }
    }
    return p;
}

// Each of `N` inputs' feed buffer in the program (-1: the program reads none).
template <std::size_t N>
constexpr std::array<std::int32_t, N> feed_buffers(const loops::Program& p) {
    std::array<std::int32_t, N> at{};
    at.fill(-1);
    for (std::size_t b = 0; b < p.buffers.size(); ++b) {
        if (p.buffers[b].home == loops::Home::Feed && static_cast<std::size_t>(p.buffers[b].feed) < N) {
            at[static_cast<std::size_t>(p.buffers[b].feed)] = static_cast<std::int32_t>(b);
        }
    }
    return at;
}

// The constraints the graph declares for the dimension `id`.
constexpr DimConstraints constraints_of(const Graph& g, std::int32_t id) {
    DimConstraints c{};
    for (const DimDecl& d : g.dims) {
        c = d.id == id ? d.constraints : c;
    }
    return c;
}

// A member of no entries: an empty type of its own.
struct Nothing {};

// `N` flags in the fewest bytes.
template <std::size_t N>
using Bits = std::conditional_t<N == 0, Nothing,
                                std::conditional_t<N <= 8, std::uint8_t,
                                                   std::conditional_t<N <= 16, std::uint16_t,
                                                                      std::conditional_t<N <= 32, std::uint32_t,
                                                                                         std::conditional_t<N <= 64, std::uint64_t, std::bitset<N>>>>>>;
template <std::size_t N>
constexpr Bits<N> bit(std::size_t k) {
    return Bits<N>{1} << k;
}

// A port's numbers as a run holds them.
template <ElemType T>
using Number = std::conditional_t<T == ElemType::F32, float, double>;

// The first `N` ranges of a compile result's list, as a constant of their own.
template <std::size_t N>
constexpr std::array<Range, N> first(const fixed_vector<Range, kMaxIO>& list) {
    std::array<Range, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = list[i];
    }
    return out;
}

// A failed check's words (Plan/26-value-ranges/04), written while compiling: a run writes only the numbers it found
// between them. `before` leaves room for the longest numbers a check finds, cutting a long port name, and the range
// comes last: so a message's 95 characters cut only a name, and then the range, never what was found.
struct Words {
    fixed_string<kMaxMessageLength> before{};
    fixed_string<kMaxMessageLength> after{};
};

// The longest a check writes of what it found: an index of 19 digits, and a number in the shortest digits of a double,
// 24 characters (-2.2250738585072014e-308). An input's index is closed by `kIndexEnd`.
inline constexpr std::size_t kIndexDigits = 19;
inline constexpr std::size_t kNumberCharacters = 24;
inline constexpr std::string_view kIndexEnd = "] = ";

// ` is outside ge(0), le(1)`, its bounds in the shortest digits that read back as them: six digits would show a number
// just past a bound as the bound.
constexpr fixed_string<kMaxMessageLength> outside(const Range& range) {
    MessageWriter after{MessageWriter::Digits::Shortest};
    after << std::string_view{" is outside "};
    ranges::write_range(after, range);
    return after.problem().message;
}

// `param gain = ` 2 ` is outside ge(0), le(1)`; `input x[` 17 `] = ` 2 ` is outside ge(-1), le(1)`. A name is cut where
// the longest index and number would not fit after it: an input's past 22 characters, a Param's past 43.
constexpr Words port_words(bool param, std::string_view name, const Range& range) {
    const std::string_view head = param ? "heinzel::Executor: param " : "heinzel::Executor: input ";
    const std::string_view open = param ? " = " : "[";
    const std::size_t found = param ? kNumberCharacters : kIndexDigits + kIndexEnd.size() + kNumberCharacters;
    MessageWriter before;
    before << head << name.substr(0, kMaxMessageLength - 1 - head.size() - open.size() - found) << open;
    return Words{before.problem().message, outside(range)};
}

// `promise #0 broken at sample ` 17 `: ` 0 ` is outside gt(0), le(2)`; of a value with no time axis, as a weight
// tensor's is, `broken at element ` 2.
constexpr Words promise_words(std::size_t k, bool by_element, const Range& range) {
    MessageWriter before;
    before << std::string_view{"heinzel::Executor: promise #"} << static_cast<std::int64_t>(k)
           << std::string_view{by_element ? " broken at element " : " broken at sample "};
    return Words{before.problem().message, outside(range)};
}

// A check that failed: its words around what it found, each number the double it was compared as.
template <class... Found>
[[noreturn]] void broken(const Words& words, Found... found) {
    MessageWriter out{MessageWriter::Digits::Shortest};
    out << words.before.view();
    (out << ... << found);
    out << words.after.view();
    fail(out.problem().message.view());
}

// `count` numbers inside `range`: the first outside fails, named by its place over `per` (an element, or a sample), and
// written as the double it was compared as, so that 0.1f reads 0.10000000149011612 beside `le(0.1)`.
template <class V>
void hold(const V* numbers, std::int64_t count, std::int64_t per, const Range& range, const Words& words, std::string_view between) {
    for (std::int64_t i = 0; i < count; ++i) {
        const auto x = static_cast<double>(numbers[i]);
        if (!ranges::holds(range, x)) {
            broken(words, i / per, between, x);
        }
    }
}

// Which of an analysed graph's last `N` outputs have no time axis (Decision 53): a tensor's value, a Param's, or one of
// those and constants alone. A check of one names an element; of a signal a sample, also where a run holds one.
template <std::size_t N>
constexpr std::array<bool, N> timeless_outputs(const Graph& g) {
    if (N == 0) {
        return {}; // a release Executor, or a debug one of no promise: no time kinds worked out for nothing
    }
    TimeKinds kind{};
    time_kinds(g, declared_variables(g), kind);
    std::array<bool, N> timeless{};
    for (std::size_t k = 0; k < N; ++k) {
        timeless[k] = kind[static_cast<std::size_t>(g.outputs[g.outputs.size() - N + k].value)] != TimeKind::Signal;
    }
    return timeless;
}

// Why an `Executor` does not run a program that compiled, or nothing: it runs one symbol at most, and that on the time
// axis, and carries state only along the stream (`loops/streams.hpp`). `print<g>()` asks too, so that it lists no
// program an `Executor` refuses. The words are named for the C++23 floor, whose `static_assert` messages are literals.
inline constexpr std::string_view kOffTime = "heinzel::Executor: at most one symbol, on the time axis";
inline constexpr std::string_view kOffStream = "heinzel::Executor: this graph's state does not run along the stream";
constexpr std::string_view why_not_run(const loops::Program& p) {
    if (!loops::stream_symbol(p).on_time) {
        return kOffTime;
    }
    if (!loops::streams(p)) {
        return kOffStream;
    }
    return {};
}

} // namespace detail

// A port's name, looked up while compiling: `ex.input("gian")` does not build. A refused graph has no ports, and every
// lookup then succeeds without looking, so the refusal is the one error a user reads.
template <const CompileResult& R, Port Kind>
struct PortName {
    std::size_t index{0};

    consteval PortName(const char* name) : index(find(name)) {}

private:
    static consteval std::size_t find(std::string_view name) {
        if (!R.refusal.view().empty()) {
            return 0;
        }
        const Graph& g = R.compiled.graph;
        const auto& ports = Kind == Port::Output ? g.outputs : g.inputs;
        // (an output `#k` is a debug build's check of a promise, no port of the caller's; an input may be named so)
        const bool check = Kind == Port::Output && name.starts_with('#');
        for (std::size_t i = 0; i < ports.size() && !check; ++i) {
            if (ports[i].name.view() != name) {
                continue;
            }
            if (Kind == Port::Input && detail::is_param(g, ports[i])) {
                fail("heinzel::Executor: a Param: use param()");
            }
            if (Kind == Port::Param && !detail::is_param(g, ports[i])) {
                fail("heinzel::Executor: not a Param");
            }
            return i;
        }
        fail("heinzel::Executor: no port by that name");
    }
};

} // namespace heinzel
