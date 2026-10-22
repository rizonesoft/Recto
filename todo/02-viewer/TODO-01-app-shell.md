---
schema_version: 1
id: app-shell
domain: 02-viewer
status: active
title: "TODO-01 -- App Shell, Tabs, Menus, File Open/Save and Shell Dialogs"
track: M2
---

# TODO-01 -- App Shell, Tabs, Menus, File Open/Save and Shell Dialogs

> **Goal:** The `recto-ui` app exists as the SPEC's one shell: a self-contained unpackaged and MSIX-ready Win32 executable with the generated design tokens, a custom title bar with document tabs and drag regions, named layout slots for all three presets, the Classic menu bar (`File`, `Edit`, `View`, `Tools`, `Window`, `Help`) and docked top command bar, all four file-opening pipelines (`Ctrl+O` picker, drag-and-drop with overlay, CLI arguments, and Explorer file activation with single-instance forwarding), file saving (`Save`, `Save a copy`, `Replace the original?`, and `Save your changes first?`), and the Password and Could-not-open dialogs per `design/SPEC.md` sections 8, 9.1, 9.2, 9.9, 10 and 11.

> **Current state:** Measured 2026-10-14. `D00 T01 §3` (stamped) created the compilable original scaffold (`src/app/Recto.vcxproj` and its application and window sources), which `D10 T01 §2` replaces with the Win32 `Recto.exe` host. `D01` provides `DocumentHandle` open, authenticate, incremental save and describe. All visual assets exist in `resources/` (`resources/msix/Assets/`, `resources/ico/`, `resources/svg/`) and all colour tokens live in `design/tokens/colors.json`, which `D10 T01 §8` generates into `src/ui/generated/recto_tokens.g.h`. Measured 2026-10-21: `resources/fonts/`, `scripts/build-icon-font.py`, any `RectoChipStyle` and any `ClassicStatusBar` do not exist yet, although SPEC sections 6, 7 and 8.4 (revised 2026-10-21) require them; §18 to §20 build them. 2026-10-22: WinUI 3 dropped for `recto-ui` (foundation plan D4); sections rewritten against `todo/10-ui/`

## Inputs

- `design/SPEC.md` sections 3 (brand and assets), 4 (colour), 5 (typography), 6 (icons), 7 (shape, spacing, elevation, 960x600 minimum window size), 8 and 8.1 (shell, named slots and title bar), 9.1 (drop overlay), 9.2 (loading, password), 9.6 and 9.9 (dialogs: Password, Could not open, Replace original, Unsaved changes), 10 (keyboard), 11 (accessibility); `design/tokens/colors.json` and the generated `src/ui/generated/recto_tokens.g.h`.
- `resources/README.md` (asset mapping for `Package.appxmanifest` and unpackaged icons) and `resources/svg/` (wordmark, app icons, drop-files illustrations).
- -> XREF: D01 T01 §1 -- `recto::pdf::diagnostics_tail()` and `PdfError` ring buffer consumed by `CouldNotOpenDialog`'s `Copy error details` in §11 here.
- -> XREF: D01 T02 §4 -- `DocumentActor` open/authenticate (`§4`), incremental save (`§5`), and `Runtime` shutdown (`§10`) consumed by §3, §5, §8, and §10 here.
- -> XREF: D02 T04 §3 -- the layout preset engine moves controls between the named slots declared in §4 here.
- -> XREF: D04 T01 §7 -- `QueueCard` and `ConversionRunner` reuse §10's `PasswordDialog` for encrypted conversion jobs and §4's overlay slot for the floating queue card.
- -> XREF: D04 T02 §1 -- `PdfWriter` and `AtomicFileWriter` reuse §10's shared dialog presets (`RectoDialogStyle`).
- -> XREF: D05 T02 §3 -- `OrganizePage` Combine mode and `§5` Unsaved-Changes / Replace-Original dialogs reuse §10's `PasswordDialog` and dialog presets (`RectoDialogStyle`).
- -> XREF: D06 T01 §7 -- `PerfSmoke` (`D06 T01 §7`) exercises the viewer shell open/dialog paths (`§7`, `§11`) built here.
- -> XREF: D07 T01 §4 -- packaging completes what §1 here starts: the Win32 manifest and assets authored here are packaged into the Inno Setup installer, Portable `.zip` (`§5`) and optional MSIX (`§6`), a full-trust desktop package with no UI framework runtime.
- -> XREF: D06 T03 §5 -- the save integrity fault-injection suite gating §8's Save, Save As and Replace-Original paths and §12's recovery snapshots.
- -> XREF: D03 T04 §14 -- Edit PDF sessions commit through these save paths.
- -> XREF: D04 T02 §15 -- opens PDFs created from the clipboard (D04 T02 §15).
- -> XREF: D07 T01 §17 -- /virtual-print launch arg.
- -> XREF: D04 T02 §25 -- new tab path.
- -> XREF: D04 T04 §22 -- open dialog accepts .ofd.
- -> XREF: D05 T02 §30 -- signed version tab opens normally.
- -> XREF: D02 T04 §5 -- restore tabs setting.
- -> XREF: D02 T06 §12 -- session exclusion of cloud copies.
- -> XREF: D01 T02 §14 -- SavePipeline writes the exported revision and marks it saved.
- -> XREF: D03 T02 §4 -- the Classic keyboard order test reaches the `ClassicStatusBar` that §20 builds.
- -> XREF: D05 T02 §1 -- the Organize grid's selected-card check badge uses §19's `RectoChipStyle`.
- -> XREF: D02 T04 §4 -- the Settings preset cards' `Default` and `"Custom"` badges use §19's `RectoChip`.
- -> XREF: D02 T03 §3 -- the side-panel switcher's count badges use §19's `RectoChipStyle`; the command box keycaps of `D02 T03 §12` do too.
- -> XREF: D06 T04 §24 -- the `D-Session.png` board and `design/SPEC.md` section 9.18 that §12, §14, §15, §16, §17 here build against.
- -> XREF: D10 T01 §2 -- the Win32 `Recto.exe` host that §1 configures, and the single-instance hand-off of §7.
- -> XREF: D10 T01 §4 -- `ui::Window` frame, DPI, minimum size, icon and close request used by §1, §7 and §8.
- -> XREF: D10 T01 §8 -- generated `recto_tokens.g.h` and `ui::ThemeManager` consumed by every section here.
- -> XREF: D10 T01 §11 -- `ui::Icon` loads the `RectoIcons.ttf` that §18 builds; `ui::SvgImage` draws the brand and drop assets.
- -> XREF: D10 T01 §19 -- non-client hit testing, drag regions and Snap Layouts for the title bar of §2 and §16.
- -> XREF: D10 T02 §13 -- the document tabs of §3, §15 and §16 build on `ui::TabStrip`.
- -> XREF: D10 T02 §14 -- tab and menu popups of §9 and §15 build on `ui::Menu`; the Classic menu bar of §9 on `ui::MenuBar` (D10 T02 §15).
- -> XREF: D10 T02 §17 -- every dialog of §8, §10, §11, §12 and §17 builds on `ui::Dialog`.
- -> XREF: D10 T02 §20 -- the pill chips of §19 and the in-app toasts of §14 and §15 build on `ui::Chip`, `ui::Badge`, `ui::Keycap` and `ui::Toast`.
- -> XREF: D10 T03 §1 -- automation names, tooltips and the UIA providers every section here relies on.
- -> XREF: D10 T03 §7 -- every visible string in §2 to §20 loads through `ui::Strings`.

## Outcome

- `Recto.exe` launches directly from the build output folder (self-contained unpackaged and MSIX-ready) with the generated tokens (Light, Dark and HighContrast sets from `design/tokens/colors.json`), vermilion accent, window icon, and `960 x 600 epx` minimum window size.
- `ShellPage` hosts the custom title bar (icon, wordmark, Home tab, document `ui::TabStrip` with dirty indicator and actor teardown, command box trigger, settings button, caption buttons with Snap Layouts), the eight named layout slots, and the Classic menu bar (`File`, `Edit`, `View`, `Tools`, `Window`, `Help`).
- PDFs open through the `Ctrl+O` picker, full-window drag-and-drop, command-line arguments, and Explorer file activation (redirecting to the primary running instance) into tabs over one shared `OpenDocumentPipeline`, and modified documents save through `Ctrl+S` (incremental or atomic replace) and `Ctrl+Shift+S` (`Save a copy`) with `Replace the original?` and `Save your changes first?` prompts.
- Password-protected and damaged PDFs trigger the SPEC 9.9 `PasswordDialog` and `CouldNotOpenDialog` (with diagnostic ring-buffer `Copy error details`) built on the shared `RectoDialogStyle` over `ui::Dialog`.
- After the owner's decision, unsaved documents keep timer-driven recovery snapshots and are offered for restore after an abnormal exit, never overwriting the originals (`§12`).
- Open tabs and their view state persist in a local `session.json` and reopen on the next launch (`RestoreTabsOnLaunch`), closed tabs come back with `Ctrl+Shift+T`, tabs can be closed in batches and reordered by drag or keyboard, and the open file can be shared through the Windows Share contract without Recto opening any network connection (§13 to §17).
- Every interface glyph comes from the generated subset font `resources/fonts/RectoIcons.ttf` (exactly the SPEC section 6 glyphs, stable Private Use Area code points) drawn by `ui::Icon`, every chip, keycap, badge and counter uses the pill `RectoChipStyle`, and the Classic preset shows the SPEC 8.4 status bar (§18 to §20).

**Adjacency:** list=applicable @ D02 T03 §1; document=applicable @ D02 T01 §8; settings=applicable @ D02 T04 §4; reporting=not-applicable (no reports in the application shell; diagnostic error text is copied via §11); notifications=applicable @ D04 T01 §6; permissions=not-applicable (single-user desktop app; document-level link and action refusals live in D02 T02 §9); audit=not-applicable (no persistent audit log over shell window state); exchange=not-applicable (clipboard and share paths belong to D03 T01 §2); reverse=applicable @ D02 T01 §3

**Adjacency rationale:** The shell's document surface is saving modified PDFs (`Save` and `Save a copy` in §8, while printing lives in D07 T01 §1), and its reverse surface is closing a tab with full `DocumentActor` teardown (`§3`) or discarding unsaved edits (`§8`). Recent files (`D02 T03 §1`), settings (`D02 T04 §4`) and conversion notifications (`D04 T01 §6`) are hosted by the shell and owned by their respective files.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | App project, unpackaged/MSIX manifest, assets, tokens and minimum window size | D00 T01 §3, D10 T01 §2, D10 T01 §4, D10 T01 §8 | [ ] |
| 2 | §2 | Custom title bar, brand lockup, drag regions and caption buttons | §1, §18, §19, D10 T01 §11, D10 T01 §19, D10 T02 §2, D10 T02 §3, D10 T03 §1, D10 T03 §5, D10 T03 §7 | [ ] |
| 3 | §3 | Document `ui::TabStrip`, tab session lifecycle, dirty state and actor teardown | §2, D01 T02 §10, D10 T01 §3, D10 T01 §12, D10 T01 §16, D10 T02 §13 | [ ] |
| 4 | §4 | Named shell layout slots, Reading room mode switcher and Classic navigation rail | §2, D10 T01 §6, D10 T01 §7, D10 T01 §9, D10 T01 §13, D10 T02 §18, D10 T02 §19 | [ ] |
| 5 | §5 | File Open Picker (`Ctrl+O`) and shared `OpenDocumentPipeline` | §3, D01 T02 §4, D10 T01 §3 | [ ] |
| 6 | §6 | Drag-and-drop file opening and full-window drop overlay | §5, D10 T01 §11, D10 T01 §15, D10 T01 §17 | [ ] |
| 7 | §7 | Command-line arguments, Explorer `.pdf` activation and single-instance forwarding | §5, D10 T01 §3, D10 T01 §4 | [ ] |
| 8 | §8 | Save (`Ctrl+S`), Save As (`Ctrl+Shift+S`), Replace-Original and Unsaved-Changes dialogs | §3, §5, D01 T02 §5, D10 T01 §4, D10 T02 §17 | [ ] |
| 9 | §9 | Classic menu bar (`File`, `Edit`, `View`, `Tools`, `Window`, `Help`) and docked top bar | §4, §5, §8, D10 T01 §12, D10 T02 §2, D10 T02 §14, D10 T02 §15 | [ ] |
| 10 | §10 | Shared SPEC 9.9 `ui::Dialog` chrome and Password dialog | §5, D10 T02 §6, D10 T02 §17 | [ ] |
| 11 | §11 | Could-not-open error dialog and diagnostic clipboard copy | §5, §10, D10 T01 §17, D10 T03 §4 | [ ] |
| 12 | §12 | Crash recovery snapshots for unsaved edits | §3, §8, §10, D04 T02 §1, D02 T04 §6, D06 T04 §24, D10 T02 §11 | [ ] |
| 13 | §13 | `SessionStore`: persist open tabs and the active tab | §3, D02 T04 §1 | [ ] |
| 14 | §14 | Restore the previous tab session on launch and the restore setting | §5, §7, §13, D02 T04 §5, D02 T03 §11, D06 T04 §24, D10 T02 §7, D10 T02 §20 | [ ] |
| 15 | §15 | Reopen closed tab (`Ctrl+Shift+T`) and close-other, left and right tab commands | §3, §8, §13, D02 T03 §11, D06 T04 §24, D10 T01 §12, D10 T02 §13, D10 T02 §14, D10 T02 §20 | [ ] |
| 16 | §16 | Drag to reorder tabs and move a tab by keyboard | §3, §13, D02 T03 §11, D06 T04 §24, D10 T01 §14, D10 T01 §19, D10 T02 §13, D10 T03 §4 | [ ] |
| 17 | §17 | Share a copy through the Windows Share contract | §8, §9, D02 T03 §11, D06 T04 §24, D10 T02 §17 | [ ] |
| 18 | §18 | `RectoIcons` subset icon font, stable code point map and glyph resources | §1 | [ ] |
| 19 | §19 | `RectoChipStyle` pill style for chips, filter tags, keycaps, badges and counters | §1, §18, D10 T01 §9, D10 T02 §20 | [ ] |
| 20 | §20 | Classic status bar: current page, page size, PDF version and zoom slider | §4, §9, D02 T02 §6, D02 T02 §7, D02 T03 §11, D02 T04 §3, D10 T02 §6, D10 T02 §8 | [ ] |

## 1. App project, unpackaged/MSIX manifest, assets, tokens and minimum window size

Wire the `recto-ui` application foundation in `src/app/` before any screen is drawn: configure `src/app/Recto.vcxproj` for the Win32 host that `D10 T01 §2` builds (unpackaged execution with no UI framework runtime, for Inno Setup Full and Portable installs per D6) alongside optional MSIX packaging as a full-trust desktop package (`src/app/Package.appxmanifest` and `resources/msix/Assets/`), consume the generated tokens in `src/ui/generated/recto_tokens.g.h` (`D10 T01 §8`) so every control reads the vermilion `RectoAccentBrush` (`#C8431F` light / `#F0603C` dark) directly, set the window icon from `resources/ico/recto.ico` through `ui::Window` (`WM_SETICON`, `D10 T01 §4`), and enforce the SPEC section 7 minimum window size (`960 x 600 epx`) through `ui::Window`.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 3, 4 and 7 with `design/screenshots/D-Brand.png` (icon, wordmark, tokens) and `D-Focus-Dark.png` (dark surfaces).
**Job:** The user can launch `Recto.exe` (both unpackaged/portable and MSIX-packaged) into a branded window with Recto's vermilion accent, theme brushes, icon, and minimum size enforced. Consumer: every UI control and surface in `src/app/`.
**Treatment:** `recto-ui` application on the `D10 T01 §2` Win32 host supporting self-contained unpackaged deployment, with the generated tokens header read by every control and `ui::Window` minimum size enforcement. Cheaper substitute that fails the checkpoint: per-control hex colours, leaving the default Windows blue accent in place, or allowing the window to shrink below `960 x 600 epx`.
**Chrome:** consume the brushes and `Light`, `Dark` and `HighContrast` sets of `src/ui/generated/recto_tokens.g.h` (source `design/tokens/colors.json`) through `ui::ThemeManager`; load `resources/ico/recto.ico` and `resources/svg/` assets without duplicating token definitions.

