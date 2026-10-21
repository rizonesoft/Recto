---
schema_version: 1
id: repo-and-build
domain: 00-workspace
status: done
title: "TODO-01 -- Repository, Native Engine Policy and Build Scaffold"
track: M0
---

# TODO-01 -- Repository, Native Engine Policy and Build Scaffold

> **Goal:** Turn this folder into a building, CI-verified repository that satisfies M0 of the foundation plan: git initialized and pushed, `GPL-3.0-or-later` and contributor files in place, the native C++20 `recto-pdf` dependency policy established (zero MuPDF), the Visual Studio 2026 solution scaffold compiling for both x64 and ARM64 in one command, and reference benchmark machines recorded.

> **Current state:** Measured 2026-10-14. Sections §1 and §2 are verified in git history; on 2026-10-14 decisions D2, D3, D5, D7, D9, D10 and D12 were revised so Recto is licensed under `GPL-3.0-or-later` and builds its own native C++20 PDF engine (`recto-pdf` in `src/pdf`), built-in OCR (`Windows.Media.Ocr`), and native OpenXML converters instead of Artifex MuPDF. `Recto.sln` and `build.cmd` build the seven native C++20 projects (`recto-pdf`, `recto-core`, `recto-services`, `Recto`, `recto-cli`, `recto-tests`, `recto-fuzz`) across `Debug|x64`, `Release|x64`, `Debug|ARM64` and `Release|ARM64`.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D1 to D17), 2 (tech stack), 3.1 and 3.2 (modules and layout), 7 (M0 definition of done), 8 (reference machines), 10 (GPL-3.0 compliance), 11 (open items).
- `design/SPEC.md` section 3.5 (`design/brand/github-banner.png` and `github-social-preview.png`) and `resources/README.md` (how `resources/` maps into `Package.appxmanifest`).
- -> XREF: D00 T02 §3 -- the plan CI gate authored there runs beside the product CI workflow built in §4 here.
- -> XREF: D99 T01 §2 -- the operator finishes the GitHub repo face (social preview upload, description, branch protection) once §1 here has pushed the initial tree.
- -> XREF: D07 T02 §2 -- the `NOTICE.md` skeleton started in §1 here is completed and audited against the shipped dependency set before Store submission.

## Outcome

- A clean clone on a Windows 11 developer machine builds x64 and ARM64 in one documented command with zero errors and zero warnings across all Recto projects.
- GitHub Actions runs on every push and pull request, building both architectures and gating on `todo-graph.py validate` and `plan --check`.
- `LICENSE` (`GPL-3.0-or-later`), `CLA.md`, `NOTICE.md`, `README.md`, `CONTRIBUTING.md`, `.gitignore`, `.gitattributes`, `Recto.sln` and `Directory.Build.props` exist at the paths foundation plan section 3.2 fixes, with zero MuPDF submodule dependencies.
- The primary `x64` reference benchmark machine (with exact hardware, OS, display, storage, and toolchain specifications) and the secondary `ARM64` cross-compile target profile are recorded in `docs/reference-machines.md` so milestone M2 and M6 performance claims have a fixed baseline (with naming a specific physical `ARM64` machine deferred to `D99 T01 §4`).

**Adjacency:** all=not-applicable (repository, build toolchain and CI scaffolding; no product data, views or user actions exist in this file)

**Adjacency rationale:** Every section here produces build inputs, project files or developer documentation consumed by later domains. The one user-visible artifact touched (`NOTICE.md`, surfaced in About at M6) has its display and completeness audit owned by D07 T02 §1 and §2.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Git repository and repo-face files | -- | [x] |
| 2 | §2 | PDF engine dependency policy (native `recto-pdf`, MuPDF retired) | §1 | [x] |
| 3 | §3 | Solution scaffold for all modules, x64 and ARM64 | §2 | [x] |
| 4 | §4 | Build CI for x64 and ARM64 | §3, D00 T02 §2 | [x] |
| 5 | §5 | Reference benchmark machines recorded | §1 | [x] |

## 1. Git repository and repo-face files

