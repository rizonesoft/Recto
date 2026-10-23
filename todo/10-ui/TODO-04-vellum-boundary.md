---
schema_version: 1
id: vellum-boundary
domain: 10-ui
status: active
title: "TODO-04 -- Vellum Boundary: Move, Rename, Neutral Theming, Identity, Versioning and Extraction"
track: M2
---

# TODO-04 -- Vellum Boundary: Move, Rename, Neutral Theming, Identity, Versioning and Extraction

> **Goal:** the UI toolkit that `D10 T01` to `T03` build is Vellum, the shared Rizonesoft UI engine of foundation plan D22 to D26, and it lives in `libs/vellum/` behind a boundary a later `git filter-repo` can cut without touching a line of code. Vellum is its own static library (`libs/vellum/vellum.vcxproj`, namespace `vellum`, headers under `libs/vellum/include/vellum/`), has its own test executable (`libs/vellum/tests/vellum-tests.vcxproj`, suite `Vellum`), its own boundary scripts, design document, token schema and generator, `VERSION` and `CHANGELOG.md`, includes nothing from Recto and carries no Recto brand. Recto consumes it as a client: Recto's theme JSON, its `RectoIcons` font, its window class, AUMID, icon, settings folder and string tables are handed to Vellum through public interfaces. A dry run proves the extraction recipe in a scratch clone during M2, and the real extraction into `rizonesoft/Vellum` (a semver tag consumed through a Rizonesoft vcpkg registry port) waits for the second app to adopt the toolkit (D25).

> **Current state:** Measured 2026-10-23 at `6ec824d`. `libs/` does not exist. The toolkit is `src/ui/` (`recto-ui.vcxproj`, `pch.h`, `pch.cpp`, `ui.cpp`, `application.cpp`, `command_line.cpp`, `include/recto/ui/ui.h`, `include/recto/ui/application.h`), namespace `recto::ui`, listed in `Recto.sln` line 12 with GUID `{DC06AE43-AD42-4200-81C2-CB2E3893EC23}`. Its tests live in `tests/recto-tests/` (`ui_project_tests.cpp`, `ui_app_tests.cpp`, `ui_render_harness.cpp`, `ui_render_harness.h`, `ui_test.h` with `RECTO_UI_TEST` and suite `Ui`), compiled by the wildcard at `tests/recto-tests/recto-tests.vcxproj` line 82, with `..\..\src\ui\include` on the include path (line 44) and a `ProjectReference` to `src\ui\recto-ui.vcxproj` (line 95). `ui_app_tests.cpp` mixes one toolkit test (`Ui.App.ParsesArguments`) with three tests of the `Recto.exe` host (`Ui.App.ManifestDpiAwareness`, `Ui.App.ExeEmbedsManifest`, `Ui.App.AppxNoFramework`). `src/app/Recto.vcxproj` names `recto-ui` in comments (lines 2 to 4) and references it (line 100). The boundary proof is `scripts/check-ui-boundary.py`, tested by `tests/scripts/test_check_ui_boundary.py` and `tests/scripts/test_ui_module_boundary.py`, driven by `GenerateSourceDependencies` and the `RectoBoundaryManifest` target in `Directory.Build.targets` (lines 27 to 40), by `build/build.ps1` (ASan list line 89, target list line 122, boundary step lines 132 to 137) and by `.github/workflows/build.yml` (lines 77 to 82). It checks one direction per side (engine versus toolkit) and has no reverse check against `src/app` and no brand scan. `scripts/release-notes.py` accepts `Ui.<Area>.<Name>` gtest ids. `design/tokens/colors.json` is Recto's palette and no generator exists. The toolkit design is `docs/specs/2026-10-22-recto-ui-toolkit-design.md`, amended 2026-10-23 for Vellum. No `VERSION`, `CHANGELOG.md` or SPDX header exists under `src/ui`. `D10 T01 §1` is built and unstamped (audit note), and `D10 T01 §2` (the Win32 host) has every item ticked except its commit, so §1 here runs after both land and moves the tree they leave; every other `D10 T01` section starts on the moved tree.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` decisions D22 (Vellum, a shared engine and not a shared look), D23 (the boundary rules), D24 (consumer-supplied theme, icons and identity), D25 (extraction trigger and the pinned dependency), D26 (licence revisited at extraction), and the dated corrections to D4, D13, section 3.1 and section 3.2.
- `docs/specs/2026-10-22-recto-ui-toolkit-design.md` section "Amendment 2026-10-23: Vellum, a shared engine", plus sections 4.1 (module rule) and 4.6 (tokens).
- `design/SPEC.md` sections 4 (colour roles), 6 (icons) and 7 (shape); `design/tokens/colors.json`.
- `R:\GitHub\Isotone\docs\plans\2026-10-23-native-replan.md` (read-only reference): the pen, raster canvas and panel needs a second consumer brings, which the public API must not foreclose.
- `Recto.sln`, `Directory.Build.targets`, `build/build.ps1`, `.github/workflows/build.yml`, `scripts/check-ui-boundary.py`, `scripts/release-notes.py`, `tests/recto-tests/recto-tests.vcxproj`, `src/app/Recto.vcxproj`.
- -> XREF: D10 T01 §2 -- the Win32 `Recto.exe` host and its `Ui.App.*` tests, which §1 splits into toolkit and host tests.
- -> XREF: D10 T01 §1 -- the static library, harness and boundary proof that §1 here moves to `libs/vellum` and renames; every later `D10 T01` section builds on the moved tree.
- -> XREF: D10 T01 §8 -- the `ThemeManager` and token roles that consume §3's schema and generated role header.
- -> XREF: D10 T01 §21 -- the include chain extends the boundary script after §2 here moves it to `libs/vellum/scripts/check-boundary.py`.
- -> XREF: D10 T01 §22 -- pen and ink input, added to Vellum by D22 because Isotone needs it.
- -> XREF: D10 T01 §11 -- `vellum::Icon` resolves glyphs through §4's `vellum::IconSet` registry instead of a hard-coded `RectoIcons` font.
- -> XREF: D10 T02 §23 -- the control gallery renders through §3's neutral test theme so Vellum's goldens never depend on Recto's palette.
- -> XREF: D10 T03 §7 -- `vellum::Strings` loads the consumer's string tables; Vellum's own strings are only its control defaults.
- -> XREF: D02 T01 §18 -- Recto's `RectoIcons.ttf` is registered with §4's `vellum::IconSet` by the app, not compiled into Vellum.
- -> XREF: D06 T04 §1 -- the board check renders Recto's surfaces through §3's Recto theme mapping.
- -> XREF: D07 T02 §3 -- `NOTICE.md` and the About page's "Built with" list name Vellum with the version §5 stamps.