**Build order:**

1. Copy `resources/msix/Assets/` into `src/app/Assets/`, update `src/app/Package.appxmanifest` per `resources/README.md` (`.pdf` `uap:FileTypeAssociation` with `Assets\FileTypes\PdfFile.png` and splash background `#1B1A19`, a `runFullTrust` desktop package with no framework dependency), and verify `src/app/Recto.vcxproj` runs unpackaged with no UI framework runtime directly from `build/bin/x64/Debug/Recto.exe`. Done when: `Recto.exe` starts from the build output directory without MSIX registration and `src/app/Package.appxmanifest` validates against all `src/app/Assets/` paths.
2. Build the application on the generated `src/ui/generated/recto_tokens.g.h` (`D10 T01 §8`) so accent controls render `#C8431F` in Light and `#F0603C` in Dark straight from `RectoAccentBrush` (no system accent ramp to override), set the window icon to `resources/ico/recto.ico` through `ui::Window`, and enforce `960 x 600 epx` scaled by monitor DPI through the `ui::Window` `WM_GETMINMAXINFO` handling. Done when: an accent `ui::Button` samples at `#C8431F` (Light) and `#F0603C` (Dark), and resizing the window cannot shrink below `960 x 600 epx`.

- [ ] Copy all generated package logos and file-type icons from `resources/msix/Assets/` into `src/app/Assets/`. Done when: every asset listed in `resources/README.md` exists under `src/app/Assets/`.
- [ ] Configure `src/app/Package.appxmanifest` with Recto display name, tile logos, splash screen `#1B1A19`, and `.pdf` `uap:FileTypeAssociation` pointing to `Assets\FileTypes\PdfFile.png`, declared as a `runFullTrust` desktop package with no framework dependency. Done when: manifest schema validation passes with zero missing file references.
- [ ] Configure `src/app/Recto.vcxproj` for unpackaged execution with no UI framework runtime while retaining `Package.appxmanifest` for optional `-EnableMsix` builds. Done when: `build/bin/x64/Debug/Recto.exe` launches directly from PowerShell without `Add-AppxPackage`. Cheaper substitute that fails the checkpoint: requiring manual MSIX sideloading before `Recto.exe` can start.
- [ ] Consume `src/ui/generated/recto_tokens.g.h` through `ui::ThemeManager` with the `Light`, `Dark` and `HighContrast` sets. Done when: `ui::ThemeManager` resolves every `Recto*Brush` name listed in `design/SPEC.md` section 4 in all three sets.
- [ ] Confirm the accent comes straight from the tokens: `RectoAccentBrush` is `#C8431F` in Light and `#F0603C` in Dark (`#A93717` / `#F48A6E` accent text), with no system accent override. Done when: a rendered accent `ui::Button` samples at `#C8431F` in Light and `#F0603C` in Dark.
- [ ] Set the runtime window and taskbar icon from `resources/ico/recto.ico` via `ui::Window` (`WM_SETICON`). Done when: `MainWindow` displays the Recto bookmark-ribbon icon in the taskbar and `Alt+Tab` switcher.
- [ ] Enforce the `960 x 600 epx` minimum window size in `src/app/main_window.cpp` using the `ui::Window` DPI-aware `WM_GETMINMAXINFO` handling. Done when: programmatic and mouse resize attempts below `960 x 600 epx` clamp at `960 x 600` scaled by the monitor's DPI scale.
- [ ] Add `Shell.TokensAndWindowBounds` test in `tests/recto-tests/` asserting brush availability, zero hex colour literals in `src/app` and `src/ui` outside the generated tokens header, and minimum size clamping. Done when: `recto-tests.exe --gtest_filter=Shell.TokensAndWindowBounds` passes.
- [ ] Commit: `app: wire recto-ui host manifest, assets, generated tokens and minimum window size`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.TokensAndWindowBounds` passes on the display-session host, accent colour samples at `#C8431F` (Light) and `#F0603C` (Dark), and searching `src/app` and `src/ui` for `#` hex literals outside `src/ui/generated/recto_tokens.g.h` returns zero matches.

## 2. Custom title bar, brand lockup, drag regions and caption buttons

Build the SPEC section 8.1 custom title bar inside `src/app/shell_page.h/.cpp`: left-to-right layout with the small app icon (`20 epx`), Newsreader `"Recto"` wordmark (`17 epx`), Home tab button (icon-only), document `ui::TabStrip` host, new-tab (`+`) button, centred Command Box trigger button (`"Search, open or run a command"` with `"Ctrl K"` badge), Settings gear button (`Ctrl+,`), and the caption buttons drawn by `ui::Window` (`Minimize`, `Maximize/Restore` with the Windows 11 Snap Layouts flyout, `Close`), with `ui::Window` non-client hit testing (`D10 T01 §19`) calculated around interactive elements.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 3.1, 3.3, 6, 8.1 and 11 with `design/screenshots/D-Focus-Light.png` and `D-Focus-Dark.png`.
**Job:** The user sees Recto's quiet branded title bar, can drag or snap the window naturally, and can reach Home, the Command Box, and Settings from the top bar at any time. Consumer: `MainWindow` hosts `ShellPage`; §3 mounts the document `ui::TabStrip` into the title bar strip.
**Treatment:** Custom title bar drawn on `ui::Window` (`D10 T01 §4`, `§19`): `HTCAPTION` over empty space, `HTCLIENT` over the interactive controls, caption-button hit codes for Snap Layouts, and theme-switched `ui::SvgImage` brand assets. Cheaper substitute that fails the checkpoint: stock system title bar with tabs pushed to a second row below it, or broken drag/Snap Layouts behavior.
**Chrome:** consume `RectoWindowBrush`, `RectoTextBrush`, `RectoTextSecondaryBrush`, `resources/svg/app-icon-small.svg` (one file for both themes, SPEC 3.1), and `resources/svg/wordmark-light.svg` (`-dark.svg`). Do not ship the Newsreader font file; use the outlined SVGs per SPEC 3.3.

**Build order:**

1. Create `src/app/shell_page.h` and `src/app/shell_page.cpp` and set `MainWindow` content to `ShellPage` on a `ui::Window` that extends the frame into the client area, with caption buttons on `RectoWindowBrush`. Done when: `MainWindow` renders the custom title bar row with `ui::Window` caption buttons styled to match Light and Dark themes.
2. Add the brand lockup (`20 epx` `app-icon-small`, the same file in both themes, + `17 epx` `wordmark` switched by theme), the icon-only `Home` tab button (`ui::Icon` `home` glyph from `RectoIcons`, automation name `Home`, `ui::ToolTip` `"Home"`), the centred Command Box button (`ControlCornerRadius` token, `"Search, open or run a command"`, `"Ctrl K"` keycaps in `RectoChipStyle`, automation name `Command box`, tooltip `"Command box (Ctrl+K)"`), and the Settings button (`ui::Icon` `settings` glyph from `RectoIcons`, automation name `Settings`, tooltip `"Settings (Ctrl+,)"`), and update the non-client hit regions on resize and layout updates. Done when: every title bar button is clickable, empty title bar areas drag and double-click maximize the window, and hovering the maximize button opens the Windows 11 Snap Layouts flyout.

- [ ] Author `src/app/shell_page.h/.cpp` and mount it as the root content of `MainWindow`. Done when: `Recto.exe` boots into `ShellPage` with no unresolved token, icon or string lookups.
- [ ] Draw the caption buttons in `ui::Window` and configure their foreground, background and hover colours from the generated tokens in both Light and Dark themes, with the `D10 T03 §5` system colours in HighContrast. Done when: caption buttons remain legible in Light, Dark, and HighContrast themes.
- [ ] Render the `20 epx` app icon (`resources/svg/app-icon-small.svg`, the same file in Light and Dark) and `17 epx` `"Recto"` wordmark (`resources/svg/wordmark-light.svg` / `wordmark-dark.svg`) via `ui::SvgImage` at the left of the title bar. Done when: the lockup matches `D-Focus-Light.png` and `D-Focus-Dark.png` metrics.
- [ ] Add the icon-only `Home` button, centred Command Box trigger button (`"Search, open or run a command"` + `"Ctrl K"`), and `Settings` button in the exact SPEC 8.1 left-to-right order. Done when: visual inspection and element tree order match SPEC 8.1. Cheaper substitute that fails the checkpoint: omitting the automation name or shortcut tooltips on icon-only title bar buttons.
- [ ] Compute the non-client hit regions (`HTCLIENT` over every interactive title bar control, `HTCAPTION` over empty strip, caption-button codes over the three caption buttons) through `ui::Window` (`D10 T01 §19`) whenever the window or tab strip resizes. Done when: clicking buttons activates them while dragging empty title bar space moves the window.
- [ ] Dim the title bar lockup and inactive tab labels to `RectoTextSecondaryBrush` when the `ui::Window` activation event (`D10 T01 §4`) reports deactivation. Done when: deactivating the window visually dims the title bar chrome per Windows conventions.
- [ ] Add `Shell.TitleBarLayoutAndHitRegions` test in `tests/recto-tests/` verifying title bar control order, automation names, tooltips, and non-empty non-client hit regions (`WM_NCHITTEST` answers). Done when: `recto-tests.exe --gtest_filter=Shell.TitleBarLayoutAndHitRegions` passes.
- [ ] Commit: `app: build custom title bar with brand lockup, drag regions and caption buttons`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.TitleBarLayoutAndHitRegions` passes, and rendered screenshots in Light and Dark themes match `D-Focus-Light.png` and `D-Focus-Dark.png` title bars side by side.

## 3. Document `ui::TabStrip`, tab session lifecycle, dirty state and actor teardown

Implement the document tab strip (`ui::TabStrip`, `D10 T02 §13`) in the title bar and the `DocumentTabSession` lifecycle model in `src/app/`: one tab per open document (`max 240 epx` width, close button visible on hover and on the active tab, `+` New Tab button routing to `Home`), dirty (`*`) indicator for unsaved document edits, `Ctrl+Tab` / `Ctrl+Shift+Tab` switching, `Ctrl+W` tab closing, automatic return to `Home` when the last document tab closes, and deterministic `DocumentActor` shutdown through `Runtime` (`D01 T02 §4`).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 7 (tabs use the token `ControlCornerRadius`, 4 epx), 8.1 (title bar tabs, max 240 epx, close on hover and active tab), and 10 (`Ctrl+W`, `Ctrl+Tab`, `Ctrl+Shift+Tab`) with `D-Focus-Light.png` and `D-Classic.png`.
**Job:** The user can hold multiple PDFs open simultaneously in tabs, switch between them effortlessly, see which tabs have unsaved changes, and close any tab knowing its background engine resources are immediately freed. Consumer: `OpenDocumentPipeline` (§5) creates tabs; `SavePipeline` (§8) clears the dirty indicator; `DocumentView` (`D02 T02`) renders inside the active tab's session.
**Treatment:** The `recto-ui` `ui::TabStrip` styled to `240 epx` max tab width with a strongly typed `DocumentTabSession` per tab tied 1:1 to a `DocumentHandle` (`DocumentActor`), its selection and title bound through `ui::Property<T>` (`D10 T01 §16`). Cheaper substitute that fails the checkpoint: leaving orphaned `DocumentActor` threads alive after tab close, or opening each document in a separate OS window.
**Chrome:** consume `ShellPage`'s title bar tab host (and secondary tab strip slot in `Reading room` preset, §4) with `RectoWindowBrush`, `RectoRaisedBrush` for the active tab, and `RectoTextSecondaryBrush` for inactive tabs. Do not invent a second tab strip control.

**Build order:**

1. Define `DocumentTabSession` (`src/app/DocumentTabSession.h/.cpp`) holding the tab id, file path, tab title (filename initially, updated to document `/Title` when `DisplayDocTitle` is set), `is_loading` flag, `is_dirty` flag, `std::shared_ptr<recto::core::DocumentHandle>` actor handle, and per-tab view state. Done when: creating and destroying `DocumentTabSession` in unit tests starts and joins its `DocumentActor` cleanly.
2. Configure `ui::TabStrip` inside `ShellPage`: `240 epx` maximum width per tab, close button visibility on pointer hover and active state, `+` add-tab button activating `Home`, `Ctrl+W` closing the active tab and `Ctrl+Tab` and `Ctrl+Shift+Tab` cycling tabs through the window accelerator table (`D10 T01 §12`), and closing the last document tab activating the `Home` view. Done when: 20 rapid tab open-and-close cycles leave the Debug live-clone and actor count at zero and return the shell to `Home`.

- [ ] Author `src/app/DocumentTabSession.h/.cpp` encapsulating per-tab `DocumentHandle` lifetime, file path, display title, `is_loading` state, and `is_dirty` state. Done when: destroying a `DocumentTabSession` deterministically invokes `Runtime::close` on its `DocumentHandle`.
- [ ] Embed `ui::TabStrip` in `ShellPage`'s title bar strip with Recto tab metrics (`ControlCornerRadius` token with no radius literal, `240 epx` maximum width, `Caption` 12 epx title, close button on active tab and pointer hover). Done when: tab geometry and typography match `D-Focus-Light.png`.
- [ ] Display an unsaved-changes dirty indicator (`•` or `*` badge with an automation name including `"Modified"`) on any tab whose `DocumentTabSession::is_dirty()` is true. Done when: toggling `set_dirty(true)` updates the tab header and accessibility name immediately.
- [ ] Wire the `ui::TabStrip` add button to focus the `Home` tab and place keyboard focus inside the inline Command Box. Done when: clicking `+` opens `Home` ready to type or pick a file.
- [ ] Wire `Ctrl+Tab` (next tab), `Ctrl+Shift+Tab` (previous tab), `Ctrl+W` (close active document tab), and middle-click on a tab header to close that tab. Done when: keyboard and mouse tab cycling and closing match SPEC section 10.
- [ ] Ensure closing the last open document tab automatically selects the `Home` tab rather than leaving an empty window or closing the app. Done when: closing the sole open tab transitions smoothly to `Home`. Cheaper substitute that fails the checkpoint: exiting the app or showing a blank gray frame when the last tab closes.
- [ ] Update the non-client hit regions (`D10 T01 §19`) dynamically as tabs are added, resized, or removed so every tab header and close button remains clickable while the remaining strip stays draggable. Done when: newly added tabs can be clicked, dragged, and closed immediately.
- [ ] Add `Shell.TabLifecycleAndActorTeardown` test in `tests/recto-tests/` running 20 rapid tab open/close cycles and asserting zero leaked `DocumentActor` instances or live clones in Debug. Done when: `recto-tests.exe --gtest_filter=Shell.TabLifecycleAndActorTeardown` passes.
- [ ] Commit: `app: implement title-bar ui::TabStrip, DocumentTabSession lifecycle and actor teardown`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.TabLifecycleAndActorTeardown` passes with 20 rapid open/close cycles leaving zero live actors or clones in Debug, and rendered tab strip matches `D-Focus-Light.png`.

## 4. Named shell layout slots, Reading room mode switcher and Classic navigation rail

