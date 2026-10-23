---
schema_version: 1
id: ui-foundation
domain: 10-ui
status: active
title: "TODO-01 -- recto-ui Foundation: Window, Composition, Elements, Layout, Tokens, Text and Input"
track: M2
---

# TODO-01 -- recto-ui Foundation: Window, Composition, Elements, Layout, Tokens, Text and Input

> **Goal:** `recto-ui` (`src/ui`), Recto's own C++23 UI toolkit, exists as a static library beside the engine and replaces the WinUI scaffold: `Recto.exe` is a Win32 `wWinMain` host that runs `ui::Application`, opens a custom-frame `ui::Window` with Mica Alt, dark mode and per-monitor DPI, composes it with DirectComposition and Direct2D on one shared D3D11 device, and lays out and paints a retained element tree in effective pixels. Design tokens are generated from `design/tokens/colors.json` and resolved by `ThemeManager` (Light, Dark, Follow Windows, high contrast). Text, icons, SVG and bitmaps draw through DirectWrite and Direct2D. Pointer, keyboard, focus, motion, popups, observable properties, commands, drag-and-drop, the clipboard, the page canvas `SwapChainHost` and Windows 11 non-client behaviour (drag regions, Snap Layouts) all work, and a headless golden-image suite proves the look against `design/screenshots/D-*.png` at 100, 150 and 200 percent. Every control in `todo/10-ui/TODO-02-ui-controls.md` and every shell surface builds on this file.

> **Current state:** Measured 2026-10-22. `src/ui` does not exist, and `Recto.sln` has no `recto-ui` project. `src/app` holds the WinUI 3 scaffold from `D00 T01 §3` (`App.xaml`, `App.xaml.h`, `App.xaml.cpp`, `MainWindow.xaml`, `MainWindow.xaml.h`, `MainWindow.xaml.cpp`, `MainWindow.idl`, `Package.appxmanifest`, `app.manifest`, `pch.h`, `pch.cpp`, `Recto.vcxproj`, `Assets/`) with a `Microsoft.WindowsAppSDK` package reference. `scripts/check-winui-pin.py`, `tests/scripts/test_check_winui_pin.py` and the `winui` matrix entry in `.github/workflows/pins.yml` (line 39) exist, and `scripts/merge_prs.py` (lines 39 to 41) accepts `winui/*` pin branches. `design/tokens/colors.json` and `design/tokens/RectoColors.xaml` both exist; no generator exists. `resources/ico/recto.ico` and `resources/svg/` exist; `resources/fonts/` does not (`D02 T01 §18` builds `RectoIcons.ttf`). `tests/recto-tests/test_support.h` already provides `compute_ssim`, `compare_rgba`, `expect_rgba_equal` and `sha256_hex`. `Directory.Build.props` applies `/W4 /WX` to every `recto-*` project and `Recto`. `design/screenshots/` holds `D-Brand.png`, `D-Focus-Light.png`, `D-Focus-Dark.png`, `D-Classic.png`, `D-ReadingRoom.png`, `D-Home.png`, `D-Search.png`, `D-Convert.png`, `D-Organize.png`, `D-Layout.png`, `D-Settings.png`, `D-About.png` and `D-Dialogs.png`. `docs/specs/2026-10-22-recto-ui-toolkit-design.md` is the approved design and none of its sections is built.

> **Groomed 2026-10-23:** re-measured against today's tree. `Directory.Build.targets` already compiles every `recto-*` project and `Recto` with `stdcpp23` (`/std:c++23preview`) and `/W4 /WX`, so `§1` adds no language-standard setting. Binaries land in `build\bin\<platform>\<configuration>\<project>\<project>.exe`, so every command in this file now names `build\bin\x64\Debug\recto-tests\recto-tests.exe` and `build\bin\x64\Debug\Recto\Recto.exe` (it said `build\bin\x64\Debug\recto-tests.exe`, a path that does not exist). `§2` was extended to cover every tracked file that still names WinUI or the Windows App SDK and to convert `src/app/Recto.vcxproj` to a plain Win32 project; `§4` gained the full screen API the F11 command needs; `§12` gained the cursor resolution nothing owned. The wider SPEC check (sections 4 to 12 against `D10 T01` to `T03`) found every control, elevation level (Z 32 and Z 64, `§9`), motion (`§14`, 167 ms), high-contrast mapping (`D10 T03 §5`) and accessibility rule owned except the hit-target minimum, now `D10 T03 §8`.

## Inputs