## Outcome

- `libs/vellum/` is a self-contained tree: `.\build.cmd` builds `vellum` and `vellum-tests` for x64 and ARM64, `vellum-tests.exe --gtest_filter=Vellum.*` passes, and nothing under `libs/vellum/` names `src/`, `recto`, `Recto` or `RECTO` outside a short allow list of historical notes in `CHANGELOG.md`.
- The boundary check refuses an include, project reference, library directive or symbol that crosses from Vellum into `src/` (reverse direction) or from the engine into Vellum, and the brand scan refuses a Recto name in Vellum source, in build, locally and in CI.
- Recto's look is data: `src/app/theme/recto_theme.json` maps `design/tokens/colors.json` onto Vellum's role schema, `libs/vellum/scripts/build-tokens.py --check` keeps every generated header current, and Recto's window class, AUMID, icon, settings folder and icon font reach Vellum only through `vellum::ApplicationIdentity` and `vellum::IconSet`.
- `libs/vellum/docs/EXTRACTION.md` is proven by a dry run that extracts the tree into a scratch clone and builds and tests it green, so the post-v1 extraction (§7) is a recipe, not a project.

**Adjacency:** all=not-applicable (build and repository boundary work with no record, list, report, setting or exchange of its own; the theme and identity data it moves belong to the app sections that consume them)

**Adjacency rationale:** This file relocates and fences the toolkit; it creates no user data and no user-facing surface. The theme JSON, icon font and identity values it routes are Recto data owned by D02 T01 and D10 T01, and the settings that select a theme are owned by D02 T04. The reverse of the move (§1) is a recorded revert, and the reverse of the extraction (§7) is consuming the in-repo copy again, which §6's runbook names.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Move `src/ui` to `libs/vellum`, rename to `vellum::`, own project and test executable | D10 T01 §1, D10 T01 §2 | [ ] |
| 2 | §2 | Boundary check: forward, reverse and brand scan in build and CI | §1 | [ ] |
| 3 | §3 | Neutral token role schema, generic generator and Recto theme mapping with `--check` | §1 | [ ] |
| 4 | §4 | Consumer-supplied identity: window class, AUMID, icon, settings paths, icon sets | §1 | [ ] |
| 5 | §5 | Versioning: `VERSION`, `CHANGELOG.md`, SPDX headers and a public API surface test | §1 | [ ] |
| 6 | §6 | Extraction runbook and dry run: `filter-repo` into a scratch clone builds green | §2, §3, §4, §5 | [ ] |
| 7 | §7 | Extraction after v1: own repository, semver tag, vcpkg registry port, Recto pins it | §6 | [ ] |

## 1. Move `src/ui` to `libs/vellum`, rename to `vellum::`, own project and test executable

