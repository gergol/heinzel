// Plan/23 P4 4.1 (sets `Machine`'s latencies and the rule table's `Latency` classes): each operation's latency (a
// dependent chain, cycles per operation) and issue cost (independent chains, twelve on x86 and 24 on ARM, cycles per
// operation), in float and in double, one number and a register of lanes (16, 32 or 64 bytes on x86 as the flags
// allow, 16 with NEON); Heinzel's float cores (`runtime/float_cores.hpp`) the same, per element; loads and stores a
// cycle from L1. Cycles are the processor's own count where Linux allows it, else nanoseconds times the clock measured
// before each row on a chain of dependent integer adds (`cycles.hpp`: macOS, a Linux that refuses the counter, or
// `-DCYCLES_CALIBRATED`). An empty asm statement per operation keeps each in a register and in order, and costs no
// instruction; its constraint is the target's vector register ("x" on x86, "v" with AVX-512, "w" on ARM).
//   clang++-22 -std=c++2c -O2 -march=native -ffp-contract=off -I include tools/tune/latency.cpp -o latency && taskset -c 4 ./latency
//
// zen2 (2026-10-08, two rounds identical to the cycle; the data sheet's numbers), latency / issue in cycles:
//   add, multiply 3 / 0.5; multiply-add 5 / 0.5; compare-and-select 1 / 0.5 — float and double, one or a register
//   divide: float 10 / 3.5, double 13 / 5; square root: float 14 / 5.5, double 20 / 8.5-8.9
//   float -> double -> float: one number 6 / 2, four lanes 10 / 2
//   a lane folded into one number (`folded`), splatted back (2026-10-09, Plan/23 10.7): four doubles 19 against 7 for the
//   same chain reading lane 0, eight floats 28 against 10, so 12 for two steps and 18 for three: 6 a step, the add's 3
//   and the shuffle's 3 (`Machine::shuffle`)
//   float cores, one number (a register of eight: issue per register): exp 62 / 18 (60 / 20), tanh 81 / 30 (80 / 30),
//   sin 59 / 32 (63 / 33), sinpi 46 / 19 (46 / 19), log 91 / 34 (82 / 29)
//   loads 1.87 a cycle, stores 1.00 (32 bytes each, from L1); the M1 Pro (2026-10-12, written out): 3.00 and 2.00
//   double cores at `Normal`, one number (2026-10-09, Plan/23 4.3b: `Machine::core_double` 110 / 100): exp 101 / 37,
//   tanh 181 / 131, sin 106 / 50, sinpi 70 / 27, log 118 / 50
// So double costs a chain nothing but at a division or a root (the Padé ladder's 1.05x in double, 3.4), a
// conversion pair 6 cycles, and a core on a chain dominates it (tanh 80: a ladder's four are 320 cycles a sample).
//
// What it prints for `Machine` (Plan/24 `01-Fields.md`), one `tuned <field> <value>` line each (an `OpCost` as its
// latency, then its slots), from each figure's least over the two rounds:
//   fp_per_cycle: 1 / a float add's issue in a register, rounded;
//   add, multiply_add, divide, root (float) and divide_double, root_double: a register's latency, rounded, and its
//   issue times fp_per_cycle, rounded;
//   core, core_double: over the five cores (exp, tanh, sin, sinpi, log; one number) the median latency and the median
//   issue times fp_per_cycle, each rounded to the nearest multiple of ten (Gerald, 2026-10-10: rounded up, a tenth of a
//   cycle decided, zen2's float issue 30.1 x 2 = 60.2 read 70; nearest keeps each reading at least 4 cycles from a
//   boundary);
//   shuffle: a fold's step less a register add's latency, (folded - lane 0) / log2(lanes) - add, the mean of the
//   doubles' and the floats', rounded (its slots stay 1);
//   forwarded: a float stored, loaded back and added to, less a float add's latency, rounded;
//   loads_per_cycle, stores_per_cycle: rounded.
// zen2 (2026-10-10, three runs alike): fp_per_cycle 2; add 3 1, multiply_add 5 1, divide 10 7, divide_double 13 10,
// root 14 11, root_double 20 17; shuffle 3 1; forwarded 8; loads 2, stores 1; core 60 60 (`machine.hpp` 70 60: a median
// latency of 61.6) and core_double 110 100, two runs by the nearest ten alike.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "heinzel/runtime/float_cores.hpp"
#include "heinzel/runtime/instruction_set.hpp"
#include "heinzel/runtime/lane_value.hpp"

