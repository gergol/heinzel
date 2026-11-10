// 440 Hz sine at amplitude 0.5 — same task as Heinzel's affine_oscillator (sine wave).
// FAUST's os.osc is a table-based oscillator (its idiomatic sine); Heinzel uses a vectorized
// minimax sine. Method differs, so the bench checks analytic RMS (= 0.5/sqrt(2)), not a
// sample match. See Plan/archive/20.
import("stdfaust.lib");
process = os.osc(440.0) * 0.5;
