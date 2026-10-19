---
schema_version: 1
id: settings-and-layout
domain: 02-viewer
status: active
title: "TODO-04 -- Settings Store, Portable Mode, Layout Presets, Settings Page, Theme and Keyboard Map"
track: M2
---

# TODO-04 -- Settings Store, Portable Mode, Layout Presets, Settings Page, Theme and Keyboard Map

> **Goal:** The shell becomes the SPEC's shape-shifter: `AppPaths` Portable mode detection and live atomic migration (`<exe-dir>\data\` vs. `%LOCALAPPDATA%\Recto\`), a single-writer typed `SettingsStore`, the `LayoutPresetEngine` moving live controls across named shell slots for `Focus`, `Classic`, `Reading room` and `"Custom"`, the complete `SettingsPage` covering all SPEC section 9.7 groups with `"Find a setting"` live filtering, live theme switching (`Light`, `Dark`, `Follow Windows`, `Mica Alt`, and `HighContrast`), `167 ms` Fluent motion with Windows reduced-motion compliance, and the complete SPEC section 10 keyboard shortcut map and `Escape` layer stack audited end to end.

> **Current state:** Measured 2026-10-14. No code exists yet. `D02 T01` declares `ShellPage` and the eight named layout slots (`§4`); `D02 T02` and `D02 T03` build `PageToolbar`, `SidePanelHost`, `ThumbnailList`, `OutlinePanel`, `DocumentInfoPanel`, `CommandRegistry`, and `CommandBoxControl`. All visual contracts are fixed in `design/SPEC.md` sections 4, 5, 7, 8, 9.7, 10, 11 and 12 with boards `D-Layout.png`, `D-Settings.png`, `D-Focus-Light.png`, `D-Focus-Dark.png`, `D-Classic.png`, and `D-ReadingRoom.png`.

## Inputs

