---
schema_version: 1
id: app-shell
domain: 02-viewer
status: active
title: "TODO-01 -- App Shell, Tabs, Menus, File Open/Save and Shell Dialogs"
track: M2
---

# TODO-01 -- App Shell, Tabs, Menus, File Open/Save and Shell Dialogs

> **Goal:** The WinUI 3 app exists as the SPEC's one shell: a self-contained unpackaged and MSIX-ready executable with `RectoColors.xaml` tokens, a custom title bar with document tabs and drag regions, named layout slots for all three presets, the Classic menu bar (`File`, `Edit`, `View`, `Tools`, `Window`, `Help`) and docked top command bar, all four file-opening pipelines (`Ctrl+O` picker, drag-and-drop with overlay, CLI arguments, and Explorer file activation with single-instance forwarding), file saving (`Save`, `Save a copy`, `Replace the original?`, and `Save your changes first?`), and the Password and Could-not-open dialogs per `design/SPEC.md` sections 8, 9.1, 9.2, 9.9, 10 and 11.

> **Current state:** Measured 2026-10-14. `D00 T01 §3` created the compilable WinUI 3 scaffold (`src/app/Recto.vcxproj`, `App.xaml/.h/.cpp`, `MainWindow.xaml/.h/.cpp`). `D01` provides `DocumentHandle` open, authenticate, incremental save and describe. All visual assets exist in `resources/` (`resources/msix/Assets/`, `resources/ico/`, `resources/svg/`) and all colour tokens live in `design/tokens/RectoColors.xaml`.

## Inputs

- `design/SPEC.md` sections 3 (brand and assets), 4 (colour), 5 (typography), 6 (icons), 7 (shape, spacing, elevation, 960x600 minimum window size), 8 and 8.1 (shell, named slots and title bar), 9.1 (drop overlay), 9.2 (loading, password), 9.6 and 9.9 (dialogs: Password, Could not open, Replace original, Unsaved changes), 10 (keyboard), 11 (accessibility); `design/tokens/RectoColors.xaml`.
- `resources/README.md` (asset mapping for `Package.appxmanifest` and unpackaged icons) and `resources/svg/` (wordmark, app icons, drop-files illustrations).
- -> XREF: D01 T01 §1 -- `recto::pdf::diagnostics_tail()` and `PdfError` ring buffer consumed by `CouldNotOpenDialog`'s `Copy error details` in §11 here.
- -> XREF: D01 T02 §4 -- `DocumentActor` open/authenticate (`§4`), incremental save (`§5`), and `Runtime` shutdown (`§10`) consumed by §3, §5, §8, and §10 here.
- -> XREF: D02 T04 §3 -- the layout preset engine moves controls between the named slots declared in §4 here.
- -> XREF: D04 T01 §7 -- `QueueCard` and `ConversionRunner` reuse §10's `PasswordDialog` for encrypted conversion jobs and §4's overlay slot for the floating queue card.
- -> XREF: D04 T02 §1 -- `PdfWriter` and `AtomicFileWriter` reuse §10's shared `RectoDialogStyles.xaml`.
- -> XREF: D04 T04 §8 -- the `Install Office or LibreOffice` dialog reuses §10's shared `RectoDialogStyles.xaml`.
- -> XREF: D05 T02 §3 -- `OrganizePage` Combine mode and `§5` Unsaved-Changes / Replace-Original dialogs reuse §10's `PasswordDialog` and `RectoDialogStyles.xaml`.
- -> XREF: D07 T01 §4 -- packaging completes what §1 here starts: the self-contained configuration, manifest and assets authored here are packaged into the Inno Setup installer, Portable `.zip` (`§5`) and optional MSIX (`§6`).

## Outcome

- `Recto.exe` launches directly from the build output folder (self-contained unpackaged and MSIX-ready) with `RectoColors.xaml` Light, Dark and HighContrast dictionaries, vermilion accent ramp, window icon, and `960 x 600 epx` minimum window size.
- `ShellPage` hosts the custom title bar (icon, wordmark, Home tab, document `TabView` with dirty indicator and actor teardown, command box trigger, settings button, caption buttons with Snap Layouts), the eight named layout slots, and the Classic menu bar (`File`, `Edit`, `View`, `Tools`, `Window`, `Help`).
- PDFs open through the `Ctrl+O` picker, full-window drag-and-drop, command-line arguments, and Explorer file activation (redirecting to the primary running instance) into tabs over one shared `OpenDocumentPipeline`, and modified documents save through `Ctrl+S` (incremental or atomic replace) and `Ctrl+Shift+S` (`Save a copy`) with `Replace the original?` and `Save your changes first?` prompts.
- Password-protected and damaged PDFs trigger the SPEC 9.9 `PasswordDialog` and `CouldNotOpenDialog` (with diagnostic ring-buffer `Copy error details`) built on the shared `RectoDialogStyle`.

**Adjacency:** list=applicable @ D02 T03 §1; document=applicable @ D02 T01 §8; settings=applicable @ D02 T04 §4; reporting=not-applicable (no reports in the application shell; diagnostic error text is copied via §11); notifications=applicable @ D04 T01 §6; permissions=not-applicable (single-user desktop app; document-level link and action refusals live in D02 T02 §9); audit=not-applicable (no persistent audit log over shell window state); exchange=not-applicable (clipboard and share paths belong to D03 T01 §2); reverse=applicable @ D02 T01 §3

**Adjacency rationale:** The shell's document surface is saving modified PDFs (`Save` and `Save a copy` in §8, while printing lives in D07 T01 §1), and its reverse surface is closing a tab with full `DocumentActor` teardown (`§3`) or discarding unsaved edits (`§8`). Recent files (`D02 T03 §1`), settings (`D02 T04 §4`) and conversion notifications (`D04 T01 §6`) are hosted by the shell and owned by their respective files.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | App project, unpackaged/MSIX manifest, assets, tokens and minimum window size | D00 T01 §3 | [ ] |
| 2 | §2 | Custom title bar, brand lockup, drag regions and caption buttons | §1 | [ ] |
| 3 | §3 | Document `TabView`, tab session lifecycle, dirty state and actor teardown | §2, D01 T02 §10 | [ ] |
| 4 | §4 | Named shell layout slots, Reading room mode switcher and Classic navigation rail | §2 | [ ] |
| 5 | §5 | File Open Picker (`Ctrl+O`) and shared `OpenDocumentPipeline` | §3, D01 T02 §4 | [ ] |
| 6 | §6 | Drag-and-drop file opening and full-window drop overlay | §5 | [ ] |
| 7 | §7 | Command-line arguments, Explorer `.pdf` activation and single-instance forwarding | §5 | [ ] |
| 8 | §8 | Save (`Ctrl+S`), Save As (`Ctrl+Shift+S`), Replace-Original and Unsaved-Changes dialogs | §3, §5, D01 T02 §5 | [ ] |
| 9 | §9 | Classic menu bar (`File`, `Edit`, `View`, `Tools`, `Window`, `Help`) and docked top bar | §4, §5, §8 | [ ] |
| 10 | §10 | Shared SPEC 9.9 `ContentDialog` chrome and Password dialog | §5 | [ ] |
| 11 | §11 | Could-not-open error dialog and diagnostic clipboard copy | §5, §10 | [ ] |

## 1. App project, unpackaged/MSIX manifest, assets, tokens and minimum window size

Wire the WinUI 3 foundation in `src/app/` before any screen is drawn: configure `src/app/Recto.vcxproj` for self-contained unpackaged execution (`WindowsPackageType=None`, `WindowsAppSDKSelfContained=true` for Inno Setup Full and Portable installs per D6) alongside optional MSIX packaging (`src/app/Package.appxmanifest` and `resources/msix/Assets/`), merge `design/tokens/RectoColors.xaml` into `src/app/App.xaml` with the vermilion `SystemAccentColor` ramp (`#C8431F` light / `#F0603C` dark), set the window icon from `resources/ico/recto.ico` via `AppWindow::SetIcon`, and enforce the SPEC section 7 minimum window size (`960 x 600 epx`).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 3, 4 and 7 with `design/screenshots/D-Brand.png` (icon, wordmark, tokens) and `D-Focus-Dark.png` (dark surfaces).
**Job:** The user can launch `Recto.exe` (both unpackaged/portable and MSIX-packaged) into a branded window with Recto's vermilion accent, theme brushes, icon, and minimum size enforced. Consumer: every UI control and surface in `src/app/`.
**Treatment:** Stock WinUI 3 app supporting self-contained unpackaged deployment with `RectoColors.xaml` merged into `App.xaml` and Win32/AppWindow minimum size enforcement. Cheaper substitute that fails the checkpoint: per-control hex colours, leaving the default Windows blue accent in place, or allowing the window to shrink below `960 x 600 epx`.
**Chrome:** consume `design/tokens/RectoColors.xaml` brushes and `ThemeDictionaries` (`Light`, `Dark`, `HighContrast`); load `resources/ico/recto.ico` and `resources/svg/` assets without duplicating token definitions.