Move the toolkit with `git mv` so history follows it: `src/ui/` becomes `libs/vellum/` with public headers under `libs/vellum/include/vellum/` (`vellum.h`, `application.h`) and sources under `libs/vellum/src/`; the namespace `recto::ui` becomes `vellum` and `recto::ui::test` becomes `vellum::test`; `recto-ui.vcxproj` becomes `libs/vellum/vellum.vcxproj` keeping its project GUID so `Recto.sln` history stays readable. The toolkit tests leave `recto-tests`: `ui_project_tests.cpp`, `ui_render_harness.cpp`, `ui_render_harness.h` and `ui_test.h` move to `libs/vellum/tests/` (`project_tests.cpp`, `render_harness.cpp`, `render_harness.h`, `vellum_test.h` with `VELLUM_TEST(Area, Name)` registering suite `Vellum`), built by a new `libs/vellum/tests/vellum-tests.vcxproj` that links GoogleTest the way `recto-tests` does and carries its own copy of the comparison helpers it needs (`compare_rgba`, `compute_ssim`) so it never includes `tests/recto-tests/test_support.h`. Decision (default, cost of reversing: move four tests back and restore the `Ui` macro in `recto-tests`): `Ui.App.ParsesArguments` tests the toolkit's command line and becomes `Vellum.App.ParsesArguments`; `Ui.App.ManifestDpiAwareness`, `Ui.App.ExeEmbedsManifest` and `Ui.App.AppxNoFramework` test `Recto.exe` and stay in `recto-tests` as `App.Host.*` through a `RECTO_APP_TEST` macro in `tests/recto-tests/app_test.h`. Every later checkpoint in `D10 T01` to `T03` that filters `Ui.<Area>.*` reads `Vellum.<Area>.*` from this section on, and runs `build\bin\x64\Debug\vellum-tests\vellum-tests.exe` (the corrections are recorded in those files). The design document moves to `libs/vellum/docs/design.md` with a pointer left at its old path.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. `git mv src/ui libs/vellum`, then restructure into `include/vellum/` and `src/`, rename the namespace and the project to `vellum.vcxproj`, and update `Recto.sln`, `src/app/Recto.vcxproj` (comments and `ProjectReference`) and `Directory.Build.targets` (`GenerateSourceDependencies` condition and the boundary comment). Done when: `msbuild Recto.sln /p:Configuration=Debug /p:Platform=x64 /t:vellum` exits 0 with zero warnings and `git log --follow libs/vellum/src/application.cpp` shows the `src/ui` history.
2. Create `libs/vellum/tests/vellum-tests.vcxproj`, move the toolkit tests and harness, and add `VELLUM_TEST`. Done when: `build\bin\x64\Debug\vellum-tests\vellum-tests.exe --gtest_filter=Vellum.Project.*` passes with the same test count `Ui.Project.*` had before the move.
3. Update `build/build.ps1` (ASan list, target list, test runs for both executables), `.github/workflows/build.yml`, `scripts/release-notes.py` (`Vellum.<Area>.<Name>` gtest ids) and `tests/scripts/test_build_ps1.py`. Done when: `.\build.cmd -Platform x64 -Configuration Debug` runs both test executables and `python -m unittest tests.scripts.test_build_ps1 tests.scripts.test_release_notes` passes.

- [ ] Move `src/ui/` to `libs/vellum/` with `git mv` and split it into `libs/vellum/include/vellum/` and `libs/vellum/src/`. Done when: `git status` shows renames, not deletes and adds. Source: foundation plan D23.
- [ ] Rename `recto::ui` to `vellum` and `recto::ui::test` to `vellum::test` in every moved file and in `src/app`. Done when: a search for `recto::ui` and `recto/ui` over the tracked tree finds only dated history notes.
- [ ] Rename `src/ui/recto-ui.vcxproj` to `libs/vellum/vellum.vcxproj` (GUID kept, `TargetName` `vellum`) and update `Recto.sln` line 12. Done when: x64 and ARM64 Debug and Release build with zero warnings.
- [ ] Update the `ProjectReference` and comments in `src/app/Recto.vcxproj`. Done when: `Recto.exe` links against `vellum.lib` and its comments name Vellum.
- [ ] Update `Directory.Build.targets`: `GenerateSourceDependencies` and `RectoBoundaryManifest` cover `vellum` in place of `recto-ui`. Done when: the Debug x64 build writes a boundary manifest for `vellum`.
- [ ] Create `libs/vellum/tests/vellum-tests.vcxproj` (console, GoogleTest from vcpkg, wildcard `ClCompile` over `libs/vellum/tests/*_tests.cpp`) and add it to `Recto.sln`. Done when: it builds for x64 and ARM64 with zero warnings and includes nothing under `tests/recto-tests/`. Cheaper substitute: leaving the toolkit tests in `recto-tests`, which an extraction cannot take along.
- [ ] Move `ui_test.h` to `libs/vellum/tests/vellum_test.h` with `VELLUM_TEST(Area, Name)` registering suite `Vellum`, and move `gtest_defaults.cpp` behaviour (fail when a filter selects nothing) into the new executable. Done when: `Vellum.Project.EmptySelectionFails` and `Vellum.Project.UniqueTestIds` pass.
- [ ] Move the harness to `libs/vellum/tests/render_harness.h` and `render_harness.cpp` as `vellum::test::render_to_bitmap` and `vellum::test::rgba_bytes`, with the comparison helpers it needs copied into `libs/vellum/tests/compare.h`. Done when: `Vellum.Project.Harness*` pass.
- [ ] Move `ui_project_tests.cpp` to `libs/vellum/tests/project_tests.cpp` with paths pointed at `libs/vellum`. Done when: `Vellum.Project.IncludeBoundary` still catches its full planted set.
- [ ] Split `ui_app_tests.cpp`: `ParsesArguments` moves to `libs/vellum/tests/app_tests.cpp` as `Vellum.App.ParsesArguments`; the three host tests stay in `tests/recto-tests/app_host_tests.cpp` as `App.Host.*` through `tests/recto-tests/app_test.h`. Done when: both filters pass and `recto-tests.vcxproj` no longer references the toolkit include folder except through the `vellum` project reference.
- [ ] Update `tests/recto-tests/recto-tests.vcxproj` (include path line 44, wildcard line 82, reference line 95). Done when: `recto-tests` builds and links `vellum.lib` only because `App.Host.*` needs nothing else from it, or drops the reference when nothing needs it.
- [ ] Update `build/build.ps1`: `vellum` and `vellum-tests` in the ASan list and target list, and both test executables run after each configuration. Done when: `python -m unittest tests.scripts.test_build_ps1` passes, including `AsanCoverage` over the new projects.
- [ ] Update `.github/workflows/build.yml` so CI runs `vellum-tests.exe` beside `recto-tests.exe`. Done when: the workflow file names both executables and the `boundary` step still runs.
- [ ] Update `scripts/release-notes.py` and `tests/scripts/test_release_notes.py` to accept `Vellum.<Area>.<Name>` and `App.Host.<Name>` gtest ids and keep the disabled-test rule. Done when: the test module passes.
- [ ] Move `docs/specs/2026-10-22-recto-ui-toolkit-design.md` to `libs/vellum/docs/design.md` with `git mv` and leave a one-paragraph pointer at the old path. Done when: every `-> XREF`-free reference in `todo/` to the old path still resolves through the pointer.
- [ ] Record a dated correction note in `D10 T01`, `D10 T02` and `D10 T03` naming the filter and executable change. Done when: `python scripts/todo-graph.py validate` exits 0.
- [ ] Commit: `vellum: move src/ui to libs/vellum, rename to vellum::, own test executable`

