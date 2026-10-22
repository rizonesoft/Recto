---
schema_version: 1
id: ui-accessibility
domain: 10-ui
status: active
title: "TODO-03 -- recto-ui Accessibility: UI Automation, High Contrast, Text Scale and Strings"
track: M2
---

# TODO-03 -- recto-ui Accessibility: UI Automation, High Contrast, Text Scale and Strings

> **Goal:** `recto-ui` gives back everything WinUI 3 supplied for free to assistive technology and to people with different needs: a UI Automation provider tree with the control patterns, the text pattern and an adapter D03 T02 reuses for page text, polite notification and live-region announcements, high contrast that switches live in every Windows contrast theme, Windows text scaling applied to the type ramp with layouts that reflow without clipping, and string tables with language selection, English fallback, a pseudo-locale and right-to-left mirroring, all proven by an automated tree walk, a keyboard-only traversal, a Narrator smoke step and an Accessibility Insights run.

> **Current state:** Measured 2026-10-22. `src/ui` does not exist yet (`D10 T01 §1` creates it), so there is no UI Automation provider, no `ThemeManager` high contrast path, no text scale handling and no `ui::Strings`. The WinUI scaffold in `src/app` (`App.xaml`, `MainWindow.xaml`, `Recto.vcxproj`) still relies on WinUI's built-in automation peers, `ThemeDictionaries` and MRT Core; `D10 T01 §2` removes it. `src/app/strings/` does not exist; `D07 T02 §5` and `§13` to `§16` still describe `Resources.resw` and are being rewritten against this file. `design/SPEC.md` section 4 (the `HighContrast` dictionary), section 11 (accessibility), section 9.7 (the Language row) and `docs/specs/2026-10-22-recto-ui-toolkit-design.md` sections 4.6, 4.7, 4.8, 4.10 and 4.12 are the governing text. `D03 T02` currently plans a custom `AutomationPeer` for page text; this file replaces that mechanism with the adapter in `§3`.

## Inputs

