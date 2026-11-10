// Graphs over a stream whose length is a symbol, on the loop-nest path, against the reference walk of the same
// graph at a literal length — bit for bit, since picking a rank or re-cutting an axis computes nothing.
// A graph is compiled once for every signal length: what a stream of run-time length needs.
//
// An order statistic's network is sized from the ORDERED axis, an element axis, so the analysis asks the
// element extents only (`sema/order.hpp`). A rearrange re-cuts element axes and passes
// the stream through whole, symbol and all.
//
//   median     the middle of 5, per sample                          {5, T} -> {T}
//   ends       the quantiles 0 and 1 of 5: the least and the most  one exact rank each
//   filter     median of 3 sliding over 16                          {16, T} -> {14, 3, T} -> {14, T}
//   split      rearrange "(c f) s -> c f s", f = 4, then * 2        {12, T} -> {3, 4, T}
//   merge      rearrange "c f s -> (c f) s", then + 1               {3, 4, T} -> {12, T}
//   the same of a computed value, which the analysis regroups once it has its shape (Plan/28-dsl-proposals/03 B3)

#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/notation/rearrange.hpp"
#include "heinzel/sema/analyze.hpp"

#include "canaried.hpp"
#include "interpret.hpp"
#include "planned.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace heinzel;

namespace {

namespace loops = heinzel::loops;

constexpr std::int32_t kSamples = 37;

enum class Case { Median, Least, Most, Filter, Split, Merge, SplitOfASum, MergeOfAProduct };

// `literal`: the stream `kSamples` long, as the walk needs it; otherwise a symbol bound when it runs.
constexpr Graph graph_of(Case which, bool literal) {
    const auto stream = [&](auto... e) { return literal ? Signal{e..., kSamples} : Signal{e..., Length{}}; };
    const bool splits = which == Case::Split || which == Case::SplitOfASum;
    const bool merges = which == Case::Merge || which == Case::MergeOfAProduct;
    Input x{"x", which == Case::Filter ? stream(16) : splits ? stream(12) : merges ? stream(3, 4) : stream(5)};
    Expr y = which == Case::Median            ? x.median(0) + 0.0
             : which == Case::Least           ? x.quantile(0, 0.0) + 0.0
             : which == Case::Most            ? x.quantile(0, 1.0) + 0.0
             : which == Case::Split           ? rearrange("(c f) -> c f", x, {{"f", 4}}) * 2.0
             : which == Case::Merge           ? rearrange("c f -> (c f)", x) + 1.0
             : which == Case::SplitOfASum     ? rearrange("(c f) -> c f", x + 1.0, {{"f", 4}}) * 2.0
             : which == Case::MergeOfAProduct ? rearrange("c f -> (c f)", x * 2.0) + 1.0
                                              : x.unfold(0, 3).median(1) + 0.0;
    Graph g = finish(Output{"y"} = y);
    if (!infer(g).ok) {
        fail("nest_symbolic_stream_test: a graph did not resolve");
    }
    return g;
}

template <Case Which>
constexpr planned::Held kNestHeld{planned::program_of(graph_of(Which, false), zen2)};
template <Case Which>
constexpr const loops::Program& kNest = kNestHeld<Which>.program;

int g_wrong = 0;

template <Case Which>
void check(const char* what) {
    static constexpr Graph literal = graph_of(Which, true);
    const std::size_t width = Which == Case::Filter ? 16 : Which >= Case::Split ? 12 : 5;
    std::vector<float> x(width * kSamples);
    std::uint32_t state = 7;
    for (float& v : x) {
        state = state * 1664525u + 1013904223u;
        v = static_cast<float>(static_cast<std::int32_t>(state >> 8) % 2001 - 1000) * 0.125f;
    }
    const std::vector<double> widened(x.begin(), x.end());
    Inputs feeds{};
    feeds.push_back(InputFeed{widened.data(), static_cast<std::int32_t>(width)});
    const auto out = std::make_unique<Signals<kSamples>>();
    run(literal, feeds, *out);

    loops::Sizes sizes{};
    sizes[0] = kSamples;
    canaried::Workspace<kNest<Which>> work(sizes);
    loops::Feeds nest_feeds{};
    nest_feeds[0] = x.data();
    canaried::run<kNest<Which>>(nest_feeds, work);
    const float* y = work.template data<float>(static_cast<std::size_t>(kNest<Which>.outputs[0]));
    const std::int32_t result = literal.outputs[0].value;
    const std::size_t outputs = static_cast<std::size_t>(elements_of(literal.values[static_cast<std::size_t>(result)].shape).count);
    for (std::size_t t = 0; t < kSamples; ++t) {
        for (std::size_t e = 0; e < outputs; ++e) {
            const float want = static_cast<float>(sample_at(*out, result, static_cast<std::int32_t>(e), t));
            if (!(y[t * outputs + e] == want) && ++g_wrong <= 10) {
                std::printf("  %s [sample %zu, element %zu]: got %a, want %a\n", what, t, e, static_cast<double>(y[t * outputs + e]),
                            static_cast<double>(want));
            }
        }
    }
}

} // namespace

int main() {
    check<Case::Median>("median of 5");
    check<Case::Least>("quantile 0 of 5");
    check<Case::Most>("quantile 1 of 5");
    check<Case::Filter>("median of 3 sliding over 16");
    check<Case::Split>("a split");
    check<Case::Merge>("a merge");
    check<Case::SplitOfASum>("a split of a sum");
    check<Case::MergeOfAProduct>("a merge of a product");
    if (g_wrong != 0) {
        std::printf("nest_symbolic_stream_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("nest_symbolic_stream_test: order statistics and rearranges over a symbolic stream agree with the walk, bit for bit\n");
    return 0;
}