**Test checkpoint:** `build\bin\x64\Debug\vellum-tests\vellum-tests.exe --gtest_filter=Vellum.*` passes with a count equal to the pre-move `Ui.Project.*` count plus one (`Vellum.App.ParsesArguments`), `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=App.Host.*` passes three tests, and `python -m unittest tests.scripts.test_build_ps1 tests.scripts.test_release_notes` passes. Fails if any test was dropped in the move.

## 2. Boundary check: forward, reverse and brand scan in build and CI

Move `scripts/check-ui-boundary.py` to `libs/vellum/scripts/check-boundary.py` with its tests (`tests/scripts/test_check_ui_boundary.py` and `tests/scripts/test_ui_module_boundary.py` become `libs/vellum/scripts/tests/test_check_boundary.py` and `libs/vellum/scripts/tests/test_module_boundary.py`) and extend it in two ways that D23 requires. The reverse check: no `vellum` translation unit opens a file under `src/` (any Recto module, `src/app` included) or `tests/recto-tests/`, no `vellum` library directive names a Recto library, and no `vellum` symbol is owned by a Recto module. The brand scan: no file under `libs/vellum/` (sources, headers, project files, scripts, docs, tests and generated headers) contains `recto`, `Recto` or `RECTO` as a word, except lines listed in `libs/vellum/scripts/brand-allow.txt` (dated history notes only). The engine-to-toolkit direction keeps every guarantee the moved script already proves. The script is parameterised by the consumer's module folders so it ships unchanged after extraction, where Recto's build passes its own folders.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. `git mv` the script and its two test modules into `libs/vellum/scripts/`, add a `--consumer-root` and `--consumer-module` argument set, and point `build/build.ps1` and `.github/workflows/build.yml` at the new path. Done when: `python libs/vellum/scripts/check-boundary.py --configuration Debug --platform x64 --consumer-root . --consumer-module src/pdf --consumer-module src/core --consumer-module src/services --consumer-module src/app` exits 0 after a Debug x64 build.
2. Add the reverse rules and their planted fixtures. Done when: a planted `#include "../../src/app/main.cpp"` in a scratch `vellum` unit, a `#pragma comment(lib, "recto-core")` and a forward-declared `recto::pdf` symbol are each refused with the rule named.
3. Add the brand scan with `brand-allow.txt`. Done when: a planted `Recto` in a scratch header is refused at `file:line` and an allow-listed history line is not.

