// The walk, running a FRAMED STREAM — representation A of the multi-rate / frame-stream model
// (Decision 44, operator 2026-09-06). MF.3 sub-step 1: the framed-input READER.
//
// `unfold(x{N}, axis rank-1, window, hop)` frames the streamed axis: the window tap becomes a static
// element axis and the frame count `W = (N-window)/hop + 1` becomes the run's new block length. Under
// representation A a frame `f` reads its source at sample `f*hop + k` (window tap `k`), so the input is
// fed WIDER than the block — `N` input samples feed `W` output frames. The walk materialises the
// framed value directly from the source input's feed (`framed_stream.hpp`), because the source is read
// across `[0, N)` while the block loops `[0, W)`.
//
// WHY THESE NUMBERS. The input carries a distinct number per sample (`x[n] = n + 1`) so a read at the
// wrong sample, the wrong tap, or off the block lands on a plausible wrong number. The framed value is
// checked element by element against the gather `x[f*hop + k]` at TWO distinct `N` (two distinct block
// lengths `W`), and a downstream elementwise op is checked to prove the frames compose through the
// walk. The refusals — a framed source that is not an input, a source also consumed unframed, a feed
// too short — are each observed objecting, because each is the reserved second-block tripwire (or a
// read off the feed) and must be a named refusal rather than a wrong number.
#include <array>
#include <cstddef>
#include <string_view>

#include "heinzel/sema/analyze.hpp"

#include "hand_built_graph.hpp"
#include "interpret.hpp"

using namespace heinzel;

namespace {

// A scalar stream `x{N}` framed to `[window, W]`, its frames the output. `window`/`hop` fixed so two
// `N` give two `W`. Built by hand — the frontend spelling that mints such a node arrives with MF.4.
constexpr Graph framed_graph(std::int32_t n, std::int32_t window, std::int32_t hop) {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{n}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 0; // the streamed axis of a rank-1 value
    k.view.window = window;
    k.view.hop = hop;
    const auto framed = add_node(g, op::Unfold, x, kNoValue, kNoValue, k);
    g.inputs.push_back(NamedValue{"x", x});
    g.outputs.push_back(NamedValue{"framed", framed});
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the framed-stream graph did not resolve");
    }
    return g;
}

constexpr Graph kG8 = framed_graph(8, 2, 2);   // W = (8-2)/2+1 = 4
constexpr Graph kG12 = framed_graph(12, 2, 2); // W = (12-2)/2+1 = 6

// The analysis really did frame the stream: window becomes an element axis, W the new innermost.
static_assert(kG8.values[static_cast<std::size_t>(kG8.outputs[0].value)].shape == Shape{2, 4},
              "the N=8 framed value is not [window=2, W=4]");
static_assert(kG12.values[static_cast<std::size_t>(kG12.outputs[0].value)].shape == Shape{2, 6},
              "the N=12 framed value is not [window=2, W=6]");

// x[n] = n + 1, fed wider than the block (N samples for W frames). Frame f, tap k reads x[f*hop + k].
template <std::int32_t N, std::size_t W>
constexpr bool framed_is_the_gather(std::int32_t window, std::int32_t hop, const Graph& g) {
    std::array<double, static_cast<std::size_t>(N)> xs{};
    for (std::int32_t n = 0; n < N; ++n) {
        xs[static_cast<std::size_t>(n)] = static_cast<double>(n + 1);
    }
    Inputs feeds{};
    feeds.push_back(InputFeed{xs.data(), 1, N}); // width 1, N samples — wider than the block W
    Signals<W> out{};
    run(g, feeds, out);
    for (std::size_t f = 0; f < W; ++f) {
        for (std::int32_t k = 0; k < window; ++k) {
            const double want = static_cast<double>(static_cast<std::int32_t>(f) * hop + k + 1);
            if (!(sample_at(out, g.outputs[0].value, k, f) == want)) {
                return false;
            }
        }
    }
    return true;
}

static_assert(framed_is_the_gather<8, 4>(2, 2, kG8),
              "the N=8 walk did not gather x[f*hop+k] into every frame's window");
static_assert(framed_is_the_gather<12, 6>(2, 2, kG12),
              "the N=12 walk did not gather x[f*hop+k] into every frame's window");

