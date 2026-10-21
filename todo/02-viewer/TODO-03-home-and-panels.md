---
schema_version: 1
id: home-and-panels
domain: 02-viewer
status: active
title: "TODO-03 -- Home, Side Panels, Outlines, Layers, Portfolios, Document Info and Command Box"
track: M2
---

# TODO-03 -- Home, Side Panels, Outlines, Layers, Portfolios, Document Info and Command Box

> **Goal:** The surfaces around the page: the persistent Recent files and remembered reading-position store, the Home screen (`D-Home.png`) with rendered cover thumbnails and inline command box, the shared `SidePanelHost` card and switcher (`Ctrl+B`), the virtualized Pages thumbnail panel (with filmstrip orientation and context menu), the Outline `TreeView` panel, the interactive Bookmark Editor and heading auto-generator, the Optional Content Groups (`OCG` / Layers) engine and live panel, the Embedded Files and PDF Portfolios (`/EmbeddedFiles` and `/Collection`) engine and panel, the read-only Document Info panel (metadata, security flags, font inventory), the Editable Info/XMP/ViewerPreferences editor, the unified `CommandRegistry`, and the `Ctrl+K` Command Box overlay.

> **Current state:** Measured 2026-10-14. No code exists yet. `D02 T01` provides `ShellPage`, `DocumentTabSession` (`§3`), named slots (`§4`), `OpenDocumentPipeline` (`§5`), `SavePipeline` (`§8`), and `ShellMenuBar` (`§9`); `D02 T02` provides `DocumentView`, the thumbnail-capable `TileTextureCache` (`§3`), `PageLayoutGeometry` (`§5`), and `PageToolbar` (`§8`). Visual contracts are fixed in `design/SPEC.md` sections 8.3, 9.1, 9.2, 9.4, 10 and 11 with boards `D-Home.png`, `D-Focus-Light.png`, `D-Classic.png`, `D-ReadingRoom.png` and `D-Dialogs.png`.

## Inputs

- `design/SPEC.md` sections 3.4 (in-app SVG graphics), 5 (typography), 6 (icons), 7 (radius 16 panels, radius 20 command box, narrow-window `< 1100 epx` auto-close), 8.3 (Pages, Outline with Bookmark Editor, Comments, Layers, Attachments & Portfolio, Signatures & Standards), 9.1 (Home), 9.2 (Info, XMP & Viewer Preferences list and editor), 9.4 (Command box), 10 (`Ctrl+K`, `Ctrl+B`, `Ctrl+G`), 11; `resources/svg/drop-files-*.svg`, `resources/svg/ribbon-marker*.svg`, `resources/svg/filetype-pdf*.svg`.
- -> XREF: D01 T01 §10 -- `DocumentDescription` (outlines, metadata, permissions, embedded fonts) consumed by `OutlinePanel` (`§5`) and `DocumentInfoPanel` (`§9`) here.
- -> XREF: D01 T02 §5 -- `DocumentActor` incremental save (`§5`), `DisplayList` OCG layer evaluation, and `TileScheduler` (`§8`) consumed by `ThumbnailList` (`§4`), `BookmarkEditor` (`§6`), `LayersPanel` (`§7`), and `DocumentInfoEditor` (`§10`) here.
- -> XREF: D02 T04 §3 -- the layout preset engine relocates `SidePanelHost` (§3) and `ThumbnailList` (§4) between slots and switches `ThumbnailList` between vertical and horizontal `ItemsRepeater` orientation.
- -> XREF: D03 T01 §6 -- the `Ctrl+F` find card and `Reading room` Search inspector tab plug into the panel hosting contract established in §3 here and mark hits on §4's filmstrip.
- -> XREF: D03 T03 §5 -- the Comments (Annotations) side panel plugs into `SidePanelHost` (§3) and shares the per-document undo/redo stack established in §6 here.
- -> XREF: D04 T05 §8 -- the Signatures & Standards (`PDF/A`, `PDF/UA`, `ZUGFeRD / Factur-X`) side panel plugs into `SidePanelHost` (§3).
- -> XREF: D05 T01 §3 -- page-tree operations (`D05 T01 §3`, `§4`, `§7`) back the thumbnail context menu (`§4`) and security permission flags (`§9`) here.
- -> XREF: D05 T02 §1 -- the Organize page grid reuses the thumbnail tile pipeline and page operation commands wired in §4 here.
- -> XREF: D05 T02 §9 -- the Viewer Signatures side panel tab (`D05 T02 §9`) plugs into `SidePanelHost` (§3).
- -> XREF: D07 T03 §2 -- `help.reportProblem` and `What's new` (§7 there) are registered in §11's `CommandRegistry` and found through §12's command box.
- -> XREF: D03 T04 §14 -- Edit PDF registers its `edit.*` commands here.
- -> XREF: D08 T01 §17 -- hosts the Assistant side panel (D08 T01 §17).
- -> XREF: D08 T02 §18 -- command registry.
- -> XREF: D08 T02 §19 -- ask AI row in command box.
- -> XREF: D08 T02 §20 -- bookmark undo stack.
- -> XREF: D08 T02 §21 -- bookmark proposals.
- -> XREF: D07 T01 §14 -- print commands.
- -> XREF: D04 T04 §18 -- attachments ask before opening.
- -> XREF: D05 T01 §19 -- page labels in thumbnails.
- -> XREF: D05 T02 §22 -- thumbnail drag source.
- -> XREF: D03 T01 §2 -- PageText counts.
- -> XREF: D03 T01 §5 -- find engine for bookmark proposals.
- -> XREF: D03 T04 §4 -- font resolver for contents page.
- -> XREF: D05 T01 §4 -- page insertion.
- -> XREF: D05 T01 §1 -- GraftMap.
- -> XREF: D01 T02 §11 -- line_weight_mode sits beside ocg_generation in TileKey.
- -> XREF: D01 T02 §14 -- layer edits are OcgEdit builders on the actor session.
- -> XREF: D01 T02 §14 -- import as layer uses the same builder contract.
- -> XREF: D01 T02 §14 -- Layers panel drives stage, undo, redo and discard.
- -> XREF: D01 T03 §10 -- bookmarks verb follows the registration rule.

## Outcome

- `RecentFilesStore` persists recent documents, cover thumbnails, and per-file reading positions (`page_index`, `zoom`, `scroll_y`, `layout_mode`) atomically in `AppPaths::data_dir()`, pruning deleted files and recovering cleanly from corrupt JSON.
- `HomePage` renders the `TitleLarge` prompt, inline `720 epx` Command Box, six recent document cards with rendered first-page covers and relative dates, `"See all"` expansion, per-item removal, and the honest empty state (`"Drop a PDF anywhere in this window"`).
- `SidePanelHost` (`Ctrl+B`) hosts `Pages` (virtualized thumbnails with ribbon marker, filmstrip orientation switch, and page context menu), `Outline` (`TreeView` with two-way section tracking and full Bookmark CRUD + heading auto-generation), `Layers` (`OCG` live visibility toggles and layer flattening), and `Attachments & Portfolio` (`/EmbeddedFiles` and `/Collection` inspect/extract/add/remove).
- `DocumentInfoPanel` displays formatted metadata, security/permission flags, and embedded fonts in both the `Reading room` inspector and `Properties` dialog, and switches into `"Edit Info & Preferences"` mode to edit `/Info`, XMP metadata, and `/ViewerPreferences`.
- `CommandRegistry` unifies every toolbar, menu, and shortcut command under one table, and `CommandBoxControl` (`Ctrl+K` overlay and Home inline box) filters across `"This document"`, `"Go to"`, `"Files"`, and `"Actions"` with bolded match spans.
- Word and character counts, bookmark creation from text styles, search strings and structure tags, bookmark import and export with `recto-cli bookmarks`, per-bookmark style, a generated contents page, and layer authoring (create, rename, delete, merge, import a page or image as a layer) are planned in §13 to §25.

**Adjacency:** list=applicable @ D02 T03 §1; document=applicable @ D02 T03 §6; settings=applicable @ D02 T04 §5; reporting=not-applicable (no reports); notifications=not-applicable (none originate here); permissions=not-applicable (no user roles; PDF encryption permission flags are displayed in §9 and external attachment open prompts in §8); audit=not-applicable (recents are user state with removal controls in §1 and §2, not an audit trail); exchange=applicable @ D02 T03 §8; reverse=applicable @ D02 T03 §1

**Adjacency rationale:** The list surface is `RecentFilesStore` and `HomePage` (`§1`, `§2`), and its reverse is removing a recent item or clearing recent history (`§1`, `§2`) plus `Undo`/`Redo` on bookmark edits (`§6`). Document surfaces are saving updated bookmarks (`§6`), flattened layers (`§7`), portfolios (`§8`), and edited metadata/viewer-preferences (`§10`) through `SavePipeline` (`D02 T01 §8`). Exchange is extracting and embedding files/portfolios in `§8`. The `"Remember reading position"` toggle is owned in `D02 T04 §5` and consumed in `§1`.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Recent files and remembered reading-position store (`RecentFilesStore`) | D02 T01 §5 | [ ] |
| 2 | §2 | Home screen (`D-Home.png`), recent covers, inline command box host and empty state | §1, D02 T01 §6, D02 T02 §3 | [ ] |
| 3 | §3 | Side-panel card container (`SidePanelHost`), switcher header and narrow auto-close | D02 T01 §4, D02 T02 §8 | [ ] |
| 4 | §4 | Pages thumbnail panel (`ThumbnailList`), filmstrip orientation and page context menu | §3, D02 T02 §4, D02 T02 §6 | [ ] |
| 5 | §5 | Outline tree panel (`OutlinePanel`) and bidirectional section tracking | §3, D02 T02 §6, D01 T01 §10 | [ ] |
| 6 | §6 | Interactive Bookmark Editor and heading-based outline auto-generation | §5, D02 T01 §8, D01 T02 §5 | [ ] |
| 7 | §7 | Optional Content Groups (`OCG` / Layers) engine and live Layers panel | §3, D02 T02 §4, D01 T02 §5 | [ ] |
| 8 | §8 | Embedded Files and PDF Portfolios (`/EmbeddedFiles` and `/Collection`) engine and panel | §3, D02 T01 §8, D02 T01 §10 | [ ] |
| 9 | §9 | Document Info panel: metadata, security permissions and embedded font inventory | §3, D01 T01 §10 | [ ] |
| 10 | §10 | Editable Document Info, XMP metadata synchronization and `/ViewerPreferences` editor | §9, D02 T01 §8, D01 T02 §5 | [ ] |
| 11 | §11 | Unified `CommandRegistry` across toolbars, menus, panels and keyboard accelerators | D02 T01 §9, D02 T02 §8 | [ ] |
| 12 | §12 | Command Box (`Ctrl+K`) overlay and Home inline search experience | §2, §5, §11 | [ ] |
| 13 | §13 | Word, character, line and page counts for the document and the selection | §9, D03 T01 §2, D03 T01 §3, D02 T03 §11 | [ ] |
| 14 | §14 | Bookmark proposals from text styles (font, size, weight) | §6, D03 T01 §1 | [ ] |
| 15 | §15 | Bookmark proposals from search strings and patterns | §6, D03 T01 §5 | [ ] |
| 16 | §16 | Bookmark proposals from structure tags (`H1` to `H6`, `Title`, `Sect`) | §6, D03 T01 §2 | [ ] |
| 17 | §17 | `Create bookmarks` dialog: choose a source, preview, apply with undo | §5, §6, §14, §15, §16, D02 T01 §10, D02 T01 §9 | [ ] |
| 18 | §18 | Bookmark import and export: JSON and plain-text outline, plus `recto-cli bookmarks` | §6, D01 T03 §1 | [ ] |
| 19 | §19 | Import and export bookmarks from the Outline panel | §5, §18, D02 T01 §10, D02 T01 §9 | [ ] |
| 20 | §20 | Per-bookmark style: colour, bold and italic (`/C` and `/F`) | §5, §6, §18 | [ ] |
| 21 | §21 | `recto-pdf` contents page generated from bookmarks | §6, D03 T04 §4, D05 T01 §3, D05 T01 §4 | [ ] |
| 22 | §22 | Insert contents page dialog and `recto-cli toc` | §21, D02 T01 §8, D02 T01 §10, D02 T01 §9 | [ ] |
| 23 | §23 | `recto-pdf` layer authoring: create, rename, delete, properties and merge | §7, D01 T02 §14 | [ ] |
| 24 | §24 | `recto-pdf` import a page or an image as a layer | §7, §23, D05 T01 §1 | [ ] |
| 25 | §25 | Layers panel editing: new, rename, delete, merge and import as layer | §7, §23, §24, D02 T01 §10 | [ ] |

## 1. Recent files and remembered reading-position store (`RecentFilesStore`)