Initialize the repository (or verify the existing remote if already created), set line-ending and ignore rules so `build/` and Visual Studio intermediates can never be committed, and author the root files every visitor and contributor sees first: `LICENSE` (`GPL-3.0-or-later`), `CLA.md` (D11), `NOTICE.md` (skeleton naming the components that exist now and the ones M1+ adds), `README.md` with `design/brand/github-banner.png`, and `CONTRIBUTING.md` pointing at `AGENTS.md` and `todo/README.md`.

> **Corrected:** 2026-10-14 -- Updated `LICENSE`, `README.md`, `NOTICE.md`, `CONTRIBUTING.md` and `CLA.md` from `AGPL-3.0-or-later` to `GPL-3.0-or-later` under revised decision D2 (`LICENSE` is the unmodified FSF GPL-3.0 text, 35,149 LF bytes, sha256 `0c2b2577702544e6ca2110800c25129ef79a7277e74f888ae852afb90cb363b4`).

> **Audit 2026-10-21 (D06 T05 §1):** `LICENSE` was restored to the verbatim GPL-3.0 text in `2affdc1`; it is still 35,149 LF bytes and its sha256 is now `3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986` (checked by `tests/scripts/test_license.py`). `build/build.ps1` is tracked by design (`D00 T01 §3`), the one exception `.gitignore` names to "no file under `build/` is tracked". The audit added the crash-dump ignores, the `.svg`, `.xaml`, Visual Studio, `.cmd` and image attributes, and the DCO sign-off this section promised.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Write `.gitignore` (`build/`, `.vs/`, `bin/`, `obj/`, `*.user`, `*.suo`, `__pycache__/`, `vcpkg_installed/`, crash dumps) and `.gitattributes` (LF for `.py`, `.md`, `.json`, `.svg`, `.xaml`, `LICENSE`; CRLF for `.sln`, `.vcxproj`, `.props`, `.targets`, `.cmd`; binary for `.png`, `.ico`, `.pdf`). Done when: `git status --ignored` shows `build/` ignored and `git check-attr -a` agrees on a sample of each extension.
2. Place the unmodified GNU General Public License v3.0 text in `LICENSE`, author `CLA.md` per D11 (DCO + grant to the project owner to relicense contributions), and author `NOTICE.md` listing current assets (Newsreader OFL 1.1 outlines in `resources/svg/` and `design/brand/`) and the components that enter in M0 to M6 (`recto-pdf` in-tree C++20 engine, Windows App SDK, GoogleTest as test-only). Done when: `LICENSE` matches the FSF text byte for byte and `CLA.md` states the grant in plain English.
3. Write `README.md` (banner from `design/brand/github-banner.png`, what Recto is, status as early M0, links to `design/SPEC.md`, the foundation plan and `todo/TODO-00-INDEX.md`, license `GPL-3.0-or-later`) and `CONTRIBUTING.md` (how to validate the TODO graph, build prerequisites, CLA requirement). Commit the initial tree and push `main` to `origin`. Done when: a fresh clone of the remote contains all files and `python scripts/todo-graph.py validate` passes in the clone.

- [x] `.gitignore`, `.gitattributes`, `LICENSE`, `CLA.md`, `NOTICE.md`, `README.md`, `CONTRIBUTING.md` committed at the root. Done when: `git status` is clean and no file under `build/` is tracked. Cheaper substitute: a stub `LICENSE` line instead of the full text, which fails the plan section 10 checklist.
- [x] Initial commit pushed to `origin/main`. Done when: `git ls-remote origin refs/heads/main` returns the commit SHA.
- [x] Commit: `repo: initial tree with design, foundation plan, TODO graph and license files`

**Test checkpoint:** in a fresh clone of the remote, `python scripts/todo-graph.py validate`, `python scripts/todo-graph.py plan --check` and `python -m unittest tests.scripts.test_todo_graph` all exit 0, and `LICENSE` is 35,149 bytes (`GPL-3.0`).

> **Verified:** 2026-10-13 | §1 | 25ac228f91ce0b778fe49565da5ae1b551242d5b
> **Review:** `docs/reviews/d00-t01-s1.json`

## 2. PDF engine dependency policy (native `recto-pdf`, MuPDF retired)

