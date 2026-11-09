---
schema_version: 1
id: build-performance
domain: 00-workspace
status: active
title: "TODO-04 -- Build Performance, Repo-Scoped Tools and the Ninja Build"
track: M0
---

# TODO-04 -- Build Performance, Repo-Scoped Tools and the Ninja Build

> **Goal:** A section's build-and-test loop on the Windows host is fast and never starves the machine: ASan and plain builds keep their own outputs so switching between them rebuilds nothing, the local build is x64 only while ARM64 stays proven in CI, the compiler's parallelism follows the memory that is free, precompiled headers cut every project's compile, gates are tiered so the heavy work happens once at stamp time, and the build tools (Ninja, CMake, sccache) and the Python the scripts run on are pinned and fetched into the repository, then the build itself moves to CMake and Ninja with job pools and a compiler cache.

> **Current state:** Measured 2026-11-09. Seven MSBuild projects (`Recto.sln`; `src/pdf`, `src/core`, `src/services`, `src/ui`, `src/app`, `src/cli`, `tests/recto-tests`, plus `fuzz`), 67 `.cpp` files, the largest `src/pdf/filters.cpp` (436 KB), `document.cpp` (156 KB), `interpreter.cpp` (135 KB) and `xref.cpp` (125 KB). `build.cmd` runs `build/build.ps1`, which builds x64 and ARM64 in Debug and Release (four solution builds) with MSBuild `/m` and runs the x64 tests; `-EnableASAN` builds the same projects into the same `build/obj` and `build/bin` folders (`Directory.Build.props` `IntDir`/`OutDir` take no account of it), so every switch between an ASan and a plain build rebuilds everything. No project uses precompiled headers. The host has 12 logical processors and 32 GB; on 2026-11-09 three review runs were killed by Claude Code for low memory while builds, ASan runs and a WSL VM (7 to 10 GB) shared it, and the commit charge stood at 50 GB of 65 GB. CI (`.github/workflows/build.yml`) builds Debug and Release for x64 and ARM64 and tests x64. The scripts run on whatever `python` is on `PATH` (3.14.10 here), checked by `scripts/check-python-pin.py`; no build tool is pinned in the repository, and `.tools/` is already ignored by `.gitignore`.

## Inputs

- Foundation plan `docs/plans/2026-10-13-recto-foundation.md` D4 (toolchain and pins, ARM64 a ship target) and D14 (C++23); `AGENTS.md` Validation (`.\build.cmd`) and its memory and one-heavy-job rules in the `process-phase` skill.
- `build/build.ps1`, `build.cmd`, `Directory.Build.props`, `Directory.Build.targets` (the D13 boundary checks read MSBuild items), `.github/workflows/build.yml`, `scripts/check-*.py` (the pin checks).
- -> XREF: D00 T01 §3 -- the solution scaffold this file speeds up; its x64 and ARM64 one-command build is kept, with ARM64 moved to CI by `§2`
- -> XREF: D00 T01 §4 -- the CI build `§2` keeps building ARM64 and `§8` moves to Ninja

## Outcome

- An ASan build and a plain build of the same configuration leave each other's outputs alone: building one after the other rebuilds only what changed, read from MSBuild's own logs.
- `build.cmd` builds x64 Debug and Release and runs their tests; ARM64 is built in CI on every push, and the deferral to after Recto v1 is a dated owner decision in the foundation plan.
- `build/build.ps1` picks MSBuild's project and compiler parallelism from the free memory and cores at start, and a build on a machine with 6 GB free does not fall below 2 GB free.
- Every project compiles through a precompiled header, with the clean x64 Debug build time measured before and after.
- The section and stamp skills run a tiered gate: the touched tests in x64 Debug while a section is built, the whole x64 suite in Debug and Release plus the section's ASan checkpoint before it stamps.
- `build/bootstrap-tools.ps1` fetches pinned Ninja, CMake, sccache and CPython into `.tools/`, each checked against its SHA-256, and the build, the scripts and the hooks use them; a pin check keeps them current.
- sccache caches the compiler's output, so a rebuild with nothing changed costs no compiles and a branch switch reuses what was built before.
- A CMake build driven by Ninja builds every project with compile and link job pools, keeps the D13 boundary checks, runs in CI, and replaces the MSBuild path once it matches its outputs.

