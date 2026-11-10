// A named duration folds to an exact integer sample lag at compile time, and the fold OBJECTS — pointed
// at a deliberately broken input, observed to complain by name — when the duration does not land on a
// whole sample or the rate is unset.
//
// The good case is proven at COMPILE TIME (`static_assert`), the strongest form: `to_samples` is
// `constexpr`, so a wrong answer would not build. The refusals are proven at RUNTIME, where the same
// `fail()` throws instead of stopping the build, so the message it complains WITH can be read back
// and matched — the tree's negative-test discipline (a refusal is shown by the reason it gives, not
// by the mere fact that something failed).

#include "heinzel/frontend/declarations.hpp"
#include "heinzel/units/named_units.hpp"

// Only the named-unit namespace is opened; `heinzel::error` is qualified.
using namespace heinzel::units;

namespace {

// The example's own fold: 3 ms at 2 kHz is exactly 6 samples. Folded at compile time.
static_assert(to_samples(3.0 * ms, 2000.0) == 6, "3 ms @ 2 kHz is 6 samples");

// `using namespace heinzel;` brings the three units a lag is written in: the very ones of `units`.
static_assert(&heinzel::ms == &heinzel::units::ms && &heinzel::us == &heinzel::units::us &&
                  &heinzel::second == &heinzel::units::second,
              "heinzel::ms, us and second are units::ms, us and second");
static_assert(heinzel::ms == Scale{1e-3} && heinzel::us == Scale{1e-6} && heinzel::second == Scale{1.0},
              "and they are a thousandth, a millionth and the whole of a second");

// The factors carry through: the same duration written three ways, and a realistic audio rate.
static_assert(to_samples(3.0 * ms, 2000.0) == to_samples(3000.0 * us, 2000.0),
              "ms and us are the same duration scaled");
static_assert(to_samples(0.003 * second, 2000.0) == 6, "seconds and ms agree on the duration");
static_assert(to_samples(5.0 * ms, 48000.0) == 240, "5 ms @ 48 kHz is 240 samples");

// Zero is a whole number of samples, and the boundary must fold rather than trip the tolerance.
static_assert(to_samples(0.0 * ms, 2000.0) == 0, "a zero duration is zero samples");

// The expected complaints, quoted so the match is against the exact reason.
constexpr std::string_view kNotWhole =
    "heinzel::units::to_samples: duration is not an integer number of samples at this sample rate";
constexpr std::string_view kUnsetRate =
    "heinzel::units::to_samples: sample rate is unset or non-positive";

// 2.5 ms at 2 kHz is 5 samples exactly — but 2.6 ms is 5.2 samples, which is not a whole lag. A
// sub-sample delay is a fractional-delay node's job; this fold refuses to silently round it away.
bool a_sub_sample_duration_is_refused() {
    try {
        (void)to_samples(2.6 * ms, 2000.0);
        return false;
    } catch (const heinzel::error& e) {
        return e.message() == kNotWhole;
    }
}

// A rate of zero is unset, not "zero samples" — folding against it would divide the meaning of every
// duration by nothing.
bool an_unset_rate_is_refused() {
    try {
        (void)to_samples(3.0 * ms, 0.0);
        return false;
    } catch (const heinzel::error& e) {
        return e.message() == kUnsetRate;
    }
}

} // namespace

int main() {
    if (!a_sub_sample_duration_is_refused()) return 1;
    if (!an_unset_rate_is_refused()) return 2;

    // And none of the refusals refuses everything: a good duration still folds at runtime, matching
    // the compile-time answer the static_asserts pinned.
    return to_samples(3.0 * ms, 2000.0) == 6 ? 0 : 3;
}