#include "cycles.hpp"

namespace {

// The target's vector registers: their asm constraint, their width (the widest the flags allow, as `machine.cmake`
// reads `vector_bytes` from the same macros), and how many independent chains fill its pipes without spilling — more
// than latency x ports of any operation measured (zen2's multiply-add 5 x 2; the M1's four pipes, 4 x 4), fewer than
// its registers less the constants (16 on x86, 32 on ARM).
#if defined(__aarch64__)
#define LATENCY_IN "w"
#define LATENCY_INOUT "+w"
constexpr std::size_t kVectorBytes = 16;
constexpr int kChains = 24;
#elif defined(__AVX512F__)
#define LATENCY_IN "v" // "x" reaches only the first 16 registers, and so no 64-byte one
#define LATENCY_INOUT "+v"
constexpr std::size_t kVectorBytes = 64;
constexpr int kChains = 12;
#elif defined(__AVX__)
#define LATENCY_IN "x"
#define LATENCY_INOUT "+x"
constexpr std::size_t kVectorBytes = 32;
constexpr int kChains = 12;
#else
#define LATENCY_IN "x"
#define LATENCY_INOUT "+x"
constexpr std::size_t kVectorBytes = 16;
constexpr int kChains = 12;
#define LATENCY_NO_HALF_REGISTER // no constraint takes the 8 bytes of floats two doubles convert from
#endif

constexpr long kSteps = 1 << 22;

tune::Cycles counter;

// An operation's cycles: on a dependent chain, and of issue a step (independent chains).
struct Cost {
    double latency = 1e300;
    double issue = 1e300;
};

template <class V>
inline void keep(V& v) {
    if constexpr (requires { v.v; }) {
        asm volatile("" : LATENCY_INOUT(v.v));
    } else {
        asm volatile("" : LATENCY_INOUT(v));
    }
}

// Cycles per operation: `op` applied along one chain, then along `kChains`.
template <class V, class Op>
Cost row(const char* what, V start, Op op) {
    const double lat = counter.least(5, [&] {
        V x = start;
        for (long i = 0; i < kSteps; ++i) {
            x = op(x), keep(x);
        }
        V sink = x;
        keep(sink);
    }) / kSteps;
    const double thr = counter.least(5, [&] {
        V x[kChains];
        for (V& v : x) {
            v = start;
        }
        for (long i = 0; i < kSteps / kChains; ++i) {
            for (V& v : x) {
                v = op(v), keep(v);
            }
        }
        for (V& v : x) {
            keep(v);
        }
    }) / (kSteps / kChains * kChains);
    std::printf("  %-34s latency %5.2f  issue %5.2f\n", what, lat, thr);
    return {lat, thr};
}

template <class T, std::size_t W>
using L = heinzel::Lane<T, W>;
template <class T, std::size_t W>
L<T, W> s(T v) { return L<T, W>::splat(v); }

enum Op : std::size_t { kAdd, kMultiply, kMultiplyAdd, kDivide, kRoot, kSelect, kOps };

template <class T, std::size_t W>
std::array<Cost, kOps> arithmetic(const char* type) {
    using V = std::conditional_t<W == 1, T, L<T, W>>;
    const auto k = [](T v) { if constexpr (W == 1) { return v; } else { return s<T, W>(v); } };
    std::printf("%s x %zu\n", type, W);
    std::array<Cost, kOps> c;
    c[kAdd] = row<V>("add", k(1), [&](V x) { return x + k(T(1e-7)); });
    c[kMultiply] = row<V>("multiply", k(1), [&](V x) { return x * k(T(0.9999999)); });
    // As the runtime computes `_madd` (`multiply_add_as_the_machine_rounds`): fused where that is one instruction, else a
    // multiply and an add (`std::fma` there is a library call, 34 cycles on zen2 at x86-64-v2, which no program runs).
    c[kMultiplyAdd] = row<V>("multiply-add (as the runtime rounds it)", k(1), [&](V x) {
        if constexpr (!heinzel::kMultiplyAddIsOneInstruction) { return x * k(T(0.999)) + k(T(0.001)); }
        else if constexpr (W == 1) { return std::fma(x, T(0.999), T(0.001)); } else { return V{__builtin_elementwise_fma(x.v, k(T(0.999)).v, k(T(0.001)).v)}; }
    });
    c[kDivide] = row<V>("divide", k(2), [&](V x) { return k(T(1)) / x; });
    c[kRoot] = row<V>("square root", k(2), [&](V x) {
        if constexpr (W == 1) { return std::sqrt(x); } else { return V{__builtin_elementwise_sqrt(x.v)}; }
    });
    c[kSelect] = row<V>("compare and select (min)", k(1), [&](V x) {
        if constexpr (W == 1) { return x < T(0.5) ? x : T(0.5); } else { return heinzel::min(x, k(T(0.5))); }
    });
    return c;
}

// A float converted to double and back: the pair's latency (an empty asm between keeps both).
template <std::size_t W>
void conversions() {
    using F = std::conditional_t<W == 1, float, float __attribute__((vector_size(4 * W)))>;
    using D = std::conditional_t<W == 1, double, double __attribute__((vector_size(8 * W)))>;
    char what[40];
    std::snprintf(what, sizeof what, "float -> double -> float, x %zu", W);
    row<F>(what, F{} + 1.0f, [](F x) {
        D d;
        if constexpr (W == 1) { d = static_cast<D>(x); } else { d = __builtin_convertvector(x, D); }
        asm volatile("" : LATENCY_INOUT(d));
        if constexpr (W == 1) { return static_cast<F>(d); } else { return __builtin_convertvector(d, F); }
    });
}

// A lane folded into one number as the runner folds a feedback stage's voices every sample (`folded`: log2(W) steps,
// each a shuffle and an add), its number splatted back: against the same chain reading lane 0 (no instruction), the
// difference is the fold, and a step less its add the shuffle (Plan/23 10, `Machine::shuffle`).
template <class T, std::size_t W>
std::array<Cost, 2> lane_folds(const char* type) {
    using V = L<T, W>;
    char what[2][48];
    std::snprintf(what[0], sizeof what[0], "%s x %zu splat, add, lane 0", type, W);
    std::snprintf(what[1], sizeof what[1], "%s x %zu splat, add, folded", type, W);
    const auto lane = [](T x) { // (kept as a register of lanes, so that no fold is seen through)
        V v = V::splat(x) + s<T, W>(T(1e-7) / T(W));
        keep(v);
        return v;
    };
    // (the same multiply in both: bounded)
    return {row<T>(what[0], T(1), [&](T x) { return lane(x).v[0] * T(1.0 / W); }),
            row<T>(what[1], T(1), [&](T x) { return heinzel::folded(heinzel::ReduceOp::Sum, lane(x)) * T(1.0 / W); })};
}

template <std::size_t W>
std::array<Cost, 5> cores() {
    using V = std::conditional_t<W == 1, float, L<float, W>>;
    const auto k = [](float v) { if constexpr (W == 1) { return v; } else { return s<float, W>(v); } };
    std::printf("float cores x %zu (per element: divide issue by %zu for a lane's share)\n", W, W);
    return {row<V>("exp", k(0.5f), [&](V x) { return heinzel::exponential_in_float(x * k(-0.5f)); }),
            row<V>("tanh", k(0.5f), [&](V x) { return heinzel::hyperbolic_tangent_in_float(x + k(0.5f)); }),
            row<V>("sin", k(0.5f), [&](V x) { return heinzel::sine_in_float(x + k(1.0f)); }),
            row<V>("sinpi", k(0.5f), [&](V x) { return heinzel::sine_of_half_turns_in_float(x * k(0.5f) + k(0.25f)); }),
            row<V>("log", k(0.5f), [&](V x) { return heinzel::logarithm_in_float(x + k(2.0f)); })};
}

// The double cores a cycle computed in double runs at `Normal` (the exact series; each number on its own, as the runner
// runs a lane of them), one number: `Machine::core_double` (Plan/23 04 4.3b).
std::array<Cost, 5> double_cores() {
    using heinzel::FnAccuracy;
    std::printf("double cores x 1 (Normal: the exact series)\n");
    return {row<double>("exp", 0.5, [](double x) { return heinzel::exponential(x * -0.5, FnAccuracy::Exact); }),
            row<double>("tanh", 0.5, [](double x) { return heinzel::hyperbolic_tangent(x + 0.5, FnAccuracy::Exact); }),
            row<double>("sin", 0.5, [](double x) { return heinzel::sine(x + 1.0, FnAccuracy::Exact); }),
            row<double>("sinpi", 0.5,
                        [](double x) { return heinzel::sine_of_half_turns(x * 0.5 + 0.25, FnAccuracy::Exact); }),
            row<double>("log", 0.5, [](double x) { return heinzel::natural_logarithm(x + 2.0, FnAccuracy::Exact); })};
}

// Loads and stores a cycle, a register's bytes each, from and to 4 KB of L1, `kChains` to an iteration at fixed offsets
// from a block's base, written out; a memory clobber a pass keeps the compiler from hoisting them. Rolled, as clang
// leaves the loop over them at -O2 on ARM, each load was a taken branch and an index's `and` and shift, and the M1
// read one load a cycle and one store, its taken branches' rate (`Plan/probes/loads/loads.cpp`: 3 and 2). ARM pairs
// the loads into `ldp`, two loads each. Returns cycles a load, a store, and a float stored, loaded back and added to.
std::array<double, 3> memory() {
    alignas(64) static float buffer[1024];
    using V = float __attribute__((vector_size(kVectorBytes)));
    constexpr long kFloats = kVectorBytes / sizeof(float);
    constexpr long kBlock = kFloats * kChains;
    constexpr long kBlocks = 1024 / kBlock; // ten; eight, a power of two, read zen2's loads 1.60 (ten: 1.85)
    const double loads = counter.least(5, [&] {
        for (long i = 0; i < kSteps / kChains; ++i) {
            const float* base = buffer + kBlock * (i % kBlocks);
#pragma clang loop unroll(full)
            for (int c = 0; c < kChains; ++c) {
                V v;
                std::memcpy(&v, base + kFloats * c, sizeof v);
                asm volatile("" : : LATENCY_IN(v));
            }
            asm volatile("" : : : "memory");
        }
    });
    const double stores = counter.least(5, [&] {
        V v{};
        for (long i = 0; i < kSteps / kChains; ++i) {
            float* base = buffer + kBlock * (i % kBlocks);
#pragma clang loop unroll(full)
            for (int c = 0; c < kChains; ++c) {
                asm volatile("" : LATENCY_INOUT(v));
                std::memcpy(base + kFloats * c, &v, sizeof v);
            }
            asm volatile("" : : : "memory");
        }
    });
    const double n = double(kSteps / kChains * kChains);
    std::printf("memory, %zu bytes each\n  loads a cycle %.2f   stores a cycle %.2f\n", kVectorBytes, n / loads, n / stores);
    // A value stored and loaded back, then added to: the store-to-load forwarding latency plus an add's.
    const double forwarded = counter.least(5, [&] {
        for (long i = 0; i < kSteps; ++i) {
            float v = buffer[0];
            v = v + 1e-7f;
            buffer[0] = v;
            asm volatile("" : : : "memory");
        }
    }) / kSteps;
    std::printf("  a float stored, loaded back and added to: %.2f cycles (less an add's 3: the forwarding)\n", forwarded);
    return {loads / n, stores / n, forwarded};
}

// Every figure a rule reads, the least over the rounds: noise only ever adds cycles.
struct Seen {
    std::array<Cost, kOps> one_float; // a float
    std::array<Cost, kOps> floats; // a register of floats
    std::array<Cost, kOps> doubles; // a register of doubles
    // Each fold pair: [0] a lane read, [1] a lane folded (`lane_folds`).
    std::array<Cost, 2> fold_doubles;
    std::array<Cost, 2> fold_floats;
    std::array<Cost, 5> cores; // one number
    std::array<Cost, 5> double_cores; // one number
    std::array<double, 3> memory{1e300, 1e300, 1e300};
};

Cost least(Cost a, Cost b) { return {std::min(a.latency, b.latency), std::min(a.issue, b.issue)}; }
double least(double a, double b) { return std::min(a, b); }
template <class T, std::size_t N>
void keep_least(std::array<T, N>& seen, const std::array<T, N>& now) {
    for (std::size_t i = 0; i < N; ++i) {
        seen[i] = least(seen[i], now[i]);
    }
}

long long rounded(double x) { return std::llround(x); }
long long to_ten(double x) { return 10 * std::llround(x / 10); }
double median(std::array<double, 5> v) {
    std::sort(v.begin(), v.end());
    return v[2];
}

// A fold's step, less its add: (folded - lane 0) over its log2(W) steps, less the add's latency.
double shuffle(const std::array<Cost, 2>& fold, std::size_t lanes, const Cost& add) {
    return (fold[1].latency - fold[0].latency) / std::log2(double(lanes)) - add.latency;
}

// The `Machine` fields, by the rules in the header.
void print_tuned(const Seen& s, std::size_t floats, std::size_t doubles) {
    const long long fp = rounded(1 / s.floats[kAdd].issue);
    const auto op = [&](const char* field, Cost c) {
        std::printf("tuned %s %lld %lld\n", field, rounded(c.latency), rounded(c.issue * double(fp)));
    };
    const auto core = [&](const char* field, const std::array<Cost, 5>& c) {
        std::array<double, 5> latency{};
        std::array<double, 5> issue{};
        for (std::size_t i = 0; i < c.size(); ++i) {
            latency[i] = c[i].latency, issue[i] = c[i].issue;
        }
        std::printf("tuned %s %lld %lld\n", field, to_ten(median(latency)), to_ten(median(issue) * double(fp)));
    };
    std::printf("tuned fp_per_cycle %lld\n", fp);
    op("add", s.floats[kAdd]), op("multiply_add", s.floats[kMultiplyAdd]);
    op("divide", s.floats[kDivide]), op("divide_double", s.doubles[kDivide]);
    op("root", s.floats[kRoot]), op("root_double", s.doubles[kRoot]);
    core("core", s.cores), core("core_double", s.double_cores);
    const double step_doubles = shuffle(s.fold_doubles, doubles, s.doubles[kAdd]);
    std::printf("tuned shuffle %lld 1\n", rounded((step_doubles + shuffle(s.fold_floats, floats, s.floats[kAdd])) / 2));
    std::printf("tuned forwarded %lld\n", rounded(s.memory[2] - s.one_float[kAdd].latency));
    std::printf("tuned loads_per_cycle %lld\n", rounded(1 / s.memory[0]));
    std::printf("tuned stores_per_cycle %lld\n", rounded(1 / s.memory[1]));
}

} // namespace

