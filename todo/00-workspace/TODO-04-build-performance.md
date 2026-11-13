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
- -> XREF: D00 T05 §2 -- ships portable C++23 and Python LSP/MCP servers (`scripts/clangd-lsp.py`, `scripts/python-lsp.py`, `scripts/lsp_mcp.py`, `.clangd`, `scripts/gen-compile-commands.py`) ahead of `§12`'s MSBuild item-evaluated compile database
- -> XREF: D00 T05 §4 -- ships `build.cmd -Quick`, free-RAM MSBuild `/m:<N>` + `/nr:false`, `build/restore.stamp` and `scripts/run-tests.py --quick` / `--full` ahead of `§3` and `§5`
- -> XREF: D00 T05 §5 -- hardens dual-runner unattended campaigns, sleep guard and moved-clone Desktop shortcuts beside `§9` and `§14`
- -> XREF: D06 T02 §12 -- clang-tidy runs over the compile database `§12` here writes, with the clang-tidy beside the clangd that `build/clangd.cmd` finds

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
- Every x64 Debug build writes `build/compile_commands.json`, clangd reads every source with no error, and a session finds definitions, references and compile errors through it.

**Adjacency:** all=not-applicable (build tooling, scripts and CI for the developer and CI host; no user-facing surface, document or setting)

**Adjacency rationale:** Nothing here ships to users or reads a user's file. The tools fetched into `.tools/` are build inputs, pinned and checked like the SDK and Actions pins; the only persistent output is build artifacts under the ignored `build/` and `.tools/` folders, and the sccache cache directory, which holds compiler output only.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | ASan and plain builds in separate output folders | -- | [x] |
| 2 | §2 | ARM64 off the local build, built in CI, deferred to after Recto v1 | -- | [x] |
| 3 | §3 | MSBuild and compiler parallelism set by free memory | §1 | [ ] |
| 4 | §4 | Precompiled headers for every project | §3 | [ ] |
| 5 | §5 | Tiered gates: touched tests while building, the full suite at stamp | §2 | [ ] |
| 6 | §6 | Repo-scoped pinned Ninja, CMake, sccache and CPython under `.tools/` | -- | [ ] |
| 7 | §7 | sccache as the compiler cache for MSVC | §6 | [ ] |
| 8 | §8 | CMake and Ninja build with compile and link job pools | §6, §7, §4 | [ ] |
| 9 | §9 | CI watched after every push until it completes | -- | [x] |
| 10 | §10 | Slow tests fixed at their cause and held by a per-test time budget | -- | [ ] |
| 11 | §11 | The x64 test suite run in parallel shards within the memory bound | §3, §10 | [ ] |
| 12 | §12 | clangd language server for Claude Code over a compile database from the MSBuild build | -- | [ ] |
| 13 | §13 | A red main reported from GitHub itself, with the workstation off | §9 | [ ] |
| 14 | §14 | Scheduled tasks that follow the clone when it moves, and say so when they do not | -- | [ ] |

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

> **Started:** 2026-11-13T08:35Z

The owner decided on 2026-11-09 that the local build is x64 only until Recto v1 ships, to halve every gate on the shared development host, while ARM64 stays a ship target. This section changes the local default, keeps ARM64 built in CI so a break is still caught on every push, and records the decision in the foundation plan so later sections that need an ARM64 host (`arm64-host` Needs) are not mistaken for done.

**Corrected 2026-11-13:** validated against the repository before build. Target architectures in `docs/plans/2026-10-13-recto-foundation.md` live in D15 (`Target OS and architectures`), the section 1 owner-decisions list, section 2's `Architectures` row, and section 7 Milestone M0's `Done when` cell rather than D4 (`UI framework`), so the dated `2026-11-09` owner decision is recorded in D15 and the section 1 owner-decisions list with section 2 and M0 aligned to match (`-Platform x64,ARM64` builds both in one command, while plain `.\build.cmd` defaults to `x64` locally until Recto v1). `build.cmd`, `README.md`, `CONTRIBUTING.md`, `AGENTS.md` Validation, and `build/build.ps1`'s `vswhere` missing-toolset error message also still state that plain `.\build.cmd` builds `ARM64` locally and requires the ARM64 compiler; all five are updated together, and `tests/scripts/test_build_ps1.py` locks the `@('x64')` parameter default, `-Platform ARM64` / `-Platform x64,ARM64` parsing, and `ARM64`-conditional `vswhere` component check.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Default `build/build.ps1` and `build.cmd` to x64, with `-Platform ARM64` still accepted. Done when: `.\build.cmd` builds and tests x64 Debug and Release only, and `tests/scripts/test_build_ps1.py` locks the default and overrides.
2. Keep ARM64 in CI and record the owner decision across the foundation plan and repo docs. Done when: CI builds ARM64 Debug and Release on the pushed commit and the foundation plan, `AGENTS.md`, `README.md` and `CONTRIBUTING.md` carry the updated contract.

