# 10 UI Toolkit

> **Phase 2 (M2 Viewer)**

`recto-ui` (`src/ui`), Recto's own C++23 UI toolkit on a Win32 window with DirectComposition, Direct2D and DirectWrite, which replaces WinUI 3 (owner decision 2026-10-22, foundation plan D4; design: `docs/specs/2026-10-22-recto-ui-toolkit-design.md`). It reproduces the Fluent look `design/SPEC.md` was written against, so every M2 to M6 surface builds on these sections instead of stock WinUI controls. Domain number 09 belonged to the withdrawn Recto Cloud domain (D20) and is not reused.

## TODOs

| TODO | Title | Status |
| ---- | ----- | :----: |
| [TODO-01](./TODO-01-ui-foundation.md) | recto-ui Foundation: Window, Composition, Elements, Layout, Tokens, Text and Input | active |
| [TODO-02](./TODO-02-ui-controls.md) | recto-ui Controls: the Fluent Control Set the SPEC Uses | active |
| [TODO-03](./TODO-03-ui-accessibility.md) | recto-ui Accessibility: UI Automation, High Contrast, Text Scale and Strings | active |

## Completed

| TODO | Title | Completed |
| ---- | ----- | :-------: |

(none yet)

## In scope

- The `recto-ui` static library and its headless render harness, the Win32 `Recto.exe` host that replaces the WinUI scaffold, and retiring the Windows App SDK pins
- Window host (custom frame, Mica Alt, dark mode, per-monitor DPI, Snap Layouts, full screen), DirectComposition compositor, element tree, layout, generated design tokens, drawing primitives and elevation, DirectWrite text, icons and images, input and cursors, focus, motion, popups, binding helpers, drag-and-drop and clipboard, and the page canvas `SwapChainHost`
- The control set: buttons, tooltips, text input with TSF, password and number boxes, check, radio and toggle, sliders and progress, combo boxes, scrolling with DirectManipulation, virtualized lists and grids, tree, title-bar tabs, menus and the menu bar, flyouts and cards, dialogs, segmented controls and the mode switcher, navigation rail and settings cards, info bars, toasts and pills, search boxes, colour swatches and custom colour entry, spell checking, calendar and date picker, pane splitter, and the control gallery
- UI Automation providers and patterns, the reusable text-range adapter, announcements, high contrast, text scaling, string tables and the accessibility verification suite

## Out of scope

- Screens and view models (02 to 08 build them on this toolkit), the document text provider (D03 T02), notifications, printing and the Share contract (owned by the sections that use them)

---

Format spec: [../README.md](../README.md) · Root index: [../TODO-00-INDEX.md](../TODO-00-INDEX.md)
