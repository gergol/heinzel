# The transitive include closure stays inside an allowlist (`Plan/archive/13-loop-tree/01` §2, §5).
#
# The direct-edge gates (`spine_include_order`, `no_include_into`) see one include at a time, so a header the
# allowlist admits that in turn includes a forbidden one passes them. This walks every include recursively and
# fails naming each reached file that is not allowed, with the chain that reached it. It fails closed, as the
# spine gate does: an include it cannot resolve, a path holding `..`, and an `#include` spelled any way but with
# quotes or angle brackets around a path (a macro, a comment in between) are offences, and so is the word
# `#include` in a comment — reword it.
#
# Two modes.
#   loops_closure — every source under a folder, which is itself allowed; ALLOW as an `#include` spells a path
#   (prefixes ending in `/`, whole files); only `heinzel/…` includes, resolved against INCLUDE_ROOT:
#     cmake -DROOT=<include/heinzel/loops> -DINCLUDE_ROOT=<include> -DLABEL=<name> "-DALLOW=…" -P closure_stays_inside.cmake
#   loops_alone — a list of files and a folder of one-header translation units (each holding exactly
#   `#include "heinzel/loops/<header>"`, one per header under HEADERS, checked both ways); `heinzel/…` includes
#   resolve against BASE/include, any other quoted one against the includer's folder, then INCLUDE_DIRS in
#   order; paths and ALLOW are relative to BASE:
#     cmake -DBASE=<repo> "-DFILES=tests/unit/a.cpp;…" "-DINCLUDE_DIRS=include;tests/support" -DHEADERS=<dir>
#           -DTUS=<dir> -DLABEL=<name> "-DALLOW=include/heinzel/loops/;…" -P closure_stays_inside.cmake

if(NOT DEFINED LABEL)
    message(FATAL_ERROR "closure_stays_inside gate: LABEL is required")
endif()
foreach(optional ALLOW FILES INCLUDE_DIRS)
    if(NOT DEFINED ${optional})
        set(${optional} "")
    endif()
endforeach()

set(_roots "")
set(_offences "")
if(DEFINED ROOT)
    get_filename_component(_base "${INCLUDE_ROOT}" ABSOLUTE)
    set(_heinzel "")
    get_filename_component(_root "${ROOT}" ABSOLUTE)
    file(RELATIVE_PATH _home "${_base}" "${_root}")
    list(APPEND ALLOW "${_home}/")
    file(GLOB_RECURSE _sources "${_root}/*.hpp" "${_root}/*.cpp")
else()
    get_filename_component(_base "${BASE}" ABSOLUTE)
    set(_heinzel "include/")
    set(_home "the listed files")
    set(_sources "")
    foreach(f IN LISTS FILES)
        list(APPEND _sources "${_base}/${f}")
    endforeach()
    file(GLOB _tus "${_base}/${TUS}/*.cpp")
    list(APPEND _sources ${_tus})
    # One translation unit per header, and one header per translation unit: `run_stage.hpp` → `run_stage.cpp`,
    # `planner/emit.hpp` → `planner_emit.cpp`, holding exactly its one include.
    file(GLOB_RECURSE _headers RELATIVE "${_base}/${HEADERS}" "${_base}/${HEADERS}/*.hpp")
    set(_wanted "")
    foreach(h IN LISTS _headers)
        string(REPLACE "/" "_" _tu "${h}")
        string(REGEX REPLACE "\\.hpp$" ".cpp" _tu "${_tu}")
        list(APPEND _wanted "${_tu}")
        if(NOT EXISTS "${_base}/${TUS}/${_tu}")
            string(APPEND _offences "\n  ${HEADERS}/${h}: no translation unit ${TUS}/${_tu} compiles it alone")
            continue()
        endif()
        file(READ "${_base}/${TUS}/${_tu}" _text)
        string(STRIP "${_text}" _text)
        if(NOT _text STREQUAL "#include \"heinzel/loops/${h}\"")
            string(APPEND _offences "\n  ${TUS}/${_tu}: holds something other than exactly #include \"heinzel/loops/${h}\"")
        endif()
    endforeach()
    foreach(tu IN LISTS _tus)
        get_filename_component(_name "${tu}" NAME)
        list(FIND _wanted "${_name}" _at)
        if(_at EQUAL -1)
            string(APPEND _offences "\n  ${TUS}/${_name}: a translation unit with no header under ${HEADERS}")
        endif()
    endforeach()
endif()
list(LENGTH _sources _source_count)
if(_source_count EQUAL 0)
    message(FATAL_ERROR "${LABEL}: no sources — a gate that scanned nothing reports the same 'passed' as one that "
                        "scanned everything")
endif()

# Is `path` (relative to the base) allowed?
function(allowed path out)
    set(${out} FALSE PARENT_SCOPE)
    foreach(entry IN LISTS ALLOW)
        string(LENGTH "${entry}" _n)
        string(SUBSTRING "${path}" 0 ${_n} _head)
        if(path STREQUAL entry OR (entry MATCHES "/$" AND _head STREQUAL entry))
            set(${out} TRUE PARENT_SCOPE)
        endif()
    endforeach()