Implement `RecentFilesStore` (`src/services/recents_store.h/.cpp`): persists the ordered list of recently opened PDFs and their remembered reading state (`last_page_index`, `zoom_factor`, `zoom_mode`, `scroll_offset_y`, `layout_mode`, `last_opened_utc`, `page_count`, and cached cover PNG path) to `recents.json` inside the active data directory (`%LOCALAPPDATA%\Recto\` or `<exe-dir>\data\` in Portable mode, `D02 T04 §1`), using atomic temp-file-then-replace writes, pruning deleted/missing files automatically at read time, degrading cleanly to an empty list if `recents.json` is corrupted, and restoring the user's reading position on reopen when `"Remember reading position"` (`SPEC` section 9.7) is enabled.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `RecentFilesStore` (`src/services/recents_store.h/.cpp`) with `record_open(path, metadata)`, `update_reading_position(path, page, zoom, scroll_y, layout)` (debounced so rapid scrolling coalesces disk writes to on-tab-close / idle / app-exit), `remove_entry(path)`, `clear_all()`, and `list_entries(prune_missing = true)`. Done when: unit tests verify most-recent-first ordering, cap enforcement (max 50 persisted entries), missing-file pruning on disk, and atomic `ReplaceFileW` persistence.
2. Integrate `RecentFilesStore` with `OpenDocumentPipeline` (`D02 T01 §5`) and `DocumentTabSession` (`D02 T01 §3`): when a PDF opens and `"Remember reading position"` is true (default), restore its saved `page_index`, `zoom`, and `layout_mode` (unless overridden by a CLI `--page=N` flag from `D02 T01 §7`); when the tab closes or changes page, update the store entry. Done when: opening a corpus PDF, scrolling to page 9 at `150%` zoom, closing the tab, and reopening the file lands on page 9 at `150%` zoom, and planting a corrupt `recents.json` logs a diagnostic without crashing.

- [ ] Author `src/services/recents_store.h/.cpp` defining `RecentFileEntry` (`canonical_path`, `display_name`, `last_opened_utc`, `page_count`, `last_page_index`, `zoom_factor`, `zoom_mode`, `scroll_offset_y`, `layout_mode`, `cover_cache_path`) and `RecentFilesStore`. Done when: `RecentFilesStore` compiles in `recto-services` with a configurable storage root path for isolated unit testing.
- [ ] Write `recents.json` exclusively through atomic temp-file-then-replace (`ReplaceFileW` / `MoveFileExW`) in the target directory, coalescing reading-position updates on a 2-second debounce and on tab/window close rather than rewriting JSON on every mouse-wheel tick. Done when: scrolling 100 pages performs at most 1 disk write on settle/close. Cheaper substitute that fails the checkpoint: rewriting `recents.json` in place on every `ScrollChanged` event.
- [ ] Prune entries whose target `.pdf` file no longer exists on disk when `list_entries()` is queried for `Home` or `Ctrl+K`. Done when: deleting a file on disk removes it from `list_entries()` automatically without showing a broken recent card.
- [ ] Handle corrupted or truncated `recents.json` files gracefully by logging a `recto::pdf::diagnostics_tail()` warning and initializing an empty store without throwing or crashing. Done when: a planted `recents.json` containing `"{corrupt"` degrades to `0` entries cleanly.
- [ ] Implement `remove_entry(path)` and `clear_all()` (also deleting associated cached cover thumbnail PNGs from `cache/covers/`). Done when: removing a recent entry deletes both its JSON record and its cached cover file.
- [ ] Restore `last_page_index`, `zoom`, and `layout_mode` in `OpenDocumentPipeline` when `"Remember reading position"` is enabled (`true` by default per SPEC 9.7), while allowing explicit `--page=N` CLI arguments (`D02 T01 §7`) to take precedence. Done when: reopening a file restores page 9, whereas `Recto.exe file.pdf --page=2` opens at page 2.
- [ ] Populate the `File -> Open Recent` submenu in `ShellMenuBar` (`D02 T01 §9`) from `RecentFilesStore::list_entries()`. Done when: opening a file adds it to the top of `File -> Open Recent`.
- [ ] Add `Panels.RecentFilesStoreAndPosition` test in `tests/recto-tests/` covering atomic persistence, MRU ordering, missing-file pruning, corrupt-file recovery, reading-position restoration, and `remove_entry` / `clear_all`. Done when: `recto-tests.exe --gtest_filter=Panels.RecentFilesStoreAndPosition` passes.
- [ ] Commit: `viewer: add RecentFilesStore with atomic persistence, pruning and reading-position restore`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.RecentFilesStoreAndPosition` passes, proving reading-position restoration across restarts, missing-file pruning, and corrupt-store recovery.

## 2. Home screen (`D-Home.png`), recent covers, inline command box host and empty state

Build `HomePage` (`src/app/HomePage.xaml/.h/.cpp`) matching `design/SPEC.md` section 9.1 and `design/screenshots/D-Home.png`: centred layout with the `TitleLargeTextBlockStyle` (`40 epx Semibold`) question `"What do you want to open?"`, subtitle help line, the `720 epx` inline expanded Command Box host with footer key hints and `"Browse files"` accent button (`Ctrl+O`), the `"Recent"` grid displaying up to 6 recent document cards with rendered first-page cover thumbnails, document title, relative last-opened day (`"Today"`, `"Yesterday"`, `"Oct 12"`), per-card context menu (`Open`, `Open file location`, `Remove from Recent`), `"See all"` toggle expanding the full recent list, and the empty state hiding `"Recent"` and displaying `"Drop a PDF anywhere in this window"` when no recents exist.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 3.4, 5 (`TitleLargeTextBlockStyle` 40 / Semibold), 7, and 9.1 with `design/screenshots/D-Home.png`.
**Job:** When the user launches Recto or opens a new tab, they can pick up right where they left off from visual cover cards or open/run anything from the inline Command Box and `Browse files` button. Consumer: `ShellPage` hosts `HomePage` on the `Home` tab (`D02 T01 §2`); `CommandBoxControl` (§12) mounts in the inline slot.
**Treatment:** First-page cover thumbnails rendered asynchronously via `RenderPool` at a thumbnail bucket (`160 x 210 epx` scaled by DPI) and cached in `cache/covers/<hash>.png`, with `resources/svg/filetype-pdf.svg` shown as an instant fallback while the cover renders. Cheaper substitute that fails the checkpoint: a plain text list of file paths without cover images, or using `IShellItemImageFactory` Explorer thumbnails that vary across machines.
**Chrome:** consume `RectoWindowBrush`, `RectoRaisedBrush`, `RectoBorderBrush`, `RectoAccentBrush`, `resources/svg/filetype-pdf.svg`, and `resources/svg/drop-files-{light,dark}.svg`.

**Build order:**

1. Implement asynchronous cover thumbnail generation in `src/services/recents_store.cpp`: when a PDF is opened or listed on `Home` without a cached cover PNG, rasterize page 1 at the thumbnail bucket off the UI thread, write `cache/covers/<path_hash>.png` atomically, and notify `HomePage` to swap the card image from `resources/svg/filetype-pdf.svg` to the rendered page cover. Done when: opening a corpus PDF populates its rendered page-1 cover PNG on disk and displays it on `HomePage`.
2. Build `src/app/HomePage.xaml/.h/.cpp` matching `D-Home.png`: `"What do you want to open?"` (`TitleLargeTextBlockStyle`), subtitle line, `720 epx` inline Command Box slot with footer `"Browse files"` accent button wired to `OpenDocumentPipeline::open_picker_async` (`D02 T01 §5`), the 6-card `"Recent"` grid with `"See all"` expansion, right-click / `...` menu (`Open`, `Open file location` via `SHOpenFolderAndSelectItems`, `Remove from Recent`), and the empty state when `RecentFilesStore` has 0 items. Done when: `HomePage` matches `D-Home.png` in both populated (6 covers) and empty states, and clicking a recent card reopens the PDF at its saved reading position.

- [ ] Author `src/app/HomePage.xaml/.h/.cpp` with the centred column layout, `"What do you want to open?"` (`TitleLargeTextBlockStyle` 40 epx Semibold), help line, and `720 epx` inline Command Box container per SPEC 5 and 9.1. Done when: typography and spacing match `D-Home.png`.
- [ ] Wire the `"Browse files"` accent button in the inline box footer to `OpenDocumentPipeline::open_picker_async` (`D02 T01 §5`). Done when: clicking `"Browse files"` opens the system `FileOpenPicker`.
- [ ] Render first-page cover thumbnails through `recto-core`'s tile/thumbnail rasterizer and cache them in `cache/covers/` (displaying `resources/svg/filetype-pdf.svg` only while a cover is still rendering). Done when: all 6 recent cards display crisp rendered first-page covers matching the PDF content. Cheaper substitute that fails the checkpoint: showing generic file icons permanently instead of rendered first-page covers.
- [ ] Display up to 6 recent cards by default with cover preview, filename/title (`BodyStrongTextBlockStyle`), and relative last-opened day (`CaptionTextBlockStyle`), plus a `"See all"` button that expands the grid to show all persisted recent files. Done when: with 8 recent files in `RecentFilesStore`, 6 show initially and clicking `"See all"` reveals all 8.
- [ ] Add per-card context menu and keyboard actions: `Enter` / click opens the file via `OpenDocumentPipeline`, `Delete` or context-menu `"Remove from Recent"` calls `RecentFilesStore::remove_entry` (§1) and updates the grid immediately, and `"Show in Explorer"` selects the file in Windows Explorer. Done when: removing a card shrinks the grid live without reopening the page.
- [ ] Implement the SPEC 9.1 empty state: when `RecentFilesStore::list_entries()` is empty, hide the `"Recent"` section header and grid completely and display `resources/svg/drop-files-{light,dark}.svg` with `"Drop a PDF anywhere in this window"` below the inline box. Done when: clearing recents transitions `HomePage` to the exact SPEC 9.1 empty state.
- [ ] Ensure keyboard navigation (`Tab`, arrow keys across the recent cover grid, `Enter` to open, `Delete` to remove) works end to end without a mouse. Done when: pressing `Down` from the inline box moves focus into the first recent card.
- [ ] Add `Panels.HomePageAndCovers` test in `tests/recto-tests/` verifying cover generation, 6-item cap + `"See all"` expansion, item removal, and empty-state visibility. Done when: `recto-tests.exe --gtest_filter=Panels.HomePageAndCovers` passes.
- [ ] Commit: `viewer: build HomePage with rendered recent covers, See All and empty state`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.HomePageAndCovers` passes on the display-session host, and rendered screenshots of `HomePage` (populated and empty states, Light and Dark) match `D-Home.png`.

## 3. Side-panel card container (`SidePanelHost`), switcher header and narrow auto-close

Build the shared `SidePanelHost` container (`src/app/controls/SidePanelHost.xaml/.h/.cpp`) per SPEC section 8.3: hosts the header row (`Panel name`, `Panel switcher menu` switching between `Pages`, `Outline`, `Comments`, `Layers`, `Attachments & Portfolio`, `Signatures & Standards`, and `Info`, panel-specific action button slot, and `Close` button `Ctrl+B`), renders as a floating card (`16 epx` from canvas edges, `CornerRadius=16`, `RectoRaisedBrush` at `90%` opacity with Acrylic where available, `ThemeShadow` Z=32) when in `LeftOverlaySlot` / `RightOverlaySlot` (`Focus`), or as a docked pane (`RectoSurfaceBrush`, `1 epx` `RectoBorderBrush`, zero shadow) when reparented to `LeftDockSlot` (`Classic`) or `RightDockSlot` (`Reading room`), and automatically closes floating panels on page click when the window width is below `1100 epx` (SPEC section 7).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 7 (`CornerRadius=16`, `ThemeShadow` Z=32 on floating panels, docked borders, `< 1100 epx` narrow-window auto-close), 8.3 (Floating side panel and docked panels), 10 (`Ctrl+B`, `Esc`), and 12 (`167 ms` fade + `8 epx` translate transition) with `design/screenshots/D-Focus-Light.png`, `D-Classic.png`, and `D-ReadingRoom.png`.
**Job:** The user can toggle the side panel with `Ctrl+B` or the toolbar button, switch between any of Recto's document panels from one consistent header, and have floating panels get out of the way automatically when clicking the page in a narrow window. Consumer: `ThumbnailList` (§4), `OutlinePanel` (§5), `LayersPanel` (§7), `AttachmentsPanel` (§8), `DocumentInfoPanel` (§9), `CommentsPanel` (`D03 T03 §2`), `StandardsPanel` (`D04 T05 §4`), and `SignaturesPanel` (`D05 T02 §6`).
**Treatment:** Single `SidePanelHost` control that adapts its border/shadow/background styling based on whether `ShellSlotManager` (`D02 T01 §4`) placed it in an overlay slot or a dock slot, preserving the active child panel instance across slot moves. Cheaper substitute that fails the checkpoint: separate panel chrome per preset or failing to auto-close floating panels when clicking the page at `< 1100 epx` window width.
**Chrome:** consume `LeftOverlaySlot`, `RightOverlaySlot`, `LeftDockSlot`, and `RightDockSlot` (`D02 T01 §4`), `RectoRaisedBrush`, `RectoSurfaceBrush`, and `RectoBorderBrush`.

**Build order:**

1. Author `src/app/controls/SidePanelHost.xaml/.h/.cpp` with the SPEC 8.3 header bar: active panel title (`BodyStrongTextBlockStyle`), switcher `DropDownButton` / `MenuFlyout` (`Pages`, `Outline`, `Comments`, `Layers`, `Attachments & Portfolio`, `Signatures & Standards`, `Info` with `16 epx` Segoe Fluent icons), a dynamic `HeaderActionContent` presenter for panel-specific action buttons (e.g. `Add Bookmark` in Outline, `Reset Layers` in Layers), and the `Close` button (`Ctrl+B`, `AutomationProperties.Name="Close side panel"`, tooltip `"Close side panel (Ctrl+B)"`). Done when: switching panels in the header swaps the body view and header action buttons cleanly while keeping `SidePanelHost` open.
2. Implement `Ctrl+B` toggle, `PageToolbar` side-panel button sync (`RectoAccentTintBrush` when open), floating vs. docked visual state switching (`FloatingCardState` with `CornerRadius=16`, `90%` opacity `RectoRaisedBrush` + `ThemeShadow` Z=32 vs. `DockedPaneState` with `CornerRadius=0`, `RectoSurfaceBrush` + `1 epx` border), and the SPEC 7 narrow-window rule (when `ShellPage.ActualWidth < 1100` epx and `SidePanelHost` is in an overlay slot, clicking `DocumentView` closes the panel). Done when: `Ctrl+B` toggles the panel, reparenting between `LeftOverlaySlot` and `LeftDockSlot` switches visual states automatically, and clicking the page at `1000 epx` window width closes the floating panel while clicking the page at `1200 epx` leaves it open.

- [ ] Author `src/app/controls/SidePanelHost.xaml/.h/.cpp` with the header row (panel title, panel switcher menu, `HeaderActionContent` slot, and `Close` button) and body `ContentPresenter`. Done when: `SidePanelHost` mounts in `ShellPage` and switches between registered panel views.
- [ ] Apply `FloatingCardState` (`16 epx` margin from canvas edge, `CornerRadius=16`, `RectoRaisedBrush` at `90%` opacity with Acrylic fallback, `ThemeShadow` Z=32) when hosted in `LeftOverlaySlot` or `RightOverlaySlot`, and `DockedPaneState` (`RectoSurfaceBrush`, `1 epx` `RectoBorderBrush` divider, no shadow) when hosted in `LeftDockSlot` or `RightDockSlot`. Done when: floating and docked appearances match `D-Focus-Light.png`, `D-Classic.png`, and `D-ReadingRoom.png`.
- [ ] Wire `Ctrl+B` and the `PageToolbar` side-panel toggle button (`D02 T02 §8`) to show/hide `SidePanelHost` per `DocumentTabSession`, highlighting the toolbar button with `RectoAccentTintBrush` whenever the panel is visible. Done when: pressing `Ctrl+B` toggles the panel and updates the toolbar button state in lockstep.
- [ ] Implement the SPEC section 7 narrow-window auto-close rule: when `ShellPage` width is `< 1100 epx` and `SidePanelHost` is floating in an overlay slot, pointer-pressing on `DocumentView` closes `SidePanelHost` (whereas docked panes in `Classic`/`Reading room` or floating panels at `>= 1100 epx` stay open). Done when: clicking the page at `1024 epx` width closes the floating panel and clicking at `1280 epx` keeps it open. Cheaper substitute that fails the checkpoint: ignoring the `1100 epx` breakpoint so floating panels permanently cover the page on small laptops.
- [ ] Animate panel open/close with the SPEC section 12 transition (`167 ms` fade + `8 epx` horizontal translation with Fluent cubic-bezier easing, or `0 ms` instant show/hide when OS animations are disabled). Done when: panel transitions respect `UISettings::AnimationsEnabled`.
- [ ] Populate the panel switcher menu with all SPEC 8.3 and 9.2 panels (`Pages`, `Outline`, `Comments`, `Layers`, `Attachments & Portfolio`, `Signatures & Standards`, `Info`) with `16 epx` Segoe Fluent icons and badge counts where applicable. Done when: every panel entry is selectable by mouse and keyboard.
- [ ] Add `Panels.SidePanelHostAndNarrowWindow` test in `tests/recto-tests/` verifying `Ctrl+B` toggling, switcher menu navigation, floating vs. docked state adaptation on slot reparenting, and `< 1100 epx` page-click auto-close. Done when: `recto-tests.exe --gtest_filter=Panels.SidePanelHostAndNarrowWindow` passes.
- [ ] Commit: `viewer: build SidePanelHost container, panel switcher and <1100 epx auto-close`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.SidePanelHostAndNarrowWindow` passes on the display-session host, and floating vs. docked panel chrome matches `D-Focus-Light.png` and `D-Classic.png`.

## 4. Pages thumbnail panel (`ThumbnailList`), filmstrip orientation and page context menu

Build the `Pages` thumbnail panel (`src/app/panels/ThumbnailList.xaml/.h/.cpp`) per SPEC section 8.3: virtualized `ItemsRepeater` rendering page thumbnails through the shared `TileScheduler` at a dedicated thumbnail zoom bucket, marking the current page with a `2 epx` `RectoAccentBrush` ring, `RectoAccentTintBrush` row/card highlight, and the vermilion bookmark ribbon (`resources/svg/ribbon-marker*.svg`) on its top-right edge, switching orientation seamlessly between a vertical stack (left panel/pane in `Focus` and `Classic`) and a horizontal stack (`BottomDockSlot` filmstrip in `Reading room`), and exposing the SPEC 8.3 right-click page context menu (`Insert Blank Page`, `Rotate Right`, `Rotate Left`, `Crop Page`, `Extract Page`, `Delete Page`).

> **Deferred:** Context-menu `Insert Blank Page`, `Crop Page`, `Extract Page`, and `Delete Page` execution -> XREF: D05 T02 §6

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 3.4 (`ribbon-marker*.svg`), 8 (thumbnail placement: left panel, left pane, bottom filmstrip), and 8.3 (`Pages` bullet: thumbnails with page numbers, 2 epx accent ring, vermilion ribbon marker, right-click context menu) with `design/screenshots/D-Focus-Light.png`, `D-Classic.png` and `D-ReadingRoom.png`.
**Job:** The user can visually scan pages in a 500-page PDF without lag, see at a glance which page they are on via the vermilion ribbon marker, jump to any page with one click, and right-click any thumbnail to rotate or organize that page. Consumer: `SidePanelHost` (§3), `BottomDockSlot` (`D02 T01 §4`), and `LayoutPresetEngine` (`D02 T04 §3`).
**Treatment:** Virtualized `ItemsRepeater` (`StackLayout` switching `Orientation` between `Vertical` and `Horizontal`) driving `TileScheduler` requests only for realized items in the visible viewport plus a small buffer, with `CancelToken` abort when scrolling past thumbnails. Cheaper substitute that fails the checkpoint: rendering all 500 thumbnails eagerly on document open, or using a separate non-virtualized `ListView` that freezes on large PDFs.
**Chrome:** consume `resources/svg/ribbon-marker.svg` (`-on-dark.svg`), `RectoAccentBrush`, `RectoAccentTintBrush`, and the shared `TileScheduler` (`D01 T02 §8`).

**Build order:**

1. Author `src/app/panels/ThumbnailList.xaml/.h/.cpp` using a `ScrollViewer` + `ItemsRepeater` (`StackLayout`) bound to the open document's page list: as items are realized (`ElementPrepared`), request a thumbnail-bucket render (`120 epx` target height/width scaled by DPI, carrying page rotation in `TileKey`) from `TileScheduler`; when items are recycled (`ElementClearing`), cancel any in-flight `CancelToken` and release the bitmap reference. Done when: scrolling the thumbnail panel through a 500-page document sustains 60 fps and holds thumbnail memory under `32 MB`.
2. Style each thumbnail item with its 1-based page number caption, `2 epx` `RectoAccentBrush` border + `RectoAccentTintBrush` background + top-right `resources/svg/ribbon-marker.svg` overlay on the active page, two-way synchronization with `DocumentView::CurrentPageChanged` (`D02 T02 §6`), `set_orientation(Orientation::Vertical | Orientation::Horizontal)` for the `Reading room` bottom filmstrip (`D-ReadingRoom.png`), and the right-click `MenuFlyout` (`Insert Blank Page`, `Rotate Right`, `Rotate Left`, `Crop Page`, `Extract Page`, `Delete Page`, where `Rotate Right/Left` execute live in M2 and the authoring verbs route to `D05 T02 §6`). Done when: clicking or arrow-keying thumbnails scrolls `DocumentView`, scrolling `DocumentView` keeps the active thumbnail and ribbon marker in view, rotated pages update their thumbnails immediately, and switching to horizontal orientation matches the `D-ReadingRoom.png` filmstrip.

- [ ] Build `src/app/panels/ThumbnailList.xaml/.h/.cpp` using a virtualized `ItemsRepeater` with `ElementPrepared` and `ElementClearing` lifecycle hooks. Done when: opening a 500-page PDF instantiates XAML elements only for the ~8-12 visible thumbnails. Cheaper substitute that fails the checkpoint: eagerly creating 500 thumbnail controls or rasterizing all pages on open.
- [ ] Request thumbnail renders through `TileScheduler` (`D01 T02 §8`) at the thumbnail zoom bucket and cancel scrolled-past requests via `CancelToken` in `ElementClearing`. Done when: rapid scrolling from thumbnail 1 to 500 aborts intermediate thumbnail renders and renders pages 490-500 immediately.
- [ ] Render the current-page indicator per SPEC 8.3: `2 epx` `RectoAccentBrush` ring around the thumbnail, `RectoAccentTintBrush` card background, and `resources/svg/ribbon-marker.svg` (`ribbon-marker-on-dark.svg` in dark theme) anchored to the top-right edge of the active thumbnail. Done when: the active thumbnail matches `D-Focus-Light.png`, `D-Classic.png`, and `D-ReadingRoom.png`.
- [ ] Synchronize `ThumbnailList` bidirectionally with `DocumentView` (`D02 T02 §6`): clicking or pressing `Enter`/`Space` on a thumbnail calls `DocumentView::go_to_page`, and scrolling `DocumentView` updates the selected thumbnail and scrolls it into view (`StartBringIntoView`). Done when: both directions stay in sync across continuous scrolling and page jumps.
- [ ] Refresh thumbnail bitmaps automatically when a page's rotation changes (`Ctrl+R` / `Ctrl+Shift+R` or thumbnail context-menu rotate) since `TileKey` encodes rotation. Done when: rotating the view updates visible thumbnails without stale unrotated images.
- [ ] Implement `set_orientation(Orientation)` switching `StackLayout::Orientation` between `Vertical` (for `LeftOverlaySlot` and `LeftDockSlot`) and `Horizontal` (for `BottomDockSlot` filmstrip in `Reading room`) without recreating `ThumbnailList` or losing scroll state. Done when: switching to `Horizontal` in `BottomDockSlot` matches `D-ReadingRoom.png`.
- [ ] Attach the SPEC 8.3 right-click `MenuFlyout` to each thumbnail (`Insert Blank Page`, `Rotate Right`, `Rotate Left`, `Crop Page`, `Extract Page`, `Delete Page`), with `Rotate Right`/`Rotate Left` wired live and the page-surgery items referencing `D05 T01 §2`. Done when: right-clicking any thumbnail opens the 6-item context menu with working rotation and named deferrals.
- [ ] Add `Panels.ThumbnailListVirtualizationAndSync` test in `tests/recto-tests/` asserting virtualized element count on a 500-page document, `< 32 MB` thumbnail memory, two-way current-page sync, rotation refresh, and vertical/horizontal filmstrip switching. Done when: `recto-tests.exe --gtest_filter=Panels.ThumbnailListVirtualizationAndSync` passes.
- [ ] Commit: `viewer: add virtualized ThumbnailList with ribbon marker, filmstrip mode and context menu`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.ThumbnailListVirtualizationAndSync` passes on the display-session host, 500-page scroll telemetry stays within budget, and screenshots match `D-Focus-Light.png`, `D-Classic.png`, and `D-ReadingRoom.png`.

## 5. Outline tree panel (`OutlinePanel`) and bidirectional section tracking

Build the `Outline` side panel (`src/app/panels/OutlinePanel.xaml/.h/.cpp`) per SPEC section 8.3: binds the hierarchical document bookmark tree (`recto::pdf::OutlineItem` from `D01 T01 §4`) into a WinUI `TreeView`, highlights the current section matching the viewport's current page with `RectoAccentTintBrush` and a `3 epx` `RectoAccentBrush` bar on the left edge, navigates `DocumentView` when an outline item is clicked or activated via keyboard, provides expand/collapse controls, and renders an honest empty state when a PDF has no outline.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 8.3 (`Outline`: hierarchical `TreeView` of document bookmarks; current section highlighted with `RectoAccentTintBrush` and a `3 epx` accent bar on the left) with `design/screenshots/D-Focus-Light.png` and `D-ReadingRoom.png`.
**Job:** The user can browse a book or manual's hierarchical table of contents, see which chapter/section they are currently reading, and jump to any section by mouse or keyboard alone. Consumer: `SidePanelHost` (§3), Interactive Bookmark Editor (§6), and `CommandBoxControl` (§12).
**Treatment:** WinUI `TreeView` bound to hierarchical `OutlineNodeViewModel` items with O(log N) page-to-section active node tracking and loop-free two-way synchronization. Cheaper substitute that fails the checkpoint: a flat un-nested list of top-level bookmarks only, or one-way navigation that fails to highlight the active section while scrolling.
**Chrome:** consume `SidePanelHost` (§3), `RectoAccentTintBrush`, `RectoAccentBrush`, and `RectoTextSecondaryBrush` for right-aligned target page numbers.

**Build order:**

1. Author `src/app/panels/OutlinePanel.xaml/.h/.cpp` binding `DocumentDescription::outline` (`D01 T01 §4`) into a hierarchical `TreeView` where each row displays the bookmark title (`BodyTextBlockStyle`, or `BodyStrongTextBlockStyle` when active), right-aligned target page number (`CaptionTextBlockStyle` in `RectoTextSecondaryBrush`), and a `3 epx` left `RectoAccentBrush` indicator bar + `RectoAccentTintBrush` row background on the active section. Done when: opening a multi-level outlined corpus PDF renders all nesting levels with target page numbers matching `recto-cli info`.
2. Implement bidirectional synchronization between `OutlinePanel` and `DocumentView` (`D02 T02 §6`): activating an outline node scrolls `DocumentView` to its target `(page_index, top_offset_pt)` and records back-history (`Alt+Left`); scrolling `DocumentView` finds the deepest outline entry whose destination precedes or equals the current page/offset and highlights it (expanding ancestor nodes if needed without triggering a feedback scroll loop); and when a PDF has an empty `/Outlines` tree, display the empty state (`"No outline in this document"` plus a button to invoke §6's `"Generate outline from headings"`). Done when: scrolling through chapters updates the active `TreeView` node automatically and keyboard-only `TreeView` navigation (`Up`, `Down`, `Left`, `Right`, `Enter`) drives the document view.

- [ ] Author `src/app/panels/OutlinePanel.xaml/.h/.cpp` hosting a hierarchical WinUI `TreeView` populated from `DocumentDescription::outline`. Done when: arbitrarily nested PDF outlines (up to the engine's recursion cap) render with expand/collapse chevrons and target page numbers.
- [ ] Style the active outline row per SPEC 8.3 with `RectoAccentTintBrush` background, `BodyStrongTextBlockStyle` label, and a `3 epx` vertical `RectoAccentBrush` bar on the left edge. Done when: the active outline item visual matches SPEC 8.3.
- [ ] Navigate `DocumentView` to the bookmark's target page and vertical coordinate (`XYZ` / `Fit` / `FitH` destination) when the user clicks or presses `Enter` on a `TreeView` item, pushing the previous position onto `DocumentView`'s `Alt+Left` back-navigation stack (`D02 T02 §6`). Done when: clicking a subsection bookmark jumps directly to that heading on the page.
- [ ] Track `DocumentView::CurrentPageChanged` and scroll offset to highlight the deepest outline entry covering the current reading position without causing a re-entrant scroll feedback loop. Done when: manually scrolling the document smoothly moves the `3 epx` accent bar down the outline tree. Cheaper substitute that fails the checkpoint: clicking-only outlines that never track the user's current chapter while scrolling.
- [ ] Support full keyboard-only operation (`Up`/`Down` between items, `Right` to expand, `Left` to collapse or move to parent, `Enter` to jump to section) with `AutomationProperties.Name` exposing title, level, and page number. Done when: a keyboard-only script navigates across chapters and subsections without mouse input.
- [ ] Add an `Expand all / Collapse all` toggle button in `SidePanelHost`'s header action slot when `OutlinePanel` is active. Done when: clicking the button expands or collapses all nested bookmark levels.
- [ ] Render an honest empty state when `DocumentDescription::outline` is empty (`"This document doesn't have an outline."` with a `"Generate from headings"` action wired in §6). Done when: opening a flat corpus PDF shows the empty state instead of a blank panel.
- [ ] Add `Panels.OutlineTreeAndTwoWaySync` test in `tests/recto-tests/` verifying multi-level tree binding, click/keyboard navigation to destination page/Y, scroll-driven active-section tracking, and empty-state rendering. Done when: `recto-tests.exe --gtest_filter=Panels.OutlineTreeAndTwoWaySync` passes.
- [ ] Commit: `viewer: build OutlinePanel TreeView with two-way section tracking and empty state`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.OutlineTreeAndTwoWaySync` passes on the display-session host, proving keyboard-only outline navigation and two-way scroll tracking.

## 6. Interactive Bookmark Editor and heading-based outline auto-generation

Implement interactive bookmark authoring and heading-based outline auto-generation in `src/pdf/outlines_edit.cpp`, `recto-core`, and `OutlinePanel` (`design/SPEC.md` section 8.3): users can add a bookmark at the current page and zoom (`GoTo` destination), add a child bookmark, rename (`F2`), reorder or nest bookmarks via drag-and-drop or keyboard (`Alt+Up`, `Alt+Down`, `Alt+Left`, `Alt+Right`), delete bookmarks (`Delete`), or auto-generate a hierarchical outline from document heading font sizes/weights on untagged PDFs, with full `Undo` / `Redo` (`Ctrl+Z` / `Ctrl+Y`) and persistence through `SavePipeline` (`D02 T01 §8`).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6 (`bookmark add`, `undo`, `redo`, `delete` glyphs), 8.3 (`Outline (with Bookmark Editor)`), and 10 (`Ctrl+Z`, `Ctrl+Y`, `Ctrl+S`) with `design/screenshots/D-Focus-Light.png`.
**Job:** When a PDF has missing, broken, or incomplete bookmarks, the user can add, rename, organize, or auto-generate a complete table of contents and save it into the PDF. Consumer: `OutlinePanel` (§5), `DocumentActor`, `SavePipeline` (`D02 T01 §8`), and `CommandRegistry` (§11).
**Treatment:** Native `recto-pdf` `/Outlines` doubly-linked tree serializer (`src/pdf/outlines_edit.cpp` writing `/Outlines`, `/First`, `/Last`, `/Parent`, `/Prev`, `/Next`, `/Count`, `/Title` UTF-16BE/PDFDocEncoding, and `/Dest` arrays) backed by a per-document reversible command stack (`Undo`/`Redo`) and mark-dirty integration with `DocumentTabSession` (`D02 T01 §3`). Cheaper substitute that fails the checkpoint: storing bookmarks in an external app-local JSON file instead of writing standard PDF `/Outlines` objects that Adobe Acrobat, Edge, and `recto-cli` read natively.
**Chrome:** extend `OutlinePanel` (§5) inside `SidePanelHost` (§3) using stock `TreeView` drag-and-drop and inline `TextBox` rename.

**Build order:**

1. Implement `src/pdf/outlines_edit.cpp` in `recto-pdf`: serialize an edited `std::vector<OutlineItem>` tree into valid indirect PDF `/Outlines` dictionary objects (computing `/First`, `/Last`, `/Parent`, `/Prev`, `/Next`, signed `/Count` for open/closed subtrees, UTF-16BE `/Title` strings, and `[page_ref /XYZ left top zoom]` `/Dest` arrays) compatible with both incremental save (`D01 T02 §2`) and full `PdfWriter` save (`D04 T02 §1`), plus `generate_outline_from_headings(Document&)` analyzing per-page font sizes, weights, and vertical spacing to detect `H1`/`H2`/`H3` heading hierarchy. Done when: `PdfOutlinesEdit.*` unit tests create, nest, reorder, delete, and auto-generate outlines, save the PDF, re-parse via `recto::pdf::Document::open`, and verify identical hierarchy and destinations under ASan.
2. Wire the Bookmark Editor UI in `OutlinePanel` (§5): header `+ Add bookmark` and `Generate outline` actions, right-click context menu (`Add bookmark here`, `Add child bookmark`, `Rename (F2)`, `Move up (Alt+Up)`, `Move down (Alt+Down)`, `Promote (Alt+Left)`, `Demote (Alt+Right)`, `Delete (Del)`), `TreeView` `CanReorderItems="True"` drag-and-drop reparenting, `Undo`/`Redo` (`Ctrl+Z`/`Ctrl+Y`) stack, and `DocumentTabSession::set_dirty(true)`. Done when: adding, renaming, nesting, deleting, undoing/redoing, and saving bookmarks in the UI updates the tab's dirty state and persists standard `/Outlines` entries verified on reopen.

- [ ] Author `src/pdf/outlines_edit.cpp` serializing hierarchical `OutlineItem` trees into standard PDF 1.7 / 2.0 `/Outlines` indirect object graphs (`/First`, `/Last`, `/Parent`, `/Prev`, `/Next`, `/Count`, UTF-16BE `/Title`, `/Dest`). Done when: `PdfOutlinesEdit.RoundTrip` writes a 3-level outline tree, saves, re-opens, and reads back every title and `/XYZ` destination identically under ASan. Cheaper substitute that fails the checkpoint: saving bookmarks only in app-local metadata rather than standard PDF `/Outlines` dictionaries.
- [ ] Implement `generate_outline_from_headings` in `src/pdf/outlines_edit.cpp` clustering distinct heading font sizes and bold weights above body text median to build a nested `H1 -> H2 -> H3` bookmark tree with accurate page and Y coordinates. Done when: running heading auto-generation on an un-outlined multi-section corpus PDF produces a structured outline matching its section headings.
- [ ] Add `+ Add bookmark` (creating a bookmark at the current viewport page and top Y offset and entering inline rename immediately) and `Generate outline from headings` buttons to `OutlinePanel`'s header action bar and empty state. Done when: clicking `+ Add bookmark` on page 4 inserts a new bookmark targeting page 4 with its title selected for editing.
- [ ] Implement inline `Rename` (`F2` or double-click), `Add child bookmark`, and `Delete` (`Delete` key or context menu) on `OutlinePanel` nodes. Done when: pressing `F2` edits the title in place, `Enter` commits, and `Esc` cancels the rename.
- [ ] Enable `TreeView` drag-and-drop reordering/nesting and keyboard hierarchy manipulation (`Alt+Up` move up, `Alt+Down` move down, `Alt+Left` outdent/promote, `Alt+Right` indent/demote). Done when: both mouse drag-and-drop and `Alt+Arrow` keys reorder and reparent bookmarks identically.
- [ ] Back every bookmark mutation (add, rename, move/nest, delete, auto-generate) with a per-session `Undo`/`Redo` command stack (`Ctrl+Z` / `Ctrl+Y`) and set `DocumentTabSession::set_dirty(true)` (`D02 T01 §3`). Done when: deleting a subtree with 5 child bookmarks and pressing `Ctrl+Z` restores the entire 5-node subtree in its exact position.
- [ ] Persist modified outlines when the user invokes `Save` (`Ctrl+S`) or `Save a copy` (`Ctrl+Shift+S`) through `SavePipeline` (`D02 T01 §8`). Done when: saving and reopening the file displays the edited bookmark tree.
- [ ] Add `PdfOutlinesEdit.*` and `Panels.BookmarkEditorAndAutoGen` tests in `tests/recto-tests/` covering `/Outlines` object-graph round-tripping, heading auto-generation, UI CRUD, `Alt+Arrow` nesting, `Ctrl+Z`/`Ctrl+Y` undo/redo, and save persistence. Done when: `recto-tests.exe --gtest_filter=PdfOutlinesEdit.*:Panels.BookmarkEditorAndAutoGen` passes under ASan.
- [ ] Commit: `viewer: add interactive Bookmark Editor, heading outline generator and /Outlines writer`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfOutlinesEdit.*:Panels.BookmarkEditorAndAutoGen` passes under ASan, proving standard PDF `/Outlines` round-tripping, heading auto-generation, and UI undo/redo.

## 7. Optional Content Groups (`OCG` / Layers) engine and live Layers panel

Implement PDF Optional Content Groups (`OCG` / Layers, ISO 32000-1 section 8.11) in `src/pdf/ocg.cpp`, `DocumentActor`, and `LayersPanel` (`src/app/panels/LayersPanel.xaml/.h/.cpp`) per `design/SPEC.md` section 8.3: parse catalog `/OCProperties` (`/OCGs`, `/D` default dictionary with `/ON`, `/OFF`, `/Order`, `/RBGroups`, `/Locked`), evaluate `BDC /OC <name> ... EMC` and `/OC` XObject/Annotation visibility during `DisplayList` compilation (`D01 T02 §1`), expose the `Layers` side panel with eye visibility toggles that immediately re-composite visible pages, and support `Reset to Default Visibility` and `Flatten Visible Layers` (saving a copy with hidden layer content removed).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6 (`layers` glyph) and 8.3 (`Layers (OCG)`: displays `/OCProperties` with eye visibility toggles that re-composite the page immediately, `Reset to Default Visibility`, and `Flatten Visible Layers`) with `design/screenshots/D-Focus-Light.png`.
**Job:** Users viewing CAD drawings, architectural plans, multi-language documents, or layered illustrations can toggle individual layers on and off in real time, reset to the document's default layer state, or flatten visible layers into a clean copy. Consumer: `SidePanelHost` (§3), `DocumentActor` (`D01 T02 §2`), `TileTextureCache` (`D02 T02 §3`), and `SavePipeline` (`D02 T01 §8`).
**Treatment:** Native `/OCProperties` and `BDC /OC ... EMC` conditional visibility evaluation in `recto-pdf` with `ocg_generation` versioning in `TileKey` so toggling a layer invalidates and re-renders visible tiles immediately without race conditions. Cheaper substitute that fails the checkpoint: ignoring `BDC /OC` operators so all layers always render on top of each other, or failing to re-render the canvas when an eye toggle is clicked.
**Chrome:** consume `SidePanelHost` (§3), `RectoColors.xaml` tokens, and Segoe Fluent eye/layers glyphs.

**Build order:**

1. Implement `src/pdf/ocg.cpp` in `recto-pdf`: parse catalog `/OCProperties` (`/OCGs` array, `/D` configuration `/Order` nested tree, `/BaseState`, `/ON`, `/OFF`, `/RBGroups` radio-button exclusivity groups, and `/Locked` layer flags), filter `BDC /OC <prop_name> ... EMC` content-stream spans, Form/Image XObject `/OC` entries, and Annotation `/OC` entries against the active `OcgState` bitmask when recording `DisplayList`s, and implement `flatten_visible_layers(Document&, OcgState)` stripping hidden `BDC /OC ... EMC` blocks and `/OCProperties` on a copy. Done when: `PdfOcg.*` unit tests toggle layers on a layered PDF fixture, verify distinct rendered pixel outputs per layer state, and verify flattened copies contain zero hidden-layer operators under ASan.
2. Build `src/app/panels/LayersPanel.xaml/.h/.cpp` hosted in `SidePanelHost` (§3): hierarchical list/tree following `/D /Order`, eye toggle button per layer (respecting `/Locked` and `/RBGroups`), `Reset to Default Visibility` header action, and `Flatten Visible Layers` action (saving via `SavePipeline::save_copy_async`, `D02 T01 §8`). Done when: toggling a layer's eye button increments `ocg_generation`, re-renders the visible viewport on `DocumentView` within one tile cycle, and `Reset to Default Visibility` restores the initial `/D` state.

- [ ] Author `src/pdf/ocg.cpp` parsing `/OCProperties` (`/OCGs`, `/D` `/BaseState`, `/ON`, `/OFF`, hierarchical `/Order`, `/RBGroups`, `/Locked`) and Optional Content Membership Dictionaries (`/Type /OCMD` with `/P` policy `/AllOn`, `/AnyOn`, `/AnyOff`, `/AllOff`). Done when: `PdfOcg.ParseConfig` parses nested layer trees and `/OCMD` policies under ASan with zero leaks.
- [ ] Evaluate active `OcgState` during `DisplayList` compilation (`src/pdf/interpreter.cpp`) for `BDC /OC <name> ... EMC` marked-content sequences, `/OC`-tagged XObjects, and `/OC`-tagged `/Annots`. Done when: turning a layer off suppresses all paths, text, and images inside its `BDC /OC ... EMC` block. Cheaper substitute that fails the checkpoint: parsing `/OCProperties` for display in the UI while still drawing every layer unconditionally on the canvas.
- [ ] Wire `DocumentActor::set_ocg_visibility(ocg_id, visible)` and `reset_ocg_visibility()` in `recto-core`, incrementing `TileKey::ocg_generation` and invalidating `TileTextureCache` (`D02 T02 §3`) so visible pages re-render immediately. Done when: toggling a layer updates the `DocumentView` canvas without stale cached tiles from the previous layer state.
- [ ] Implement `flatten_visible_layers` in `src/pdf/ocg.cpp` removing hidden `BDC /OC ... EMC` content streams, hidden `/OC` XObjects/annotations, and `/OCProperties` when writing a flattened copy. Done when: the flattened PDF renders identically to the active layer view and has an empty layer list when reopened.
- [ ] Build `src/app/panels/LayersPanel.xaml/.h/.cpp` with eye visibility toggles (`AutomationProperties.Name` naming the layer and its visible/hidden state), `/RBGroups` mutual-exclusion handling, disabled state for `/Locked` layers, `Reset to Default Visibility`, and `Flatten Visible Layers`. Done when: every SPEC 8.3 `Layers (OCG)` control works and is keyboard accessible.
- [ ] Render an honest empty state (`"This document has no layers."`) when a PDF has no `/OCProperties` dictionary. Done when: opening a standard single-layer PDF shows the empty state cleanly.
- [ ] Register `Toggle Layers Panel`, `Reset Layer Visibility`, and `Flatten Visible Layers` in `CommandRegistry` (§11). Done when: all three commands are reachable from `Ctrl+K`.
- [ ] Add `PdfOcg.*` and `Panels.OcgLayersPanel` tests in `tests/recto-tests/` verifying `/OCProperties` parsing, `BDC /OC` pixel suppression, live UI toggle re-rendering, `Reset to Default Visibility`, and `Flatten Visible Layers`. Done when: `recto-tests.exe --gtest_filter=PdfOcg.*:Panels.OcgLayersPanel` passes under ASan.
- [ ] Commit: `viewer: add OCG Layers engine in recto-pdf and live LayersPanel in viewer`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfOcg.*:Panels.OcgLayersPanel` passes under ASan, proving live layer toggling on the canvas, `/OCMD` evaluation, and layer flattening.

## 8. Embedded Files and PDF Portfolios (`/EmbeddedFiles` and `/Collection`) engine and panel

Implement embedded file attachments and PDF Portfolios (`/Names /EmbeddedFiles`, `FileAttachment` annotations, and `/Collection` portfolios per ISO 32000-1 sections 7.11 and 12.3.5) in `src/pdf/portfolio.cpp`, `recto-core`, and `AttachmentsPanel` (`src/app/panels/AttachmentsPanel.xaml/.h/.cpp`) per `design/SPEC.md` section 8.3: list every embedded file and portfolio item with file icon, name, formatted byte size, modified date, and description; support `Open` (opening embedded `.pdf` items in a new Recto tab, and prompting with a security confirmation dialog before launching any external file type per SPEC 9.9), `Save / Extract` to disk, `Add File`, `Remove`, and `Save as PDF Portfolio`.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 6 (`paperclip attachment`, `portfolio folder` glyphs), 8.3 (`Attachments & Portfolio`: file icon, name, size, modified date, description; `Open` prompting before external types, `Save / Extract`, `Add File`, `Remove`, `Save as PDF Portfolio`), and 9.9 (security confirmation before opening external files) with `design/screenshots/D-Focus-Light.png`.
**Job:** Users working with legal bundles, ZUGFeRD invoices, scientific data attachments, or PDF Portfolios (`/Collection`) can inspect, extract, open, add, or remove embedded files and save multi-file PDF Portfolios directly inside Recto. Consumer: `SidePanelHost` (§3), `OpenDocumentPipeline` (`D02 T01 §5`), `SavePipeline` (`D02 T01 §8`), and `CommandRegistry` (§11).
**Treatment:** Native `recto-pdf` `/Names /EmbeddedFiles` name-tree and `/Collection` portfolio parser/writer (`src/pdf/portfolio.cpp`) with enforced decompression byte ceilings on `/EF` streams, atomic disk extraction (`ReplaceFileW`), and mandatory security prompt before launching non-PDF attachments. Cheaper substitute that fails the checkpoint: shell-executing extracted attachments without warning, or ignoring `/Collection` portfolio dictionaries.
**Chrome:** consume `SidePanelHost` (§3), `RectoDialogStyles.xaml` (`D02 T01 §10`), `FileOpenPicker` / `FileSavePicker`, and `RectoColors.xaml`.

**Build order:**

1. Implement `src/pdf/portfolio.cpp` in `recto-pdf`: parse the catalog `/Names /EmbeddedFiles` name tree and page `/FileAttachment` annotations into `EmbeddedFileEntry` structs (`name`, `unicode_filename` `/UF` or `/F`, `description` `/Desc`, `mime_subtype` `/Subtype`, `uncompressed_size` `/Params /Size`, `creation_date`, `mod_date`, `checksum` `/CheckSum`, `collection_item` `/CI` portfolio metadata fields) plus `/Collection` portfolio schema (`/Schema`, `/D` initial document, `/View`, folder hierarchy); extract `/EF /F` stream bytes within decompression caps; and implement `add_embedded_file`, `remove_embedded_file`, and `create_pdf_portfolio`. Done when: `PdfPortfolio.*` unit tests round-trip embedded files and a multi-file `/Collection` portfolio, verifying byte-identical extraction and MD5 `/CheckSum` validation under ASan.
2. Build `src/app/panels/AttachmentsPanel.xaml/.h/.cpp` hosted in `SidePanelHost` (§3): displays each attachment/portfolio item with file-type glyph, name, formatted size, modified date, and description; wires `Save / Extract` (`FileSavePicker` for one item, `FolderPicker` for `Extract All` with path-traversal sanitization stripping `..`, `/`, `\`, and drive prefixes from embedded filenames), `Open` (opening embedded `.pdf` files directly into a new Recto tab via a read-only temp copy, refusing dangerous executable extensions `.exe`/`.bat`/`.cmd`/`.ps1`/`.vbs`/`.js`/`.msi`/`.scr`, and showing a SPEC 9.9 confirmation dialog before opening other external file types), `Add File` (`FileOpenPicker`), `Remove`, and `Save as PDF Portfolio`. Done when: driven tests verify extraction, path-traversal neutralization on malicious `/UF` names (`..\..\evil.txt`), `.exe` launch refusal, external-file confirmation dialog, and adding/removing attachments with `Undo`/`Redo`.

- [ ] Author `src/pdf/portfolio.cpp` parsing and writing `/Names /EmbeddedFiles` name trees, `/Filespec` dictionaries (`/F`, `/UF`, `/EF`, `/Desc`, `/CI`), page `/FileAttachment` annotations, and `/Collection` PDF Portfolio dictionaries. Done when: `PdfPortfolio.ReadWriteAndCollection` round-trips attachments and portfolios with byte-for-byte payload equality under ASan.
- [ ] Sanitize embedded filenames (`/UF` and `/F`) in `src/pdf/portfolio.cpp` and `AttachmentsPanel` by stripping directory separators (`/`, `\`), `..` segments, Windows reserved device names (`CON`, `NUL`, `COM1`), and trailing dots/spaces before writing any extracted file to disk. Done when: a crafted attachment named `..\..\AppData\Roaming\evil.dll` extracts strictly as `evil.dll` inside the user-chosen folder. Cheaper substitute that fails the checkpoint: joining the raw `/UF` string directly to the output directory path (arbitrary file overwrite vulnerability).
- [ ] Build `src/app/panels/AttachmentsPanel.xaml/.h/.cpp` displaying file icon, name, formatted size (`KB`/`MB`), modified date, and description, plus a portfolio banner when the open PDF is a `/Collection` portfolio. Done when: visual layout and item metadata match SPEC 8.3.
- [ ] Wire `Open` on attachment rows: if the attachment is a `.pdf`, open it safely inside a new Recto tab; if it carries an executable/script extension (`.exe`, `.dll`, `.bat`, `.cmd`, `.ps1`, `.vbs`, `.js`, `.wsf`, `.msi`, `.scr`, `.hta`, `.lnk`), refuse execution outright with a warning dialog; for other external documents (`.docx`, `.xlsx`, `.xml`, `.png`, `.txt`), prompt via a SPEC 9.9 confirmation `ContentDialog` naming the filename and file type before calling `Launcher::LaunchFileAsync`. Done when: embedded PDFs open in Recto, executables are refused, and external files never open without confirmation.
- [ ] Wire `Save / Extract` (single file via `FileSavePicker` and `Extract All` via `FolderPicker`) using atomic temp-file-then-replace writes. Done when: extracted files match the embedded stream bytes 1:1.
- [ ] Wire `Add File` (`FileOpenPicker`), `Remove` (`Delete`), and `Save as PDF Portfolio` (packaging multiple selected files into a `/Collection` PDF) with `Undo`/`Redo` (`Ctrl+Z`/`Ctrl+Y`) and `DocumentTabSession::set_dirty(true)`. Done when: adding and removing attachments marks the tab dirty and persists through `SavePipeline` (`D02 T01 §8`).
- [ ] Render an honest empty state (`"No attachments in this document"` with an `"Add file"` button) when a PDF has zero embedded files. Done when: standard PDFs without attachments display the empty state.
- [ ] Add `PdfPortfolio.*` and `Panels.AttachmentsAndPortfolio` tests in `tests/recto-tests/` covering `/EmbeddedFiles` and `/Collection` round-tripping, path-traversal filename sanitization, executable launch refusal, external open confirmation, and UI add/remove/extract flows. Done when: `recto-tests.exe --gtest_filter=PdfPortfolio.*:Panels.AttachmentsAndPortfolio` passes under ASan.
- [ ] Commit: `viewer: add EmbeddedFiles/Portfolio engine in recto-pdf and AttachmentsPanel UI`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfPortfolio.*:Panels.AttachmentsAndPortfolio` passes under ASan, proving `/EmbeddedFiles` and `/Collection` round-tripping, path-traversal sanitization, and safe attachment opening.

## 9. Document Info panel: metadata, security permissions and embedded font inventory

Build the read-only `DocumentInfoPanel` (`src/app/panels/DocumentInfoPanel.xaml/.h/.cpp`) per `design/SPEC.md` section 9.2 and `design/screenshots/D-ReadingRoom.png`: displays formatted document metadata (`Title`, `Author`, `Subject`, `Keywords`, `Created`, `Modified`, `Creator`, `Producer`), structural properties (`Pages`, `Page size` in `mm`, `in`, and `pt`, `PDF version`, `File size`, `Fast Web View / Linearized` status, and `PDF/A` or `PDF/UA` conformance badge), the complete `Security & Permissions` summary (encryption algorithm and individual permission flags), and the `Fonts` inventory (font name, type, embedded/subset status, encoding), hosted both in the `Reading room` right inspector (`D-ReadingRoom.png`) and in the `Document Properties` dialog (`More -> Properties` / `File -> Document Properties`).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 5 (`CaptionTextBlockStyle` 12 / Regular labels, `BodyTextBlockStyle` 14 / Regular values), 8.2 (`More -> Properties`), and 9.2 (`Info, XMP & Viewer Preferences panel`) with `design/screenshots/D-ReadingRoom.png`.
**Job:** The user can inspect every fact about an open PDF (who created it, its physical dimensions in `mm` and `inches`, whether it is linearized or PDF/A compliant, what encryption and permissions apply, and whether all fonts are embedded) and copy any value with one click. Consumer: `SidePanelHost` (§3), `Reading room` inspector (`D02 T04 §3`), `Properties` dialog, and Editable Info mode (§10).
**Treatment:** Single `DocumentInfoPanel` control shared between `SidePanelHost` and the `Properties` `ContentDialog`, formatting raw PDF values into localized dates, human-readable byte sizes, tri-unit page dimensions (`210 × 297 mm (8.27 × 11.69 in / 595 × 842 pt)`), explicit encryption/permission flags, and per-font badges. Cheaper substitute that fails the checkpoint: dumping unformatted PDF `D:20261014...` date strings, omitting mm/inch conversions, or writing `"Encrypted: Yes"` without the cipher and permission flags.
**Chrome:** consume `SidePanelHost` (§3), `RectoDialogStyles.xaml` (`D02 T01 §10`), `CaptionTextBlockStyle`, and `BodyTextBlockStyle`.

**Build order:**

1. Implement the formatting helpers in `src/app/panels/DocumentInfoFormatter.h/.cpp` converting `DocumentDescription` (`D01 T01 §4`) into display records: PDF date strings (`D:YYYYMMDDHHmmSSOHH'mm'`) to ISO/localized date-time strings, page points (`1 pt = 1/72 in = 25.4/72 mm`) to `mm`, `in`, and `pt` with standard paper size detection (`A4`, `Letter`, `Legal`, `A3`), byte counts to `KB`/`MB`, encryption summary (`None`, `RC4 40-bit`, `RC4 128-bit`, `AES-128 (R=4)`, `AES-256 (R=5/R=6)`) with 6 distinct permission statuses (`Printing`, `Copying text`, `Modifying document`, `Adding annotations`, `Filling form fields`, `Accessibility extraction`), and deduplicated document-wide font records (`name` stripping `ABCDEF+` subset prefix into a `"Subset"` badge, `subtype`, `is_embedded`, `encoding`). Done when: unit tests verify exact formatting against known PDF fixtures and match `recto-cli info` output field for field.
2. Build `src/app/panels/DocumentInfoPanel.xaml/.h/.cpp` with selectable/copyable label-value rows, a `"Copy all info"` header button, an `"Edit Info & Preferences"` trigger button (activating §10's edit mode), and host the same control inside both `SidePanelHost` (`Info` tab / `Reading room` right inspector) and a `DocumentPropertiesDialog` opened from `More -> Properties` (`D02 T02 §8`) and `File -> Document Properties` (`D02 T01 §9`). Done when: both the inspector and the `Properties` dialog display identical formatted values matching `D-ReadingRoom.png` and screen-reader automation peers read each label-value pair in order.

- [ ] Author `src/app/panels/DocumentInfoFormatter.h/.cpp` formatting PDF dates (`D:...`), tri-unit page sizes (`mm`, `in`, `pt` + paper name `A4`/`Letter`), file sizes, linearization status, PDF/A & PDF/UA badges, encryption cipher + 6 permission flags, and font inventory. Done when: unit tests verify every formatter edge case (malformed PDF dates, custom page dimensions, `ABCDEF+FontName` subset tags, AES-256 permission bitmasks).
- [ ] Author `src/app/panels/DocumentInfoPanel.xaml/.h/.cpp` matching the `Info` inspector layout in `D-ReadingRoom.png` using `CaptionTextBlockStyle` labels and `BodyTextBlockStyle` values with `IsTextSelectionEnabled="True"`. Done when: visual layout matches `D-ReadingRoom.png` in Light and Dark themes.
- [ ] Display the complete `Security` breakdown (`Method` + explicit `Allowed` / `Not allowed` badges for `Printing`, `Copying`, `Modifying`, `Annotating`, `Form filling`, `Page extraction`) and the complete `Fonts` list (`Font family`, `Type`, `Embedded` / `Embedded subset` / `Not embedded` badge). Done when: an encrypted or subset-font corpus file displays every cipher, permission, and font accurately. Cheaper substitute that fails the checkpoint: showing only `"Encrypted: Yes"` or omitting the font inventory.
- [ ] Host `DocumentInfoPanel` inside `SidePanelHost` (§3) and inside `DocumentPropertiesDialog` (`More -> Properties` in `D02 T02 §8` and `File -> Document Properties` in `D02 T01 §9`). Done when: opening `Properties` from the toolbar or menu shows the exact same control and data as the `Reading room` inspector.
- [ ] Wire `"Copy all info"` to copy a clean Markdown/plain-text table of all document properties, security flags, and fonts to the clipboard. Done when: clicking `"Copy all info"` places the full formatted summary on the system clipboard.
- [ ] Set `AutomationProperties.Name` and `LabeledBy` relationships on every info row so Windows Narrator reads `"Pages: 12"`, `"Page size: 210 by 297 millimeters, A4"`, etc. in logical order. Done when: UIA property tree inspection confirms every value is paired with its label.
- [ ] Add `Panels.DocumentInfoAndFonts` test in `tests/recto-tests/` comparing `DocumentInfoFormatter` and `DocumentInfoPanel` rows against `recto-cli info` across 3 corpus files (including encrypted and multi-font files). Done when: `recto-tests.exe --gtest_filter=Panels.DocumentInfoAndFonts` passes.
- [ ] Commit: `viewer: build DocumentInfoPanel with tri-unit page sizes, security permissions and font inventory`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.DocumentInfoAndFonts` passes on the display-session host, values match `recto-cli info` across three corpus files, and rendered screenshot matches `D-ReadingRoom.png`.

## 10. Editable Document Info, XMP metadata synchronization and `/ViewerPreferences` editor

Implement `src/pdf/metadata_edit.cpp` in `recto-pdf` and the `"Edit Info & Preferences"` mode inside `DocumentInfoPanel` (§9) per `design/SPEC.md` section 9.2: users can edit `/Info` fields (`Title`, `Author`, `Subject`, `Keywords`, `Creator`, `Producer`), add/edit/remove custom XMP metadata properties in the catalog `/Metadata` RDF/XML stream (keeping `/Info` and XMP synchronized per PDF 1.7 / PDF 2.0 rules), and configure catalog `/ViewerPreferences` (`DisplayDocTitle`, `FitWindow`, `CenterWindow`, `HideToolbar`, `HideMenubar`, initial `PageMode` such as `UseNone` / `UseOutlines` / `UseThumbs` / `FullScreen`, initial `PageLayout` such as `SinglePage` / `OneColumn` / `TwoPageRight`, and initial open page number `/OpenAction`), saving via `SavePipeline` (`D02 T01 §8`).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.3 and 9.2 ("Clicking **Edit Info & Preferences** lets the user edit `/Info` fields, custom XMP metadata properties, and `/ViewerPreferences` (`DisplayDocTitle`, `FitWindow`, `CenterWindow`, `HideToolbar`, `HideMenubar`, initial `PageMode` and `PageLayout`, initial open page) and save to a copy") with `design/screenshots/D-ReadingRoom.png`.
**Job:** The user can update a document's title, author, keywords, custom XMP tags, and initial viewer behavior (such as showing the document title in the window bar or opening directly in two-page layout on page 3) without needing external metadata tools. Consumer: `DocumentInfoPanel` (§9), `DocumentTabSession` (`D02 T01 §3` updating the tab title live when `DisplayDocTitle` or `Title` changes), and `SavePipeline` (`D02 T01 §8`).
**Treatment:** Synchronized `/Info` dictionary + `/Metadata` XMP RDF/XML packet writer (`src/pdf/metadata_edit.cpp`) plus `/ViewerPreferences` and `/OpenAction` catalog dictionary serialization in `recto-pdf`, with `Undo`/`Redo` support and live application of `DisplayDocTitle`, `PageLayout`, and `PageMode` in the open tab. Cheaper substitute that fails the checkpoint: updating `/Info` while leaving a stale contradicting `/Metadata` XMP stream in the catalog, or ignoring `/ViewerPreferences` when opening a PDF.
**Chrome:** inline edit mode inside `DocumentInfoPanel` (§9) consuming `RectoColors.xaml` input styles (`CornerRadius=8`, `RectoBorderStrongBrush`).

**Build order:**

1. Implement `src/pdf/metadata_edit.cpp` in `recto-pdf`: parse and serialize `/Info` dictionary strings (UTF-16BE / PDFDocEncoding), parse and synthesize/update the catalog `/Metadata` XMP XML packet (`dc:title`, `dc:creator`, `dc:description`, `pdf:Keywords`, `xmp:CreatorTool`, `pdf:Producer`, `xmp:ModifyDate`, plus custom namespace/key-value properties) so `/Info` and XMP never disagree, and parse/write catalog `/ViewerPreferences` (`DisplayDocTitle`, `FitWindow`, `CenterWindow`, `HideToolbar`, `HideMenubar`, `NonFullScreenPageMode`), `/PageMode`, `/PageLayout`, and `/OpenAction` (`[page_ref /XYZ null null null]`). Done when: `PdfMetadataEdit.*` unit tests modify metadata, custom XMP keys, and viewer preferences, save via incremental save (`D01 T02 §2`), and re-parse identical `/Info`, XMP, and `/ViewerPreferences` fields under ASan.
2. Add the `"Edit Info & Preferences"` view state to `DocumentInfoPanel` (§9) with text inputs for `Title`, `Author`, `Subject`, `Keywords`, an add/remove custom XMP property list, toggles and dropdowns for `/ViewerPreferences` (`DisplayDocTitle`, `FitWindow`, `CenterWindow`, `HideToolbar`, `HideMenubar`, initial `PageMode`, initial `PageLayout`, initial open page `1..N`), `Apply` (updating the live `DocumentActor` state, updating the tab title immediately if `DisplayDocTitle` is set, and marking `DocumentTabSession::set_dirty(true)`), and `Cancel`. Done when: editing `Title` and turning on `DisplayDocTitle` updates the tab header immediately, marks the tab dirty, and persists in the saved PDF copy.

- [ ] Author `src/pdf/metadata_edit.cpp` reading and writing synchronized `/Info` dictionary fields and `/Metadata` XMP RDF/XML streams (escaping XML entities `&`, `<`, `>`, `"`, `'` safely so crafted metadata strings never produce malformed XML or XMP injection). Done when: `PdfMetadataEdit.InfoAndXmpSync` round-trips Unicode metadata and custom XMP properties with special XML characters under ASan. Cheaper substitute that fails the checkpoint: writing `/Info` only and leaving `/Metadata` XMP out of sync, or concatenating unescaped strings into XMP XML.
- [ ] Parse and serialize catalog `/ViewerPreferences` (`DisplayDocTitle`, `FitWindow`, `CenterWindow`, `HideToolbar`, `HideMenubar`), `/PageMode` (`UseNone`, `UseOutlines`, `UseThumbs`, `UseOC`, `UseAttachments`, `FullScreen`), `/PageLayout` (`SinglePage`, `OneColumn`, `TwoColumnLeft`, `TwoColumnRight`, `TwoPageLeft`, `TwoPageRight`), and `/OpenAction` initial page in `src/pdf/metadata_edit.cpp`. Done when: `PdfMetadataEdit.ViewerPreferences` round-trips every flag and initial page index under ASan.
- [ ] Honor `/PageMode`, `/PageLayout`, `/OpenAction` initial page, and `/ViewerPreferences /DisplayDocTitle` when opening a PDF in `OpenDocumentPipeline` (`D02 T01 §5`) and `DocumentView` (`D02 T02 §5`) unless overridden by user CLI args. Done when: a PDF with `/PageMode /UseOutlines` and `/PageLayout /TwoPageRight` opens with the Outline panel visible and Two-Page Spread active.
- [ ] Build the `"Edit Info & Preferences"` form inside `DocumentInfoPanel` (§9) with inputs for `Title`, `Author`, `Subject`, `Keywords`, custom XMP key-value rows (`+ Add property`, `Remove`), and all SPEC 9.2 `/ViewerPreferences` controls. Done when: clicking `"Edit Info & Preferences"` switches `DocumentInfoPanel` to the editable form with current values populated.
- [ ] Validate the initial open page `NumberBox` against `1..page_count` and wire `Apply` / `Cancel` with `Undo`/`Redo` (`Ctrl+Z`/`Ctrl+Y`) and `DocumentTabSession::set_dirty(true)`. Done when: applying edits updates the live tab title (when `DisplayDocTitle` is on) and `Ctrl+Z` reverts the metadata change.
- [ ] Register `"Edit Info & Preferences"` and `"Document Properties"` in `CommandRegistry` (§11). Done when: both commands are reachable from `Ctrl+K` and the `Edit` / `File` menus.
- [ ] Add `PdfMetadataEdit.*` and `Panels.EditableInfoAndViewerPrefs` tests in `tests/recto-tests/` verifying `/Info` + XMP synchronization, XML entity escaping, `/ViewerPreferences` open behavior, and UI edit/undo/save persistence. Done when: `recto-tests.exe --gtest_filter=PdfMetadataEdit.*:Panels.EditableInfoAndViewerPrefs` passes under ASan.
- [ ] Commit: `viewer: add Editable Document Info, XMP sync and /ViewerPreferences editor`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfMetadataEdit.*:Panels.EditableInfoAndViewerPrefs` passes under ASan, proving synchronized `/Info` + XMP serialization, `/ViewerPreferences` enforcement on open, and live UI editing.

## 11. Unified `CommandRegistry` across toolbars, menus, panels and keyboard accelerators

Build the central `CommandRegistry` (`src/app/commands/CommandRegistry.h/.cpp`) per `design/SPEC.md` sections 8.2, 9.4, 10 and `AGENTS.md` ("Every toolbar and menu command is reachable from the command box (Ctrl+K), every icon-only button carries an automation name and tooltip"): one authoritative table of every command in Recto (`id`, `section_group`, `title`, `search_keywords`, `icon_glyph`, `shortcut_display`, `virtual_key` + `modifiers`, `is_enabled` predicate, `is_checked` predicate, and `execute` handler) that `ShellPage`, `ShellMenuBar` (`D02 T01 §9`), `PageToolbar` (`D02 T02 §8`), `SidePanelHost` (§3), and `CommandBoxControl` (§12) all bind to.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Implement `CommandRegistry` (`src/app/commands/CommandRegistry.h/.cpp`) storing strongly typed `CommandDescriptor` entries grouped by SPEC 9.4 category (`ThisDocument`, `GoTo`, `Files`, `Actions`), with dynamic `can_execute(AppCommandContext)` evaluation (e.g., requires an active open `DocumentTabSession`, requires multi-page document, requires dirty document) and `execute(AppCommandContext)` dispatch. Done when: registering all shell, file, view, panel, annotation, form, redaction, signature, conversion, organize, and settings commands populates a single queryable registry with zero duplicate command IDs or conflicting shortcuts.
2. Bind every button and menu item in `ShellPage` (`D02 T01 §2`), `ShellMenuBar` (`D02 T01 §9`), `PageToolbar` (`D02 T02 §8`), and `SidePanelHost` (§3) to its `CommandRegistry` entry so labels, Segoe Fluent glyphs, shortcut tooltips, `AutomationProperties.Name`, and `IsEnabled` states derive automatically from the registry, and write an automated completeness test verifying that 100% of toolbar and menu commands exist in `CommandRegistry`. Done when: `Panels.CommandRegistryCompleteness` scrapes every toolbar and menu element in `src/app` and confirms 1:1 registration, non-empty `AutomationProperties.Name`, and non-empty tooltips.

- [ ] Author `src/app/commands/CommandRegistry.h/.cpp` defining `CommandId`, `CommandCategory` (`ThisDocument`, `GoTo`, `Files`, `Actions`), `CommandDescriptor`, and `AppCommandContext`. Done when: `CommandRegistry` compiles and supports O(1) lookup by `CommandId` and ordered enumeration by category.
- [ ] Register every M2 command (file open/save/close, tab navigation, zoom/fit/rotate/layout/page-colour/full-screen, side-panel toggles, bookmark/layer/attachment/info actions, settings, presets, theme) plus named entries for every M3-M6 surface command (`Find`, `Find & Replace`, `Annotate`, `Form Designer`, `Redact & Sanitize`, `Sign Document`, `Compare PDFs`, `Extract Tables & Images`, `Convert` targets, `Organize` tools, `Print`, `About`). Done when: every command in `design/SPEC.md` sections 8.2, 8.3, 9.4, 9.5, 9.6, and 10 has a registered `CommandDescriptor`.
- [ ] Provide a XAML helper / attached property (`CommandBinding::CommandId`) that automatically sets a `Button` or `MenuFlyoutItem`'s `FontIcon` glyph, `AutomationProperties.Name`, tooltip (`"<Title> (<Shortcut>)"` or `"<Title>"`), `IsEnabled` state, and click handler from `CommandRegistry`. Done when: binding a button to a `CommandId` populates its icon, accessibility name, tooltip, and handler from one source of truth. Cheaper substitute that fails the checkpoint: maintaining separate hard-coded command lists in the toolbar, menu bar, and Command Box.
- [ ] Migrate `ShellPage` title bar buttons (`D02 T01 §2`), `ShellMenuBar` items (`D02 T01 §9`), `PageToolbar` buttons and `More` menu items (`D02 T02 §8`), and `SidePanelHost` switcher/action buttons (§3) onto `CommandRegistry`. Done when: executing any command via `CommandRegistry::execute(id)` invokes the exact same code path as clicking its toolbar or menu button.
- [ ] Re-evaluate `can_execute` predicates automatically whenever the active tab, document lock/load state, or selection state changes, updating bound UI controls' `IsEnabled` properties live. Done when: switching from `Home` to an open PDF tab enables all `ThisDocument` commands immediately.
- [ ] Add `Panels.CommandRegistryCompleteness` test in `tests/recto-tests/` enumerating all interactive controls in `ShellPage`, `ShellMenuBar`, `PageToolbar`, and `SidePanelHost` and asserting that every command is in `CommandRegistry` and every icon-only button has `AutomationProperties.Name` and a tooltip. Done when: `recto-tests.exe --gtest_filter=Panels.CommandRegistryCompleteness` passes.
- [ ] Commit: `viewer: add unified CommandRegistry binding toolbars, menus and accessibility metadata`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.CommandRegistryCompleteness` passes on the display-session host, proving 100% toolbar/menu coverage in `CommandRegistry` and complete `AutomationProperties.Name` + tooltip bindings.

## 12. Command Box (`Ctrl+K`) overlay and Home inline search experience

Build `CommandBoxControl` (`src/app/controls/CommandBoxControl.xaml/.h/.cpp`) per `design/SPEC.md` sections 9.1 and 9.4 and `design/screenshots/D-Dialogs.png` and `D-Home.png`: one shared control hosted both as the `Ctrl+K` top-centred overlay card (`560 to 720 epx` wide, `CornerRadius=20`, `RectoRaisedBrush`, `ThemeShadow` Z=64) and expanded inline (`720 epx` wide) on `HomePage` (§2). Typing filters across four grouped sections (`"This document"`, `"Go to"`, `"Files"`, and `"Actions"`) in real time, bolding matched character spans in row labels, right-aligning keyboard shortcut badges, supporting full keyboard navigation (`Up`, `Down`, `Enter`, `Esc`), and executing the chosen item through `CommandRegistry` (§11), `OpenDocumentPipeline` (`D02 T01 §5`), or `DocumentView` (`D02 T02 §6`).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 7 (`CornerRadius=20`, `ThemeShadow` Z=64), 9.1 (inline `720 epx` command box on Home with key hints and `"Browse files"` footer), 9.4 (`Command box (Ctrl+K)` overlay card: `"This document"`, `"Go to"`, `"Files"`, `"Actions"`, bolded matched characters, right-aligned shortcuts), and 10 (`Ctrl+K`, `Esc`) with `design/screenshots/D-Dialogs.png` and `D-Home.png`.
**Job:** The user can press `Ctrl+K` from anywhere in Recto, type a few letters of a command, page number, chapter title, or recent filename, and press `Enter` to run or jump to it without touching the mouse. Consumer: `ShellPage` overlay host (`D02 T01 §2`) and `HomePage` inline host (§2).
**Treatment:** Single `CommandBoxControl` reading live from `CommandRegistry` (§11), `RecentFilesStore` (§1), and the active tab's page count and `OutlinePanel` tree (§5), with `RichTextBlock` / `Run` span highlighting for matched characters. Cheaper substitute that fails the checkpoint: a command box that only searches static menu names (omitting recent files, `"Page N"` jumps, or outline headings) or omits bolding of matched query characters.
**Chrome:** consume `RectoRaisedBrush`, `RectoBorderBrush`, `RectoAccentTintBrush` for the selected result row, `RectoTextSecondaryBrush` for section headers and right-aligned shortcuts, and `ThemeShadow` Z=64 in overlay mode.

**Build order:**

1. Implement the multi-source query engine `CommandBoxFilter` (`src/app/controls/CommandBoxFilter.h/.cpp`): given a query string and `AppCommandContext`, rank and group matches into the four SPEC 9.4 sections (`"This document"` with enabled document-scoped commands from `CommandRegistry`, omitted when on `Home` with no open document; `"Go to"` with numeric page jumps where typing `"14"` or `"page 14"` offers `"Go to page 14"` plus matching outline/bookmark titles from the open document's `OutlineItem` tree §5; `"Files"` with matching recent files from `RecentFilesStore::list_entries()` §1; and `"Actions"` with app-wide commands and conversion workflows from `CommandRegistry`), computing exact character match ranges (`std::vector<TextRange>`) for bold rendering. Done when: unit tests verify section grouping, prefix/substring ranking, `"page N"` recognition, outline matching, recent-file matching, and character highlight span indices in `< 1 ms`.
2. Build `src/app/controls/CommandBoxControl.xaml/.h/.cpp` in both `OverlayMode` (toggled by `Ctrl+K` or the title bar Command Box button, `560 to 720 epx` wide at top-centre with light-dismiss backdrop and `Esc` close) and `InlineHomeMode` (`720 epx` wide on `HomePage` with footer key hints `"↑↓ to navigate · Enter to select · Esc to clear"` and `"Browse files"` accent button), rendering each result row with its `16 epx` Segoe Fluent icon, label with bolded matched character `Run`s (`BodyStrongTextBlockStyle` on matched spans, `BodyTextBlockStyle` on unmatched spans), and right-aligned shortcut caption. Done when: a keyboard-only session opens a recent file, jumps to an outline heading, jumps to a page number, rotates the view, and opens Settings entirely through `Ctrl+K`.

- [ ] Author `src/app/controls/CommandBoxFilter.h/.cpp` querying `CommandRegistry` (§11), active document pages and outline entries (§5), and `RecentFilesStore` (§1) into the four SPEC 9.4 sections (`"This document"`, `"Go to"`, `"Files"`, `"Actions"`). Done when: empty query shows top contextual defaults per section and non-empty query filters and ranks across all four sources in `< 2 ms`.
- [ ] Support natural page-number queries in `"Go to"`: typing a number `N` (e.g., `"12"`) or `"page 12"` / `"p12"` when a document with `>= 12` pages is open surfaces `"Go to page 12"` as the top `"Go to"` result. Done when: typing `"7"` and pressing `Enter` jumps `DocumentView` to page 7.
- [ ] Render matched character substrings in bold (`FontWeights::SemiBold`) inside each result row's label while keeping non-matched characters in `FontWeights::Normal` per SPEC 9.4. Done when: typing `"rot"` highlights `"**Rot**ate view right"` and `"**Rot**ate view left"` in bold. Cheaper substitute that fails the checkpoint: plain un-highlighted result labels.
- [ ] Display the `16 epx` Segoe Fluent glyph on the left and the keyboard shortcut badge (`CaptionTextBlockStyle`, `RectoTextSecondaryBrush`) on the right of every result row, highlighting the active row with `RectoAccentTintBrush`. Done when: row visuals match the `Ctrl+K` board in `D-Dialogs.png` and `D-Home.png`.
- [ ] Implement full keyboard navigation (`Down` / `Up` cycling across results and skipping section headers, `Home` / `End` inside the query box, `Enter` executing the highlighted item and closing the overlay, `Esc` clearing the query in `InlineHomeMode` or closing the card in `OverlayMode`). Done when: the entire `Ctrl+K` flow works mouse-free.
- [ ] Mount `CommandBoxControl` in `ShellPage` as the `Ctrl+K` top-centred overlay (`CornerRadius=20`, `ThemeShadow` Z=64) and on `HomePage` (§2) in `InlineHomeMode` (`720 epx` wide with footer key hints and `"Browse files"` button). Done when: pressing `Ctrl+K` on `HomePage` focuses the inline box rather than opening a duplicate popup over it, and pressing `Ctrl+K` on any other page opens the top-centred overlay card.
- [ ] Announce result counts politely to screen readers as the user types (`"5 results, Rotate view right selected"`). Done when: UIA live-region / automation peer notifications fire on filter updates.
- [ ] Add `Panels.CommandBoxFilterAndKeyboardFlow` test in `tests/recto-tests/` driving `Ctrl+K` queries for commands (`"rotate"`), outline titles, page numbers (`"page 3"`), and recent filenames, verifying bold span indices and end-to-end execution. Done when: `recto-tests.exe --gtest_filter=Panels.CommandBoxFilterAndKeyboardFlow` passes.
- [ ] Keep IME composition intact in the command box: never re-request focus or rewrite the query while `TextCompositionStarted` is active, and filter only on committed text. Done when: `Panels.CommandBoxFilterAndKeyboardFlow` composes a Microsoft Pinyin, Japanese and Korean query and executes the selected result.
- [ ] Commit: `viewer: build Ctrl+K CommandBoxControl in overlay and Home inline modes`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.CommandBoxFilterAndKeyboardFlow` passes on the display-session host, and screenshots of `CommandBoxControl` in overlay (`D-Dialogs.png`) and Home inline (`D-Home.png`) modes match the SPEC boards.

## 13. Word, character, line and page counts for the document and the selection

Foxit PDF Editor and PDFelement show word and character counts; Acrobat Pro does not. §9 shows the facts stored in the file; counts need the extracted text, so this section sits on `PageText` and runs in the background, never on the UI thread. Counts cover a whole document, a page range and the current selection, and say plainly when pages have no text (scanned and not yet OCR'd) instead of reporting zero as if it were the answer.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.2 (Info panel) and section 5 type styles, with `design/screenshots/D-ReadingRoom.png`.
**Job:** A writer or translator learns how long a document is and how long their selection is. Consumer: `DocumentInfoPanel` (§9), `SelectionModel` (`D03 T01 §3`).
**Treatment:** Background job over cached `PageText`. Cheaper substitute that fails the checkpoint: counting on the UI thread or treating a text-free page as zero words without saying so.
**Chrome:** `CaptionTextBlockStyle`, `BodyTextBlockStyle`, tokens.

**Build order:**

1. Implement `src/services/text_counts.h/.cpp`: `count_text(PageTextSnapshot, TextRange) -> TextCounts { words, characters, characters_no_spaces, lines, paragraphs }` using `Windows.Data.Text.WordsSegmenter` for word boundaries (so CJK and Thai count correctly) and `PageText` line and paragraph spans, plus `DocumentActor` job `count_document(page_range, CancelToken)` that reads `PageText` through the `get_page_text` cache (`D03 T01 §2`), reports progress, and returns per-page counts and the list of pages with no text. Done when: a 100-page corpus file counts within 1 percent of the independent reference counts in `tests/corpus/counts.csv` and cancels within 200 ms.
2. Show a `Text` group in `DocumentInfoPanel` (§9) with `Words`, `Characters`, `Characters without spaces`, `Lines`, `Paragraphs`, a progress bar while counting, a note `N pages have no text` listing the pages (with a link to the OCR command), and a `Selection` row that follows the current selection (`D03 T01 §3`) with a 250 ms debounce. Done when: selecting a paragraph updates the `Selection` row and a scanned page appears in the no-text note.

- [ ] Add `Count words and characters` to `CommandRegistry` (`D02 T03 §11`) and the `Tools` menu, opening the Info panel on the `Text` group. Done when: `Ctrl+K` finds it.
- [ ] Add the verb `recto-cli count <file> [--pages 1-5] [--json]` in `src/cli/` returning the same numbers and the no-text page list, exit code 0 on success. Done when: the CLI numbers equal the panel numbers on three corpus files.
- [ ] Mark OCR-derived counts with `from OCR` when `PageText` came from recognition, since they are estimates. Done when: an OCR'd fixture shows the label.
- [ ] Add `tests/corpus/counts.csv` with reference counts produced by an independent script and a `tests/corpus/PROVENANCE.md` row. Done when: `python scripts/build-corpus.py --check` passes.
- [ ] Add `Panels.TextCounts` and `Cli.Count` covering CJK, hyphenated line ends, empty pages, ranges, selection and cancel. Done when: `recto-tests.exe --gtest_filter=Panels.TextCounts:Cli.Count` passes. Cheaper substitute: counting by splitting on spaces, which miscounts CJK.
- [ ] Commit: `viewer: add word and character counts to Document Info and recto-cli count`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.TextCounts:Cli.Count` passes on the display-session host.

## 14. Bookmark proposals from text styles (font, size, weight)

Foxit's and UPDF's bookmark-from-styles tools let the user say that every line in 18 pt bold is a chapter. §6's `generate_outline_from_headings` guesses the hierarchy; this engine lets the user decide, by listing the styles the document actually uses and mapping chosen ones to levels. It only proposes: it returns a tree and writes nothing.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/pdf/bookmark_styles.h/.cpp`: `collect_text_styles(PageTextSnapshot) -> std::vector<TextStyleGroup>` grouping spans by normalized font name (subset prefix removed), size rounded to 0.5 pt, bold, italic and fill colour, each with occurrence count, page list and a sample string, sorted by size then count. Done when: a corpus book lists its chapter, section and body styles in size order with correct counts.
2. Implement `propose_bookmarks_from_styles(styles_to_levels, StyleOptions { min_chars, max_chars, join_lines, ignore_repeated }) -> PdfResult<std::vector<OutlineItem>>`: join consecutive lines of one style within 1.5 line heights into one title, drop titles repeated on more than 30 percent of pages (running headers), set each destination to `XYZ` at the span's top-left, and nest by level without skipping levels. Done when: mapping two styles to levels 1 and 2 produces the expected nested tree and ignores a running header.

- [ ] Bound the work: at most 50,000 spans per document considered, titles trimmed to 512 characters, depth at most 32. Done when: a synthetic 1,000,000-span document returns a bounded result with a truncation flag.
- [ ] Refuse pages with no text with a typed `NoTextOnPages` error listing them. Done when: a scanned fixture returns the error, not an empty tree.
- [ ] Add `PdfBookmarkStyles.Collect`, `PdfBookmarkStyles.Propose` and `PdfBookmarkStyles.Bounds` under ASan. Done when: `recto-tests.exe --gtest_filter=PdfBookmarkStyles.*` passes. Cheaper substitute: reusing the heading heuristic with a fixed size threshold.
- [ ] Commit: `pdf: propose bookmarks from user-mapped text styles`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfBookmarkStyles.*` passes under ASan.

## 15. Bookmark proposals from search strings and patterns

Foxit's bookmark-from-search tool turns every `Chapter 3`, `Article 12` or `Section 4.2` into a bookmark. Recto already has a background find engine (`D03 T01 §5`) with `Match case`, `Whole word` and `Regex`; this engine reuses it so there is one matcher, then turns the matches into a proposed tree. It writes nothing.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `propose_bookmarks_from_search(FindQuery, SearchOptions { level, title_source (Match, Line, Template), template, max_results }) -> PdfResult<std::vector<OutlineItem>>` over the find engine's results, taking the title from the match, the whole line, or a template such as `Chapter {1}` filled from regex groups, trimmed to 512 characters. Done when: the query `^Chapter \d+` yields one bookmark per chapter with the line as its title and a `Template` of `{0}` yields the matched text.
2. Drop duplicate matches at the same position, cap results at 5,000 with a truncation flag, honour `CancelToken`, and bound the regex through the engine's existing step limit so a hostile pattern cannot hang the job. Done when: a catastrophic-backtracking pattern ends in under 2 s with a `PatternTooComplex` error.

- [ ] Order proposals by page then vertical position and assign one level to all, leaving nesting to the user in the dialog (§17). Done when: the output order matches reading order.
- [ ] Add `PdfBookmarkSearch.Propose`, `PdfBookmarkSearch.Templates` and `PdfBookmarkSearch.HostilePattern` under ASan. Done when: `recto-tests.exe --gtest_filter=PdfBookmarkSearch.*` passes. Cheaper substitute: a second regex implementation next to the find engine.
- [ ] Commit: `pdf: propose bookmarks from search matches`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfBookmarkSearch.*` passes under ASan.

## 16. Bookmark proposals from structure tags (`H1` to `H6`, `Title`, `Sect`)

Foxit and UPDF build bookmarks from a tagged PDF's headings. A tagged document already says what is a heading, so this is the most accurate source and costs nothing in guesswork. It reads the immutable `StructNode` snapshot (`D03 T01 §2`), turns `H1` to `H6`, `H` and `Title` into levels, and refuses an untagged document with a message that points to the style and search sources.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `propose_bookmarks_from_structure(StructSnapshot, StructOptions { include_title, use_actual_text }) -> PdfResult<std::vector<OutlineItem>>`: walk `/StructTreeRoot` in reading order, map `H1` to `H6` to levels 1 to 6, treat a bare `H` as one level below its nearest heading ancestor, take titles from `/ActualText` or the element's text, and resolve each destination from the element's `/Pg` and marked-content bounding box. Done when: a tagged corpus book yields the same tree as its existing outline within the title text.
2. Return a typed `NoStructure` error for a document without a structure tree and `NoHeadings` when it has one with no heading elements; bound depth to 32 and node visits to 200,000. Done when: an untagged file returns `NoStructure` and a cyclic `/K` fixture ends at the bound.

- [ ] Skip elements marked `/Artifact` and headings with empty text. Done when: an artifact heading is not proposed.
- [ ] Add `PdfBookmarkStructure.Propose`, `PdfBookmarkStructure.NoStructure` and `PdfBookmarkStructure.Bounds` under ASan. Done when: `recto-tests.exe --gtest_filter=PdfBookmarkStructure.*` passes. Cheaper substitute: guessing headings by font size on a tagged document.
- [ ] Commit: `pdf: propose bookmarks from structure tags`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfBookmarkStructure.*` passes under ASan.

## 17. `Create bookmarks` dialog: choose a source, preview, apply with undo

One place to use §14, §15 and §16: the user picks `Text styles`, `Search` or `Structure tags`, sees the proposed tree with checkboxes, and applies it. Existing bookmarks are never replaced silently: the default is to append, and `Replace existing` says how many bookmarks it will remove. Applying goes through the Bookmark Editor's undoable command stack (§6), and nothing reaches the file until Save.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 9.9 form layout and section 8.3 `Outline`.
**Job:** A user turns a long untagged report into a navigable outline in a minute, and can back out in one keystroke. Consumer: `OutlinePanel` (§5), bookmark undo stack (§6).
**Treatment:** Preview then one undoable composite command. Cheaper substitute that fails the checkpoint: writing bookmarks immediately, or replacing the existing outline without a count.
**Chrome:** `RectoDialogStyles.xaml` (`D02 T01 §10`), `RectoAccentTintBrush`, stock `TreeView`.

**Build order:**

1. Build `CreateBookmarksDialog` (`src/app/dialogs/CreateBookmarksDialog.xaml/.h/.cpp`) with a source selector, a style table for `Text styles` (sample text, size, count, a level `ComboBox` of `Ignore` and 1 to 6), a query box with the §5 find options for `Search`, a `Structure` option showing the engine's refusal reason when unavailable, a `TreeView` preview with per-node checkboxes, and `Append` or `Replace existing (N bookmarks)`. Done when: choosing styles shows the preview tree live within 500 ms on a 300-page file, and an untagged file shows why `Structure` is unavailable.
2. On `Apply`, push one composite command onto the bookmark undo stack (§6) so a single `Ctrl+Z` removes everything added, mark the tab dirty, and select the first new bookmark in `OutlinePanel`. Done when: apply then `Ctrl+Z` restores the previous outline exactly, and save writes standard `/Outlines`.

- [ ] Run the proposal engines on the actor thread with a cancel button and a progress ring. Done when: cancelling a long search leaves the outline unchanged.
- [ ] Add `Create bookmarks...` to the `OutlinePanel` header menu (§5), the `Edit` menu (`D02 T01 §9`) and `CommandRegistry`. Done when: `Ctrl+K` finds it.
- [ ] Require at least one checked proposal and disable `Apply` otherwise, with a text reason. Done when: unchecking everything disables `Apply` and says why.
- [ ] Add `Panels.CreateBookmarksDialog` covering the three sources, append and replace, undo as one step, cancel and refusal text. Done when: `recto-tests.exe --gtest_filter=Panels.CreateBookmarksDialog` passes. Cheaper substitute: applying proposals directly to the file without a preview.
- [ ] Commit: `viewer: add the Create bookmarks dialog with preview and undo`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.CreateBookmarksDialog` passes on the display-session host.

## 18. Bookmark import and export: JSON and plain-text outline, plus `recto-cli bookmarks`

Foxit and UPDF import and export bookmarks so an outline built for one file can be reused or edited in a text editor. This section defines the two formats and the engine that reads and writes them, and a `recto-cli bookmarks` verb. Import is hostile input: titles, depth, counts and page numbers are validated, and the CLI writes a new file with `-o`, never in place.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/pdf/bookmark_io.h/.cpp`: `export_bookmarks(OutlineTree, Format) -> std::string` for JSON (`{"version":1,"bookmarks":[{"title","page","x","y","zoom","color","bold","italic","open","children"}]}` with 1-based pages) and for a text outline (one bookmark per line: leading tab characters give the depth, then the title, then a tab and the 1-based page number; the title escapes `\`, tab and newline as `\\`, `\t` and `\n`; the format intentionally carries only title, depth and page, so destination coordinates, zoom, colour, style and open state are dropped and import uses a `Fit` destination at the page top and closed items), and `parse_bookmarks(std::string_view, Format, page_count) -> PdfResult<OutlineTree>`. Done when: export then parse reproduces the full tree for JSON, and reproduces titles, depth and pages for the text format, on three corpus outlines including non-default colour, style, zoom and open state, and titles containing tabs, newlines and backslashes.
2. Validate on parse: UTF-8 titles up to 512 characters, depth at most 32, at most 100,000 nodes, pages within 1 to `page_count`, unknown JSON fields ignored, unknown version refused, and report the 1-based line or JSON path of the first fault. Done when: a file with page 9999, a 40-deep tree and a 200,000-node tree each fail with a typed error naming the fault.

- [ ] Add `recto-cli bookmarks export <in.pdf> --format json|txt [-o file]` and `recto-cli bookmarks import <in.pdf> <bookmarks> --mode replace|append -o <out.pdf>` (via the §6 outline writer and the incremental writer, `D01 T02 §5`), exit code 2 on validation failure, and refuse `-o` equal to the input. Done when: import writes a new file whose outline equals the source and leaves the input byte-identical.
- [ ] Handle a UTF-8 byte order mark, `\r\n` line ends and tab or four-space indents in the text format. Done when: all three variants parse to the same tree.
- [ ] Add `tests/corpus/bookmarks/` fixtures (valid, deep, huge, bad page, bad UTF-8) and register `fuzz_bookmarks` in `scripts/run-fuzz-campaign.ps1`. Done when: a 60 s `fuzz_bookmarks` run is clean.
- [ ] Add `PdfBookmarkIo.*` and `Cli.Bookmarks` under ASan. Done when: `recto-tests.exe --gtest_filter=PdfBookmarkIo.*:Cli.Bookmarks` passes. Cheaper substitute: an ad-hoc parser that trusts page numbers.
- [ ] Commit: `pdf: import and export bookmarks as JSON and text with recto-cli bookmarks`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfBookmarkIo.*:Cli.Bookmarks` passes under ASan and the 60 s `fuzz_bookmarks` run is clean.

## 19. Import and export bookmarks from the Outline panel

The UI half of §18: `Export bookmarks...` and `Import bookmarks...` in the `OutlinePanel` header menu (§5), using the file pickers and the same append or replace choice as §17. Import goes through the undoable bookmark stack so a wrong file costs one `Ctrl+Z`.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.3 (`Outline` header actions) and 9.9 form dialog layout.
**Job:** A user moves an outline between files or edits it in a text editor. Consumer: `OutlinePanel` (§5), `bookmark_io` (§18).
**Treatment:** Stock pickers and the existing undo stack. Cheaper substitute that fails the checkpoint: a hidden developer-only command.
**Chrome:** `RectoDialogStyles.xaml`, Segoe Fluent Icons.

**Build order:**

1. Add `Export bookmarks...` (a `FileSavePicker` offering `.json` and `.txt`, writing through temp-file-then-replace) and `Import bookmarks...` (a `FileOpenPicker`, a `ContentDialog` offering `Append` or `Replace existing (N bookmarks)` and showing the validation error text on failure) to the `OutlinePanel` header menu. Done when: exporting then importing with `Replace` on a fresh copy of the document yields an identical outline.
2. Apply an import as one composite undo command (§6) and mark the tab dirty. Done when: `Ctrl+Z` after import restores the previous outline.

- [ ] Register `Export bookmarks` and `Import bookmarks` in `CommandRegistry` and the `Edit` menu. Done when: both are reachable from `Ctrl+K`.
- [ ] Show the parser's line or path in the error dialog and keep the document unchanged. Done when: a bad file shows its fault location and changes nothing.
- [ ] Add `Panels.BookmarkImportExport` covering both formats, append, replace, undo and error display. Done when: `recto-tests.exe --gtest_filter=Panels.BookmarkImportExport` passes. Cheaper substitute: importing without a validation step.
- [ ] Commit: `viewer: import and export bookmarks from the Outline panel`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.BookmarkImportExport` passes on the display-session host.

## 20. Per-bookmark style: colour, bold and italic (`/C` and `/F`)

Foxit and UPDF let each bookmark have its own colour and bold or italic text. PDF stores this in the outline item's `/C` (RGB array) and `/F` (flags: italic, bold; ISO 32000-1 outline item dictionary), so other readers show it too. §6 writes titles and destinations only; this section adds the two keys, an editor control, and tree rendering that stays readable.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 8.3 `Outline (with Bookmark Editor)` and section 11 (colour is never the only cue).
**Job:** A user highlights important bookmarks in a way every PDF reader shows. Consumer: `OutlinePanel` (§5, §6).
**Treatment:** Standard `/C` and `/F` keys. Cheaper substitute that fails the checkpoint: styles kept outside the PDF.
**Chrome:** `RectoTextBrush`, `RectoSurfaceBrush`, stock `ColorPicker`.

**Build order:**

1. Extend `src/pdf/outlines_edit.cpp` and `OutlineItem` with `color` (three floats 0 to 1, omitted when black) and `bold` and `italic` flags written as `/C` and `/F`, read back on open, preserved on items the user did not edit, and covered by the §18 JSON format. Done when: set, save and reopen reproduces colour and flags, and a file with unknown `/F` bits keeps them.
2. Add `Bookmark style` to the `OutlinePanel` editor (§6): a colour swatch flyout with 8 preset swatches and a custom `ColorPicker`, and `Bold` and `Italic` toggle buttons, applying to the selection and undoable. Done when: styling two selected bookmarks then `Ctrl+Z` removes both styles in one step.

- [ ] Render a styled row with its colour and weight, but fall back to `RectoTextBrush` when the colour has less than 4.5:1 contrast against the panel surface (the label is regular-size body text, so the 3:1 large-text allowance does not apply) and show a small swatch dot so the style is still visible. Done when: a near-white bookmark and a mid-tone bookmark at about 3.5:1 in the Light theme both show in text colour with their swatch, and a 7:1 colour keeps its own colour.
- [ ] Expose the style in the automation name (`Chapter 1, bold, red`). Done when: Narrator reads the style.
- [ ] Add `PdfOutlineStyle.RoundTrip` and `Panels.BookmarkStyle` covering flags, colour, undo and contrast fallback. Done when: `recto-tests.exe --gtest_filter=PdfOutlineStyle.*:Panels.BookmarkStyle` passes. Cheaper substitute: storing styles in an app-local file.
- [ ] Commit: `viewer: add per-bookmark colour, bold and italic`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfOutlineStyle.*:Panels.BookmarkStyle` passes on the display-session host.

## 21. `recto-pdf` contents page generated from bookmarks

Foxit and UPDF can insert a table-of-contents page built from the bookmarks. The engine adds one or more new pages with title lines, dot leaders and right-aligned page numbers, each line a `/Link` to its destination, on a copy. Inserting pages shifts later page numbers, so the numbers shown and every `/Dest` in the outline and links are computed after the shift. Text uses the Edit font resolver so titles outside WinAnsi never become `?`.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/pdf/toc_page.h/.cpp`: `insert_contents_page(Document&, OutlineTree, TocOptions { position (Start or AfterPage n), heading, max_depth, font, size, dot_leaders, add_bookmark }) -> PdfResult<TocResult>` laying out lines by the first page's size, flowing to extra pages, using `D03 T04 §4` glyph coverage, writing `/Link` annotations with `/Dest` arrays, and returning the inserted page range. Done when: a 40-bookmark outline yields a contents page whose every line links to the right page after the shift.
2. Insert through the copy-based page-tree edit list (`D05 T01 §3`, `§4`) so existing outline, link and named-destination targets are remapped, and optionally add a `Contents` bookmark at the new page. Done when: after insertion every original bookmark still lands on the same content page.

- [ ] Bound the work to 5,000 entries and 100 contents pages and refuse an empty outline with a typed error. Done when: a 50,000-entry outline returns a bounded result with a truncation flag.
- [ ] Write only to a copy; the open document is never modified in place. Done when: the source file hash is unchanged.
- [ ] Add `PdfTocPage.Layout`, `PdfTocPage.RemapsTargets` and `PdfTocPage.NoQuestionMarks` (CJK and accented titles) under ASan. Done when: `recto-tests.exe --gtest_filter=PdfTocPage.*` passes. Cheaper substitute: a text-only page of numbers without links.
- [ ] Commit: `pdf: generate a contents page from bookmarks`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfTocPage.*` passes under ASan.

## 22. Insert contents page dialog and `recto-cli toc`

The user-facing half of §21: a dialog with a live preview, reachable from the `OutlinePanel` header menu and `Ctrl+K`, and a `recto-cli toc` verb. The result is saved as a copy, never over the open document.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 9.9 form layout.
**Job:** A user adds a clickable contents page to a long document and saves it as a copy. Consumer: `toc_page` (§21), `SavePipeline` (`D02 T01 §8`).
**Treatment:** Dialog over the engine with a scratch-copy preview. Cheaper substitute that fails the checkpoint: previewing by modifying the open document.
**Chrome:** `RectoDialogStyles.xaml`.

**Build order:**

1. Build `InsertContentsDialog` (`src/app/dialogs/InsertContentsDialog.xaml/.h/.cpp`) with position, heading text, depth, font and size, dot leaders, `Add Contents bookmark`, a live first-page preview rendered from §21 on a scratch copy, and `Create copy` using `SavePipeline::save_copy_async` (`D02 T01 §8`). Done when: the preview updates within 1 s of a change and `Create copy` writes a valid PDF with the contents page.
2. Add `recto-cli toc <in.pdf> -o <out.pdf> [--position start|after N] [--depth N] [--heading text]`, refusing `-o` equal to the input, exit code 2 on an empty outline. Done when: the CLI output equals the dialog output for the same options.

- [ ] Register `Insert contents page` in `CommandRegistry` and the `Tools` menu. Done when: `Ctrl+K` finds it.
- [ ] Add `Panels.InsertContentsDialog` and `Cli.Toc` covering preview, copy save, options and refusals. Done when: `recto-tests.exe --gtest_filter=Panels.InsertContentsDialog:Cli.Toc` passes. Cheaper substitute: editing the open document in place.
- [ ] Commit: `viewer: add the Insert contents page dialog and recto-cli toc`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.InsertContentsDialog:Cli.Toc` passes on the display-session host.

## 23. `recto-pdf` layer authoring: create, rename, delete, properties and merge

Foxit and PDFelement edit layers and Acrobat Pro does too; §7 only views and flattens them. This engine edits the `/OCProperties` structure: create an empty layer, rename it, delete it, set its initial state and `/Usage` (view, print, export), reorder in `/Order`, and merge several layers into one by repointing references (page `/Properties` entries, XObject and annotation `/OC` entries, and `/OCMD` member lists) so no content stream is rewritten. Flatten stays in §7 (`flatten_visible_layers`). Edits are staged in the actor and saved through the normal save paths.

**Phase:** 2
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Extend `src/pdf/ocg.cpp` with `build_create_layer(const Document& working, name, initial_state)`, `build_rename_layer`, `build_delete_layer(mode: RemoveMembership | KeepContent)`, `build_set_layer_usage(print, view, export)`, and `build_reorder_layers`, all returning `PdfResult<recto::pdf::OcgBuildResult>` (the change set plus the reference of any layer the operation created; `build_create_layer` returns the new layer's reference) and never mutating `working`, each wrapped as an `OcgEdit` of the matching `OcgEditKind` (`D01 T02 §14`) and keeping `/OCGs`, `/D /Order`, `/ON`, `/OFF`, `/RBGroups` and `/Locked` consistent; `delete_layer` with `KeepContent` makes the content permanent by dropping only the `/OC` association. Done when: after each operation the written file reopens with the expected layer list and a pixel check of a fixture matches the expected visibility.
2. Implement `build_merge_layers(const Document& working, sources, target) -> PdfResult<recto::pdf::OcgBuildResult>` by repointing every reference to a source `OCG` to the target `OCG` and removing the sources, but only when the result keeps every membership meaning: reject with `MergeChangesMembership` any source or target that appears in an `/OCMD` with a `/VE` visibility expression or a `/P` policy of `AllOff`, `AnyOff` or `AllOn` together with another member that is also being merged (for example `A AND NOT B`), and any merge that includes a `/Locked` layer or members of one `/RBGroups` set. Done when: merging two layers of a fixture leaves one layer showing the union of their content and no dangling `OCG` reference, and fixtures with an `/OCMD` `/VE` `A AND NOT B`, a nested `/VE`, and `AnyOff` over two merged members are each refused with `MergeChangesMembership` and unchanged.

- [ ] Register each builder as an `OcgEditKind` for the actor edit session of `D01 T02 §14` (`stage_ocg_edit`, undo, redo), which marks the document dirty and publishes a new `ocg_generation`; this section adds no actor code. Done when: `PdfOcgEdit.StagesThroughActor` stages each real builder, then undoes it, and the exported revision equals the opened bytes.
- [ ] Bound work: at most 10,000 layers and 200 merge sources; names are UTF-16BE or PDFDocEncoding text strings of at most 512 characters. Done when: an oversize request returns a typed error.
- [ ] Add `PdfOcgEdit.CreateRenameDelete`, `PdfOcgEdit.Usage`, `PdfOcgEdit.Merge` and `PdfOcgEdit.LockedAndRadioGroupsRefused` under ASan. Done when: `recto-tests.exe --gtest_filter=PdfOcgEdit.*` passes. Cheaper substitute: rewriting content streams to merge layers.
- [ ] Commit: `pdf: add layer create, rename, delete, usage and merge`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfOcgEdit.*` passes under ASan.

## 24. `recto-pdf` import a page or an image as a layer

Foxit's and PDFelement's `Import as layer` places an image or another PDF's page over the current page as a new layer, which is how people add a translation overlay or an annotated scan. The source page becomes a Form XObject (grafted with `GraftMap`, `D05 T01 §1`, so fonts and images are deduplicated) drawn inside `BDC /OC` on the target page, with position, scale and opacity. The source file is only read.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `build_import_page_as_layer(const Document& working, int page, SourceSpec { path, password }, int source_page, LayerPlacement { origin, scale, rotation, opacity, layer_name }) -> PdfResult<recto::pdf::OcgBuildResult>`, the builder shape `D01 T02 §14` runs on the actor thread, handed only the working document: it captures the source path and password by value and opens the source `Document` inside the builder (it may keep a `LayerImport { changes, ocg }` internally and converts it to `recto::pdf::OcgBuildResult`): it uses `GraftMap` to copy the source page into a Form XObject, allocates every new object through `ObjectChangeSet::allocate`, and replaces the target page's content and resources and the catalog `/OCProperties` in the change set, never mutating `working`; the new layer's reference is returned in the `OcgBuildResult`. Done when: a two-page fixture overlaid on page 1 shows both when visible and only the original when the layer is hidden, and `Actor.OcgEditImportPage` stages this real builder (not a test-only builder) through `DocumentActor` and renders the overlay.
2. Implement `build_import_image_as_layer` with the same builder shape (image path captured by value, decoded inside the builder) for JPEG, PNG and other WIC-decodable images by creating an image XObject (DCT passthrough for JPEG, Flate for the rest) with the same placement fields. Done when: a PNG with transparency imports with its alpha as an `/SMask`.

- [ ] Bound inputs: source page object graph limits from `GraftMap`, image at most 16,384 by 16,384 pixels and 256 MB decoded, reject encrypted sources without a password with a typed error. Done when: oversize and encrypted sources fail cleanly.
- [ ] Write to a copy through the edit session (staged, undoable) and never modify the source. Done when: source file hash unchanged.
- [ ] Add `PdfOcgImport.Page`, `PdfOcgImport.Image` and `PdfOcgImport.Bounds` under ASan. Done when: `recto-tests.exe --gtest_filter=PdfOcgImport.*` passes. Cheaper substitute: flattening the source into the page content with no layer.
- [ ] Commit: `pdf: import a page or image as a new layer`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfOcgImport.*` passes under ASan.

## 25. Layers panel editing: new, rename, delete, merge and import as layer

The UI half of §23 and §24 inside `LayersPanel` (§7): `New layer`, `Rename` (`F2`), `Delete`, `Merge selected layers`, `Layer properties` (initial state, print, view, export), `Import page as layer...` and `Import image as layer...`. Every edit is undoable and shows its effect at once; `Flatten Visible Layers` stays where §7 put it.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 8.3 `Layers (OCG)` with `design/screenshots/D-Focus-Light.png`; the dialogs follow section 9.9 form layout.
**Job:** A user builds and tidies layers, including adding an overlay, without leaving the panel. Consumer: `LayersPanel` (§7), `ocg_edit` (§23, §24).
**Treatment:** Panel commands over the staged-edit engine. Cheaper substitute that fails the checkpoint: edit support only through the command box.
**Chrome:** `RectoDialogStyles.xaml`, `SidePanelHost` (§3), Segoe Fluent Icons.

**Build order:**

1. Add a `Layer tools` header menu and an item context menu to `LayersPanel` for the seven commands, with multi-select for merge, a `Delete layer` dialog offering `Remove the layer and its content association (keep content visible)` or `Delete the layer`, and a placement dialog for the imports (position, scale, opacity, preview). Done when: creating a layer, importing a page into it, merging it with another and undoing each step returns the panel and canvas to the previous state.
2. Drive the canvas through `set_ocg_visibility` and the `ocg_generation` invalidation of §7 so a new or merged layer renders at once, show the unsaved state only from the edit session's reported state (the panel keeps no dirty flag of its own), and select a newly created layer by the reference the stage reply returns. Done when: a new layer appears in the tree and is selected by that reference, an imported overlay renders without reopening the file, and the unsaved marker follows undo and redo.

- [ ] Disable the tools for `/Locked` layers, radio-group members where merge is refused, and encrypted documents without edit permission, each with a reason in the tooltip. Done when: each disabled state names its reason.
- [ ] Register the seven commands in `CommandRegistry` (`D02 T03 §11`). Done when: all are found by `Ctrl+K`.
- [ ] Keep names and states in the automation tree and announce edits politely. Done when: Narrator reads `Layer Overlay created`.
- [ ] Add `Panels.LayersEditing` covering each command, undo, disabled reasons and canvas refresh. Done when: `recto-tests.exe --gtest_filter=Panels.LayersEditing` passes. Cheaper substitute: a read-only panel with edit commands only on `Ctrl+K`.
- [ ] Commit: `viewer: edit layers in the Layers panel`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.LayersEditing` passes on the display-session host.

## Verification

- Every `design/SPEC.md` section 8.3, 9.1, 9.2 (Info, XMP & Viewer Preferences) and 9.4 element is working and proven on rendered surfaces in the review findings.
- `RecentFilesStore` survives restarts, prunes deleted files, and recovers from corrupt JSON; `ThumbnailList`, `OutlinePanel`, `LayersPanel`, `AttachmentsPanel`, `DocumentInfoPanel`, and `CommandBoxControl` pass their `Panels.*`, `PdfOcg.*`, `PdfPortfolio.*`, `PdfOutlinesEdit.*`, and `PdfMetadataEdit.*` test suites under ASan.
- Full `recto-tests` suite green; `python scripts/todo-graph.py validate` passes.
- The panel features of §13 to §25 are proven by `Panels.TextCounts`, `Cli.Count`, `PdfBookmark*.*`, `Panels.CreateBookmarksDialog`, `PdfBookmarkIo.*`, `Cli.Bookmarks`, `Panels.BookmarkImportExport`, `PdfOutlineStyle.*`, `PdfTocPage.*`, `Cli.Toc`, `PdfOcgEdit.*`, `PdfOcgImport.*` and `Panels.LayersEditing`.
