# Recto's CMake and Ninja build (D00 T04 section 8; design: docs/specs/2026-11-09-cmake-ninja-build-design.md).
# Configure with a preset from CMakePresets.json inside the Visual Studio developer environment: build\build.ps1 does
# that, and so do Visual Studio's Open Folder and `build\cmake.cmd --preset <name>` from a developer prompt. Every flag
# mirrors Directory.Build.props and Directory.Build.targets, which the MSBuild build (Recto.sln) still uses, so the two
# builds compile the same code the same way.
cmake_minimum_required(VERSION 4.0)

# The pinned CMake only (build/tools.lock.json, D00 T04 section 6): another CMake would generate a different build.
file(READ "${CMAKE_CURRENT_LIST_DIR}/build/tools.lock.json" recto_lock)
string(JSON recto_cmake_pin GET "${recto_lock}" cmake version)
if(NOT CMAKE_VERSION VERSION_EQUAL recto_cmake_pin)
    message(FATAL_ERROR "This is CMake ${CMAKE_VERSION} (${CMAKE_COMMAND}); Recto pins CMake ${recto_cmake_pin}. "
        "Run build\\cmake.cmd, which runs the pinned one (fetch it with pwsh -File build\\bootstrap-tools.ps1).")
endif()
string(JSON recto_cppwinrt_version GET "${recto_lock}" cppwinrt version)
string(JSON recto_buildtools_version GET "${recto_lock}" sdk-buildtools version)
set(RECTO_TOOLS_DIR "${CMAKE_CURRENT_LIST_DIR}/.tools")

# The Windows SDK Directory.Build.props targets; the developer environment must name it (build.ps1 asks vcvarsall for it).
set(RECTO_WINDOWS_SDK_VERSION 10.0.26100.0)
string(REGEX REPLACE "[\\/]+$" "" recto_env_sdk "$ENV{WindowsSDKVersion}")
if(NOT recto_env_sdk STREQUAL RECTO_WINDOWS_SDK_VERSION)
    message(FATAL_ERROR "The developer environment uses Windows SDK '${recto_env_sdk}'; Recto targets "
        "${RECTO_WINDOWS_SDK_VERSION}. Enter it with vcvarsall.bat <arch> ${RECTO_WINDOWS_SDK_VERSION} (build\\build.ps1 does).")
endif()

# Before project(): the runtime library, debug information and runtime-check policies apply to every target, and the
# per-configuration flags CMake would add are replaced by what MSBuild's compile lines carry. MSBuild gives no NDEBUG,
# _DEBUG, WIN32, _WINDOWS or /GR (only src/app defines _DEBUG or NDEBUG, in its own CMakeLists.txt).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded") # /Z7: debug information in each object, which sccache caches
cmake_policy(SET CMP0184 NEW)
if(RECTO_ASAN)
    set(CMAKE_MSVC_RUNTIME_CHECKS "") # MSVC's AddressSanitizer refuses /RTC
else()
    set(CMAKE_MSVC_RUNTIME_CHECKS "$<$<CONFIG:Debug>:StackFrameErrorCheck;UninitializedVariable>") # /RTC1
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# C++23 and C17 through CMake's own standard switches, so a dependency's cxx_std_17 (GoogleTest) adds nothing.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No module dependency scanning and no `import std` target: MSBuild does neither for these projects (its
# ScanSourceForModuleDependencies is off), and Recto's sources import no modules.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_CXX_MODULE_STD OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
# The manifest tool from the pinned Microsoft.Windows.SDK.BuildTools, as Recto.vcxproj uses (D00 T04 section 8).
if(NOT DEFINED RECTO_TARGET_ARCH)
    message(FATAL_ERROR "Configure with a preset (RECTO_TARGET_ARCH names x64 or arm64).")
