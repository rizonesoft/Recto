---
schema_version: 1
id: accessibility
domain: 03-text
status: active
title: "TODO-02 -- Accessibility: UIA Text, Keyboard Order and Announcements"
track: M3
---

# TODO-02 -- Accessibility: UIA Text, Keyboard Order and Announcements

> **Goal:** Recto is operable and readable without sight or a mouse: page text exposed through a UI Automation text provider (the M3 accessibility deliverable), two-way selection and form/annotation peer exposure, an automated control accessibility inventory (`AutomationProperties.Name`, shortcut tooltips, `32 epx` / `36 epx` target bounds), a logical keyboard order with visible accent focus across all three presets, and polite coalescing announcements for the states a sighted user would glance at.

> **Current state:** Measured 2026-10-14. No code exists; the obligations are written down in SPEC section 11 and scattered through sections 9 and 10 (selection announced, hit targets at least `32 epx`, `36 epx` in the floating toolbar, colour never the only signal). Sections in `D02` and `D03 T01` each carry their own surface-level accessibility duties; this file builds the UIA text provider they cannot, and audits the whole app against section 11 as the M3 gate. Reduced motion and high contrast are owned by `D02 T04 §7` and `D02 T04 §8` and re-verified here where text surfaces touch them.

## Inputs

- `design/SPEC.md` sections 7 (target sizes), 9.3 (selection announcements), 10 (keyboard navigation), 11 (the accessibility contract), 4 (`HighContrast` dictionary).
- `docs/plans/2026-10-13-recto-foundation.md` sections 6.3 (`PageText` shared with UIA text provider at M3), 7 (M3 row).
- Microsoft UI Automation provider documentation for `ITextProvider`, `ITextRangeProvider` and WinUI `AutomationPeer` authoring.
- -> XREF: D02 T04 §7 -- HighContrast and reduced-motion theme hooks (`D02 T04 §7`, `§8`) and keyboard traversal (`D02 T04 §9`) are audited by `§3` and `§4` here.
- -> XREF: D03 T03 §10 -- Interactive form fields and annotation comments expose UIA peers alongside the page text provider.
- -> XREF: D04 T01 §6 -- Conversion job completion notifications invoke the polite announcement sink built in §6 here.
- -> XREF: D07 T02 §6 -- the M6 Store & Release Compliance verification (`D07 T02 §6`) gates on the UIA verification pass in `§6` here.

## Outcome

- A screen reader can read a page top to bottom, move by character, word, line, and page, traverse tagged PDF `/StructTreeRoot` structure (`HeadingLevel` for `/H1`-`/H6`, `/Table`/`/TR`/`/TH`/`/TD` cells, `/L`/`/LI` lists, and `/Figure` `/Alt` descriptions, falling back to `PageText` geometric reading order when untagged), select text, and follow find results against the same `PageText` model sighted selection uses (`§1`, `§2`).
- Every icon-only control across the shell, panels, and toolbars is verified by an automated inventory to carry `AutomationProperties.Name`, a shortcut tooltip, and `>= 32 epx` (`>= 36 epx` in floating bars) hit targets (`§3`).
- Keyboard-only operation reaches every control in title-bar, toolbar, panel, page order with a visible `--accent` focus indicator across `Focus`, `Classic` and `Reading room` presets (`§4`).
- Find counts, current page changes, selection extents, document open/close, and conversion completion (when `D04 T01 §6` emits its events) announce politely without stealing focus (`§5`, `§6`).