**Build order:**

1. Copy `resources/msix/Assets/` into `src/app/Assets/`, update `src/app/Package.appxmanifest` per `resources/README.md` (`.pdf` `uap:FileTypeAssociation` with `Assets\FileTypes\PdfFile.png` and splash background `#1B1A19`), and verify `src/app/Recto.vcxproj` runs self-contained (`WindowsAppSDKSelfContained=true`) directly from `build/bin/x64/Debug/Recto.exe`. Done when: `Recto.exe` starts from the build output directory without MSIX registration and `src/app/Package.appxmanifest` validates against all `src/app/Assets/` paths.
2. Merge `design/tokens/RectoColors.xaml` into `src/app/App.xaml`, override the `SystemAccentColor` Light1..3 and Dark1..3 ramp so stock accent controls render `#C8431F` in Light and `#F0603C` in Dark, set `AppWindow.SetIcon` to `resources/ico/recto.ico`, and attach a `WM_GETMINMAXINFO` subclass (or `OverlappedPresenter` bounds) enforcing `960 x 600 epx` scaled by monitor DPI. Done when: a stock `Style="{StaticResource AccentButtonStyle}"` button samples at `#C8431F` (Light) and `#F0603C` (Dark), and resizing the window cannot shrink below `960 x 600 epx`.

- [ ] Copy all generated package logos and file-type icons from `resources/msix/Assets/` into `src/app/Assets/`. Done when: every asset listed in `resources/README.md` exists under `src/app/Assets/`.
- [ ] Configure `src/app/Package.appxmanifest` with Recto display name, tile logos, splash screen `#1B1A19`, and `.pdf` `uap:FileTypeAssociation` pointing to `Assets\FileTypes\PdfFile.png`. Done when: manifest schema validation passes with zero missing file references.
- [ ] Configure `src/app/Recto.vcxproj` for self-contained unpackaged execution (`WindowsAppSDKSelfContained=true`) while retaining `Package.appxmanifest` for optional `-EnableMsix` builds. Done when: `build/bin/x64/Debug/Recto.exe` launches directly from PowerShell without `Add-AppxPackage`. Cheaper substitute that fails the checkpoint: requiring manual MSIX sideloading before `Recto.exe` can start.
- [ ] Merge `design/tokens/RectoColors.xaml` into `src/app/App.xaml` with `Light`, `Dark`, and `HighContrast` `ThemeDictionaries`. Done when: `App.Current.Resources` resolves every `Recto*Brush` key listed in `design/SPEC.md` section 4.
- [ ] Map `SystemAccentColor` and its `Light1..3` / `Dark1..3` resources in `RectoColors.xaml` to Recto vermilion (`#C8431F` light, `#F0603C` dark, `#A93717` / `#F48A6E` accent text). Done when: a rendered `AccentButtonStyle` button samples at `#C8431F` in Light and `#F0603C` in Dark on Windows App SDK 2.4.0.
- [ ] Set the runtime window and taskbar icon from `resources/ico/recto.ico` via `AppWindow::SetIcon`. Done when: `MainWindow` displays the Recto bookmark-ribbon icon in the taskbar and `Alt+Tab` switcher.
- [ ] Enforce the `960 x 600 epx` minimum window size in `src/app/MainWindow.xaml.cpp` using DPI-aware `WM_GETMINMAXINFO` handling. Done when: programmatic and mouse resize attempts below `960 x 600 epx` clamp at `960 x 600` scaled by the monitor's rasterization scale.
- [ ] Add `Shell.TokensAndWindowBounds` test in `tests/recto-tests/` asserting brush availability, zero hex colour literals in `src/app/**/*.xaml` outside `RectoColors.xaml`, and minimum size clamping. Done when: `recto-tests.exe --gtest_filter=Shell.TokensAndWindowBounds` passes.
- [ ] Commit: `app: wire WinUI shell manifest, assets, RectoColors tokens and minimum window size`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.TokensAndWindowBounds` passes on the display-session host, accent colour samples at `#C8431F` (Light) and `#F0603C` (Dark), and searching `src/app` for `#` hex literals outside `RectoColors.xaml` returns zero matches.

## 2. Custom title bar, brand lockup, drag regions and caption buttons

Build the SPEC section 8.1 custom title bar inside `src/app/ShellPage.xaml/.h/.cpp`: left-to-right layout with the small app icon (`20 epx`), Newsreader `"Recto"` wordmark (`17 epx`), Home tab button (icon-only), document `TabView` host, new-tab (`+`) button, centred Command Box trigger button (`"Search, open or run a command"` with `"Ctrl K"` badge), Settings gear button (`Ctrl+,`), and native Windows caption buttons (`Minimize`, `Maximize/Restore` with Windows 11 Snap Layouts flyout, `Close`), with `InputNonClientPointerSource` drag regions calculated around interactive elements.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 3.1, 3.3, 6, 8.1 and 11 with `design/screenshots/D-Focus-Light.png` and `D-Focus-Dark.png`.
**Job:** The user sees Recto's quiet branded title bar, can drag or snap the window naturally, and can reach Home, the Command Box, and Settings from the top bar at any time. Consumer: `MainWindow` hosts `ShellPage`; §3 mounts the document `TabView` into the title bar strip.
**Treatment:** Custom title bar via `AppWindowTitleBar::ExtendsContentIntoTitleBar(true)` with explicit `SetRegionRects(NonClientRegionKind::Passthrough, ...)` for interactive controls and theme-switched `SvgImageSource` brand assets. Cheaper substitute that fails the checkpoint: stock system title bar with tabs pushed to a second row below it, or broken drag/Snap Layouts behavior.
**Chrome:** consume `RectoWindowBrush`, `RectoTextBrush`, `RectoTextSecondaryBrush`, `resources/svg/app-icon-small.svg` (`-on-dark.svg`), and `resources/svg/wordmark-light.svg` (`-dark.svg`). Do not ship the Newsreader font file; use the outlined SVGs per SPEC 3.3.

**Build order:**

1. Create `src/app/ShellPage.xaml`, `src/app/ShellPage.xaml.h`, and `src/app/ShellPage.xaml.cpp` and set `MainWindow` content to `ShellPage` with `ExtendsContentIntoTitleBar(true)` and transparent caption button backgrounds matching `RectoWindowBrush`. Done when: `MainWindow` renders the custom title bar row with native caption buttons styled to match Light and Dark themes.
2. Add the brand lockup (`20 epx` `app-icon-small` + `17 epx` `wordmark` switched by theme), the icon-only `Home` tab button (`FontIcon` home glyph, `AutomationProperties.Name="Home"`, tooltip `"Home"`), the centred Command Box pill button (`"Search, open or run a command"`, `"Ctrl K"` caption badge, `AutomationProperties.Name="Command box"`, tooltip `"Command box (Ctrl+K)"`), and the Settings button (`FontIcon` settings glyph, `AutomationProperties.Name="Settings"`, tooltip `"Settings (Ctrl+,)"`), and update `InputNonClientPointerSource` passthrough rects on `SizeChanged` and layout updates. Done when: every title bar button is clickable, empty title bar areas drag and double-click maximize the window, and hovering the maximize button opens the Windows 11 Snap Layouts flyout.

