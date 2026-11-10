// `heinzel::host` (Plan 24): the machine HEINZEL_AUTOTUNE measured while this build directory was configured, built
// only with that option. A measured machine can cost speed, never a bit: at `High` every operation is computed in
// double and rounded once, in the order written, whatever the plan, so each program planned for `host` gives the
// walk's numbers bit for bit (`executed.hpp`), as it does for the default machine and zen2.
//
// The corpus: gated benchmark rows (`corpus_graphs.hpp`, copied unanalysed: `executed` analyses a graph at `High`)
// whose plans read the fields the probes measure — a relaid contraction (`relay_pays_after`, the register tile),
// attention's turned operand (`turn_pays_after`) and `exp`, a conv's square recomputed or stored (`recompute_always`),
// the channel strip's `tanh` and the oscillator's `sin` (`core`, the stream rows), a ladder's coupled cycles (the
// latencies, `window`), harris's windows and elem's lanes — at sizes the walk takes in a moment. The image rows' bands
// (`band_bytes`, `band_when_over`) need images of megabytes, too large to walk here; the band tests hold those.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "heinzel/dsp/oscillator.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/host_machine.hpp"
#include "heinzel/notation/einsum.hpp"

#include "executed.hpp"

using namespace heinzel;
using testing::executed;

namespace {

constexpr Index n{};
constexpr Length kSamples{.multiple_of = 1, .min = 1, .expect = 512};

// Numbers a float holds, spread over a few units, so that `tanh`, `exp` and `sin` leave their flat ends.
std::vector<double> numbers(std::size_t count, unsigned seed) {
    std::vector<double> v(count);
    for (std::size_t i = 0; i < count; ++i) {
        v[i] = 0.125 * static_cast<double>(static_cast<int>((i * 37 + seed * 11) % 61) - 30);
    }
    return v;
}

constexpr Graph matmul() { // 33 blocks of six rows, B of 64 KiB (over an L1)
    Input a{"a", {200, 128}};
    Input b{"b", {128, 128}};
    Output y{"y"};
    y = einsum("ik,kj->ij", a, b);
    return y;
}

constexpr Graph attention() { // K read 48 times
    Input q{"Q", {48, 64}};
    Input k{"K", {48, 64}};
    Input v{"V", {48, 64}};
    Expr scores = einsum("qd,kd->qk", q, k) * (1.0 / 8.0);
    Expr e = exp(scores - scores.max(1, keepdims));
    Output out{"out"};
    out = einsum("qk,kd->qd", e / e.sum(1, keepdims), v);
    return out;
}

template <int K>
constexpr Graph conv() { // K - 1 extra operations an output
    Input x{"x", {4096}};
    Input h{"h", {1, K}};
    Output y{"y"};
    y = ((x * x).unfold(0, K) * h).sum(1);
    return y;
}

constexpr Graph strip() {
    const auto saturate = [](Expr s, double d, double b) { return tanh(constant(d) * s + b); };
    Expr s = constant(2.0) * Input{"x", Signal{1 << 14}};
    s = saturate(s, 1.5, 0.10);
    s = saturate(s, 1.3, -0.05);
    s = saturate(s, 1.1, 0.0);
    Output y{"y"};
    y = clamp(constant(0.7) * s, -1.0, 1.0);
    return y;
}

constexpr Graph ladder_bank() { // sixteen voices of a five-stage ladder, one coupled cycle
    constexpr float a1 = 0.87730577f;
    constexpr float g = 1.0f - a1;
    constexpr float b0 = g * 0.76923076923f;
    constexpr float b1 = g * 0.23076923076f;
    constexpr float drive = 1.2f;
    constexpr float gain = 0.76730449f;
    constexpr float drive2 = 1.008f;
    constexpr float gain2 = 0.98788632f;
    constexpr float comp = 0.5f;
    constexpr float k = -2.92f;
    const auto f = [](float v) { return static_cast<double>(v); };
    Input x{"x", Signal{16, kSamples}};
    Variable s0;
    Variable s1;
    Variable s2;
    Variable s3;
    Variable s4;
    Expr dx = f(gain) * tanh(f(drive) * x);
    s0[n] = dx + f(k) * (f(gain2) * tanh(f(drive2) * s4[n - 1]) - dx * f(comp));
    s1[n] = f(b1) * s0[n - 1] + f(a1) * s1[n - 1] + f(b0) * s0[n];
    s2[n] = f(b1) * s1[n - 1] + f(a1) * s2[n - 1] + f(b0) * s1[n];
    s3[n] = f(b1) * s2[n - 1] + f(a1) * s3[n - 1] + f(b0) * s2[n];
    s4[n] = f(b1) * s3[n - 1] + f(a1) * s4[n - 1] + f(b0) * s3[n];
    Output y{"y"};
    y = f(1.2f) * s2;
    return y;
}

constexpr Graph fm_oscillator() {
    Input inc{"inc", Signal{kSamples}};
    Output y{"y"};
    y = dsp::oscillator(inc, constant(static_cast<double>(0.5f)));
    return y;
}

constexpr Graph harris() {
    Input x{"x", {40, 72}};
    Input wx{"wx", {1, 3, 1, 3}};
    Input wy{"wy", {1, 3, 1, 3}};
    Expr taps = x.unfold2d(0, 3, 1, 3);
    Expr dx = (taps * wx).sum({1, 3});
    Expr dy = (taps * wy).sum({1, 3});
    const auto box = [](Expr v) { return v.unfold2d(0, 3, 1, 3).sum({1, 3}); };
    Expr sxx = box(dx * dx);
    Expr sxy = box(dx * dy);
    Expr syy = box(dy * dy);
    Expr tr = sxx + syy;
    Output y{"y"};
    y = sxx * syy - sxy * sxy - static_cast<double>(0.04f) * tr * tr;
    return y;
}

constexpr Graph elem() {
    Input a{"a", Signal{kSamples}};
    Input b{"b", Signal{kSamples}};
    Output y{"y"};
    y = a * b + b;
    return y;
}

} // namespace

