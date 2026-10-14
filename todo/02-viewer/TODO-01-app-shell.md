---
schema_version: 1
id: app-shell
domain: 02-viewer
status: active
title: "TODO-01 -- App Shell, Tabs and Document Lifecycle"
track: M2
---

# TODO-01 -- App Shell, Tabs and Document Lifecycle

> **Goal:** The WinUI 3 app exists as the SPEC's one shell: a custom title bar with a tab per open document, documents opening through every path Windows offers, and the password and could-not-open dialogs behaving exactly as `design/SPEC.md` sections 8 and 9.9 specify, with tokens only from `RectoColors.xaml`.

> **Current state:** Measured 2026-10-13. No app code exists. D00 T01 §3 will create the empty WinUI 3 packaged app project (Windows App SDK 2.4.0, C++/WinRT, D4) and D01 provides `DocumentHandle` open, authenticate and describe. Everything visual is already decided: `design/SPEC.md` sections 4 to 8 (tokens, type, icons, shape, the shell and its title bar), section 9.2 (loading and password behavior), 9.9 (dialogs), 10 (keyboard), 11 (accessibility), with `design/screenshots/D-Focus-Light.png`, `D-Classic.png` and `D-Dialogs.png` as the boards. Package assets are generated and waiting in `resources/msix/Assets/` with the manifest mapping in `resources/README.md`.

## Inputs

- `design/SPEC.md` sections 3 (brand and assets), 4 (colour), 5 (typography), 6 (icons), 7 (shape and spacing), 8 and 8.1 (shell and title bar), 9.2 (loading, password), 9.9 (dialogs), 11 (accessibility); `design/tokens/RectoColors.xaml`.
- `resources/README.md` (where every generated asset goes in `Package.appxmanifest`) and `resources/svg/` (wordmark and in-app graphics).
- -> XREF: D07 T01 §2 -- packaging completes what §1 here starts: the manifest this file authors is the one MSIX ships, including the `.pdf` association assets.
- -> XREF: D02 T04 §1 -- the layout preset engine moves this shell's controls between slots; the slot names are fixed in §2 here and consumed there.

## Outcome

- The app launches to the SPEC shell: icon, wordmark, Home tab, document tabs, command box button, settings, caption buttons, correct in light, dark and high contrast.
- A PDF opens from the picker, drag-and-drop, the command line and file activation, each in its own tab with the document lifecycle (loading, password, error) behaving per SPEC 9.2 and 9.9.
- Closing a tab closes exactly its document (actor shut down through `Runtime`), and the window honors the SPEC minimum size and keyboard map for shell commands.

**Adjacency:** list=applicable @ D02 T03 §1; document=not-applicable (no printed output from the shell; printing is D07 T01 §1); settings=applicable @ D02 T04 §2; reporting=not-applicable (no reports in a document viewer shell); notifications=applicable @ D04 T01 §3; permissions=not-applicable (single-user desktop app, no roles; document-level refusals such as links live in D02 T02 §4); audit=not-applicable (no change history over shell state); exchange=not-applicable (the clipboard and share paths belong to D03 T01 §2); reverse=applicable @ D02 T01 §2

**Adjacency rationale:** The shell's own reverse is closing what it opened: tab close with full actor teardown, owned in section 2. Lists (recent files), settings and notifications are real Recto surfaces the shell hosts but does not own, so they are declared with their owning sections instead of absorbed here. Permissions as roles do not exist in a single-user viewer; the refusals that do exist (links, macros) are document-level and owned where they trigger.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | App project, manifest, assets and tokens | D00 T01 §3 | [ ] |
| 2 | §2 | Title bar, tabs and shell slots | §1 | [ ] |
| 3 | §3 | Open paths: picker, drop, command line, activation | §2, D01 T02 §1 | [ ] |
| 4 | §4 | Password and could-not-open dialogs | §3 | [ ] |

## 1. App project, manifest, assets and tokens

The packaged app skeleton with the SPEC's foundations wired before any screen is drawn: generated assets in place, the token dictionary merged, theme dictionaries (Light, Dark, HighContrast) live, and the window icon set. Getting tokens in first is what makes "no hex on a control" enforceable from the first XAML file.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 3 and 4 with `design/screenshots/D-Brand.png` (icon, wordmark, tokens) and `D-Focus-Dark.png` (dark surfaces). Placement and colours match the boards through tokens, never literals.
**Job:** The user can launch Recto and recognize it (icon, wordmark, vermilion accent) in either theme. Consumer: every later surface reads these tokens and assets.
**Treatment:** Stock WinUI 3 packaged app with `RectoColors.xaml` merged into `App.xaml` and the system accent ramp replaced per SPEC section 4. Cheaper substitute that fails the checkpoint: per-control colours or the Windows blue accent left in place.
**Chrome:** consume `design/tokens/RectoColors.xaml` brushes and stock `ThemeDictionaries`; wordmark and icons from `resources/svg/` via `SvgImageSource` with light and dark theme resources. Do not invent a second theme mechanism.