**Adjacency:** all=not-applicable (build tooling, scripts and CI for the developer and CI host; no user-facing surface, document or setting)

**Adjacency rationale:** Nothing here ships to users or reads a user's file. The tools fetched into `.tools/` are build inputs, pinned and checked like the SDK and Actions pins; the only persistent output is build artifacts under the ignored `build/` and `.tools/` folders, and the sccache cache directory, which holds compiler output only.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | ASan and plain builds in separate output folders | -- | [ ] |
| 2 | §2 | ARM64 off the local build, built in CI, deferred to after Recto v1 | -- | [ ] |
| 3 | §3 | MSBuild and compiler parallelism set by free memory | §1 | [ ] |
| 4 | §4 | Precompiled headers for every project | §3 | [ ] |
| 5 | §5 | Tiered gates: touched tests while building, the full suite at stamp | §2 | [ ] |
| 6 | §6 | Repo-scoped pinned Ninja, CMake, sccache and CPython under `.tools/` | -- | [ ] |
| 7 | §7 | sccache as the compiler cache for MSVC | §6 | [ ] |
| 8 | §8 | CMake and Ninja build with compile and link job pools | §6, §7, §4 | [ ] |

## 1. ASan and plain builds in separate output folders

`Directory.Build.props` sends every build of a configuration to `build/obj/<project>/<platform>/<configuration>/` and `build/bin/<platform>/<configuration>/<project>/`, whether `EnableASAN` is set or not, so the object files of one overwrite the other's and each switch is a full rebuild (seen many times on 2026-11-09). This section gives ASan builds their own folders and keeps everything that reads the output paths (`build/build.ps1`, `build/asan-runtime.ps1`, the test runner paths, CI) pointed at the right one.

**Corrected 2026-11-09:** plan review (`docs/reviews/00-workspace/D00-T04-s1.md`). `build/build.ps1` does more than share folders: it records the last ASan state per configuration in `build/asan-state-<cfg>-<plat>.txt` and deletes the projects' obj and bin folders whenever it flips, so the cleanup and its state file go, with `tests/scripts/test_build_ps1.py`, which requires them, changed to prove the opposite. Isolation is proven by hashing every artifact of the other build before and after, in both directions; compiler work is counted by object files whose write time changed (`build/build-evidence.ps1`), checked against a run that touches one source and must count exactly that unit, so a count read from nothing cannot pass as zero.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Give ASan builds their own `IntDir` and `OutDir` in `Directory.Build.props`. Done when: an ASan build writes only under `build/obj-asan/` and `build/bin-asan/`.
2. Point `build/build.ps1` and `build/asan-runtime.ps1` at the folder of the build they ran. Done when: `build.ps1 -EnableASAN` runs the ASan `recto-tests.exe` and a plain build runs the plain one.

- [ ] Set `IntDir` to `build\obj-asan\...` and `OutDir` to `build\bin-asan\...` in `Directory.Build.props` when `EnableASAN` is `true`, leaving the plain paths unchanged. Done when: an ASan build writes under `build/obj-asan/` and `build/bin-asan/` only, every plain artifact keeping its hash. Cheaper substitute: deleting the plain outputs before each ASan build, which is the full rebuild this section removes.
- [ ] Read the test binary and the ASan runtime copy from the ASan folders in `build/build.ps1` and `build/asan-runtime.ps1`. Done when: `build.ps1 -EnableASAN -Configuration Debug -TestFilter 'PdfError.*'` runs `build\bin-asan\x64\Debug\recto-tests\recto-tests.exe`, and `build.ps1 -Platform x64 -Configuration Debug -TestFilter 'PdfError.*'` runs `build\bin\x64\Debug\recto-tests\recto-tests.exe`.
- [ ] Retire the ASan-state cleanup in `build/build.ps1` (the `asan-state-*.txt` file and the deletion of obj and bin folders when it flips), and change `tests/scripts/test_build_ps1.py` to require that no build deletes the other mode's folders. Done when: the script holds no `asan-state` code and the test fails if the deletion is put back.
- [ ] Add `build/build-evidence.ps1`, which records the SHA-256 of every file under a build's obj and bin folders and the write times of its `.obj` files, and compares two records: files changed, removed, and object files rewritten. Done when: a run after touching `src/pdf/error.cpp`'s timestamp reports exactly `error.obj` rewritten in each project that compiles it, and a run with no change reports none.
- [ ] Prove the switch keeps both builds and rebuilds nothing, in both directions. Done when: plain x64 Debug, ASan, plain, ASan, each succeeding, with `build-evidence.ps1` showing the plain artifacts unchanged across each ASan build, the ASan artifacts unchanged across each plain build, and no object file rewritten on the third and fourth builds.
- [ ] Update the CI ASan job and any script that names `build\bin\x64\Debug` for an ASan run (`.github/workflows/*.yml`, `scripts/`). Done when: a search for `bin\x64\Debug` in ASan steps finds none left, and the CI ASan job passes on the pushed commit.
- [ ] Commit: `build: give ASan builds their own output folders`

