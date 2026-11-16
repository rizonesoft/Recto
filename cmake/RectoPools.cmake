# Ninja compile and link pools from one memory budget (D00 T04 sections 3 and 8). build/build.ps1 passes
# RECTO_COMPILE_JOBS and RECTO_LINK_JOBS from free memory each run; a preset run directly (a developer prompt, Visual
# Studio's Open Folder) sizes them here at configure, and the build-start gate (memory-gate.cmake) checks them again
# before every build, so memory another program took since configure is never assumed free.
set(RECTO_RESERVE_MB 2048)    # left for the OS and desktop, as section 3
set(RECTO_COMPILER_MB 1536)   # per concurrent cl.exe, as section 3
# One link: the largest, recto-tests, peaked at 457 MB (Debug) and 584 MB (Release) of commit on 2026-11-16.
set(RECTO_LINK_MB 1024 CACHE STRING "Budget for one link, MB (measured: recto-tests 457 MB Debug, 584 MB Release)")
set(RECTO_COMPILE_JOBS "" CACHE STRING "Compile pool depth (empty: sized from free memory at configure)")
set(RECTO_LINK_JOBS "" CACHE STRING "Link pool depth, at most 2 (empty: sized from free memory at configure)")

# Tests only: the RECTO_ASSUME_FREE_MB environment variable stands in for the reading, at configure here and at build time
# in memory-gate.cmake, as -AvailableMb does for build/parallelism.ps1.
function(recto_free_mb out)
    if(DEFINED ENV{RECTO_ASSUME_FREE_MB} AND NOT "$ENV{RECTO_ASSUME_FREE_MB}" STREQUAL "")
        set(${out} "$ENV{RECTO_ASSUME_FREE_MB}" PARENT_SCOPE)
    else()
        cmake_host_system_information(RESULT free QUERY AVAILABLE_PHYSICAL_MEMORY)
        set(${out} "${free}" PARENT_SCOPE)
    endif()
endfunction()

cmake_host_system_information(RESULT recto_cores QUERY NUMBER_OF_LOGICAL_CORES)
recto_free_mb(recto_free)
math(EXPR recto_budget "${recto_free} - ${RECTO_RESERVE_MB}")
math(EXPR recto_minimum "${RECTO_RESERVE_MB} + ${RECTO_COMPILER_MB} + ${RECTO_LINK_MB}")
if(recto_free LESS recto_minimum)
    message(FATAL_ERROR "Insufficient free memory to build safely: ${recto_free} MB free is below the ${recto_minimum} MB "
        "minimum (${RECTO_RESERVE_MB} MB reserve + ${RECTO_COMPILER_MB} MB for 1 compiler + ${RECTO_LINK_MB} MB for 1 link). "
        "Close memory-heavy applications and configure again.")
endif()
if(RECTO_LINK_JOBS)
    set(recto_links "${RECTO_LINK_JOBS}")
else()
    # A second link only when two links and two compilers still fit.
    math(EXPR recto_two "2 * ${RECTO_LINK_MB} + 2 * ${RECTO_COMPILER_MB}")
    if(recto_budget LESS recto_two)
        set(recto_links 1)
    else()
        set(recto_links 2)
    endif()
endif()
if(RECTO_COMPILE_JOBS)
    set(recto_compiles "${RECTO_COMPILE_JOBS}")
elseif(DEFINED ENV{RECTO_BUILD_JOBS} AND NOT "$ENV{RECTO_BUILD_JOBS}" STREQUAL "")
    set(recto_compiles "$ENV{RECTO_BUILD_JOBS}")
else()
    math(EXPR recto_compiles "(${recto_budget} - ${recto_links} * ${RECTO_LINK_MB}) / ${RECTO_COMPILER_MB}")
    if(recto_compiles GREATER recto_cores)
        set(recto_compiles ${recto_cores})
    endif()
endif()
if(NOT recto_compiles MATCHES "^[1-9][0-9]*$" OR recto_compiles GREATER recto_cores)
    message(FATAL_ERROR "Invalid compile pool depth '${recto_compiles}': expected an integer between 1 and ${recto_cores}.")
endif()
if(NOT recto_links MATCHES "^[12]$")
    message(FATAL_ERROR "Invalid link pool depth '${recto_links}': expected 1 or 2.")
endif()
set(RECTO_POOL_COMPILE ${recto_compiles})
set(RECTO_POOL_LINK ${recto_links})
message(STATUS "Recto pools: free ${recto_free} MB, ${recto_cores} cores: compile=${recto_compiles}, link=${recto_links}")

set_property(GLOBAL PROPERTY JOB_POOLS compile=${recto_compiles} link=${recto_links})
set(CMAKE_JOB_POOL_COMPILE compile)
set(CMAKE_JOB_POOL_LINK link)

# Runs before every target's work (recto_target adds the dependency) and on every build: cheap, and the only check
# that sees memory taken after configure.
add_custom_target(recto-memory-gate
    COMMAND "${CMAKE_COMMAND}" -DRECTO_RESERVE_MB=${RECTO_RESERVE_MB} -DRECTO_COMPILER_MB=${RECTO_COMPILER_MB}
        -DRECTO_LINK_MB=${RECTO_LINK_MB} -DRECTO_POOL_COMPILE=${recto_compiles} -DRECTO_POOL_LINK=${recto_links}
        -P "${CMAKE_SOURCE_DIR}/cmake/memory-gate.cmake"
    COMMENT "Checking free memory against the configured pools"
    VERBATIM)