- `design/SPEC.md` sections 4 (colour, dark page modes, HighContrast), 5 (12 px Semibold all-caps group headers), 7 (radius 16 settings cards), 8 (the five layout settings composing three presets, D17: one shell, not three), 9.7 (Settings groups and order, Portable mode toggle with live `<exe-dir>\data\` migration, find-a-setting), 10 (keyboard table), 11 (accessibility and reduced motion), 12 (167 ms motion); `resources/svg/layout-{focus,classic,readingroom}-{light,dark}.svg`.
- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D6 Portable mode, D17), 6.2 (preset implementation summary).
- -> XREF: D02 T01 §4 -- the eight named slot hosts and `ShellSlotManager` this file's `LayoutPresetEngine` (§3) drives.
- -> XREF: D02 T03 §3 -- `SidePanelHost` and `ThumbnailList` (`§4`) whose live reparenting and orientation switching §3 here executes.
- -> XREF: D03 T01 §8 -- `Automatic OCR on scanned pages`, `OCR engine`, and `OCR language` settings in §5 here are consumed by `D03 T01 §8`.
- -> XREF: D03 T02 §1 -- high-contrast and reduced-motion settings in §7 and §8 here are audited in `D03 T02 §1` and `§2`.
- -> XREF: D03 T03 §7 -- `Highlight form fields` setting in §5 here is consumed by `D03 T03 §7`.
- -> XREF: D04 T01 §4 -- `Where to save converted files` setting in §5 here is consumed by `D04 T01 §4`.
- -> XREF: D04 T03 §5 -- `Default PDF to Word reconstruction mode` setting in §5 here is consumed by `D04 T03 §5`.
- -> XREF: D04 T04 §7 -- `Default Office to PDF page setup` setting in §5 here is consumed by `D04 T04 §7`.
- -> XREF: D05 T01 §6 -- `Default timestamp server URL` setting in §6 here is consumed by `D05 T01 §6`.
- -> XREF: D05 T02 §9 -- `Verify signatures when opening` setting in §6 here is consumed by `D05 T02 §9`.
- -> XREF: D07 T01 §5 -- the Inno Setup Portable installer and `Recto-Portable-*.zip` archives pre-create `data\portable.mode` consumed by `AppPaths` (§1) here.
- -> XREF: D07 T02 §1 -- the `About Recto` page (`D07 T02 §1`) and `Save local crash report bundles` (`D07 T02 §5`) complete the navigation and toggle in §6 and §10 here.

## Outcome

- `AppPaths` resolves `<exe-dir>\data\` when `<exe-dir>\data\portable.mode` exists (or `%LOCALAPPDATA%\Recto\` in standard mode) and migrates `settings.json`, `recents.json`, and cache/temp directories atomically when Portable mode is toggled in Settings.
- `SettingsStore` persists every SPEC 9.7 setting atomically with a single writer, recovers cleanly from corrupt JSON, and notifies subscribed runtime consumers immediately on change.
- `LayoutPresetEngine` switches between `Focus`, `Classic`, and `Reading room` by moving live controls across `ShellPage`'s named slots without losing document state, and switches the preset label to `"Custom"` whenever any of the five fine-tune settings is modified individually.
- `SettingsPage` implements every SPEC 9.7 group (`Layout`, `Theme`, `Reading`, `Converting`, `Signatures and security`, `Performance and privacy` including **Portable mode**, `Default PDF app`, `About`) and filters rows live as the user types in `"Find a setting"`.
- `Light`, `Dark`, `Follow Windows`, `HighContrast`, and reduced-motion settings apply live without restarting, and every SPEC section 10 keyboard shortcut and `Esc` layer priority is bound through `CommandRegistry` and verified by `Keyboard.Audit` and `docs/keyboard-shortcuts.md`.

**Adjacency:** list=applicable @ D02 T04 §4; document=applicable @ D02 T04 §10; settings=applicable @ D02 T04 §2; reporting=not-applicable (no reports); notifications=not-applicable (none originate here); permissions=not-applicable (no roles); audit=not-applicable (settings history is not kept, by design); exchange=not-applicable (no settings import or export in v1; Portable mode migrates the data folder in §1); reverse=applicable @ D02 T04 §3

**Adjacency rationale:** `SettingsStore` (`§2`) and `SettingsPage` (`§§4-6`) own all application settings, and `"Find a setting"` (`§4`) is the list/filter surface over those rows. The document surface here is the generated `docs/keyboard-shortcuts.md` reference (`§10`) linked from About (`D07 T02 §1`). The reverse surface is resetting from a `"Custom"` fine-tuned layout back to a named preset (`Focus`, `Classic`, or `Reading room`) in `§3` and toggling Portable mode back to standard `%LOCALAPPDATA%\Recto\` storage in `§1`.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `AppPaths` Portable mode path resolver and live atomic data migration | D00 T01 §3 | [ ] |
| 2 | §2 | Typed `SettingsStore` service with atomic persistence and change notifications | §1 | [ ] |
| 3 | §3 | Layout preset engine (`Focus`, `Classic`, `Reading room`, `Custom`) over shell slots | §2, D02 T01 §4, D02 T02 §8, D02 T03 §3, D02 T03 §4 | [ ] |
| 4 | §4 | Settings page frame, `"Find a setting"` live filter, and Layout & Theme (`D-Layout.png`) | §3, D02 T03 §11 | [ ] |
| 5 | §5 | Settings page: Reading and Converting groups (`D-Settings.png`) | §4, D02 T02 §9, D02 T03 §1 | [ ] |
| 6 | §6 | Settings page: Security, Performance & Privacy (Portable mode & Cache), Default & About | §4, D02 T02 §3 | [ ] |
| 7 | §7 | Live theme switching (`Light`, `Dark`, `Follow Windows`), Mica Alt and HighContrast | §4, D02 T02 §10 | [ ] |
| 8 | §8 | Motion system (`167 ms` Fluent transitions) and Windows reduced-motion compliance | §3, D02 T02 §8, D02 T03 §3 | [ ] |
| 9 | §9 | Complete SPEC section 10 keyboard accelerator bindings and `Escape` layer stack | §3, D02 T03 §11, D02 T03 §12 | [ ] |
| 10 | §10 | End-to-end keyboard audit harness (`Keyboard.Audit`) and `docs/keyboard-shortcuts.md` | §9 | [ ] |

## 1. `AppPaths` Portable mode path resolver and live atomic data migration

Implement `AppPaths` (`src/services/app_paths.h/.cpp`) per foundation plan decision D6 and `design/SPEC.md` section 9.7: at startup, check whether `<exe-dir>\data\portable.mode` exists next to `Recto.exe`; when present, resolve `data_dir()`, `settings_path()`, `recents_path()`, `cache_dir()`, `temp_dir()`, and `crash_reports_dir()` inside `<exe-dir>\data\` with zero writes to `%LOCALAPPDATA%\Recto` or the Windows Registry; when absent, resolve to `%LOCALAPPDATA%\Recto\`. Implement `set_portable_mode(bool enable)` to migrate existing `settings.json`, `recents.json`, and cached cover thumbnails atomically between `%LOCALAPPDATA%\Recto\` and `<exe-dir>\data\` at runtime when toggled in Settings (§6).

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/app_paths.h/.cpp` providing `AppPaths::initialize(custom_exe_dir, custom_local_appdata_dir)`, `is_portable_mode()`, `data_dir()`, `settings_path()`, `recents_path()`, `cache_dir()`, `covers_cache_dir()`, `temp_dir()`, and `crash_reports_dir()`. Done when: unit tests confirm that placing `data\portable.mode` inside a temporary `exe_dir` routes 100% of resolved paths under `<exe-dir>\data\` and creates zero directories under `local_appdata_dir`.
2. Implement `AppPaths::set_portable_mode(bool enable)` performing atomic live migration: verify write permissions in the destination directory first (returning a typed `PdfResult<void>` refusal if `<exe-dir>` is inside a read-only `C:\Program Files\` folder without elevation), copy `settings.json`, `recents.json`, and `cache/covers/` to the target directory via temp-file-then-replace (`ReplaceFileW` / `MoveFileExW`), create or remove `<exe-dir>\data\portable.mode`, switch the active `data_dir()` pointer, and remove the migrated files from the old location. Done when: toggling `set_portable_mode(true)` and `set_portable_mode(false)` in tests migrates all state files losslessly and refuses gracefully if `<exe-dir>` is non-writable.

- [ ] Author `src/services/app_paths.h/.cpp` resolving `<exe-dir>` via `GetModuleFileNameW(nullptr, ...)` and `%LOCALAPPDATA%\Recto` via `SHGetKnownFolderPath(FOLDERID_LocalAppData, ...)` with injectable test overrides. Done when: `AppPaths` compiles in `recto-services` and supports isolated temp-directory unit tests.
- [ ] Detect `<exe-dir>\data\portable.mode` at startup and set `is_portable_mode() = true` before `SettingsStore` or `RecentFilesStore` initializes. Done when: a portable installation with `data\portable.mode` never touches `%LOCALAPPDATA%\Recto` on startup or shutdown. Cheaper substitute that fails the checkpoint: reading a `"portable_mode"` boolean from `%LOCALAPPDATA%\Recto\settings.json` on startup, which creates `%LOCALAPPDATA%\Recto` even in portable builds.
- [ ] Resolve `settings_path()`, `recents_path()`, `cache_dir()`, `covers_cache_dir()`, `temp_dir()`, and `crash_reports_dir()` relative to the active `data_dir()`. Done when: every service (`SettingsStore`, `RecentFilesStore`, temp save helpers, crash bundle writer) queries `AppPaths` instead of hard-coding environment variables.
- [ ] Probe write access in `<exe-dir>\data\` before starting `set_portable_mode(true)` migration, returning a descriptive error if `Recto.exe` is installed in a protected directory (`Program Files`) where standard users cannot write. Done when: attempting portable migration in a read-only test directory returns an error without corrupting `%LOCALAPPDATA%\Recto`.
- [ ] Migrate `settings.json`, `recents.json`, and `cache/covers/` atomically (`ReplaceFileW` / `MoveFileExW`) during `set_portable_mode(bool)` before writing/deleting the `portable.mode` marker file. Done when: a simulated failure mid-migration leaves the source data directory intact.
- [ ] Clean up migrated files from the previous data directory once the new `portable.mode` state is committed. Done when: switching from Standard to Portable mode leaves no orphaned user recents behind in `%LOCALAPPDATA%\Recto\`.
- [ ] Add `Settings.PortableModePathsAndMigration` test in `tests/recto-tests/` verifying startup detection of `data\portable.mode`, zero `%LOCALAPPDATA%` footprint, round-trip `Standard -> Portable -> Standard` live migration, and read-only directory refusal. Done when: `recto-tests.exe --gtest_filter=Settings.PortableModePathsAndMigration` passes.
- [ ] Commit: `services: implement AppPaths Portable mode resolver and atomic data migration`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Settings.PortableModePathsAndMigration` passes, proving zero `%LOCALAPPDATA%` footprint in Portable mode and lossless live migration both ways.

## 2. Typed `SettingsStore` service with atomic persistence and change notifications

Implement the single-writer typed settings store (`src/services/settings.h/.cpp`) covering every setting in `design/SPEC.md` sections 8 and 9.7: `LayoutPreset` (`Focus`, `Classic`, `ReadingRoom`, `Custom`), the five layout slot settings (`ToolbarPlacement`, `SidePanelStyle`, `ThumbnailPlacement`, `SearchAndOutlineSide`, `NavigationStyle`), `AppTheme` (`Light`, `Dark`, `FollowWindows`), `DarkPageColorMode` (`Original`, `Dimmed`, `Inverted`), `DefaultZoom`, `DefaultPageLayout`, `AskBeforeOpeningLinks`, `RememberReadingPosition`, `AutoOcrScannedPages`, `HighlightFormFields`, `DefaultOfficePageSetup` (`MatchOriginal`, `A4`, `Letter`), `DefaultPdfToWordMode`, `OcrEngineChoice` (`HybridAuto`, `NeuralHighAccuracy`, `WindowsMediaOcr`), `OcrLanguage`, `SaveConvertedLocation`, `CustomSaveConvertedFolder`, `VerifySignaturesOnOpen`, `DefaultTimestampUrl`, `TileCacheBudgetMb` (clamped `[128, 2048]`, default `512`), and `SaveLocalCrashBundles` (default `false`).

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `RectoSettings` and `SettingsStore` in `src/services/settings.h/.cpp` with SPEC-exact default values, validation/clamping rules (e.g., `TileCacheBudgetMb` clamped to `[128, 2048]`, unknown enum strings falling back to their SPEC defaults), and atomic JSON serialization (`ReplaceFileW` / `MoveFileExW`) to `AppPaths::settings_path()` (§1). Done when: modifying every field in `RectoSettings`, saving, and reloading into a fresh `SettingsStore` instance reproduces identical values, and a corrupt or truncated `settings.json` falls back to SPEC defaults and logs a diagnostic entry.
2. Implement thread-safe observer subscriptions (`SubscriptionToken subscribe(std::function<void(const RectoSettings&)>)`) so runtime consumers (`LayoutPresetEngine` §3, `TileTextureCache` `D02 T02 §3`, `PresentationRenderer` `D02 T02 §10`, `OpenLinkDialog` `D02 T02 §9`, `RecentFilesStore` `D02 T03 §1`) react immediately when a setting changes. Done when: mutating a setting fires all active subscribers and unsubscribing via `SubscriptionToken` stops delivery cleanly.

- [ ] Author `src/services/settings.h/.cpp` defining strongly typed enums and fields for all SPEC section 8 and 9.7 settings with their exact SPEC defaults (`Focus` preset, `FollowWindows` theme, `Dimmed` dark pages, `AskBeforeOpeningLinks = true`, `RememberReadingPosition = true`, `DefaultOfficePageSetup = MatchOriginal`, `OcrEngineChoice = HybridAuto`, `TileCacheBudgetMb = 512`, `SaveLocalCrashBundles = false`). Done when: default-constructed `RectoSettings` matches every default stated in `design/SPEC.md`.
- [ ] Enforce single-writer atomic persistence to `AppPaths::settings_path()` via temp-file-then-replace (`ReplaceFileW` / `MoveFileExW`). Done when: searching `src/` confirms `SettingsStore::save` is the sole writer of `settings.json`. Cheaper substitute that fails the checkpoint: scattering `ApplicationData::Current().LocalSettings()` writes across XAML code-behind files.
- [ ] Clamp numeric settings (`TileCacheBudgetMb` to `[128, 2048]`, `DefaultZoom` to `[25, 500]`) and validate enum strings on load so hand-edited or out-of-range JSON values never put the app into an invalid state. Done when: loading `"tile_cache_budget_mb": 999999` clamps to `2048` and `"tile_cache_budget_mb": -5` clamps to `128`.
- [ ] Recover gracefully from corrupt, empty, or schema-mismatched `settings.json` files by resetting to SPEC defaults and recording a warning in `recto::pdf::diagnostics_tail()`. Done when: a planted corrupt `settings.json` loads default settings without throwing or crashing.
- [ ] Implement thread-safe `subscribe` / `unsubscribe` change notification callbacks invoked whenever a setting changes or Portable mode migrates paths. Done when: concurrent readers and subscribers observe consistent `RectoSettings` snapshots.
- [ ] Add `Settings.StorePersistenceAndValidation` unit tests in `tests/recto-tests/` covering round-trip JSON persistence of all settings, numeric clamping, corrupt-file fallback, and subscriber notifications. Done when: `recto-tests.exe --gtest_filter=Settings.StorePersistenceAndValidation` passes.
- [ ] Commit: `services: add single-writer typed SettingsStore with atomic JSON persistence`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Settings.StorePersistenceAndValidation` passes, verifying all SPEC settings round-trip, out-of-range values clamp, and corrupt files degrade to defaults cleanly.

## 3. Layout preset engine (`Focus`, `Classic`, `Reading room`, `Custom`) over shell slots

Implement `LayoutPresetEngine` (`src/app/LayoutPresetEngine.h/.cpp`) per `design/SPEC.md` section 8 and foundation plan decision D17 ("One app shell, three layout presets (Focus default, Classic, Reading room) through settings and named slots: never three shells"): maps the three presets to their five component layout settings (`PageToolbarPlacement`, `SidePanelsStyle`, `ThumbnailsPlacement`, `SearchAndOutlineSide`, `NavigationMode`), moves the live control instances (`PageToolbar`, `SidePanelHost`, `ThumbnailList`, `OutlinePanel`, `SearchCard`, `ShellMenuBar`, `NavigationRailControl`, and document `TabView`) across `ShellPage`'s eight named slots (`D02 T01 §4`) while preserving open tabs, current page, zoom, scroll offset, and panel state, and automatically updates the active preset name to `"Custom"` whenever any single layout setting deviates from the three preset tuples.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 8 table (`Focus`, `Classic`, `Reading room`, and `"Custom"` derivation) with `design/screenshots/D-Focus-Light.png`, `D-Classic.png`, `D-ReadingRoom.png`, and `D-Layout.png`.
**Job:** The user can switch Recto's entire interface between a minimal floating canvas (`Focus`), a traditional command-bar-and-rail desktop layout (`Classic`), or a dark-first book reader with a bottom filmstrip and right inspector (`Reading room`), or mix-and-match individual slot settings (`Custom`), mid-reading without losing their place. Consumer: `ShellPage` (`D02 T01 §4`) and `SettingsPage` (§4).
**Treatment:** Live reparenting of the single `PageToolbar`, `SidePanelHost`, `ThumbnailList`, and `TabView` instances via `ShellSlotManager` (`D02 T01 §4`) plus `ThumbnailList::set_orientation` (`D02 T03 §4`). Cheaper substitute that fails the checkpoint: recreating `DocumentView` or reloading the PDF when switching presets, or ignoring individual fine-tune changes.
**Chrome:** consume `ShellSlotManager` (`D02 T01 §4`), `PageToolbar` (`D02 T02 §8`), `SidePanelHost` (`D02 T03 §3`), and `ThumbnailList` (`D02 T03 §4`).

**Build order:**

1. Author `src/app/LayoutPresetEngine.h/.cpp` defining the exact SPEC section 8 5-tuple mapping for `Focus` (Page toolbar = `BottomOverlaySlot` floating bottom centre; Side panels = `LeftOverlaySlot` floating cards; Page thumbnails = in left panel `Vertical`; Search & outline = `LeftOverlaySlot`; Navigation = title bar tabs + Command Box), `Classic` (Page toolbar = `TopBarSlot` docked command bar under `ShellMenuBar`; Side panels = `LeftDockSlot` docked left pane; Page thumbnails = in left pane `Vertical`; Search & outline = `LeftDockSlot`; Navigation = `LeftDockSlot` navigation rail `NavigationRailControl`), and `Reading room` (Page toolbar = `TopBarSlot` docked strip under tabs; Side panels = `RightDockSlot` docked right inspector; Page thumbnails = `BottomDockSlot` `Horizontal` filmstrip along bottom; Search & outline = `RightDockSlot` inspector tabs; Navigation = mode switcher in `TitleBarSlot` `Home | Read | Convert | Organize` with document `TabView` moved to `TopBarSlot`), and implement `derive_preset_from_tuple(tuple) -> LayoutPreset` returning `Focus`, `Classic`, `ReadingRoom`, or `Custom`. Done when: unit tests confirm the 3 canonical tuples map to their named presets and all other 5-tuples resolve to `Custom`.
2. Wire `LayoutPresetEngine::apply_layout(const RectoSettings&)` to `ShellSlotManager::reparent_control` (`D02 T01 §4`) and subscribe it to `SettingsStore` (§2): moving `PageToolbar`, `SidePanelHost`, `ThumbnailList` (switching `Vertical` vs. `Horizontal`), `ShellMenuBar`, `NavigationRailControl`, and `TabView` to their target slots in a single layout pass while `DocumentView` stays untouched in `CenterCanvasSlot`. Done when: cycling `Focus -> Classic -> Reading room -> Focus` with a PDF open on page 7 at `150%` zoom preserves the exact `DocumentView` instance, page 7, `150%` zoom, and active side panel.

- [ ] Author `src/app/LayoutPresetEngine.h/.cpp` encoding the exact SPEC section 8 5-property tuples for `Focus`, `Classic`, and `Reading room` and `derive_preset_from_tuple`. Done when: changing any 1 of the 5 properties away from `Focus`, `Classic`, or `Reading room` immediately computes `LayoutPreset::Custom`.
- [ ] Apply `Focus` layout by placing `TabView` in `TitleBarSlot`, `PageToolbar` in `BottomOverlaySlot`, `SidePanelHost` in `LeftOverlaySlot`, `ThumbnailList` (`Vertical`) inside `SidePanelHost`, and collapsing `ShellMenuBar` and `NavigationRailControl`. Done when: `Focus` layout matches `D-Focus-Light.png` and `D-Focus-Dark.png`.
- [ ] Apply `Classic` layout by placing `ShellMenuBar` and `PageToolbar` in `TopBarSlot`, `NavigationRailControl` and `SidePanelHost` in `LeftDockSlot`, and `ThumbnailList` (`Vertical`) inside `SidePanelHost`. Done when: `Classic` layout matches `D-Classic.png`.
- [ ] Apply `Reading room` layout by activating the mode switcher (`Home | Read | Convert | Organize`) in `TitleBarSlot`, moving `TabView` and `PageToolbar` into `TopBarSlot`, docking `SidePanelHost` (hosting `Info`, `Outline`, `Search`) into `RightDockSlot`, and moving `ThumbnailList` (`Horizontal` filmstrip) into `BottomDockSlot`. Done when: `Reading room` layout matches `D-ReadingRoom.png`.
- [ ] Ensure `DocumentView` (`CenterCanvasSlot`), open `DocumentTabSession` actors, scroll offset, zoom factor, and active panel selection remain completely uninterrupted across preset and slot changes. Done when: pointer-identity assertions in tests verify the exact same `DocumentView`, `PageToolbar`, and `ThumbnailList` instances survive `Focus -> Classic -> Reading room` transitions. Cheaper substitute that fails the checkpoint: rebuilding `ShellPage` or re-opening the document on preset change.
- [ ] Provide `apply_named_preset(LayoutPreset)` (the reverse action from `Custom`) that resets all 5 fine-tune properties to the chosen named preset and persists them in `SettingsStore`. Done when: selecting `Focus` after customizing individual slots restores all 5 `Focus` defaults at once.
- [ ] Register `Layout Preset: Focus`, `Layout Preset: Classic`, and `Layout Preset: Reading room` commands in `CommandRegistry` (`D02 T03 §11`). Done when: all three presets can be switched directly from `Ctrl+K` and the `View -> Layout Preset` menu.
- [ ] Add `Settings.LayoutPresetEngineAndStatePreservation` test in `tests/recto-tests/` cycling all 3 presets plus `Custom` fine-tuned combinations with an open document and asserting slot placements, `ThumbnailList` orientation, `Custom` derivation, and preserved page/zoom/scroll state. Done when: `recto-tests.exe --gtest_filter=Settings.LayoutPresetEngineAndStatePreservation` passes.
- [ ] Commit: `viewer: implement LayoutPresetEngine reparenting live controls across Focus, Classic, Reading room and Custom`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Settings.LayoutPresetEngineAndStatePreservation` passes on the display-session host, and rendered screenshots of `Focus`, `Classic`, and `Reading room` match `D-Focus-Light.png`, `D-Classic.png`, and `D-ReadingRoom.png`.

## 4. Settings page frame, `"Find a setting"` live filter, and Layout & Theme (`D-Layout.png`)

Build the `SettingsPage` frame (`src/app/SettingsPage.xaml/.h/.cpp`), the real-time `"Find a setting"` filter box, and the first two SPEC section 9.7 groups (`Layout` and `Theme`) matching `design/screenshots/D-Layout.png` and `D-Settings.png`: single centred column (`760 to 900 epx` wide), `TitleTextBlockStyle` (`28 epx Semibold`) `"Settings"` heading, `"Find a setting"` filter input, `12 px Semibold` all-caps group headers (`letter-spacing 0.6`), `CornerRadius=16` grouped cards on `RectoRaisedBrush` with `1 epx` `RectoBorderBrush` dividers, the three interactive preset wireframe cards (`resources/svg/layout-{focus,classic,readingroom}-{light,dark}.svg` with `1.5 epx` `RectoAccentBrush` active border, `3 epx` `RectoAccentTintBrush` glow, and check badge), the five fine-tune layout dropdown rows, and the `Theme` group (`App theme` and `Pages in dark theme`).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 3.4 (`layout-*-light.svg` and `-dark.svg`), 5 (`TitleTextBlockStyle` 28 / Semibold, 12 px Semibold all-caps group labels with 0.6 letter spacing), 7 (`CornerRadius=16` settings groups, non-colour check badge on selected card per section 11), 8 and 9.7 with `design/screenshots/D-Layout.png` and `D-Settings.png`.
**Job:** The user can open Settings (`Ctrl+,`, title bar gear, or `Ctrl+K`), visually pick `Focus`, `Classic`, or `Reading room` from wireframe cards, fine-tune individual slot placements, choose their theme and dark-page mode, or type a keyword in `"Find a setting"` to filter straight to the row they want. Consumer: `SettingsStore` (§2), `LayoutPresetEngine` (§3), and theme/page-colour consumers (§7, `D02 T02 §10`).
**Treatment:** Single-column `ScrollViewer` (`MaxWidth=900`, `MinWidth=760` when window allows) with data-tagged setting rows so `"Find a setting"` hides non-matching rows and collapses empty groups live as the user types. Cheaper substitute that fails the checkpoint: text-only preset buttons without the wireframe SVGs, omitting the check badge on the active preset card, or a search box that does not collapse empty groups.
**Chrome:** consume `resources/svg/layout-{focus,classic,readingroom}-{light,dark}.svg` via theme resources, `RectoRaisedBrush`, `RectoBorderBrush`, `RectoAccentBrush`, and `RectoAccentTintBrush`.

**Build order:**

1. Author `src/app/SettingsPage.xaml/.h/.cpp` with the `760 to 900 epx` centred column, `"Settings"` title (`TitleTextBlockStyle`), `"Find a setting"` search box (`CornerRadius=8`, `Esc` clears query), and the row-filtering engine that matches query substrings against each setting row's title, description, and keywords, collapsing any group card whose visible row count drops to 0 and showing an honest empty state (`"No settings match '<query>'"`) when 0 rows match across all groups. Done when: typing `"toolbar"`, `"dark"`, `"portable"`, or `"xyz_nonexistent"` filters rows and groups accurately.
2. Build the **LAYOUT** group card (3 preset cards `Focus`, `Classic`, `Reading room` displaying `resources/svg/layout-{focus,classic,readingroom}-{light,dark}.svg` wireframes, `1.5 epx` `RectoAccentBrush` border + `3 epx` `RectoAccentTintBrush` glow + top-right check badge on the active preset, a `"Custom"` status badge when fine-tuned, and the 5 fine-tune `ComboBox` rows: `Page toolbar`, `Side panels`, `Page thumbnails`, `Search and outline`, `Navigation`) and the **THEME** group card (`App theme`: `Light`, `Dark`, `Follow Windows`; `Pages in dark theme`: `Original`, `Dimmed`, `Inverted`), bound two-way to `SettingsStore` (§2). Done when: clicking each preset wireframe card rearranges the shell live, changing a fine-tune dropdown flips the preset indicator to `"Custom"`, and rendered screenshots match `D-Layout.png`.

- [ ] Author `src/app/SettingsPage.xaml/.h/.cpp` with the `760 to 900 epx` centred column, `TitleTextBlockStyle` (`28 epx Semibold`) header, and `12 px Semibold` all-caps group labels (`CharacterSpacing=60` corresponding to `0.6 em` tracking per SPEC 5). Done when: typography and card geometry (`CornerRadius=16`) match `D-Layout.png` and `D-Settings.png`.
- [ ] Build the three interactive preset cards (`Focus`, `Classic`, `Reading room`) loading `resources/svg/layout-{focus,classic,readingroom}-{light,dark}.svg` via theme-aware `SvgImageSource` resources. Done when: wireframe illustrations switch cleanly between Light and Dark themes.
- [ ] Style the selected preset card with a `1.5 epx` `RectoAccentBrush` border, `3 epx` `RectoAccentTintBrush` outer glow, and a visible check badge (`FontIcon` check glyph on `RectoAccentBrush` circle so selection is never conveyed by colour alone per SPEC 11). Done when: the active preset card matches `D-Layout.png` and passes high-contrast inspection. Cheaper substitute that fails the checkpoint: plain radio buttons without the wireframe cards or omitting the check badge.
- [ ] Wire the five fine-tune `ComboBox` rows (`Page toolbar`, `Side panels`, `Page thumbnails`, `Search and outline`, `Navigation`) to `SettingsStore` (§2) and `LayoutPresetEngine` (§3), displaying `"Custom"` with a `"Reset to Focus"` action when any row deviates from a named preset. Done when: changing `Page thumbnails` from `Left panel` to `Bottom filmstrip` updates the shell immediately and shows `"Custom"`.
- [ ] Build the **THEME** group card with `App theme` (`Light`, `Dark`, `Follow Windows`) and `Pages in dark theme` (`Original`, `Dimmed`, `Inverted`) bound two-way to `SettingsStore`. Done when: changing either selector updates `SettingsStore` and notifies live subscribers immediately.
- [ ] Implement the `"Find a setting"` live filter box at the top of `SettingsPage` matching row labels and descriptions, collapsing empty group cards, displaying `"No settings match '<query>'"` when nothing matches, and clearing on `Esc`. Done when: typing `"dimmed"` shows only the `Pages in dark theme` row under `THEME` and collapses `LAYOUT`.
- [ ] Wire `Ctrl+,` and the title bar Settings gear button (`D02 T01 §2`) to open/focus `SettingsPage` in `ShellPage`. Done when: pressing `Ctrl+,` opens `SettingsPage` and places focus in `"Find a setting"`.
- [ ] Add `Settings.LayoutAndThemeGroupsAndFilter` test in `tests/recto-tests/` verifying preset card selection, fine-tune `"Custom"` transition, `Theme` bindings, and `"Find a setting"` group collapse and empty state. Done when: `recto-tests.exe --gtest_filter=Settings.LayoutAndThemeGroupsAndFilter` passes.
- [ ] Commit: `viewer: build SettingsPage frame, Find-a-setting filter, and Layout & Theme groups`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Settings.LayoutAndThemeGroupsAndFilter` passes on the display-session host, and rendered `SettingsPage` Layout group matches `D-Layout.png`.

## 5. Settings page: Reading and Converting groups (`D-Settings.png`)

Build the **READING** and **CONVERTING** group cards on `SettingsPage` (`src/app/SettingsPage.xaml/.h/.cpp`) per `design/SPEC.md` section 9.7 and `design/screenshots/D-Settings.png`: **READING** covers `Default zoom` (`Fit width`, `Fit page`, `100%`, `125%`, `150%`), `Default page layout` (`Continuous`, `Single page`, `Two-page spread`), `Ask before opening links` (`ToggleSwitch`, default on, consumed by `D02 T02 §9`), `Remember reading position` (`ToggleSwitch`, default on, consumed by `D02 T03 §1`), `Automatic OCR on scanned pages` (`ToggleSwitch`, consumed by `D03 T01 §8`), and `Highlight form fields` (`ToggleSwitch`, consumed by `D03 T03 §7`); **CONVERTING** covers `Default Office to PDF page setup` (`Match original document`, `A4`, `Letter`, consumed by `D04 T04 §7`), `Default PDF to Word reconstruction mode` (`Flowing`, `Exact`, `Continuous`, consumed by `D04 T03 §5`), `OCR engine` (`Hybrid High-Accuracy Neural + Windows OCR` default, `High-Accuracy Neural OCR`, `Windows Fast OCR`, consumed by `D03 T01 §8`), `OCR language` (enumerating installed `Windows.Media.Ocr.OcrEngine.AvailableRecognizerLanguages`, consumed by `D03 T01 §8`), and `Where to save converted files` (`Same folder as original`, `Ask every time`, or custom folder via `FolderPicker`, consumed by `D04 T01 §4`).

> **Deferred:** Runtime consumption of `Automatic OCR on scanned pages`, `OCR engine`, and `OCR language` -> XREF: D03 T01 §8
> **Deferred:** Runtime consumption of `Highlight form fields` -> XREF: D03 T03 §7
> **Deferred:** Runtime consumption of `Where to save converted files` -> XREF: D04 T01 §4
> **Deferred:** Runtime consumption of `Default PDF to Word reconstruction mode` -> XREF: D04 T03 §5
> **Deferred:** Runtime consumption of `Default Office to PDF page setup` -> XREF: D04 T04 §7

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.7 (`Reading` and `Converting` groups) with `design/screenshots/D-Settings.png`.
**Job:** The user can configure how newly opened PDFs display (`Default zoom`, `Default page layout`, `Remember reading position`, `Ask before opening links`) and how OCR and document conversions behave (`Default Office to PDF page setup`, `PDF to Word mode`, `OCR engine`, `OCR language`, `Save location`). Consumer: `SettingsStore` (§2), `DocumentView` (`D02 T02 §§5, 7, 9`), `RecentFilesStore` (`D02 T03 §1`), and `D03` / `D04` services.
**Treatment:** Grouped `CornerRadius=16` setting cards using stock WinUI `ComboBox`, `ToggleSwitch`, and `Button` (`Browse...` `FolderPicker`) controls bound two-way to `SettingsStore`, with live enumeration of installed Windows OCR languages (`OcrEngine::AvailableRecognizerLanguages()`). Cheaper substitute that fails the checkpoint: hard-coding `"English"` as the only OCR language instead of querying `Windows.Media.Ocr`, or leaving M2 reading settings (`Default zoom`, `Default page layout`, `Ask before opening links`, `Remember reading position`) disconnected from `DocumentView` and `RecentFilesStore`.
**Chrome:** consume `SettingsPage` (§4) card templates, `RectoRaisedBrush`, and `RectoBorderBrush`.

**Build order:**

1. Build the **READING** group card in `src/app/SettingsPage.xaml/.h/.cpp` with `Default zoom`, `Default page layout`, `Ask before opening links`, `Remember reading position`, `Automatic OCR on scanned pages`, and `Highlight form fields`, and wire `OpenDocumentPipeline` (`D02 T01 §5`) and `DocumentView` (`D02 T02`) so newly opened documents without a saved per-file reading position start at `Default zoom` and `Default page layout`, `OpenLinkDialog` (`D02 T02 §9`) reads `Ask before opening links`, and `RecentFilesStore` (`D02 T03 §1`) reads `Remember reading position`. Done when: changing `Default page layout` to `Two-page spread` opens the next unvisited PDF in `Two-page spread` mode, and turning off `Remember reading position` opens previously visited PDFs at page 1.
2. Build the **CONVERTING** group card in `src/app/SettingsPage.xaml/.h/.cpp`: bind `Default Office to PDF page setup` (`Match original document`, `A4`, `Letter`), bind `Default PDF to Word mode` (`Flowing`, `Exact`, `Continuous`), bind `OCR engine` (`Hybrid High-Accuracy Neural + Windows OCR`, `High-Accuracy Neural OCR`, `Windows Fast OCR`), query `Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages()` to populate the `OCR language` `ComboBox` (defaulting to the user profile language or first installed language), and bind `Where to save converted files` with a `FolderPicker` (`IInitializeWithWindow`) when `"Custom folder"` is chosen. Done when: every control persists in `SettingsStore`, `OCR language` lists the host's real installed Windows OCR packs, and `"Find a setting"` (§4) filters all rows in both groups.

- [ ] Build the **READING** group card on `SettingsPage` with `Default zoom`, `Default page layout`, `Ask before opening links`, `Remember reading position`, `Automatic OCR on scanned pages`, and `Highlight form fields` bound two-way to `SettingsStore` (§2). Done when: all 6 rows render in exact SPEC 9.7 order matching `D-Settings.png`.
- [ ] Connect `Default zoom` and `Default page layout` to `OpenDocumentPipeline` (`D02 T01 §5`) and `DocumentView` (`D02 T02 §§5, 7`) as the initial view state for newly opened documents. Done when: setting `Default zoom = 150%` and `Default page layout = Single page` opens a new PDF at `150%` in `Single page` mode.
- [ ] Connect `Ask before opening links` to `OpenLinkDialog` (`D02 T02 §9`) and `Remember reading position` to `RecentFilesStore` (`D02 T03 §1`). Done when: toggling either switch in Settings immediately governs the next link click or file reopen. Cheaper substitute that fails the checkpoint: persisting `Default zoom`, `Ask before opening links`, or `Remember reading position` in JSON without wiring `DocumentView` or `RecentFilesStore` to read them.
- [ ] Build the **CONVERTING** group card with `Default Office to PDF page setup` (`Match original document`, `A4`, `Letter`), `Default PDF to Word mode` (`Flowing`, `Exact`, `Continuous`), `OCR engine` (`Hybrid High-Accuracy Neural + Windows OCR`, `High-Accuracy Neural OCR`, `Windows Fast OCR`), `OCR language`, and `Where to save converted files`. Done when: all 5 rows render and persist in `SettingsStore`.
- [ ] Populate the `OCR language` `ComboBox` dynamically from `Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages()` (`Language::DisplayName` and `LanguageTag`). Done when: the dropdown lists the host OS's installed language packs and persists the selected BCP-47 tag (e.g. `en-US`).
- [ ] Wire the `"Custom folder..."` option in `Where to save converted files` to `Windows::Storage::Pickers::FolderPicker` (initialized with `IInitializeWithWindow`) and display the chosen folder path under the row. Done when: picking a custom folder saves its path to `SettingsStore`.
- [ ] Tag all rows across **READING** and **CONVERTING** with search metadata so `"Find a setting"` (§4) surfaces them on queries like `"zoom"`, `"links"`, `"ocr"`, `"word"`, `"office"`, and `"save"`. Done when: driven filter queries match the exact expected rows.
- [ ] Add `Settings.ReadingAndConvertingGroups` test in `tests/recto-tests/` verifying two-way `SettingsStore` bindings, live consumer effects on `DocumentView` (`Default zoom`, `Default page layout`, `Ask before opening links`) and `RecentFilesStore` (`Remember reading position`), and `Windows.Media.Ocr` language enumeration. Done when: `recto-tests.exe --gtest_filter=Settings.ReadingAndConvertingGroups` passes.
- [ ] Commit: `viewer: add Settings Reading and Converting groups with live consumer bindings`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Settings.ReadingAndConvertingGroups` passes on the display-session host, and rendered **READING** and **CONVERTING** cards match `D-Settings.png`.

## 6. Settings page: Security, Performance & Privacy (Portable mode & Cache), Default & About

Complete the remaining groups of `SettingsPage` (`src/app/SettingsPage.xaml/.h/.cpp`) per `design/SPEC.md` section 9.7 and `design/screenshots/D-Settings.png`: **SIGNATURES AND SECURITY** covers `Verify signatures when opening` (`ToggleSwitch`, default on, consumed by `D05 T02 §9`) and `Default timestamp server URL` (`TextBox` for RFC 3161 TSA endpoint, consumed by `D05 T01 §6`); **PERFORMANCE AND PRIVACY** covers **Portable mode** (`ToggleSwitch`: `"Store settings, recent files, cache and temporary files in a data folder next to Recto.exe"`, displaying the active resolved `AppPaths::data_dir()` path and calling `AppPaths::set_portable_mode` §1 to migrate files live), `Page cache` (`Slider` / `NumberBox` from `128 MB` to `2048 MB`, default `512 MB`, updating `TileTextureCache::set_budget_bytes` `D02 T02 §3` live), and `Save local crash report bundles` (`ToggleSwitch`, default `off`, consumed by `D07 T02 §5`); **DEFAULT PDF APP & ABOUT** covers the `Default PDF app` card (`"Open Windows Default apps"` button launching `ms-settings:defaultapps`) and `About Recto` navigation card (opening `D07 T02 §1`'s About view).

> **Deferred:** Runtime consumption of `Verify signatures when opening` and `Default timestamp server URL` -> XREF: D05 T01 §6
> **Deferred:** Full `About` page content and `Save local crash report bundles` consumer -> XREF: D07 T02 §1

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.7 (`Signatures and security`, `Performance and privacy`, `Default PDF app`, `About`) with `design/screenshots/D-Settings.png`.
**Job:** The user can toggle **Portable mode** in place (seeing the active data folder path and migrating their settings and recents immediately), tune the GPU tile cache memory budget from `128 MB` to `2 GB`, configure signature verification and crash bundle privacy, or jump to Windows Default Apps and About. Consumer: `AppPaths` (§1), `SettingsStore` (§2), `TileTextureCache` (`D02 T02 §3`), `D05 T01 §6`, `D05 T02 §9`, and `D07 T02 §§1, 5`.
**Treatment:** Grouped `CornerRadius=16` cards bound to `SettingsStore` (§2) and `AppPaths` (§1), with inline error feedback if enabling Portable mode fails because `<exe-dir>` is inside a read-only directory (`Program Files`), and immediate live propagation of `TileCacheBudgetMb` to every open `DocumentView`'s `TileTextureCache`. Cheaper substitute that fails the checkpoint: a Portable mode toggle that requires manual file copying or app restart, or a cache slider that does not update `TileTextureCache::set_budget_bytes` live.
**Chrome:** consume `SettingsPage` (§4) card templates, `RectoRaisedBrush`, `RectoBorderBrush`, and `RectoWarningBrush` / `RectoDangerBrush` for read-only folder notice.

**Build order:**

1. Build the **SIGNATURES AND SECURITY** group card (`Verify signatures when opening` toggle and `Default timestamp server URL` input validating `http://` / `https://` scheme or empty string) and the **PERFORMANCE AND PRIVACY** group card (**Portable mode** toggle with live resolved `AppPaths::data_dir()` path caption and `"Open data folder"` link, `Page cache` slider `128 MB` to `2048 MB` stepped by `64 MB` with formatted `"512 MB"` label, and `Save local crash report bundles` toggle defaulting to `off`). Done when: toggling **Portable mode** calls `AppPaths::set_portable_mode` (§1), migrates `settings.json` and `recents.json` on disk, and updates the displayed data folder path immediately (or displays an inline warning and reverts the toggle if `<exe-dir>` is read-only), and moving the `Page cache` slider immediately updates `TileTextureCache::stats().budget_bytes` on open tabs.
2. Build the **DEFAULT PDF APP** row (`"Make Recto your default PDF viewer"` with `"Open Windows settings"` button calling `Launcher::LaunchUriAsync(Uri("ms-settings:defaultapps"))`) and the **ABOUT** navigation row (`"About Recto"` showing version string and navigating to the About view, placeholder frame until `D07 T02 §1` lands). Done when: every SPEC 9.7 group and row is complete on `SettingsPage` and searchable via `"Find a setting"`.

- [ ] Build the **SIGNATURES AND SECURITY** group card with `Verify signatures when opening` (`ToggleSwitch`, default `true`) and `Default timestamp server URL` (`TextBox` with `https://` validation) bound to `SettingsStore` (§2). Done when: both rows persist and filter under `"signature"` or `"timestamp"` in `"Find a setting"`.
- [ ] Build the **PERFORMANCE AND PRIVACY** group card featuring the **Portable mode** `ToggleSwitch` (`"Store settings, recent files, cache and temporary files in a data folder next to Recto.exe"`), live resolved data path caption (`AppPaths::data_dir()`), and `"Open folder"` button. Done when: toggling **Portable mode** executes `AppPaths::set_portable_mode` (§1), migrates `settings.json` and `recents.json`, and updates the path caption live. Cheaper substitute that fails the checkpoint: a toggle that writes a flag without migrating files or fails silently when `<exe-dir>` is read-only.
- [ ] Display an inline `InfoBar` / warning message with icon and text if the user toggles **Portable mode** on while `Recto.exe` resides in a non-writable directory (such as `C:\Program Files\Recto`), reverting the toggle to `Off` cleanly. Done when: read-only directory refusal is surfaced clearly in the UI without crashing.
- [ ] Wire the `Page cache` slider (`128 MB` to `2048 MB`, default `512 MB`) to `SettingsStore` and broadcast the new byte budget (`mb * 1024 * 1024`) to every active `TileTextureCache` (`D02 T02 §3`). Done when: moving the slider from `512 MB` to `256 MB` immediately updates `TileTextureCache::stats().budget_bytes` to `268,435,456`.
- [ ] Wire the `Save local crash report bundles` `ToggleSwitch` (default `off` per SPEC 9.7 and D16) to `SettingsStore`. Done when: the setting defaults to `false` and persists across restarts.
- [ ] Build the **DEFAULT PDF APP** row launching `ms-settings:defaultapps` via `Windows::System::Launcher::LaunchUriAsync` and the **ABOUT** row navigating to the About view (`D07 T02 §1`). Done when: all 8 SPEC 9.7 top-level groups/cards exist on `SettingsPage` in the exact SPEC order.
- [ ] Add `Settings.SecurityPrivacyPortableAndCache` test in `tests/recto-tests/` driving the UI **Portable mode** toggle (verifying file migration on disk and read-only folder fallback), the `Page cache` slider (verifying live `TileTextureCache` budget telemetry change), and `"Find a setting"` coverage across all 8 groups. Done when: `recto-tests.exe --gtest_filter=Settings.SecurityPrivacyPortableAndCache` passes.
- [ ] Commit: `viewer: complete SettingsPage with Signatures, Portable mode, Cache slider, Default App and About`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Settings.SecurityPrivacyPortableAndCache` passes on the display-session host, **Portable mode** toggle migrates files live, `Page cache` slider updates `TileTextureCache` telemetry live, and screenshot matches `D-Settings.png`.

## 7. Live theme switching (`Light`, `Dark`, `Follow Windows`), Mica Alt and HighContrast

Implement live application theming and high-contrast accessibility across `ShellPage` and all viewer surfaces (`design/SPEC.md` sections 4, 9.7 and 11): switching `App theme` between `Light`, `Dark`, and `Follow Windows` updates `FrameworkElement::RequestedTheme`, `AppWindowTitleBar` caption button colours, `MicaController` (`MicaKind::BaseAlt` backdrop with `RectoWindowBrush` fallback), open `ContentDialog`s, theme-switched `SvgImageSource` assets (`*-light.svg` / `*-dark.svg` / `*-on-dark.svg`), and `DocumentView`'s canvas clear colour and dark page colour mode (`D02 T02 §10`) immediately without restarting. Verify that `HighContrast` theme dictionaries map every `Recto*Brush` to system `SystemColor*` colours and that zero surfaces rely on colour alone to convey state.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 3.1, 3.3, 3.4, 4 (`RectoColors.xaml`, `Mica Alt`, `HighContrast`, `Pages in dark theme`), 9.7 (`Theme` group), and 11 ("Colour is never the only signal") with `design/screenshots/D-Focus-Light.png`, `D-Focus-Dark.png`, and `D-ReadingRoom.png`.
**Job:** The user can switch between Light, Dark, and Follow Windows themes (or use a Windows High Contrast accessibility theme) and have every title bar, toolbar, panel, icon, SVG illustration, dialog, and page canvas update live in a single frame. Consumer: every UI surface in `src/app/`.
**Treatment:** Central `ThemeManager` (`src/app/ThemeManager.h/.cpp`) subscribed to `SettingsStore` (§2) and `Windows::UI::ViewManagement::UISettings::ColorValuesChanged` plus `AccessibilitySettings::HighContrastChanged`, configuring `SystemBackdrop` (`DesktopAcrylicController` / `MicaController` `MicaKind::BaseAlt`) and updating `RequestedTheme` on root elements and open popups/dialogs. Cheaper substitute that fails the checkpoint: requiring an app restart to apply theme changes, or leaving title-bar caption buttons or SVG graphics stuck in the initial startup theme.
**Chrome:** consume `design/tokens/RectoColors.xaml` (`Light`, `Dark`, `HighContrast` dictionaries) and `resources/svg/` theme pairs.

**Build order:**

1. Implement `ThemeManager` (`src/app/ThemeManager.h/.cpp`): configure `MicaController` (`MicaKind::BaseAlt`) with fallback to solid `RectoWindowBrush` when Mica is unsupported or disabled; apply `ElementTheme::Light`, `ElementTheme::Dark`, or `ElementTheme::Default` (`Follow Windows`) to `ShellPage` and any open `ContentDialog` / `Popup` roots in `VisualTreeHelper::GetOpenPopupsForXamlRoot`; update `AppWindowTitleBar` button foreground/hover colours; notify `PresentationRenderer` (`D02 T02 §§1, 10`) to update the swap-chain clear colour and active `PageColorMode`; and subscribe to `UISettings::ColorValuesChanged` so OS-level theme flips re-theme Recto live when `Follow Windows` is active. Done when: flipping `Light -> Dark -> Follow Windows` in Settings and simulating an OS theme change updates the window, caption buttons, SVG wordmark/illustrations, open dialogs, and canvas clear colour immediately with zero restart.
2. Execute the SPEC section 11 `HighContrast` and non-colour-signal audit across all M2 surfaces: verify `HighContrast` resource resolution in `RectoColors.xaml`, confirm selected thumbnails carry the vermilion ribbon marker (`D02 T03 §4`), selected preset cards carry the check badge (§4), active outline items carry the `3 epx` bar (`D02 T03 §5`), and password/error states carry warning icons + text (`D02 T01 §§10, 11`). Done when: `Settings.ThemeAndHighContrast` verifies every `Recto*Brush` resolves in `Light`, `Dark`, and `HighContrast` and every stateful control exposes a non-colour indicator.

- [ ] Author `src/app/ThemeManager.h/.cpp` applying `MicaKind::BaseAlt` backdrop (with `RectoWindowBrush` fallback) and live `RequestedTheme` updates across `ShellPage` and all open XAML popups/dialogs. Done when: switching `Light <-> Dark` while a menu or dialog is open re-themes both the shell and the popup immediately. Cheaper substitute that fails the checkpoint: applying theme only at app launch or forgetting `AppWindowTitleBar` caption buttons.
- [ ] Subscribe to `Windows::UI::ViewManagement::UISettings::ColorValuesChanged` and `AccessibilitySettings::HighContrastChanged` (marshaled to `MainWindow`'s `DispatcherQueue`) so `Follow Windows` and OS High Contrast switches apply live. Done when: changing the OS theme while `Follow Windows` is selected updates Recto in `< 100 ms`.
- [ ] Verify that all theme-dependent SVG graphics (`wordmark-{light,dark}.svg`, `app-icon-small{-on-dark}.svg`, `drop-files-{light,dark}.svg`, `ribbon-marker{-on-dark}.svg`, `layout-*-{light,dark}.svg`) switch sources automatically when the effective theme changes. Done when: inspecting `SvgImageSource::UriSource` after a `Light -> Dark` flip confirms every `-dark` / `-on-dark` asset is loaded.
- [ ] Synchronize `PresentationRenderer` (`D02 T02 §§1, 10`) with `ThemeManager`: in `Light` theme, pages render `Original` on `#E6E7EA` canvas; in `Dark` theme, pages apply the user's `Pages in dark theme` setting (`Dimmed` default, `Original`, or `Inverted`) on `#141312` canvas. Done when: switching to `Dark` theme dims the open PDF page to `#D9D5CE` and darkens the canvas to `#141312` in one frame.
- [ ] Verify `HighContrast` dictionary completeness in `design/tokens/RectoColors.xaml` and confirm that in `HighContrast` mode `PresentationRenderer` disables `Dimmed` tinting so page contrast is never degraded. Done when: every brush key in `RectoColors.xaml` resolves in `Light`, `Dark`, and `HighContrast` without fallback errors.
- [ ] Audit all M2 selection and error states for SPEC section 11 non-colour signals (ribbon marker on active thumbnail, check badge on active preset card, `3 epx` bar on active outline item, lock/warning icons and text on dialogs). Done when: zero state indicators rely solely on colour differences.
- [ ] Add `Settings.ThemeAndHighContrast` test in `tests/recto-tests/` exercising live `Light`, `Dark`, `Follow Windows`, and `HighContrast` transitions, SVG URI switching, caption button re-theming, and swap-chain clear/page-colour updates. Done when: `recto-tests.exe --gtest_filter=Settings.ThemeAndHighContrast` passes.
- [ ] Commit: `viewer: implement live ThemeManager, Mica Alt backdrop and HighContrast compliance`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Settings.ThemeAndHighContrast` passes on the display-session host, and rendered screenshots in `Light`, `Dark`, OS-flip, and `HighContrast` match the SPEC boards.

## 8. Motion system (`167 ms` Fluent transitions) and Windows reduced-motion compliance

Implement the centralized motion helper (`src/app/MotionHelper.h/.cpp`) governing all transitions specified in `design/SPEC.md` sections 11 and 12: floating panels, side panels, contextual mode bars, and the Command Box card animate entrance and exit with a `167 ms` fade and `8 epx` translation using the standard Fluent cubic-bezier easing curve; dialogs use stock `ContentDialog` motion; page scrolling has zero artificial page-turn animations; and when Windows `"Show animations in Windows"` (`UISettings::AnimationsEnabled`) is turned off, all panel/card transitions complete in `0 ms` and the Floating Page Toolbar (`D02 T02 §8`) never fades to `40%` opacity.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (toolbar 3 s fade to 40%), 11 ("Respect 'Show animations in Windows'; when off, the toolbar does not fade and panels appear without motion"), and 12 ("Panels and cards: fade and translate 8 epx over 167 ms with the standard Fluent easing").
**Job:** Transitions feel crisp and responsive (`167 ms`) when animations are enabled, and disappear completely (`0 ms`, opaque toolbar) for users who disable animations in Windows Accessibility settings. Consumer: `PageToolbar` (`D02 T02 §8`), `SidePanelHost` (`D02 T03 §3`), and `CommandBoxControl` (`D02 T03 §12`).
**Treatment:** Single `MotionHelper` service wrapping `UISettings::AnimationsEnabled()` (with a test override hook `set_animations_enabled_for_test(std::optional<bool>)`) and providing standard `167 ms` + `8 epx` Composition / Storyboard helpers that short-circuit to instant state assignment when animations are disabled. Cheaper substitute that fails the checkpoint: hard-coding XAML `Storyboard` durations that ignore `UISettings::AnimationsEnabled()`.
**Chrome:** consume `MotionHelper` across `PageToolbar`, `SidePanelHost`, `CommandBoxControl`, and contextual mode bars.

**Build order:**

1. Author `src/app/MotionHelper.h/.cpp` querying `Windows::UI::ViewManagement::UISettings().AnimationsEnabled()` (with a deterministic test seam `MotionHelper::set_test_override(std::optional<bool>)`), exposing `duration_ms()` (`167 ms` when enabled, `0 ms` when disabled) and `translation_epx()` (`8.0f` when enabled, `0.0f` when disabled), and providing `animate_card_enter(UIElement, offset_x, offset_y)` and `animate_card_exit(UIElement, offset_x, offset_y, on_complete)` using standard Fluent deceleration easing (`CubicBezier(0.1, 0.9, 0.2, 1.0)`). Done when: unit tests verify `167 ms` / `8 epx` parameters when enabled and `0 ms` / `0 epx` instant completion when disabled.
2. Wire `MotionHelper` into `SidePanelHost` (`D02 T03 §3`), `CommandBoxControl` (`D02 T03 §12`), and `PageToolbar`'s 3-second reading auto-fade (`D02 T02 §8`). Done when: with animations enabled, panels and `Ctrl+K` fade/slide over `167 ms` and `PageToolbar` fades to `0.4` after 3 seconds; with animations disabled via `MotionHelper::set_test_override(false)`, panels and `Ctrl+K` appear in `0 ms` and `PageToolbar` stays at `1.0` opacity indefinitely.

- [ ] Author `src/app/MotionHelper.h/.cpp` wrapping `UISettings::AnimationsEnabled()` with a test-override seam and standard SPEC 12 constants (`167 ms` duration, `8 epx` translation, Fluent cubic-bezier easing). Done when: `MotionHelper` compiles and exposes deterministic state to all viewer controls.
- [ ] Apply `MotionHelper::animate_card_enter` and `animate_card_exit` to `SidePanelHost` (`D02 T03 §3`) when opening/closing in floating overlay slots (`8 epx` horizontal slide + fade over `167 ms`). Done when: toggling `Ctrl+B` in `Focus` mode animates over `167 ms` when animations are on and snaps in `0 ms` when animations are off.
- [ ] Apply `MotionHelper::animate_card_enter` and `animate_card_exit` to `CommandBoxControl` (`D02 T03 §12`) in `Ctrl+K` overlay mode (`8 epx` vertical slide + fade over `167 ms`). Done when: `Ctrl+K` animates over `167 ms` when enabled and snaps in `0 ms` when disabled.
- [ ] Gate `PageToolbar`'s 3-second idle opacity fade (`D02 T02 §8`) through `MotionHelper::are_animations_enabled()` so turning off OS animations immediately restores and locks `PageToolbar.Opacity = 1.0`. Done when: `PageToolbar.Opacity` remains `1.0` after 5+ seconds of idle time when animations are disabled. Cheaper substitute that fails the checkpoint: fading the toolbar even when Windows `"Show animations"` is turned off.
- [ ] Verify that page scrolling and page navigation (`D02 T02 §6`) use zero artificial page-turn animations per SPEC section 12. Done when: `Page Down` and `go_to_page` transition without synthetic page-curl or slide delays.
- [ ] Add `Settings.MotionAndReducedMotion` test in `tests/recto-tests/` driving `SidePanelHost`, `CommandBoxControl`, and `PageToolbar` with `MotionHelper::set_test_override(true)` and `set_test_override(false)`. Done when: `recto-tests.exe --gtest_filter=Settings.MotionAndReducedMotion` passes.
- [ ] Commit: `viewer: add 167 ms Fluent MotionHelper and Windows reduced-motion compliance`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Settings.MotionAndReducedMotion` passes on the display-session host, verifying both `167 ms` / `8 epx` transitions and `0 ms` / opaque-toolbar reduced-motion compliance.

## 9. Complete SPEC section 10 keyboard accelerator bindings and `Escape` layer stack

Bind every row of the `design/SPEC.md` section 10 keyboard shortcut table through `CommandRegistry` (`D02 T03 §11`) and implement the deterministic `EscapeLayerManager` (`src/app/commands/EscapeLayerManager.h/.cpp`) so pressing `Esc` always closes the topmost open surface in strict priority order: (1) active modal `ContentDialog` / flyout / context menu, (2) `Ctrl+K` Command Box overlay, (3) `Ctrl+F` / `Ctrl+H` Find & Replace card, (4) active contextual mode bar (`Markup`, `Form Designer`, `Redaction`, `Extractor`), (5) floating side panel in `Focus` preset, and (6) `F11` Full Screen mode.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 10 (complete 21-row Keyboard table) and 11 (logical focus order and focus visuals).
**Job:** A keyboard user can drive every single operation in Recto using the exact shortcuts documented in SPEC section 10, without stock WinUI control keys swallowing document shortcuts or `Esc` closing the wrong layer. Consumer: `ShellPage`, `DocumentView`, `CommandRegistry` (`D02 T03 §11`), and the keyboard audit (§10).
**Treatment:** Root `KeyboardAccelerator` / `PreviewKeyDown` routing on `ShellPage` backed by `CommandRegistry` and `EscapeLayerManager`, with explicit focus-context guards so typing inside a `TextBox` / `PasswordBox` (such as `"Find a setting"`, `Ctrl+K`, `Ctrl+G`, or a form field) never triggers single-key document navigation (`Home`, `End`, `Page Up`, `Page Down`, `Space`). Cheaper substitute that fails the checkpoint: scattered ad-hoc `KeyDown` handlers that conflict when focus is inside a text box, or `Esc` exiting Full Screen while the `Ctrl+K` Command Box is still open on top.
**Chrome:** consume `CommandRegistry` (`D02 T03 §11`) and stock WinUI focus visuals in `RectoAccentBrush`.

**Build order:**

1. Bind all 21 rows (33 distinct key combinations) from `design/SPEC.md` section 10 in `CommandRegistry` and `ShellPage`: `Ctrl+O` (Open file); `Ctrl+K` (Command box); `Ctrl+F`, `F3`, `Shift+F3` (Find, next match, previous match); `Ctrl+H` (Find and replace text); `Ctrl+P` (Print); `Ctrl+S`, `Ctrl+Shift+S` (Save changes, Save a copy); `Ctrl+Z`, `Ctrl+Y` (Undo, Redo); `Ctrl+Shift+A` (Toggle Markup and Annotation bar); `Ctrl+Shift+X` (Toggle Redaction and Sanitize bar); `Ctrl+Shift+G` (Sign document); `Ctrl+Shift+D` (Compare PDFs); `Ctrl+W`, `Ctrl+Tab`, `Ctrl+Shift+Tab` (Close tab, next tab, previous tab); `Ctrl+Plus` (`VK_OEM_PLUS` and `VK_ADD`), `Ctrl+Minus` (`VK_OEM_MINUS` and `VK_SUBTRACT`) (Zoom in, zoom out); `Ctrl+0` (`Number0` and `NumberPad0`), `Ctrl+1` (`Number1` and `NumberPad1`) (Fit width, actual size); `Ctrl+R`, `Ctrl+Shift+R` (Rotate view right, left); `Ctrl+G` (Go to page); `Home`, `End`, `Page Up`, `Page Down` (First page, last page, previous screen, next screen when focus is not inside a text input or list control); `Ctrl+B` (Show or hide the side panel); `Ctrl+,` (`VK_OEM_COMMA`, Settings); `F11` (Full screen); and `Esc` (Close topmost card, mode bar, menu, dialog, or full screen). Done when: `CommandRegistry::dump_shortcuts()` enumerates all 21 SPEC section 10 rows with zero missing or colliding bindings.
2. Implement `EscapeLayerManager` (`src/app/commands/EscapeLayerManager.h/.cpp`) where open surfaces register/push dismissal callbacks by priority tier (`DialogOrFlyout > CommandBox > FindCard > ContextualModeBar > FloatingSidePanel > FullScreen`), and pressing `Esc` pops and closes strictly the highest-priority active surface. Done when: opening Full Screen (`F11`), opening the floating side panel (`Ctrl+B`), opening the Command Box (`Ctrl+K`), and pressing `Esc` three times closes `Ctrl+K` first, then the floating side panel second, and exits Full Screen third.

- [ ] Bind all 21 rows (33 key combinations including main-keyboard and numpad `+`, `-`, `0`, `1`) of `design/SPEC.md` section 10 in `CommandRegistry` (`D02 T03 §11`) and `ShellPage`. Done when: every row in SPEC section 10 maps to a registered `CommandId`.
- [ ] Guard bare-key navigation accelerators (`Home`, `End`, `Page Up`, `Page Down`, `Space`) so they move the document viewport when focus is on `DocumentView` or toolbar buttons, but let `TextBox`, `PasswordBox`, `NumberBox`, `TreeView`, and `ComboBox` controls handle cursor/list movement natively when focused. Done when: pressing `Home` inside `"Find a setting"` moves the text caret to the start of the query instead of scrolling an open PDF tab to page 1. Cheaper substitute that fails the checkpoint: global `Home`/`End` handlers that break text editing in inputs.
- [ ] Intercept `Ctrl+Tab` and `Ctrl+Shift+Tab` at the `ShellPage` `PreviewKeyDown` level so tab switching works reliably even when focus is inside a child panel or toolbar. Done when: pressing `Ctrl+Tab` from any focused child control switches to the next document tab.
- [ ] Author `src/app/commands/EscapeLayerManager.h/.cpp` managing the 6-tier `Esc` dismissal stack (`Dialog/Flyout -> CommandBox -> FindCard -> ContextualModeBar -> FloatingSidePanel -> FullScreen`). Done when: `EscapeLayerManager::try_dismiss_topmost()` closes exactly one layer per `Esc` press in priority order.
- [ ] Ensure that in `Classic` and `Reading room` presets where `SidePanelHost` is docked (`LeftDockSlot` / `RightDockSlot`) rather than a floating card, pressing `Esc` does not unexpectedly collapse the permanent docked pane (closing it only via `Ctrl+B` or its close button). Done when: `Esc` closes floating cards in `Focus` while leaving docked panes in `Classic`/`Reading room` stable.
- [ ] Verify that stock WinUI focus visuals use `RectoAccentBrush` (`SystemControlFocusVisualPrimaryBrush`) with `2 epx` outer thickness across all controls per SPEC section 11. Done when: keyboard `Tab` focus rings render in vermilion across Light and Dark themes.
- [ ] Add `Keyboard.BindingsAndEscapeStack` test in `tests/recto-tests/` verifying numpad + main-keyboard zoom/fit keys, text-input focus guards for `Home`/`End`, and multi-layer `Esc` unwinding (`Ctrl+K -> Floating SidePanel -> FullScreen`). Done when: `recto-tests.exe --gtest_filter=Keyboard.BindingsAndEscapeStack` passes.
- [ ] Commit: `viewer: bind all SPEC section 10 keyboard shortcuts and implement EscapeLayerManager`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Keyboard.BindingsAndEscapeStack` passes on the display-session host, proving all 33 key combinations, text-box focus guards, and 6-tier `Esc` unwinding.

## 10. End-to-end keyboard audit harness (`Keyboard.Audit`) and `docs/keyboard-shortcuts.md`

Build the repeatable table-driven keyboard audit suite (`Keyboard.Audit` in `tests/recto-tests/`) and author `docs/keyboard-shortcuts.md` (which the About page links to in `D07 T02 §1`): synthesize every SPEC section 10 shortcut in the running app and assert its observable effect (or its registered deferred target for M3-M6 commands), verify logical `Tab` focus traversal order (`Title bar -> Toolbar -> Active mode bar -> Side panel -> Page`), prove the audit can fail by unbinding a shortcut in a negative test case, and verify that `docs/keyboard-shortcuts.md` agrees line for line with `CommandRegistry` (§11).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `docs/keyboard-shortcuts.md` documenting every shortcut in `design/SPEC.md` section 10 grouped by category (`File & Tabs`, `Navigation & View`, `Search & Text`, `Tools & Authoring`, `Shell & Accessibility`), and implement a generator/verifier helper in `CommandRegistry` so `docs/keyboard-shortcuts.md` and `CommandRegistry::dump_shortcuts()` are verified byte-consistent by test. Done when: any mismatch between `CommandRegistry` and `docs/keyboard-shortcuts.md` fails the test with the exact differing line.
2. Implement `Keyboard.Audit` and `Keyboard.AuditFailsOnMissingBinding` in `tests/recto-tests/`: iterate through all 21 SPEC section 10 rows in the live shell, invoke each accelerator, assert the state transition (tab cycle, zoom step, rotation, fit width, actual size, go-to-page focus, side panel toggle, Command Box toggle, Settings open, Full Screen toggle, `Esc` layer close), traverse the `Tab` focus ring (`TitleBarSlot -> TopBarSlot/BottomOverlaySlot -> SidePanelHost -> DocumentView`), and verify that temporarily removing a binding causes the audit assertions to fail. Done when: `recto-tests.exe --gtest_filter=Keyboard.*` passes in full.

- [ ] Author `docs/keyboard-shortcuts.md` containing the complete SPEC section 10 keyboard shortcut reference table formatted cleanly in GitHub-flavored Markdown (no em dashes or en dashes). Done when: `docs/keyboard-shortcuts.md` covers all 21 SPEC section 10 rows and matches `CommandRegistry`.
- [ ] Implement `CommandRegistry::verify_markdown_table(path)` comparing the registered shortcut bindings against `docs/keyboard-shortcuts.md`. Done when: `Keyboard.MarkdownSync` confirms 100% agreement between code and documentation.
- [ ] Build the table-driven `Keyboard.Audit` test in `tests/recto-tests/` exercising every SPEC section 10 shortcut against a running `ShellPage` with two open corpus tabs and asserting each shortcut's state change. Done when: all 21 rows execute and log their verified state transitions. Cheaper substitute that fails the checkpoint: checking static enum tables without driving the UI shell.
- [ ] Implement `Keyboard.FocusOrderTraversal` in `tests/recto-tests/` stepping `Tab` and `Shift+Tab` through the shell and asserting the SPEC section 11 focus order (`TitleBarSlot -> Toolbar -> SidePanel -> DocumentView`) with zero focus traps. Done when: a full `Tab` cycle returns cleanly to the first title bar control.
- [ ] Implement `Keyboard.AuditFailsOnMissingBinding` in `tests/recto-tests/` which unregisters one shortcut (`Ctrl+R`) in a scoped test instance, runs the audit verifier, and asserts that the verifier fails naming `Ctrl+R`, then restores it. Done when: the negative test proves the audit checkpoint is falsifiable.
- [ ] Quote the complete `Keyboard.Audit` table output in the section review findings. Done when: all `Keyboard.*` tests pass with 0 warnings.
- [ ] Commit: `viewer: add end-to-end Keyboard.Audit suite and docs/keyboard-shortcuts.md`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Keyboard.*` passes on the display-session host (including `Keyboard.Audit`, `Keyboard.MarkdownSync`, `Keyboard.FocusOrderTraversal`, and `Keyboard.AuditFailsOnMissingBinding`).

## Verification

- All three layout presets (`Focus`, `Classic`, `Reading room`) plus `"Custom"` rendered against `D-Focus-Light.png`, `D-Classic.png`, `D-ReadingRoom.png`, and `D-Layout.png`, with document state preserved across live preset switches.
- Every `design/SPEC.md` section 9.7 setting row (including **Portable mode** live migration and `Page cache` budget slider) and every section 10 keyboard shortcut accounted for and proven by `Settings.*` and `Keyboard.*` tests.
- Full `recto-tests` suite green; `python scripts/todo-graph.py validate` passes.
