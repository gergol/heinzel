#!/usr/bin/env bash
# What the extent solver costs at `gradients()`'s `infer` — the solver's SECOND call site — and whether
# that cost stays FLAT per differentiable section (M7.6 re-take of M2.8, standing rule S-6).
#
# Compiles tests/compile_time/gradient_cost.cpp once per (rung, regime). Syntax-only and clang invoked
# directly, so no cache and no code generation are in the number. Several runs per point, minimum
# reported, because noise on a shared host only ever adds time.
#
# The two regimes are the two ways gradients() reaches the solver:
#   literal   every extent is a number — gradients() infers a fully-literal augmented graph.
#   unbound   symbolic extents, nothing bound — the R4-2 mean reciprocal runs and the resolving pass
#             canonicalises the appended adjoint nodes' symbolic shapes.
#
# HOW TO READ THE OUTPUT. Compare the MARGINAL column across rungs, not the totals. A marginal cost
# that stays about the same is a linear design that is merely large; a marginal cost that WIDENS as the
# chain grows is super-linear, and no budget ever fixes that (amendment 03).
#
# Usage: tools/measure_gradient_cost.sh [compiler]
set -uo pipefail

CXX="${1:-clang++}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$HERE/tests/compile_time/gradient_cost.cpp"
# The ladder climbs to 240–300 values: today's capacities, whatever the headers' defaults (docs/limitations.md).
CAPACITIES=(-DHEINZEL_MAX_VALUES=320 -DHEINZEL_MAX_NODES=320 -DHEINZEL_MAX_IO=32 -DHEINZEL_MAX_DIM_DECLS=8
            -DHEINZEL_MAX_STMTS=512 -DHEINZEL_MAX_TREE_NODES=3584 -DHEINZEL_MAX_BUFFERS=96 -DHEINZEL_MAX_STAGES=16
            -DHEINZEL_MAX_STAGE_STMTS=128)
FLAGS=(-std=c++2c -fconstexpr-steps=268435456 -I"$HERE/include" -I"$HERE/tests/compile_time"
       -I"$HERE/tests/support" -fsyntax-only "${CAPACITIES[@]}")
REPEATS=9
RUNGS=(4 8 12 16 20 24)

min_ms() { printf "%s\n" "$@" | sort -n | head -1; }

time_one() { # $1 = sections, $2 = regime number
    local runs=()
    for _ in $(seq "$REPEATS"); do
        local t0 t1
        t0=$(date +%s%N)
        "$CXX" "${FLAGS[@]}" -DHEINZEL_MEASURE_SECTIONS="$1" -DHEINZEL_MEASURE_REGIME="$2" "$SRC" \
            || { echo "COMPILE FAILED: sections=$1 regime=$2" >&2; return 1; }
        t1=$(date +%s%N)
        runs+=( $(( (t1 - t0) / 1000000 )) )
    done
    min_ms "${runs[@]}"
}

echo "compiler: $($CXX --version | head -1)"
echo "source:   $SRC"
echo "repeats:  $REPEATS (minimum reported), -fsyntax-only, no cache"
echo "rungs:    ${RUNGS[*]} sections, equally spaced so the marginal columns are comparable"
echo

for regime in 0 1; do
    case $regime in
        0) name="literal (augmented graph fully literal)" ;;
        1) name="unbound (symbolic, R4-2 reciprocal + canonicalise)" ;;
    esac
    echo "regime $regime — $name"
    prev=""
    prev_n=""
    for n in "${RUNGS[@]}"; do
        ms=$(time_one "$n" "$regime") || exit 1
        if [ -n "$prev" ]; then
            marginal=$(awk "BEGIN{printf \"%.2f\", ($ms-$prev)/($n-$prev_n)}")
            printf "  %2d sections: %4d ms   (marginal %s ms/section)\n" "$n" "$ms" "$marginal"
        else
            printf "  %2d sections: %4d ms\n" "$n" "$ms"
        fi
        prev="$ms"
        prev_n="$n"
    done
    echo
done