- [ ] Move `scripts/check-ui-boundary.py` to `libs/vellum/scripts/check-boundary.py` with `git mv`. Done when: `git log --follow` shows its history.
- [ ] Move the two boundary test modules to `libs/vellum/scripts/tests/` and make them runnable with `python -m unittest discover -s libs/vellum/scripts/tests`. Done when: every existing planted crossing is still caught.
- [ ] Add `--consumer-root` and repeated `--consumer-module` arguments so no Recto folder name is hard-coded in the script. Done when: the brand scan passes over the script itself.
- [ ] Add the reverse source rule: no `vellum` unit's `/sourceDependencies` report names a file under any consumer module or `tests/recto-tests/`. Done when: the planted `src/app` include is refused. Source: Microsoft Learn "/sourceDependencies".
- [ ] Add the reverse library rule: `dumpbin /directives` and `lib /list` of `vellum.lib` name no consumer library or object. Done when: the planted `#pragma comment(lib)` is refused. Source: Microsoft Learn "DUMPBIN /DIRECTIVES".
- [ ] Add the reverse symbol rule: `dumpbin /symbols` of `vellum.lib` needs no symbol a consumer module owns (`@pdf@recto@@`, `@core@recto@@`, `@services@recto@@`, `Recto*` extern "C", and `src/app` names). Done when: the planted forward declaration is refused.
- [ ] Add the brand scan over every tracked file under `libs/vellum/` with word-boundary matching and `libs/vellum/scripts/brand-allow.txt`. Done when: a planted brand word is refused and the live tree passes. Cheaper substitute: a reviewer reading for brand names, which a generated header defeats.
- [ ] Update `build/build.ps1` (boundary step) and `tests/scripts/test_build_ps1.py` to call the new script with Recto's consumer modules. Done when: `python -m unittest tests.scripts.test_build_ps1` passes.
- [ ] Update `.github/workflows/build.yml` (the boundary step at lines 77 to 82) to run the moved script and `python -m unittest discover -s libs/vellum/scripts/tests`. Done when: the workflow names the new path.
- [ ] Update `AGENTS.md` validation block and `docs/todo-workflow.md` if either names the old script. Done when: a search for `check-ui-boundary` over the tracked tree finds only dated history notes.
- [ ] Commit: `vellum: boundary check gains reverse rules and a brand scan`

**Test checkpoint:** `python -m unittest discover -s libs/vellum/scripts/tests` passes with every planted forward, reverse and brand crossing caught, and `python libs/vellum/scripts/check-boundary.py --configuration Debug --platform x64 --consumer-root . --consumer-module src/pdf --consumer-module src/core --consumer-module src/services --consumer-module src/app` exits 0 after `.\build.cmd -Platform x64 -Configuration Debug`. Fails if a planted reverse crossing passes.

## 3. Neutral token role schema, generic generator and Recto theme mapping with `--check`

Vellum owns the shape of a theme, and Recto owns its values (D24). `libs/vellum/tokens/schema.json` names the roles every control reads (surfaces, text, strokes, accent, focus, selection, status, overlay, elevation, radii, spacing, type ramp and motion durations), each with its type and the high-contrast system colour it maps to. `libs/vellum/scripts/build-tokens.py` validates a theme JSON against the schema and writes a C++ header of `constexpr` role values for Light, Dark and HighContrast, with `--check` failing when a generated header is stale. Vellum's own tests use `libs/vellum/tests/themes/test-theme.json`, a deliberately neutral grey theme, so no Vellum golden depends on Recto's palette. Recto maps `design/tokens/colors.json` onto the roles in `src/app/theme/recto_theme.json` and generates `src/app/generated/recto_theme.g.h`, which the app hands to `vellum::ThemeManager` at start-up. `design/tokens/colors.json` stays the source of Recto's colours: the mapping file references its keys and never copies a hex value.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `libs/vellum/tokens/schema.json` from `design/SPEC.md` sections 4, 5, 7 and 12 with neutral role names. Done when: every colour and shape role `D10 T01 §9`, `§10` and `D10 T02` name has an entry, and no entry is named after a Recto surface.
2. Implement `libs/vellum/scripts/build-tokens.py` (stdlib Python: validate, resolve references, emit header, `--check`). Done when: `python libs/vellum/scripts/build-tokens.py --theme libs/vellum/tests/themes/test-theme.json --out libs/vellum/tests/generated/test_theme.g.h --check` exits 0 and exits 1 after a planted edit to the theme.
3. Author `src/app/theme/recto_theme.json` referencing `design/tokens/colors.json` keys, and wire both generations into `build/build.ps1` and CI. Done when: `python libs/vellum/scripts/build-tokens.py --theme src/app/theme/recto_theme.json --palette design/tokens/colors.json --out src/app/generated/recto_theme.g.h --check` exits 0.

- [ ] Author `libs/vellum/tokens/schema.json` (role, type, high-contrast mapping, required flag). Done when: the schema validates itself through `build-tokens.py --self-test`. Source: `design/SPEC.md` sections 4, 5, 7 and 12.
- [ ] Implement theme validation in `libs/vellum/scripts/build-tokens.py`: unknown role, missing required role, wrong type, unresolved palette key and a hex value in a mapping that should reference a palette are each refused with the path. Done when: `libs/vellum/scripts/tests/test_build_tokens.py` covers each refusal.
- [ ] Emit the header (`namespace vellum::theme_data`, one `constexpr` table per mode) with a stable order so regeneration is byte-identical. Done when: two runs produce identical bytes.
- [ ] Add `--check` comparing the would-be output to the file on disk and naming the first differing role. Done when: a stale header fails with the role named. Cheaper substitute: regenerating in the build without a check, which hides drift in review.
- [ ] Author `libs/vellum/tests/themes/test-theme.json` (neutral greys, a blue accent) and generate `libs/vellum/tests/generated/test_theme.g.h`. Done when: `vellum-tests` builds against it and no test includes Recto's theme.
- [ ] Author `src/app/theme/recto_theme.json` mapping every role to a `design/tokens/colors.json` key or a SPEC value. Done when: generation succeeds and every role resolves. Source: `design/SPEC.md` section 4.
- [ ] Generate `src/app/generated/recto_theme.g.h` and run `--check` for both themes in `build/build.ps1` and `.github/workflows/build.yml`. Done when: CI fails on a stale header.
- [ ] Retire `design/tokens/RectoColors.xaml` if nothing reads it, recording the retirement in `design/SPEC.md` Status. Done when: a search finds no reader, or the reader is named and kept.
- [ ] Commit: `vellum: neutral token role schema, generator and Recto theme mapping`