- `design/SPEC.md` sections 4 (colour and the high contrast mapping), 5 (type ramp), 9.7 (the `Language` row of `Language and updates`), 10 (keyboard) and 11 (accessibility); `docs/specs/2026-10-22-recto-ui-toolkit-design.md` sections 4.6 (tokens and `ThemeManager`), 4.7 (text), 4.8 (input and focus), 4.10 (accessibility) and 4.12 (strings).
- Microsoft documentation for UI Automation providers (`IRawElementProviderSimple`, `IRawElementProviderFragment`, `IRawElementProviderFragmentRoot`, control pattern providers, `ITextProvider`, `ITextRangeProvider`, `UiaRaiseNotificationEvent`), `SPI_GETHIGHCONTRAST`, `GetSysColor`, `Windows.UI.ViewManagement.UISettings` and the Win32 `STRINGTABLE` resource.
- -> XREF: D10 T01 §8 -- the generated `recto_tokens.g.h` carries the high contrast mapping of each brush to a `COLOR_*` index that `§5` applies and verifies.
- -> XREF: D10 T01 §13 -- `ui::FocusManager` owns the Fluent focus visual and arrow groups that `§5` restyles for high contrast and `§8` walks.
- -> XREF: D10 T02 §1 -- the `Control` base declares the automation name, role and tooltip hooks that `§1` turns into providers; `D10 T02 §23` is the control gallery that `§2` and `§8` walk.
- -> XREF: D03 T02 §1 -- the document text provider (`DocumentViewAutomationPeer` over `PageText`) is rebuilt on `§1` (fragment root and element provider) and `§3` (`ui::TextRangeAdapter`) instead of a WinUI `AutomationPeer`.
- -> XREF: D03 T02 §5 -- the `AnnouncementService` raises its polite page, find, selection and job messages through the notification sink that `§4` builds.
- -> XREF: D07 T02 §5 -- localization readiness (the unresourced-string scan and the 40 percent pseudo-locale layout pass) runs over the `.rc` string tables and the pseudo-locale generator that `§7` builds.
- -> XREF: D07 T02 §13 -- the translation catalogue (`languages.json`, glossary, checker) is enforced over the `STRINGTABLE` files `§7` defines and the id-completeness script it adds.
- -> XREF: D07 T02 §14 -- the interface language setting supplies the `UiLanguage` value that `ui::Strings` selects by in `§7`.
- -> XREF: D07 T02 §16 -- right-to-left readiness consumes the flow direction, mirroring and icon flip rules built in `§7`.
- -> XREF: D02 T01 §2 -- `TODO-01-app-shell.md` consumes sections of this file (first use in the section named).
- -> XREF: D02 T02 §16 -- `TODO-02-document-view.md` consumes sections of this file (first use in the section named).
- -> XREF: D02 T03 §5 -- `TODO-03-home-and-panels.md` consumes sections of this file (first use in the section named).
- -> XREF: D02 T04 §4 -- `TODO-04-settings-and-layout.md` consumes sections of this file (first use in the section named).
- -> XREF: D02 T05 §4 -- `TODO-05-rich-media-3d-geo.md` consumes sections of this file (first use in the section named).
- -> XREF: D02 T06 §3 -- `TODO-06-cloud-storage-connectors.md` consumes sections of this file (first use in the section named).
- -> XREF: D03 T03 §10 -- `TODO-03-annotations-and-forms.md` consumes sections of this file (first use in the section named).
- -> XREF: D03 T04 §12 -- `TODO-04-edit-pdf.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T02 §1 -- `TODO-02-pdf-conversions-a.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T04 §1 -- `TODO-04-office-to-pdf.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T05 §1 -- `TODO-05-excel-web-and-compliance.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T06 §1 -- `TODO-06-print-production.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T07 §1 -- `TODO-07-action-wizard.md` consumes sections of this file (first use in the section named).
- -> XREF: D05 T02 §1 -- `TODO-02-organize-ui.md` consumes sections of this file (first use in the section named).
- -> XREF: D06 T03 §8 -- `TODO-03-user-failure-gates.md` consumes sections of this file (first use in the section named).
- -> XREF: D07 T01 §11 -- `TODO-01-packaging-and-print.md` consumes sections of this file (first use in the section named).
- -> XREF: D08 T01 §15 -- `TODO-01-ai-assistant.md` consumes sections of this file (first use in the section named).

## Outcome

- A UI Automation client (the in-process `IUIAutomation` test client, Narrator, Accessibility Insights) walks every `recto-ui` window and popup as a tree of elements with correct names, help text, accelerator keys, automation ids, control types, bounding rectangles in screen pixels at every DPI and focus state, and receives focus, property, structure and notification events (`§1`, `§4`).
- Each control exposes exactly the UIA patterns its control type requires, and `ui::TextRangeAdapter` lets `TextBox` and the page text of D03 T02 answer `ITextProvider` and `ITextRangeProvider` through one tested implementation (`§2`, `§3`).
- Switching Windows to any of the four contrast themes (Aquatic, Desert, Dusk, Night sky) repaints every open window live with system colours, a visible focus ring, distinguishable selection and disabled states and a readable page canvas; changing Windows text size rescales the type ramp live and the shell reflows without clipping from 100 to 225 percent (`§5`, `§6`).
- Every user-facing string comes from a `STRINGTABLE` resource selected by the Language setting then the Windows display language, falls back to English, is checked for id completeness in every shipped language, survives a lengthened pseudo-locale and mirrors the layout for right-to-left languages (`§7`).
- One verification run proves it: a UIA tree walk of the control gallery against expected roles, names and patterns, keyboard-only traversal of every control, an attended Narrator pass and a recorded Accessibility Insights FastPass result (`§8`).

**Adjacency:** list=not-applicable (an access layer; it lists no records of its own); document=not-applicable (nothing a user carries, sends or prints; page text exposure is owned by D03 T02 §1); settings=applicable @ D07 T02 §14; reporting=not-applicable (verification results are test artifacts, not user reports); notifications=applicable @ D03 T02 §5; permissions=not-applicable (no refusal paths; assistive technology is never gated); audit=not-applicable (no persistent trail over accessibility state); exchange=not-applicable (no import or export); reverse=not-applicable (switching contrast, text size or language is itself the reverse; nothing is created)

**Adjacency rationale:** This file builds the platform services every surface relies on to be accessible rather than a product feature with records, so most keys are not applicable. The one settings consumer is the interface language, whose Settings row and restart prompt belong to `D07 T02 §14`; `ui::Strings` in `§7` is the loader that setting feeds. The announcements that reach a screen reader are produced by `D03 T02 §5` through the notification sink here. Contrast, text scale and animation preferences are read from Windows, not stored by Recto, so there is no Recto setting to own for them.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | UI Automation provider tree: fragment root, navigation, properties, focus events | D10 T02 §1 | [ ] |
| 2 | §2 | UIA control patterns for every recto-ui control | §1, D10 T02 §23 | [ ] |
| 3 | §3 | UIA `TextPattern` for `TextBox` and a reusable text-range adapter | §1, D10 T02 §4 | [ ] |
| 4 | §4 | UIA notification and live-region announcements | §1 | [ ] |
| 5 | §5 | High contrast: system colour mapping, live switching and high-contrast focus visuals | D10 T01 §8, D10 T01 §13 | [ ] |
| 6 | §6 | Windows text scaling applied to the type ramp and layout | D10 T01 §7, D10 T01 §10 | [ ] |
| 7 | §7 | `Strings`: localized string tables, fallback and pseudo-localization | D10 T01 §1 | [ ] |
| 8 | §8 | Accessibility verification: UIA tree walk, keyboard-only traversal, Narrator smoke | §2, §3, §4, §5, §6 | [ ] |

## 1. UI Automation provider tree: fragment root, navigation, properties, focus events

WinUI created automation peers for every control. `recto-ui` draws its own pixels, so to Windows each `ui::Window` is one opaque HWND until it answers `WM_GETOBJECT`. This section builds the provider layer in `src/ui/a11y/`: a fragment root per window and per popup window, a fragment per element that has an automation role, the navigation between them, the properties assistive technology reads, and the events it listens for. Patterns come in `§2`; this section must not break the UI thread rule (every UIA call is marshalled to the UI thread, `D10 T01 §3`) or the headless harness (the tree is built from the element tree, not from an HWND).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Add `ui::AutomationInfo` to `ui::Element` (`src/ui/element.h`) and implement `ui::UiaProvider` (`src/ui/a11y/uia_provider.h/.cpp`) with the three interfaces, `WM_GETOBJECT` handling in `ui::Window` and the `UiaDisconnectProvider` call on removal. Done when: `Ui.UiaRootReturned` shows `UiaReturnRawElementProvider` answers `UiaRootObjectId` for a window and a popup, and `ElementFromHandle` from an in-process `IUIAutomation` client returns a root.
2. Implement navigation, the property set and the event raising in `src/ui/a11y/uia_events.cpp`, then the tree-walk tests in `tests/recto-tests/ui_uia_tree_tests.cpp`. Done when: `Ui.UiaTreeWalk` walks a fixture tree with `TreeWalker` in both directions and reads every listed property, and focus, property and structure events arrive at an `IUIAutomation` event handler.

- [ ] Add `ui::AutomationInfo` (`name`, `help_text`, `accelerator_key`, `access_key`, `automation_id`, `control_type`, `localized_control_type`, `is_content`, `is_control`) and make `ui::Element` own one, so no control keeps a private copy. Done when: `Ui.AutomationInfoDefaults` shows an element with no role is absent from the control view and one with a role is present. Cheaper substitute: a property bag on each control class that the provider reads by `dynamic_cast`. Source: toolkit design section 4.5 and 4.10.
- [ ] Handle `WM_GETOBJECT` with `lParam == UiaRootObjectId` in `ui::Window::on_message` (`src/ui/window.cpp`) by returning `UiaReturnRawElementProvider(hwnd, wParam, lParam, root_provider)`, and answer `OBJID_CLIENT` for the legacy MSAA bridge through `UiaReturnRawElementProvider` as well. Done when: `Ui.UiaRootReturned` retrieves the root for the main window and for an owned popup from a client. Cheaper substitute: implementing only `IAccessible`, which gives no patterns or text.
- [ ] Implement `IRawElementProviderSimple` (`ProviderOptions` as `ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading`, `GetPatternProvider`, `GetPropertyValue`, `HostRawElementProvider`) in `src/ui/a11y/uia_provider.cpp`. Done when: the root returns the window's host provider and a child returns `nullptr` for the host. Source: Microsoft UIA provider documentation.
- [ ] Implement `IRawElementProviderFragment` navigation (`Navigate` for Parent, NextSibling, PreviousSibling, FirstChild, LastChild over the logical element tree skipping elements without a role), `GetRuntimeId` (`UiaAppendRuntimeId` plus a stable per element id), `BoundingRectangle` and `GetEmbeddedFragmentRoots`. Done when: `Ui.UiaNavigation` finds the same order as `ui::Element` children and distinct runtime ids across 10000 elements.
- [ ] Compute `UIA_BoundingRectanglePropertyId` in screen pixels from the element's arranged bounds, the window's DPI scale and `ClientToScreen`, and return an empty rectangle for collapsed or clipped-out elements with `UIA_IsOffscreenPropertyId` true. Done when: `Ui.UiaBoundsAcrossDpi` matches the layout rectangle at 100, 150 and 200 percent and after a window move. Cheaper substitute: returning epx values unscaled, which highlights the wrong region in Narrator.
- [ ] Implement the property getters `Name`, `HelpText`, `AcceleratorKey`, `AccessKey`, `AutomationId`, `ControlType`, `LocalizedControlType`, `IsEnabled`, `IsKeyboardFocusable`, `HasKeyboardFocus`, `IsContentElement`, `IsControlElement`, `ClassName` and `FrameworkId` (`"RectoUI"`) from `ui::AutomationInfo` and the element's state. Done when: `Ui.UiaProperties` reads every property of a button fixture and gets the exact set values. Cheaper substitute: leaving `HelpText` empty and moving the tooltip into `Name`.
- [ ] Implement `IRawElementProviderFragmentRoot` (`ElementProviderFromPoint` through `Element::hit_test`, `GetFocus` through `ui::FocusManager`) on the window root and on each popup root. Done when: `Ui.UiaFromPoint` returns the deepest element under a point and `Ui.UiaGetFocus` returns the focused one.
- [ ] Raise `UiaRaiseAutomationEvent` for `UIA_AutomationFocusChangedEventId` on every `ui::FocusManager` change, `UiaRaiseAutomationPropertyChangedEvent` for name, enabled state, toggle state and value changes, and `UiaRaiseStructureChangedEvent` for child added, removed and reordered, all guarded by `UiaClientsAreListening()`. Done when: `Ui.UiaEvents` receives each event kind in an `IUIAutomationEventHandler` and no call is made when no client listens.
- [ ] Call `UiaDisconnectProvider` for an element and its descendants when `ui::Element` leaves the tree, and keep providers alive only through the clients' reference counts. Done when: `Ui.UiaDisconnect` removes a subtree and a client holding an old element gets `UIA_E_ELEMENTNOTAVAILABLE` rather than a crash, under ASan. Cheaper substitute: leaking providers until window close.
- [ ] Marshal provider calls arriving on UIA threads to the UI thread through `ui::Dispatcher` (`D10 T01 §3`) with a bounded wait, returning `UIA_E_TIMEOUT` instead of blocking if the UI thread is busy. Done when: `Ui.UiaThreadMarshal` calls a property from a worker thread and gets the UI-thread value, and a blocked UI thread produces a timeout, not a deadlock.
- [ ] Add `Ui.UiaTreeWalk` in `tests/recto-tests/ui_uia_tree_tests.cpp` walking the fixture tree with `IUIAutomationTreeWalker` in both directions. Done when: `recto-tests.exe --gtest_filter=Ui.Uia*` passes under ASan and fails if `Navigate` returns the wrong sibling.
- [ ] Commit: `ui: UI Automation provider tree with fragment roots, navigation, properties and events`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Ui.Uia*` passes under ASan on the display-session host, quoting the property table read for the button fixture.