**Test checkpoint:** plain, ASan, plain and ASan x64 Debug builds in turn, each passing, rewrite no object file on the third and fourth and leave the other mode's artifacts byte-identical (`build/build-evidence.ps1`), and setting the ASan `IntDir` back to the plain one makes the fourth rewrite every object file.

## 2. ARM64 off the local build, built in CI, deferred to after Recto v1

The owner decided on 2026-11-09 that the local build is x64 only until Recto v1 ships, to halve every gate on the shared development host, while ARM64 stays a ship target. This section changes the local default, keeps ARM64 built in CI so a break is still caught on every push, and records the decision in the foundation plan so later sections that need an ARM64 host (`arm64-host` Needs) are not mistaken for done.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Default `build/build.ps1` and `build.cmd` to x64, with `-Platform ARM64` still accepted. Done when: `.\build.cmd` builds and tests x64 Debug and Release only.
2. Keep ARM64 in CI and record the owner decision. Done when: CI builds ARM64 Debug and Release on the pushed commit and the foundation plan carries the dated note.

- [ ] Change the default `$Platform` in `build/build.ps1` to `x64` and its help text, keeping `-Platform x64,ARM64` and `-Platform ARM64` working. Done when: `.\build.cmd` logs only `Debug|x64` and `Release|x64`, and `build.cmd -Platform ARM64 -SkipTests` still builds ARM64.
- [ ] Keep `.github/workflows/build.yml` passing `-Platform x64,ARM64` for both configurations, and say in its header that ARM64 is built in CI only. Done when: the pushed commit's `build.yml` run shows `Debug|ARM64` and `Release|ARM64` built.
- [ ] Add a dated `**Corrected 2026-11-09:**` owner decision under D4 in `docs/plans/2026-10-13-recto-foundation.md`: ARM64 stays a v1 ship target, built in CI on every push, off the local build until Recto v1, with its cost of reversing (one default in `build.ps1`). Done when: the note is in the plan and `AGENTS.md`'s Validation line for `build.cmd` says x64.
- [ ] Commit: `build: build x64 locally and ARM64 in CI`

**Test checkpoint:** `.\build.cmd` builds only `|x64` configurations (counted from its logs), and the CI run on the same commit builds both ARM64 configurations.

## 3. MSBuild and compiler parallelism set by free memory

MSBuild `/m` starts one project build per core and `MultiProcessorCompilation` one compiler per core inside each, so on 12 cores a build can run many 1 to 2 GB compilers at once whatever is free; on 2026-11-09 free memory fell to 88 MB during builds. This section has `build/build.ps1` choose both limits from the free memory and cores when it starts, so a build takes what it can without starving the machine.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Compute the limits in `build/build.ps1`. Done when: the script prints the free memory, the cores, and the `/m` and `CL_MPCount` it chose.
2. Prove the floor holds. Done when: a clean x64 Debug build started with about 6 GB free never reads under 2 GB free, sampled every second.

