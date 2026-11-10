# Milestone M2.7: no extent is read as a plain integer.
#
# An axis extent is ENCODED — a non-negative value is a size, a value at or below -2 is a symbol
# whose id is -e-2 (`ir/vocab.hpp`). The encoding is a fact of that one header. Everywhere else an
# extent is carried as `Extent` and every question about it is asked through a named helper:
# `is_symbol`, `is_literal`, `symbol_of`, `literal_of`, `same_extent`, `extent_at`, `set_extent`.
#
# WHY THIS IS A GATE AND NOT A REVIEW NOTE. A raw read is invisible until the first symbolic value
# reaches it. `e == 1` is right today only because a symbol happens to encode negative; `e * 2` is
# silently wrong the moment `e` is a symbol. Neither is a compile error and neither shows up in any
# test until a graph declares a symbol — and M4 writes twenty-nine operations before M5 makes one
# flow. This is old Phase 3's audit converted into something that runs unattended.
#
# Three patterns, each of which is ALWAYS a defect outside the vocabulary header:
#
#   1. a field or variable whose name says it holds an extent, declared as a bare integer. The name
#      must END in extent(s) or dim(s): `kMaxDimDecls` is a count of declared dimensions and `rank` is
#      a count of axes, and a count really is a plain integer. Only the thing that holds
#      an encoded extent is the offence;
#   2. arithmetic applied to a `dims[...]` read — the encoding is not linear, so this is never right;
#   3. a `dims[...]` read compared against a numeric literal — that assumes the literal encoding.
#
# EXEMPT is a list of paths; a file is skipped when it IS one or lives under one. The vocabulary
# header is exempt because it defines the encoding, and the deliberate-offender fixture directory is
# exempt because its whole job is to be rejected by this gate when it is pointed at separately.
#
# Run with:  cmake -DROOT=<dir> "-DEXEMPT=<path>;<path>" -P extents_are_not_plain_integers.cmake

if(NOT DEFINED EXEMPT)
    set(EXEMPT "")
endif()

file(GLOB_RECURSE _sources "${ROOT}/*.hpp" "${ROOT}/*.cpp")
list(LENGTH _sources _source_count)
if(_source_count EQUAL 0)
    message(FATAL_ERROR "extents gate: no sources under ${ROOT} — a gate that scanned nothing "
                        "reports the same 'passed' as one that scanned everything")
endif()

set(_offences "")

foreach(source IN LISTS _sources)
    get_filename_component(_absolute "${source}" ABSOLUTE)
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
    # Prose about extents is fine and is encouraged, so comments go first.
    string(REGEX REPLACE "//[^\n]*" "" code "${contents}")
    string(REPLACE ";" "\;" code "${code}")
    string(REPLACE "\n" ";" lines "${code}")

    foreach(line IN LISTS lines)
        # 1 — an extent declared as a bare integer.
        if(line MATCHES "(^|[^A-Za-z_:>])(int|unsigned|long|std::int32_t|std::uint32_t|std::int64_t|std::size_t)[ \t]+[A-Za-z_][A-Za-z_0-9]*([Ee]xtents?|[Dd]ims?)([^A-Za-z_0-9]|$)")
            list(APPEND _offences "${source}: an extent declared as a bare integer:${line}")
        endif()
        if(line MATCHES "dims\\[")
            # 2 — arithmetic on a read.
            if(line MATCHES "\\][ \t]*[-+*/%]")
                list(APPEND _offences "${source}: arithmetic on an encoded extent:${line}")
            endif()
            # 3 — compared against a numeric literal.
            if(line MATCHES "\\][ \t]*(==|!=|<=|>=|<|>)[ \t]*-?[0-9]")
                list(APPEND _offences "${source}: an encoded extent compared against a number:${line}")
            endif()
        endif()
    endforeach()
endforeach()

if(_offences)
    string(REPLACE ";" "\n  " _report "${_offences}")
    message(FATAL_ERROR
        "an axis extent is being handled as a plain integer:\n  ${_report}\n"
        "An extent is encoded (ir/vocab.hpp): non-negative is a size, <= -2 is a symbol. Carry it as "
        "`Extent` and ask `is_literal` / `literal_of` / `is_symbol` / `symbol_of` / `same_extent`, "
        "and read and write it with `extent_at` / `set_extent`. Arithmetic on the encoding, and "
        "comparing it against a number, are both silently wrong as soon as a symbol reaches them.")
endif()

message(STATUS "gate passed: ${_source_count} files, no extent handled as a plain integer")