## 2. UIA control patterns for every recto-ui control

A button that Narrator can find but not invoke is not accessible. This section gives each control from `D10 T02` the patterns its UIA control type requires, driven by one table so a new control cannot ship without its patterns. It must not break `§1` (patterns hang off the same provider objects) and keeps the control state as the single source of truth: a pattern method calls the same command or setter the pointer does.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Add the pattern table `src/ui/a11y/pattern_table.h` (control type, required patterns, required properties) and the pattern providers in `src/ui/a11y/uia_patterns.cpp`. Done when: `Ui.UiaPatternTable` lists every `recto-ui` control class against its row and a missing row fails the build of the test.
2. Wire each control to its patterns and add the table-driven gallery test in `tests/recto-tests/ui_uia_patterns_tests.cpp`. Done when: `Ui.UiaRequiredPatterns` asserts every gallery control exposes its required patterns and properties through an `IUIAutomation` client.

- [ ] Implement `IInvokeProvider` for `Button`, `HyperlinkButton`, `AppBarButton`, `MenuItem`, `Chip`, `TabStrip` close buttons and `SettingsCard` actions by calling the element's `ui::Command` (`D10 T01 §16`). Done when: `Ui.UiaInvoke` invokes each from a client and the bound command ran once. Cheaper substitute: simulating a mouse click at the centre, which fails off screen and under a disabled state.
- [ ] Implement `IToggleProvider` for `ToggleButton`, `CheckBox` (three states), `ToggleSwitch` and toggled toolbar buttons. Done when: `Ui.UiaToggle` cycles each and the property-changed event fires with the new `ToggleState`.
- [ ] Implement `IValueProvider` for `TextBox`, `PasswordBox` (read refused, `IsPassword` true), `NumberBox`, `ComboBox` text and `SearchBox`, and `IRangeValueProvider` for `Slider`, `ProgressBar`, `ProgressRing` (read-only) and the `NumberBox` spin range. Done when: `Ui.UiaValueRange` sets and reads each, refuses a write to a read-only one with `UIA_E_NOTSUPPORTED`, and never returns a password.
- [ ] Implement `IExpandCollapseProvider` for `ComboBox`, `DropDownButton`, `Expander`, `TreeView` items, `Menu` submenus and `SplitButton` flyouts, and `ISelectionProvider` plus `ISelectionItemProvider` for `ComboBox` lists, `RadioButton` groups, `SegmentedControl`, `ModeSwitcher`, `TabStrip`, `ItemsView` and `NavigationRail`. Done when: `Ui.UiaExpandSelect` expands, collapses, selects and adds to selection, and `SelectionItem.Select` on a radio unselects its siblings.
- [ ] Implement `IScrollProvider` for `ScrollViewer` and virtualized `ItemsView`, `IScrollItemProvider.ScrollIntoView` for items that are virtualized away (realizing them first), and `IVirtualizedItemProvider.Realize` so a client finds an item that is not in the tree. Done when: `Ui.UiaScroll` scrolls by amount and percent, brings item 5000 of a 10000 item `ItemsView` into view, and reports `HorizontallyScrollable` correctly.
- [ ] Implement `IGridProvider` and `IGridItemProvider` for the grid layout of `ItemsView` (page grid and thumbnails) and `ITableProvider` row and column headers where `SettingsGroup` lists use them. Done when: `Ui.UiaGrid` reads row and column counts and `GetItem(row, col)` for a 6 by 4 grid fixture.
- [ ] Implement `IWindowProvider` and `ITransformProvider` for the top-level window, and `Dialog` exposing `UIA_IsModalPropertyId` through `IWindowProvider` with `WindowInteractionState`. Done when: `Ui.UiaWindowPattern` reads state, maximizes, minimizes, closes and finds a `Dialog` modal.
- [ ] Write `pattern_table.h` from the Microsoft control type requirements (`Button`: Invoke; `CheckBox`: Toggle; `ComboBox`: ExpandCollapse and Value; `Edit`: Value (and Text, `§3`); `List`/`ListItem`: Selection/SelectionItem plus Scroll; `Slider`: RangeValue; `Tab`/`TabItem`: Selection/SelectionItem; `Tree`/`TreeItem`: ExpandCollapse and SelectionItem; `Menu`/`MenuItem`: ExpandCollapse and Invoke) for every control type `recto-ui` maps to. Done when: each class has one row and `Ui.UiaPatternTable` fails if a control declares a role with no row.
- [ ] Implement `Ui.UiaRequiredPatterns` in `tests/recto-tests/ui_uia_patterns_tests.cpp` instantiating every control from the gallery (`D10 T02 §23`) and asserting its `GetCurrentPattern` set and required properties against the table. Done when: the test passes and fails when `Invoke` is removed from `Button`. Cheaper substitute: checking a handful of controls by hand.
- [ ] Verify disabled and hidden behaviour: a disabled control reports `IsEnabled` false and its pattern methods return `UIA_E_ELEMENTNOTENABLED`, a collapsed element is absent from the control view. Done when: `Ui.UiaDisabledAndHidden` passes for a disabled button, slider and combo box.
- [ ] Commit: `ui: UIA control patterns for every recto-ui control from one requirements table`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Ui.UiaRequiredPatterns:Ui.UiaInvoke:Ui.UiaToggle:Ui.UiaValueRange:Ui.UiaExpandSelect:Ui.UiaScroll:Ui.UiaGrid:Ui.UiaWindowPattern` passes on the display-session host, quoting the control-by-pattern table.

## 3. UIA `TextPattern` for `TextBox` and a reusable text-range adapter

Screen readers read text through `ITextProvider` and `ITextRangeProvider`, which is also what D03 T02 needs for page text. This section builds that once: `ui::TextRangeAdapter`, a UIA range implementation over an abstract text source, used first by `TextBox` and then by the document text provider. It must not know about PDFs, must not copy the whole text of a large document into memory and must keep selection a single state held by the source, not by the adapter.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Define `ui::ITextSource` and implement `ui::TextRangeAdapter` (`src/ui/a11y/text_range_adapter.h/.cpp`) with every `ITextRangeProvider` method and the `ITextProvider`/`ITextProvider2` wrapper. Done when: `Ui.TextRangeUnits` expands, moves and compares ranges by every unit over an in-memory source.
2. Implement `ui::TextBox` as a source and expose its provider, then add the in-process client tests in `tests/recto-tests/ui_uia_text_tests.cpp`. Done when: `Ui.TextBoxUia` reads, selects and receives selection-changed events from `TextBox` through `IUIAutomationTextPattern`.

- [ ] Define `ui::ITextSource` in `src/ui/a11y/text_source.h`: `length()`, `slice(start, count)`, `unit_boundary(offset, unit, direction)` for character, format, word, line, paragraph, page and document, `attributes(offset)` (font name, size, weight, italic, style id, read only), `bounding_rects(start, end)` in screen pixels, `selection()` and `set_selection()`, `range_from_point(pt)`, `scroll_into_view(start, end, align_top)`, `visible_ranges()`, `caret()`. Done when: the header compiles with no PDF, Direct2D or window includes.
- [ ] Implement `ui::TextRangeAdapter` as `ITextRangeProvider` and `ITextRangeProvider2` (`Clone`, `Compare`, `CompareEndpoints`, `ExpandToEnclosingUnit`, `FindText`, `FindAttribute`, `GetAttributeValue`, `GetBoundingRectangles`, `GetEnclosingElement`, `GetText` with `maxLength`, `Move`, `MoveEndpointByUnit`, `MoveEndpointByRange`, `Select`, `AddToSelection`, `RemoveFromSelection`, `ScrollIntoView`, `GetChildren`, `ShowContextMenu`) over offsets, never a copy of the text. Done when: `Ui.TextRangeUnits` passes for every unit on ASCII, surrogate pairs and combining marks, and `Ui.TextRangeMoveBounds` shows `Move` clamps at the start and end and returns the moved count. Cheaper substitute: returning `E_NOTIMPL` for word and line units.
- [ ] Implement `ITextProvider` (`DocumentRange`, `GetSelection`, `GetVisibleRanges`, `RangeFromChild`, `RangeFromPoint`, `SupportedTextSelection`) and `ITextProvider2` (`RangeFromAnnotation`, `GetCaretRange`) wrapping the adapter. Done when: `Ui.TextProviderSurface` calls every method from a client.
- [ ] Implement `GetBoundingRectangles` by returning one rectangle per laid-out line segment (`SAFEARRAY` of doubles, four per rectangle) from the source in screen pixels. Done when: `Ui.TextRangeBounds` returns the line rectangles of a wrapped multi-line `TextBox` selection that match its layout. Source: Microsoft ITextRangeProvider documentation.
- [ ] Implement the `TextSelectionChanged` event: the source calls `adapter.notify_selection_changed()`, which raises `UIA_Text_TextSelectionChangedEventId` through `UiaRaiseAutomationEvent` with a 50 ms coalescing window during a drag, and `UIA_Text_TextChangedEventId` on edits. Done when: `Ui.TextSelectionEvents` receives one selection event per drag burst and one per keyboard change.
- [ ] Make `ui::TextBox` (`D10 T02 §4`) implement `ui::ITextSource` over its edit buffer, selection and DirectWrite layout, and expose `IValueProvider` from `§2` plus the text pattern from this adapter. Done when: `Ui.TextBoxUia` types, selects a word with `ExpandToEnclosingUnit(Word)` then `Select`, and reads it back through UIA.
- [ ] Support `Select`, `AddToSelection` and `RemoveFromSelection` by writing the source selection, so keyboard, pointer and UIA changes agree character for character. Done when: `Ui.TextSelectionTwoWay` selects by pointer then reads by UIA and the reverse and the strings are equal. Cheaper substitute: a second selection state in the adapter.
- [ ] Hold a `UiaTextRange` only as `(source weak reference, start, end)` and survive text edits by adjusting endpoints through an edit-notification list, and return `UIA_E_ELEMENTNOTAVAILABLE` after the source is destroyed. Done when: `Ui.TextRangeEditStability` edits text under a live range and then reads it without a crash under ASan.
- [ ] Document the source contract in code comments only: state in `src/ui/a11y/text_source.h` what D03 T02 must implement (`ITextSource` over `PageText` snapshots, offsets as `(page, char)` flattened by a page-start table). Done when: the header comment names the unit rules and the bounding rectangle contract, and `D03 T02 §1` can implement it without reading `§3`'s tests.
- [ ] Add a conformance suite `ui::testing::TextSourceConformance` that any `ITextSource` can run, and run it on the in-memory fixture and on `TextBox`. Done when: `Ui.TextSourceConformance` passes for both and exports the suite for D03 T02 to reuse.
- [ ] Commit: `ui: UIA TextPattern for TextBox and the reusable text-range adapter`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Ui.TextRange*:Ui.TextProviderSurface:Ui.TextBoxUia:Ui.TextSelection*:Ui.TextSourceConformance` passes under ASan on the display-session host.

