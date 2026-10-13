---
schema_version: 1
id: repo-and-build
domain: 00-workspace
status: active
title: "TODO-01 -- Repository, MuPDF Pin and Build Scaffold"
track: M0
---

# TODO-01 -- Repository, MuPDF Pin and Build Scaffold

> **Goal:** Milestone M0 stands: a public AGPL git repository where a clean clone builds every Recto module for x64 and ARM64 with one command, in CI and locally. Nothing here ships to users; everything later depends on it being boring and green.

> **Current state:** Measured 2026-10-13. `R:\GitHub\RectoPDF` is not yet a git repository and contains no code: `design/` (SPEC, screenshots, tokens, mockups, generators), `docs/plans/2026-10-13-recto-foundation.md`, `resources/` (generated icons and MSIX assets), `todo/`, `scripts/` and `.claude/skills/` (this TODO system, D00 T02), plus `AGENTS.md`, `CLAUDE.md`, `.gitignore`, `.gitattributes` and `.github/workflows/plan.yml`. No LICENSE, NOTICE, CLA, CONTRIBUTING or README exists. No MuPDF source, solution or project file exists. The decisions this file executes are D2 (AGPL-3.0-or-later), D9 (MuPDF submodule with Recto config), D11 (CLA), D14 (MSBuild plus vcpkg manifest for test-only dependencies) and foundation plan sections 3.1, 3.2, 7 (M0), 8 and 11.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D2, D9, D11, D14), 3.1 (module table), 3.2 (repository layout), 7 (M0 row), 10 (AGPL checklist), 11 (open items: MuPDF tag, reference machines).
- `resources/README.md`: the MSIX asset names the app project manifest must reference once the app project exists.
- -> XREF: D00 T02 §1 -- the TODO graph engine whose CI gate (§4 here) and first stamp both need this repository to exist; D00 T02 §2 already authored `.github/workflows/plan.yml` for it.
- -> XREF: D99 T01 §2 -- the operator finishes the public repo face (About, topics, social preview) that §1 here only prepares locally.

## Outcome

- `git clone` of the repository, followed by the one documented build command, produces `recto-tests`, `recto-cli` and the app project (empty shells where later domains have not filled them) for x64 and ARM64.
- The AGPL license, CLA, CONTRIBUTING and NOTICE skeleton are at the repository root, so the first outside contribution is legally possible.
- MuPDF is a pinned submodule whose tag and config are recorded, and the engine domains can start against it.
- GitHub Actions fails a change that breaks the build, the tests or the TODO graph.

