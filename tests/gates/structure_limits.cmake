# Operator amendment 06: structure is a deliverable, and it is gated.
#
# THE PROPERTY. A file is at most MAX_FILE_LINES lines, and a function is at most
# MAX_FUNCTION_LINES lines from its signature to the brace that closes it. The old tree
# (/home/user/ctg3 at c4012b8) has 102 headers totalling 44,236 lines, ten of them over a thousand,
# a 2,944-line `frontend/builder.hpp`, and 38 functions of 60 lines or more with a 1,064-line one at
# the top. Nobody decided that; it accumulated one convenient addition at a time, which is exactly
# the kind of erosion this tree writes gates for rather than intentions.
#
# THESE ARE LIMITS, NOT TARGETS, and the gate cannot tell the difference — so this is worth saying
# where somebody about to satisfy it will read it. A 58-line function that is one coherent step is
# fine. Shredding it into six 10-line functions called once each makes the code less readable, and
# it makes this gate pass. If a split does not name a concern the file actually contains, do not do
# it: the amendment names that as a finding in its own right.
#
# HOW THE FILE IS WALKED, and why no CMake list is built out of it. The obvious spelling —
# `string(REPLACE "\n" ";" ...)` then `foreach(IN LISTS)` — is wrong here, and wrong in the worst
# direction. A C++ line ends in `;` more often than not, so the text must be escaped before it is
# split, and that escaping does not survive contact with real source. Measured on this tree's own
# `sema/analyze.hpp` while this gate was being written: the list round-trip collapsed thirteen
# consecutive lines into one element, reported a 525-line file as 513 lines, and placed `infer()`
# at line 166 when its signature is at line 178. `file(STRINGS)` fails from the other side — it
# splits on semicolons WITHIN a line, and reported the same file as 530. Both errors are silent,
# and a collapsed line shortens whichever function contains it, so the failure mode of the obvious
# spelling is a false PASS. Lines are therefore peeled off the front of the string one at a time
# with FIND and SUBSTRING and never become list elements. That is quadratic in file size, which at
# a few hundred lines a file is not worth a thought.
#
# HOW A FUNCTION IS MEASURED. Comments and string and character literals are removed line by line,
# keeping the line COUNT intact — a stripped line becomes empty rather than disappearing — so that
# a brace inside prose or inside a message never moves the depth, while every number reported is
# still a line number in the real file. Brace depth is then tracked per line as opens minus closes.
# A function begins where the depth rises while not already inside one, on a statement that has a
# `)` before its `{` and does not begin with a keyword that opens some other kind of brace. It ends
# on the line that brings the depth back. A signature broken over several lines is measured from
# its FIRST line, because that is where a reader would say it starts, and a nested lambda is NOT
# measured separately, because the reader still has to hold it.
#
# WHAT IT CANNOT SEE is written down in docs/decisions.md rather than left implied here — including
# the third of the amendment's three limits, "a file declares one concern", which is a judgement and
# is gated by nothing. A gate whose blind spots are undocumented is one somebody later trusts too
# far.
#
# Run with:
#   cmake -DROOT=<dir> -DMAX_FILE_LINES=600 -DMAX_FUNCTION_LINES=60 "-DEXEMPT=<path>;<path>" \
#         -P structure_limits.cmake

if(NOT DEFINED EXEMPT)
    set(EXEMPT "")
endif()
if(NOT DEFINED MAX_FILE_LINES)
    message(FATAL_ERROR "structure gate: MAX_FILE_LINES was not given, and a limit nobody set is not a limit")
endif()
if(NOT DEFINED MAX_FUNCTION_LINES)
    message(FATAL_ERROR "structure gate: MAX_FUNCTION_LINES was not given, and a limit nobody set is not a limit")
endif()

file(GLOB_RECURSE _sources "${ROOT}/*.hpp" "${ROOT}/*.cpp")
list(LENGTH _sources _source_count)
if(_source_count EQUAL 0)
    message(FATAL_ERROR "structure gate: no sources under ${ROOT} — a gate that scanned nothing "
                        "reports the same 'passed' as one that scanned everything")
endif()

# Keywords that open a brace which is not a function body. `extern "C"` and aggregate initialisers
# need no entry: neither has a `)` immediately before its `{`.
set(_not_a_function "^[ \t]*(namespace|struct|class|enum|union|if|for|while|switch|else|do|try|catch)[ \t(<{:]")