Originally stamped for the `third_party/mupdf` submodule pin at tag `1.28.5` (`433c72e34da3f4aeffde1cc0461d490f3ef32f42`). On 2026-10-14, decisions D3, D5, D9 and D12 were revised to retire MuPDF completely and build Recto's own native C++20 PDF engine (`recto-pdf` in `src/pdf`) over Windows 11 platform APIs (Direct2D, DirectWrite, WIC, Windows CNG `bcrypt.dll`, `Windows.Media.Ocr`), removing `.gitmodules`, `third_party/mupdf`, `third_party/mupdf.vcxproj`, `build/mupdf-config.h`, `build/mupdf-win32.targets` and `build/build-mupdf.ps1`.

> **Corrected:** 2026-10-14 -- Retired `third_party/mupdf` submodule and C17 bridge wrapper under revised decisions D2 (`GPL-3.0-or-later`), D3 (`recto-pdf` native C++20 engine), D9 (in-tree C++20 over Windows 11 SDK) and D12 (no PDF JavaScript engine by construction). The historical stamp below records the initial M0 §2 verification; §3's solution scaffold now compiles `src/pdf/recto-pdf.vcxproj` with zero submodule dependencies.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Historical step (completed in `433c72e`): pinned `third_party/mupdf` at `1.28.5` with `FZ_ENABLE_JS=0`. Done when: recorded in git history at `433c72e`.
2. 2026-10-14 architectural revision: deinitialize and remove `third_party/mupdf` and `.gitmodules`, delete the MuPDF MSBuild wrapper files, and establish `src/pdf/recto-pdf.vcxproj` as Recto's native C++20 PDF engine static library. Done when: `git status` shows no submodule gitlinks and `Recto.sln` references `src/pdf/recto-pdf.vcxproj` with `/W4 /WX` enforced across all projects.

- [x] Engine dependency boundary established (`third_party/mupdf` retired in favor of in-tree C++20 `src/pdf/recto-pdf.vcxproj`, zero JavaScript engine by construction). Done when: no submodule exists in the repository and all engine code compiles under Recto's own `/W4 /WX` C++20 rules.
- [x] Clean-clone reproducibility with zero external submodules. Done when: `build.cmd` builds from a fresh clone without fetching any git submodule.
- [x] Commit: `build: pin MuPDF submodule with JavaScript disabled (D9, D12)`

> **Audit 2026-10-21 (D06 T05 §2):** `git status` cannot show a committed, unchanged submodule; the reliable gitlink check is the index (`git ls-files --stage` holds no mode `160000` entry, 0 at the audit). The checkpoint's two paths and graph validation do not compile the engine; the clean-clone C++20 `/W4 /WX` build is proven by the `build` workflow, which builds every push to `main` that changes code from a fresh checkout (pushes that touch only `todo/`, `docs/` or Markdown are skipped by its `paths-ignore`). `python -m unittest tests.scripts.test_engine_dependency_policy` repeats the policy on every run: no gitlink in the index, no `.gitmodules` or `third_party`, no MuPDF in build files, sources or workflows, and C++20 with `/W4 /WX` in `Directory.Build.targets` (see `docs/reviews/06-quality/audit-d00-t01-s2.md`). The older mockup boards' MuPDF text is filed as `D06 T05 §25`.

**Test checkpoint:** `Test-Path .gitmodules` is `False`, `Test-Path third_party` is `False`, and `python scripts/todo-graph.py validate` exits 0.

> **Verified:** 2026-10-14 | §2 | 433c72e34da3f4aeffde1cc0461d490f3ef32f42
> **Review:** `docs/reviews/d00-t01-s2.json`

## 3. Solution scaffold for all modules, x64 and ARM64

Create `Recto.sln`, `Directory.Build.props`, `Directory.Build.targets`, `vcpkg.json` (test-only `gtest`, per D14) and a minimal compiling project for each module in foundation plan section 3.1: `src/pdf` (`recto-pdf`, C++20 static lib), `src/core` (`recto-core`, C++20 static lib), `src/services` (`recto-services`, C++20 static lib), `src/app` (`Recto`, WinUI 3 packaged app with Windows App SDK 2.4.0), `src/cli` (`recto-cli`, C++20 console), `tests/recto-tests` (`recto-tests`, GoogleTest runner) and `fuzz/` (`recto-fuzz` stub target). Provide the single documented build script `build/build.ps1` (plus a root `build.cmd` forwarder) that restores packages and builds both architectures so M0's "builds x64 and ARM64 in one command" is literal.

