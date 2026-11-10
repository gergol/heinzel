# Plan/archive/14-dsl L9 (DS-19): what a user reads is written in the DSL's one spelling — the MECHANICAL half of "one
# spelling" (Decision 28) for the files a user reads: the examples, the tutorials' and parity ports' `_here`
# programs, the benchmarks.
#
# THE PROPERTY. The old spellings L9 retired — a context threaded through every leaf, a Variable's methods, a delay
# by name, the `notation::` builders, the dims and specialize steps —, the planner's `Draft` and `draft_of` (now
# `Schedule` and `schedule_of`), the loop program's printer `loops::print` (a user writes `print<g>()`, which shows the
# graph and the schedule too: Plan/28-dsl-proposals/04 C3) and the internals beneath the DSL (`detail`) appear in none
# of those files. A program there writes `Input`, `Output`, `y[n - k]`, the members and the public notation; a graph no
# declaration can say is a test's business (Plan/archive/14-dsl/02 §3, DS-18) and lives elsewhere.
#
# MATCHING. A name matches only as a whole word (CMake's regex has no `\b`): at each end of the name that is a
# word character, the neighbour must not be one. A name that begins with `.` or ends with `(`, as `.is(` does, is
# matched literally on that side, so `y.is(` is caught and `fir_delayed(` is not; blanks are allowed around a `(`
# and wherever the name has a space. `detail::` alone is not matched after a `:`, so another library's
# `kfr::detail::` passes — `heinzel::detail` and `hz::detail` are names of their own, and so are `namespace detail`
# and `= detail`, which reach it without `detail::`. `instantiate(` is not matched after a `.` or a `>`: another
# library's member (`dsp->instantiate(48000)`, Faust's) is no call of ours. In C++ files comments are stripped first,
# in one pass from the left that steps over string literals, a block comment replaced by its newlines so line numbers
# stay; prose in any other file counts (`examples/README.md`, the root `README.md`).
#
# Run with:  cmake "-DFILES=<glob>;<glob>" "-DTREES=<dir>;<dir>" "-DEXEMPT=<path>" -P public_spelling.cmake
# FILES are globs of files (not recursive); TREES are scanned recursively for C++, Markdown and CMake files.

set(_refused "BuildContext" ".previous(" ".is(" "delayed(" "finish(where" "input(where" "notation::" "append_unary"
             "detail::" "heinzel::detail" "hz::detail" "namespace detail" "= detail" "finish_gradients"
             "declare_dim" "extent_of" "with_sample_rate" "with_default_precision" "set_shape" "set_dtype"
             "InputAnnot" "Draft" "draft_of" "units::millisecond" "units::microsecond"
             "set_default_precision" "instantiate(" "loops::print")

set(_sources "")
foreach(glob IN LISTS FILES)
    file(GLOB _matched "${glob}")
    list(APPEND _sources ${_matched})
endforeach()
foreach(tree IN LISTS TREES)
    file(GLOB_RECURSE _matched "${tree}/*.cpp" "${tree}/*.hpp" "${tree}/*.md" "${tree}/CMakeLists.txt")
    list(APPEND _sources ${_matched})
endforeach()
set(_kept "")
foreach(source IN LISTS _sources)
    get_filename_component(_absolute "${source}" ABSOLUTE) # a glob keeps the `..` of its root; the exemption has none
    set(_skip FALSE)
    foreach(exempt IN LISTS EXEMPT)
        get_filename_component(_exempt_absolute "${exempt}" ABSOLUTE)
        string(FIND "${_absolute}/" "${_exempt_absolute}/" _at) # a prefix, read literally: a path is no regex
        if(_at EQUAL 0)
            set(_skip TRUE)
        endif()
    endforeach()
    if(NOT _skip)
        list(APPEND _kept "${_absolute}")
    endif()
endforeach()
list(LENGTH _kept _source_count)
if(_source_count EQUAL 0)
    message(FATAL_ERROR "public-spelling gate: no sources — a gate that scanned nothing reports the same 'passed' "
                        "as one that scanned everything")
endif()

