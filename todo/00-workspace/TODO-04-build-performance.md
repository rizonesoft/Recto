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
- Every push's CI runs are followed to their end in the background, and a red one is the next work before any new row.
- No test takes longer than its budget: the slow tests are fixed at their cause, the run names its slowest tests, and a test over budget fails the run.
- The x64 suite runs in parallel shards sized from free memory, every test run exactly once, with its wall time measured against the serial run.

**Adjacency:** all=not-applicable (build tooling, scripts and CI for the developer and CI host; no user-facing surface, document or setting)

**Adjacency rationale:** Nothing here ships to users or reads a user's file. The tools fetched into `.tools/` are build inputs, pinned and checked like the SDK and Actions pins; the only persistent output is build artifacts under the ignored `build/` and `.tools/` folders, and the sccache cache directory, which holds compiler output only.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | ASan and plain builds in separate output folders | -- | [x] |
| 2 | §2 | ARM64 off the local build, built in CI, deferred to after Recto v1 | -- | [ ] |
| 3 | §3 | MSBuild and compiler parallelism set by free memory | §1 | [ ] |
| 4 | §4 | Precompiled headers for every project | §3 | [ ] |
| 5 | §5 | Tiered gates: touched tests while building, the full suite at stamp | §2 | [ ] |
| 6 | §6 | Repo-scoped pinned Ninja, CMake, sccache and CPython under `.tools/` | -- | [ ] |
| 7 | §7 | sccache as the compiler cache for MSVC | §6 | [ ] |
| 8 | §8 | CMake and Ninja build with compile and link job pools | §6, §7, §4 | [ ] |
| 9 | §9 | CI watched after every push until it completes | -- | [ ] |
| 10 | §10 | Slow tests fixed at their cause and held by a per-test time budget | -- | [ ] |
| 11 | §11 | The x64 test suite run in parallel shards within the memory bound | §3, §10 | [ ] |

## 1. ASan and plain builds in separate output folders

> **Started:** 2026-11-09T14:46Z

`Directory.Build.props` sends every build of a configuration to `build/obj/<project>/<platform>/<configuration>/` and `build/bin/<platform>/<configuration>/<project>/`, whether `EnableASAN` is set or not, so the object files of one overwrite the other's and each switch is a full rebuild (seen many times on 2026-11-09). This section gives ASan builds their own folders and keeps everything that reads the output paths (`build/build.ps1`, `build/asan-runtime.ps1`, the test runner paths, CI) pointed at the right one.

**Corrected 2026-11-09:** plan review (`docs/reviews/00-workspace/D00-T04-s1.md`). `build/build.ps1` does more than share folders: it records the last ASan state per configuration in `build/asan-state-<cfg>-<plat>.txt` and deletes the projects' obj and bin folders whenever it flips, so the cleanup and its state file go, with `tests/scripts/test_build_ps1.py`, which requires them, changed to prove the opposite. Isolation is proven by hashing every artifact of the other build before and after, in both directions; compiler work is counted by object files whose write time changed (`build/build-evidence.ps1`), checked against a run that touches one source and must count exactly that unit, so a count read from nothing cannot pass as zero.

**Corrected 2026-11-09:** completion review. `build-evidence.ps1` refuses a record or a comparison with no object file (exit 2) and takes only a real platform and configuration, lists every changed, appeared, gone and rewritten path, and is tested on a fake build tree (`tests.scripts.test_build_ps1.BuildEvidence`).

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Give ASan builds their own `IntDir` and `OutDir` in `Directory.Build.props`. Done when: an ASan build writes only under `build/obj-asan/` and `build/bin-asan/`.
2. Point `build/build.ps1` and `build/asan-runtime.ps1` at the folder of the build they ran. Done when: `build.ps1 -EnableASAN` runs the ASan `recto-tests.exe` and a plain build runs the plain one.