## 4. UIA notification and live-region announcements

Screen readers need to be told about changes that move no focus: "Page 3 of 14", "Selected 12 words", "Conversion finished". WinUI's `RaiseNotificationEvent` and live regions go away with it, so `recto-ui` raises UIA notifications itself and marks live regions. It must not steal focus, must not shout over the user's screen reader by default (polite processing), and keeps the message text with the caller, so `D03 T02 §5` decides what to say and this section decides how it is delivered.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 11 (polite announcements; colour is never the only signal) and section 9.3 (selection and search status); no board draws an announcement, so the visible twins are the existing status texts (`"2 of 14"`, the queue card, toasts).
**Job:** A screen reader user hears that a page changed, a selection grew or a job finished without moving focus. Consumer: Narrator, NVDA and JAWS through UIA notification events, and `AnnouncementService` in `D03 T02 §5`.
**Treatment:** `ui::Announcer::post(text, kind, processing, activity_id)` over `UiaRaiseNotificationEvent`, plus a `LiveSetting` property on `ui::InfoBar`, `ui::Toast` and status text elements with `UIA_LiveRegionChangedEventId`. Cheaper substitute that fails the checkpoint: moving focus to a hidden label, or updating visible text with no UIA event so only sighted users learn of the change.
**Chrome:** consume the window root provider from `§1`; add no visible element; `ui::InfoBar`, `ui::Toast` and the status text of `D10 T02 §20` carry the live-region flag, never a second custom control.

**Build order:**

1. Implement `ui::Announcer` (`src/ui/a11y/announcer.h/.cpp`) with `NotificationKind` and `NotificationProcessing` enums mapped to the UIA values and a test sink. Done when: `Ui.AnnouncerRaises` receives `ItemAdded`, `ActionCompleted`, `ActionAborted` and `Other` with each processing mode in a client handler.
2. Add `LiveSetting` to `ui::AutomationInfo` and raise `UIA_LiveRegionChangedEventId` on text change for live elements. Done when: `Ui.LiveRegionChanged` receives the event for `InfoBar` message changes with `Polite` and for an `Assertive` fixture.