- [ ] In `build/build.ps1`, read `\Memory\Available MBytes` and the logical processor count, and set the compilers allowed at once to `max(1, min(cores, floor((free_mb - 2048) / 1536)))`, passed as MSBuild `/m:<projects>` (at most 2 when fewer than 6 compilers fit) and `/p:CL_MPCount=<compilers per project>` with `MultiProcessorCompilation` on. Done when: the script's header line reads, for example, `free 9,500 MB, 12 cores: /m:2, CL_MPCount=4`, and the MSBuild log shows the same `CL_MPCount`.
- [ ] Let `RECTO_BUILD_JOBS` override the computed number for a run, and refuse a value under 1 or over the cores with a clear message. Done when: `RECTO_BUILD_JOBS=2` gives `2` compilers in all and `RECTO_BUILD_JOBS=0` stops the build before MSBuild starts.
- [ ] Measure it. Done when: a clean x64 Debug build with about 6 GB free samples `Available MBytes` every second and never reads under 2,048, and its wall time, with and without the limit, is recorded in the findings file.
- [ ] Commit: `build: choose MSBuild and compiler parallelism from free memory`

**Test checkpoint:** a clean `build.ps1 -Platform x64 -Configuration Debug -SkipTests` started with about 6 GB free keeps `Available MBytes` at or above 2,048 throughout, and the same build with the limit removed falls below it.

## 4. Precompiled headers for every project

No project uses a precompiled header, so `windows.h`, Direct2D, DirectWrite, WIC and the standard library are parsed again for every one of 67 files. This section adds one precompiled header per project, made of the system and standard headers only (no project header, so it does not change what depends on what), and measures the clean build.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `pch.h` and `pch.cpp` to each project and turn precompiled headers on in `Directory.Build.targets`. Done when: every project builds with `/Yu` and its `.pch` exists.
2. Measure and keep the boundary checks. Done when: the clean x64 Debug build time is recorded before and after and the D13 boundary check passes.