- [x] Set `IntDir` to `build\obj-asan\...` and `OutDir` to `build\bin-asan\...` in `Directory.Build.props` when `EnableASAN` is `true`, leaving the plain paths unchanged. Done when: an ASan build writes under `build/obj-asan/` and `build/bin-asan/` only, every plain artifact keeping its hash. Cheaper substitute: deleting the plain outputs before each ASan build, which is the full rebuild this section removes.
- [x] Read the test binary and the ASan runtime copy from the ASan folders in `build/build.ps1` and `build/asan-runtime.ps1`. Done when: `build.ps1 -EnableASAN -Configuration Debug -TestFilter 'PdfError.*'` runs `build\bin-asan\x64\Debug\recto-tests\recto-tests.exe`, and `build.ps1 -Platform x64 -Configuration Debug -TestFilter 'PdfError.*'` runs `build\bin\x64\Debug\recto-tests\recto-tests.exe`.
- [x] Retire the ASan-state cleanup in `build/build.ps1` (the `asan-state-*.txt` file and the deletion of obj and bin folders when it flips), and change `tests/scripts/test_build_ps1.py` to require that no build deletes the other mode's folders. Done when: the script holds no `asan-state` code and the test fails if the deletion is put back.
- [x] Add `build/build-evidence.ps1`, which records the SHA-256 of every file under a build's obj and bin folders and the write times of its `.obj` files, and compares two records: files changed, removed, and object files rewritten. Done when: a run after touching `src/pdf/error.cpp`'s timestamp reports exactly `error.obj` rewritten in each project that compiles it, and a run with no change reports none.
- [x] Prove the switch keeps both builds and rebuilds nothing, in both directions. Done when: plain x64 Debug, ASan, plain, ASan, each succeeding, with `build-evidence.ps1` showing the plain artifacts unchanged across each ASan build, the ASan artifacts unchanged across each plain build, and no object file rewritten on the third and fourth builds.
- [x] Update the CI ASan job and any script that names `build\bin\x64\Debug` for an ASan run (`.github/workflows/*.yml`, `scripts/`). Done when: a search for `bin\x64\Debug` in ASan steps finds none left, and `.github/workflows/build.yml`'s ASan job names no output path (it runs through `build/build.ps1`, which picks the folder). **Corrected 2026-11-09:** said the CI ASan job passes on the pushed commit, which is read after the stamp's push (`process-phase` step 3), not before it.
- [x] Commit: `build: give ASan builds their own output folders`

**Test checkpoint:** plain, ASan, plain and ASan x64 Debug builds in turn, each passing, rewrite no object file on the third and fourth and leave the other mode's artifacts byte-identical (`build/build-evidence.ps1`), and setting the ASan `IntDir` back to the plain one makes the fourth rewrite every object file.

> **Verified:** 2026-11-09 | §1 | 345146e195fa4db3cf62947c6e7af6e7ef3e5569
> **Review:** `docs/reviews/d00-t04-s1.json`

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

**Corrected 2026-11-10:** a check of the tree. No project, props or targets file sets `MultiProcessorCompilation` (a search for it, `CL_MPCount` and `UseMultiToolTask` in every `.vcxproj`, `Directory.Build.props`, `Directory.Build.targets` and `build/build.ps1` finds nothing), so the paragraph above is wrong about today's build: each project compiles its sources one at a time (24 in `recto-pdf`, 28 in `recto-tests`), and only MSBuild's `/m` runs projects side by side. This section therefore also turns `MultiProcessorCompilation` on for every project, in `Directory.Build.targets` (the layer that owns the shared `ClCompile` settings), so the limit computed here bounds compilers that really run in parallel, and it measures the clean build before and after.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Turn parallel compilation on for every project. Done when: a clean x64 Debug build's log shows `/MP` on every project's compile line and more than one `cl.exe` compiling `recto-pdf` at once.
2. Compute the limits in `build/build.ps1`. Done when: the script prints the free memory, the cores, and the `/m` and `CL_MPCount` it chose.
3. Prove the floor holds. Done when: a clean x64 Debug build started with about 6 GB free never reads under 2 GB free, sampled every second.