> **Started:** 2026-10-14 -- agent session (`40f8013`, updated for native `recto-pdf` C++20 module)

> **Corrected:** 2026-10-14 -- two environmental facts recorded per AGENTS.md working rules:
> 1. **Toolset `v145` (Visual Studio 2026):** MSBuild in VS 2022 (`v143`) has no `ApplicationType\Windows Store\10.0\Platforms\ARM64` directory, so a WinUI 3 project (`ApplicationType=Windows Store`) cannot build `ARM64` there; VS 2026 (`18.5`, toolset `v145`, MSVC `14.51.36231`) ships both `x64` and `ARM64` under `Windows Store\10.0`. `Directory.Build.props`, `Recto.vcxproj`, `build/build.ps1` and `CONTRIBUTING.md` bind `v145`.
> 2. **Native C++20 `recto-pdf` (`src/pdf`):** Replaces `third_party/mupdf` and `src/bridge` (`recto-bridge`). Every project in `Recto.sln` is Recto's own C++20 code and compiles with **0 errors and 0 warnings** under `/W4 /WX /permissive- /utf-8 /Zc:preprocessor` across `Debug` and `Release` on both `x64` and `ARM64`.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `Directory.Build.props` and `Directory.Build.targets` enforcing D14 and D15 across every project: Windows 11 SDK (`10.0.26100.0` target, `10.0.22621.0` min), `/std:c++20`, `/permissive-`, `/Zc:preprocessor`, `/utf-8`, `/W4 /WX`, `/sdl` in Release, an `EnableASAN` property for the sanitizer configuration, and all outputs under `build/bin/$(Platform)/$(Configuration)/` and `build/obj/`. Done when: a deliberate unused-variable warning in `src/core` fails the build with `C2220` and removing it restores green.
2. Create the seven `.vcxproj` files with the dependency chain of plan section 3.1 (`Recto -> recto-services -> recto-core -> recto-pdf`, `recto-cli -> recto-core`, `recto-tests -> recto-services + recto-core + recto-pdf`, `recto-fuzz -> recto-pdf`), each with one real translation unit (`recto::pdf::abi_version()` in `recto-pdf`, echoed by `recto-core`, `recto-services`, `recto-cli` and asserted in `recto-tests`; an empty `Window` in `Recto`). Done when: referencing a `src/app` header from `src/core` fails to find the include path by construction.
3. Write `build/build.ps1` and `build.cmd` that restore NuGet and vcpkg (`vcpkg.json` containing only `gtest`), build `Recto.sln` for `Debug` and `Release` on `x64` and `ARM64`, and run `recto-tests` on `x64`; update `CONTRIBUTING.md` with the exact command. Done when: `.\build.cmd` from a clean worktree exits 0 and produces `Recto.exe`, `recto-cli.exe` and `recto-tests.exe` under both `build/bin/x64/` and `build/bin/ARM64/`.

- [x] `Recto.sln`, `Directory.Build.props`, `Directory.Build.targets`, `vcpkg.json` and all seven module projects created with plan 3.1's downward-only include paths. Done when: `msbuild Recto.sln /p:Configuration=Release /p:Platform=x64` and `/p:Platform=ARM64` both exit 0 with 0 warnings across all seven projects. Cheaper substitute: building x64 only and leaving ARM64 project configurations unverified, violating D15.
- [x] One-command build script (`build.cmd` / `build/build.ps1`) documented and working from a clean clone. Done when: `.\build.cmd` exits 0 and runs the initial `recto-tests` binary (1 passing smoke test per library).
- [x] Commit: `build: solution scaffold for all Recto modules, x64 and ARM64`

> **Audit 2026-10-21 (D06 T05 §3):** the Windows App SDK is now 2.5.1 (decision D4 and `src/app/Recto.vcxproj`), superseding the 2.4.0 this section names. VS 2022 cannot substitute because the projects use the v145 toolset, which only VS 2026 ships; the missing ARM64 Store platform seen here came from an incomplete installation. `build/build.ps1` now asks `vswhere` for an installation with the C++ x64 and ARM64 tools. **Corrected 2026-10-21:** it first also required the Windows 11 SDK and WinUI C++ support as Visual Studio components, which GitHub's `windows-latest` image does not register and the build does not need (the SDK is its own install, the Windows App SDK comes from NuGet); CI builds failed from `59902fc` until the requirement was narrowed. The test suite has grown from the 3 scaffold tests to 98.