- [ ] Author `src/app/ShellPage.xaml/.h/.cpp` and mount it as the root content of `MainWindow`. Done when: `Recto.exe` boots into `ShellPage` with zero XAML binding errors.
- [ ] Enable `AppWindowTitleBar::ExtendsContentIntoTitleBar(true)` and configure caption button foreground/background/hover colours from `RectoColors.xaml` tokens in both Light and Dark themes. Done when: caption buttons remain legible in Light, Dark, and HighContrast themes.
- [ ] Render the `20 epx` app icon (`resources/svg/app-icon-small.svg` / `app-icon-small-on-dark.svg`) and `17 epx` `"Recto"` wordmark (`resources/svg/wordmark-light.svg` / `wordmark-dark.svg`) via `SvgImageSource` at the left of the title bar. Done when: the lockup matches `D-Focus-Light.png` and `D-Focus-Dark.png` metrics.
- [ ] Add the icon-only `Home` button, centred Command Box trigger button (`"Search, open or run a command"` + `"Ctrl K"`), and `Settings` button in the exact SPEC 8.1 left-to-right order. Done when: visual inspection and layout tree order match SPEC 8.1. Cheaper substitute that fails the checkpoint: omitting `AutomationProperties.Name` or shortcut tooltips on icon-only title bar buttons.
- [ ] Compute and register `NonClientRegionKind::Passthrough` rectangles via `InputNonClientPointerSource` for all interactive title bar controls whenever the window or tab strip resizes. Done when: clicking buttons activates them while dragging empty title bar space moves the window.
- [ ] Dim the title bar lockup and inactive tab labels to `RectoTextSecondaryBrush` when `Window::Activated` reports `WindowActivationState::Deactivated`. Done when: deactivating the window visually dims the title bar chrome per Windows conventions.
- [ ] Add `Shell.TitleBarLayoutAndHitRegions` test in `tests/recto-tests/` verifying title bar control order, automation names, tooltips, and non-empty passthrough rect calculation. Done when: `recto-tests.exe --gtest_filter=Shell.TitleBarLayoutAndHitRegions` passes.
- [ ] Commit: `app: build custom title bar with brand lockup, drag regions and caption buttons`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.TitleBarLayoutAndHitRegions` passes, and rendered screenshots in Light and Dark themes match `D-Focus-Light.png` and `D-Focus-Dark.png` title bars side by side.

## 3. Document `TabView`, tab session lifecycle, dirty state and actor teardown

Implement the document tab strip (`TabView`) in the title bar and the `DocumentTabSession` lifecycle model in `src/app/`: one tab per open document (`max 240 epx` width, close button visible on hover and on the active tab, `+` New Tab button routing to `Home`), dirty (`*`) indicator for unsaved document edits, `Ctrl+Tab` / `Ctrl+Shift+Tab` switching, `Ctrl+W` tab closing, automatic return to `Home` when the last document tab closes, and deterministic `DocumentActor` shutdown through `Runtime` (`D01 T02 §4`).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 7 (radius 8 for tabs), 8.1 (title bar tabs, max 240 epx, close on hover and active tab), and 10 (`Ctrl+W`, `Ctrl+Tab`, `Ctrl+Shift+Tab`) with `D-Focus-Light.png` and `D-Classic.png`.
**Job:** The user can hold multiple PDFs open simultaneously in tabs, switch between them effortlessly, see which tabs have unsaved changes, and close any tab knowing its background engine resources are immediately freed. Consumer: `OpenDocumentPipeline` (§5) creates tabs; `SavePipeline` (§8) clears the dirty indicator; `DocumentView` (`D02 T02`) renders inside the active tab's session.
**Treatment:** Stock WinUI `TabView` styled to `240 epx` max tab width with a strongly typed `DocumentTabSession` per tab tied 1:1 to a `DocumentHandle` (`DocumentActor`). Cheaper substitute that fails the checkpoint: leaving orphaned `DocumentActor` threads alive after tab close, or opening each document in a separate OS window.
**Chrome:** consume `ShellPage`'s title bar tab host (and secondary tab strip slot in `Reading room` preset, §4) with `RectoWindowBrush`, `RectoRaisedBrush` for the active tab, and `RectoTextSecondaryBrush` for inactive tabs. Do not invent a second tab strip control.

**Build order:**

1. Define `DocumentTabSession` (`src/app/DocumentTabSession.h/.cpp`) holding the tab id, file path, tab title (filename initially, updated to document `/Title` when `DisplayDocTitle` is set), `is_loading` flag, `is_dirty` flag, `std::shared_ptr<recto::core::DocumentHandle>` actor handle, and per-tab view state. Done when: creating and destroying `DocumentTabSession` in unit tests starts and joins its `DocumentActor` cleanly.
2. Style and wire `TabView` inside `ShellPage`: `MaxWidth="240"` per `TabViewItem`, close button visibility on pointer hover and active state, `+` add-tab button activating `Home`, `Ctrl+W` closing the active tab, `Ctrl+Tab` and `Ctrl+Shift+Tab` cycling tabs, and closing the last document tab activating the `Home` view. Done when: 20 rapid tab open-and-close cycles leave the Debug live-clone and actor count at zero and return the shell to `Home`.

- [ ] Author `src/app/DocumentTabSession.h/.cpp` encapsulating per-tab `DocumentHandle` lifetime, file path, display title, `is_loading` state, and `is_dirty` state. Done when: destroying a `DocumentTabSession` deterministically invokes `Runtime::close` on its `DocumentHandle`.
- [ ] Embed `TabView` in `ShellPage`'s title bar strip with custom `TabViewItem` styling (`CornerRadius=8`, `MaxWidth=240 epx`, `CaptionTextBlockStyle` 12 epx title, close button on active tab and pointer hover). Done when: tab geometry and typography match `D-Focus-Light.png`.
- [ ] Display an unsaved-changes dirty indicator (`•` or `*` badge with `AutomationProperties.Name` including `"Modified"`) on any tab whose `DocumentTabSession::is_dirty()` is true. Done when: toggling `set_dirty(true)` updates the tab header and accessibility name immediately.
- [ ] Wire the `+` (`AddTabButton`) in `TabView` to focus the `Home` tab and place keyboard focus inside the inline Command Box. Done when: clicking `+` opens `Home` ready to type or pick a file.
- [ ] Wire `Ctrl+Tab` (next tab), `Ctrl+Shift+Tab` (previous tab), `Ctrl+W` (close active document tab), and middle-click on a tab header to close that tab. Done when: keyboard and mouse tab cycling and closing match SPEC section 10.
- [ ] Ensure closing the last open document tab automatically selects the `Home` tab rather than leaving an empty window or closing the app. Done when: closing the sole open tab transitions smoothly to `Home`. Cheaper substitute that fails the checkpoint: exiting the app or showing a blank gray frame when the last tab closes.
- [ ] Update `InputNonClientPointerSource` passthrough regions dynamically as tabs are added, resized, or removed so every tab header and close button remains clickable while the remaining strip stays draggable. Done when: newly added tabs can be clicked, dragged, and closed immediately.
- [ ] Add `Shell.TabLifecycleAndActorTeardown` test in `tests/recto-tests/` running 20 rapid tab open/close cycles and asserting zero leaked `DocumentActor` instances or live clones in Debug. Done when: `recto-tests.exe --gtest_filter=Shell.TabLifecycleAndActorTeardown` passes.
- [ ] Commit: `app: implement title-bar TabView, DocumentTabSession lifecycle and actor teardown`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.TabLifecycleAndActorTeardown` passes with 20 rapid open/close cycles leaving zero live actors or clones in Debug, and rendered tab strip matches `D-Focus-Light.png`.

## 4. Named shell layout slots, Reading room mode switcher and Classic navigation rail

Implement the eight named layout slot containers in `ShellPage` required by SPEC section 8 and decision D17 (`TitleBarSlot`, `TopBarSlot`, `BottomOverlaySlot`, `LeftDockSlot`, `RightDockSlot`, `LeftOverlaySlot`, `RightOverlaySlot`, `BottomDockSlot`), plus the `Classic` left navigation rail (`Home`, `Read`, `Convert`, `Organize` with `20 epx` glyphs) and the `Reading room` title-bar mode switcher (replacing the Home icon button with `Home | Read | Convert | Organize` with an accent underline indicator and moving the document tab strip into `TopBarSlot`).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6 (`20 epx` rail icons), 7 (borders on docked panes, `ThemeShadow` Z=32 on floating overlays), 8 and 8.1 with `design/screenshots/D-Focus-Light.png`, `D-Classic.png`, `D-ReadingRoom.png` and `D-Layout.png`.
**Job:** The shell provides the exact named slots and navigation chrome so the layout preset engine (`D02 T04 §3`) can rearrange controls between `Focus`, `Classic`, and `Reading room` without ever recreating the shell or losing document state. Consumer: `PageToolbar` (`D02 T02 §8`), `SidePanelHost` (`D02 T03 §3`), `ThumbnailList` (`D02 T03 §4`), and `LayoutPresetEngine` (`D02 T04 §3`).
**Treatment:** One `ShellPage` containing all eight named slot hosts and `ShellSlotManager::reparent_control`, with docked slots bordered by `1 epx` `RectoBorderBrush` on `RectoSurfaceBrush` and overlay slots floating with `ThemeShadow` Z=32. Cheaper substitute that fails the checkpoint: building three separate `Page` classes for `Focus`, `Classic`, and `Reading room` (explicitly forbidden by D17).
**Chrome:** consume `RectoSurfaceBrush`, `RectoRaisedBrush`, `RectoBorderBrush`, and `RectoAccentBrush` from `RectoColors.xaml`.

**Build order:**

1. Add the eight named slot containers (`TitleBarSlot`, `TopBarSlot`, `BottomOverlaySlot`, `LeftDockSlot`, `RightDockSlot`, `LeftOverlaySlot`, `RightOverlaySlot`, `BottomDockSlot`) and the central workspace host (`CenterCanvasSlot` for `Home`, `DocumentView`, `ConvertPage`, `OrganizePage`, `SettingsPage`) to `src/app/ShellPage.xaml`, along with `ShellSlotManager` (`src/app/ShellSlotManager.h/.cpp`) to move live `UIElement` instances between slots without destroying their state. Done when: a stateful test control moved across all eight slots preserves its internal state and focusable children.
2. Build the `Classic` left navigation rail (`NavigationRailControl` in `LeftDockSlot`, `20 epx` Segoe Fluent icons for `Home`, `Read`, `Convert`, `Organize`, `Settings`) and the `Reading room` title-bar mode switcher (`Home`, `Read`, `Convert`, `Organize` with `RectoAccentBrush` underline indicator and document tabs reparented to `TopBarSlot`). Done when: toggling slot configurations matches the shell chrome of `D-Focus-Light.png`, `D-Classic.png`, and `D-ReadingRoom.png`.

