// Tour 01: a first graph. Two channels in, their average out.
//
// A graph is an ordinary `constexpr` function. It declares what comes in and what goes out, and writes the arithmetic
// between them. `Executor<downmix>` compiles it while this file compiles: the analysis, the planning and the lowering
// all run in the C++ compiler, and what is left at run time is the loop they made. You bind your buffers to its ports
// by name and call `run()`.
//
//   - `Input left{"left", Signal{kSamples}}` declares a signal: kSamples numbers a run, one per sample.
//   - `Output mono{"mono"}` is assigned once. Its shape and element type follow from what it is assigned.
//   - A number such as 0.5 takes the type of what it meets: in this float graph it is a float.
//   - A port's name is checked while compiling: `mixer.input("lft")` would stop the build ("no port by that name").
//
// The result is checked against the same average written as a plain C++ loop: it is the same numbers, bit for bit.

#include <cmath>
#include <cstdio>
#include <vector>

#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kSamples = 1024;

constexpr Graph downmix() {
    Input left{"left", Signal{kSamples}};
    Input right{"right", Signal{kSamples}};
    Output mono{"mono"};
    mono = 0.5 * (left + right);
    return mono;
}

} // namespace

int main() {
    std::vector<float> left(kSamples);
    std::vector<float> right(kSamples);
    std::vector<float> mono(kSamples);
    for (int t = 0; t < kSamples; ++t) {
        left[t] = std::sin(0.01f * static_cast<float>(t));
        right[t] = std::sin(0.03f * static_cast<float>(t));
    }

    Executor<downmix> mixer;
    mixer.input("left") = left.data(); // read where it lies: nothing is copied in
    mixer.input("right") = right.data();
    mixer.output("mono") = mono.data(); // written straight into your buffer
    mixer.run();

    for (int t = 0; t < kSamples; ++t) {
        if (mono[t] != 0.5f * (left[t] + right[t])) {
            std::fprintf(stderr, "01_first_graph: sample %d is %g, not the loop's %g\n", t, mono[t], 0.5f * (left[t] + right[t]));
            return 1;
        }
    }
    std::printf("01_first_graph: %d samples of two channels averaged, the plain loop's numbers bit for bit\n", kSamples);
    return 0;
}
