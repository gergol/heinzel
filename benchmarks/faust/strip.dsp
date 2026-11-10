// Saturating channel strip: input gain -> 3 tanh saturators -> output gain -> clamp[-1,1].
// Same constants as Heinzel's g_strip / channel_strip_bench:
//   kGin=2.0, (kD1,kB1)=(1.5,0.10), (kD2,kB2)=(1.3,-0.05), (kD3,kB3)=(1.1,0.0), kGout=0.7.
// Checked against the textbook scalar strip reference within a loose tolerance (tanh rounds
// slightly differently between FAUST and Heinzel's minimax). See Plan/archive/20.
import("stdfaust.lib");
sat(d, b) = *(d) : +(b) : ma.tanh;
process = *(2.0) : sat(1.5, 0.10) : sat(1.3, -0.05) : sat(1.1, 0.0) : *(0.7) : max(-1.0) : min(1.0);