- [ ] Declare `TitleBarSlot`, `TopBarSlot`, `BottomOverlaySlot`, `LeftDockSlot`, `RightDockSlot`, `LeftOverlaySlot`, `RightOverlaySlot`, `BottomDockSlot`, and `CenterCanvasSlot` in `src/app/ShellPage.xaml`. Done when: all slot hosts exist in the visual tree and collapse cleanly (`Visibility::Collapsed`) when empty so they consume zero layout space.
- [ ] Style docked slots (`TopBarSlot`, `LeftDockSlot`, `RightDockSlot`, `BottomDockSlot`) with `RectoSurfaceBrush` backgrounds and `1 epx` `RectoBorderBrush` dividers (zero drop shadows per SPEC 7). Done when: docked panes render flat with crisp `1 epx` borders.
- [ ] Style floating overlay slots (`BottomOverlaySlot`, `LeftOverlaySlot`, `RightOverlaySlot`) with `16 epx` canvas margins and `ThemeShadow` (`Translation.Z = 32` per SPEC 7). Done when: floating cards cast the SPEC 7 elevation shadow over the page canvas.
- [ ] Implement `ShellSlotManager::reparent_control(UIElement, SlotId)` in `src/app/ShellSlotManager.h/.cpp` that detaches a live control from its current slot panel and attaches it to the target slot panel in a single UI tick. Done when: reparenting `TabView` or a test toolbar preserves selection and text input state. Cheaper substitute that fails the checkpoint: destroying and instantiating new controls on layout change.
- [ ] Build the `Classic` left navigation rail (`Home`, `Read`, `Convert`, `Organize`, plus `Settings` pinned at bottom) with `20 epx` Segoe Fluent glyphs, `AutomationProperties.Name`, and tooltips. Done when: activating rail items switches the active workspace mode in `ShellPage`.
- [ ] Build the `Reading room` title-bar mode switcher (`Home`, `Read`, `Convert`, `Organize` with a `2 epx` `RectoAccentBrush` active underline indicator) and support reparenting the document `TabView` into `TopBarSlot` directly beneath the title bar per SPEC 8.1. Done when: the `Reading room` header matches `D-ReadingRoom.png`.
- [ ] Enforce the SPEC 11 logical keyboard `Tab` order across slots (`TitleBarSlot -> TopBarSlot -> BottomOverlaySlot -> LeftDockSlot/LeftOverlaySlot -> RightDockSlot/RightOverlaySlot -> BottomDockSlot -> CenterCanvasSlot`). Done when: pressing `Tab` traverses populated slots in exact SPEC 11 order.
- [ ] Add `Shell.NamedSlotsAndReparenting` test in `tests/recto-tests/` verifying lossless control reparenting across all eight slots, empty-slot collapse, and keyboard tab-index ordering. Done when: `recto-tests.exe --gtest_filter=Shell.NamedSlotsAndReparenting` passes.
- [ ] Commit: `app: add named shell layout slots, Classic nav rail and Reading room mode switcher`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.NamedSlotsAndReparenting` passes on the display-session host, proving lossless control reparenting across all eight named slots and SPEC 11 keyboard tab order.

## 5. File Open Picker (`Ctrl+O`) and shared `OpenDocumentPipeline`

Build the central `OpenDocumentPipeline` (`src/app/OpenPipeline.h/.cpp`) that every file-opening entry point in Recto calls, and wire the first entry point: the native Windows `FileOpenPicker` (`Ctrl+O`, toolbar/menu `Open`, and Home `"Browse files"`). Opening a PDF creates or activates a tab, dispatches `Runtime::open` on the document's `DocumentActor` off the UI thread, updates the tab title from the filename to the PDF `/Title` when `DisplayDocTitle` applies, and routes encrypted or damaged files to the §10 and §11 dialogs.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 3.1, 8.1, 9.1, 9.2 (loading states) and 10 (`Ctrl+O`) with `D-Focus-Light.png`.
**Job:** The user can press `Ctrl+O` (or click `Browse files`) to pick one or more PDFs from Explorer and have them open immediately into tabs without blocking the UI thread. Consumer: drag-and-drop (§6), CLI/activation (§7), Home recents (`D02 T03 §2`), and `DocumentView` (`D02 T02`).
**Treatment:** Single `OpenDocumentPipeline::open_files_async` service backed by `Runtime::open` on background `DocumentActor` threads, with `IInitializeWithWindow` HWND binding for `FileOpenPicker` in unpackaged/self-contained mode. Cheaper substitute that fails the checkpoint: calling `FileOpenPicker` without HWND initialization (which crashes unpackaged WinUI 3 apps) or parsing the PDF on the UI thread.
**Chrome:** consume `DocumentTabSession` (§3) and `ShellPage`'s `TabView`. Do not create separate open code paths for different UI surfaces.

**Build order:**

1. Implement `OpenDocumentPipeline` (`src/app/OpenPipeline.h/.cpp`): canonicalize file paths (`std::filesystem::weakly_canonical`), check if any open `DocumentTabSession` already owns that path (if so, select that tab and return), otherwise create a new `DocumentTabSession` in `is_loading` state with the filename as its initial tab title and invoke `Runtime::open` asynchronously. Done when: opening the same corpus PDF twice selects the existing tab on the second call instead of spawning a second `DocumentActor`.
2. Wire `FileOpenPicker` (`PickMultipleFilesAsync` with `.pdf` filter and `IInitializeWithWindow::Initialize(hwnd)`) to `Ctrl+O` and `OpenDocumentPipeline::open_picker_async`, and handle the `DocumentActor` open completion: on `Ok(DocumentDescription)`, populate page count/dimensions and update the tab title if `/ViewerPreferences /DisplayDocTitle` is true and `/Title` is non-empty; on `ErrorCode::NeedsPassword`, invoke §10's password hook; on any other `PdfError`, close the loading tab and invoke §11's error hook. Done when: opening valid, multi-file, encrypted, and corrupt corpus files through `OpenDocumentPipeline` routes each case deterministically off the UI thread.

- [ ] Author `src/app/OpenPipeline.h/.cpp` providing `open_file_async(path, initial_page)` and `open_files_async(paths)`. Done when: all open requests flow through `OpenDocumentPipeline` and never call `recto::pdf::Document::open` on the UI thread.
- [ ] Deduplicate already-open files by canonical path (`std::filesystem::weakly_canonical`) so re-opening an active document focuses its existing tab. Done when: opening `hello.pdf` twice produces 1 tab and 1 `DocumentActor`.
- [ ] Bind `Windows::Storage::Pickers::FileOpenPicker` with `IInitializeWithWindow` using the `MainWindow` `HWND` so the system file picker works in both unpackaged/portable mode and MSIX mode. Done when: `open_picker_async` opens the native Explorer dialog in unpackaged `Recto.exe`. Cheaper substitute that fails the checkpoint: omitting `IInitializeWithWindow`, causing `COMException` in unpackaged builds.
- [ ] Support multi-file selection in `FileOpenPicker` (`PickMultipleFilesAsync`), opening each selected `.pdf` in its own tab in selection order and focusing the first newly opened tab. Done when: selecting 3 corpus files opens 3 tabs in order.
- [ ] Set the initial tab title to the filename (e.g., `contract.pdf`) immediately while `is_loading` is true, and update the tab title to the PDF metadata `/Title` once `DocumentDescription` arrives if `/ViewerPreferences /DisplayDocTitle` is true (keeping the full file path in the tab tooltip). Done when: a slow open never shows an untitled or unresponsive tab.
- [ ] Route `ErrorCode::NeedsPassword` from `Runtime::open` to the password prompt handler (§10) and any format/IO `PdfError` to the could-not-open handler (§11) without disturbing other open tabs in a multi-file batch. Done when: opening `[good.pdf, broken.pdf]` leaves `good.pdf` open and reports `broken.pdf` once.
- [ ] Bind the `Ctrl+O` keyboard accelerator on `ShellPage` to `OpenDocumentPipeline::open_picker_async`. Done when: pressing `Ctrl+O` from `Home` or an open tab invokes the picker pipeline.
- [ ] Add `Shell.OpenPipelineAndDedup` test in `tests/recto-tests/` verifying async actor creation, canonical-path tab deduplication, `DisplayDocTitle` tab title updates, and mixed good/bad batch routing. Done when: `recto-tests.exe --gtest_filter=Shell.OpenPipelineAndDedup` passes.
- [ ] Commit: `app: add OpenDocumentPipeline and Ctrl+O FileOpenPicker with tab deduplication`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.OpenPipelineAndDedup` passes on the display-session host, verifying single-file, multi-file, duplicate-path, and mixed good/corrupt batch opens.

## 6. Drag-and-drop file opening and full-window drop overlay

