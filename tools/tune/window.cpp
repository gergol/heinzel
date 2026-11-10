// Plan 25 step 0.5a: `Machine::window`, the out-of-order window in issue slots (`schedule/cost.hpp`: one sample's own
// path overlaps the next samples' only as far as the window holds their slots), measured where the cost model decides
// by it: the order of a cascade's feedback sums (`rewrite/feedback_sums.hpp`), whose best flips with the count of
// sections, at a count that is the core's (zen2 between two and three, the M1 between three and four).
//
// A chain of adds per sample (`Plan/probes/window/window.cpp`) does not measure it: its reading grows with the chain, and
// at no chain length is it zen2's 100 and within the M1's flip at once (zen2 93 at 32 adds, the M1 250 there, where its
// flip wants 135-235). So the flip itself: one voice of K = 2 to 4 biquad sections (five terms each, `cascade-order`'s
// `runner_orders.cpp`), each in both whole-graph orders (F, each section adding the one before last; B, each section's
// own newest state last), planned for the base machine and timed through the runner; B/F measured is the median over 8
// processes of two rounds each. The same two graphs priced (`loops::graph::feedback_cycles`) at every window from 25 to
// 1000 in steps of 5, the rest of the base machine as it is. Not five sections: past both cores' flips, where the model
// prices B/F far off the measured (zen2 1.70 against 1.13 at 100, the M1 1.59 against 1.23 at 200), so it would pull the
// fit away from the flip it is to place (with it, zen2 read 125 and the M1 225, 10 short of the M1's last right window).
//
// How it decides: of the windows at which the rule (B where priced `kEarlyMargin` cheaper) takes the faster order at
// every K (a B/F within 3 % of 1 is a tie, either order right), the one whose priced B/F is nearest the measured (least
// squares of their logarithms); `none` where no window takes the faster order at every K.
// 2026-10-11, B/F for K 2 3 4: zen2 (core 4) 0.75 1.10 1.26, right from 55 up, nearest at 95; the M1 Pro (unpinned,
// stage 1's `host`) 0.60 0.71 1.10, right at 135-235, nearest at 180.
//   clang++-22 -std=c++2c -O3 -march=native -DNDEBUG -fconstexpr-steps=268435456 -ffp-contract=off -fno-math-errno \
//     -Wno-psabi -I include -I tests/support -I tools/tune tools/tune/window.cpp -o window && taskset -c 4 ./window
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/rewrite/rewritten.hpp"
#include "heinzel/sema/analyze.hpp"

#include "planned.hpp"
#include "probe.hpp"

namespace hz = heinzel;
namespace loops = heinzel::loops;