Implement the eight named layout slot containers in `ShellPage` required by SPEC section 8 and decision D17 (`TitleBarSlot`, `TopBarSlot`, `BottomOverlaySlot`, `LeftDockSlot`, `RightDockSlot`, `LeftOverlaySlot`, `RightOverlaySlot`, `BottomDockSlot`), built as `ui::DockPanel` and `ui::OverlayPanel` children (`D10 T01 §7`), plus the `Classic` left navigation rail (`ui::NavigationRail`, `D10 T02 §19`: `Home`, `Read`, `Convert`, `Organize` with `20 epx` glyphs) and the `Reading room` title-bar mode switcher (`ui::ModeSwitcher`, `D10 T02 §18`, replacing the Home icon button with `Home | Read | Convert | Organize` with an accent underline indicator and moving the document tab strip into `TopBarSlot`).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6 (`20 epx` rail icons), 7 (borders on docked panes, elevation Z=32 on floating overlays), 8 and 8.1 with `design/screenshots/D-Focus-Light.png`, `D-Classic.png`, `D-ReadingRoom.png` and `D-Layout.png`.
**Job:** The shell provides the exact named slots and navigation chrome so the layout preset engine (`D02 T04 §3`) can rearrange controls between `Focus`, `Classic`, and `Reading room` without ever recreating the shell or losing document state. Consumer: `PageToolbar` (`D02 T02 §8`), `SidePanelHost` (`D02 T03 §3`), `ThumbnailList` (`D02 T03 §4`), and `LayoutPresetEngine` (`D02 T04 §3`).
**Treatment:** One `ShellPage` containing all eight named slot hosts and `ShellSlotManager::reparent_control`, with docked slots bordered by `1 epx` `RectoBorderBrush` on `RectoSurfaceBrush` and overlay slots floating with `ui::Painter` elevation Z=32 (`D10 T01 §9`). Cheaper substitute that fails the checkpoint: building three separate page classes for `Focus`, `Classic`, and `Reading room` (explicitly forbidden by D17).
**Chrome:** consume `RectoSurfaceBrush`, `RectoRaisedBrush`, `RectoBorderBrush`, and `RectoAccentBrush` from the generated tokens header.

**Build order:**

1. Add the eight named slot containers (`TitleBarSlot`, `TopBarSlot`, `BottomOverlaySlot`, `LeftDockSlot`, `RightDockSlot`, `LeftOverlaySlot`, `RightOverlaySlot`, `BottomDockSlot`) and the central workspace host (`CenterCanvasSlot` for `Home`, `DocumentView`, `ConvertPage`, `OrganizePage`, `SettingsPage`) to `src/app/shell_page.cpp`, along with `ShellSlotManager` (`src/app/shell_slot_manager.h/.cpp`) to move live `ui::Element` instances between slots without destroying their state (reparenting the same element instance, `D10 T01 §6`). Done when: a stateful test control moved across all eight slots preserves its internal state and focusable children.
2. Build the `Classic` left navigation rail (`ui::NavigationRail` in `LeftDockSlot`, `20 epx` `RectoIcons` glyphs `home`, `document`, `document_arrow_right`, `grid` and `settings` for `Home`, `Read`, `Convert`, `Organize`, `Settings`) and the `Reading room` title-bar mode switcher (`ui::ModeSwitcher`: `Home`, `Read`, `Convert`, `Organize` with `RectoAccentBrush` underline indicator and document tabs reparented to `TopBarSlot`). Done when: toggling slot configurations matches the shell chrome of `D-Focus-Light.png`, `D-Classic.png`, and `D-ReadingRoom.png`.

- [ ] Declare `TitleBarSlot`, `TopBarSlot`, `BottomOverlaySlot`, `LeftDockSlot`, `RightDockSlot`, `LeftOverlaySlot`, `RightOverlaySlot`, `BottomDockSlot`, and `CenterCanvasSlot` in `src/app/shell_page.cpp`. Done when: all slot hosts exist in the element tree and collapse cleanly (`ui::Visibility::Collapsed`) when empty so they consume zero layout space.
- [ ] Style docked slots (`TopBarSlot`, `LeftDockSlot`, `RightDockSlot`, `BottomDockSlot`) with `RectoSurfaceBrush` backgrounds and `1 epx` `RectoBorderBrush` dividers (zero drop shadows per SPEC 7). Done when: docked panes render flat with crisp `1 epx` borders.
- [ ] Style floating overlay slots (`BottomOverlaySlot`, `LeftOverlaySlot`, `RightOverlaySlot`) with `16 epx` canvas margins and `ui::Painter` elevation (Z=32 per SPEC 7, `D10 T01 §9`). Done when: floating cards cast the SPEC 7 elevation shadow over the page canvas.
- [ ] Implement `ShellSlotManager::reparent_control(ui::Element&, SlotId, SlotOccupant)` in `src/app/shell_slot_manager.h/.cpp` that detaches a live control from its current slot panel and attaches it to the target slot panel in a single UI tick. `SlotOccupant` carries a stacking `order` (a slot holding several controls stacks them by ascending order, top to bottom, each at its own measured height, never overlapping) and a `focus_group` (`BeforeCanvas` by default, `AfterCanvas` for a control SPEC 11 places after the page, such as the Classic status bar of §20). Done when: reparenting `ui::TabStrip` or a test toolbar preserves selection and text input state, and two test controls placed in `BottomDockSlot` with orders 0 and 1 stack in that order with disjoint bounds. Cheaper substitute that fails the checkpoint: destroying and instantiating new controls on layout change.
- [ ] Build the `Classic` left navigation rail (`Home`, `Read`, `Convert`, `Organize`, plus `Settings` pinned at bottom) on `ui::NavigationRail` with `20 epx` `RectoIcons` glyphs (`home`, `document`, `document_arrow_right`, `grid`, `settings`), automation names, and tooltips. Done when: activating rail items switches the active workspace mode in `ShellPage`.
- [ ] Build the `Reading room` title-bar mode switcher (`Home`, `Read`, `Convert`, `Organize` with a `2 epx` `RectoAccentBrush` active underline indicator, `ui::ModeSwitcher`) and support reparenting the document `ui::TabStrip` into `TopBarSlot` directly beneath the title bar per SPEC 8.1. Done when: the `Reading room` header matches `D-ReadingRoom.png`.
- [ ] Enforce the SPEC 11 logical keyboard `Tab` order by occupant focus group, not by slot alone, as `ui::FocusManager` focus scopes (`D10 T01 §13`) (`TitleBarSlot -> TopBarSlot -> BottomOverlaySlot -> LeftDockSlot/LeftOverlaySlot -> RightDockSlot/RightOverlaySlot -> BottomDockSlot occupants in BeforeCanvas, such as the filmstrip -> CenterCanvasSlot -> occupants in AfterCanvas, such as the Classic status bar`), so a status bar sharing `BottomDockSlot` with the filmstrip is still reached after the page. Done when: pressing `Tab` traverses populated slots in exact SPEC 11 order, and with a `BeforeCanvas` and an `AfterCanvas` test occupant both in `BottomDockSlot`, `Tab` reaches the first before `CenterCanvasSlot` and the second after it (and `Shift+Tab` reverses).
- [ ] Add `Shell.NamedSlotsAndReparenting` test in `tests/recto-tests/` verifying lossless control reparenting across all eight slots, empty-slot collapse, multi-occupant stacking order in `BottomDockSlot`, and keyboard tab-index ordering including the split `BottomDockSlot` focus groups. Done when: `recto-tests.exe --gtest_filter=Shell.NamedSlotsAndReparenting` passes, and fails when the `AfterCanvas` occupant is reached before `CenterCanvasSlot`.
- [ ] Commit: `app: add named shell layout slots, Classic nav rail and Reading room mode switcher`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.NamedSlotsAndReparenting` passes on the display-session host, proving lossless control reparenting across all eight named slots and SPEC 11 keyboard tab order, with an `AfterCanvas` occupant of `BottomDockSlot` reached after the page; ordering by slot alone makes the test fail.

## 5. File Open Picker (`Ctrl+O`) and shared `OpenDocumentPipeline`

Build the central `OpenDocumentPipeline` (`src/app/open_pipeline.h/.cpp`) that every file-opening entry point in Recto calls, and wire the first entry point: the system `IFileOpenDialog` picker (`Ctrl+O`, toolbar/menu `Open`, and Home `"Browse files"`). Opening a PDF creates or activates a tab, dispatches `Runtime::open` on the document's `DocumentActor` off the UI thread, updates the tab title from the filename to the PDF `/Title` when `DisplayDocTitle` applies, and routes encrypted or damaged files to the §10 and §11 dialogs.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 3.1, 8.1, 9.1, 9.2 (loading states) and 10 (`Ctrl+O`) with `D-Focus-Light.png`.
**Job:** The user can press `Ctrl+O` (or click `Browse files`) to pick one or more PDFs from Explorer and have them open immediately into tabs without blocking the UI thread. Consumer: drag-and-drop (§6), CLI/activation (§7), Home recents (`D02 T03 §2`), and `DocumentView` (`D02 T02`).
**Treatment:** Single `OpenDocumentPipeline::open_files_async` service backed by `Runtime::open` on background `DocumentActor` threads, completions marshalled to the UI thread through `ui::Dispatcher` (`D10 T01 §3`), and `IFileOpenDialog` owned by the `MainWindow` HWND. Cheaper substitute that fails the checkpoint: showing the picker with no owner window (it opens behind Recto) or parsing the PDF on the UI thread.
**Chrome:** consume `DocumentTabSession` (§3) and `ShellPage`'s `ui::TabStrip`. Do not create separate open code paths for different UI surfaces.

**Build order:**

1. Implement `OpenDocumentPipeline` (`src/app/open_pipeline.h/.cpp`): canonicalize file paths (`std::filesystem::weakly_canonical`), check if any open `DocumentTabSession` already owns that path (if so, select that tab and return), otherwise create a new `DocumentTabSession` in `is_loading` state with the filename as its initial tab title and invoke `Runtime::open` asynchronously. Done when: opening the same corpus PDF twice selects the existing tab on the second call instead of spawning a second `DocumentActor`.
2. Wire `IFileOpenDialog` (multi-select, `.pdf` filter, `FOS_FORCEFILESYSTEM`) to `Ctrl+O` and `OpenDocumentPipeline::open_picker_async`, and handle the `DocumentActor` open completion on the UI thread through `ui::Dispatcher`: on `Ok(DocumentDescription)`, populate page count/dimensions and update the tab title if `/ViewerPreferences /DisplayDocTitle` is true and `/Title` is non-empty; on `ErrorCode::NeedsPassword`, invoke §10's password hook; on any other `PdfError`, close the loading tab and invoke §11's error hook. Done when: opening valid, multi-file, encrypted, and corrupt corpus files through `OpenDocumentPipeline` routes each case deterministically off the UI thread.

- [ ] Author `src/app/open_pipeline.h/.cpp` providing `open_file_async(path, initial_page)` and `open_files_async(paths)`. Done when: all open requests flow through `OpenDocumentPipeline` and never call `recto::pdf::Document::open` on the UI thread.
- [ ] Deduplicate already-open files by canonical path (`std::filesystem::weakly_canonical`) so re-opening an active document focuses its existing tab. Done when: opening `hello.pdf` twice produces 1 tab and 1 `DocumentActor`.
- [ ] Show `IFileOpenDialog` with the `MainWindow` `HWND` as owner so the system file picker is modal to Recto in both unpackaged/portable mode and MSIX mode. Done when: `open_picker_async` opens the native Explorer dialog in unpackaged `Recto.exe` in front of the Recto window. Cheaper substitute that fails the checkpoint: passing a null owner window, so the picker opens behind Recto.
- [ ] Support multi-file selection in `IFileOpenDialog` (`FOS_ALLOWMULTISELECT`), opening each selected `.pdf` in its own tab in selection order and focusing the first newly opened tab. Done when: selecting 3 corpus files opens 3 tabs in order.
- [ ] Set the initial tab title to the filename (e.g., `contract.pdf`) immediately while `is_loading` is true, and update the tab title to the PDF metadata `/Title` once `DocumentDescription` arrives if `/ViewerPreferences /DisplayDocTitle` is true (keeping the full file path in the tab tooltip). Done when: a slow open never shows an untitled or unresponsive tab.
- [ ] Route `ErrorCode::NeedsPassword` from `Runtime::open` to the password prompt handler (§10) and any format/IO `PdfError` to the could-not-open handler (§11) without disturbing other open tabs in a multi-file batch. Done when: opening `[good.pdf, broken.pdf]` leaves `good.pdf` open and reports `broken.pdf` once.
- [ ] Bind the `Ctrl+O` window accelerator (`D10 T01 §12`) on `ShellPage` to `OpenDocumentPipeline::open_picker_async`. Done when: pressing `Ctrl+O` from `Home` or an open tab invokes the picker pipeline.
- [ ] Add `Shell.OpenPipelineAndDedup` test in `tests/recto-tests/` verifying async actor creation, canonical-path tab deduplication, `DisplayDocTitle` tab title updates, and mixed good/bad batch routing. Done when: `recto-tests.exe --gtest_filter=Shell.OpenPipelineAndDedup` passes.
- [ ] Commit: `app: add OpenDocumentPipeline and Ctrl+O file picker with tab deduplication`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.OpenPipelineAndDedup` passes on the display-session host, verifying single-file, multi-file, duplicate-path, and mixed good/corrupt batch opens.

## 6. Drag-and-drop file opening and full-window drop overlay

Implement window-wide drag-and-drop file opening per SPEC section 9.1: dragging files anywhere over `ShellPage` displays the full-window drop overlay (`resources/svg/drop-files-light.svg` and `drop-files-dark.svg` via theme-aware `ui::SvgImage`, dashed accent border, `"Drop a PDF anywhere in this window"` label), and dropping files feeds them into `OpenDocumentPipeline::open_files_async` (§5).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 3.4, 4, 7 and 9.1 with `design/screenshots/D-Home.png` and `resources/svg/drop-files-*.svg`.
**Job:** The user can drag one or more PDFs from Windows Explorer and drop them anywhere on the Recto window (whether on `Home`, an open document, or the tab bar) to open them in tabs. Consumer: `OpenDocumentPipeline` (§5).
**Treatment:** Window-wide OLE `IDropTarget` (`D10 T01 §17`) registered on the `MainWindow` HWND, with a hit-test-invisible translucent drop overlay on the root overlay layer (`D10 T01 §15`) using `RectoRaisedBrush`, `RectoAccentBrush`, and `drop-files-{light,dark}.svg`. Cheaper substitute that fails the checkpoint: accepting drops only inside a small box on the Home page, or showing no visual drop feedback during `DragOver`.
**Chrome:** consume `resources/svg/drop-files-light.svg` and `resources/svg/drop-files-dark.svg` through `ui::SvgImage` theme sources and route dropped file paths into `OpenDocumentPipeline::open_files_async` (§5). Do not invent a second drop overlay or open pipeline.

**Build order:**

1. Build the `DropOverlayControl` (`src/app/controls/drop_overlay.h/.cpp`) hosted at the top layer of the root overlay (hit-test invisible, hidden by default) with `RectoWindowBrush` / `RectoRaisedBrush` backdrop, `2 epx` `RectoAccentBrush` border on the `OverlayCornerRadius` token (8 epx, SPEC 7) with a `24 epx` margin, `resources/svg/drop-files-{light,dark}.svg` illustration, and `"Drop a PDF anywhere in this window"` `Subtitle` text style. Done when: toggling the overlay visibility in Light and Dark themes matches the SPEC 3.4 and 9.1 design.
2. Wire the `IDropTarget` callbacks (`DragEnter`, `DragOver`, `DragLeave`, `Drop`) on `ShellPage`: inspect the `IDataObject` for `CF_HDROP`, return `DROPEFFECT_COPY` and show `DropOverlayControl` when file items are present, hide the overlay on `DragLeave` or `Drop`, and pass the dropped file paths to `OpenDocumentPipeline::open_files_async`. Done when: dropping a batch of valid and broken corpus files onto `ShellPage` opens every valid PDF in a tab and routes broken/non-PDF files to §11's error dialog.

