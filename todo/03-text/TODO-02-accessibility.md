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
- -> XREF: D03 T01 §1 -- `DocumentViewAutomationPeer` and `UiaTextRange` (`§1`, `§2`) consume `PageText`'s character quads, `Span::heading_level`, and immutable `PageText::struct_nodes` (`/StructTreeRoot` `/MCID` structure snapshot) produced by `D03 T01 §1` and cached by `D03 T01 §2` (`DocumentActor::get_page_text`).
- -> XREF: D03 T03 §10 -- Interactive form fields and annotation comments expose UIA peers alongside the page text provider.
- -> XREF: D04 T01 §6 -- Conversion job completion notifications invoke the polite announcement sink built in §6 here.
- -> XREF: D07 T02 §6 -- the M6 Store & Release Compliance verification (`D07 T02 §6`) gates on the UIA verification pass in `§6` here.
- -> XREF: D02 T04 §9 -- Read Out Loud accelerators join the audited keyboard map.
- -> XREF: D03 T01 §1 -- Read Out Loud reads `PageText` in reading order.
- -> XREF: D03 T03 §15 -- article threads for reading order.
- -> XREF: D02 T04 §9 -- Ctrl+4 shortcut table.
- -> XREF: D06 T04 §8 -- the `D-ReadAloud.png` board and `design/SPEC.md` section 9.13 that §9, §12 here build against.
- -> XREF: D02 T01 §20 -- the `ClassicStatusBar` that §4's Classic tab order reaches after the page.

## Outcome

- A screen reader can read a page top to bottom, move by character, word, line, and page, traverse tagged PDF `/StructTreeRoot` structure (`HeadingLevel` for `/H1`-`/H6`, `/Table`/`/TR`/`/TH`/`/TD` cells, `/L`/`/LI` lists, and `/Figure` `/Alt` descriptions via `PageText::struct_nodes`, falling back to `PageText` geometric reading order when untagged), select text, and follow find results against the same `PageText` model sighted selection uses (`§1`, `§2`).
- Every icon-only control across the shell, panels, and toolbars is verified by an automated inventory to carry `AutomationProperties.Name`, a shortcut tooltip, and `>= 32 epx` (`>= 36 epx` in floating bars) hit targets (`§3`).
- Keyboard-only operation reaches every control in title-bar, toolbar, panel, page order with a visible `RectoAccentBrush` focus indicator across `Focus`, `Classic` and `Reading room` presets (`§4`).
- Find counts, current page changes, selection extents, document open/close, and conversion completion (when `D04 T01 §6` emits its events) announce politely without stealing focus (`§5`, `§6`).
- Read Out Loud speaks the current page, the rest of the document or the selection in `PageText` reading order (tagged structure when present, OCR text on scanned pages) through installed Windows voices with `Windows.Media.SpeechSynthesis`, with pause, stop, voice, rate and volume, a highlighted word that the page follows, `Ctrl+K` commands and shortcuts, and no work on the UI thread (`§7`, `§8`, `§9`).