- [x] Change the default `$Platform` in `build/build.ps1` to `@('x64')`, update the header/help comments in `build/build.ps1` and `build.cmd` and the `vswhere` missing-tools message so `ARM64` is named only when requested, keep `-Platform x64,ARM64` and `-Platform ARM64` working, and add regression coverage in `tests/scripts/test_build_ps1.py`. Done when: `python -m unittest tests.scripts.test_build_ps1` passes, `.\build.cmd` emits only `=== Recto Debug|x64` and `=== Recto Release|x64` headers and passes x64 tests, and `.\build.cmd -Platform ARM64 -SkipTests` builds `Debug|ARM64` and `Release|ARM64`. Cheaper substitute: dropping `ARM64` from `ValidateSet` or `Recto.sln`, which would break `-Platform ARM64` and CI.
- [x] Keep `.github/workflows/build.yml` passing `-Platform x64,ARM64` for both `Debug` and `Release`, and state in its header comment that `ARM64` is built in CI on every code push while local `.\build.cmd` defaults to `x64` (`D00 T04 §2`). Done when: `.github/workflows/build.yml` explicitly passes `-Platform x64,ARM64` on both steps and the pushed candidate's `build.yml` run builds `Debug|ARM64` and `Release|ARM64`. Cheaper substitute: letting `build.yml` rely on `build.ps1`'s default `$Platform`, which would silently stop building `ARM64` in CI.
- [x] Record the dated `2026-11-09` owner decision in `docs/plans/2026-10-13-recto-foundation.md` (D15, section 1 owner-decisions list, section 2 `Architectures`, and Milestone M0 `Done when`: `ARM64` stays a v1 ship target, built in CI on every code push, off the local `.\build.cmd` default until Recto v1, with reversing costing one default in `build/build.ps1`), and align `AGENTS.md` Validation, `README.md`, and `CONTRIBUTING.md`. Done when: all four documents state that `.\build.cmd` builds `x64` locally by default (`-Platform x64,ARM64` for both) while CI builds `x64` and `ARM64`. Cheaper substitute: updating `AGENTS.md` alone while leaving `README.md`, `CONTRIBUTING.md` and D15 claiming plain `.\build.cmd` builds `ARM64`.
- [x] Commit: `build: build x64 locally and ARM64 in CI`

**Test checkpoint:** `python -m unittest tests.scripts.test_build_ps1` passes, `.\build.cmd` builds only `|x64` configurations (`=== Recto Debug|x64` and `=== Recto Release|x64` in its output, zero `|ARM64`), `.\build.cmd -Platform ARM64 -SkipTests` builds `Debug|ARM64` and `Release|ARM64`, and the CI `build.yml` run on the pushed commit builds both ARM64 configurations.

> **Verified:** 2026-11-13 | §2 | b3d6e340f0ae792c9653bd204f2306e1da219fd9
> **Review:** `docs/reviews/d00-t04-s2.json`

## 3. MSBuild and compiler parallelism set by free memory

> **Started:** 2026-11-13T10:46Z

MSBuild `/m` starts one project build per core and `MultiProcessorCompilation` one compiler per core inside each, so on 12 cores a build can run many 1 to 2 GB compilers at once whatever is free; on 2026-11-09 free memory fell to 88 MB during builds. This section has `build/build.ps1` choose both limits from the free memory and cores when it starts, so a build takes what it can without starving the machine.

**Corrected 2026-11-10:** a check of the tree. No project, props or targets file sets `MultiProcessorCompilation` (a search for it, `CL_MPCount` and `UseMultiToolTask` in every `.vcxproj`, `Directory.Build.props`, `Directory.Build.targets` and `build/build.ps1` finds nothing), so the paragraph above is wrong about today's build: each project compiles its sources one at a time (24 in `recto-pdf`, 28 in `recto-tests`), and only MSBuild's `/m` runs projects side by side. This section therefore also turns `MultiProcessorCompilation` on for every project, in `Directory.Build.targets` (the layer that owns the shared `ClCompile` settings), so the limit computed here bounds compilers that really run in parallel, and it measures the clean build before and after.