- [ ] Set `MultiProcessorCompilation` to `true` in the shared `ClCompile` item definition of `Directory.Build.targets`, for plain and ASan builds, leaving the two precompiled header sources (`/Yc` in `src/app` and `src/ui`) to compile first as MSBuild orders them. Done when: `build\msbuild-Debug-x64.log` shows `/MP` on the compile line of all eight projects, the build passes with 0 warnings, and `tests/scripts/test_build_ps1.py` fails if the setting is removed.
- [ ] Measure the clean x64 Debug and Release solution builds before and after the setting on the same host, with the free memory at the start of each. Done when: the four wall times and each build's peak count of `cl.exe` processes are recorded in the findings file, and the builds with the setting are faster.
- [ ] In `build/build.ps1`, read `\Memory\Available MBytes` and the logical processor count, and set the compilers allowed at once to `max(1, min(cores, floor((free_mb - 2048) / 1536)))`, passed as MSBuild `/m:<projects>` (at most 2 when fewer than 6 compilers fit) and `/p:CL_MPCount=<compilers per project>` with `MultiProcessorCompilation` on. Done when: the script's header line reads, for example, `free 9,500 MB, 12 cores: /m:2, CL_MPCount=4`, and the MSBuild log shows the same `CL_MPCount`.
- [ ] Let `RECTO_BUILD_JOBS` override the computed number for a run, and refuse a value under 1 or over the cores with a clear message. Done when: `RECTO_BUILD_JOBS=2` gives `2` compilers in all and `RECTO_BUILD_JOBS=0` stops the build before MSBuild starts.
- [ ] Measure it. Done when: a clean x64 Debug build with about 6 GB free samples `Available MBytes` every second and never reads under 2,048, and its wall time, with and without the limit, is recorded in the findings file.
- [ ] Commit: `build: choose MSBuild and compiler parallelism from free memory`

**Test checkpoint:** a clean `build.ps1 -Platform x64 -Configuration Debug -SkipTests` started with about 6 GB free shows `/MP` for every project, keeps `Available MBytes` at or above 2,048 throughout, and the same build with the limit removed falls below it.

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

## 9. CI watched after every push until it completes

> **Started:** 2026-11-09T15:17Z

The owner asked on 2026-11-09 that CI never stay red longer than it has to. Today `scripts/ci_status.py` reads the latest completed `build.yml` and `plan.yml` runs only at a section boundary (`process-phase` step 3) and at a heartbeat, so a push whose build fails is noticed only after the next section has started, and a run cancelled by a newer push reads as not red. This section adds a wait mode that follows the runs of one pushed commit to their end, and has `process-phase` start it in the background after every push, so a red is diagnosed and fixed before the next row is claimed.

**Corrected 2026-11-09:** plan review (`docs/reviews/00-workspace/D00-T04-s9.md`). What decides each case:
- Which workflows a push starts is read from the workflows themselves: `ci_status.py --wait <sha>` reads each watched workflow's `push` filter (`paths`, `paths-ignore`, `branches`) and the files `git diff --name-only <sha>^ <sha>` changed, so a stamp that touches only `todo/` and `docs/` expects `plan.yml` alone and a code-only push expects `build.yml` alone. An expected run that has not appeared within 10 minutes is `unknown` (exit 3), never green; a run that appears for a workflow not expected is watched too.
- Main builds are never cancelled by a newer push (each main commit has its own concurrency group in `build.yml`), so a cancelled run is a manual or infrastructure stop: `cancelled` (exit 5), never green.
- The verdict over all of the commit's runs: any red is red (exit 1, every failed job named), else any unknown is 3, else any cancelled is 5, else a timeout (default 60 minutes) is 4, else green (exit 0); one workflow's cancel or absence never hides another's failure.
- The watch writes `build/ci-watch/<sha>.json` (the sha, its process id, its start, each workflow's state and its exit when it ends), replaced atomically, so the heartbeat and the next session can tell a running watch from a finished or dead one.
- Consuming it: `process-phase` starts the watch in the background after every push and may go on with the current row while it runs; its notification is acted on when it arrives. Exit 1 is the red-first rule at once (finish or stop the step in hand at its next safe point, never during a review, and fix the red before anything else). Exit 5 is one `gh run rerun <id>`, watched again on the same sha. Exit 3 or 4 is a second watch, then a Critical event naming it. No new row is claimed, and no section stamps, while the newest push's watch has not exited 0. The heartbeat reads `build/ci-watch/<sha>.json`: a watch whose process is gone without an exit is started again for its sha, never a second one beside a live one.

**Corrected 2026-11-09:** the build. One state file for every watch let a newer push's watch overwrite an older one still running (two ran at once on the first day), so each watch writes `build/ci-watch/<sha>.json` and `--gate` reads the one of `origin/main`'s head; a push's changed files are the whole push (`--since`, the branch's old head), as GitHub filters it, not the head commit alone.

