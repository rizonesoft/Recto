# The build-start memory gate (D00 T04 section 8): run with cmake -P before any compile or link of every build. It
# refuses below one compiler and one link above the reserve, as configure does, and refuses when the configured pools
# no longer fit what is free now, naming the command that sizes them again.
if(RECTO_ASSUME_FREE_MB)
    set(free "${RECTO_ASSUME_FREE_MB}")
else()
    cmake_host_system_information(RESULT free QUERY AVAILABLE_PHYSICAL_MEMORY)
endif()
math(EXPR minimum "${RECTO_RESERVE_MB} + ${RECTO_COMPILER_MB} + ${RECTO_LINK_MB}")
math(EXPR needed "${RECTO_RESERVE_MB} + ${RECTO_POOL_COMPILE} * ${RECTO_COMPILER_MB} + ${RECTO_POOL_LINK} * ${RECTO_LINK_MB}")
if(free LESS minimum)
    message(FATAL_ERROR "Insufficient free memory to build safely: ${free} MB free is below the ${minimum} MB minimum "
        "(${RECTO_RESERVE_MB} MB reserve + ${RECTO_COMPILER_MB} MB for 1 compiler + ${RECTO_LINK_MB} MB for 1 link).")
endif()
if(free LESS needed)
    message(FATAL_ERROR "The build's pools (compile=${RECTO_POOL_COMPILE}, link=${RECTO_POOL_LINK}) need ${needed} MB but "
        "${free} MB is free now. Size them again: run build\\build.ps1, or configure the preset again "
        "(build\\cmake.cmd --preset <name>, or Project > Configure Cache in Visual Studio).")
endif()