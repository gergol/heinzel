// Tour 02: Params and functions. A drive pedal: gain into a clipper, mixed back with the dry signal.
//
//   - `Param drive{"drive"}` is one number a run, set from the host: `pedal.param("drive") = 8.0`. Changing it
//     between runs recompiles nothing; the program reads it at the start of each run.
//   - The math is spelled as in C++: `tanh`, `exp`, `exp2`, `log`, `log10`, `sin`, `cos`, `tan`, `sinpi` (sin of pi
//     times x), `cospi`, `pow`, `sqrt`, `rsqrt`, `floor`, `frac`, `abs`, `min`, `max`, `clamp`. (`sqrt`, `log`,
//     `pow`, `tan` and a division ask for the ranges of their operands: tour 12.)
//   - `a > b` is a mask, 1 where it holds and 0 where not, and so are `<`, `>=` and `<=`; a comparison takes an
//     expression or a number on either side (`x > 0.0`). `where(mask, t, f)` picks between two values, and either
//     or both may be a number. Both are computed and blended, with no branch, so a `where` runs in vector lanes like
//     any other arithmetic.
//   - A piece of a graph is a `constexpr` function from expressions to an expression, like `clip` below. Calling it
//     adds its arithmetic to the graph; nothing is called at run time.
//
// The pedal is run at two settings and checked against the same pedal written as a plain C++ loop.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "heinzel/dsl.hpp"

using namespace heinzel;

namespace {

constexpr int kSamples = 2048;

// A valve-like clipper: the positive half rounds off softly, the negative half is cut hard at -0.6.
constexpr Expr clip(Expr x) { return where(x > 0.0, tanh(x), max(x, -0.6)); }

constexpr Graph pedal() {
    Input guitar{"guitar", Signal{kSamples}};
    Param drive{"drive"};
    Param level{"level"};
    Param mix{"mix"};
    Output out{"out"};
    Expr clipped = clip(drive * guitar);
    out = mix * level * clipped + (1.0 - mix) * guitar;
    return out;
}

struct Setting {
    float drive;
    float level;
    float mix;
};

std::vector<float> reference(const std::vector<float>& guitar, Setting s);

} // namespace

int main() {
    std::vector<float> guitar(kSamples);
    std::vector<float> out(kSamples);
    for (int t = 0; t < kSamples; ++t) { // a decaying pluck
        guitar[t] = std::exp(-0.002f * static_cast<float>(t)) * std::sin(0.05f * static_cast<float>(t));
    }

    Executor<pedal> pedal;
    pedal.input("guitar") = guitar.data();
    pedal.output("out") = out.data();

    for (const Setting s : {Setting{2.0f, 0.8f, 0.5f}, Setting{12.0f, 0.5f, 1.0f}}) {
        pedal.param("drive") = s.drive;
        pedal.param("level") = s.level;
        pedal.param("mix") = s.mix;
        pedal.run();

        // The program's `tanh` is its own float core, within two units in the last place of the exact one; the
        // C++ library's is another. The bound leaves room for both.
        const std::vector<float> want = reference(guitar, s);
        for (int t = 0; t < kSamples; ++t) {
            if (!(std::fabs(out[t] - want[t]) < 1e-6f)) {
                std::fprintf(stderr, "02_params_and_functions: drive %g, sample %d is %g, not %g\n", s.drive, t, out[t], want[t]);
                return 1;
            }
        }
    }
    std::printf("02_params_and_functions: the pedal at two settings, each within 1e-6 of the plain loop\n");
    return 0;
}

namespace {

std::vector<float> reference(const std::vector<float>& guitar, Setting s) {
    std::vector<float> out(guitar.size());
    for (std::size_t t = 0; t < guitar.size(); ++t) {
        const float x = s.drive * guitar[t];
        const float clipped = x > 0.0f ? std::tanh(x) : std::max(x, -0.6f);
        out[t] = s.mix * s.level * clipped + (1.0f - s.mix) * guitar[t];
    }
    return out;
}

} // namespace