# A name's regex: escaped, blanks allowed around a `(` and where the name has a space, a word boundary at each end
# that is a word character (`detail::` alone also not after a `:`, `instantiate(` not after a member access, `= detail`
# not before a `:`).
function(name_regex name out)
    string(REPLACE "." "\\." regex "${name}")
    string(REPLACE " " "[ \t\r\n]*" regex "${regex}") # before the `(`, whose blanks this would rewrite
    string(REPLACE "(" "[ \t\r\n]*\\([ \t\r\n]*" regex "${regex}")
    if(name STREQUAL "detail::")
        set(regex "[^A-Za-z0-9_:]${regex}")
    elseif(name STREQUAL "instantiate(")
        set(regex "[^A-Za-z0-9_.>]${regex}")
    elseif(name MATCHES "^[A-Za-z0-9_]")
        set(regex "[^A-Za-z0-9_]${regex}")
    endif()
    if(name STREQUAL "= detail") # an alias of it, not `= detail::x`
        set(regex "${regex}[^A-Za-z0-9_:]")
    elseif(name MATCHES "[A-Za-z0-9_]$")
        set(regex "${regex}[^A-Za-z0-9_]")
    endif()
    set(${out} "${regex}" PARENT_SCOPE)
endfunction()

# The C++ text with its comments removed, from the left: whichever of `//`, `/*`, a string, a raw string or a character
# comes first decides, so a URL in a block comment, a `/*` in a string or a `'"'` cannot open or close one. A `'` or an
# `R"` right after a word character is not a literal (`48'000`, `FOR"`). A comment leaves its newlines, and a `//`
# comment continued by a backslash runs on to the next line.
function(strip_comments text out)
    set(kept "")
    set(scanning 1) # a variable: in script mode `while(TRUE)` needs a policy set
    while(scanning)
        string(FIND "${text}" "//" at_line)
        string(FIND "${text}" "/*" at_block)
        string(FIND "${text}" "\"" at_quote)
        string(FIND "${text}" "'" at_char)
        string(FIND "${text}" "R\"" at_raw)
        set(at -1)
        foreach(candidate IN ITEMS ${at_line} ${at_block} ${at_quote} ${at_char} ${at_raw})
            if(candidate GREATER -1 AND (at EQUAL -1 OR candidate LESS at))
                set(at ${candidate})
            endif()
        endforeach()
        if(at EQUAL -1)
            set(scanning 0)
            continue()
        endif()
        string(SUBSTRING "${text}" 0 ${at} before)
        string(SUBSTRING "${text}" ${at} -1 text)
        if(at LESS 3) # the three characters before `at`, from what is kept when the text has fewer
            string(LENGTH "${kept}" kept_length)
            set(tail "${kept}${before}")
            if(kept_length GREATER 3)
                math(EXPR from "${kept_length} - 3")
                string(SUBSTRING "${kept}" ${from} -1 tail)
                string(APPEND tail "${before}")
            endif()
        else()
            math(EXPR from "${at} - 3")
            string(SUBSTRING "${before}" ${from} 3 tail)
        endif()
        string(APPEND kept "${before}")
        set(after_word FALSE) # a word ends before `at`, and it is no encoding prefix (`u8'`, `L'`, `LR"`)
        if(tail MATCHES "[A-Za-z0-9_]$" AND NOT tail MATCHES "(^|[^A-Za-z0-9_])(u8|[uUL])$")
            set(after_word TRUE)
        endif()
        if(at EQUAL at_raw AND NOT after_word AND text MATCHES "^R\"([^ ()\\\\\t\n]*)\\(")
            set(close ")${CMAKE_MATCH_1}\"")
            string(LENGTH "${CMAKE_MATCH_0}" open)
            string(SUBSTRING "${text}" ${open} -1 body)
            string(FIND "${body}" "${close}" end)
            if(end EQUAL -1)
                string(APPEND kept "${text}")
                set(text "")
            else()
                string(LENGTH "${close}" close_length)
                math(EXPR length "${open} + ${end} + ${close_length}")
                string(SUBSTRING "${text}" 0 ${length} literal)
                string(SUBSTRING "${text}" ${length} -1 text)
                string(APPEND kept "${literal}")
            endif()
        elseif((at EQUAL at_quote OR at EQUAL at_char) AND NOT (at EQUAL at_char AND after_word))
            string(SUBSTRING "${text}" 0 1 mark) # a literal stays, escapes included; no pattern recurses per character
            string(SUBSTRING "${text}" 1 -1 text)
            string(APPEND kept "${mark}")
            set(reading 1) # run to the next mark or newline; an odd count of backslashes before it escapes it
            while(reading)
                set(run "") # MATCHES, not REGEX MATCH: an empty match is an error there
                if(mark STREQUAL "'" AND text MATCHES "^[^'\n]+")
                    set(run "${CMAKE_MATCH_0}")
                elseif(mark STREQUAL "\"" AND text MATCHES "^[^\"\n]+")
                    set(run "${CMAKE_MATCH_0}")
                endif()
                string(LENGTH "${run}" length)
                set(slash_count 0)
                if(run MATCHES "\\\\+$")
                    string(LENGTH "${CMAKE_MATCH_0}" slash_count)
                endif()
                math(EXPR escaped "${slash_count} % 2")
                string(SUBSTRING "${text}" ${length} 1 next)
                if(next STREQUAL "" OR (next STREQUAL "\n" AND escaped EQUAL 0))
                    set(reading 0) # the end of the text, or an unescaped newline: the literal stops before it
                else()
                    math(EXPR length "${length} + 1") # the mark (closing, or escaped) or an escaped newline
                    if(escaped EQUAL 0)
                        set(reading 0)
                    endif()
                endif()
                string(SUBSTRING "${text}" 0 ${length} run)
                string(SUBSTRING "${text}" ${length} -1 text)
                string(APPEND kept "${run}")
            endwhile()
        elseif(at EQUAL at_line) # to the end of the line, on past a line ending in a backslash; its newlines kept
            set(newlines "")
            set(running 1)
            while(running)
                string(FIND "${text}" "\n" end)
                if(end EQUAL -1)
                    set(text "")
                    set(running 0)
                    continue()
                endif()
                string(SUBSTRING "${text}" 0 ${end} line)
                string(SUBSTRING "${text}" ${end} -1 text)
                if(line MATCHES "\\\\\r?$")
                    string(APPEND newlines "\n")
                    string(SUBSTRING "${text}" 1 -1 text)
                else()
                    set(running 0)
                endif()
            endwhile()
            string(APPEND kept "${newlines}")
        elseif(at EQUAL at_block) # to the `*/`, its newlines kept
            string(SUBSTRING "${text}" 2 -1 text)
            string(FIND "${text}" "*/" end)
            if(end EQUAL -1)
                set(comment "${text}")
                set(text "")
            else()
                string(SUBSTRING "${text}" 0 ${end} comment)
                math(EXPR after "${end} + 2")
                string(SUBSTRING "${text}" ${after} -1 text)
            endif()
            string(REGEX REPLACE "[^\n]" "" newlines "${comment}")
            string(APPEND kept "${newlines}")
        else() # an `R"` or a `'` that opens nothing: kept as a character
            string(SUBSTRING "${text}" 0 1 mark)
            string(SUBSTRING "${text}" 1 -1 text)
            string(APPEND kept "${mark}")
        endif()
    endwhile()
    set(${out} "${kept}${text}" PARENT_SCOPE)
