#!/usr/bin/env bash
# `Plan/15-zero-overhead/01`'s table: each workload in each arm, built for its bytes (B), for its counts (I, C, M, F)
# and for its check against the bare arm, then measured pinned to one core. Run it on a quiet box: every build runs
# first, at most JOBS at once, and nothing is measured while one runs.
#
#   measure.sh <out-dir> [include-dir]
#
# include-dir is another tree's `include/` (a scratch copy, say); this tree's by default. The arm `main` builds
# `main_arm.cpp` against MAIN_INCLUDE, `main`'s `include/` (default `build/main-tree/include`). EXTRA_FLAGS adds flags
# (-UNDEBUG, say). The environment narrows the run: WORKLOADS="w1 w3", ARMS="bare executor"; JOBS (default 4), CORE
# (default 4), CXX (default clang++-22), SIZE (default llvm-size-22).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=${1:?usage: measure.sh <out-dir> [include-dir]}
include=$(cd "${2:-$root/include}" && pwd)
main_include=${MAIN_INCLUDE:-$root/build/main-tree/include}
mkdir -p "$out"
out=$(cd "$out" && pwd)
workloads=${WORKLOADS:-w1 w2 w3 w4 w5 w6 w7 w8 w9 w10 w11 w12}
arms=${ARMS:-hand bare bare_copy executor main}
core=${CORE:-4}
size=${SIZE:-llvm-size-22}
# The library's test flags (`CMakeLists.txt`), then 01's; `main` builds with them too, its own tree on the path.
flags=(-std=c++2c -fconstexpr-steps=268435456 -ffp-contract=off -fno-math-errno -Wno-psabi -DHEINZEL_HAVE_THREADS
       -O2 -DNDEBUG -g0 ${EXTRA_FLAGS:-})

# <workload> <arm> <kind> of every build: a bare arm is the reference, so it has no check, W9 and W12 have no hand loop,
# and `main` cannot run W4, W5, W9, W10, W11 or W12 (`main_arm.cpp`; W10's constant tensor and W11's ranges have no
# spelling there, and W12 no hand loop to check `main` against).
builds() {
    for w in $workloads; do
        for a in $arms; do
            [[ $w =~ ^w(9|12)$ && $a == hand ]] && continue
            [[ $a == main && $w =~ ^w([459]|10|11|12)$ ]] && continue
            for k in size count check; do
                [[ $k == check && $a == bare ]] || echo "$w $a $k"
            done
        done
    done
}

build() {
    local w=$1 a=$2 k=$3 kind=() source=$here/arm.cpp tree=(-I "$include" -I "$root/tests/support")
    [[ $k == count ]] && kind=(-DCOUNT)
    [[ $k == check ]] && kind=(-DCHECK)
    [[ $a == main ]] && source=$here/main_arm.cpp tree=(-I "$main_include")
    "${CXX:-clang++-22}" "${flags[@]}" "${tree[@]}" -I "$here" "${kind[@]}" -DWORKLOAD="$w" -DARM="${a^^}" "$source" -o "$out/$w-$a-$k" \
        > "$out/$w-$a-$k.log" 2>&1 || { echo "build failed: $out/$w-$a-$k.log" >&2; touch "$out/failed"; }
}

rm -f "$out/failed"
while read -r w a k; do
    while (( $(jobs -rp | wc -l) >= ${JOBS:-4} )); do wait -n || true; done
    build "$w" "$a" "$k" &
done < <(builds)
wait
[[ ! -e $out/failed ]] || exit 1

bytes() { "$size" -B "$1" | awk 'NR == 2 { print $4 }'; }
# The least of three first-run fault counts, as made or touched; and the arena's bytes a count found and touched.
least_faults() { for _ in 1 2 3; do taskset -c "$core" "$1" "$2" | awk '{ print $2 }'; done | sort -n | head -1; }
arena_found() { taskset -c "$core" "$1" touched | awk '{ print $4 }'; }

declare -A B I C M K F
details=()
for w in $workloads; do
    for a in $arms; do
        [[ -e $out/$w-$a-size ]] || continue
        B[$w,$a]=$(bytes "$out/$w-$a-size")
        line=$(taskset -c "$core" "$out/$w-$a-count") || true
        details+=("$w $a: $line")
        [[ $line == I\ * ]] || line="I skipped C – M – K – Z –" # SKIP: counting refused
        read -r _ i _ c _ m _ k _ <<< "$line"
        I[$w,$a]=$i C[$w,$a]=$c M[$w,$a]=$m K[$w,$a]=$k
        if [[ $a == bare || $a == executor ]]; then
            made=$(least_faults "$out/$w-$a-count" faults) touched=$(least_faults "$out/$w-$a-count" touched)
            found=$(arena_found "$out/$w-$a-count") kept=${K[$w,bare]:-0}
            F[$w,$a]=$((made - touched))
            # No arena found while the program keeps bytes: the count cannot tell "no faults" from "nothing touched".
            [[ $found == 0 && $kept =~ ^[0-9]+$ && $kept -gt 0 ]] && F[$w,$a]=""
            details+=("$w $a: first run's faults $made as made, $touched touched; the arena's $found bytes found and touched")
        fi
        [[ -e $out/$w-$a-check ]] && details+=("$w $a check: $("$out/$w-$a-check" || touch "$out/failed")")
    done
done

declare -A title=([w1]="W1 gain" [w2]="W2 elem" [w3]="W3 biquad" [w4]="W4 comb" [w5]="W5 fir" [w6]="W6 rowsum"
                  [w7]="W7 matmul" [w8]="W8 blur" [w9]="W9 voice" [w10]="W10 kernel" [w11]="W11 gain, ranged"
                  [w12]="W12 lanes")
cell() { echo "${1:-–}"; }
echo "| workload | B hand | B bare | B bare + copy | B \`Executor\` | B main | I hand | I bare | I bare + copy | I \`Executor\` | I main | C bare / \`Executor\` / main | M \`Executor\` / main (kept) | F bare / \`Executor\` |"
echo "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"
for w in $workloads; do
    row="| ${title[$w]}"
    for a in hand bare bare_copy executor main; do row+=" | $(cell "${B[$w,$a]:-}")"; done
    for a in hand bare bare_copy executor main; do row+=" | $(cell "${I[$w,$a]:-}")"; done
    row+=" | $(cell "${C[$w,bare]:-}") / $(cell "${C[$w,executor]:-}") / $(cell "${C[$w,main]:-}")"
    row+=" | $(cell "${M[$w,executor]:-}") / $(cell "${M[$w,main]:-}") ($(cell "${K[$w,bare]:-}"))"
    row+=" | $(cell "${F[$w,bare]:-}") / $(cell "${F[$w,executor]:-}") |"
    echo "$row"
done
echo
printf '%s\n' "${details[@]}"
echo
for f in "$out"/*-size; do
    echo "largest symbols of $(basename "$f"):"
    nm -S --size-sort -C -t d "$f" | tail -5 | cut -c 18- | awk '{ $1 += 0; print "  " $0 }'
done
[[ ! -e $out/failed ]]