**Adjacency:** list=not-applicable (no records are listed by build scaffolding); document=not-applicable (no printed or carried output); settings=applicable @ D00 T01 §3; reporting=not-applicable (CI status is GitHub's surface, not a Recto report); notifications=not-applicable (no lifecycle notices in scaffolding); permissions=not-applicable (single-operator repository, GitHub owns access); audit=applicable @ D00 T01 §2; exchange=not-applicable (no import or export of user data); reverse=not-applicable (git history is the undo)

**Adjacency rationale:** Two keys are real here: settings, because the build's tunables (architectures, configurations, the MuPDF feature config) must live in named files a later section reads, never in a developer's local state; and audit, because the pinned MuPDF tag and submodule commit are the provenance every later AGPL claim (§5 of the release compliance file) rests on. Everything else is a product surface owned by later domains.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Git repository and repo-face files | -- | [x] |
| 2 | §2 | MuPDF submodule pinned with Recto config | §1 | [ ] |
| 3 | §3 | Solution scaffold with one-command build | §2 | [ ] |
| 4 | §4 | GitHub Actions build and test CI | §3 | [ ] |
| 5 | §5 | Reference machines and performance baseline record | §3 | [ ] |

## 1. Git repository and repo-face files

> **Started:** 2026-10-13T21:19:35Z

Recto is open source from the first public commit (D2), and the CLA must exist before the first outside contribution lands (D11), so the legal scaffolding is part of the first commit, not a later cleanup. This section also creates the history every stamp in this tree will cite.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Run `git init -b main` at the repository root and set the repo-local identity from the owner's existing git configuration. Done when: `git status` lists the current tree as untracked and nothing is silently ignored that should ship (check `design/`, `resources/`, `todo/` against `.gitignore`).
2. Add `LICENSE` (unmodified AGPL-3.0-or-later text), `CLA.md`, `CONTRIBUTING.md` (build pointer, CLA requirement, TODO workflow pointer to `todo/README.md`), `NOTICE.md` (MuPDF and Windows App SDK entries with room for the rest, completed in D07 T02 §2) and `README.md` (banner from `design/brand/github-banner.png`, one paragraph, build and plan pointers). Done when: each file renders on GitHub and the license is byte-identical to the FSF text.
3. Make the first commit of exactly the current tree plus these files, then push to the GitHub remote the operator created. Done when: `git log --oneline` shows the initial commit and the remote carries it; `python scripts/todo-graph.py validate` still passes on the committed tree.

- [x] `LICENSE`, `CLA.md`, `CONTRIBUTING.md`, `NOTICE.md`, `README.md` at the root. Done when: a reader landing on the repository learns the license, how to contribute and where the plan lives without asking. Cheaper substitute: a README that says "coming soon" and defers the CLA.
- [x] First commit pushed, including `design/`, `resources/`, `docs/plans/`, `todo/`, `scripts/`, `.claude/skills/`. Done when: a clean clone elsewhere validates the TODO graph (`python scripts/todo-graph.py plan --check` exits 0).
- [x] Commit: `repo: initial commit, design, plan and todo system`

**Test checkpoint:** in a fresh clone under `R:\scratch`, `python -m unittest tests.scripts.test_todo_graph` passes and `python scripts/todo-graph.py validate` prints the section count, proving the committed tree is self-contained.

> **Duration:** 2026-10-13T21:19:35Z to 2026-10-13T21:33:24Z

> **Verified:** 2026-10-13 | §1 | 25ac228f91ce0b778fe49565da5ae1b551242d5b
> **Review:** `docs/reviews/d00-t01-s1.json`

## 2. MuPDF submodule pinned with Recto config

> **Started:** 2026-10-13T21:39:44Z

D9: MuPDF comes from source as a git submodule at a pinned release tag, built with Recto's own feature config, never the vcpkg port and never the generated `mupdfcpp` bindings. JavaScript is disabled (D12). The tag choice is an M0 open item: pick the current stable Artifex release, record it here with a dated correction if it differs from what ships, and treat the submodule commit as part of every release's corresponding source.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add the submodule at `third_party/mupdf` checked out at the chosen release tag. Done when: `git submodule status` shows the tag's commit and `third_party/mupdf/README` names the same version the record below claims.
2. Write `build/mupdf-config.h` with `FZ_ENABLE_JS 0` and the font and feature choices the foundation plan assumes, wired the way MuPDF's build expects (`FZ_CONFIG` include path). Done when: the MuPDF static libraries build against this config with no JavaScript objects linked (check the build log for `mujs` absence).
3. Record the pin in this section (tag, commit, date) and in `NOTICE.md`. Done when: D01 T01 §1 can name the exact headers it builds against without re-deriving the version.

**Corrected 2026-10-13:** Three details above were written before the pinned tree existed and are corrected from it. (1) There is no `FZ_CONFIG` include-path hook in MuPDF 1.28.5: `include/mupdf/fitz/config.h` takes feature choices as preprocessor defines ahead of its `#ifndef` defaults, so `build/mupdf-config.h` is wired as an MSVC forced include (`/FI`, applied through the `CL` environment variable for the upstream win32 projects). (2) The win32 `libthirdparty.vcxproj` always compiles the MuJS unity file `thirdparty/mujs/one.c`; it has no JS-off switch of its own. Recto matches the Makefile's `mujs=no` behaviour from its own side: `build/mupdf-win32.targets` removes that compile item (passed as `CustomAfterMicrosoftCommonTargets`), and the `thirdparty/mujs` nested submodule stays uninitialized so a wiring failure breaks the build loudly. JavaScript's absence is therefore proven at symbol level (no `js_*` definitions in `libthirdparty.lib`, no undefined `js_*` references in `libmupdf.lib`), not by log inspection alone, although the build log also contains no MuJS compilation. (3) "Font choices" in step 2 resolves to retaining the upstream default font set and every other upstream feature default except JavaScript; the foundation plan chooses none, so none is invented (recorded in `build/mupdf-config.h`). The x64 build at upstream's own warning settings reports 19 warnings and 0 errors under MSVC v143 (C4127 constant-conditional from MuPDF macro patterns, C4146, C4267, C4324 padding, C4115, C5286; all in upstream sources), so the clean-warnings bar D00 T01 §3 sets binds Recto's own projects, with upstream treated as external code whose warnings are recorded here rather than suppressed: the build is consumed through its libraries, and how Recto's projects treat upstream warnings at their boundary is §3's recorded choice.

**Pin record:** MuPDF 1.28.5, tag commit `8ad45e92f0935d3d87f1db3f873086472a5e1b24`, pinned 2026-10-13 from https://github.com/ArtifexSoftware/mupdf.git. Build proof: `msbuild third_party/mupdf/platform/win32/mupdf.sln /t:libmupdf` Release, x64, toolset v143 (upstream declares v142), `CL` set to force-include `build/mupdf-config.h`, `CustomAfterMicrosoftCommonTargets` set to `build/mupdf-win32.targets`: 0 errors, producing `libmupdf.lib`, `libthirdparty.lib`, `libharfbuzz.lib`, `libextract.lib`, `libresources.lib`, `libpkcs7.lib`, `libmubarcode.lib`, `libzxing.lib`, `libleptonica.lib` and `libtesseract.lib` under `third_party/mupdf/platform/win32/x64/Release/`.

- [x] `third_party/mupdf` submodule at a named release tag. Done when: a clean clone with `git submodule update --init` reproduces the identical tree. Cheaper substitute: a copied source drop or the vcpkg port, both refused by D9.
- [x] `build/mupdf-config.h` disabling JavaScript. Done when: `libmupdf` and `libthirdparty` build for x64 with warnings clean at the configured level.
- [x] Pin recorded (tag + commit + date) in this section and `NOTICE.md`. Done when: the two records agree and name a commit `git -C third_party/mupdf rev-parse HEAD` reproduces.
- [ ] Commit: `build: pin MuPDF submodule with Recto config (JS disabled)`

**Test checkpoint:** `git submodule status` output matches the recorded tag, and the §3 build links the MuPDF libraries built from `build/mupdf-config.h` (the build log names the config path).

## 3. Solution scaffold with one-command build

Foundation plan sections 3.1 and 3.2 fix the module table and layout; D14 fixes MSBuild for every project and vcpkg manifest mode for test-only dependencies. The scaffold's job is that every later section lands in a project that already builds, with the dependency rules (only the bridge sees MuPDF headers) mechanically visible from the project references.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Create `Recto.sln` with the projects from the module table: `mupdf` (static libs from the submodule), `recto-bridge` (`src/bridge`, C17, `/TC`), `recto-core` (`src/core`, C++20), `recto-services` (`src/services`, C++20), `Recto` (WinUI 3 packaged app, `src/app`, Windows App SDK 2.4.0), `recto-cli` (`src/cli`), `recto-tests` (`tests/`), `recto-fuzz` (`fuzz/`). Each produces its trivial artifact (empty lib, hello-world exe, empty window). Done when: the solution builds Debug and Release for the current architecture with zero warnings.
2. Encode the dependency rules as project references exactly as the module table states, plus `vcpkg.json` (manifest mode, GoogleTest only) and a `Directory.Build.props` carrying language versions and warning levels. Done when: adding a forbidden reference (app to bridge) is the only way to break the rule, and no project needs a machine-local path.
3. Add the one-command build entry (`build.cmd` or documented `msbuild Recto.sln` invocation covering restore, both architectures, both configurations) and document it in `CONTRIBUTING.md`. Done when: a clean clone plus that command yields all artifacts for x64 and ARM64.

- [ ] `Recto.sln` and the eight projects with correct references. Done when: `msbuild Recto.sln -p:Configuration=Release -p:Platform=x64` and the ARM64 equivalent both exit 0. Cheaper substitute: one monolithic app project that "will be split later".
- [ ] `vcpkg.json` manifest with GoogleTest, consumed by `recto-tests` only. Done when: `recto-tests` runs one trivial passing test and no shipping project links a vcpkg artifact.
- [ ] One-command build documented in `CONTRIBUTING.md`. Done when: the command as written, run verbatim on a clean clone, succeeds.
- [ ] Commit: `build: solution scaffold for all Recto modules, x64 and ARM64`

**Test checkpoint:** both architecture builds exit 0 with zero warnings, and `recto-tests` reports its smoke test passed (quote the counts).

## 4. GitHub Actions build and test CI

M0's done-when is a clean clone building both architectures in CI with one command. The TODO graph gate already exists (`.github/workflows/plan.yml`, D00 T02 §2); this section adds the product build beside it: build, unit tests, fuzz smoke slot (filled by D01 T03 §2), and the MSIX package artifact once the app project can package (D07 owns the real packaging).

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `.github/workflows/build.yml` on Windows runners: checkout with submodules, the §3 one-command build for x64 and ARM64, then `recto-tests`. Done when: a push that breaks compilation fails the workflow and names the failing project.
2. Upload build logs (and, once packaging exists, the MSIX) as workflow artifacts. Done when: a failed ARM64 build is diagnosable from the artifact without owning an ARM64 machine.
3. Mark `plan-gates` and the build workflow as required checks in the repository settings note in D99 T01 §2. Done when: the manual setup section names both checks verbatim.

- [ ] `.github/workflows/build.yml` building x64 and ARM64 and running `recto-tests`. Done when: two consecutive pushes are green and a deliberately broken commit (reverted after) went red. Cheaper substitute: x64 only, with ARM64 "checked locally sometimes".
- [ ] Workflow artifacts carry logs per architecture. Done when: the ARM64 log downloads from a run and names the failing file on failure.
- [ ] Commit: `ci: GitHub Actions build and test for x64 and ARM64`

**Test checkpoint:** the workflow run for the section's own commit is green for both architectures (quote the run summary), and `plan-gates` is green on the same commit.

## 5. Reference machines and performance baseline record

Foundation plan section 8 sets performance targets against reference machines that M0 must choose: one mid-range x64 laptop and one ARM64 laptop. Later performance sections (D06 T01 §4, D02 T02) measure against these machines by name, so the choice and the machines' specs are recorded once, here.

**Phase:** 0
**Needs:** owner-decision
**Surface:** no

**Build order:**

1. Record the chosen machines in `docs/reference-machines.md`: model, CPU, RAM, GPU, Windows build, display scale. Done when: a performance result quoted later can name its machine and a reader can tell whether it is the reference.
2. Record the empty-scaffold baseline (app start to empty window for the §3 shell) measured on both machines. Done when: D02's viewer targets have a pre-viewer baseline to be compared against, not folklore.

- [ ] `docs/reference-machines.md` naming both machines with specs. Done when: the owner confirms both machines are the ones the targets bind to. Cheaper substitute: "the dev machine", unnamed.
- [ ] Baseline measurements recorded with date and build configuration. Done when: both machines have a dated start-time figure from the scaffold build.
- [ ] Commit: `docs: reference machines and scaffold performance baseline`

**Test checkpoint:** `docs/reference-machines.md` exists with both machines and both dated baselines, and `python scripts/todo-graph.py resolve 'D00 T01 §5'` no longer reports a waiting Need once the owner decision is recorded and the Need removed.

## Verification

- A clean clone (with submodules) builds both architectures with the one documented command, locally and in CI (M0 done-when, foundation plan section 7).
- `recto-tests` runs green; `python scripts/todo-graph.py validate` and `plan --check` pass on the committed tree.
- The AGPL checklist items owned here (foundation plan section 10: LICENSE at root, CLA before first outside PR, pinned submodule commit per release) are visibly in place.
