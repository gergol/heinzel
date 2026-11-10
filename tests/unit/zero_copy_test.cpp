// Outputs written straight into the caller's buffer (`Plan/18-zero-copy/02`): each graph on `Executor` against the
// copy path, bit for bit — the same program by `loops::run`, each output then copied out of the workspace, which is
// what `Executor` did before L12. The caller's buffers are exactly sized, NaN before every run and behind a NaN guard,
// so a point read before it is written, or written outside its output, shows; a `Length` graph runs in blocks of its
// own buffers, short ones included. Then the bindings that must be routed through the workspace and copied, so that
// they mean what they meant: an output in place over its input, one a float above it, two outputs over each other.
// And which outputs may be written in place at all (`loops::kInPlaceOf`).

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include "heinzel/compile.hpp"
#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/loops/run.hpp"
#include "heinzel/loops/workspace.hpp"
#include "heinzel/notation/einsum.hpp"

#include "corpus_graphs.hpp"
#include "well_formed.hpp"

namespace {

using namespace heinzel;
using corpus::n;

template <auto Build>
inline constexpr CompileResult kCompiled = try_compile(Build());
template <auto Build>
inline constexpr const Graph& kGraph = kCompiled<Build>.compiled.graph;
template <auto Build>
inline constexpr const loops::Program& kProgram = kCompiled<Build>.compiled.program;

int g_wrong = 0;

void expect(bool ok, const char* name, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("%s: %s\n", name, what);
    }
}

constexpr std::size_t kGuard = 16; // NaN elements in front of each output

// ---- the graphs ----------------------------------------------------------------------------------------

constexpr Graph elem_f64() {
    Input a{"a", Signal{Length{}}, f64};
    Input b{"b", Signal{Length{}}, f64};
    Output y{"y"};
    y = a * b + b;
    return y;
}
constexpr Graph tanh_voices() {
    Input x{"x", Signal{3, 64}};
    Output y{"y"};
    y = tanh(x * 1.5);
    return y;
}
constexpr Graph median() { return corpus::sliding_median(37, 5); }
constexpr Graph tall() { return corpus::tall(true); }
// An output a contraction reads after it is made, and one a later stage reads three samples back.
constexpr Graph read_later() {
    Input a{"a", {12, 8}};
    Input b{"b", {8, 10}};
    Output p{"p"};
    Output q{"q"};
    p = a * 0.5;
    q = einsum("ik,kj->ij", p, b);
    return finish(p, q);
}
constexpr Graph lag_of_an_output() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    Output z{"z"};
    y = x * 0.5;
    z[n] = y[n - 3] + x[n];
    return finish(y, z);
}
// A signal, a timeless value, a rank-0 one and an F64 signal, and two outputs of one value.
constexpr Graph several() {
    Input x{"x", Signal{4, 32}};
    Input w{"w", {4, 1}};
    Input d{"d", Signal{32}, f64};
    Output y{"y"};
    Output timeless{"timeless"};
    Output scalar{"scalar"};
    Output wide{"wide"};
    Output again{"again"};
    y = x * w;
    timeless = w * 2.0;
    scalar = w.sum(0).sum(0);
    wide = d * 3.0;
    again = x * w + 1.0;
    return finish(y, timeless, scalar, wide, again);
}
constexpr Graph two_of_one() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    Output z{"z"};
    Expr half = x * 0.5;
    y = half;
    z = half;
    return finish(y, z);
}
constexpr Graph ramp_out() {
    Output r{"r"};
    r = iota({16});
    return r;
}
// (Eight back: float's lanes run as many samples at once as double's twice, so its state stays float and the output
// is read in its own past; three back fills double's two lanes and not float's four, and runs in double.)
constexpr Graph comb() {
    Input x{"x", Signal{64}};
    Output y{"y"};
    y[n] = x[n] + 0.5 * y[n - 8];
    return y;
}

// ---- the run, and the copy path --------------------------------------------------------------------------