**Corrected 2026-11-09:** the first watches. GitHub reports a job stopped at its `timeout-minutes` as `cancelled` and says why only in its check-run annotation, so `main`'s build timed out twice (`5963438`, `9b8fb92`) while the snapshot report passed over both and read green from the run before. A cancelled run whose job carries the timeout annotation is now red in the report, `--wait` and `--scheduled` (`TimedOut` in `tests/scripts/test_ci_status.py`); the slow test behind the timeouts, `Scheduler.CallbackAdmission` at 479 s, was a cubic `plan_fetches` in `src/core/tile_scheduler.cpp`, fixed under `D01 T02 §9`.
**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add the wait mode to `scripts/ci_status.py`. Done when: `ci_status.py --wait <sha>` exits when that commit's runs end, with their verdict.
2. Use it after every push. Done when: `process-phase` step 3 and the `process-plan` heartbeat name it, and a red it reports is the next work.

- [x] Add `--wait <sha>` (with `--timeout <minutes>`, default 60, and `--poll <seconds>`, default 60) to `scripts/ci_status.py`: expected workflows from each workflow's `push` filters and the commit's changed files, each followed through `gh run list --commit <sha>` to completion, the verdict and exit codes as decided above, and `build/ci-watch/<sha>.json` written atomically as it goes. Done when: `tests/scripts/test_ci_status.py` drives it with a fake runner and clock through a stamp-only push (only `plan.yml` expected, green), a code-only push (only `build.yml`), an expected run appearing after three polls, an expected run never appearing (exit 3 after 10 minutes, not green), a red `build.yml` beside a green `plan.yml` (exit 1, the job named), a cancelled run beside a red one (exit 1), a cancelled run alone (exit 5), a rerun that turns the same run green after a red attempt (exit 0), and a timeout (exit 4).
- [x] Have `process-phase` step 3 start `python scripts/ci_status.py --wait <pushed sha>` with the shell's `run_in_background` option after every push, act on its exit as decided above, and refuse to claim a new row or stamp while the newest push's watch has not exited 0 (read from `build/ci-watch/<sha>.json`). Done when: `.claude/skills/process-phase/SKILL.md` says so with the command and each exit's action.
- [x] Have the heartbeat prompt in `.claude/skills/process-plan/SKILL.md` (and the one `CronCreate` installs) read `build/ci-watch/<sha>.json`: a live watch is left alone, a finished one's exit is acted on as above, a dead one (its process gone, no exit) is started again for its sha. Done when: the canonical prompt says so, and `scripts/tests/` and `tests/scripts/` pass.
- [x] Prove a delayed red is handled before the next row. Done when: `tests/scripts/test_ci_status.py` reads, for a watch that ended red after the next row was started, `ci_status.py --gate` (the check `process-phase` runs before claiming a row or stamping) exiting 1 and naming the red run, and exiting 0 only once a later watch on the fix's sha is green.- [ ] Pick the watch up in a new session: `process-plan`'s audit (`.claude/skills/process-plan/SKILL.md` section 1) reads `build/ci-watch/<sha>.json` and starts `--wait` for `origin/main`'s head when it has no finished green watch. **Corrected 2026-11-09:** owner question, whether a new session watches CI on its own. Done when: the audit names the step, and a session started with a dead watch file for the head restarts it.
- [x] Tell every session the CI state at its start: a `SessionStart` hook in `.claude/settings.json` runs `python scripts/ci_status.py --gate --brief` and prints one line (green, red with the run and its failed jobs, or a watch still running). Done when: a new session's first context shows that line, and a red `main` reads as red in it.
- [x] Pick the watch up in a new session: `process-plan`'s audit (`.claude/skills/process-plan/SKILL.md` section 1) reads `build/ci-watch/<sha>.json` and starts `--wait` for `origin/main`'s head when it has no finished green watch. **Corrected 2026-11-09:** owner question, whether a new session watches CI on its own. Done when: the audit names the step, and a session started with a dead watch file for the head restarts it.
- [x] Watch `main` while no session is open: a `recto-ci-watch` task in `.conclave/automations/tasks.json` every 15 minutes runs `python scripts/ci_status.py --scheduled`, which exits 1 once for each red run it has not reported before (remembered in `build/automations/ci-watch-reported.json`, written atomically) so the runner's failure email names that run once, and 0 otherwise; the README's task table lists it and `install.ps1` registers it. **Corrected 2026-11-09:** owner question, whether session-bound watching is enough; it is not when no session is open. Done when: `tests/scripts/test_ci_status.py` reads a red run reported once and not again on the next run, a new red run reported, and green exiting 0; the task is registered on this machine (`Get-ScheduledTask -TaskPath \Recto\`).
- [ ] Commit: `ci: watch every push's runs to their end and act on a red at once`

**Test checkpoint:** `python -m unittest tests.scripts.test_ci_status` passes; a red fake run makes `ci_status.py --wait <sha>` exit 1 with its failed job named, an expected run that never appears makes it exit 3, and `--gate` refuses while the newest watch is red; reading a cancelled or a missing run as green fails the tests.

## 10. Slow tests fixed at their cause and held by a per-test time budget

The suite, not the build, is most of a gate. In the last full local run (2026-11-09, `build/test-results-x64-Debug.xml` and `-Release.xml`, 511 tests) the Debug suite took 950 s and the Release suite 174 s, while the four solution builds took 13 to 53 s each; in CI the two build-and-test steps took 23 and 18 minutes of a 45 minute job that timed out twice (`5963438`, `9b8fb92`). Fourteen tests are 87% of the Debug time: `Scheduler.CallbackAdmission` 344 s (131 s in Release), `PdfDocument.ObjectCacheBudget` 105 s, `PdfStagedRevisions.ManyEdits` 52 s and `.Bounds` 38 s, `PdfInterpreter.SeparationAndDeviceN` 27 s, `PdfIncrementalWriter.RoundOneRegressions` 27 s, `PdfFilters.CancelWhileRunning` 23 s, `PdfInterpreter.FormXObjectRefusals` 22 s, `PdfInterpreter.TextFontCount` 17 s, `PdfSerialize.PayloadLimit` 17 s, `PdfDocument.OpenPageTreeLevelsShareOneBudget` 17 s, `PdfSerialize.Failures` 15 s, `Runtime.ShutdownUnderActiveTileLoad` 15 s and `PdfStagedRevisions.HistoryPastOneEditsLimit` 13 s. That run predates `3da4496`, which made `plan_fetches` linear, so `Scheduler.CallbackAdmission` is measured again first. The first slow test found this way was a cubic loop in the product, not a slow test, so each one is read for its cause before anything is changed: slow product code is fixed in the product, and a test that does more work than its claim needs is cut to what the claim needs, never to less. A budget then keeps the next slow test from arriving unnoticed.

The budget is a default, not a measured law: 20 s for a test in Debug, 5 s in Release and 60 s under AddressSanitizer, with any test that truly needs longer named with its reason. Changing it costs one number in `tests/recto-tests/time-budget.json`.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Measure every test and report the slow ones. Done when: `scripts/test-times.py` lists a run's tests by time from its gtest XML and names each one over budget.
2. Fix the slow tests at their cause. Done when: every test is within the budget or named as an exception with its reason, and each changed test still fails when the behaviour it proves is broken.
3. Enforce the budget in the build. Done when: `build/build.ps1` fails a run with a test over budget, naming it and its time.

- [ ] Add `scripts/test-times.py` (stdlib Python): `report <xml>...` prints the suite's total, the 15 slowest tests and each suite's time; `check <xml>... --budget <file> --mode debug|release|asan` exits 1 naming every test over its budget and its time, 0 otherwise, and exits 2 on an XML with no tests. Done when: `tests/scripts/test_test_times.py` drives both commands on fixture XML: a run within budget (0), one test over (1, named with its time), a listed exception within its own limit (0) and over it (1), and an empty report (2).
- [ ] Add `tests/recto-tests/time-budget.json`: the three default limits above and an `exceptions` list of `{test, mode, seconds, reason}`, read by `test-times.py check`, which refuses an exception with no reason or naming a test the XML does not hold. Done when: the file parses, and the tooling test reads an exception without a reason and one naming an unknown test both refused (exit 2) with the entry named.
- [ ] Measure the suite again on the current tree: `build.ps1 -Platform x64` then `python scripts/test-times.py report` on both XML files, and the same under `-EnableASAN -Configuration Debug`. Done when: the three totals and each mode's tests over budget are recorded in the findings file, with `Scheduler.CallbackAdmission`'s time after `3da4496` beside its 344 s and 131 s before.
- [ ] For `Scheduler.CallbackAdmission` (`tests/recto-tests/scheduler_tests.cpp`), find what its remaining time is spent on (a profile or timestamps around its phases) and fix that where it lives: in `src/core/tile_scheduler.cpp` if the product is slow, in the test if it waits or repeats beyond what callback admission needs. Done when: it runs within the budget in all three modes, and it still fails when a callback is admitted after its request was cancelled (the mutation named in the findings file).
- [ ] Do the same for `PdfDocument.ObjectCacheBudget` and `PdfDocument.OpenPageTreeLevelsShareOneBudget` (`tests/recto-tests/pdf_document_tests.cpp`). Done when: both run within the budget, and each still fails when its budget is not charged (one mutation per test, named in the findings file).
- [ ] Do the same for `PdfStagedRevisions.ManyEdits`, `.Bounds` and `.HistoryPastOneEditsLimit` (`tests/recto-tests/pdf_staged_revisions_tests.cpp`) and `PdfIncrementalWriter.RoundOneRegressions` (`tests/recto-tests/pdf_incremental_writer_tests.cpp`). Done when: all four run within the budget, any cost that grows faster than the edits it handles is fixed in `src/pdf` and not in the test, and each test still fails under its named mutation.
- [ ] Do the same for `PdfInterpreter.SeparationAndDeviceN`, `.FormXObjectRefusals` and `.TextFontCount` (`tests/recto-tests/pdf_interpreter_tests.cpp`) and `PdfSerialize.PayloadLimit` and `.Failures` (`tests/recto-tests/pdf_serialize_tests.cpp`). Done when: all five run within the budget and each still fails under its named mutation.
- [ ] Do the same for `PdfFilters.CancelWhileRunning` (`tests/recto-tests/pdf_filters_tests.cpp`) and `Runtime.ShutdownUnderActiveTileLoad` (`tests/recto-tests/runtime_tests.cpp`), keeping the cancel limits they prove (`D01 T02 §7`). Done when: both run within the budget or are exceptions whose reason says what the time is needed for, and each still fails under its named mutation.
- [ ] Fix or except every other test the new measurement finds over budget, the same way. Done when: `python scripts/test-times.py check` exits 0 for the Debug, Release and ASan runs, and every exception's reason says why the time is needed.
- [ ] Run `test-times.py report` and `check` from `build/build.ps1` after each configuration's tests (the mode from the configuration and `-EnableASAN`; no check under `-TestFilter`, which runs a part of the suite), failing the build on a test over budget. Done when: a full run prints the total and the slowest tests, a sleep added to one test that takes it past the budget fails `build.ps1` naming that test, and `tests/scripts/test_build_ps1.py` fails if the check is removed.
- [ ] Record the result. Done when: the findings file holds the Debug, Release and ASan suite totals before and after, the Debug total is at most 300 s and the Release total at most 60 s on this host (or the findings file says which measured cost stands in the way and where it is filed), and the next green `build.yml` run's two build-and-test steps are recorded beside the 23 and 18 minutes before.
- [ ] Commit: `tests: fix the slow tests at their cause and hold a per-test time budget`

**Test checkpoint:** `python -m unittest tests.scripts.test_test_times` passes; `build.ps1 -Platform x64` passes with `python scripts/test-times.py check` at exit 0 for Debug and Release; and a 25 s sleep added to one Debug test fails the build naming it.

## 11. The x64 test suite run in parallel shards within the memory bound

`build/build.ps1` runs `recto-tests.exe` as one process, one test after another, on a host with 12 logical processors. GoogleTest splits a run across processes by itself (`GTEST_TOTAL_SHARDS` and `GTEST_SHARD_INDEX`), so once `§10` has taken the slow tests out, the rest of the suite's wall time is cut by running shards side by side, as many as the free memory `§3` reads allows. This section adds the sharded run, proves every test still runs exactly once, keeps the tests that measure time or own a machine-wide resource in a serial pass of their own, and makes sure no shard outlives the run.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Make the suite safe to run side by side. Done when: two full runs of `recto-tests.exe` started at the same moment both pass, ten times in a row.
2. Run it in shards from `build/build.ps1`. Done when: a full run starts the shards, then the serial list, and reports every test exactly once.
3. Measure it. Done when: the sharded and the serial wall times are recorded and the sharded one is shorter.

- [ ] Find every test that cannot share the machine with another test process: a fixed file or folder name outside `TempDir`, a named kernel object, a fixed window class or single-instance check, the clipboard, or a time limit that a busy host breaks (the cancel limits of `D01 T02 §7`, `Ui.*` tests that launch `Recto.exe`). Done when: each is listed in the findings file with its file and line, and each is either fixed to use a name of its own (`TempDir`, a per-process suffix) or entered in `tests/recto-tests/serial-tests.txt`, one gtest filter pattern per line with its reason in a comment.
- [ ] Prove the rest runs side by side. Done when: two `recto-tests.exe` processes started together with `--gtest_filter` excluding the serial list both pass in 10 consecutive rounds in x64 Debug, and the same under ASan in 3.
- [ ] In `build/build.ps1`, when no `-TestFilter` is given, run the suite as N shard processes (`GTEST_TOTAL_SHARDS=N`, `GTEST_SHARD_INDEX=i`, the serial list excluded by `--gtest_filter`), then the serial list in one process, each writing `test-results-x64-<cfg>-shard<i>.xml` (or `-serial.xml`) and its console output to `build\test-x64-<cfg>-shard<i>.log`; N is the compiler count `§3` computes, at most 6, halved under `-EnableASAN`, and `RECTO_TEST_SHARDS` overrides it (1 is today's single run). Done when: the script prints the N it chose and why, a run with `RECTO_TEST_SHARDS=1` starts one process, and a failing test fails the build with that shard's log printed.
- [ ] Prove every test ran exactly once: after the run, compare the test names in all the XML reports with `recto-tests.exe --gtest_list_tests` (in `scripts/test-times.py`, as `coverage <list> <xml>...`), failing on a test missing or reported twice. Done when: `tests/scripts/test_test_times.py` reads a missing test and a duplicated test both refused by name, and a real run with one shard's XML deleted fails the build.
- [ ] Stop every shard's whole process tree when one shard fails to start, when the run passes a deadline (default 30 minutes, `RECTO_TEST_TIMEOUT_MINUTES`), and when `build.ps1` is interrupted, through a job object that kills on close. Done when: a shard made to hang is gone, with the `Recto.exe` it launched, within 10 seconds of the deadline, and `Get-Process recto-tests, Recto` finds nothing after the run fails.
- [ ] Feed the per-shard reports to `§10`'s `test-times.py report` and `check`, and keep CI's diagnostics upload (`build/test-results-*.xml` in `.github/workflows/build.yml`, which the new names match) and add `build/test-*.log` to it. Done when: the budget check reads all the reports of a sharded run, and a failed CI run's artifact holds every shard's XML and log.
- [ ] Measure the full x64 Debug and Release test runs serial (`RECTO_TEST_SHARDS=1`) and sharded on the same host, with the free memory at the start and the lowest `Available MBytes` during each. Done when: the four wall times are recorded in the findings file, the sharded runs are shorter, and free memory never read under 2,048 MB.
- [ ] Commit: `tests: run the x64 suite in parallel shards within the memory bound`

**Test checkpoint:** `build.ps1 -Platform x64` passes with the suite in shards and `python scripts/test-times.py coverage` reporting every listed test exactly once; deleting one shard's XML before the coverage check fails the build; and a hung shard is killed with its children at the deadline.

## Verification

- A plain and an ASan build of x64 Debug, built one after the other, recompile nothing the second time each, and `.\build.cmd` builds x64 only while CI builds ARM64.
- A clean build started with about 6 GB free keeps 2 GB free throughout, every project compiles through a precompiled header, and a warm sccache rebuild hits for every unit.
- `build/bootstrap-tools.ps1` fills `.tools/` from verified pins, the scripts and hooks run on the pinned CPython, and the CMake and Ninja build passes the suite and the boundary checks in CI.
- Every project's compile line shows `/MP`, no test runs past its budget (`python scripts/test-times.py check`), and the sharded suite reports every listed test exactly once.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