**Build order:**

1. Copy `resources/msix/Assets/` into `src/app/Assets/`, author `src/app/Package.appxmanifest` per `resources/README.md` (identity placeholder values where the Store will assign real ones, `.pdf` file-type association with `Assets\FileTypes\PdfFile.png`, splash background `#1B1A19` through the token-named value in the manifest as the README shows). Done when: the package builds and the app tile and splash render the Recto icon, not the template's.
2. Merge `design/tokens/RectoColors.xaml` into `App.xaml`, set the window icon from `resources/ico/recto.ico` (`AppWindow.SetIcon`), and verify the accent mapping SPEC section 4 warns about against Windows App SDK 2.4.0, adjusting the ramp (not controls) if it differs. Done when: a stock accent `Button` renders `#C8431F` in light and `#F0603C` in dark, sampled on the rendered window.

- [ ] Manifest, assets and app entry in `src/app/`. Done when: the packaged app installs from the build output and launches to an empty branded window. Cheaper substitute that fails the checkpoint: unpackaged-only wiring that skips the manifest asset rules.
- [ ] Token dictionary merged with Light, Dark and HighContrast dictionaries. Done when: no XAML in `src/app` contains a `#` hex colour outside `RectoColors.xaml` (search proves it).
- [ ] Commit: `app: packaged WinUI shell skeleton with Recto tokens and assets`

**Test checkpoint:** the app launches in light and dark themes on the display-session host with the accent sampled at `#C8431F` and `#F0603C`, and the hex search over `src/app` (excluding the token file) returns nothing.

## 2. Title bar, tabs and shell slots

SPEC 8.1 fixes the title bar order: small icon, wordmark, Home tab, one `TabView` document tab each (max 240 epx, hover close), new tab, centred command box button, settings, system caption buttons, draggable outside interactive elements. Under it, `ShellPage` provides the named slots every preset uses (title bar, top bar, bottom overlay, left dock, right dock, left overlay, right overlay, bottom dock, SPEC section 8); D02 T04 moves controls between them, so the slots and their names are this section's contract.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 8 and 8.1 with `design/screenshots/D-Focus-Light.png`, `D-Classic.png` and `D-Layout.png` (slot arrangement per preset).
**Job:** The user can hold several documents open, switch and close them, and always reach Home, the command box and Settings. Consumer: every mode page (Home, Viewer, Convert, Organize) is hosted by this shell.
**Treatment:** One `ShellPage` hosting shared controls in named slots, stock `TabView` for documents. Cheaper substitute that fails the checkpoint: a separate window per document, or preset-specific shells (refused by D17).
**Chrome:** consume the slot hosts and the `TabView` styling from this section everywhere; later sections add content to slots, never new chrome. Do not invent a second tab strip.

**Build order:**