endif()
set(recto_buildtools_bin "${RECTO_TOOLS_DIR}/sdk-buildtools/${recto_buildtools_version}/bin/${RECTO_WINDOWS_SDK_VERSION}/x64")
set(CMAKE_MT "${recto_buildtools_bin}/mt.exe" CACHE FILEPATH "Manifest tool (pinned SDK build tools)" FORCE)
set(CMAKE_RC_COMPILER "${recto_buildtools_bin}/rc.exe" CACHE FILEPATH "Resource compiler (pinned SDK build tools)" FORCE)
set(RECTO_CPPWINRT "${RECTO_TOOLS_DIR}/cppwinrt/${recto_cppwinrt_version}/bin/cppwinrt.exe")
foreach(tool "${CMAKE_MT}" "${CMAKE_RC_COMPILER}" "${RECTO_CPPWINRT}")
    if(NOT EXISTS "${tool}")
        message(FATAL_ERROR "${tool} is missing; fetch the pinned tools with pwsh -File build\\bootstrap-tools.ps1.")
    endif()
endforeach()

project(Recto LANGUAGES C CXX)

if(NOT MSVC OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Recto builds with MSVC (cl.exe) from the Visual Studio developer environment.")
endif()
if(MSVC_VERSION LESS 1950)
    message(FATAL_ERROR "Recto needs the v145 toolset (Visual Studio 2026); this cl.exe is ${CMAKE_CXX_COMPILER_VERSION}.")
endif()
# The per-configuration flags MSBuild's compile lines carry, and nothing else: CMake's platform module appends
# /DWIN32 /D_WINDOWS /EHsc everywhere, /Ob0 in Debug and /Ob2 /DNDEBUG in Release to its own defaults, so they are set
# here, after project(), where they shadow the cache for every target.
foreach(lang C CXX)
    set(CMAKE_${lang}_FLAGS "")
    set(CMAKE_${lang}_FLAGS_DEBUG "/Od")
    set(CMAKE_${lang}_FLAGS_RELEASE "/O2")
endforeach()
# The switch MSBuild's own table (cl.xml) gives LanguageStandard stdcpp23 on this toolset: /std:c++23 from 19.52, where
# CMake gives the same, and /std:c++23preview before it, where CMake would give /std:c++latest (D14).
if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.52)
    set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "-std:c++23preview")
    set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "-std:c++23preview")
endif()
# The preset's architecture and the developer environment's compiler must agree: an arm64 preset from an x64 prompt
# would build x64 binaries into the ARM64 folder.
if(RECTO_TARGET_ARCH STREQUAL "arm64")
    set(RECTO_PLATFORM ARM64)
else()
    set(RECTO_PLATFORM x64)
endif()
if(NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL RECTO_PLATFORM)
    message(FATAL_ERROR "The preset builds ${RECTO_PLATFORM} but this developer environment's cl.exe targets "
        "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}; open a developer prompt for ${RECTO_TARGET_ARCH} (vcvarsall.bat "
        "${RECTO_TARGET_ARCH}) or use build\\build.ps1 -Platform ${RECTO_PLATFORM}.")
endif()

option(RECTO_ASAN "Build with AddressSanitizer (x64 only), into build/cmake-bin-asan" OFF)
option(RECTO_SCCACHE "Compile through sccache: no precompiled header, pch.h force-included, as MSBuild's sccache mode" ON)
if(RECTO_ASAN AND NOT RECTO_PLATFORM STREQUAL "x64")
    message(FATAL_ERROR "AddressSanitizer (RECTO_ASAN) is supported on x64 only.")
endif()

include(cmake/RectoPools.cmake)
include(cmake/RectoBoundary.cmake)

# Outputs: build/cmake-bin[-asan]/<platform>/<configuration>/<target>/, the layout build/bin has, in a folder of its
# own, so the CMake and MSBuild builds never run each other's executables.
if(RECTO_ASAN)
    set(RECTO_BIN_ROOT "${CMAKE_SOURCE_DIR}/build/cmake-bin-asan/${RECTO_PLATFORM}")