**Test checkpoint:** `python -m unittest discover -s libs/vellum/scripts/tests` passes including `test_build_tokens`, and `python libs/vellum/scripts/build-tokens.py --theme src/app/theme/recto_theme.json --palette design/tokens/colors.json --out src/app/generated/recto_theme.g.h --check` exits 0. Fails if a role is missing from the Recto mapping.

## 4. Consumer-supplied identity: window class, AUMID, icon, settings paths, icon sets

Vellum carries no product identity (D24). `vellum::ApplicationIdentity` (product name, window class prefix, AppUserModelID, icon resource id, per-user settings and cache folder names, single-instance pipe name) is passed to `vellum::Application` by the consumer; Vellum derives every window class, mutex, pipe and folder name from it and has no default that names a product. `vellum::IconSet` is a registry of glyph fonts: a consumer registers a font file (or resource) and a code point map under a set name, and `vellum::Icon` resolves `set:name` pairs through it; Vellum ships no icon font of its own. Recto supplies `RectoIcons.ttf` (`D02 T01 §18`) and its identity from `src/app`.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `libs/vellum/include/vellum/identity.h` (`ApplicationIdentity`) and thread it through `vellum::Application`. Done when: `Vellum.Identity.NamesDeriveFromIdentity` passes and a search of `libs/vellum/src` finds no product string.
2. Add `libs/vellum/include/vellum/icon_set.h` (`IconSet::register_font`, `IconSet::resolve`). Done when: `Vellum.Icons.RegistryResolves` registers a test font and resolves a glyph, and an unknown set or name returns a typed error.
3. Supply Recto's identity in `src/app/main.cpp`. Done when: `Recto.exe --smoke` runs with window class `Recto.*` and AUMID `Rizonesoft.Recto`, read back by `App.Host.Identity`.

- [ ] Define `vellum::ApplicationIdentity` in `libs/vellum/include/vellum/identity.h` with validation (non-empty, length limits, characters legal for a window class, pipe and folder). Done when: `Vellum.Identity.RefusesInvalid` covers each rule.
- [ ] Require an identity in `vellum::Application` construction (no default). Done when: constructing without one does not compile.
- [ ] Derive window class names, the single-instance mutex and pipe, and settings and cache folders from the identity. Done when: `Vellum.Identity.NamesDeriveFromIdentity` passes with two different identities producing disjoint names.
- [ ] Set the process AppUserModelID from the identity before the first window. Done when: `Vellum.Identity.SetsAumid` reads it back through `GetCurrentProcessExplicitAppUserModelID`. Source: Microsoft Learn `SetCurrentProcessExplicitAppUserModelID`.
- [ ] Define `vellum::IconSet` in `libs/vellum/include/vellum/icon_set.h`: register a font from a file path or an `RT_RCDATA` resource with a JSON code point map, resolve `set:name` to a font face and code point. Done when: `Vellum.Icons.RegistryResolves` and `Vellum.Icons.UnknownGlyphIsTypedError` pass.
- [ ] Add a small open-licence test icon font under `libs/vellum/tests/fonts/` with its licence file. Done when: the test uses it and the brand scan passes.
- [ ] Supply Recto's identity in `src/app/main.cpp` and add `App.Host.Identity` in `tests/recto-tests/app_host_tests.cpp`. Done when: the test reads Recto's window class and AUMID from a `--smoke` run.
- [ ] Commit: `vellum: consumer-supplied application identity and icon set registry`

**Test checkpoint:** `build\bin\x64\Debug\vellum-tests\vellum-tests.exe --gtest_filter=Vellum.Identity.*:Vellum.Icons.*` passes, and `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=App.Host.Identity` passes. Fails if Vellum falls back to a hard-coded product name.

## 5. Versioning: `VERSION`, `CHANGELOG.md`, SPDX headers and a public API surface test

