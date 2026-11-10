# MF.6 s3d-2 — run-and-compare parity to the old tree at c4012b8

Old tree: exported from rewrite `main` (== ctg3 c4012b8) via `git archive main`, built in a
THROWAWAY dir /tmp/oldtree (ctg3 never touched), WAV-only:
  git archive main | tar -x -C /tmp/oldtree
  cmake -S /tmp/oldtree -B /tmp/oldtree/build -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
        -DHEINZEL_BUILD_EXAMPLES=ON -DHEINZEL_BUILD_TESTS=OFF
  ninja -C /tmp/oldtree/build energy_fraction

Signal: 48128 mono frames (94*hop=512, no trailing-block drop either side), 48 kHz,
  x[n] = 0.9*(n/N) * sin(2*pi*300*n/48000)  (amplitude ramp; W = (48128-1024)/512+1 = 93 frames)

Old-tree binary output (energy_fraction --threshold=T /tmp/test.wav), recorded 2026-09-07:
  --threshold=0.05  0.924731
  --threshold=0.1   0.849462
  --threshold=0.2   0.688172
  --threshold=0.3   0.526882

Rewrite pump (kEf1024 = energy_fraction symbolic-N, win=1024/hop=512), offline AND --stream:
  thr=0.05 offline=0.924731 stream=0.924731 (rc 0/0, frames 93/93)
  thr=0.10 offline=0.849462 stream=0.849462 (rc 0/0, frames 93/93)
  thr=0.20 offline=0.688172 stream=0.688172 (rc 0/0, frames 93/93)
  thr=0.30 offline=0.526882 stream=0.526882 (rc 0/0, frames 93/93)

RESULT: rewrite offline == rewrite --stream == old-tree binary, bit-for-bit, at all four
thresholds (above counts 86/79/64/49 of 93). Pinned durably in tests/unit/cli_run_test.cpp
matches_old_tree_energy_fraction (offline==stream exact; within 1 window of the recorded old-tree
count to absorb the old-float/new-double + clang/gcc gap).