- [ ] Author `src/app/controls/drop_overlay.h/.cpp` consuming `resources/svg/drop-files-light.svg` and `resources/svg/drop-files-dark.svg` via `ui::SvgImage` and the generated tokens. Done when: the overlay renders with zero hex literals and switches illustrations cleanly between Light and Dark themes.
- [ ] Register the `IDropTarget` on the whole `MainWindow` HWND so drag-and-drop works over `Home`, open document tabs, side panels, and toolbars alike. Done when: dragging a file over any quadrant of the window triggers `DragOver`. Cheaper substitute that fails the checkpoint: enabling drop only on `HomePage`.
- [ ] Inspect the `IDataObject` for `CF_HDROP` in `DragOver` to show `DropOverlayControl` and return `DROPEFFECT_COPY` (drop description `"Open in Recto"`), while rejecting plain-text or URL drags on the window shell. Done when: dragging files shows the overlay and dragging selected text does not.
- [ ] Hide `DropOverlayControl` reliably on `DragLeave`, `Drop`, and `Esc` cancellation without flicker when moving the pointer across child controls (the overlay is hit-test invisible). Done when: moving the dragged cursor across the window never strobes the overlay.
- [ ] Extract the `HDROP` file paths in `ShellPage`'s `IDropTarget::Drop` and forward them in drop order to `OpenDocumentPipeline::open_files_async` (§5). Done when: dropping 3 PDFs opens 3 tabs in exact drop order.
- [ ] Handle mixed drops (e.g., 2 valid PDFs + 1 corrupt PDF or `.exe` file) by opening the valid PDFs into tabs and routing the invalid file to the §11 error dialog without losing the valid tabs. Done when: mixed-drop tests preserve all valid tabs.
- [ ] Add `Shell.DragAndDropOpen` test in `tests/recto-tests/` exercising `DragOver` overlay state transitions and multi-file/mixed-file drop routing through `OpenDocumentPipeline`. Done when: `recto-tests.exe --gtest_filter=Shell.DragAndDropOpen` passes.
- [ ] Commit: `app: implement window-wide drag-and-drop file opening and SVG drop overlay`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.DragAndDropOpen` passes on the display-session host, and screenshots of the active drop overlay in Light and Dark themes match SPEC sections 3.4 and 9.1.

## 7. Command-line arguments, Explorer `.pdf` activation and single-instance forwarding

Complete the remaining two Windows file-opening entry points in `src/app/app.cpp`: command-line arguments (`Recto.exe "path\to\file.pdf" --page=5`) and Windows Explorer file activation (the shell `open` verb launching `Recto.exe "%1"` when double-clicking an associated `.pdf` file; an MSIX full-trust desktop package passes the file the same way), with single-instance forwarding (a named mutex plus a `WM_COPYDATA` hand-off to the primary window, `D10 T01 §2` and `§4`) so opening a `.pdf` from Explorer or a second terminal when `Recto.exe` is already running opens a new tab in the existing window and brings it to the foreground.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 3.2 (`.pdf` file association behavior) and 8.1 (one tab per open document in one shell window).
**Job:** The user can double-click `.pdf` files in Windows Explorer or launch `Recto.exe <file.pdf>` from a terminal/script and have them open as tabs in their active Recto window. Consumer: `OpenDocumentPipeline` (§5).
**Treatment:** Named mutex `Local\RectoPrimaryInstance` single-instance registration, `WM_COPYDATA` forwarding of the serialized `LaunchRequest` to the primary `ui::Window`, `CommandLineToArgvW` Unicode parsing, and `SetForegroundWindow` activation. Cheaper substitute that fails the checkpoint: spawning a separate `Recto.exe` window per double-clicked file, or failing when paths contain spaces or non-ASCII characters.
**Chrome:** route all activated paths and optional `--page=N` arguments into `OpenDocumentPipeline` (§5) and `ShellPage` (§3).

**Build order:**

1. Implement `parse_launch_arguments(std::wstring_view cmd_line)` in `src/app/launch_args.h/.cpp` using `CommandLineToArgvW` to extract one or more file paths plus optional `--page=<N>` / `-p <N>` arguments, resolving relative paths against the caller's working directory. Done when: unit tests parse quoted paths with spaces, Unicode paths (CJK/accented characters), multiple files, and `--page=12` flags accurately.
2. Register the primary instance in `src/app/app.cpp` with the named mutex `Local\RectoPrimaryInstance`: if the current process is not the primary instance, serialize its `LaunchRequest`, send it to the primary window with `WM_COPYDATA` (`SendMessageTimeout`) and exit cleanly; in the primary instance, handle both the initial launch and every later `WM_COPYDATA` request, marshal to the UI thread through `ui::Dispatcher` (`D10 T01 §3`), restore/foreground `MainWindow`, and invoke `OpenDocumentPipeline::open_files_async`. Done when: launching a second `Recto.exe "second.pdf"` process opens `second.pdf` as a second tab in the first `Recto.exe` window and exits the second process with code 0.

- [ ] Author `src/app/launch_args.h/.cpp` parsing Win32 command-line strings (`CommandLineToArgvW`) into structured `LaunchRequest` records (`std::vector<std::filesystem::path> files`, `std::optional<uint32_t> initial_page`). Done when: `LaunchArgs` handles spaces, relative paths, `--page=N`, and Unicode filenames without truncation.
- [ ] Register the primary instance by creating the named mutex `Local\RectoPrimaryInstance` during app startup in `src/app/app.cpp`. Done when: the mutex ownership test returns true for the first instance and false for a second instance.
- [ ] Forward secondary instance launches to the primary window with `WM_COPYDATA` and terminate the secondary process before creating a window. Done when: invoking `Recto.exe file2.pdf` while `Recto.exe file1.pdf` is running opens `file2.pdf` as a tab in the first window. Cheaper substitute that fails the checkpoint: handling file activation only on cold startup and ignoring already-running instances.
- [ ] Handle Explorer `.pdf` activation for both the MSIX-packaged full-trust desktop package and Inno Setup unpackaged associations (the `open` verb passes `"%1"` on the command line in both). Done when: Explorer activations extract every path and open tabs in `ShellPage`.
- [ ] Marshal forwarded `WM_COPYDATA` requests onto the UI thread through `ui::Dispatcher` (`D10 T01 §3`) and bring `MainWindow` to the foreground (`ShowWindow(SW_RESTORE)` + `SetForegroundWindow`). Done when: activating a file while Recto is minimized restores the window and focuses the newly opened tab.
- [ ] Honor `--page=N` on command-line launch by passing `initial_page` into `OpenDocumentPipeline::open_file_async` so the tab opens directly at page `N`. Done when: `Recto.exe sample.pdf --page=3` opens `sample.pdf` positioned on page 3.
- [ ] Add `Shell.LaunchArgsAndActivation` test in `tests/recto-tests/` covering `LaunchArgs` parsing (quotes, Unicode, relative paths, `--page=N`), cold CLI launch, and secondary-instance tab forwarding. Done when: `recto-tests.exe --gtest_filter=Shell.LaunchArgsAndActivation` passes.
- [ ] Rejoin unquoted, space-split arguments into the longest existing file path before treating them as separate files (`C:\My Files\a b.pdf` passed unquoted by host software). Done when: `Shell.LaunchArgsAndActivation` opens the spaced fixture from the unquoted argument list.
- [ ] Accept Adobe-style open parameters after `#` in a file argument (`page=N`, `nameddest=Name`, `zoom=P`, `search=word`) and strip them from the path, so a help link such as `manual.pdf#nameddest=intro` opens the document at that destination. Done when: `Shell.LaunchArgsAndActivation` opens `manual.pdf#nameddest=intro` at the named destination and reports no file error.
- [ ] Give single-instance forwarding a liveness handshake: if the primary instance does not acknowledge a forwarded `WM_COPYDATA` request within 3 s (`SendMessageTimeout`), the new process opens its own window and logs why, never exiting silently. Done when: `Shell.LaunchArgsAndActivation` with a primary instance frozen in the test harness still shows the requested file within 5 s.
- [ ] Parse `/p <file>` and `/pt <file> <printer> <driver> <port>` into a print-only `LaunchRequest` consumed by `D07 T01 §9`. Done when: `Shell.LaunchArgsAndActivation` parses both forms into print requests with the printer name preserved.
- [ ] Commit: `app: support CLI arguments, Explorer .pdf activation and single-instance tab forwarding`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.LaunchArgsAndActivation` passes on the display-session host, proving cold CLI opens, Unicode/spaced paths, `--page=N` navigation, and single-instance redirection into tabs.

## 8. Save (`Ctrl+S`), Save As (`Ctrl+Shift+S`), Replace-Original and Unsaved-Changes dialogs

Implement the viewer's file-saving pipeline (`src/app/save_pipeline.h/.cpp`) and its two SPEC section 9.9 confirmation dialogs: `Save` (`Ctrl+S`), `Save a copy` / `Save As` (`Ctrl+Shift+S` via the system `IFileSaveDialog`), the `"Replace the original?"` `ui::Dialog` (`Keep both`, `Replace`), and the `"Save your changes first?"` `ui::Dialog` (`Discard`, `Cancel`, `Save`) triggered when closing a modified tab (`Ctrl+W`), closing the window with modified tabs, or leaving Organize with unsaved changes. Every file write obeys the foundation plan's atomic temp-file-then-replace rule (`ReplaceFileW` / `MoveFileExW`) in the destination folder.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Save a copy` in More menu), 9.6 and 9.9 (`Replace original` and `Unsaved Organize changes` / unsaved tab changes dialogs), and 10 (`Ctrl+S`, `Ctrl+Shift+S`) with `design/screenshots/D-Dialogs.png`.
**Job:** The user can save their changes (annotations, form fills, bookmarks, metadata, or page edits) safely without ever corrupting the original PDF on disk or losing unsaved work when closing a tab or window. Consumer: `ShellPage` tab/window close (§3), Classic menu bar (§9), `PageToolbar` (`D02 T02 §8`), Bookmark/Info editors (`D02 T03 §6`, `§10`), Annotations & Forms (`D03 T03`), and Organize (`D05 T02`).
**Treatment:** `SavePipeline` executing on background job/actor threads with atomic temp-file-then-replace (`ReplaceFileW` / `MoveFileExW`), readback verification, and the two SPEC 9.9 `ui::Dialog` prompts. Cheaper substitute that fails the checkpoint: truncating and writing over the open source file in place, or closing dirty tabs without asking.
**Chrome:** consume `RectoDialogStyle` (§10), `IFileSaveDialog` owned by the `MainWindow` HWND, and `DocumentTabSession`'s dirty indicator (§3).

**Build order:**

1. Implement `SavePipeline` (`src/app/save_pipeline.h/.cpp`): `save_copy_async(session)` prompting `IFileSaveDialog` (defaulting to `<stem> (copy).pdf` in the source folder) and writing via atomic temp-file-then-replace; and `save_async(session)` which either appends an incremental revision (`D01 T02 §2`) or prompts `"Replace the original?"` (`Keep both` -> saves `<stem> (edited).pdf`, `Replace` -> writes temp file in destination folder and atomically replaces the original via `ReplaceFileW`), clearing `session.set_dirty(false)` on success and reading back the saved header/XRef to confirm validity. Done when: saving an edited document in both `Keep both` and `Replace` modes produces a valid PDF verified by `recto::pdf::Document::open` and clears the tab's dirty indicator.
2. Implement `UnsavedChangesDialog` (`"Save your changes first?"` with `Discard`, `Cancel`, `Save`) on `ui::Dialog` and hook it into `ShellPage::close_tab_async` (`Ctrl+W`, tab close button) and the `MainWindow` close request (`WM_CLOSE` through `ui::Window`, `D10 T01 §4`): `Cancel` aborts the close and keeps the tab open; `Discard` tears down the tab/actor immediately; `Save` awaits `SavePipeline::save_async` and only closes the tab if the save succeeded. Done when: attempting to close a dirty tab or window prompts the user and honors all three button outcomes without data loss.