- [ ] Implement `ui::Announcer::post` calling `UiaRaiseNotificationEvent(provider, kind, processing, displayString, activityId)` on the window root, with `NotificationKind_ItemAdded`, `ItemRemoved`, `ActionCompleted`, `ActionAborted` and `Other`, and `NotificationProcessing_ImportantAll`, `ImportantMostRecent`, `All`, `MostRecent`, `CurrentThenMostRecent`. Done when: `Ui.AnnouncerRaises` records the exact parameters at a client. Cheaper substitute: always `ImportantAll`, which makes every message interrupt.
- [ ] Default every announcement to polite (`MostRecent`) and make `post` take an explicit `assertive` flag that only error dialogs may use, enforced by a `Ui.AnnouncerPolitenessAudit` source scan of call sites. Done when: the audit finds no `ImportantAll` call outside the error path.
- [ ] Guard with `UiaClientsAreListening()` and drop the call when no client is attached, and cap messages at `255` characters (longer ones truncated at a word boundary with the ellipsis). Done when: `Ui.AnnouncerNoListener` shows no UIA call without a listener and `Ui.AnnouncerLength` shows the cap.
- [ ] Guarantee announcements never change focus: posting leaves `ui::FocusManager::focused()` unchanged and raises no focus event. Done when: `Ui.AnnouncerKeepsFocus` posts while a `TextBox` has focus and finds the same focused element and no `AutomationFocusChanged` event.
- [ ] Add the `LiveSetting` property (`Off`, `Polite`, `Assertive`) to `ui::AutomationInfo` and expose `UIA_LiveSettingPropertyId` through `GetPropertyValue`. Done when: `Ui.UiaProperties` reads `LiveSetting` of an `InfoBar` as `Polite`.
- [ ] Raise `UiaRaiseAutomationEvent(UIA_LiveRegionChangedEventId)` when a live element's text or content changes, coalesced to one event per `100 ms`. Done when: `Ui.LiveRegionChanged` shows one event for ten rapid text updates.
- [ ] Mark `ui::InfoBar`, `ui::Toast` (`Polite`), the queue card status and error `ui::InfoBar` (`Assertive`) as live regions in their constructors in `src/ui/controls/`. Done when: `Ui.LiveRegionControls` reads the setting of each from the gallery.
- [ ] Provide the sink interface `ui::IAnnouncementSink` in `src/ui/a11y/announcement_sink.h` with only plain types, so `src/app` forwards `AnnouncementService` messages into it without `recto-services` including `recto-ui`. Done when: the header has no Windows or `recto-ui` type in its signatures and `Ui.AnnouncementSinkBoundary` compiles it in a library that links no `recto-ui` target.
- [ ] Add a headless test sink `ui::testing::AnnouncementLog` recording kind, processing, text and activity id for `D03 T02` tests. Done when: `Ui.AnnouncerRaises` and the D03 T02 announcement tests can read the log without a UIA client.
- [ ] Commit: `ui: UIA notification events and live regions with a polite default`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Ui.Announcer*:Ui.LiveRegion*:Ui.AnnouncementSinkBoundary` passes on the display-session host, quoting the recorded notification parameters.

## 5. High contrast: system colour mapping, live switching and high-contrast focus visuals

SPEC section 4 says the `HighContrast` dictionary maps every Recto brush to a system colour; WinUI applied it when Windows changed. `recto-ui` must do it itself: read `SPI_GETHIGHCONTRAST`, resolve each brush through the mapping that `D10 T01 §8` generates, repaint every window on the change, and make sure the result is usable, not merely recoloured: a focus ring that stays visible, selection and disabled states that are distinguishable, and a page canvas that does not invert documents. It must not break the Light and Dark themes (high contrast is a third mode of `ThemeManager`) and keeps SPEC section 11's rule that colour is never the only signal.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 4 (the `HighContrast` dictionary maps every brush to a system colour) and section 11 (focus visual, colour never the only signal); no screenshot exists, so the golden images for this section are captured from the control gallery in each Windows contrast theme and recorded under `tests/corpus/ui/contrast/`.
**Job:** A person who uses a Windows contrast theme opens Recto and sees every surface in their chosen colours, can always see where focus is, can tell selected, disabled and current items apart, and reads the document page unchanged. Consumer: the same person at runtime, and the gallery in `D10 T02 §23`.
**Treatment:** `ThemeManager` high contrast mode with a `SystemColorSource` over `GetSysColor`, the generated brush to `COLOR_*` table, a live re-resolve on `WM_SETTINGCHANGE` (`SPI_SETHIGHCONTRAST`) and `WM_THEMECHANGED`, and a high contrast variant of the focus visual and selection drawing. Cheaper substitute that fails the checkpoint: a fixed black-and-white palette, mapping brushes once at startup, or inverting the page bitmaps.
**Chrome:** consume `recto_tokens.g.h` (`D10 T01 §8`) and `ui::FocusManager` (`D10 T01 §13`); add no hex value to any control and no second brush table.

**Build order:**

1. Add `ui::SystemColorSource` (`src/ui/system_colors.h/.cpp`) and the high contrast mode to `ui::ThemeManager` (`src/ui/theme_manager.cpp`) resolving brushes through the generated table, and a live switch on `WM_SETTINGCHANGE`/`WM_THEMECHANGED`. Done when: `Ui.HighContrastResolve` returns the injected `COLOR_WINDOW` for `RectoSurfaceBrush`, and `Ui.HighContrastLiveSwitch` repaints an open window within one frame of the change.
2. Draw the high contrast focus, selection, disabled and page canvas treatments and capture golden images in the four Windows contrast themes. Done when: `Ui.HighContrastGallery` matches the goldens for Aquatic, Desert, Dusk and Night sky and `Ui.HighContrastSignals` finds a non-colour cue for each state.

- [ ] Implement `SystemColorSource` with a Win32 implementation (`GetSysColor` for `COLOR_WINDOW`, `WINDOWTEXT`, `HIGHLIGHT`, `HIGHLIGHTTEXT`, `HOTLIGHT`, `GRAYTEXT`, `BTNFACE`, `BTNTEXT`) and an injected test implementation, so tests never change the machine's real setting. Done when: `Ui.SystemColorSourceInjection` resolves from the injected palette. Cheaper substitute: calling `SystemParametersInfo(SPI_SETHIGHCONTRAST)` in tests, which changes the user's desktop.
- [ ] Store the four Windows 11 contrast theme palettes (Aquatic, Desert, Dusk, Night sky) as captured values in `tests/corpus/ui/contrast-themes.json`, read once from a real machine with each theme and recorded by `scripts/capture-contrast-palettes.ps1`. Done when: the JSON holds all eight `COLOR_*` values for four themes and `Ui.ContrastPalettesComplete` fails on a missing value.
- [ ] Add the high contrast mode to `ui::ThemeManager`: `SPI_GETHIGHCONTRAST` with `HCF_HIGHCONTRASTON` selects it, brush lookup resolves `RectoXBrush` through the generated `HighContrast` row to `SystemColorSource`, and unmapped brushes fail the build of `recto_tokens.g.h` rather than falling back to a literal. Done when: `Ui.HighContrastResolve` resolves every brush in `design/SPEC.md` section 4 and a brush with no mapping is a generator error (`D10 T01 §8`).
- [ ] Handle `WM_SETTINGCHANGE` with `lParam` equal to `"ImmersiveColorSet"` or `SPI_SETHIGHCONTRAST` and `WM_THEMECHANGED` in `ui::Window`, re-resolve the mode, invalidate the whole element tree and re-issue Mica or solid backdrop (`D10 T01 §4`) in every open window and popup. Done when: `Ui.HighContrastLiveSwitch` toggles the injected source Light, high contrast, Dark and back with the window open, and every window repaints with no restart.
- [ ] Draw the focus ring in high contrast as a 2 epx `COLOR_HIGHLIGHT` ring with a 1 epx `COLOR_WINDOW` inner ring that stays visible against `COLOR_WINDOW`, `COLOR_BTNFACE` and `COLOR_HIGHLIGHT` backgrounds, in `ui::FocusManager`'s visual (`src/ui/focus_visual.cpp`). Done when: `Ui.HighContrastFocusRing` measures a contrast ratio of at least 3:1 between ring and background in all four themes.
- [ ] Draw selected rows, selected tabs and toggled buttons as `COLOR_HIGHLIGHT` fill with `COLOR_HIGHLIGHTTEXT` text plus a 2 epx outline, and the current page and current search hit with a thick border, so state is carried by shape as well as colour (SPEC 11). Done when: `Ui.HighContrastSignals` shows selected, current, hovered, pressed and disabled controls differ in at least one non-colour attribute. Cheaper substitute: relying on the accent fill alone, which is lost when the accent maps to one system colour.
- [ ] Draw disabled controls with `COLOR_GRAYTEXT` text and a dashed or reduced-weight outline, and links with `COLOR_HOTLIGHT` plus an underline. Done when: `Ui.HighContrastDisabledAndLinks` finds the underline and the `GRAYTEXT` colour in the gallery.
- [ ] Keep the page canvas unchanged in high contrast: tiles render as authored (no inversion), the surround uses `COLOR_WINDOW`, page edges get a `COLOR_WINDOWTEXT` hairline, and the search hit and text selection overlays use `COLOR_HIGHLIGHT` with `COLOR_HIGHLIGHTTEXT`. Done when: `Ui.HighContrastCanvas` renders a fixture page and its pixels are bit-identical to the Light render inside the page rectangle.
- [ ] Capture golden images of the control gallery (`D10 T02 §23`) in each of the four contrast themes at 100 percent into `tests/corpus/ui/contrast/` and compare with the golden-image tool (`D10 T01 §20`). Done when: `Ui.HighContrastGallery` passes and fails when one brush mapping changes.
- [ ] Verify with a real machine step recorded for `§8`: switch Windows through the four contrast themes while Recto is open. Done when: the attended checklist of `§8` carries the result for each theme.
- [ ] Add `Ui.HighContrast*` tests in `tests/recto-tests/ui_high_contrast_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Ui.HighContrast*` passes and fails if the mapping table is replaced by literals.
- [ ] Commit: `ui: high contrast with live system colour mapping, focus ring and non-colour state cues`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Ui.HighContrast*:Ui.SystemColorSourceInjection:Ui.ContrastPalettesComplete` passes on the display-session host, quoting the contrast ratios measured in each theme.