endfunction()

set(_offences "")
foreach(source IN LISTS _kept)
    file(READ "${source}" text)
    string(REPLACE ";" " " text "${text}") # a CMake list separator; no refused name holds one
    if(source MATCHES "\\.(cpp|hpp)$")
        strip_comments("${text}" text)
    endif()
    set(text "\n${text}\n")                # every name has a neighbour on each side
    foreach(name IN LISTS _refused)
        name_regex("${name}" regex)
        set(rest "${text}")
        set(base 0)
        while(rest MATCHES "${regex}")
            set(found "${CMAKE_MATCH_0}")
            string(FIND "${rest}" "${found}" at)
            math(EXPR absolute "${base} + ${at}")
            string(SUBSTRING "${text}" 0 ${absolute} before)
            string(REGEX MATCHALL "\n" newlines "${before}")
            list(LENGTH newlines line) # the leading newline makes this the 1-based line of `found`'s first character
            if(found MATCHES "^\n")
                math(EXPR line "${line} + 1")
            endif()
            list(APPEND _offences "${source}:${line}: ${name}")
            # Keep the match's last character: it may be the boundary in front of the next one.
            string(LENGTH "${found}" length)
            math(EXPR next "${at} + ${length} - 1")
            string(SUBSTRING "${rest}" ${next} -1 rest)
            math(EXPR base "${base} + ${next}")
        endwhile()
    endforeach()
endforeach()

if(_offences)
    list(LENGTH _offences _count)
    string(REPLACE ";" "\n  " _report "${_offences}")
    message(FATAL_ERROR
        "a file a user reads uses a spelling the DSL retired, or its internals:\n  ${_report}\n(${_count} in all)\n"
        "Write the DSL (Input, Output, y[n - k], the members, the public notation, subgraph, gradients, "
        "finish(High, y), print<g>()); a graph no declaration can say belongs in a test (Plan/14-dsl/02 §3).")
endif()

message(STATUS "public spelling: ${_source_count} files, none uses a retired spelling or detail")
