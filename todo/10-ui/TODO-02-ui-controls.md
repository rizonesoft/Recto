---
schema_version: 1
id: ui-controls
domain: 10-ui
status: active
title: "TODO-02 -- recto-ui Controls: the Fluent Control Set the SPEC Uses"
track: M2
---

# TODO-02 -- recto-ui Controls: the Fluent Control Set the SPEC Uses

> **Goal:** `recto-ui` has every Fluent (WinUI 3) control that `design/SPEC.md` uses, built in `src/ui/controls/` on the foundation of `D10 T01`: a `Control` base with visual states and token styles, buttons, tooltips, text input with Text Services Framework, password and number inputs, check, radio and switch, slider and progress, combo box, scroll viewer with DirectManipulation, virtualized items view and tree, title-bar tab strip, menus and menu bar, flyout and card, dialog, segmented and mode switchers, navigation rail and settings cards, info bar, toast and pills, search box and colour swatches. Each control reproduces the Fluent look and behaviour at the SPEC metrics, in Light, Dark and HighContrast, proven by headless golden images and a control gallery.

> **Current state:** Measured 2026-10-22. `src/ui/` does not exist yet: `recto-ui` is created by `D10 T01 §1`. The only UI code is the WinUI 3 scaffold in `src/app/` (`App.xaml`, `MainWindow.xaml`) that `D10 T01 §2` retires, so every control here replaces a WinUI stock control that no screen uses yet. `design/SPEC.md` (sections 4 to 12), the board screenshots `design/screenshots/D-*.png` and the HTML mockups in `design/mockups/html/` hold the metrics. `resources/fonts/RectoIcons.ttf` does not exist yet (built by `D02 T01 §18`), and the generated token header comes from `D10 T01 §8`.

## Inputs