# Accumulated as a STRING, not a list, for the same reason the lines are: a default argument or a
# `for` header inside a reported signature contains semicolons, and a list would break the message
# apart at them.
set(_offences "")
set(_offence_count 0)
set(_files_scanned 0)
set(_functions_seen 0)

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
    math(EXPR _files_scanned "${_files_scanned}+1")

    file(READ "${source}" _contents)
    # A backslash would be re-interpreted wherever a line is expanded below, and nothing about
    # brace depth depends on one. A line continuation therefore reads as a line break, which for
    # depth-counting purposes it is.
    string(REPLACE "\\" " " _contents "${_contents}")
    string(REPLACE "\r" "" _contents "${_contents}")

    # The line count a reader sees in an editor: one per newline, plus one more for a final line
    # that has none. MATCHALL of a single character produces a list of identical one-character
    # elements, so no escaping question arises.
    string(REGEX MATCHALL "\n" _newlines "${_contents}")
    list(LENGTH _newlines _line_count)
    if(NOT _contents STREQUAL "" AND NOT _contents MATCHES "\n$")
        math(EXPR _line_count "${_line_count}+1")
    endif()

    if(_line_count GREATER MAX_FILE_LINES)
        set(_offences "${_offences}\n  ${source}: the file is ${_line_count} lines, and the limit is ${MAX_FILE_LINES}")
        math(EXPR _offence_count "${_offence_count}+1")
    endif()

    set(_depth 0)
    set(_fn_depth -1)      # -1 when not inside a function
    set(_fn_start 0)
    set(_fn_name "")
    set(_stmt_start 1)
    set(_stmt_text "")
    set(_prev_ended TRUE)
    set(_in_block_comment FALSE)
    set(_number 0)
    set(_rest "${_contents}")

    while(_number LESS _line_count)
        math(EXPR _number "${_number}+1")
        string(FIND "${_rest}" "\n" _break)
        if(_break LESS 0)
            set(_raw "${_rest}")
            set(_rest "")
        else()
            string(SUBSTRING "${_rest}" 0 ${_break} _raw)
            math(EXPR _after_break "${_break}+1")
            string(SUBSTRING "${_rest}" ${_after_break} -1 _rest)
        endif()

        # ---- strip what must not be counted, keeping the line ----------------------------------
        set(_line "${_raw}")
        if(_in_block_comment)
            if(_line MATCHES "\\*/")
                string(REGEX REPLACE "^.*\\*/" "" _line "${_line}")
                set(_in_block_comment FALSE)
            else()
                set(_line "")
            endif()
        endif()
        # Block comments opened and closed on this line, then one left open.
        string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/" " " _line "${_line}")
        if(_line MATCHES "/\\*")
            string(REGEX REPLACE "/\\*.*$" "" _line "${_line}")
            set(_in_block_comment TRUE)
        endif()
        string(REGEX REPLACE "//.*$" "" _line "${_line}")
        # Literals go after comments, so an apostrophe in prose is never read as one.
        string(REGEX REPLACE "\"[^\"]*\"" "\"\"" _line "${_line}")
        string(REGEX REPLACE "'[^']*'" "''" _line "${_line}")

        string(STRIP "${_line}" _stripped)

        # ---- where the current statement began --------------------------------------------------
        if(_prev_ended AND NOT _stripped STREQUAL "")
            set(_stmt_start ${_number})
            set(_stmt_text "${_stripped}")
        elseif(NOT _stripped STREQUAL "")
            set(_stmt_text "${_stmt_text} ${_stripped}")
        endif()

        # ---- brace depth --------------------------------------------------------------------
        string(REGEX MATCHALL "{" _open_braces "${_line}")
        string(REGEX MATCHALL "}" _close_braces "${_line}")
        list(LENGTH _open_braces _opens)
        list(LENGTH _close_braces _closes)
        math(EXPR _line_after "${_depth}+${_opens}-${_closes}")

        if(_fn_depth LESS 0 AND _line_after GREATER _depth)
            # A template's angle-bracket prefix is stripped rather than treated as a disqualifying
            # keyword: excluding it would make every template function invisible to this gate.
            string(REGEX REPLACE "^[ \t]*template[ \t]*<[^>]*>[ \t]*" "" _candidate "${_stmt_text}")
            if(_candidate MATCHES "\\)[^;{}]*\\{" AND NOT _candidate MATCHES "${_not_a_function}")
                set(_fn_depth ${_depth})
                set(_fn_start ${_stmt_start})
                # Up to the LAST brace with none after it, not the first: a default argument such
                # as `= Bindings{}` contains a brace, and cutting at the first one truncates the
                # very signature the message exists to identify.
                string(REGEX REPLACE "\\{[^{}]*$" "" _fn_name "${_candidate}")
                string(STRIP "${_fn_name}" _fn_name)
                math(EXPR _functions_seen "${_functions_seen}+1")
            endif()
        elseif(_fn_depth GREATER_EQUAL 0 AND _line_after LESS_EQUAL _fn_depth)
            math(EXPR _length "${_number}-${_fn_start}+1")
            if(_length GREATER MAX_FUNCTION_LINES)
                set(_offences "${_offences}\n  ${source}:${_fn_start}: `${_fn_name}` is ${_length} lines, and the limit is ${MAX_FUNCTION_LINES}")
                math(EXPR _offence_count "${_offence_count}+1")
            endif()
            set(_fn_depth -1)
        endif()

        set(_depth ${_line_after})
        if(_stripped STREQUAL "" OR _stripped MATCHES "[;{}:]$" OR _raw MATCHES "^[ \t]*#")
            set(_prev_ended TRUE)
        else()
            set(_prev_ended FALSE)
        endif()
    endwhile()

    # The walk above is the part of this gate most likely to be quietly wrong — that is the whole
    # lesson of the list round-trip described at the top — so it says so when it is. A file whose
    # braces do not balance means the depth tracking desynchronised, and every function length
    # measured after that point is unreliable. Reporting nothing would be the false pass.
    if(NOT _depth EQUAL 0)
        message(FATAL_ERROR
            "structure gate: after reading ${source} the brace depth is ${_depth} rather than 0. "
            "The gate cannot measure this file, and reporting it as clean would be a false pass. "
            "Most likely a macro or a raw string literal that this scanner does not understand — "
            "see the blind spots recorded in docs/decisions.md.")
    endif()
endforeach()

if(_offence_count GREATER 0)
    message(FATAL_ERROR
        "structure limits (operator amendment 06):${_offences}\n"
        "A file that wants more than ${MAX_FILE_LINES} lines is split into files named for the "
        "concerns it turned out to hold — not into `<name>_2.hpp`. A function that wants more than "
        "${MAX_FUNCTION_LINES} is usually several steps that already have names in its own comments; "
        "that is the decomposition, and it does not have to be invented. Do not split something "
        "coherent merely to lower a number: that is a finding too, and this gate cannot see it.")
endif()

message(STATUS "structure limits: ${_files_scanned} files under ${ROOT}, ${_functions_seen} functions, "
               "none over ${MAX_FILE_LINES} lines or ${MAX_FUNCTION_LINES} lines")