**Test checkpoint:** `.\build.cmd` exits 0 with 0 warnings on both `x64` and `ARM64`, and `build\bin\x64\Release\recto-tests\recto-tests.exe` reports 3 passing tests (`PdfEngineReportsAbiVersionOne`, `CoreReportsThePdfEngineAbi`, `ServicesReportTheCoreAbi`).

> **Verified:** 2026-10-14 | §3 | 5e3d9a687e2e23cb900032a929f1c280757ac6f0
> **Review:** `docs/reviews/d00-t01-s3.json`

## 4. Build CI for x64 and ARM64

Add `.github/workflows/build.yml` running on `windows-latest` on every push and pull request: build `x64` and `ARM64` (`Release` and `Debug`), run `recto-tests` on `x64` (and on an ARM64 runner if available on the repo's plan, otherwise record that ARM64 is compile-and-link verified in CI and runtime-verified on the §5 reference machine), and fail on any compiler warning.

> **Started:** 2026-10-18T09:15:00Z
> **Corrected:** 2026-10-18 -- Keyed the `actions/cache` step on `hashFiles('src/app/Recto.vcxproj', 'vcpkg.json', 'Directory.Build.props', 'Directory.Build.targets')` with `NUGET_PACKAGES` and `VCPKG_DEFAULT_BINARY_CACHE` because `src/app/Recto.vcxproj` uses `PackageReference` rather than `src/app/packages.config`, recorded that `ARM64` is compile-and-link verified in CI on `windows-latest` (using Visual Studio 2026 `v145`) and runtime-verified via `D99 T01 §4`, and updated `Test checkpoint` to resolve the run ID via `gh run list --workflow=build.yml --limit 1` before calling `gh run view <run-id> --log` because `gh run view` does not accept `--workflow`.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `.github/workflows/build.yml` invoking `build/build.ps1`, caching NuGet and vcpkg archives keyed on `vcpkg.json` and project files, and uploading test XML results on failure. Done when: the workflow file passes local syntax inspection and invokes the exact same script a developer runs locally.
2. Push and observe a green run on GitHub Actions; prove the gate can fail by confirming the step exits non-zero when `recto-tests` returns non-zero (tested locally via `build.ps1`, quoted in the review). Done when: `gh run list --workflow=build.yml --limit 1` shows `completed / success` on `main`.

- [x] Author `.github/workflows/build.yml` triggered on `push` to `main` and `pull_request` on `windows-latest` with `concurrency` cancellation for superseded PR runs. Done when: `.github/workflows/build.yml` parses cleanly and defines explicit job timeouts.
- [x] Configure `actions/cache` for NuGet (`NUGET_PACKAGES`) and vcpkg binary archives (`VCPKG_DEFAULT_BINARY_CACHE`) keyed on `hashFiles('src/app/Recto.vcxproj', 'vcpkg.json', 'Directory.Build.props', 'Directory.Build.targets')`. Done when: repeat workflow runs restore cached packages without recompiling GoogleTest from source.
- [x] Invoke `powershell -NoProfile -ExecutionPolicy Bypass -File build/build.ps1 -Configuration Debug` and `-Configuration Release` across `x64` and `ARM64` with `/p:TreatWarningsAsErrors=true`. Done when: all four configuration-platform pairs (`Debug|x64`, `Release|x64`, `Debug|ARM64`, `Release|ARM64`) compile and link in CI with 0 warnings. Cheaper substitute: a workflow that only checks out the repo or builds one architecture.
- [x] Execute `recto-tests.exe --gtest_output=xml:build/test-results-x64-$cfg.xml` on `x64` (Debug and Release) inside the workflow and fail the job immediately on any non-zero exit code. Done when: test failures fail the workflow step with the failing test named.
- [x] Upload `build/test-results-*.xml` and MSBuild failure logs as GitHub Actions artifacts when `failure()` occurs so CI failures are diagnosable without re-running locally. Done when: the `upload-artifact` step is wired with `if: always()` or `if: failure()`.
- [x] Push to `origin/main` and verify the live GitHub Actions `build.yml` run completes `success` across all steps. Done when: `gh run list --workflow=build.yml --limit 1` reports `completed / success`.
- [x] Commit: `ci: build x64 and ARM64 and run recto-tests on every push`

> **Audit 2026-10-21 (D06 T05 §7):** CI no longer builds on every push: since 2026-10-21 `build.yml` skips pushes that touch only `todo/`, `docs/` or Markdown (`paths-ignore`; the foundation plan carries the dated correction). The four-configuration build and the ASan job now keep separate dependency caches, keyed also on `vcpkg-configuration.json`, so the x64-only ASan job can no longer save an incomplete cache the full build restores.

**Test checkpoint:** `gh run list --workflow=build.yml --limit 1` and `gh run view <run-id> --log` for the candidate commit show `x64` build, `ARM64` build and `recto-tests` steps all succeeding with 0 warnings.

> **Verified:** 2026-10-18 | §4 | 6f0c29e1e65dd6e746849079f8e7e203ae3a76cd
> **Review:** `docs/reviews/d00-t01-s4.json`

## 5. Reference benchmark machines recorded

Foundation plan section 8 binds performance targets to the primary reference Windows 11 host machine (and cross-compiled/secondary ARM64 verification), and section 11 item 2 was resolved on 2026-10-14 to use this host machine as the primary reference benchmark machine. Query and record this host machine's exact specifications (CPU, cores, RAM, GPU, storage, Windows 11 build, display DPI) plus the non-binding ARM64 cross-compile target profile in `docs/reference-machines.md` and cross-link from the foundation plan.

> **Started:** 2026-10-14 -- agent session
> **Deferred:** Naming and runtime-benchmarking a specific physical `ARM64` machine (no `ARM64` hardware is attached today; `ARM64` is compile-and-link verified on every build) -> XREF: D99 T01 §4

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Query this host machine's hardware and OS specifications (`Get-CimInstance Win32_Processor`, `Win32_ComputerSystem`, `Win32_VideoController`, `Win32_OperatingSystem`) and write `docs/reference-machines.md` with a table of hardware and OS details (primary x64 host machine plus the non-binding ARM64 target profile) and the `recto-cli bench` command (landed in D01 T03 §3) that runs on them, and verify foundation plan section 11 item 2 links to `docs/reference-machines.md`. Done when: `docs/reference-machines.md` names concrete hardware specifications for the primary x64 host machine with no placeholders and states plainly that physical `ARM64` device naming is deferred to `D99 T01 §4`.
2. Verify that `owner-decision` has been removed from this section's `**Needs:**` line under the 2026-10-14 owner decision. Done when: `python scripts/todo-graph.py query blocked` no longer lists `D00 T01 §5` as waiting on an owner decision.

- [x] `docs/reference-machines.md` committed and linked from `docs/plans/2026-10-13-recto-foundation.md` section 8 and 11. Done when: the primary x64 host machine is specified strongly enough that another person can tell whether a measurement came from it, and the secondary `ARM64` profile is explicitly marked non-binding until `D99 T01 §4` names a physical `ARM64` device.
- [x] Commit: `docs: record x64 and ARM64 reference benchmark machines`

> **Audit 2026-10-21 (D06 T05 §4):** `recto-cli bench` has not landed: it is owned by `D01 T03 §3`, which is still open (`src/cli/main.cpp` prints only the ABI today); the performance harness and baseline are `D06 T01 §7` and `§8`, not `§4`. `docs/reference-machines.md` now cites those owners, its fingerprint commands also print disk sizes, MSBuild, the Windows SDK and the Windows App SDK version, and section 1 records what they print on 2026-10-21 (Visual Studio 18.11.1 after an update).

**Test checkpoint:** `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0, and `docs/reference-machines.md` contains zero `TODO` or `TBD` tokens.

> **Verified:** 2026-10-14 | §5 | c0f764763e8017306f8df19741625f5839b8dc9b
> **Review:** `docs/reviews/d00-t01-s5.json`

## Verification

- M0 definition of done (`docs/plans/2026-10-13-recto-foundation.md` section 7): a clean clone builds `x64` and `ARM64` in one command (`.\build.cmd`), all `recto-tests` pass, and both `plan-gates.yml` and `build.yml` are green on `main`.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