Implement window-wide drag-and-drop file opening per SPEC section 9.1: dragging files anywhere over `ShellPage` displays the full-window drop overlay (`resources/svg/drop-files-light.svg` and `drop-files-dark.svg` via theme-aware `SvgImageSource`, dashed accent border, `"Drop a PDF anywhere in this window"` label), and dropping files feeds them into `OpenDocumentPipeline::open_files_async` (§5).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 3.4, 4, 7 and 9.1 with `design/screenshots/D-Home.png` and `resources/svg/drop-files-*.svg`.
**Job:** The user can drag one or more PDFs from Windows Explorer and drop them anywhere on the Recto window (whether on `Home`, an open document, or the tab bar) to open them in tabs. Consumer: `OpenDocumentPipeline` (§5).
**Treatment:** Full-window `AllowDrop="True"` root handler on `ShellPage` with a hit-test-invisible (`IsHitTestVisible="False"`) translucent drop overlay using `RectoRaisedBrush`, `RectoAccentBrush`, and `drop-files-{light,dark}.svg`. Cheaper substitute that fails the checkpoint: accepting drops only inside a small box on the Home page, or showing no visual drop feedback during `DragOver`.
**Chrome:** consume `resources/svg/drop-files-light.svg` and `resources/svg/drop-files-dark.svg` via theme resources and route dropped `StorageFile` paths into `OpenDocumentPipeline::open_files_async` (§5). Do not invent a second drop overlay or open pipeline.

**Build order:**

1. Build the `DropOverlayControl` (`src/app/controls/DropOverlayControl.xaml/.h/.cpp`) hosted at the top Z-layer of `ShellPage` (`IsHitTestVisible="False"`, `Visibility="Collapsed"` by default) with `RectoWindowBrush` / `RectoRaisedBrush` backdrop, `2 epx` `RectoAccentBrush` rounded border (`CornerRadius=16`, `24 epx` margin), `resources/svg/drop-files-{light,dark}.svg` illustration, and `"Drop a PDF anywhere in this window"` `SubtitleTextBlockStyle` text. Done when: toggling the overlay visibility in Light and Dark themes matches the SPEC 3.4 and 9.1 design.
2. Wire `DragOver`, `DragLeave`, and `Drop` on `ShellPage`: inspect `DragEventArgs::DataView()` for `StandardDataFormats::StorageItems()`, set `AcceptedOperation = DataPackageOperation::Copy` and show `DropOverlayControl` when file items are present, hide the overlay on `DragLeave` or `Drop`, and pass the dropped file paths to `OpenDocumentPipeline::open_files_async`. Done when: dropping a batch of valid and broken corpus files onto `ShellPage` opens every valid PDF in a tab and routes broken/non-PDF files to §11's error dialog.

- [ ] Author `src/app/controls/DropOverlayControl.xaml/.h/.cpp` consuming `resources/svg/drop-files-light.svg` and `resources/svg/drop-files-dark.svg` via `ThemeDictionaries` and `RectoColors.xaml` tokens. Done when: the overlay renders with zero hex literals and switches illustrations cleanly between Light and Dark themes.
- [ ] Set `AllowDrop="True"` on the root grid of `ShellPage` so drag-and-drop works over `Home`, open document tabs, side panels, and toolbars alike. Done when: dragging a file over any quadrant of the window triggers `DragOver`. Cheaper substitute that fails the checkpoint: enabling drop only on `HomePage`.
- [ ] Inspect `DataPackageView::Contains(StandardDataFormats::StorageItems())` in `DragOver` to show `DropOverlayControl` and set `DataPackageOperation::Copy` (`DragUIOverride.Caption = "Open in Recto"`), while rejecting plain-text or URL drags on the window shell. Done when: dragging files shows the overlay and dragging selected text does not.
- [ ] Hide `DropOverlayControl` reliably on `DragLeave`, `Drop`, and `Esc` cancellation without flicker when moving the pointer across child controls (`IsHitTestVisible="False"` on the overlay). Done when: moving the dragged cursor across the window never strobes the overlay.
- [ ] Extract `IStorageFile` paths in `ShellPage::OnDrop` and forward them in drop order to `OpenDocumentPipeline::open_files_async` (§5). Done when: dropping 3 PDFs opens 3 tabs in exact drop order.
- [ ] Handle mixed drops (e.g., 2 valid PDFs + 1 corrupt PDF or `.exe` file) by opening the valid PDFs into tabs and routing the invalid file to the §11 error dialog without losing the valid tabs. Done when: mixed-drop tests preserve all valid tabs.
- [ ] Add `Shell.DragAndDropOpen` test in `tests/recto-tests/` exercising `DragOver` overlay state transitions and multi-file/mixed-file drop routing through `OpenDocumentPipeline`. Done when: `recto-tests.exe --gtest_filter=Shell.DragAndDropOpen` passes.
- [ ] Commit: `app: implement window-wide drag-and-drop file opening and SVG drop overlay`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.DragAndDropOpen` passes on the display-session host, and screenshots of the active drop overlay in Light and Dark themes match SPEC sections 3.4 and 9.1.

## 7. Command-line arguments, Explorer `.pdf` activation and single-instance forwarding

Complete the remaining two Windows file-opening entry points in `src/app/App.xaml.cpp`: command-line arguments (`Recto.exe "path\to\file.pdf" --page=5`) and Windows Explorer file activation (`FileActivatedEventArgs` / double-clicking an associated `.pdf` file), with single-instance activation redirection (`Microsoft::Windows::AppLifecycle::AppInstance`) so opening a `.pdf` from Explorer or a second terminal when `Recto.exe` is already running opens a new tab in the existing window and brings it to the foreground.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 3.2 (`.pdf` file association behavior) and 8.1 (one tab per open document in one shell window).
**Job:** The user can double-click `.pdf` files in Windows Explorer or launch `Recto.exe <file.pdf>` from a terminal/script and have them open as tabs in their active Recto window. Consumer: `OpenDocumentPipeline` (§5).
**Treatment:** `Microsoft::Windows::AppLifecycle::AppInstance::FindOrRegisterForKey("RectoPrimaryInstance")` single-instance registration with `Activated` event forwarding, `CommandLineToArgvW` Unicode parsing, and `SetForegroundWindow` activation. Cheaper substitute that fails the checkpoint: spawning a separate `Recto.exe` window per double-clicked file, or failing when paths contain spaces or non-ASCII characters.
**Chrome:** route all activated paths and optional `--page=N` arguments into `OpenDocumentPipeline` (§5) and `ShellPage` (§3).

**Build order:**

1. Implement `parse_launch_arguments(std::wstring_view cmd_line)` in `src/app/LaunchArgs.h/.cpp` using `CommandLineToArgvW` to extract one or more file paths plus optional `--page=<N>` / `-p <N>` arguments, resolving relative paths against the caller's working directory. Done when: unit tests parse quoted paths with spaces, Unicode paths (CJK/accented characters), multiple files, and `--page=12` flags accurately.
2. Wire `AppInstance::FindOrRegisterForKey("RectoPrimaryInstance")` in `src/app/App.xaml.cpp`: if the current process is not the primary instance, call `RedirectActivationToAsync` and exit cleanly; in the primary instance, handle both initial `OnLaunched` and subsequent `AppInstance::Activated` events (`ExtendedActivationKind::Launch` and `ExtendedActivationKind::File`), marshal to the UI dispatcher queue (`DispatcherQueue::TryEnqueue`), restore/foreground `MainWindow`, and invoke `OpenDocumentPipeline::open_files_async`. Done when: launching a second `Recto.exe "second.pdf"` process opens `second.pdf` as a second tab in the first `Recto.exe` window and exits the second process with code 0.

- [ ] Author `src/app/LaunchArgs.h/.cpp` parsing Win32 command-line strings (`CommandLineToArgvW`) into structured `LaunchRequest` records (`std::vector<std::filesystem::path> files`, `std::optional<uint32_t> initial_page`). Done when: `LaunchArgs` handles spaces, relative paths, `--page=N`, and Unicode filenames without truncation.
- [ ] Register the primary instance key via `Microsoft::Windows::AppLifecycle::AppInstance::FindOrRegisterForKey` during app startup in `src/app/App.xaml.cpp`. Done when: `IsCurrent()` returns true for the first instance and false for a second instance.
- [ ] Redirect secondary instance activations via `RedirectActivationToAsync` and terminate the secondary process before creating a window. Done when: invoking `Recto.exe file2.pdf` while `Recto.exe file1.pdf` is running opens `file2.pdf` as a tab in the first window. Cheaper substitute that fails the checkpoint: handling file activation only on cold startup and ignoring already-running instances.
- [ ] Handle `ExtendedActivationKind::File` (`IFileActivatedEventArgs`) for both MSIX-packaged and Inno Setup unpackaged Explorer `.pdf` associations. Done when: Explorer file activation events extract every `IStorageFile` path and open tabs in `ShellPage`.
- [ ] Marshal redirected `AppInstance::Activated` background-thread callbacks onto `MainWindow`'s `DispatcherQueue` (`TryEnqueue`) and bring `MainWindow` to the foreground (`ShowWindow(SW_RESTORE)` + `SetForegroundWindow`). Done when: activating a file while Recto is minimized restores the window and focuses the newly opened tab.
- [ ] Honor `--page=N` on command-line launch by passing `initial_page` into `OpenDocumentPipeline::open_file_async` so the tab opens directly at page `N`. Done when: `Recto.exe sample.pdf --page=3` opens `sample.pdf` positioned on page 3.
- [ ] Add `Shell.LaunchArgsAndActivation` test in `tests/recto-tests/` covering `LaunchArgs` parsing (quotes, Unicode, relative paths, `--page=N`), cold CLI launch, and secondary-instance tab forwarding. Done when: `recto-tests.exe --gtest_filter=Shell.LaunchArgsAndActivation` passes.
- [ ] Commit: `app: support CLI arguments, Explorer .pdf activation and single-instance tab forwarding`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.LaunchArgsAndActivation` passes on the display-session host, proving cold CLI opens, Unicode/spaced paths, `--page=N` navigation, and single-instance redirection into tabs.