// A port's numbers, at its element type, `frames` samples of a `Length` graph's: deterministic, different per block.
struct Port {
    ElemType type{ElemType::F32};
    std::size_t count{0};
    std::vector<std::byte> bytes;
};
std::size_t elements(const Shape& s, std::int64_t frames) {
    std::size_t count = 1;
    for (std::int32_t k = 0; k < s.rank; ++k) {
        const Extent e = extent_at(s, k);
        count *= static_cast<std::size_t>(is_symbol(e) ? frames : literal_of(e));
    }
    return count;
}
std::size_t held(ElemType t) { return t == ElemType::F64 ? 8 : 4; }
Port numbers(const Graph& g, std::int32_t value, std::int64_t frames, std::size_t seed) {
    const Value& v = g.values[static_cast<std::size_t>(value)];
    Port p{v.type, elements(v.shape, frames), {}};
    p.bytes.resize(p.count * held(p.type));
    for (std::size_t i = 0; i < p.count; ++i) {
        const double x = 0.25 * static_cast<double>((i * 7 + seed * 13) % 29) - 3.0;
        const float f = static_cast<float>(x);
        p.type == ElemType::F64 ? void(std::memcpy(p.bytes.data() + i * 8, &x, 8)) : void(std::memcpy(p.bytes.data() + i * 4, &f, 4));
    }
    return p;
}
// An output's buffer: `kGuard` elements, then the output, all NaN.
std::vector<std::byte> guarded(const Port& out) { return std::vector<std::byte>((kGuard + out.count) * held(out.type), std::byte{0xff}); }
bool guard_holds(const std::vector<std::byte>& buffer, const Port& out) {
    for (std::size_t i = 0; i < kGuard * held(out.type); ++i) {
        if (buffer[i] != std::byte{0xff}) {
            return false;
        }
    }
    return true;
}

template <class E, const Graph& G, std::size_t I>
void bind_input(E& ex, const Port& in) {
    constexpr const NamedValue& port = G.inputs[I];
    if constexpr (G.values[static_cast<std::size_t>(port.value)].shape.rank == 0) {
        double value = 0.0;
        in.type == ElemType::F64 ? void(std::memcpy(&value, in.bytes.data(), 8)) : void(value = *reinterpret_cast<const float*>(in.bytes.data()));
        ex.param(port.name.data_) = value;
    } else if constexpr (G.values[static_cast<std::size_t>(port.value)].type == ElemType::F64) {
        ex.input(port.name.data_) = reinterpret_cast<const double*>(in.bytes.data());
    } else {
        ex.input(port.name.data_) = reinterpret_cast<const float*>(in.bytes.data());
    }
}
template <class E, const Graph& G, std::size_t O>
void bind_output(E& ex, std::byte* at) {
    constexpr const NamedValue& port = G.outputs[O];
    if constexpr (G.values[static_cast<std::size_t>(port.value)].type == ElemType::F64) {
        ex.output(port.name.data_) = reinterpret_cast<double*>(at);
    } else {
        ex.output(port.name.data_) = reinterpret_cast<float*>(at);
    }
}

