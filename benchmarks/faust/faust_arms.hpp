// Handles into the committed FAUST-generated TUs: one factory per (workload × mode), appended to each
// generated .cpp by tools/gen-faust.sh. `dsp` is the vendored base in faust_dsp.h. The bench creates an
// arm via the factory, drives it through the base pointer (init + compute), and deletes it. See Plan/archive/20.
#ifndef HEINZEL_FAUST_ARMS_HPP
#define HEINZEL_FAUST_ARMS_HPP

#include "faust_dsp.h"

extern dsp* heinzel_make_sine_scal();
extern dsp* heinzel_make_sine_vec();
extern dsp* heinzel_make_strip_scal();
extern dsp* heinzel_make_strip_vec();
extern dsp* heinzel_make_biquad3_scal();
extern dsp* heinzel_make_biquad3_vec();
extern dsp* heinzel_make_ladder_scal();
extern dsp* heinzel_make_ladder_vec();
extern dsp* heinzel_make_ladder16_scal();
extern dsp* heinzel_make_ladder16_vec();
extern dsp* heinzel_make_reverb_scal();
extern dsp* heinzel_make_reverb_vec();

#endif // HEINZEL_FAUST_ARMS_HPP