1. Build `ShellPage` with the custom title bar (`Window.ExtendsContentIntoTitleBar`, drag regions correct around interactive elements), the Home tab, document `TabView` with add and close, the command box button (opens D02 T03 §4's surface; a stated placeholder until then) and the settings button. Done when: the window matches `D-Focus-Light.png` title-bar order and metrics side by side, and snap layouts and caption buttons behave as stock.
2. Tab lifecycle against `DocumentHandle`: creating a tab creates its document session state (document opens in §3), closing a tab tears its actor down through `Runtime` (D01 T02 §4), Ctrl+W and Ctrl+Tab follow SPEC section 10. Done when: closing the last document tab leaves Home, and the live-clone assert holds in Debug after rapid open and close.
3. Declare the named slot hosts and a `LayoutSettings` placeholder shape the preset engine (D02 T04 §1) will drive. Done when: moving a test control between two slots at runtime works without recreating it.

- [ ] `ShellPage`, title bar and document tabs. Done when: keyboard order is title bar, toolbar slot, side panel slot, page (SPEC 11) and every icon-only button has an automation name and shortcut tooltip. Cheaper substitute that fails the checkpoint: the stock title bar with tabs squeezed below it.
- [ ] Tab close equals full document teardown. Done when: 20 rapid open and close cycles in the UI leave the Debug live-clone count at zero.
- [ ] Commit: `app: shell with title-bar tabs and named layout slots`

**Test checkpoint:** rendered comparison against `D-Focus-Light.png` and `D-Focus-Dark.png` title bars on the display-session host (screenshots in the review findings), plus the open and close cycle test green.

## 3. Open paths: picker, drop, command line, activation

Every doorway Windows gives a document app, all ending at the same `Runtime::open` call on the UI's behalf (plan 4.6): the system file picker (Ctrl+O), drag-and-drop anywhere in the window, command-line arguments, and packaged file activation from Explorer. Loading shows page outlines at the right size immediately once the viewer lands (D02 T02); here the tab enters its loading state and the actor reports description or failure back.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.1 (drop anywhere, drop overlay) and 9.2 (loading), with `design/screenshots/D-Home.png`.
**Job:** The user can open a PDF by the path they already have in hand. Consumer: the document view (D02 T02) reads the opened handle and description.
**Treatment:** One open pipeline behind four entry points, system picker and activation contracts stock. Cheaper substitute that fails the checkpoint: picker-only opening, or a second open path that bypasses the actor.
**Chrome:** consume the Home drop overlay visual (`resources/svg/drop-files-*.svg`) and the shell's tab creation from §2. Do not invent a second drop surface.

**Build order:**

1. Wire the picker (`FileOpenPicker` with the window handle), drag-and-drop with the full-window overlay, `AppInstance` command-line parsing and `FileActivatedEventArgs` handling into one `OpenFilesAsync` path that creates tabs and starts actors. Done when: all four paths open the same corpus file into identical tabs.
2. Multi-file and mixed results: several files open several tabs; a non-PDF or damaged file routes to §4's dialog without disturbing the good tabs. Done when: dropping the render corpus plus the broken corpus opens the good files and reports each bad one once.

- [ ] Four open paths over one pipeline in `src/app`. Done when: Explorer double-click on a `.pdf` with Recto associated (development package) opens that file in a tab. Cheaper substitute that fails the checkpoint: handling activation only when the app is already running.
- [ ] Loading and tab title states (file name, then document title where the metadata provides one, per the description API). Done when: a slow open never shows a blank unresponsive tab.
- [ ] Commit: `app: open PDFs by picker, drop, command line and activation`

**Test checkpoint:** `recto-tests --gtest_filter=Shell.OpenPaths` drives scripted opens through all four paths on the display-session host, with the broken-corpus file producing the §4 dialog path and no crash (screenshots and log quoted in findings).

## 4. Password and could-not-open dialogs

SPEC 9.2 and 9.9: a password-protected file shows the password dialog before any page appears; a wrong password keeps the dialog with a red border and "That password didn't work."; a damaged or non-PDF file gets "Couldn't open this file" with a Copy error details action. Both are `ContentDialog` restyled to the SPEC dialog shape (radius 20, 28 epx padding, alert stacking).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.9 dialog table with `design/screenshots/D-Dialogs.png` (password and could-not-open boards).
**Job:** The user can unlock a protected document or understand precisely why one would not open. Consumer: the actor (the password attempt is posted back for `pdf::Document::authenticate`); the log (copied error details).
**Treatment:** Two `ContentDialog`s in the shared dialog style later dialogs reuse. Cheaper substitute that fails the checkpoint: `MessageBox`-style stock dialogs, or opening a protected tab showing blank pages behind the prompt.
**Chrome:** consume the dialog style, tokens and Segoe Fluent glyphs (lock, warning) established here for all SPEC 9.9 dialogs. Do not invent per-dialog styling.

**Build order:**

1. Password dialog wired to the actor's needs-password report: submit posts the attempt, success proceeds to description, failure stays with the error state, Cancel closes the tab through the §2 teardown. Done when: the encrypted corpus file opens with its known password, refuses a wrong one in place, and Cancel leaves no actor behind.
2. Could-not-open dialog carrying the `PdfError` message with Copy error details (file name, error text, Recto and `recto-pdf` engine versions) to the clipboard. Done when: every `tests/corpus/broken/` file lands here with text a bug report could use verbatim.

- [ ] Password dialog with retry and cancel semantics. Done when: no page content renders behind it at any point. Cheaper substitute that fails the checkpoint: closing and reopening the dialog per attempt, losing the entered state.
- [ ] Could-not-open dialog with copyable details. Done when: the pasted details name the file and the engine error without a path into the user's private folders beyond what they opened.
- [ ] Commit: `app: password and could-not-open dialogs`

**Test checkpoint:** driven on the rendered dialogs against `D-Dialogs.png` (screenshots in findings): correct password opens, wrong password shows the SPEC error state, broken file shows could-not-open with working Copy.

## Verification

- M2's shell obligations from the SPEC (sections 8, 8.1, 9.2 loading states, 9.9 dialogs owned here, 10 shell keys, 11 automation names) each proven on rendered surfaces, not source.
- Full `recto-tests` suite green; the app builds x64 and ARM64; `python scripts/todo-graph.py validate` passes.
- No hex colour outside `RectoColors.xaml` under `src/app` (searched, quoted).
