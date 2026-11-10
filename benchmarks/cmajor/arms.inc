// The Cmajor arms' host (cmajor_arms.hpp), included by arms_O3.cpp and arms_O4.cpp under `CMAJOR_ARMS`, the table's
// name. A patch runs as a host runs Cmajor's generated class: its frames copied in, `advance`, copied out, a block of
// at most `maxFramesPerBlock` at a time. Each class sits in a namespace of this file's: the two compilations link into
// one binary.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "cmajor_arms.hpp"

namespace {

namespace generated {
#include "generated/Filter.cpp"
#include "generated/Freeverb.cpp"
} // namespace generated

constexpr double kRate = 48000.0;

template <class P, class In, class Out>
void drive(P& p, In set_input, Out out, const float* x, float* y, std::size_t n, std::size_t in_channels) {
    for (std::size_t i = 0; i < n; i += P::maxFramesPerBlock) {
        const auto m = static_cast<std::uint32_t>(std::min<std::size_t>(P::maxFramesPerBlock, n - i));
        (p.*set_input)(x + i * in_channels, m, 0);
        p.advance(static_cast<std::int32_t>(m));
        p.copyOutputFrames(static_cast<std::uint32_t>(out), y + i * 2, m);
    }
}

void* freeverb_start() {
    auto* p = new generated::Freeverb;
    p->initialise(0, kRate);
    return p;
}
// roomSize, damping, width, wetLevel, dryLevel.
void freeverb_knobs(void* patch, const float* v) {
    auto* p = static_cast<generated::Freeverb*>(patch);
    p->addEvent_roomSize(v[0]);
    p->addEvent_damping(v[1]);
    p->addEvent_width(v[2]);
    p->addEvent_wetLevel(v[3]);
    p->addEvent_dryLevel(v[4]);
}
// The knobs at the patch's `init` values, as a host sends them; then silence until the smoothers reach them (0.02 s,
// 960 frames). The reverb's lines stay zero over it, so what follows runs as a fresh reverb at those knobs.
void* freeverb_make() {
    auto* p = static_cast<generated::Freeverb*>(freeverb_start());
    constexpr float init[5] = {80.0f, 50.0f, 100.0f, 33.0f, 40.0f};
    freeverb_knobs(p, init);
    std::array<float, 1024> silence{};
    std::array<float, 2 * 1024> sink{};
    drive(*p, &generated::Freeverb::setInputFrames_audioIn, generated::Freeverb::EndpointHandles::audioOut,
          silence.data(), sink.data(), silence.size(), 1);
    return p;
}
void freeverb_run(void* p, const float* x, float* y, std::size_t n) {
    drive(*static_cast<generated::Freeverb*>(p), &generated::Freeverb::setInputFrames_audioIn,
          generated::Freeverb::EndpointHandles::audioOut, x, y, n, 1);
}
void freeverb_destroy(void* p) { delete static_cast<generated::Freeverb*>(p); }

void* filter_start() {
    auto* p = new generated::Filter;
    p->initialise(0, kRate);
    return p;
}
// frequency, q.
void filter_knobs(void* patch, const float* v) {
    auto* p = static_cast<generated::Filter*>(patch);
    p->addEvent_frequency(v[0]);
    p->addEvent_q(v[1]);
}
void* filter_make() {
    void* p = filter_start();
    constexpr float init[2] = {1000.0f, 0.707107f};
    filter_knobs(p, init);
    return p;
}
void filter_run(void* p, const float* x, float* y, std::size_t n) {
    drive(*static_cast<generated::Filter*>(p), &generated::Filter::setInputFrames_in, generated::Filter::EndpointHandles::out,
          x, y, n, 2);
}
void filter_destroy(void* p) { delete static_cast<generated::Filter*>(p); }

} // namespace

const cmajor_arms::Arms cmajor_arms::CMAJOR_ARMS{
    {freeverb_make, freeverb_start, freeverb_knobs, freeverb_run, freeverb_destroy},
    {filter_make, filter_start, filter_knobs, filter_run, filter_destroy},
};