- `docs/specs/2026-10-22-recto-ui-toolkit-design.md` (approved 2026-10-22): section 4.1 (module rule), 4.2 (threads), 4.3 (window host), 4.4 (composition and drawing), 4.5 (elements and layout), 4.6 (tokens), 4.7 (text, icons, images), 4.8 (input, focus, motion), 4.9 (popups), 5 (WinUI to recto-ui mapping), 6 (this file's section list), 7 (changes to the existing plan).
- `design/SPEC.md` sections 3 (brand), 4 (colour), 5 (typography), 6 (icons), 7 (shape, spacing, elevation, minimum window size 960 x 600 epx), 8 and 8.1 (shell, named slots, title bar), 10 (keyboard), 11 (accessibility), 12 (motion); `design/tokens/colors.json`.
- `docs/plans/2026-10-13-recto-foundation.md` sections 4.2 (thread ownership), 4.3 (runtime and thread map) and 4.6 (presentation thread), decisions D3, D4, D13, D15 and D17.
- `tests/recto-tests/test_support.h` (`compute_ssim`, `compare_rgba`), `Directory.Build.props`, `Recto.sln`, `build/build.ps1`, `.github/workflows/pins.yml`, `scripts/check-sdk-pins.py`, `scripts/merge_prs.py`.
- -> XREF: D02 T01 §1 -- the manifest, assets and minimum window size build on §2 (host), §4 (window) and §8 (tokens) here.
- -> XREF: D02 T01 §2 -- the shell title bar (lockup, tabs, command box button, caption buttons) builds on §19's non-client hit testing.
- -> XREF: D02 T01 §18 -- `resources/fonts/RectoIcons.ttf` and its code point map are built there and consumed by §11 here.
- -> XREF: D02 T02 §1 -- the presentation thread consumes §18's `SwapChainHost`, composition swap chain and shared `ID3D11Device`.
- -> XREF: D06 T04 §1 -- the design-board checks use §20's golden-image harness and `--update` path.
- -> XREF: D10 T02 §1 -- the `Control` base and every control build on §9, §10, §13 and §16 here.
- -> XREF: D10 T03 §1 -- the UI Automation provider tree builds on §6's element tree and §4's `WM_GETOBJECT` hook.
- -> XREF: D00 T01 §1 -- `TODO-01-repo-and-build.md` consumes sections of this file (first use in the section named).
- -> XREF: D02 T03 §2 -- `TODO-03-home-and-panels.md` consumes sections of this file (first use in the section named).
- -> XREF: D02 T04 §2 -- `TODO-04-settings-and-layout.md` consumes sections of this file (first use in the section named).
- -> XREF: D02 T05 §3 -- `TODO-05-rich-media-3d-geo.md` consumes sections of this file (first use in the section named).
- -> XREF: D02 T06 §3 -- `TODO-06-cloud-storage-connectors.md` consumes sections of this file (first use in the section named).
- -> XREF: D03 T01 §3 -- `TODO-01-selection-and-search.md` consumes sections of this file (first use in the section named).
- -> XREF: D03 T02 §3 -- `TODO-02-accessibility.md` consumes sections of this file (first use in the section named).
- -> XREF: D03 T03 §4 -- `TODO-03-annotations-and-forms.md` consumes sections of this file (first use in the section named).
- -> XREF: D03 T04 §1 -- `TODO-04-edit-pdf.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T01 §1 -- `TODO-01-jobs-and-queue.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T02 §1 -- `TODO-02-pdf-conversions-a.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T03 §1 -- `TODO-03-pdf-to-word.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T04 §1 -- `TODO-04-office-to-pdf.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T05 §1 -- `TODO-05-excel-web-and-compliance.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T06 §1 -- `TODO-06-print-production.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T07 §1 -- `TODO-07-action-wizard.md` consumes sections of this file (first use in the section named).
- -> XREF: D05 T02 §22 -- `TODO-02-organize-ui.md` consumes sections of this file (first use in the section named).
- -> XREF: D06 T01 §1 -- `TODO-01-corpus-and-suites.md` consumes sections of this file (first use in the section named).
- -> XREF: D06 T03 §3 -- `TODO-03-user-failure-gates.md` consumes sections of this file (first use in the section named).
- -> XREF: D06 T05 §3 -- `TODO-05-stamp-audits.md` consumes sections of this file (first use in the section named).
- -> XREF: D07 T01 §1 -- `TODO-01-packaging-and-print.md` consumes sections of this file (first use in the section named).
- -> XREF: D07 T02 §2 -- `TODO-02-store-and-compliance.md` consumes sections of this file (first use in the section named).

## Outcome

- `Recto.exe` is a Win32 desktop app with no Windows App SDK, XAML or MIDL: `.\build.cmd` builds x64 and ARM64 and `Recto.exe` opens an empty `ui::Window` with a Mica Alt backdrop, correct dark mode, per-monitor DPI and a 960 x 600 epx minimum size.
- `recto-ui` renders any element tree headless into a BGRA bitmap through `recto::ui::test::render_to_bitmap`, so layout, hit testing, focus order, tokens and pixels are asserted in `recto-tests` without a display session, and `build-ui-tokens.py --check` keeps `recto_tokens.g.h` in step with `colors.json`.
- The window, compositor, element tree, layout, painter, text, icons, input, focus, motion, popups, drag-and-drop, clipboard, `SwapChainHost` and non-client behaviour required by the SPEC exist and are exercised by `Ui.*` tests, and the golden suite compares the toolkit's output with the SPEC boards in Light, Dark and HighContrast at three scales.

**Adjacency:** all=not-applicable (infrastructure toolkit with no record, list, report or exchange of its own; the only persisted state is the theme and animation reads from Windows settings, and Recto's own settings that feed it are owned by D02 T04)

**Adjacency rationale:** This file builds the rendering, layout, input and theme substrate. It owns no user data, so list, document, reporting, notifications, permissions, audit, exchange and reverse have nothing to carry. Settings the toolkit reads (theme choice, transparency effects, animation) are Windows settings read through `ThemeManager`; Recto's own Theme and Language settings rows are owned by D02 T04 and bind to `ThemeManager` and `ui::Strings`.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `recto-ui` static library, module boundary and headless render harness | D00 T01 §3 | [ ] |
| 2 | §2 | Win32 `Recto.exe` host replaces the WinUI scaffold; Windows App SDK pins retired | §1 | [ ] |
| 3 | §3 | UI thread `Dispatcher`, timers and engine callback marshalling | §1 | [ ] |
| 4 | §4 | Top-level `Window`: custom frame, Mica Alt, dark mode, DPI and minimum size | §2, §3 | [ ] |
| 5 | §5 | Compositor: shared D3D11 device, DirectComposition tree, D2D surfaces, device loss | §4 | [ ] |
| 6 | §6 | `Element` tree: retained rendering, invalidation, hit testing and visual layers | §5 | [ ] |
| 7 | §7 | Layout engine: epx measure and arrange, pixel snapping, Stack, Grid, Dock, Overlay | §6 | [ ] |
| 8 | §8 | Design tokens generated from `design/tokens/colors.json` and `ThemeManager` | §1 | [ ] |
| 9 | §9 | Drawing primitives: token radii, hairlines, borders, opacity, elevation shadows | §5, §8 | [ ] |
| 10 | §10 | Text: DirectWrite type ramp, `TextBlock`, trimming and highlighted ranges | §7, §8 | [ ] |
| 11 | §11 | Icon, SVG and bitmap image elements | §10, D02 T01 §18 | [ ] |
| 12 | §12 | Pointer and keyboard input: routing, capture, wheel, accelerators, access keys | §6 | [ ] |
| 13 | §13 | Focus manager: tab order, focus scopes, arrow groups and the Fluent focus visual | §9, §12 | [ ] |
| 14 | §14 | Motion: frame clock, Fluent easing, panel motion and the Windows animation setting | §6 | [ ] |
| 15 | §15 | Popup host: owned popup windows, placement, light dismiss, in-window overlay layer | §5, §13 | [ ] |
| 16 | §16 | Observable properties, `Command` objects and view-model binding helpers | §3 | [ ] |
| 17 | §17 | OLE drag-and-drop target and clipboard formats | §3, §12 | [ ] |
| 18 | §18 | `SwapChainHost`: composition swap chain element for the page canvas | §5, §6 | [ ] |
| 19 | §19 | Non-client hit testing: drag regions, resize edges, caption buttons, Snap Layouts | §4, §9, §10, §12 | [ ] |
| 20 | §20 | Golden-image suite: headless renders at 100, 150 and 200 percent against SPEC boards | §1, §9, §10 | [ ] |

## 1. `recto-ui` static library, module boundary and headless render harness

> **Started:** 2026-10-22T22:27:03Z

> **Corrected 2026-10-23:** three drifted claims, fixed before building. (1) The `/W4 /WX` and `stdcpp23` settings live in `Directory.Build.targets` (the section said `Directory.Build.props`, which only sets toolset, SDK, output and intermediate folders); the project inherits them either way and sets neither. (2) GoogleTest names are `Suite.Test` with single identifiers, so `TEST(Ui, ...)` cannot produce the three-part names this file filters on (`Ui.Project.IncludeBoundary`, `Ui.Dispatcher.*`, `Ui.App.*`) and a filter that matches nothing exits 0 with no tests run. `tests/recto-tests/ui_test.h` adds the `RECTO_UI_TEST(Area, Name)` macro that registers through `::testing::RegisterTest` under suite `Ui` and test name `Area.Name`, so every `Ui.<Area>.*` filter in this file selects exactly its tests; every `ui_*_tests.cpp` uses it (cost of reversing: rename the tests to `Ui<Area>` suites and every later checkpoint filter); `scripts/release-notes.py` accepts `Ui.<Area>.<Name>` as a gtest id (not a Python test id) so a user-bug fix can cite these tests in `Regression-Test:`. (3) `render_to_bitmap(root, ...)` cannot take an `Element` tree because `ui::Element` is built by `§6`: `root` is a `DrawRoot` callback (`std::function<void(const RenderContext&)>`, render target at the requested DPI so it draws in epx) plus a `RenderOptions::min_stride` so a test can force a stride wider than `width * 4`. `§6` adds the `Element` overload and `§5` moves the factory behind `GraphicsDevice::create_headless`. The include-boundary scan matches engine headers by bare name as well as by path (the engine headers are included as `"recto_pdf.h"`, not `"pdf/..."`), so the test builds the engine header set from the real tree.

> **Corrected 2026-10-23 (repair after the audit):** the audit (`docs/reviews/10-ui/audit-d10-t01-s1.md`) found that both source guards accept forbidden source the compiler accepts, so the two guard items are unticked until the repair below holds. The repair also builds every in-scope gap of the audit's completion review: the boundary at the project level (MSBuild-evaluated include folders and project references, before any native build), a harness that refuses bad requests before drawing and reports draw failures with their tags, apartment and concurrency guarantees, an RGBA adapter for the comparison helpers, a scale matrix, test-id hygiene (unique ids, disabled tests, a filter that selects nothing fails) and proof that `recto-ui` itself is instrumented under ASan. Filed rather than built: reusing the harness factories across renders (`§5`, which moves the harness onto `GraphicsDevice::create_headless`), and text determinism and failure artifacts for golden images (`§20`). Decision (default, cost of reversing: a Python port of the lexer): the source-level scan stays one native test with one lexer; the pre-build Python check covers the project level only.

Create the `recto-ui` C++23 static library in `src/ui/` (namespace `recto::ui`) and add it to `Recto.sln` for x64 and ARM64 Debug and Release. It depends on the Windows SDK only (design spec section 4.1): `src/ui` never includes `src/pdf`, `src/core` or `src/services`, and they never include `src/ui` (the D13 boundary). `Directory.Build.props` already applies `/W4 /WX` to every project named `recto-*`, so the project name `recto-ui` inherits the zero-warning bar with no per-project override. The section also lands the headless harness `recto::ui::test::render_to_bitmap(root, width_px, height_px, dpi_scale)` that renders an element tree into a WIC BGRA bitmap with an explicit stride, so every later section asserts pixels without a display session. The harness is a source file in `tests/recto-tests/` that links the static library; `recto-ui` itself carries no test code.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/ui/recto-ui.vcxproj` (static library, `PlatformToolset v145`, `LanguageStandard` inherited from `Directory.Build.targets` (C++23, D14), precompiled header `src/ui/pch.h`), add it to `Recto.sln`, and add a `ProjectReference` from `tests/recto-tests/recto-tests.vcxproj`. Done when: `msbuild src/ui/recto-ui.vcxproj /p:Configuration=Debug /p:Platform=x64` and the `ARM64` equivalent exit 0 with zero warnings.
2. Add `src/ui/include/recto/ui/ui.h` with the namespace and a version constant, plus `tests/recto-tests/ui_project_tests.cpp` with the include-boundary test. Done when: `Ui.Project.IncludeBoundary` scans `src/ui`, `src/pdf`, `src/core` and `src/services` and fails on a planted forbidden include.
3. Implement `tests/recto-tests/ui_render_harness.h` and `ui_render_harness.cpp` (`recto::ui::test::render_to_bitmap`, `Bitmap` struct with `width`, `height`, `stride`, `bytes`). Done when: a 64 x 64 solid-colour fixture round-trips through `compare_rgba` with the stride respected.

- [x] Create `src/ui/recto-ui.vcxproj` as a static library for x64 and ARM64 and add it to `Recto.sln`. Done when: both platforms build with zero warnings under `/W4 /WX`. Source: `Directory.Build.props`, design spec section 4.1.
- [x] Add `src/ui/pch.h`, `src/ui/pch.cpp` and `src/ui/include/recto/ui/ui.h` declaring `namespace recto::ui`. Done when: `recto-tests` includes `<recto/ui/ui.h>` and links.
- [x] Add the `Ui.Project.IncludeBoundary` test in `tests/recto-tests/ui_project_tests.cpp` that scans `#include` lines of `src/ui/**` for `pdf/`, `core/`, `services/` paths and of `src/pdf`, `src/core`, `src/services` for `recto/ui` paths. Done when: the test passes on the tree and fails when a forbidden include is planted in a scratch copy. Cheaper substitute: a grep in CI that is not a registered test.
- [x] Add a `Ui.Project.NoWinRtUiTypes` assertion in the same file that no `Microsoft.UI` or `winrt/Microsoft.UI` token appears under `src/ui`. Done when: the test fails if one is planted.
- [x] Implement `recto::ui::test::render_to_bitmap` in `tests/recto-tests/ui_render_harness.cpp` using `IWICImagingFactory2::CreateBitmap` (`GUID_WICPixelFormat32bppPBGRA`) and `ID2D1Factory7::CreateWicBitmapRenderTarget`, returning bytes plus the WIC stride. Done when: `Ui.Project.HarnessStride` renders into a bitmap whose stride is deliberately wider than `width * 4` and the pixels still compare equal. Source: Microsoft Learn `ID2D1Factory::CreateWicBitmapRenderTarget`. Cheaper substitute: assuming `stride == width * 4`.
- [x] Add `Ui.Project.HarnessSolid` rendering a filled rectangle and comparing it with `compare_rgba` at max delta 0. Done when: the test passes and fails when the fill colour is changed.
- [x] Add `tests/recto-tests/ui_*_tests.cpp` to `recto-tests.vcxproj` through a wildcard `ClCompile` item so later sections add files without editing the project. Done when: a newly dropped `ui_x_tests.cpp` is compiled with no project edit.
- [x] Harden the source lexer and include scan in `tests/recto-tests/ui_project_tests.cpp` (`logical_source`, `scan_includes`): preprocessing numbers with digit separators ([lex.ppnumber]), header names after `#include` or `#import` copied whole ([lex.header]), the `%:` digraph ([lex.digraph]), UTF-16 source with a byte order mark, raw-string bodies opaque, every compilable or includable extension (`.hh`, `.hxx`, `.ipp`, `.tpp`, `.inc`, `.c`, `.ixx`, `.cppm`), an unreadable file failing the scan, findings at the physical line, and engine includes of a toolkit header by bare name. Done when: `Ui.Project.IncludeBoundary` catches exactly its 27 planted shapes and `Ui.Project.ScanRefusesUnreadableFile` passes. Source: the audit round 1 findings and completion review items 7 and 8.
- [x] Report `Ui.Project.NoWinRtUiTypes` findings at their physical line through the same lexer. Done when: the planted set, including a token after a digit separator and a UTF-16 file, is caught at exactly `file:line`, and a token in a comment is not.
- [x] Add `tests/scripts/test_ui_module_boundary.py`: MSBuild evaluates `recto-pdf`, `recto-core`, `recto-services` and `recto-ui` in all four configurations, and no engine project includes a folder under `src/ui` or reaches `recto-ui` through project references (followed transitively), and `recto-ui` neither includes an engine folder nor reaches an engine project. Done when: `python -m unittest tests.scripts.test_ui_module_boundary` passes and its planted projects fail with the named violations. Cheaper substitute: a text search of the `.vcxproj` files, which misses inherited and conditioned settings.
- [x] Validate every `render_to_bitmap` request before COM, allocation or the callback (sides 1 to 16384, a finite scale from 0.25 to 16, `min_stride` at most 16 MiB), close the draw and rethrow when the callback throws, and name the HRESULT, the `SetTags` tags and the request when `EndDraw` fails. Done when: `Ui.Project.HarnessRefusesBeforeDrawing`, `Ui.Project.HarnessReportsDrawFailure` and `Ui.Project.HarnessPassesCallbackExceptions` pass. Source: Microsoft Learn `ID2D1RenderTarget::EndDraw`.
- [x] Prove the harness keeps the caller's COM apartment (none, STA, MTA) and renders identically on four threads at once. Done when: `Ui.Project.HarnessKeepsCallerApartment` and `Ui.Project.HarnessRendersConcurrently` pass.
- [x] Add `recto::ui::test::rgba_bytes` (packed premultiplied RGBA for `compare_rgba`, `expect_rgba_equal` and `compute_ssim`) and a scale matrix on a non-square bitmap at 125, 150 and 175 percent. Done when: `Ui.Project.HarnessRgbaAdapter` (half-transparent red, red and blue never swapped) and `Ui.Project.HarnessScaleMatrix` (every pixel of an aliased epx rectangle) pass.
- [x] Make test ids reliable: document disabled-test filtering in `tests/recto-tests/ui_test.h`, add `Ui.Project.UniqueTestIds`, `Ui.Project.DisabledTestsRunOnlyWhenAsked` (over a registered `Ui.DISABLED_Project.Probe`) and a `tests/scripts/test_release_notes.py` case that a disabled `Ui` test is not evidence. Done when: the three tests pass.
- [x] Make a run whose filter selects no test fail in `tests/recto-tests/gtest_defaults.cpp` (GoogleTest's `fail_if_no_test_selected`, unless the command line or `GTEST_FAIL_IF_NO_TEST_SELECTED` decides). Done when: `Ui.Project.EmptySelectionFails` passes (a filter matching nothing exits non-zero with "No tests ran", `--gtest_fail_if_no_test_selected=false` exits 0, listing is unaffected).
- [x] Prove `recto-ui` itself is instrumented under ASan: `recto::ui::built_with_address_sanitizer()` in `src/ui/include/recto/ui/ui.h`, `Ui.Project.ToolkitInstrumentedLikeTests`, and `tests/scripts/test_build_ps1.py` `AsanCoverage` (every C++ project in `Recto.sln`, except `recto-fuzz`, which `D06 T02 §3` builds with its own fuzzer instrumentation, is in the ASan clean list and target list). Done when: both pass, and `ToolkitInstrumentedLikeTests` passes in the `-EnableASAN` run.
- [ ] Commit: `ui: add recto-ui static library, module boundary tests and headless render harness`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Project.*` passes (every `Ui.Project` test, the count quoted), `python -m unittest tests.scripts.test_ui_module_boundary tests.scripts.test_build_ps1 tests.scripts.test_release_notes` passes, and `msbuild Recto.sln /p:Configuration=Debug /p:Platform=ARM64 /t:recto-ui` exits 0 (corrected 2026-10-23: the repair added the Python checks).

> **Unstamped:** 2026-10-23: audit 2026-10-23 (docs/reviews/10-ui/audit-d10-t01-s1.md, Sol round 1: STAMP does-not-hold, P1): Ui.Project.IncludeBoundary and Ui.Project.NoWinRtUiTypes accept forbidden source (digit separators hiding a comment opener, // inside an angle-bracket header name, the %:include digraph, UTF-16 source, bare toolkit header names from an engine library); repair -> XREF: D10 T01 §1 (revoked receipt `docs/reviews/d10-t01-s1.revoked-2026-10-23.json`)

## 2. Win32 `Recto.exe` host replaces the WinUI scaffold; Windows App SDK pins retired

> **Started:** 2026-10-23T01:09:11Z

Replace the WinUI scaffold from `D00 T01 §3` in `src/app/` with a plain Win32 host. Delete `App.xaml`, `App.xaml.h`, `App.xaml.cpp`, `MainWindow.xaml`, `MainWindow.xaml.h`, `MainWindow.xaml.cpp`, `MainWindow.idl`, the XAML and MIDL items in `Recto.vcxproj` and the `Microsoft.WindowsAppSDK` `PackageReference`. Add `src/app/main.cpp` with `wWinMain` that calls `ui::Application::run`. Keep `Microsoft.Windows.CppWinRT` and `Microsoft.Windows.SDK.BuildTools` only for the WinRT APIs Recto calls outside the UI (design spec section 1). `app.manifest` declares per-monitor v2 DPI awareness, long paths, UTF-8 code page and common-controls-free Windows 11 compatibility. `Package.appxmanifest` becomes a full-trust Win32 desktop package (`runFullTrust`, no Windows App SDK `PackageDependency`). The section also retires the WinUI pin machinery: `scripts/check-winui-pin.py`, `tests/scripts/test_check_winui_pin.py`, the `winui` entry in `.github/workflows/pins.yml`, the `winui/` branch pattern in `scripts/merge_prs.py`, any WinAppSDK handling in `build/build.ps1`, and the reference in `scripts/check-sdk-pins.py`'s docstring. `NOTICE.md` drops the Windows App SDK and WebView2 rows and `AGENTS.md` drops `check-winui-pin.py` from its validation list and the WinUI wording from its overview and interface rules.

> **Groomed 2026-10-23:** the section named the pin machinery but not every tracked file that still carries WinUI or Windows App SDK residue, and it did not turn `Recto.vcxproj` into a plain Win32 desktop project (today it is still `ApplicationType` `Windows Store`, `AppxPackage` `true`, `UseWinUI` `true`, with a NuGet moniker workaround, `module.g.cpp`, `Midl`, `Image` and `AppxManifest` items, and `src/app/pch.h` still includes ten `winrt/Microsoft.UI.*` headers). Measured today, the residue outside `docs/`, `todo/` and `design/mockups/` is: `src/app/*` (above), `scripts/check-winui-pin.py`, `tests/scripts/test_check_winui_pin.py`, `tests/scripts/test_check_sdk_pins.py` (lines 37 and 61 to 62), `tests/scripts/test_pin_automations.py` (lines 83 to 94), `.github/workflows/pins.yml` (lines 39 to 40), `scripts/merge_prs.py` (lines 39 to 41), `scripts/check-sdk-pins.py` (docstring lines 9 to 10), `build/build.ps1` (lines 22 to 23), `.conclave/automations/README.md` (line 21), `AGENTS.md` (overview, interface rules, validation line 89), `CONTRIBUTING.md` (line 43), `NOTICE.md` (lines 18 to 19), `docs/reference-machines.md` (lines 22 and 74), `docs/user-demand.md` (line 18) and `.gitattributes` (the `*.xaml` rule, removed by `§8` with `RectoColors.xaml`). `design/mockups/` is dated design history (three rejected directions that name WinUI 3) and is the one exception; `docs/plans/`, `docs/specs/`, `docs/reviews/` and `todo/` keep dated history notes. The micro-steps below add the missing files, the project conversion and a residue scan test so the cleanup is provable.

> **Corrected 2026-10-23:** validated against the tree before building (Opus 5.5 took the section over from an uncommitted Sonnet start and re-verified every line of it). (1) The residue scan: build order step 3's `rg` command and `Ui.App.NoWinUiResidue` as first written excluded all of `docs/` (so `docs/reference-machines.md` and `docs/user-demand.md`, which this section must clean, stayed unguarded) and would have failed on `design/tokens/RectoColors.xaml` and the `.gitattributes` `*.xaml` rule, which `§8` retires. The test is now the scan: case-insensitive `winui`, `windowsappsdk`, `windows app sdk`, `microsoft.ui`, `microsoft::ui`, `xaml` and `webview2`, plus any `.xaml` or `winui` path, over `git ls-files --cached --others --exclude-standard` (so residue is caught before it is committed), minus dated history (`docs/plans/`, `docs/specs/`, `docs/reviews/`, `todo/`, `design/mockups/`), binary files (a NUL in the first 8000 bytes) and a six-entry allowlist in `tests/recto-tests/ui_app_tests.cpp` that states each reason (`design/SPEC.md` names the Fluent look it reproduces; `RectoColors.xaml` and `.gitattributes` until `§8`; the two guard test files; the refused `winui/` case in `tests/scripts/test_merge_prs.py`). Step 3's Done when is `Ui.App.NoWinUiResidue` passing. (2) The scaffold's `Package.appxmanifest` had no `.pdf` association to keep; `D02 T01 §1` adds it. (3) `Microsoft.Windows.SDK.BuildTools` stays on a recorded reason (default, cost of reversing: one `PackageReference` and the `TRACKED` entry in `scripts/check-sdk-pins.py`): it pins `rc.exe` and `mt.exe`, which embed `app.manifest` and, from `§4`, the icon resource, plus `MakeAppx` and `signtool` for `D07 T01 §5`; `Microsoft.Windows.CppWinRT` stays for the WinRT calls outside the UI (design spec section 1). (4) Ambition within the surface: `--self-test-exit` waits for the compositor to present the first frame (`DwmFlush`) and then checks what the manifest promises in the running process (per-monitor v2 DPI awareness, `GetACP() == CP_UTF8`), exiting 2 or 3 when either is missing, because a correct XML file the linker never embedded passes every file check; `Ui.App.ExeEmbedsManifest` reads the `RT_MANIFEST` resource of the built `Recto.exe`; arguments come from `__wargv`, so a quoted path that contains the flag's text is not the flag (`Ui.App.ParsesArguments`); `Ui.App.SelfTestExit` and `Ui.App.ClosesWithExitCodeZero` launch the real `Recto.exe` unattended, which `tests/recto-tests/recto-tests.vcxproj` builds first through a build-order `ProjectReference`, and `build/build.ps1` adds `Recto` to the ASan build and runtime staging. (5) The message loop is a plain `GetMessageW` loop until `§3` routes it through the `Dispatcher` wait (`§3`'s `MsgWaitForMultipleObjectsEx` item). (6) A machine that built the WinUI scaffold keeps a stale merged `.winmd` under `build\obj\Recto\`, which the C++/WinRT targets glob and fail on; delete `build\obj\Recto` and `build\bin\<platform>\<configuration>\Recto` once (derived output; CI builds clean).

> **Deferred:** `design/tokens/RectoColors.xaml` and the `.gitattributes` `*.xaml` rule stay on the `Ui.App.NoWinUiResidue` allowlist until the token generator retires them -> XREF: D10 T01 §8

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Delete the XAML, MIDL and WinAppSDK items from `src/app/Recto.vcxproj`, delete the XAML and IDL files, and add `src/app/main.cpp` and `src/ui/include/recto/ui/application.h` (`Application::run`, message loop skeleton). Done when: `Recto.vcxproj` contains no `Page`, `Midl` or `Microsoft.WindowsAppSDK` item and builds.
2. Rewrite `src/app/app.manifest` and `src/app/Package.appxmanifest`. Done when: `Ui.App.ManifestDpiAwareness` parses the manifest XML and finds `PerMonitorV2`, and no `Windows.Desktop` framework dependency or `WindowsAppRuntime` appears in the appx manifest.
3. Remove the pin machinery and doc references. Done when: `python -m unittest discover -s tests/scripts` passes and `Ui.App.NoWinUiResidue` passes (corrected 2026-10-23: it was an `rg` command that excluded all of `docs/`; see the note above).
4. Convert `src/app/Recto.vcxproj` and `src/app/pch.h` to a plain Win32 desktop executable that references `recto-ui` and `recto-services`. Done when: the project has no `ApplicationType`, `AppxPackage`, `UseWinUI`, `EnableMsixTooling`, `MinimalCoreWin`, `NuGetTarget*`, `Midl`, `Page`, `ApplicationDefinition` or `module.g.cpp` item, `Package.appxmanifest` is a `None` item that the build never packages, and `Ui.App.ProjectIsPlainWin32` passes.
5. Update `Recto.sln` and the build scripts so the new graph still builds. Done when: `.\build.cmd -SkipTests` exits 0 for x64 and ARM64 and `Recto.exe` is produced in `build\bin\<platform>\<configuration>\`.

- [x] Delete `src/app/App.xaml*`, `src/app/MainWindow.xaml*`, `src/app/MainWindow.idl` and the matching `Recto.vcxproj` items; remove the `Microsoft.WindowsAppSDK` `PackageReference` and `WindowsAppSDKSelfContained`. Done when: `.\build.cmd` builds x64 and ARM64 with no XAML compiler step in the log.
- [x] Add `src/app/main.cpp` with `int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)` calling `recto::ui::Application::run` and returning its exit code; initialise COM as STA with `CoInitializeEx`. Done when: `Recto.exe` starts and exits with code 0 when the window closes.
- [x] Implement `ui::Application::run` in `src/ui/application.cpp`: `EnableMouseInPointer(TRUE)`, create the first `ui::Window` (empty, from §4 once it lands; a bare `CreateWindowExW` placeholder until then), pump messages through the §3 dispatcher hook, and honour `--self-test-exit` by posting `WM_CLOSE` after the first presented frame so a display-session test can launch and close it without a person. Done when: `Recto.exe` shows one empty window and `Recto.exe --self-test-exit` exits with code 0 within 5 seconds. Cheaper substitute: a hidden console-only exit.
- [x] Rewrite `src/app/app.manifest` for per-monitor v2 DPI (`<dpiAwareness>PerMonitorV2</dpiAwareness>`), `longPathAware`, `activeCodePage` UTF-8 and the Windows 10/11 supported-OS GUID. Done when: `Ui.App.ManifestDpiAwareness` passes. Source: Microsoft Learn "Setting the default DPI awareness for a process".
- [x] Rewrite `src/app/Package.appxmanifest` as a full-trust Win32 desktop app (`rescap:Capability Name="runFullTrust"`, `EntryPoint="Windows.FullTrustApplication"`) with no Windows App SDK framework dependency, keeping the asset and `.pdf` association entries. Done when: `Ui.App.AppxNoFramework` asserts the XML has `runFullTrust` and no `PackageDependency`.
- [x] Keep `Microsoft.Windows.CppWinRT` and `Microsoft.Windows.SDK.BuildTools` references in `src/app/Recto.vcxproj` only for WinRT APIs, and confirm `scripts/check-sdk-pins.py` still checks both pins. Done when: `python scripts/check-sdk-pins.py` exits 0 and `Ui.Project.NoWinRtUiTypes` still passes.
- [x] Remove `scripts/check-winui-pin.py`, `tests/scripts/test_check_winui_pin.py`, the `winui` matrix entry in `.github/workflows/pins.yml`, the `winui/` alternative in the branch regex of `scripts/merge_prs.py`, any WinAppSDK handling in `build/build.ps1`, and the WinUI sentence in the `scripts/check-sdk-pins.py` docstring; update `tests/scripts/test_merge_prs.py` and `test_build_ps1.py` if they referenced them. Done when: `python -m unittest discover -s tests/scripts` passes. Cheaper substitute: leaving the script and disabling its workflow job.
- [x] Convert `src/app/Recto.vcxproj` to a plain Win32 desktop project: delete `ApplicationType`, `ApplicationTypeRevision`, `AppxPackage`, `UseWinUI`, `EnableMsixTooling`, `MinimalCoreWin`, the `CppWinRT*` globals, the `NuGetTarget*` and `RuntimeIdentifiers` workaround, the `Image`, `AppxManifest`, `Midl`, `Page` and `ApplicationDefinition` items and `$(GeneratedFilesDir)module.g.cpp`; keep `Package.appxmanifest` and `Assets\` as `None` items for `D07 T01 §5`; add `main.cpp`, a `ProjectReference` to `..\ui\recto-ui.vcxproj`, `..\ui\include` on the include path, and `d3d11.lib;dxgi.lib;dcomp.lib;d2d1.lib;dwrite.lib;windowscodecs.lib;dwmapi.lib;uxtheme.lib` as link inputs; rewrite the header comment (no "WinUI 3 packaged app"). Done when: `msbuild src/app/Recto.vcxproj /p:Configuration=Debug /p:Platform=x64` and the `ARM64` equivalent exit 0 with zero warnings and `Ui.App.ProjectIsPlainWin32` (new, in `ui_app_tests.cpp`) fails when `UseWinUI` is restored. Source: Microsoft Learn "Walkthrough: Create a traditional Windows Desktop application (C++)". Cheaper substitute: leaving `AppxPackage` on and ignoring the packaging warnings.
- [x] Rewrite `src/app/pch.h` to include only `<windows.h>`, `<unknwn.h>`, `<winrt/Windows.Foundation.h>` and `<winrt/Windows.Foundation.Collections.h>` (delete the ten `winrt/Microsoft.UI.*` includes and the `GetCurrentTime` undef comment about `Storyboard`) and keep `src/app/pch.cpp` as the creating unit. Done when: `Recto.exe` builds and `rg -n "Microsoft.UI" src/app` returns nothing. Cheaper substitute: leaving the unused includes, which fail once the Windows App SDK package is gone.
- [x] Rewrite the `Executable` and `EntryPoint` attributes in `src/app/Package.appxmanifest` to `Recto.exe` and `Windows.FullTrustApplication` (the scaffold's `$targetnametoken$` and `$targetentrypoint$` tokens exist only for the removed packaging build) and keep every `Assets\` logo entry, because `scripts/check-package-assets.py` (`D07 T01 §5`) audits them. Done when: `Ui.App.AppxNoFramework` also asserts `Executable="Recto.exe"` and no `$target` token.
- [x] Edit `build/build.ps1` and `Recto.sln`: confirm `recto-ui` builds before `Recto` and `recto-tests` through project dependencies, delete the comment lines 22 to 23 that say the Windows App SDK comes from NuGet, and confirm the `-restore` step restores only `Microsoft.Windows.CppWinRT` and `Microsoft.Windows.SDK.BuildTools`. Done when: `.\build.cmd -SkipTests` exits 0 for both platforms and `Select-String build/build.ps1 -Pattern "App SDK"` returns nothing.
- [x] Fix `tests/scripts/test_check_sdk_pins.py`: drop the `Microsoft.WindowsAppSDK` line from `REFERENCES` and replace `test_windows_app_sdk_is_left_to_check_winui_pin` with `test_only_the_two_toolchain_packages_are_tracked` asserting `set(mod.TRACKED) == {"Microsoft.Windows.CppWinRT", "Microsoft.Windows.SDK.BuildTools"}`; in `tests/scripts/test_pin_automations.py` retarget `NotifierTests.test_notify_passes_the_automations_sender` at `scripts/lib/pin_proposal.py` `notify(root, subject, body, alert_module=fake)` (the shared implementation the removed script delegated to). Done when: `python -m unittest tests.scripts.test_check_sdk_pins tests.scripts.test_pin_automations` passes and the notifier test fails when `alert_module.send` is called without the `From` line. Cheaper substitute: deleting the notifier test.
- [x] Rewrite the `scripts/merge_prs.py` `PRODUCERS` branch regex to `pins/(?:toolchain|vcpkg)-[\w.-]+` and its comment to "the toolchain and vcpkg pins", and add a `tests/scripts/test_merge_prs.py` case that a `winui/2.5.2` branch from `github-actions[bot]` is refused as an unknown producer. Done when: `python -m unittest tests.scripts.test_merge_prs` passes and the new case fails against the old regex.
- [x] Update the prose that names the retired pin: `.conclave/automations/README.md` (delete the WinUI table row at line 21), `CONTRIBUTING.md` (drop the "WinUI application development workload" parenthesis and the "until `D10 T01 §2` the Windows App SDK" clause from the prerequisites), `docs/reference-machines.md` (remove "Windows App SDK `2.5.1`" from both Compiler and SDK cells and replace the line-74 `Select-String` probe with one that lists the `Microsoft.Windows.CppWinRT` and `Microsoft.Windows.SDK.BuildTools` versions), `docs/user-demand.md` line 18 ("the app is WinUI 3" becomes "the app draws its own UI with recto-ui on Direct2D"), the `scripts/check-sdk-pins.py` docstring lines 9 to 10 and `.github/workflows/pins.yml` (the matrix entry and any comment naming the WinUI pin). Done when: the residue scan from build order step 3 returns nothing.
- [x] Update `NOTICE.md` (remove the Windows App SDK 2.5.1 mention from the row at line 18 so it names only `Microsoft.Windows.CppWinRT`, and delete the WebView2 SDK row at line 19, since `recto-ui` has no web view) and `AGENTS.md` (overview, interface rules, validation list without `check-winui-pin.py`, `source-defect` lens without WinUI). Done when: `rg -n -i "windows app sdk|winui|webview2" NOTICE.md AGENTS.md` returns no stale claim and `python scripts/todo-graph.py validate` passes. Source: `D07 T02 §3` regenerates `NOTICE.md` from the build, so this edit only keeps the interim file true.
- [x] Add `Ui.App.*` tests in `tests/recto-tests/ui_app_tests.cpp` (manifest DPI, appx no framework, no `Microsoft.WindowsAppSDK` in `Recto.vcxproj`, plain Win32 project, and `Ui.App.NoWinUiResidue`, which walks `git ls-files` minus `docs/`, `todo/`, `design/mockups/`, `design/SPEC.md` and binary files and fails on `Microsoft.UI`, `WindowsAppSDK`, `check-winui-pin` or an `.xaml` path). Done when: `--gtest_filter=Ui.App.*` passes and fails when the package reference or a planted `.xaml` file is restored.
- [ ] Commit: `app: replace WinUI scaffold with Win32 wWinMain host; retire Windows App SDK pins`

**Test checkpoint:** `.\build.cmd` builds x64 and ARM64, `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.App.*` passes, `python -m unittest discover -s tests/scripts` passes, `python scripts/check-sdk-pins.py` exits 0, and `build\bin\x64\Debug\Recto\Recto.exe --self-test-exit` on the display-session host opens the window and exits with code 0.

## 3. UI thread `Dispatcher`, timers and engine callback marshalling

`ui::Dispatcher` replaces WinUI's `DispatcherQueue` (design spec section 4.2). Any thread calls `Dispatcher::post(std::move_only_function<void()>)`; the UI thread drains the queue on a private `WM_APP` message and from `MsgWaitForMultipleObjectsEx` so posted work and input interleave fairly. `Dispatcher::post_after(duration, fn)` and `ui::Timer` run on `SetTimer`-free waitable timers (`CreateWaitableTimerExW` with `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`) signalled into the same wait. `Dispatcher::is_ui_thread()` supports assertions, and `Dispatcher::post_and_wait` is deliberately absent so engine threads never block on the UI (plan 4.2). Files: `src/ui/dispatcher.h`, `src/ui/dispatcher.cpp`, `src/ui/timer.h`.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the lock-protected queue, the `PostMessageW` wake-up with a registered private message, and `run_one`/`drain` in `src/ui/dispatcher.cpp`. Done when: a thread posting 10,000 closures sees all run on the dispatcher thread in post order.
2. Implement waitable-timer scheduling and shutdown semantics (`Dispatcher::shutdown` rejects later posts and runs none). Done when: a timer set for 50 ms fires between 45 and 150 ms and never after `shutdown`.
3. Add `tests/recto-tests/ui_dispatcher_tests.cpp`. Done when: `Ui.Dispatcher.*` passes under AddressSanitizer.

- [ ] Create `src/ui/dispatcher.h` and `dispatcher.cpp` with `post`, `post_after`, `is_ui_thread`, `shutdown`; the queue holds `std::move_only_function<void()>`. Done when: `Ui.Dispatcher.CrossThreadPost` posts 8 threads x 1,000 closures and every closure runs on the UI thread id.
- [ ] Wake the loop with `PostMessageW(hwnd_message_only, WM_APP + 1)` using a message-only window owned by the dispatcher, and wait in `MsgWaitForMultipleObjectsEx(QS_ALLINPUT, MWMO_INPUTAVAILABLE)`. Done when: `Ui.Dispatcher.InterleavesWithInput` posts a closure while a synthetic `WM_TIMER` is pending and both run without starvation. Source: Microsoft Learn `MsgWaitForMultipleObjectsEx`.
- [ ] Implement `ui::Timer` (one-shot and repeating) on `CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)`. Done when: `Ui.Dispatcher.TimerFiresOnce` and `TimerRepeats` pass.
- [ ] Reject posts after `shutdown` without running or leaking closures. Done when: `Ui.Dispatcher.PostAfterShutdownIsDropped` passes and ASan reports no leak. Cheaper substitute: running the leftover closures on the destructor thread.
- [ ] Expose a `DispatcherHandle` (copyable, thread-safe, weak) that `DocumentActor` callbacks in `src/app` capture to marshal results, without `recto-services` including `src/ui`. Done when: `Ui.Dispatcher.HandleOutlivesDispatcher` posts through an expired handle and gets `false`.
- [ ] Add a debug assertion `RECTO_UI_THREAD_ONLY()` used by later sections on element mutation. Done when: `Ui.Dispatcher.AssertsOffThread` death-test passes in Debug.
- [ ] Commit: `ui: add UI thread Dispatcher, timers and thread-safe handle`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Dispatcher.*` passes (cross-thread post order, timers, shutdown, handle expiry) and the same run with `EnableASAN=true` reports no error.

## 4. Top-level `Window`: custom frame, Mica Alt, dark mode, DPI and minimum size

`ui::Window` creates a `WS_OVERLAPPEDWINDOW` HWND with `WS_EX_NOREDIRECTIONBITMAP`, extends the frame into the client area with `DwmExtendFrameIntoClientArea`, answers `WM_NCCALCSIZE` with the full client rectangle (keeping the system resize borders and shadow), and sets `DWMWA_SYSTEMBACKDROP_TYPE` to `DWMSBT_TABBEDWINDOW` (Mica Alt) and `DWMWA_USE_IMMERSIVE_DARK_MODE` from the theme (design spec section 4.3). It handles `WM_DPICHANGED` (relayout at the suggested rectangle), `WM_GETMINMAXINFO` (960 x 600 epx at the current scale, SPEC section 7), `WM_SETICON` from `resources/ico/recto.ico`, and activation events. Mica falls back to a solid `RectoWindowBrush` when unavailable or transparency effects are off. Files: `src/ui/window.h`, `src/ui/window.cpp`, `src/ui/window_backdrop.cpp`, `src/ui/window_fullscreen.cpp`.

> **Groomed 2026-10-23:** the SPEC `F11` Full Screen command (`design/SPEC.md` section 10) and `D02 T02 §10` already depend on `D10 T01 §4` for a full screen window, but no section built one (the file had no full screen, cursor or placement-restore step). The full screen API is added here as one more micro-step, not as a new section, so the existing `D02 T02` edge covers it. Revealing the chrome on screen-edge hover is the consumer's job (`D02 T02 §10`).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 3.1 (icon), 4 (window brush), 7 (minimum size) and 8.1 (title bar region) with `design/screenshots/D-Focus-Light.png` and `D-Focus-Dark.png` window frame and backdrop.
**Job:** The user opens Recto and gets a native-feeling window with the Windows 11 backdrop, system shadow and resize borders, the right icon in the taskbar and Alt+Tab, correct sizing on any monitor, and a window that cannot shrink below 960 x 600 epx. Consumer: every shell surface in `src/app`.
**Treatment:** One Win32 window with DWM frame extension, Mica Alt and per-monitor v2 DPI. Cheaper substitute that fails the checkpoint: a stock caption bar with a second drawn title row, a solid-colour client with no Mica, or a minimum size in raw pixels that is wrong above 100 percent scale.
**Chrome:** consumes `RectoWindowBrush` through `ThemeManager` (§8; until §8 ships the backdrop test asserts DWM attributes, not colour) and `resources/ico/recto.ico`; no hex colour is set in this section.

**Build order:**

1. Implement `ui::Window` class registration, creation with the extended styles, `WM_NCCALCSIZE`, DWM attributes and `dpi_scale()` in `src/ui/window.cpp`. Done when: a created window reports `DWMWA_SYSTEMBACKDROP_TYPE == DWMSBT_TABBEDWINDOW` through `DwmGetWindowAttribute` and has a full-client rectangle.
2. Implement `WM_DPICHANGED`, `WM_GETMINMAXINFO`, icon and activation events. Done when: `Ui.Window.MinTrackSize` returns `960 * scale` by `600 * scale` at 100, 150 and 200 percent.
3. Add `tests/recto-tests/ui_window_tests.cpp`. Done when: `Ui.Window.*` passes on the display-session host.

- [ ] Add `ui::Window` (`create`, `show`, `close`, `hwnd`, `dpi_scale`, `on_activated`, `on_size`, `on_dpi_changed`) in `src/ui/window.h` and `window.cpp` with `WS_EX_NOREDIRECTIONBITMAP`. Done when: the window is created without a redirection surface (`GetWindowLongPtr` shows the ex-style). Source: design spec section 4.3.
- [ ] Handle `WM_NCCALCSIZE` (wParam TRUE) by returning 0 with the proposed rect unchanged except the top inset when maximized (use `GetSystemMetricsForDpi(SM_CXPADDEDBORDER)`) and call `DwmExtendFrameIntoClientArea` with a 1 px top margin. Done when: `Ui.Window.ClientEqualsWindowWhenNormal` and `ClientInsetWhenMaximized` pass. Cheaper substitute: leaving the caption bar and hiding it with a child window.
- [ ] Set `DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_TABBEDWINDOW` when `ThemeManager::transparency_enabled()`, else `DWMSBT_NONE`, and `DWMWA_USE_IMMERSIVE_DARK_MODE` from the resolved theme in `src/ui/window_backdrop.cpp`. Done when: `Ui.Window.BackdropAttributes` reads both attributes back through `DwmGetWindowAttribute` in Light and Dark. Source: Microsoft Learn `DWMWINDOWATTRIBUTE`.
- [ ] Implement `WM_DPICHANGED` applying the suggested `RECT` with `SetWindowPos` and raising `on_dpi_changed(scale)`. Done when: `Ui.Window.DpiChangeRelayouts` posts a synthetic `WM_DPICHANGED` at 144 dpi and observes `dpi_scale() == 1.5` and the new bounds. Source: Microsoft Learn "WM_DPICHANGED".
- [ ] Implement `WM_GETMINMAXINFO` with `ptMinTrackSize` = 960 x 600 epx times the current scale. Done when: `Ui.Window.MinTrackSize` passes at 100, 125, 150, 175 and 200 percent. Cheaper substitute: a fixed 960 x 600 pixel limit.
- [ ] Send `WM_SETICON` (`ICON_BIG` and `ICON_SMALL`) from `resources/ico/recto.ico` loaded with `LoadImageW` at `GetSystemMetricsForDpi` sizes, embedded as a resource in `src/app/Recto.rc`. Done when: `Ui.Window.IconSet` finds both icons through `WM_GETICON`.
- [ ] Raise `on_activated(bool)` from `WM_ACTIVATE` and `WM_NCACTIVATE` (return TRUE so DWM does not draw a caption) and track `is_active()`. Done when: `Ui.Window.ActivationEvents` observes deactivate then activate.
- [ ] Expose `Window::set_root(std::shared_ptr<Element>)` as a declaration only (implemented in §6) and a `WM_GETOBJECT` hook point for D10 T03 §1. Done when: the header compiles and `Ui.Window.GetObjectHookIsCalled` passes with a stub provider.
- [ ] Add `ui::Window::set_full_screen(bool)` and `is_full_screen()` in `src/ui/window_fullscreen.cpp`: save the `WINDOWPLACEMENT` and style, cover the current monitor (`MonitorFromWindow`, `GetMonitorInfoW` `rcMonitor`) with a borderless `WS_POPUP` frame, raise `on_full_screen_changed`, and restore the saved placement and style on exit, including when the window was maximized, when it is moved to another monitor in between and when the monitor DPI differs. Done when: `Ui.Window.FullScreenCoversMonitor`, `Ui.Window.FullScreenRestoresPlacement` (normal and maximized) and `Ui.Window.FullScreenAcrossDpi` pass on the display-session host and a second call with the same value changes nothing. Source: SPEC section 10 (`F11`); Microsoft Learn "How do I switch a window between normal and fullscreen?" (Raymond Chen, The Old New Thing). Cheaper substitute: resizing the window to the screen size without removing the frame, which leaves the title bar and shadow.
- [ ] Commit: `ui: add Window with custom frame, Mica Alt, dark mode, per-monitor DPI, minimum size and full screen`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Window.*` passes on the display-session host (client rect, backdrop attributes, DPI change, minimum size at five scales, icon, activation), and `Recto.exe` shows a Mica Alt window in the screenshot compared by eye against the `D-Focus-Light.png` frame.

## 5. Compositor: shared D3D11 device, DirectComposition tree, D2D surfaces, device loss

`ui::GraphicsDevice` owns one `ID3D11Device` (`D3D11_CREATE_DEVICE_BGRA_SUPPORT`, hardware with WARP fallback), one multithreaded `ID2D1Factory7`, one `ID2D1Device6`, one `IDWriteFactory7` and one `IDCompositionDesktopDevice` (design spec section 4.4). `ui::Compositor` maps layers to `IDCompositionVisual2` objects backed by `IDCompositionVirtualSurface`, drawing with `BeginDraw` on the dirty rectangle only. Device loss (`DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_RESET`, `D2DERR_RECREATE_TARGET`) rebuilds every surface and re-issues every registered swap chain without losing element state. Files: `src/ui/graphics_device.h`, `graphics_device.cpp`, `compositor.h`, `compositor.cpp`.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `GraphicsDevice::create` with hardware then WARP fallback and expose the shared `ID3D11Device` and factories. Done when: `Ui.Compositor.DeviceCreated` passes on a host and with `D3D_DRIVER_TYPE_WARP` forced.
2. Implement `Compositor` visual tree and virtual surface draw with dirty rects. Done when: drawing one 16 x 16 dirty rect in a 512 x 512 surface changes only those pixels in a captured readback.
3. Implement device-loss recovery and the `Ui.Compositor.DeviceLostRecovers` test. Done when: after simulated removal the next frame renders and every registered swap-chain host is re-issued once.

- [ ] Add `GraphicsDevice` (`create`, `d3d_device`, `d2d_factory`, `d2d_device`, `dwrite_factory`, `dcomp_device`, `recreate`) with `D3D11_CREATE_DEVICE_BGRA_SUPPORT` and `D2D1_FACTORY_TYPE_MULTI_THREADED`. Done when: `Ui.Compositor.DeviceCreated` passes. Source: Microsoft Learn `D3D11CreateDevice`, `D2D1CreateFactory`.
- [ ] Fall back to `D3D_DRIVER_TYPE_WARP` when hardware creation fails, and record which was used in `GraphicsDevice::is_software()`. Done when: `Ui.Compositor.WarpFallback` forces the hardware path to fail via a test hook and still creates the device. Cheaper substitute: failing startup when no GPU exists.
- [ ] Add `Compositor` and `Layer` (`IDCompositionVisual2`, `IDCompositionVirtualSurface`, `resize`, `begin_draw(dirty)`, `end_draw`) bound to a `Window` through `IDCompositionTarget`. Done when: `Ui.Compositor.TargetBound` creates a target for a real window and `Commit` succeeds.
- [ ] Implement dirty-rect drawing: `Layer::begin_draw` clips to the rect and returns the `ID2D1DeviceContext` offset. Done when: `Ui.Compositor.DirtyRectOnly` shows untouched pixels unchanged after a 16 x 16 draw. Source: Microsoft Learn `IDCompositionVirtualSurface::BeginDraw`.
- [ ] Add `GraphicsDevice::on_device_lost` handling for `DXGI_ERROR_DEVICE_REMOVED` and `D2DERR_RECREATE_TARGET`: recreate the device, every surface, and call each registered `DeviceDependent::on_device_recreated`. Done when: `Ui.Compositor.DeviceLostRecovers` simulates removal through `ID3D11Device5`/`IDXGIDevice` test hook (`GraphicsDevice::test_force_device_removed`) and the next draw succeeds.
- [ ] Re-issue swap chains: expose `register_device_dependent(weak_ptr<DeviceDependent>)` that §18 `SwapChainHost` uses. Done when: `Ui.Compositor.DependentsNotifiedOnce` counts exactly one callback per recovery.
- [ ] Add a headless variant: `GraphicsDevice::create_headless` for the harness (no `IDCompositionTarget`). Done when: §1's `render_to_bitmap` can use it and `Ui.Project.HarnessSolid` still passes.
- [ ] Reuse the headless factories across renders in `tests/recto-tests/ui_render_harness.cpp` (one `ID2D1Factory7` and one WIC factory per thread from `GraphicsDevice::create_headless`, the caller's COM apartment still kept) and add `Ui.Compositor.HarnessRenderCost`, which renders a 64 x 64 bitmap 200 times and records the warm median and p95. Done when: the warm median is at most 1 ms and the p95 at most 2 ms on the reference machine (`docs/reference-machines.md`), `Ui.Project.HarnessKeepsCallerApartment` and `Ui.Project.HarnessRendersConcurrently` still pass, and the measured numbers are quoted in the stamp. Source: the `D10 T01 §1` audit completion review 2026-10-23 (every render recreated COM, WIC and Direct2D factories: warm median 5 ms, p95 9 ms); Microsoft Learn "Improving the performance of Direct2D apps" (reuse resources). Cheaper substitute: a process-wide factory created in one apartment and used from others without marshaling.
- [ ] Commit: `ui: add GraphicsDevice and Compositor with virtual surfaces and device-loss recovery`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Compositor.*` passes (device creation, WARP fallback, dirty-rect isolation, device-lost recovery with exactly one notification per dependent).

## 6. `Element` tree: retained rendering, invalidation, hit testing and visual layers

`ui::Element` is the retained tree node (design spec section 4.5): parent and children, desired and arranged bounds in epx, margin, padding, alignment, min and max size, visibility, opacity, transform, clip, tooltip text and automation properties (`name`, `help_text`, `automation_id`, `role`), `render(Painter&)`, `hit_test(point)` and `invalidate(rect)`. Elements that ask for their own layer (floating cards, the command box, dialogs, the page canvas host, anything animated in composition) get an `IDCompositionVisual2` through §5; the rest paint into their ancestor layer. Invalidation coalesces dirty rectangles per layer and draws once per frame on the UI thread. `Window::set_root` attaches the tree. Files: `src/ui/element.h`, `element.cpp`, `layer_host.cpp`, `invalidation.cpp`.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the element tree (add, remove, reparent without recreation, enumerate) in `src/ui/element.cpp`. Done when: reparenting an element keeps its pointer identity and its property values.
2. Implement invalidation and per-layer draw scheduling. Done when: two invalidations in one frame produce one draw whose dirty rect is their union.
3. Implement hit testing with clip and opacity rules, and tests in `tests/recto-tests/ui_element_tests.cpp`. Done when: `Ui.Element.*` passes.

- [ ] Add `ui::Element` with parent/children, `add_child`, `remove_child`, `reparent(Element*)`, visibility and opacity in `src/ui/element.h`. Done when: `Ui.Element.ReparentKeepsIdentity` passes (D17 requires no recreation). Source: design spec section 4.5.
- [ ] Add automation properties (`name`, `help_text`, `automation_id`, `role`, `access_key`, `accelerator_text`) and `tooltip`. Done when: `Ui.Element.AutomationPropertiesRoundTrip` passes; D10 T03 §1 reads exactly these.
- [ ] Implement `invalidate(rect)` and a frame scheduler that coalesces dirty rects per layer and draws on the UI thread through `Dispatcher`. Done when: `Ui.Element.InvalidateCoalesces` shows one draw call for 100 invalidations. Cheaper substitute: redrawing the whole window on every invalidate.
- [ ] Implement `hit_test(point_epx)` honouring visibility, clip, transform and `hit_test_visible`. Done when: `Ui.Element.HitTestRespectsClipAndZ` passes with overlapping siblings and a clipped child.
- [ ] Add layer ownership: `Element::request_layer()` allocates a `Layer` (§5), draws the subtree into it and positions the visual. Done when: `Ui.Element.LayerMovesWithoutRedraw` moves a layered element and observes zero `render` calls.
- [ ] Implement `Window::set_root`, resize propagation and the first-frame draw in `src/ui/window.cpp`. Done when: `Ui.Element.RootDrawsOnResize` renders a coloured root through the harness at two sizes.
- [ ] Guard mutation to the UI thread with `RECTO_UI_THREAD_ONLY()` from §3. Done when: `Ui.Element.MutationOffThreadAsserts` passes in Debug.
- [ ] Commit: `ui: add Element tree with invalidation, hit testing and composition layers`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Element.*` passes (identity-preserving reparent, coalesced invalidation, hit test order, layer move without redraw).

## 7. Layout engine: epx measure and arrange, pixel snapping, Stack, Grid, Dock, Overlay

Layout is measure then arrange in effective pixels (epx) with final positions rounded to whole device pixels at the window's scale so 1 epx borders stay crisp at 100, 125, 150, 175 and 200 percent (design spec section 4.5). Panels: `StackPanel`, `Grid` (pixel, auto and star tracks), `DockPanel`, `OverlayPanel` (named slots), `WrapPanel`. The SPEC's eight named slots (title bar, top bar, bottom overlay, left dock, right dock, left overlay, right overlay, bottom dock; SPEC section 8) are `OverlayPanel` and `DockPanel` children of the shell; moving a control between slots reparents the same element instance and nothing is recreated (D17). Files: `src/ui/layout.h`, `layout.cpp`, `panels/stack_panel.cpp`, `grid.cpp`, `dock_panel.cpp`, `overlay_panel.cpp`, `wrap_panel.cpp`, `src/ui/shell_slots.h`.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `measure`/`arrange` with margin, padding, alignment, min/max and the pixel-snapping pass in `src/ui/layout.cpp`. Done when: a 1 epx child at x = 0.5 epx lands on a whole device pixel at every tested scale.
2. Implement the five panels. Done when: golden layout tables for each panel pass in `tests/recto-tests/ui_layout_tests.cpp`.
3. Implement named slots and slot reparenting. Done when: moving the "thumbnail list" test element across three presets keeps one instance.

- [ ] Add `Element::measure(available_epx)` and `arrange(final_rect_epx)` with margin, padding, horizontal/vertical alignment and min/max size in `src/ui/layout.cpp`. Done when: `Ui.Layout.MeasureArrangeBasics` passes.
- [ ] Add pixel snapping: `snap_rect(rect_epx, scale)` rounds edges (not sizes) to whole device pixels, so adjacent siblings never gap or overlap. Done when: `Ui.Layout.SnapAtScales` passes at 100, 125, 150, 175 and 200 percent for 1 epx borders and 4 epx grid steps. Cheaper substitute: rounding sizes independently, which leaves 1 px gaps at 125 percent.
- [ ] Implement `StackPanel` (horizontal/vertical, spacing, fill-last) in `src/ui/panels/stack_panel.cpp`. Done when: `Ui.Layout.StackSpacing` passes.
- [ ] Implement `Grid` with pixel, auto and star tracks, row/column span, in `src/ui/panels/grid.cpp`. Done when: `Ui.Layout.GridStarAuto` passes including a star track shrinking below content and an auto track measuring unconstrained.
- [ ] Implement `DockPanel` (left, top, right, bottom, fill) and `WrapPanel` in `src/ui/panels/`. Done when: `Ui.Layout.DockFill` and `Ui.Layout.WrapBreaks` pass.
- [ ] Implement `OverlayPanel` with named slot anchors (alignment plus offset in epx) in `src/ui/panels/overlay_panel.cpp`. Done when: `Ui.Layout.OverlayAnchors` places a bottom-centre child at the SPEC 8.2 position within 0 px.
- [ ] Define `ShellSlots` in `src/ui/shell_slots.h`: the eight SPEC section 8 slots as an enum and a `ShellLayout` panel composing `DockPanel` plus `OverlayPanel`, with `move_to_slot(Element&, Slot)`. Done when: `Ui.Layout.SlotMovePreservesInstance` moves one element across all eight slots and the pointer, property values and focus state are unchanged. Source: SPEC section 8, D17.
- [ ] Add a layout stress test: 5,000 elements measure and arrange in under 16 ms on the Debug-less (Release) build. Done when: `Ui.Layout.StressBudget` passes in Release. Cheaper substitute: asserting only correctness.
- [ ] Commit: `ui: add layout engine, panels, pixel snapping and shell slots`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Layout.*` passes at 100, 125, 150, 175 and 200 percent (snapping, panels, slot reparenting without recreation).

## 8. Design tokens generated from `design/tokens/colors.json` and `ThemeManager`

`design/tokens/colors.json` becomes the single colour source (design spec section 4.6). `scripts/build-ui-tokens.py` (stdlib Python only, `--check` mode for CI) generates `src/ui/generated/recto_tokens.g.h` with every `Recto*Brush` colour for Light and Dark, the HighContrast mapping of each brush to a system colour (`COLOR_WINDOW`, `COLOR_WINDOWTEXT`, `COLOR_HIGHLIGHT`, `COLOR_HIGHLIGHTTEXT`, `COLOR_HOTLIGHT`, `COLOR_GRAYTEXT`, `COLOR_BTNFACE`, `COLOR_BTNTEXT`), the radii (`ControlCornerRadius` 4, `OverlayCornerRadius` 8, pill), the 4 epx spacing grid, the elevations (Z 32 and Z 64), the type ramp (SPEC section 5) and the motion constants (167 ms, 8 epx; SPEC section 12). Token names stay the SPEC's names. `ui::ThemeManager` resolves Light, Dark or Follow Windows (registry `AppsUseLightTheme`, `WM_SETTINGCHANGE` `ImmersiveColorSet`), high contrast (`SPI_GETHIGHCONTRAST`, `WM_SYSCOLORCHANGE`), transparency effects (`EnableTransparency`) and the animation setting (`SPI_GETCLIENTAREAANIMATION`), and repaints every window live. `design/tokens/RectoColors.xaml` is deleted, and SPEC sections 1 and 4 and `design/tokens/README` text point at `colors.json`. A test proves no hex colour literal appears in `src/ui` or `src/app` outside the generated header.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 4 (colour), 5 (type ramp values), 7 (radii, spacing, elevation) and 12 (motion constants) with `design/screenshots/D-Brand.png` (token swatches) and `D-Focus-Dark.png` (dark surfaces).
**Job:** The user gets Recto's vermilion accent and exact Light, Dark and HighContrast surfaces in every window, switching live when Windows changes theme or contrast; the developer changes a colour in one JSON file. Consumer: every painter call in `src/ui` and `src/app`.
**Treatment:** Generated header from `colors.json` with a `--check` gate, `ThemeManager` resolving every brush per theme, and a no-hex-literal test. Cheaper substitute that fails the checkpoint: copying hex values into C++ constants by hand, keeping `RectoColors.xaml` alongside `colors.json`, or mapping HighContrast to fixed colours instead of system colours.
**Chrome:** consumes `design/tokens/colors.json` only; produces `recto::ui::tokens::*` consumed by §9, §10, §13 and the controls in D10 T02.

**Build order:**

1. Write `scripts/build-ui-tokens.py` (generate and `--check`) and `tests/scripts/test_build_ui_tokens.py`. Done when: `python scripts/build-ui-tokens.py --check` exits 0 on the committed header and exits non-zero after editing one colour in `colors.json` without regenerating.
2. Implement `ui::ThemeManager` in `src/ui/theme_manager.h` and `theme_manager.cpp` with the registry, `SystemParametersInfoW` and `WM_SETTINGCHANGE` handling. Done when: `Ui.Tokens.ThemeResolution` passes for Light, Dark, Follow Windows and HighContrast.
3. Wire the generator into the build as a pre-build step of `recto-ui.vcxproj` and delete `design/tokens/RectoColors.xaml`. Done when: `Ui.Tokens.NoHexLiteralsOutsideGenerated` passes and nothing references the XAML file.

- [ ] Write `scripts/build-ui-tokens.py` reading `design/tokens/colors.json` and emitting `src/ui/generated/recto_tokens.g.h` with `constexpr` colour arrays per theme, token names exactly as the SPEC (`RectoWindowBrush`, `RectoSurfaceBrush`, `RectoAccentBrush`, and so on). Done when: the header contains every key of `colors.json` for both themes and the script uses the stdlib only. Source: SPEC section 4. Cheaper substitute: hand-maintained header.
- [ ] Add `--check` mode that regenerates in memory and compares bytes with the committed header. Done when: `python scripts/build-ui-tokens.py --check` exits 0 on a clean tree and 1 after a one-character edit to the header.
- [ ] Add the HighContrast table mapping each brush to one of the eight system colours via `GetSysColor`, emitted as `enum class SysColor` data. Done when: `Ui.Tokens.HighContrastUsesSystemColors` resolves `RectoTextBrush` to `COLOR_WINDOWTEXT` and `RectoAccentBrush` to `COLOR_HIGHLIGHT` under a forced contrast theme. Source: Microsoft Learn `GetSysColor`.
- [ ] Emit radii (4, 8, pill as `half height`), the 4 epx spacing grid, elevation records (Z 32, Z 64: key and ambient blur, offsets, opacities), the type ramp (Caption 12, Body 14, BodyStrong 14 Semibold, Subtitle 20 Semibold, Title 28 Semibold, TitleLarge 40 Semibold, GroupLabel 12 Semibold caps 0.6 tracking) and motion constants (167 ms, 8 epx). Done when: `Ui.Tokens.ConstantsMatchSpec` asserts each value against numbers quoted from SPEC sections 5, 7 and 12.
- [ ] Implement `ui::ThemeManager` (`theme()`, `set_preference(Light|Dark|FollowWindows)`, `is_high_contrast()`, `transparency_enabled()`, `animations_enabled()`, `brush(Token)`, `on_changed`) in `src/ui/theme_manager.cpp`, reading `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize` values `AppsUseLightTheme` and `EnableTransparency`. Done when: `Ui.Tokens.ThemeResolution` passes with a registry-read test hook. Source: Microsoft Learn `SystemParametersInfo` `SPI_GETHIGHCONTRAST`, `SPI_GETCLIENTAREAANIMATION`.
- [ ] Handle `WM_SETTINGCHANGE` (`ImmersiveColorSet`, `SPI_SETHIGHCONTRAST`), `WM_SYSCOLORCHANGE` and `WM_THEMECHANGED` in `ui::Window`, re-resolve tokens, invalidate every layer and re-apply the DWM dark attribute. Done when: `Ui.Tokens.LiveRepaint` flips the theme and a rendered root changes colour in the next harness frame without recreating elements.
- [ ] Delete `design/tokens/RectoColors.xaml`; update SPEC sections 1 and 4, `AGENTS.md` interface rules and `design/tokens` mentions to point at `colors.json` and the generated header. Done when: `rg -n "RectoColors.xaml" --glob "!docs/**" --glob "!todo/**" --glob "!resources/**"` returns nothing.
- [ ] Remove the `design/tokens/RectoColors.xaml` and `.gitattributes` entries from `kAllowed` in `tests/recto-tests/ui_app_tests.cpp` and the `*.xaml text eol=lf` line from `.gitattributes`, and close `§2`'s `Deferred:` line as `Resolved:`. Done when: `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.App.NoWinUiResidue` passes with both entries gone (added 2026-10-23 by `§2`, which left them on the allowlist for this section). Cheaper substitute: leaving the allowlist entries, which lets a new `.xaml` file or `xaml` mention under `design/tokens/` back in unnoticed.
- [ ] Add `Ui.Tokens.NoHexLiteralsOutsideGenerated` in `tests/recto-tests/ui_tokens_tests.cpp` scanning `src/ui` and `src/app` for `#[0-9A-Fa-f]{6}` and `0x[0-9A-Fa-f]{6,8}` colour-shaped literals outside `src/ui/generated/`. Done when: the test passes and fails when a hex colour is planted in a scratch copy. Cheaper substitute: a code-review convention.
- [ ] Add `tests/scripts/test_build_ui_tokens.py` (determinism, `--check` failure, missing key). Done when: `python -m unittest tests.scripts.test_build_ui_tokens` passes.
- [ ] Commit: `ui: generate design tokens from colors.json and add ThemeManager`

**Test checkpoint:** `python scripts/build-ui-tokens.py --check` exits 0, `python -m unittest tests.scripts.test_build_ui_tokens` passes, and `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Tokens.*` passes (spec constants, system-colour mapping, live repaint, no hex literals outside the generated header).

## 9. Drawing primitives: token radii, hairlines, borders, opacity, elevation shadows

`ui::Painter` wraps an `ID2D1DeviceContext` for an element subtree (design spec section 4.4): rounded rectangles at the token radii, 1 px hairlines snapped to device pixels, borders, solid and gradient brushes from tokens, opacity layers, clips, and elevation shadows. Elevation reproduces Fluent `ThemeShadow` Z 32 (toolbar, floating panels) and Z 64 (dialogs, command box) as a key blur plus an ambient blur using the Direct2D Shadow effect (`CLSID_D2D1Shadow`), cached per size and invalidated on theme or scale change. Acrylic on the floating side panel (SPEC 8.3, "where available") blurs the layer beneath with `CLSID_D2D1GaussianBlur` plus Fluent noise and tint, falling back to `RectoRaisedBrush` at 90 percent opacity when transparency effects are off or on battery saver (design spec section 4.4). Files: `src/ui/painter.h`, `painter.cpp`, `elevation.cpp`, `acrylic.cpp`.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 7 (radii 4 and 8, pill, elevation Z 32 and Z 64, document page shadow 1 epx key and 24 epx ambient) and section 8.3 (acrylic side panel) with `design/screenshots/D-Focus-Light.png`, `D-Focus-Dark.png` and `D-Dialogs.png` card and dialog shadows.
**Job:** The user sees floating things (toolbar, panels, command box, menus, dialogs, queue card) lift off the canvas with the SPEC shadows, crisp 1 px borders at every scale, and the translucent side panel only when Windows allows it. Consumer: every control in D10 T02 and every shell surface.
**Treatment:** D2D Shadow effect with Fluent key and ambient parameters, snapped hairlines and token radii only. Cheaper substitute that fails the checkpoint: a fixed grey offset rectangle for shadows, un-snapped 1 px borders that blur at 125 percent, or per-control radius literals.
**Chrome:** consumes §8 tokens (`RectoBorderBrush`, `RectoSurfaceBrush`, radii, elevation records) and §5 layers; produces `Painter` for every `Element::render`.

**Build order:**

1. Implement `Painter` primitives (fill, stroke, rounded rect, hairline, clip, opacity layer) in `src/ui/painter.cpp`. Done when: `Ui.Painter.RoundedRectRadii` renders 4 and 8 epx corners matching a reference at SSIM >= 0.99.
2. Implement elevation shadows with caching in `src/ui/elevation.cpp`. Done when: `Ui.Painter.ElevationZ32` and `ElevationZ64` match the recorded reference crops at SSIM >= 0.97.
3. Implement acrylic and its fallback in `src/ui/acrylic.cpp`. Done when: `Ui.Painter.AcrylicFallback` renders opaque raised brush at 90 percent when transparency is off.

- [ ] Add `Painter` (`fill_rect`, `fill_rounded`, `stroke_rounded`, `hairline`, `push_clip`, `push_opacity`, `draw_shadow`) over `ID2D1DeviceContext5` in `src/ui/painter.h`. Done when: `Ui.Painter.Basics` renders a filled rect through the harness exactly (delta 0).
- [ ] Snap hairlines: `Painter::hairline` aligns a 1 epx line to whole device pixels (`floor(x * scale) + 0.5` centre for 1 px strokes) at the window scale. Done when: `Ui.Painter.HairlineCrispAt125` shows a single-pixel column of full-intensity border colour at 125 percent. Cheaper substitute: drawing at fractional coordinates and accepting a two-pixel grey line.
- [ ] Add token radii helpers `control_radius()`, `overlay_radius()`, `pill_radius(height)`. Done when: `Ui.Painter.PillIsHalfHeight` asserts radius = height / 2. Source: SPEC section 7.
- [ ] Implement `ElevationShadow` with the `CLSID_D2D1Shadow` effect, key and ambient layers from the Z 32 and Z 64 token records, cached per (size, scale, theme). Done when: `Ui.Painter.ElevationZ32` and `Ui.Painter.ElevationZ64` match reference crops, and `Ui.Painter.ShadowCacheReuse` observes one effect build for 100 draws. Source: Microsoft Learn "Shadow effect"; WinUI `ThemeShadow` values recorded in the tokens (design spec section 4.4).
- [ ] Implement the document-page shadow (1 epx key, 24 epx ambient, square corners) as `Painter::draw_page_shadow`. Done when: `Ui.Painter.PageShadow` matches its reference crop. Source: SPEC section 7.
- [ ] Implement the acrylic backdrop (`CLSID_D2D1GaussianBlur` over the sampled layer beneath, noise tile, tint from `RectoRaisedBrush`) in `src/ui/acrylic.cpp`. Done when: `Ui.Painter.AcrylicBlur` shows the blurred underlying pattern at the panel position. Source: SPEC section 8.3.
- [ ] Add the acrylic fallback to `RectoRaisedBrush` at 90 percent opacity when `ThemeManager::transparency_enabled()` is false or `GetSystemPowerStatus` reports battery saver. Done when: `Ui.Painter.AcrylicFallback` passes for both causes. Cheaper substitute: always blurring.
- [ ] Add HighContrast painting rules (no shadow, 1 px `COLOR_WINDOWTEXT` border on elevated surfaces). Done when: `Ui.Painter.HighContrastNoShadow` passes under a forced contrast theme.
- [ ] Commit: `ui: add Painter primitives, elevation shadows and acrylic backdrop`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Painter.*` passes: radii, snapped hairlines at 125 percent, Z 32 and Z 64 shadows against reference crops at SSIM thresholds from `test_support.h`'s `compute_ssim`, acrylic fallback.

## 10. Text: DirectWrite type ramp, `TextBlock`, trimming and highlighted ranges

`ui::TextStyle` encodes the SPEC section 5 ramp as named styles (`Caption` 12, `Body` 14, `BodyStrong` 14 Semibold, `Subtitle` 20 Semibold, `Title` 28 Semibold, `TitleLarge` 40 Semibold, `GroupLabel` 12 Semibold caps with 0.6 tracking) in Segoe UI Variable, with the optical axis (`opsz`) set from the size as WinUI does through `IDWriteTextFormat3` and `DWRITE_FONT_AXIS_VALUE` (design spec section 4.7). `ui::TextBlock` measures and draws wrapped or single-line text with ellipsis trimming (`DWRITE_TRIMMING_GRANULARITY_CHARACTER` with an inline ellipsis sign), line-height and alignment, and bold highlighted character ranges (the command box's typed characters). Antialiasing: grayscale on composition layers with alpha, ClearType where the layer is opaque. Files: `src/ui/text_style.h`, `text_block.h`, `text_block.cpp`, `text_layout_cache.cpp`.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 5 (type ramp and group labels) with `design/screenshots/D-Home.png` (Title large question), `D-Settings.png` (group labels, row titles) and `D-Dialogs.png` (Subtitle, command box highlight).
**Job:** The user reads crisp Segoe UI Variable at the SPEC sizes, long names truncate with an ellipsis instead of overflowing, and the command box shows which typed characters matched. Consumer: every control that shows text.
**Treatment:** DirectWrite with named styles, optical sizing and the grayscale/ClearType rule. Cheaper substitute that fails the checkpoint: GDI text, one font size for all roles, truncating by clipping without an ellipsis, or bolding the whole matched word.
**Chrome:** consumes §8 type-ramp tokens and text colour tokens; produces `TextBlock` and `TextStyle` used by D10 T02 controls.

**Build order:**

1. Implement `TextStyle` and the `IDWriteFactory7` text-format cache in `src/ui/text_style.cpp`. Done when: each named style reports the SPEC size, weight and tracking.
2. Implement `TextBlock` measure, draw, wrapping and trimming in `src/ui/text_block.cpp`. Done when: `Ui.Text.TrimEllipsis` shows the ellipsis and a measured width <= the available width.
3. Implement highlight ranges and the antialiasing rule. Done when: `Ui.Text.HighlightRanges` renders bold only for the given character ranges and `Ui.Text.AntialiasModeByLayer` passes.

- [ ] Add `TextStyle` (name, size, weight, line height, tracking, caps, `opsz`) with the seven SPEC styles in `src/ui/text_style.h`. Done when: `Ui.Text.RampMatchesSpec` asserts every row against SPEC section 5. Source: SPEC section 5.
- [ ] Create `IDWriteTextFormat3` for "Segoe UI Variable" with `DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE` set to the point size, falling back to "Segoe UI" when the variable family is missing. Done when: `Ui.Text.OpticalSizeAxis` reads the axis value back for 12 and 28. Source: Microsoft Learn `IDWriteTextFormat3::SetFontAxisValues`.
- [ ] Add `ui::TextBlock` (text, style, colour token, wrapping, max lines, alignment) with `measure`/`render`. Done when: `Ui.Text.MeasureWrap` passes for 1, 2 and 3 lines at three widths.
- [ ] Implement trimming with `IDWriteInlineObject` ellipsis (`IDWriteFactory::CreateEllipsisTrimmingSign`) and `SetTrimming`. Done when: `Ui.Text.TrimEllipsis` passes for a 40-character file name in a 120 epx slot. Cheaper substitute: clipping at the edge with no ellipsis.
- [ ] Add `set_highlight(std::vector<Range>)` using `IDWriteTextLayout::SetFontWeight` on ranges, in `src/ui/text_block.cpp`. Done when: `Ui.Text.HighlightRanges` renders SemiBold glyphs only inside the range. Source: SPEC section 9.4.
- [ ] Implement `GroupLabel` upper-casing (locale-aware `LCMapStringEx` with `LCMAP_UPPERCASE`) and 0.6 tracking via `IDWriteTextLayout1::SetCharacterSpacing`. Done when: `Ui.Text.GroupLabelCapsTracking` compares glyph advance with and without tracking.
- [ ] Choose antialiasing per layer: `D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE` on layers with alpha, `CLEARTYPE` on opaque layers, in `Painter::draw_text`. Done when: `Ui.Text.AntialiasModeByLayer` reads the mode from a test hook for both layer kinds.
- [ ] Add a text layout cache keyed on (text, style, width, scale) with LRU eviction of 512 entries. Done when: `Ui.Text.LayoutCacheHit` shows one `CreateTextLayout` for 100 renders of unchanged text.
- [ ] Commit: `ui: add DirectWrite type ramp, TextBlock, ellipsis trimming and highlighted ranges`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Text.*` passes (ramp values, optical axis, wrap, ellipsis, highlight ranges, antialiasing mode, cache).

## 11. Icon, SVG and bitmap image elements

`ui::Icon` draws one glyph of the private font `resources/fonts/RectoIcons.ttf` (built by `D02 T01 §18`, loaded as a private DirectWrite font set through `IDWriteFactory5::CreateFontSetBuilder` and `IDWriteInMemoryFontFileLoader`), looked up by SPEC section 6 concept name at 16, 20 or 24 epx, Regular or Filled, coloured by state (design spec section 4.7). Built-in control glyphs (caption buttons, chevrons, check marks, spinner arrows, scrollbar arrows) use Segoe Fluent Icons as WinUI's stock controls do, which keeps SPEC section 6's "stock controls keep their system glyphs" rule true. `ui::SvgImage` renders `resources/svg/` files through `ID2D1DeviceContext5::CreateSvgDocument`, with a Light and Dark source switched by theme. `ui::BitmapImage` draws BGRA buffers (Home covers, thumbnails, previews) with an explicit stride, never assuming `width * 4`. Files: `src/ui/icon.h`, `icon.cpp`, `icon_font.cpp`, `svg_image.cpp`, `bitmap_image.cpp`.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 6 (icon set, sizes, Regular and Filled, no glyph inside a filled or tinted tile) and section 3.4 (in-app graphics) with `design/screenshots/D-Brand.png` (icon sheet) and `D-Home.png` (drop illustration, covers).
**Job:** The user sees the exact Recto glyphs at 16, 20 and 24 epx in the right state colour, crisp brand SVGs in both themes, and undistorted thumbnails. Consumer: every control with a glyph and every Home or Organize thumbnail.
**Treatment:** Private font set by concept name, Segoe Fluent Icons for stock glyphs, native D2D SVG, strided bitmaps. Cheaper substitute that fails the checkpoint: PNG exports of icons, hard-coded code points in controls, rasterising SVG through a third-party library, or `width * 4` strides that skew padded buffers.
**Chrome:** consumes `resources/fonts/RectoIcons.ttf` and its `RectoIcons.map.json` code point map (D02 T01 §18), `resources/svg/`, and §8 tokens for state colours; no glyph inside a filled or tinted tile.

**Build order:**

1. Implement the private font-set loader and concept-name lookup in `src/ui/icon_font.cpp`. Done when: `Ui.Image.IconLookupByName` finds every concept in the map and refuses an unknown name with a typed error.
2. Implement `Icon` drawing, sizes and states. Done when: `Ui.Image.IconRegularFilled` renders both variants at 16, 20 and 24 epx into different pixels.
3. Implement `SvgImage` and `BitmapImage`. Done when: `Ui.Image.SvgThemeSwitch` and `Ui.Image.BitmapStride` pass.

- [ ] Load `resources/fonts/RectoIcons.ttf` as a private DirectWrite font set (memory loader, no system install) in `src/ui/icon_font.cpp`. Done when: `Ui.Image.PrivateFontLoads` resolves the family without registering it system-wide. Source: Microsoft Learn `IDWriteFontSetBuilder1`.
- [ ] Load the concept-name to code point map from the `D02 T01 §18` output and expose `Icon::from_concept(name, size, filled)`; an unknown name returns an error, never a blank. Done when: `Ui.Image.IconLookupByName` passes for all names and `Ui.Image.UnknownIconIsError` passes. Cheaper substitute: falling back to a question-mark glyph silently.
- [ ] Implement `ui::Icon` (16, 20, 24 epx; Regular, Filled; colour from state tokens; `AutomationProperties` left to the owner) in `src/ui/icon.cpp`. Done when: `Ui.Image.IconRegularFilled` passes and the glyph is centred on the pixel grid.
- [ ] Add `ui::SystemGlyph` for Segoe Fluent Icons (caption buttons, chevrons, check mark, spinner arrows, scrollbar arrows) with the font fallback chain "Segoe Fluent Icons", "Segoe MDL2 Assets". Done when: `Ui.Image.SystemGlyphResolves` passes. Source: SPEC section 6.
- [ ] Implement `ui::SvgImage` (`ID2D1DeviceContext5::CreateSvgDocument`, `IStream` from file, viewport sizing, Light and Dark source pair chosen by `ThemeManager`) in `src/ui/svg_image.cpp`. Done when: `Ui.Image.SvgThemeSwitch` renders `wordmark-light.svg` in Light and `wordmark-dark.svg` in Dark through the harness. Source: Microsoft Learn `ID2D1DeviceContext5::CreateSvgDocument`.
- [ ] Implement `ui::BitmapImage` from a BGRA span with explicit `stride_bytes`, premultiplied-alpha conversion and `ID2D1Bitmap1` upload, with `Stretch` modes. Done when: `Ui.Image.BitmapStride` renders a 10 x 4 bitmap with a 64-byte stride and every pixel matches. Cheaper substitute: copying rows assuming `width * 4`.
- [ ] Recreate device-dependent bitmaps and SVG documents through §5's `DeviceDependent`. Done when: `Ui.Image.SurvivesDeviceLoss` passes after a simulated device removal.
- [ ] Commit: `ui: add Icon, SvgImage and BitmapImage elements`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Image.*` passes (private font load, concept lookup, Regular and Filled at three sizes, SVG theme switch, padded-stride bitmap, device-loss rebuild). It requires `resources/fonts/RectoIcons.ttf` from `D02 T01 §18`.

## 12. Pointer and keyboard input: routing, capture, wheel, accelerators, access keys

Input arrives through `WM_POINTER*` with `EnableMouseInPointer(TRUE)` so mouse, touch and pen share one path (design spec section 4.8). `ui::Input` routes by hit test with capture, enter, leave and bubbling; handles wheel, `Ctrl+wheel` (zoom) and horizontal wheel using the system wheel-lines and character settings (`SPI_GETWHEELSCROLLLINES`, `SPI_GETWHEELSCROLLCHARS`) and double-click time and size (`GetDoubleClickTime`, `SM_CXDOUBLECLK`); routes `WM_KEYDOWN`, `WM_SYSKEYDOWN`, `WM_CHAR` and `WM_KEYUP` to the focused element and up the tree; maps SPEC section 10 shortcuts through a window accelerator table to `ui::Command` (§16); and shows access keys on `Alt`. DirectManipulation pan and pinch are delivered by `ScrollViewer` in D10 T02 §10, not here. Files: `src/ui/input.h`, `input.cpp`, `pointer_router.cpp`, `keyboard_router.cpp`, `accelerators.cpp`, `access_keys.cpp`.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Implement pointer routing (down, up, move, enter, leave, capture, cancel) in `src/ui/pointer_router.cpp`. Done when: `Ui.Input.EnterLeaveAndCapture` passes with synthetic pointer events.
2. Implement wheel and click-count handling. Done when: `Ui.Input.WheelUsesSystemLines` passes with `SPI` values injected.
3. Implement keyboard routing, accelerators and access keys, and `tests/recto-tests/ui_input_tests.cpp`. Done when: `Ui.Input.*` passes on the display-session host.

- [ ] Call `EnableMouseInPointer(TRUE)` in `ui::Application::run` and handle `WM_POINTERDOWN`, `WM_POINTERUPDATE`, `WM_POINTERUP`, `WM_POINTERENTER`, `WM_POINTERLEAVE`, `WM_POINTERCAPTURECHANGED` in `src/ui/pointer_router.cpp`. Done when: `Ui.Input.PointerSequence` receives down, move, up in order for a synthetic mouse path. Source: Microsoft Learn "Pointer input messages".
- [ ] Implement capture (`Element::capture_pointer`, release on up or cancel) and enter/leave computed from hit tests. Done when: `Ui.Input.CaptureOutsideBounds` keeps delivering moves after the pointer leaves the element.
- [ ] Carry device kind and pen pressure in `PointerEvent` via `GetPointerInfo`/`GetPointerPenInfo`. Done when: `Ui.Input.PenPressureReported` passes with an injected pen pointer (`InjectSyntheticPointerInput`). Cheaper substitute: treating pen as mouse and dropping pressure.
- [ ] Implement wheel, `Ctrl+wheel` and horizontal wheel with `WM_POINTERWHEEL`/`WM_POINTERHWHEEL`, scaling by `SPI_GETWHEELSCROLLLINES` and `SPI_GETWHEELSCROLLCHARS`. Done when: `Ui.Input.WheelUsesSystemLines` yields 3-line and 6-line deltas and `Ui.Input.CtrlWheelIsZoom` marks the event as zoom.
- [ ] Compute click count from `GetDoubleClickTime` and `SM_CXDOUBLECLK`/`SM_CYDOUBLECLK`. Done when: `Ui.Input.DoubleClickSettings` counts a double at the configured time and a single beyond it.
- [ ] Route keyboard events to the focused element then bubble to ancestors (`KeyEvent`, `CharEvent`, handled flag) in `src/ui/keyboard_router.cpp`. Done when: `Ui.Input.KeyBubbles` passes and a handled event stops bubbling.
- [ ] Implement the accelerator table (`AcceleratorTable::bind(Key, Modifiers, Command*)`) evaluated before element delivery for global shortcuts and after for element-local ones, loaded from the SPEC section 10 list by `src/app`. Done when: `Ui.Input.AcceleratorInvokesCommand` runs a bound `Command` on `Ctrl+K` and refuses a disabled command.
- [ ] Implement access keys: `Alt` press-release shows key tips on elements with an `access_key`, `Alt+letter` invokes. Done when: `Ui.Input.AccessKeysShowAndInvoke` passes. Source: SPEC sections 8.4, 11.
- [ ] Add `Element::cursor()` (a `ui::Cursor` enum: `Arrow`, `IBeam`, `Hand`, `SizeWE`, `SizeNS`, `SizeAll`, `NotAllowed`, `Wait`, `AppStarting`) and answer `WM_SETCURSOR` in `src/ui/cursor.cpp` by walking up from the hit-tested element to the first one that sets a cursor, loading system cursors with `LoadCursorW(nullptr, IDC_*)`, falling back to `Arrow`, and showing `Wait` for the window while a modal `Dialog` operation declares itself busy. Done when: `Ui.Input.CursorFollowsElement` hit-tests a `TextBox`, a `Hyperlink`, a `Splitter` edge and plain text and reads `GetCursor()` as `IBeam`, `Hand`, `SizeWE` and `Arrow`, and `Ui.Input.CursorHiddenForTouchAndPen` shows no cursor change for touch and pen pointers. Source: Microsoft Learn "WM_SETCURSOR message". Cheaper substitute: each control calling `SetCursor` on `WM_MOUSEMOVE`, which flickers and breaks under capture.
- [ ] Commit: `ui: add pointer, wheel and keyboard routing, accelerators, access keys and cursors`

> **Groomed 2026-10-23:** only the `Splitter` (`D10 T02 §26`) set a cursor and no section owned `WM_SETCURSOR`, so text fields, links and the page canvas would all show the arrow. The cursor micro-step above gives every control one place to declare its cursor; `D10 T02 §4`, `§2` and `§26` set `Element::cursor()` instead of calling `SetCursor`.

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Input.*` passes on the display-session host (pointer sequence, capture, pen pressure, system wheel lines, double-click settings, key bubbling, accelerators, access keys, cursor resolution).

## 13. Focus manager: tab order, focus scopes, arrow groups and the Fluent focus visual

`ui::FocusManager` owns keyboard focus (design spec section 4.8): tab order follows the logical tree with explicit tab stops and focus scopes (dialogs and popups trap focus and return it on close), arrow-key groups for toolbars, lists and segmented controls, and the Fluent focus visual drawn only for keyboard focus: a 2 epx outer ring in `RectoAccentBrush` and a 1 epx inner ring in the surface colour (SPEC section 11 "stock focus visual in the accent colour"). Pointer focus changes never show the ring until a key press (WinUI `FocusVisualKind` semantics). Files: `src/ui/focus_manager.h`, `focus_manager.cpp`, `focus_visual.cpp`.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 11 (focus visual, logical order) and section 4 (accent) with `design/screenshots/D-Focus-Light.png` and `D-Focus-Dark.png` (keyboard focus ring on a toolbar button).
**Job:** The keyboard-only user tabs through the shell in the SPEC order, sees exactly where focus is, and returns to where they were after a dialog or flyout closes; the mouse user never sees stray rings. Consumer: every interactive control and `Dialog` in D10 T02.
**Treatment:** One `FocusManager` with scopes, groups and a drawn two-ring visual. Cheaper substitute that fails the checkpoint: relying on `Tab` handled per control, a 1 px dotted rectangle, or showing the ring after mouse clicks.
**Chrome:** consumes `RectoAccentBrush`, `RectoSurfaceBrush`, control and overlay radii from §8 and `Painter` from §9; produces focus APIs used by D10 T02 §1.

**Build order:**

1. Implement focus state, `move_focus(direction)` and tab order in `src/ui/focus_manager.cpp`. Done when: `Ui.Focus.TabOrderLogicalTree` walks a fixture in order and wraps.
2. Implement scopes (trap and restore) and arrow groups. Done when: `Ui.Focus.ScopeTrapsAndRestores` and `ArrowGroupMovesWithinGroup` pass.
3. Implement the focus visual and keyboard-only visibility. Done when: `Ui.Focus.VisualOnlyAfterKeyboard` renders no ring after a click and a ring after `Tab`.

- [ ] Add `FocusManager` (`focus`, `focused`, `move_next`, `move_previous`, `on_focus_changed`) with `Element::focusable`, `tab_index` and `tab_stop`. Done when: `Ui.Focus.TabOrderLogicalTree` passes with explicit tab indexes overriding tree order.
- [ ] Add focus scopes: `FocusScope` element traps `Tab` within itself and stores the previously focused element for restore on close. Done when: `Ui.Focus.ScopeTrapsAndRestores` passes with a nested scope.
- [ ] Add arrow groups: `FocusGroup` makes `Left/Right/Up/Down` move among members while `Tab` leaves the group, remembering the last member. Done when: `Ui.Focus.ArrowGroupMovesWithinGroup` passes for a toolbar and a vertical list.
- [ ] Skip invisible, disabled and `tab_stop == false` elements. Done when: `Ui.Focus.SkipsDisabledAndHidden` passes.
- [ ] Track the input modality: `FocusManager::keyboard_modality` becomes true on key events and false on pointer events. Done when: `Ui.Focus.VisualOnlyAfterKeyboard` passes. Source: WinUI `FocusVisualKind` behaviour as quoted in design spec section 4.8.
- [ ] Draw the focus visual in `src/ui/focus_visual.cpp`: 2 epx outer ring in `RectoAccentBrush`, 1 epx inner ring in the surface colour, following the focused element's radius, snapped to device pixels. Done when: `Ui.Focus.VisualRendersTwoRings` matches a reference crop at SSIM >= 0.98 at 100 and 200 percent. Cheaper substitute: a single 2 px ring with no inner contrast ring.
- [ ] Draw the high-contrast focus visual in `COLOR_HIGHLIGHT` and `COLOR_WINDOWTEXT` under a forced contrast theme. Done when: `Ui.Focus.HighContrastVisual` passes.
- [ ] Handle `WM_SETFOCUS`/`WM_KILLFOCUS` on the window so focus restores when the window reactivates. Done when: `Ui.Focus.WindowReactivationRestores` passes.
- [ ] Commit: `ui: add FocusManager with scopes, arrow groups and the Fluent focus visual`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Focus.*` passes (tab order, scopes, arrow groups, keyboard-only visual at 100 and 200 percent, high contrast).

## 14. Motion: frame clock, Fluent easing, panel motion and the Windows animation setting

`ui::Motion` drives opacity, translation and scale animations on composition visuals through DirectComposition animation objects (`IDCompositionAnimation`, `IDCompositionEffectGroup`, `IDCompositionTranslateTransform`) so they run off the UI thread (design spec section 4.8). Timings follow SPEC section 12: panels and cards fade and translate 8 epx over 167 ms with the standard Fluent easing (`cubic-bezier(0, 0, 0, 1)` decelerate for enter, accelerate for exit); dialogs use the ContentDialog open and close curve (scale 1.05 to 1 with fade over 250 ms in, 167 ms out) from §15's overlay layer. `SPI_GETCLIENTAREAANIMATION` off (or `ThemeManager::animations_enabled() == false`) means no motion and no toolbar fade: every transition snaps to its end state. Files: `src/ui/motion.h`, `motion.cpp`, `easing.cpp`, `frame_clock.cpp`.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 11 ("Show animations in Windows") and 12 (motion) with `design/screenshots/D-Focus-Light.png` (floating toolbar and panels that animate) and `D-Dialogs.png` (dialog).
**Job:** The user sees panels and cards ease in with the SPEC timing, and sees no motion at all when Windows animations are off. Consumer: floating toolbar, side panels, cards, dialogs, toasts, and the command box.
**Treatment:** DirectComposition animations with the Fluent curves and a reduced-motion switch. Cheaper substitute that fails the checkpoint: UI-thread timer-driven redraws, linear easing, or animating even when `SPI_GETCLIENTAREAANIMATION` is off.
**Chrome:** consumes motion constants and easing from §8 and `Layer` visuals from §5/§6; produces `Motion::fade_translate_in/out` used by D10 T02 §16, §17, §20.

**Build order:**

1. Implement the easing functions and DComp animation builders in `src/ui/easing.cpp` and `motion.cpp`. Done when: `Ui.Motion.EasingSamples` matches the Fluent curve at 0, 25, 50, 75, 100 percent within 0.01.
2. Implement `Motion::fade_translate_in/out` on a `Layer`. Done when: `Ui.Motion.FadeTranslate167` observes the animation duration of 167 ms and the 8 epx offset.
3. Implement the reduced-motion path and the frame clock. Done when: `Ui.Motion.NoMotionWhenDisabled` ends at the final state with zero animation objects.

- [ ] Implement `Easing::fluent_standard` as a cubic-bezier sampled into an `IDCompositionAnimation` segment list in `src/ui/easing.cpp`. Done when: `Ui.Motion.EasingSamples` passes. Source: Fluent motion "standard easing" as quoted in SPEC section 12.
- [ ] Implement `Motion::fade_translate_in(Element&)` and `fade_translate_out`: opacity 0 to 1 and translate 8 epx to 0 over 167 ms using `IDCompositionVisual3::SetOpacity` and `SetOffsetY` animations. Done when: `Ui.Motion.FadeTranslate167` passes. Source: SPEC section 12.
- [ ] Implement the dialog open/close curve (scale 1.05 to 1, fade, 250 ms in and 167 ms out) as `Motion::dialog_in/out`. Done when: `Ui.Motion.DialogCurve` passes for both directions. Cheaper substitute: reusing the panel curve for dialogs.
- [ ] Read `SPI_GETCLIENTAREAANIMATION` through `ThemeManager::animations_enabled()` and make every `Motion::*` call set the end state immediately when false. Done when: `Ui.Motion.NoMotionWhenDisabled` passes and `Ui.Motion.SettingChangeLive` flips it on `WM_SETTINGCHANGE`. Source: SPEC section 11.
- [ ] Add a frame clock (`FrameClock::on_tick`) using `IDCompositionDevice3`/`DCompositionWaitForCompositorClock` for UI-thread animations that cannot run in composition (rare, documented). Done when: `Ui.Motion.FrameClockTicks` observes ticks at the display refresh cadence within 20 percent.
- [ ] Complete animations safely: cancelling mid-flight leaves the layer at a defined state and never leaves opacity 0 elements hit-testable. Done when: `Ui.Motion.CancelLeavesDefinedState` passes.
- [ ] Add `Motion::zoom_scale(Layer&, from, to)` for the page canvas tile scale (SPEC 12 "animated scale of existing tiles"). Done when: `Ui.Motion.ZoomScaleAnimates` passes.
- [ ] Commit: `ui: add Motion with Fluent easing, panel and dialog curves and the animation setting`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Motion.*` passes on the display-session host (easing samples, 167 ms and 8 epx, dialog curve, no motion when the setting is off, cancel safety).

## 15. Popup host: owned popup windows, placement, light dismiss, in-window overlay layer

Menus, tooltips, combo box lists, suggestion lists and anchored flyouts can extend past the window, so they are owned `WS_POPUP` windows with their own composition target (`WS_EX_NOREDIRECTIONBITMAP`, `WS_EX_NOACTIVATE`, `WS_EX_TOOLWINDOW`), placed inside the monitor work area (`MonitorFromRect`, `GetMonitorInfoW`), dismissed by outside click, `Esc` or window deactivation, and returning focus to their owner (design spec section 4.9). Dialogs, the command box, floating side panels, the queue card, toasts and the drop overlay stay inside the window on the root overlay layer; dialogs add the smoke layer and trap focus via §13. Files: `src/ui/popup_host.h`, `popup_host.cpp`, `popup_placement.cpp`, `overlay_layer.cpp`.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 7 (overlay radius 8, elevation Z 32 on menus, Z 64 on dialogs and the command box), 9.4 (command box top centre), 9.9 (dialog smoke) with `design/screenshots/D-Dialogs.png`.
**Job:** The user opens a menu or combo list that extends past the window edge, dismisses it with a click outside, `Esc` or Alt+Tab, and keyboard focus returns to the control that opened it; dialogs dim the window and trap focus. Consumer: D10 T02 `Menu`, `ComboBox`, `ToolTip`, `Flyout`, `Dialog`, `SearchBox`.
**Treatment:** Owned popup windows for anything that may leave the window, an in-window overlay layer for the rest, with real light dismiss. Cheaper substitute that fails the checkpoint: drawing all popups clipped inside the main window, dismiss only on `Esc`, or losing focus restoration.
**Chrome:** consumes §5 compositor, §9 elevation, §13 focus scopes and §14 motion; produces `PopupHost` and the root overlay layer for D10 T02.

**Build order:**

1. Implement `PopupHost::show(Element& content, Placement)` creating the owned popup window with its own composition target. Done when: `Ui.Popup.ShowsOwnedWindow` finds a popup HWND whose owner is the main window.
2. Implement work-area placement. Done when: `Ui.Popup.PlacementFlipsAndClamps` keeps the popup inside the monitor work area for each edge.
3. Implement light dismiss, focus return and the in-window overlay layer with smoke. Done when: `Ui.Popup.LightDismiss*` and `Ui.Popup.DialogSmokeTrapsFocus` pass.

- [ ] Add `PopupHost` with `show(content, anchor_rect, placement)`, `close`, `on_closed`, creating a `WS_POPUP` window with `WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW` and a `Compositor` target in `src/ui/popup_host.cpp`. Done when: `Ui.Popup.ShowsOwnedWindow` passes.
- [ ] Implement placement (bottom-start, top-start, right, left, flip when no room, clamp to the work area, DPI of the target monitor) in `src/ui/popup_placement.cpp`. Done when: `Ui.Popup.PlacementFlipsAndClamps` passes on the primary monitor at 100 and 150 percent with a simulated work area. Source: Microsoft Learn `MonitorFromRect`, `GetMonitorInfo`.
- [ ] Implement light dismiss: outside pointer down (using `WH_MOUSE`-free capture on the popup chain), `Esc`, owner deactivation (`WM_ACTIVATEAPP` false), and owner move or resize. Done when: `Ui.Popup.LightDismissOutsideClick`, `LightDismissEsc` and `LightDismissDeactivate` pass. Cheaper substitute: closing only on `Esc`.
- [ ] Return keyboard focus to the owning element on close. Done when: `Ui.Popup.FocusReturnsToOwner` passes.
- [ ] Support nested popups (submenus) closing together from the root. Done when: `Ui.Popup.NestedClosesWithRoot` passes.
- [ ] Implement the root overlay layer in `src/ui/overlay_layer.cpp` for in-window overlays with z-order, and the smoke layer (`RectoSmokeBrush`, fade 167 ms via §14) for dialogs. Done when: `Ui.Popup.DialogSmokeTrapsFocus` shows pointer blocked beneath and `Tab` trapped.
- [ ] Apply the §9 elevation (Z 32 for menus and flyouts, Z 64 for dialogs and the command box) and overlay radius to popup content. Done when: `Ui.Popup.ElevationMatchesSpec` matches reference crops. Source: SPEC section 7.
- [ ] Handle device loss and DPI change for popup windows. Done when: `Ui.Popup.SurvivesDpiChange` passes.
- [ ] Commit: `ui: add PopupHost, work-area placement, light dismiss and in-window overlay layer`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Popup.*` passes on the display-session host (owned window, flip and clamp, three dismiss paths, focus return, nested close, smoke and focus trap).

## 16. Observable properties, `Command` objects and view-model binding helpers

`ui::Property<T>` holds an observable value with change callbacks and `ui::Command` holds execute and can-execute with a changed event (design spec section 4.5); this is the whole binding story, replacing `x:Bind`, `INotifyPropertyChanged` and `ICommand`. View models in `src/app` expose properties and commands and controls subscribe. Binding helpers `bind(element_property, view_property)` (one-way and two-way) marshal changes to the UI thread through the §3 dispatcher when a property changes off-thread. Subscriptions are RAII `Subscription` tokens so a destroyed element never receives a callback. Files: `src/ui/property.h`, `command.h`, `binding.h`, `subscription.h`.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `Property<T>` and `Subscription` in `src/ui/property.h`. Done when: `Ui.Binding.PropertyNotifiesOnChange` fires once per change and not for equal values.
2. Implement `Command` and `bind` helpers. Done when: `Ui.Binding.CommandCanExecute` disables a bound element when `can_execute` flips.
3. Implement cross-thread marshalling. Done when: `Ui.Binding.OffThreadSetMarshals` runs the subscriber on the UI thread.

- [ ] Add `ui::Property<T>` with `get`, `set`, `subscribe` returning `Subscription`, equality suppression via `operator==`. Done when: `Ui.Binding.PropertyNotifiesOnChange` passes. Source: design spec section 4.5.
- [ ] Add `Subscription` (RAII, move-only, safe to destroy during notification). Done when: `Ui.Binding.UnsubscribeDuringNotify` passes under AddressSanitizer.
- [ ] Add `ui::Command` (`execute`, `can_execute`, `notify_can_execute_changed`, `on_can_execute_changed`, name, accelerator text) in `src/ui/command.h`. Done when: `Ui.Binding.CommandCanExecute` passes; the command carries the name the command box (Ctrl+K) lists.
- [ ] Add `bind_one_way(Property<T>&, Element&, setter)` and `bind_two_way` in `src/ui/binding.h`. Done when: `Ui.Binding.TwoWayDoesNotLoop` shows a two-way binding settling after one round.
- [ ] Marshal off-thread `Property::set` to the UI thread through `Dispatcher` in `Property<T>::set_from_any_thread`. Done when: `Ui.Binding.OffThreadSetMarshals` records the subscriber thread id equals the UI thread. Cheaper substitute: letting subscribers run on the worker thread.
- [ ] Add a `CommandRegistry` (id to `Command`) that §12 accelerators and the command box in D02 T03 enumerate. Done when: `Ui.Binding.RegistryListsAllCommands` passes.
- [ ] Commit: `ui: add observable Property, Command and binding helpers`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Binding.*` passes (change notification, unsubscribe safety, can-execute, two-way settle, off-thread marshalling), also under `EnableASAN=true`.

## 17. OLE drag-and-drop target and clipboard formats

`ui::Window` registers an OLE `IDropTarget` (`RegisterDragDrop` after `OleInitialize`) that accepts `CF_HDROP` and shell item data (`CFSTR_SHELLIDLIST`, `IShellItemArray` from `IDataObject`), raises drag enter, over and leave events for the full-window drop overlay (SPEC 9.1, built by `D02 T01 §6`), and delivers the dropped paths to a handler on the UI thread. `ui::Clipboard` reads and writes `CF_UNICODETEXT`, registered `HTML Format` (CF_HTML with the `StartHTML`/`EndHTML` header) and `Rich Text Format`, with delayed rendering where cheap, on the UI thread through `OleSetClipboard`. Files: `src/ui/drop_target.h`, `drop_target.cpp`, `clipboard.h`, `clipboard.cpp`.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the `IDropTarget` COM object and registration in `src/ui/drop_target.cpp`. Done when: `Ui.Drop.RegistersAndAcceptsHdrop` drives the target with a synthetic `IDataObject`.
2. Implement shell item enumeration and effect negotiation. Done when: `Ui.Drop.RejectsNonFiles` returns `DROPEFFECT_NONE` for text-only data.
3. Implement `Clipboard`. Done when: `Ui.Drop.ClipboardRoundTrip` round-trips text, HTML and RTF.

- [ ] Add `DropTarget : IDropTarget` with `DragEnter`, `DragOver`, `DragLeave`, `Drop` forwarding to `ui::Window` events `on_drag_enter(DragInfo)`, `on_drag_leave`, `on_drop(paths)`. Done when: `Ui.Drop.EnterLeaveEvents` observes enter then leave. Source: Microsoft Learn `IDropTarget`.
- [ ] Accept `CF_HDROP` and `CFSTR_SHELLIDLIST`; resolve to filesystem paths via `SHCreateShellItemArrayFromDataObject` and `SIGDN_FILESYSPATH`, ignoring items without a path. Done when: `Ui.Drop.RegistersAndAcceptsHdrop` returns the dropped paths and `Ui.Drop.VirtualItemIgnored` skips a virtual item.
- [ ] Negotiate `DROPEFFECT_COPY` for files and `DROPEFFECT_NONE` otherwise, honouring modifier keys. Done when: `Ui.Drop.RejectsNonFiles` passes. Cheaper substitute: accepting everything and filtering later.
- [ ] Keep drop handling off long work: deliver paths via `Dispatcher::post` and return from `Drop` immediately. Done when: `Ui.Drop.DropReturnsFast` asserts `Drop` completes in under 50 ms for 1,000 paths.
- [ ] Implement `ui::Clipboard::set_text/get_text` (`CF_UNICODETEXT`), `set_html/get_html` (registered `HTML Format` with a correct CF_HTML header and fragment offsets), `set_rtf/get_rtf` (registered `Rich Text Format`). Done when: `Ui.Drop.ClipboardRoundTrip` passes through `OleSetClipboard`/`OleGetClipboard`. Source: Microsoft Learn "HTML Clipboard Format".
- [ ] Retry on `CLIPBRD_E_CANT_OPEN` with bounded backoff (5 tries, 20 ms) and return a typed error after. Done when: `Ui.Drop.ClipboardBusyRetries` passes with a contending clipboard holder.
- [ ] Commit: `ui: add OLE drop target and clipboard text, HTML and RTF formats`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Drop.*` passes (drag events, `CF_HDROP` and shell items, rejection, fast return, clipboard round trip, busy retry).

## 18. `SwapChainHost`: composition swap chain element for the page canvas

`ui::SwapChainHost` is the element the page canvas lives in (plan section 4.6, design spec sections 4.1 and 4.2). It creates the swap chain with `IDXGIFactory2::CreateSwapChainForComposition` (`DXGI_FORMAT_B8G8R8A8_UNORM`, `DXGI_SCALING_STRETCH`, premultiplied alpha, flip-sequential or flip-discard), sets it as the content of its `IDCompositionVisual2` (`SetContent`), handles resize (`ResizeBuffers` coordinated with the presentation thread), and hands the swap chain and the shared `ID3D11Device` to the `recto-services` presentation thread through plain COM interfaces (`IDXGISwapChain1*`, `ID3D11Device*`) so `recto-services` has no `recto-ui` include. The UI thread only moves and clips the host visual. Device loss re-issues the swap chain through §5's `DeviceDependent`. Files: `src/ui/swap_chain_host.h`, `swap_chain_host.cpp`.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement swap chain creation and visual binding in `src/ui/swap_chain_host.cpp`. Done when: `Ui.SwapChain.Creates` produces a composition swap chain whose `GetDesc1` matches.
2. Implement the hand-off interface and resize protocol. Done when: `Ui.SwapChain.ResizeProtocol` shows the presentation thread paused, buffers resized, thread resumed.
3. Implement device-loss re-issue and tests in `tests/recto-tests/ui_swapchain_tests.cpp`. Done when: `Ui.SwapChain.*` passes.

- [ ] Add `SwapChainHost : Element` with `create(GraphicsDevice&, size_px)` calling `IDXGIFactory2::CreateSwapChainForComposition` and `IDCompositionVisual2::SetContent`. Done when: `Ui.SwapChain.Creates` passes. Source: Microsoft Learn `CreateSwapChainForComposition`.
- [ ] Expose `PresentationTarget` as a plain COM-only struct `{ IDXGISwapChain1* swap_chain; ID3D11Device* device; }` obtained through `acquire_target()`, with no `ui::` types in the signature. Done when: `Ui.SwapChain.TargetIsPlainCom` compiles a translation unit that includes only the header and `<dxgi1_2.h>`.
- [ ] Implement the resize protocol: UI thread calls `request_resize(w, h)`; the presentation thread releases back-buffer references at its next frame boundary and calls `ack_resize`; the host then calls `ResizeBuffers`. Done when: `Ui.SwapChain.ResizeProtocol` passes with a simulated presentation thread and no `DXGI_ERROR_INVALID_CALL`.
- [ ] Keep the UI thread to visual transforms only: `set_offset`, `set_clip`, never `Present`. Done when: `Ui.SwapChain.UiThreadNeverPresents` asserts the host exposes no present method. Cheaper substitute: presenting from the UI thread for convenience.
- [ ] Re-issue the swap chain on device loss, notify the presentation thread through `on_target_replaced`. Done when: `Ui.SwapChain.DeviceLossReissues` passes with §5's test hook.
- [ ] Support DPI and layout changes by resizing to `size_epx * scale` and keeping the visual scale at 1. Done when: `Ui.SwapChain.ScaleChange` passes at 150 percent.
- [ ] Add an include-boundary assertion that `src/services` does not include `src/ui`. Done when: `Ui.Project.IncludeBoundary` still passes with the hand-off in place.
- [ ] Commit: `ui: add SwapChainHost composition swap chain element with presentation hand-off`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.SwapChain.*` passes (creation, plain-COM hand-off, resize protocol, no UI-thread present, device-loss re-issue, scale change) and `Ui.Project.IncludeBoundary` passes.

## 19. Non-client hit testing: drag regions, resize edges, caption buttons, Snap Layouts

`ui::Window` answers `WM_NCHITTEST` so the custom title bar behaves like a native one (design spec section 4.3): empty title bar space returns `HTCAPTION`, interactive elements return `HTCLIENT`, window edges return the resize codes (`HTLEFT`, `HTTOPLEFT` and so on, with the top edge inset handled for the extended frame), and the three caption buttons return `HTMINBUTTON`, `HTMAXBUTTON` and `HTCLOSE` so Windows 11 shows the Snap Layouts flyout when hovering maximize. Caption buttons are drawn by `recto-ui` with Segoe Fluent Icons at Windows 11 metrics (46 x 32 epx at rest), with hover, pressed and close-red states; double-click on `HTCAPTION` maximizes or restores; `Alt+Space` opens the system menu (`GetSystemMenu`, `TrackPopupMenu`); a deactivated window dims the title bar chrome. Drag regions come from elements marked `title_bar_interactive` (the shell registers its tabs, buttons and command box). Files: `src/ui/non_client.h`, `non_client.cpp`, `caption_buttons.cpp`, `system_menu.cpp`.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.1 (title bar draggable everywhere except interactive elements, caption buttons) and 11 with `design/screenshots/D-Focus-Light.png` and `D-Focus-Dark.png` title bar and caption buttons.
**Job:** The user drags the window by empty title bar space, double-clicks to maximize, resizes from every edge, sees the Snap Layouts flyout on hovering maximize, uses the system menu with `Alt+Space`, and sees the chrome dim when Recto is in the background. Consumer: the shell title bar of `D02 T01 §2`.
**Treatment:** `WM_NCHITTEST` with real non-client codes and drawn Windows 11 caption buttons. Cheaper substitute that fails the checkpoint: client-side buttons returning `HTCLIENT` (no Snap Layouts), a transparent drag child window, or handling drag by capturing the mouse manually.
**Chrome:** consumes §9 painter, §10 and §11 system glyphs (Segoe Fluent Icons `ChromeMinimize`, `ChromeMaximize`, `ChromeRestore`, `ChromeClose`), §8 tokens (`RectoTextBrush`, `RectoTextSecondaryBrush`, close hover `#C42B1C` taken from the token file, not a literal in code) and §12 pointer routing.

**Build order:**

1. Implement the hit-test function (pure, testable) in `src/ui/non_client.cpp` returning the right code for a point. Done when: `Ui.NonClient.HitCodes` passes for caption, client, eight resize edges and three buttons at 100 and 200 percent.
2. Implement the caption button elements and their states. Done when: `Ui.NonClient.CaptionButtonsRender` matches reference crops at SSIM >= 0.97.
3. Implement double-click, system menu and deactivated dimming with Snap Layouts verified live. Done when: `Ui.NonClient.*` passes on the display-session host and the flyout appears on hover (recorded in the stamp).

- [ ] Implement `Window::hit_test_non_client(point_px)` combining the resize-border width (`GetSystemMetricsForDpi(SM_CXSIZEFRAME)` plus padded border), the caption-button rects and `title_bar_interactive` regions, in `src/ui/non_client.cpp`. Done when: `Ui.NonClient.HitCodes` passes. Source: Microsoft Learn "WM_NCHITTEST".
- [ ] Handle `WM_NCHITTEST` by calling DWM first (`DwmDefWindowProc`) then the function above, returning `HTMINBUTTON`, `HTMAXBUTTON`, `HTCLOSE` over the drawn buttons. Done when: `Ui.NonClient.MaxButtonReturnsHtMaxButton` passes. Cheaper substitute: returning `HTCLIENT` over the buttons, which breaks Snap Layouts.
- [ ] Draw caption buttons 46 x 32 epx with Segoe Fluent Icons glyphs (`E921`, `E922`, `E923`, `E8BB`) at 10 epx size, rest, hover, pressed and disabled-deactivated states, and the close button in red on hover, in `src/ui/caption_buttons.cpp`. Done when: `Ui.NonClient.CaptionButtonsRender` matches reference crops in Light and Dark. Source: Windows 11 title bar metrics; design spec section 4.3.
- [ ] Track button hover and pressed states from `WM_NCMOUSEMOVE`, `WM_NCLBUTTONDOWN`, `WM_NCLBUTTONUP` and `WM_NCMOUSELEAVE` and invoke minimize, maximize/restore and close on release (`SC_MINIMIZE`, `SC_MAXIMIZE`/`SC_RESTORE`, `SC_CLOSE`). Done when: `Ui.NonClient.ButtonStatesAndActions` passes.
- [ ] Implement double-click on `HTCAPTION` to maximize or restore (default `DefWindowProc` behaviour preserved) and drag-to-snap. Done when: `Ui.NonClient.DoubleClickMaximizes` passes on the display-session host.
- [ ] Implement `Alt+Space` and right-click on `HTCAPTION` opening the system menu with `GetSystemMenu` and `TrackPopupMenu` at the right place. Done when: `Ui.NonClient.AltSpaceOpensSystemMenu` sees `WM_SYSCOMMAND` from the menu. Source: Microsoft Learn `GetSystemMenu`.
- [ ] Dim the title bar lockup and inactive tab labels to `RectoTextSecondaryBrush` and the caption glyphs to the disabled token when `Window::is_active()` is false. Done when: `Ui.NonClient.DeactivatedDims` compares active and inactive renders.
- [ ] Add the live Snap Layouts check: hover the maximize button for 1 second and detect the Snap Layouts flyout window (`ApplicationFrameInputSinkWindow`/`SnapLayouts` class via `EnumWindows`). Done when: `Ui.NonClient.SnapLayoutsFlyoutAppears` passes on Windows 11 with a display session.
- [ ] Commit: `ui: add non-client hit testing, drawn caption buttons, Snap Layouts and system menu`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.NonClient.*` passes on the display-session host (hit codes at two scales, `HTMAXBUTTON`, caption button renders, double-click, system menu, deactivated dimming, Snap Layouts flyout detected).

## 20. Golden-image suite: headless renders at 100, 150 and 200 percent against SPEC boards

The golden suite renders toolkit fixtures headless through the §1 harness at 100, 150 and 200 percent in Light, Dark and HighContrast and compares them with crops of `design/screenshots/D-*.png` (design spec section 8: golden images per control and per board at three scales and three themes). Comparison uses `compute_ssim` from `tests/recto-tests/test_support.h` with per-crop thresholds in a hand-edited manifest `tests/recto-tests/golden/ui/manifest.json` (screenshot, crop rect at 100 percent, theme, scale, threshold). The screenshots are 100 percent boards, so 150 and 200 percent references are the same crop resampled by WIC Fant scaling with a lower threshold; HighContrast has no SPEC board, so its references are reviewed baselines in `tests/recto-tests/golden/ui/hc/`. A `--update` path writes candidate PNGs and a diff sheet to `build/golden-candidates/` for review and never overwrites `design/screenshots/` or the committed baselines. This section ships the harness and the foundation crops (window frame colours, title bar strip, tokens swatch, text ramp, focus ring, shadows); controls add their crops in D10 T02 §23 and boards in D06 T04 §1. Files: `tests/recto-tests/ui_golden.h`, `ui_golden.cpp`, `ui_golden_tests.cpp`, `tests/recto-tests/golden/ui/manifest.json`.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the golden loader (WIC PNG decode to BGRA with explicit stride, crop, Fant resample) in `tests/recto-tests/ui_golden.cpp`. Done when: `Ui.Golden.CropAndScale` returns the expected dimensions at 100, 150 and 200 percent.
2. Implement the manifest-driven comparison with `compute_ssim` and the diff report. Done when: `Ui.Golden.ThresholdFailsOnDrift` fails when a fixture colour is changed by 8 levels.
3. Implement `--update` candidate writing and the foundation fixtures. Done when: `recto-tests.exe --ui-golden-update` writes candidates under `build/golden-candidates/` and leaves `design/screenshots/` byte-identical (SHA-256 checked).

- [ ] Add `GoldenImage` (`load_png`, `crop`, `scale_fant`) in `tests/recto-tests/ui_golden.cpp` using `IWICBitmapDecoder` and `IWICBitmapScaler` (`WICBitmapInterpolationModeFant`), with explicit stride. Done when: `Ui.Golden.CropAndScale` passes. Source: Microsoft Learn `IWICBitmapScaler`.
- [ ] Define `tests/recto-tests/golden/ui/manifest.json` (entries: `id`, `screenshot`, `crop`, `theme`, `scale`, `ssim_min`, `fixture`) and a loader with schema validation. Done when: `Ui.Golden.ManifestValidates` rejects a missing field and an `ssim_min` outside (0, 1].
- [ ] Add foundation fixtures (`window_frame_light`, `titlebar_strip_dark`, `token_swatches`, `type_ramp`, `focus_ring`, `elevation_z32`, `elevation_z64`) built from `src/ui` primitives only. Done when: each fixture renders deterministically (`Ui.Golden.FixturesDeterministic` renders twice with identical SHA-256).
- [ ] Compare each fixture with its crop at 100, 150 and 200 percent in Light and Dark using `compute_ssim`, thresholds from the manifest (start 0.93 at 100, 0.90 at 150 and 200, recorded as defaults; tighten once measured). Done when: `Ui.Golden.Foundation` passes and each entry prints its measured SSIM.
- [ ] Add HighContrast baselines under `tests/recto-tests/golden/ui/hc/` for the same fixtures (Aquatic and Desert contrast themes via forced `ThemeManager` state) at three scales. Done when: `Ui.Golden.HighContrast` passes against the committed baselines.
- [ ] Prove the suite can fail: `Ui.Golden.ThresholdFailsOnDrift` changes the accent token by 8 levels in a scratch fixture and expects the comparison to fail. Done when: the meta-test passes. Cheaper substitute: thresholds so low every image passes.
- [ ] Implement `--ui-golden-update`: write candidate PNGs, a side-by-side diff sheet and a `report.md` to `build/golden-candidates/`, never to `design/screenshots/` or the baselines. Done when: `Ui.Golden.UpdateNeverTouchesScreenshots` compares SHA-256 of every `design/screenshots/*.png` before and after the update run.
- [ ] Expose `recto::ui::test::golden::expect_matches(id)` for D10 T02 §23 and D06 T04 §1 to add entries without new code. Done when: a sample entry added to the manifest in a scratch copy is picked up by the test with no C++ change.
- [ ] Declare the harness rendering profile in `tests/recto-tests/ui_golden.cpp` (grayscale text antialiasing, `DWRITE_RENDERING_MODE` and gamma from one `IDWriteRenderingParams3`, software target) and, when `expect_matches` fails, write the actual, expected and difference PNGs plus a `report.md` (fixture id, theme, scale, stride, SSIM and threshold, rendering profile, Windows build and the Segoe UI Variable file version) to `build/golden-failures/<id>/`. Done when: `Ui.Golden.TextDeterministic` renders the `type_ramp` fixture twice at 100, 150 and 200 percent with identical SHA-256, and `Ui.Golden.FailureWritesArtifacts` forces a mismatch and finds all four files with every report field filled. Source: the `D10 T01 §1` audit completion review 2026-10-23 (cross-machine text determinism and inspectable failure artifacts); Microsoft Learn "Supported pixel formats and alpha modes" (grayscale text on premultiplied targets). Cheaper substitute: a bare SSIM number in the failure message, which leaves a reviewer nothing to look at.
- [ ] Commit: `ui: add golden-image suite with SSIM thresholds and a review-only update path`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Golden.*` passes (crop and scale, manifest validation, deterministic fixtures, Light and Dark foundation crops at 100, 150 and 200 percent, HighContrast baselines, drift detection, update path leaves `design/screenshots/` unchanged).

## Verification

- Build: `.\build.cmd` builds x64 and ARM64 Debug and Release with `recto-ui` and `Recto.exe` at zero warnings, and `Recto.exe` opens a Mica Alt window with working drag, resize, Snap Layouts and system menu on a Windows 11 display-session host.
- Tests: `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.*` passes in full, and the headless subset (`Ui.Project.*`, `Ui.Dispatcher.*`, `Ui.Compositor.*`, `Ui.Element.*`, `Ui.Layout.*`, `Ui.Tokens.*`, `Ui.Painter.*`, `Ui.Text.*`, `Ui.Image.*`, `Ui.Binding.*`, `Ui.SwapChain.*`, `Ui.Golden.*`) passes without a display session; the same run under `EnableASAN=true` reports no error in `Ui.Dispatcher.*` and `Ui.Binding.*`.
- Tokens and boundary: `python scripts/build-ui-tokens.py --check` exits 0, `Ui.Tokens.NoHexLiteralsOutsideGenerated` and `Ui.Project.IncludeBoundary` pass, and `rg -n "Microsoft.WindowsAppSDK|check-winui-pin" --glob "!docs/**" --glob "!todo/**"` returns nothing.
- Tooling: `python -m unittest discover -s tests/scripts` and `python -m unittest discover -s scripts/tests` pass after the WinUI pin removal.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