**Corrected 2026-11-13:** `D00 T05 §4` added free-memory `/m:<N>` project-parallelism capping, `/nr:false` node-reuse disabling, `-Quick` / `-Project`, `-MaxCpuCount`, and `build/restore.stamp` to `build/build.ps1`; this section replaces `Get-RamBoundedJobs` with `build/parallelism.ps1` (`Read-AvailableMemoryMb` and `Resolve-BuildParallelism`, dot-sourced by `build/build.ps1` and tracked in `.gitignore`), turns `MultiProcessorCompilation` on in `Directory.Build.targets`, updates CLI help in `build/build.ps1` and `build.cmd`, and records the clean-build wall-time and memory-floor measurements. Validated against VC `v180` `Microsoft.Cl.Common.props:101` and plan review (`fe81c8f`): (1) MSBuild maps `CL_MPCount` directly to per-project `%(ClCompile.ProcessorNumber)` (`/MP<CL_MPCount>`) without dividing across `/m:<projects>` nodes, so simultaneous project builds can run up to `projects * CL_MPCount` `cl.exe` processes; `Resolve-BuildParallelism` therefore splits `total = min(cores, floor((free_mb - 2048) / 1536))` into `projects = 1` when `total < 4`, `projects = 2` when `4 <= total < 6`, and `projects = 3` when `total >= 6`, with `CL_MPCount = floor(total / projects)` (`projects * CL_MPCount <= total` everywhere; `9,500 MB` on 12 cores -> `free 9,500 MB, 12 cores: /m:2, CL_MPCount=2`). (2) Rather than clamping `max(1, ...)` below the `2,048 MB` reserve, automatic mode (`RECTO_BUILD_JOBS` unset and `-MaxCpuCount 0`) refuses before `vswhere` or MSBuild starts whenever `floor((free_mb - 2048) / 1536) < 1` (`free_mb < 3584`), naming the measured free memory, the `3,584 MB` minimum (`2,048 MB` reserve + `1,536 MB` for 1 compiler), and recovery options (free memory or set `RECTO_BUILD_JOBS=1` / `-MaxCpuCount 1`). (3) In automatic mode, `Read-AvailableMemoryMb` queries `\Memory\Available MBytes` first and falls back to `Win32_OperatingSystem.FreePhysicalMemory / 1024.0`; if both fail or return non-finite or `<= 0` data, `build/build.ps1` refuses before MSBuild starts instead of inventing a fallback worker count. (4) Precedence is explicit: non-empty `RECTO_BUILD_JOBS` overrides `-MaxCpuCount` and automatic sizing (refusing non-integers, `< 1`, or `> cores` before MSBuild starts); when `RECTO_BUILD_JOBS` is unset, `-MaxCpuCount 0` (the default) selects automatic memory-based sizing and explicit `-MaxCpuCount` outside `0..cores` (including `< 0` or `> cores`, changed from `[ValidateRange(0, 128)]` to resolver validation against live `cores`) refuses before MSBuild starts. On the shared 32 GB host, the four clean x64 Debug and Release before/after `/MP` benchmarks compare matched 2-compiler runs (`/m:2` before `/MP` vs `/m:1, CL_MPCount=2` with `/MP`), and the 1-second `Available MBytes` floor traces compare `build/build.ps1` starting near `4 GB` (`4,089 MB`, `min = 3,419 MB >= 2,048`) and `5.3 GB` (`5,457 MB`, `min = 4,086 MB >= 2,048`) against the same clean x64 Debug build with `/m:<projects>` and `CL_MPCount` limits removed (`MSBuild.exe Recto.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nr:false`, dropping from `6,171 MB` to `503 MB < 2,048`).

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Turn parallel compilation on for every project. Done when: a clean x64 Debug build's log shows `/MP` on every project's compile line and more than one `cl.exe` compiling `recto-pdf` at once when `CL_MPCount >= 2`.
2. Compute and enforce the memory and override limits in `build/parallelism.ps1` and `build/build.ps1`. Done when: the script prints the free memory, the cores, and the `/m` and `CL_MPCount` it chose, refuses `< 3,584 MB` free or failed memory detection in auto mode, and behavioral MSBuild argument-capture tests pass.
3. Prove the floor holds. Done when: a clean x64 Debug build started with ~4 GB free (`4,089 MB`, and also ~5.3 GB at `5,457 MB`) never reads under 2 GB free, sampled every second, while the same clean x64 Debug build with `/m:<projects>` and `CL_MPCount` limits removed (`/m` + `/MP`) falls below 2 GB free.

- [x] Set `MultiProcessorCompilation` to `true` in the shared `ClCompile` item definition of `Directory.Build.targets`, for plain and ASan builds, leaving the two precompiled header sources (`/Yc` in `src/app` and `src/ui`) to compile first as MSBuild orders them. Done when: `build\msbuild-Debug-x64.log` shows `/MP` on the compile line of all eight projects, the build passes with 0 warnings, and `tests/scripts/test_build_ps1.py` fails if the setting is removed.
- [x] Measure the clean x64 Debug and Release solution builds before and after the setting on the same host at matched compiler concurrency (`2` compilers: `/m:2` before `/MP` vs `/m:1, CL_MPCount=2` with `/MP`), with the free memory at the start of each. Done when: the four wall times, starting free memory, and each build's peak count of `cl.exe` processes are recorded in `docs/reviews/00-workspace/D00-T04-s3.md`, more than one `cl.exe` compiles `recto-pdf` at once under `/MP`, and the builds with `/MP` are faster.
- [x] Add `build/parallelism.ps1` (`Read-AvailableMemoryMb` and `Resolve-BuildParallelism`) and wire `build/build.ps1` to read `\Memory\Available MBytes` (falling back to `Win32_OperatingSystem.FreePhysicalMemory / 1024.0` when the counter fails, and refusing before MSBuild starts if both probes fail or return `<= 0` / non-finite data), refuse automatic mode below `3,584 MB` free (`floor((free_mb - 2048) / 1536) < 1`) with an actionable recovery message, and set `total = min(cores, floor((free_mb - 2048) / 1536))` split into `/m:<projects>` (`1` when `< 4`, `2` when `4..5`, `3` when `>= 6`, so `projects * CL_MPCount <= total`) and `/p:CL_MPCount=<compilers per project>` across solution, `-Project`, `-EnableASAN`, and `-t:Restore` (`/m:<projects>`) invocations. Done when: the header line prints `free <N> MB, <C> cores: /m:<M>, CL_MPCount=<K>`, the MSBuild log shows `/MP<K>` on every project's compile line, and `tests/scripts/test_build_ps1.py` verifies `3,583 MB` refusal, `3,584 MB` acceptance, counter success, CIM fallback, and dual-probe failure. Cheaper substitute: clamping `max(1, ...)` below `3,584 MB` or silently defaulting to 4 jobs when memory probes fail, violating the `2,048 MB` floor.
- [x] Let `RECTO_BUILD_JOBS` override automatic sizing and `-MaxCpuCount` for a run, keep `-MaxCpuCount 0` as automatic mode and `-MaxCpuCount <1..cores>` as the CLI override when `RECTO_BUILD_JOBS` is unset, refuse any `RECTO_BUILD_JOBS` (`< 1`, `> cores`, or non-integer) or `-MaxCpuCount` (`< 0` or `> cores`) with a clear message before `vswhere` or MSBuild starts, update the help comments in `build/build.ps1` and `build.cmd`, and add behavioral MSBuild argument-capture tests in `tests/scripts/test_build_ps1.py`. Done when: `tests/scripts/test_build_ps1.py` drives `build/build.ps1` against a fake `vswhere`/MSBuild shim and proves the captured `/m:<M>` and `/p:CL_MPCount=<K>` arguments across default solution, `-Project`, `-EnableASAN`, and `-t:Restore` paths, `RECTO_BUILD_JOBS` over `-MaxCpuCount` precedence, `-MaxCpuCount 0` auto mode, and pre-MSBuild refusals for `RECTO_BUILD_JOBS=0`, `-1`, `abc`, `<cores+1>`, and `-MaxCpuCount <cores+1>`. Cheaper substitute: source-string regex checks that never execute `build/build.ps1` or inspect the arguments MSBuild actually receives.
- [x] Measure the memory floor. Done when: a clean x64 Debug build on this host (~4 GB free at start, `4,089 MB`, and ~5.3 GB at `5,457 MB`) samples `\Memory\Available MBytes` every second and never reads under `2,048`, the same clean x64 Debug build with the `/m:<projects>` and `CL_MPCount` limits removed (`MSBuild.exe Recto.sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nr:false`) drops below `2,048`, and the wall times and minimum `Available MBytes` readings are recorded in `docs/reviews/00-workspace/D00-T04-s3.md`.
- [ ] Commit: `build: choose MSBuild and compiler parallelism from free memory`

