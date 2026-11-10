// The compare-exchange network generator behind `_order` (milestone M5, step S5.1).
//
// A neutral primitive with no IR, so this test includes only the header. Almost everything is a
// static_assert: the network is generated while the compiler evaluates, and the whole point of a
// SCHEDULE-as-data object is that it is fixed at compile time, so a test that only ran the program
// would not test the thing that matters. It pins the generator's comparator count against the closed
// form `oem_count` (the old tree's own pin), then proves the network SORTS by applying it, proves the
// pruned selection network answers the ranks it kept, and pins `quantile_rank` to numpy 'linear'.
#include "heinzel/support/order_net.hpp"

#include <array>
#include <cstddef>

using namespace heinzel::order;

namespace {

// Apply a comparator network to an array: each CE puts the min on wire a and the max on wire b, so a
// swap when `arr[a] > arr[b]` realizes it. This is the arithmetic the executor will run per lane; here
// it is the checker that turns "a list of comparators" into "a sort" so the generator can be tested.
template <std::size_t N>
constexpr std::array<double, N> apply(const NetScratch& net, std::array<double, N> arr) {
    for (std::size_t i = 0; i < net.n; ++i) {
        const auto a = static_cast<std::size_t>(net.ce[i].a);
        const auto b = static_cast<std::size_t>(net.ce[i].b);
        if (arr[a] > arr[b]) {
            const double t = arr[a];
            arr[a] = arr[b];
            arr[b] = t;
        }
    }
    return arr;
}

template <std::size_t N>
constexpr bool ascending_prefix(const std::array<double, N>& a, std::size_t upto) {
    for (std::size_t i = 1; i < upto; ++i) {
        if (a[i - 1] > a[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

// ---- the comparator count, pinned to the closed form -------------------------------------------
//
// For a power-of-two extent no wire is dropped, so `make_sort_net(P).n` is exactly `oem_count(P)`.
static_assert(make_sort_net(1).n == 0, "one wire is already sorted");
static_assert(make_sort_net(2).n == static_cast<std::size_t>(oem_count(2)), "two wires: one comparator");
static_assert(make_sort_net(4).n == static_cast<std::size_t>(oem_count(4)));
static_assert(make_sort_net(8).n == static_cast<std::size_t>(oem_count(8)));
static_assert(make_sort_net(16).n == static_cast<std::size_t>(oem_count(16)));
static_assert(oem_count(2) == 1 && oem_count(4) == 5 && oem_count(8) == 19,
              "the odd-even mergesort closed form, spot-checked against the hand count");

// A non-power-of-two extent generates for `next_pow2(L)` and drops every comparator touching a wire
// ≥ L, so its count is at most the padded network's and strictly below it whenever L is not a power
// of two (some comparator touched a dropped wire).
static_assert(make_sort_net(5).n < static_cast<std::size_t>(oem_count(next_pow2(5))),
              "a non-power-of-two extent drops the comparators that touch its padding wires");
static_assert(make_sort_net(8).n == static_cast<std::size_t>(oem_count(next_pow2(8))),
              "a power-of-two extent drops nothing");

// ---- the network SORTS -------------------------------------------------------------------------
//
// Applied to a reversed input and to an arbitrary one, the full network leaves every wire ascending —
// for a non-power-of-two extent (5) and a power-of-two one (8), so the wire-dropping is exercised.
static_assert(ascending_prefix(apply<5>(make_sort_net(5), {5.0, 4.0, 3.0, 2.0, 1.0}), 5),
              "the L=5 network sorts a reversed input ascending");
static_assert(apply<5>(make_sort_net(5), {5.0, 4.0, 3.0, 2.0, 1.0}) ==
                  std::array<double, 5>{1.0, 2.0, 3.0, 4.0, 5.0},
              "and the sorted values are exactly the inputs in order");
static_assert(apply<8>(make_sort_net(8), {3.0, 1.0, 4.0, 1.5, 5.0, 9.0, 2.0, 6.0}) ==
                  std::array<double, 8>{1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 9.0},
              "the L=8 network sorts an arbitrary input ascending");

// ---- pruning to a SELECTION network ------------------------------------------------------------
//
// A full-range prune keeps the whole network (every wire is live); a narrower range keeps fewer, and
// the value it leaves on a kept wire is the same as the full sort's — that is what "answers the ranks
// it kept" means, and the wires it did not keep are free to hold anything.
static_assert(prune_net(make_sort_net(8), 8, 0, 7) == make_sort_net(8),
              "keeping every rank prunes nothing");
static_assert(prune_net(make_sort_net(8), 8, 4, 4).n < make_sort_net(8).n,
              "keeping a single rank drops the comparators that cannot influence it");

// A median of five (the middle rank, r=2) from the pruned selection network equals the middle of the
// full sort — the selection network is the sort pruned to that one rank.
static_assert(apply<5>(prune_net(make_sort_net(5), 5, 2, 2), {5.0, 1.0, 4.0, 2.0, 3.0})[2] == 3.0,
              "the pruned median-of-5 network leaves the median on its rank");

// TopK: the two largest of eight are the top ranks [6,7] of the ascending sort, and the pruned
// network leaves them there.
static_assert([] {
    const auto sorted = apply<8>(prune_net(make_sort_net(8), 8, 6, 7), {3.0, 1.0, 4.0, 1.5, 5.0, 9.0, 2.0, 6.0});
    return sorted[6] == 6.0 && sorted[7] == 9.0;
}(), "the pruned top-2 network leaves the two largest on the top ranks");

// `net_for` is `prune_net` right-sized to a static array: a full range is `oem_count`; a single-rank
// selection is smaller.
static_assert(net_for<8, 0, 7>().size() == static_cast<std::size_t>(oem_count(8)),
              "a full-range net_for is the whole sort network");
static_assert(net_for<5, 2, 2>().size() < net_for<5, 0, 4>().size(),
              "a median selection net_for is smaller than the full sort");

// ---- quantile_rank pinned to numpy 'linear' ----------------------------------------------------
//
// q=0 is the minimum, q=1 the maximum, both exact ranks (w=0). Over an ODD extent the median q=0.5 is
// the exact middle rank; over an EVEN extent it is the lerp of the two middles at w=0.5 — the case a
// pure rank cannot express.
static_assert(quantile_rank(5, 0.0) == QRank{0, 0, 0.0}, "q=0 is the minimum, an exact rank");
static_assert(quantile_rank(5, 1.0) == QRank{4, 4, 0.0}, "q=1 is the maximum, an exact rank");
static_assert(quantile_rank(5, 0.5) == QRank{2, 2, 0.0}, "the median of an odd extent is an exact rank");
static_assert(quantile_rank(4, 0.5) == QRank{1, 2, 0.5},
              "the median of an even extent is the lerp of the two middles, w=0.5");
static_assert(quantile_rank(4, 0.25) == QRank{0, 1, 0.75},
              "p=(L-1)q=0.75 splits rank 0 and rank 1 at weight 0.75, numpy's linear rule");

int main() { return 0; }
