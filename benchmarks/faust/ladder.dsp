// Moog 4-pole ladder low-pass @ 1 kHz / resonance 0.7 — same DSP task as Heinzel's g_ladder
// (the JUCE LPF12 LadderFilter). FAUST's ve.moog_vcf is its own idiomatic Moog model, so
// the output will NOT match Heinzel sample-for-sample (different topology); the bench validates a
// sanity band (finite, RMS in range), not equality. A serial recurrence on both sides.
// See Plan/archive/20.
import("stdfaust.lib");
process = ve.moog_vcf(0.7, 1000.0);