## 8. Save (`Ctrl+S`), Save As (`Ctrl+Shift+S`), Replace-Original and Unsaved-Changes dialogs

Implement the viewer's file-saving pipeline (`src/app/SavePipeline.h/.cpp`) and its two SPEC section 9.9 confirmation dialogs: `Save` (`Ctrl+S`), `Save a copy` / `Save As` (`Ctrl+Shift+S` via `FileSavePicker`), the `"Replace the original?"` `ContentDialog` (`Keep both`, `Replace`), and the `"Save your changes first?"` `ContentDialog` (`Discard`, `Cancel`, `Save`) triggered when closing a modified tab (`Ctrl+W`), closing the window with modified tabs, or leaving Organize with unsaved changes. Every file write obeys the foundation plan's atomic temp-file-then-replace rule (`ReplaceFileW` / `MoveFileExW`) in the destination folder.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Save a copy` in More menu), 9.6 and 9.9 (`Replace original` and `Unsaved Organize changes` / unsaved tab changes dialogs), and 10 (`Ctrl+S`, `Ctrl+Shift+S`) with `design/screenshots/D-Dialogs.png`.
**Job:** The user can save their changes (annotations, form fills, bookmarks, metadata, or page edits) safely without ever corrupting the original PDF on disk or losing unsaved work when closing a tab or window. Consumer: `ShellPage` tab/window close (§3), Classic menu bar (§9), `PageToolbar` (`D02 T02 §8`), Bookmark/Info editors (`D02 T03 §6`, `§10`), Annotations & Forms (`D03 T03`), and Organize (`D05 T02`).
**Treatment:** `SavePipeline` executing on background job/actor threads with atomic temp-file-then-replace (`ReplaceFileW` / `MoveFileExW`), readback verification, and the two SPEC 9.9 `ContentDialog` prompts. Cheaper substitute that fails the checkpoint: truncating and writing over the open source file in place, or closing dirty tabs without asking.
**Chrome:** consume `RectoDialogStyle` (§10), `FileSavePicker` initialized with `IInitializeWithWindow`, and `DocumentTabSession`'s dirty indicator (§3).

**Build order:**

1. Implement `SavePipeline` (`src/app/SavePipeline.h/.cpp`): `save_copy_async(session)` prompting `FileSavePicker` (defaulting to `<stem> (copy).pdf` in the source folder) and writing via atomic temp-file-then-replace; and `save_async(session)` which either appends an incremental revision (`D01 T02 §2`) or prompts `"Replace the original?"` (`Keep both` -> saves `<stem> (edited).pdf`, `Replace` -> writes temp file in destination folder and atomically replaces the original via `ReplaceFileW`), clearing `session.set_dirty(false)` on success and reading back the saved header/XRef to confirm validity. Done when: saving an edited document in both `Keep both` and `Replace` modes produces a valid PDF verified by `recto::pdf::Document::open` and clears the tab's dirty indicator.
2. Implement `UnsavedChangesDialog` (`"Save your changes first?"` with `Discard`, `Cancel`, `Save`) and hook it into `ShellPage::close_tab_async` (`Ctrl+W`, tab close button) and `MainWindow::OnClosing` (`WM_CLOSE` / `AppWindow::Closing`): `Cancel` aborts the close and keeps the tab open; `Discard` tears down the tab/actor immediately; `Save` awaits `SavePipeline::save_async` and only closes the tab if the save succeeded. Done when: attempting to close a dirty tab or window prompts the user and honors all three button outcomes without data loss.

- [ ] Author `src/app/SavePipeline.h/.cpp` with `save_async` (`Ctrl+S`) and `save_copy_async` (`Ctrl+Shift+S`) executing file writes off the UI thread. Done when: no PDF serialization runs on the UI thread.
- [ ] Enforce the atomic temp-file-then-replace contract (`<dest>\.~recto-<uuid>.tmp` written, flushed via `FlushFileBuffers`, verified, then swapped via `ReplaceFileW` or `MoveFileExW`) for every save operation. Done when: a simulated write failure leaves the original destination file byte-for-byte untouched. Cheaper substitute that fails the checkpoint: opening the destination path with `std::ofstream(..., std::ios::trunc)`.
- [ ] Wire `Windows::Storage::Pickers::FileSavePicker` (initialized with `IInitializeWithWindow` and `.pdf` extension choice) for `Save a copy` (`Ctrl+Shift+S`), falling back to `FileSavePicker` automatically if `Save` targets a read-only file or read-only directory. Done when: saving a read-only PDF prompts for a new path instead of failing silently.
- [ ] Build the `"Replace the original?"` `ContentDialog` per SPEC 9.9 (`Keep both` saving alongside the original, `Replace` performing atomic replacement). Done when: the dialog layout and button actions match `design/SPEC.md` section 9.9.
- [ ] Build the `"Save your changes first?"` `ContentDialog` per SPEC 9.9 (`Discard`, `Cancel`, `Save` in accent) and wire it to tab close (`Ctrl+W`, close button, middle-click) whenever `DocumentTabSession::is_dirty()` is true. Done when: closing an unmodified tab closes immediately, while closing a dirty tab displays the prompt.
- [ ] Intercept `AppWindow::Closing` on `MainWindow` when one or more open tabs (or Organize) have unsaved changes, prompting cleanly before allowing the process to exit. Done when: pressing `Alt+F4` with a dirty tab open blocks window destruction until `Discard` or `Save` completes.
- [ ] Clear `DocumentTabSession::set_dirty(false)` and update the tab title/tooltip upon successful save. Done when: the tab's dirty indicator disappears immediately after `Save` completes.
- [ ] Add `Shell.SavePipelineAndUnsavedPrompt` test in `tests/recto-tests/` verifying atomic temp-then-replace writes, simulated mid-write failure preservation of originals, `Keep both` vs `Replace`, and `Discard` / `Cancel` / `Save` close interception. Done when: `recto-tests.exe --gtest_filter=Shell.SavePipelineAndUnsavedPrompt` passes.
- [ ] Commit: `app: implement Ctrl+S Save, Ctrl+Shift+S Save As, Replace-Original and Unsaved-Changes dialogs`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.SavePipelineAndUnsavedPrompt` passes on the display-session host, proving atomic temp-then-replace saves, readback verification, and dirty-tab close interception.

## 9. Classic menu bar (`File`, `Edit`, `View`, `Tools`, `Window`, `Help`) and docked top bar

Build the Classic menu bar (`MenuBar` with `File`, `Edit`, `View`, `Tools`, `Window`, `Help`) and docked top command bar hosted in `TopBarSlot` (`D-Classic.png`), exposing every file, view, annotation, form, redaction, signing, conversion, and organization command with its Segoe Fluent glyph and SPEC section 10 keyboard shortcut text.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6 (`16 epx` toolbar/menu glyphs), 8, 8.2, 9.2 and 10 with `design/screenshots/D-Classic.png` and `explorations/A-*.png`.
**Job:** Users who prefer a traditional desktop PDF workflow (`Classic` preset) or menu-driven discovery can access `File` (`Open`, `Open Recent`, `Save`, `Save a Copy`, `Print`, `Properties`, `Close Tab`, `Exit`), `Edit`, `View`, `Tools`, `Window`, and `Help` from the top bar. Consumer: `ShellPage`'s `TopBarSlot` (§4), `CommandRegistry` (`D02 T03 §11`), and `LayoutPresetEngine` (`D02 T04 §3`).
**Treatment:** Stock WinUI `MenuBar` and docked `CommandBar` in `TopBarSlot` on `RectoSurfaceBrush` with `1 epx` bottom `RectoBorderBrush`, sharing the exact command handlers as the floating toolbar and `Ctrl+K`. Cheaper substitute that fails the checkpoint: omitting a `File` / `View` / `Tools` menu structure in `Classic` mode or wiring menu items to duplicate handlers that drift from the toolbar.
**Chrome:** consume `TopBarSlot` (§4), `RectoSurfaceBrush`, `RectoBorderBrush`, and `16 epx` Segoe Fluent `FontIcon`s.

