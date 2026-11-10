#pragma once

// The compare-exchange network behind `_order` (sort, top-k, quantile). A sort with data-dependent
// branches does not fit a loop nest; for a compile-time extent a Batcher odd-even mergesort network is a
// fixed sequence of branchless compare-exchanges, the same on every input, so the loop tree and the
// reference walk run the same comparisons in the same order. Selection (top-k, quantile) is the sort
// network pruned to the ranks it needs. Its own namespace keeps `CE` and `Net` out of `heinzel`.

#include <array>
#include <cstddef>
#include <cstdint>

#include "heinzel/support/fail.hpp"

namespace heinzel::order {

// The ordered axis is bounded because the network is a fixed-capacity compile-time object. Raise the
// bound the same in every translation unit: two values would size the network differently in two TUs.
// The `_order` shape check names this override when an extent exceeds it.
#ifndef HEINZEL_MAX_ORDER_EXTENT
#define HEINZEL_MAX_ORDER_EXTENT 256
#endif

inline constexpr int kMaxOrderedLength = HEINZEL_MAX_ORDER_EXTENT;
static_assert(kMaxOrderedLength >= 1 && kMaxOrderedLength <= 32767, "HEINZEL_MAX_ORDER_EXTENT must fit a std::int16_t wire index");

// One compare-exchange: min → wire a, max → wire b. Every CE the generator emits has a < b, so the
// network is ascending and the drop-high-wires pruning below is sound.
struct CE {
    std::int16_t a{};
    std::int16_t b{};

    constexpr bool operator==(const CE&) const = default;
};

constexpr int next_pow2(int n) {
    int p = 1;
    while (p < n) {
        p *= 2;
    }
    return p;
}

// Exact odd-even mergesort comparator count for a power-of-two n: n·t·(t−1)/4 + n − 1 (t = log2 n).
// Used to size the scratch exactly — the unit test pins the generator against it.
constexpr int oem_count(int n) {
    int t = 0;
    for (int p = 1; p < n; p *= 2) {
        ++t;
    }
    return n * t * (t - 1) / 4 + n - 1;
}

inline constexpr std::size_t kMaxNet = static_cast<std::size_t>(oem_count(next_pow2(kMaxOrderedLength)));

// Over-capacity working form (the NTTP-sizing pattern: build here, right-size in `net_for`).
struct NetScratch {
    std::array<CE, kMaxNet> ce{};
    std::size_t n{0};

    constexpr void push(int a, int b) {
        if (n >= kMaxNet) {
            fail("heinzel::order: comparator network exceeds capacity (bug: oem_count under-sizes the scratch)");
        }
        ce[n++] = CE{static_cast<std::int16_t>(a), static_cast<std::int16_t>(b)};
    }