namespace {

constexpr std::array<std::int32_t, 3> kSections{2, 3, 4};
constexpr std::int64_t kSamples = std::int64_t{1} << 16;
constexpr double kTie = 0.03;  // a B/F this near 1 decides nothing
constexpr int kRounds = 2;     // draws of the placement a program is timed on, in each process
constexpr int kProcesses = 8;  // address spaces it is measured in
constexpr std::array<std::array<float, 5>, 4> kC{{{0.3f, 0.2f, 0.1f, -0.4f, 0.1f}, {0.5f, 0.1f, 0.05f, 0.3f, -0.2f},
                                                 {0.4f, 0.2f, 0.1f, -0.1f, 0.15f}, {0.6f, 0.1f, 0.05f, 0.2f, -0.1f}}};

// K sections, the coefficients cycled from four, at Normal (whose reorder grant the rule needs).
template <std::int32_t K>
constexpr hz::Graph cascade() {
    using namespace heinzel;
    constexpr Index n{};
    std::array<Variable, K> y{};
    Expr s = Input{"x", Signal{Length{}}};
    for (std::int32_t k = 0; k < K; ++k) {
        const auto& c = kC[static_cast<std::size_t>(k % 4)];
        const auto f = [](float v) { return static_cast<double>(v); };
        y[k][n] = f(c[0]) * s + f(c[1]) * s[n - 1] + f(c[2]) * s[n - 2] - f(c[3]) * y[k][n - 1] - f(c[4]) * y[k][n - 2];
        s = y[k];
    }
    Graph g = finish(Output{"y"} = s);
    set_default_precision(g, numerics::Precision::Normal);
    if (!infer(g).ok) {
        fail("window: the cascade did not resolve");
    }
    return g;
}

// The cascade's sums in one whole-graph order (`early`: B), as the rule builds its two candidates.
template <std::int32_t K, bool Early>
constexpr hz::Graph sums() {
    bool upstream = false;
    return hz::rewrite::detail::in_order(cascade<K>(), tune::kBase.fma, Early, upstream);
}

template <std::int32_t K, bool Early>
struct Arm {
    static constexpr planned::Held kHeld{planned::program_of(hz::rewrite::double_state(sums<K, Early>(), tune::kBase), tune::kBase)};
};

template <std::size_t... I>
std::vector<tune::TimeFn> arms_of(std::index_sequence<I...>) {
    return {&tune::Timed<Arm<kSections[I], false>::kHeld.program>::us..., &tune::Timed<Arm<kSections[I], true>::kHeld.program>::us...};
}

// One process's measurement: B/F at each K.
std::vector<double> pass() {
    loops::Sizes sizes{};
    sizes[static_cast<std::size_t>(hz::kStreamLength)] = kSamples;
    std::vector<std::vector<std::byte>> out;
    const auto t = tune::rounds(arms_of(std::make_index_sequence<kSections.size()>{}), sizes, out, kRounds);
    std::vector<double> v;
    for (std::size_t k = 0; k < kSections.size(); ++k) {
        v.push_back(tune::trimmed_mean(t[kSections.size() + k]) / tune::trimmed_mean(t[k]));
    }
    return v;
}

// The cascades' priced B/F at window `w`.
template <std::size_t... I>
std::array<double, kSections.size()> priced(std::int32_t w, std::index_sequence<I...>) {
    hz::Machine m = tune::kBase;
    m.window = w;
    return {(hz::loops::graph::feedback_cycles(sums<kSections[I], true>(), m) /
             hz::loops::graph::feedback_cycles(sums<kSections[I], false>(), m))...};
}

} // namespace

int main() {
    tune::heading("window: a cascade's two orders of its feedback sums, measured and priced");
    const auto passes = tune::in_processes(kProcesses, pass);
    std::array<double, kSections.size()> measured{};
    for (std::size_t k = 0; k < kSections.size(); ++k) {
        std::vector<double> r;
        for (const std::vector<double>& p : passes) {
            r.push_back(p[k]);
        }
        measured[k] = tune::median(r);
        std::printf("  %d sections: B/F %.3f (%.3f-%.3f over %d processes)\n", kSections[k], measured[k],
                    *std::min_element(r.begin(), r.end()), *std::max_element(r.begin(), r.end()), kProcesses);
    }
    std::int32_t pick = -1;
    std::int32_t lo = -1;
    std::int32_t hi = -1;
    double least = 1e300;
    for (std::int32_t w = 25; w <= 1000; w += 5) {
        const auto p = priced(w, std::make_index_sequence<kSections.size()>{});
        bool right = true;
        double error = 0;
        for (std::size_t k = 0; k < kSections.size(); ++k) {
            const bool takes_b = p[k] <= 1 - hz::rewrite::kEarlyMargin;
            right = right && (std::fabs(measured[k] - 1) <= kTie || takes_b == (measured[k] < 1));
            error += std::pow(std::log(p[k] / measured[k]), 2);
        }
        if (w % 50 == 0 && w <= 400) {
            std::printf("  window %4d: priced B/F", w);
            for (const double each : p) {
                std::printf(" %.2f", each);
            }
            std::printf("%s\n", right ? "" : "  (takes the slower order)");
        }
        if (right) {
            lo = lo < 0 ? w : lo;
            hi = w;
            if (error < least) {
                least = error;
                pick = w;
            }
        }
    }
    if (pick < 0) {
        std::printf("  no window takes the faster order at every count of sections\n");
        tune::none("window");
        return 0;
    }
    std::printf("  the faster order at every count from %d to %d; nearest the measured at %d\n", lo, hi, pick);
    tune::tuned("window", pick);
}