**Adjacency:** all=not-applicable (an access layer over other files' records and surfaces; it owns no data, and its every element is the accessible twin of a surface owned elsewhere)

**Adjacency rationale:** Accessibility here is a property of surfaces other sections build, plus one genuinely new surface (the UIA text provider), which is the accessible form of `D03 T01`'s text model. Declaring product adjacencies would double-own them; the file's contract is instead the section 11 audit in `§3` and `§4`, which checks each owning surface in place.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `DocumentViewAutomationPeer` implementing `ITextProvider` and `ITextRangeProvider` | D03 T01 §2, D02 T02 §5 | [ ] |
| 2 | §2 | Two-way UIA text selection synchronization, `FindText`, and child peer hooks | §1, D03 T01 §3, D03 T01 §5 | [ ] |
| 3 | §3 | Automated control accessibility inventory (`AutomationProperties.Name`, `32/36 epx`) | D02 T04 §10 | [ ] |
| 4 | §4 | Logical `Tab` / `Shift+Tab` focus order and accent focus ring across all 3 presets | §3, D02 T01 §20 | [ ] |
| 5 | §5 | Polite coalescing `AnnouncementService` for page changes, find counts, and selection | §2, D03 T01 §6 | [ ] |
| 6 | §6 | Conversion job completion announcement sink (`D04 T01 §6`) and UIA verification pass | §4, §5 | [ ] |
| 7 | §7 | Read Out Loud reading plan: current page, to end of document and selection | D03 T01 §2, D03 T01 §3, D03 T01 §8, D03 T03 §6, D03 T03 §8 | [ ] |
| 8 | §8 | `SpeechService` over `Windows.Media.SpeechSynthesis`: voices, rate, volume, pause, stop | §7, D02 T04 §2 | [ ] |
| 9 | §9 | Read Out Loud bar, word highlight, shortcuts, `Ctrl+K` commands and settings | §5, §7, §8, D03 T01 §4, D02 T03 §11, D02 T04 §5, D02 T04 §8, D06 T04 §8 | [ ] |
| 10 | §10 | Reflow model: logical blocks from the structure tree, article threads or heuristics | D03 T01 §2, D03 T03 §15 | [ ] |
| 11 | §11 | Reflow layout: DirectWrite text layout at a chosen width, size and spacing | §10, D03 T01 §11 | [ ] |
| 12 | §12 | Reflow view and Text Viewer: `Ctrl+4`, size controls, settings and accessibility | §5, §9, §11, D03 T01 §6, D02 T03 §11, D02 T04 §5, D02 T04 §9, D06 T04 §8 | [ ] |

## 1. `DocumentViewAutomationPeer` implementing `ITextProvider` and `ITextRangeProvider`

Implement `DocumentViewAutomationPeer` (`src/app/a11y/DocumentViewAutomationPeer.h/.cpp`) and the backing `TextRangeModel` (`src/services/uia_text_range.h/.cpp`) implementing the UI Automation `ITextProvider` and `ITextRangeProvider` patterns over `D03 T01`'s shared `PageText` model (including OCR-populated `PageText` on scanned and vector-outlined pages and `PageText::struct_nodes` / `Span::heading_level` extracted from PDF `/StructTreeRoot` in `D03 T01 §1` and delivered via `DocumentActor::get_page_text` in `D03 T01 §2` per `SPEC 11`): document range, unit movement (`Character`, `Word`, `Line`, `Paragraph`/`Block`, `Page`, `Document`), `StyleId_Heading1`..`StyleId_Heading6` attribute exposure from `Span::heading_level`, `GetBoundingRectangles`, `RangeFromPoint`, and `ScrollIntoView`.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 11 (page text exposed through a UIA text provider, with existing tagged-PDF `/StructTreeRoot` headings, tables, reading order, and `/Alt` descriptions surfaced to assistive technology); no visual board governs the peer itself.
**Job:** A blind user can read any digital, tagged, or OCR-recognized PDF page top to bottom and navigate by character, word, line, block, heading, or page with Narrator or NVDA. Consumer: Windows Narrator, NVDA, JAWS, Accessibility Insights, and `A11y.TextProvider` automated UIA client tests.
**Treatment:** `ITextProvider` and `ITextRangeProvider` over the shared `std::shared_ptr<const PageText>` snapshot (`PageText::blocks`, `PageText::spans`, and `PageText::struct_nodes` produced by `D03 T01 §1`-`§2`, never touching the live `recto::pdf::Document` graph from the UI thread) with `(page_index, char_index)` range endpoints and screen-space bounding rectangles computed via `D02 T02 §5`. Cheaper substitute that fails the checkpoint: exposing only the control name and a static plain-text dump, ignoring `PageText::struct_nodes` reading order and heading styles on tagged PDFs, or returning `E_NOTIMPL` for word and line unit navigation.
**Chrome:** consume `PageText` (`D03 T01 §1`-`§2`) and `DocumentView` geometry (`D02 T02 §5`). Do not invent a parallel text store for accessibility.

**Build order:**

1. Implement `UiaTextRange` (`src/services/uia_text_range.cpp`) over `(start_page, start_char)` and `(end_page, end_char)` endpoints backed by `DocumentActor::get_page_text`, supporting `Clone`, `Compare`, `CompareEndpoints`, `ExpandToEnclosingUnit`, `Move`, `MoveEndpointByUnit`, `MoveEndpointByRange`, and `GetText` across `TextUnit::Character`, `Word`, `Line`, `Paragraph`, `Page`, and `Document`, honoring `PageText::struct_nodes` reading order and `Span::heading_level` (`StyleId_Heading1`..`StyleId_Heading6`) when the PDF is tagged. Done when: `A11y.TextRangeUnits` unit tests walk both an untagged two-column corpus page and a tagged `/StructTreeRoot` PDF by word, line, paragraph, and heading in reading order.
2. Implement `DocumentViewAutomationPeer` (`src/app/a11y/DocumentViewAutomationPeer.cpp`) overriding `GetPatternCore(PatternInterface::Text)` and wiring `DocumentRange`, `GetVisibleRanges`, `RangeFromPoint`, `GetBoundingRectangles`, and `ScrollIntoView`. Done when: an in-process UIA client (`IUIAutomationTextPattern`) queries `DocumentView`, reads page text, and scrolls a target line into view.

- [ ] Implement `UiaTextRange` in `src/services/uia_text_range.h/.cpp` representing an ordered `(page_index, char_index)` interval over `PageText` snapshots fetched on demand from `DocumentActor`. Done when: `UiaTextRange` handles single-page and multi-page spans without copying full-document strings into memory.
- [ ] Implement `ExpandToEnclosingUnit`, `Move`, and `MoveEndpointByUnit` for `TextUnit::Character`, `Word`, `Line`, `Paragraph` (`PageText::Block` or `PageText::struct_nodes` block element), `Page`, and `Document`. Done when: a screen-reader walk of a two-column corpus page reads column 1 top-to-bottom before column 2, and a tagged PDF follows its `PageText::struct_nodes` sequence. Cheaper substitute that fails the checkpoint: pattern methods returning not-implemented for word and line units.
- [ ] Implement `GetText(int max_length)` returning UTF-8/UTF-16 text for the range (including OCR-recognized text layers from `D03 T01 §8`) truncated cleanly at `max_length` when `>= 0`. Done when: `GetText(-1)` on a page range equals `PageText::to_utf8_string()`.
- [ ] Implement `GetAttributeValue` for `FontNameAttribute`, `FontSizeAttribute`, `FontWeightAttribute`, `IsItalicAttribute`, `StyleIdAttribute` (`StyleId_Heading1`..`StyleId_Heading6` mapped from `PageText::Span::heading_level` `1..6`), and `IsReadOnlyAttribute` (`true`) sourced from `PageText::Span`. Done when: UIA attribute queries return the exact font name, point size, bold/italic flags, and heading `StyleId` of the underlying span.
- [ ] Implement `GetBoundingRectangles` transforming the range's coalesced line quads from PDF page points to screen pixel coordinates through `D02 T02 §5`'s `page_to_viewport` transform and window screen offset. Done when: Narrator's blue highlight rectangle aligns over the spoken word on screen.
- [ ] Implement `RangeFromPoint(Point screen_pt)` and `GetVisibleRanges()` returning the visible page text ranges currently intersecting the viewport. Done when: hit-testing a screen point through UIA returns the character range under the cursor.
- [ ] Implement `ScrollIntoView(bool align_to_top)` scrolling `DocumentView` (`D02 T02 §4`) so the target text range is visible inside the viewport. Done when: `A11y.TextProvider` calls `ScrollIntoView` on a page-5 range and verifies the viewport jumps to page 5.
- [ ] Commit: `feat(a11y): implement DocumentViewAutomationPeer ITextProvider and ITextRangeProvider`

**Test checkpoint:** `recto-tests.exe --gtest_filter=A11y.TextRangeUnits:A11y.TextProvider` passes, verifying document text, word and line expansion, tagged `/StructTreeRoot` heading attributes, bounding rectangles, and `ScrollIntoView`.

## 2. Two-way UIA text selection synchronization, `FindText`, and child peer hooks

Connect `DocumentViewAutomationPeer` (`§1`) bidirectionally to `SelectionModel` (`D03 T01 §3`), delegate `ITextRangeProvider::FindText` to `D03 T01 §5`'s `SearchEngine`, and expose child `AutomationPeer` hooks (`RangeFromChild` / `GetChildrenCore` / `GetEnclosingElement`) over `PageText::struct_nodes` (`D03 T01 §1`-`§2`) so interactive annotations and form fields (`D03 T03 §10`) as well as tagged PDF `/StructTreeRoot` structure elements (`/H1`..`/H6` with `AutomationProperties::HeadingLevel`, `/Table` / `/TR` / `/TH` / `/TD` with `ITableProvider`/`IGridItemProvider`, and `/Figure` with `/Alt` accessible names per `SPEC 11`) appear in the UIA tree at their page coordinates without accessing `recto::pdf::Document` on the UI thread.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.3 and 11 (selection synchronized between visual canvas and screen readers, and tagged-PDF `/StructTreeRoot` structure and `/Alt` figure descriptions exposed through UIA), with `D-Search.png`.
**Job:** Selecting text with the mouse updates the screen reader's selection immediately, selecting text via a screen reader (`ITextRangeProvider::Select`) paints the exact `RectoTextSelectionBrush` quads on canvas and feeds `Ctrl+C`, and screen readers can jump across tagged headings, tables, figures (`/Alt`), and form controls. Consumer: `SelectionModel` (`D03 T01 §3`), Windows clipboard (`D03 T01 §4`), tagged PDF structure peers, and `D03 T03 §10` form/annotation peers.
**Treatment:** Bidirectional binding between `SelectionModel` and `DocumentViewAutomationPeer::GetSelection` / `ITextRangeProvider::Select` / `RemoveFromSelection`, raising `AutomationEvents::TextPatternOnTextSelectionChanged`, plus child `AutomationPeer` projection over `PageText::struct_nodes` (`D03 T01 §1`) and interactive widgets. Cheaper substitute that fails the checkpoint: one-way selection where screen-reader selection does not update the visual canvas or clipboard, or dropping `PageText::struct_nodes` `/Alt` and table/heading structure peers.
**Chrome:** consume `SelectionModel` (`D03 T01 §3`), `PageText::struct_nodes` (`D03 T01 §1`-`§2`), and `SearchEngine` (`D03 T01 §5`). Do not maintain separate UIA and visual selection states.

**Build order:**

1. Wire `ITextProvider::GetSelection()`, `ITextRangeProvider::Select()`, and `ITextRangeProvider::RemoveFromSelection()` directly to `SelectionModel`, and raise `AutomationEvents::TextPatternOnTextSelectionChanged` whenever pointer, keyboard, or UIA selection changes. Done when: selecting by mouse then reading the UIA selection (and the reverse) agree character-for-character.
2. Implement `ITextRangeProvider::FindText(text, backward, ignore_case)` delegating to `D03 T01 §5`'s `SearchEngine`, and add `GetChildrenCore` / `RangeFromChild` / `GetEnclosingElement` child peer projection over `PageText::struct_nodes` (`/H1`..`/H6`, `/Table`, `/Figure` `/Alt`) and `D03 T03 §10` widgets. Done when: `A11y.SelectionAndFind` and `A11y.TaggedStructTreePeers` pass end-to-end.

- [ ] Implement `ITextProvider::GetSelection()` returning a `UiaTextRange` matching `SelectionModel`'s current `(anchor, active)` endpoints (or an empty array when nothing is selected). Done when: pointer-selecting a word on canvas and querying `GetSelection()` via UIA returns that exact word range.
- [ ] Implement `ITextRangeProvider::Select()` and `RemoveFromSelection()` updating `SelectionModel` so `RectoTextSelectionBrush` quads paint on canvas and `Ctrl+C` (`D03 T01 §4`) copies the peer-selected text. Done when: calling `Select()` from an automation client updates the visual highlight and clipboard fixture. Cheaper substitute that fails the checkpoint: `Select()` returning `S_OK` without updating `SelectionModel`.
- [ ] Raise `AutomationPeer::RaiseAutomationEvent(AutomationEvents::TextPatternOnTextSelectionChanged)` with a 50 ms drag-coalescing window whenever `SelectionModel` changes. Done when: UIA event listeners receive `TextSelectionChanged` without event flooding during mouse drag.
- [ ] Implement `ITextRangeProvider::FindText(BSTR text, BOOL backward, BOOL ignore_case)` over the enclosing range using `D03 T01 §5`'s normalized search matcher (including ligature and soft-hyphen folding). Done when: a UIA client calling `FindText` locates matches across ligatures and line wraps.
- [ ] Implement `GetChildrenCore()`, `RangeFromChild(IRawElementProviderSimple*)`, and `GetEnclosingElement()` on `DocumentViewAutomationPeer` reading `PageText::struct_nodes` (`D03 T01 §1`) so tagged `/StructTreeRoot` elements (`/H1`..`/H6` with `AutomationHeadingLevel`, `/Table`/`/TR`/`/TH`/`/TD` grid peers, and `/Figure` image peers with `AutomationProperties.Name` set to `StructNode::alt_text` and bounds from `StructNode::bbox_pt`) and interactive form widgets/annotations registered by `D03 T03 §10` expose child automation peers anchored to `(char_start, char_end)` on the page. Done when: `A11y.TaggedStructTreePeers` verifies heading levels, table cell coordinates, `/Figure` `/Alt` names, and `RangeFromChild` mapping on a tagged corpus PDF.
- [ ] Verify `HighContrast` (`ContrastThemes::HighContrast` from `D02 T04 §7`) selection and search highlight legibility over `DocumentView` when UIA selection is active. Done when: `A11y.SelectionAndFind` verifies high-contrast token binding.
- [ ] Commit: `a11y: UI Automation text provider over the shared PageText model`

**Test checkpoint:** `recto-tests.exe --gtest_filter=A11y.TextRangeUnits:A11y.TextProvider:A11y.SelectionAndFind:A11y.TaggedStructTreePeers` passes, quoting document text, word and line expansion, tagged `/StructTreeRoot` `/Alt` and heading/table peers, `FindText`, and two-way selection sync.

## 3. Automated control accessibility inventory (`AutomationProperties.Name`, `32/36 epx`)

Build the automated control accessibility inventory in `tests/recto-tests/a11y_inventory_tests.cpp`: enumerate every interactive control across the shell, title bar, main toolbar, floating page toolbar, floating selection toolbar, side panels, Convert, Organize, Settings, About, and dialogs from `CommandRegistry` (`D02 T01 §5`) and the live XAML visual tree, asserting that every icon-only button carries a non-empty `AutomationProperties.Name`, a tooltip (with keyboard shortcut where bound), and `32/36 epx` hit-target bounds (`>= 32 x 32 epx`, or `>= 36 x 36 epx` in the floating page toolbar, every contextual mode bar and the floating selection toolbar).

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 11 (hit targets at least `32 x 32 epx`, `36` in the floating toolbar and mode bars; every icon-only button carries an automation name and shortcut tooltip), with the floating selection toolbar held to `36 epx` as `D03 T01 §4` builds it.
**Job:** Every button and control in Recto has a spoken accessible name, a hover tooltip with its shortcut, and a comfortable hit target, enforced by an automated test that fails whenever an unnamed or undersized button is added. Consumer: screen readers, voice control (`Windows Voice Access`), keyboard/mouse users, and CI regression suite.
**Treatment:** Automated XAML tree + `CommandRegistry` walker that instantiates each surface and checks `AutomationProperties::GetName`, `ToolTipService::GetToolTip`, and `MinWidth`/`MinHeight`/`ActualWidth`/`ActualHeight`. Cheaper substitute that fails the checkpoint: spot-checking a few famous toolbar buttons by hand with no automated enumeration.
**Chrome:** consume `CommandRegistry` (`D02 T01 §5`) as the single source of truth for command labels and shortcut strings so tooltips and automation names never drift.

**Build order:**

1. Write the XAML visual-tree and `CommandRegistry` accessibility walker (`src/app/a11y/A11yInventory.h/.cpp`) that visits every interactive `Control` (`Button`, `ToggleButton`, `AppBarButton`, `RadioButton`, `ComboBox`, `TextBox`, `Slider`, `NavigationViewItem`) across all views and presets. Done when: the walker produces a complete structured table of control IDs, automation names, tooltips, and pixel bounds.
2. Fix any missing `AutomationProperties.Name`, tooltip, or `MinWidth`/`MinHeight` gaps at their owning XAML surfaces and add the negative-proof test (planting an unnamed button causes `A11y.Inventory` to fail, then removing it). Done when: `A11y.Inventory` passes with zero unnamed icon-only controls and zero undersized targets.

- [ ] Implement `A11yInventory::scan_element_tree(DependencyObject root)` in `src/app/a11y/A11yInventory.cpp` traversing all interactive controls and recording `class_name`, `automation_id`, `automation_name`, `tooltip_text`, `width_epx`, and `height_epx`. Done when: `scan_element_tree` visits every control in the shell, toolbars, side panels, floating bars, and mode pages.
- [ ] Cross-check every command in `CommandRegistry` (`D02 T01 §5`) against its bound toolbar/menu buttons so `AutomationProperties.Name` matches the command title and `ToolTipService.ToolTip` formats `"<Title> (<Shortcut>)"` whenever a keyboard shortcut exists. Done when: zero bound buttons have a tooltip that disagrees with `CommandRegistry`.
- [ ] Assert that every icon-only button (`Button` / `ToggleButton` whose content is a `FontIcon` or `PathIcon` without visible text label) has a non-empty `AutomationProperties.Name` and non-empty tooltip. Done when: `A11y.Inventory` reports `0` nameless icon-only buttons across all surfaces. Cheaper substitute that fails the checkpoint: naming controls ad-hoc with no automated tree scan.
- [ ] Assert that every interactive toolbar, panel, and dialog button has hit-target bounds `>= 32 x 32 epx`, and every button in `PageToolbar` (`D02 T02 §8`), in each contextual mode bar (`MarkupAnnotationBar`, `FormBar`, `RedactionBar`, `TableImageExtractorBar`, `MeasureBar`, `EditPdfBar`, `ReadAloudBar` and any later bar registered in the mode bar slot) and in `SelectionFloatingToolbar` (`D03 T01 §4`) has hit-target bounds `>= 36 x 36 epx`. Done when: `A11y.Inventory` reports `0` undersized hit targets, and a planted `32 x 32 epx` button in a test mode bar is reported.
- [ ] Verify that every `TextBox`, `ComboBox`, `ToggleSwitch`, and `RadioButtons` group in Settings (`D02 T04`), Convert (`D04 T01`), and Organize (`D05 T02`) exposes an accessible label via `Header`, `AutomationProperties.Name`, or `AutomationProperties.LabeledBy`. Done when: zero form inputs lack an accessible name.
- [ ] Prove the test can fail by injecting a synthetic nameless icon `Button` into a test container, asserting `A11yInventory::validate` returns a violation, and verifying the live app tree returns `0` violations. Done when: `A11y.InventoryNegativeProof` passes.
- [ ] Fix any discovered name, tooltip, or `MinWidth`/`MinHeight` gaps directly in the owning XAML templates. Done when: `recto-tests.exe --gtest_filter=A11y.Inventory*` passes 100%.
- [ ] Commit: `test(a11y): add automated A11yInventory control accessibility and hit-target suite`

**Test checkpoint:** `recto-tests.exe --gtest_filter=A11y.Inventory*` passes, quoting total inspected control counts and `0` violations, plus proving failure when a nameless button is planted.

## 4. Logical `Tab` / `Shift+Tab` focus order and accent focus ring across all 3 presets

Verify and enforce SPEC section 11's keyboard focus order (`Title bar -> Menu bar (Classic) -> Toolbar -> Active mode bar -> Side panel -> Page, including form fields and annotations in reading or tab order -> Status bar (Classic)`), as the `ShellSlotManager` focus groups of `D02 T01 §4` apply it (a `BeforeCanvas` bottom-dock occupant such as the filmstrip before the page, the Classic status bar in `AfterCanvas` after it), and high-visibility `RectoAccentBrush` focus visuals across all three layout presets (`Focus`, `Classic`, `Reading room`) and all mode pages (`Home`, `Document`, `Convert`, `Organize`, `Settings`, `About`), plus `Esc` layer-dismissal order.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 4 (`RectoAccentBrush`), 8 (Focus, Classic, and Reading room presets), 10 (keyboard shortcuts and `Esc` dismissal), and 11 (logical tab order and visible focus indicator in `RectoAccentBrush`).
**Job:** A keyboard-only user can traverse the entire interface in a predictable clockwise/top-down order without focus traps, dead stops, or invisible focus states, and press `Esc` to peel back popovers, find cards, and mode bars one layer at a time. Consumer: keyboard users and assistive technology focus trackers.
**Treatment:** Explicit `TabIndex` / `XYFocusKeyboardNavigation` / `TabNavigation` container scoping and `FocusVisualPrimaryBrush` bound to `RectoAccentBrush`, verified by driven programmatic `FocusManager::TryMoveFocus` tab walks in all three presets. Cheaper substitute that fails the checkpoint: relying on default XAML tree insertion order where focus jumps erratically between the status bar, title bar, and hidden panels.
**Chrome:** consume `RectoAccentBrush` (`2 epx` outer focus ring) and `RectoSurfaceBrush` (`1 epx` inner ring) from `design/tokens/RectoColors.xaml`.

**Build order:**

1. Configure `TabNavigation` scopes and `FocusVisualPrimaryBrush` / `FocusVisualSecondaryBrush` across `AppShell`, `TitleBarStrip`, `MainToolbar`, `SidePanelHost`, `DocumentView`, `FloatingPageToolbar`, and `ClassicStatusBar`. Done when: every focusable control displays a `2 epx` `RectoAccentBrush` focus ring when focused via keyboard.
2. Implement the driven `Tab` and `Shift+Tab` walk tests (`A11y.TabOrder`) across `Focus`, `Classic`, and `Reading room` presets and all mode pages, plus the multi-layer `Esc` dismissal stack test (`SelectionFloatingToolbar -> CommandBox / FindCard -> Markup/Form bar -> fullscreen/preset`). Done when: `A11y.TabOrder` and `A11y.EscapeStack` pass in all three presets.

- [ ] Bind `SystemControlFocusVisualPrimaryBrush` / `FocusVisualPrimaryBrush` to `RectoAccentBrush` (`2 epx` thickness) and `FocusVisualSecondaryBrush` to `RectoSurfaceBrush` (`1 epx` thickness) in `src/app/App.xaml` and verify high-contrast override behavior. Done when: keyboard-focused controls render the `RectoAccentBrush` focus ring in both Light and Dark themes.
- [ ] Enforce the SPEC 11 landmark `Tab` order in `Focus` preset: `TitleBarStrip (Home tab, document tabs, New tab, Command box, Settings; SPEC 8.1) -> FloatingPageToolbar -> active contextual mode bar (when open, even though it sits visually above the toolbar) -> SidePanelHost (when open) -> DocumentView canvas (form fields and annotations in tab order)`. Done when: `A11y.TabOrderFocusPreset` walks every stop in exact landmark order, cycles cleanly without landing on collapsed elements, and fails for a walk that reaches the canvas before `FloatingPageToolbar`.
- [ ] Enforce the landmark `Tab` order in `Classic` preset: `TitleBarStrip -> ClassicMenuBar -> MainToolbar (docked command bar) -> active contextual mode bar (when open) -> navigation rail and left SidePanelHost -> DocumentView canvas -> ClassicStatusBar`; in a Custom layout that adds the bottom filmstrip, the filmstrip comes before `DocumentView` and `ClassicStatusBar` stays after it. Done when: `A11y.TabOrderClassicPreset` passes forward (`Tab`) and backward (`Shift+Tab`) for both layouts.
- [ ] Enforce the landmark `Tab` order in `Reading room` preset: `TitleBarStrip (mode switcher, Command box, Settings) -> top strip (document tabs and the docked PageToolbar) -> active contextual mode bar (when open) -> Right Inspector (Info, Outline and Search tabs and their content) -> Bottom Filmstrip -> DocumentView canvas`. Done when: `A11y.TabOrderReadingRoomPreset` passes forward and backward and fails for a walk that reaches the canvas before the inspector or the filmstrip. Cheaper substitute that fails the checkpoint: leaving collapsed Classic toolbar or hidden panel controls in the active tab ring.
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

## 7. Read Out Loud reading plan: current page, to end of document and selection

Build the engine that decides what Read Out Loud says and in which order: a `ReadAloudPlan` made from the shared `PageText` snapshots (`D03 T01 §1`-`§2`) for the current page, from the current page to the end of the document, or for the current selection (`D03 T01 §3`), in tagged reading order when the page has structure and in geometric reading order otherwise. This is the content half of Acrobat Pro's `Read Out Loud`. It must not break the thread contract (the plan is built from immutable `std::shared_ptr<const PageText>` snapshots off the UI thread and never touches the live `recto::pdf::Document`) and it keeps Recto's improvement that scanned pages read through the OCR text layer when one exists (`D03 T01 §8`) and say so plainly when none does.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Define `ReadAloudUnit` (`text`, `page_index`, `char_start`, `char_end`, `kind` among `Heading`, `Paragraph`, `ListItem`, `TableCell`, `FigureAlt`, a `synthetic` flag for words with no drawn glyphs, and `highlight_rect_pt` for units highlighted as a box) and `ReadAloudPlan` in `src/services/read_aloud_plan.h/.cpp`, and implement `build_page_plan`, `build_to_end_plan` (lazy, one page at a time) and `build_selection_plan` over `PageText`. Done when: `A11y.ReadAloudPlanOrder` reads `text/two-column-links-outline.pdf` column by column and a tagged fixture in `/StructTreeRoot` order.
2. Implement the word map from each unit to the `CharQuad` ranges of its drawn words so the viewer can highlight the word being spoken, mark inserted words (figure `/Alt` text, table header prefixes) `synthetic` with no quads, and implement the prefetch of the next page's `PageText` through `DocumentActor::get_page_text`. Done when: `A11y.ReadAloudWordMap` maps every drawn word of a fixture page to the exact quads, reports every synthetic word as quad-less, and `A11y.ReadAloudPrefetch` shows page `N+1` requested before page `N` ends.

- [ ] Split text into units at block, list item, table cell and sentence boundaries with a maximum of `1000` characters per unit (long paragraphs split at the last sentence end or space before the cap), so one synthesis request stays short. Done when: `A11y.ReadAloudPlanOrder` shows no unit over `1000` characters on a `5000`-character paragraph fixture.
- [ ] Add a `FormField` unit kind: read each AcroForm field (`D03 T03 §6` field tree) as its name (`/TU`, else `/T`), its type (`text field`, `check box`, `radio button`, `drop-down`, `button`), its value or state (`checked`, `not checked`, selected option), and `required` or `read only` when set, in the `/Tabs` order or structure order of `D03 T03 §8`, and highlight the widget rectangle as a box like a figure. Done when: `A11y.ReadAloudFormFields` reads a fixture form's text field, check box, radio group and combo box in tab order with the expected phrases and a box highlight for each, and speaks no password field value.
- [ ] Use `/Alt` text for `Figure` nodes and `/ActualText` where present, mark heading units as `Heading` (the word `Heading` is not spoken) so the speaker can pause longer, and read `TableCell` units row by row with the header cell text prefixed on the first row only. Done when: `A11y.ReadAloudTaggedStructure` reads a tagged fixture's figure alt text, heading and a `2 x 3` table in the expected order.
- [ ] Define the highlight policy for synthetic text: a `FigureAlt` unit is highlighted as one box over the figure's `StructNode::bbox_pt` for its whole duration, and header-cell prefix words are spoken without any highlight while the cell's own drawn words highlight normally. Done when: `A11y.ReadAloudSyntheticHighlight` reads a tagged figure and a table, finds a box highlight for the figure and quads only for drawn table words, and finds no unit asking for quads that do not exist.
- [ ] Skip page artifacts (running headers, footers, page numbers marked `/Artifact`) on tagged pages and repeat nothing for hidden text (`Tr 3` invisible runs are read only when they are the OCR layer of a scanned page). Done when: `A11y.ReadAloudArtifacts` omits a tagged footer and reads the OCR layer of `text/scanned.pdf` after OCR.
- [ ] Return a typed `ReadAloudNotice::NoReadableText` for a page with no text and no OCR layer, and continue to the next page in a to-end plan after recording it. Done when: `A11y.ReadAloudNoText` shows the notice for an image-only page and the plan continuing to the next page.
- [ ] Build the selection plan from `SelectionModel` quads in selection order across lines and pages, trimmed to whole words, and reject an empty selection. Done when: `A11y.ReadAloudSelection` reads a selection that spans two pages in order and returns `NoReadableText` for an empty one.
- [ ] Make plan building cancellable with `CancelToken` and bounded at `2000` units per page and `100000` units per plan. Done when: `A11y.ReadAloudBounds` ends a hostile page of `500000` one-character blocks inside the cap under ASan.
- [ ] Add `A11y.ReadAloudPlanOrder`, `ReadAloudWordMap`, `ReadAloudPrefetch`, `ReadAloudTaggedStructure`, `ReadAloudArtifacts`, `ReadAloudNoText`, `ReadAloudSelection` and `ReadAloudBounds` in `tests/recto-tests/a11y_read_aloud_plan_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=A11y.ReadAloud*` passes under ASan.
- [ ] Commit: `feat(a11y): add the Read Out Loud reading plan`

**Test checkpoint:** `recto-tests.exe --gtest_filter=A11y.ReadAloud*` passes under ASan, quoting the unit order for the two-column and tagged fixtures.

## 8. `SpeechService` over `Windows.Media.SpeechSynthesis`: voices, rate, volume, pause, stop

Add the speaking half of Read Out Loud: a `SpeechService` that turns `ReadAloudUnit`s (`§7`) into audio with `Windows.Media.SpeechSynthesis`, supports pause, resume and stop, a chosen installed voice, speaking rate and volume, and reports each word boundary so the viewer can highlight it. It must not break the UI thread rule (all synthesis and playback run on background threads and only small progress events reach the UI) and keeps Recto's improvement that the text never leaves the machine: only voices installed in Windows are listed and used, and no network call is made by Recto.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `ISpeechEngine` (`synthesize(text, options)`, `play`, `pause`, `resume`, `stop`, word-boundary callback) and `WinRtSpeechEngine` in `src/services/speech_service.h/.cpp` using `SpeechSynthesizer`, `SpeechSynthesizerOptions` (`SpeakingRate`, `AudioVolume`, `IncludeWordBoundaryMetadata`), `MediaSource` and `MediaPlayer`, with a `FakeSpeechEngine` for tests. Done when: `A11y.SpeechServiceFake` drives play, pause, resume and stop through the fake and `A11y.SpeechServiceWinRt` speaks a short phrase through the real engine on the Windows host.
2. Implement `SpeechService` over the plan: one unit synthesising ahead while the previous plays, a monotonic position (unit index, word index), and a state machine `Idle`, `Preparing`, `Speaking`, `Paused`, `Stopping`, `Failed`. Done when: `A11y.SpeechStateMachine` verifies every legal transition and that `stop` from any state ends in `Idle` with no pending operation.

- [ ] Enumerate `SpeechSynthesizer::AllVoices` into `SpeechVoice` (`id`, `display_name`, `language`, `gender`), pick the saved `SpeechVoiceId` setting when it still exists and otherwise the voice matching the document `/Lang` or the Windows display language, and return a typed `SpeechError::NoVoiceInstalled` when the list is empty. Done when: `A11y.SpeechVoices` selects by saved id, by document language, by fallback, and reports no voice with a fake empty list.
- [ ] Apply `SpeechRate` (`0.5` to `3.0`, default `1.0`) and `SpeechVolume` (`0.0` to `1.0`, default `1.0`) from `SettingsStore` (`D02 T04 §2`), clamp out-of-range values, and apply a changed rate or volume to the next unit without restarting the current one. Done when: `A11y.SpeechSettingsApply` shows the clamp and the next-unit application.
- [ ] Read word boundaries from the synthesized stream's timed metadata (`SpeechCue` cues carrying start and end positions in the input text), map them to `(unit, word)` through `§7`'s word map, raise `on_word(unit, word)` for drawn words and `on_unit_box(unit)` once for a box-highlighted synthetic unit on a background thread, and raise no word event for a synthetic word. Done when: `A11y.SpeechWordEvents` shows ordered word events for a three-sentence plan and none after `stop`.
- [ ] Implement `pause`, `resume` and `stop` with `MediaPlayer::Pause`, `Play` and `Close`, cancel any in-flight `IAsyncOperation` for a synthesis not yet played, and complete `stop` within `250 ms` of the call. Done when: `A11y.SpeechStopLatency` measures `stop` during synthesis and during playback under `250 ms` and leaves no running operation.
- [ ] Handle failure without throwing: a synthesis `winrt::hresult_error`, an audio device removal and a voice that disappears mid-read become `SpeechError` values with a plain message, move the state to `Failed`, and leave the document untouched. Done when: `A11y.SpeechFailures` injects each failure through the fake and finds `Failed` with the expected message.
- [ ] Confirm no network use: the service calls no HTTP API and no cloud voice, and a source search in the test finds no `HttpClient`, `WinHttp` or `WebSocket` symbol in `speech_service.cpp`. Done when: `A11y.SpeechOffline` passes.
- [ ] Add the tests named above in `tests/recto-tests/a11y_speech_service_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=A11y.Speech*` passes on the Windows host.
- [ ] Commit: `feat(a11y): add the SpeechService over Windows.Media.SpeechSynthesis`

**Test checkpoint:** `recto-tests.exe --gtest_filter=A11y.Speech*` passes on the Windows host, with the stop-latency measurement quoted.

## 9. Read Out Loud bar, word highlight, shortcuts, `Ctrl+K` commands and settings

Add the Read Out Loud surface: a contextual `ReadAloudBar`, the on-canvas highlight of the word and sentence being read with automatic scrolling, the commands and shortcuts, and the voice, rate and volume settings, matching Acrobat Pro's `View` > `Read Out Loud`. It must not break the shell rules (`Esc` closes the bar and stops reading, every command is in `Ctrl+K`, motion respects "Show animations in Windows") and keeps Recto's improvement that reading never blocks the UI thread and never speaks for a screen-reader user by default: Recto announces only `Reading started`, `Paused` and `Stopped` through `AnnouncementService` so Narrator or NVDA is not talked over.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (contextual mode bars), 9.7 (Reading settings group), 10 (keyboard) and 11 (colour never the only signal, reduced motion); genuinely new surface, following the `Markup & Annotation Bar` chrome (`48 epx`, `RectoRaisedBrush`, `OverlayCornerRadius` 8 epx, `6 epx` padding, SPEC 8.2). Design board: `design/SPEC.md` section 9.13 with `design/screenshots/D-ReadAloud.png` (drawn by `D06 T04 §8`).
**Job:** The user presses `Ctrl+Shift+V` to hear the current page, `Ctrl+Shift+B` to hear from here to the end, or chooses `Read selection` on selected text. They pause with `Ctrl+Shift+C`, stop with `Ctrl+Shift+Y`, change voice, rate and volume while listening, and see the spoken word highlighted with the page following along. Consumer: `DocumentView`, `SelectionFloatingToolbar` context menu (`D03 T01 §4`), `CommandRegistry`, `SettingsPage` (`D02 T04 §5`).
**Treatment:** A `ReadAloudBar` bound to `ReadAloudViewModel` (`src/services/read_aloud_view_model.h/.cpp`) over `SpeechService` (`§8`) and `ReadAloudPlan` (`§7`), a `ReadAloudHighlightLayer` on `DocumentView` drawing the word as a `RectoAccentTintBrush` fill with a `2 epx` `RectoAccentBrush` underline (not colour alone) and the sentence as a lighter tint, and an auto-scroll that keeps the word in view. Cheaper substitute that fails the checkpoint: reading the whole document with no pause or stop, no highlight, or a highlight updated from the UI thread by polling.
**Chrome:** consume `RectoColors.xaml` tokens, `D02 T04 §8` motion rules, `DocumentView` geometry (`D02 T02 §5`), Fluent UI System Icons (`RectoIcons`: `play`, `pause`, `stop`), and `AnnouncementService` (`§5`).

**Build order:**

1. Implement `ReadAloudViewModel` and the `ReadAloudBar` with `Pause`/`Resume`, `Stop`, a `Voice` combo, `Rate` and `Volume` sliders, and the status text (`Reading page 4 of 20`), with commands `Read This Page`, `Read to End of Document`, `Read Selection`, `Pause or Resume Reading` and `Stop Reading` in `CommandRegistry`. Done when: `ReadAloudUi.CommandsAndBar` runs each command against a `FakeSpeechEngine` and every command appears in `Ctrl+K`.
2. Implement `ReadAloudHighlightLayer` with word and sentence highlighting, auto-scroll that follows the speech across lines and pages, and the `Reading` settings rows. Done when: `ReadAloudUi.HighlightAndFollow` reads a two-page fixture and the highlighted quads equal the spoken words' quads in order, with the viewport following to page 2.

- [ ] Build `ReadAloudBar` (`src/app/controls/ReadAloudBar.xaml/.h/.cpp`, `48 epx`) with `Pause`/`Resume` and `Stop` icon buttons (automation names `Pause reading`, `Resume reading`, `Stop reading`, tooltips with their shortcuts), a `Voice` combo box, `Rate` and `Volume` sliders and a status label, opened by any read command and closed by `Esc` or `Stop`. Done when: the bar passes the `D03 T02 §3` inventory and `Esc` stops speech and closes the bar.
- [ ] Register `Read This Page` (`Ctrl+Shift+V`), `Read to End of Document` (`Ctrl+Shift+B`), `Read Selection` (selection context menu and `Ctrl+K`), `Pause or Resume Reading` (`Ctrl+Shift+C`) and `Stop Reading` (`Ctrl+Shift+Y`) in `CommandRegistry`, enabled only when a document is open (`Read Selection` only with a selection) and active only while the document view has focus so text boxes keep their own `Ctrl+Shift+C`. Done when: `ReadAloudUi.CommandsAndBar` triggers each shortcut and the generated `docs/keyboard-shortcuts.md` (`D02 T04 §10`) lists all five.
- [ ] Draw `ReadAloudHighlightLayer` from `on_word` and `on_unit_box` events marshalled to the UI thread through `DispatcherQueue`, with the word fill plus underline (or the box outline for a figure) and the sentence tint, and scroll to keep the word at least `64 epx` inside the viewport, instantly when animations are off and with the `167 ms` transition when on. Done when: `ReadAloudUi.HighlightAndFollow` and `ReadAloudUi.ReducedMotion` verify the quads and the two scroll modes. Cheaper substitute: highlighting the whole line.
- [ ] Add a `Read form fields` row (`SpeechReadFormFields`, default on) to the Settings `Reading` group (`D02 T04 §5`) read by the plan builder of `§7`, and never speak the value of a field flagged password or with a hidden appearance. Done when: `ReadAloudUi.FormFieldSetting` turns field reading off and on and sees the plan change.
- [ ] Add `Voice`, `Speech rate` and `Speech volume` rows to the Settings `Reading` group (`D02 T04 §5`) bound to `SpeechVoiceId`, `SpeechRate` and `SpeechVolume`, searchable in `Find a setting`, with a `Preview voice` button speaking one sentence. Done when: `ReadAloudUi.SettingsRows` changes each setting, sees `SpeechService` read it, and finds the rows through `Find a setting`.
- [ ] Announce `Reading started`, `Reading paused`, `Reading resumed`, `Reading stopped` and `Reached the end of the document` through `AnnouncementService` without moving keyboard focus, and never announce individual words. Done when: `ReadAloudUi.Announcements` records exactly those notifications for a scripted session.
- [ ] Keep the UI responsive while reading: no synthesis, plan building or `PageText` fetch on the UI thread, and a frame-time check that reading a `200`-page document adds no frame over `16.7 ms` from this feature. Done when: `ReadAloudUi.UiThreadSafety` asserts no blocking call on the UI thread and records the frame times.
- [ ] Stop reading on tab close, on document switch and on app exit, and release the audio device within `250 ms`. Done when: `ReadAloudUi.LifecycleStop` closes the tab mid-sentence and finds `SpeechService` in `Idle`.
- [ ] Add `ReadAloudUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=ReadAloudUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add Read Out Loud with word highlight, shortcuts and settings`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ReadAloudUi.*` passes on the display-session host, quoting the highlight quads, the announcement log and the frame-time record.

## 10. Reflow model: logical blocks from the structure tree, article threads or heuristics

Build the content half of Reflow: a `ReflowDocument` that turns the shared `PageText` snapshots into a flow of logical blocks (headings, paragraphs, lists, table rows, figures) that can be laid out at any width. This is Acrobat Pro's `Reflow` (View, Zoom) and matches Foxit PDF Editor's Reflow and Text Viewer. It is an accessibility feature because a low-vision or small-screen reader gets one column of text at a chosen size instead of a page to pan. It must not break the thread contract (built from immutable `std::shared_ptr<const PageText>` snapshots off the UI thread, never the live `recto::pdf::Document`) and it keeps Recto's rule that nothing is written to the file; OCR text on scanned pages reflows like any other text.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `ReflowBlock` (`kind` among `Heading`, `Paragraph`, `ListItem`, `TableRow`, `Figure`, `PageBreak`, `heading_level`, `list_depth`, `marker`, `text`, `cells`, `alt_text`, `figure_bbox_pt`, and `anchors` as `(page_index, char_start, char_end)` ranges) and `ReflowDocument` in `src/services/reflow_model.h/.cpp`, and implement `build_reflow_page` with this precedence for order and roles: `PageText::struct_nodes` (tagged), then `/Threads` article order (`D03 T03 §15`), then geometric reading order. Done when: `Reflow.OrderPrecedence` reads a tagged fixture in structure order, an untagged fixture with an article thread in thread order, and a two-column untagged fixture column by column.
2. Implement the untagged heuristics and the cross-page join. Done when: `Reflow.Heuristics` finds headings, lists and merged paragraphs on `text/two-column-links-outline.pdf` and `Reflow.CrossPage` joins a paragraph split by a page break.

- [ ] Join lines into paragraphs using the hyphen folding of `D03 T01 §1`, line-end punctuation, indent and vertical gap, and never join across a column gutter or table cell. Done when: `Reflow.Paragraphs` yields one block per source paragraph on the two-column fixture.
- [ ] Detect headings on untagged pages as spans whose size is at least `1.2` times the page's most common body size or that are bold on a line of their own, assigning levels by distinct size rank (`1..6`), and take `heading_level` from `Span::heading_level` when tagged. Done when: `Reflow.Headings` finds the same levels on the tagged and untagged versions of one fixture.
- [ ] Detect list items from bullets (`U+2022`, `U+25E6`, `-`, `*`) and numbering (`1.`, `1)`, `a.`, `(i)`) at a consistent indent, keeping the marker separate from the text. Done when: `Reflow.Lists` reads a nested list with markers and depths.
- [ ] Produce `TableRow` blocks from `StructNode` table cells, one block per row with cell texts, header cells first, and read an untagged table as ordinary paragraphs line by line. Done when: `Reflow.Tables` reads a `3 x 3` table row by row.
- [ ] Drop page artifacts: tagged `/Artifact` content, and on untagged pages text repeating at the same position on at least `3` pages only when it lies inside the top `10%` or bottom `10%` of the page height (running headers, footers, page numbers). Done when: `Reflow.Artifacts` omits the repeated footer on an untagged fixture, keeps a title that appears once, and keeps a repeated body instruction line and a repeated table label that sit in the page body.
- [ ] Add `Figure` blocks from `StructNode` `/Figure` and image XObject boxes, with `/Alt` text as `alt_text`. Done when: `Reflow.Figures` lists a figure with its alt text and box.
- [ ] Bound the work: at most `20000` blocks per page, `50` million characters per document, `CancelToken` between pages, build lazily one page at a time. Done when: `Reflow.Bounds` ends a hostile page of `500000` one-character blocks inside the cap under ASan.
- [ ] Add `Reflow.*` tests in `tests/recto-tests/reflow_model_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Reflow.*` passes under ASan.
- [ ] Commit: `feat(a11y): add the Reflow model from structure, article threads and heuristics`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Reflow.*` passes under ASan, and ignoring `/Threads` makes `Reflow.OrderPrecedence` fail on the threaded fixture.

