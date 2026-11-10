// 3-section EQ cascade (80 Hz HP / 1 kHz +6 dB peak / 8 kHz LP @ 48 kHz) — the topology-MATCHED
// arm. These are Heinzel's own RBJ coefficients (dsp::highpass/peaking/lowpass, the kEq in the JUCE
// bench), emitted verbatim so FAUST runs the identical transfer function; fi.tf2(b0,b1,b2,a1,a2)
// uses the same convention (a0 normalized, y[n]=b0 x[n]+b1 x[n-1]+b2 x[n-2]-a1 y[n-1]-a2 y[n-2]).
// The bench cross-checks FAUST's output against the measured frequency response within tolerance;
// a sign-convention mismatch would fail that check loudly. Regenerate via tools/gen-faust.sh.
// (Recompute the numbers with benchmarks/faust/.coeff_print if the spec changes.) See Plan/archive/20.
import("stdfaust.lib");
hp = fi.tf2(0.992621481, -1.98524296,  0.992621481, -1.98518848, 0.985297322);
pk = fi.tf2(1.04395306,  -1.89532065,  0.867722273, -1.89532065, 0.911675334);
lp = fi.tf2(0.155042127,  0.310084254, 0.155042127, -0.620168507, 0.240337029);
process = hp : pk : lp;