    constexpr bool operator==(const NetScratch& o) const {
        if (n != o.n) {
            return false;
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (!(ce[i] == o.ce[i])) {
                return false;
            }
        }
        return true;
    }
};

// Full ascending Batcher odd-even mergesort for extent L.
//
// Generated for P = next_pow2(L), then every CE touching a wire ≥ L is DROPPED. That is exact, not an
// approximation: pad wires L…P−1 with +inf and induct — every CE has a < b, so a wire ≥ L is only ever
// the HIGH end (b) of a CE whose low end is either real (min(x,+inf) = x, max = +inf: both wires
// unchanged) or also +inf (a no-op). So the padding wires hold +inf forever and their comparators
// never move a value. What remains sorts wires 0…L−1 ascending.
constexpr NetScratch make_sort_net(int L) {
    if (L < 1 || L > kMaxOrderedLength) {
        fail("heinzel::order: ordered extent out of range (1 … HEINZEL_MAX_ORDER_EXTENT)");
    }
    NetScratch net{};
    const int P = next_pow2(L);
    for (int p = 1; p < P; p *= 2) {
        for (int k = p; k >= 1; k /= 2) {
            for (int j = k % p; j <= P - 1 - k; j += 2 * k) {
                const int hi = (k - 1 < P - j - k - 1) ? k - 1 : P - j - k - 1;
                for (int i = 0; i <= hi; ++i) {
                    if ((i + j) / (2 * p) == (i + j + k) / (2 * p) && i + j + k < L) {
                        net.push(i + j, i + j + k);
                    }
                }
            }
        }
    }
    return net;
}

// Prune to a SELECTION network: keep only the comparators that can influence output wires [r0, r1].
// All three OrderOps need a contiguous rank range (sort → [0,L−1]; topk → [L−k,L−1]; quantile →
// {lo, hi} with hi ≤ lo+1), so a range is the whole vocabulary — no rank-set machinery.
//
// One backward liveness pass: walking from the end, a CE whose outputs are both dead can be deleted
// (deleting it changes only those two wires, and nothing downstream reads them); a CE with a live
// output is kept and makes BOTH its input wires live (each output is a function of both inputs).
constexpr NetScratch prune_net(const NetScratch& full, int L, int r0, int r1) {
    if (r0 < 0 || r1 < r0 || r1 >= L) {
        fail("heinzel::order: rank range out of bounds for the ordered extent");
    }
    std::array<bool, static_cast<std::size_t>(kMaxOrderedLength)> live{};
    for (int r = r0; r <= r1; ++r) {
        live[static_cast<std::size_t>(r)] = true;
    }
    std::array<bool, kMaxNet> keep{};
    for (std::size_t i = full.n; i-- > 0;) {
        const auto a = static_cast<std::size_t>(full.ce[i].a);
        const auto b = static_cast<std::size_t>(full.ce[i].b);
        if (live[a] || live[b]) {
            live[a] = live[b] = true;
            keep[i] = true;
        }
    }
    NetScratch out{};
    for (std::size_t i = 0; i < full.n; ++i) {
        if (keep[i]) {
            out.ce[out.n++] = full.ce[i];
        }
    }
    return out;
}

// The right-sized network the loop tree's runner holds as a `static constexpr`.
template <std::size_t N>
struct Net {
    std::array<CE, N> ce{};

    static constexpr std::size_t size() { return N; }

    constexpr bool operator==(const Net&) const = default;
};

// Selection network for extent L keeping output ranks [R0, R1], right-sized (count deduced).
template <int L, int R0, int R1>
constexpr auto net_for() {
    constexpr NetScratch s = prune_net(make_sort_net(L), L, R0, R1);
    Net<s.n> out{};
    for (std::size_t i = 0; i < s.n; ++i) {
        out.ce[i] = s.ce[i];
    }
    return out;
}

// numpy's 'linear' quantile, resolved to ranks into the ASCENDING order:
//   p = (L−1)·q,  lo = floor(p),  hi = ceil(p),  w = p − lo,  out = (1−w)·asc[lo] + w·asc[hi].
// w == 0 ⇒ an exact rank (lo == hi, no arithmetic, bit-exact). Integer truncation IS floor because
// p ≥ 0, so no <cmath> — and the product is spelled (L−1)·q exactly as numpy spells it, so the
// double rounding matches bit-for-bit.
struct QRank {
    int lo{};
    int hi{};
    double w{};

    constexpr bool operator==(const QRank&) const = default;
};

constexpr QRank quantile_rank(int L, double q) {
    if (L < 1 || !(q >= 0.0 && q <= 1.0)) {
        fail("heinzel::order: quantile q must be in [0,1] over a positive extent");
    }
    const double p = static_cast<double>(L - 1) * q;
    int lo = static_cast<int>(p);
    if (lo > L - 1) {
        lo = L - 1;
    }
    const double w = p - static_cast<double>(lo);
    return QRank{lo, w > 0.0 ? lo + 1 : lo, w};
}

} // namespace heinzel::order
