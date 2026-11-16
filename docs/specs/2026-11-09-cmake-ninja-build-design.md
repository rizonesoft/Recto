# CMake and Ninja build: design (D00 T04 §8)

Status: proposed, 2026-11-16. Owner section: `D00 T04 §8` in `todo/00-workspace/TODO-04-build-performance.md`.

## Why

MSBuild cannot cap compiles and links separately: `/m` counts projects and `CL_MPCount` counts compilers per project, and a link of `recto-tests` (the largest) can start while every compiler slot is busy. A no-change MSBuild build still evaluates and up-to-date-checks every project, which costs seconds. Ninja schedules every compile and link of the whole tree in one graph with named job pools, and a no-change Ninja build reads one dependency log. CMake generates that graph and is what Visual Studio opens as a folder.

## What stays the same

- The toolset (MSVC v145 from Visual Studio 2026, D4), the Windows SDK 10.0.26100 target with 10.0.22621 minimum, C++23 through `/std:c++23preview` (D14), and every compile flag in `Directory.Build.targets`: `/W4 /WX /permissive- /Zc:preprocessor /utf-8`, `WIN32_LEAN_AND_MEAN`, `NOMINMAX`, `UNICODE`, `/Z7`, `/MDd` or `/MD`, `/sdl` outside Debug, `/bigobj` where a project has it, `/fsanitize=address` in the ASan presets. MSBuild's Debug defaults that CMake does not give are added (`/RTC1`, `/JMC`), and under ASan, as MSBuild does for `EnableASAN`, runtime checks are off (`CMAKE_MSVC_RUNTIME_CHECKS` empty, policy CMP0184), linking is not incremental and `/JMC` is dropped, since MSVC's AddressSanitizer refuses `/RTC` and incremental linking.
- The module graph: `recto-pdf`, `recto-core`, `recto-services` and `recto-ui` static libraries; `Recto`, `recto-cli`, `recto-tests` and `recto-fuzz` executables, with the same `ProjectReference` edges and the same system libraries.
- The pinned tools from `§6` (`.tools/` CMake, Ninja, sccache, CPython) and the sccache cache of `§7`.
- `Recto.sln` builds as today and stays in CI (`build.ps1 -MSBuild`) until a full milestone has passed on CMake.

## Layout