## 6. Windows text scaling applied to the type ramp and layout

Windows has a Text size slider (Settings, Accessibility, Text size) from 100 to 225 percent that WinUI honoured through its text elements. `recto-ui` draws text itself, so it reads the factor, scales the SPEC section 5 ramp and relays out. It must not scale icons, hit targets beyond what Fluent does, or window chrome, and it must not clip: a larger text size grows rows, wraps or trims text and moves overflowing toolbar items into the overflow menu instead.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 5 (type ramp in epx), section 7 (hit targets and spacing grid) and section 11 (accessibility); no board draws large text, so the golden images are captured from the control gallery and the shell at each text scale into `tests/corpus/ui/textscale/`.
**Job:** A person with low vision sets Windows text size to 150 percent and every label, menu, dialog and setting in Recto grows to match without cut-off words or overlapping controls, and the change applies while Recto is open. Consumer: every text element in `recto-ui` and the layouts of `D02`.
**Treatment:** `ui::TextScale` service wrapping `winrt::Windows::UI::ViewManagement::UISettings::TextScaleFactor()` with the `TextScaleFactorChanged` event marshalled to the UI thread, multiplying the font size of each named style in `ui::TextStyle`, then a full relayout; fixed-size icons and 32 epx hit targets stay unscaled. Cheaper substitute that fails the checkpoint: scaling the whole window as a bitmap, scaling only `Body`, or scaling icons and buttons with the text.
**Chrome:** consume the type ramp (`D10 T01 §10`) and the layout engine (`D10 T01 §7`); no control reads the factor itself.

**Build order:**

1. Implement `ui::TextScale` (`src/ui/text_scale.h/.cpp`) with a Windows source and an injected test source, and multiply it into `ui::TextStyle` resolution (`src/ui/text_style.cpp`). Done when: `Ui.TextScaleRamp` shows each named style's resolved size at `1.0`, `1.5` and `2.25`.
2. Relayout live on change and prove the shell and gallery reflow without clipping at 100 to 225 percent. Done when: `Ui.TextScaleLiveChange` relayouts on an injected change and `Ui.TextScaleNoClipping` passes for the gallery at all six scales.

