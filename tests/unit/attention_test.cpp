// Single-head scaled dot-product self-attention, built in THIS tree's language and run both ways the
// tree can run it — walked, and on the loop tree (`Executor`) — with the two answers required to agree.
//
// This is the `attention` example (old `python/examples/attention.py` @ c4012b8): two einsum
// contractions around a numerically-stable softmax. It needs NO new op and NO new spelling — the
// einsum contraction (MB), the keepdims max/sum reductions (M5) and the elementwise transcendentals
// (M4) already express it. What it adds is a case no prior corpus graph had: several SAME-WIDTH
// structural temporaries simultaneously live.
//
// WHY T == D. The old scheduled runner miscomputed a graph whose structural op wrote its output into a
// slot a same-width operand had just been handed back into; the hazard needs a same-width match, so it
// hid until an attention softmax at T == D put the score/attn temporaries [T,T] and the output [T,D] all
// at one width. That runner is gone; the case stays, as a graph whose temporaries are all one width.
// The loop tree runs it at `High` (where it computes as the walk does) and must give the walk's numbers
// bit for bit; the walk is the established-correct oracle (softmax_here, einsum_contractions_here are
// each parity-checked against the old tree). `main()` adds an independent double oracle within the old
// example's atol=1e-4, so the check does not rest on the walk alone.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "heinzel/executor.hpp"
#include "heinzel/frontend/graph_builder.hpp"
#include "heinzel/notation/einsum.hpp"
#include "heinzel/sema/analyze.hpp"

#include "executed.hpp"
#include "interpret.hpp"
#include "run_inputs.hpp"

using namespace heinzel;