**Test checkpoint:** `python -m unittest tests.scripts.test_build_ps1` passes (including behavioral MSBuild argument capture across solution, `-Project`, `-EnableASAN`, and `-t:Restore`, `< 3,584 MB` and failed-probe refusals, and `RECTO_BUILD_JOBS` / `-MaxCpuCount` precedence), a clean `build.ps1 -Platform x64 -Configuration Debug -SkipTests` shows `/MP` for all eight projects with 0 warnings and keeps 1-second sampled `Available MBytes` at or above `2,048` throughout, and the same clean x64 Debug build with `/m:<projects>` and `CL_MPCount` limits removed (`/m` + `/MP`) falls below `2,048`.

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
- [ ] Write `build/compile_commands.json` from CMake (`CMAKE_EXPORT_COMPILE_COMMANDS`) and retire `§12`'s MSBuild generator once `build.cmd` uses the CMake build. Done when: `§12`'s `build\clangd.cmd --check=src/pdf/filters.cpp` reports 0 errors against the CMake-written database and `scripts/build-compile-commands.py` is gone with its target.
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

**Corrected 2026-11-10:** completion review (`docs/reviews/00-workspace/D00-T04-s9.md`), thirteen gaps in scope, all built; what each decides:
- A red ends the watch the poll it is seen (exit 1, the failed jobs and the workflows still running named), because a red is acted on at once and its fix's push runs every workflow again; before, a red build waited for `plan.yml` or the 60-minute limit. Default taken, with its cost of changing: the `red or finished` condition in `wait()`.
- A failed read of one workflow is waited out and never hides another's red; a read that keeps failing for 10 minutes makes that workflow `unknown`.
- `--gate` admits a green only while it is still true: it reads main's head on GitHub (`git ls-remote`) and refuses when this clone has not fetched it, and it reads the commit's runs again and refuses a rerun in progress or one that turned red. Git or `gh` not answering is exit 3, never the recorded green.
- A watch started again for a commit keeps the first watch's `--since`, counts itself in `watches`, and is refused (exit 6) while the first is alive; every end's last line and every gate refusal carry `next:` with the exact command (wait, start a watch, pull, rerun a cancelled run, a second watch, a Critical event after a second unknown or timed-out end).
- A state file that cannot be written (full disk, locked file, refused folder) is exit 3 with the path and what to do, never exit 1; the old file stays, no temporary file is left, and a red already seen is still the watch's answer.
- The session's start line (`--gate --brief`) names the failed jobs on its one line.
- The scheduled task reports every red run in each workflow's last 10, not only the newest, and counts a run as reported only when `.conclave/automations/run.py` records that the email was sent (`"alert": "sent"` in `build/automations/recto-ci-watch/last.json`, for the run that raised it); until then each poll raises it again. On its first run only the standing red is news.
- The watch reads every 10 seconds by default (it was 60) and prints what it expects and each change of state with the seconds since it began. Not taken: a 3-second read, since every watch and `gh` call on this machine shares GitHub's 5,000 requests an hour, and three watches at 10 seconds already use about 2,200.

> **Deferred:** an alert that reaches the owner while this workstation is off -> XREF: §13
**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add the wait mode to `scripts/ci_status.py`. Done when: `ci_status.py --wait <sha>` exits when that commit's runs end, with their verdict.
2. Use it after every push. Done when: `process-phase` step 3 and the `process-plan` heartbeat name it, and a red it reports is the next work.