**Build order:**

1. Author `src/app/controls/ShellMenuBar.xaml/.h/.cpp` containing the six top-level menus (`File`, `Edit`, `View`, `Tools`, `Window`, `Help`) with `MenuFlyoutItem`, `MenuFlyoutSubItem`, `ToggleMenuFlyoutItem`, and `RadioMenuFlyoutItem` entries, `16 epx` `FontIcon` glyphs, and `KeyboardAcceleratorTextOverride` labels matching SPEC section 10. Done when: every menu opens cleanly by mouse or `Alt+F` / `Alt+E` / `Alt+V` / `Alt+T` / `Alt+W` / `Alt+H` access keys.
2. Wire `File` (`Open... Ctrl+O`, `Open Recent >`, `Save Ctrl+S`, `Save a Copy... Ctrl+Shift+S`, `Print... Ctrl+P`, `Convert Document...`, `Properties...`, `Close Tab Ctrl+W`, `Exit`), `Edit` (`Undo Ctrl+Z`, `Redo Ctrl+Y`, `Copy Ctrl+C`, `Select All on Page Ctrl+A`, `Find... Ctrl+F`, `Find & Replace... Ctrl+H`), `View` (`Zoom In`, `Zoom Out`, `Fit Width Ctrl+0`, `Actual Size Ctrl+1`, `Page Layout > Continuous / Single Page / Two-Page Spread`, `Rotate Right Ctrl+R`, `Rotate Left Ctrl+Shift+R`, `Page Colour > Original / Dimmed / Inverted`, `Side Panel Ctrl+B`, `Layout Preset > Focus / Classic / Reading room`, `Full Screen F11`), `Tools` (`Annotate & Markup Ctrl+Shift+A`, `Form Designer`, `Redact & Sanitize Ctrl+Shift+X`, `Sign Document Ctrl+Shift+G`, `Compare PDFs Ctrl+Shift+D`, `Extract Tables & Images`, `Organize Pages`, `Crop & Resize`, `Watermark`, `Header / Footer & Bates`, `Optimize & Compress`), `Window` (open document tabs list, `Next Tab Ctrl+Tab`, `Previous Tab Ctrl+Shift+Tab`), and `Help` (`Keyboard Shortcuts`, `About Recto`), disabling document-specific items when on `Home` with zero open documents. Done when: every menu item is either live in M2 (`Open`, `Save`, `Save a Copy`, `Close Tab`, `Exit`, view/tab items) or explicitly bound to its named resolving section (`D03` to `D07`).

- [ ] Author `src/app/controls/ShellMenuBar.xaml/.h/.cpp` with `File`, `Edit`, `View`, `Tools`, `Window`, and `Help` menus and WinUI `AccessKey` mnemonics (`F`, `E`, `V`, `T`, `W`, `H`). Done when: pressing `Alt+F` opens the `File` menu with keyboard focus on `Open...`.
- [ ] Populate the `File` menu with `Open... (Ctrl+O)`, dynamic `Open Recent` submenu, `Save (Ctrl+S)`, `Save a Copy... (Ctrl+Shift+S)`, `Print... (Ctrl+P)`, `Convert...`, `Document Properties...`, `Close Tab (Ctrl+W)`, and `Exit`. Done when: `Open`, `Save`, `Save a Copy`, `Close Tab`, and `Exit` execute through §§3, 5, and 8. Cheaper substitute that fails the checkpoint: hiding `Open` or `Save` inside an overflow `...` button with no `File` menu in `Classic` mode.
- [ ] Populate the `Edit` menu with `Undo (Ctrl+Z)`, `Redo (Ctrl+Y)`, `Copy (Ctrl+C)`, `Select All on Page (Ctrl+A)`, `Find... (Ctrl+F)`, `Find & Replace... (Ctrl+H)`, `Manage Bookmarks...`, and `Edit Info & Preferences...`. Done when: all `Edit` items display their SPEC section 10 shortcuts and enable/disable based on active document state.
- [ ] Populate the `View` menu with zoom commands, `Page Layout` radio items (`Continuous`, `Single Page`, `Two-Page Spread`), rotation commands, `Page Colour` radio items (`Original`, `Dimmed`, `Inverted`), `Side Panel (Ctrl+B)` toggle, `Layout Preset` submenu (`Focus`, `Classic`, `Reading room`), and `Full Screen (F11)`. Done when: checked/radio states in the `View` menu stay synchronized with the active document view and layout preset.
- [ ] Populate the `Tools` menu with every SPEC 8.2 contextual mode bar and dialog trigger (`Annotate & Markup Ctrl+Shift+A`, `Form Designer`, `Redact & Sanitize Ctrl+Shift+X`, `Sign Document Ctrl+Shift+G`, `Compare PDFs Ctrl+Shift+D`, `Extract Tables & Images`, `Organize Pages`, `Crop & Resize`, `Watermark`, `Header / Footer & Bates`, `Optimize & Compress`). Done when: every tool entry names its resolving section or executes its live bar.
- [ ] Populate the `Window` menu with a live radio list of open document tabs plus `Next Tab (Ctrl+Tab)` and `Previous Tab (Ctrl+Shift+Tab)`, and the `Help` menu with `Keyboard Shortcuts` and `About Recto`. Done when: selecting an open tab in `Window` switches to that tab.
- [ ] Update menu item `IsEnabled` states automatically whenever the active tab changes between `Home` (no document) and an open `DocumentTabSession`. Done when: `Save`, `Print`, `Rotate`, and `Tools` items disable on `Home` and enable when a PDF tab is active.
- [ ] Add `Shell.MenuBarStructureAndState` test in `tests/recto-tests/` asserting all 6 top-level menus, item counts, shortcut labels, and enabled/disabled state transitions between `Home` and an open document tab. Done when: `recto-tests.exe --gtest_filter=Shell.MenuBarStructureAndState` passes.
- [ ] Commit: `app: build Classic menu bar (File, Edit, View, Tools, Window, Help) and TopBarSlot host`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.MenuBarStructureAndState` passes on the display-session host, and rendered `Classic` top bar matches `D-Classic.png`.

## 10. Shared SPEC 9.9 `ContentDialog` chrome and Password dialog

Establish the shared `RectoDialogStyle` (`CornerRadius=20`, `Padding=28 epx`, `RectoRaisedBrush`, `ThemeShadow` Z=64, alert full-width stacked button layout vs. form left-aligned side-by-side button layout per SPEC 9.9) and implement `PasswordDialog` (`src/app/dialogs/PasswordDialog.xaml/.h/.cpp`) wired to `DocumentActor::authenticate`: a password-protected PDF prompts before any page renders, wrong attempts keep the dialog open with a `RectoDangerBrush` border and `"That password didn't work."`, and `Cancel` tears down the pending tab and actor cleanly.

> **Deferred:** Queue-card password retry invocation from background conversion jobs -> XREF: D04 T01 §7

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6 (`24 epx` dialog header icons), 7 (`CornerRadius=20`, `ThemeShadow` Z=64), 9.2 and 9.9 (`Password` dialog row) with `design/screenshots/D-Dialogs.png`.
**Job:** The user can unlock an encrypted PDF (`RC4`, `AES-128`, or `AES-256`) with immediate inline feedback on mistyped passwords, or cancel cleanly without leaving a broken empty tab behind. Consumer: `OpenDocumentPipeline` (§5) and the Convert queue (`D04 T01 §7`).
**Treatment:** Shared `RectoDialogStyle` `ResourceDictionary` plus stateful `PasswordDialog` that validates credentials asynchronously against `DocumentActor::authenticate` inside `PrimaryButtonClick` (`args.Cancel(true)` while authenticating or on wrong password) so the dialog never closes and reopens on a typo. Cheaper substitute that fails the checkpoint: closing and reopening the dialog on a wrong password, or rendering blank pages behind the prompt before authentication succeeds.
**Chrome:** author `src/app/dialogs/RectoDialogStyles.xaml` consumed by all SPEC 9.9 dialogs across the app (`D02`, `D04`, `D05`). Do not style dialogs individually.

**Build order:**

1. Author `src/app/dialogs/RectoDialogStyles.xaml` (merged into `App.xaml`) defining the SPEC 9.9 `ContentDialog` template overrides (`CornerRadius=20`, `Padding=28 epx`, `RectoRaisedBrush`, `RectoBorderBrush`, `24 epx` header icon slot, `SubtitleTextBlockStyle` 20 epx Semibold title, alert stacked full-width button template and form side-by-side `Cancel` + accent primary button template). Done when: test dialogs in both Alert and Form layouts match `D-Dialogs.png` geometry and token colours in Light and Dark themes.
2. Build `PasswordDialog` (`src/app/dialogs/PasswordDialog.xaml/.h/.cpp`) with the `24 epx` lock `FontIcon`, `"Enter the password"` title, `"<filename> is protected."` body text, `PasswordBox` (auto-focused on open, `Enter` submits), inline error row (`RectoDangerBrush` border on `PasswordBox` + warning icon + `"That password didn't work."` text, hidden initially), and `Open` / `Cancel` buttons wired to `OpenDocumentPipeline` (§5). Done when: the encrypted corpus PDF unlocks on the correct password, displays the red border and `"That password didn't work."` in place on a wrong password, and closes the pending tab with zero leaked actors on `Cancel` or `Esc`.

- [ ] Author `src/app/dialogs/RectoDialogStyles.xaml` with `CornerRadius=20`, `28 epx` padding, `RectoRaisedBrush`, `ThemeShadow` Z=64, Alert stacked buttons, and Form side-by-side buttons per SPEC 9.9. Done when: both dialog templates render with zero hex literals and match `D-Dialogs.png`.
- [ ] Author `src/app/dialogs/PasswordDialog.xaml/.h/.cpp` with `24 epx` lock glyph, `"Enter the password"`, `"<file> is protected."`, `PasswordBox`, `Open` (primary accent), and `Cancel`. Done when: opening the dialog places keyboard focus directly inside `PasswordBox`.
- [ ] Keep `DocumentTabSession` in its pre-render `is_locked` state while `PasswordDialog` is open so zero page outlines or page contents appear behind the dialog. Done when: SPEC 9.2's rule ("show the password dialog before any page appears") holds in visual and state assertions. Cheaper substitute that fails the checkpoint: showing blank white pages behind the password dialog.
- [ ] Defer dialog dismissal in `PrimaryButtonClick` (`ContentDialogButtonClickDeferral`) while `DocumentActor::authenticate` runs on the actor thread. Done when: the UI thread stays responsive during AES-256 `R=6` key-derivation rounds.
- [ ] On `ErrorCode::InvalidPassword`, set `args.Cancel(true)` to keep `PasswordDialog` open in place, apply `RectoDangerBrush` to the `PasswordBox` border, show `"That password didn't work."` with a warning icon (colour is never the only signal per SPEC 11), and select the password text for immediate re-typing. Done when: a wrong password attempt never dismisses/re-animates the dialog.
- [ ] On `Cancel` button click or `Esc`, dismiss `PasswordDialog` and tear down the pending `DocumentTabSession` and its `DocumentActor` through §3's close path. Done when: cancelling a password prompt leaves zero zombie tabs or actors.
- [ ] Add `Shell.PasswordDialogFlow` test in `tests/recto-tests/` driving wrong-password retry, correct-password unlock, and cancel teardown against the encrypted corpus PDF. Done when: `recto-tests.exe --gtest_filter=Shell.PasswordDialogFlow` passes.
- [ ] Commit: `app: add shared SPEC 9.9 ContentDialog styles and PasswordDialog retry/cancel flow`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.PasswordDialogFlow` passes on the display-session host, and screenshots of `PasswordDialog` (initial and wrong-password states) match `D-Dialogs.png`.