else()
    set(RECTO_BIN_ROOT "${CMAKE_SOURCE_DIR}/build/cmake-bin/${RECTO_PLATFORM}")
endif()

# Every compile writes the /sourceDependencies report the D13 boundary check reads, beside its object. Joined to its
# value (cl accepts both forms) so clangd, which does not know the switch, never reads the path as a second source
# (.clangd removes /sourceDependencies* from the compile database).
string(APPEND CMAKE_CXX_COMPILE_OBJECT " /sourceDependencies<OBJECT>.json")

if(RECTO_SCCACHE)
    string(JSON recto_sccache_dir GET "${recto_lock}" sccache executable)
    string(JSON recto_sccache_version GET "${recto_lock}" sccache version)
    set(CMAKE_C_COMPILER_LAUNCHER "${RECTO_TOOLS_DIR}/sccache/${recto_sccache_version}/${recto_sccache_dir}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_C_COMPILER_LAUNCHER}")
endif()

add_library(recto_defaults INTERFACE)
target_compile_definitions(recto_defaults INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
# Directory.Build.targets and MSBuild's own defaults: Level4, warnings as errors, conformance, the standard
# preprocessor, UTF-8, /sdl outside Debug,
# Just My Code in Debug (not under ASan).
target_compile_options(recto_defaults INTERFACE
    /W4 /WX /permissive- /Zc:preprocessor /utf-8 /EHsc /Zc:inline /FC /diagnostics:column /external:W4
    $<$<NOT:$<CONFIG:Debug>>:/sdl>
    $<$<AND:$<CONFIG:Debug>,$<NOT:$<BOOL:${RECTO_ASAN}>>>:/JMC>
    $<$<BOOL:${RECTO_ASAN}>:/fsanitize=address>)
# The developer environment's external include folders, named on each command line so build/compile_commands.json is
# complete without the environment; as /external:I they stay external, as cl treats EXTERNAL_INCLUDE.
# (foreach IN LISTS takes a variable name; ENV{...} is not one and would iterate nothing.)
set(recto_external_include "$ENV{EXTERNAL_INCLUDE}")
if(NOT recto_external_include)
    message(FATAL_ERROR "EXTERNAL_INCLUDE is empty: configure inside the Visual Studio developer environment (build\\build.ps1 does).")
endif()
foreach(dir IN LISTS recto_external_include)
    if(dir)
        file(TO_CMAKE_PATH "${dir}" dir)
        target_compile_options(recto_defaults INTERFACE "/external:I${dir}")
    endif()
endforeach()
target_link_options(recto_defaults INTERFACE /DEBUG:FULL
    $<$<OR:$<NOT:$<CONFIG:Debug>>,$<BOOL:${RECTO_ASAN}>>:/INCREMENTAL:NO>)

# One Recto module target: its project folder first on the include path, pch.h precompiled or force-included, its
# outputs in the build/bin layout, and the build-start memory gate before any of its work.
function(recto_target target)
    target_link_libraries(${target} PRIVATE recto_defaults)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    if(RECTO_SCCACHE)
        target_compile_options(${target} PRIVATE /FIpch.h)
    else()
        target_precompile_headers(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pch.h")
    endif()
    foreach(kind RUNTIME ARCHIVE LIBRARY PDB COMPILE_PDB)
        set_target_properties(${target} PROPERTIES ${kind}_OUTPUT_DIRECTORY "${RECTO_BIN_ROOT}/$<CONFIG>/${target}")
    endforeach()
    add_dependencies(${target} recto-memory-gate)
endfunction()

add_subdirectory(src/pdf)
add_subdirectory(src/core)
add_subdirectory(src/services)
add_subdirectory(src/ui)
add_subdirectory(src/app)
add_subdirectory(src/cli)
add_subdirectory(tests/recto-tests)
add_subdirectory(fuzz)

recto_boundary_check(recto-pdf recto-core recto-services recto-ui)