- [x] Add `--wait <sha>` (with `--timeout <minutes>`, default 60, and `--poll <seconds>`, default 60) to `scripts/ci_status.py`: expected workflows from each workflow's `push` filters and the commit's changed files, each followed through `gh run list --commit <sha>` to completion, the verdict and exit codes as decided above, and `build/ci-watch/<sha>.json` written atomically as it goes. Done when: `tests/scripts/test_ci_status.py` drives it with a fake runner and clock through a stamp-only push (only `plan.yml` expected, green), a code-only push (only `build.yml`), an expected run appearing after three polls, an expected run never appearing (exit 3 after 10 minutes, not green), a red `build.yml` beside a green `plan.yml` (exit 1, the job named), a cancelled run beside a red one (exit 1), a cancelled run alone (exit 5), a rerun that turns the same run green after a red attempt (exit 0), and a timeout (exit 4).
- [x] Have `process-phase` step 3 start `python scripts/ci_status.py --wait <pushed sha>` with the shell's `run_in_background` option after every push, act on its exit as decided above, and refuse to claim a new row or stamp while the newest push's watch has not exited 0 (read from `build/ci-watch/<sha>.json`). Done when: `.claude/skills/process-phase/SKILL.md` says so with the command and each exit's action.
- [x] Have the heartbeat prompt in `.claude/skills/process-plan/SKILL.md` (and the one `CronCreate` installs) read `build/ci-watch/<sha>.json`: a live watch is left alone, a finished one's exit is acted on as above, a dead one (its process gone, no exit) is started again for its sha. Done when: the canonical prompt says so, and `scripts/tests/` and `tests/scripts/` pass.
- [x] Prove a delayed red is handled before the next row. Done when: `tests/scripts/test_ci_status.py` reads, for a watch that ended red after the next row was started, `ci_status.py --gate` (the check `process-phase` runs before claiming a row or stamping) exiting 1 and naming the red run, and exiting 0 only once a later watch on the fix's sha is green.
- [x] Tell every session the CI state at its start: a `SessionStart` hook in `.claude/settings.json` runs `python scripts/ci_status.py --gate --brief` and prints one line (green, red with the run and its failed jobs, or a watch still running). Done when: a new session's first context shows that line, and a red `main` reads as red in it.
- [x] Pick the watch up in a new session: `process-plan`'s audit (`.claude/skills/process-plan/SKILL.md` section 1) reads `build/ci-watch/<sha>.json` and starts `--wait` for `origin/main`'s head when it has no finished green watch. **Corrected 2026-11-09:** owner question, whether a new session watches CI on its own. Done when: the audit names the step, and a session started with a dead watch file for the head restarts it.
- [x] Watch `main` while no session is open: a `recto-ci-watch` task in `.conclave/automations/tasks.json` every 15 minutes runs `python scripts/ci_status.py --scheduled`, which exits 1 once for each red run it has not reported before (remembered in `build/automations/ci-watch-reported.json`, written atomically) so the runner's failure email names that run once, and 0 otherwise; the README's task table lists it and `install.ps1` registers it. **Corrected 2026-11-09:** owner question, whether session-bound watching is enough; it is not when no session is open. Done when: `tests/scripts/test_ci_status.py` reads a red run reported once and not again on the next run, a new red run reported, and green exiting 0; the task is registered on this machine (`Get-ScheduledTask -TaskPath \Recto\`).
- [x] Report a red the poll it is seen, keep it through a sibling's failed read, and wait a short outage out, in `wait()` of `scripts/ci_status.py`. Done when: `WaitAtOnce` in `tests/scripts/test_ci_status.py` reads exit 1 at 0 seconds with `plan.yml` still pending and named, exit 1 with `plan.yml` unreadable, exit 0 after three failed reads, and exit 3 with `gh could not read it for 10 minutes` after ten minutes of them.
- [x] Refuse a stale green in `gate()`: the remote head through `git ls-remote origin refs/heads/main`, and the commit's runs read again. Done when: `Gate` reads exit 6 for a head this clone has not fetched (naming `git pull --ff-only`), exit 6 for a rerun in progress, exit 1 for a rerun gone red with its failed job, and exit 3 when `gh` fails, each on a watch file that says green.
- [x] Keep the push range and the count across restarts and never double a live watch. Done when: a watch started without `--since` over a state file that has one diffs that range and records `watches` 2, and one started beside a live process exits 6 and leaves the file byte for byte.
- [x] Name the next command for every end and every refusal (`next_step()`). Done when: `Gate.test_each_other_end_names_its_own_recovery` reads `gh run rerun <id>` for a cancelled end, a second watch for a first unknown or timed-out end, and the Critical event for a second.
- [x] Make a failed state write unknown, not red (`write_state()`). Done when: a full disk, a locked file and a refused folder each make `--wait` exit 3 naming the path with no `.tmp` left, the old file is unchanged, a watch that saw a red still exits 1, and `--scheduled` exits 3.
- [x] Put the failed jobs on the session's start line. Done when: `--gate --brief` over a red watch prints one line that names both failed jobs and exits 0.
- [x] Report every unreported red run, and forget one only after its email is sent: `scheduled()` with `alert_delivered()`, and `"alert"` in `last.json` written by `.conclave/automations/run.py`. Done when: `Scheduled` reads a red raised again with no word from the runner, with `not sent`, and with a `sent` from another moment, then never after this run's `sent`; two reds and a later green between two polls all named; history skipped on the first run; and `scripts/tests/test_automations.py` reads `sent` and `not sent` in `last.json`.
- [x] Print the watch as it runs, at a 10-second default. Done when: `WaitAtOnce.test_progress_is_printed_as_it_happens` reads the expectation line, the registration wait, each change with its seconds, and the closing `next:` line.
- [x] Prove the proof: mutate each new behaviour and run the tests. Done when: the findings file lists every mutant of `scripts/ci_status.py` with `killed`, none surviving.
- [x] Commit: `ci: watch every push's runs to their end and act on a red at once`

**Test checkpoint:** `python -m unittest tests.scripts.test_ci_status` passes; a red fake run makes `ci_status.py --wait <sha>` exit 1 with its failed job named, an expected run that never appears makes it exit 3, and `--gate` refuses while the newest watch is red; reading a cancelled or a missing run as green fails the tests.

> **Verified:** 2026-11-10 | §9 | 2f838d200748f253583467b7437270061e1caf1e
> **Review:** `docs/reviews/d00-t04-s9.json`

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

## 12. clangd language server for Claude Code over a compile database from the MSBuild build

A session that needs a symbol's definition, its callers or its type today searches the sources and reads pieces of them, and the sources are large: `src/pdf/filters.cpp` is 436 KB, `tests/recto-tests/pdf_filters_tests.cpp` 383 KB, `pdf_interpreter_tests.cpp` 320 KB and `src/pdf/document.cpp` 156 KB (measured 2026-11-10). A language server answers those questions in a few lines and reports a compile error without a build. Nothing is set up: the repository has no `compile_commands.json`, no `.clangd` and no language server configuration under `.claude/`, and MSBuild writes no compile database. `clangd.exe` is already on this host inside Visual Studio 2026 (`VC\Tools\Llvm\x64\bin\clangd.exe`, the C++ Clang tools component), so no new tool is fetched. This section writes the compile database from the build that ran, points clangd at it, proves clangd reads every source as MSVC compiles it (`/std:c++23preview`, the Windows SDK, the vcpkg and NuGet include folders), connects it to Claude Code, and bounds its memory, because this host runs short of memory. `§8` later writes the same database from CMake.

Two defaults, each with its cost of changing: clangd comes from Visual Studio and is not pinned in `.tools/` (moving it into `§6`'s lock costs one lock entry and one line in `build/clangd.cmd`), and the database is x64 Debug only (another configuration costs one argument to the generator).

**Corrected 2026-11-13:** `D00 T05 §2` already shipped `.clangd`, portable `.vcxproj`-parsed `scripts/gen-compile-commands.py` (`build/compile_commands.json` with 0 errors across all 68 units), `scripts/clangd-lsp.py`, `scripts/python-lsp.py` and `scripts/lsp_mcp.py` wired into `.lsp.json`, `.mcp.json` and `.agents/plugins/recto-lsp/`; this section therefore builds on those servers to add the MSBuild item-evaluated `RectoCompileCommands` target (`Directory.Build.targets`) and the indexing working-set benchmark before `§8` switches the database to CMake.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write the compile database from the build. Done when: an x64 Debug build leaves `build/compile_commands.json` with one entry for every source the build compiled.
2. Prove clangd reads the tree. Done when: `clangd --check` reports 0 errors for every source, within a memory bound.
3. Connect it to Claude Code and the working rules. Done when: a new session answers a definition and a references query through the language server, and a type error shows without a build.

- [ ] Add a `RectoCompileCommands` target to `Directory.Build.targets`, after `ClCompile` for all eight projects, that writes each compiled item's full path with the include folders, preprocessor definitions, language standard, forced includes, precompiled header and other options `cl.exe` was given, to `build\compile-commands\<platform>\<configuration>\<project>.txt`, read from the items as compiled (as `RectoBoundaryManifest` does), never from a second evaluation. Done when: an x64 Debug build writes eight files that together name every `.cpp` the log shows compiled.
- [ ] Add `scripts/build-compile-commands.py` (stdlib Python), which turns those files into `build/compile_commands.json` (one `directory`, `file` and `arguments` entry for each source, in `clang-cl` form), written to a temporary file and then renamed, and run it from `build/build.ps1` after the x64 Debug build. Done when: `tests/scripts/test_compile_commands.py` reads a fixture of two projects into the expected JSON, a source with spaces and a quoted definition survive, a project file that is missing or empty is refused by name (exit 2), and a real build's database holds as many entries as the build compiled sources.
- [ ] Keep the database current: a source added to or removed from a project appears in or leaves the database on the next build. Done when: the test adds and removes a unit in the fixture and reads both, and `build/build.ps1` fails when the generator fails.
- [ ] Add `build/clangd.cmd`, which finds `clangd.exe` in the Visual Studio installation `build/build.ps1` selects (through `vswhere`, component `Microsoft.VisualStudio.Component.VC.Llvm.Clang`) and runs it with the arguments given, and stops with the missing component's name when it is not installed. Done when: `build\clangd.cmd --version` prints clangd's version, and with the component's folder hidden it exits non-zero naming the component.
- [ ] Add a tracked `.clangd` at the repository root: the compile database folder (`build`), and the removal or replacement of each `cl.exe` option clangd does not accept, each with a comment saying which option and why. Done when: `build\clangd.cmd --check=src/pdf/error.cpp` reports 0 errors and no unknown-argument warning.
- [ ] Prove every source reads as MSVC compiles it: run `build\clangd.cmd --check=<file>` over every database entry from `scripts/build-compile-commands.py check`. Done when: every source reports 0 errors, or each one that does not is listed in the findings file with its diagnostic, and its cause is fixed in `.clangd` or the generator (never by editing the source to please clangd); the count checked equals the count of database entries.
- [ ] Bound clangd's memory: run it with a fixed worker count (`-j=2`), precompiled preambles on disk (`--pch-storage=disk`) and the background index on, and measure its working set while it indexes the tree and while `src/pdf/filters.cpp` is open. Done when: both peaks are recorded in the findings file, and if the indexing peak passes 3,072 MB the background index is turned off in `build/clangd.cmd` and the peak measured again under it.
- [ ] Connect clangd to Claude Code for this repository through its language server support, with `build\clangd.cmd` as the server command for C and C++ files, in the tracked configuration under `.claude/` if Claude Code reads one there and otherwise by the documented install step written into `CONTRIBUTING.md`; the mechanism is read from Claude Code's own documentation and named in the findings file. Done when: in a new session a definition query on a function declared in `src/pdf/document.h` returns its file and line, a references query on it returns its call sites, and a deliberate type error in a scratch copy of a source is reported without a build.
- [ ] Tell the writers to use it: one line in `AGENTS.md`'s Working rules and in `.claude/agents/code-explorer.md` (definition, references, type and diagnostics through the language server before searching and reading sources; search stays for text that is not a symbol). Done when: both files carry the line and `python -m unittest discover -s scripts/tests` passes.
- [ ] Measure the saving on one real lookup: every caller of one function in `src/pdf/filters.cpp`, found once by search and reading and once through the language server. Done when: the bytes of tool output each way, and whether both found the same callers, are recorded in the findings file.
- [ ] Commit: `build: clangd over a compile database written from the MSBuild build`

**Test checkpoint:** `python -m unittest tests.scripts.test_compile_commands` passes; after `build.ps1 -Platform x64 -Configuration Debug -SkipTests`, `python scripts/build-compile-commands.py check` reports every database entry checked by clangd with 0 errors; and removing a project's file from `build\compile-commands\x64\Debug` makes the generator exit 2 naming it.

## 13. A red main reported from GitHub itself, with the workstation off

`§9` notices a red `main` from this workstation: the session's watch while a session is open, and the `recto-ci-watch` task every 15 minutes while the operator is logged on. With the machine off or logged out nothing tells the owner, and the task's 15 minutes is slower than GitHub's own notifications. `§9`'s completion review (`docs/reviews/00-workspace/D00-T04-s9.md`, 2026-11-10) filed this: the alert must leave from GitHub within a minute of the run ending, with no workstation involved and no new credential. Decided, as a default with its cost of changing (one job in each workflow): a last job in `build.yml` and `plan.yml` that runs only on a push to `main` whose earlier jobs failed, were cancelled or timed out, and opens (or comments on) one issue labelled `ci-red` assigned to the repository owner through the workflow's own `GITHUB_TOKEN`, so GitHub's notification email reaches the owner; the next green push run closes it. No SMTP secret is stored. The local watch and task of `§9` stay as they are.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Add the alert job and its script. Done when: `scripts/ci_red_issue.py` opens, comments on and closes the `ci-red` issue through `gh`, driven by a fake `gh` in its tests.
2. Wire it into both workflows. Done when: a failed push run on `main` leaves one open `ci-red` issue naming the run, and the next green run closes it.

- [ ] Write `scripts/ci_red_issue.py` (stdlib, `gh` injectable): `report --workflow <file> --run <id> --sha <sha> --jobs <json>` opens an issue titled `main is red: <workflow>` labelled `ci-red` and assigned to the repository owner when none is open for that workflow, and otherwise adds one comment naming the new run; the body names the commit, the run's URL and each failed, cancelled or timed-out job. Done when: `tests/scripts/test_ci_red_issue.py` reads one `gh issue create` for a first red, one `gh issue comment` and no second issue for the next, and the failed jobs in the body.
- [ ] Add `resolve --workflow <file> --run <id> --sha <sha>`: closes the open `ci-red` issue of that workflow with a comment naming the green run, and does nothing when none is open. Done when: the test reads one `gh issue close` after a red and none on a main that was never red.
- [ ] Refuse quietly what it must not act on: a pull request run, a branch other than `main`, and a `gh` that fails (exit 3 with the reason, the job red so the failure shows in the run). Done when: the test reads no `gh issue` call for a pull request event and exit 3 for a failing `gh`.
- [ ] Add a last job `ci-red` to `.github/workflows/build.yml` and `.github/workflows/plan.yml`: `needs` every other job, `if: always() && github.event_name == 'push' && github.ref == 'refs/heads/main'`, `permissions: issues: write`, running `report` when any needed job's result is `failure`, `cancelled` or timed out, and `resolve` when all succeeded. Done when: `tests/scripts/test_ci_red_issue.py` reads both workflow files and finds the job, its `needs` covering every other job, its condition and its permission, and `python scripts/check-actions-pins.py check` passes.
- [ ] Teach `scripts/ci_status.py` that the `ci-red` job is the reporter, not a verdict: a run whose only failed job is `ci-red` is named as an alert failure, and the job is never listed among a red run's failed jobs. Done when: `tests/scripts/test_ci_status.py` reads both cases.
- [ ] Prove it once on GitHub: push a commit to a scratch branch protected from nothing, or use `workflow_dispatch` with an input that fails one job on purpose, and read the issue opened, then closed by the next green run. Done when: the findings file records the two run URLs, the issue number, the seconds from the run's end to the issue, and the issue closed.
- [ ] Document it in `.conclave/automations/README.md` beside `recto-ci-watch` and in `scripts/ci_status.py`'s module text. Done when: both say which alert leaves from GitHub and which from this machine.
- [ ] Commit: `ci: open one issue for a red main from the workflow itself, and close it when green`

**Test checkpoint:** `python -m unittest tests.scripts.test_ci_red_issue tests.scripts.test_ci_status` passes; a second red run adds a comment and never a second issue; and removing the `ci-red` job from either workflow fails the test.

## 14. Scheduled tasks that follow the clone when it moves, and say so when they do not

Found on 2026-11-10 while proving `§9` end to end: every task under `\Recto\` in Windows Task Scheduler still ran `D:\GitHub\Recto\.conclave\automations\run.py` after the clone moved to `D:\Recto`, so each start failed with `0x8007010B` (the directory name is invalid) and `recto-ci-watch`, `recto-pr-merge`, `recto-python-pin` and `recto-visual-studio` had done nothing since 2026-11-09T20:25Z. Nothing reported it: the runner that emails a failure is the thing that could not start. The tasks were registered again for this clone by hand (`pwsh .conclave/automations/install.ps1`), and a real `recto-ci-watch` run then exited 0. `scripts/start-unattended-run.ps1 -InstallShortcut` already rewrites the Desktop shortcut after a move; the tasks have no such check. This section adds one that runs where a session already looks at the machine, and repairs the registration when it is stale.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add a check to the installer. Done when: `pwsh .conclave/automations/install.ps1 -Check` exits 0 when every task in `tasks.json` is registered for this clone and its last result is not a start failure, and 1 with one line per stale task otherwise.
2. Run it where a session starts. Done when: a session opened on a clone whose tasks point elsewhere is told so in its first context, with the command that repairs it.

- [ ] Add `-Check` to `.conclave/automations/install.ps1`: for each task in `tasks.json`, compare the registered action's script path and working directory with this clone, and read `LastTaskResult`; print `stale: <task>: registered for <path>, this clone is <path>` or `failing: <task>: last result <hex>` and exit 1, or `automations: <n> task(s) registered for this clone` and exit 0; a task that is not registered at all is `missing: <task>`. It changes nothing. Done when: `scripts/tests/test_automations.py` drives the check through a function that takes the registered tasks as data (so the test needs no Task Scheduler) and reads each of the three lines and exit codes.
- [ ] Share the comparison between `-Check` and the install path, so a task registered for another clone is replaced by an install and never left beside a new one. Done when: the test reads an install over a stale registration producing one task per name, registered for this clone.
- [ ] Print the check at a session's start: a `SessionStart` hook entry in `.claude/settings.json` beside the CI line runs `pwsh -NoProfile -File .conclave/automations/install.ps1 -Check` (on Windows only), bounded to 20 seconds, and its one-line result is the session's first context; it never fails the hook. Done when: a session started with a task registered for another path shows the `stale:` line and `next: pwsh .conclave/automations/install.ps1`.
- [ ] Have `scripts/start-unattended-run.ps1` print the same check with its other start-up lines, as a warning when it exits 1. Done when: `tests/scripts/test_start_unattended_run.py` reads the warning with a fake check that exits 1 and no warning with one that exits 0.
- [ ] Give Linux and WSL the same: `install.sh --check` compares each unit's `ExecStart` and `WorkingDirectory` with this clone. Done when: `scripts/tests/test_automations.py` reads a stale unit reported through the units `run.py --units` writes.
- [ ] Document it in `.conclave/automations/README.md` ("Install and inspect") and in `AGENTS.md`'s Validation block. Done when: both show the check command and say what a stale registration looks like (`LastTaskResult` `0x8007010B` on every task).
- [ ] Commit: `automations: check that the scheduled tasks are registered for this clone`

**Test checkpoint:** `python -m unittest discover -s scripts/tests` passes; `pwsh .conclave/automations/install.ps1 -Check` exits 0 on this machine; and registering one task for another folder (in the test's data) makes the check exit 1 naming it.

## Verification

- A plain and an ASan build of x64 Debug, built one after the other, recompile nothing the second time each, and `.\build.cmd` builds x64 only while CI builds ARM64.
- A clean build started with about 6 GB free keeps 2 GB free throughout, every project compiles through a precompiled header, and a warm sccache rebuild hits for every unit.
- `build/bootstrap-tools.ps1` fills `.tools/` from verified pins, the scripts and hooks run on the pinned CPython, and the CMake and Ninja build passes the suite and the boundary checks in CI.
- Every project's compile line shows `/MP`, no test runs past its budget (`python scripts/test-times.py check`), and the sharded suite reports every listed test exactly once.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
