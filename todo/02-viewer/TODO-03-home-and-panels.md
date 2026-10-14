---
schema_version: 1
id: home-and-panels
domain: 02-viewer
status: active
title: "TODO-03 -- Home, Panels, Document Info and the Command Box"
track: M2
---

# TODO-03 -- Home, Panels, Document Info and the Command Box

> **Goal:** The surfaces around the page: Home with recents and remembered reading position, the Pages and Outline side panels (with interactive Bookmark editing), the Layers (OCG) and Attachments & Portfolio side panels, the document Info, XMP and Viewer Preferences panel, and the Ctrl+K command box that reaches every command, each matching its SPEC board and reusing the shell slots and tile path instead of inventing parallel machinery.

> **Current state:** Measured 2026-10-14. No code exists. D02 T01 provides the shell and open paths; D02 T02 provides the view, current-page rule and thumbnail-capable tile path. The designs are fixed: SPEC 9.1 (Home), 8.3 (floating and docked side panels: Pages, Outline with Bookmark Editor, Layers, Attachments & Portfolio), 9.2 (Info, XMP and Viewer Preferences panel), 9.4 (command box), with boards `D-Home.png`, `D-Focus-Light.png`, `D-ReadingRoom.png` (inspector) and `D-Dialogs.png` (command box). Recent-file and reading-position storage shape is this file's to define, read later by no one else.

## Inputs

- `design/SPEC.md` sections 8.3, 9.1, 9.2 (Info, XMP & Viewer Preferences list and editor), 9.4, 10 (Ctrl+K, Ctrl+B, Ctrl+G), 11; `resources/svg/drop-files-*.svg`, `resources/svg/ribbon-marker*.svg`, `resources/svg/filetype-pdf*.svg` (recent covers fall back to rendered first pages).
- -> XREF: D03 T01 §3 -- find lives in this file's panel family (top-right card in Focus and Classic, inspector tab in Reading room per SPEC 9.3); the panel hosting contract is agreed in §2 here and filled there.
- -> XREF: D03 T03 §2 -- the Comments (Annotations) side panel plugs into the side-panel switcher built in §2 and §5 here.
- -> XREF: D02 T04 §1 -- presets relocate these panels between slots and switch the thumbnail list orientation; the controls built here must survive reparenting unchanged.

## Outcome

- Home opens anything fast: inline command box, six recents with covers and last-opened day, correct empty state, and reading position restored per file.
- Pages, Outline (with interactive Bookmark CRUD and heading auto-generation), Layers (OCG visibility toggles), and Attachments & Portfolio (`/EmbeddedFiles` and `/Collection` inspect/extract/add/remove) panels track and drive the document in both floating and docked slots.
- Info, XMP & Viewer Preferences panel displays and edits `/Info`, XMP metadata, and `/ViewerPreferences` (`DisplayDocTitle`, `FitWindow`, `CenterWindow`, `HideToolbar`, `PageMode`, `PageLayout`).
- Every command in the app is reachable and runnable from Ctrl+K, with documents, pages and outline entries as first-class results.

**Adjacency:** list=applicable @ D02 T03 §1; document=applicable @ D02 T03 §5; settings=applicable @ D02 T04 §2; reporting=not-applicable (no reports); notifications=not-applicable (none originate here); permissions=not-applicable (no roles; a missing recent file is a state, handled in section 1, not a refusal); audit=not-applicable (recents are user state with a clear-history control in Settings, not an audit trail); exchange=applicable @ D02 T03 §5; reverse=applicable @ D02 T03 §1