// Runs `Build` on `Executor` over `frames` (one run each; a static graph's frames are its runs) and on the copy path,
// and compares every output of every run.
template <auto Build>
void against_the_copy_path(const char* name, std::span<const std::int64_t> frames) {
    using E = Executor<Build>;
    static constexpr const Graph& G = kGraph<Build>;
    static constexpr const loops::Program& P = kProgram<Build>;
    static_assert(loops::accepted<P>());
    constexpr std::size_t ins = G.inputs.size();
    constexpr std::size_t outs = G.outputs.size();
    std::int64_t most = 1;
    for (const std::int64_t f : frames) {
        most = f > most ? f : most;
    }
    E ex;
    loops::Workspace<P> work = [&] {
        if constexpr (E::has_length) {
            ex.prepare(most);
            return loops::Workspace<P>(most);
        } else {
            return loops::Workspace<P>();
        }
    }();
    for (std::size_t r = 0; r < frames.size(); ++r) {
        std::vector<Port> in;
        std::vector<Port> out;
        std::vector<std::vector<std::byte>> mine;
        loops::Feeds feeds{};
        for (std::size_t i = 0; i < ins; ++i) {
            in.push_back(numbers(G, G.inputs[i].value, frames[r], r * ins + i));
            feeds[i] = in.back().bytes.data();
        }
        for (std::size_t o = 0; o < outs; ++o) {
            out.push_back(numbers(G, G.outputs[o].value, frames[r], 0));
            mine.push_back(guarded(out.back()));
        }
        [&]<std::size_t... I, std::size_t... O>(std::index_sequence<I...>, std::index_sequence<O...>) {
            (bind_input<E, G, I>(ex, in[I]), ...);
            (bind_output<E, G, O>(ex, mine[O].data() + kGuard * held(out[O].type)), ...);
        }(std::make_index_sequence<ins>{}, std::make_index_sequence<outs>{});
        if constexpr (E::has_length) {
            ex.run(frames[r]);
            work.set_length(frames[r]);
        } else {
            ex.run();
        }
        loops::run<P>(feeds, work);
        for (std::size_t o = 0; o < outs; ++o) {
            const std::size_t bytes = out[o].count * held(out[o].type);
            const std::byte* copied = work.bytes_of(static_cast<std::size_t>(P.outputs[o]));
            expect(std::memcmp(mine[o].data() + kGuard * held(out[o].type), copied, bytes) == 0, name, "an output differs from the copy path's");
            expect(guard_holds(mine[o], out[o]), name, "a run wrote in front of an output");
        }
    }
}

// ---- bindings that are routed -----------------------------------------------------------------------------

// A FIR in place over its input, in blocks: the run reads x[n - 3] after y[n] would have overwritten it.
bool fir_in_place() {
    using E = Executor<corpus::fir_over_a_feed>;
    const std::int64_t blocks[] = {16, 5, 16, 1, 7};
    E apart;
    E in_place;
    apart.prepare(16), in_place.prepare(16);
    bool same = true;
    for (std::size_t r = 0; r < std::size(blocks); ++r) {
        std::vector<float> x(static_cast<std::size_t>(blocks[r]));
        std::vector<float> y(x.size(), std::numeric_limits<float>::quiet_NaN());
        for (std::size_t i = 0; i < x.size(); ++i) {
            x[i] = 0.25f * static_cast<float>((i * 5 + r * 3) % 11) - 1.0f;
        }
        std::vector<float> buf = x;
        apart.input("x") = x.data(), apart.output("y") = y.data();
        in_place.input("x") = buf.data(), in_place.output("y") = buf.data();
        apart.run(blocks[r]), in_place.run(blocks[r]);
        same = same && std::memcmp(buf.data(), y.data(), y.size() * sizeof(float)) == 0;
    }
    return same;
}

// y one float above a, so that writing y[i] overwrites a[i + 1] before it is read; and back apart, written in place.
bool one_float_above() {
    using E = Executor<corpus::elem>;
    constexpr std::size_t count = 37;
    std::vector<float> a(count);
    std::vector<float> b(count);
    std::vector<float> y(count);
    std::vector<float> shifted(count + 1);
    for (std::size_t i = 0; i < count; ++i) {
        a[i] = 0.5f * static_cast<float>(i % 7) - 1.0f, b[i] = 0.25f * static_cast<float>(i % 5) + 0.5f;
    }
    std::copy(a.begin(), a.end(), shifted.begin());
    E apart;
    E over;
    apart.prepare(count), over.prepare(count);
    apart.input("a") = a.data(), apart.input("b") = b.data(), apart.output("y") = y.data();
    over.output("y") = shifted.data() + 1; // (bound before its input: routed when the input comes)
    over.input("a") = shifted.data(), over.input("b") = b.data();
    apart.run(count), over.run(count);
    bool same = shifted[0] == a[0] && std::memcmp(shifted.data() + 1, y.data(), count * sizeof(float)) == 0;
    std::vector<float> fresh(count);
    over.input("a") = a.data(), over.output("y") = fresh.data(); // apart again
    over.run(count);
    return same && fresh == y;
}