- [ ] Add `pch.h` (the system and standard headers each project's sources include, nothing from `src/`) and `pch.cpp` to `src/pdf`, `src/core`, `src/services`, `src/ui`, `src/app`, `src/cli`, `tests/recto-tests` and `fuzz`, force-included with `/FI` so no source changes. Done when: each project's MSBuild log shows `/Yc` for `pch.cpp` and `/Yu` for the rest, and `build\obj\<project>\x64\Debug\*.pch` exists for every project.
- [ ] Keep `Directory.Build.targets`'s D13 boundary check exact with the forced header in place (a `pch.h` naming a `src/**` header from another module would hide a crossing). Done when: the boundary check passes, and a `pch.h` in `src/ui` that includes a `src/pdf` header is refused by it.
- [ ] Measure the clean x64 Debug build of the solution before and after on the same host with the same free memory. Done when: both wall times and the peak compiler working set are recorded in the findings file, and the build is faster.
- [ ] Commit: `build: precompiled headers for every project`

**Test checkpoint:** a clean `build.ps1 -Platform x64 -Configuration Debug` passes with 0 warnings and every project's log shows `/Yu`, and including a `src/pdf` header in `src/ui/pch.h` fails the boundary check.

## 5. Tiered gates: touched tests while building, the full suite at stamp

The `process-todo-section`, `review-todo-section` and `process-phase` skills run `build.cmd`'s four builds (now two) after most fixes, so a section with several review rounds rebuilds and retests everything many times. This section tiers the gates: while a section is built and reviewed, the touched projects in x64 Debug with the section's own tests (and its ASan checkpoint where it names one); before it stamps, the full x64 Debug and Release suite once on the final candidate.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write the two tiers into the skills. Done when: `process-todo-section` and `review-todo-section` name the iteration gate and the stamp gate with their commands.
2. Make `stamp` evidence name the stamp gate. Done when: the stamp's `--check` lines quote a full-suite run on the stamped commit.

- [ ] Add an iteration gate to `.claude/skills/process-todo-section/SKILL.md` step 4 and `.claude/skills/review-todo-section/SKILL.md` step 5: `build.ps1 -Platform x64 -Configuration Debug -TestFilter '<the section's tests>'`, plus the section's ASan checkpoint when it names one. Done when: both skills name the command and say it is what runs after each fix.
- [ ] Add the stamp gate to `review-todo-section` step 7: `.\build.cmd` (x64 Debug and Release, every test) once on the candidate tip that stamps, quoted in a `--check`. Done when: the step says so, and a stamp of a section in this file quotes a full-suite run on its stamped commit.
- [ ] Record the tiers in `AGENTS.md`'s Validation section in one line each. Done when: the two lines are there and match the skills.
- [ ] Commit: `skills: tier the build gates between a section's rounds and its stamp`

**Test checkpoint:** `python -m unittest discover -s scripts/tests` and `python scripts/todo-graph.py validate` pass, and the skills and `AGENTS.md` name the same two commands.

## 6. Repo-scoped pinned Ninja, CMake, sccache and CPython under `.tools/`

No build tool is pinned in the repository, and the scripts and hooks run on whatever `python` the machine has. This section fetches pinned versions of Ninja, CMake, sccache and the CPython embeddable distribution into the ignored `.tools/` folder, each checked against its SHA-256 before it is used, and points the build, the scripts, CI and the Claude hooks at them, so a fresh clone or another machine gets the same tools without installing anything.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Pin the tools and fetch them. Done when: `build/bootstrap-tools.ps1` fills `.tools/` from `build/tools.lock.json` and refuses a download whose hash differs.
2. Use them everywhere and keep them current. Done when: the build, the scripts, CI and the hooks run the `.tools/` versions and a pin check flags a newer stable release.

- [ ] Add `build/tools.lock.json` naming, for Ninja, CMake, sccache and CPython (the Windows x64 embeddable zip), the version, the download URL and the SHA-256, all from each project's own release page. Done when: the file holds four entries, each hash matching its release's published checksum.
- [ ] Add `build/bootstrap-tools.ps1`, which downloads each missing or wrong-version tool to a temporary file in `.tools/`, checks its SHA-256, extracts it into `.tools/<tool>/<version>/` and only then renames it into place, and refuses with the tool, the expected and the actual hash on a mismatch. Done when: a run on an empty `.tools/` fetches all four, a second run downloads nothing, and a lock entry with one wrong hash digit stops with that message and leaves no folder for that tool.
- [ ] Run the bootstrap from `build/build.ps1` and `build.cmd` before anything else, and add `build/python.cmd` that runs the `.tools/` CPython. Done when: `build.cmd` on a clean clone fetches the tools and builds, and `build\python.cmd -c "import sys; print(sys.version)"` prints the pinned version.
- [ ] Point the scripts' documented commands, the CI workflows and the hooks in `.claude/settings.json` at `build\python.cmd` (or the `.tools/` interpreter), keeping them stdlib only. Done when: `python -m unittest discover -s scripts/tests` and `tests/scripts` pass through `build\python.cmd`, the hooks run with no `python` on `PATH`, and CI uses the pinned version.
- [ ] Add `scripts/check-tool-pins.py check`, in the pattern of `scripts/check-sdk-pins.py`, which compares each lock entry with its project's latest stable release (D4: tools are the latest stable), and hook it into the scheduled pin automation in `.conclave/automations/`. Done when: it exits 0 on the current lock, non-zero naming the tool when an entry is behind, with a test in `tests/scripts/`.
- [ ] Retire `scripts/check-python-pin.py`'s reliance on the machine's Python in favour of the lock's CPython entry, keeping its CI check. Done when: it reads the version from `build/tools.lock.json` and its tests pass.
- [ ] Commit: `build: fetch pinned Ninja, CMake, sccache and CPython into .tools`

**Test checkpoint:** `build/bootstrap-tools.ps1` on an empty `.tools/` fetches and verifies all four tools and a corrupted hash in the lock stops it, `build\python.cmd -m unittest discover -s tests/scripts` passes, and `python scripts/check-tool-pins.py check` exits 0.

## 7. sccache as the compiler cache for MSVC

A rebuild after switching branches, configurations or back from an ASan build compiles everything again. This section puts sccache (pinned by `§6`) in front of `cl.exe` with the debug information format it can cache, so unchanged translation units come from its cache.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Route compiles through sccache. Done when: a second clean build of x64 Debug reports a cache hit for every translation unit.
2. Bound and measure it. Done when: the cache has a size limit and the rebuild time is recorded.

- [ ] Use `/Z7` (debug information in the object, which sccache can cache) instead of `/Zi` for Debug and ASan builds in `Directory.Build.props`, keeping the PDBs the linker writes. Done when: tests still report file and line in failures and ASan reports still symbolize.
- [ ] Run `cl.exe` through the `.tools/` sccache, with `SCCACHE_DIR` under `build/sccache/` and `SCCACHE_CACHE_SIZE` at 10 GB, in `build/build.ps1` (MSBuild: `CLToolExe`/`CLToolPath` pointing at a wrapper) and in the Ninja build of `§8` (the compiler launcher). Done when: `sccache --show-stats` after a clean build shows misses, and after deleting `build/obj` and building again shows a hit for every unit.
- [ ] Measure a full rebuild from a warm cache against a cold one. Done when: both times are recorded in the findings file and the warm one is a fraction of the cold.
- [ ] Commit: `build: cache MSVC compiles with sccache`

**Test checkpoint:** after a clean x64 Debug build, deleting `build/obj` and building again reports 100% cache hits in `sccache --show-stats` and passes the suite.

## 8. CMake and Ninja build with compile and link job pools

MSBuild's scheduler cannot cap compiles and links separately, and its no-change build still costs seconds per project. This section adds a CMake build of every project, driven by the `.tools/` Ninja with a compile job pool and a smaller link pool sized from free memory, through sccache, keeps the D13 boundary checks (moved from MSBuild items to the compiler's `/sourceDependencies` output), and replaces the MSBuild path in `build.cmd` and CI once both produce the same tests passing. It begins with a short design note because it is expensive to reverse.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write the design note and the CMake build. Done when: `docs/specs/2026-11-09-cmake-ninja-build-design.md` is reviewed and `cmake --build` builds every project.
2. Prove parity and switch. Done when: the CMake build passes the same suite, the boundary checks and CI, and `build.cmd` uses it.

