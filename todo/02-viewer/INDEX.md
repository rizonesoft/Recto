# 02 Viewer

> **Phase 2 (M2 Viewer)**

The WinUI app as the SPEC's one shell: title-bar tabs, the tiled document view, Home and panels, the command box, settings and the three layout presets. Every surface section carries Fidelity to `design/SPEC.md` and its screenshot.

## TODOs

| TODO | Title | Status |
| ---- | ----- | :----: |
| [TODO-01](./TODO-01-app-shell.md) | App Shell, Tabs, Menus, File Open/Save and Shell Dialogs | active |
| [TODO-02](./TODO-02-document-view.md) | Document View: Presentation, Tiles, Layouts, Scroll, Zoom, Toolbar and Links | active |
| [TODO-03](./TODO-03-home-and-panels.md) | Home, Side Panels, Outlines, Layers, Portfolios, Document Info and Command Box | active |
| [TODO-04](./TODO-04-settings-and-layout.md) | Settings Store, Portable Mode, Layout Presets, Settings Page, Theme and Keyboard | active |
| [TODO-05](./TODO-05-rich-media-3d-geo.md) | Rich Media, 3D Models and Geospatial PDFs | active |

## Completed

| TODO | Title | Completed |
| ---- | ----- | :-------: |

(none yet)

## In scope

- `src/app` shell, custom title bar, tabs, named slots, Classic menu bar (`File`, `Edit`, `View`, `Tools`, `Window`, `Help`), four open paths (`Ctrl+O` picker, drag-and-drop, CLI args, Explorer activation), `Save` / `Save a copy` / `Replace original` / `Unsaved changes`, and shell dialogs (Password, Could-not-open)
- Presentation thread, worker D3D11 texture uploads, budgeted LRU tile cache, Continuous/Single-Page/Two-Page Spread geometry, scroll, zoom with stretched preview, Floating Page Toolbar with 3s auto-fade, safe links, dark page colour modes, `F11` Full Screen
- `RecentFilesStore`, `HomePage`, `SidePanelHost`, `ThumbnailList` (with filmstrip orientation), `OutlinePanel`, interactive Bookmark Editor, `OCG` Layers panel, Attachments and PDF Portfolios (`/EmbeddedFiles` and `/Collection`), read-only and editable Info/XMP/ViewerPreferences, unified `CommandRegistry`, `Ctrl+K` Command Box, `AppPaths` Portable mode, `SettingsStore`, `LayoutPresetEngine`, `SettingsPage`, live `ThemeManager`, `MotionHelper`, and the audited SPEC section 10 keyboard map

## Out of scope

- Text selection, find and the UIA provider (03)
- Conversions (04), Organize authoring (05), printing (07)

---

Format spec: [../README.md](../README.md) · Root index: [../TODO-00-INDEX.md](../TODO-00-INDEX.md)