Vellum is versioned on its own from the day it moves, so the extraction carries a real history (D25). `libs/vellum/VERSION` holds a semver string starting at `0.1.0`; `libs/vellum/CHANGELOG.md` follows Keep a Changelog with an Unreleased section every Vellum-touching commit updates; `vellum::version()` and `VELLUM_VERSION_*` macros are generated from `VERSION`. Every source file under `libs/vellum/` starts with `SPDX-License-Identifier: GPL-3.0-or-later` and a Rizonesoft copyright line, and `libs/vellum/LICENSE` is a copy of the root licence (D26 revisits the choice at extraction). The public API surface test lists every declaration in `libs/vellum/include/vellum/` into `libs/vellum/api/vellum.api.txt`; a change to the surface without a matching `VERSION` bump (minor for additions, major for removals or signature changes once `1.0.0` is reached) fails the check.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `libs/vellum/VERSION`, `libs/vellum/CHANGELOG.md`, `libs/vellum/LICENSE` and generate `libs/vellum/include/vellum/version.h`. Done when: `Vellum.Version.MatchesFile` passes.
2. Add SPDX headers and `libs/vellum/scripts/check-spdx.py`. Done when: a planted file without a header fails the check.
3. Add `libs/vellum/scripts/api-surface.py` and the committed `libs/vellum/api/vellum.api.txt`. Done when: a planted public function without a version bump fails `--check`.

- [ ] Add `libs/vellum/VERSION` (`0.1.0`) and `libs/vellum/CHANGELOG.md` with the move recorded under `0.1.0`. Done when: both exist and the brand scan allow list covers only the history line.
- [ ] Generate `libs/vellum/include/vellum/version.h` from `VERSION` in the build. Done when: `Vellum.Version.MatchesFile` compares `vellum::version()` with the file.
- [ ] Copy the root licence to `libs/vellum/LICENSE`. Done when: the two files are byte-identical and a check in `check-spdx.py` keeps them so.
- [ ] Add SPDX and copyright headers to every source, header, script and project file under `libs/vellum/`. Done when: `python libs/vellum/scripts/check-spdx.py` exits 0. Source: SPDX specification, "Using SPDX short identifiers in source files".
- [ ] Implement `libs/vellum/scripts/api-surface.py` listing public declarations (namespaces, classes, functions, enums, constants) from the headers into a sorted text file, with `--check`. Done when: `libs/vellum/scripts/tests/test_api_surface.py` covers an addition, a removal and a signature change.
- [ ] Enforce the bump rule: an API change without a `VERSION` change fails `--check` with the changed lines quoted. Done when: the planted change fails and passes after the bump. Cheaper substitute: a changelog convention nobody enforces.
- [ ] Run `check-spdx.py` and `api-surface.py --check` in `build/build.ps1` and `.github/workflows/build.yml`. Done when: both run in CI.
- [ ] Commit: `vellum: version file, changelog, SPDX headers and API surface check`

**Test checkpoint:** `python -m unittest discover -s libs/vellum/scripts/tests` passes including `test_api_surface`, `python libs/vellum/scripts/check-spdx.py` exits 0, and `build\bin\x64\Debug\vellum-tests\vellum-tests.exe --gtest_filter=Vellum.Version.*` passes. Fails if the API changes without a bump.

## 6. Extraction runbook and dry run: `filter-repo` into a scratch clone builds green

Write `libs/vellum/docs/EXTRACTION.md` and prove it. The runbook names every step: a fresh clone, `git filter-repo --path libs/vellum/ --path-rename libs/vellum/:` (history preserved), the standalone build files the extracted tree needs (`libs/vellum/Vellum.sln`, `libs/vellum/Directory.Build.props` and `Directory.Build.targets` copies of the toolset, warning and C++23 settings, `libs/vellum/vcpkg.json` for GoogleTest), the CI workflow template (`libs/vellum/.github/workflows/build.yml`), the vcpkg registry port layout (`ports/vellum/portfile.cmake`, `vcpkg.json`, the registry `versions/` baseline), how Recto switches from the in-repo tree to the port, and how to switch back. The standalone build files live in `libs/vellum/` from this section on and are built in Recto's CI as well, so they cannot rot. The dry run runs the recipe into a scratch folder under `R:\scratch` and builds and tests the result with no Recto file present.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add the standalone build files under `libs/vellum/` and build `libs/vellum/Vellum.sln` from Recto's tree. Done when: `msbuild libs/vellum/Vellum.sln /p:Configuration=Debug /p:Platform=x64` exits 0 and `vellum-tests.exe` passes from that build.
2. Write `libs/vellum/docs/EXTRACTION.md` and `libs/vellum/scripts/extract-dry-run.ps1`. Done when: the script clones, filters, builds and tests into a given scratch folder and exits non-zero on any failure.
3. Run the dry run and record the result. Done when: `pwsh libs/vellum/scripts/extract-dry-run.ps1 -Scratch R:\scratch\vellum-extract` exits 0 and its log shows `git log` history for `src/application.cpp` reaching back to the `src/ui` commits.

