#!/usr/bin/env bash
# What a canonical renumbering pass costs at compile time (plan/TRACEABILITY.md Q-2).
#
# Compiles tests/compile_time/renumber_cost.cpp twice per rung — once building the cascade the way
# the library does, once building it and then walking it — and reports the difference. Syntax-only
# and clang invoked directly, so no cache and no code generation are in the number. Five runs per
# point, median reported, because a single wall-clock reading on a shared host is noise.
#
# Usage: tools/measure_renumber_cost.sh [compiler]
set -uo pipefail

CXX="${1:-/home/user/.local/opt/llvm-22/bin/clang++}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$HERE/tests/compile_time/renumber_cost.cpp"
# The ladder climbs to 240–300 values: today's capacities, whatever the headers' defaults (docs/limitations.md).
CAPACITIES=(-DHEINZEL_MAX_VALUES=320 -DHEINZEL_MAX_NODES=320 -DHEINZEL_MAX_IO=32 -DHEINZEL_MAX_DIM_DECLS=8
            -DHEINZEL_MAX_STMTS=512 -DHEINZEL_MAX_TREE_NODES=3584 -DHEINZEL_MAX_BUFFERS=96 -DHEINZEL_MAX_STAGES=16
            -DHEINZEL_MAX_STAGE_STMTS=128)
FLAGS=(-std=c++2c -fconstexpr-steps=268435456 -I"$HERE/include" -I"$HERE/tests/compile_time" -fsyntax-only "${CAPACITIES[@]}")
REPEATS=9

min_ms() { printf "%s\n" "$@" | sort -n | head -1; }

time_one() { # $1 = sections, $2 = "renumber" or "baseline"
    local extra=()
    [ "$2" = renumber ] && extra=(-DHEINZEL_MEASURE_RENUMBER)
    local runs=()
    for _ in $(seq "$REPEATS"); do
        local t0 t1
        t0=$(date +%s%N)
        "$CXX" "${FLAGS[@]}" -DHEINZEL_MEASURE_SECTIONS="$1" "${extra[@]}" "$SRC" || { echo "COMPILE FAILED: $1 $2" >&2; return 1; }
        t1=$(date +%s%N)
        runs+=( $(( (t1 - t0) / 1000000 )) )
    done
    min_ms "${runs[@]}"
}

echo "compiler: $($CXX --version | head -1)"
echo "source:   $SRC"
echo "repeats:  $REPEATS (minimum reported: noise only ever adds time), -fsyntax-only, no cache"
echo
printf '%-10s %12s %12s %10s %9s\n' sections baseline renumber delta factor
for n in 3 6 12 18 24 30; do
    b=$(time_one "$n" baseline)   || continue
    r=$(time_one "$n" renumber)   || continue
    printf '%-10s %10s ms %10s ms %8s ms %8sx\n' \
        "$n" "$b" "$r" "$((r - b))" "$(awk -v a="$r" -v b="$b" 'BEGIN{printf "%.2f", a/b}')"
done
