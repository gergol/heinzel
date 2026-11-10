// One pre-norm transformer block — causal self-attention, then a ReLU MLP, each behind an RMS norm and a
// residual connection — written as it is on a whiteboard and compiled to loop nests while this file compiles.
//
//   - Every product is an `einsum` over the element axes: the projections, the attention scores, the weighted sum
//     of the values and both layers of the MLP. A block of tokens is a tensor; nothing here has time.
//   - RMS norm and softmax are short functions over reductions that keep their axis (`keepdims`), so the result
//     broadcasts back against what was reduced.
//   - The causal mask is built from two index ramps (`iota`) and a branch-free `where`.
//   - A shape mistake is a build error. `try_compile` hands the reason back as text instead of stopping, so the
//     example asserts the reason below.
//
// The block's output is checked against the same block written as plain C++ loops.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kTokens = 64;
constexpr int kWidth = 64;
constexpr int kHidden = 256;
constexpr double kScale = 0.125;
static_assert(kScale * kScale * kWidth == 1.0, "attention scores are scaled by 1/√kWidth");

constexpr Expr rms_norm(Expr x, Expr gain) { return x * rsqrt((x * x).mean(1, keepdims) + 1e-6) * gain; }

// Along axis 1 — the keys, for attention scores laid out query × key.
constexpr Expr softmax(Expr s) {
    Expr e = exp(s - s.max(1, keepdims));
    return e / e.sum(1, keepdims);
}

// A query attends to its own position and the ones before it.
constexpr Expr causal(Expr s) {
    Expr key = iota({1, kTokens});
    Expr query = iota({kTokens, 1});
    return where(key > query, -1e30, s);
}

constexpr Graph block() {
    Input x{"x", {kTokens, kWidth}};
    Input wq{"wq", {kWidth, kWidth}};
    Input wk{"wk", {kWidth, kWidth}};
    Input wv{"wv", {kWidth, kWidth}};
    Input wo{"wo", {kWidth, kWidth}};
    Input w1{"w1", {kWidth, kHidden}};
    Input w2{"w2", {kHidden, kWidth}};
    Input g1{"g1", {kWidth}};
    Input g2{"g2", {kWidth}};
    Output out{"out"};
    Expr h = rms_norm(x, g1);
    Expr q = einsum("td,de->te", h, wq);
    Expr k = einsum("td,de->te", h, wk);
    Expr v = einsum("td,de->te", h, wv);
    Expr scores = einsum("qe,ke->qk", q, k) * kScale;
    Expr masked = causal(scores);
    Expr attention = softmax(masked);
    Expr attended = einsum("qk,ke->qe", attention, v);
    Expr projected = einsum("te,ed->td", attended, wo);
    Expr y = x + projected;
    Expr h2 = rms_norm(y, g2);
    Expr hidden = einsum("td,df->tf", h2, w1);
    Expr rectified = max(hidden, 0.0);
    Expr mlp = einsum("tf,fd->td", rectified, w2);
    out = y + mlp;
    return out;
}

// Queries 64 wide against keys 48 wide: `Executor<mismatched_heads>` would stop the build here; `try_compile` says
// why.
constexpr Graph mismatched_heads() {
    Input q{"q", {kTokens, 64}};
    Input k{"k", {kTokens, 48}};
    Output scores{"scores"};
    scores = einsum("qe,ke->qk", q, k);
    return scores;
}
static_assert(try_compile(mismatched_heads()).refusal.view() == "einsum's contracted extents disagree: 64 and 48");

using Matrix = std::vector<float>;

struct Weights {
    Matrix wq;
    Matrix wk;
    Matrix wv;
    Matrix wo;
    Matrix w1;
    Matrix w2;
    Matrix g1;
    Matrix g2;
};

Matrix random(int rows, int columns, float centre, float spread, unsigned& seed);
Matrix reference(const Matrix& x, const Weights& w);

} // namespace

