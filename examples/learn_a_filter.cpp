// Learning a filter by gradient descent: a 32-tap FIR filter is fitted to what a resonant biquad makes of white
// noise, and so learns the biquad's impulse response.
//
//   - Autodiff is a graph transform: `gradients` appends the backward pass to the fit's forward graph, in the
//     same vocabulary, so the loss and its gradient are one program — compiled once, while this file compiles,
//     and run once per step of a plain C++ descent loop that updates the taps in place.
//   - The declaration alone says which axis is time: the biquad reads a `Signal` and feeds back on its own past,
//     `y[n - 1]`; the fit reads the same 2048 numbers as a tensor `{kSamples}` and slides a window over it.
//
// The first gradient is checked against its closed form, the learned taps against the biquad's impulse response.

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kSamples = 2048;
constexpr int kTaps = 32;
constexpr int kFrames = kSamples - kTaps + 1;
constexpr Index n{};

// The system to identify, a resonant low-pass.
constexpr Graph biquad() {
    Input x{"x", Signal{kSamples}};
    Output y{"y"};
    y[n] = 0.2 * x[n] + 0.4 * x[n - 1] + 0.2 * x[n - 2] + 1.1 * y[n - 1] - 0.6 * y[n - 2];
    return y;
}

// The model: every window of 32 input samples, weighted by the taps and summed. Its loss is the mean squared
// error against the biquad's output; the loss's gradient with respect to the taps is derived, not written.
constexpr Graph fit() {
    Input x{"x", {kSamples}};
    Input taps{"taps", {kTaps}};
    Input target{"target", {kFrames}};
    Output loss{"loss"};
    Expr prediction = (x.unfold(0, kTaps) * taps).sum(1);
    Expr error = prediction - target;
    loss = (error * error).mean(0);
    return gradients(loss, {Grad{"gradient", taps}});
}

std::vector<float> noise(int count);
bool is_the_closed_form(const float* gradient, const std::vector<float>& x, const std::vector<float>& target);
std::vector<double> impulse_response();

} // namespace

int main() {
    // White noise in, the biquad's answer out. A window's sum lands on its last sample, so frame f is compared
    // with the biquad's output at f + kTaps - 1.
    const std::vector<float> x = noise(kSamples);
    std::vector<float> y(kSamples);
    Executor<biquad> system;
    system.input("x") = x.data();
    system.output("y") = y.data();
    system.run();
    const std::vector<float> target(y.begin() + kTaps - 1, y.end());

    std::vector<float> taps(kTaps, 0.0f);
    std::vector<float> gradient(kTaps);
    float loss = 0.0f;
    Executor<fit> fitting;
    fitting.input("x") = x.data();
    fitting.input("taps") = taps.data(); // read afresh at every run: the loop below updates it in place
    fitting.input("target") = target.data();
    fitting.output("loss") = &loss;
    fitting.output("gradient") = gradient.data();

    for (int step = 0; step <= 200; ++step) {
        fitting.run();
        if (step == 0 && !is_the_closed_form(gradient.data(), x, target)) {
            std::fprintf(stderr, "learn_a_filter: the derived gradient is not the closed form's\n");
            return 1;
        }
        if (step % 40 == 0) {
            std::printf("step %3d  loss %.3e\n", step, static_cast<double>(loss));
        }
        for (int k = 0; k < kTaps; ++k) {
            taps[k] -= 0.1f * gradient[k];
        }
    }

    // A window holds its samples oldest first, so the taps learn the impulse response back to front.
    const std::vector<double> h = impulse_response();
    double off = 0.0;
    for (int n = 0; n < kTaps; ++n) {
        if (const double d = std::fabs(taps[kTaps - 1 - n] - h[n]); std::isnan(d) || d > off) {
            off = d; // a NaN stays
        }
    }
    std::printf("\n  n   biquad    learned\n");
    for (int n = 0; n < 6; ++n) {
        std::printf("%3d  %8.5f  %8.5f\n", n, h[n], static_cast<double>(taps[kTaps - 1 - n]));
    }
    if (!(off < 1e-4)) {
        std::fprintf(stderr, "learn_a_filter: the learned taps are %.3g away from the impulse response\n", off);
        return 1;
    }
    std::printf("\nlearn_a_filter: all %d taps within %.1e of the biquad's impulse response\n", kTaps, off);
    return 0;
}

namespace {

// Uniform in [-1, 1).
std::vector<float> noise(int count) {
    std::vector<float> v(count);
    unsigned seed = 3;
    for (float& s : v) {
        seed = seed * 1664525u + 1013904223u;
        s = static_cast<float>(seed >> 8) / 8388608.0f - 1.0f;
    }
    return v;
}

// ---- the closed forms, to check the compiled program against -----------------------------------------------

// With every tap zero the model says nothing, so d loss / d tap k = -2/F · Σ_f target[f] · x[f + k].
bool is_the_closed_form(const float* gradient, const std::vector<float>& x, const std::vector<float>& target) {
    for (int k = 0; k < kTaps; ++k) {
        double sum = 0.0;
        for (int f = 0; f < kFrames; ++f) {
            sum += static_cast<double>(target[f]) * x[f + k];
        }
        if (!(std::fabs(gradient[k] + 2.0 * sum / kFrames) < 1e-5)) {
            return false;
        }
    }
    return true;
}

// The biquad's answer to a single 1, run as the recurrence it is.
std::vector<double> impulse_response() {
    std::vector<double> h(kTaps);
    double x1 = 0;
    double x2 = 0;
    double y1 = 0;
    double y2 = 0;
    for (int n = 0; n < kTaps; ++n) {
        const double x = n == 0 ? 1.0 : 0.0;
        h[n] = 0.2 * x + 0.4 * x1 + 0.2 * x2 + 1.1 * y1 - 0.6 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = h[n];
    }
    return h;
}

} // namespace