- [ ] Author `src/app/save_pipeline.h/.cpp` with `save_async` (`Ctrl+S`) and `save_copy_async` (`Ctrl+Shift+S`) executing file writes off the UI thread. Done when: no PDF serialization runs on the UI thread.
- [ ] Enforce the atomic temp-file-then-replace contract (`<dest>\.~recto-<uuid>.tmp` written, flushed via `FlushFileBuffers`, verified, then swapped via `ReplaceFileW` or `MoveFileExW`) for every save operation. Done when: a simulated write failure leaves the original destination file byte-for-byte untouched. Cheaper substitute that fails the checkpoint: opening the destination path with `std::ofstream(..., std::ios::trunc)`.
- [ ] Wire `IFileSaveDialog` (owner `MainWindow` HWND, `.pdf` default extension) for `Save a copy` (`Ctrl+Shift+S`), falling back to the save dialog automatically if `Save` targets a read-only file or read-only directory. Done when: saving a read-only PDF prompts for a new path instead of failing silently.
- [ ] Build the `"Replace the original?"` `ui::Dialog` per SPEC 9.9 (`Keep both` saving alongside the original, `Replace` performing atomic replacement). Done when: the dialog layout and button actions match `design/SPEC.md` section 9.9.
- [ ] Build the `"Save your changes first?"` `ui::Dialog` per SPEC 9.9 (`Discard`, `Cancel`, `Save` in accent) and wire it to tab close (`Ctrl+W`, close button, middle-click) whenever `DocumentTabSession::is_dirty()` is true. Done when: closing an unmodified tab closes immediately, while closing a dirty tab displays the prompt.
- [ ] Intercept the `ui::Window` close request (`WM_CLOSE`, `D10 T01 §4`) on `MainWindow` when one or more open tabs (or Organize) have unsaved changes, prompting cleanly before allowing the process to exit. Done when: pressing `Alt+F4` with a dirty tab open blocks window destruction until `Discard` or `Save` completes.
- [ ] Clear `DocumentTabSession::set_dirty(false)` and update the tab title/tooltip upon successful save; when the document has an optional-content edit session (`D01 T02 §14`), pass the revision id returned by `export_ocg_revision` to `mark_ocg_saved(revision)` so an edit staged during the write stays dirty, and pass it to `abandon_ocg_export(revision)` when the write fails or is cancelled. Done when: the tab's dirty indicator disappears immediately after `Save` completes, an edit staged between export and save completion leaves the tab dirty, and a failed write leaves the tab dirty with its export released.
- [ ] Add `Shell.SavePipelineAndUnsavedPrompt` test in `tests/recto-tests/` verifying atomic temp-then-replace writes, simulated mid-write failure preservation of originals, `Keep both` vs `Replace`, and `Discard` / `Cancel` / `Save` close interception. Done when: `recto-tests.exe --gtest_filter=Shell.SavePipelineAndUnsavedPrompt` passes.
- [ ] Commit: `app: implement Ctrl+S Save, Ctrl+Shift+S Save As, Replace-Original and Unsaved-Changes dialogs`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.SavePipelineAndUnsavedPrompt` passes on the display-session host, proving atomic temp-then-replace saves, readback verification, and dirty-tab close interception.

## 9. Classic menu bar (`File`, `Edit`, `View`, `Tools`, `Window`, `Help`) and docked top bar

Build the Classic menu bar (`ui::MenuBar`, `D10 T02 §15`, with `File`, `Edit`, `View`, `Tools`, `Window`, `Help`) and docked top command bar hosted in `TopBarSlot` (`D-Classic.png`), exposing every file, view, annotation, form, redaction, signing, conversion, and organization command with its `RectoIcons` glyph and SPEC section 10 keyboard shortcut text.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6 (`16 epx` toolbar/menu glyphs), 8, 8.2, 8.4 (Classic menu bar), 9.2 and 10 with `design/screenshots/D-Classic.png` and `explorations/A-*.png`.
**Job:** Users who prefer a traditional desktop PDF workflow (`Classic` preset) or menu-driven discovery can access `File` (`Open`, `Open Recent`, `Save`, `Save a Copy`, `Print`, `Properties`, `Close Tab`, `Exit`), `Edit`, `View`, `Tools`, `Window`, and `Help` from the top bar. Consumer: `ShellPage`'s `TopBarSlot` (§4), `CommandRegistry` (`D02 T03 §11`), and `LayoutPresetEngine` (`D02 T04 §3`).
**Treatment:** `ui::MenuBar` over `ui::Menu` popups (`D10 T02 §14`) and a docked command bar of `ui::Button` icon buttons (`D10 T02 §2`) in `TopBarSlot` on `RectoSurfaceBrush` with `1 epx` bottom `RectoBorderBrush`, sharing the exact command handlers as the floating toolbar and `Ctrl+K`. Cheaper substitute that fails the checkpoint: omitting a `File` / `View` / `Tools` menu structure in `Classic` mode or wiring menu items to duplicate handlers that drift from the toolbar.
**Chrome:** consume `TopBarSlot` (§4), `RectoSurfaceBrush`, `RectoBorderBrush`, and `16 epx` `RectoIcons` `ui::Icon` glyphs.

**Build order:**

1. Author `src/app/controls/shell_menu_bar.h/.cpp` containing the six top-level menus (`File`, `Edit`, `View`, `Tools`, `Window`, `Help`) as `ui::MenuBar` entries whose `ui::Menu` items cover command, submenu, check and radio kinds (`D10 T02 §14`), `16 epx` `ui::Icon` glyphs, and shortcut labels matching SPEC section 10. Done when: every menu opens cleanly by mouse or `Alt+F` / `Alt+E` / `Alt+V` / `Alt+T` / `Alt+W` / `Alt+H` access keys.
2. Wire `File` (`Open... Ctrl+O`, `Open Recent >`, `Save Ctrl+S`, `Save a Copy... Ctrl+Shift+S`, `Print... Ctrl+P`, `Convert Document...`, `Properties...`, `Close Tab Ctrl+W`, `Exit`), `Edit` (`Undo Ctrl+Z`, `Redo Ctrl+Y`, `Copy Ctrl+C`, `Select All on Page Ctrl+A`, `Find... Ctrl+F`, `Find & Replace... Ctrl+H`), `View` (`Zoom In`, `Zoom Out`, `Fit Width Ctrl+0`, `Actual Size Ctrl+1`, `Page Layout > Continuous / Single Page / Two-Page Spread`, `Rotate Right Ctrl+R`, `Rotate Left Ctrl+Shift+R`, `Page Colour > Original / Dimmed / Inverted`, `Side Panel Ctrl+B`, `Layout Preset > Focus / Classic / Reading room`, `Full Screen F11`), `Tools` (`Annotate & Markup Ctrl+Shift+A`, `Form Designer`, `Redact & Sanitize Ctrl+Shift+X`, `Sign Document Ctrl+Shift+G`, `Compare PDFs Ctrl+Shift+D`, `Extract Tables & Images`, `Organize Pages`, `Crop & Resize`, `Watermark`, `Header / Footer & Bates`, `Optimize & Compress`), `Window` (open document tabs list, `Next Tab Ctrl+Tab`, `Previous Tab Ctrl+Shift+Tab`), and `Help` (`Keyboard Shortcuts`, `About Recto`), disabling document-specific items when on `Home` with zero open documents. Done when: every menu item is either live in M2 (`Open`, `Save`, `Save a Copy`, `Close Tab`, `Exit`, view/tab items) or explicitly bound to its named resolving section (`D03` to `D07`).

- [ ] Author `src/app/controls/shell_menu_bar.h/.cpp` with `File`, `Edit`, `View`, `Tools`, `Window`, and `Help` menus and access key mnemonics (`F`, `E`, `V`, `T`, `W`, `H`, `D10 T01 §12`). Done when: pressing `Alt+F` opens the `File` menu with keyboard focus on `Open...`.
- [ ] Populate the `File` menu with `Open... (Ctrl+O)`, dynamic `Open Recent` submenu, `Save (Ctrl+S)`, `Save a Copy... (Ctrl+Shift+S)`, `Print... (Ctrl+P)`, `Convert...`, `Document Properties...`, `Close Tab (Ctrl+W)`, and `Exit`. Done when: `Open`, `Save`, `Save a Copy`, `Close Tab`, and `Exit` execute through §§3, 5, and 8. Cheaper substitute that fails the checkpoint: hiding `Open` or `Save` inside an overflow `...` button with no `File` menu in `Classic` mode.
- [ ] Populate the `Edit` menu with `Undo (Ctrl+Z)`, `Redo (Ctrl+Y)`, `Copy (Ctrl+C)`, `Select All on Page (Ctrl+A)`, `Find... (Ctrl+F)`, `Find & Replace... (Ctrl+H)`, `Manage Bookmarks...`, and `Edit Info & Preferences...`. Done when: all `Edit` items display their SPEC section 10 shortcuts and enable/disable based on active document state.
- [ ] Populate the `View` menu with zoom commands, `Page Layout` radio items (`Continuous`, `Single Page`, `Two-Page Spread`), rotation commands, `Page Colour` radio items (`Original`, `Dimmed`, `Inverted`), `Side Panel (Ctrl+B)` toggle, `Layout Preset` submenu (`Focus`, `Classic`, `Reading room`), and `Full Screen (F11)`. Done when: checked/radio states in the `View` menu stay synchronized with the active document view and layout preset.
- [ ] Populate the `Tools` menu with every SPEC 8.2 contextual mode bar and dialog trigger (`Annotate & Markup Ctrl+Shift+A`, `Form Designer`, `Redact & Sanitize Ctrl+Shift+X`, `Sign Document Ctrl+Shift+G`, `Compare PDFs Ctrl+Shift+D`, `Extract Tables & Images`, `Organize Pages`, `Crop & Resize`, `Watermark`, `Header / Footer & Bates`, `Optimize & Compress`). Done when: every tool entry names its resolving section or executes its live bar.
- [ ] Populate the `Window` menu with a live radio list of open document tabs plus `Next Tab (Ctrl+Tab)` and `Previous Tab (Ctrl+Shift+Tab)`, and the `Help` menu with `Keyboard Shortcuts` and `About Recto`. Done when: selecting an open tab in `Window` switches to that tab.
- [ ] Update menu item enabled states automatically whenever the active tab changes between `Home` (no document) and an open `DocumentTabSession`. Done when: `Save`, `Print`, `Rotate`, and `Tools` items disable on `Home` and enable when a PDF tab is active.
- [ ] Add `Shell.MenuBarStructureAndState` test in `tests/recto-tests/` asserting all 6 top-level menus, item counts, shortcut labels, and enabled/disabled state transitions between `Home` and an open document tab. Done when: `recto-tests.exe --gtest_filter=Shell.MenuBarStructureAndState` passes.
- [ ] Commit: `app: build Classic menu bar (File, Edit, View, Tools, Window, Help) and TopBarSlot host`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.MenuBarStructureAndState` passes on the display-session host, and rendered `Classic` top bar matches `D-Classic.png`.

## 10. Shared SPEC 9.9 `ui::Dialog` chrome and Password dialog

Establish the shared `RectoDialogStyle` presets over `ui::Dialog` (`D10 T02 §17`; the `OverlayCornerRadius` token of 8 epx, `Padding=28 epx`, `RectoRaisedBrush`, elevation Z=64, alert full-width stacked button layout vs. form left-aligned side-by-side button layout per SPEC 9.9) and implement `PasswordDialog` (`src/app/dialogs/password_dialog.h/.cpp`) wired to `DocumentActor::authenticate`: a password-protected PDF prompts before any page renders, wrong attempts keep the dialog open with a `RectoDangerBrush` border and `"That password didn't work."`, and `Cancel` tears down the pending tab and actor cleanly.

> **Deferred:** Queue-card password retry invocation from background conversion jobs -> XREF: D04 T01 §7

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6 (`24 epx` dialog header glyphs, no tile), 7 (`OverlayCornerRadius` 8 epx, elevation Z=64), 9.2 and 9.9 (`Password` dialog row) with `design/screenshots/D-Dialogs.png`.
**Job:** The user can unlock an encrypted PDF (`RC4`, `AES-128`, or `AES-256`) with immediate inline feedback on mistyped passwords, or cancel cleanly without leaving a broken empty tab behind. Consumer: `OpenDocumentPipeline` (§5) and the Convert queue (`D04 T01 §7`).
**Treatment:** Shared `RectoDialogStyle` presets (`src/app/dialogs/dialog_style.h/.cpp`) plus stateful `PasswordDialog` that validates credentials asynchronously against `DocumentActor::authenticate` while its primary button handler keeps the `ui::Dialog` open (disabled buttons, progress, close only on success) so the dialog never closes and reopens on a typo. Cheaper substitute that fails the checkpoint: closing and reopening the dialog on a wrong password, or rendering blank pages behind the prompt before authentication succeeds.
**Chrome:** author `src/app/dialogs/dialog_style.h/.cpp` consumed by all SPEC 9.9 dialogs across the app (`D02`, `D04`, `D05`). Do not style dialogs individually.

**Build order:**

1. Author `src/app/dialogs/dialog_style.h/.cpp` defining the SPEC 9.9 presets over `ui::Dialog` (the `OverlayCornerRadius` token of 8 epx with no radius literal, `Padding=28 epx`, `RectoRaisedBrush`, `RectoBorderBrush`, a `24 epx` header glyph slot drawn directly in the status colour with no tile, tinted box or circle behind it, `Subtitle` 20 epx Semibold title, alert stacked full-width button layout and form side-by-side `Cancel` + accent primary button layout). Done when: test dialogs in both Alert and Form layouts match `D-Dialogs.png` geometry and token colours in Light and Dark themes.
2. Build `PasswordDialog` (`src/app/dialogs/password_dialog.h/.cpp`) with the `24 epx` lock `ui::Icon`, `"Enter the password"` title, `"<filename> is protected."` body text, `ui::PasswordBox` (`D10 T02 §6`, auto-focused on open, `Enter` submits), inline error row (`RectoDangerBrush` border on the box + warning icon + `"That password didn't work."` text, hidden initially), and `Open` / `Cancel` buttons wired to `OpenDocumentPipeline` (§5). Done when: the encrypted corpus PDF unlocks on the correct password, displays the red border and `"That password didn't work."` in place on a wrong password, and closes the pending tab with zero leaked actors on `Cancel` or `Esc`.

- [ ] Author `src/app/dialogs/dialog_style.h/.cpp` with the `OverlayCornerRadius` token (8 epx), `28 epx` padding, `RectoRaisedBrush`, elevation Z=64, a `24 epx` alert glyph drawn in the status colour (`RectoDangerBrush`, `RectoWarningBrush` or `RectoAccentBrush`) with no tile, tinted box or circle behind it, Alert stacked buttons, and Form side-by-side buttons per SPEC 9.9. Done when: both dialog layouts render with zero hex or radius literals and match SPEC 9.9.
- [ ] Author `src/app/dialogs/password_dialog.h/.cpp` with the `24 epx` `lock_closed` glyph in `RectoAccentBrush` (no tile), `"Enter the password"`, `"<file> is protected."`, `ui::PasswordBox`, `Open` (primary accent), and `Cancel`. Done when: opening the dialog places keyboard focus directly inside the password box.
- [ ] Keep `DocumentTabSession` in its pre-render `is_locked` state while `PasswordDialog` is open so zero page outlines or page contents appear behind the dialog. Done when: SPEC 9.2's rule ("show the password dialog before any page appears") holds in visual and state assertions. Cheaper substitute that fails the checkpoint: showing blank white pages behind the password dialog.
- [ ] Keep `PasswordDialog` open while `DocumentActor::authenticate` runs on the actor thread (the primary handler disables the buttons and shows progress, and `ui::Dialog` closes only on success). Done when: the UI thread stays responsive during AES-256 `R=6` key-derivation rounds.
- [ ] On `ErrorCode::InvalidPassword`, keep `PasswordDialog` open in place, apply `RectoDangerBrush` to the password box border, show `"That password didn't work."` with a warning icon (colour is never the only signal per SPEC 11), and select the password text for immediate re-typing. Done when: a wrong password attempt never dismisses/re-animates the dialog.
- [ ] On `Cancel` button click or `Esc`, dismiss `PasswordDialog` and tear down the pending `DocumentTabSession` and its `DocumentActor` through §3's close path. Done when: cancelling a password prompt leaves zero zombie tabs or actors.
- [ ] Add `Shell.PasswordDialogFlow` test in `tests/recto-tests/` driving wrong-password retry, correct-password unlock, and cancel teardown against the encrypted corpus PDF. Done when: `recto-tests.exe --gtest_filter=Shell.PasswordDialogFlow` passes.
- [ ] Commit: `app: add shared SPEC 9.9 ui::Dialog presets and PasswordDialog retry/cancel flow`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.PasswordDialogFlow` passes on the display-session host, and screenshots of `PasswordDialog` (initial and wrong-password states) match `D-Dialogs.png`.

## 11. Could-not-open error dialog and diagnostic clipboard copy

Implement the SPEC section 9.9 `"Couldn't open this file"` alert `ui::Dialog` (`src/app/dialogs/could_not_open_dialog.h/.cpp`): displays the `24 epx` warning icon, `"Couldn't open this file"`, `"<file> is damaged or isn't a PDF."`, and full-width stacked `OK` (primary) and `Copy error details` buttons. Clicking `Copy error details` formats a privacy-safe diagnostic block (filename without private parent directory paths, `PdfError` code and message, recent `recto::pdf::diagnostics_tail()` ring-buffer lines, Recto version, and `recto-pdf` ABI version) onto the Windows clipboard through `ui::Clipboard` (`D10 T01 §17`).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.9 (`Could not open` row, Alert layout with centred icon/title/text and full-width stacked buttons) with `design/screenshots/D-Dialogs.png`.
**Job:** When a file is corrupt, truncated, or not a PDF, the user gets a clear explanation and a one-click `Copy error details` payload ready to paste into a bug report without exposing their private filesystem hierarchy. Consumer: `OpenDocumentPipeline` (§5).
**Treatment:** Alert-layout `ui::Dialog` consuming the `RectoDialogStyle` presets (§10) and `recto::pdf::diagnostics_tail()` (`D01 T01 §1`) with path scrubbing. Cheaper substitute that fails the checkpoint: a generic Win32 `MessageBoxW`, silent open failure, or copying full `C:\Users\<username>\...` paths into the clipboard diagnostic block.
**Chrome:** consume the Alert layout from the `RectoDialogStyle` presets (§10), `RectoWarningBrush`, and the `RectoIcons` `warning` glyph (`24 epx`, drawn in `RectoWarningBrush` with no tile behind it).

**Build order:**

1. Implement `format_open_error_report(filename, pdf_error, diagnostics)` in `src/app/dialogs/could_not_open_dialog.h/.cpp` that strips parent directory paths (keeping only `path.filename()`), formats the `ErrorCode` name, `PdfError::message`, byte offset (if present), last diagnostic ring-buffer entries for that failure, Recto version, and `recto::pdf::abi_version()`. Done when: unit tests confirm the generated report contains the filename, error code, and engine version while containing zero parent directory segments (`C:\Users\...`).
2. Build `CouldNotOpenDialog` (`src/app/dialogs/could_not_open_dialog.h/.cpp`) in the SPEC 9.9 Alert layout (centred `24 epx` warning icon, `"Couldn't open this file"`, `"<file> is damaged or isn't a PDF."`, stacked full-width `OK` and `Copy error details` buttons), wire `Copy error details` to `ui::Clipboard`, and connect the dialog to `OpenDocumentPipeline` (§5) with a queue so multiple bad files dropped together show sequentially (`ui::Dialog` is modal and one at a time). Done when: opening every file in `tests/corpus/broken/` presents `CouldNotOpenDialog`, copies the scrubbed diagnostic text to the clipboard, and leaves the app clean.

- [ ] Author `src/app/dialogs/could_not_open_dialog.h/.cpp` using the Alert layout from the `RectoDialogStyle` presets (centred `24 epx` warning glyph, `"Couldn't open this file"`, `"<file> is damaged or isn't a PDF."`, full-width stacked `OK` and `Copy error details` buttons). Done when: the rendered dialog matches the `Could not open` board in `D-Dialogs.png`.
- [ ] Implement `format_open_error_report` combining `path.filename()`, `PdfError` code/message/offset, `recto::pdf::diagnostics_tail()`, and engine/app version strings while scrubbing all directory prefixes above the filename. Done when: the formatted string for `C:\Users\Alice\Secret\bad.pdf` contains `bad.pdf` and never `Alice` or `Secret`. Cheaper substitute that fails the checkpoint: dumping raw full paths or omitting the engine error details.
- [ ] Wire `Copy error details` to copy the formatted report to the Windows clipboard (`ui::Clipboard`) and update the button label briefly to `"Copied"` with a screen-reader announcement (UIA notification, `D10 T03 §4`). Done when: clicking `Copy error details` places the exact diagnostic block on the system clipboard.
- [ ] Queue dialog presentations in `OpenDocumentPipeline` so dropping 3 broken files in one batch displays `CouldNotOpenDialog` sequentially rather than opening overlapping modal dialogs. Done when: dropping 3 corrupt files shows 3 dialogs in sequence with zero overlap or exceptions.
- [ ] Ensure the failed tab's temporary `DocumentTabSession` and `DocumentActor` are completely torn down before or when `CouldNotOpenDialog` opens. Done when: no broken tab remains in `ui::TabStrip` after a failed open.
- [ ] Add `Shell.CouldNotOpenAndDiagnostics` test in `tests/recto-tests/` opening `tests/corpus/broken/` files, asserting path-scrubbed diagnostic output on the clipboard, and verifying sequential multi-error dialog queuing. Done when: `recto-tests.exe --gtest_filter=Shell.CouldNotOpenAndDiagnostics` passes.
- [ ] Commit: `app: implement CouldNotOpenDialog with path-scrubbed diagnostic clipboard copy`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.CouldNotOpenAndDiagnostics` passes on the display-session host against `tests/corpus/broken/`, clipboard contents match the scrubbed diagnostic format, and screenshot matches `D-Dialogs.png`.