## 11. Could-not-open error dialog and diagnostic clipboard copy

Implement the SPEC section 9.9 `"Couldn't open this file"` alert `ContentDialog` (`src/app/dialogs/CouldNotOpenDialog.xaml/.h/.cpp`): displays the `24 epx` warning icon, `"Couldn't open this file"`, `"<file> is damaged or isn't a PDF."`, and full-width stacked `OK` (primary) and `Copy error details` buttons. Clicking `Copy error details` formats a privacy-safe diagnostic block (filename without private parent directory paths, `PdfError` code and message, recent `recto::pdf::diagnostics_tail()` ring-buffer lines, Recto version, and `recto-pdf` ABI version) onto the Windows clipboard.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.9 (`Could not open` row, Alert layout with centred icon/title/text and full-width stacked buttons) with `design/screenshots/D-Dialogs.png`.
**Job:** When a file is corrupt, truncated, or not a PDF, the user gets a clear explanation and a one-click `Copy error details` payload ready to paste into a bug report without exposing their private filesystem hierarchy. Consumer: `OpenDocumentPipeline` (§5).
**Treatment:** Alert-styled `ContentDialog` consuming `RectoDialogStyles.xaml` (§10) and `recto::pdf::diagnostics_tail()` (`D01 T01 §1`) with path scrubbing. Cheaper substitute that fails the checkpoint: a generic Win32 `MessageBoxW`, silent open failure, or copying full `C:\Users\<username>\...` paths into the clipboard diagnostic block.
**Chrome:** consume the Alert template from `RectoDialogStyles.xaml` (§10), `RectoWarningBrush`, and Segoe Fluent warning glyph (`24 epx`).

**Build order:**

1. Implement `format_open_error_Report(filename, pdf_error, diagnostics)` in `src/app/dialogs/CouldNotOpenDialog.h/.cpp` that strips parent directory paths (keeping only `path.filename()`), formats the `ErrorCode` name, `PdfError::message`, byte offset (if present), last diagnostic ring-buffer entries for that failure, Recto version, and `recto::pdf::abi_version()`. Done when: unit tests confirm the generated report contains the filename, error code, and engine version while containing zero parent directory segments (`C:\Users\...`).
2. Build `CouldNotOpenDialog` (`src/app/dialogs/CouldNotOpenDialog.xaml/.h/.cpp`) in the SPEC 9.9 Alert layout (centred `24 epx` warning icon, `"Couldn't open this file"`, `"<file> is damaged or isn't a PDF."`, stacked full-width `OK` and `Copy error details` buttons), wire `Copy error details` to `Windows::ApplicationModel::DataTransfer::Clipboard::SetContent`, and connect the dialog to `OpenDocumentPipeline` (§5) with a queue so multiple bad files dropped together show sequentially without overlapping `ContentDialog` exceptions. Done when: opening every file in `tests/corpus/broken/` presents `CouldNotOpenDialog`, copies the scrubbed diagnostic text to the clipboard, and leaves the app clean.

- [ ] Author `src/app/dialogs/CouldNotOpenDialog.xaml/.h/.cpp` using the Alert layout from `RectoDialogStyles.xaml` (centred `24 epx` warning glyph, `"Couldn't open this file"`, `"<file> is damaged or isn't a PDF."`, full-width stacked `OK` and `Copy error details` buttons). Done when: the rendered dialog matches the `Could not open` board in `D-Dialogs.png`.
- [ ] Implement `format_open_error_report` combining `path.filename()`, `PdfError` code/message/offset, `recto::pdf::diagnostics_tail()`, and engine/app version strings while scrubbing all directory prefixes above the filename. Done when: the formatted string for `C:\Users\Alice\Secret\bad.pdf` contains `bad.pdf` and never `Alice` or `Secret`. Cheaper substitute that fails the checkpoint: dumping raw full paths or omitting the engine error details.
- [ ] Wire `Copy error details` to copy the formatted report to the Windows `Clipboard` (`DataPackage`) and update the button label briefly to `"Copied"` with a screen-reader announcement. Done when: clicking `Copy error details` places the exact diagnostic block on the system clipboard.
- [ ] Queue dialog presentations in `OpenDocumentPipeline` so dropping 3 broken files in one batch displays `CouldNotOpenDialog` sequentially rather than throwing WinUI's single-active-`ContentDialog` `COMException`. Done when: dropping 3 corrupt files shows 3 dialogs in sequence with zero exceptions.
- [ ] Ensure the failed tab's temporary `DocumentTabSession` and `DocumentActor` are completely torn down before or when `CouldNotOpenDialog` opens. Done when: no broken tab remains in `TabView` after a failed open.
- [ ] Add `Shell.CouldNotOpenAndDiagnostics` test in `tests/recto-tests/` opening `tests/corpus/broken/` files, asserting path-scrubbed diagnostic output on the clipboard, and verifying sequential multi-error dialog queuing. Done when: `recto-tests.exe --gtest_filter=Shell.CouldNotOpenAndDiagnostics` passes.
- [ ] Commit: `app: implement CouldNotOpenDialog with path-scrubbed diagnostic clipboard copy`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.CouldNotOpenAndDiagnostics` passes on the display-session host against `tests/corpus/broken/`, clipboard contents match the scrubbed diagnostic format, and screenshot matches `D-Dialogs.png`.

## Verification

- Every M2 shell obligation from `design/SPEC.md` (sections 3, 4, 7, 8, 8.1, 9.1 drop overlay, 9.2 loading states, 9.9 Password / Could-not-open / Replace-original / Unsaved-changes dialogs, 10 shell keys, 11 automation names and tab order) proven on rendered surfaces and by `Shell.*` tests in `recto-tests`.
- Full `recto-tests` suite green; `Recto.sln` builds `Debug` and `Release` for `x64` and `ARM64` with zero warnings; `python scripts/todo-graph.py validate` passes.
- Zero `#` hex colour literals exist under `src/app` outside `design/tokens/RectoColors.xaml`.