- `docs/specs/2026-10-22-recto-ui-toolkit-design.md` sections 4 (architecture), 5 (the WinUI to recto-ui mapping) and 6 (this file's section list).
- `design/SPEC.md` sections 4 (colour), 5 (typography), 6 (icons), 7 (shape, spacing, elevation), 8 to 9.9 (screens and dialogs), 10 (keyboard), 11 (accessibility) and 12 (motion), with `design/screenshots/D-*.png` and the sizes in `design/mockups/html/`.
- -> XREF: D10 T01 §1 -- the `recto-ui` library, module boundary and headless render harness that every golden test here uses.
- -> XREF: D10 T03 §2 -- the UIA pattern providers wrap the automation hooks these controls expose, after the gallery of §23.
- -> XREF: D10 T03 §1 -- the UIA fragment tree reads the automation name, role and bounds that `Control` of §1 carries.
- -> XREF: D10 T03 §3 -- the text-range adapter wraps `TextBox` of §4.
- -> XREF: D10 T04 §1 -- moves this library to `libs/vellum` as `vellum::`; `§3` there supplies the neutral test theme the gallery renders against.
- -> XREF: D10 T03 §4 -- notification and live-region announcements are raised by the toast, info bar and search box of §20 and §21.
- -> XREF: D02 T01 §19 -- the `RectoChip` variants build on the pill primitives of §20.
- -> XREF: D02 T01 §3 -- the document tabs use the `TabStrip` of §13.
- -> XREF: D02 T01 §9 -- the Classic menu bar uses the `MenuBar` of §15.
- -> XREF: D02 T01 §10 -- the shell dialogs (Password, Could not open, Replace original, Unsaved changes) use the `Dialog` of §17.
- -> XREF: D02 T01 §4 -- the Classic navigation rail and the Reading room mode switcher use §19 and §18.
- -> XREF: D02 T03 §12 -- the command box uses the `SearchBox` of §21.
- -> XREF: D02 T04 §4 -- the Settings page uses the `SettingsGroup` and `SettingsCard` of §19.
- -> XREF: D02 T02 §6 -- `TODO-02-document-view.md` consumes sections of this file (first use in the section named).
- -> XREF: D02 T05 §4 -- `TODO-05-rich-media-3d-geo.md` consumes sections of this file (first use in the section named).
- -> XREF: D02 T06 §3 -- `TODO-06-cloud-storage-connectors.md` consumes sections of this file (first use in the section named).
- -> XREF: D03 T01 §4 -- `TODO-01-selection-and-search.md` consumes sections of this file (first use in the section named).
- -> XREF: D03 T02 §3 -- `TODO-02-accessibility.md` consumes sections of this file (first use in the section named).
- -> XREF: D03 T03 §3 -- `TODO-03-annotations-and-forms.md` consumes sections of this file (first use in the section named).
- -> XREF: D03 T04 §11 -- `TODO-04-edit-pdf.md` consumes sections of this file (first use in the section named).
- -> XREF: D03 T05 §7 -- `TODO-05-multi-file-search.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T01 §3 -- `TODO-01-jobs-and-queue.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T02 §1 -- `TODO-02-pdf-conversions-a.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T03 §1 -- `TODO-03-pdf-to-word.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T04 §1 -- `TODO-04-office-to-pdf.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T05 §1 -- `TODO-05-excel-web-and-compliance.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T06 §1 -- `TODO-06-print-production.md` consumes sections of this file (first use in the section named).
- -> XREF: D04 T07 §1 -- `TODO-07-action-wizard.md` consumes sections of this file (first use in the section named).
- -> XREF: D05 T02 §1 -- `TODO-02-organize-ui.md` consumes sections of this file (first use in the section named).
- -> XREF: D06 T03 §3 -- `TODO-03-user-failure-gates.md` consumes sections of this file (first use in the section named).
- -> XREF: D06 T04 §1 -- `TODO-04-design-boards.md` consumes sections of this file (first use in the section named).
- -> XREF: D07 T01 §10 -- `TODO-01-packaging-and-print.md` consumes sections of this file (first use in the section named).
- -> XREF: D07 T02 §2 -- `TODO-02-store-and-compliance.md` consumes sections of this file (first use in the section named).
- -> XREF: D07 T03 §2 -- `TODO-03-listening-to-users.md` consumes sections of this file (first use in the section named).
- -> XREF: D08 T01 §15 -- `TODO-01-ai-assistant.md` consumes sections of this file (first use in the section named).
- -> XREF: D08 T02 §10 -- `TODO-02-assistant-workspace.md` consumes sections of this file (first use in the section named).

## Outcome

- Every control the SPEC names exists in `src/ui/controls/` with all its visual states in Light, Dark and HighContrast, keyboard behaviour, an automation hook, a tooltip for icon-only use and a headless golden test.
- `recto-ui-gallery` and the `Ui.Gallery.*` tests render every control in every state at three scales and three themes against stored golden images, and fail for a control that is not registered.
- No control sets a colour or corner radius literal: colours come from the generated token header and radii from the SPEC section 7 tokens.

**Adjacency:** all=not-applicable (a UI toolkit control library: it owns no records, lists, reports, notifications, permissions, audit trail, exchange formats or undo of its own; screens that use these controls declare those surfaces)

**Adjacency rationale:** This file builds reusable controls, not a user-facing feature with data. The list, document, settings, reporting, notification, permission, audit, exchange and reverse surfaces belong to the screens in D02 to D05 that place these controls; the control-level obligations (selection models, find-a-setting filtering, toast announcements, undo inside a text box) are owned and tested here.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `Control` base: visual states, tokens, automation name, tooltip and command hooks | D10 T01 §9, D10 T01 §10, D10 T01 §13, D10 T01 §16 | [ ] |
| 2 | §2 | Buttons: standard, accent, danger, icon, toggle and hyperlink | §1, D10 T01 §11 | [ ] |
| 3 | §3 | `ToolTip` with name and shortcut text | §1, D10 T01 §15 | [ ] |
| 4 | §4 | `TextBox` editing core: caret, selection, undo, clipboard and placeholder | §1, D10 T01 §17 | [ ] |
| 5 | §5 | Text Services Framework for `TextBox`: IME, dictation and touch keyboard | §4 | [ ] |
| 6 | §6 | `PasswordBox` and `NumberBox` | §4 | [ ] |
| 7 | §7 | `CheckBox`, `RadioButton` group and `ToggleSwitch` | §1 | [ ] |
| 8 | §8 | `Slider`, `ProgressBar` and `ProgressRing` | §1, D10 T01 §14 | [ ] |
| 9 | §9 | `ComboBox` and `DropDownButton` | §2, D10 T01 §15 | [ ] |
| 10 | §10 | `ScrollViewer`: overlay scrollbars, smooth wheel, DirectManipulation pan and pinch | §1, D10 T01 §14 | [ ] |
| 11 | §11 | Virtualized `ItemsView`: stack, filmstrip and grid layouts, selection, reorder hooks | §10 | [ ] |
| 12 | §12 | `TreeView`: expand, collapse, keyboard, selection bar and drag reorder | §11 | [ ] |
| 13 | §13 | `TabStrip`: title-bar document tabs, close, add, overflow and drag reorder | §2, §10 | [ ] |
| 14 | §14 | `Menu` and context menu: glyphs, shortcuts, separators, submenus and check items | §2, D10 T01 §15 | [ ] |
| 15 | §15 | `MenuBar` with access keys and Alt activation | §14 | [ ] |
| 16 | §16 | `Flyout` popover and the floating `Card` container | §1, D10 T01 §14, D10 T01 §15 | [ ] |
| 17 | §17 | `Dialog`: modal overlay, smoke, alert and form layouts and Fluent motion | §2, §16 | [ ] |
| 18 | §18 | `SegmentedControl` and the underlined mode switcher | §2 | [ ] |
| 19 | §19 | `NavigationRail`, `Expander`, `SettingsGroup` and `SettingsCard` | §2, §7 | [ ] |
| 20 | §20 | `InfoBar`, in-app `Toast`, and the `Chip`, `Badge` and `Keycap` pills | §2, D10 T01 §14 | [ ] |
| 21 | §21 | `SearchBox` with a suggestion list for the command box and Find a setting | §4, §11, D10 T01 §15 | [ ] |
| 22 | §22 | `ColorSwatchPicker` from token swatches | §16 | [ ] |
| 23 | §23 | Control gallery: every control in Light, Dark and HighContrast with golden images | §2, §3, §4, §5, §6, §7, §8, §9, §10, §11, §12, §13, §14, §15, §16, §17, §18, §19, §20, §21, §22, §24, §25, §26, §27, D10 T01 §20, D10 T04 §3 | [ ] |
| 24 | §24 | Spell checking for `TextBox` through the Windows Spell Checking API | §4, §14 | [ ] |
| 25 | §25 | `CalendarFlyout` and `DatePicker` with locale-aware weeks and formats | §11, §16 | [ ] |
| 26 | §26 | `Splitter` for resizable panes with pointer, keyboard and limits | §1 | [ ] |
| 27 | §27 | Custom colour entry: hex field, live preview and recent colours | §4, §22 | [ ] |

## 1. `Control` base: visual states, tokens, automation name, tooltip and command hooks

`ui::Control` in `src/ui/controls/control.h` and `control.cpp` is the base every control in this file derives from. It replaces the WinUI `Control`, `VisualStateManager` and `Style`: a control holds a `VisualState` set (rest, hover, pressed, disabled, focused, selected, toggled), resolves its colours from the generated token structs of `D10 T01 §8` for the active theme (Light, Dark, HighContrast), draws radii only from the token radii of SPEC section 7 (4 epx control, 8 epx overlay, pill), paints the Fluent focus visual of `D10 T01 §13` for keyboard focus only, and carries an automation name, help text and tooltip text that `D10 T03 §1` reads. It also owns the `ui::Command` hook of `D10 T01 §16` so a control invokes a command and follows its can-execute state. Pointer and keyboard routing come from `D10 T01 §12`. Style structs are plain C++ data, not markup, so a state table is one place to read and one place to test.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 4, 7, 11 and 12 with `design/screenshots/D-Focus-Light.png` (rest, hover and toggled button states) and `D-Dialogs.png`.
**Job:** A control author declares states and style once; every screen that places a control gets SPEC colours, radii, focus ring and accessibility data without repeating them. Consumers: every section in this file and every screen in `src/app`.
**Treatment:** One `Control` base with a state machine and style structs resolved from tokens. Cheaper substitute that fails the checkpoint: each control reading brushes itself, or a Light-only state table.
**Chrome:** consume the generated token header (`recto_tokens.g.h`) from `D10 T01 §8`, `ui::Painter` radii and elevation from `D10 T01 §9`, the focus visual of `D10 T01 §13`, `ui::Command` from `D10 T01 §16`.

**Build order:**

1. Create `src/ui/controls/control.h` and `control.cpp` with `ui::Control`, the `VisualState` flags and a `StyleResolver` that maps a state to token brushes per theme. Done when: `Ui.Controls.Control.StateResolution` resolves every state to a non-null token in Light, Dark and HighContrast.
2. Add automation name, help text, access key and tooltip text properties and the `bind_command(ui::Command&)` hook with can-execute following. Done when: disabling the bound command disables the control and re-enabling restores the previous state.
3. Add `Ui.Controls.Control.*` tests in `tests/recto-tests/ui/control_test.cpp`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.Control.*` passes.

- [ ] Create the class `ui::Control` in `src/ui/controls/control.h` deriving from `ui::Element` with pointer and keyboard state tracking (hover, pressed, focused, captured). Done when: a synthetic pointer enter, press, release and leave sequence produces the state sequence rest, hover, pressed, hover, rest. Cheaper substitute: tracking hover in each control separately.
- [ ] Define `ui::ControlStyle` structs in `src/ui/controls/control_style.h` (background, border, foreground, glyph, radius, padding per state) filled from the token header, never from literals. Done when: `Ui.Controls.Control.NoLiterals` fails if any colour literal appears under `src/ui/controls/`. Cheaper substitute: a lint comment instead of a test.
- [ ] Resolve HighContrast by the token-to-system-colour map of `D10 T01 §8` so every state is distinguishable without colour alone (SPEC 4 and 11). Done when: in HighContrast the focused, selected and disabled crops each differ from rest by border or fill.
- [ ] Draw the Fluent focus visual (2 epx accent outer ring, 1 epx inner ring in the surface colour) only when focus came from the keyboard (SPEC 11, `D10 T01 §13`). Done when: a mouse click leaves no ring and a Tab press shows it. Cheaper substitute: always drawing the ring.
- [ ] Add the disabled behaviour: no input events, `RectoTextSecondaryBrush` foreground at the token disabled alpha, not focusable. Done when: a disabled control ignores press and is skipped by `FocusManager` Tab traversal.
- [ ] Add the `tooltip_text` and `shortcut_text` properties that `§3` shows, and require a name for icon-only controls (debug assertion). Done when: constructing an icon-only control without an automation name asserts in Debug and the gallery lists none.
- [ ] Expose the automation hook `Control::automation()` returning role `per subclass` and the bound command name through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.Control.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Add the headless golden test `Ui.Controls.Control.Golden` in `tests/recto-tests/ui/control_golden_test.cpp` rendering a probe control in every state at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.Control.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: Control base with visual states, token styles, automation and command hooks`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.Control.*` passes and the probe control's golden crops are identical to the stored images in all three themes.

## 2. Buttons: standard, accent, danger, icon, toggle and hyperlink

`src/ui/controls/button.h` and `button.cpp` build the `ui::Button` family that replaces WinUI `Button`, `AccentButtonStyle`, `ToggleButton`, `AppBarButton` and `HyperlinkButton`. Standard buttons are 32 epx tall with 4 epx radius, `RectoBorderStrongBrush` border and `RectoRaisedBrush` or `RectoSurfaceBrush` fill, body text 14 epx. The accent button fills with `RectoAccentBrush` (hover `RectoAccentHoverBrush`, text `RectoOnAccentBrush`), used for Convert, Open, Save and Browse files. The danger variant (Apply Redactions, SPEC 8.2 item 3) uses `RectoDangerBrush`. The icon button is 36 epx square in toolbars and mode bars and never below 32 epx elsewhere (SPEC 11), with a 16 epx `ui::Icon` glyph from RectoIcons in `RectoTextSecondaryBrush`, `RectoTextBrush` on hover. The toggle button, when on, uses `RectoAccentTintBrush` with the Filled glyph in `RectoAccentBrush` (SPEC 6 and 8.2); that background is the control's state, not an icon tile. The hyperlink button draws `RectoAccentTextBrush` text with underline on hover. Press and release use the Fluent 83 ms fade (SPEC 12).

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6, 7, 8.2, 9.9 and 11 with `design/screenshots/D-Focus-Light.png` (toolbar buttons, accent Convert, toggled panel button) and `D-Dialogs.png` (dialog buttons).
**Job:** The user clicks, toggles or follows a link on every screen. Consumers: floating toolbar and mode bars (D02 T01 and D03), dialogs (`§17`), Home, Convert, Organize, Settings.
**Treatment:** Stock-equivalent Fluent buttons with the SPEC accent, 36 epx toolbar hit targets and tint toggles. Cheaper substitute that fails the checkpoint: one button class with per-screen colours, 28 epx icon buttons, or a toggled state drawn as a filled tile behind the glyph.
**Chrome:** `RectoRaisedBrush`, `RectoSurfaceBrush`, `RectoBorderStrongBrush`, `RectoAccentBrush`, `RectoAccentHoverBrush`, `RectoOnAccentBrush`, `RectoAccentTintBrush`, `RectoAccentTextBrush`, `RectoDangerBrush`, `ui::Icon`, `Control` of `§1`.

**Build order:**

1. Create `src/ui/controls/button.h` and `button.cpp` with `ui::Button`, `AccentButton`, `DangerButton`, `IconButton`, `ToggleButton` and `HyperlinkButton`. Done when: `Ui.Controls.Button.Variants` constructs each and reports its role and default metrics.
2. Implement press, release and `Enter` or `Space` activation, toggle semantics and the `Invoke` event bound to `ui::Command`. Done when: a synthetic click and a `Space` press each fire exactly once and toggle flips `checked`.
3. Add `Ui.Controls.Button.*` golden and metric tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.Button.*` passes.

- [ ] Create the standard `ui::Button` in `src/ui/controls/button.cpp` at 32 epx height, 12 epx horizontal padding, 4 epx radius, 1 epx `RectoBorderStrongBrush` border and Body text (SPEC 5 and 7). Done when: `Ui.Controls.Button.StandardMetrics` measures 32 epx height and 4 epx radius at 100 and 200 percent. Cheaper substitute: measuring only at 100 percent.
- [ ] Add `AccentButton` with `RectoAccentBrush` fill, hover `RectoAccentHoverBrush`, pressed at the token pressed alpha and `RectoOnAccentBrush` text, and `DangerButton` with `RectoDangerBrush` for Apply Redactions (SPEC 8.2 item 3). Done when: crops sample the Light `#C8431F` and Dark `#F0603C` accent fills and the danger fill. Cheaper substitute: tinting the standard button.
- [ ] Add `IconButton` at 36 epx square (toolbar and mode bars) and 32 epx minimum elsewhere (SPEC 11) with a 16 epx `ui::Icon`, glyph `RectoTextSecondaryBrush` and `RectoTextBrush` on hover. Done when: `Ui.Controls.Button.HitTargets` asserts hit rectangles of at least 32 x 32 epx and 36 x 36 epx in the toolbar variant.
- [ ] Add `ToggleButton` with `RectoAccentTintBrush` background and the Filled glyph in `RectoAccentBrush` when on (SPEC 6 and 8.2). Done when: toggling swaps Regular for Filled glyph and the tint crop matches Light and Dark tokens. Cheaper substitute: an accent-filled tile behind the glyph.
- [ ] Add `HyperlinkButton` with `RectoAccentTextBrush` text and hover underline. Done when: the hover crop shows an underline and the rest crop does not.
- [ ] Add the Fluent 83 ms press and release colour transition through `ui::Motion` and none when animations are off (SPEC 11 and 12). Done when: with `SPI_GETCLIENTAREAANIMATION` false the state change is immediate.
- [ ] Render the states rest, hover, pressed, disabled, focused and toggled in `src/ui/controls/button.cpp` from `RectoRaisedBrush`, `RectoBorderStrongBrush`, `RectoAccentBrush`, `RectoAccentHoverBrush` and `RectoAccentTintBrush` in Light, Dark and HighContrast. Done when: the `Button` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Add arrow-key group navigation for toolbar rows (Left and Right within a row, Tab leaves the row) using the arrow groups of `D10 T01 §13`. Done when: Right on the last toolbar button stays put and Tab moves to the next scope.
- [ ] Expose the automation hook `Button::automation()` returning role `Button, ToggleButton or Hyperlink` and the toggle state and shortcut text through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.Button.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Require an automation name and tooltip for every `IconButton` and show the name plus shortcut through `§3`. Done when: `Ui.Controls.Button.IconNeedsName` asserts a Debug failure without a name and the tooltip text includes the shortcut (SPEC 11).
- [ ] Add the headless golden test `Ui.Controls.Button.Golden` in `tests/recto-tests/ui/button_golden_test.cpp` rendering every variant in rest, hover, pressed, disabled, focused and toggled at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.Button.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: Button family with accent, danger, icon, toggle and hyperlink variants`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.Button.*` passes and the accent fill samples `#C8431F` in Light and `#F0603C` in Dark.

## 3. `ToolTip` with name and shortcut text

`src/ui/controls/tooltip.h` and `tooltip.cpp` replace WinUI `ToolTip` and `ToolTipService`. A tooltip is a small owned popup window from `ui::PopupHost` (`D10 T01 §15`) on `RectoRaisedBrush` with 1 epx `RectoBorderBrush`, 4 epx radius, 12 epx Caption text and an optional right-aligned shortcut in `RectoTextSecondaryBrush` (for example `Settings` and `Ctrl+,`). It appears after the Windows hover delay (`SPI_GETMOUSEHOVERTIME`), on keyboard focus after 500 ms, hides on press, leave or `Esc`, never takes focus, and is placed below the owner and flipped inside the monitor work area. Every icon-only control gets one (SPEC 11).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 7 and 11 with `design/screenshots/D-Focus-Light.png` (icon-only toolbar buttons that carry tooltips).
**Job:** The user hovers or focuses an icon-only control and reads its name and shortcut. Consumers: `Button` icon variants (`§2`), title bar buttons, mode bars, `TabStrip` (`§13`).
**Treatment:** A tooltip popup window with name plus shortcut and Windows hover timing. Cheaper substitute that fails the checkpoint: a native `TOOLTIPS_CLASS` control with system colours, or a tooltip with the name only.
**Chrome:** `RectoRaisedBrush`, `RectoBorderBrush`, `RectoTextBrush`, `RectoTextSecondaryBrush`, `ui::PopupHost`, Caption `ui::TextStyle`.

**Build order:**

1. Create `src/ui/controls/tooltip.h` and `tooltip.cpp` with `ui::ToolTip` and `ui::ToolTipService::attach(Element&, text, shortcut)`. Done when: attaching text to an element makes hover open a popup with that text after the system delay.
2. Place the popup below the owner, flipped and clamped to the monitor work area, without stealing focus. Done when: `Ui.Controls.ToolTip.Placement` places a tooltip for an owner at each screen edge fully inside the work area.
3. Add `Ui.Controls.ToolTip.*` tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.ToolTip.*` passes.

- [ ] Implement `ui::ToolTipService` in `src/ui/controls/tooltip.cpp` reading `SPI_GETMOUSEHOVERTIME` for pointer hover and 500 ms for keyboard focus, with a reshow-immediately window while the pointer moves between owners. Done when: a fake clock test shows the popup at the delay, not before, and instantly for the next owner within 100 ms.
- [ ] Draw the popup chrome: `RectoRaisedBrush` fill, 1 epx `RectoBorderBrush`, 4 epx radius, 8 by 4 epx padding, Caption text with the shortcut right aligned in `RectoTextSecondaryBrush`. Done when: the golden crop shows both text runs on one line with a 16 epx gap. Cheaper substitute: concatenating the shortcut into the name string.
- [ ] Hide on pointer leave, press, `Esc`, owner disable or window deactivation and never take activation (`WS_EX_NOACTIVATE`). Done when: each hide trigger removes the popup and the owner keeps keyboard focus.
- [ ] Honour the animation setting: 83 ms fade when on, none when off (SPEC 11 and 12). Done when: with animations off the popup appears at full opacity in one frame.
- [ ] Render the states shown and hidden in `src/ui/controls/tooltip.cpp` from `RectoRaisedBrush`, `RectoBorderBrush` and `RectoTextBrush` in Light, Dark and HighContrast. Done when: the `ToolTip` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Support keyboard: tooltip text is read from the focused element and `Esc` dismisses without moving focus. Done when: focusing an icon button via Tab shows the tooltip and `Esc` hides it.
- [ ] Expose the automation hook `ToolTip::automation()` returning role `ToolTip` and the owner's text as the automation help text through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.ToolTip.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Add the headless golden test `Ui.Controls.ToolTip.Golden` in `tests/recto-tests/ui/tooltip_golden_test.cpp` rendering a name-only and a name-plus-shortcut tooltip at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.ToolTip.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: ToolTip with name and shortcut text and Windows hover timing`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.ToolTip.*` passes and the placement test keeps the popup inside the monitor work area at all four screen edges.

## 4. `TextBox` editing core: caret, selection, undo, clipboard and placeholder

`src/ui/controls/textbox.h` and `textbox.cpp` build the single-line and multi-line `ui::TextBox` that replaces WinUI `TextBox` and plain `RichEditBox`. It is 32 epx tall with 4 epx radius, `RectoRaisedBrush` fill, 1 epx `RectoBorderStrongBrush` border and the Fluent bottom accent border (2 epx `RectoAccentBrush`) when focused, Body text, placeholder in `RectoTextSecondaryBrush`. It owns its text buffer with a piece table or gap buffer, caret blink from `GetCaretBlinkTime`, pointer and keyboard selection (double click word, triple click line, Shift extend), word navigation with `Ctrl+Left` and `Ctrl+Right`, `Home`, `End`, an undo and redo stack with typing coalescing, and clipboard cut, copy and paste through `ui::Clipboard` (`D10 T01 §17`). Text is laid out and drawn through the `ui::TextBlock` layout of `D10 T01 §10`. IME comes in `§5`.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 4, 5, 7 and 9.3 with `design/screenshots/D-Search.png` (find query field, Replace with box) and `D-Settings.png` (Find a setting).
**Job:** The user types, selects, edits, undoes and pastes text. Consumers: Find and Replace card, page number entry, Settings fields, dialogs (`§17`), `SearchBox` (`§21`), `NumberBox` and `PasswordBox` (`§6`).
**Treatment:** A full editing core with Fluent focus border, caret and undo. Cheaper substitute that fails the checkpoint: wrapping a native `EDIT` control or an append-only text field without selection, undo or word navigation.
**Chrome:** `RectoRaisedBrush`, `RectoBorderStrongBrush`, `RectoAccentBrush`, `RectoTextBrush`, `RectoTextSecondaryBrush`, `RectoTextSelectionBrush`, `ui::Clipboard`, `ui::TextBlock`.

**Build order:**

1. Create `src/ui/controls/textbox.h` and `textbox.cpp` with the text buffer, caret and selection model. Done when: `Ui.Controls.TextBox.Model` covers insert, delete, replace-selection and caret movement including surrogate pairs and combining marks.
2. Add input handling: pointer selection, keyboard navigation, word jumps, clipboard and undo. Done when: scripted input sequences reproduce the expected text and selection.
3. Add `Ui.Controls.TextBox.*` tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.TextBox.*` passes.

- [ ] Implement the buffer and caret in `src/ui/controls/textbox.cpp` moving by grapheme cluster using DirectWrite cluster metrics. Done when: Backspace removes a whole emoji ZWJ sequence and a base plus combining mark as one unit. Cheaper substitute: moving by UTF-16 code unit.
- [ ] Draw the caret with blink period from `GetCaretBlinkTime` (solid when it returns `INFINITE`) and restart the blink on every edit or move. Done when: a fake clock test toggles caret visibility at the system period and keeps it solid right after typing.
- [ ] Implement selection by pointer (click, drag, double click word, triple click line) and by keyboard (Shift plus arrows, `Ctrl+Shift+Left`, `Ctrl+A`) drawn in `RectoTextSelectionBrush`. Done when: each gesture yields the expected range in `Ui.Controls.TextBox.Selection`.
- [ ] Implement word navigation with `Ctrl+Left`, `Ctrl+Right`, `Ctrl+Backspace`, `Ctrl+Delete` using DirectWrite word break analysis. Done when: a mixed Latin and CJK string moves by the expected boundaries.
- [ ] Implement the undo and redo stack (`Ctrl+Z`, `Ctrl+Y`) coalescing consecutive typing into one step and clearing redo on a new edit. Done when: type, undo, redo and edit-after-undo produce the expected buffers. Cheaper substitute: snapshotting the whole string per keystroke without coalescing.
- [ ] Implement cut, copy and paste through `ui::Clipboard` as `CF_UNICODETEXT`, stripping newlines in single-line mode and refusing paste into a read-only box. Done when: a round trip through the clipboard preserves text and the read-only box ignores `Ctrl+V`.
- [ ] Draw the placeholder in `RectoTextSecondaryBrush` while empty and the focused bottom accent border (2 epx `RectoAccentBrush`). Done when: the focused crop shows the accent bottom border in Light and Dark.
- [ ] Render the states rest, hover, focused, disabled and read-only in `src/ui/controls/textbox.cpp` from `RectoRaisedBrush`, `RectoBorderStrongBrush` and `RectoAccentBrush` in Light, Dark and HighContrast. Done when: the `TextBox` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Add multi-line mode with `Enter` for newline, vertical caret movement keeping the column, and wrap to width, scrolled by `ScrollViewer` (`§10`) when it overflows. Done when: `Ui.Controls.TextBox.MultiLine` keeps the preferred column across lines of unequal length.
- [ ] Expose the automation hook `TextBox::automation()` returning role `Edit` and the placeholder or label as name and the text as value through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.TextBox.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Add the headless golden test `Ui.Controls.TextBox.Golden` in `tests/recto-tests/ui/textbox_golden_test.cpp` rendering empty with placeholder, filled, selected, focused and disabled boxes at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.TextBox.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: TextBox editing core with caret, selection, undo and clipboard`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.TextBox.*` passes and the undo test fails if typing coalescing is removed.

## 5. Text Services Framework for `TextBox`: IME, dictation and touch keyboard

`src/ui/input/tsf_bridge.h` and `tsf_bridge.cpp` give `ui::TextBox` full Text Services Framework support. The bridge implements `ITextStoreACP2` (document lock, `GetText`, `SetText`, `InsertTextAtSelection`, `GetSelection`, `SetSelection`, `GetTextExt`, `GetACPFromPoint`, `RequestSupportedAttrs`) and `ITfContextOwnerCompositionSink`, registers with `ITfThreadMgr` and `ITfDocumentMgr`, and associates the focus with the active `TextBox`. Composition text is drawn with the TSF display attributes (underline for the converting clause, thicker for the target clause) and the candidate window is placed from `GetTextExt`. Japanese, Chinese and Korean IME composition, dictation, handwriting panels and the touch keyboard (`ITfFnShowHide` or the input pane, shown on touch focus) work in every field. Without TSF only direct typing would work and East Asian users could not enter text.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 9.3 and 11 with `design/screenshots/D-Search.png` (find field that receives composed text).
**Job:** A user typing Japanese, Chinese or Korean, dictating or using the touch keyboard in any Recto text field. Consumers: `TextBox`, `PasswordBox` and `NumberBox` (`§6`), `SearchBox` (`§21`).
**Treatment:** A real `ITextStoreACP2` text store with composition rendering and touch keyboard. Cheaper substitute that fails the checkpoint: handling only `WM_CHAR` and `WM_IME_COMPOSITION`, so dictation, handwriting and the touch keyboard fail.
**Chrome:** `RectoTextBrush`, `RectoAccentBrush` for the composition underline, `ui::TextBox` of `§4`, `ui::Window` HWND of `D10 T01 §4`.

**Build order:**

1. Create `src/ui/input/tsf_bridge.h` and `tsf_bridge.cpp` implementing `ITextStoreACP2` over `ui::TextBox` with the document lock protocol. Done when: `Ui.Controls.TextBox.Tsf.Store` runs `GetText`, `SetText` and selection calls against a fake TSF client with correct lock semantics.
2. Register with `ITfThreadMgr` per UI thread and create one `ITfDocumentMgr` per focused `TextBox`. Done when: focusing a box makes TSF report it as the active document and blurring releases it.
3. Add `Ui.Controls.TextBox.Tsf.*` tests plus a manual IME script. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.TextBox.Tsf.*` passes.

- [ ] Implement the `ITextStoreACP2` methods and the `OnTextChange`, `OnSelectionChange` and `OnLayoutChange` advise sinks in `src/ui/input/tsf_bridge.cpp`. Done when: edits made by the box notify the sink and edits made by TSF update the buffer and the undo stack as one step.
- [ ] Implement composition handling (`OnStartComposition`, `OnUpdateComposition`, `OnEndComposition`) drawing the composed range with the display attribute underline in the box. Done when: a scripted Japanese composition shows the underlined range and commits the final text on end.
- [ ] Implement `GetTextExt` and `GetACPFromPoint` from the text layout so the candidate window sits under the composition and handwriting maps pointer to text. Done when: `Ui.Controls.TextBox.Tsf.TextExt` returns screen rectangles inside the box for a range at the start, middle and end, at 100 and 200 percent.
- [ ] Show and hide the touch keyboard on touch focus through `ITfFnShowHide` or the input pane for pen and touch, and not for mouse focus. Done when: a touch-origin focus requests the keyboard and a mouse-origin focus does not.
- [ ] Add Japanese, Chinese (Pinyin) and Korean (Hangul) composition tests with the Windows text input processors on a display-session host. Done when: composing `nihongo` yields the expected candidates and committed text and Korean jamo compose into a syllable in the box. Cheaper substitute: testing only a Latin keyboard.
- [ ] Support dictation (`Win+H`) and handwriting insertion through `InsertTextAtSelection`. Done when: a dictation result inserts at the caret and replaces a selection.
- [ ] Render the states composing and committed (extends the `§4` states) in `src/ui/controls/textbox.cpp` from `RectoAccentBrush` underline and `RectoTextBrush` in Light, Dark and HighContrast. Done when: the `TextBox` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Keep password fields out of TSF history by marking the context private (the password input scope and `TS_SS_NOHIDDENTEXT`) for `PasswordBox`. Done when: a `PasswordBox` document reports the password input scope.
- [ ] Expose the automation hook `TextBox::automation()` returning role `Edit with TSF` and the active composition state through the UIA text pattern hook of `D10 T03 §3` through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.TextBox.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Add the golden `Ui.Controls.TextBox.Tsf.Golden` for the composition underline styles (converted, target, input) in Light, Dark and HighContrast. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.TextBox.Tsf.Golden` passes and fails when the underline thickness changes.
- [ ] Commit: `ui: TextBox Text Services Framework bridge for IME, dictation and touch keyboard`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.TextBox.Tsf.*` passes and a scripted Japanese composition commits the expected text.

## 6. `PasswordBox` and `NumberBox`

`src/ui/controls/passwordbox.h`, `passwordbox.cpp`, `numberbox.h` and `numberbox.cpp` replace WinUI `PasswordBox` and `NumberBox`. `PasswordBox` shows bullets, offers a reveal button (Show glyph `eye`, Hide glyph `eye_off`, press and hold or toggle), never copies or cuts to the clipboard (`Ctrl+C` is ignored), clears its buffer on destruction with `SecureZeroMemory`, and in the wrong-password state draws a 1 epx `RectoDangerBrush` border and the helper text `That password didn't work.` (SPEC 9.9). `NumberBox` is a `TextBox` with minimum, maximum, step, spin buttons (Fluent up and down chevrons from Segoe Fluent Icons), `Up` and `Down` and mouse wheel stepping, a format string, validation on commit and a clamped or rejected invalid state with `RectoDangerBrush`. Both are 32 epx tall with the `§4` focus border.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 7 and 9.9 with `design/screenshots/D-Dialogs.png` (Password dialog with wrong-password state) and `D-Settings.png` (page cache size input).
**Job:** The user enters a PDF password or a bounded number (page, zoom percent, DPI, cache MB). Consumers: Password dialog (D02 T01 §10), Sign dialog, Protected and Signed PDF options, Go to page, Settings.
**Treatment:** Fluent PasswordBox and NumberBox with reveal, clamp and red error border. Cheaper substitute that fails the checkpoint: a `TextBox` with `*` characters that can be copied, or a number field that accepts letters.
**Chrome:** `RectoDangerBrush`, `RectoBorderStrongBrush`, `RectoAccentBrush`, `RectoTextSecondaryBrush`, `ui::Icon` (`eye`, `eye_off`), `TextBox` of `§4`.

**Build order:**

1. Create `PasswordBox` in `src/ui/controls/passwordbox.cpp` deriving from `TextBox` with masked drawing, reveal button and clipboard denial. Done when: `Ui.Controls.PasswordBox.NoClipboard` asserts that copy and cut leave the clipboard unchanged.
2. Create `NumberBox` in `src/ui/controls/numberbox.cpp` with min, max, step, spin buttons and commit validation. Done when: `Ui.Controls.NumberBox.Clamp` clamps typed 5000 to a max of 400 on commit.
3. Add `Ui.Controls.PasswordBox.*` and `Ui.Controls.NumberBox.*` tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.PasswordBox.*:Ui.Controls.NumberBox.*` passes.

- [ ] Draw bullets at the Body size and a reveal button (36 by 32 epx hit area, 16 epx `eye` or `eye_off` glyph through `ui::Icon`) that reveals while pressed or when toggled. Done when: the revealed crop shows plain text and the hidden crop shows bullets. Cheaper substitute: always showing plain text.
- [ ] Block copy, cut and drag of the content and keep the buffer in a zeroed-on-free allocator. Done when: `Ui.Controls.PasswordBox.NoClipboard` passes and a debug probe finds no password bytes after destruction.
- [ ] Add the wrong-password state: `RectoDangerBrush` 1 epx border and `That password didn't work.` helper text with a status glyph (SPEC 9.9, colour never the only signal). Done when: the error crop shows border, glyph and text in Light and Dark. Cheaper substitute: a red border alone.
- [ ] Add `NumberBox` spin buttons with Fluent chevrons from Segoe Fluent Icons (built-in chevrons stay system glyphs, SPEC 6), step by `Up`, `Down` and wheel, `Shift` for a large step. Done when: stepping from 100 by 25 within 50 to 400 yields the expected values and stops at the limits.
- [ ] Parse and format numbers with the user locale (`GetNumberFormatEx`), show the invalid state when parse fails and revert or clamp on commit per a `ValidationMode`. Done when: German `1,5` and English `1.5` both parse to 1.5 under their locale.
- [ ] Render the states rest, hover, focused, disabled, revealed and error in `src/ui/controls/passwordbox.cpp` from `RectoBorderStrongBrush`, `RectoAccentBrush` and `RectoDangerBrush` in Light, Dark and HighContrast. Done when: the `PasswordBox` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Keyboard: `Enter` commits `NumberBox`, `Esc` reverts to the last committed value. Done when: `Esc` restores the previous number and `Enter` commits and fires the changed event.
- [ ] Expose the automation hook `PasswordBox::automation()` returning role `Edit (password) and Spinner for NumberBox` and RangeValue minimum, maximum and value for `NumberBox` through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.PasswordBox.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Require an automation name and a tooltip on the reveal button (`Show password`). Done when: `Ui.Controls.PasswordBox.RevealNamed` asserts both.
- [ ] Add the headless golden test `Ui.Controls.PasswordBox.Golden` in `tests/recto-tests/ui/passwordbox_golden_test.cpp` rendering password in hidden, revealed and error states and a number box with spin buttons at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.PasswordBox.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: PasswordBox and NumberBox with reveal, clamp and validation`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.PasswordBox.*` passes and the clipboard test fails if copy is allowed.

## 7. `CheckBox`, `RadioButton` group and `ToggleSwitch`

`src/ui/controls/checkbox.h`, `radiobutton.h` and `toggleswitch.h` (with `.cpp`) replace WinUI `CheckBox`, `RadioButton`, `RadioButtons` and `ToggleSwitch`. The checkbox is a 20 epx box with 4 epx radius, `RectoBorderStrongBrush` outline, accent fill with a Segoe Fluent Icons check (built-in glyph) on `RectoOnAccentBrush` when checked, and an indeterminate dash. The radio button is a 20 epx circle with a 10 epx accent dot. The toggle switch is a 40 by 20 epx stadium track (a control part keeping the shape of the stock Fluent switch, not a chip) with a 12 epx thumb that grows on hover and press. Label text is Body, hit target at least 32 epx tall (SPEC 11). `RadioGroup` provides arrow-key selection with a roving tab stop. Motion is the Fluent 167 ms thumb slide, none when animations are off.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6, 7, 9.7, 11 with `design/screenshots/D-Settings.png` (toggle rows, radio and check options).
**Job:** The user turns a setting on or off or picks one of a few options. Consumers: Settings (D02 T04), Convert options (D04), conversion dialogs, Find card toggles (as `ToggleButton` of `§2`).
**Treatment:** Fluent check, radio and switch with SPEC accent and 32 epx hit targets. Cheaper substitute that fails the checkpoint: owner-drawn squares without a label hit area, radio selection by mouse only, or an accent in a hard-coded colour.
**Chrome:** `RectoBorderStrongBrush`, `RectoAccentBrush`, `RectoOnAccentBrush`, `RectoTextBrush`, `RectoTextSecondaryBrush`, Segoe Fluent Icons check glyph, `Control` of `§1`.

**Build order:**

1. Create `CheckBox`, `RadioButton`, `RadioGroup` and `ToggleSwitch` in `src/ui/controls/`. Done when: `Ui.Controls.CheckBox.Construct` builds each with label and checked state.
2. Implement activation, tri-state cycling, group exclusivity and the roving tab stop. Done when: Space toggles, Arrow keys move and select within a group, and Tab leaves the group.
3. Add golden and behaviour tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.CheckBox.*:Ui.Controls.RadioButton.*:Ui.Controls.ToggleSwitch.*` passes.

- [ ] Draw `CheckBox` at 20 epx with 4 epx radius, outline `RectoBorderStrongBrush`, checked fill `RectoAccentBrush` and the Segoe Fluent Icons check on `RectoOnAccentBrush`, plus indeterminate. Done when: unchecked, checked and indeterminate crops match tokens in Light and Dark. Cheaper substitute: a RectoIcons glyph for the check (stock control glyphs stay system glyphs, SPEC 6).
- [ ] Draw `RadioButton` as a 20 epx circle with a 10 epx accent dot and `RadioGroup` exclusivity with a model value. Done when: selecting another radio unselects the first and the group value changes once.
- [ ] Draw `ToggleSwitch` with a 40 by 20 epx track, 12 epx thumb growing to 14 epx on hover and press. Done when: on and off crops match tokens and the thumb slides over 167 ms, instantly with animations off.
- [ ] Make the whole label row the hit target with at least 32 epx height (SPEC 11). Done when: `Ui.Controls.CheckBox.HitTargets` asserts clicking the label toggles the control.
- [ ] Render the states rest, hover, pressed, disabled, focused, checked and indeterminate in `src/ui/controls/checkbox.cpp` from `RectoBorderStrongBrush` and `RectoAccentBrush` in Light, Dark and HighContrast. Done when: the `CheckBox` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Keyboard: `Space` toggles checkbox and switch, arrow keys move and select in a radio group, `Home` and `End` jump. Done when: scripted keys produce the expected selected index in `Ui.Controls.RadioButton.Keyboard`.
- [ ] Expose the automation hook `CheckBox::automation()` returning role `CheckBox, RadioButton or Button (switch)` and the Toggle pattern state and the SelectionItem state for radios through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.CheckBox.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Give a label-less use an automation name and tooltip. Done when: a switch without a label asserts in Debug.
- [ ] Add the headless golden test `Ui.Controls.CheckBox.Golden` in `tests/recto-tests/ui/checkbox_golden_test.cpp` rendering check, radio and switch controls in all states at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.CheckBox.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: CheckBox, RadioButton group and ToggleSwitch`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.CheckBox.*` passes and clicking the label of a switch toggles it.

## 8. `Slider`, `ProgressBar` and `ProgressRing`

`src/ui/controls/slider.h`, `progressbar.h` and `progressring.h` (with `.cpp`) replace WinUI `Slider`, `ProgressBar` and `ProgressRing`. The slider has a 4 epx track in `RectoBorderStrongBrush` with the filled part in `RectoAccentBrush`, a 20 epx circular thumb (with a 10 epx inner dot growing on hover) and a value tooltip; it serves the zoom slider of the Classic status bar (SPEC 8.4), opacity and stroke width in the annotation style popover, speech rate and volume. The progress bar is a 4 epx track with an accent fill, determinate and indeterminate (a 1.5 s sweeping segment). The progress ring is determinate or indeterminate, 16 to 32 epx, used by the queue card (SPEC 9.5). Indeterminate motion is replaced by a static arc when animations are off (SPEC 11).

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 4, 8.2, 8.4, 9.5 and 12 with `design/screenshots/D-Classic.png` (status bar zoom slider) and `D-Convert.png` (queue card progress ring).
**Job:** The user adjusts a continuous value or sees the progress of work. Consumers: Classic status bar zoom slider, annotation style popover, Settings, queue card, loading states.
**Treatment:** Fluent slider, progress bar and ring with accent fills and reduced motion fallback. Cheaper substitute that fails the checkpoint: a native `msctls_trackbar32`, a spinner GIF, or ignoring the animation setting.
**Chrome:** `RectoAccentBrush`, `RectoBorderStrongBrush`, `RectoTextSecondaryBrush`, `ui::Motion` of `D10 T01 §14`, `Control` of `§1`.

**Build order:**

1. Create `Slider`, `ProgressBar` and `ProgressRing` in `src/ui/controls/`. Done when: `Ui.Controls.Progress.Construct` builds each and reports value ranges.
2. Drive indeterminate loops from the `ui::Motion` frame clock and stop them when hidden or when animations are off. Done when: a hidden ring consumes no frames.
3. Add tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.Slider.*:Ui.Controls.ProgressBar.*:Ui.Controls.ProgressRing.*` passes.

- [ ] Draw `Slider` with a 4 epx track, accent fill up to the value, 20 epx thumb and optional tick marks, horizontal and vertical. Done when: crops at 0, 50 and 100 percent match the tokens in Light and Dark.
- [ ] Implement pointer drag with capture, click-to-jump on the track, wheel stepping and keys `Left`, `Right` (small step), `PageUp`, `PageDown` (large step), `Home`, `End`. Done when: scripted input produces the expected values within min and max and snaps to the step.
- [ ] Show a value tooltip on drag using `§3` formatted by a callback (for example `125%`). Done when: dragging shows the formatted text and releasing hides it.
- [ ] Draw `ProgressBar` determinate and indeterminate (1.5 s sweep) at 4 epx height with 2 epx rounded ends. Done when: determinate crop width equals the value fraction and the indeterminate segment moves across frames.
- [ ] Draw `ProgressRing` determinate arc and indeterminate rotation at 16, 24 and 32 epx, replaced by a static partial arc with animations off. Done when: with `SPI_GETCLIENTAREAANIMATION` false two frames render identically.
- [ ] Render the states rest, hover, pressed, disabled and focused in `src/ui/controls/slider.cpp` from `RectoAccentBrush` and `RectoBorderStrongBrush` in Light, Dark and HighContrast. Done when: the `Slider` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Draw the Fluent focus ring around the thumb for keyboard focus. Done when: the focused crop shows the ring and a mouse press does not.
- [ ] Expose the automation hook `Slider::automation()` returning role `Slider or ProgressBar` and RangeValue minimum, maximum, current value and the name through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.Slider.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Give the ring and bar an automation name such as `Converting` supplied by the owner. Done when: `Ui.Controls.ProgressBar.Named` asserts a name for each instance in the gallery.
- [ ] Add the headless golden test `Ui.Controls.Slider.Golden` in `tests/recto-tests/ui/slider_golden_test.cpp` rendering slider, bar and ring at several values at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.Slider.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: Slider, ProgressBar and ProgressRing with reduced-motion fallback`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.Slider.*` passes and the ring renders two identical frames when animations are off.

## 9. `ComboBox` and `DropDownButton`

`src/ui/controls/combobox.h` and `dropdownbutton.h` (with `.cpp`) replace WinUI `ComboBox`, `DropDownButton` and `SplitButton`. `ComboBox` is a 32 epx field with 4 epx radius and a Segoe Fluent Icons chevron (built-in glyph); its list opens in an owned popup window from `ui::PopupHost` on `RectoRaisedBrush` with 8 epx radius, elevation Z32, 32 epx rows with `RectoAccentTintBrush` on the selected row and a 3 epx accent bar, aligned so the selected item sits over the field (Fluent behaviour) and clamped to the work area. `DropDownButton` is a button with a trailing chevron that opens a `Menu` (`§14`) or a `Flyout` (`§16`); the split variant has a main action and a separate drop part. It serves zoom presets, page size, OCR language, save location, file-exists rule and the More menu.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6, 7, 9.5 and 9.7 with `design/screenshots/D-Settings.png` (dropdown rows) and `D-Convert.png` (option dropdowns).
**Job:** The user picks one value from a list or opens an action menu. Consumers: Convert options, Settings, zoom presets, Organize and annotation popovers.
**Treatment:** Fluent combo box with popup list, type-ahead and keyboard model. Cheaper substitute that fails the checkpoint: a native `COMBOBOX` window, or a list drawn inside the window that is clipped by the window edge.
**Chrome:** `RectoRaisedBrush`, `RectoBorderStrongBrush`, `RectoAccentTintBrush`, `RectoAccentBrush`, `RectoTextBrush`, Segoe Fluent Icons chevron, `ui::PopupHost`.

**Build order:**

1. Create `ComboBox`, `DropDownButton` and `SplitButton` in `src/ui/controls/`. Done when: `Ui.Controls.ComboBox.Construct` builds each with items.
2. Open the list in a popup window with selected-item alignment, scrolling for long lists and light dismiss. Done when: the popup stays inside the work area at every screen edge and closes on outside click and `Esc`.
3. Add tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.ComboBox.*:Ui.Controls.DropDownButton.*` passes.

- [ ] Draw the closed field: 32 epx height, 4 epx radius, 1 epx `RectoBorderStrongBrush`, Body text, right chevron from Segoe Fluent Icons in `RectoTextSecondaryBrush`. Done when: the golden crop matches the SPEC field metrics in Light and Dark.
- [ ] Draw the list popup with 8 epx radius, `RectoRaisedBrush`, elevation Z32, 32 epx rows, hover row subtle fill, selected row `RectoAccentTintBrush` with a 3 epx accent bar. Done when: crops of hover and selected rows match the tokens.
- [ ] Implement keyboard: `Alt+Down`, `F4` or `Space` open, arrows move, `Enter` commits, `Esc` closes, type-ahead jumps by typed prefix with a 1 s reset. Done when: typing `Le` selects `Letter` in `Ui.Controls.ComboBox.TypeAhead`.
- [ ] Implement an editable mode that embeds a `TextBox` of `§4` and filters the list. Done when: typing narrows the list and `Enter` commits free text only when allowed.
- [ ] Implement `DropDownButton` and `SplitButton` hosting a `Menu` (`§14`) or `Flyout` (`§16`) and returning focus to the button on close. Done when: closing a menu returns keyboard focus to the owner.
- [ ] Render the states rest, hover, pressed, disabled, focused and open in `src/ui/controls/combobox.cpp` from `RectoBorderStrongBrush`, `RectoAccentBrush` and `RectoAccentTintBrush` in Light, Dark and HighContrast. Done when: the `ComboBox` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Expose the automation hook `ComboBox::automation()` returning role `ComboBox` and ExpandCollapse state, the selected value and SelectionItem for list rows through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.ComboBox.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Give icon-only drop-down buttons (More) an automation name and tooltip. Done when: `Ui.Controls.DropDownButton.IconNamed` asserts both.
- [ ] Add the headless golden test `Ui.Controls.ComboBox.Golden` in `tests/recto-tests/ui/combobox_golden_test.cpp` rendering closed, open, hovered row and selected row at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.ComboBox.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: ComboBox and DropDownButton with popup list and type-ahead`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.ComboBox.*` passes and type-ahead selects the matching item.

## 10. `ScrollViewer`: overlay scrollbars, smooth wheel, DirectManipulation pan and pinch

`src/ui/controls/scrollviewer.h` and `scrollviewer.cpp` replace WinUI `ScrollViewer` and `ScrollView`. It hosts one content element, clips it, and draws Fluent overlay scrollbars: a thin 2 epx rail in `RectoTextSecondaryBrush` that expands on hover to a 6 epx bar with arrow glyphs (Segoe Fluent Icons) and fades after 2 s. Mouse wheel scrolling is smooth (animated offset with the Fluent ease over about 167 ms); `Shift+wheel` is horizontal; `PageUp`, `PageDown`, `Home`, `End` and arrows scroll the focused viewer. Precision touchpad and touch pan and pinch zoom run through DirectManipulation (`IDirectManipulationManager`, `IDirectManipulationViewport`, `IDirectManipulationContent`) with inertia. The viewer exposes `scroll_into_view(rect)` and viewport change notifications (offset, extent, viewport size) used by `§11` for virtualization and by the page canvas.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 7, 9.2, 11 and 12 with `design/screenshots/D-Focus-Light.png` (side panel list scrollbar) and `D-Organize.png` (page grid scrolling).
**Job:** The user scrolls lists, panels, grids and settings with wheel, touchpad, touch or scrollbar. Consumers: `ItemsView` (`§11`), `TreeView` (`§12`), Settings, side panels, multi-line `TextBox` (`§4`).
**Treatment:** Overlay scrollbars, smooth wheel and DirectManipulation. Cheaper substitute that fails the checkpoint: native `WS_VSCROLL` scrollbars, jumpy per-notch wheel steps, or touch handled as mouse emulation without inertia or pinch.
**Chrome:** `RectoTextSecondaryBrush`, `RectoTextBrush`, `RectoSurfaceBrush`, `ui::Motion` of `D10 T01 §14`, Segoe Fluent Icons arrows, `Control` of `§1`.

**Build order:**

1. Create `src/ui/controls/scrollviewer.h` and `scrollviewer.cpp` with offsets, extents, clipping and the viewport change event. Done when: `Ui.Controls.ScrollViewer.Model` clamps offsets to extent minus viewport.
2. Create the overlay scrollbar element with rail, expanded bar, thumb drag, track click paging and auto-hide. Done when: crops show the rail at rest and the expanded bar on hover.
3. Wire DirectManipulation in `src/ui/input/direct_manipulation.cpp`. Done when: a synthetic contact sequence pans and pinches the viewport with inertia on a display-session host.

- [ ] Implement offset clamping, extent and viewport tracking and `scroll_into_view(rect, align)` with minimal movement. Done when: scrolling an item below the viewport into view moves exactly enough to show it.
- [ ] Draw the Fluent overlay scrollbar: 2 epx rail, 6 epx expanded bar on hover or drag, thumb with minimum 32 epx length, arrow buttons in the expanded state, fade out after 2 s idle. Done when: rest, hover, dragging and hidden crops match the tokens in Light, Dark and HighContrast (always solid in HighContrast).
- [ ] Implement smooth wheel scrolling with an animated offset (about 167 ms Fluent ease) that retargets on new wheel input, `Shift+wheel` horizontal, and instant jumps with animations off. Done when: a fake clock test shows monotonic approach to the target and retargeting without overshoot. Cheaper substitute: adding the wheel delta immediately.
- [ ] Create `ui::DirectManipulationHost` in `src/ui/input/direct_manipulation.cpp` using `IDirectManipulationManager`, a viewport with pan, inertia and pinch zoom configuration, and a content updated by `IDirectManipulationUpdateHandler`. Done when: touchpad and touch pan produce offset deltas and a pinch produces a zoom factor event consumed by the owner.
- [ ] Route `WM_POINTER` contacts (touch, precision touchpad) to the viewport via `SetContact` and feed the updates on the UI thread. Done when: a pen or mouse contact is not captured by DirectManipulation and still reaches the content.
- [ ] Add keyboard scrolling: arrows by 48 epx, `PageUp` and `PageDown` by 90 percent of the viewport, `Home` and `End`, `Ctrl+Home` and `Ctrl+End`. Done when: scripted keys reach the expected offsets.
- [ ] Publish viewport change notifications (offset, extent, viewport, scale) coalesced to one per frame. Done when: `Ui.Controls.ScrollViewer.ViewportEvents` receives a single event per frame during a pan.
- [ ] Render the states scrollbar rest, hover, pressed thumb, disabled and focused in `src/ui/controls/scrollviewer.cpp` from `RectoTextSecondaryBrush`, `RectoTextBrush` and `RectoSurfaceBrush` in Light, Dark and HighContrast. Done when: the `ScrollViewer` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Expose the automation hook `ScrollViewer::automation()` returning role `Pane` and the Scroll pattern values (horizontal and vertical percent and view size) and ScrollItem for `scroll_into_view` through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.ScrollViewer.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Add the headless golden test `Ui.Controls.ScrollViewer.Golden` in `tests/recto-tests/ui/scrollviewer_golden_test.cpp` rendering a content area with scrollbars at rest, hover and dragging at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.ScrollViewer.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: ScrollViewer with overlay scrollbars, smooth wheel and DirectManipulation`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.ScrollViewer.*` passes and the smooth wheel test fails if the offset jumps to the target in one step.

## 11. Virtualized `ItemsView`: stack, filmstrip and grid layouts, selection, reorder hooks

`src/ui/controls/itemsview.h`, `itemsview.cpp` and `itemsview_layouts.cpp` replace WinUI `ListView`, `GridView`, `ItemsRepeater` and `ItemsView`. It virtualizes: only the realized range plus a small buffer exists, and items are recycled through a template pool when scrolled out, driven by `ScrollViewer` viewport notifications. Layouts: vertical stack (side panels, thumbnails in the left panel, command box rows), horizontal stack (the filmstrip along the bottom in Reading room, SPEC 8), and a uniform grid (Organize page cards, Home recent covers). Selection modes none, single and multiple, with `Ctrl` toggle and `Shift` range, selected rows on `RectoAccentTintBrush` (SPEC 4) with the accent glyph. Keyboard follows the focus model (arrows, `Home`, `End`, `Page`, type-ahead). Drag reorder hooks expose begin, over and drop events with a drop indicator line (accent, 2 epx) for Organize and Combine. The data source is an abstract `ItemsSource` so view models stay in `src/app`.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 4, 8, 8.3, 9.1, 9.4 and 9.6 with `design/screenshots/D-Organize.png` (grid and selection), `D-ReadingRoom.png` (filmstrip) and `D-Home.png` (covers).
**Job:** The user browses, selects and reorders collections. Consumers: Pages panel (D03), filmstrip, Organize grid (D05), Home recents (D02), command box rows (D02 T03), Comments, Attachments and Layers panels.
**Treatment:** One recycling items control with three layouts and Fluent selection. Cheaper substitute that fails the checkpoint: realizing every item (breaks 2000-page thumbnails), or three separate list classes.
**Chrome:** `RectoAccentTintBrush`, `RectoAccentBrush`, `RectoBorderBrush`, `RectoTextBrush`, `ScrollViewer` of `§10`, focus groups of `D10 T01 §13`.

**Build order:**

1. Create `ItemsView`, `ItemsSource` and `ItemTemplate` in `src/ui/controls/itemsview.h`. Done when: `Ui.Controls.ItemsView.Realization` shows at most viewport plus buffer items realized for 100000 items.
2. Implement `StackLayout` (vertical, horizontal) and `UniformGridLayout` in `src/ui/controls/itemsview_layouts.cpp` with item size, spacing and wrapping. Done when: the grid computes columns from width and keeps scroll anchoring on resize.
3. Add selection, keyboard and reorder hooks plus tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.ItemsView.*` passes.

- [ ] Implement the realization window and the template recycle pool keyed by item kind, reusing elements instead of allocating on scroll. Done when: scrolling through 10000 items allocates no element after the pool is warm (counter assertion). Cheaper substitute: creating and destroying elements per scroll step.
- [ ] Implement the vertical stack, horizontal stack (filmstrip) and uniform grid layouts, switchable on the same instance by `set_layout` without recreating items (the Pages list moves between the left panel and the filmstrip, SPEC 8). Done when: switching layout keeps selection and the first visible item.
- [ ] Implement selection modes with click, `Ctrl` toggle, `Shift` range and `Ctrl+A`, drawing selected rows on `RectoAccentTintBrush` with an accent glyph and a check badge for page cards (SPEC 11). Done when: scripted clicks produce the expected selected set and the badge crop is present.
- [ ] Implement keyboard navigation (arrows in the layout's directions, `Home`, `End`, `PageUp`, `PageDown`, `Enter` activate, `Space` toggle, type-ahead) with a roving focus item. Done when: arrow keys on a 4 column grid move by one and by a row as expected.
- [ ] Implement drag reorder hooks (`on_drag_begin`, `on_drag_over`, `on_drop`) with auto-scroll near the edges and a 2 epx `RectoAccentBrush` drop indicator line, using the OLE drag support of `D10 T01 §17`. Done when: dragging item 3 between 6 and 7 reports index 6 and draws the indicator there.
- [ ] Expose scroll-into-view for the focused or selected item and ensure realized items report stable automation positions. Done when: focusing an unrealized item realizes it and scrolls it into view.
- [ ] Render the states item rest, hover, pressed, selected, focused and disabled in `src/ui/controls/itemsview.cpp` from `RectoAccentTintBrush`, `RectoAccentBrush` and `RectoBorderBrush` in Light, Dark and HighContrast. Done when: the `ItemsView` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Expose the automation hook `ItemsView::automation()` returning role `List and ListItem` and Selection and SelectionItem patterns, item count, position in set and Grid and GridItem for the grid layout through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.ItemsView.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Add the headless golden test `Ui.Controls.ItemsView.Golden` in `tests/recto-tests/ui/itemsview_golden_test.cpp` rendering each layout with selection, focus and the drop indicator at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.ItemsView.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: virtualized ItemsView with stack, filmstrip and grid layouts`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.ItemsView.*` passes and the realization test fails if all items are realized.

## 12. `TreeView`: expand, collapse, keyboard, selection bar and drag reorder

`src/ui/controls/treeview.h` and `treeview.cpp` replace WinUI `TreeView` for the Outline panel and Bookmark Editor (SPEC 8.3). It flattens expanded nodes into the virtualized `ItemsView` of `§11`, so large outlines stay fast. Rows are 32 epx with a 12 epx per level indent, a Segoe Fluent Icons chevron (rotates 90 degrees over 167 ms) and a Body label. The current section row is highlighted with `RectoAccentTintBrush` and a 3 epx `RectoAccentBrush` bar on the left (SPEC 8.3); hover is a subtle fill. Keyboard: `Right` expands or enters, `Left` collapses or goes to the parent, `Up` `Down` move, `*` expands all, `Enter` activates. Drag reorder supports nesting and reordering with an indicator line and an indent-aware drop target, plus keyboard reorder (`Alt+Up`, `Alt+Down`, `Alt+Right`, `Alt+Left`).

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6, 8.3 and 11 with `design/screenshots/D-ReadingRoom.png` (outline in the inspector) and `D-Focus-Light.png` (outline panel).
**Job:** The user navigates and edits the document outline. Consumers: Outline panel and Bookmark Editor (D03), Structure Tag Tree inspector (D03).
**Treatment:** Virtualized tree with the 3 epx accent bar for the current item and keyboard reorder. Cheaper substitute that fails the checkpoint: a flat indented list without expand state, or drag reorder only by mouse.
**Chrome:** `RectoAccentTintBrush`, `RectoAccentBrush`, `RectoTextBrush`, `RectoTextSecondaryBrush`, Segoe Fluent Icons chevron, `ItemsView` of `§11`.

**Build order:**

1. Create `src/ui/controls/treeview.h` and `treeview.cpp` with a `TreeNode` source, a flattening adapter and expand state. Done when: `Ui.Controls.TreeView.Flatten` returns visible rows for a 10000 node tree in time bounded by the visible nodes.
2. Implement row drawing, chevrons, current-item bar and keyboard model. Done when: goldens match and scripted keys reach the expected rows.
3. Add reorder and nesting with tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.TreeView.*` passes.

- [ ] Implement the flattening adapter over `ItemsSource` with lazy child loading (`has_children` and `load_children`) and stable expand state across source changes. Done when: expanding a lazy node loads children once and collapsing keeps the loaded state.
- [ ] Draw rows at 32 epx with a 12 epx indent, a rotating chevron (Segoe Fluent Icons, 167 ms) and Body text trimmed with ellipsis. Done when: crops at depth 0 to 3 show the right indents in Light and Dark.
- [ ] Draw the current-section highlight: `RectoAccentTintBrush` fill with a 3 epx `RectoAccentBrush` bar on the left edge, distinct from selection (SPEC 8.3). Done when: the crop for a current and selected row shows both the bar and the tint. Cheaper substitute: tint only, without the bar.
- [ ] Implement keyboard: `Right`, `Left`, `Up`, `Down`, `Home`, `End`, `*`, `Enter`, type-ahead. Done when: `Ui.Controls.TreeView.Keyboard` covers expand, collapse to parent and activate.
- [ ] Implement drag reorder and nesting with a drop indicator whose indent shows the target level, and keyboard reorder with `Alt+Arrow`. Done when: dropping at the lower half of a node nests it and the reported new parent and index are correct.
- [ ] Expose inline rename (`F2`) through an embedded `TextBox` of `§4` with `Enter` and `Esc`. Done when: renaming commits the new title and `Esc` restores the old one.
- [ ] Render the states row rest, hover, selected, current, focused and disabled in `src/ui/controls/treeview.cpp` from `RectoAccentTintBrush`, `RectoAccentBrush` and `RectoTextSecondaryBrush` in Light, Dark and HighContrast. Done when: the `TreeView` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Expose the automation hook `TreeView::automation()` returning role `Tree and TreeItem` and ExpandCollapse state, level, position in set and SelectionItem through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.TreeView.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Add the headless golden test `Ui.Controls.TreeView.Golden` in `tests/recto-tests/ui/treeview_golden_test.cpp` rendering a three level outline with expanded, collapsed, selected and current rows at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.TreeView.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: TreeView with accent bar, keyboard model and drag reorder`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.TreeView.*` passes and the current-item crop fails without the 3 epx bar.

## 13. `TabStrip`: title-bar document tabs, close, add, overflow and drag reorder

`src/ui/controls/tabstrip.h` and `tabstrip.cpp` replace WinUI `TabView` and live inside the custom title bar (SPEC 8.1). Tabs are at most 240 epx wide and shrink evenly down to a minimum, 32 epx tall with 4 epx top radius, Caption title with ellipsis, an optional 16 epx glyph, a close button (`dismiss`) shown on hover and always on the active tab, and a dirty indicator slot (a small accent dot replacing the close glyph until hover). The active tab uses `RectoSurfaceBrush`, inactive tabs `RectoTextSecondaryBrush` text; the `+` add button follows the last tab. When tabs overflow, the strip scrolls with the `ScrollViewer` of `§10` and shows scroll arrows. Drag reorders tabs with live shifting and an out-of-strip tear threshold event. The strip reports its interactive hit regions to the non-client hit testing of `D10 T01 §19` so empty strip space drags the window while tabs, close and add buttons stay client area.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6, 7, 8.1 and 11 with `design/screenshots/D-Focus-Light.png` and `D-Focus-Dark.png` (title bar tabs, active tab, close button).
**Job:** The user switches, closes, adds and reorders document tabs. Consumers: the shell title bar (D02 T01 §3), session restore and reopen closed tab (D02 T01 §13 to §16).
**Treatment:** A custom tab strip in the title bar feeding caption hit testing. Cheaper substitute that fails the checkpoint: tabs on a second row below a system title bar, or tab clicks that make the window drag.
**Chrome:** `RectoSurfaceBrush`, `RectoWindowBrush`, `RectoTextBrush`, `RectoTextSecondaryBrush`, `RectoAccentBrush` (dirty dot), `ui::Icon` (`dismiss`, `add`), `ScrollViewer` of `§10`, non-client regions of `D10 T01 §19`.

**Build order:**

1. Create `src/ui/controls/tabstrip.h` and `tabstrip.cpp` with `TabItem` (title, glyph, dirty, pinned, tooltip) and the strip layout. Done when: `Ui.Controls.TabStrip.Layout` caps width at 240 epx and shrinks evenly for 1, 8 and 40 tabs.
2. Implement close, add, overflow scrolling and drag reorder. Done when: scripted pointer sequences reorder tabs and fire the expected events.
3. Publish interactive regions to `D10 T01 §19` and add tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.TabStrip.*` passes.

- [ ] Implement tab width distribution (240 epx maximum, shrinking, minimum showing glyph and close on the active tab) and re-layout on resize and DPI change. Done when: widths sum to the available space and each tab is a whole number of device pixels wide.
- [ ] Draw tabs: active `RectoSurfaceBrush` with 4 epx top radius, inactive transparent with `RectoTextSecondaryBrush` text, hover fill, and the Caption title with ellipsis. Done when: active, inactive, hover and pressed crops match the D-Focus-Light.png and D-Focus-Dark.png metrics.
- [ ] Implement the close button visible on hover and on the active tab (`dismiss` 16 epx glyph, 24 epx hit area inside the 32 epx tall tab) and middle-click close. Done when: hover shows the button on an inactive tab and the active tab shows it at rest.
- [ ] Implement the dirty indicator slot: a 6 epx `RectoAccentBrush` dot in the close position until hover swaps in the close glyph (colour is not the only signal: tooltip and automation name add `unsaved changes`). Done when: the dirty crop shows the dot and the automation name contains the unsaved state.
- [ ] Implement the `+` add button after the last tab (36 by 32 epx hit target, name `New tab`, tooltip with shortcut) that stays visible during overflow, pinned to the right of the strip. Done when: the button remains visible with 40 tabs.
- [ ] Implement overflow scrolling with arrow buttons, wheel, and scroll-into-view for the active tab (via `§10`). Done when: activating tab 30 scrolls it into view.
- [ ] Implement drag reorder with a 4 epx start threshold, live shifting animated 167 ms (none when animations are off), keyboard move with `Ctrl+Shift+Left` and `Ctrl+Shift+Right`, and a tear-off event when the pointer leaves vertically by 24 epx. Done when: dragging tab 1 past tab 3 reports the new order and `Esc` cancels.
- [ ] Publish tab, close and add rectangles as client regions and leave empty space as caption area to `D10 T01 §19`. Done when: `Ui.Controls.TabStrip.HitRegions` finds the caption result on empty space and the client result on a tab and on the add button. Cheaper substitute: making the whole strip client area.
- [ ] Render the states tab rest, hover, pressed, active, dirty, disabled and focused in `src/ui/controls/tabstrip.cpp` from `RectoSurfaceBrush`, `RectoTextSecondaryBrush` and `RectoAccentBrush` in Light, Dark and HighContrast. Done when: the `TabStrip` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Expose the automation hook `TabStrip::automation()` returning role `Tab and TabItem` and SelectionItem state, the document title and the unsaved suffix, plus the close action through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.TabStrip.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Add the headless golden test `Ui.Controls.TabStrip.Golden` in `tests/recto-tests/ui/tabstrip_golden_test.cpp` rendering one, five and overflowing tab sets in the title bar strip with an active and a dirty tab at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.TabStrip.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: TabStrip for title-bar document tabs with close, add, overflow and reorder`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.TabStrip.*` passes and empty strip space reports caption while a tab reports client area.

## 14. `Menu` and context menu: glyphs, shortcuts, separators, submenus and check items

`src/ui/controls/menu.h` and `menu.cpp` replace WinUI `MenuFlyout`, `ContextFlyout` and `CommandBarFlyout`. A menu is an owned popup window (it can extend past the app window) on `RectoRaisedBrush` with 1 epx `RectoBorderBrush`, 8 epx radius (SPEC 7), elevation Z32, 4 epx vertical padding and 32 epx rows with 4 epx radius hover. Row layout: a 16 epx glyph column (RectoIcons via `ui::Icon`, `RectoTextSecondaryBrush`), the label, and a right-aligned shortcut column in `RectoTextSecondaryBrush` (SPEC 8.4 and 10). Item kinds: command, check, radio, separator, submenu (Segoe Fluent Icons chevron) and header. Disabled items are dimmed and skipped by activation. Keyboard: arrows, `Enter`, `Esc`, `Left` and `Right` for submenus, mnemonics (underlined access keys) and type-ahead. Submenus open after a 400 ms hover delay, offset to avoid the parent. Menus open from `DropDownButton` (`§9`), right-click and `Shift+F10`.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6, 7, 8.4, 9.3 and 10 with `design/screenshots/D-Classic.png` (File menu) and `D-Search.png` (selection context menu).
**Job:** The user runs a command from a menu or context menu. Consumers: More menu, text selection context menu, thumbnail context menu, Classic `MenuBar` (`§15`), tab context menu.
**Treatment:** A popup-window menu with glyph and shortcut columns, submenus and mnemonics. Cheaper substitute that fails the checkpoint: a native `HMENU` popup (no SPEC look, no token colours), or an in-window menu clipped by the window edge.
**Chrome:** `RectoRaisedBrush`, `RectoBorderBrush`, `RectoTextBrush`, `RectoTextSecondaryBrush`, `RectoAccentBrush` (check and radio mark), `ui::Icon`, `ui::PopupHost`, `ui::Command` of `D10 T01 §16`.

**Build order:**

1. Create `src/ui/controls/menu.h` and `menu.cpp` with `MenuItem` kinds and a `Menu::show_at(point, owner)` API. Done when: `Ui.Controls.Menu.Construct` builds every kind and returns the activated command.
2. Implement submenu cascade, hover delay, keyboard model and mnemonics. Done when: scripted keys open a submenu, move, activate and close in order.
3. Add tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.Menu.*` passes.

- [ ] Draw the popup chrome: `RectoRaisedBrush`, 1 epx `RectoBorderBrush`, 8 epx radius, Z32 elevation, 4 epx padding, minimum width 160 epx. Done when: the crop matches the SPEC 7 radii and shadow in Light and Dark.
- [ ] Draw rows: 32 epx tall, 16 epx glyph column (an empty column reserved when no item has a glyph), label, shortcut column right aligned, hover fill with 4 epx radius, accent check or radio mark in `RectoAccentBrush`. Done when: crops of command, check, radio, submenu and disabled rows match the tokens. Cheaper substitute: a glyph inside a tinted tile (SPEC 6 forbids tiles).
- [ ] Draw separators as 1 epx `RectoBorderBrush` lines with 4 epx vertical margin and optional section headers in Caption. Done when: the separator crop is one device pixel at 100 and 200 percent.
- [ ] Implement submenus: 400 ms hover open, immediate on `Right`, safe-triangle pointer tolerance, close on `Left` and `Esc`. Done when: moving diagonally toward an open submenu keeps it open in `Ui.Controls.Menu.SafeTriangle`.
- [ ] Implement keyboard: `Up` and `Down` skipping separators and disabled rows, `Enter`, `Esc`, `Home`, `End`, mnemonics with underlined letters, and type-ahead. Done when: the mnemonic `S` activates `Save` in a menu where it is unique.
- [ ] Place the popup in the monitor work area with flipping, scrolling for tall menus and return focus to the owner on close. Done when: a menu opened at the bottom right corner flips up and left and stays inside the work area.
- [ ] Wire items to `ui::Command` so enabled state follows can-execute and the shortcut text is the command's accelerator text (SPEC 8.4: every item runs the same command as the toolbar and `Ctrl+K`). Done when: disabling the command greys the item live.
- [ ] Render the states row rest, hover, pressed, disabled, focused, checked and submenu open in `src/ui/controls/menu.cpp` from `RectoRaisedBrush`, `RectoAccentBrush` and `RectoTextSecondaryBrush` in Light, Dark and HighContrast. Done when: the `Menu` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Expose the automation hook `Menu::automation()` returning role `Menu and MenuItem` and the item name, accelerator key, access key and Toggle state for check items through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.Menu.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Add the headless golden test `Ui.Controls.Menu.Golden` in `tests/recto-tests/ui/menu_golden_test.cpp` rendering a menu with glyphs, shortcuts, separators, check, radio and a submenu at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.Menu.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: Menu and context menu with glyph column, shortcuts and submenus`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.Menu.*` passes and a disabled command greys its item live.

## 15. `MenuBar` with access keys and Alt activation

`src/ui/controls/menubar.h` and `menubar.cpp` replace the WinUI `MenuBar` for the Classic preset (SPEC 8.4). The bar is a strip in the top bar slot with six top-level menus in order: File, Edit, View, Tools, Window, Help, with access keys `Alt+F`, `Alt+E`, `Alt+V`, `Alt+T`, `Alt+W`, `Alt+H`. Titles are Body text in items at least 32 epx high (SPEC 11) with 4 epx radius hover and underlined access letters when `Alt` is held or the bar is active. Pressing and releasing `Alt` alone focuses the bar; arrows move between menus and open them; hovering across the bar while a menu is open switches menus. Each opened menu is a `ui::Menu` of `§14`. Items needing a document are disabled on Home.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.4, 10 and 11 with `design/screenshots/D-Classic.png` (menu bar above the docked command bar).
**Job:** The Classic user opens commands by mouse, `Alt` or access keys. Consumers: the Classic preset top bar slot (D02 T01 §9).
**Treatment:** A real menu bar with Alt activation and six menus. Cheaper substitute that fails the checkpoint: six `DropDownButton`s with no access keys or Alt focus handling.
**Chrome:** `RectoSurfaceBrush`, `RectoBorderBrush`, `RectoTextBrush`, `RectoAccentTintBrush` (open menu title), `Menu` of `§14`.

**Build order:**

1. Create `src/ui/controls/menubar.h` and `menubar.cpp` with `MenuBarItem` titles bound to `Menu` models. Done when: `Ui.Controls.MenuBar.Construct` builds six menus in the SPEC order.
2. Implement `Alt` press-release focus, access keys `Alt+F`, `Alt+E`, `Alt+V`, `Alt+T`, `Alt+W`, `Alt+H`, arrow navigation and hover switching. Done when: scripted `Alt` then `F` opens File and `Right` opens Edit.
3. Add tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.MenuBar.*` passes.

- [ ] Draw the bar on `RectoSurfaceBrush` with a 1 epx `RectoBorderBrush` bottom edge, items with 12 epx padding and 4 epx radius hover, open item on `RectoAccentTintBrush`. Done when: crops of rest, hover and open items match the D-Classic.png metrics.
- [ ] Draw underlined access letters only while `Alt` is held or the bar is keyboard active. Done when: the crop with `Alt` shows underlines on F, E, V, T, W, H and the idle crop does not.
- [ ] Implement `Alt` press-and-release to focus the first title, `Esc` to return focus to the previous element, and `F10` as an alias. Done when: after `Esc` the previously focused element regains focus.
- [ ] Make mnemonics work from anywhere in the window through the window accelerator hook of `D10 T01 §12` so `Alt+F` opens File regardless of focus. Done when: with focus in a `TextBox`, `Alt+F` opens the File menu.
- [ ] Bind menu contents to `ui::Command`s: Open, Open Recent, Save, Save a Copy, Print, Properties, Close Tab and Exit in File (SPEC 8.4), and disable document commands on Home. Done when: on Home the document items are disabled.
- [ ] Make the bar a toolbar-style arrow group in the keyboard order after the title bar and before the command bar (SPEC 11). Done when: the Tab order test lists the bar between title bar and toolbar.
- [ ] Render the states title rest, hover, pressed, open, focused and disabled in `src/ui/controls/menubar.cpp` from `RectoSurfaceBrush`, `RectoAccentTintBrush` and `RectoTextBrush` in Light, Dark and HighContrast. Done when: the `MenuBar` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Expose the automation hook `MenuBar::automation()` returning role `MenuBar and MenuItem` and the access key and ExpandCollapse state for each title through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.MenuBar.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Add the headless golden test `Ui.Controls.MenuBar.Golden` in `tests/recto-tests/ui/menubar_golden_test.cpp` rendering the bar with File open and Alt underlines shown at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.MenuBar.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: MenuBar with six menus, access keys and Alt activation`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.MenuBar.*` passes and Alt+F opens File while a TextBox has focus.

## 16. `Flyout` popover and the floating `Card` container

`src/ui/controls/flyout.h`, `flyout.cpp`, `card.h` and `card.cpp` replace WinUI `Flyout` and `TeachingTip`. `Card` is the shared floating container: `RectoRaisedBrush`, 1 epx `RectoBorderBrush`, 8 epx radius, elevation Z32, 16 epx padding, optionally at 90 percent opacity with acrylic for the floating side panel (SPEC 8.3, falling back to opaque `RectoRaisedBrush` when transparency is off). It is used in-window by the find card, side panels, the queue card and the command box (Z64), and its appearance runs a 167 ms fade plus 8 epx translate (SPEC 12). `Flyout` hosts arbitrary content in an anchored owned popup window with the same chrome, placement (top, bottom, left, right, auto with flip), light dismiss and focus return (annotation style popover, zoom presets, redaction appearance popover). No motion when animations are off.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 4, 7, 8.2, 8.3, 9.3, 9.4 and 12 with `design/screenshots/D-Focus-Light.png` (floating side panel card) and `D-Search.png` (find card).
**Job:** The user opens popovers and floating panels. Consumers: floating side panels (D02 and D03), find card, annotation style popover, queue card (D04), command box shell (D02 T03 §12).
**Treatment:** One Card chrome shared by in-window cards and popover flyouts with SPEC motion. Cheaper substitute that fails the checkpoint: separate bespoke panels per screen, no elevation, or instant appearance ignoring the 167 ms fade and 8 epx translate.
**Chrome:** `RectoRaisedBrush`, `RectoBorderBrush`, elevation tokens Z32 and Z64, `ui::Motion` of `D10 T01 §14`, `ui::PopupHost` of `D10 T01 §15`, `ui::Painter` shadows of `D10 T01 §9`.

**Build order:**

1. Create `Card` in `src/ui/controls/card.cpp` with chrome, elevation level, padding, acrylic option and appear and disappear motion. Done when: `Ui.Controls.Card.Chrome` checks radius 8, border, padding and Z32 and Z64 shadows.
2. Create `Flyout` in `src/ui/controls/flyout.cpp` hosting content in a popup window with placement and light dismiss. Done when: `Ui.Controls.Flyout.Placement` keeps it inside the work area for each placement.
3. Add tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.Card.*:Ui.Controls.Flyout.*` passes.

- [ ] Draw the card chrome with 8 epx radius, 1 epx `RectoBorderBrush`, fill `RectoRaisedBrush`, elevation Z32 or Z64 via `ui::Painter`. Done when: crops show a key and ambient shadow matching the recorded token values in Light and Dark. Cheaper substitute: a flat bordered rectangle.
- [ ] Implement the acrylic option with Gaussian blur plus tint at 90 percent, falling back to opaque `RectoRaisedBrush` when transparency effects are off or battery saver is on. Done when: with transparency off the card renders opaque.
- [ ] Implement the appear motion: opacity 0 to 1 and 8 epx translate over 167 ms with Fluent easing, reversed on close, none when animations are off. Done when: a fake clock test samples the expected opacity and offset at 0, 83 and 167 ms.
- [ ] Implement `Flyout` placement (top, bottom, left, right, auto), flip and clamp to the work area, and an anchor offset of 8 epx. Done when: an anchor at the right edge opens leftwards.
- [ ] Implement light dismiss (outside click, `Esc`, deactivation), focus move into the flyout on open and focus return to the anchor on close. Done when: `Esc` returns focus to the anchor and an outside click closes without activating the clicked control twice.
- [ ] Support non-modal in-window cards (find card, queue card) that close on `Esc` (topmost first, SPEC 10) without light dismiss. Done when: two stacked cards close in reverse order on repeated `Esc`.
- [ ] Render the states appearing, shown, disappearing, disabled content and focused content in `src/ui/controls/card.cpp` from `RectoRaisedBrush`, `RectoBorderBrush` and the elevation tokens in Light, Dark and HighContrast. Done when: the `Card` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Expose the automation hook `Flyout::automation()` returning role `Pane or Window (popup) named after its content` and the owner's anchor relationship for UIA navigation through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.Flyout.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Add the headless golden test `Ui.Controls.Card.Golden` in `tests/recto-tests/ui/card_golden_test.cpp` rendering a card at Z32 and Z64, an acrylic card and a flyout in three placements at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.Card.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: Flyout popover and floating Card container with SPEC motion`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.Card.*` passes and with transparency effects off the acrylic card renders opaque.

## 17. `Dialog`: modal overlay, smoke, alert and form layouts and Fluent motion

`src/ui/controls/dialog.h` and `dialog.cpp` replace WinUI `ContentDialog` and `RectoDialogStyles.xaml` with the shared Dialog chrome of SPEC 9.9. A dialog sits on the root overlay layer (inside the window, `D10 T01 §15`), on `RectoRaisedBrush` with 8 epx radius, 28 epx padding, elevation Z64, over a smoke layer (black at the Fluent smoke alpha) that blocks input to the shell. Two layouts: alert, with a 24 epx status glyph centred (colour `RectoDangerBrush`, `RectoWarningBrush` or `RectoAccentBrush`, never in a tile), title, text, and full-width stacked buttons with the primary on top; and form, left aligned with Cancel and the primary button side by side at the bottom right. `Esc` closes (Cancel), `Enter` activates the primary; focus is trapped in the dialog, initial focus goes to the primary or the first field, and focus returns to the opener on close. Open and close use the stock ContentDialog motion: scale from 0.96 and fade in 250 ms, 167 ms fade out, none when animations are off.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6, 7, 9.9, 11 and 12 with `design/screenshots/D-Dialogs.png` (Password, Could not open, Replace original, Unsaved changes, Sign document).
**Job:** The user answers a modal prompt or fills a modal form. Consumers: shell dialogs (D02 T01 §10), Replace original and Unsaved changes, Sign, Redact, Watermark, Crop and Optimize dialogs (D03, D05), Link in a document.
**Treatment:** One Dialog control with alert and form layouts, smoke, focus trap and ContentDialog motion. Cheaper substitute that fails the checkpoint: per-dialog hand-made popups, a dialog without focus trap, or alert buttons side by side.
**Chrome:** `RectoRaisedBrush`, `RectoBorderBrush`, `RectoDangerBrush`, `RectoWarningBrush`, `RectoAccentBrush`, elevation Z64, `Button` family of `§2`, `Card` chrome of `§16`, focus scope of `D10 T01 §13`.

**Build order:**

1. Create `src/ui/controls/dialog.h` and `dialog.cpp` with `Dialog::show(layout, title, content, buttons)` returning an awaitable result. Done when: `Ui.Controls.Dialog.Result` returns the pressed button identifier for a scripted click.
2. Implement the smoke layer, focus trap, `Esc` and `Enter` handling and focus return. Done when: Tab cycles inside the dialog and never reaches the shell.
3. Implement alert and form layouts and motion, then add tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.Dialog.*` passes.

- [ ] Draw the dialog chrome: 8 epx radius, 28 epx padding, Z64 elevation, width 320 to 548 epx and the smoke layer in front of the shell. Done when: the crop matches the D-Dialogs.png metrics in Light and Dark. Cheaper substitute: reusing the 16 epx card padding.
- [ ] Implement the alert layout: 24 epx status glyph centred in the status colour with no tile or circle (SPEC 6 and 9.9), centred title (Subtitle) and text, full-width stacked buttons with the primary on top. Done when: Password, Could not open and Replace original sample crops match, with the right glyph colour token.
- [ ] Implement the form layout: left aligned title and content region, footer with Cancel and the primary button side by side at the right. Done when: the Sign document sample crop shows both buttons on one row, primary on the right.
- [ ] Implement a modal focus scope with focus trap, initial focus on the primary button or first field, and focus return to the opener. Done when: `Ui.Controls.Dialog.FocusTrap` shows Tab and Shift+Tab never leave and the opener is refocused on close.
- [ ] Implement `Esc` as cancel and `Enter` as the primary action unless focus is in a multi-line field or on another button. Done when: scripted `Esc` returns Cancel and `Enter` returns Primary.
- [ ] Implement the stock ContentDialog motion (scale 0.96 to 1 with 250 ms fade in, 167 ms fade out) and smoke fade, none when animations are off (SPEC 12). Done when: a fake clock samples opacity and scale at 0, 125 and 250 ms.
- [ ] Support a destructive primary (`DangerButton` of `§2`), a disabled primary until valid, and the three-button Unsaved changes alert (Save on top, then Discard, then Cancel, SPEC 9.9). Done when: the three-button crop stacks the buttons in that order.
- [ ] Render the states opening, shown, closing, primary disabled and focused button in `src/ui/controls/dialog.cpp` from `RectoRaisedBrush`, `RectoAccentBrush` and `RectoDangerBrush` in Light, Dark and HighContrast. Done when: the `Dialog` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Expose the automation hook `Dialog::automation()` returning role `Window (modal dialog)` and the title as name, the content text as the description and each button's name through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.Dialog.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Add the headless golden test `Ui.Controls.Dialog.Golden` in `tests/recto-tests/ui/dialog_golden_test.cpp` rendering the alert and form layouts with smoke over the shell backdrop at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.Dialog.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: Dialog with smoke, alert and form layouts, focus trap and ContentDialog motion`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.Dialog.*` passes and Tab never leaves the dialog.

## 18. `SegmentedControl` and the underlined mode switcher

`src/ui/controls/segmentedcontrol.h` and `modeswitcher.h` (with `.cpp`) replace WinUI `SelectorBar`, `Pivot` and the segmented controls. `SegmentedControl` is a track with a `RectoBorderStrongBrush` hairline and 4 epx radius, 32 epx tall, equal-width segments, and a selected segment raised on `RectoRaisedBrush` (or `RectoAccentTintBrush` with accent text, per the SPEC board) that slides over 167 ms. It serves Organize Combine, Split and Reorder, Settings theme and preset options, and Convert option groups. `ModeSwitcher` is the Reading room title bar switcher (Home, Read, Convert, Organize, SPEC 8.1): text items with a 2 epx `RectoAccentBrush` underline indicator that slides to the selected item. Both are single tab stops with arrow navigation and automation as a selection list.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6, 7, 8.1 and 9.6 with `design/screenshots/D-Organize.png` (segmented Combine, Split, Reorder) and `D-ReadingRoom.png` (mode switcher in the title bar).
**Job:** The user switches mode or option within a view. Consumers: Organize (D05), Reading room title bar (D02 T01 §4), Settings (D02 T04).
**Treatment:** Two concrete controls with the SPEC look. Cheaper substitute that fails the checkpoint: a row of toggle buttons without a shared selection model, or the accent underline replaced by a bold label.
**Chrome:** `RectoBorderStrongBrush`, `RectoRaisedBrush`, `RectoAccentBrush`, `RectoAccentTintBrush`, `RectoTextBrush`, `RectoTextSecondaryBrush`, `Button` states of `§2`.

**Build order:**

1. Create `SegmentedControl` and `ModeSwitcher` in `src/ui/controls/` with a shared `SelectionModel`. Done when: `Ui.Controls.Segmented.Selection` raises one change event per selection.
2. Implement arrow-key roving selection, indicator slide and DPI-snapped geometry. Done when: indicator rectangles are whole pixels at 125 and 175 percent.
3. Add tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.Segmented.*:Ui.Controls.ModeSwitcher.*` passes.

- [ ] Draw the `SegmentedControl` track and segments: 32 epx tall, 4 epx radius, equal widths with a 64 epx minimum, selected segment raised with the accent text. Done when: crops for two to four segments match D-Organize.png in Light and Dark.
- [ ] Animate the selection indicator sliding over 167 ms with Fluent easing and none when animations are off. Done when: a fake clock samples the indicator at 0, 83 and 167 ms.
- [ ] Draw `ModeSwitcher` items (Home, Read, Convert, Organize) in Body text with the 2 epx `RectoAccentBrush` underline under the selected item and `RectoTextSecondaryBrush` for the others, 36 epx hit targets. Done when: the crop matches D-ReadingRoom.png.
- [ ] Support glyph plus label items (RectoIcons 16 epx) and icon-only items with required name and tooltip. Done when: an icon-only segment without a name asserts in Debug.
- [ ] Render the states segment rest, hover, pressed, selected, disabled and focused in `src/ui/controls/segmentedcontrol.cpp` from `RectoRaisedBrush`, `RectoAccentTintBrush` and `RectoBorderStrongBrush` in Light, Dark and HighContrast. Done when: the `SegmentedControl` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Keyboard: one tab stop, `Left` and `Right` select the neighbour (selection follows focus), `Home` and `End` jump, disabled segments skipped. Done when: scripted keys reach the expected index.
- [ ] Expose the automation hook `SegmentedControl::automation()` returning role `RadioButton group or TabItem list` and SelectionItem state, position in set and the segment name through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.SegmentedControl.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Give each segment a tooltip when its label is truncated or icon-only. Done when: truncated labels show the full text in a tooltip.
- [ ] Add the headless golden test `Ui.Controls.SegmentedControl.Golden` in `tests/recto-tests/ui/segmentedcontrol_golden_test.cpp` rendering a three segment control and the mode switcher in each selection at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.SegmentedControl.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: SegmentedControl and underlined ModeSwitcher`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.Segmented.*` passes and indicator rectangles are whole device pixels at 125 and 175 percent.

## 19. `NavigationRail`, `Expander`, `SettingsGroup` and `SettingsCard`

`src/ui/controls/navigationrail.h`, `expander.h`, `settingsgroup.h` and `settingscard.h` (with `.cpp`) replace WinUI `NavigationView`, `Expander` and the Community Toolkit `SettingsCard`. `NavigationRail` is the Classic left rail (Home, Convert, Organize, Read): a 48 epx wide column on `RectoSurfaceBrush` with 20 epx RectoIcons glyphs (SPEC 6), selected item on `RectoAccentTintBrush` with an accent Filled glyph and a 3 epx accent bar, tooltips from `§3`. `Expander` is a header row with a Segoe Fluent Icons chevron that reveals content with a 167 ms height animation. `SettingsGroup` is a 12 px Semibold all caps group label (letter spacing 0.6, SPEC 5) above an 8 epx radius card of rows; `SettingsCard` is a row of at least 56 epx with a 16 epx glyph column (optional), title (Body), description (Caption, `RectoTextSecondaryBrush`), and a right-aligned action (switch, combo box, button). The managed-by-policy state disables the row and shows a 16 epx `lock_shield` glyph with `Managed by your organization` helper text (SPEC 9.7); the row remains findable by `Find a setting`.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 5, 6, 7, 8 and 9.7 with `design/screenshots/D-Classic.png` (navigation rail), `D-Settings.png` and `D-Layout.png` (settings groups and cards).
**Job:** The Classic user switches destination; the Settings user changes options in grouped cards. Consumers: Classic shell (D02 T01 §4), Settings page (D02 T04 §4), Convert option groups, About details.
**Treatment:** Four concrete controls with the SPEC group label and policy lock state. Cheaper substitute that fails the checkpoint: plain stacked labels and controls with no group card, or a managed setting hidden instead of shown disabled with the lock glyph and note.
**Chrome:** `RectoSurfaceBrush`, `RectoRaisedBrush`, `RectoBorderBrush`, `RectoAccentTintBrush`, `RectoAccentBrush`, `RectoTextSecondaryBrush`, `ui::Icon` (`lock_shield`), the Group label `ui::TextStyle` of `D10 T01 §10`.

**Build order:**

1. Create the four controls in `src/ui/controls/` with item models. Done when: `Ui.Controls.Settings.Construct` builds a group with three card kinds.
2. Implement the rail selection and expander animation. Done when: `Ui.Controls.NavigationRail.Selection` and `Ui.Controls.Expander.Toggle` pass.
3. Implement the managed state and add tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.NavigationRail.*:Ui.Controls.Expander.*:Ui.Controls.Settings.*` passes.

- [ ] Draw `NavigationRail` at 48 epx width with 20 epx glyphs, 40 epx item height, selected tint with the Filled accent glyph and 3 epx bar. Done when: crops match the D-Classic.png metrics in Light and Dark. Cheaper substitute: 16 epx glyphs (SPEC 6 sets 20 epx in the rail).
- [ ] Give rail items names and tooltips and roving arrow keys (`Up`, `Down`, `Home`, `End`, `Enter`). Done when: `Ui.Controls.NavigationRail.Keyboard` selects the expected item and every item has a tooltip.
- [ ] Draw `Expander` with a header row, chevron rotation and a 167 ms height animation (none when animations are off), keyboard `Enter` and `Space`. Done when: collapsed and expanded crops match and a fake clock samples intermediate heights.
- [ ] Draw `SettingsGroup`: a 12 px Semibold all caps label with 0.6 letter spacing above an 8 epx radius card of rows separated by 1 epx `RectoBorderBrush` lines (SPEC 5 and 7). Done when: the label crop shows caps and spacing and the card radius is 8 epx.
- [ ] Draw `SettingsCard` rows: minimum height 56 epx, 16 epx side padding, optional 16 epx glyph with no tile, Body title, Caption description and a right action host. Done when: crops with each action type (switch, combo box, button) match D-Settings.png.
- [ ] Implement the managed-by-policy state: disabled row, 16 epx `lock_shield` glyph, `Managed by your organization` helper text, and a `Managed` marker exposed to the search filter (SPEC 9.7). Done when: the managed crop shows glyph and note and the action is not focusable.
- [ ] Provide a `filter(text)` visibility API on `SettingsGroup` so `Find a setting` hides non-matching rows and empty groups while keeping managed rows findable. Done when: filtering `cache` leaves only matching rows and managed rows still match.
- [ ] Render the states row rest, hover, pressed (clickable rows), disabled, managed and focused in `src/ui/controls/settingscard.cpp` from `RectoRaisedBrush`, `RectoBorderBrush` and `RectoTextSecondaryBrush` in Light, Dark and HighContrast. Done when: the `SettingsCard` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Expose the automation hook `SettingsCard::automation()` returning role `Group and ListItem` and the card title as name, the description as help text and the managed state in the name through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.SettingsCard.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Add the headless golden test `Ui.Controls.SettingsGroup.Golden` in `tests/recto-tests/ui/settingsgroup_golden_test.cpp` rendering a Layout group, a Theme group and a managed row, plus the navigation rail at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.SettingsGroup.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: NavigationRail, Expander, SettingsGroup and SettingsCard`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.Settings.*` passes and the managed row is disabled but still matched by the filter.

## 20. `InfoBar`, in-app `Toast`, and the `Chip`, `Badge` and `Keycap` pills

`src/ui/controls/infobar.h`, `toast.h`, `chip.h`, `badge.h` and `keycap.h` (with `.cpp`) replace WinUI `InfoBar`, the in-app toast and the `RectoChipStyle` base. `InfoBar` is the quiet banner of the signature status (SPEC 8.2 item 5): `RectoSurfaceBrush`, a status badge glyph in `RectoSuccessBrush`, `RectoWarningBrush` or `RectoDangerBrush` with text (never colour alone, SPEC 11), message, action buttons (`View Signatures`, `Sign`) and a close button. `Toast` is the in-app notification at the bottom of the window on `RectoRaisedBrush`, 8 epx radius, Z32, shown for about 5 s with a message and an action (SPEC 9.9). `Chip`, `Badge` and `Keycap` are the full-pill primitives of SPEC 7 (radius = half the height) and the only pills: filter chips with optional close, status badges, count badges and counters, and shortcut keycaps (Caption, `RectoBorderBrush` outline). The `RectoChip` variants for D02 are built later by D02 T01 §19 on these primitives.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 7, 8.2, 9.4 and 9.9 with `design/screenshots/D-Dialogs.png` (keycaps, toast), `D-Focus-Light.png` (signature banner) and `D-Organize.png` (chips, badges).
**Job:** The user reads status, a notice or a shortcut hint and filters by chip. Consumers: signature banner (D05), conversion toast (D04), command box keycaps (D02 T03), Organize page badges, Convert file chips, side panel count badges.
**Treatment:** Banner, toast and the three pill primitives built once. Cheaper substitute that fails the checkpoint: rounded rectangles with a literal radius per screen, or a status banner that uses colour only.
**Chrome:** `RectoSurfaceBrush`, `RectoRaisedBrush`, `RectoBorderBrush`, `RectoSuccessBrush`, `RectoWarningBrush`, `RectoDangerBrush`, `RectoAccentTintBrush`, the pill radius token, Caption `ui::TextStyle`, `Button` of `§2`.

**Build order:**

1. Create the five controls in `src/ui/controls/`. Done when: `Ui.Controls.Pills.Construct` builds each.
2. Implement `InfoBar` severity, actions and close, `Toast` queue and timing, and the pills with the half-height radius token. Done when: `Ui.Controls.Pills.Radius` asserts radius equals half the height for every pill size.
3. Add tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.InfoBar.*:Ui.Controls.Toast.*:Ui.Controls.Pills.*` passes.

- [ ] Draw `InfoBar` with a status glyph in the severity colour, bold title, message, action buttons and a close button, on `RectoSurfaceBrush` with a 1 epx `RectoBorderBrush` edge. Done when: success, warning and danger crops each show glyph and text and match D-Focus-Light.png. Cheaper substitute: a coloured strip with no glyph.
- [ ] Implement `Toast` placement at the bottom centre of the window, 16 epx above the edge, with the 167 ms appear motion of `§16`, a 5 s auto-dismiss that pauses on hover or focus, an action button and queueing. Done when: a fake clock dismisses after 5 s unless hovered.
- [ ] Announce toasts and info bar changes to assistive technology as notifications (the `D10 T03 §4` hook). Done when: showing a toast calls the automation notification hook with the message.
- [ ] Draw `Chip` (pill, 24 epx tall, Caption, optional leading glyph and close button, filled `RectoAccentTintBrush` when selected) with the half-height radius, never a literal. Done when: chip crops in rest, selected and removable states match the tokens.
- [ ] Draw `Badge` and counters (pill, minimum 16 epx, status colours with text or glyph) and `Keycap` (pill, 1 epx `RectoBorderBrush`, Caption, 4 epx horizontal padding). Done when: `Ctrl` `K` keycap crops match D-Dialogs.png.
- [ ] Expose the primitives as a stable API (`ui::Chip`, `ui::Badge`, `ui::Keycap`) for `RectoChip` in D02 T01 §19 and add a no-literal-radius audit. Done when: `Audit.NoCornerRadiusLiterals` finds none under `src/ui/controls/`.
- [ ] Render the states bar rest, hover on actions, disabled actions, focused and dismissing in `src/ui/controls/infobar.cpp` from `RectoSurfaceBrush`, `RectoSuccessBrush`, `RectoWarningBrush` and `RectoDangerBrush` in Light, Dark and HighContrast. Done when: the `InfoBar` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Keyboard: the close button and actions are tab stops, `Esc` dismisses the topmost toast or info bar when focused, and chips are removable with `Delete`. Done when: scripted keys dismiss each.
- [ ] Expose the automation hook `InfoBar::automation()` returning role `Pane or Text with an alert live region` and the severity and message in the name and the action buttons' names through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.InfoBar.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Give icon-only close buttons an automation name and tooltip. Done when: `Ui.Controls.InfoBar.CloseNamed` asserts both.
- [ ] Add the headless golden test `Ui.Controls.InfoBar.Golden` in `tests/recto-tests/ui/infobar_golden_test.cpp` rendering three severities, a toast, chips, badges and keycaps at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.InfoBar.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: InfoBar, Toast and the Chip, Badge and Keycap pill primitives`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.Pills.*` passes and the radius test fails if a pill uses a literal radius.

## 21. `SearchBox` with a suggestion list for the command box and Find a setting

`src/ui/controls/searchbox.h` and `searchbox.cpp` replace WinUI `AutoSuggestBox`. It combines a `TextBox` (`§4`, leading `search` glyph, clear button) with a suggestion list from `ItemsView` (`§11`) in an owned popup window (the floating command box and the Home inline box stay in-window as `Card`s using the same list). Rows follow SPEC 9.4: a 16 epx glyph drawn directly on the row in `RectoTextSecondaryBrush`, the label with the typed characters in bold, a right-aligned shortcut as `Keycap` pills of `§20`, section headers (This document, Go to, Files, Actions), the selected row on `RectoAccentTintBrush` with its glyph in `RectoAccentBrush`. Arrow keys move through suggestions while focus stays in the text field (Fluent behaviour), `Enter` chooses, `Esc` closes, and text change events are debounced for async providers. The same control serves `Find a setting` in Settings and the find query.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6, 7, 9.1, 9.4 and 9.7 with `design/screenshots/D-Dialogs.png` (command box, top centre), `D-Home.png` (inline box) and `D-Settings.png` (Find a setting).
**Job:** The user searches and chooses an action, file, page or setting. Consumers: command box (D02 T03 §12), Home inline box, Settings Find a setting (D02 T04), page number entry.
**Treatment:** An AutoSuggestBox equivalent with SPEC rows, bold typed characters and keycaps. Cheaper substitute that fails the checkpoint: a plain TextBox with a separate list that moves focus away from the field, or rows with a tinted glyph tile.
**Chrome:** `RectoRaisedBrush`, `RectoAccentTintBrush`, `RectoAccentBrush`, `RectoTextBrush`, `RectoTextSecondaryBrush`, `Keycap` of `§20`, `TextBox` of `§4`, `ItemsView` of `§11`, `ui::PopupHost`.

**Build order:**

1. Create `src/ui/controls/searchbox.h` and `searchbox.cpp` with `SuggestionProvider` (sync and async), sections and result rows. Done when: `Ui.Controls.SearchBox.Provider` shows results for a typed query and cancels the stale async query.
2. Implement the keyboard model keeping focus in the field, bold typed characters and keycaps. Done when: scripted keys reach and choose the third result.
3. Add tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.SearchBox.*` passes.

- [ ] Embed `TextBox` with a leading `search` glyph and a clear button (`dismiss`, tooltip `Clear`) and show the suggestion popup on text change or `Down`. Done when: typing shows the list and clearing hides it.
- [ ] Draw suggestion rows: 16 epx glyph directly on the row (no tile), label with the matched characters in BodyStrong, shortcut keycaps right aligned, section headers in Caption. Done when: row crops match D-Dialogs.png in Light and Dark. Cheaper substitute: bolding the whole label.
- [ ] Highlight the selected row with `RectoAccentTintBrush` and an accent glyph, and make row selection by arrows keep text focus. Done when: `Down` twice selects row 2 and the caret remains in the field.
- [ ] Implement matching: case-insensitive subsequence ranking with bold ranges returned to the row, a 120 ms debounce for async providers and cancellation of stale queries. Done when: typing `ocr` bolds `OCR` in `Searchable PDF (OCR)` and a slow stale query result is dropped.
- [ ] Implement `Enter` (choose), `Esc` (close, then clear on a second press) and result activation by click. Done when: scripted keys produce the chosen item and the closing order.
- [ ] Support the inline mode (Home, 720 epx wide) and the floating command box mode (560 to 720 epx, Z64, radius 8) as hosts of the same list. Done when: both modes render the same rows in `Ui.Controls.SearchBox.Hosts`.
- [ ] Render the states field rest, hover, focused, disabled, and suggestion row rest, hover, selected in `src/ui/controls/searchbox.cpp` from `RectoRaisedBrush`, `RectoAccentTintBrush` and `RectoAccentBrush` in Light, Dark and HighContrast. Done when: the `SearchBox` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Expose the automation hook `SearchBox::automation()` returning role `ComboBox (edit) with a List popup` and ExpandCollapse state, the selected suggestion and a status notification such as `3 results` through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.SearchBox.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Announce result count changes to assistive technology (the `D10 T03 §4` hook). Done when: typing triggers a count notification once per settled query.
- [ ] Add the headless golden test `Ui.Controls.SearchBox.Golden` in `tests/recto-tests/ui/searchbox_golden_test.cpp` rendering empty, typing with sectioned results, selected row and a no-results state at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.SearchBox.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: SearchBox with suggestion list for the command box and Find a setting`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.SearchBox.*` passes and focus remains in the text field while arrows move the selected suggestion.

## 22. `ColorSwatchPicker` from token swatches

`src/ui/controls/colorswatchpicker.h` and `colorswatchpicker.cpp` replace the swatch use of WinUI `ColorPicker` in the annotation style popover (SPEC 8.2 item 1). The picker is a grid of 24 epx circular swatches (the swatch circle is the colour itself, not a glyph tile) drawn from a token palette (the accent, status colours, text colour and the highlight palette defined in `design/tokens/colors.json`), with a 2 epx `RectoAccentBrush` selection ring, and no free colour entry. It lives in a `Flyout` of `§16`, supports roving arrow-key navigation across rows and columns, and exposes the chosen `ui::Color`. HighContrast shows the swatches with a system-colour outline and a check mark so selection never relies on colour alone.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 4, 8.2 and 11 with `design/screenshots/D-Focus-Light.png` (annotation style popover swatches).
**Job:** The user picks a markup, stroke or fill colour for annotations. Consumers: Markup and Annotation Bar style popover (D03), Redaction Appearance popover (fill colour).
**Treatment:** A fixed token-driven swatch grid. Cheaper substitute that fails the checkpoint: swatches with hex literals in the control, or a free colour wheel the SPEC does not ask for.
**Chrome:** palette tokens from `design/tokens/colors.json`, `RectoAccentBrush` (ring), `RectoBorderStrongBrush`, `Flyout` of `§16`.

**Build order:**

1. Create `src/ui/controls/colorswatchpicker.h` and `colorswatchpicker.cpp` with a palette provider read from generated tokens. Done when: `Ui.Controls.ColorSwatchPicker.Palette` lists the token swatches without any literal in the control.
2. Implement selection, ring drawing and roving keyboard. Done when: scripted arrows select the expected swatch and the ring crop matches.
3. Add tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.ColorSwatchPicker.*` passes.

- [ ] Draw a grid of 24 epx circular swatches with 8 epx gaps, a 1 epx `RectoBorderStrongBrush` outline for light swatches and a 2 epx `RectoAccentBrush` ring (4 epx offset) on the selected swatch. Done when: crops match the tokens in Light and Dark.
- [ ] Add a check glyph (Segoe Fluent Icons) inside the selected swatch and a system-colour outline in HighContrast so selection is not colour only (SPEC 11). Done when: the HighContrast crop shows the check and outline.
- [ ] Read the palette from the generated token header, with a per-purpose subset (highlight, stroke, fill) selected by an enum. Done when: the highlight purpose offers the search-hit-family colours and the fill purpose offers the stroke palette.
- [ ] Implement pointer selection, hover ring and a `colour_changed` event feeding the annotation style. Done when: clicking raises one event with the colour.
- [ ] Render the states swatch rest, hover, pressed, selected, disabled and focused in `src/ui/controls/colorswatchpicker.cpp` from `RectoAccentBrush` and `RectoBorderStrongBrush` in Light, Dark and HighContrast. Done when: the `ColorSwatchPicker` golden crops for every listed state differ from each other, match the token values in all three themes, and no hex literal exists in the file. Cheaper substitute: hard-coded colours or a Light-only render.
- [ ] Keyboard: arrow keys move in the grid, `Enter` or `Space` selects, one tab stop for the grid. Done when: `Ui.Controls.ColorSwatchPicker.Keyboard` selects the expected swatch.
- [ ] Expose the automation hook `ColorSwatchPicker::automation()` returning role `List with ListItem` and each swatch's colour name (for example `Vermilion`) as name and SelectionItem state through the `ui::Element` automation properties of `D10 T01 §6`. Done when: `Ui.Controls.ColorSwatchPicker.Automation` asserts the role, a non-empty name and the state that `D10 T03 §2` wraps in a UIA provider. Cheaper substitute: leaving the name empty and fixing it in the UIA layer.
- [ ] Give every swatch a tooltip with its colour name. Done when: the tooltip text is non-empty for all swatches.
- [ ] Add the headless golden test `Ui.Controls.ColorSwatchPicker.Golden` in `tests/recto-tests/ui/colorswatchpicker_golden_test.cpp` rendering the grid for each purpose with a selected swatch at 100 and 200 percent in Light, Dark and HighContrast through the harness of `D10 T01 §1`. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.ColorSwatchPicker.Golden` passes and fails when a token colour or a metric changes by one unit. Cheaper substitute: asserting only that rendering does not crash.
- [ ] Commit: `ui: ColorSwatchPicker from token swatches`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.ColorSwatchPicker.*` passes and the control contains no colour literal.

## 23. Control gallery: every control in Light, Dark and HighContrast with golden images

`src/ui/gallery/` holds the control gallery: a small executable `recto-ui-gallery` and a headless test page that render every control of this file in every state, in Light, Dark and HighContrast, at 100, 150 and 200 percent. It is the single place where look drift is caught: each control section's golden test renders its own crop, and the gallery renders the composed board and compares it to stored golden images through the golden-image suite of `D10 T01 §20`. The executable also lets a reviewer open the real window and operate every control (tab order, popups, dialogs, IME) on a display-session host. `D10 T03 §2` needs the gallery before it wires UIA patterns, and a control that is missing from the gallery fails the coverage test.

> **Corrected 2026-10-23:** after `D10 T04 §1` the gallery lives in `libs/vellum/gallery/` as `vellum-gallery`, and read `Ui.Gallery.*` and other `Ui.*` filters in this file as `Vellum.*` in `vellum-tests.exe`. Its goldens render with Vellum's neutral test theme (`D10 T04 §3`), so no Vellum golden depends on Recto's palette; Recto's own look is proved by the board checks of `D06 T04 §1` against `src/app/theme/recto_theme.json` (D24).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 4 to 12 with every `design/screenshots/D-*.png` board that shows a control (`D-Focus-Light.png`, `D-Focus-Dark.png`, `D-Classic.png`, `D-Dialogs.png`, `D-Settings.png`, `D-Organize.png`); a new internal surface that no SPEC board shows.
**Job:** The developer and reviewer inspect and verify every control and state. Consumers: `D10 T03 §2` (UIA patterns), D06 T04 design boards, every later screen section as the reference for which control to use.
**Treatment:** One gallery page per control group, rendered headless and interactive. Cheaper substitute that fails the checkpoint: golden images of individual controls only, with no composed board, so a layout or theme interaction regression passes.
**Chrome:** all control sections of this file, tokens of `D10 T01 §8`, the golden harness of `D10 T01 §20`.

**Build order:**

1. Create `src/ui/gallery/main.cpp`, `gallery_pages.cpp` and one page per control group registering through a `GalleryRegistry`. Done when: `recto-ui-gallery.exe` starts and lists every registered control.
2. Add the headless runner rendering every page in Light, Dark and HighContrast at 100, 150 and 200 percent and comparing to `tests/golden/ui-controls/`. Done when: `recto-tests.exe --gtest_filter=Ui.Gallery.*` passes.
3. Add the coverage test that every `ui::Control` subclass is in the gallery. Done when: adding an unregistered control fails `Ui.Gallery.Coverage`.

- [ ] Create `GalleryRegistry` and the page scaffold in `src/ui/gallery/` with a theme switcher (Light, Dark, HighContrast), scale selector and state forcing for pressed, hover and focus. Done when: the executable shows each control and the theme switcher repaints live.
- [ ] Author pages: Buttons and Toggles, Text input, Selection, Range and Progress, Lists and Trees, Tabs and Menus, Overlays (Flyout, Dialog, Toast), Navigation and Settings, Pills and Chips, Search and Color. Done when: every control of `§2` to `§22` appears with all its states on a page.
- [ ] Add the coverage test using a build-time list of `ui::Control` subclasses. Done when: `Ui.Gallery.Coverage` fails after temporarily adding an unregistered control and passes after registering it.
- [ ] Render each page headless in Light, Dark and HighContrast at 100, 150 and 200 percent and save actuals to `build/golden-actual/` on mismatch. Done when: the nine renders per page match the stored goldens within the tolerance of `D10 T01 §20`.
- [ ] Generate the golden images in `tests/golden/ui-controls/` with a record of the font and DirectWrite version. Done when: the goldens are reproduced byte-stable on a second run on the same machine.
- [ ] Add a keyboard-only traversal script in the interactive gallery covering every page and report unreachable controls. Done when: the script reaches each control by Tab and arrows and reports zero unreachable ones.
- [ ] Add the token audit: no colour or radius literal anywhere under `src/ui/controls/` and `src/ui/gallery/`. Done when: `Audit.UiControlsNoLiterals` passes and fails when a literal is added.
- [ ] Add an accessibility smoke in the gallery that lists controls without automation names. Done when: the list is empty for the gallery build.
- [ ] Commit: `ui: control gallery with golden images in Light, Dark and HighContrast`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Gallery.*` passes and the coverage test fails for an unregistered control.

## 24. Spell checking for `TextBox` through the Windows Spell Checking API

`src/ui/controls/spell_check.h` and `spell_check.cpp` give `ui::TextBox` (`§4`) the spell checking WinUI's `TextBox.IsSpellCheckEnabled` provided, through the Windows Spell Checking API (`ISpellCheckerFactory`, `ISpellChecker::ComprehensiveCheck`, `ISpellChecker::Suggest`, `ISpellChecker::Add`, `ISpellChecker::Ignore`). Checking runs on idle after an edit, never on every keystroke, over the changed paragraph only; misspelled ranges are drawn by the `TextBox` as the Fluent red wavy underline in `RectoDangerBrush`; right-clicking or `Shift+F10` on a misspelled word opens a `ui::Menu` (`§14`) with up to five suggestions, Ignore and Add to dictionary above the normal Cut, Copy and Paste items. The checker language follows the field's language tag, then the current input language (`GetKeyboardLayout` mapped through `LCIDToLocaleName`), then the Windows display language. A field opts in through `TextBox::set_spell_check(true)`; password and number fields never check. Consumers: the Edit PDF paragraph editor, form text fields, annotation note editors and the spell-check command of `D03` (they consume this section instead of an app-side adorner).

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 4, 6, 8.2 and 11 (spell check concept glyph `text_proofing_tools`); the wavy underline follows WinUI's `TextBox` proofing look. Genuinely new on the boards: no D board shows a misspelling, so the golden reference is a reviewed baseline.
**Job:** The user sees misspelled words in editable text and fixes them from suggestions. Consumers: D03 Edit PDF text editing, form fields, note and comment editors.
**Treatment:** Windows spell checker with idle, paragraph-scoped checking, a wavy underline and a suggestion menu. Cheaper substitute that fails the checkpoint: checking the whole field on every keystroke, a straight underline, or a bundled dictionary instead of the Windows one.
**Chrome:** `RectoDangerBrush` (underline), `ui::Menu` of `§14`, `ui::TextBox` of `§4`, `ui::Dispatcher` idle timer of `D10 T01 §3`.

**Build order:**

1. Create `src/ui/controls/spell_check.h` and `spell_check.cpp` wrapping `ISpellCheckerFactory` and `ISpellChecker` per language, with a cache of checkers. Done when: `Ui.Controls.SpellCheck.Detects` reports `teh` as misspelled and `the` as correct for `en-US`.
2. Hook `TextBox` edits to an idle check over the changed paragraph and draw wavy underlines. Done when: `Ui.Controls.SpellCheck.IdleScope` shows one check call per idle period covering only the edited paragraph.
3. Add the suggestion menu and tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.SpellCheck.*` passes.

- [ ] Create `ui::SpellService` in `src/ui/controls/spell_check.cpp` over `CoCreateInstance(CLSID_SpellCheckerFactory)` with `IsSupported` checks and a per-language `ISpellChecker` cache. Done when: an unsupported language returns no checker and no error dialog. Source: Microsoft Learn "Spell Checking API".
- [ ] Run `ComprehensiveCheck` on idle (`D10 T01 §3` timer, 300 ms after the last edit) over the edited paragraph only. Done when: `Ui.Controls.SpellCheck.IdleScope` passes. Cheaper substitute: checking the whole field on each keystroke.
- [ ] Draw misspelled ranges in `src/ui/controls/textbox.cpp` as a 1 epx wavy underline in `RectoDangerBrush` (system colour in HighContrast), following text wrapping and scrolling. Done when: the golden crop shows the wave under exactly the misspelled glyph run in Light, Dark and HighContrast. Cheaper substitute: a straight underline.
- [ ] Add suggestions, Ignore and Add to dictionary to the `TextBox` context menu (`§14`) when the caret or pointer is on a misspelled word, applying a suggestion as one undoable edit. Done when: `Ui.Controls.SpellCheck.Suggest` replaces the word and `Ctrl+Z` restores it.
- [ ] Resolve the checker language from the field language tag, then the input language, then the display language, and re-check when the input language changes (`WM_INPUTLANGCHANGE`). Done when: `Ui.Controls.SpellCheck.Language` picks `fr-FR` for a field tagged `fr-FR` on an English system.
- [ ] Never spell check `PasswordBox` or `NumberBox`, and let a field turn checking off. Done when: `Ui.Controls.SpellCheck.OptIn` shows no checker calls for those controls.
- [ ] Expose misspellings to UI Automation as the `AnnotationType_SpellingError` annotation on the text range (consumed by `D10 T03 §3`). Done when: the text range adapter test sees the annotation on the misspelled range.
- [ ] Commit: `ui: spell checking for TextBox through the Windows Spell Checking API`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.SpellCheck.*` passes on a host with the `en-US` and `fr-FR` spelling resources, and the golden crop fails when the underline becomes straight.

## 25. `CalendarFlyout` and `DatePicker` with locale-aware weeks and formats

`src/ui/controls/calendar.h` and `calendar.cpp` replace WinUI's `CalendarView` and `CalendarDatePicker` for the places Recto edits a date (form date fields, metadata dates, signature and stamp dates). `ui::CalendarFlyout` is a `Flyout` (`§16`) holding a month grid built on `ui::ItemsView` (`§11`) uniform grid layout with a header (month and year, previous and next month buttons), the week starting on the user's first day of week (`GetLocaleInfoEx` `LOCALE_IFIRSTDAYOFWEEK`), day names from `LOCALE_SABBREVDAYNAME*`, today outlined in `RectoAccentBrush` and the selected day filled with `RectoAccentBrush` and `RectoOnAccentBrush` text. `ui::DatePicker` is a field showing the date in the user's short date format (`GetDateFormatEx` with `DATE_SHORTDATE`) with a `calendar` glyph button opening the flyout; typing a date parses it with the same locale and shows an error state when it does not parse.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 4, 6 (`calendar` glyph), 7 and 11; genuinely new surface (no D board draws a date picker), so the golden reference is a reviewed baseline matching the Fluent CalendarDatePicker look.
**Job:** The user picks or types a date. Consumers: form date fields and the Form Designer date format (D03 T03), document metadata dates (D02 T03), stamp and signature date fields.
**Treatment:** Locale-aware month grid in a flyout plus a typed field. Cheaper substitute that fails the checkpoint: a fixed Sunday-first grid, a US-only date format, or a plain text box with no picker.
**Chrome:** `RectoAccentBrush`, `RectoOnAccentBrush`, `RectoTextSecondaryBrush`, `Flyout` of `§16`, `ItemsView` of `§11`, buttons of `§2`, `TextBox` of `§4`.

**Build order:**

1. Create `src/ui/controls/calendar.h` and `calendar.cpp` with the month model (first day of week, days in month, leading and trailing days). Done when: `Ui.Controls.Calendar.Model` produces correct grids for February 2028 with Monday and Sunday first.
2. Build `CalendarFlyout` and `DatePicker` with keyboard and typed entry. Done when: arrow keys move by day and week, `Page Up` and `Page Down` by month, and a typed date in `en-ZA` and `de-DE` short formats parses.
3. Add golden and automation tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.Calendar.*` passes.

- [ ] Implement the month model in `src/ui/controls/calendar.cpp` from `LOCALE_IFIRSTDAYOFWEEK` and the Gregorian calendar, with min and max dates. Done when: `Ui.Controls.Calendar.Model` passes for leap years and both week starts.
- [ ] Draw the grid with today outlined and the selected day filled in the accent, out-of-month days in `RectoTextSecondaryBrush`, and 32 epx day cells. Done when: golden crops match in Light, Dark and HighContrast. Cheaper substitute: colour-only selection with no fill shape in HighContrast.
- [ ] Add header navigation with `chevron_left` and `chevron_right` icon buttons carrying automation names and tooltips. Done when: buttons move one month and announce the new month name.
- [ ] Implement keyboard navigation (arrows, `Home` and `End` within the week, `Page Up` and `Page Down` by month, `Enter` selects, `Esc` closes). Done when: `Ui.Controls.Calendar.Keyboard` passes.
- [ ] Implement `DatePicker` display and parse with `GetDateFormatEx` and the locale's short date pattern (`LOCALE_SSHORTDATE`), showing an error border and helper text on a bad date. Done when: `Ui.Controls.Calendar.Parse` accepts `22/10/2026` in `en-ZA` and rejects `31/02/2026`. Cheaper substitute: hard-coded `MM/dd/yyyy`.
- [ ] Expose Grid and GridItem semantics plus the selected date as the field value through the automation properties of `D10 T01 §6` (wrapped by `D10 T03 §2`). Done when: `Ui.Controls.Calendar.Automation` asserts row and column counts and the value.
- [ ] Commit: `ui: CalendarFlyout and DatePicker with locale-aware weeks and formats`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.Calendar.*` passes, and the model test fails when the first day of week is forced to Sunday for `en-GB`.

## 26. `Splitter` for resizable panes with pointer, keyboard and limits

`src/ui/controls/splitter.h` and `splitter.cpp` provide the pane splitter the Classic left pane, the Reading room inspector, split views and side-by-side compare need (WinUI had none built in; apps used a community `GridSplitter`). A `ui::Splitter` sits between two `ui::Grid` tracks (`D10 T01 §7`), shows a 1 epx `RectoBorderBrush` line widening to a 3 epx `RectoAccentBrush` grip on hover and drag, sets the `IDC_SIZEWE` or `IDC_SIZENS` cursor, captures the pointer while dragging (`D10 T01 §12`), clamps both tracks to their min and max sizes, and reports the final size through a `ui::Property<float>` so the owner persists it. Keyboard: the splitter is a tab stop; arrow keys move it by 8 epx (`Shift` for 32 epx); `Home` and `End` go to the limits; double-click and `Enter` reset to the default size.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 4, 7, 8 and 11 with `design/screenshots/D-Classic.png` (left pane edge) and `D-ReadingRoom.png` (inspector edge).
**Job:** The user resizes a docked pane or a split view. Consumers: Classic left dock and Reading room inspector (D02 T04 layout), split and side-by-side views (D02 T02), Compare PDFs (D03 T01).
**Treatment:** One splitter control with pointer, keyboard, limits and persistence. Cheaper substitute that fails the checkpoint: fixed pane widths, or a drag handle with no keyboard path.
**Chrome:** `RectoBorderBrush`, `RectoAccentBrush`, `ui::Grid` tracks of `D10 T01 §7`, pointer capture of `D10 T01 §12`, `ui::Property` of `D10 T01 §16`.

**Build order:**

1. Create `src/ui/controls/splitter.h` and `splitter.cpp` resizing the adjacent `ui::Grid` tracks with clamping. Done when: `Ui.Controls.Splitter.Clamp` cannot drag a track below its minimum or above its maximum.
2. Add keyboard, reset and the size property. Done when: `Ui.Controls.Splitter.Keyboard` moves by 8 and 32 epx and `Enter` resets.
3. Add golden and automation tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.Splitter.*` passes.

- [ ] Implement drag with pointer capture, live relayout and `IDC_SIZEWE` or `IDC_SIZENS` cursors in `src/ui/controls/splitter.cpp`. Done when: a scripted drag of 100 epx moves the boundary by 100 epx at 100, 150 and 200 percent scale.
- [ ] Clamp both adjacent tracks to their min and max and keep star tracks proportional. Done when: `Ui.Controls.Splitter.Clamp` passes.
- [ ] Draw rest, hover, dragging and focused states (1 epx line, 3 epx accent grip, Fluent focus visual) from tokens in Light, Dark and HighContrast. Done when: golden crops differ per state and match the tokens. Cheaper substitute: an invisible hit strip with no hover feedback.
- [ ] Add the keyboard path (arrows 8 epx, `Shift` 32 epx, `Home` and `End`, `Enter` and double-click reset). Done when: `Ui.Controls.Splitter.Keyboard` passes.
- [ ] Publish the final size through `ui::Property<float>` on drag end only, so owners persist one write per drag. Done when: one drag produces one property change.
- [ ] Expose RangeValue semantics (value in epx, min, max) and the name "Resize <pane>" through the automation properties of `D10 T01 §6` (wrapped by `D10 T03 §2`). Done when: `Ui.Controls.Splitter.Automation` asserts the range and name.
- [ ] Commit: `ui: Splitter for resizable panes with pointer, keyboard and limits`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.Splitter.*` passes, and the clamp test fails when the minimum is ignored.

## 27. Custom colour entry: hex field, live preview and recent colours

`src/ui/controls/colour_entry.h` and `colour_entry.cpp` add the free colour entry that the token swatches of `§22` deliberately leave out, for the places that edit a document colour rather than pick an interface token (custom annotation and stamp colours, reading tints, Action Wizard colour parameters, watermark text colour). `ui::ColourEntry` is a compact row under the swatch grid in the same `Flyout`: a 24 epx preview swatch, a `ui::TextBox` (`§4`) accepting `#RRGGBB` or `RRGGBB` (and `#RRGGBBAA` when the consumer allows opacity), validation with an error state, and up to eight recent custom colours kept per consumer through a `ui::Property<std::vector<Color>>` that the consumer persists in its own settings. The colour values are document data supplied by the user, so the no-hex-literal rule for interface code is untouched: the control holds no colour literal of its own.

**Phase:** 2
**Needs:** windows-host
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 4, 7, 8.2 and 11; genuinely new surface (no D board draws custom colour entry), so the golden reference is a reviewed baseline placed under the `§22` swatch grid.
**Job:** The user types or reuses a custom colour for document content. Consumers: annotation style and stamps (D03 T03), reading tints (D02 T02), custom colour in panels (D02 T03), Action Wizard parameters (D04 T07), watermark text (D05 T02).
**Treatment:** Hex field with live preview and recent colours next to the token swatches. Cheaper substitute that fails the checkpoint: a full colour wheel or HSV editor the SPEC does not ask for, or accepting malformed hex silently.
**Chrome:** `ColorSwatchPicker` of `§22`, `TextBox` of `§4`, `Flyout` of `§16`, `RectoBorderStrongBrush`, `RectoDangerBrush` (error state).

**Build order:**

1. Create `src/ui/controls/colour_entry.h` and `colour_entry.cpp` with the hex parser and validation. Done when: `Ui.Controls.ColourEntry.Parse` accepts `#C8431F`, `c8431f` and `#C8431F80` (opacity allowed) and rejects `#C8431`, `#GG0000` and `#C8431F80` when opacity is not allowed.
2. Add the preview swatch, recent colours and the `§22` flyout integration. Done when: entering a valid colour updates the preview and adds it to the front of the recent list without duplicates.
3. Add golden and automation tests. Done when: `recto-tests.exe --gtest_filter=Ui.Controls.ColourEntry.*` passes.

- [ ] Implement the hex parser in `src/ui/controls/colour_entry.cpp` with optional `#`, case-insensitive digits and consumer-controlled alpha. Done when: `Ui.Controls.ColourEntry.Parse` passes. Cheaper substitute: accepting any string and clamping it.
- [ ] Show an error border in `RectoDangerBrush` and the helper text "Enter a colour like #C8431F" on invalid input, keeping the last valid colour applied. Done when: an invalid entry never reaches the consumer's colour property.
- [ ] Draw the 24 epx preview swatch with a `RectoBorderStrongBrush` outline and a checkerboard under translucent colours. Done when: golden crops show the checkerboard for `#C8431F80` in Light, Dark and HighContrast.
- [ ] Keep up to eight recent custom colours, most recent first and de-duplicated, through `ui::Property<std::vector<Color>>` owned and persisted by the consumer. Done when: `Ui.Controls.ColourEntry.Recent` keeps order and drops the ninth.
- [ ] Place the row under the `§22` swatch grid in the same flyout and share one `colour_changed` event, with keyboard order swatches, then the field, then recents. Done when: `Ui.Controls.ColourEntry.Keyboard` tabs through in that order.
- [ ] Expose the field value, the preview's colour name or hex as its name and the recent colours as a list through the automation properties of `D10 T01 §6` (wrapped by `D10 T03 §2`). Done when: `Ui.Controls.ColourEntry.Automation` asserts names and values.
- [ ] Commit: `ui: custom colour entry with hex field, live preview and recent colours`

**Test checkpoint:** `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.ColourEntry.*` passes, and the parse test fails when a five-digit hex is accepted.
## Verification

- Every SPEC control (sections 4 to 12 of `design/SPEC.md`) exists as a `recto-ui` control and appears in `recto-ui-gallery`: `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=Ui.Controls.*:Ui.Gallery.*` passes, with golden images matching in Light, Dark and HighContrast at 100, 150 and 200 percent.
- No colour or corner-radius literal exists under `src/ui/controls/` and `src/ui/gallery/`: `Audit.UiControlsNoLiterals` and `Audit.NoCornerRadiusLiterals` pass.
- Every icon-only control has an automation name and a tooltip with its shortcut, and the gallery accessibility smoke lists no unnamed control.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