## 12. Crash recovery snapshots for unsaved edits

Users of PDF editors commonly lose edits when the app crashes or the machine restarts; a typical autosave runs only when the window repaints, skips new documents, marks failed snapshots as written, and lets a second instance list the first instance's live snapshots as crash leftovers. Recto keeps a recovery snapshot for each dirty document on a timer, independent of input and repaint, and offers to restore after an abnormal exit. A snapshot holds document content on disk, so the owner decided its defaults on 2026-10-21 (foundation plan, owner decisions after the decision table): on by default, written every 5 minutes while a document is dirty, and a snapshot of an encrypted document is written with that document's own security handler, passwords, permissions and `/EncryptMetadata` setting, so it is exactly as protected as the original and never more readable, with no plaintext copy written at any point.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.9 dialog chrome; the restore prompt is a genuinely new dialog with no screenshot. The setting lives in SPEC 9.7's `Performance and privacy` group (`D02 T04 §6`). Design board: `design/SPEC.md` section 9.18 with `design/screenshots/D-Session.png` (drawn by `D06 T04 §24`).
**Job:** A user whose Recto session ended abnormally reopens Recto and restores each unsaved document, or discards it. Consumer: `DocumentTabSession` dirty state (§3), `AtomicFileWriter` (`D04 T02 §1`) and the `SettingsStore` interval setting.
**Treatment:** One restore dialog listing recoverable documents with their original names and snapshot times, `Restore` and `Discard` per row. Cheaper substitute that fails the checkpoint: silently reopening snapshots as if they were the originals.
**Chrome:** Shared `RectoDialogStyle` presets (§10), `ui::Dialog` and `ui::ItemsView` (`D10 T02 §11`), generated tokens.

**Build order:**

1. Read the owner decision recorded 2026-10-21 in the foundation plan (on by default, 5 minute interval, encrypted snapshots kept encrypted) and make it the `SettingsStore` default for the recovery setting. Done when: the decision line exists in the plan and a fresh settings file reads `recovery.enabled = true` and `recovery.interval_minutes = 5`.
2. Implement `src/services/recovery_store.h/.cpp` writing a full PDF snapshot of each dirty document through `AtomicFileWriter` into `AppPaths::recovery_dir()` on a timer, with a per-instance lock file. Done when: `Shell.RecoverySnapshots` finds a snapshot after the interval with no input events.

- [ ] Record the decision in `docs/plans/2026-10-13-recto-foundation.md` and `docs/user-demand.md`. Done when: both files name the decision and its date.
- [ ] Write snapshots from a thread-pool timer at the configured interval while any document is dirty, never from the render or input loop. Done when: the test sees a snapshot after one interval with the window idle and unfocused.
- [ ] Mark a snapshot written only after `AtomicFileWriter` reports a verified replace, and retry a failed snapshot on the next tick without a new edit. Done when: an injected write failure is retried and the second tick succeeds.
- [ ] Cover new, never-saved documents and documents opened from read-only or temporary locations. Done when: a new document's edits are recoverable.
- [ ] Hold a lock file per running instance and offer restore only for snapshots whose owning instance is gone. Done when: a second instance lists none of the first instance's live snapshots.
- [ ] Keep snapshots of encrypted documents encrypted with the document's own security handler, passwords, permissions and `/EncryptMetadata` setting (owner decision 2026-10-21; never skip them and never write them in the clear): the writer encrypts every string and stream in memory before the first byte reaches `AtomicFileWriter`, so the temporary file is encrypted from its first write. Done when: `Shell.RecoveryEncryptedSnapshots` finds that a snapshot of an AES-256 fixture with a non-empty user password cannot be opened without it; that a snapshot of a fixture with an empty user password and of one with `/EncryptMetadata false` reopen with exactly the original's security handler, `/P` and metadata encryption; and that a planted plaintext canary string from each fixture's content stream is absent from the recovery folder, including from the temporary file of a write interrupted halfway by an injected failure.
- [ ] Delete a document's snapshot on save, on close with `Don't save` and after `Discard`, and keep Portable mode snapshots under the portable data folder. Done when: the recovery folder is empty after each path and Portable mode writes nothing to `%LOCALAPPDATA%`.
- [ ] Build `RecoveryDialog` shown at startup after an abnormal exit, restoring into new dirty tabs named after the originals, never overwriting the original files. Done when: restore opens a dirty tab and the original file's hash is unchanged. Cheaper substitute: restoring by replacing the original file.
- [ ] Commit: `app: keep crash recovery snapshots of unsaved documents and offer restore`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.RecoverySnapshots:Shell.RecoveryDialog:Shell.RecoveryEncryptedSnapshots` passes on the display-session host, including the idle-timer, retry, second-instance and Portable mode cases, and writing an encrypted document's snapshot in the clear before encrypting it fails `Shell.RecoveryEncryptedSnapshots` on the interrupted-write canary.

## 13. `SessionStore`: persist open tabs and the active tab

