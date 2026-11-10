#pragma once

// Computing a statement's value at a width (a number, or a `Lane` of them) from its operands, and storing it. What an
// operation means stays in `runtime/compute.hpp` (`compute_as`). An F32 value is computed in float, except under
// `High`, which computes in double and rounds once; a gather too: at `High`, or over a double, a lane at a time in
// double, as the reference walk computes it, bit for bit; else its tap and blend in float, over every lane at once. A
// table read whose phase the proof keeps in one turn (`Stmt::phase_in_one_turn`) takes no wrap, the same tap.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "heinzel/ir/timing.hpp"
#include "heinzel/loops/program.hpp"
#include "heinzel/loops/walk.hpp"
#include "heinzel/runtime/compute.hpp"
#include "heinzel/runtime/lane_value.hpp"
#include "heinzel/runtime/table_gather_element.hpp"

namespace heinzel::loops {

template <class T, std::size_t W>
using ValueOf = std::conditional_t<W == 1, T, Lane<T, W>>;

// No value here: a statement whose value is made elsewhere (a closed loop, a later sibling) pads the locals.
struct Absent {};

template <class To, class T, std::size_t W>
HEINZEL_LOOPS_INLINE ValueOf<To, W> converted(ValueOf<T, W> x) {
    if constexpr (std::is_same_v<To, T>) {
        return x;
    } else if constexpr (W == 1) {
        return static_cast<To>(x);
    } else {
        return {__builtin_convertvector(x.v, typename Lane<To, W>::vec)};
    }
}

// A value `made` at some width, read at width W as `To`: converted, and splatted where it was made at width 1 (a value
// made after a fold, read inside a later lane loop).
template <class To, class T, std::size_t W>
HEINZEL_LOOPS_INLINE ValueOf<To, W> widened(const auto& made) {
    if constexpr (W > 1 && std::is_arithmetic_v<std::remove_cvref_t<decltype(made)>>) {
        return Lane<To, W>::splat(static_cast<To>(made));
    } else {
        return converted<To, T, W>(made);
    }
}

template <const Program& P, std::size_t S, std::size_t K>
inline constexpr const Stmt& kStmt = P.stmts[static_cast<std::size_t>(P.stages[S].first) + K];

// Reads slot `Slot`'s element where the walk stands, stored as T, at width W as C: a lane load if the slot moves along
// the lane axis (only its first `valid` lanes in a tail), otherwise a splat of one number.
template <const Program& P, std::size_t S, std::size_t Slot, std::size_t W, class C, class T>
HEINZEL_LOOPS_INLINE ValueOf<C, W> read(const auto& walk) {
    const T* from = reinterpret_cast<const T*>(walk.at[Slot]);
    if constexpr (W == 1) {
        return converted<C, T, 1>(*from);
    } else {
        constexpr bool moves = access_of<P, S>(Slot).buffer_axis[static_cast<std::size_t>(kLaneAxis<P, S>)] >= 0;
        if constexpr (std::remove_cvref_t<decltype(walk)>::tail && moves) {
            return converted<C, T, W>(Lane<T, W>::load_first(from, lanes_valid<W>(walk)));
        } else {
            return converted<C, T, W>(moves ? Lane<T, W>::load(from) : Lane<T, W>::splat(*from));
        }
    }
}

// Operand `I` of statement K at width W, in type C.
template <const Program& P, std::size_t S, std::size_t K, std::size_t I, std::size_t W, class C, class... Locals>
HEINZEL_LOOPS_INLINE ValueOf<C, W> operand(const auto& walk, const Locals&... locals) {
    constexpr const Access& a = kStmt<P, S, K>.in[I];
    if constexpr (a.source == Source::Immediate) {
        return Broadcast<ValueOf<C, W>>::of(std::bit_cast<double>(a.immediate));
    } else if constexpr (a.source == Source::First) {
        return Broadcast<ValueOf<C, W>>::of(kAtStart<std::remove_cvref_t<decltype(walk)>> ? 1.0 : 0.0);
    } else if constexpr (a.source == Source::Local) {
        // Pack indexing, not `std::get` on a tuple: that made instantiating a stage cubic in its size.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++26-extensions" // accepted under C++23 too, as an extension
        const auto& made = locals...[static_cast<std::size_t>(a.local)];
#pragma clang diagnostic pop
        return widened<C, element_t<kStmt<P, S, static_cast<std::size_t>(a.local)>.type>, W>(made);
    } else if constexpr (a.source == Source::Buffer) {
        return read<P, S, K * kSlots + I, W, C, element_t<P.buffers[static_cast<std::size_t>(a.buffer)].type>>(walk);
    } else {
        return ValueOf<C, W>{};
    }
}

// Stores statement K's value where its store says, if it has one; in a tail, only the first `valid` lanes.
template <const Program& P, std::size_t S, std::size_t K, std::size_t W>
HEINZEL_LOOPS_INLINE void stored(const auto& walk, ValueOf<element_t<kStmt<P, S, K>.type>, W> made) {
    if constexpr (kStmt<P, S, K>.store.source == Source::Buffer) {
        using T = element_t<kStmt<P, S, K>.type>;
        T* to = reinterpret_cast<T*>(walk.at[K * kSlots + kMaxNodeInputs]);
        if constexpr (W == 1) {
            *to = made;
        } else if constexpr (std::remove_cvref_t<decltype(walk)>::tail) {
            made.store_first(to, lanes_valid<W>(walk));
        } else {
            made.store(to);
        }
    }
}

// Writes `value` where operand I of statement K reads it, in the buffer's type: a State's register closed back into
// the past it was opened from (`NodeKind::Close`); in a tail, only the first `valid` lanes.
template <const Program& P, std::size_t S, std::size_t K, std::size_t I, std::size_t W, class V>
HEINZEL_LOOPS_INLINE void written_back(const auto& walk, const V& value) {
    using T = element_t<P.buffers[static_cast<std::size_t>(kStmt<P, S, K>.in[I].buffer)].type>;
    T* to = reinterpret_cast<T*>(walk.at[K * kSlots + I]);
    if constexpr (W == 1) {
        *to = value;
    } else if constexpr (std::remove_cvref_t<decltype(walk)>::tail) {
        value.store_first(to, lanes_valid<W>(walk));
    } else {
        value.store(to);
    }
}

// A read whose address is a signal, computed a lane at a time in double, as the walk computes it (`High`, or a double
// operand). The position says which two samples of the indexed axis to blend: for a delay line, so many samples back
// from where the walk stands; for a table, a place in it.
template <const Program& P, std::size_t S, std::size_t K, std::size_t W, class... Locals>
HEINZEL_LOOPS_INLINE auto gathered(const auto& walk, const Locals&... locals) {
    constexpr const Stmt& st = kStmt<P, S, K>;
    using T = element_t<st.type>;
    using From = element_t<P.buffers[static_cast<std::size_t>(st.in[0].buffer)].type>;
    constexpr std::size_t slot = K * kSlots;
    constexpr std::size_t axes = P.stages[S].axes.size();
    const ValueOf<double, W> position = operand<P, S, K, 1, W, double>(walk, locals...);
    const std::ptrdiff_t along = (*walk.stride)[slot][axes]; // one step of the axis the position indexes
    // A source that stands still along the lanes (a table constant over time, lanes along time) is read at one
    // place by every lane.
    constexpr bool moves = W > 1 && access_of<P, S>(slot).buffer_axis[static_cast<std::size_t>(kLaneAxis<P, S>)] >= 0;
    constexpr std::ptrdiff_t per_lane = moves ? lane_bytes<P, S>()[slot] : 0;
    const auto blend = [&] [[gnu::always_inline]] (std::size_t lane, double where) {
        const std::byte* at = walk.at[slot] + static_cast<std::ptrdiff_t>(lane) * per_lane;
        std::ptrdiff_t near = 0;
        std::ptrdiff_t far = 0;
        double fraction = 0.0;
        if constexpr (st.gather == Gather::Back) {
            const SignalledTap tap = signalled_tap(st.reach, where);
            near = -tap.whole, far = near - 1, fraction = tap.fraction;
        } else {
            const TableTap tap = table_tap<!st.phase_in_one_turn>(st.reach, where);
            near = tap.index, far = tap.next, fraction = tap.fraction;
        }
        return static_cast<T>(lerp2(*reinterpret_cast<const From*>(at + near * along), *reinterpret_cast<const From*>(at + far * along), fraction));
    };
    if constexpr (W == 1) {
        return blend(0, position);
    } else {
        std::array<T, W> made{};
        const std::size_t valid = std::remove_cvref_t<decltype(walk)>::tail ? lanes_valid<W>(walk) : W;
        each<W>([&] [[gnu::always_inline]] (std::size_t lane) {
            if (lane < valid) { // past a ragged end there is no position, and nothing to read where it would point
                made[lane] = blend(lane, position[lane]);
            }
        });
        return Lane<T, W>::load(made.data());
    }
}

// ---- a gather in float --------------------------------------------------------------------------------------------
//
// An F32 read at `Normal` and `Fast`, all its operands floats, takes its tap and its blend in float, as a plain loop
// does: `table_tap`'s and `signalled_tap`'s steps on the float the position is, over every lane at once. Only the two
// loads are a lane at a time. It is no longer the walk's number, which is computed in double and rounded once: how far
// it may lie from it is Decision 78's bound, which `tables_test` and `delay_lines_test` hold. The probe `float-reads`
// (Plan 28) measured the writings: the tap a lane at a time in float about as fast, a hardware gather slower.

template <class V>
struct FloatTap {
    V whole;    // integral-valued: the near cell, or how many samples back the near tap is
    V fraction; // how far toward the far tap, in [0, 1); a NaN where a table's phase is no number
};

// `table_tap` in float: x = frac(phase) N lies in [0, N]; a place that rounds up to N, and a NaN, fail `x < N` and read
// the first cell, the NaN handed on by `x - x`. The floor of a place below N's float is at most N - 1 whatever N is (a
// float above 2^24 is whole, and N's float the nearest), so no index leaves the table. A phase in one turn (`Wraps`
// false) is read with no wrap, as `table_tap` reads it: a place below 0, made only by a false fact, at the first cell.
template <bool Wraps = true, class V>
HEINZEL_LOOPS_INLINE constexpr FloatTap<V> table_tap_in_float(std::int32_t n, V phase) {
    const V cells = Broadcast<V>::of(n);
    const V zero = Broadcast<V>::of(0.0);
    const V turn = Wraps ? phase - static_cast<V>(round_toward_negative(phase)) : phase;
    const V x = turn * cells;
    const V at_least_zero = Wraps ? x : select(x > zero, x, zero);
    const V place = select(x < cells, at_least_zero, zero);
    const V whole = static_cast<V>(round_toward_negative(place));
    return {whole, (place - whole) + (x - x)};
}

// `signalled_tap` in float: the distance held to [0, `most`], a NaN to 0 (`position > 0` fails), so its whole part, the
// truncation, lies in [0, the line's longest] and its fraction is exact.
template <class V>
HEINZEL_LOOPS_INLINE constexpr FloatTap<V> signalled_tap_in_float(float most, V position) {
    const V zero = Broadcast<V>::of(0.0);
    const V ceiling = Broadcast<V>::of(most);
    const V p = select(position > zero, select(position > ceiling, ceiling, position), zero);
    const V whole = static_cast<V>(truncate_toward_zero(p));
    return {whole, p - whole};
}

// The largest float at most `k`: a line longer than 2^24 samples may round to a float past its longest.
constexpr float largest_float_at_most(std::int32_t k) {
    const auto rounded = static_cast<float>(k);
    return static_cast<double>(rounded) > static_cast<double>(k) ? std::bit_cast<float>(std::bit_cast<std::uint32_t>(rounded) - 1) : rounded;
}

template <const Program& P, std::size_t S, std::size_t K, std::size_t W, class... Locals>
HEINZEL_LOOPS_INLINE ValueOf<float, W> gathered_in_float(const auto& walk, const Locals&... locals) {
    constexpr const Stmt& st = kStmt<P, S, K>;
    constexpr std::size_t slot = K * kSlots;
    constexpr std::size_t axes = P.stages[S].axes.size();
    const std::ptrdiff_t along = (*walk.stride)[slot][axes];
    constexpr bool moves = W > 1 && access_of<P, S>(slot).buffer_axis[static_cast<std::size_t>(kLaneAxis<P, S>)] >= 0;
    constexpr std::ptrdiff_t per_lane = moves ? lane_bytes<P, S>()[slot] : 0;
    const ValueOf<float, W> position = operand<P, S, K, 1, W, float>(walk, locals...);
    FloatTap<ValueOf<float, W>> tap{};
    if constexpr (st.gather == Gather::Back) {
        constexpr float most = largest_float_at_most(st.reach);
        tap = signalled_tap_in_float(most, position);
    } else {
        tap = table_tap_in_float<!st.phase_in_one_turn>(st.reach, position);
    }
    // One lane's two taps: a cell and the next, the first after the last; or so many samples back and one more.
    const auto read_taps = [&] [[gnu::always_inline]] (std::size_t lane, float whole, float& near, float& far) {
        const std::byte* at = walk.at[slot] + static_cast<std::ptrdiff_t>(lane) * per_lane;
        const auto k = static_cast<std::int32_t>(whole);
        const std::ptrdiff_t first = st.gather == Gather::Back ? -k : k;
        const std::ptrdiff_t second = st.gather == Gather::Back ? -k - 1 : (k + 1 < st.reach ? k + 1 : 0);
        near = *reinterpret_cast<const float*>(at + first * along);
        far = *reinterpret_cast<const float*>(at + second * along);
    };
    ValueOf<float, W> near{};
    ValueOf<float, W> far{};
    if constexpr (W == 1) {
        read_taps(0, tap.whole, near, far);
    } else {
        std::array<float, W> nears{};
        std::array<float, W> fars{};
        const std::size_t valid = std::remove_cvref_t<decltype(walk)>::tail ? lanes_valid<W>(walk) : W;
        each<W>([&] [[gnu::always_inline]] (std::size_t lane) {
            if (lane < valid) { // past a ragged end there is no position, and nothing to read where it would point
                read_taps(lane, tap.whole[lane], nears[lane], fars[lane]);
            }
        });
        near = Lane<float, W>::load(nears.data());
        far = Lane<float, W>::load(fars.data());
    }
    // `Normal` grants the fused multiply-add where the machine has one.
    return multiply_add_as_the_machine_rounds(tap.fraction, far - near, near);
}

// A Load, a Map or a Gather: its value, in the statement's own type, computed in C: double at `High`, else in its
// operands' type, double where one is a double.
template <const Program& P, std::size_t S, std::size_t K, std::size_t W, class... Locals>
HEINZEL_LOOPS_INLINE auto made_by(const auto& walk, const Locals&... locals) {
    constexpr const Stmt& st = kStmt<P, S, K>;
    using T = element_t<st.type>;
    using C = std::conditional_t<st.precision == numerics::Precision::High, double, element_t<st.computed_in>>;
    if constexpr (st.kind == StmtKind::Gather && std::is_same_v<C, float>) {
        return gathered_in_float<P, S, K, W>(walk, locals...);
    } else if constexpr (st.kind == StmtKind::Gather) {
        return gathered<P, S, K, W>(walk, locals...);
    } else if constexpr (st.kind == StmtKind::Load) {
        return operand<P, S, K, 0, W, T>(walk, locals...);
    } else {
        constexpr FnAccuracy tier = numerics::grants(st.precision).fn; // which core a transcendental takes
        const ValueOf<C, W> r = compute_as<ValueOf<C, W>>(st.op, tier, operand<P, S, K, 0, W, C>(walk, locals...), operand<P, S, K, 1, W, C>(walk, locals...),
                                                          operand<P, S, K, 2, W, C>(walk, locals...));
        return converted<T, C, W>(r);
    }
}

// ---- a Compute at one lane of a skewed stage (`Node::lane`) ------------------------------------------------------

// Is lane L real at the walk's step s, its sample s − L in the block [0, B)? In every plain step. In the head
// (s < k − 1, s < B), lane 0 is and the last lane is not; in the drain (s ≥ B), lane 0 is not and the last lane is
// from step k − 1 on.
template <const Program& P, std::size_t S, std::int32_t L>
HEINZEL_LOOPS_INLINE bool lane_real(const auto& walk) {
    constexpr Steps steps = std::remove_cvref_t<decltype(walk)>::steps;
    constexpr std::int64_t last = kSectionsOf<P, S> - 1;
    if constexpr (steps == Steps::Plain) {
        return true;
    } else if constexpr (steps == Steps::Head) {
        return L == 0 || (L < last && walk.step >= L);
    } else {
        return L > 0 && walk.step >= L && (L == last || walk.step - L < walk.block);
    }
}

// The walk at lane L's sample: every slot L samples back along time.
template <const Program& P, std::size_t S, std::int32_t L>
HEINZEL_LOOPS_INLINE auto at_lane(const auto& walk) {
    if constexpr (L == 0) {
        return walk;
    } else {
        return loop_moved<P, S, node_at(P, P.stages[S].time_loop).axis, 1>(walk, -L);
    }
}

// Statement K at lane L is made in body L / W only, where the lane is real, at the lane's sample; its value is that
// lane's, broadcast. Elsewhere it reads nothing (the fill nothing past the block) and is zero, which no real lane reads.
template <const Program& P, std::size_t S, std::size_t K, std::int32_t L, std::size_t W, class... Locals>
HEINZEL_LOOPS_INLINE auto made_at_lane(const auto& walk, std::size_t body, const Locals&... locals) {
    using T = element_t<kStmt<P, S, K>.type>;
    constexpr auto lane = static_cast<std::size_t>(L);
    ValueOf<T, W> made{};
    if (body == lane / W && lane_real<P, S, L>(walk)) {
        const ValueOf<T, W> all = made_by<P, S, K, W>(at_lane<P, S, L>(walk), locals...);
        if constexpr (W == 1) {
            made = all;
        } else {
            made = Lane<T, W>::splat(all[lane % W]);
        }
    }
    return made;
}

// ...and stored there, one number: a store at one lane moves along no lane.
template <const Program& P, std::size_t S, std::size_t K, std::int32_t L, std::size_t W>
HEINZEL_LOOPS_INLINE void stored_at_lane(const auto& walk, std::size_t body, ValueOf<element_t<kStmt<P, S, K>.type>, W> made) {
    if (body == static_cast<std::size_t>(L) / W && lane_real<P, S, L>(walk)) {
        if constexpr (W == 1) {
            stored<P, S, K, 1>(at_lane<P, S, L>(walk), made);
        } else {
            stored<P, S, K, 1>(at_lane<P, S, L>(walk), made[0]);
        }
    }
}

} // namespace heinzel::loops