int main() {
    const std::vector<double> a = numbers(200 * 128, 1);
    const std::vector<double> b = numbers(128 * 128, 2);
    executed<matmul, 0, host>({{"a", a.data(), 200 * 128}, {"b", b.data(), 128 * 128}});
    const std::vector<double> q = numbers(48 * 64, 3);
    const std::vector<double> k = numbers(48 * 64, 4);
    const std::vector<double> v = numbers(48 * 64, 5);
    executed<attention, 0, host>({{"Q", q.data(), 48 * 64}, {"K", k.data(), 48 * 64}, {"V", v.data(), 48 * 64}});
    const std::vector<double> x = numbers(4096, 6);
    const std::vector<double> h5 = numbers(5, 7);
    const std::vector<double> h17 = numbers(17, 8);
    executed<conv<5>, 0, host>({{"x", x.data(), 4096}, {"h", h5.data(), 5}});
    executed<conv<17>, 0, host>({{"x", x.data(), 4096}, {"h", h17.data(), 17}});
    const std::vector<double> s = numbers(1 << 14, 9);
    executed<strip, 0, host>({{"x", s.data()}});
    const std::vector<double> voices = numbers(16 * 512, 10);
    executed<ladder_bank, 512, host>({{"x", voices.data(), 16}});
    const std::vector<double> inc = numbers(1000, 11);
    executed<fm_oscillator, 1000, host>({{"inc", inc.data()}});
    const std::vector<double> image = numbers(40 * 72, 12);
    const std::vector<double> wx = numbers(9, 13);
    const std::vector<double> wy = numbers(9, 14);
    executed<harris, 0, host>({{"x", image.data(), 40 * 72}, {"wx", wx.data(), 9}, {"wy", wy.data(), 9}});
    const std::vector<double> ea = numbers(4099, 15);
    const std::vector<double> eb = numbers(4099, 16);
    executed<elem, 4099, host>({{"a", ea.data()}, {"b", eb.data()}});
    std::printf("host_machine: nine graphs planned for heinzel::host give the walk's bits at High\n");
    return 0;
}