// Two outputs over each other: the last copied wins, as it did before L12.
bool two_over_each_other() {
    using E = Executor<lag_of_an_output>;
    std::vector<float> x(64);
    std::vector<float> y(64);
    std::vector<float> z(64);
    std::vector<float> buf(96);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = 0.125f * static_cast<float>(i % 13) - 0.75f;
    }
    E apart;
    E over;
    apart.input("x") = x.data(), apart.output("y") = y.data(), apart.output("z") = z.data();
    over.input("x") = x.data(), over.output("y") = buf.data(), over.output("z") = buf.data() + 32;
    apart.run(), over.run();
    return std::memcmp(buf.data(), y.data(), 32 * sizeof(float)) == 0 && std::memcmp(buf.data() + 32, z.data(), 64 * sizeof(float)) == 0;
}

// ---- which outputs may be written in place -------------------------------------------------------------

static_assert(loops::kInPlaceOf<kProgram<corpus::elem>>.size() == 1, "a map's output is written in place");
static_assert(loops::kInPlaceOf<kProgram<corpus::matmul<16, 8, 12>>>.size() == 1, "a contraction's output is written in place");
static_assert(loops::kInPlaceOf<kProgram<comb>>.size() == 0, "an output read in its own past is copied (step B)");
static_assert(loops::kInPlaceOf<kProgram<two_of_one>>.size() == 1, "two outputs of one value: one buffer, written in place once");
static_assert(loops::kInPlaceOf<kProgram<several>>.size() == 5, "every output of `several` its own buffer, in place");
static_assert(loops::kInPlaceOf<kProgram<ramp_out>>.size() == 0, "a ramp output is a constant, copied from its one sample");

} // namespace

int main() {
    static constexpr std::int64_t kOnce[] = {1, 1, 1};
    static constexpr std::int64_t kBlocks[] = {16, 5, 16, 1, 7};
    against_the_copy_path<corpus::elem>("elem", kBlocks);
    against_the_copy_path<elem_f64>("elem_f64", kBlocks);
    against_the_copy_path<tanh_voices>("tanh_voices", kOnce);
    against_the_copy_path<corpus::rowsum<64>>("rowsum", kBlocks);
    against_the_copy_path<corpus::matmul<16, 8, 12>>("matmul", kOnce);
    against_the_copy_path<tall>("tall_bias_relu", kOnce);
    against_the_copy_path<corpus::rmsnorm>("rmsnorm", kOnce);
    against_the_copy_path<median>("median", kOnce);
    against_the_copy_path<corpus::fir_over_a_feed>("fir_over_a_feed", kBlocks);
    against_the_copy_path<corpus::padded>("padded", kOnce);
    against_the_copy_path<read_later>("read_later", kOnce);
    against_the_copy_path<lag_of_an_output>("lag_of_an_output", kOnce);
    against_the_copy_path<several>("several", kOnce);
    against_the_copy_path<two_of_one>("two_of_one", kOnce);
    against_the_copy_path<comb>("comb", kOnce);
    expect(fir_in_place(), "fir_over_a_feed", "in place over its input, it differs from apart");
    expect(one_float_above(), "elem", "one float above its input, or apart again, it differs");
    expect(two_over_each_other(), "lag_of_an_output", "two outputs over each other: not the last copy's");
    if (g_wrong != 0) {
        std::printf("zero_copy_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("zero_copy_test: outputs written in place equal the copy path's; overlapping bindings routed\n");
    return 0;
}
