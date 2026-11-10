#pragma once

// A DELIBERATE OFFENDER for the core_no_audio gate (M8/O12 family; MF.6, Decision 44's audio layer).
// Nothing includes this file; it exists only to be the one thing the `_would_object` test points the
// gate at, so a gate that has quietly stopped objecting is caught. A core header reaching into
// `heinzel/audio/` is the edge O12 forbids: the audio layer (WAV I/O) is a leaf that may build ON
// the core, so a core header naming audio would mean the freestanding/embedded core was shaped by an
// I/O facility and could not compile with the audio layer deleted. Mirrors the old tree's
// check-core-no-audio.cmake, direction (1).
#include "heinzel/audio/wav.hpp"

namespace heinzel::fixture {
inline constexpr int core_no_audio_offender = 0;
}
