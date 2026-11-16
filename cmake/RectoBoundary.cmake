# The D13 boundary between the engine libraries and recto-ui, checked by the build graph itself (D10 T01 section 1,
# D00 T04 section 8). For each module target this writes, at generate time, the manifest scripts/check-ui-boundary.py
# reads (the format the RectoBoundaryManifest target in Directory.Build.targets writes for MSBuild), and adds one custom
# command that runs the check after any module library changes. A direct `cmake --build` or a Visual Studio build
# therefore fails on a crossing as build\build.ps1 does, and a no-change build runs nothing.
#
#   project|<build.ninja>|<object folder>|<library file>
#   unit|<source>|<ExcludedFromBuild>|<GenerateSourceDependencies>|<SourceDependenciesFile>|<ObjectFileName>|
#        <PrecompiledHeader>|<PrecompiledHeaderOutputFile>|<AdditionalOptions>
#
# Ninja names each object <binary dir>/CMakeFiles/<target>.dir/<source relative to the target's folder>.obj, and every
# compile writes /sourceDependencies <object>.json (CMakeLists.txt). Were CMake ever to name an object otherwise, its
# report would be missing and the check refuses the unit, so a wrong path fails closed.

function(recto_boundary_manifest target)
    get_target_property(sources ${target} SOURCES)
    get_target_property(source_dir ${target} SOURCE_DIR)
    get_target_property(binary_dir ${target} BINARY_DIR)
    set(object_dir "${binary_dir}/CMakeFiles/${target}.dir")
    # build.ninja: Ninja runs the compiler and librarian in its folder, which relative member paths are relative to.
    set(lines "project|${CMAKE_BINARY_DIR}/build.ninja|${object_dir}/|$<TARGET_FILE:${target}>")
    if(RECTO_SCCACHE)
        set(pch_mode NotUsing)
    else()
        set(pch_mode Use)
        # CMake's precompiled header unit: it creates the .pch the others use.
        set(pch_object "${object_dir}/cmake_pch.cxx.obj")
        string(APPEND lines "\nunit|${object_dir}/cmake_pch.cxx|false|true|${pch_object}.json|${pch_object}|Create|${object_dir}/cmake_pch.pch|")
    endif()
    foreach(source IN LISTS sources)
        if(NOT source MATCHES "\\.(cpp|cxx|cc|c)$")
            continue()
        endif()
        cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${source_dir}" NORMALIZE OUTPUT_VARIABLE absolute)
        cmake_path(RELATIVE_PATH absolute BASE_DIRECTORY "${source_dir}" OUTPUT_VARIABLE relative)
        set(object "${object_dir}/${relative}.obj")
        string(APPEND lines "\nunit|${absolute}|false|true|${object}.json|${object}|${pch_mode}||")
    endforeach()
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/boundary/$<CONFIG>/${target}.txt" CONTENT "${lines}\n")
endfunction()

function(recto_boundary_check)
    set(libraries)
    set(manifests)
    foreach(target IN LISTS ARGN)
        recto_boundary_manifest(${target})
        list(APPEND libraries "$<TARGET_FILE:${target}>")
        list(APPEND manifests "${CMAKE_BINARY_DIR}/boundary/$<CONFIG>/${target}.txt")
    endforeach()
    set(stamp "${CMAKE_BINARY_DIR}/boundary/$<CONFIG>/checked.stamp")
    add_custom_command(
        OUTPUT "${stamp}"
        COMMAND "${CMAKE_SOURCE_DIR}/build/python.cmd" "${CMAKE_SOURCE_DIR}/scripts/check-ui-boundary.py"
            --configuration $<CONFIG> --platform ${RECTO_PLATFORM} --vs "$ENV{VSINSTALLDIR}"
            --manifests "${CMAKE_BINARY_DIR}/boundary/$<CONFIG>"
        COMMAND "${CMAKE_COMMAND}" -E touch "${stamp}"
        DEPENDS ${ARGN} ${manifests} "${CMAKE_SOURCE_DIR}/scripts/check-ui-boundary.py"
        COMMENT "Checking the engine/recto-ui boundary (D13) from the compiler's and librarian's own records"
        VERBATIM)
    add_custom_target(recto-boundary-check ALL DEPENDS "${stamp}")
    # ALL covers only the default target: a build of one target (`--target Recto`, build.ps1 -Project) would skip the
    # check. Every executable links the module libraries, so each depends on it, and a named build of any of them fails
    # on a crossing as the full build does. (build.ps1 -Project names the check too, for a library built alone.)
    foreach(executable IN ITEMS Recto recto-cli recto-tests recto-fuzz)
        if(TARGET ${executable})
            add_dependencies(${executable} recto-boundary-check)
        endif()
    endforeach()
endfunction()