- [ ] Implement `ui::TextScale` reading `UISettings::TextScaleFactor()` (range `1.0` to `2.25`) through C++/WinRT at startup, with an injectable `TextScaleSource` for tests. Done when: `Ui.TextScaleSource` reads an injected `1.5` and a real value of `1.0` or more on the host. Cheaper substitute: reading the registry value `HKCU\Software\Microsoft\Accessibility\TextScaleFactor`, which is an undocumented path.
- [ ] Subscribe to `UISettings::TextScaleFactorChanged` (raised on a non-UI thread) and post the new factor to the UI thread through `ui::Dispatcher`, coalescing bursts during slider drag, then invalidate layout of every window. Done when: `Ui.TextScaleLiveChange` changes the injected factor from a worker thread and the window relayouts on the UI thread within one frame.
- [ ] Multiply the factor into every named style of the ramp in `ui::TextStyle` (`Caption` 12, `Body` 14, `BodyStrong` 14, `Subtitle` 20, `Title` 28, `TitleLarge` 40, `GroupLabel` 12), scaling line height with it and rounding to whole device pixels at the window's DPI. Done when: `Ui.TextScaleRamp` checks all seven styles at `1.0`, `1.25`, `1.5`, `1.75`, `2.0` and `2.25`.
- [ ] Keep `ui::Icon`, caption buttons, scrollbars, focus rings and the 32 epx and 36 epx minimum hit targets of SPEC section 7 at their unscaled size, while letting a row grow taller than the minimum when its text needs it. Done when: `Ui.TextScaleKeepsIcons` finds identical icon and caption button sizes at `1.0` and `2.0` and a `ui::Button` with a 28 epx label growing its height. Cheaper substitute: scaling icon glyphs, which breaks the pixel-snapped 16 epx icon set.
- [ ] Make text elements wrap or trim with an ellipsis instead of clipping (`ui::TextBlock` wrap and `Trimming` per style), and make `ui::TabStrip`, `ui::Menu` and the toolbar move items that no longer fit into an overflow menu. Done when: `Ui.TextScaleNoClipping` finds no `TextBlock` whose laid-out text exceeds its arranged bounds without trimming, and the toolbar overflow lists the displaced commands.
- [ ] Verify the dialog, Settings and command box layouts: dialogs grow their height up to the window and scroll their body past it, Settings cards reflow to one column under `960 epx` of content, and the command box keeps its row height from the scaled `Body` style. Done when: `Ui.TextScaleDialogsAndSettings` passes at `2.25` in a `960 x 600 epx` window.
- [ ] Apply the factor to the DirectWrite layouts of the page canvas chrome only (page number box, status text), never to the document's own text. Done when: `Ui.TextScaleDocumentUntouched` renders a page at `2.0` with unchanged pixels.
- [ ] Capture golden images of the shell (Focus preset) and the gallery at `1.0`, `1.5` and `2.25` into `tests/corpus/ui/textscale/` and compare. Done when: `Ui.TextScaleGallery` passes and fails when a ramp style ignores the factor.
- [ ] Add `Ui.TextScale*` tests in `tests/recto-tests/ui_text_scale_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Ui.TextScale*` passes.
- [ ] Commit: `ui: Windows text scaling for the type ramp with live relayout and overflow instead of clipping`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Ui.TextScale*` passes on the display-session host, quoting the resolved ramp at `2.25` and the zero-clipped-element count for the gallery.

## 7. `Strings`: localized string tables, fallback and pseudo-localization

WinUI used `.resw` files and MRT Core to pick a language; both leave with the Windows App SDK. `recto-ui` uses the Win32 standard instead: `STRINGTABLE` resources compiled into `Recto.exe`, one `.rc` per language under `src/app/strings/`, loaded explicitly by language id. This section builds `ui::Strings`, the selection by the Language setting then the Windows display language, the English fallback, a generated pseudo-locale for layout tests, a check that every id exists in every shipped language, and the layout mirroring that right-to-left languages need. The release plan answers the right-to-left question: `D07 T02 §16` ships right-to-left readiness (no right-to-left language in the first set of ten), so mirroring is in scope here as the engine capability, with the first right-to-left language a translation task. Default chosen: `Win32 STRINGTABLE`, because it needs no runtime and is the Win32 standard; cost of changing later is one loader and a resource conversion script. This section must not break `recto-cli` (it keeps English, stable output) or `recto-core` (no UI strings in engine libraries).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.7 (the `Language` row, "Follow Windows first, then each shipped language by its native name") and section 5 (the type ramp the lengthened strings must fit); the wording of every string stays that of the SPEC and the screens, so no board changes.
**Job:** A user who runs Windows in German, or picks German in Settings, sees Recto in German, and a translator can add a language by adding one `.rc` file and a reviewed `languages.json` entry. Consumer: every UI string in `src/app` and `src/ui`, and `D07 T02 §14`.
**Treatment:** `ui::Strings::get(StringId)` over `FindResourceExW(RT_STRING, block, MAKELANGID(...))` per language, a generated `string_ids.g.h` from the English `.rc`, the selection chain `UiLanguage` setting then `GetUserPreferredUILanguages` then `en-US`, `scripts/generate-pseudolocale.py` for `qps-ploc`, and `scripts/check-strings.py` for id completeness; mirroring through a `ui::FlowDirection` property resolved in measure and arrange. Cheaper substitute that fails the checkpoint: string literals in C++, a `std::map` of translations built at startup, or mirroring the whole window including the PDF page.
**Chrome:** no visible chrome of its own; consume `RectoIcons` metadata for directional glyphs (`D02 T01 §18`, `D10 T01 §11`) and the layout panels (`D10 T01 §7`).

**Build order:**

1. Define the `.rc` layout and ids, generate `string_ids.g.h` and implement `ui::Strings` (`src/ui/strings.h/.cpp`) with selection and fallback. Done when: `Ui.StringsSelection` returns the German string for `de-DE`, the English string for an unshipped tag and for a string missing in German.
2. Add `scripts/check-strings.py`, `scripts/generate-pseudolocale.py` and the layout survival test over the gallery. Done when: `python scripts/check-strings.py` exits `0` and `Ui.PseudolocaleLayout` finds no clipped text.
3. Add flow direction to the layout engine and the icon flip rule, then the right-to-left layout tests. Done when: `Ui.RtlLayoutMirror` mirrors `StackPanel`, `Grid` and `DockPanel` and `Ui.RtlIconAudit` passes.

- [ ] Create `src/app/strings/en-US.rc` as the source language with `LANGUAGE LANG_ENGLISH, SUBLANG_ENGLISH_US`, one `STRINGTABLE` of `IDS_<AREA>_<NAME>` ids with explicit numeric ranges per area (shell 1000s, dialogs 2000s, settings 3000s, convert 4000s, organize 5000s, errors 6000s, accessibility 7000s), and plural keys as separate ids `IDS_X_ONE` and `IDS_X_OTHER` per `D07 T02 §13`. Done when: `rc.exe` compiles it and `scripts/check-strings.py --ids` lists every id once. Cheaper substitute: ids assigned by hand in several files, which collide.
- [ ] Generate `src/app/strings/string_ids.g.h` from `en-US.rc` with `scripts/build-string-ids.py` (stdlib Python, `--check` in CI), so the English file is the one source of ids. Done when: `python scripts/build-string-ids.py --check` exits `0` and fails on a hand edit of the header.
- [ ] Implement `ui::Strings::get(StringId)` returning `std::wstring_view` into the resource data (via `LockResource` of the `RT_STRING` block, no copy) and `format(StringId, args...)` with `{0}` placeholders, in `src/ui/strings.h/.cpp`. Done when: `Ui.StringsGetAndFormat` returns the text and formats `"{0} of {1}"` as `"2 of 14"`.
- [ ] Implement language selection in `ui::Strings::select(UiLanguage)`: a shipped tag from the setting wins, `FollowWindows` walks `GetUserPreferredUILanguages(MUI_LANGUAGE_NAME)` and takes the first tag that matches a shipped language exactly or by primary subtag (`de-AT` to `de-DE`), otherwise `en-US`. Done when: `Ui.StringsSelection` covers setting, system chain, primary-subtag match and the English fallback.
- [ ] Fall back per string: when a shipped language lacks an id (a draft not yet complete), return the English string and record the id in a debug-only `missing` list instead of an empty string. Done when: `Ui.StringsMissingFallsBack` removes one id from a test language and finds English plus one `missing` entry.
- [ ] Make the language fixed per process at startup before the first window (restart on change, `D07 T02 §14`), accept `--language <tag>` for testing, and keep `recto-cli` on English. Done when: `Ui.StringsStartupLanguage` starts with `--language de-DE` and reads German, and `recto-cli --help` output is English under a German setting.
- [ ] Write `scripts/check-strings.py` verifying every id in `en-US.rc` exists in every `reviewed` language of `src/app/strings/languages.json`, no language defines an id the English file lacks, placeholders `{0}` match in count and index, and no id appears twice. Done when: `python scripts/check-strings.py` exits `0` and a planted missing id, extra id and wrong placeholder each exit `2` in `tests/scripts/test_check_strings.py`.
- [ ] Write `scripts/generate-pseudolocale.py` emitting `src/app/strings/qps-ploc.rc` from `en-US.rc` with accented characters, about 40 percent length growth and `[! ... !]` brackets, leaving `{0}` placeholders intact and `--check` verifying it is current. Done when: the script exits `0` with the same id set as English and `tests/scripts/test_generate_pseudolocale.py` verifies a placeholder survives. Cheaper substitute: padding with spaces, which does not exercise wrapping or glyph height.
- [ ] Add `Ui.PseudolocaleLayout` in `tests/recto-tests/ui_strings_tests.cpp`: render the gallery and the Home, Settings, Convert and About screens headless under `qps-ploc` and assert no element's text is clipped without a trimming marker and no two siblings overlap. Done when: the test passes and fails when a fixed-width label is planted. Source: `D07 T02 §5`.
- [ ] Add `ui::FlowDirection` (`LeftToRight`, `RightToLeft`, inherited) to `ui::Element` (`src/ui/element.h`), set at the window root from `languages.json` `direction`, and pin the page canvas, thumbnails and the page number box to `LeftToRight`. Done when: `Ui.RtlFlowInherit` finds the shell right to left and the canvas left to right.
- [ ] Mirror the layout panels in arrange when flow direction is right to left: `StackPanel` horizontal order reverses, `Grid` column tracks run from the right, `DockPanel` `Left` and `Right` swap, `WrapPanel` fills from the right, and `OverlayPanel` slots mirror horizontally, all by flipping the arranged rectangle inside the parent so hit testing, painting and UIA bounds agree. Done when: `Ui.RtlLayoutMirror` compares each panel's arranged rectangles with the mirror of the left-to-right ones for the same content, in a 3 by 3 fixture.
- [ ] Mirror text and input: DirectWrite reading direction set from the flow direction for UI text (runs of the other direction stay bidirectional through the Unicode algorithm), arrow keys in arrow groups and sliders swap `Left` and `Right`, and caption buttons mirror to the left like Windows right-to-left windows. Done when: `Ui.RtlTextAndKeys` checks reading direction, the swapped arrows in a `Slider` and a toolbar, and the caption button rectangles.
- [ ] Add the icon flip rule: a `RectoIcons` glyph flips in right-to-left only when marked `mirror_in_rtl` in the glyph map (back, forward, previous, next, panel toggles, list indent), never for search, print, zoom, clock or media glyphs, and `ui::Icon` applies a horizontal transform. Done when: `Ui.RtlIconAudit` lists each flagged glyph, finds no flag on a non-directional one and renders the flipped pixels of `back`.
- [ ] Add `Ui.Strings*` and `Ui.Rtl*` tests, including a mirrored fixture pseudo-locale generated by `D07 T02 §16`'s extension when present. Done when: `recto-tests.exe --gtest_filter=Ui.Strings*:Ui.Pseudolocale*:Ui.Rtl*` passes and `python -m unittest discover -s tests/scripts -p "test_check_strings.py"` passes.
- [ ] Commit: `ui: Win32 string tables, language selection, pseudo-locale and right-to-left layout mirroring`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Ui.Strings*:Ui.Pseudolocale*:Ui.Rtl*` passes on the display-session host and `python scripts/check-strings.py` exits `0`, quoting the shipped language list and the zero-missing-id count.

