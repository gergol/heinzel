# Milestone M8.4 / standing rule S-2 (amendment 02 A2-2): one spelling everywhere, test graphs
# included — the MECHANICAL half.
#
# THE PROPERTY. There is one way to add a node or a value to a graph by hand, and it is
# `tests/support/hand_built_graph.hpp`. That header says so itself: "Shared rather than copied into
# each test file: two copies of 'how a test adds a node' drift, and then two tests that look like
# they are testing the same thing are not." A user-facing program (a tutorial, an example, a parity
# `_here`) does not add nodes by hand at all — it writes the language (`frontend/graph_builder.hpp`'s
# `Expr` spellings, or the `notation/` headers), which lowers to nodes for it. So across the whole
# test tree, the raw idiom for adding to a graph — `g.nodes.push_back(...)` / `g.values.push_back(...)`
# — belongs in exactly one file, and that file is WRITING. A second occurrence is a second spelling
# of the one thing this rule exists to keep single.
#
# WHY A PROXY, and which one. "One spelling of the language" in full is not mechanical — whether a
# program says `clamp(x, lo, hi)` or `min(max(x, lo), hi)`, `a + b` or a named add, is a reading a
# machine cannot make without a semantic model, and M8.4 says so ("a check enforces whatever part of this is
# mechanical, and the part that is not is recorded as reviewed"). What IS mechanical is the boundary
# this gate draws: the raw graph-mutation idiom (`.nodes.push_back` / `.values.push_back`) is the one
# way to build a graph that BYPASSES both the language and the shared hand-built helper, and it is
# exactly the drift `hand_built_graph.hpp` was extracted to prevent. Confining it to that one file is
# a check; the alias-level review is Decision <recorded, dated> in docs/decisions.md. The spellings a user reads
# (no retired one, no `detail::`) are `public_spelling.cmake`'s (Plan/archive/14-dsl L9).
#
# WHAT IT DOES NOT CATCH, said plainly. Constructing a throwaway `Node{op::Add}` to feed a predicate
# (as `tiers_test.cpp` does for `compute`) is not adding to a graph and is not matched —
# the idiom keyed on is `.push_back` onto a graph's `nodes`/`values`, which is the act of building,
# and onto its `ranges`, which a declaration or a `promise` records and the helper's `add_range` spells
# (Plan/26-value-ranges). Registering a named input/output (`g.inputs.push_back`) is graph metadata,
# not a node or a value, and is likewise not the concern.
#
# EXEMPT is a list of paths; a file is skipped when it IS one or lives under one. The exemptions are
# the recorded non-mechanical half — files where the raw IR is the SUBJECT under test, not a graph
# spelled a second way: `unit/graph_value_test.cpp` (the graph-as-a-value property needs the raw
# aggregates it is asserting are plain values), `unit/inference_test.cpp` (injects a `Const` value
# with a payload `add_value` does not set, to test const inference), and `compile_time/
# canonical_renumber.hpp` (a measured-but-not-adopted renumber candidate that copies a graph node by
# node; guard-only, outside the default build). `gates/fixtures` is exempt because the would-object
# fixture is deliberately an offender.
#
# Run with:  cmake -DROOT=<dir> -DWRITING=<file> "-DEXEMPT=<path>;<path>" -P one_spelling.cmake

if(NOT DEFINED EXEMPT)
    set(EXEMPT "")
endif()

file(GLOB_RECURSE _sources "${ROOT}/*.hpp" "${ROOT}/*.cpp")
list(LENGTH _sources _source_count)
if(_source_count EQUAL 0)
    message(FATAL_ERROR "one-spelling gate: no sources under ${ROOT} — a gate that scanned nothing "
                        "reports the same 'passed' as one that scanned everything")
endif()

if(DEFINED WRITING)
    get_filename_component(_writing "${WRITING}" ABSOLUTE)
else()
    set(_writing "")
endif()

set(_offences "")

foreach(source IN LISTS _sources)
    get_filename_component(_absolute "${source}" ABSOLUTE)
    if(_writing AND _absolute STREQUAL _writing)
        continue()
    endif()
    set(_skip FALSE)
    foreach(exempt IN LISTS EXEMPT)
        get_filename_component(_exempt_absolute "${exempt}" ABSOLUTE)
        string(FIND "${_absolute}/" "${_exempt_absolute}/" _at) # a prefix, read literally: a path is no regex
        if(_at EQUAL 0)
            set(_skip TRUE)
        endif()
    endforeach()
    if(_skip)
        continue()
    endif()

    file(READ "${source}" contents)
    # Prose describing the idiom is fine and is encouraged, so comments go first.
    string(REGEX REPLACE "//[^\n]*" "" code "${contents}")
    string(REGEX REPLACE "/\\*([^*]|\\*[^/])*\\*/" "" code "${code}")

    if(code MATCHES "\\.(nodes|values|ranges)\\.push_back")
        string(REGEX MATCH "\\.(nodes|values|ranges)\\.push_back" offender "${code}")
        list(APPEND _offences "${source}: adds to a graph with ${offender}")
    endif()
endforeach()

if(_offences)
    string(REPLACE ";" "\n  " _report "${_offences}")
    message(FATAL_ERROR
        "how a test adds a node or value to a graph is written in exactly one place, and that place "
        "is ${WRITING}:\n  ${_report}\n"
        "A user-facing program writes the language (frontend Expr / notation), which lowers to nodes; "
        "a test that needs a graph the language cannot spell yet calls the shared hand-built helper. "
        "Adding to a graph inline here is a second copy of 'how a test adds a node' — the drift "
        "hand_built_graph.hpp was extracted to prevent. If the raw IR is genuinely the SUBJECT under "
        "test, add the file to this gate's EXEMPT list with its reason, which is the recorded "
        "non-mechanical half of M8.4.")
endif()

message(STATUS "one spelling: ${_source_count} files, only ${WRITING} adds to a graph by hand")