endfunction()

# What `file` (relative to the base) includes, resolved to paths relative to the base; `bad`: what cannot be.
function(includes_of file out bad)
    file(READ "${_base}/${file}" _text)
    string(REGEX REPLACE "\\\\\r?\n" "" _text "${_text}") # a spliced line is one line
    string(REGEX REPLACE "[][;]" "_" _text "${_text}")     # no match may carry a list metacharacter
    string(REGEX MATCHALL "(#|%:)[ \t]*include[ \t]*[<\"][^\">\n\\]*|(#|%:)[ \t]*include|(#|%:)[ \t]*/[*]" _all "${_text}")
    get_filename_component(_folder "${file}" DIRECTORY)
    set(_found "")
    set(_odd "")
    foreach(include IN LISTS _all)
        string(STRIP "${include}" _shown)
        if(include MATCHES "<" AND NOT include MATCHES "<heinzel/|\\.\\.")
            # A standard header — unless it names a file of the project's include folders (`<offender.hpp>` from a
            # test target's `-I tests/support` is as much an include of it as `"offender.hpp"`).
            string(REGEX REPLACE ".*<" "" _spelled "${include}")
            set(_where "")
            foreach(dir IN LISTS INCLUDE_DIRS)
                if(_where STREQUAL "" AND DEFINED BASE AND EXISTS "${_base}/${dir}/${_spelled}")
                    set(_where "${dir}/${_spelled}")
                endif()
            endforeach()
            if(NOT _where STREQUAL "")
                list(APPEND _found "${_where}")
            endif()
            continue()
        endif()
        # (`..` first: a MATCHES that fails clears CMAKE_MATCH_1)
        if(include MATCHES "\\.\\.")
            list(APPEND _odd "${_shown}")
        elseif(include MATCHES "[<\"](heinzel/[^\">]+)$")
            list(APPEND _found "${_heinzel}${CMAKE_MATCH_1}")
        elseif(include MATCHES "\"([^\">]+)$" AND DEFINED BASE)
            set(_spelled "${CMAKE_MATCH_1}")
            set(_where "")
            foreach(dir IN LISTS _folder INCLUDE_DIRS)
                if(_where STREQUAL "" AND EXISTS "${_base}/${dir}/${_spelled}")
                    set(_where "${dir}/${_spelled}")
                endif()
            endforeach()
            list(APPEND _found "${_where}")
            if(_where STREQUAL "")
                list(APPEND _odd "${_shown} (found in no include folder)")
            endif()
        else()
            list(APPEND _odd "${_shown}")
        endif()
    endforeach()
    list(REMOVE_ITEM _found "")
    set(${out} "${_found}" PARENT_SCOPE)
    set(${bad} "${_odd}" PARENT_SCOPE)
endfunction()

set(_seen "")
set(_queue "")
foreach(source IN LISTS _sources)
    file(RELATIVE_PATH _key "${_base}" "${source}")
    list(APPEND _queue "${_key}")
    list(APPEND _seen "${_key}")
    string(MD5 _hash "${_key}")
    set(_chain_${_hash} "${_key}")
endforeach()

while(_queue)
    list(POP_FRONT _queue _here)
    string(MD5 _hash "${_here}")
    set(_chain "${_chain_${_hash}}")
    includes_of("${_here}" _found _odd)
    foreach(odd IN LISTS _odd)
        string(APPEND _offences "\n  ${_chain}: an include this gate cannot follow: ${odd}")
    endforeach()
    foreach(next IN LISTS _found)
        list(FIND _seen "${next}" _at)
        if(NOT _at EQUAL -1)
            continue()
        endif()
        list(APPEND _seen "${next}")
        set(_reached "${_chain} -> ${next}")
        allowed("${next}" _ok)
        if(NOT EXISTS "${_base}/${next}")
            string(APPEND _offences "\n  ${_reached}: resolves to no file")
        elseif(NOT _ok)
            string(APPEND _offences "\n  ${_reached}")
        else()
            string(MD5 _hash "${next}")
            set(_chain_${_hash} "${_reached}")
            list(APPEND _queue "${next}")
        endif()
    endforeach()
endwhile()

list(LENGTH _seen _reached_count)
if(NOT _offences STREQUAL "")
    message(FATAL_ERROR "${LABEL}:${_offences}\n"
                        "The include closure of ${_home} reaches a file outside its allowlist, or a header lacks its "
                        "translation unit (Plan/13-loop-tree/01-Boundary-and-Layout.md §2, §5). Adding to the list is a "
                        "reviewed change with a line of reason, never done to get a build green.")
endif()
message(STATUS "${LABEL}: ${_source_count} sources, ${_reached_count} files in the closure, all allowed")