- One `CMakeLists.txt` at the root and one beside each `.vcxproj`, each declaring the one target its project declares, with the same source list (the projects list sources explicitly; so do the CMake files, so a new file is added to both until the MSBuild path is retired).
- `CMakePresets.json`: configure and build presets (no test presets: the suite runs only through `build.ps1`'s runner of `§11`, with its shards, serial pass, deadline, coverage and time budget, which a CTest registration would bypass) `x64-debug`, `x64-release`, `x64-asan-debug`, `x64-asan-release`, `arm64-debug`, `arm64-release`, generator `Ninja`, binary folder `build/cmake/<preset>`, the `.tools/` CMake and Ninja.
- Outputs: `build/cmake-bin[-asan]/<platform>/<configuration>/<target>/`, the layout `build/bin` uses (so `Ui.App`'s `recto_exe()`, the ASan runtime staging and the test runner work unchanged) in a folder of its own, so neither backend ever runs the other's executables.
- One `recto_defaults` interface target carries the shared flags; every target links it, as every project imports `Directory.Build.*`.

## Compiler, environment and cache

- `build/build.ps1` enters the Visual Studio developer environment for the platform (`vcvarsall.bat x64` or `x64_arm64`, from the Visual Studio `vswhere` finds), then runs `cmake --preset` and `cmake --build --preset`. The CMake files never search for Visual Studio themselves.
- sccache (the default): `CMAKE_C_COMPILER_LAUNCHER` and `CMAKE_CXX_COMPILER_LAUNCHER` name `.tools/` sccache, with the server and cache settings `Initialize-RectoSccache` already gives. As in MSBuild's sccache mode, no precompiled header is built and `pch.h` is force-included (`/FIpch.h`), because sccache does not cache `/Yc` and `/Yu`. Under `-NoSccache` each target uses `target_precompile_headers(pch.h)`.
- Every compile writes `/sourceDependencies <object>.json` (an override of `CMAKE_CXX_COMPILE_OBJECT`), which the boundary check reads; the cache proof checks that a report exists and is current after a cache hit, and if sccache does not restore it, the wrapper of `§7` (`build/sccache-cl.cpp`) stands between Ninja and sccache as it does for MSBuild.

## Entry points

`build/build.ps1` is one way in; a preset run directly (`cmake --preset x64-debug` from a Visual Studio developer prompt, or Visual Studio's Open Folder) is the other, and it must be as safe:

- Each preset names its architecture and host toolset with `"strategy": "external"` (`x64` or `arm64`, `host=x64`), which is how Visual Studio sets up the matching developer environment; on the command line the developer prompt supplies it, and configure refuses a compiler whose target does not match the preset.
- Configure sizes the pools itself when `build.ps1` passed none: `cmake_host_system_information(AVAILABLE_PHYSICAL_MEMORY)` with the same budget as below, and the same refusal below one compiler and one link (the `RECTO_ASSUME_FREE_MB` environment variable stands in for the reading in tests, at configure and in the build-start gate, as `-AvailableMb` does for `parallelism.ps1`).
- The presets name the `.tools/` Ninja (`CMAKE_MAKE_PROGRAM`) and, for Visual Studio, the `.tools/` CMake (`cmakeExecutable`, which only IDEs read). On the command line `build\cmake.cmd` runs the pinned CMake, as `build\python.cmd` runs the pinned CPython; configure refuses any other CMake (its version is not the one `build/tools.lock.json` pins) with that command, and refuses with the `build/bootstrap-tools.ps1` command when `.tools/` is absent.
- Memory is checked when each build starts, not only at configure: a gate target every other target depends on runs `cmake -P cmake/memory-gate.cmake`, which reads free memory and fails the build below one compiler and one link, or when the configured pools' budget exceeds what is free by more than one compiler's 1,536 MB (memory moves by a few hundred MB between sizing and the first compile, which the reserve absorbs), naming the command that reconfigures them; `build.ps1` reconfigures with fresh pool sizes on every run.
- The boundary check is part of the build graph: a custom command whose output is a stamp file, depending on the four module libraries, their manifests and `scripts/check-ui-boundary.py`, runs the check after any of them changes and fails the build when it does. A direct `cmake --build` or a Visual Studio build therefore fails on a crossing exactly as `build.ps1` does, and a no-change build runs nothing.

## Job pools and memory

- `set_property(GLOBAL PROPERTY JOB_POOLS compile=<C> link=<L>)` with `CMAKE_JOB_POOL_COMPILE` and `CMAKE_JOB_POOL_LINK`, `C` and `L` passed by `build.ps1` as cache variables.
- One budget: `free - 2,048 MB reserve >= C * 1,536 MB + L * link_mb`, where `link_mb` is the measured peak commit of the largest link (`recto-tests`), recorded in the findings file, and `L` is at most 2. Below one compiler and one link, the build refuses as `§3`'s does; `RECTO_BUILD_JOBS` and `-MaxCpuCount` still set `C`.

## Dependencies

- GoogleTest through vcpkg manifest mode (`vcpkg.json`), the vcpkg toolchain file, `VCPKG_INSTALLED_DIR` at the repository's `vcpkg_installed/` that MSBuild already uses, triplet `x64-windows` or `arm64-windows`; `find_package(GTest CONFIG)` in `tests/recto-tests`, with the DLLs copied beside the executable.
- C++/WinRT and the SDK build tools: the two NuGet packages `Recto.vcxproj` references are pinned in `build/tools.lock.json` with version, nuget.org URL and SHA-256, fetched into `.tools/` by `build/bootstrap-tools.ps1`. A custom command runs the pinned `cppwinrt.exe` over the SDK's `References` metadata into `build/cmake/<preset>/generated/winrt`, which `src/app` includes; `CMAKE_RC_COMPILER` and `CMAKE_MT` name the pinned `rc.exe` and `mt.exe`. `scripts/check-sdk-pins.py pr`, which moves `Recto.vcxproj`, moves the lock file's version and hash in the same change, so one update keeps both builds on one version.
- Manifests: `src/app/app.manifest` and `tests/recto-tests/recto-tests.manifest` are listed as sources, which CMake embeds with `mt.exe` the way MSBuild's `Manifest` item does.

## The D13 boundary check

`scripts/check-ui-boundary.py` stays the proof, run by the build graph itself (see Entry points). CMake writes, at generate time, one manifest per module target in the format the `RectoBoundaryManifest` target writes (`project|<CMakeLists.txt>|<object folder>|<library>` and one `unit|...` line per source with its object and report paths) into `build/cmake/<preset>/boundary/`, and the script reads them through its existing `--manifests` option; the project line names `build.ninja`, whose folder is where Ninja runs the librarian, so the member paths `lib /list` reports resolve as they do under MSBuild. A unit whose report is missing is refused, so a wrong object path fails closed. `dumpbin /directives`, `/symbols` and `lib /list` read the CMake-built libraries as they read the MSBuild ones.

## Compile database

`CMAKE_EXPORT_COMPILE_COMMANDS`, with each of the developer environment's `EXTERNAL_INCLUDE` folders named on the command line as `/external:I` (as plain `/I` they would stop being external and fail `/W4 /WX` on SDK headers), so each command line is complete without the environment. `build.ps1` copies it to `build/compile_commands.json` only after a full x64 Debug non-ASan build; `scripts/clangd-lsp.py` keeps using an existing database, and `scripts/build-compile-commands.py` and its MSBuild target go once `build.cmd` uses CMake.

## Visual Studio

Visual Studio 2026 opens the folder and reads `CMakePresets.json` (Open Folder or `devenv .`), with the same presets the command line uses. `Recto.sln` keeps opening as today while it exists.

## The governing decision

Foundation plan D8 names MSBuild the primary build system. When `build.cmd` moves to CMake, D8 gets a dated amendment: CMake and Ninja primary, `Recto.sln` kept in one CI job for parity until a full milestone has passed, and the cost of going back below.

## Going back to MSBuild

The MSBuild path is never removed in this section: `build.ps1 -MSBuild` builds `Recto.sln` as today and CI keeps one job on it. Going back is one change to `build.cmd`'s default and CI's job list; nothing in the sources depends on the generator. Deleting the CMake files afterwards loses only the job pools and the no-change speed.

## Proof

- ASan: the x64 Debug and Release ASan presets each build and pass the suite (CI runs Release only today, so Debug is proven locally).
- Entry points: a preset run from a developer prompt with no `build.ps1` builds, sizes its pools and refuses on low memory; an `arm64-*` preset run from an x64 prompt is refused; a forbidden include fails `cmake --build` itself.
- Parity: each backend builds and runs the suite from its own folder on the same commit; both list the same tests and pass, and the boundary check passes on both.
- Speed and memory: clean and no-change x64 Debug times of both, no-change CMake under 2 s, and the lowest free memory during a clean CMake build started with about 6 GB free at or above 2,048 MB.
- Cache: a warm rebuild hits for every unit; header and forced-include edits miss where they should; reports exist after hits; a compile error fails the build.

## Notes from the build

- C++23 comes from `CMAKE_CXX_STANDARD 23`, which absorbs GoogleTest's `cxx_std_17`; on toolsets before 19.52 the switch is `/std:c++23preview`, as MSBuild's own `cl.xml` maps `stdcpp23` there, and from 19.52 `/std:c++23` in both builds.
- Module scanning and the `import std` target that CMake 4.5 adds for C++23 are off, as MSBuild's `ScanSourceForModuleDependencies` is.
- CMake's platform defaults (`/DWIN32 /D_WINDOWS`, `/Ob0`, `/Ob2 /DNDEBUG`) are replaced after `project()` by MSBuild's `/Od` and `/O2`.
- GoogleTest's include folder is passed as plain `/I` (its package marks it system), as MSBuild's vcpkg integration passes it.
- The CMake modules live in `cmake/` at the root; `build/cmake/<preset>` holds only generated build folders.
- One link budget: `recto-tests` peaked at 457 MB (Debug) and 584 MB (Release) of commit, so a link is budgeted 1,024 MB.
- `/sourceDependencies` is joined to its value (`/sourceDependencies<object>.json`, which cl accepts): clangd does not know the switch, so a separate value would be read as a second source file; `.clangd` removes the joined switch whole.
- clang-cl 22 ignores `/std:c++23` (and would fall back to C++14), so `.clangd` swaps it for `/std:c++23preview`, which it reads; the build itself keeps `/std:c++23`.
- `build.ps1`'s `-MSBuild` switch shares its name, case-insensitively, with the old `$msbuild` path variable, now `$msbuildExe`.