**Adjacency rationale:** The list surface is Recents itself, and its reverse is removing a recent and forgetting a reading position, both owned in section 1 so the record can shrink as deliberately as it grows. Section 5 owns attachment/portfolio file extraction and insertion (`exchange`) and saving updated bookmarks, portfolios, or metadata/viewer-preferences to a copy (`document`). Settings appears because "remember reading position" is a SPEC 9.7 toggle whose consumer is section 1; the value lives in D02 T04 §2 and is read here. Search-shaped listing (find results) belongs to D03 and is not claimed here.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Home, recents and reading position | D02 T01 §3 | [ ] |
| 2 | §2 | Pages and Outline panels | D02 T02 §3 | [ ] |
| 3 | §3 | Document Info panel | §2 | [ ] |
| 4 | §4 | Command box (Ctrl+K) | §1, §2 | [ ] |
| 5 | §5 | Interactive Bookmark Editor, Layers (OCG), Attachments & Portfolios, and Editable Info/XMP/ViewerPreferences | §2, §3, D01 T02 §4 | [ ] |

## 1. Home, recents and reading position

SPEC 9.1: the question, the expanded command box (720 epx) filtering actions and files together, Recent as six covers with name and last-opened day plus See all, drop anywhere, and the empty state hiding Recent. Reading position (page, and view state worth restoring) is remembered per file when the Reading setting allows (D02 T04 §2 owns the toggle; default on per SPEC 9.7) in app-local storage that survives restarts and tolerates moved or deleted files.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.1 with `design/screenshots/D-Home.png`, including the empty state text.
**Job:** The user can reopen what they were reading, where they stopped, or open something new, in seconds. Consumer: the shell's open pipeline (D02 T01 §3) executes what Home chooses; the command box (§4) shares the filtering model.
**Treatment:** Covers rendered through the D01 tile path at a thumbnail bucket and cached on disk with the recents record. Cheaper substitute that fails the checkpoint: a plain file-name list, or shell thumbnail extraction that differs per machine.
**Chrome:** consume `drop-files-*.svg` for the empty state and the shared command-box control from §4 (inline expanded instance). Do not invent a second search box for Home.

**Build order:**

1. Recents store: record per file (path, last opened, last page, zoom and layout worth restoring), most-recent-first, capped, pruned on missing files at read time, written atomically (temp-then-replace) in app local data. Done when: reopening a file after an app restart lands on its last page, and deleting the file removes the entry on next Home display without an error.
2. Home page per the board: covers from rendered first pages (fallback to the file-type graphic while rendering), See all expanding the list, remove-from-recents per item (the reverse), empty state. Done when: side-by-side with `D-Home.png` the layout, type ramp and spacing match, and keyboard flow from the inline box to a recent works end to end.

- [ ] Recents and reading-position store with pruning and atomic writes. Done when: a corrupted store file (planted) degrades to an empty Home, never a crash, with the parse failure logged. Cheaper substitute that fails the checkpoint: one JSON blob rewritten in place on every page turn.
- [ ] Home surface complete: inline box, six recents, See all, remove item, empty state. Done when: the surface-completeness account lists every SPEC 9.1 element working.
- [ ] Commit: `viewer: Home with recents and remembered reading position`

**Test checkpoint:** driven session in findings: open two corpus files to known pages, restart the app, Home matches `D-Home.png` and both reopen at their pages; the planted corrupt-store case degrades cleanly.

## 2. Pages and Outline panels

