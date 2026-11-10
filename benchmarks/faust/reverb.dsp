// Freeverb (mono) at the same room/damp/wet as Heinzel's dsp::reverb_mono(x, fb=0.84, damp=0.2,
// wet=0.33, dry=0.0). FAUST's re.mono_freeverb is the canonical Schroeder/Freeverb topology
// (8 combs + 4 allpass); its tuning differs from heinzel's, so outputs won't match sample-for-sample
// (sanity-band checked, not equality). re.mono_freeverb(combfb, allpassfb, damp, spread).
// dry=0, so output is just the wet reverb scaled by 0.33. See Plan/archive/20.
import("stdfaust.lib");
process = re.mono_freeverb(0.84, 0.5, 0.2, 0) * 0.33;
