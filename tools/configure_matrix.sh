#!/usr/bin/env bash
# Milestone M8.6 — every build option this tree declares configures, and the ones that produce an
# artifact build. The exit condition is a property of the CMake surface, not of any one graph, so the
# check is a script that drives cmake over each declared option value, exactly as the measurement
# subjects (tools/measure_*.sh) and the compile-time guard are driven by hand rather than folded into
# ctest — a matrix of clean configures does not belong in the inner loop that runs on every commit.
#
# WHAT IT PROVES. For each option in CMakeLists.txt (HEINZEL_BUILD_TESTS, HEINZEL_BUILD_COMPILE_TIME_GUARD,
# HEINZEL_CXX_STANDARD, HEINZEL_CONSTEXPR_STEPS): the tree configures at each value it declares, and the
# values that yield a build target build. It does NOT re-run the full ctest suite per value — that is the
# boundary's job under the default configuration; here the question is only "does the option configure
# and build", which is what M8.6 asks and no more.
#
# WHAT IT DOES NOT PROVE. That an absent option (one of the old tree's twenty-two this tree does not yet
# declare) is correctly absent — that is a reading of docs/decisions.md's Decision 18 accounting, not a
# thing a configure can check. And it does not build every value's full suite; a value that configures but
# whose suite regresses is caught by the boundary run, not here.
#
# Usage:  tools/configure_matrix.sh [compiler]     (compiler defaults to clang++ on PATH — amendment 01)
# Exit 0 iff every declared option configured, and the artifact-producing values built.
set -uo pipefail

CXX="${1:-clang++}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/heinzel-m8s6-matrix.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
export CCACHE_BASEDIR="$HERE"

fail=0
pass=0

# configure_only LABEL -Dopt=val ...   — configure must succeed, no build expected.
configure_only() {
    local label="$1"; shift
    local dir="$WORK/${label//[^A-Za-z0-9_]/_}"
    if cmake -S "$HERE" -B "$dir" -DCMAKE_CXX_COMPILER="$CXX" "$@" >"$dir.log" 2>&1; then
        echo "PASS configure  $label"; pass=$((pass+1))
    else
        echo "FAIL configure  $label   (see $dir.log)"; fail=$((fail+1)); tail -5 "$dir.log"
    fi
}

# configure_rejects LABEL -Dopt=val ...   — configure must FAIL (a declared value the tree rejects on
# purpose, e.g. a C++ standard that is neither 26 nor 23). A rejection that stops configuring is the
# pass; a configure that succeeds here is the defect.
configure_rejects() {
    local label="$1"; shift
    local dir="$WORK/${label//[^A-Za-z0-9_]/_}"
    if cmake -S "$HERE" -B "$dir" -DCMAKE_CXX_COMPILER="$CXX" "$@" >"$dir.log" 2>&1; then
        echo "FAIL reject     $label   (configured, but should have been rejected)"; fail=$((fail+1))
    else
        echo "PASS reject     $label   (rejected at configure, as declared)"; pass=$((pass+1))
    fi
}

# configure_and_build LABEL TARGET -Dopt=val ...   — configure then build TARGET (empty TARGET = all).
configure_and_build() {
    local label="$1" target="$2"; shift 2
    local dir="$WORK/${label//[^A-Za-z0-9_]/_}"
    if ! cmake -S "$HERE" -B "$dir" -DCMAKE_CXX_COMPILER="$CXX" "$@" >"$dir.log" 2>&1; then
        echo "FAIL configure  $label   (see $dir.log)"; fail=$((fail+1)); tail -5 "$dir.log"; return
    fi
    local build_args=(--build "$dir" -j "$(nproc)")
    [ -n "$target" ] && build_args+=(--target "$target")
    if cmake "${build_args[@]}" >>"$dir.log" 2>&1; then
        echo "PASS build      $label${target:+ (target $target)}"; pass=$((pass+1))
    else
        echo "FAIL build      $label   (see $dir.log)"; fail=$((fail+1)); tail -8 "$dir.log"
    fi
}

echo "== M8.6 build-option configure matrix (compiler: $CXX) =="

# HEINZEL_BUILD_TESTS — ON is the default surface built at the boundary; here prove OFF configures and
# builds too (an INTERFACE library with no test targets: a consumer that only wants the headers).
configure_and_build "tests=OFF (library only)"        "" -DHEINZEL_BUILD_TESTS=OFF

# HEINZEL_CXX_STANDARD — 26 is the default; 23 is the declared portable floor. Both build the full suite.
configure_and_build "std=23 (portable floor)"         "" -DHEINZEL_CXX_STANDARD=23
configure_rejects   "std=99 (not 26 or 23)"              -DHEINZEL_CXX_STANDARD=99

# HEINZEL_BUILD_COMPILE_TIME_GUARD — OFF by default (guard out of the default build and out of ctest,
# success condition 3). ON must build the guard target on demand and STILL register zero ADDITIONAL
# ctests. "Zero additional" is the honest form of the property: the guard block adds only
# EXCLUDE_FROM_ALL executables and no add_test(), so the option must leave the registered-test COUNT
# unchanged. We compare the total against a guard=OFF baseline rather than grepping test names — an
# earlier version matched names against 'guard|_cost' and false-positived once the suite grew ordinary
# tests carrying "cost" (build_refuses_row_with_no_cost, parity_cost_model_flip). ctest -N's total is
# reported reliably as "Total Tests: N" even when per-config executable paths cannot be resolved, so
# that line, not the per-test listing, is the count we trust.
configure_and_build "guard=ON (build guard target)"   "heinzel_compile_time_guard" -DHEINZEL_BUILD_COMPILE_TIME_GUARD=ON
_registered_test_count() { ctest --test-dir "$1" -N 2>/dev/null | sed -n 's/^Total Tests: *\([0-9][0-9]*\).*/\1/p' | tail -1; }
guard_on_dir="$WORK/guard_ON__build_guard_target_"
guard_off_dir="$WORK/guard_OFF_baseline"
if cmake -S "$HERE" -B "$guard_off_dir" -DCMAKE_CXX_COMPILER="$CXX" -DHEINZEL_BUILD_COMPILE_TIME_GUARD=OFF >"$guard_off_dir.log" 2>&1 && [ -d "$guard_on_dir" ]; then
    n_on="$(_registered_test_count "$guard_on_dir")"
    n_off="$(_registered_test_count "$guard_off_dir")"
    if [ -n "$n_off" ] && [ -n "$n_on" ] && [ "$n_on" = "$n_off" ]; then
        echo "PASS guard=ON registers zero additional ctests ($n_off, unchanged by the option — success condition 3)"; pass=$((pass+1))
    else
        echo "FAIL guard=ON changed the registered-test count (OFF=$n_off, ON=$n_on) — the guard must register no ctest"; fail=$((fail+1))
    fi
else
    echo "FAIL guard check: could not configure the guard=OFF baseline, or the guard=ON dir is missing"; fail=$((fail+1))
fi

# HEINZEL_CONSTEXPR_STEPS — a freely chosen ceiling (amendment 03). A raised value must configure; the
# flag it feeds is compiler-specific and chosen in CMakeLists.txt, so configuring proves it is accepted.
configure_only "constexpr steps raised (2^29)"           -DHEINZEL_CONSTEXPR_STEPS=536870912

echo "== matrix: $pass passed, $fail failed =="
[ "$fail" -eq 0 ] || exit 1
