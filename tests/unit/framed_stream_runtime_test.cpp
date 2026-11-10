// A FRAMED STREAM whose frame count is a symbol — MF.6 s3c-pre, representation A of the multi-rate /
// frame-stream model (Decision 44).
//
// The graph is built once with its source length a symbol: `W = (N-window)/hop+1` follows from `N`, so the
// one graph serves every `N`. This checks that it lands on the walk of its literal twin, element by element,
// at two distinct N (two block lengths W), and that a block length that is not a literal frame count is
// refused. The walk runs it, at block lengths fixed when the test is compiled: the loop tree does not run a
// framed stream yet, so no runner takes the length only when it runs. The too-short-feed refusal is
// `framed_stream_interpret_test.cpp`'s.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"
#include "interpret.hpp"

using namespace heinzel;

namespace {

// A scalar stream `x{N}` framed to `[window, W]`, its frames squared elementwise. Built by hand at a
// LITERAL N, so its frame count W is a literal.
constexpr Graph framed_squared(std::int32_t n, std::int32_t window, std::int32_t hop) {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{n}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 0; // the streamed axis of a rank-1 value
    k.view.window = window;
    k.view.hop = hop;
    const auto framed = add_node(g, op::Unfold, x, kNoValue, kNoValue, k);
    const auto sq = add_node(g, op::Mul, framed, framed);
    g.inputs.push_back(NamedValue{"x", x});
    g.outputs.push_back(NamedValue{"sq", sq});
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the framed-squared graph did not resolve");
    }
    return g;
}

constexpr Graph kSq8 = framed_squared(8, 2, 2);   // W = 4
constexpr Graph kSq12 = framed_squared(12, 2, 2); // W = 6

// The SAME graph with a SYMBOLIC source length — a declared runtime dimension `Dim{0}` on the streamed
// axis (milestone MD), and a declared frame-count symbol `Dim{1}` for the derived `W`. This is the
// shape run at a length known only at run time: `W = (N-window)/hop+1` follows from `N` (recorded by
// `record_derived_extent`). Built by hand at window 2 / hop 2 so it matches `kSq8`/`kSq12`
// frame-for-frame once `N` is chosen.
constexpr Graph framed_squared_symbolic() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{Dim{0}}, ValueCategory::Input); // symbolic N, rank-1
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 0; // the streamed axis of a rank-1 value
    k.view.window = 2;
    k.view.hop = 2;
    k.view.frames = 1; // the declared frame-count symbol Dim{1}, W = (N-2)/2 + 1
    const auto framed = add_node(g, op::Unfold, x, kNoValue, kNoValue, k);
    const auto sq = add_node(g, op::Mul, framed, framed);
    g.inputs.push_back(NamedValue{"x", x});
    g.outputs.push_back(NamedValue{"sq", sq});
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the symbolic framed-squared graph did not resolve");
    }
    return g;
}

constexpr Graph kSqSym = framed_squared_symbolic();
static_assert(!is_static(kSqSym), "the symbolic framed graph keeps its runtime length a symbol");
static_assert(is_symbol(extent_at(kSqSym.values[static_cast<std::size_t>(kSqSym.outputs[0].value)].shape,
                                  kSqSym.values[static_cast<std::size_t>(kSqSym.outputs[0].value)].shape.rank - 1)),
              "and its frame count W is a symbol, not a literal");

// x[n] = n + 1, fed wider than the block (N samples for W frames).
template <std::int32_t N>
constexpr std::array<double, static_cast<std::size_t>(N)> ramp() {
    std::array<double, static_cast<std::size_t>(N)> xs{};
    for (std::int32_t n = 0; n < N; ++n) {
        xs[static_cast<std::size_t>(n)] = static_cast<double>(n + 1);
    }
    return xs;
}

// The walk's frames of the squared value, element by element (window fastest, then frame).
template <std::int32_t N, std::size_t W>
constexpr std::array<double, 2 * W> squared_walked(const Graph& g) {
    const std::array<double, static_cast<std::size_t>(N)> xs = ramp<N>();
    Inputs feeds{};
    feeds.push_back(InputFeed{xs.data(), 1, N});
    Signals<W> out{};
    run(g, feeds, out);
    std::array<double, 2 * W> got{};
    for (std::size_t f = 0; f < W; ++f) {
        for (std::int32_t k = 0; k < 2; ++k) {
            got[f * 2 + static_cast<std::size_t>(k)] = sample_at(out, g.outputs[0].value, k, f);
        }
    }
    return got;
}

// A block length that is not the framed value's literal frame count is refused before the run.
bool a_block_that_is_not_the_frame_count_is_refused() {
    try {
        (void)squared_walked<8, 5>(kSq8); // 5 != W (4)
        return false;
    } catch (const error& e) {
        return e.message() == std::string_view{"heinzel::run: the block length must be the framed value's frame count"};
    }
}

} // namespace

int main() {
    // The SYMBOLIC-W graph at two lengths, each matching the literal-N walk frame for frame — W is not
    // known until the length is.
    if (!(squared_walked<8, 4>(kSqSym) == squared_walked<8, 4>(kSq8))) {
        return 1;
    }
    if (!(squared_walked<12, 6>(kSqSym) == squared_walked<12, 6>(kSq12))) {
        return 2;
    }
    // The frames really are the squared gather, not zero — a run that never materialised them would
    // agree at all-zero and prove nothing.
    const std::array<double, 8> first = squared_walked<8, 4>(kSq8);
    if (!(first[0] == 1.0 && first[1] == 4.0)) {
        return 3;
    }
    if (!a_block_that_is_not_the_frame_count_is_refused()) {
        return 4;
    }
    return 0;
}