int main() {
    std::printf("cycles: %s\n", counter.counted() ? "the processor's count (perf_event_open)"
                                                  : "nanoseconds times a chain of dependent integer adds' cycles a "
                                                    "nanosecond, measured before each row");
    constexpr std::size_t kFloats = kVectorBytes / sizeof(float);
    constexpr std::size_t kDoubles = kVectorBytes / sizeof(double);
    Seen seen;
    for (int round = 0; round < 2; ++round) {
        std::printf("== round %d (cycles per operation)\n", round + 1);
        keep_least(seen.one_float, arithmetic<float, 1>("float"));
        keep_least(seen.floats, arithmetic<float, kFloats>("float"));
        arithmetic<double, 1>("double");
        keep_least(seen.doubles, arithmetic<double, kDoubles>("double"));
        conversions<1>();
#ifndef LATENCY_NO_HALF_REGISTER
        conversions<kDoubles>();
#endif
        keep_least(seen.fold_doubles, lane_folds<double, kDoubles>("double"));
        keep_least(seen.fold_floats, lane_folds<float, kFloats>("float"));
        keep_least(seen.cores, cores<1>());
        cores<kFloats>();
        keep_least(seen.double_cores, double_cores());
        keep_least(seen.memory, memory());
    }
    print_tuned(seen, kFloats, kDoubles);
}