int main() {
    unsigned seed = 7;
    const auto weights = [&](int rows, int columns, float spread) { return random(rows, columns, 0.0f, spread, seed); };
    const Matrix x = weights(kTokens, kWidth, 1.0f);
    const Weights w{weights(kWidth, kWidth, 0.125f),  weights(kWidth, kWidth, 0.125f),  weights(kWidth, kWidth, 0.125f),
                    weights(kWidth, kWidth, 0.125f),  weights(kWidth, kHidden, 0.125f), weights(kHidden, kWidth, 0.0625f),
                    random(1, kWidth, 1.0f, 0.25f, seed), random(1, kWidth, 1.0f, 0.25f, seed)};

    Matrix out(kTokens * kWidth);
    Executor<block> attend;
    attend.input("x") = x.data();
    attend.input("wq") = w.wq.data();
    attend.input("wk") = w.wk.data();
    attend.input("wv") = w.wv.data();
    attend.input("wo") = w.wo.data();
    attend.input("w1") = w.w1.data();
    attend.input("w2") = w.w2.data();
    attend.input("g1") = w.g1.data();
    attend.input("g2") = w.g2.data();
    attend.output("out") = out.data();
    attend.run();

    const Matrix want = reference(x, w);
    double off = 0.0;
    for (std::size_t i = 0; i < want.size(); ++i) {
        if (const double d = std::fabs(out[i] - want[i]); std::isnan(d) || d > off) {
            off = d; // a NaN stays
        }
    }
    if (!(off < 1e-5)) {
        std::fprintf(stderr, "transformer_block: %.3g away from the plain C++ block\n", off);
        return 1;
    }
    std::printf("transformer_block: %d tokens, %d wide, within %.1e of the plain C++ block\n", kTokens, kWidth, off);
    return 0;
}

namespace {

// Uniform in [centre - spread, centre + spread).
Matrix random(int rows, int columns, float centre, float spread, unsigned& seed) {
    Matrix m(rows * columns);
    for (float& v : m) {
        seed = seed * 1664525u + 1013904223u;
        v = centre + spread * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
    }
    return m;
}

// ---- the same block as plain C++ loops, to check the compiled one against ----------------------------------

Matrix product(const Matrix& a, const Matrix& b, int rows, int inner, int columns) {
    Matrix c(rows * columns);
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < columns; ++j) {
            for (int l = 0; l < inner; ++l) {
                c[i * columns + j] += a[i * inner + l] * b[l * columns + j];
            }
        }
    }
    return c;
}

Matrix normed(const Matrix& x, const Matrix& gain) {
    Matrix y(x.size());
    for (int t = 0; t < kTokens; ++t) {
        float squares = 0;
        for (int d = 0; d < kWidth; ++d) {
            squares += x[t * kWidth + d] * x[t * kWidth + d];
        }
        const float scale = 1 / std::sqrt(squares / kWidth + 1e-6f);
        for (int d = 0; d < kWidth; ++d) {
            y[t * kWidth + d] = x[t * kWidth + d] * scale * gain[d];
        }
    }
    return y;
}

// Each query's softmax over the keys up to its own position; the later keys get nothing.
Matrix attention(const Matrix& q, const Matrix& k) {
    Matrix a(kTokens * kTokens);
    for (int i = 0; i < kTokens; ++i) {
        float* row = &a[i * kTokens];
        for (int j = 0; j <= i; ++j) {
            for (int e = 0; e < kWidth; ++e) {
                row[j] += q[i * kWidth + e] * k[j * kWidth + e];
            }
            row[j] *= static_cast<float>(kScale);
        }
        const float top = *std::max_element(row, row + i + 1);
        float sum = 0;
        for (int j = 0; j <= i; ++j) {
            row[j] = std::exp(row[j] - top);
            sum += row[j];
        }
        for (int j = 0; j <= i; ++j) {
            row[j] /= sum;
        }
    }
    return a;
}

Matrix reference(const Matrix& x, const Weights& w) {
    const Matrix h = normed(x, w.g1);
    const Matrix q = product(h, w.wq, kTokens, kWidth, kWidth);
    const Matrix k = product(h, w.wk, kTokens, kWidth, kWidth);
    const Matrix v = product(h, w.wv, kTokens, kWidth, kWidth);
    const Matrix a = attention(q, k);
    const Matrix attended = product(a, v, kTokens, kTokens, kWidth);
    const Matrix o = product(attended, w.wo, kTokens, kWidth, kWidth);
    Matrix y(x.size());
    for (std::size_t i = 0; i < y.size(); ++i) {
        y[i] = x[i] + o[i];
    }
    const Matrix h2 = normed(y, w.g2);
    Matrix f = product(h2, w.w1, kTokens, kWidth, kHidden);
    for (float& hidden : f) {
        hidden = std::max(hidden, 0.0f);
    }
    const Matrix m = product(f, w.w2, kTokens, kHidden, kWidth);
    for (std::size_t i = 0; i < y.size(); ++i) {
        y[i] += m[i];
    }
    return y;
}

} // namespace