namespace {

// T == D is the aliasing case: the score/attn temporaries [T,T] and the output [T,D] are one width, so
// a structural op's output can take a slot a same-width operand was just handed back into.
constexpr std::int32_t T = 4;
constexpr std::int32_t D = 4;
constexpr std::int32_t kQKV = T * D;   // one Q/K/V tensor, row-major, a length-1 stream
constexpr double kScale = 0.5;         // 1/sqrt(D), D == 4 — a Python constant folded in, as the old example

// The three inputs, carried THROUGH FLOAT so the values are exactly representable and the reference and
// the graph see the same bits. Three different formulas so scores are a genuine distribution and the
// output moves.
constexpr double q_value(std::int32_t i) { return static_cast<double>(0.11f * static_cast<float>(i % 13) - 0.5f); }
constexpr double k_value(std::int32_t i) { return static_cast<double>(0.07f * static_cast<float>(i % 17) - 0.3f); }
constexpr double v_value(std::int32_t i) { return static_cast<double>(0.19f * static_cast<float>(i % 11) - 0.8f); }

// out = softmax_k( (Q Kᵀ) / sqrt(D) ) V. scores = einsum("qd,kd->qk") contracts the feature axis d;
// out = einsum("qk,kd->qd") contracts the key axis k; the stable softmax subtracts a keepdims max
// over the key axis and divides by a keepdims sum — the softmax_here pipeline over the key axis.
constexpr Graph build_attention() {
    Input Q{"Q", {T, D}};
    Input K{"K", {T, D}};
    Input V{"V", {T, D}};
    Expr scores = einsum("qd,kd->qk", Q, K) * kScale; // {T,T}
    Expr m = scores.max(1, keepdims);                 // {T,1}
    Expr e = exp(scores - m);                         // {T,T}
    Expr s = e.sum(1, keepdims);                      // {T,1}
    Expr attn = e / s;                                // {T,T}
    Expr out = einsum("qk,kd->qd", attn, V);          // {T,D}
    return finish(Output{"out"} = out);
}

constexpr Graph analysed(numerics::Precision level) {
    Graph g = build_attention();
    set_default_precision(g, level);
    if (!infer(g).ok) {
        fail("the attention graph did not resolve");
    }
    return g;
}

constexpr Graph kAttention = analysed(numerics::Precision::Normal);
constexpr Graph attention_high() { return analysed(numerics::Precision::High); }

constexpr std::int32_t kOut = T * D;

constexpr std::array<double, kQKV> q_input() {
    std::array<double, kQKV> a{};
    for (std::int32_t i = 0; i < kQKV; ++i) { a[static_cast<std::size_t>(i)] = q_value(i); }
    return a;
}
constexpr std::array<double, kQKV> k_input() {
    std::array<double, kQKV> a{};
    for (std::int32_t i = 0; i < kQKV; ++i) { a[static_cast<std::size_t>(i)] = k_value(i); }
    return a;
}
constexpr std::array<double, kQKV> v_input() {
    std::array<double, kQKV> a{};
    for (std::int32_t i = 0; i < kQKV; ++i) { a[static_cast<std::size_t>(i)] = v_value(i); }
    return a;
}

constexpr Inputs attention_feeds(const std::array<double, kQKV>& q, const std::array<double, kQKV>& k,
                                 const std::array<double, kQKV>& v) {
    Inputs feeds{};
    feeds.push_back(InputFeed{q.data(), kQKV});
    feeds.push_back(InputFeed{k.data(), kQKV});
    feeds.push_back(InputFeed{v.data(), kQKV});
    return feeds;
}

// Walked: the graph interpreted, no plan involved — the established-correct oracle.
constexpr std::array<double, kOut> attention_walked(const Graph& g = kAttention) {
    const std::array<double, kQKV> q = q_input();
    const std::array<double, kQKV> k = k_input();
    const std::array<double, kQKV> v = v_input();
    Inputs feeds = attention_feeds(q, k, v);
    Signals<1> s{};
    run(g, feeds, s);
    std::array<double, kOut> got{};
    for (std::int32_t e = 0; e < kOut; ++e) {
        got[static_cast<std::size_t>(e)] = sample_at(s, g.outputs[0].value, e, 0);
    }
    return got;
}

// On the loop tree, at High: the graph's inputs are F32, so its buffers are float.
std::array<double, kOut> attention_executed() {
    const std::array<double, kQKV> q = q_input();
    const std::array<double, kQKV> k = k_input();
    const std::array<double, kQKV> v = v_input();
    std::array<float, kQKV> qf{};
    std::array<float, kQKV> kf{};
    std::array<float, kQKV> vf{};
    for (std::size_t i = 0; i < kQKV; ++i) {
        qf[i] = static_cast<float>(q[i]);
        kf[i] = static_cast<float>(k[i]);
        vf[i] = static_cast<float>(v[i]);
    }
    std::array<float, kOut> out{};
    Executor<attention_high> ex;
    testing::held(ex);
    ex.input("Q") = qf.data();
    ex.input("K") = kf.data();
    ex.input("V") = vf.data();
    ex.output("out") = out.data();
    ex.run();
    std::array<double, kOut> got{};
    for (std::size_t e = 0; e < kOut; ++e) {
        got[e] = static_cast<double>(out[e]);
    }
    return got;
}

// The output really moves — an attention that answered one number everywhere would agree with anything
// and prove nothing.
constexpr bool attention_moves() {
    const std::array<double, kOut> y = attention_walked();
    for (std::int32_t e = 1; e < kOut; ++e) {
        if (!(y[static_cast<std::size_t>(e)] == y[0])) {
            return true;
        }
    }
    return false;
}

static_assert(attention_moves(), "the attention output is constant — the agreement in main proves nothing");

// An independent double oracle: attention computed directly, so the check does not rest on the walk
// alone. Not constexpr — it uses std::exp at runtime — and compared within the old example's atol.
std::array<double, kOut> reference_attention() {
    const std::array<double, kQKV> q = q_input();
    const std::array<double, kQKV> k = k_input();
    const std::array<double, kQKV> v = v_input();
    std::array<double, kOut> out{};
    for (std::int32_t qi = 0; qi < T; ++qi) {
        std::array<double, T> score{};
        double mx = -1e300;
        for (std::int32_t ki = 0; ki < T; ++ki) {
            double dot = 0.0;
            for (std::int32_t d = 0; d < D; ++d) {
                dot += q[static_cast<std::size_t>(qi * D + d)] * k[static_cast<std::size_t>(ki * D + d)];
            }
            score[static_cast<std::size_t>(ki)] = dot * kScale;
            if (score[static_cast<std::size_t>(ki)] > mx) { mx = score[static_cast<std::size_t>(ki)]; }
        }
        double sum = 0.0;
        for (std::int32_t ki = 0; ki < T; ++ki) {
            score[static_cast<std::size_t>(ki)] = std::exp(score[static_cast<std::size_t>(ki)] - mx);
            sum += score[static_cast<std::size_t>(ki)];
        }
        for (std::int32_t d = 0; d < D; ++d) {
            double acc = 0.0;
            for (std::int32_t ki = 0; ki < T; ++ki) {
                acc += (score[static_cast<std::size_t>(ki)] / sum) * v[static_cast<std::size_t>(ki * D + d)];
            }
            out[static_cast<std::size_t>(qi * D + d)] = acc;
        }
    }
    return out;
}

} // namespace

int main() {
    if (!(attention_executed() == attention_walked(attention_high()))) {
        std::printf("attention: the loop tree at High disagreed with the walk\n");
        return 2;
    }
    const std::array<double, kOut> got = attention_walked();
    const std::array<double, kOut> ref = reference_attention();
    for (std::int32_t e = 0; e < kOut; ++e) {
        const double diff = got[static_cast<std::size_t>(e)] - ref[static_cast<std::size_t>(e)];
        if (diff > 1e-4 || diff < -1e-4) {
            std::printf("attention: element %d disagreed with the reference by %.3e\n", e, diff);
            return 1;
        }
    }
    std::printf("attention ok\n");
    return 0;
}