UPDF's and Foxit PDF Editor's restore-last-session behaviour (Acrobat Pro also reopens its tabs) needs the open tabs to survive an exit; today only `RecentFilesStore` remembers files. This section is the storage half: a small local `session.json` holding each open tab's path and view state, never document content. It keeps Recto's rules: offline, temp-file-then-replace, Portable mode data folder, no telemetry.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/services/session_store.h/.cpp` with `SessionSnapshot` (`schema_version`, `saved_utc`, `clean_exit`, `active_index`, and per tab `canonical_path`, `page_index`, `zoom_factor`, `zoom_mode`, `scroll_offset_y`, `layout_mode`) and `SessionStore::save` / `load` writing through temp-file-then-replace (`ReplaceFileW` or `MoveFileExW`) to `AppPaths::session_path()`. Done when: a save then reload reproduces the snapshot field for field, and an injected write failure leaves the previous `session.json` unchanged.
2. Skip tabs that cannot be restored safely: documents that needed a password, documents opened from a temporary or cloud cache folder, and tabs still loading. Debounce writes to 2 s after the last tab or view change; write at once on tab close and clean exit. Done when: a fixture with one encrypted, one temp-folder and two normal tabs saves exactly the two normal tabs.

- [ ] Add `AppPaths::session_path()` beside `settings_path()` (`D02 T04 §1`) so Portable mode keeps the file under the portable data folder. Done when: Portable mode writes no session file to `%LOCALAPPDATA%`.
- [ ] Return an empty snapshot for a corrupt, truncated or newer-schema `session.json` and record one `recto::pdf::diagnostics_tail()` line. Done when: a planted `{corrupt` file loads as zero tabs without throwing.
- [ ] Cap the snapshot at 50 tabs. Done when: a 200-tab synthetic snapshot saves 50 tabs.
- [ ] Store paths and view state only: no page text, thumbnails or document bytes. Done when: `Shell.SessionStorePersistence` finds a sentinel string placed in the PDF body nowhere in the saved file.
- [ ] Add `Shell.SessionStorePersistence` in `tests/recto-tests/` covering round trip, atomic failure, skipped tabs, corrupt file, cap and the Portable path. Done when: `recto-tests.exe --gtest_filter=Shell.SessionStorePersistence` passes.
- [ ] Commit: `services: add SessionStore for open tab persistence`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.SessionStorePersistence` passes, and the atomic-failure case fails if `session.json` is rewritten in place.

## 14. Restore the previous tab session on launch and the restore setting

UPDF's and Foxit's session restore, matched here: on launch Recto reopens the tabs from `SessionStore` (§13), the active tab first, at their saved page and zoom, unless the user turned it off. It must not open a file twice (§5 dedup), must not fight crash recovery (§12) or file arguments (§7), and must report missing files once instead of failing silently. Offline, no scripts, tokens only, and reachable from `Ctrl+K`.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new behaviour; the setting row follows `design/SPEC.md` section 9.7 (Reading group) and the toast follows section 9 toast chrome. Design board: `design/SPEC.md` section 9.18 with `design/screenshots/D-Session.png` (drawn by `D06 T04 §24`).
**Job:** A user who closes Recto with a set of documents open gets the same tabs back at the same pages next time, and can switch that off. Consumer: `OpenDocumentPipeline` (§5), `SettingsStore` (`D02 T04 §2`).
**Treatment:** `ui::ToggleSwitch` row (`D10 T02 §7`) and the existing `ui::Toast` (`D10 T02 §20`); restore runs through the one open pipeline. Cheaper substitute that fails the checkpoint: a second code path that opens documents directly, or restoring all tabs in parallel before the first page appears.
**Chrome:** generated tokens, the shared toast host, `CommandRegistry`.

**Build order:**

1. Add the setting `RestoreTabsOnLaunch` (default on, because `RememberReadingPosition` is already on by default and stores the same facts) to `RectoSettings` and a `Restore tabs on launch` row in the Reading group of the Settings page (`D02 T04 §5`). Done when: the row toggles the setting and a restart honours it.
2. After `ShellPage` loads, call `OpenDocumentPipeline::open_files_async` for each saved tab in order: open the active tab first, then the rest one at a time after the first page renders, at most 20 restored tabs; collect missing files and show one in-app toast `N files from your last session were not found` naming up to three of them. Done when: a session of 5 tabs reopens all 5 with the saved active tab selected and the first page visible before the 5th tab starts loading.

- [ ] Run restore after the recovery dialog (§12) and treat files named on the command line or by Explorer activation (§7) as additional tabs opened last and selected. Done when: launching with `second.pdf` while a 2-tab session exists yields 3 tabs with `second.pdf` active.
- [ ] Skip restore when `RestoreTabsOnLaunch` is off, and in that case do not write `session.json`. Done when: with the setting off no `session.json` exists after a run.
- [ ] Show the missing-files toast through the existing toast host and expose the names in its details `ui::Flyout`, with an automation name. Done when: deleting two files between runs shows one toast naming both.
- [ ] Register `Restore tabs on launch` as a toggle command in `CommandRegistry` (`D02 T03 §11`). Done when: the command is reachable from `Ctrl+K` and flips the setting.
- [ ] Add `Shell.SessionRestoreOnLaunch` covering order, active tab, dedup, missing files, argument precedence and the off setting. Done when: `recto-tests.exe --gtest_filter=Shell.SessionRestoreOnLaunch` passes. Cheaper substitute: a restore that opens every tab in parallel at startup.
- [ ] Commit: `app: restore the previous tab session on launch`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.SessionRestoreOnLaunch` passes on the display-session host.

## 15. Reopen closed tab (`Ctrl+Shift+T`) and close-other, left and right tab commands

UPDF's tab menu closes tabs to the left or right and reopens the last closed tab with `Ctrl+Shift+T`, as browsers do; Acrobat Pro has no reopen command. Recto's `ui::TabStrip` today closes one tab at a time (§3). This section adds a bounded closed-tab stack and the batch close commands, every one routed through §8's unsaved-changes prompt so no edit is lost, and reachable from the tab context menu and `Ctrl+K`.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.1 (title bar tabs) and 10 (keyboard); the context menu is a `ui::Menu` (`D10 T02 §14`), no screenshot. Design board: `design/SPEC.md` section 9.18 with `design/screenshots/D-Session.png` (drawn by `D06 T04 §24`).
**Job:** A user who closed the wrong tab gets it back at the same page with one key, and can clear a crowded tab strip in one command without losing edits. Consumer: `DocumentTabSession` (§3), `SavePipeline` (§8), `CommandRegistry` (`D02 T03 §11`).
**Treatment:** `ui::TabStrip` and `ui::Menu` over a bounded in-memory stack. Cheaper substitute that fails the checkpoint: closing batches without the unsaved-changes prompt, or keeping closed tabs alive in memory.
**Chrome:** generated tokens and Fluent UI System Icons (`RectoIcons`); no second tab control.

**Build order:**

1. Implement `ClosedTabStack` (`src/app/ClosedTabStack.h/.cpp`): a last-in first-out list of at most 10 entries holding `canonical_path`, `page_index`, `zoom_factor`, `layout_mode` and the tab's position; push on every tab close that was not discarded because the file is gone; pop restores through `OpenDocumentPipeline` (§5). Done when: closing 3 tabs then pressing `Ctrl+Shift+T` three times reopens them in reverse order at their saved pages.
2. Add `Close other tabs`, `Close tabs to the right`, `Close tabs to the left` and `Reopen closed tab` to the tab context menu, the `ShellMenuBar` `Window` menu (§9) and `CommandRegistry`; a batch close asks `Save your changes first?` for each dirty tab in order and stops the whole batch when the user presses `Cancel`. Done when: closing 4 tabs with one dirty tab prompts once, and `Cancel` leaves all remaining tabs open.

- [ ] Bind `Ctrl+Shift+T` in the window accelerator table (`D10 T01 §12`) and skip it while a text box has focus and the key means something there. Done when: `Ctrl+Shift+T` from the canvas reopens the last closed tab and from the find box does not.
- [ ] Disable `Reopen closed tab` when the stack is empty and when the top entry's file no longer exists, removing that entry and showing the toast `That file is gone`. Done when: a deleted file's entry is dropped and the next entry is offered.
- [ ] Keep dirty tabs out of the stack until their close completes with `Discard` or `Save`, and record only the on-disk state for a discarded tab. Done when: discarding a dirty tab pushes the on-disk state only.
- [ ] Put `Copy path` and `Show in folder` on the same context menu through `CommandRegistry`. Done when: both commands act on the right-clicked tab, not the active tab.
- [ ] Give every new command an automation name and a tooltip with its shortcut. Done when: `Panels.CommandRegistryCompleteness` still passes.
- [ ] Add `Shell.ClosedTabStackAndBatchClose` covering stack order, the 10 cap, missing files, dirty batch with `Cancel`, and the key guard in a text box. Done when: `recto-tests.exe --gtest_filter=Shell.ClosedTabStackAndBatchClose` passes. Cheaper substitute: closing dirty tabs in a batch without the prompt.
- [ ] Commit: `app: add reopen closed tab and batch tab close commands`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.ClosedTabStackAndBatchClose` passes on the display-session host.

## 16. Drag to reorder tabs and move a tab by keyboard

UPDF lets users drag document tabs into a new order; the strip is fixed today. Reordering must also work without a pointer (`Ctrl+Shift+PageUp` and `Ctrl+Shift+PageDown`), keep the title-bar drag regions correct, and save the new order in the session (§13). Tab tear-off into a second window is not planned (one shell, D17).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.1 (tabs) and 11 (keyboard and screen-reader operation). Design board: `design/SPEC.md` section 9.18 with `design/screenshots/D-Session.png` (drawn by `D06 T04 §24`).
**Job:** A user arranges tabs to match their work and does the same from the keyboard. Consumer: `DocumentTabSession` (§3), `SessionStore` (§13).
**Treatment:** `ui::TabStrip` drag reordering (`D10 T02 §13`) plus two commands. Cheaper substitute that fails the checkpoint: drag-only reordering, or reordering that leaves stale non-client hit regions.
**Chrome:** generated tokens; the existing tab strip.

**Build order:**

1. Enable `ui::TabStrip` reordering within the strip and disable cross-window drags, handle its reorder event to move the `DocumentTabSession` in the session list, and recompute the non-client hit regions (`D10 T01 §19`) after each move. Done when: dragging tab 3 before tab 1 reorders the session list and tab headers stay clickable.
2. Add `Move tab left` and `Move tab right` commands on `Ctrl+Shift+PageUp` and `Ctrl+Shift+PageDown` that move the active tab one slot and announce `Tab moved to position N of M` through a polite UIA live region. Done when: the keys move the tab and Narrator reads the new position.

- [ ] Keep `Home` pinned as the first tab and refuse to drop a document tab before it. Done when: dropping a tab left of `Home` lands it right of `Home`.
- [ ] Save the new order through `SessionStore` (§13) on the 2 s debounce. Done when: reorder, close, relaunch restores the new order.
- [ ] Disable drag reordering while a modal dialog is open and respect the Windows animation setting through `ui::Motion` (`D10 T01 §14`, `SPI_GETCLIENTAREAANIMATION`) for the slide animation. Done when: with animations off the tab jumps without a transition.
- [ ] Register the two move commands in `CommandRegistry` (`D02 T03 §11`). Done when: both are reachable from `Ctrl+K`.
- [ ] Add `Shell.TabReorderAndKeyboardMove` covering drag order, keyboard order, `Home` pin and session persistence. Done when: `recto-tests.exe --gtest_filter=Shell.TabReorderAndKeyboardMove` passes. Cheaper substitute: a pointer-only reorder with no keyboard equivalent.
- [ ] Commit: `app: reorder tabs by drag and keyboard`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.TabReorderAndKeyboardMove` passes on the display-session host.

## 17. Share a copy through the Windows Share contract

UPDF, PDFelement and Foxit PDF Editor share or email the open file (Acrobat Pro has Share and Send by email too); Recto does it with the Windows Share contract (`IDataTransferManagerInterop::ShowShareUIForWindow` and `DataTransferManager`) so the user's installed mail, Teams or Nearby Sharing target does the sending and Recto itself opens no network connection (D16). If the tab has unsaved changes the user chooses between sharing the saved file and sharing a fresh copy.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; the Share sheet is the Windows one and the choice dialog follows `design/SPEC.md` section 9.9 form layout. Design board: `design/SPEC.md` section 9.18 with `design/screenshots/D-Session.png` (drawn by `D06 T04 §24`).
**Job:** A user sends the open PDF, or a saved copy of it, through any installed share target without leaving Recto. Consumer: `SavePipeline` (§8), `CommandRegistry` (`D02 T03 §11`).
**Treatment:** `DataTransferManager` with the window HWND and a StorageFile, copy through the existing save path. Cheaper substitute that fails the checkpoint: building a mail or upload client inside Recto, or sharing a dirty document's stale bytes without saying so.
**Chrome:** the `RectoDialogStyle` presets (§10) and Fluent UI System Icons (`RectoIcons`).

**Build order:**

1. Implement `ShareService::share_file_async(session)` in `src/app/ShareService.h/.cpp`: bind `DataTransferManager` with the `MainWindow` HWND, answer `DataRequested` with the file as `StorageItems` (title the file name, description `Shared from Recto`), and fail with a typed message when the OS refuses the call. Done when: sharing a corpus PDF opens the Windows Share sheet listing installed targets, and an injected refusal shows `Sharing isn't available here` without a crash.
2. For a dirty tab show a `ui::Dialog` (§10 presets) with `Share saved file` and `Save a copy and share`; the second path writes a copy through `SavePipeline::save_copy_async` into a `share` folder under `AppPaths::temp_dir()`, shares it, and deletes it when the share completes, with a sweep of stale files at next launch. Done when: sharing a dirty tab with the second choice sends bytes that include the edit and leaves the temp folder empty afterwards.

- [ ] Add `Share...` to the `File` menu (§9), the `More` menu of `PageToolbar` (`D02 T02 §8`), the tab context menu and `CommandRegistry`. Done when: `Ctrl+K` finds `Share`.
- [ ] Disable `Share` on `Home`, while a tab is loading and for password-locked tabs. Done when: each state shows the command disabled.
- [ ] Add a source scan that `ShareService` contains no WinHTTP, `LaunchUriAsync` or `mailto:` use. Done when: `Audit.ShareNoDirectNetwork` passes and fails when a banned call is added to a scratch copy.
- [ ] Share a fresh copy, never the cloud-cache temp file, for a document opened through a cloud connector (`D02 T06`). Done when: `Shell.ShareCopyForDirtyAndCloud` shares a copy for the cloud case.
- [ ] Add `Shell.ShareCopyForDirtyAndCloud` covering clean share, dirty share choices, temp cleanup and the refusal message. Done when: `recto-tests.exe --gtest_filter=Shell.ShareCopyForDirtyAndCloud` passes. Cheaper substitute: a `mailto:` link, which cannot attach the file.
- [ ] Commit: `app: share a copy through the Windows Share contract`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.ShareCopyForDirtyAndCloud:Audit.ShareNoDirectNetwork` passes on the display-session host.

## 18. `RectoIcons` subset icon font, stable code point map and glyph resources

`design/SPEC.md` section 6 (owner decision 2026-10-21, foundation plan D15 and the 2026-10-21 icons entry) makes Fluent UI System Icons (Microsoft, MIT, npm `@fluentui/svg-icons` pinned at `1.1.344`) the only source of Recto glyphs, delivered as the subset font `resources/fonts/RectoIcons.ttf` that holds exactly the section 6 table's glyphs (Regular and Filled) plus the two custom glyphs (Redact, Squiggly underline), drawn by `ui::Icon` (`D10 T01 §11`) from a private DirectWrite font set. No section built it, yet §2 and every later surface consume it, so it lands before §2. The build downloads nothing: the SVGs it needs are vendored into `resources/fonts/fluent-svg/` with the package's MIT licence by a separate, repeatable `--vendor` step, and custom glyphs live in `resources/fonts/custom/`. The SPEC table is the only glyph list: the script parses it, so a row added to SPEC section 6 becomes a glyph in the font through one documented workflow that later sections (the design boards of `D06 T04`) reuse: add the row, run `--vendor` (which copies the new name's two SVGs from the same pinned package and records their hashes in `SOURCE.md` and their names in `NOTICE.md`), or for a concept the set lacks draw a custom glyph under the SPEC section 6 acceptance rule, then rebuild. Default recorded here: the outlines are compiled with `fontTools` (the standard OpenType library for Python), pinned to one exact version in `scripts/requirements-icon-font.txt`; changing it later only rewrites the outline step of the script.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Write `scripts/build-icon-font.py --vendor`: unpack `npm pack @fluentui/svg-icons@1.1.344` in `R:\scratch`, check the tarball against the registry's `dist.integrity`, and for every distinct Fluent name in the SPEC section 6 table that has no file yet, copy only `icons/<name>_20_regular.svg` and `icons/<name>_20_filled.svg` into `resources/fonts/fluent-svg/`, append each file's name and SHA-256 to `resources/fonts/fluent-svg/SOURCE.md` (which names the package, version and tarball integrity), add the name to the Fluent UI System Icons entry of `NOTICE.md`, and copy the package `LICENSE` once. It never changes an existing file, and it refuses a different package version than the one `SOURCE.md` records. Run it once now for the initial table. Done when: the folder holds exactly two SVGs per distinct table name plus `LICENSE` and `SOURCE.md`, a second run changes nothing, and a name the pinned package lacks is reported by name (the fix is a dated correction of that SPEC section 6 row to the nearest existing 20 px glyph, or a custom glyph under the SPEC section 6 rule, never a silent substitute).
2. Draw the custom glyphs `resources/fonts/custom/recto_redact_20_regular.svg`, `recto_redact_20_filled.svg`, `recto_squiggly_20_regular.svg` and `recto_squiggly_20_filled.svg` on the 20 px grid in the Fluent style (filled shapes, no strokes, the 20 x 20 `viewBox` of the vendored files) under the SPEC section 6 custom-glyph acceptance rule. Done when: each file has a `viewBox="0 0 20 20"` and only `path` elements, matching the vendored files' structure.
3. Write the build in `scripts/build-icon-font.py`: parse the SPEC section 6 table, map each concept to its glyph name (a `custom glyph` row for concept `<Concept>` maps to `recto_<concept in snake case>` read from `resources/fonts/custom/`, today `recto_redact` and `recto_squiggly`; every other row reads `resources/fonts/fluent-svg/`), compile each SVG to TrueType outlines, assign each `<name>_regular` and `<name>_filled` a Private Use Area code point from `resources/fonts/RectoIcons.codepoints.json` (append only: existing names keep their code point, new names take the next free value from `U+E000`), and write the font with fixed `head` timestamps and a sorted glyph order so a rebuild is byte-identical. Done when: two consecutive runs produce identical bytes for `resources/fonts/RectoIcons.ttf`.
4. Generate `src/app/recto_icon_glyphs.h` (one `constexpr wchar_t const*` per glyph in `recto::icons`, the names `ui::Icon` resolves by SPEC section 6 concept name) from the code point map, and copy the font into the build output as `Assets/Fonts/RectoIcons.ttf` through `src/app/Recto.vcxproj`, where `ui::Icon` loads it as a private DirectWrite font set (`D10 T01 §11`). Done when: `build/bin/x64/Debug/Assets/Fonts/RectoIcons.ttf` exists after `.\build.cmd` and `recto_icon_glyphs.h` lists every glyph.
5. Add `--check` (rebuild in memory and compare with the committed font, map and header) and the tests. Done when: `python scripts/build-icon-font.py --check` exits 0 on the committed outputs and 1 after any one of them is edited.

- [ ] Implement `--vendor` in `scripts/build-icon-font.py` and run it to vendor the pinned SVGs into `resources/fonts/fluent-svg/` with `LICENSE` and `SOURCE.md` (package, version, tarball integrity, per-file SHA-256), copying only the glyphs the SPEC section 6 table names and adding each name to `NOTICE.md`. Done when: the file count equals two per distinct table name, `SOURCE.md` lists every file with its hash, and on a fixture SPEC with one added row `--vendor` adds exactly that name's two SVGs, two `SOURCE.md` rows and one `NOTICE.md` name while leaving every existing file byte-identical.
- [ ] Draw `recto_redact` and `recto_squiggly` in Regular and Filled under `resources/fonts/custom/` on the 20 px grid. Done when: the four SVGs exist with `viewBox="0 0 20 20"` and paths only. Cheaper substitute: an emoji, a stroke-only drawing or a Segoe glyph, which SPEC section 6 forbids.
- [ ] Pin `fontTools` to one exact version in `scripts/requirements-icon-font.txt` and make `scripts/build-icon-font.py` exit with a clear install hint when it is missing. Done when: running the script without `fontTools` prints the install command and exits 2.
- [ ] Parse the SPEC section 6 table in `scripts/build-icon-font.py` into the distinct glyph set (custom rows included, read from `resources/fonts/custom/`), failing on a row whose SVG is missing with the fix in the message (`run --vendor` for a Fluent name, draw the files under `resources/fonts/custom/` for a custom row). Done when: removing one vendored SVG makes the build exit 1 naming that concept, glyph and fix, and removing one custom SVG does the same for its custom row.
- [ ] Compile Regular and Filled outlines into `resources/fonts/RectoIcons.ttf` with family name `RectoIcons`, fixed timestamps and sorted glyph order. Done when: two runs give the same SHA-256.
- [ ] Assign code points through the append-only map `resources/fonts/RectoIcons.codepoints.json`. Done when: adding a name gives it the next free code point and leaves every existing one unchanged.
- [ ] Generate `src/app/recto_icon_glyphs.h` from the map. Done when: the header lists every glyph and `ui::Icon` resolves every concept name in it.
- [ ] Copy the font into the app output through a `Content` item in `src/app/Recto.vcxproj`. Done when: the x64 and ARM64 build outputs both contain `Assets/Fonts/RectoIcons.ttf`.
- [ ] List every vendored glyph name (written by `--vendor`) and both custom glyphs under the Fluent UI System Icons entry of `NOTICE.md`, with the MIT notice. Done when: every name in `RectoIcons.codepoints.json` appears in `NOTICE.md`.
- [ ] Add `Shell.IconFontSpecimen` in `tests/recto-tests/`: for every name in `RectoIcons.codepoints.json` (Regular and Filled, the two custom glyphs included), render the code point from the font loaded as a private DirectWrite font set at 16, 20 and 24 epx into a WIC bitmap with Direct2D (the same drawing path `ui::Icon` uses, without depending on it), rasterize the source SVG at the same size with Direct2D (`ID2D1DeviceContext5::CreateSvgDocument`), and require non-empty ink, no ink on the outer pixel rows and columns (no clipping) and an alpha-mask intersection over union of at least 0.9 with the SVG raster (so an empty, flipped or offset outline fails); run it against the font in the unpackaged output and in the `-EnableMsix` package layout. Done when: `recto-tests.exe --gtest_filter=Shell.IconFontSpecimen` passes, and fails on a fixture font with one glyph's outline emptied, one mirrored vertically and one shifted 4 units outside the em box.
- [ ] Add `tests/scripts/test_build_icon_font.py` asserting: every SPEC section 6 glyph is in the font in both forms; no glyph outside the table (and `.notdef`) is present; no existing name's code point changed against the committed map; every map name is in `NOTICE.md`; `--check` passes. Done when: `python -m unittest tests.scripts.test_build_icon_font` passes, and fails on a fixture SPEC with an extra row, a fixture font with an extra glyph, and a fixture map with a moved code point.
- [ ] Commit: `resources: build the RectoIcons subset font from the pinned Fluent UI System Icons`

**Test checkpoint:** `python -m unittest tests.scripts.test_build_icon_font` passes, `python scripts/build-icon-font.py --check` exits 0 and `recto-tests.exe --gtest_filter=Shell.IconFontSpecimen` passes on the display-session host; a fixture font with an emptied, mirrored or shifted outline fails the specimen test, and adding a row to a fixture copy of the SPEC section 6 table without rebuilding, adding an unlisted glyph to a fixture font, or changing an existing name's code point in a fixture map each makes the test fail.

## 19. `RectoChipStyle` pill style for chips, filter tags, keycaps, badges and counters

`design/SPEC.md` section 7 (revised 2026-10-21) allows three corner radii: 4 epx on controls, 8 epx on overlays, and a full pill (radius half the height) through the style `RectoChipStyle`, used only for chips and filter tags, shortcut keycaps, status badges and counters. §2 (the `Ctrl K` keycaps on the command box button), `D02 T03 §3` (panel switcher count badges), `D02 T03 §12` (command box shortcut keycaps) and `D05 T02 §1` (selected-card check badge) already consume the style, and nothing created it; §10's `RectoDialogStyle` presets cannot host it because §10 sits after §5, which comes after §2. `recto-ui` supplies the generic pill elements (`ui::Chip`, `ui::Badge` and `ui::Keycap`, `D10 T02 §20`, with the pill radius from `ui::Painter`, `D10 T01 §9`); this section owns the Recto variants, metrics, tones and hit rules on top of them, and the one place where the pill radius is computed from the height.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 7 (full pill, `RectoChipStyle`, only chips, filter tags, keycaps, badges and counters), 6 (glyph colour rules), 9.4 (shortcut keycaps), 9.5 (removable file chips) and 11 (colour never the only signal; hit targets at least 32 x 32 epx), with `design/screenshots/D-Home.png` (key hints) and `D-Dialogs.png` (command box keycaps).
**Job:** Every chip, filter tag, keycap, status badge and counter in Recto looks and behaves the same, so the user reads a pill as "a tag or a key", never as a button. Consumer: §2, `D02 T03 §3`, `D02 T03 §12`, `D02 T04 §4`, `D05 T02 §1`, and every later chip (Convert file chips, Comments filters, review status chips).
**Treatment:** One `RectoChip` element built on the `D10 T02 §20` pill elements with its default style `RectoChipStyle`, whose border radius is recomputed as half the rendered height. Cheaper substitute that fails the checkpoint: a literal radius per control, a 4 epx rounded rectangle, or a status badge that shows colour without text or glyph.
**Chrome:** `RectoBorderStrongBrush`, `RectoSurfaceBrush`, `RectoTextSecondaryBrush`, `RectoAccentTintBrush` and `RectoAccentBrush` (selected), `RectoSuccessBrush`, `RectoWarningBrush` and `RectoDangerBrush` (badge tones, always with text or a `RectoIcons` glyph), the `Caption` text style.

**Build order:**

1. Author `src/app/controls/recto_chip.h/.cpp`, a `ui::Element` built on `ui::Chip`, `ui::Badge` and `ui::Keycap` (`D10 T02 §20`) with `Kind` (`Chip`, `FilterTag`, `Keycap`, `Badge`, `Counter`), `Tone` (`Neutral`, `Accent`, `Success`, `Warning`, `Danger`), `IsSelected` and `IsRemovable` (a dismiss button with automation name `Remove <label>` and a tooltip) as `ui::Property<T>` values, setting the visual pill's corner radius to half of that pill's arranged height on every layout. The visual pill and the hit area are separate parts: for the interactive kinds (`Chip`, `FilterTag`) the control's hit bounds are at least 32 x 32 epx (SPEC 11), with the pill centred vertically inside a transparent hit region that still receives input, and a removable chip's dismiss button owns its own 32 x 32 epx hit box at the trailing end (its glyph is 12 epx), carved out of the chip body's hit region so a press lands on exactly one of them. `Keycap`, `Badge` and `Counter` are not interactive (no pointer or keyboard activation), so the hit rule does not apply to them. Done when: a chip whose pill renders at 16, 20, 24 and 32 epx height reports a pill corner radius of exactly half the pill height, and every interactive chip's hit bounds and dismiss box measure at least 32 x 32 epx.
2. Author `src/app/controls/recto_chip_style.h` with `RectoChipStyle` (default style of `RectoChip`): 20 epx high for `Keycap`, `Badge` and `Counter` and 24 epx for `Chip` and `FilterTag` (default recorded here; changing it is a one-line constant change), 8 epx horizontal padding, the `Caption` text style, and token brushes per `Tone`. Done when: each `Kind` and `Tone` renders with token colours in Light, Dark and HighContrast, and the 24 epx `Chip` and `FilterTag` pills sit in 32 epx hit bounds.

- [ ] Author `src/app/controls/recto_chip.h/.cpp` with `Kind`, `Tone`, `IsSelected` and `IsRemovable` as `ui::Property<T>` values. Done when: the control compiles and every property change re-applies its visual state.
- [ ] Recompute the visual pill corner radius as half of its own arranged height (not the hit bounds) in `RectoChip`'s arrange pass. Done when: resizing a chip keeps its ends fully round at every height. Cheaper substitute: a fixed literal radius that breaks when the text scale changes.
- [ ] Author `src/app/controls/recto_chip_style.h` with the `RectoChipStyle` metrics and token brushes. Done when: `RectoChip` resolves `RectoChipStyle` for every `Kind` without per-instance overrides.
- [ ] Render `Keycap` content in the `Caption` text style with `RectoTextSecondaryBrush` on `RectoSurfaceBrush` and a 1 epx `RectoBorderStrongBrush` edge, one pill per key (`Ctrl`, `K`). Done when: `Ctrl K` renders as two keycaps matching `D-Home.png` key hints.
- [ ] Render `Badge` tones `Success`, `Warning` and `Danger` with a 12 epx `RectoIcons` glyph (`checkmark`, `warning`, `dismiss`) beside the text. Done when: no tone can render with colour alone (SPEC 11).
- [ ] Give `Chip` and `FilterTag` hit bounds of at least 32 x 32 epx around the 24 epx pill in `RectoChip`'s hit test (a transparent, hit-testable outer region; the visual pill stays 24 epx), and activate them by click, `Enter` or `Space` (raising `Click` and toggling `IsSelected` for `FilterTag`). Done when: a pointer press 3 epx above the pill's top edge activates the chip, and `Enter` and `Space` on a focused chip raise the same `Click`. Cheaper substitute: a 24 epx hit area equal to the visual pill, which SPEC 11 forbids.
- [ ] Add the removable path: `IsRemovable` shows a dismiss button with its own 32 x 32 epx hit box at the trailing end, excluded from the chip body's hit region; `Delete` or `Backspace` on a focused chip raises `RemoveRequested`; the button has an automation name and tooltip. Done when: removing a chip by mouse and by keyboard raises the same event, a press inside the dismiss box raises `RemoveRequested` and never `Click`, and a press on the chip body outside it raises `Click` and never `RemoveRequested`.
- [ ] Expose `RectoChip` through UI Automation (`D10 T03 §1`) as a text element (`Keycap`, `Badge`, `Counter`) or a button (`Chip`, `FilterTag`) with its label as the name. Done when: Accessibility Insights lists every chip with a name and the right control type.
- [ ] Add a source scan that no source under `src/app` passes a numeric corner radius literal to the painter or a control. Done when: `Audit.NoCornerRadiusLiterals` passes and fails when a scratch copy passes a literal radius of `10`.
- [ ] Add `Shell.ChipStyleShape` asserting half-height pill radius at 16, 20, 24 and 32 epx, token brushes per `Tone` in all three themes, glyph presence on status tones, interactive hit bounds and dismiss box of at least 32 x 32 epx, chip activation by pointer (including a press in the transparent hit margin), `Enter` and `Space`, and dismissal by pointer on the dismiss box, `Delete` and `Backspace` with no cross-firing between `Click` and `RemoveRequested`. Done when: `recto-tests.exe --gtest_filter=Shell.ChipStyleShape` passes.
- [ ] Commit: `app: add RectoChipStyle pill style for chips, keycaps, badges and counters`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.ChipStyleShape:Audit.NoCornerRadiusLiterals` passes on the display-session host; replacing the computed radius with a literal makes `Shell.ChipStyleShape` fail at the 32 epx case, and shrinking the `FilterTag` hit bounds to the 24 epx pill, or letting a dismiss-box press also raise `Click`, makes it fail.

## 20. Classic status bar: current page, page size, PDF version and zoom slider

`design/SPEC.md` section 8.4 gives the Classic preset a status bar in the bottom dock slot: the current page (click to type a page), the page size, the PDF version, and the zoom percentage with a zoom slider, on `RectoSurfaceBrush` with a 1 epx `RectoBorderBrush` top edge, after the page in the keyboard order (section 11). `D03 T02 §4` already tests a `ClassicStatusBar` in the Classic tab order, but no section builds it; it belongs beside the Classic menu bar of §9. It reads and writes view state only through `CommandRegistry` and the viewport channel, never a second handler. Default recorded here: the status bar is shown exactly when `ShellMenuBar` is placed (the Classic preset, or a Custom layout whose page toolbar is the docked top command bar); changing that rule is a one-line change in the layout mapping. Because the page thumbnails setting is independent, a Custom layout can put the bottom filmstrip (`D02 T03 §4`) in `BottomDockSlot` together with the status bar; the composition rule is: the filmstrip stacks above the status bar (§4 `SlotOccupant` order 0 and 1), the status bar keeps a fixed 32 epx height (default recorded here, so its `NumberBox` and `Slider` meet the SPEC 11 32 epx hit target), the filmstrip keeps its own height, the canvas gives up the combined height, and in the keyboard order the filmstrip comes before the page and the status bar after it (§4 focus groups).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8 (bottom dock slot), 8.4 (status bar), 10 (`Ctrl+G`, `Ctrl+Plus`, `Ctrl+Minus`) and 11 (keyboard order: status bar after the page), with `design/screenshots/D-Classic.png`.
**Job:** A Classic user reads the page number, page size, PDF version and zoom at a glance, types a page number to jump, and drags the slider to zoom. Consumer: the viewport of `D02 T02 §6` and `§7`, `LayoutPresetEngine` (`D02 T04 §3`), and the Classic tab order test of `D03 T02 §4`.
**Treatment:** A `ClassicStatusBar` element in `BottomDockSlot` bound to the active tab's viewport state through `ui::Property<T>`, with go-to-page and zoom routed through `CommandRegistry`. Cheaper substitute that fails the checkpoint: static text that does not follow scrolling, a page box with its own navigation handler, or a status bar that also shows in Focus or Reading room.
**Chrome:** `BottomDockSlot` (§4), `RectoSurfaceBrush`, 1 epx `RectoBorderBrush` top edge, the `Caption` text style, `ui::NumberBox` (`D10 T02 §6`) and `ui::Slider` (`D10 T02 §8`) with `ControlCornerRadius`, `CommandRegistry` (`D02 T03 §11`).

**Build order:**

1. Author `src/app/controls/classic_status_bar.h/.cpp` with four fields left to right: the page field (`3 / 9`; click or `Enter` turns it into a `ui::NumberBox` that runs `view.goToPage` from `CommandRegistry`, `Esc` cancels, an out-of-range number shows `Pages 1 to 9` and keeps the current page), the page size of the current page (in millimetres or inches by the Windows measurement system, with all of mm, in and pt in the tooltip), the PDF version (`PDF 1.7`), and the zoom percentage with a `ui::Slider` over the zoom engine's range from `D02 T02 §7`. Done when: scrolling, `Ctrl+G`, `Ctrl+Plus` and `Ctrl+Minus` update every field within one frame, and typing `5` and `Enter` jumps to page 5.
2. Register the status bar with `LayoutPresetEngine` (`D02 T04 §3`) so it is placed in `BottomDockSlot` exactly when `ShellMenuBar` is placed, as `SlotOccupant` order 1 in focus group `AfterCanvas` (§4), and collapse its fields on `Home`. Done when: Classic shows it, Focus and Reading room do not, the Reading room filmstrip in `BottomDockSlot` is unaffected, a Custom layout with the docked top command bar and the bottom filmstrip shows the filmstrip directly above a 32 epx status bar with disjoint bounds and no clipping of either, and `Home` shows an empty bar with no stale values.

- [ ] Author `src/app/controls/classic_status_bar.h/.cpp` on `RectoSurfaceBrush` with a 1 epx `RectoBorderBrush` top edge and `Caption` text fields. Done when: the Classic window matches the status strip of `D-Classic.png`. Cheaper substitute: a plain text row with a hex background.
- [ ] Bind the page field to the active tab's viewport state and route typed pages through `CommandRegistry` `view.goToPage`. Done when: the field follows scrolling and a typed page jumps there through the same handler as `Ctrl+G`.
- [ ] Refuse an out-of-range or empty page entry in place with `Pages 1 to N` and keep the current page. Done when: entering `0`, `N+1` or letters leaves the view unchanged and shows the message.
- [ ] Show the current page size from the page geometry of `D02 T02 §5` in the Windows measurement system, with mm, in and pt in the tooltip. Done when: an A4 page shows `210 x 297 mm` on a metric system and `8.27 x 11.69 in` on a US system.
- [ ] Show the PDF version from the document description of `D01 T02 §4` as `PDF <major>.<minor>`. Done when: corpus files of versions 1.4, 1.7 and 2.0 show their versions.
- [ ] Bind the zoom percentage and `ui::Slider` two-way to the zoom engine of `D02 T02 §7` through `CommandRegistry` `view.zoomTo`. Done when: dragging the slider zooms with anchor preservation and `Ctrl+Plus` moves the slider.
- [ ] Give the page field, size, version and zoom controls automation names and tooltips, and place the bar after the page in the keyboard order through the §4 `AfterCanvas` focus group. Done when: `Tab` from the page canvas reaches the page field first, in Classic and in the Custom filmstrip layout (where the filmstrip is reached before the canvas).
- [ ] Register placement with `LayoutPresetEngine` so the bar follows `ShellMenuBar`, as `BottomDockSlot` occupant order 1 below the filmstrip's order 0, at a fixed 32 epx height. Done when: switching Focus, Classic and Reading room shows the bar only in Classic, and the Custom layout (docked top command bar plus bottom filmstrip) shows both strips stacked filmstrip first.
- [ ] Add `Shell.ClassicStatusBar` covering field updates on scroll and zoom, typed page jump and refusal, unit display, version text, preset visibility, the Custom filmstrip plus status bar composition (stacking order, disjoint bounds, 32 epx bar height, filmstrip before and status bar after the canvas in `Tab` and `Shift+Tab`), `Home` state and automation names. Done when: `recto-tests.exe --gtest_filter=Shell.ClassicStatusBar` passes.
- [ ] Commit: `app: build the Classic status bar with page, size, version and zoom`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.ClassicStatusBar` passes on the display-session host; making the page field ignore viewport changes, showing the bar in Focus, stacking it above the filmstrip in the Custom layout, or reaching it before the page makes the test fail.

## Verification

- Every M2 shell obligation from `design/SPEC.md` (sections 3, 4, 7, 8, 8.1, 9.1 drop overlay, 9.2 loading states, 9.9 Password / Could-not-open / Replace-original / Unsaved-changes dialogs, 10 shell keys, 11 automation names and tab order) proven on rendered surfaces and by `Shell.*` tests in `recto-tests`.
- Full `recto-tests` suite green; `Recto.sln` builds `Debug` and `Release` for `x64` and `ARM64` with zero warnings; `python scripts/todo-graph.py validate` passes.
- Zero `#` hex colour literals exist under `src/app` or `src/ui` outside `src/ui/generated/recto_tokens.g.h`.
- The tab session, closed-tab, reorder and share behaviour of §13 to §17 is proven by `Shell.SessionStorePersistence`, `Shell.SessionRestoreOnLaunch`, `Shell.ClosedTabStackAndBatchClose`, `Shell.TabReorderAndKeyboardMove`, `Shell.ShareCopyForDirtyAndCloud` and `Audit.ShareNoDirectNetwork`.
- The icon font, chip style and Classic status bar of §18 to §20 are proven by `python scripts/build-icon-font.py --check`, `tests.scripts.test_build_icon_font`, `Shell.IconFontSpecimen`, `Shell.ChipStyleShape`, `Audit.NoCornerRadiusLiterals` and `Shell.ClassicStatusBar`.