## 11. Reflow layout: DirectWrite text layout at a chosen width, size and spacing

Lay a `ReflowDocument` (§10) out at a given width: wrapped lines at a chosen font, size and line spacing, with figure images sized to the width. This is the layout half of Acrobat Pro's `Reflow` and Foxit PDF Editor's Reflow. It must not break the UI thread rule (layout runs on a worker and returns plain line and run data) and it keeps Recto's improvement that the reading font is the user's choice, not the PDF's embedded font, so low-vision readers get a large clear face.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `layout_reflow(ReflowDocument page range, ReflowStyle, width_px, CancelToken)` in `src/services/reflow_layout.h/.cpp` over DirectWrite `IDWriteTextLayout`, returning `ReflowLine` items (`block_index`, `y`, `height`, runs with text ranges and style) and total height for virtualization. Done when: `ReflowLayout.Wrap` wraps a paragraph to the width and `ReflowLayout.Heights` totals the line heights.
2. Add figure sizing, right-to-left blocks and the hit-test and anchor maps. Done when: `ReflowLayout.Figures`, `ReflowLayout.Bidi` and `ReflowLayout.AnchorMap` pass.

- [ ] Define `ReflowStyle` (`font_family`, `font_size_pt` `8..72`, `line_spacing` `1.0..2.5`, `max_line_width_chars` `0` for none or `40..120`, `plain_text_mode`) and clamp out-of-range values. Done when: `ReflowLayout.StyleClamp` clamps each field.
- [ ] Size `Figure` blocks to at most the layout width and `60%` of the viewport height, with `alt_text` as the fallback line when the crop cannot render, using `render_page_area` (`D03 T01 §11`) for the crop. Done when: `ReflowLayout.Figures` finds the width cap and the alt fallback.
- [ ] Shape right-to-left and mixed-direction text with the DirectWrite bidi analysis so Arabic and Hebrew blocks read in logical order and align to the right. Done when: `ReflowLayout.Bidi` lays out an Arabic block right-aligned in logical order.
- [ ] Build the maps between layout positions and `(page_index, char_index)` anchors: `hit_test(point)`, `caret_to_anchor`, `anchor_to_rect`. Done when: `ReflowLayout.AnchorMap` round-trips ten random positions to the same anchor.
- [ ] Lay out incrementally by block range so a `1000`-page document lays out only the visible range plus one screen on each side, and cancel stale layouts on a width change. Done when: `ReflowLayout.Incremental` lays out `3` screens of a long fixture in under `50 ms` and cancels a superseded request.
- [ ] Add `ReflowLayout.*` tests in `tests/recto-tests/reflow_layout_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ReflowLayout.*` passes on the Windows host.
- [ ] Commit: `feat(a11y): add DirectWrite layout for the Reflow view`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ReflowLayout.*` passes on the Windows host, and mapping positions by character count instead of anchors makes `ReflowLayout.AnchorMap` fail.

## 12. Reflow view and Text Viewer: `Ctrl+4`, size controls, settings and accessibility

Add the reading surface: a `ReflowView` in the document slot of the one shell that shows §10 and §11 as a single column, plus a `Text Viewer` mode that shows the same text as plain, unstyled text. This is Acrobat Pro's `Reflow` (`Ctrl+4`) and Foxit PDF Editor's Reflow and Text Viewer. It must not break the shell rules (one shell, no new window, `Esc` and `Ctrl+K` work, motion respects "Show animations in Windows") and it keeps Recto's improvements that the view is fully exposed to UI Automation, selection and `Ctrl+F` keep working, and the file is never changed.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** Genuinely new surface. It follows `design/SPEC.md` section 8 (the document slot in the three presets), section 9.3 (selection and search behave as on a page) and section 11 (accessibility), with `design/screenshots/D-Search.png` for the search treatment. Design board: `design/SPEC.md` section 9.13 with `design/screenshots/D-ReadAloud.png` (drawn by `D06 T04 §8`).
**Job:** The user presses `Ctrl+4`, sees the document as one readable column at their chosen size and font, reads or listens to it, searches, selects and copies text, jumps to the page position of any paragraph, and presses `Ctrl+4` again to return to the page. Consumer: `DocumentView` slot, `FindCard` (`D03 T01 §6`), `AnnouncementService` (§5) and screen readers.
**Treatment:** A virtualized `ReflowView` drawn from §11 line data on a `Canvas` surface with `RectoCanvasBrush` background and `RectoTextBrush` text, a `Reflow` toggle in the View menu and a label `Reflow view` in the status area (text, not colour), size controls bound to `Ctrl+Plus` and `Ctrl+Minus` while the view is active, and a `Text Viewer` toggle that sets `plain_text_mode`. Cheaper substitute that fails the checkpoint: re-rendering the page bitmap at a larger zoom, or a view that screen readers read as one unlabelled image.
**Chrome:** consume `RectoCanvasBrush`, `RectoTextBrush`, `RectoTextSecondaryBrush`, `RectoSearchHitBrush`, `RectoTextSelectionBrush` from `design/tokens/RectoColors.xaml` and the high-contrast dictionary; no hex values.

**Build order:**

1. Build `ReflowView` (`src/app/views/ReflowView.xaml/.h/.cpp`) and `ReflowViewModel` (`src/services/reflow_view_model.h/.cpp`) with virtualized drawing from §11, pointer and keyboard selection over anchors, copy as plain text, and the `Show in page` action. Done when: `AppTests.ReflowView` opens a fixture, scrolls, selects two paragraphs, copies the text and uses `Show in page` to land on the source page with the anchor highlighted.
2. Register the commands, settings rows, search mapping and UIA peer. Done when: `AppTests.ReflowCommands`, `AppTests.ReflowSearch` and `A11y.ReflowPeer` pass.

- [ ] Register `Reflow` (`view.reflow`, `Ctrl+4`), `Text Viewer` (`view.textViewer`, no default accelerator) and `Show in page` (`view.reflowShowInPage`) in `CommandRegistry` (`D02 T03 §11`), enabled when a document is open, and add `Ctrl+4` to the SPEC section 10 table through `D02 T04 §9`. Done when: the command box finds all three and `Keyboard.Audit` reports `Ctrl+4` bound once.
- [ ] Keep the current position when toggling: entering reflow scrolls to the first block of the current page, and leaving it scrolls the page view to the page of the block at the top. Done when: `AppTests.ReflowView` toggles twice and lands on the same page.
- [ ] Add `Reflow font` (`ReflowFontFamily`, default `Segoe UI`), `Reflow text size` (`ReflowFontSize`, default `16` pt), `Reflow line spacing` (`ReflowLineSpacing`, default `1.4`) and `Reflow line width` (`ReflowMaxLineChars`, default `80`) to the Settings `Reading` group (`D02 T04 §5`), searchable in `Find a setting`. Done when: `AppTests.ReflowSettings` changes each and sees the view re-layout.
- [ ] Map `Ctrl+F` hits from `SearchEngine` (`D03 T01 §5`) to reflow anchors so hits highlight in `RectoSearchHitBrush` and `F3` scrolls to them. Done when: `AppTests.ReflowSearch` finds a word across two pages and highlights it in the reflow text.
- [ ] Expose the view to UI Automation as a document with a text pattern, `AutomationProperties.HeadingLevel` on headings, list and table item peers, and `alt_text` as the name of figures. Done when: `A11y.ReflowPeer` walks a tagged fixture with a UIA client and reads headings, a list, a table row and a figure alt text in order.
- [ ] Read Out Loud (`§7` to `§9`) keeps working in reflow by reading the same plan and highlighting the matching reflow words through the anchors. Done when: `ReadAloudUi.ReflowHighlight` highlights the spoken word in the reflow view.
- [ ] Honour reduced motion and high contrast: no animated scrolling when animations are off, and `HighContrast` brushes used for text and selection. Done when: `AppTests.ReflowMotionAndContrast` checks both.
- [ ] Give the toggle, size controls and `Show in page` an automation name and a tooltip with their shortcut. Done when: the inventory (`§3`) finds none missing or undersized. Cheaper substitute: tooltips only.
- [ ] Add the tests named above. Done when: `recto-tests.exe --gtest_filter=AppTests.Reflow*:A11y.ReflowPeer:ReadAloudUi.ReflowHighlight` passes on the display-session host.
- [ ] Commit: `feat(a11y): add the Reflow view and Text Viewer`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.Reflow*:A11y.ReflowPeer:ReadAloudUi.ReflowHighlight` passes on the display-session host, and drawing the page bitmap instead of text makes `A11y.ReflowPeer` fail.

## Verification

- SPEC section 11 read line by line against the findings: every clause proven by `A11y.*` tests (`TextRangeUnits`, `TextProvider`, `SelectionAndFind`, `Inventory`, `TabOrder`, `EscapeStack`, `Announcements`, `FastScrollCoalescing`, `JobAnnouncementSink`, `ColourNotOnlySignal`, `EndToEndSection11`, `ReadAloud*`, `Speech*`), `ReadAloudUi.*`, `Reflow.*`, `ReflowLayout.*`, `AppTests.Reflow*` and `A11y.ReflowPeer`.
- M3's UIA deliverable demonstrable with Windows Narrator on the reference machine.
- `recto-tests` full suite green; `python scripts/todo-graph.py validate` passes.
