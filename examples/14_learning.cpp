// Tour 14: learning. A linear model fitted by gradient descent, its gradients derived by the compiler.
//
//   - `gradients(loss, {Grad{"dw", w}, Grad{"db", b}})` takes a graph that computes a loss and appends the backward
//     pass: the gradient of the loss with respect to each named input, as outputs "dw" and "db". The result is an
//     ordinary graph, compiled once, which computes the loss and both gradients in one run.
//   - Training is a plain C++ loop around that one program: run, step the weights against their gradients in place,
//     run again. The inputs are read where they lie at every run, so the program sees each step's weights.
//   - Gradients run through elementwise arithmetic, sums and means, and windows over a tensor's axes (the showcase
//     `learn_a_filter` fits a filter's taps through one). A learned number is a tensor input (`b` here, `{1}`): its
//     gradient keeps the axis it is summed over.
//
// The first gradients are checked against their closed form; the data is made from known weights, without noise, so
// the fit must find them.

#include <cmath>
#include <cstdio>
#include <vector>

#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kExamples = 256;
constexpr int kFeatures = 3;

constexpr Graph fit() {
    Input x{"x", {kExamples, kFeatures}};
    Input y{"y", {kExamples}};
    Input w{"w", {kFeatures}};
    Input b{"b", {1}};
    Expr error = (x * w).sum(1) + b - y;
    Output loss{"loss"};
    loss = (error * error).mean(0);
    return gradients(loss, {Grad{"dw", w}, Grad{"db", b}});
}

constexpr float kTrueW[kFeatures] = {2.0f, -1.0f, 0.5f};
constexpr float kTrueB = 0.25f;

bool is_the_closed_form(const float* dw, float db, const std::vector<float>& x, const std::vector<float>& y);

} // namespace

int main() {
    std::vector<float> x(kExamples * kFeatures);
    std::vector<float> y(kExamples);
    unsigned seed = 14;
    for (int e = 0; e < kExamples; ++e) {
        y[e] = kTrueB;
        for (int f = 0; f < kFeatures; ++f) {
            seed = seed * 1664525u + 1013904223u;
            x[e * kFeatures + f] = static_cast<float>(seed >> 8) / 8388608.0f - 1.0f;
            y[e] += kTrueW[f] * x[e * kFeatures + f];
        }
    }

    float w[kFeatures] = {};
    float b = 0.0f;
    float loss = 0.0f;
    float dw[kFeatures];
    float db = 0.0f;
    Executor<fit> trainer;
    trainer.input("x") = x.data();
    trainer.input("y") = y.data();
    trainer.input("w") = w;
    trainer.input("b") = &b;
    trainer.output("loss") = &loss;
    trainer.output("dw") = dw;
    trainer.output("db") = &db;

    for (int step = 0; step <= 300; ++step) {
        trainer.run();
        if (step == 0 && !is_the_closed_form(dw, db, x, y)) {
            std::fprintf(stderr, "14_learning: the derived gradients are not the closed form's\n");
            return 1;
        }
        if (step % 60 == 0) {
            std::printf("step %3d  loss %.3e\n", step, static_cast<double>(loss));
        }
        for (int f = 0; f < kFeatures; ++f) {
            w[f] -= 0.5f * dw[f];
        }
        b -= 0.5f * db;
    }

    double off = std::fabs(b - kTrueB);
    for (int f = 0; f < kFeatures; ++f) {
        off = std::fmax(off, std::fabs(w[f] - kTrueW[f]));
    }
    if (!(off < 1e-4)) {
        std::fprintf(stderr, "14_learning: the weights stopped %.3g from the true ones\n", off);
        return 1;
    }
    std::printf("14_learning: w = (%.4f, %.4f, %.4f), b = %.4f, within %.1e of the weights the data was made from\n",
                static_cast<double>(w[0]), static_cast<double>(w[1]), static_cast<double>(w[2]), static_cast<double>(b), off);
    return 0;
}

namespace {

// With every weight 0 the model says nothing, so the error is -y: d loss / d w_f = -2/N · Σ y·x_f and
// d loss / d b = -2/N · Σ y.
bool is_the_closed_form(const float* dw, float db, const std::vector<float>& x, const std::vector<float>& y) {
    double sum_b = 0.0;
    double sum_w[kFeatures] = {};
    for (int e = 0; e < kExamples; ++e) {
        sum_b += y[e];
        for (int f = 0; f < kFeatures; ++f) {
            sum_w[f] += static_cast<double>(y[e]) * x[e * kFeatures + f];
        }
    }
    bool ok = std::fabs(db + 2.0 * sum_b / kExamples) < 1e-5;
    for (int f = 0; f < kFeatures; ++f) {
        ok = ok && std::fabs(dw[f] + 2.0 * sum_w[f] / kExamples) < 1e-5;
    }
    return ok;
}

} // namespace