**Adjacency:** all=not-applicable (an access layer over other files' records and surfaces; it owns no data, and its every element is the accessible twin of a surface owned elsewhere)

**Adjacency rationale:** Accessibility here is a property of surfaces other sections build, plus one genuinely new surface (the UIA text provider), which is the accessible form of `D03 T01`'s text model. Declaring product adjacencies would double-own them; the file's contract is instead the section 11 audit in `§3` and `§4`, which checks each owning surface in place.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `DocumentViewAutomationPeer` implementing `ITextProvider` and `ITextRangeProvider` | D03 T01 §2, D02 T02 §5 | [ ] |
| 2 | §2 | Two-way UIA text selection synchronization, `FindText`, and child peer hooks | §1, D03 T01 §3 | [ ] |
| 3 | §3 | Automated control accessibility inventory (`AutomationProperties.Name`, `32/36 epx`) | D02 T04 §10 | [ ] |
| 4 | §4 | Logical `Tab` / `Shift+Tab` focus order and `--accent` focus ring across all 3 presets | §3 | [ ] |
| 5 | §5 | Polite coalescing `AnnouncementService` for page changes, find counts, and selection | §2, D03 T01 §6 | [ ] |
| 6 | §6 | Conversion job completion announcement sink (`D04 T01 §6`) and UIA verification pass | §4, §5 | [ ] |

## 1. `DocumentViewAutomationPeer` implementing `ITextProvider` and `ITextRangeProvider`

Implement `DocumentViewAutomationPeer` (`src/app/a11y/DocumentViewAutomationPeer.h/.cpp`) and the backing `TextRangeModel` (`src/services/uia_text_range.h/.cpp`) implementing the UI Automation `ITextProvider` and `ITextRangeProvider` patterns over `D03 T01`'s shared `PageText` model (including OCR-populated `PageText` on scanned and vector-outlined pages) and any existing PDF `/StructTreeRoot` logical structure tree (`SPEC 11`): document range, unit movement (`Character`, `Word`, `Line`, `Paragraph`/`Block`, `Page`, `Document`), `StyleId_Heading1`..`StyleId_Heading6` attribute exposure from tagged `/H1`..`/H6` elements (or inferred heading levels), `GetBoundingRectangles`, `RangeFromPoint`, and `ScrollIntoView`.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 11 (page text exposed through a UIA text provider, with existing tagged-PDF `/StructTreeRoot` headings, tables, reading order, and `/Alt` descriptions surfaced to assistive technology); no visual board governs the peer itself.
**Job:** A blind user can read any digital, tagged, or OCR-recognized PDF page top to bottom and navigate by character, word, line, block, heading, or page with Narrator or NVDA. Consumer: Windows Narrator, NVDA, JAWS, Accessibility Insights, and `A11y.TextProvider` automated UIA client tests.
**Treatment:** `ITextProvider` and `ITextRangeProvider` over the shared `PageText` model (ordered by `/StructTreeRoot` `/MCID` reading sequence when the PDF is tagged, or `D03 T01 §1` geometric column/block reading order when untagged) with `(page_index, char_index)` range endpoints and screen-space bounding rectangles computed via `D02 T02 §5`. Cheaper substitute that fails the checkpoint: exposing only the control name and a static plain-text dump, ignoring `/StructTreeRoot` reading order and heading styles on tagged PDFs, or returning `E_NOTIMPL` for word and line unit navigation.
**Chrome:** consume `PageText` (`D03 T01 §1`-`§2`) and `DocumentView` geometry (`D02 T02 §5`). Do not invent a parallel text store for accessibility.

**Build order:**

1. Implement `UiaTextRange` (`src/services/uia_text_range.cpp`) over `(start_page, start_char)` and `(end_page, end_char)` endpoints backed by `DocumentActor::get_page_text`, supporting `Clone`, `Compare`, `CompareEndpoints`, `ExpandToEnclosingUnit`, `Move`, `MoveEndpointByUnit`, `MoveEndpointByRange`, and `GetText` across `TextUnit::Character`, `Word`, `Line`, `Paragraph`, `Page`, and `Document`, honoring `/StructTreeRoot` reading order and `/H1`..`/H6` `StyleIdAttribute` when the PDF is tagged. Done when: `A11y.TextRangeUnits` unit tests walk both an untagged two-column corpus page and a tagged `/StructTreeRoot` PDF by word, line, paragraph, and heading in reading order.
2. Implement `DocumentViewAutomationPeer` (`src/app/a11y/DocumentViewAutomationPeer.cpp`) overriding `GetPatternCore(PatternInterface::Text)` and wiring `DocumentRange`, `GetVisibleRanges`, `RangeFromPoint`, `GetBoundingRectangles`, and `ScrollIntoView`. Done when: an in-process UIA client (`IUIAutomationTextPattern`) queries `DocumentView`, reads page text, and scrolls a target line into view.

- [ ] Implement `UiaTextRange` in `src/services/uia_text_range.h/.cpp` representing an ordered `(page_index, char_index)` interval over `PageText` snapshots fetched on demand from `DocumentActor`. Done when: `UiaTextRange` handles single-page and multi-page spans without copying full-document strings into memory.
- [ ] Implement `ExpandToEnclosingUnit`, `Move`, and `MoveEndpointByUnit` for `TextUnit::Character`, `Word`, `Line`, `Paragraph` (`PageText::Block` or `/StructTreeRoot` block element), `Page`, and `Document`. Done when: a screen-reader walk of a two-column corpus page reads column 1 top-to-bottom before column 2, and a tagged PDF follows its `/StructTreeRoot` `/K` sequence. Cheaper substitute that fails the checkpoint: pattern methods returning not-implemented for word and line units.
- [ ] Implement `GetText(int max_length)` returning UTF-8/UTF-16 text for the range (including OCR-recognized text layers from `D03 T01 §8`) truncated cleanly at `max_length` when `>= 0`. Done when: `GetText(-1)` on a page range equals `PageText::to_utf8_string()`.
- [ ] Implement `GetAttributeValue` for `FontNameAttribute`, `FontSizeAttribute`, `FontWeightAttribute`, `IsItalicAttribute`, `StyleIdAttribute` (`StyleId_Heading1`..`StyleId_Heading6` mapped from tagged `/StructTreeRoot` `/H1`..`/H6` nodes), and `IsReadOnlyAttribute` (`true`) sourced from `PageText::Span`. Done when: UIA attribute queries return the exact font name, point size, bold/italic flags, and heading `StyleId` of the underlying span.
- [ ] Implement `GetBoundingRectangles` transforming the range's coalesced line quads from PDF page points to screen pixel coordinates through `D02 T02 §5`'s `page_to_viewport` transform and window screen offset. Done when: Narrator's blue highlight rectangle aligns over the spoken word on screen.
- [ ] Implement `RangeFromPoint(Point screen_pt)` and `GetVisibleRanges()` returning the visible page text ranges currently intersecting the viewport. Done when: hit-testing a screen point through UIA returns the character range under the cursor.
- [ ] Implement `ScrollIntoView(bool align_to_top)` scrolling `DocumentView` (`D02 T02 §4`) so the target text range is visible inside the viewport. Done when: `A11y.TextProvider` calls `ScrollIntoView` on a page-5 range and verifies the viewport jumps to page 5.
- [ ] Commit: `feat(a11y): implement DocumentViewAutomationPeer ITextProvider and ITextRangeProvider`

**Test checkpoint:** `recto-tests.exe --gtest_filter=A11y.TextRangeUnits:A11y.TextProvider` passes, verifying document text, word and line expansion, tagged `/StructTreeRoot` heading attributes, bounding rectangles, and `ScrollIntoView`.

## 2. Two-way UIA text selection synchronization, `FindText`, and child peer hooks

Connect `DocumentViewAutomationPeer` (`§1`) bidirectionally to `SelectionModel` (`D03 T01 §3`), delegate `ITextRangeProvider::FindText` to `D03 T01 §5`'s `SearchEngine`, and expose child `AutomationPeer` hooks (`RangeFromChild` / `GetChildrenCore` / `GetEnclosingElement`) so interactive annotations and form fields (`D03 T03 §10`) as well as tagged PDF `/StructTreeRoot` structure elements (`/H1`..`/H6` with `AutomationProperties::HeadingLevel`, `/Table` / `/TR` / `/TH` / `/TD` with `ITableProvider`/`IGridItemProvider`, and `/Figure` with `/Alt` accessible names per `SPEC 11`) appear in the UIA tree at their page coordinates.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.3 and 11 (selection synchronized between visual canvas and screen readers, and tagged-PDF `/StructTreeRoot` structure and `/Alt` figure descriptions exposed through UIA), with `D-Search.png`.
**Job:** Selecting text with the mouse updates the screen reader's selection immediately, selecting text via a screen reader (`ITextRangeProvider::Select`) paints the exact `RectoTextSelectionBrush` quads on canvas and feeds `Ctrl+C`, and screen readers can jump across tagged headings, tables, figures (`/Alt`), and form controls. Consumer: `SelectionModel` (`D03 T01 §3`), Windows clipboard (`D03 T01 §4`), tagged PDF structure peers, and `D03 T03 §10` form/annotation peers.
**Treatment:** Bidirectional binding between `SelectionModel` and `DocumentViewAutomationPeer::GetSelection` / `ITextRangeProvider::Select` / `RemoveFromSelection`, raising `AutomationEvents::TextPatternOnTextSelectionChanged`, plus child `AutomationPeer` projection for tagged `/StructTreeRoot` nodes and interactive widgets. Cheaper substitute that fails the checkpoint: one-way selection where screen-reader selection does not update the visual canvas or clipboard, or dropping `/StructTreeRoot` `/Alt` and table/heading structure peers.
**Chrome:** consume `SelectionModel` (`D03 T01 §3`) and `SearchEngine` (`D03 T01 §5`). Do not maintain separate UIA and visual selection states.

**Build order:**

1. Wire `ITextProvider::GetSelection()`, `ITextRangeProvider::Select()`, and `ITextRangeProvider::RemoveFromSelection()` directly to `SelectionModel`, and raise `AutomationEvents::TextPatternOnTextSelectionChanged` whenever pointer, keyboard, or UIA selection changes. Done when: selecting by mouse then reading the UIA selection (and the reverse) agree character-for-character.
2. Implement `ITextRangeProvider::FindText(text, backward, ignore_case)` delegating to `D03 T01 §5`'s `SearchEngine`, and add `GetChildrenCore` / `RangeFromChild` / `GetEnclosingElement` child peer projection for tagged `/StructTreeRoot` nodes (`/H1`..`/H6`, `/Table`, `/Figure` `/Alt`) and `D03 T03 §10` widgets. Done when: `A11y.SelectionAndFind` and `A11y.TaggedStructTreePeers` pass end-to-end.

- [ ] Implement `ITextProvider::GetSelection()` returning a `UiaTextRange` matching `SelectionModel`'s current `(anchor, active)` endpoints (or an empty array when nothing is selected). Done when: pointer-selecting a word on canvas and querying `GetSelection()` via UIA returns that exact word range.
- [ ] Implement `ITextRangeProvider::Select()` and `RemoveFromSelection()` updating `SelectionModel` so `RectoTextSelectionBrush` quads paint on canvas and `Ctrl+C` (`D03 T01 §4`) copies the peer-selected text. Done when: calling `Select()` from an automation client updates the visual highlight and clipboard fixture. Cheaper substitute that fails the checkpoint: `Select()` returning `S_OK` without updating `SelectionModel`.
- [ ] Raise `AutomationPeer::RaiseAutomationEvent(AutomationEvents::TextPatternOnTextSelectionChanged)` with a 50 ms drag-coalescing window whenever `SelectionModel` changes. Done when: UIA event listeners receive `TextSelectionChanged` without event flooding during mouse drag.
- [ ] Implement `ITextRangeProvider::FindText(BSTR text, BOOL backward, BOOL ignore_case)` over the enclosing range using `D03 T01 §5`'s normalized search matcher (including ligature and soft-hyphen folding). Done when: a UIA client calling `FindText` locates matches across ligatures and line wraps.
- [ ] Implement `GetChildrenCore()`, `RangeFromChild(IRawElementProviderSimple*)`, and `GetEnclosingElement()` on `DocumentViewAutomationPeer` so tagged `/StructTreeRoot` elements (`/H1`..`/H6` with `AutomationHeadingLevel`, `/Table`/`/TR`/`/TH`/`/TD` grid peers, and `/Figure` image peers with `AutomationProperties.Name` set to `/Alt` text) and interactive form widgets/annotations registered by `D03 T03 §10` expose child automation peers anchored to their character offset on the page. Done when: `A11y.TaggedStructTreePeers` verifies heading levels, table cell coordinates, `/Figure` `/Alt` names, and `RangeFromChild` mapping on a tagged corpus PDF.
- [ ] Verify `HighContrast` (`ContrastThemes::HighContrast` from `D02 T04 §7`) selection and search highlight legibility over `DocumentView` when UIA selection is active. Done when: `A11y.SelectionAndFind` verifies high-contrast token binding.
- [ ] Commit: `a11y: UI Automation text provider over the shared PageText model`

**Test checkpoint:** `recto-tests.exe --gtest_filter=A11y.TextRangeUnits:A11y.TextProvider:A11y.SelectionAndFind:A11y.TaggedStructTreePeers` passes, quoting document text, word and line expansion, tagged `/StructTreeRoot` `/Alt` and heading/table peers, `FindText`, and two-way selection sync.

## 3. Automated control accessibility inventory (`AutomationProperties.Name`, `32/36 epx`)

Build the automated control accessibility inventory in `tests/recto-tests/a11y_inventory_tests.cpp`: enumerate every interactive control across the shell, title bar, main toolbar, floating page toolbar, floating selection toolbar, side panels, Convert, Organize, Settings, About, and dialogs from `CommandRegistry` (`D02 T01 §5`) and the live XAML visual tree, asserting that every icon-only button carries a non-empty `AutomationProperties.Name`, a tooltip (with keyboard shortcut where bound), and `32/36 epx` hit-target bounds (`>= 32 x 32 epx`, or `>= 36 x 36 epx` in the floating selection toolbar).

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 7 (minimum `32 epx` interactive hit targets; `36 epx` in floating selection toolbar) and 11 (every icon-only button carries an automation name and shortcut tooltip).
**Job:** Every button and control in Recto has a spoken accessible name, a hover tooltip with its shortcut, and a comfortable hit target, enforced by an automated test that fails whenever an unnamed or undersized button is added. Consumer: screen readers, voice control (`Windows Voice Access`), keyboard/mouse users, and CI regression suite.
**Treatment:** Automated XAML tree + `CommandRegistry` walker that instantiates each surface and checks `AutomationProperties::GetName`, `ToolTipService::GetToolTip`, and `MinWidth`/`MinHeight`/`ActualWidth`/`ActualHeight`. Cheaper substitute that fails the checkpoint: spot-checking a few famous toolbar buttons by hand with no automated enumeration.
**Chrome:** consume `CommandRegistry` (`D02 T01 §5`) as the single source of truth for command labels and shortcut strings so tooltips and automation names never drift.

**Build order:**

1. Write the XAML visual-tree and `CommandRegistry` accessibility walker (`src/app/a11y/A11yInventory.h/.cpp`) that visits every interactive `Control` (`Button`, `ToggleButton`, `AppBarButton`, `RadioButton`, `ComboBox`, `TextBox`, `Slider`, `NavigationViewItem`) across all views and presets. Done when: the walker produces a complete structured table of control IDs, automation names, tooltips, and pixel bounds.
2. Fix any missing `AutomationProperties.Name`, tooltip, or `MinWidth`/`MinHeight` gaps at their owning XAML surfaces and add the negative-proof test (planting an unnamed button causes `A11y.Inventory` to fail, then removing it). Done when: `A11y.Inventory` passes with zero unnamed icon-only controls and zero undersized targets.

- [ ] Implement `A11yInventory::scan_element_tree(DependencyObject root)` in `src/app/a11y/A11yInventory.cpp` traversing all interactive controls and recording `class_name`, `automation_id`, `automation_name`, `tooltip_text`, `width_epx`, and `height_epx`. Done when: `scan_element_tree` visits every control in the shell, toolbars, side panels, floating bars, and mode pages.
- [ ] Cross-check every command in `CommandRegistry` (`D02 T01 §5`) against its bound toolbar/menu buttons so `AutomationProperties.Name` matches the command title and `ToolTipService.ToolTip` formats `"<Title> (<Shortcut>)"` whenever a keyboard shortcut exists. Done when: zero bound buttons have a tooltip that disagrees with `CommandRegistry`.
- [ ] Assert that every icon-only button (`Button` / `ToggleButton` whose content is a `FontIcon` or `PathIcon` without visible text label) has a non-empty `AutomationProperties.Name` and non-empty tooltip. Done when: `A11y.Inventory` reports `0` nameless icon-only buttons across all surfaces. Cheaper substitute that fails the checkpoint: naming controls ad-hoc with no automated tree scan.
- [ ] Assert that every interactive toolbar, panel, and dialog button has hit-target bounds `>= 32 x 32 epx`, and every button in `SelectionFloatingToolbar` (`D03 T01 §4`) has hit-target bounds `>= 36 x 36 epx`. Done when: `A11y.Inventory` reports `0` undersized hit targets.
- [ ] Verify that every `TextBox`, `ComboBox`, `ToggleSwitch`, and `RadioButtons` group in Settings (`D02 T04`), Convert (`D04 T01`), and Organize (`D05 T02`) exposes an accessible label via `Header`, `AutomationProperties.Name`, or `AutomationProperties.LabeledBy`. Done when: zero form inputs lack an accessible name.
- [ ] Prove the test can fail by injecting a synthetic nameless icon `Button` into a test container, asserting `A11yInventory::validate` returns a violation, and verifying the live app tree returns `0` violations. Done when: `A11y.InventoryNegativeProof` passes.
- [ ] Fix any discovered name, tooltip, or `MinWidth`/`MinHeight` gaps directly in the owning XAML templates. Done when: `recto-tests.exe --gtest_filter=A11y.Inventory*` passes 100%.
- [ ] Commit: `test(a11y): add automated A11yInventory control accessibility and hit-target suite`

**Test checkpoint:** `recto-tests.exe --gtest_filter=A11y.Inventory*` passes, quoting total inspected control counts and `0` violations, plus proving failure when a nameless button is planted.

## 4. Logical `Tab` / `Shift+Tab` focus order and `--accent` focus ring across all 3 presets

Verify and enforce SPEC section 11's keyboard focus order (`Title bar -> Main toolbar -> Left/Right side panel -> Document page canvas -> Floating page toolbar / Status bar`) and high-visibility `--accent` (`RectoAccentBrush`) focus visuals across all three layout presets (`Focus`, `Classic`, `Reading room`) and all mode pages (`Home`, `Document`, `Convert`, `Organize`, `Settings`, `About`), plus `Esc` layer-dismissal order.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 4 (`RectoAccentBrush`), 8 (Focus, Classic, and Reading room presets), 10 (keyboard shortcuts and `Esc` dismissal), and 11 (logical tab order and visible focus indicator in `--accent`).
**Job:** A keyboard-only user can traverse the entire interface in a predictable clockwise/top-down order without focus traps, dead stops, or invisible focus states, and press `Esc` to peel back popovers, find cards, and mode bars one layer at a time. Consumer: keyboard users and assistive technology focus trackers.
**Treatment:** Explicit `TabIndex` / `XYFocusKeyboardNavigation` / `TabNavigation` container scoping and `FocusVisualPrimaryBrush` bound to `RectoAccentBrush`, verified by driven programmatic `FocusManager::TryMoveFocus` tab walks in all three presets. Cheaper substitute that fails the checkpoint: relying on default XAML tree insertion order where focus jumps erratically between the status bar, title bar, and hidden panels.
**Chrome:** consume `RectoAccentBrush` (`2 epx` outer focus ring) and `RectoSurfaceBrush` (`1 epx` inner ring) from `design/tokens/RectoColors.xaml`.

**Build order:**

1. Configure `TabNavigation` scopes and `FocusVisualPrimaryBrush` / `FocusVisualSecondaryBrush` across `AppShell`, `TitleBarStrip`, `MainToolbar`, `SidePanelHost`, `DocumentView`, `FloatingPageToolbar`, and `ClassicStatusBar`. Done when: every focusable control displays a `2 epx` `RectoAccentBrush` focus ring when focused via keyboard.
2. Implement the driven `Tab` and `Shift+Tab` walk tests (`A11y.TabOrder`) across `Focus`, `Classic`, and `Reading room` presets and all mode pages, plus the multi-layer `Esc` dismissal stack test (`SelectionFloatingToolbar -> CommandBox / FindCard -> Markup/Form bar -> fullscreen/preset`). Done when: `A11y.TabOrder` and `A11y.EscapeStack` pass in all three presets.

- [ ] Bind `SystemControlFocusVisualPrimaryBrush` / `FocusVisualPrimaryBrush` to `RectoAccentBrush` (`2 epx` thickness) and `FocusVisualSecondaryBrush` to `RectoSurfaceBrush` (`1 epx` thickness) in `src/app/App.xaml` and verify high-contrast override behavior. Done when: keyboard-focused controls render the `--accent` focus ring in both Light and Dark themes.
- [ ] Enforce the SPEC 11 landmark `Tab` order in `Focus` preset: `TitleBarStrip (Back/Home, Tabs, New Tab, Mode Switcher, Command Box, Overflow) -> SidePanelHost (when open) -> DocumentView canvas -> FloatingPageToolbar`. Done when: `A11y.TabOrderFocusPreset` walks every stop in exact landmark order and cycles cleanly without landing on collapsed elements.
- [ ] Enforce the landmark `Tab` order in `Classic` preset: `TitleBarStrip -> ClassicMenuBar -> MainToolbar -> Left SidePanelHost -> DocumentView canvas -> ClassicStatusBar`. Done when: `A11y.TabOrderClassicPreset` passes forward (`Tab`) and backward (`Shift+Tab`).
- [ ] Enforce the landmark `Tab` order in `Reading room` preset: `TitleBarStrip -> DocumentView canvas -> Right Inspector (Pages/Bookmarks/Search/Info tabs and content) -> Bottom Filmstrip`. Done when: `A11y.TabOrderReadingRoomPreset` passes forward and backward. Cheaper substitute that fails the checkpoint: leaving collapsed Classic toolbar or hidden panel controls in the active tab ring.
- [ ] Verify `Tab` order across `Home`, `Convert` (`Step 1 -> Step 2 -> Step 3 -> Action button`), `Organize` (`Header -> Mode tabs -> Page grid -> Action bar`), `Settings`, and `About`. Done when: `A11y.TabOrderModePages` visits every interactive control in logical top-to-bottom, left-to-right order.
- [ ] Verify deterministic `Esc` dismissal priority order: (1) open context menu or popover, (2) `CommandBox` modal (`Ctrl+K`), (3) active text selection & `SelectionFloatingToolbar`, (4) `FindCard` (`Ctrl+F`/`Ctrl+H`), (5) contextual mode bar (`Markup`/`Form`/`Redaction`/`Table`). Done when: `A11y.EscapeStack` opens all layers and verifies five consecutive `Esc` presses close them in exact top-down order.
- [ ] Commit: `a11y: keyboard order, names and focus verified across surfaces`

**Test checkpoint:** `recto-tests.exe --gtest_filter=A11y.Inventory*:A11y.TabOrder*:A11y.EscapeStack` passes across `Focus`, `Classic`, and `Reading room` presets.

## 5. Polite coalescing `AnnouncementService` for page changes, find counts, and selection

Implement `AnnouncementService` (`src/app/a11y/AnnouncementService.h/.cpp`) using WinUI 3 `AutomationPeer::RaiseNotificationEvent` (`AutomationNotificationKind`, `AutomationNotificationProcessing::MostRecent` / `CurrentThenMostRecent`): deliver polite, non-focus-stealing, rate-limited announcements for page changes while scrolling/jumping, find result counts and active hit positions, text selection extents, and document open/close events.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.3 (selection and search results announced to screen readers) and 11 (polite live announcements); no visual board governs the announcements themselves.
**Job:** A screen-reader user hears what changed (settled page number, `"14 results on 4 pages"`, `"Result 2 of 14, page 3"`, `"Selected 12 words"`, `"Opened Annual-Report.pdf, 42 pages"`) without hunting for it or being drowned by rapid scroll ticks. Consumer: Windows Narrator / NVDA / JAWS via UIA `Notification` events, and `A11y.Announcements` automated listener tests.
**Treatment:** One shell-level `AnnouncementService` with per-category debounce/coalescing windows (`250 ms` for page scroll settling, `200 ms` for search query completion, `150 ms` for selection extent) raising polite UIA notifications with `AutomationNotificationProcessing::MostRecent`. Cheaper substitute that fails the checkpoint: assertive announcements on every scroll tick that drown the screen reader, or visual status text updates that never raise a UIA notification event.
**Chrome:** consume view, selection, and find state from `D02 T02`, `D03 T01 §3`, and `D03 T01 §6`; register the sink once on `AppShell`. Do not invent per-surface announcement hacks.

**Build order:**

1. Implement `AnnouncementService` (`src/app/a11y/AnnouncementService.cpp`) with category-keyed coalescing timers (`PageChange`, `FindResults`, `SelectionExtent`, `DocumentLifecycle`, `JobStatus`), raising `AutomationPeer::RaiseNotificationEvent` on the shell live peer and notifying registered test listeners. Done when: rapid emissions within the coalescing window collapse into a single `MostRecent` announcement for that category.
2. Wire `DocumentView` page changes, `SearchViewModel` result counts and `F3`/`Shift+F3` hit navigation, `SelectionModel` word/character selection counts, and tab open/close events into `AnnouncementService`. Done when: `A11y.Announcements` records the exact expected announcement sequence for a scripted open, scroll, select, find, and close session.

- [ ] Implement `AnnouncementService` in `src/app/a11y/AnnouncementService.h/.cpp` with `announce_polite(AnnouncementCategory, std::wstring message, std::wstring activity_id)` and configurable coalescing windows (`250 ms` default). Done when: `AnnouncementService` raises `AutomationPeer::RaiseNotificationEvent` with `AutomationNotificationProcessing::MostRecent` and records emissions for test inspection.
- [ ] Wire `DocumentView` current-page changes (`D02 T02 §4`) to `AnnouncementCategory::PageChange` with a `250 ms` settling debounce so a fast 2-second scroll across 50 pages announces only `"Page 50 of 120"` once scrolling settles. Done when: `A11y.FastScrollCoalescing` confirms a 50-page rapid scroll emits exactly 1 page announcement. Cheaper substitute that fails the checkpoint: raising a notification on every page boundary crossed during fast scroll.
- [ ] Wire `SearchViewModel` (`D03 T01 §6`) to `AnnouncementCategory::FindResults` announcing `"N results on M pages"` (or `"No results for '<query>'"`) when a background search settles, and `"Result k of N, page P: <snippet>"` on `F3` / `Shift+F3`. Done when: `A11y.FindAnnouncements` verifies result-count and hit-step announcements.
- [ ] Wire `SelectionModel` (`D03 T01 §3`) to `AnnouncementCategory::SelectionExtent` announcing `"Selected <text>"` (for short selections `<= 40` chars) or `"Selected N words (M characters)"` (for longer selections), and `"Selection cleared"` on `Esc`. Done when: `A11y.SelectionAnnouncements` verifies word/char selection and clear announcements.
- [ ] Wire `TabModel` (`D02 T01 §3`) document open, tab switch, and tab close events to `AnnouncementCategory::DocumentLifecycle` announcing `"Opened <Title>, N pages"` and `"Closed <Title>"`. Done when: opening and closing a document emits the expected lifecycle notifications.
- [ ] Honour `ReducedMotion` / quiet coalescing timing without ever stealing keyboard focus from the active control (`TextBox`, `DocumentView`, or toolbar button). Done when: raising any announcement leaves `FocusManager::GetFocusedElement` unchanged.
- [ ] Add `A11y.Announcements`, `A11y.FastScrollCoalescing`, `A11y.FindAnnouncements`, and `A11y.SelectionAnnouncements` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=A11y.*Announcements*` passes 100%.
- [ ] Commit: `feat(a11y): implement polite coalescing AnnouncementService for page, find, and selection`

**Test checkpoint:** `recto-tests.exe --gtest_filter=A11y.Announcements:A11y.FastScrollCoalescing:A11y.FindAnnouncements:A11y.SelectionAnnouncements` passes and quotes the listener logs.

## 6. Conversion job completion announcement sink (`D04 T01 §6`) and UIA verification pass

Add the conversion and background-job completion announcement API (`AnnouncementService::announce_job_completion` and `announce_job_failure`) for `D04 T01 §6` to plug into, and run the complete SPEC section 11 end-to-end UI Automation and Windows Narrator verification pass across M2 and M3 surfaces.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.5 (conversion completion notifications) and 11 (complete accessibility contract across UIA text, keyboard focus, target bounds, high contrast, reduced motion, and polite announcements).
**Job:** Background conversion and OCR jobs announce their completion or failure politely to screen readers, and every clause of SPEC section 11 is proven end-to-end by automated UIA tests and a recorded Narrator verification checklist. Consumer: `D04 T01 §6` job notifications and the M3 milestone sign-off.
**Treatment:** Typed `announce_job_completion` / `announce_job_failure` sink on `AnnouncementService` plus an end-to-end UIA client test (`A11y.EndToEndSection11`) verifying every SPEC section 11 clause in one continuous session. Cheaper substitute that fails the checkpoint: visual-only conversion toasts with no UIA notification sink.
**Chrome:** consume `AnnouncementService` (`§5`), `DocumentViewAutomationPeer` (`§1`-`§2`), and `A11yInventory` (`§3`).

**Build order:**

1. Add `announce_job_started`, `announce_job_completion(job_title, output_filename)`, and `announce_job_failure(job_title, reason)` to `AnnouncementService` (`src/app/a11y/AnnouncementService.h/.cpp`) using `AutomationNotificationKind::ActionCompleted` and `ActionAborted` (`-> XREF: D04 T01 §6`). Done when: `A11y.JobAnnouncementSink` verifies polite `ActionCompleted` and `ActionAborted` UIA notifications.
2. Implement `A11y.EndToEndSection11` in `tests/recto-tests/a11y_e2e_tests.cpp` driving an out-of-process / COM `IUIAutomation` client across document open, UIA text reading (digital + OCR page), word/line expansion, two-way selection, `Ctrl+F` search announcement, preset tab-order walk, high-contrast theme switch, and job completion announcement. Done when: `A11y.EndToEndSection11` passes 100%.

- [ ] Implement `announce_job_started`, `announce_job_completion`, and `announce_job_failure` on `AnnouncementService` raising `AutomationNotificationKind::ActionCompleted` / `ActionAborted` with `AutomationNotificationProcessing::CurrentThenMostRecent` so stacked batch completions are never dropped. Done when: `A11y.JobAnnouncementSink` verifies two rapid job completions both announce in order.
- [ ] Document and export the `IAnnouncementSink` interface in `src/services/announcement_sink.h` so `D04 T01 §6` invokes it directly from `ConversionQueueViewModel` without coupling `recto-services` to WinUI XAML headers. Done when: `src/services/announcement_sink.h` compiles in `recto-services` with zero XAML dependencies (`-> XREF: D04 T01 §6`). Cheaper substitute that fails the checkpoint: leaving conversion notifications visual-only with no service-level announcement sink.
- [ ] Verify that every status badge and diff indicator in M2 and M3 (`Search` hits, `Compare PDFs` insertions/deletions in `D03 T01 §10`, required form fields in `D03 T03 §8`) pairs colour with a distinct icon glyph, border style, or text label (`SPEC 11`: colour is never the only signal). Done when: `A11y.ColourNotOnlySignal` inspects all status badges and diff rows.
- [ ] Implement `A11y.EndToEndSection11` using Windows COM `CUIAutomation` (`IUIAutomation`, `IUIAutomationTextPattern`, `IUIAutomationNotificationEventHandler`) against the live `DocumentView` to verify UIA reading, selection sync, and notification events in one automated pass. Done when: `A11y.EndToEndSection11` passes on both digital and OCR-recognized corpus PDFs.
- [ ] Run a live Windows Narrator smoke pass on the display-session host (reading a two-column PDF, selecting a sentence, running `Ctrl+F`, and tabbing through the toolbar) and record the clause-by-clause SPEC section 11 verification table. Done when: every clause of SPEC section 11 is verified and recorded.
- [ ] Commit: `a11y: polite announcements for page, find and selection state`

**Test checkpoint:** `recto-tests.exe --gtest_filter=A11y.*` passes 100%, quoting listener logs for the scripted session, fast-scroll coalescing, job completion sink, and `A11y.EndToEndSection11`.

## Verification

- SPEC section 11 read line by line against the findings: every clause proven by `A11y.*` tests (`TextRangeUnits`, `TextProvider`, `SelectionAndFind`, `Inventory`, `TabOrder`, `EscapeStack`, `Announcements`, `FastScrollCoalescing`, `JobAnnouncementSink`, `ColourNotOnlySignal`, `EndToEndSection11`).
- M3's UIA deliverable demonstrable with Windows Narrator on the reference machine.
- `recto-tests` full suite green; `python scripts/todo-graph.py validate` passes.