- [ ] Write `docs/specs/2026-11-09-cmake-ninja-build-design.md`: the target layout (one target per project), how vcpkg, NuGet (`src/app`), resources, the manifest, ASan, the precompiled headers of `§4`, the sccache launcher of `§7` and the job pools are expressed, how the D13 boundary check reads `/sourceDependencies`, how Visual Studio opens it, and the cost of going back to MSBuild. Done when: the note exists and its review (through `scripts/review-dispatch.py --kind plan`) returns no P1.
- [ ] Add `CMakeLists.txt` at the root and one per project, with `CMakePresets.json` presets `x64-debug`, `x64-release` and `x64-asan` (and `arm64-*` for CI), using the `.tools/` CMake and Ninja and MSVC from the Visual Studio developer environment. Done when: `cmake --preset x64-debug` and `cmake --build --preset x64-debug` build every target with 0 warnings.
- [ ] Define Ninja job pools `compile` and `link`, sized by `build/build.ps1` from free memory as `§3` does (links at most 2), and assign every compile and link to them. Done when: a build's `ninja -d stats` and a sample of running processes show no more compiles and links than the pools allow.
- [ ] Port the D13 boundary check from `Directory.Build.targets` to a CMake step that reads each compile's `/sourceDependencies` JSON. Done when: it passes on the tree and refuses a `src/ui` file that includes a `src/pdf` header, as the MSBuild check does.
- [ ] Run the suite from the CMake build and compare it with the MSBuild build on the same commit. Done when: both list the same tests and both pass, and the clean and no-change build times of each are recorded in the findings file.
- [ ] Move `build.cmd`, `build/build.ps1` and `.github/workflows/build.yml` to the CMake build, keeping the MSBuild solution building in CI until a full milestone has passed on CMake. Done when: CI passes on the CMake build for x64 and ARM64 and `.\build.cmd` uses it locally.
- [ ] Commit: `build: CMake and Ninja with compile and link job pools`

**Test checkpoint:** `cmake --build --preset x64-debug` followed by the suite passes with 0 warnings, a no-change `cmake --build` runs no compile, and including a `src/pdf` header from `src/ui` fails the ported boundary check.

## Verification

- A plain and an ASan build of x64 Debug, built one after the other, recompile nothing the second time each, and `.\build.cmd` builds x64 only while CI builds ARM64.
- A clean build started with about 6 GB free keeps 2 GB free throughout, every project compiles through a precompiled header, and a warm sccache rebuild hits for every unit.
- `build/bootstrap-tools.ps1` fills `.tools/` from verified pins, the scripts and hooks run on the pinned CPython, and the CMake and Ninja build passes the suite and the boundary checks in CI.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