## 8. Accessibility verification: UIA tree walk, keyboard-only traversal, Narrator smoke

Each section above proves its own piece. This section proves the whole: that the control gallery presents the tree, names and patterns a screen reader expects, that every control can be reached and operated with the keyboard alone, that Narrator reads it, and that Accessibility Insights finds no failures. It must not weaken any earlier test to pass, and its Narrator and Accessibility Insights steps are attended, because they need a person to hear and judge.

**Phase:** 2
**Needs:** windows-host, display-session, attended
**Surface:** no

**Build order:**

1. Write the expected tree for the control gallery as `tests/corpus/ui/gallery-uia-expected.json` and the walker test `Ui.A11yGalleryTreeWalk` in `tests/recto-tests/ui_a11y_walk_tests.cpp`. Done when: the walker compares role, name and pattern set for every gallery element and fails when one name changes.
2. Add the keyboard-only traversal test `Ui.A11yKeyboardTraversal` and the attended Narrator and Accessibility Insights checklist with a results file. Done when: the traversal reaches every focusable control with no trap, and `docs/reviews/10-ui/accessibility-verification.md` carries the Narrator and FastPass results.

- [ ] Author `tests/corpus/ui/gallery-uia-expected.json` listing, for every control instance in the gallery (`D10 T02 §23`) in Light, the expected `ControlType`, `Name`, `AutomationId`, parent and required patterns. Done when: `Ui.A11yExpectedTreeComplete` finds one entry per gallery element and no entry without a gallery element.
- [ ] Implement `Ui.A11yGalleryTreeWalk` walking the live gallery with `IUIAutomation::GetRootElement`/`ElementFromHandle` and `TreeWalker`, comparing against the JSON and printing a diff per mismatch. Done when: it passes and fails when a button's `Name` is cleared. Cheaper substitute: asserting only the element count.
- [ ] Assert across the gallery that every icon-only control has a non-empty `Name` and a tooltip-derived `HelpText`, every input has a label (`LabeledBy` or `Name`), every list item has a `Name`, and no two siblings with the same role share an `AutomationId`. Done when: `Ui.A11yNamesAndLabels` reports `0` violations and reports a planted unnamed icon button.
- [ ] Repeat the tree walk in high contrast (`§5` injected source) and at text scale `2.0` (`§6`) and assert the tree is unchanged apart from bounding rectangles. Done when: `Ui.A11yTreeStableAcrossModes` passes in both modes.
- [ ] Implement `Ui.A11yKeyboardTraversal`: with synthetic key input, press `Tab` from the first control through the whole gallery and `Shift+Tab` back, assert every focusable element is reached exactly once per cycle in logical order, focus never gets trapped (a cycle returns to the start or leaves the window) and the focus ring is drawn for keyboard focus only. Done when: the test passes and fails when a planted element sets `Tab` handling that swallows the key.
- [ ] Operate every control by keyboard in `Ui.A11yKeyboardOperate`: `Space` and `Enter` invoke buttons and toggles, arrows move within toolbars, lists, tree, tabs, sliders and radio groups, `Esc` dismisses menus, flyouts and dialogs and returns focus to the opener, and `F6` or the SPEC section 10 order moves between landmarks. Done when: the test performs each action and compares the control state, and the `Dialog` returns focus on close.
- [ ] Cross-check pointer-only affordances: every control reachable by pointer has a keyboard route or a `Ctrl+K` command (`AGENTS.md` interface rules), listed in `docs/reviews/10-ui/keyboard-coverage.md` with any exception owned by a named section. Done when: `Ui.A11yNoPointerOnlyControl` finds zero controls without a route.
- [ ] Write the Narrator smoke script `docs/reviews/10-ui/narrator-smoke.md` (start Narrator with `Win+Ctrl+Enter`, open the gallery, move with `Caps Lock+Right`, activate a button, read a `TextBox`, toggle a switch, open a dialog, hear a polite announcement from `§4`, switch to each contrast theme) with an expected speech line for each step. Done when: the document lists at least 20 steps each with an expected result. Cheaper substitute: asking whether Narrator "seems fine".
- [ ] Run the Narrator smoke with a person present on the display-session host and record each step as pass or fail with the machine and Windows build in `docs/reviews/10-ui/accessibility-verification.md`. Done when: the file records every step, with any failure filed as a defect in `todo/findings.md` and fixed or owned before stamping.
- [ ] Run Accessibility Insights for Windows FastPass against the gallery and the shell and save the exported result as `build/a11y/fastpass.json` with a summary in `docs/reviews/10-ui/accessibility-verification.md` (automated checks failed: `0`; tab stops, keyboard and contrast steps passed). Done when: the summary records the tool version and zero failing automated checks.
- [ ] Run the attended contrast and text size checks from `§5` and `§6` (all four contrast themes, Text size 100 to 225 percent) and record the result per setting. Done when: the file has a row for each of the four themes and six text sizes.
- [ ] Add `scripts/check-a11y-record.py` verifying the results file contains the Narrator, FastPass, contrast and text size rows with a date and tool versions, and wire it into `Verification`. Done when: `python scripts/check-a11y-record.py` exits `0` and exits `2` when a row is missing in `tests/scripts/test_check_a11y_record.py`.
- [ ] Commit: `ui: accessibility verification with UIA gallery walk, keyboard traversal, Narrator and FastPass record`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Ui.A11y*` passes on the display-session host and `python scripts/check-a11y-record.py` exits `0`, quoting the gallery element count, the zero-violation counts and the Narrator and FastPass result lines.

## Verification

- `recto-tests.exe --gtest_filter=Ui.Uia*:Ui.Text*:Ui.Announcer*:Ui.LiveRegion*:Ui.HighContrast*:Ui.TextScale*:Ui.Strings*:Ui.Pseudolocale*:Ui.Rtl*:Ui.A11y*` passes under ASan on the display-session host.
- `python scripts/check-strings.py`, `python scripts/build-string-ids.py --check`, `python scripts/generate-pseudolocale.py --check` and `python scripts/check-a11y-record.py` exit `0`.
- The control gallery tree walk, keyboard-only traversal, Narrator smoke and Accessibility Insights FastPass results are recorded in `docs/reviews/10-ui/accessibility-verification.md`, with all four contrast themes and text sizes 100 to 225 percent checked.
- `D03 T02 §1` implements `ui::ITextSource` for `PageText` and passes `Ui.TextSourceConformance`; `D03 T02 §5` announces through `ui::IAnnouncementSink`.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
