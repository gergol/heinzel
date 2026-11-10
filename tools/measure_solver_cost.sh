#!/usr/bin/env bash
# What the extent solver costs at compile time, and — the question that actually decides anything —
# whether that cost stays FLAT per graph section (milestone M2.8, standing rule S-6).
#
# Compiles tests/compile_time/solver_cost.cpp once per (rung, regime). Syntax-only and clang invoked
# directly, so no cache and no code generation are in the number. Several runs per point, minimum
# reported, because noise on a shared host only ever adds time.
#
# The three regimes are the solver's three call sites:
#   literal   every extent is a number — the solver is constructed and never asked anything. This is
#             what almost every graph in this tree does, so it is the cost paid by everybody.
#   bound     symbolic extents with a binding — seeding, one unify per section, and the resolving
#             pass substituting sizes back into every shape.
#   unbound   symbolic extents with no binding — the unifications happen and nothing is pinned, so
#             the resolving pass canonicalises instead of substituting.
#
# HOW TO READ THE OUTPUT. Compare the MARGINAL column across rungs, not the totals. A marginal cost
# that stays about the same is a linear design that is merely large, and raising a dial pays for it.
# A marginal cost that WIDENS as the chain grows is super-linear, and no budget ever fixes that —
# raising one buys a constant factor and moves the cliff a step or two (amendment 03).
#
# Usage: tools/measure_solver_cost.sh [compiler]
set -uo pipefail

CXX="${1:-/home/user/.local/opt/llvm-22/bin/clang++}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$HERE/tests/compile_time/solver_cost.cpp"
# The ladder climbs to 240–300 values: today's capacities, whatever the headers' defaults (docs/limitations.md).
CAPACITIES=(-DHEINZEL_MAX_VALUES=320 -DHEINZEL_MAX_NODES=320 -DHEINZEL_MAX_IO=32 -DHEINZEL_MAX_DIM_DECLS=8
            -DHEINZEL_MAX_STMTS=512 -DHEINZEL_MAX_TREE_NODES=3584 -DHEINZEL_MAX_BUFFERS=96 -DHEINZEL_MAX_STAGES=16
            -DHEINZEL_MAX_STAGE_STMTS=128)
FLAGS=(-std=c++2c -fconstexpr-steps=268435456 -I"$HERE/include" -I"$HERE/tests/compile_time" -fsyntax-only "${CAPACITIES[@]}")
# Nine runs per point and EQUALLY SPACED rungs. Both matter for the question being asked: the
# marginal cost per section is a difference between two noisy numbers, so it is far noisier than
# either, and intervals of different widths are not comparable to each other at all. An early
# reading of this at five repeats and rungs 3,6,12,18,24 produced a first interval of -0.33 ms per
# section, which is not a measurement of anything.
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

for regime in 0 1 2; do
    case $regime in
        0) name="literal   (constructed, never asked)" ;;
        1) name="bound     (seed, unify, substitute)" ;;
        2) name="unbound   (unify, canonicalise)" ;;
    esac
    echo "regime $regime — $name"
    printf '  %-10s %10s %14s\n' sections compile 'marginal/section'
    previous_ms=""
    previous_n=""
    for n in "${RUNGS[@]}"; do
        ms=$(time_one "$n" "$regime") || continue
        if [ -n "$previous_ms" ]; then
            marginal=$(awk -v a="$ms" -v b="$previous_ms" -v n="$n" -v p="$previous_n" \
                           'BEGIN{printf "%.2f", (a-b)/(n-p)}')
            printf '  %-10s %8s ms %11s ms\n' "$n" "$ms" "$marginal"
        else
            printf '  %-10s %8s ms %14s\n' "$n" "$ms" "-"
        fi
        previous_ms=$ms
        previous_n=$n
    done
    echo
done