- [ ] Add `libs/vellum/Vellum.sln`, `libs/vellum/Directory.Build.props`, `libs/vellum/Directory.Build.targets` and `libs/vellum/vcpkg.json`. Done when: the standalone solution builds x64 and ARM64 from inside Recto's tree.
- [ ] Build `libs/vellum/Vellum.sln` in `.github/workflows/build.yml` beside `Recto.sln`. Done when: CI builds it.
- [ ] Add `libs/vellum/.github/workflows/build.yml` as the template the extracted repository uses (inert in Recto, since GitHub reads only the root `.github/`). Done when: `actionlint` or a YAML parse passes on it.
- [ ] Write `libs/vellum/docs/EXTRACTION.md`: prerequisites, the `filter-repo` command, tagging `v0.x.y`, the registry port, switching Recto to the port, switching back, and the licence decision D26 asks for. Done when: every command in it is in backticks and the dry run executes the same commands.
- [ ] Add a vcpkg registry port skeleton under `libs/vellum/packaging/vcpkg/ports/vellum/` (`portfile.cmake`, `vcpkg.json`). Done when: `vcpkg format-manifest --all` accepts the manifests. Source: Microsoft Learn "Create a vcpkg registry".
- [ ] Implement `libs/vellum/scripts/extract-dry-run.ps1` (clone to scratch, `git filter-repo`, build `Vellum.sln`, run `vellum-tests.exe` and the script tests, print a summary). Done when: a planted reference to a Recto file makes the dry run fail.
- [ ] Run the dry run and quote its summary in the stamp check. Done when: it exits 0. Cheaper substitute: reading the runbook without running it, which misses a missing file.
- [ ] Commit: `vellum: standalone build files, extraction runbook and dry run`

**Test checkpoint:** `pwsh libs/vellum/scripts/extract-dry-run.ps1 -Scratch R:\scratch\vellum-extract` exits 0 with `vellum-tests` passing in the extracted clone and the history check true. Fails if the extracted tree needs any file outside `libs/vellum/`.

## 7. Extraction after v1: own repository, semver tag, vcpkg registry port, Recto pins it

Run when the second consumer (Rizonesoft Office or Isotone) commits to adopting Vellum (D25). Follow `libs/vellum/docs/EXTRACTION.md`: create `rizonesoft/Vellum`, push the filtered history, decide the licence (D26: stay `GPL-3.0-or-later`, or move to LGPL or MPL with the contributor agreement's commercial clause), tag the first release, publish the port to a Rizonesoft vcpkg registry (the default; a git submodule is the recorded fallback), switch Recto's `vcpkg.json` and `vcpkg-configuration.json` to the pinned version, and delete `libs/vellum/` from Recto in the same commit that adds the pin. Recto's boundary step keeps running against the installed headers and library.

**Phase:** 7
**Needs:** windows-host, owner-decision
**Surface:** no

**Build order:**

1. Record the owner's licence and timing decision in foundation plan D26. Done when: the decision is dated in the plan and the `owner-decision` Need is removed.
2. Run the runbook to create the repository, tag and registry. Done when: `vcpkg install vellum` from the Rizonesoft registry succeeds on a clean machine.
3. Switch Recto to the pin and remove `libs/vellum/`. Done when: `.\build.cmd` passes with `libs/vellum/` absent.

- [ ] Record the licence decision in `docs/plans/2026-10-13-recto-foundation.md` D26. Done when: the decision and its cost of reversing are dated.
- [ ] Create `rizonesoft/Vellum` from the filtered history and push the first semver tag. Done when: the tag's CI is green.
- [ ] Publish the port to the Rizonesoft vcpkg registry with a versions baseline. Done when: a clean `vcpkg install vellum` succeeds.
- [ ] Switch `vcpkg.json` and `vcpkg-configuration.json` in Recto to the registry and the pinned version, and add the pin to the D4 pin checks (`scripts/check-vcpkg-baseline.py` or a new `scripts/check-vellum-pin.py`). Done when: the pin check passes and reports a newer tag as stale.
- [ ] Remove `libs/vellum/` from Recto and point the boundary step at the installed package. Done when: `.\build.cmd` passes and the boundary check still refuses a planted crossing.
- [ ] Update `NOTICE.md`, the About page "Built with" list and `AGENTS.md` for the external dependency. Done when: each names Vellum with its pinned version.
- [ ] Commit: `vellum: consume the extracted Vellum package through the Rizonesoft registry`

**Test checkpoint:** `.\build.cmd` exits 0 with `libs/vellum/` absent and Vellum resolved from the registry pin, and `python scripts/check-vellum-pin.py check` exits 0. Fails if any Recto file still reads from `libs/vellum/`.

## Verification

- `python scripts/todo-graph.py validate` exits 0 and `python scripts/todo-graph.py plan --check` reports the plan current.
- `build\bin\x64\Debug\vellum-tests\vellum-tests.exe --gtest_filter=Vellum.*` passes and `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=App.Host.*` passes.
- `python -m unittest discover -s libs/vellum/scripts/tests` passes, covering the boundary, brand scan, token generator and API surface.
- `python libs/vellum/scripts/build-tokens.py --theme src/app/theme/recto_theme.json --palette design/tokens/colors.json --out src/app/generated/recto_theme.g.h --check` exits 0.
- `pwsh libs/vellum/scripts/extract-dry-run.ps1 -Scratch R:\scratch\vellum-extract` exits 0.
