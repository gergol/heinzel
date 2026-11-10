// 16-voice bank of the Moog ladder (par = 16 independent voices, 16 in / 16 out) — the
// headline arm. Heinzel lanes the 16 voices into SIMD lanes (Plan/archive/18, W voices/instruction);
// FAUST -vec vectorizes the TIME axis of 16 independent serial ladders. The bench reveals
// which wins: across-voice (Heinzel) vs along-time (FAUST). Sanity-band checked per channel.
// See Plan/archive/20.
import("stdfaust.lib");
voice = ve.moog_vcf(0.7, 1000.0);
process = par(i, 16, voice);