// The frames compose through the walk: a downstream elementwise square of the framed value is the
// square of each gathered number. A run that left the framed value unmaterialised (or skipped its node
// without filling it) would square zero here.
constexpr Graph framed_squared_graph(std::int32_t n, std::int32_t window, std::int32_t hop) {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{n}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 0;
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
constexpr Graph kSq = framed_squared_graph(8, 2, 2);

constexpr bool framed_square_composes() {
    std::array<double, 8> xs{};
    for (std::int32_t n = 0; n < 8; ++n) {
        xs[static_cast<std::size_t>(n)] = static_cast<double>(n + 1);
    }
    Inputs feeds{};
    feeds.push_back(InputFeed{xs.data(), 1, 8});
    Signals<4> out{};
    run(kSq, feeds, out);
    for (std::size_t f = 0; f < 4; ++f) {
        for (std::int32_t k = 0; k < 2; ++k) {
            const double base = static_cast<double>(static_cast<std::int32_t>(f) * 2 + k + 1);
            if (!(sample_at(out, kSq.outputs[0].value, k, f) == base * base)) {
                return false;
            }
        }
    }
    return true;
}
static_assert(framed_square_composes(),
              "the framed value did not compose through a downstream elementwise op");

// ---- the refusals, each observed objecting ------------------------------------------------------
//
// In `main` rather than `static_assert`: under constant evaluation a `fail()` IS the compile error,
// so a refusal can only be watched from a run.

bool refused(std::string_view expected, void (*attempt)()) {
    try {
        attempt();
        return false;
    } catch (const error& e) {
        return e.message() == expected;
    }
}

// A framed unfold whose source is a COMPUTED value, not a graph input: its N samples are not in any
// feed, so it would need the source to stream over N while the block streams over W — the reserved
// second-block tripwire. The analysis accepts the shape; the run refuses by name.
constexpr Graph framed_of_computed() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{8}, ValueCategory::Input);
    const auto y = add_node(g, op::Neg, x); // a computed value, still streamed [8]
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 0;
    k.view.window = 2;
    k.view.hop = 2;
    const auto framed = add_node(g, op::Unfold, y, kNoValue, kNoValue, k);
    g.inputs.push_back(NamedValue{"x", x});
    g.outputs.push_back(NamedValue{"framed", framed});
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the framed-of-computed graph did not resolve");
    }
    return g;
}
constexpr Graph kComputed = framed_of_computed();

bool a_framed_source_that_is_not_an_input_is_refused() {
    return refused("heinzel::run: a framed stream's source is not a graph input (a second block "
                   "length; reserved)",
                   [] {
        std::array<double, 8> xs{};
        Inputs feeds{};
        feeds.push_back(InputFeed{xs.data(), 1, 8});
        Signals<4> out{};
        run(kComputed, feeds, out);
    });
}

// A framed input that ALSO feeds the graph unframed would have to stream over N (the unframed read)
// and over W (the frames) at once — the same second block. Refused by name.
constexpr Graph framed_and_unframed() {
    Graph g{};
    const auto x = add_value(g, ElemType::F64, Shape{8}, ValueCategory::Input);
    OpAttr k{};
    k.kind = AttrKind::View;
    k.view.kind = ViewKind::Unfold;
    k.view.axis = 0;
    k.view.window = 2;
    k.view.hop = 2;
    const auto framed = add_node(g, op::Unfold, x, kNoValue, kNoValue, k);
    const auto also = add_node(g, op::Neg, x); // x read unframed as well
    g.inputs.push_back(NamedValue{"x", x});
    g.outputs.push_back(NamedValue{"framed", framed});
    g.outputs.push_back(NamedValue{"also", also});
    const Diagnostic d = infer(g);
    if (!d.ok) {
        fail("the framed-and-unframed graph did not resolve");
    }
    return g;
}
constexpr Graph kBoth = framed_and_unframed();

bool a_framed_input_also_read_unframed_is_refused() {
    return refused("heinzel::run: a framed input is also read unframed (a second block "
                   "length; reserved)",
                   [] {
        std::array<double, 8> xs{};
        Inputs feeds{};
        feeds.push_back(InputFeed{xs.data(), 1, 8});
        Signals<4> out{};
        run(kBoth, feeds, out);
    });
}

// A feed that does not declare enough samples for the frames to read (here `samples` left at the
// block-length default 0) would read off its end. Refused by name rather than read.
bool a_feed_too_short_for_its_frames_is_refused() {
    return refused("heinzel::run: a framed input's feed holds fewer samples than its frames read", [] {
        std::array<double, 8> xs{};
        Inputs feeds{};
        feeds.push_back(InputFeed{xs.data(), 1, 0}); // 0 = "exactly the block"; too short for the frames
        Signals<4> out{};
        run(kG8, feeds, out);
    });
}

} // namespace

int main() {
    if (!framed_is_the_gather<8, 4>(2, 2, kG8)) {
        return 1;
    }
    if (!framed_is_the_gather<12, 6>(2, 2, kG12)) {
        return 2;
    }
    if (!framed_square_composes()) {
        return 3;
    }
    if (!a_framed_source_that_is_not_an_input_is_refused()) {
        return 4;
    }
    if (!a_framed_input_also_read_unframed_is_refused()) {
        return 5;
    }
    if (!a_feed_too_short_for_its_frames_is_refused()) {
        return 6;
    }
    return 0;
}