SPEC 8.3: one side-panel card switching between Pages (thumbnails with page numbers, current page ringed in accent with the ribbon marker) and Outline (`TreeView`, current section tinted with a 3 epx accent bar), floating in Focus and docked in the other presets, toggled by Ctrl+B and the toolbar. Thumbnails render through the scheduler at a thumbnail bucket (D02 T02 §2's path), virtualized so a 500-page document scrolls its panel without rendering 500 pages; `ThumbnailList` switches `ItemsRepeater` orientation for the Reading room filmstrip (D02 T04 §1 drives the switch).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 8.3 and 9.2 with `D-Focus-Light.png` (floating panel) and `D-Classic.png` (docked pane), `D-ReadingRoom.png` (filmstrip context).
**Job:** The user can see where they are in the document and jump anywhere in one click or keypress. Consumer: the view's current-page rule (D02 T02 §3) both feeds and is driven by these panels.
**Treatment:** One panel control, two contents, virtualized thumbnails on the shared tile path. Cheaper substitute that fails the checkpoint: eagerly rendering every thumbnail, or a page-number list without images.
**Chrome:** consume the ribbon marker graphic, accent tint selection and the slot hosts; the panel is reparented by presets, never rebuilt. Do not invent a second thumbnail pipeline.

**Build order:**

1. `ThumbnailList` with virtualized realization, scheduler requests at the thumbnail bucket by visible range, current-page ring and ribbon marker, click and keyboard navigation scrolling the view. Done when: a 500-page document's panel scrolls fluidly with bounded thumbnail memory (telemetry quoted) and the current page stays marked while reading.
2. `OutlineTree` from the `recto-pdf` outline (D01 T01 §4): hierarchy, expand state, current-section tracking from the view's current page, activation scrolling the view. Done when: the outlined corpus file tracks correctly in both directions, and a document with no outline shows the panel's honest empty state.
3. Panel host behavior: switcher between Pages and Outline, close, Ctrl+B, narrow-window auto-close under 1100 epx on page click (SPEC section 7). Done when: both floating and docked slot placements behave identically (verified by moving slots under D02 T04 §1's test hook or manually here).

- [ ] Thumbnail list reusing the tile path at a thumbnail bucket. Done when: rotation changes thumbnails without stale images (keys carry rotation, D01 T02 §2). Cheaper substitute that fails the checkpoint: downscaling full-page tiles already in the texture cache as the only source.
- [ ] Outline with two-way current-section sync. Done when: keyboard-only navigation can move through a document by outline alone.
- [ ] Commit: `viewer: Pages and Outline side panels`

**Test checkpoint:** `recto-tests --gtest_filter=Panels.*` green, screenshots against the three boards in findings, 500-page panel scroll telemetry quoted, and outline two-way sync driven by keyboard only.

## 3. Document Info panel

SPEC 9.2's Info list: title, author, pages, page size, PDF version, file size, created date, producer, security and fonts, shown as the Reading room inspector content and as Properties from the toolbar's More menu elsewhere (D02 T02 §3 named that deferral). Values come from the `recto-pdf` description API (D01 T01 §4: metadata, encryption summary and the font list), formatted for reading (page size in mm and inches, dates localized), with copyable text.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.2 with `design/screenshots/D-ReadingRoom.png` (inspector Info tab).
**Job:** The user can learn what a document is (origin, size, protection, fonts) without leaving Recto. Consumer: nobody downstream computes from it; it is a read surface whose accuracy is the product.
**Treatment:** One Info content control hosted in inspector or dialog by preset. Cheaper substitute that fails the checkpoint: a raw metadata dump with unformatted PDF dictionary strings.
**Chrome:** consume the panel header pattern from §2 and Caption/Body typography per SPEC section 5. Do not invent a second property-grid visual language.

**Build order:**

1. Wire `DocumentActor` description queries from `recto-pdf` (D01 T01 §4): encryption summary (method, permissions as readable flags) and per-document font list (name, type, embedded or not). Done when: the encrypted corpus file reports AES and its permissions accurately, and the font list matches `recto-cli info` for the corpus set.
2. Build the Info content with formatted values and text selection or copy, hosted in the inspector slot and in a Properties dialog from More. Done when: both hosts show identical values for the same document, side by side with the board.

- [ ] Security and fonts through `DocumentActor` in Recto types. Done when: a permissions-restricted corpus file (added via D05 T01 §3 when it lands; until then generated per corpus rules) reads correctly. Cheaper substitute that fails the checkpoint: "Encrypted: yes" with no method or permissions.
- [ ] Info content in both hosts. Done when: screen-reader names read label and value pairs in order.
- [ ] Commit: `viewer: document Info panel with security and fonts`

**Test checkpoint:** screenshot against `D-ReadingRoom.png` in findings, with value accuracy spot-checked against `recto-cli info` output for three corpus files (quoted).

## 4. Command box (Ctrl+K)

SPEC 9.4: a 560 to 720 epx top-centred card with sections This document, Go to (pages and outline entries), Files (recents) and Actions (conversions and app commands); typed characters bolded in labels, shortcuts right-aligned, Esc closes, arrows and Enter drive it, and every toolbar and menu command is reachable here. The same control renders expanded inline on Home (§1). The command registry it reads is the app's single command table: toolbar, menus and keyboard accelerators bind to the same entries, which is what makes "every command reachable" checkable.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.1 and 9.4 with `D-Dialogs.png` (command box) and `D-Home.png` (inline form).
**Job:** The user can do anything Recto does by typing a few characters, mouse-free. Consumer: the executed command goes through the same handlers as toolbars and menus.
**Treatment:** One command registry plus one filterable box control in two hosts. Cheaper substitute that fails the checkpoint: a search box that only finds files, or a hard-coded list that drifts from the menus.
**Chrome:** consume the registry everywhere (toolbar, menus, accelerators bind to it); results use SPEC list-row styling and accent tint. Do not invent command definitions inside views.

**Build order:**

1. Command registry: id, label, icon glyph, shortcut, availability rule (needs document, needs selection and the like), handler; migrate the shell and toolbar commands built so far onto it. Done when: the registry enumerates every command the shell and page toolbar expose, asserted by a test comparing registry ids to the toolbar's command set.
2. The box: filtering across commands, recents, pages (by number and outline title) with section headers, keyboard model per SPEC 10 and the Home inline mode. Done when: typing "rotate", a file name fragment, and an outline title each surface the right first result in driven tests, and Enter executes through the same handler as the toolbar.

- [ ] Single command registry consumed by toolbar, menus, accelerators and the box. Done when: adding a command in one place appears in all four. Cheaper substitute that fails the checkpoint: per-surface command copies.
- [ ] Command box in overlay and Home-inline hosts. Done when: the full SPEC 10 keyboard flow works without the mouse.
- [ ] Commit: `viewer: Ctrl+K command box over a single command registry`

**Test checkpoint:** driven keyboard-only session in findings (open file, go to outline entry, run rotate, open Settings, all from Ctrl+K), screenshots against `D-Dialogs.png`, registry completeness test green.

## 5. Interactive Bookmark Editor, Layers (OCG), Attachments & Portfolios, and Editable Info/XMP/ViewerPreferences

Complete the SPEC 8.3 and 9.2 document-structure panels and their `recto-pdf` engines (`src/pdf/ocg.cpp`, `src/pdf/portfolio.cpp`, `src/pdf/outlines_edit.cpp`):
1. **Interactive Bookmark Editor (Outline panel):** Add bookmark at current page/zoom (`GoTo` destination), rename, add child bookmark, drag or keyboard reorder/nest, delete, or auto-generate bookmarks from document heading styles, and save to a copy.
2. **Layers (Optional Content Groups / OCG) engine & panel:** Parse `/OCProperties` (`/OCGs`, `/D` default visibility, `/Order`, and `BDC /OC ... EMC` conditional content streams in `src/pdf/ocg.cpp`), expose the **Layers** tab in the side panel with eye toggles that re-record/composite the visible layers live on `DocumentActor`, plus `Reset to Default Visibility` and `Flatten Visible Layers`.
3. **Attachments & PDF Portfolios engine & panel:** Parse, extract, add, and delete embedded files (`/Names /EmbeddedFiles` and `FileAttachment` annotations) and **PDF Portfolios (`/Collection`)** in `src/pdf/portfolio.cpp`, surfaced in the **Attachments & Portfolio** side panel with file icon, name, size, modified date, description, `Open` (prompting before opening external file types per SPEC 9.9), `Save / Extract`, `Add File`, `Remove`, and `Save as PDF Portfolio`.
4. **Editable Info, XMP Metadata & Viewer Preferences:** Extend the §3 Info panel with `Edit Info & Preferences` (`Title`, `Author`, `Subject`, `Keywords`, custom XMP metadata properties, and `/ViewerPreferences` flags `DisplayDocTitle`, `FitWindow`, `CenterWindow`, `HideToolbar`, `HideMenubar`, initial `PageMode` and `PageLayout`, and initial open page).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 8.3 (Outline Bookmark Editor, Layers panel, Attachments & Portfolio panel) and 9.2 (Editable Info, XMP & Viewer Preferences panel) with `D-Focus-Light.png` and `D-ReadingRoom.png`.
**Job:** The user can edit document bookmarks, toggle PDF layers (`OCG`), extract or bundle attachments and PDF Portfolios, and edit document metadata and viewer preferences. Consumer: `DocumentActor` (live layer visibility and outline navigation) and saved PDF copies.
**Treatment:** Native `recto-pdf` support for `/OCProperties`, `/EmbeddedFiles`, `/Collection`, `/Outlines` writing, `/Info` + XMP packet synchronization, and `/ViewerPreferences` hosted in the shared side-panel switcher. Cheaper substitute that fails the checkpoint: read-only outlines, ignoring `BDC /OC` layer blocks when rendering, or showing `/Info` without XMP or `/ViewerPreferences` editing.
**Chrome:** consume the §2 side-panel switcher, `TreeView` styling, and `RectoColors.xaml` tokens across Focus, Classic, and Reading room slots.

**Build order:**

1. Implement `src/pdf/ocg.cpp` (`/OCProperties` parsing and conditional `BDC /OC` display-list filtering), `src/pdf/portfolio.cpp` (`/EmbeddedFiles` and `/Collection` portfolio read/write/extract), and outline/metadata/viewer-preferences serialization in `src/pdf/document.cpp`. Done when: `PdfOcg.*`, `PdfPortfolio.*`, and `PdfOutlinesEdit.*` unit tests toggle layer visibility, round-trip embedded files and portfolios, and update `/Outlines`, `/Info`, XMP, and `/ViewerPreferences` under ASan.
2. Build the **Layers** panel, **Attachments & Portfolio** panel, **Outline** edit controls (add, rename, nest, reorder, delete), and **Info & Viewer Preferences** edit mode in `src/app`, registering all commands in the §4 `Ctrl+K` registry. Done when: toggling an OCG layer updates the rendered page immediately, extracting an attachment writes the exact bytes to disk, and edited bookmarks and metadata persist in the saved copy.

- [ ] `src/pdf/ocg.cpp`, `src/pdf/portfolio.cpp`, and outline/metadata/viewer-preferences editing in `recto-pdf`. Done when: `recto-tests.exe --gtest_filter=PdfOcg.*:PdfPortfolio.*:PdfOutlinesEdit.*` passes under ASan. Cheaper substitute: ignoring `/OCProperties` or `/EmbeddedFiles` during document load.
- [ ] Layers panel, Attachments & Portfolio panel, Interactive Bookmark Editor, and Editable Info/XMP/ViewerPreferences UI wired in all three layout presets. Done when: driven UI tests in `Panels.OcgPortfolioAndOutlines` verify live layer toggling, attachment extraction, bookmark editing, and `Ctrl+K` reachability.
- [ ] Commit: `viewer: add Bookmark Editor, OCG Layers panel, Attachments/Portfolio panel, and Info/XMP/ViewerPreferences editor`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfOcg.*:PdfPortfolio.*:PdfOutlinesEdit.*:Panels.*` passes under ASan, with display-session screenshots of the Layers, Attachments/Portfolio, Bookmark Editor, and Info/Preferences panels recorded in findings.

## Verification

- Every SPEC 9.1, 8.3, 9.2 Info and 9.4 element accounted for on rendered surfaces in the review findings.
- Recents survive restart, pruning and corruption as specified; panels stay in sync both ways.
- `recto-tests` full suite green; `python scripts/todo-graph.py validate` passes.
