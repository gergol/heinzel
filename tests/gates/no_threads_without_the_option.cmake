# P6 (`Plan/archive/parallel/02` §4): a build without `HEINZEL_HAVE_THREADS` references no thread — even for a program
# the planner split among workers, run serially. `main`'s `freestanding_no_threads`, hosted: with
# `-ffreestanding`, libstdc++ refuses the `<vector>` the planner uses while it compiles a graph (transient,
# which is allowed), and the property this check owes is the threads', not the freestanding library's.
#
# It compiles `fixtures/no_threads/tu.cpp` with the tree's compiler, standard and constexpr budget, without
# exceptions or RTTI, and reads the object's symbols: none may name a thread. `EXTRA` adds flags — the
# would-object twin passes `-DHEINZEL_HAVE_THREADS -DNO_THREADS_TU_WITH_A_POOL`, and this script must then object.
#
# Run with:  cmake -DCXX=.. -DSTD=.. -DSTEPS=.. -DINC=.. -DSUPPORT=.. -DSRC=.. -DOBJ=.. [-DEXTRA=..] -P no_threads_without_the_option.cmake

separate_arguments(_extra UNIX_COMMAND "${EXTRA}")
execute_process(
    COMMAND ${CXX} ${STD} ${STEPS} -O2 -fno-exceptions -fno-rtti ${_extra} -I ${INC} -I ${SUPPORT} -c ${SRC} -o ${OBJ}
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "no-threads check: the translation unit did not compile (rc=${rc}):\n${out}${err}")
endif()

find_program(NM_TOOL NAMES nm llvm-nm)
if(NOT NM_TOOL)
    message(FATAL_ERROR "no-threads check: no nm — a check that read no symbol would pass for the wrong reason")
endif()
execute_process(COMMAND ${NM_TOOL} -C ${OBJ} OUTPUT_VARIABLE syms RESULT_VARIABLE nm_rc)
if(NOT nm_rc EQUAL 0 OR syms STREQUAL "")
    message(FATAL_ERROR "no-threads check: nm read no symbol from ${OBJ}")
endif()
# A thread by name, or what a hand-made one would join through: an atomic wait compiles to `syscall` and names
# no thread at all.
string(TOLOWER "${syms}" syms_lc)
if(syms_lc MATCHES "thread|syscall|sched_yield|futex|semaphore|latch|barrier|condition_variable|mutex")
    message(FATAL_ERROR "no-threads check: the object references threading:\n${syms}")
endif()
message(STATUS "no-threads check: a split program run serially, compiled without HEINZEL_HAVE_THREADS, names no thread")
