---
schema_version: 1
id: organize-ui
domain: 05-organize
status: active
title: "TODO-02 -- Organize UI, Crop/Watermark/Bates Dialogs, Redaction Bar and Digital Signatures UI"
track: M4
---

# TODO-02 -- Organize UI, Crop/Watermark/Bates Dialogs, Redaction Bar and Digital Signatures UI

> **Goal:** SPEC 9.6's one page grid with three modes (drag to reorder with a selection action bar, combine many files with per-source colours, and split with click-between-pages breaks and named parts), plus the interactive Organize authoring dialogs with live page preview (`Insert Blank Page`, `Crop & Resize Pages`, `Watermark & Stamp`, `Header, Footer & Bates Numbering`) and the Viewer's security & redaction surfaces (`Redaction & Sanitize Bar` `Ctrl+Shift+X`, `Signature Status Banner & Sign Mode` `Ctrl+Shift+G`, `Sign Document Dialog`, `Signatures & Standards` side panel, and `Protected & Signed PDF` / `Unlocked PDF` Convert cards), all compiling to `D05 T01`'s operations and guarded so unsaved work cannot be lost silently.

> **Current state:** Measured 2026-10-14. No code exists. The boards are `D-Organize.png` (reorder, the chosen design), `explorations/A-Organize.png` (combine) and `explorations/B-Organize.png` (split); SPEC 8.2, 8.3, 9.5, 9.6 and 9.9 define the Organize header, dark floating selection bar, live-preview authoring dialogs, redaction/sanitization bar, and digital signature surfaces. Page thumbnails reuse `D02 T03 §4`'s tile pipeline at grid size; saves run as `D04 T01` jobs.

## Inputs

- `design/SPEC.md` sections 8.2 (`Redaction & Sanitize Bar`, `Signature Status Banner & Sign Mode`), 8.3 (`Signatures & Standards` side panel), 9.5 (`Protected & Signed PDF` and `Unlocked PDF` cards), 9.6 (`Organize` modes and tools), 9.9 (`unsaved-changes`, `Replace original`, `Watermark`, `Header/Footer & Bates`, `Crop & Resize`, `Sanitize Document`, and `Sign Document` dialogs), 7 (grid shape), 10 (`Ctrl+Shift+X`, `Ctrl+Shift+G`), 11 (keyboard operation).
- -> XREF: D02 T01 §10 -- the `Unsaved-Changes`, `Replace original`, `Insert Blank Page`, `Crop & Resize Pages`, `Watermark & Stamp`, `Header, Footer & Bates`, `Sanitize Document`, and `Sign Document` modals use `D02 T01 §10`'s single-active-dialog host.
- -> XREF: D02 T02 §8 -- the Floating Page Toolbar's `Organize`, `Redact`, and `Sign` buttons activate the surfaces built in §1, §9, and §10 here.
- -> XREF: D02 T03 §3 -- the Viewer `Signatures` side panel (`§11` here) plugs into `SidePanelHost` (`D02 T03 §3`).
- -> XREF: D02 T03 §4 -- the Organize grid (`§1` here) reuses the thumbnail tile pipeline from `D02 T03 §4`, and the viewer thumbnail context menu (`Insert Blank Page`, `Crop`, `Extract`, `Delete`) invokes the dialogs and operations wired in §6 here.
- -> XREF: D02 T04 §6 -- the `Verify signatures when opening` and `Default timestamp server URL` settings (`D02 T04 §6`) feed §10's `Signature Status Banner` and `Sign Document Dialog`.
- -> XREF: D03 T01 §4 -- the floating selection toolbar's `Redact` button (`D03 T01 §4`) invokes §9's redaction marker.
- -> XREF: D03 T01 §6 -- `Find & Redact` in §9 reuses the `FindCard` and regex search engine from `D03 T01 §5`-`§6`.
- -> XREF: D04 T01 §4 -- the `Protected & Signed PDF` and `Unlocked PDF` Convert cards (§11 here) register against `D04 T01 §3`-`§4`.
- -> XREF: D04 T01 §5 -- saves from Organize (§5) run as queue jobs with progress and cancel on `D04 T01 §5`'s `QueueCard`.
- -> XREF: D04 T05 §9 -- the `Signatures` section (§11 here) and `Standards & E-Invoice` section (`D04 T05 §9`) share `SignaturesAndStandardsPanel`.
- -> XREF: D05 T01 §1 -- the engine operation vocabulary (`D05 T01 §1`-`§10`) every mode, dialog, and security bar compiles to.
- -> XREF: D08 T01 §11 -- the assistant's ContextBuilder reads pending marks so redacted content is never sent.
- -> XREF: D04 T07 §5 -- the Action Wizard's redact step (D04 T07 §5) uses these presets.
- -> XREF: D09 T05 §20 -- signature appearance library sync.
- -> XREF: D08 T02 §23 -- organize grid.
- -> XREF: D08 T02 §31 -- Suggested redaction mark state.
- -> XREF: D03 T01 §15 -- zone OCR to redaction.
- -> XREF: D05 T01 §27 -- sanitize counts from the inspector.
- -> XREF: D02 T04 §2 -- copy image DPI setting.
- -> XREF: D08 T02 §31 -- suggested marks.
- -> XREF: D02 T03 §4 -- thumbnail drag source.
- -> XREF: D02 T01 §5 -- signed version tab opens normally.

## Outcome

- A user can select, reorder, rotate, delete, and extract pages in a virtualized grid by direct manipulation and keyboard (`§1`, `§2`), see an accurate unsaved-change summary, undo/redo within the session, and save with the original protected by `AtomicFileWriter` and the `Unsaved-Changes` guard (`§5`).
- Combining files (`§3`, `explorations/A-Organize.png`) and splitting into named parts (`§4`, `explorations/B-Organize.png`) produce the exact outputs shown in the grid.
- Interactive `Insert Blank Page` and `Crop & Resize Pages` (`§6`), `Watermark & Stamp` (`§7`), and `Header, Footer, Page Numbers & Bates Numbering` (`§8`) dialogs provide a live miniature page preview and compile to `D05 T01 §4`-`§6` operations.
- The Viewer's `Redaction & Sanitize Bar` (`Ctrl+Shift+X`, `§9`), `Signature Status Banner` & `Sign Document Dialog` (`Ctrl+Shift+G`, `§10`), `Signatures` side panel, and `Protected & Signed PDF` / `Unlocked PDF` Convert cards (`§11`) provide end-to-end visual redaction, sanitization, cryptographic signing, and verification.
- A `Replace Pages` dialog (`§12`) lets a user pick pages to replace, a source PDF and its pages, preview before and after, and see exactly what bookmarks, links and form fields will change before a copy is written.
- A `Backgrounds` dialog (`§13`) adds a colour, image or PDF-page background with a live preview over the real page content, and lists, edits and removes the managed backgrounds and watermarks already in the file.
- Certificate encryption in the existing Protect flow (`§14`) lets a user choose recipients from the Windows certificate store or `.cer` files with per-recipient permissions, and a certificate prompt (`§15`) opens public-key encrypted files, each reachable from `Ctrl+K`.
- The page tools of `D05 T01 §15` to `§23` and `§30` have their surfaces (`§16` to `§26`): split methods, Split Page, N-up, order commands, Page Labels, blank page review, cross-document drag, copy as image, binder in Combine, ID Card Copy and Paging Seal, each reachable from `Ctrl+K`.
- The Redaction Bar gains profiles, country packs and a review list (`§27`), suggested marks, a labelled whiteout and whole-page redaction (`§28`); an Inspect Document panel (`§29`), signed-version view and comparison (`§30`), Trusted Lists (`§31`) and pen pressure capture (`§32`) complete the security surfaces.

**Adjacency:** list=not-applicable (the grid is the list, in this file); document=applicable @ D05 T02 §5; settings=applicable @ D02 T04 §6; reporting=not-applicable (the unsaved-change summary is in-place state, §2); notifications=applicable @ D04 T01 §6; permissions=not-applicable (no roles); audit=not-applicable (session undo is in-memory state, not a history record); exchange=not-applicable (file in and out are the modes themselves, §3 and §4); reverse=applicable @ D05 T02 §1

**Adjacency rationale:** The grid's reverse is session `Undo`/`Redo` (`§1`) and its document is the saved output (`§5`); both are owned here because Organize is where Recto authors PDF structure interactively. Queue notices (`D04 T01 §6`) and settings (`D02 T04 §5`-`§6`) belong to their respective files.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Virtualized Organize `PageGrid`, `OrganizeEditModel`, multi-select & `Undo`/`Redo` stack | D05 T01 §3, D02 T03 §4 | [ ] |
| 2 | §2 | Drag-and-drop and keyboard page reordering, floating action bar & `90°` rotation badges | §1 | [ ] |
| 3 | §3 | Organize **Combine** mode (`A-Organize.png`: per-source chips, `Add files` & edge tint) | §2, D05 T01 §2 | [ ] |
| 4 | §4 | Organize **Split** mode (`B-Organize.png`: click-between-pages dividers & part names) | §2, D05 T01 §2 | [ ] |
| 5 | §5 | Organize `Save` (replace-original) and `Save as copy` jobs plus `Unsaved-Changes` guard | §2, §3, §4, D04 T01 §5, D02 T01 §10 | [ ] |
| 6 | §6 | `Insert Blank Page` dialog (`E8A5`) and interactive `Crop & Resize Pages` (`E7A8`) | §5, D05 T01 §4, D02 T01 §10 | [ ] |
| 7 | §7 | Two-column `640 epx` `Watermark & Stamp` dialog (`SPEC 9.6` & `9.9` with live preview) | §5, D05 T01 §5, D02 T01 §10 | [ ] |
| 8 | §8 | Two-column `640 epx` `Header, Footer & Bates Numbering` dialog with live page preview | §7, D05 T01 §6 | [ ] |
| 9 | §9 | Viewer `Redaction & Sanitize Bar` (`Ctrl+Shift+X`), `Find & Redact` & `Sanitize` dialog | §5, D05 T01 §10, D03 T01 §4, D03 T01 §6 | [ ] |
| 10 | §10 | Viewer `Signature Status Banner`, `Draw Signature Box` (`Ctrl+Shift+G`) & `Sign Dialog` | §5, D05 T01 §8, D05 T01 §9, D02 T04 §6 | [ ] |
| 11 | §11 | Viewer `Signatures` side panel (`SPEC 8.3`) and `Protected & Signed` / `Unlocked` cards | §10, D02 T03 §3, D04 T01 §4, D05 T01 §9 | [ ] |
| 12 | §12 | `Replace Pages` dialog: source picker, before and after preview, consequences | §5, D05 T01 §14, D02 T01 §10, D02 T03 §4, D02 T03 §11 | [ ] |
| 13 | §13 | `Backgrounds` dialog: live preview over page content and managed marks list | §7, D05 T01 §13, D02 T03 §11 | [ ] |
| 14 | §14 | Certificate encryption in the Protect flow: recipients, permissions, warnings | §11, D05 T01 §11, D04 T01 §4, D02 T03 §11 | [ ] |
| 15 | §15 | Certificate prompt when opening a public-key encrypted PDF | D05 T01 §12, D02 T01 §5, D02 T01 §10, D04 T01 §7 | [ ] |
| 16 | §16 | Split mode: split by file size and by top-level bookmarks | §4, D05 T01 §15 | [ ] |
| 17 | §17 | Split Page dialog: grid or draggable cut lines with a live preview | §5, §6, D05 T01 §16 | [ ] |
| 18 | §18 | N-up dialog: several pages on one sheet with a live sheet preview | §5, §7, D05 T01 §17 | [ ] |
| 19 | §19 | Reverse, swap, interleave and bookmark-order commands in Organize | §3, D05 T01 §18 | [ ] |
| 20 | §20 | Page Labels dialog: ranges, styles, prefixes and a live label preview | §5, D05 T01 §19 | [ ] |
| 21 | §21 | Blank page review: find, check and remove blank pages from the grid | §1, D05 T01 §20 | [ ] |
| 22 | §22 | Drag pages between open documents and out to Explorer | §3, §5, D05 T01 §3 | [ ] |
| 23 | §23 | Copy a page as an image from the Organize grid | §2, D01 T02 §9 | [ ] |
| 24 | §24 | Combine mode: table of contents page and a rebuildable binder project | §3, §5, D05 T01 §30 | [ ] |
| 25 | §25 | ID Card Copy dialog: front and back images on one page | §5, §6, D05 T01 §23 | [ ] |
| 26 | §26 | Paging Seal dialog: a seal image split across page edges | §7, §13, D05 T01 §22 | [ ] |
| 27 | §27 | Redaction Bar: saved profiles, country packs and a match review list | §9, D05 T01 §25, D05 T01 §26 | [ ] |
| 28 | §28 | Redaction Bar: suggested marks, whiteout and whole-page redaction | §9, D05 T01 §24 | [ ] |
| 29 | §29 | Inspect Document panel: what the file carries, with a path to Sanitize | §9, D05 T01 §27, D02 T03 §3 | [ ] |
| 30 | §30 | Signatures panel: view the signed version and compare it with the current document | §11, D05 T01 §28 | [ ] |
| 31 | §31 | Trusted Lists dialog: opt-in download, manual import and per-list control | §11, D05 T01 §29 | [ ] |
| 32 | §32 | Pressure-sensitive pen capture for handwritten signatures | §10 | [ ] |

## 1. Virtualized Organize `PageGrid`, `OrganizeEditModel`, multi-select & `Undo`/`Redo` stack

Build the foundation of `OrganizePage` (`src/app/views/OrganizePage.xaml/.cpp` and `src/services/organize_edit_model.h/.cpp`):
- **`OrganizeEditModel`:** In-memory ordered list of page items (`source_id`, `source_page_index`, `current_page_number_1based`, `rotation_delta_deg`, `is_blank_page`, `is_selected`) with a reversible session `Undo` / `Redo` stack (`Ctrl+Z` / `Ctrl+Y`) so edits never touch the file on disk until `Save`.
- **Virtualized `PageGrid`:** Renders `160 x 212 epx` page cards (`20 epx` grid gap) reusing `D02 T03 §4`'s thumbnail tile pipeline at the grid bucket, with single click, `Ctrl+Click` toggle, `Shift+Click` range selection, `Ctrl+A` select all, `Esc` clear selection, and accessible check badges (`SPEC 11`: never colour alone).

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 7 (Organize grid card `160 x 212 epx`, `20 epx` gap), 9.6 (`Organize` header and page grid), and 11 (selection check badge + accent border, keyboard focus), with `design/screenshots/D-Organize.png`.
**Job:** The user can open any PDF in Organize, scroll smoothly through hundreds of page thumbnails with bounded memory, select single, non-contiguous, or range groups of pages with mouse or keyboard, and undo/redo any structural edit. Consumer: Reorder (`§2`), Combine (`§3`), Split (`§4`), and Save compilation (`§5`).
**Treatment:** One `OrganizeEditModel` backing a virtualized `ItemsView` / `GridView` with `160 x 212 epx` cards and a snapshot/command `Undo`/`Redo` stack. Cheaper substitute that fails the checkpoint: non-virtualized thumbnail allocation that exhausts RAM on a 300-page PDF, or applying edits directly to the file on disk per click so Undo is impossible.
**Chrome:** consume the mode-switcher `Organize` slot (`D02 T01 §4`), the floating page toolbar's `Organize` button (`D02 T02 §8`), the thumbnail tile pipeline (`D02 T03 §4`), and `RectoColors.xaml` card/selection tokens.

**Build order:**

1. Implement `OrganizeEditModel` in `src/services/organize_edit_model.h/.cpp` managing the ordered `PageItem` vector, selection set (`select_single`, `toggle_select`, `select_range`, `select_all`, `clear_selection`), and reversible `Undo` / `Redo` history stack (`can_undo`, `can_redo`, `undo`, `redo`, `is_dirty`). Done when: `OrganizeUi.EditModelAndUndo` verifies multi-selection semantics and multi-step undo/redo returning the model to the exact baseline state (`is_dirty() == false`).
2. Build `OrganizePage.xaml/.cpp` with the mode header (`Reorder`, `Combine`, `Split` segmented switch, `Undo`/`Redo` buttons, `Save as copy`, `Save`) and the virtualized `160 x 212 epx` `PageGrid`. Done when: a 200-page PDF scrolls smoothly with bounded thumbnail memory and matches `D-Organize.png`.

- [ ] Implement `OrganizePageItem` (`item_id`, `source_id`, `source_page_index`, `display_page_number`, `rotation_delta_deg`, `is_blank_page`, `blank_size_pt`, `is_selected`, `source_color_index`) and `OrganizeEditModel` in `src/services/organize_edit_model.h/.cpp`. Done when: `OrganizeEditModel` compiles cleanly in `recto-services` with zero XAML dependencies.
- [ ] Implement `Click` (single select), `Ctrl+Click` / `Space` (toggle item in selection), `Shift+Click` / `Shift+Arrow` (contiguous range select from anchor), `Ctrl+A` (select all), and `Esc` (clear selection) in `OrganizeEditModel`. Done when: `OrganizeUi.SelectionSemantics` verifies all 5 selection gestures.
- [ ] Implement the session `Undo` / `Redo` stack (`Ctrl+Z` / `Ctrl+Y`) in `OrganizeEditModel`, recording snapshots before every reorder, rotate, delete, blank-page insert,combine, or split-break mutation. Done when: performing 5 edits followed by 5 `undo()` calls restores the exact initial page sequence and clears `is_dirty()`.
- [ ] Build the virtualized `PageGrid` in `src/app/views/OrganizePage.xaml` using `160 x 212 epx` page cards with `20 epx` spacing, rendering each page thumbnail via `D02 T03 §4`'s `ThumbnailCache` with `CancelToken` cancellation when scrolled out of view. Done when: scrolling a 200-page document in `OrganizePage` keeps thumbnail cache memory bounded (`<= 64` grid bitmaps). Cheaper substitute that fails the checkpoint: eagerly rasterizing all 200 pages on entering Organize.
- [ ] Render selected cards with a `2 epx` `RectoAccentBrush` border, `RectoAccentSubtleBrush` card fill, and a top-left circular `--accent` check badge (`SPEC 11`: colour is never the only selection signal). Done when: `OrganizePage` card selection matches `D-Organize.png`.
- [ ] Wire the mode-switcher `Organize` tab (`D02 T01 §4`), floating page toolbar `Organize` button (`D02 T02 §8`), and `Ctrl+K` (`Organize Pages`) to load the active document into `OrganizePage`. Done when: switching to `Organize` loads the current PDF's pages into `PageGrid`.
- [ ] Add `OrganizeUi.EditModelAndUndo` and `OrganizeUi.SelectionSemantics` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=OrganizeUi.EditModel*:OrganizeUi.Selection*` passes.

- [ ] Commit: virtualized Organize `PageGrid`, `OrganizeEditModel`, multi-select (`Click`/`Ctrl`/`Shift`), and session `Undo`/`Redo` stack (`D-Organize.png`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=OrganizeUi.EditModel*:OrganizeUi.Selection*` passes, and `PageGrid` card layout matches `D-Organize.png`.

## 2. Drag-and-drop and keyboard page reordering, floating action bar & `90°` rotation badges

Implement **Reorder** mode interactions on `OrganizePage` (`SPEC 9.6` and `D-Organize.png`):
- Drag-and-drop reordering of single or multiple selected pages with an `--accent` vertical drop-insertion bar between cards, plus full keyboard reordering (`Alt+Left` / `Alt+Right` or the action bar's `Move to...` page index popover).
- The dark floating selection action bar (`"N pages selected"`, `Rotate left`, `Rotate right`, `Move to...`, `Insert blank page`, `Extract to new PDF`, `Delete` in `--danger` colour, and `Clear selection`).
- `90°` / `180°` / `270°` rotation badges on rotated page cards and live rotated thumbnail rendering.
- The header change summary (`"2 moved · 1 rotated · 1 deleted"`) computed live from `OrganizeEditModel` against the opened baseline.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.6 (`Reorder` mode, dark floating selection action bar, rotation badges, drop indicator, and header change summary) with `design/screenshots/D-Organize.png`.
**Job:** The user can rearrange, rotate, delete, or extract pages by mouse drag or keyboard, see a live summary of every unsaved change in the header, and undo any mistake with `Ctrl+Z`. Consumer: `OrganizeEditModel` (`§1`) and `Save` compilation (`§5`).
**Treatment:** Multi-item drag-and-drop and keyboard `move_selected_to(target_index)` mutating `OrganizeEditModel`, paired with a deterministic diff calculator (`compute_change_summary()`) comparing current model state to the baseline state so rotating a page 4 times (`360°`) or undoing a move reports zero phantom changes. Cheaper substitute that fails the checkpoint: mouse-only drag with no keyboard reorder path, or hand-incrementing change counters that still say `"1 rotated"` after rotating back to `0°` or pressing `Ctrl+Z`.
**Chrome:** consume the dark floating bar tokens (`#1B1A19` / `#2A2826`), `RectoAccentBrush` drop indicator and rotation badge, and `RectoDangerBrush` for `Delete`.

**Build order:**

1. Implement `move_selected_pages(target_index)`, `rotate_selected_pages(delta_deg)`, `delete_selected_pages()`, and `compute_change_summary()` (`moved_count`, `rotated_count`, `deleted_count`, `inserted_count`, `formatted_summary`) in `src/services/organize_edit_model.cpp`. Done when: `OrganizeUi.ReorderRotateDeleteSummary` verifies that a scripted edit sequence and its undo (or `4 x 90°` full-circle rotation) report exact change counts and return to `"No changes"` when back at baseline.
2. Build the drag-and-drop drop-indicator visual, the `Move to...` popover, the `90°` rotation badge on page cards, and the dark floating `OrganizeSelectionBar` in `src/app/views/OrganizePage.xaml/.cpp`. Done when: screenshots of `OrganizePage` with active selection, rotation badge, and drop indicator match `D-Organize.png`.

- [ ] Implement `move_selected_pages(size_t target_insertion_index)` in `OrganizeEditModel` preserving the relative order of multi-selected pages and recording an undo snapshot. Done when: moving pages `{2, 5}` to index `0` produces `[2, 5, 1, 3, 4, 6]` and `undo()` restores `[1, 2, 3, 4, 5, 6]`.
- [ ] Implement `rotate_selected_pages(int delta_deg)` (`-90` or `+90`, normalized modulo `360`) and `delete_selected_pages()` (preventing deletion of the last remaining page with a polite message). Done when: rotating `+90°` sets `rotation_delta_deg = 90` and deleting all pages is refused cleanly.
- [ ] Compute `ChangeSummary` (`moved_count`, `rotated_count`, `deleted_count`, `inserted_count`) directly by comparing the current `OrganizeEditModel` items against the opened baseline snapshot (never by accumulating imperative counters). Done when: rotating a page `+90°` then `-90°` (or pressing `Ctrl+Z`) immediately clears `"1 rotated"` from the header summary. Cheaper substitute that fails the checkpoint: imperative counters that show phantom changes after undo or full-circle rotation.
- [ ] Render the `4 epx` vertical `RectoAccentBrush` drop-insertion indicator between grid cards during pointer drag-over and reorder on drop. Done when: dragging a selection of pages drops them at the highlighted gap.
- [ ] Build the dark floating `OrganizeSelectionBar` (`#1B1A19` background, `#2A2826` border, anchored bottom-center of `OrganizePage` when `selected_count > 0`) containing `"N pages selected"`, `Rotate left`, `Rotate right`, `Move to...` (flyout with `Before page N` / `After page N` / `Beginning` / `End`), `Insert blank`, `Extract to new PDF...`, `Delete` (`RectoDangerBrush`), and `Clear` (`Esc`). Done when: keyboard-only reordering via `Move to...` or `Alt+Left`/`Alt+Right` achieves the exact same model state as pointer drag (`OrganizeUi.KeyboardReorderParity`).
- [ ] Render the pill badge (`"90°"`, `"180°"`, `"270°"`) at the top-right of any card with `rotation_delta_deg != 0` and apply the rotation transform to its thumbnail preview. Done when: rotated cards match `D-Organize.png`.
- [ ] Commit: `organize: page grid with Reorder, selection actions and undo`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OrganizeUi.*` passes, screenshots against `D-Organize.png` verify the selection bar, rotation badge, and drop indicator, and scripted edit-and-undo model equality is quoted.

## 3. Organize **Combine** mode (`A-Organize.png`: per-source chips, `Add files` & edge tint)

Build **Combine** mode in `OrganizePage` (`SPEC 9.6` and `design/screenshots/explorations/A-Organize.png`):
- Source file chip bar with `Add files...`, drag-and-drop file ingestion, per-source removable chips assigned a distinct accessible colour from the SPEC source palette (paired with source letter/filename badge so colour is never the only signal), and per-source page counts.
- Every page card in the shared `PageGrid` tinted along its bottom edge with its source's colour and labeled with its source badge (`A · 1`, `B · 1`), supporting full cross-source drag/keyboard reordering, rotation, and deletion.
- Encrypted source PDFs added to Combine prompt for their password via `PasswordDialog` (`D02 T01 §10`) before their pages enter the grid; cancelling the password dialog leaves the grid untouched with an honest status message.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.6 (`Combine` mode with source colour chips, bottom-edge page tints, and output filename input) with `design/screenshots/explorations/A-Organize.png`.
**Job:** The user can combine two or more PDFs into one document, visually distinguish which pages came from which source file, reorder or delete individual pages across sources, and save the combined PDF. Consumer: `D05 T01 §2`'s `merge_pdfs` operation via `§5`'s save flow.
**Treatment:** Multi-source extension of `OrganizeEditModel` (`add_source_document`, `remove_source_document`) where each source opens its own `DocumentActor` for thumbnail rendering and assigns a deterministic palette colour + letter code (`A`, `B`, `C`...). Cheaper substitute that fails the checkpoint: a file-only list with no page-level thumbnail grid or cross-file page reordering.
**Chrome:** consume the shared `PageGrid` (`§1`), `PasswordDialog` (`D02 T01 §10`), and the SPEC source-chip palette from `RectoColors.xaml`.

**Build order:**

1. Extend `OrganizeEditModel` (`src/services/organize_edit_model.cpp`) with multi-source management (`CombineSourceInfo`: `source_id`, `file_path`, `display_name`, `short_tag` `A`..`Z`, `color_index`, `password`, `page_count`), `add_source_document`, `remove_source_document(source_id)`, and `compile_to_merge_specs()`. Done when: adding 3 corpus PDFs populates `OrganizeEditModel` with all pages tagged by source, and removing source 2 removes all (and only) source 2's pages while recording an undo step.
2. Build the Combine mode header strip (`SourceChipsBar`, `Add files...`, `Output filename` box, and `Save combined PDF` button) and bottom-edge source colour bar on `PageGrid` cards matching `explorations/A-Organize.png`, plus the encrypted-source `PasswordDialog` flow. Done when: `OrganizeUi.CombineMode` combines 3 corpus files, verifies encrypted-source password prompt and cancel handling, and matches `explorations/A-Organize.png`.

- [ ] Implement `add_source_document` and `remove_source_document(source_id)` on `OrganizeEditModel`, assigning each added file a distinct `color_index` (`0..5` cycling through 6 high-contrast-safe source colours) and short source badge (`A`, `B`, `C`...) so colour is always paired with a text badge (`SPEC 11`). Done when: `OrganizeUi.CombineMultiSourceModel` adds 3 sources, reorders pages across sources, removes source `B`, and verifies only `A` and `C` pages remain.
- [ ] Build the Combine source-chip bar (`OrganizePage.xaml`) matching `explorations/A-Organize.png`: each chip shows the source colour dot + letter tag (`A`), filename, `"N pages"` count, and remove (`X`) button, followed by the `+ Add files...` button and drag-and-drop file support. Done when: the Combine header and chips match `explorations/A-Organize.png`. Cheaper substitute that fails the checkpoint: merging whole files blindly with no per-page grid visibility or per-source colour chips.
- [ ] Render the `4 epx` bottom-edge source colour stripe and source badge (`"A · p.1"`) on each `PageGrid` card when in `Combine` mode. Done when: every card in `Combine` mode visibly indicates its origin file by both colour stripe and text tag.
- [ ] Check whether an added PDF requires a password before inserting its pages into `OrganizeEditModel`: if `pdf::Document::open` returns `PasswordRequired`, open `D02 T01 §10`'s `PasswordDialog`; on valid password, unlock and add its pages; on cancel, add nothing and show a polite status message (`"Skipped '<filename>' (password required)"`). Done when: `OrganizeUi.CombineEncryptedSource` tests both valid password entry and cancel refusal.
- [ ] Provide the editable `Output file name` `TextBox` (defaulting to `<first_file_stem>-combined.pdf`) and `Save combined PDF` primary button wired to `§5` and `D05 T01 §2`'s `merge_pdfs`. Done when: `OrganizeUi.CombineSaveRoundTrip` saves a combined PDF and verifies every output page pixel-matches (`expect_rgba_equal`) the grid order.
- [ ] Commit: `organize: Combine mode with per-source colour`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OrganizeUi.Combine*` passes, screenshot matches `explorations/A-Organize.png`, combined output renders match page-for-page, and the encrypted-source cancel path is verified.

## 4. Organize **Split** mode (`B-Organize.png`: click-between-pages dividers & part names)

Build **Split** mode in `OrganizePage` (`SPEC 9.6` and `design/screenshots/explorations/B-Organize.png`):
- Interactive scissors/divider gutters between every pair of adjacent pages in `PageGrid` (clickable by mouse or toggled via `Space`/`Enter` when focused by keyboard) that place or remove a visual split break.
- Derived **Parts**coloured bands in the grid and a right-hand **Parts panel** listing each part (`Part 1`, `Part 2`...) with its colour badge, derived page range (`"Pages 1-3 (3 pages)"`), and an editable output filename `TextBox`.
- Split method selector (`Split by placed breaks`, `Split every N pages`, `One file per page`) that updates the visual breaks and parts list live, plus duplicate/invalid filename collision validation before `Save N files` starts.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.6 (`Split` mode with visual between-page break dividers, coloured part groups, and right-hand parts panel) with `design/screenshots/explorations/B-Organize.png`.
**Job:** The user can visually slice a PDF into multiple named files by clicking between pages or choosing a split rule, edit each output file's name, and save all parts in one batch job. Consumer: `D05 T01 §2`'s `split_pdf` operation via `§5`'s save flow.
**Treatment:** Split break set (`std::btree_set<size_t>` / sorted boundary indices `1..N-1`) in `OrganizeEditModel` driving derived `SplitPartItem` models (`part_index`, `color_index`, `start_page_1based`, `end_page_1based`, `output_filename`), preserving user-edited custom filenames whenever a part's start page survives a break toggle, and validating filename uniqueness before enabling `Save N files`. Cheaper substitute that fails the checkpoint: a plain text range input with no visual between-page split dividers, or mouse-only 2-pixel split targets unreachable by keyboard.
**Chrome:** consume the shared `PageGrid` (`§1`), the source/part colour palette (`§3`), and `D05 T01 §1`'s shared range semantics.

**Build order:**

1. Implement split break management in `OrganizeEditModel` (`toggle_split_break(after_page_index)`, `set_split_every_n(n)`, `set_split_single_pages()`, `clear_split_breaks()`, derived `parts()` list with custom filename preservation, and `validate_split_filenames()`). Done when: `OrganizeUi.SplitModelAndValidation` verifies break toggling, `every-N` generation, custom name preservation, and duplicate/illegal filename refusal.
2. Build the between-page interactive split gutters on `PageGrid` (`>= 24 epx` hit width with hover/focus scissors indicator and active dashed `--accent` / part-colour divider line) and the right-hand **Parts panel** matching `explorations/B-Organize.png`. Done when: `OrganizeUi.SplitModeUi` places breaks by mouse and keyboard, verifies collision refusal on duplicate filenames, saves the split parts, and matches `explorations/B-Organize.png`.

- [ ] Implement `SplitModeState` in `OrganizeEditModel` (`src/services/organize_edit_model.cpp`): sorted break indices `after_page_index` (`0 .. page_count - 2`), `toggle_split_break`, `set_split_every_n(uint32_t n)`, and `set_split_single_pages()`, deriving `N_breaks + 1` `SplitPartItem`s with default names `<stem>-part-1.pdf`..`<stem>-part-K.pdf` and preserving custom user-typed names keyed by `start_page_index`. Done when: toggling a new break in Part 2 preserves Part 1's custom filename (`"Cover-and-TOC.pdf"`).
- [ ] Build the interactive between-page split gutter control in `PageGrid` (`OrganizePage.xaml`): `>= 24 epx` wide focusable button (`AutomationProperties.Name = "Split between page K and page K+1"`) displaying a subtle hover line + scissors glyph (`E8C6`) when inactive and a solid coloured split divider when active. Done when: both mouse click and keyboard (`Tab` / `Arrow` + `Space` / `Enter`) toggle a split break at any page boundary. Cheaper substitute that fails the checkpoint: mouse-only between-page targets or a text-only range box without visual grid breaks.
- [ ] Tint each `PageGrid` card's bottom border and part badge (`"Part 1"`, `"Part 2"`) with its derived part colour matching `explorations/B-Organize.png`. Done when: adding or removing a split break immediately re-colours the downstream cards and updates their part badges.
- [ ] Build the right-hand **Parts panel** (`explorations/B-Organize.png`) containing the method selector (`By placed breaks`, `Every N pages` with `NumberBox`, `One file per page`), part count summary (`"3 files will be created"`), scrollable list of part cards (colour badge, `"Pages X-Y"`, editable `TextBox` for filename), and primary button `"Save N files"`. Done when: the Parts panel matches `explorations/B-Organize.png`.
- [ ] Validate part filenames live in `validate_split_filenames()`: refuse empty names, illegal Windows filename characters (`<>:"/\|?*`), or case-insensitive duplicate names between two parts (e.g., naming both Part 1 and Part 2 `"intro.pdf"`), showing an inline error badge on the colliding part cards and disabling `"Save N files"`. Done when: `OrganizeUi.SplitFilenameCollision` proves duplicate part names block Save with the exact fault named.
- [ ] Compile the validated `SplitPartItem` list to `D05 T01 §2`'s `split_pdf` via `§5`'s save flow and verify each output file's name, page count, and rendered pixels. Done when: `OrganizeUi.SplitSaveRoundTrip` passes.
- [ ] Commit: `organize: Split mode with visual breaks and named parts`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OrganizeUi.Split*` passes, screenshot matches `explorations/B-Organize.png`, split outputs match by filename, page count, and renders, and duplicate-name refusal is verified.

## 5. Organize `Save` (replace-original) and `Save as copy` jobs plus `Unsaved-Changes` guard

Implement the Organize save pipeline and the `Unsaved-Changes` guard (`SPEC 9.6` and `9.9`, `D-Organize.png`, `D-Dialogs.png`):
- **`Save` and `Save as copy`:** Compile the active Organize mode (`Reorder` edit plan in `D05 T01 §3`, `Combine` merge specs in `D05 T01 §2`, `Split` part specs in `D05 T01 §2`) into an isolated `ConversionJob` on `D04 T01 §1`'s runner and `D04 T01 §5`'s `QueueCard`:
  - `Save` on `Reorder` confirms via the SPEC 9.9 `Replace original file?` dialog (`D-Dialogs.png`) and atomically replaces the source file via `AtomicFileWriter` (`D04 T02 §1`), then reloads the viewer/grid baseline so the change summary resets to clean (`"No changes"`).
  - `Save as copy` prompts via `FileSavePicker` (or `FolderPicker` in `Split` mode) and writes to the new destination.
- **`Unsaved-Changes` Guard (`"Save your changes first?"`):** Whenever the user attempts to leave Organize with `OrganizeEditModel::is_dirty() == true` (switching mode tabs to `Document`/`Convert`/`Home`, closing the tab, or closing the app window), present the SPEC 9.9 modal (`Save`, `Discard`, `Cancel` in `D-Dialogs.png`): `Cancel` stays in Organize; `Discard` resets `OrganizeEditModel` to the untouched file on disk; `Save` completes the atomic save job before proceeding with navigation or close.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.6 (Organize header `Save` and `Save as copy`) and 9.9 (`Unsaved changes` dialog: `"Save your changes first?"` `Save` / `Discard` / `Cancel`, and `Replace original` confirmation) with `design/screenshots/D-Organize.png` and `design/screenshots/D-Dialogs.png`.
**Job:** The user can save their reorganized, combined, or split PDF safely without ever corrupting the original file, and cannot accidentally lose unsaved page edits when switching tabs or closing the window. Consumer: `ConversionRunner` (`D04 T01 §1`), `QueueCard` (`D04 T01 §5`), and `AppShell` navigation/close guards (`D02 T01 §3`-`§4`).
**Treatment:** Compile-on-save snapshot submitted as a `ConversionJob` over `AtomicFileWriter`, marking `OrganizeEditModel` clean **only** when the job finishes with `JobState::Completed` (never at submission time, so a cancelled or failed save keeps the model dirty). Cheaper substitute that fails the checkpoint: writing to disk on every drag edit, or marking the model clean before the save job completes.
**Chrome:** consume `QueueCard` (`D04 T01 §5`), `AtomicFileWriter` (`D04 T02 §1`), and the shared `ContentDialog` host (`D02 T01 §10`).

**Build order:**

1. Implement `OrganizeSaveController` (`src/services/organize_save_controller.cpp`) compiling `OrganizeEditModel` snapshots for `Reorder`, `Combine`, and `Split` into `ConversionJob` specs (`D04 T01 §1`), locking/snapshotting the saving state, and updating the model baseline only on `JobState::Completed`. Done when: `OrganizeUi.SaveJobsAndCancel` verifies that a completed save resets `is_dirty() == false` and a cancelled/failed save job leaves `is_dirty() == true` and the original file byte-for-byte untouched.
2. Build `UnsavedChangesDialog` (`"Save your changes first?"` `Save` / `Discard` / `Cancel`) and `ReplaceOriginalDialog` (`D-Dialogs.png`), and hook `UnsavedChangesDialog` into `AppShell` mode switching, tab close (`Ctrl+W`), and window close (`WM_CLOSE` / `AppWindow::Closing`). Done when: `OrganizeUi.UnsavedChangesGuard` drives all 3 exit triggers (`mode switch`, `tab close`, `window close`) across all 3 dialog choices (`Cancel`, `Discard`, `Save`) and verifies byte-for-byte SHA-256 file integrity on `Discard`.

- [ ] Implement `OrganizeSaveController::submit_save` and `submit_save_as_copy` in `src/services/organize_save_controller.h/.cpp` compiling an immutable snapshot of `OrganizeEditModel` into `D05 T01 §2`-`§3` operations and submitting it to `ConversionRunner` (`D04 T01 §1`). Done when: Organize saves show progress in `QueueCard` (`D04 T01 §5`) and raise completion notifications (`D04 T01 §6`).
- [ ] Keep `OrganizeEditModel::is_dirty()` `true` while a save job is `Waiting` or `Running`, flipping to `false` (and resetting the baseline snapshot so the header summary clears) **only** when `ConversionRunner` reports `JobState::Completed`. Done when: cancelling a save job mid-write leaves `is_dirty() == true` and the file on disk untouched. Cheaper substitute that fails the checkpoint: clearing `is_dirty()` at job submission before the file is written.
- [ ] Refresh any open viewer `DocumentActor` tab pointing to the replaced file path after an atomic `Save` (replace original) completes so switching back to `Document` view displays the newly saved page order and rotations. Done when: reordering pages in Organize, clicking `Save`, and switching to `Document` view displays the new page order.
- [ ] Build `ReplaceOriginalDialog` and `UnsavedChangesDialog` (`src/app/dialogs/OrganizeGuardDialogs.xaml/.cpp`, hosted via `D02 T01 §10`) matching `D-Dialogs.png` (`"Save your changes first?"` with `Save` primary accent button, `Discard` button, and `Cancel` button). Done when: dialog screenshots match `D-Dialogs.png`.
- [ ] Intercept `AppShell` mode navigation (leaving `Organize`), tab close (`Ctrl+W` / tab `X`), and window close (`AppWindow::Closing`) whenever `OrganizeEditModel::is_dirty() == true`: `Cancel` aborts navigation/close and keeps the user in Organize; `Discard` discards in-memory edits, leaves the source file byte-for-byte untouched (verified by SHA-256), and proceeds with navigation/close; and `Save` runs the atomic save job to completion and proceeds only if the save succeeds (staying in Organize if the save fails or is cancelled). Done when: `OrganizeUi.UnsavedChangesGuard` tests all 3 exit paths x 3 button responses (9 combinations).
- [ ] Commit: `organize: save flows as jobs with the unsaved-changes guard`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OrganizeUi.Save*:OrganizeUi.UnsavedChangesGuard` passes, dialog screenshots match `D-Dialogs.png`, and the `Discard` byte-identity SHA-256 hash is verified.

## 6. `Insert Blank Page` dialog (`E8A5`) and interactive `Crop & Resize Pages` (`E7A8`)

Build the `Insert Blank Page` dialog (`E8A5`) and the interactive `Crop & Resize Pages` dialog (`E7A8`, `SPEC 9.6` & `9.9`), accessible from the Organize header/selection bar (`§2`), the viewer's left page-thumbnail context menu (`D02 T03 §4`), the `Tools` menu, and `Ctrl+K`, and wired to `D05 T01 §4`:
- **`Insert Blank Page` Dialog:** Position (`Before selected page` / `After selected page` / `At beginning` / `At end`), Page count (`1..100`), Paper size (`Match current page`, `A4`, `Letter`, `Legal`, `Custom pt/mm/in`), and Orientation (`Portrait` / `Landscape`), inserting directly into `OrganizeEditModel` (`§1`) with full `Ctrl+Z` undo.
- **`Crop & Resize Pages` Dialog (`640 epx` two-column modal):** Interactive crop handles overlaid on a live miniature page preview on the right, numeric margin inputs (`Top`, `Bottom`, `Left`, `Right` in `pt`, `mm`, `in`) on the left, target box selector (`CropBox`, `MediaBox`, `TrimBox`, `BleedBox`, `ArtBox`), one-click `Auto-Detect White Margins` (`D05 T01 §4`), paper-size rescaling (`Scale page content to fit A4 / Letter / Legal / Custom`), and page scope (`Selected pages`, `All pages`, `Even/Odd pages`).

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 9.6 (`Organize` tools: `Insert Blank Page` `E8A5`, `Crop & Resize` `E7A8`) and 9.9 (`Crop & Resize Pages Dialog` two-column `640 epx` layout with interactive crop handles on live preview).
**Job:** Users can insert blank pages of any size or visually crop margins and rescale page sizes with live preview handles and automatic white-margin detection. Consumer: `OrganizeEditModel` (`§1`), `D02 T03 §4` thumbnail context menu, and `D05 T01 §4` engine operations.
**Treatment:** Two-column `640 epx` `ContentDialog` where dragging the `--accent` crop handles on the right preview updates the `Top`/`Bottom`/`Left`/`Right` numeric `NumberBox` values on the left in real time (and vice versa), with unit conversion (`pt`, `mm`, `in`) and `detect_white_margins` (`D05 T01 §4`). Cheaper substitute that fails the checkpoint: numeric-only margin inputs with no live page preview or draggable crop handles, or omitting `Auto-Detect White Margins`.
**Chrome:** consume the shared `ContentDialog` host (`D02 T01 §10`), `RectoAccentBrush` crop box/handles, and `D05 T01 §4` operations.

**Build order:**

1. Build `InsertBlankPageDialog` (`src/app/dialogs/InsertBlankPageDialog.xaml/.cpp`) and wire it to `OrganizeEditModel::insert_blank_pages` (`§1`) and the viewer thumbnail context menu (`D02 T03 §4`). Done when: inserting a blank page adds a crisp blank card to `PageGrid`, updates the header change summary, and undoes cleanly with `Ctrl+Z`.
2. Build `CropAndResizeDialog` (`src/app/dialogs/CropAndResizeDialog.xaml/.cpp`, `640 epx` two-column modal with interactive 8-handle crop box over the rendered page preview, `pt`/`mm`/`in` unit switcher, `Auto-Detect White Margins` button, target box dropdown, and paper-size rescaling controls). Done when: `AppTests.OrganizeCropAndBlankUi` inserts a blank page, opens `CropAndResizeDialog`, clicks `Auto-Detect White Margins`, drags a crop handle, and applies the crop job through `ConversionRunner`.

- [ ] Build `InsertBlankPageDialog` (`src/app/dialogs/InsertBlankPageDialog.xaml/.cpp`, hosted via `D02 T01 §10`) with `Position` (`After current page`, `Before current page`, `At beginning of document`, `At end of document`), `Count` (`1..100`), `Size` (`Match current page`, `A4`, `Letter`, `Legal`, `Custom`), and `Orientation` (`Portrait`, `Landscape`). Done when: confirming the dialog inserts the blank page entries into `OrganizeEditModel` with undo support.
- [ ] Build `CropAndResizeDialog` (`src/app/dialogs/CropAndResizeDialog.xaml/.cpp`, `640 epx` width) with left-column controls (`Apply to box`: `CropBox`, `MediaBox`, `TrimBox`, `BleedBox`, `ArtBox`; `Units`: `Points (pt)`, `Millimeters (mm)`, `Inches (in)`; `Top`/`Bottom`/`Left`/`Right` `NumberBox` inputs; `Auto-Detect White Margins` button; `Page size rescaling`: `Keep original size`, `Scale to A4`, `Scale to Letter`, `Scale to Legal`, `Custom`; `Pages`: `Selected`, `All`, `Even`, `Odd`) and right-column live page preview (`1..N` page stepper). Done when: `CropAndResizeDialog` renders at `640 epx` width and passes `D03 T02 §3` accessibility checks.
- [ ] Implement bidirectional synchronization between the 8 draggable `--accent` crop handles on the right page preview and the `Top`/`Bottom`/`Left`/`Right` `NumberBox` controls on the left (converting accurately between `pt`, `mm = pt * 25.4 / 72.0`, and `in = pt / 72.0`). Done when: dragging the top crop handle downward on the preview immediately increments the `Top` margin `NumberBox`. Cheaper substitute that fails the checkpoint: static preview with no draggable crop handles or no `Auto-Detect White Margins` button.
- [ ] Wire the `Auto-Detect White Margins` button to `D05 T01 §4`'s `detect_white_margins`, snapping the preview crop box and numeric margin fields to the tight content bounds + padding. Done when: clicking `Auto-Detect White Margins` on a corpus page snaps the crop margins to the non-white content box.
- [ ] Wire `Insert Blank Page` (`E8A5`) and `Crop & Resize Pages...` (`E7A8`) from the Organize header, `OrganizeSelectionBar` (`§2`), viewer thumbnail context menu (`D02 T03 §4`), `Tools` menu, and `Ctrl+K`. Done when: `AppTests.OrganizeCropAndBlankUi` passes end-to-end.

- [ ] Commit: `Insert Blank Page` dialog (`E8A5`) and interactive `Crop & Resize Pages` dialog (`E7A8`, `SPEC 9.6` & `9.9`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.OrganizeCropAndBlankUi` passes.

## 7. Two-column `640 epx` `Watermark & Stamp` dialog (`SPEC 9.6` & `9.9` with live preview)

Build the two-column `640 epx` **Watermark & Stamp Dialog** (`src/app/dialogs/WatermarkDialog.xaml/.cpp`, `SPEC 9.6` & `9.9`), reachable from the Organize header, the Viewer `Tools -> Watermark...` menu, and `Ctrl+K`, and wired to `D05 T01 §5`:
- **Left column controls:** Watermark source (`Text` with text box, font family, size, colour swatch | `Image or PDF Logo` with file picker and scale `%`), Rotation slider/presets (`-90°..+90°`, `-45°`, `0°`, `+45°`), Opacity slider (`0%..100%`), Layer (`Behind page content` | `In front of page content`), Position (3x3 9-point anchor grid or `Tile across page` with spacing), and Page scope (`All pages`, `Even`, `Odd`, custom range).
- **Right column live preview:** Renders the selected page's `DisplayList` composited with `D05 T01 §5`'s `build_watermark_preview_display_list` so every change to text, colour, opacity, angle, layer, or 9-point anchor updates the miniature page preview immediately.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 9.6 (`Organize` tools: `Watermark`) and 9.9 (`Watermark & Background Dialog`: `640 epx` two-column modal with 9-point anchor grid and live miniature page preview).
**Job:** Users can apply text or logo watermarks (behind or in front of page content, angled or tiled) and see the exact visual result on a live page preview before saving. Consumer: `ConversionRunner` (`D04 T01 §1`), `D05 T01 §5` `apply_watermark`, and `OrganizePage` / `DocumentView`.
**Treatment:** `640 epx` two-column `ContentDialog` bound to `WatermarkDialogViewModel` calling `build_watermark_preview_display_list` (`D05 T01 §5`) on background worker threads to update the right-hand preview image within `< 16 ms` of any control change. Cheaper substitute that fails the checkpoint: a form-only dialog with no live miniature page preview or missing 9-point anchor grid / tiling controls.
**Chrome:** consume `D02 T01 §10`'s modal dialog host, `RectoColors.xaml` tokens, and `D05 T01 §5`'s preview and save APIs.

**Build order:**

1. Implement `WatermarkDialogViewModel` (`src/services/watermark_dialog_view_model.cpp`) and build `WatermarkDialog.xaml/.cpp` (`640 epx` two-column layout with text/image switch, font/size/colour, `-90°..+90°` rotation, `0..100%` opacity, behind/in-front layer radio, 3x3 9-point position grid + `Tile across page`, page scope, and live page preview with page stepper). Done when: `AppTests.WatermarkDialogUi` changes text, rotation (`-45°`), opacity (`35%`), and 9-point anchor, verifies the live preview bitmap updates on each change, and runs the watermark save job through `ConversionRunner`.

- [ ] Implement `WatermarkDialogViewModel` in `src/services/watermark_dialog_view_model.h/.cpp` constructing a `WatermarkOptions` struct (`D05 T01 §5`) from UI state and triggering a debounced (`16 ms`) preview render via `build_watermark_preview_display_list`. Done when: `WatermarkDialogViewModel` produces updated preview bitmaps for every property change.
- [ ] Build the left-column controls in `WatermarkDialog.xaml` (`640 epx` total dialog width): `Type` segmented toggle (`Text` | `Image / PDF Logo`), text input + font/size/colour controls (or file picker + scale `%` slider for image/PDF logo), `Rotation` slider (`-90°` to `+90°` with `-45°`, `0°`, `+45°` quick chips), `Opacity` slider (`10%` to `100%`), and `Layer` radio buttons (`Behind page content` | `In front of page content`). Done when: switching between `Text` and `Image / PDF Logo` swaps the source input rows cleanly.
- [ ] Build the interactive `3 x 3` **9-point anchor grid** control (`Top-Left`, `Top-Center`, `Top-Right`, `Middle-Left`, `Center`, `Middle-Right`, `Bottom-Left`, `Bottom-Center`, `Bottom-Right`) plus the `Tile across page` toggle in `WatermarkDialog.xaml`, with accessible names on all 9 anchor buttons (`D03 T02 §3`). Done when: clicking any of the 9 anchor cells moves the watermark to that corner/edge in the live preview. Cheaper substitute that fails the checkpoint: center-only placement without the 9-point anchor grid or tiling toggle.
- [ ] Build the right-column live miniature page preview pane with `< Prev Page` / `Page k of N` / `Next Page >` stepper so the user can inspect how the watermark looks on page 1, interior pages, and landscape pages before applying. Done when: stepping pages updates the previewed page + watermark composite.
- [ ] Wire the primary `Apply Watermark` button to submit `D05 T01 §5`'s `apply_watermark` job through `ConversionRunner` (`D04 T01 §1`) and `AtomicFileWriter`. Done when: `AppTests.WatermarkDialogUi` applies a watermark and verifies the output PDF.

- [ ] Commit: two-column `640 epx` `Watermark & Stamp` dialog (`SPEC 9.6` & `9.9` with live miniature page preview)

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.WatermarkDialogUi` passes.

## 8. Two-column `640 epx` `Header, Footer & Bates Numbering` dialog with live page preview

Build the two-column `640 epx` **Header, Footer, Page Numbers & Bates Numbering Dialog** (`src/app/dialogs/HeaderFooterBatesDialog.xaml/.cpp`, `SPEC 9.6` & `9.9`), reachable from the Organize header, the Viewer `Tools -> Header, Footer & Bates Numbering...` menu, and `Ctrl+K`, and wired to `D05 T01 §6`:
- **Left column controls:** 6 text input zones (`Top-Left`, `Top-Center`, `Top-Right`, `Bottom-Left`, `Bottom-Center`, `Bottom-Right`), one-click token insertion chips (`Insert Page Number {page}`, `Insert Page of Total {page} of {total}`, `Insert Date {date}`, `Insert File Name {filename}`, `Insert Bates Number {bates}`), **Bates Numbering** configuration (`Prefix` e.g. `CASE-`, `Suffix`, `Start Number`, `Digits 4-10`, plus multi-file batch list when stamping a folder/set of PDFs), Typography & Margins (`Font`, `Size`, `Colour`, `Top/Bottom/Left/Right Margin`), and `Shrink page content to avoid overlapping existing text` toggle.
- **Right column live preview:** Miniature page preview showing the expanded header, footer, and Bates strings (`CASE-000101`) and the shrunk page content box live as the user edits.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 9.6 (`Organize` tools: `Header / Footer & Bates`) and 9.9 (`Header, Footer, Page Numbers & Bates Numbering Dialog`: `640 epx` two-column modal with 6 zone inputs, token chips, Bates settings, shrink-to-avoid-overlap toggle, and live preview).
**Job:** Users can add page numbers, running headers/footers, or sequential legal Bates numbers (`CASE-000001`) across one or many PDFs with one-click token chips and a live page preview. Consumer: `ConversionRunner` (`D04 T01 §1`), `D05 T01 §6` `apply_headers_footers_and_bates`, and `OrganizePage` / `DocumentView`.
**Treatment:** `640 epx` two-column `ContentDialog` tracking the last-focused zone `TextBox` (`Top-Left`..`Bottom-Right`) so clicking any token chip (`Insert Page Number`, `Insert Bates Number`, etc.) inserts `{page}` / `{bates}` at the caret in that zone and updates the right-hand miniature page preview immediately. Cheaper substitute that fails the checkpoint: requiring users to memorize and type `{bates}` syntax manually without token insertion chips, omitting Bates numbering, or omitting the live page preview.
**Chrome:** consume `D02 T01 §10`'s modal dialog host, `RectoColors.xaml` tokens, and `D05 T01 §6`'s engine operations.

**Build order:**

1. Implement `HeaderFooterBatesDialogViewModel` (`src/services/header_footer_bates_view_model.cpp`) and build `HeaderFooterBatesDialog.xaml/.cpp` (`640 epx` two-column modal with 6 zone inputs, 5 token insertion chips, Bates prefix/suffix/start/digits controls, multi-file batch queue support, shrink-to-avoid-overlap toggle, and live page preview). Done when: `AppTests.WatermarkAndBatesUi` inserts `{page} of {total}` into `Bottom-Center` and `{bates}` (`CASE-`, start `101`, `6` digits) into `Bottom-Right` via token chips, verifies the live preview shows `"1 of 10"` and `"CASE-000101"`, and saves a stamped PDF whose `PageText` contains `"CASE-000101"`..`"CASE-000110"`.

- [ ] Implement `HeaderFooterBatesDialogViewModel` in `src/services/header_footer_bates_view_model.h/.cpp` tracking the active zone (`TopLeft`..`BottomRight`), inserting token strings (`{page}`, `{page} of {total}`, `{date}`, `{filename}`, `{bates}`) at the active zone's cursor, configuring `BatesConfig` (`prefix`, `suffix`, `start_number`, `digit_count` `4..10`), and rendering the live preview page. Done when: `HeaderFooterBatesDialogViewModel` expands tokens live for whichever preview page index is selected.
- [ ] Build the 6 header/footer zone `TextBox` inputs (`Top-Left`, `Top-Center`, `Top-Right`, `Bottom-Left`, `Bottom-Center`, `Bottom-Right`) and the 5 one-click token insertion pill buttons (`+ Page Number`, `+ Page of Total`, `+ Date`, `+ File Name`, `+ Bates Number`) in `HeaderFooterBatesDialog.xaml`. Done when: focusing `Bottom-Right` and clicking `+ Bates Number` inserts `{bates}` and opens/enables the Bates configuration section. Cheaper substitute that fails the checkpoint: offering only a single footer box or omitting the token insertion chips and Bates configuration.
- [ ] Build the **Bates Numbering** section (`Prefix`, `Suffix`, `Start number` `NumberBox`, `Number of digits` `4..10` `NumberBox` with live sample badge e.g. `"Preview: CASE-000101"`, and `Add files for multi-document sequential Bates batch...`). Done when: changing `Prefix` to `"EXH-"` and `Digits` to `6` updates the sample badge and right-hand page preview to `"EXH-000001"`.
- [ ] Build the **Appearance & Margin Safety** section: `Font family`, `Font size`, `Text colour`, `Top / Bottom / Left / Right margin` (`pt`/`mm`/`in`), `Start numbering at page` offset, and the `Shrink page content to avoid overlapping existing text` toggle (`SPEC 9.9`, wired to `D05 T01 §6`). Done when: toggling `Shrink page content to avoid overlapping existing text` visibly insets the page content preview inside the header/footer bands.
- [ ] Wire `Apply Header, Footer & Bates` to submit `D05 T01 §6`'s single-document or multi-document sequential Bates job through `ConversionRunner` (`D04 T01 §1`). Done when: `AppTests.WatermarkAndBatesUi` passes end-to-end.
- [ ] Commit: `organize: add Insert Blank Page, Crop/Resize, Watermark and Bates Numbering dialogs`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.OrganizeCropAndBlankUi:AppTests.WatermarkAndBatesUi` passes.

## 9. Viewer `Redaction & Sanitize Bar` (`Ctrl+Shift+X`), `Find & Redact` & `Sanitize` dialog

Build the visual redaction and document sanitization surfaces specified in `design/SPEC.md` sections 8.2 (`Redaction & Sanitize Bar`) and 9.9 (`Sanitize Document Dialog`), wired to `D05 T01 §10`:
- **Contextual `40 epx` `Redaction & Sanitize Bar` (`Ctrl+Shift+X`, floating page toolbar `Redact` button `D02 T02 §8`, floating selection toolbar `Redact` button `D03 T01 §4`, or `Ctrl+K`):**
  - `Mark Text & Area for Redaction`: crosshair/I-beam tool that marks either selected `PageText` character quads or a rubber-band rectangular region with a translucent red (`--danger` `RectoDangerBrush` border + `20%` red fill) preview overlay; hovering a mark shows the solid burn-in preview + exemption label.
  - `Mark Pages for Redaction`: marks entire pages or ranges for complete page redaction.
  - `Find & Redact`: opens `D03 T01 §6`'s `FindCard` in `Mark matches for redaction` mode with one-click **Regex pattern presets** (`Email Addresses`, `Phone Numbers`, `Social Security / National ID Numbers`, `Credit Card Numbers`, `Dates`, `IPv4 Addresses`), allowing single-match or `Mark All Matches for Redaction`.
  - `Redaction Appearance`: burn-in fill colour (`Black` default, `White`, `Dark Gray`) and optional overlay exemption code (`None`, `REDACTED`, `CONFIDENTIAL`, `PRIVILEGED`, `FOIA (b)(6)`, or custom text).
  - `Sanitize Document...`: opens the SPEC 9.9 `Sanitize Document Dialog` with checkboxes for stripping `Document metadata & XMP`, `JavaScript & actions`, `Embedded attachments`, `Comments, annotations & form data`, `Hidden layers (OCGs)`, and `Deleted objects & incremental history`.
  - `Apply Redactions (N)` (`--danger` primary button): opens a non-bypassable confirmation dialog warning that physical redaction permanently removes underlying text, vector paths, and image pixels, then executes `D05 T01 §10`'s `Redactor::apply_redactions` via `ConversionRunner` / `AtomicFileWriter`.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Redaction & Sanitize Bar`), 9.3 (`SelectionFloatingToolbar` `Redact` action), 9.9 (`Sanitize Document Dialog` and `Apply Redactions` confirmation), and 10 (`Ctrl+Shift+X`).
**Job:** Users can mark sensitive text, images, or regex patterns (`Emails`, `SSNs`, `Credit Cards`, `Phones`, `Dates`) for redaction, preview the exact redaction boxes with exemption codes, permanently scrub the underlying content stream and image pixels, or one-click sanitize all hidden metadata and scripts from a PDF. Consumer: `DocumentView` (`D02 T02`), `D05 T01 §10` `Redactor`, and `ConversionRunner`.
**Treatment:** Two-phase `Mark -> Apply` workflow where pending `RedactionMark`s are reversible on-canvas preview overlays (undoable via `Ctrl+Z`) until the user clicks `Apply Redactions` and confirms, which runs `D05 T01 §10`'s physical content-stream redactor to write a clean scrubbed PDF. Cheaper substitute that fails the checkpoint: applying redactions as black rectangle annotations that leave selectable text underneath, or omitting `Find & Redact` regex presets and `Sanitize Document`.
**Chrome:** consume `RectoDangerBrush` for redaction marks and the `Apply Redactions` button, `D03 T01 §6`'s `FindCard` for `Find & Redact`, and `D02 T01 §10`'s modal dialog host.

**Build order:**

1. Implement `RedactionViewModel` (`src/services/redaction_view_model.cpp`) managing pending `RedactionMark`s (`add_text_selection_mark`, `add_area_mark`, `add_page_mark`, `add_search_hit_marks`, `remove_mark`, `clear_marks`, `Undo`/`Redo` integration), built-in regex presets (`Email`, `Phone`, `Ssn`, `CreditCard`, `IsoDate`, `Ipv4`), burn-in appearance settings, and `Apply Redactions` / `Sanitize Document` job submission. Done when: `RedactionUi.ViewModelAndRegexPresets` marks email addresses and credit-card numbers via regex presets and verifies exact `RedactionMark` quads.
2. Build `RedactionBar` (`src/app/controls/RedactionBar.xaml/.cpp`, `40 epx` strip), the on-canvas redaction preview overlay (`RedactionCanvasOverlay`), `SanitizeDocumentDialog` (`SPEC 9.9`), and the `Apply Redactions` confirmation dialog. Done when: `AppTests.RedactionAndSanitizeUi` marks text via regex (`Email Addresses`) and area lasso, applies redactions, and verifies the saved PDF has zero extractable redacted text and zero XMP/`/Info` metadata after sanitization.

- [ ] Implement `RedactionViewModel` in `src/services/redaction_view_model.h/.cpp` storing pending `RedactionMark` items per page with `Ctrl+Z` / `Ctrl+Y` undo/redo support and exposing built-in regex preset patterns (`Email Addresses`, `Phone Numbers`, `SSN / National ID`, `Credit Card Numbers`, `Dates`, `IPv4 Addresses`). Done when: `RedactionUi.ViewModelAndRegexPresets` matches all 6 preset patterns against a test page and creates `RedactionMark`s over the matched `CharQuad`s.
- [ ] Build the `40 epx` contextual `RedactionBar` (`SPEC 8.2`, toggled via `Ctrl+Shift+X`, floating page toolbar `Redact` in `D02 T02 §8`, or `Ctrl+K`) with `Mark Text & Area`, `Mark Pages...`, `Find & Redact` (with regex preset dropdown), `Appearance` (`Fill colour` + `Exemption code` e.g. `REDACTED`, `FOIA (b)(6)`), `Sanitize Document...`, and `Apply Redactions (N)` (`RectoDangerBrush` button enabled when `N > 0`). Done when: `RedactionBar` renders at `40 epx` height and all controls carry automation names and tooltips (`D03 T02 §3`).
- [ ] Wire `SelectionFloatingToolbar`'s `Redact` button and right-click context menu `Mark for Redaction` (`D03 T01 §4`) to add a `RedactionMark` over the selected character quads and open `RedactionBar`. Done when: selecting a phrase and clicking `Redact` on the floating selection toolbar marks the phrase in translucent `--danger` red.
- [ ] Render pending `RedactionMark` boxes on `DocumentView` with a `1.5 epx` `RectoDangerBrush` border and `20%` red fill (transitioning on pointer hover to preview the solid burn-in fill colour and centered exemption code text, with a delete `X` handle). Done when: hovering a redaction mark shows the exact post-redaction burn-in appearance. Cheaper substitute that fails the checkpoint: drawing a black annotation rectangle without invoking `D05 T01 §10`'s physical stream redactor.
- [ ] Build `SanitizeDocumentDialog` (`src/app/dialogs/SanitizeDocumentDialog.xaml/.cpp`, `SPEC 9.9`, hosted via `D02 T01 §10`) with 6 category checkboxes (all checked by default) and item counts detected in the active document (`Metadata & XMP`, `JavaScript & actions`, `Embedded files`, `Comments & form data`, `Hidden layers (OCGs)`, `Deleted objects & incremental history`), invoking `D05 T01 §10`'s `Redactor::sanitize_document`. Done when: `AppTests.RedactionAndSanitizeUi` sanitizes a document and confirms `0` remaining metadata/scripts/attachments.
- [ ] Build the `Apply Redactions` confirmation modal (`SPEC 9.9`: warning that underlying text, paths, and image pixels inside the `N` marked regions will be permanently removed, with `Save as redacted copy...` default and `Replace original`) invoking `D05 T01 §10`'s `Redactor::apply_redactions`. Done when: `AppTests.RedactionAndSanitizeUi` passes end-to-end.

- [ ] Commit: viewer `Redaction & Sanitize Bar` (`Ctrl+Shift+X`, `SPEC 8.2` & `9.9`: `Mark Text & Area`, `Find & Redact` regex presets, `Sanitize Document` dialog, `Apply Redactions` confirmation)

**Test checkpoint:** `recto-tests.exe --gtest_filter=RedactionUi.*:AppTests.RedactionAndSanitizeUi` passes.

## 10. Viewer `Signature Status Banner`, `Draw Signature Box` (`Ctrl+Shift+G`) & `Sign Dialog`

Build the Viewer's digital signature banner and interactive signing workflow specified in `design/SPEC.md` sections 8.2 (`Signature Status Banner & Sign Mode`) and 9.9 (`Sign Document Dialog`), wired to `D05 T01 §8` and `D05 T01 §9`:
- **Automatic `36 epx` `Signature Status Banner` (`SPEC 8.2`):** Appears below the toolbar whenever a PDF containing `/FT /Sig` digital signatures opens (honoring Settings `Verify signatures when opening` from `D02 T04 §6`), displaying:
  - Green (`RectoSuccessBrush` + shield check icon): `"All digital signatures are valid"`
  - Amber (`RectoWarningBrush` + warning icon): `"Signed, but certificate is untrusted or unsigned changes were made"`
  - Red (`RectoDangerBrush` + error icon): `"Signature is invalid -- document has been altered since signing"`
  along with `View Signatures` (opens §11's side panel) and `Sign Document...` (`Ctrl+Shift+G`).
- **On-Canvas `Draw Signature Box` Mode (`Ctrl+Shift+G`):** Switches the cursor to a crosshair so the user drags a signature box on any page (or clicks an existing unsigned `/FT /Sig` placeholder field from `D03 T03 §8`), opening the **`Sign Document Dialog` (`SPEC 9.9`)**:
  - **Certificate Source:** `Windows Certificate Store` personal certificate picker, `PKCS#12 (.pfx / .p12) file` with password, or `Create Self-Signed Certificate...` (Name, Organization, Email).
  - **Signature Appearance:** `Draw handwritten ink` pad, `Upload signature image`, or `Standard text stamp` + `Include signer name, date, reason & location` toggle, with a live appearance preview.
  - **Signing Details & Cryptographic Options:** `Reason` (e.g. `I approve this document`), `Location`, `Contact Info`, standard (`PAdES (ETSI.CAdES.detached)` vs `PKCS#7 (adbe.pkcs7.detached)`), digest (`SHA-256`, `SHA-384`, `SHA-512`), `RFC 3161 Timestamp Server URL` (seeded from `D02 T04 §6`), and `Lock document after signing (/DocMDP)`.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Signature Status Banner & Sign Mode`), 9.7 (`Verify signatures when opening` and `Default timestamp server URL` settings), 9.9 (`Sign Document Dialog`), 10 (`Ctrl+Shift+G`), and 11 (status banner pairs colour with distinct icon and text).
**Job:** Users opening a signed PDF immediately see whether its cryptographic signatures are valid, untrusted, or tampered with, and can draw a signature box on any page (or click a signature form field) to cryptographically sign the document with a Windows Certificate Store, `.pfx`, or self-signed certificate. Consumer: `DocumentView` (`D02 T02`), `D05 T01 §8`-`§9` `SignatureEngine`, and `SignaturesAndStandardsPanel` (`§11`).
**Treatment:** Non-blocking background verification via `DocumentActor` driving the 3-state `SignatureStatusBanner`, paired with on-canvas signature rectangle placement and `SignDocumentDialog` executing `SignatureEngine::sign_document` via incremental save (`D05 T01 §8`). Cheaper substitute that fails the checkpoint: drawing a visual ink annotation without a cryptographic X.509 signature, or omitting the 3-state verification banner.
**Chrome:** consume `RectoSuccessBrush`, `RectoWarningBrush`, `RectoDangerBrush` tokens, `D02 T04 §6` settings, and `D02 T01 §10`'s modal dialog host.

**Build order:**

1. Build `SignatureStatusBanner` (`src/app/controls/SignatureStatusBanner.xaml/.cpp`, `36 epx` strip) bound to `D05 T01 §9`'s `SignatureVerificationSummary`, and wire on-canvas `Draw Signature Box` mode (`Ctrl+Shift+G`, floating page toolbar `Sign` button `D02 T02 §8`, or clicking an unsigned `Sig` form widget in `D03 T03 §8`). Done when: `SignatureUi.BannerStates` verifies all 3 banner states (`ValidTrusted`, `ValidUntrustedRoot`, `InvalidDigestMismatch`) with distinct icons, colours, and accessible names.
2. Build `SignDocumentDialog` (`src/app/dialogs/SignDocumentDialog.xaml/.cpp`, `SPEC 9.9`) with Certificate picker (Windows Cert Store, `.pfx` file, or inline self-signed certificate generator), 3-mode Signature Appearance editor (live ink canvas, image picker, standard text stamp) + live preview box, Reason/Location/Contact inputs, and PAdES / SHA-256..512 / RFC 3161 TSA / `/DocMDP` controls. Done when: `AppTests.SignDocumentDialogUi` creates/selects a certificate, draws an ink appearance, signs a PDF via incremental save, and verifies the green valid banner on reopen.

- [ ] Build `SignatureStatusBanner` (`36 epx` height) displaying the shield icon + status message + `View Signatures` button + `Sign Document` button whenever an opened PDF contains `>= 1` digital signatures. Done when: opening a signed, untrusted, or tampered PDF displays the exact corresponding SPEC 8.2 banner state.
- [ ] Implement on-canvas `Draw Signature Box` mode (`Ctrl+Shift+G`): dragging a rectangle on `DocumentView` (or clicking an unsigned `/FT /Sig` field overlay from `D03 T03 §8`) captures `(page_index, rect_pt)` and opens `SignDocumentDialog`. Done when: `Ctrl+Shift+G` arms placement mode and `Esc` cancels it cleanly.
- [ ] Build the **Certificate Source** section of `SignDocumentDialog` (`SPEC 9.9`): `Windows Certificate Store` dropdown populated via `list_windows_personal_certificates` (`D05 T01 §8`), `Browse PKCS#12 (.pfx / .p12)...` + password `PasswordBox`, and `Create Self-Signed Certificate...` inline expander (`Common Name`, `Organization`, `Email`). Done when: users on a machine with no pre-installed personal certificates can generate or load a `.pfx` certificate directly in the dialog. Cheaper substitute that fails the checkpoint: failing with `"No certificate found"` when the Windows personal store is empty instead of supporting `.pfx` files and self-signed certificate creation.
- [ ] Build the **Signature Appearance & Metadata** section of `SignDocumentDialog`: mode tabs (`Standard Text`, `Draw Handwritten Ink` canvas with `Clear`, `Upload Image`), `Include signer name, date, reason & location` checkbox, live `/AP` preview box, `Reason` combo/textbox (`I approve this document`, `I am the author of this document`, `I have reviewed this document`, or custom), `Location`, and `Contact Info`. Done when: drawing ink or typing a reason updates the live signature preview immediately.
- [ ] Build the **Cryptographic Standards** section: `Format` (`PAdES / ETSI.CAdES.detached` vs `PKCS#7 / adbe.pkcs7.detached`), `Hash algorithm` (`SHA-256`, `SHA-384`, `SHA-512`), `Timestamp server URL` (pre-filled from `D02 T04 §6`'s `Default timestamp server URL` setting), and `Lock document after signing (/DocMDP)` dropdown (`Do not lock`, `No changes allowed (P=1)`, `Allow form filling and counter-signing (P=2)`, `Allow annotations, form filling, and counter-signing (P=3)`). Done when: all options bind cleanly to `PdfSignOptions` (`D05 T01 §8`).
- [ ] Execute `SignatureEngine::sign_document` (`D05 T01 §8`) via `ConversionRunner` / `AtomicFileWriter` on dialog confirmation and reload the signed PDF tab so `SignatureStatusBanner` updates immediately. Done when: `AppTests.SignDocumentDialogUi` signs and counter-signs a document end-to-end.

- [ ] Keep a signature appearance library in `AppPaths::data_dir()`: add, rename, delete, set default and preview several typed, drawn and image appearances, and initials, persisted atomically and kept across upgrades. Done when: `SignatureUi.AppearanceLibrary` stores three appearances, restarts, and finds all three and the default.
- [ ] Import a signature photo by resampling first and limiting size after, then removing the paper background with an adjustable threshold and live preview. Done when: `SignatureUi.ImageBackgroundRemoval` turns a 12 MP phone photo fixture into a transparent appearance under the stored size limit.
- [ ] Place appearances upright as displayed on pages rotated 0, 90, 180 and 270 degrees, and keep the aspect ratio while resizing. Done when: `SignatureUi.UprightOnRotatedPages` renders the appearance upright on all four rotations after save and reopen.
- [ ] Commit: viewer `Signature Status Banner`, on-canvas `Draw Signature Box` mode (`Ctrl+Shift+G`), and `Sign Document Dialog` (`SPEC 9.9`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=SignatureUi.*:AppTests.SignDocumentDialogUi:SignatureUi.AppearanceLibrary:SignatureUi.ImageBackgroundRemoval:SignatureUi.UprightOnRotatedPages` passes.

## 11. Viewer `Signatures` side panel (`SPEC 8.3`) and `Protected & Signed` / `Unlocked` cards

Complete the digital signature and encryption surfaces specified in `design/SPEC.md` sections 8.3 (`Signatures & Standards` side panel) and 9.5 (`Protected & Signed PDF` and `Unlocked PDF` Convert cards):
- **Viewer `Signatures` Side Panel (`SPEC 8.3`, hosted in `SignaturesAndStandardsPanel.xaml/.cpp` alongside `D04 T05 §9`):**
  - Lists every digital signature and unsigned signature field in the document with per-signature status cards: verification badge (`Valid`, `Untrusted Root`, `Invalid / Altered`), signer Common Name & email, issuer Common Name, signing timestamp (and RFC 3161 TSA authority badge), hash & format (`PAdES B-T · SHA-256`), `Reason` & `Location`, `/ByteRange` coverage status (`Covers entire file` vs `Revision K of M`), `Jump to Signature Field` button, and `Certificate Details...` modal (full X.509 chain, serial number, validity period, SHA-256 thumbprint, key usage).
- **Convert Cards (`SPEC 9.5`):**
  - **`Protected & Signed PDF` Card:** Combines `AES-256` (`V=5, R=6`) password & permission encryption (`D05 T01 §7`: user open password, owner permissions password, `Allow printing`, `Allow copying text`, `Allow editing & page assembly`, `Allow annotations & form filling` checkboxes) and optional batch X.509 digital signing (`D05 T01 §8`).
  - **`Unlocked PDF` Card:** Removes password protection and permission restrictions (`D05 T01 §7`'s `decrypt_pdf`) when given the valid password.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.3 (`Signatures & Standards` side panel -- Signatures section) and 9.5 (`Protected & Signed PDF` and `Unlocked PDF` Convert cards and Step 3 options).
**Job:** Users can inspect every cryptographic signature and X.509 certificate chain in the side panel, jump to signature fields on canvas, or batch-protect, sign, or unlock PDFs on the Convert page. Consumer: `SidePanelHost` (`D02 T03 §3`), `ConvertPage` (`D04 T01 §3`-`§4`), and `ConversionRunner`.
**Treatment:** Structured per-signature verification cards in `SignaturesAndStandardsPanel` plus `ProtectedPdfOptionsControl` and `UnlockedPdfOptionsControl` in `ConvertPage`. Cheaper substitute that fails the checkpoint: omitting certificate chain/thumbprint inspection, or offering encryption without granular print/copy/edit/annotate permission checkboxes.
**Chrome:** consume `SignaturesAndStandardsPanel` (`D04 T05 §9` / `D02 T03 §3`), `ConvertPage` (`D04 T01 §4`), and `D02 T01 §10`'s modal dialog host.

**Build order:**

1. Build the **Signatures** section of `SignaturesAndStandardsPanel.xaml/.cpp` (`SPEC 8.3`) and `CertificateDetailsDialog` (`src/app/dialogs/CertificateDetailsDialog.xaml/.cpp`), wiring `View Signatures` on `SignatureStatusBanner` (`§10`) to open the side panel. Done when: `SignatureUi.SidePanelAndCertDetails` opens a counter-signed PDF, inspects both signature cards and their X.509 certificate details, and clicks `Jump to Signature Field` to scroll `DocumentView` to the widget.
2. Build `ProtectedPdfOptionsControl` and `UnlockedPdfOptionsControl` (`src/app/views/convert/`) and register the **Protected & Signed PDF** and **Unlocked PDF** cards in `ConverterRegistry` (`D04 T01 §3`). Done when: `AppTests.DigitalSignaturesUi` runs an `AES-256` encrypt+sign job and an `Unlocked PDF` decrypt job through `ConversionRunner` and verifies the outputs.

- [ ] Build the **Signatures** section of `SignaturesAndStandardsPanel.xaml/.cpp` (`SPEC 8.3`) rendering a card for each `VerifiedSignatureInfo` (`D05 T01 §9`): status icon + badge, signer CN/email, issuer CN, formatted signing time, `Reason` / `Location`, `PAdES`/`PKCS#7` + `SHA-256` tag, `/DocMDP` lock badge, `Jump to Signature Field` button, and `Certificate Details...` button, plus an unsigned-fields list with `Sign Field...` buttons. Done when: clicking `View Signatures` on `SignatureStatusBanner` (`§10`) opens the panel and lists all signatures.
- [ ] Build `CertificateDetailsDialog` (`src/app/dialogs/CertificateDetailsDialog.xaml/.cpp`, hosted via `D02 T01 §10`) displaying the X.509 certificate hierarchy tree (`Root CA -> Intermediate -> Signer`), Subject, Issuer, Serial Number, Valid From / Valid To dates, Public Key algorithm, Key Usage, and `Copy SHA-256 Thumbprint` button. Done when: `SignatureUi.SidePanelAndCertDetails` verifies all certificate fields match the signing cert.
- [ ] Build `ProtectedPdfOptionsControl` (`src/app/views/convert/ProtectedPdfOptionsControl.xaml/.cpp`, `SPEC 9.5`) exposing `Require a password to open the document` toggle + `PasswordBox` (with confirmation and empty-password guard), `Restrict editing, printing, and copying` toggle + owner `PasswordBox` + permission checkboxes (`Allow printing`, `Allow high-resolution printing`, `Allow copying text and graphics`, `Allow annotations and form filling`, `Allow page changes`), and `Digitally sign document` toggle (selecting a certificate from Windows Cert Store or `.pfx`). Done when: all controls map to `PdfEncryptionWriteOptions` (`D05 T01 §7`) and `PdfSignOptions` (`D05 T01 §8`). Cheaper substitute that fails the checkpoint: offering only an open password with no owner permission checkboxes or batch signing option.
- [ ] Build `UnlockedPdfOptionsControl` (`src/app/views/convert/UnlockedPdfOptionsControl.xaml/.cpp`, `SPEC 9.5`) exposing the document password `PasswordBox` (or prompting via `QueueCard`'s `Enter password` flow in `D04 T01 §7` if left blank) to produce a clean unencrypted copy via `D05 T01 §7`'s `decrypt_pdf`. Done when: converting an `AES-256` encrypted PDF via `Unlocked PDF` produces an unencrypted PDF that opens without a password prompt.
- [ ] Register both **Protected & Signed PDF** and **Unlocked PDF** cards in `ConverterRegistry` (`D04 T01 §3`) with their SPEC Principle 4 honest capability hint lines. Done when: `AppTests.DigitalSignaturesUi` runs both cards end-to-end through `ConversionRunner`.
- [ ] Commit: `app: add Redaction and Sanitize bar, Sign Document dialog and Signatures side panel`

**Test checkpoint:** `recto-tests.exe --gtest_filter=RedactionUi.*:SignatureUi.*:AppTests.RedactionAndSanitizeUi:AppTests.SignDocumentDialogUi:AppTests.DigitalSignaturesUi` passes.

## 12. `Replace Pages` dialog: source picker, before and after preview, consequences

Build the dialog for Acrobat Pro's Replace Pages tool (`src/app/dialogs/ReplacePagesDialog.xaml/.cpp`), wired to `replace_pages` (`D05 T01 §14`). A user chooses the pages to replace (prefilled from the Organize selection or the viewer thumbnail selection, and not required to be adjacent), picks a source PDF and the pages to use, sees the target pages and their replacements side by side, and is told what the operation will do to bookmarks, links and form fields before writing anything, using the engine's dry run. The result is a copy; the original is only replaced through the existing Replace-original flow (`§5`). Reachable from the Organize header and selection bar, the viewer thumbnail context menu, the `Tools` menu and `Ctrl+K`.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new dialog; it follows `design/SPEC.md` section 9.6 (Organize Page & Document Tools, dialogs with a live page preview over the working copy) and the section 9.9 form-dialog chrome (`ContentDialog`, radius 20, `28 epx` padding, Cancel and primary buttons side by side), and adds `Replace Pages` to the section 9.6 tool list.
**Job:** A user can replace chosen pages of the open document with pages from another PDF, see the before and after pages and the consequences, and save a consistent copy. Consumer: `ConversionRunner` (`D04 T01 §1`), `replace_pages` (`D05 T01 §14`), the save flows of `§5`.
**Treatment:** A `ContentDialog` bound to `ReplacePagesDialogViewModel` with a target field, source picker, source pages field, paired thumbnail strips, mode and bookmark-policy choices, and a consequence summary from `replace_pages` with `dry_run`. Cheaper substitute that fails the checkpoint: a dialog with two text boxes that replaces the whole span between the first and last page, or one with no preview and no consequence summary.
**Chrome:** consume the single-active-dialog host (`D02 T01 §10`), the thumbnail tile pipeline (`D02 T03 §4`), `OrganizeSelectionBar` (`§2`), the Unsaved-Changes guard and Replace-original flow (`§5`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Implement `ReplacePagesDialogViewModel` in `src/services/replace_pages_dialog_view_model.h/.cpp` and build `ReplacePagesDialog.xaml/.cpp` with the target and source fields, paired previews and dry-run summary. Done when: `AppTests.ReplacePagesDialogUi` replaces pages 2 and 6 of a 10-page PDF with pages 1 and 2 of another, shows the before and after strips and the consequence summary, and writes a copy whose pages 3 to 5 are unchanged.
2. Wire the entry points, the unsaved-changes guard and the result report. Done when: `AppTests.ReplacePagesDialogUi` opens the dialog from the selection bar, the thumbnail menu and `Ctrl+K`, and shows the post-run summary with warnings.

- [ ] Implement `ReplacePagesDialogViewModel` building `ReplacePagesOptions` from UI state, validating fields with `parse_page_range_spec` (`D05 T01 §1`) on each keystroke with the engine's error text, and running `replace_pages` with `dry_run` on a worker thread (debounced `250 ms`) to fill the consequence summary. Done when: an inverted range shows the exact refusal and a valid input shows counts without writing a file.
- [ ] Build the target pages field (prefilled from the current selection, any set of pages, with a `Use selection` button) and the source row (`Choose PDF...` through `FileOpenPicker`, a `Use this document` option, the source page count, and the source password through the SPEC 9.9 Password dialog when needed). Done when: choosing an encrypted source prompts for its password by file name.
- [ ] Build the paired preview: a `Before` strip of target page thumbnails and an `After` strip of the replacement thumbnails with arrows, using the tile pipeline at strip size, and an inline message when the counts differ. Done when: changing either field updates the strips and a count mismatch in `One to one` mode disables `Replace` with the stated reason.
- [ ] Build the mode and policy choices: `One to one` or `Replace with a different number of pages`, and `Point bookmarks and links at the replacement` or `Remove bookmarks that pointed at replaced pages`, with one-line effects shown from the dry run (`"3 bookmarks move, 1 is removed, 4 form fields are removed"`). Done when: switching the policy changes the dry-run numbers.
- [ ] Show the engine's warnings before the user confirms (`pages_untagged`, `xfa_dropped`, `fields_renamed`) in an `InfoBar` with plain wording. Done when: replacing a page of a tagged form with an untagged page shows all three.
- [ ] Apply through `ConversionRunner` (`D04 T01 §1`) to a copy (default `<stem> (pages replaced).pdf`), then offer `Open copy` and `Replace original` (the SPEC 9.9 dialog of `§5`); if the Organize grid has unsaved edits, show the Unsaved-Changes guard first because the engine reads the file on disk. Done when: `AppTests.ReplacePagesDialogUi` covers the guard, the copy and the original left byte-identical.
- [ ] Wire the entry points: Organize header `Replace pages...` and `OrganizeSelectionBar` action, the viewer thumbnail context menu (`D02 T03 §4`), the `Tools` menu, and `organize.replacePages` in `CommandRegistry` with synonyms `swap pages`, `replace page`, `substitute`. Done when: `Ctrl+K` "swap" opens the dialog with the current selection. Cheaper substitute: a dialog with no command-box entry.
- [ ] Add `Replace Pages` to the section 9.6 tool list of `design/SPEC.md`. Done when: the SPEC names the tool and the dialog matches it.
- [ ] Give every control an automation name and tooltip and operate the whole dialog by keyboard (`D03 T02 §3`). Done when: the control inventory test passes for the dialog.
- [ ] Add `ReplacePagesDialog.*` and `AppTests.ReplacePagesDialogUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=ReplacePagesDialog.*:AppTests.ReplacePagesDialogUi` passes.
- [ ] Commit: `app: add the Replace Pages dialog with preview and consequence summary`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ReplacePagesDialog.*:AppTests.ReplacePagesDialogUi` passes on the display-session host.

## 13. `Backgrounds` dialog: live preview over page content and managed marks list

Build the **Backgrounds** dialog (`src/app/dialogs/BackgroundDialog.xaml/.cpp`) beside the Watermark dialog (`§7`), wired to the managed marks engine (`D05 T01 §13`). It matches Acrobat Pro's Background tool (add, update, remove) and its watermark removal. The preview shows the real content of the selected page with the background composited beneath it, so what the user sees is what is saved. A second tab lists the managed backgrounds and the recognised watermarks already in the file, with `Edit` and `Remove`, so a mark can be changed or taken off long after it was added. Reachable from the Organize header, the viewer `Tools -> Background...` menu and `Ctrl+K`.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.9 (`Watermark / Header & Footer / Bates` two-column dialog with a live rendered page preview) and section 9.6 (Organize Page & Document Tools), following the `Watermark & Stamp` dialog of `§7` and adding `Background` to the section 9.6 tool list; no separate screenshot exists.
**Job:** A user can add a colour, image or PDF-page background, see it under the real page content, change or remove it later, and remove an unwanted watermark. Consumer: `ConversionRunner` (`D04 T01 §1`), `PageMarks` (`D05 T01 §13`) and `OrganizePage` / `DocumentView`.
**Treatment:** A two-column `640 epx` `ContentDialog` with a segmented `New background | Existing marks` switch, source, fit, scale, offset, rotation, opacity and page-scope controls on the left, and a live preview from `build_background_preview_display_list` with a page stepper on the right. Cheaper substitute that fails the checkpoint: a form with no live preview, a preview on a blank page, or a dialog that can add a background but never list or remove one.
**Chrome:** consume the dialog host (`D02 T01 §10`), the layout and 9-point conventions of `WatermarkDialog` (`§7`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Implement `BackgroundDialogViewModel` in `src/services/background_dialog_view_model.h/.cpp` and build the `New background` tab with live preview. Done when: `AppTests.BackgroundDialogUi` changes source, fit, opacity and scope and sees the preview update under real page text on each change, then applies a background through `ConversionRunner`.
2. Build the `Existing marks` tab with edit and remove, and wire the entry points. Done when: `AppTests.BackgroundDialogUi` lists a background and a watermark in a prepared file, edits the background, removes the watermark, and the output has no remaining mark bytes.

- [ ] Implement `BackgroundDialogViewModel` building `BackgroundOptions` (`D05 T01 §13`) from UI state and requesting a debounced (`16 ms`) preview from `build_background_preview_display_list` on a worker thread. Done when: every property change produces an updated bitmap whose pixels equal the saved page.
- [ ] Build the left-column controls: `Source` segmented (`Colour`, `Image`, `PDF page`), a colour swatch for content colour, file pickers for image and PDF (with a source page number), `Fit` (`Fill page`, `Fit inside`, `Centre`, `Tile`), scale, offsets, rotation, opacity, `Area` (`Page box` or `Media box`) and page scope (`All`, `Even`, `Odd`, custom range). Done when: switching the source swaps the input rows cleanly. Cheaper substitute: colour only.
- [ ] Build the right-column preview pane with the page stepper (`< Prev Page`, `Page k of N`, `Next Page >`) showing the real page content with the background under it, and a note naming pages in `occluded_pages` whose own fill hides the background. Done when: stepping to an occluded page shows the note and the preview still shows the page content.
- [ ] Build the `Existing marks` tab as a `ListView` of marks from `list_marks` (name, kind, pages, whether editable) with `Edit` (loads the mark into the first tab and applies as `update_mark`) and `Remove` (a confirmation dialog stating which pages lose it); recognised watermarks show `Remove` only. Done when: editing a colour changes it on every page of the mark and removing a mark leaves page text and pixels as in the unmarked file.
- [ ] Apply through `ConversionRunner` and `AtomicFileWriter` on a copy using the same save and Replace-original behaviour as the Watermark dialog (`§7`, `§5`), and show the Unsaved-Changes guard first when the Organize grid has edits. Done when: `AppTests.BackgroundDialogUi` leaves the original byte-identical on `Save as copy`.
- [ ] Wire the entry points: Organize header `Background...`, the viewer `Tools -> Background...` menu, and `organize.backgrounds` and `organize.removeWatermark` in `CommandRegistry` with synonyms `background`, `page colour`, `remove watermark`, `wallpaper`. Done when: `Ctrl+K` "remove watermark" opens the dialog on the `Existing marks` tab. Cheaper substitute: no command-box entry.
- [ ] Add `Background` to the section 9.6 tool list of `design/SPEC.md`. Done when: the SPEC names the tool.
- [ ] Give every control an automation name and tooltip, with all nine anchor and fit controls keyboard operable (`D03 T02 §3`). Done when: the control inventory test passes for the dialog.
- [ ] Add `BackgroundDialog.*` and `AppTests.BackgroundDialogUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=BackgroundDialog.*:AppTests.BackgroundDialogUi` passes.
- [ ] Commit: `app: add the Backgrounds dialog with live preview and managed marks list`

**Test checkpoint:** `recto-tests.exe --gtest_filter=BackgroundDialog.*:AppTests.BackgroundDialogUi` passes on the display-session host.

## 14. Certificate encryption in the Protect flow: recipients, permissions, warnings

Add certificate (public-key) encryption to the existing **Protected & Signed PDF** card (`§11`) as a second way to protect a file: a `Protect with` choice of `Password` or `Certificate`. In certificate mode the user builds a recipient list from the Windows certificate store or from `.cer`, `.crt` and `.p7b` files, sets permissions for each recipient, and is warned about every problem (expired certificate, no encipherment key usage, not able to reopen the file themselves) before anything is written. It runs `encrypt_pdf_for_recipients` (`D05 T01 §11`) and never offers a path that produces an unprotected file. Reachable from the Convert card, the viewer `Tools` menu and `Ctrl+K`.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.5 (`Protected & Signed PDF` card and Step 3 options, `D-Convert.png`); the recipient list and certificate picker are a genuinely new part of that Step 3 form and follow its toggle, `PasswordBox` and checkbox conventions and the `CertificateDetailsDialog` of `§11`.
**Job:** A user can encrypt a PDF so only chosen people can open it, choose what each person may do, and know before saving that the file will open for them and for the user. Consumer: `ConvertPage` (`D04 T01 §3`-`§4`), `ConversionRunner` and `PubSec` (`D05 T01 §11`).
**Treatment:** A `CertificateRecipientsControl` inside `ProtectedPdfOptionsControl` with a recipient `ListView`, a `CertificatePickerDialog`, per-recipient permission checkboxes, an `Include me` selector, and warnings that gate the primary button. Cheaper substitute that fails the checkpoint: one permission set for all recipients, an expired or unsuitable certificate accepted silently, or an `Encrypt` button that falls back to a password or to no encryption.
**Chrome:** consume `ProtectedPdfOptionsControl` and the Step 3 host (`§11`, `D04 T01 §4`), `CertificateDetailsDialog` (`§11`), the dialog host (`D02 T01 §10`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` status tokens paired with icons and text (`SPEC 11`).

**Build order:**

1. Implement `CertificateRecipientsViewModel` in `src/services/certificate_recipients_view_model.h/.cpp` and `CertificatePickerDialog` over `D05 T01 §11`'s recipient loading and validation. Done when: `AppTests.CertificateEncryptionUi` adds a store certificate and a `.cer` file, sees validation badges, sets different permissions for each, and builds a matching `PubSecEncryptionOptions`.
2. Build `CertificateRecipientsControl`, gate the primary action on its warnings, and run the job. Done when: `AppTests.CertificateEncryptionUi` encrypts a file for two test recipients through `ConversionRunner` and reopens it with each test key at its own permissions.

- [ ] Add the `Protect with` segmented choice (`Password`, `Certificate`) to `ProtectedPdfOptionsControl` (`§11`) without changing the password controls; `Certificate` shows `CertificateRecipientsControl`. Done when: switching modes swaps the option rows and keeps the other mode's entries.
- [ ] Implement `CertificateRecipientsViewModel` holding recipients with their validation state (`Valid`, `Expiring`, `Expired`, `Unsuitable`, with reason), per-recipient `PdfPermissions`, `EncryptMetadata`, the `Include me` certificate and the acknowledgment state, and producing `PubSecEncryptionOptions`. Done when: each state maps to the matching option field in `AppTests.CertificateEncryptionUi`.
- [ ] Build `CertificatePickerDialog` (`src/app/dialogs/CertificatePickerDialog.xaml/.cpp`) listing certificates from the `AddressBook` and `MY` stores with search by name or email, columns for name, issuer, expiry and usage, multi-select, and a `Details...` button opening `CertificateDetailsDialog` (`§11`); `Add from file...` takes `.cer`, `.crt` and `.p7b`. Done when: searching narrows the list and an unsuitable certificate shows its reason instead of being hidden.
- [ ] Build the recipient `ListView` with a status icon plus text badge per row (never colour alone), `Remove`, and the permission checkboxes (`Allow printing`, `Allow high-resolution printing`, `Allow copying text and graphics`, `Allow annotations and form filling`, `Allow page changes`) for the selected recipient with an `Apply to all recipients` button. Done when: two recipients keep different permissions. Cheaper substitute: one shared permission set.
- [ ] Build the `Include me so I can reopen this file` selector (certificates from `MY` with a private key), defaulting on when one exists; when no recipient has an available private key require an explicit `I understand I will not be able to open this file` acknowledgment before the primary button enables. Done when: the button stays disabled without an eligible recipient or the acknowledgment.
- [ ] Disable `Digitally sign document` in certificate mode with the stated reason `"Encrypting for certificates rewrites the file and would invalidate a signature."`, and block expired or unsuitable recipients unless the user checks `Allow expired certificates` for the expired case. Done when: `AppTests.CertificateEncryptionUi` shows the reason and the gating.
- [ ] Set the primary button label to `"Encrypt with certificates"` and enqueue `encrypt_pdf_for_recipients` through `ConversionRunner`; a typed refusal appears in the `QueueCard` and no output file exists. Done when: an injected CMS failure shows the error and creates no file.
- [ ] Register `protect.encryptWithCertificate` in `CommandRegistry` (opens the Convert page on the card in `Certificate` mode with the current file) and add `Encrypt with certificate...` to the viewer `Tools` menu, with synonyms `certificate`, `public key`, `encrypt for`, `recipients`. Done when: `Ctrl+K` "recipients" lands on the card in certificate mode. Cheaper substitute: no command-box entry.
- [ ] Give every control an automation name and tooltip and make the picker and list keyboard operable (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `CertificateRecipients.*` and `AppTests.CertificateEncryptionUi` in `tests/recto-tests/` using test-time certificates (no key material committed). Done when: `recto-tests.exe --gtest_filter=CertificateRecipients.*:AppTests.CertificateEncryptionUi` passes.
- [ ] Commit: `app: add certificate encryption to the Protect card with recipients and permissions`

**Test checkpoint:** `recto-tests.exe --gtest_filter=CertificateRecipients.*:AppTests.CertificateEncryptionUi` passes on the display-session host.

## 15. Certificate prompt when opening a public-key encrypted PDF

Make files from `§14` (and other readers' certificate encryption) open in the UI. When `OpenDocumentPipeline` (`D02 T01 §5`) or a Convert job receives `PdfErrorCode::CertificateRequired` (`D05 T01 §12`), it shows a **Choose a certificate** dialog beside the SPEC 9.9 Password dialog instead of the password prompt. The dialog lists the user's certificates that match the file's recipients, lets the user pick another certificate or a `.pfx` file (imported for this session only, never into the store), and waits visibly while Windows asks for a smart-card or token PIN. The file is never opened as a damaged or blank document, and a wrong certificate is a clear message with a way to try another.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.9 (Password dialog, `ContentDialog` radius 20, wrong-attempt state with a red border and message); the certificate list and recipient hints are a genuinely new dialog that follows its layout and button conventions.
**Job:** A recipient can open, convert or print a certificate-encrypted PDF by choosing the certificate that was used for them, and sees plainly when their certificates do not match. Consumer: `OpenDocumentPipeline` (`D02 T01 §5`), the queue error recovery (`D04 T01 §7`) and `DocumentActor::supply_certificate` (`D05 T01 §12`).
**Treatment:** A `CertificatePromptDialog` listing matching certificates (preselected), a `Choose another certificate...` list, an `Open a .pfx file...` path with its password, a waiting state for the PIN, and inline errors. Cheaper substitute that fails the checkpoint: reusing the password box, auto-picking a certificate without asking, or importing a `.pfx` private key into the user's store.
**Chrome:** consume the dialog host and Password dialog conventions (`D02 T01 §10`), `OpenDocumentPipeline` (`D02 T01 §5`), the `Enter password` recovery flow of `QueueCard` (`D04 T01 §7`), and `RectoColors.xaml` tokens.

**Build order:**

1. Implement `CertificatePromptViewModel` in `src/services/certificate_prompt_view_model.h/.cpp` matching `PubSecRecipientHint`s against `MY` certificates that have an available private key, and `CertificatePromptDialog.xaml/.cpp`. Done when: `AppTests.CertificatePromptUi` opens a `D05 T01 §11` output, preselects the matching test certificate, opens the document after `Open`, and shows the permissions the recipient was given.
2. Wire the open pipeline and the queue recovery. Done when: `AppTests.CertificatePromptUi` also shows the no-match message, a cancelled PIN returning to the dialog, and a Convert job that pauses for a certificate and resumes.

- [ ] Add a `CertificateRequired` branch to `OpenDocumentPipeline` (`src/app/OpenPipeline.cpp`) that shows `CertificatePromptDialog` and calls `DocumentActor::supply_certificate`, keeping the tab in its loading state while the key operation runs. Done when: the tab opens only after a successful decrypt and the UI thread is never blocked.
- [ ] Build the matching list (name, issuer, expiry, a `Matches this file` badge with text) from the recipient hints and `MY` store certificates with private keys, preselecting the unique match; with none, show `"None of your certificates can open this file."` and the number of recipients, without revealing recipient names beyond the issuer and serial. Done when: the no-match state shows the message and a `Choose another certificate...` list.
- [ ] Build `Open a .pfx file...` using `PFXImportCertStore` with `PKCS12_NO_PERSIST_KEY` into an in-memory store, asking the `.pfx` password through the SPEC 9.9 Password dialog, and never adding the certificate or key to the Windows store. Done when: `AppTests.CertificatePromptUi` opens a file with a `.pfx` and finds the user's certificate store unchanged.
- [ ] Show a visible waiting state `"Waiting for your smart card or PIN..."` with a `Cancel` while the key operation runs, announced to assistive technology (`D03 T02 §5`), and map `PinCancelled`, `PrivateKeyUnavailable`, `NoMatchingCertificate` and `DecryptionFailed` to plain messages with a next step, returning to the dialog for another attempt. Done when: each code shows its message in `AppTests.CertificatePromptUi`.
- [ ] Handle the same code in `QueueCard` recovery (`D04 T01 §7`): a job whose input needs a certificate pauses as `NeedsCertificate`, shows the dialog, and resumes with the chosen certificate. Done when: a Convert job over a certificate-encrypted PDF completes after the choice and fails cleanly on cancel.
- [ ] Give every control an automation name and tooltip and make the list and buttons keyboard operable (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `CertificatePrompt.*` and `AppTests.CertificatePromptUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=CertificatePrompt.*:AppTests.CertificatePromptUi` passes.
- [ ] Commit: `app: add the certificate prompt for opening public-key encrypted PDFs`

**Test checkpoint:** `recto-tests.exe --gtest_filter=CertificatePrompt.*:AppTests.CertificatePromptUi` passes on the display-session host.

## 16. Split mode: split by file size and by top-level bookmarks

UPDF, PDFelement, Foxit PDF Editor and Acrobat Pro split by file size and by top-level bookmarks. This section adds both to the Split mode method selector of `§4` (`By placed breaks`, `Every N pages`, `One file per page`), driven by the engine plans of `D05 T01 §15`. The grid stays the truth: choosing a method places the breaks and fills the Parts panel, so the person sees which pages go where before anything is written, and can still edit names and move breaks. Bookmark titles become file names through the engine's Windows-safe sanitizer and stay editable. Saves run through `§5`; the source is never touched.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.6 (`Split` mode and Parts panel) with `design/screenshots/explorations/B-Organize.png`; the two added methods and the size field are new controls that follow the existing method selector and `NumberBox` styling.
**Job:** The user can split a PDF into files no larger than a size they type, or into one file per top-level bookmark, see the resulting parts and warnings first, and save them in one job. Consumer: `OrganizeEditModel` (`§1`, `§4`), `plan_split_by_size` and `plan_split_by_bookmarks` (`D05 T01 §15`), the save flow (`§5`).
**Treatment:** Two more entries in the method selector that call the engine plan on a worker thread (debounced `250 ms`), convert the plan to split breaks and part names in `OrganizeEditModel`, and show plan warnings as `InfoBar` rows. Cheaper substitute that fails the checkpoint: a size box that divides pages equally without the engine plan, or bookmark splitting that shows no preview and cannot rename parts.
**Chrome:** consume the shared `PageGrid` and Parts panel (`§1`, `§4`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Extend `OrganizeEditModel` with plan-driven breaks (`apply_split_plan`) and the two methods. Done when: `OrganizeUi.SplitBySizeModel` and `OrganizeUi.SplitByBookmarksModel` turn engine plans into the expected breaks and names with undo.
2. Build the controls, warnings and entry points. Done when: `AppTests.SplitMethodsUi` splits a fixture by size and by bookmarks and the saved files match the engine plan.

- [ ] Implement `apply_split_plan(SplitPlan)` in `src/services/organize_edit_model.cpp` converting a `SplitPlan` (`D05 T01 §15`) into split breaks, part colours and default names, keeping user-edited names keyed by start page as `§4` does, as one undoable step. Done when: `OrganizeUi.SplitBySizeModel` applies a plan and `undo()` restores the previous breaks.
- [ ] Add `By file size` and `By top-level bookmarks` to the method selector of the Parts panel (`OrganizePage.xaml`), the first with a `NumberBox` and a unit `ComboBox` (`KB`, `MB`, `GB`) and the second with a `Level` `NumberBox` and a front-matter choice (`Separate file`, `Attach to first part`, `Leave out`). Done when: switching methods swaps the option rows and preserves the other method's entries. Cheaper substitute: free-text size entry with no unit control.
- [ ] Run the plan on a worker thread with a `CancelToken` and show a progress ring while it computes, then fill the Parts panel; a plan failure leaves the previous breaks and shows the engine's message. Done when: changing the size cancels the previous plan and a refusal (`1 KB` limit) keeps the earlier parts.
- [ ] Show plan warnings inline: `OversizePart` on the part card with the page and its size, `BookmarksOutOfOrder` and `BookmarkUnresolved` in an `InfoBar` above the parts with counts, each with a text label as well as an icon (`SPEC 11`). Done when: `AppTests.SplitMethodsUi` shows each warning for the matching fixture.
- [ ] Keep part names editable with the live collision validation of `§4`, including the names sanitized from bookmark titles, and show non-Latin titles correctly. Done when: a CJK-titled outline fixture yields readable names and a duplicate edit disables `Save N files`.
- [ ] Register `organize.splitBySize` and `organize.splitByBookmarks` in `CommandRegistry` with synonyms `split by size`, `chapters`, `max size`, `split bookmarks`, opening Organize in Split mode with the method chosen. Done when: `Ctrl+K` "chapters" opens Split mode by bookmarks. Cheaper substitute: no command-box entry.
- [ ] Give every new control an automation name and tooltip and make the method rows keyboard operable (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `OrganizeUi.SplitBySize*`, `OrganizeUi.SplitByBookmarks*` and `AppTests.SplitMethodsUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=OrganizeUi.SplitBy*:AppTests.SplitMethodsUi` passes.
- [ ] Commit: `organize: split by file size and by top-level bookmarks in Split mode`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OrganizeUi.SplitBy*:AppTests.SplitMethodsUi` passes on the display-session host.

## 17. Split Page dialog: grid or draggable cut lines with a live preview

UPDF's, PDFelement's and Foxit PDF Editor's Split Page turns one page into several, for example a two-page scan spread; Acrobat Pro can only crop. This dialog drives `split_page` (`D05 T01 §16`). The person picks a grid or drags cut lines on a preview of the real page, chooses which pages to split, and decides whether the pieces keep the other pieces' content (default: removed, so each piece carries only its own text and pixels). The result is a copy through the save flow of `§5`.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new dialog; it follows `design/SPEC.md` section 9.9 (two-column `640 epx` live-preview dialog as in `Crop & Resize Pages`, `§6`) and adds `Split Page` to the section 9.6 tool list.
**Job:** The user can split pages into a grid or along cut lines they place, see the pieces, and save a copy. Consumer: `ConversionRunner` (`D04 T01 §1`), `split_page` (`D05 T01 §16`), the save flows of `§5`.
**Treatment:** A `640 epx` `ContentDialog` bound to `SplitPageDialogViewModel` with grid inputs on the left, a page preview with draggable and keyboard-nudgeable cut lines on the right, and a page stepper. Cheaper substitute that fails the checkpoint: numeric cut positions only with no preview, or cut lines that can only be moved with the mouse.
**Chrome:** consume the dialog host (`D02 T01 §10`), the preview and handle conventions of `CropAndResizeDialog` (`§6`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Implement `SplitPageDialogViewModel` and the preview with cut lines. Done when: `AppTests.SplitPageDialogUi` sets a `1 x 2` grid, nudges a cut line, and the preview shows the expected pieces.
2. Add scope, content-removal choice, save and entry points. Done when: `AppTests.SplitPageDialogUi` saves a copy whose pieces match the preview and whose source is byte-identical.

- [ ] Implement `SplitPageDialogViewModel` in `src/services/split_page_dialog_view_model.h/.cpp` building `PageSplitSpec` (`D05 T01 §16`) from the grid fields or the cut lines and validating with the engine's rules on each change. Done when: an invalid cut shows the engine's text and a valid one enables `Split`.
- [ ] Build the left column: `Rows` and `Cols` `NumberBox` (`1` to `10`), `Cut lines` list with a numeric position per line in `pt`, `mm` or `in`, `Reading order` (`Left to right`, `Right to left`), and `Pages` (`Current`, `Selected`, `All`, `Range`). Done when: editing a number moves the matching preview line.
- [ ] Build the right-column preview with draggable cut lines (`RectoAccentBrush`), a `1 epx` grid for the grid mode, arrow-key nudging of the focused line by `1 pt` (`10 pt` with `Shift`), and the page stepper. Done when: dragging a line updates its number and arrow keys move it. Cheaper substitute: mouse-only lines.
- [ ] Add the `Remove other pieces' content from each piece` toggle, on by default, with the one-line explanation `"Off keeps the other pieces' text and images hidden in each file."`. Done when: `AppTests.SplitPageDialogUi` saves with the toggle on and finds no text outside each piece in the output.
- [ ] Apply through `ConversionRunner` to a copy (default `<stem> (split pages).pdf`), show the Unsaved-Changes guard first when the Organize grid has edits, and offer `Open copy` and `Replace original` (`§5`). Done when: the source is byte-identical after `Save as copy`.
- [ ] Wire the entry points: Organize header and `OrganizeSelectionBar` `Split page...`, the viewer thumbnail context menu (`D02 T03 §4`), the `Tools` menu, and `organize.splitPage` in `CommandRegistry` with synonyms `divide page`, `cut page`, `split spread`; add `Split Page` to the section 9.6 tool list of `design/SPEC.md`. Done when: `Ctrl+K` "spread" opens the dialog. Cheaper substitute: no command-box entry.
- [ ] Give every control an automation name and tooltip and operate the whole dialog by keyboard (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `SplitPageDialog.*` and `AppTests.SplitPageDialogUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=SplitPageDialog.*:AppTests.SplitPageDialogUi` passes.
- [ ] Commit: `app: add the Split Page dialog with draggable cut lines`

**Test checkpoint:** `recto-tests.exe --gtest_filter=SplitPageDialog.*:AppTests.SplitPageDialogUi` passes on the display-session host.

## 18. N-up dialog: several pages on one sheet with a live sheet preview

PDFelement's and Foxit PDF Editor's N-up create a saved PDF with several pages per sheet. This dialog drives `make_nup` (`D05 T01 §17`); printing N-up stays in the print dialog (`D07 T01 §10`). The person picks rows and columns, sheet size and orientation, margins, order and border, sees sheet 1 of the result rendered from the real pages, and saves a new file. The open document is not changed.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new dialog; it follows `design/SPEC.md` section 9.9 (two-column `640 epx` live-preview dialog as in `Watermark & Stamp`, `§7`) and adds `N-up` to the section 9.6 tool list.
**Job:** The user can lay several pages on each sheet, see the sheet, and save the result as a new PDF. Consumer: `ConversionRunner` (`D04 T01 §1`), `make_nup` (`D05 T01 §17`), `§5` save flows.
**Treatment:** A `640 epx` `ContentDialog` bound to `NUpDialogViewModel`, with a grid picker and sheet controls on the left and a sheet preview with a sheet stepper on the right, rendered from `make_nup` on a worker thread for the first sheets. Cheaper substitute that fails the checkpoint: a form with no preview, or a preview drawn from placeholders instead of the real pages.
**Chrome:** consume the dialog host (`D02 T01 §10`), the preview pane conventions of `WatermarkDialog` (`§7`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Implement `NUpDialogViewModel` and the preview. Done when: `AppTests.NUpDialogUi` changes the grid, sheet and order and the preview shows the matching sheet each time.
2. Add save, scope and entry points. Done when: `AppTests.NUpDialogUi` saves a new file whose sheets match the preview.

- [ ] Implement `NUpDialogViewModel` in `src/services/nup_dialog_view_model.h/.cpp` building `NUpOptions` (`D05 T01 §17`) from UI state and rendering a debounced (`250 ms`) preview of sheet `k` by running `make_nup` on the first `rows * cols * 3` pages to a memory stream. Done when: every property change produces a new preview bitmap and a refused value shows the engine's text.
- [ ] Build the left column: grid presets (`2`, `4`, `6`, `9` per sheet) plus custom `Rows` and `Cols`, `Sheet size`, `Orientation` (`Automatic`, `Portrait`, `Landscape`), `Margin`, `Gap`, `Order`, `Border` toggle with width, `Repeat each page` toggle and `Pages` scope. Done when: switching a preset updates the numbers and the preview.
- [ ] Build the right-column sheet preview with a `< Prev sheet` / `Sheet k of M` / `Next sheet >` stepper and a one-line note when links, tags or bookmarks of the source are not carried. Done when: the note shows `links_dropped` from the result.
- [ ] Apply through `ConversionRunner` to a new file (default `<stem> (N-up).pdf`) and offer `Open file`; the source document is never replaced by this operation. Done when: `AppTests.NUpDialogUi` leaves the source byte-identical. Cheaper substitute: offering `Replace original`.
- [ ] Wire the entry points: Organize header `N-up...`, the viewer `Tools` menu, and `organize.nup` in `CommandRegistry` with synonyms `pages per sheet`, `multiple pages per page`, `imposition`; add `N-up` to the section 9.6 tool list of `design/SPEC.md`. Done when: `Ctrl+K` "pages per sheet" opens the dialog. Cheaper substitute: no command-box entry.
- [ ] Give every control an automation name and tooltip and operate the whole dialog by keyboard (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `NUpDialog.*` and `AppTests.NUpDialogUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=NUpDialog.*:AppTests.NUpDialogUi` passes.
- [ ] Commit: `app: add the N-up dialog with a live sheet preview`

**Test checkpoint:** `recto-tests.exe --gtest_filter=NUpDialog.*:AppTests.NUpDialogUi` passes on the display-session host.

## 19. Reverse, swap, interleave and bookmark-order commands in Organize

UPDF, PDFelement and Foxit PDF Editor expose Reverse Order, Swap Odd and Even, and Interleave as one-click page commands. In Recto the Organize grid is the truth (`§1`), so Reverse and Swap act on the grid model on the selection or all pages with full undo and a live change summary, and Interleave is an order choice in Combine mode (`§3`) that arranges the pages of sources `A` and `B` as `A1, B1, A2, B2` with an option to reverse `B` for two-pass duplex scans. The engine functions of `D05 T01 §18` serve the CLI and Action Wizard; a test proves the UI result equals the engine output so the two cannot drift.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.6 (`Organize` header, selection action bar, Combine header) with `design/screenshots/D-Organize.png` and `design/screenshots/explorations/A-Organize.png`; the commands extend the existing overflow menu and the Combine header with a segmented `Order` control.
**Job:** The user can reverse the page order, swap odd and even pages, or interleave two files in one or two clicks, see the result in the grid, and undo it. Consumer: `OrganizeEditModel` (`§1`), the Combine mode (`§3`), `interleave_pdfs`, `swap_odd_even` and `reverse_pages` (`D05 T01 §18`).
**Treatment:** Model operations `reverse_range`, `swap_odd_even_range` and `interleave_sources` that rewrite the ordered item list as one undoable step, plus the menu items and a Combine `Order` segmented control. Cheaper substitute that fails the checkpoint: reordering by dragging page by page, or a UI order that differs from the engine's.
**Chrome:** consume the Organize header overflow menu and `OrganizeSelectionBar` (`§2`), the Combine header (`§3`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Implement the three model operations with undo and change-summary integration. Done when: `OrganizeUi.ReverseSwapInterleave` verifies the sequences, undo and the summary text.
2. Add the controls, entry points and the engine-equality test. Done when: `AppTests.OrganizeOrderCommandsUi` runs each command and `OrganizeUi.EngineEquality` shows the saved output equals the engine's.

- [ ] Implement `reverse_range`, `swap_odd_even_range` and `interleave_sources(a, b, reverse_b, allow_unequal)` in `src/services/organize_edit_model.cpp`, each as one undo step acting on the selection or, with no selection, on all pages, and refusing unequal counts with the engine's message unless allowed. Done when: `OrganizeUi.ReverseSwapInterleave` covers odd and even counts, a selection, and the duplex case.
- [ ] Count the result in `compute_change_summary()` (`§2`) as moved pages so the header says `"6 moved"` and returns to `"No changes"` after undo. Done when: `OrganizeUi.ReverseSwapInterleave` checks the summary.
- [ ] Add `Reverse order` and `Swap odd and even pages` to the Organize header overflow menu and the selection bar, with automation names and tooltips. Done when: both commands act from the pointer and the keyboard. Cheaper substitute: commands reachable only from `Ctrl+K`.
- [ ] Add an `Order` segmented control to the Combine header (`One after another`, `Alternate A and B`, `Alternate A and B, B reversed`) enabled when exactly two sources are loaded, reflowing the grid through `interleave_sources`. Done when: `AppTests.OrganizeOrderCommandsUi` combines a fronts file and a backs file and the grid shows fronts and backs alternating.
- [ ] Prove UI and engine agree: compile the saved Combine output and compare it pixel for pixel with `interleave_pdfs` on the same inputs, and the Reverse and Swap outputs with `reverse_pages` and `swap_odd_even`. Done when: `OrganizeUi.EngineEquality` passes for all three.
- [ ] Add `Order pages by bookmarks` (`D05 T01 §21`) to the Organize header overflow menu: run `order_pages_by_bookmarks` with its dry run on a worker thread, show the new order as a preview strip with the count of moved pages and any unresolved bookmarks, and on `Apply` rewrite the grid as one undoable step; register `organize.orderByBookmarks` with synonyms `follow outline`, `sort by bookmarks`. Done when: `OrganizeUi.OrderByBookmarks` applies, undoes and equals the engine output, and `Ctrl+K` "follow outline" opens the preview. Cheaper substitute: a CLI-only operation.
- [ ] Register `organize.reverse`, `organize.swapOddEven` and `organize.interleave` in `CommandRegistry` with synonyms `flip order`, `backwards`, `odd even`, `duplex scan`, `front and back`. Done when: `Ctrl+K` "duplex scan" opens Combine with the alternate order. Cheaper substitute: no command-box entry.
- [ ] Add `OrganizeUi.ReverseSwapInterleave`, `OrganizeUi.EngineEquality`, `OrganizeUi.OrderByBookmarks` and `AppTests.OrganizeOrderCommandsUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=OrganizeUi.ReverseSwapInterleave:OrganizeUi.EngineEquality:OrganizeUi.OrderByBookmarks:AppTests.OrganizeOrderCommandsUi` passes.
- [ ] Commit: `organize: reverse, swap odd and even, and interleave commands`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OrganizeUi.ReverseSwapInterleave:OrganizeUi.EngineEquality:OrganizeUi.OrderByBookmarks:AppTests.OrganizeOrderCommandsUi` passes on the display-session host.

## 20. Page Labels dialog: ranges, styles, prefixes and a live label preview

Acrobat Pro, Foxit PDF Editor, PDFelement and UPDF all let a person set page labels (`i, ii, iii` then `1, 2, 3`, or `A-1`). This dialog edits the ranges of `D05 T01 §19` on a copy, with a table of ranges on the left and a strip of the resulting labels for every page on the right, so the result is visible before saving. The Organize grid shows each page's label under its physical number whenever labels exist. The labels live in the file's `/PageLabels`, so what the dialog shows is read back from the stored tree.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new dialog; it follows `design/SPEC.md` section 9.9 form-dialog chrome (`ContentDialog`, radius 20, `28 epx` padding) and the card grid of section 9.6, and adds `Page Labels` to the section 9.6 tool list.
**Job:** The user can define label ranges, see every page's resulting label, and save a copy with the labels. Consumer: `ConversionRunner` (`D04 T01 §1`), `set_page_labels` and `build_label_preview` (`D05 T01 §19`), the save flows of `§5`.
**Treatment:** A `ContentDialog` bound to `PageLabelsDialogViewModel` with an editable range table and a virtualized preview list of `physical page -> label`. Cheaper substitute that fails the checkpoint: a single prefix and start field with no ranges, or a preview that is not read from the engine formatter.
**Chrome:** consume the dialog host (`D02 T01 §10`), the thumbnail tile pipeline (`D02 T03 §4`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Implement `PageLabelsDialogViewModel` and the range table with the live preview. Done when: `AppTests.PageLabelsDialogUi` adds a Roman range and a decimal range and the preview shows `i, ii, iii, 1, 2`.
2. Add card labels in the grid, save and entry points. Done when: `AppTests.PageLabelsDialogUi` saves a copy and reads the labels back from the file.

- [ ] Implement `PageLabelsDialogViewModel` in `src/services/page_labels_dialog_view_model.h/.cpp` loading `read_page_labels`, validating edits with the engine's rules (first range at page `1`, ascending, within the page count, start of at least `1`) and producing the preview with `build_label_preview`. Done when: an invalid edit shows the engine's text and disables `Apply`.
- [ ] Build the range table: `From page`, `Style` (`None`, `1, 2, 3`, `i, ii, iii`, `I, II, III`, `a, b, c`, `A, B, C`), `Prefix`, `Start at`, with `Add range` and `Remove`, each cell keyboard operable. Done when: adding and removing ranges updates the preview immediately.
- [ ] Build the preview list with a `page k -> label` row per page (virtualized) and a toggle `Show only range starts`. Done when: a `2000`-page fixture scrolls smoothly.
- [ ] Show the label under the page number on each Organize grid card and the viewer-agnostic helper `format_page_caption(page, label)` (`"4 (iv)"`) whenever the document has labels. Done when: `AppTests.PageLabelsDialogUi` finds the captions on cards.
- [ ] Apply through `ConversionRunner` to a copy with the Replace-original and Unsaved-Changes behaviour of `§5`, and offer `Remove all labels`. Done when: the source is byte-identical after `Save as copy`. Cheaper substitute: writing the labels into the open document in place.
- [ ] Wire the entry points: Organize header `Page labels...`, the viewer `Tools` menu and `organize.pageLabels` in `CommandRegistry` with synonyms `page numbers`, `roman numerals`, `numbering`; add `Page Labels` to the section 9.6 tool list of `design/SPEC.md`. Done when: `Ctrl+K` "roman numerals" opens the dialog. Cheaper substitute: no command-box entry.
- [ ] Give every control an automation name and tooltip and operate the table by keyboard (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `PageLabelsDialog.*` and `AppTests.PageLabelsDialogUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=PageLabelsDialog.*:AppTests.PageLabelsDialogUi` passes.
- [ ] Commit: `app: add the Page Labels dialog with a live label preview`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PageLabelsDialog.*:AppTests.PageLabelsDialogUi` passes on the display-session host.

## 21. Blank page review: find, check and remove blank pages from the grid

PDFelement and UPDF delete blank pages automatically; Recto shows its evidence first. This section adds a `Find blank pages` review to Organize on top of `detect_blank_pages` (`D05 T01 §20`): a sensitivity choice, a list of flagged pages as thumbnails with the reason, a checkbox per page, and a button that selects them in the grid or removes them into the edit model so the normal undo and unsaved-change summary apply. Nothing is deleted from the file until the person saves.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new dialog; it follows `design/SPEC.md` section 9.9 form-dialog chrome and the card grid of section 9.6, and adds `Find Blank Pages` to the section 9.6 tool list.
**Job:** The user can find blank pages, judge each from its thumbnail and reason, and remove the chosen ones as an undoable edit. Consumer: `OrganizeEditModel` (`§1`), `detect_blank_pages` and `plan_blank_page_deletion` (`D05 T01 §20`).
**Treatment:** A `ContentDialog` bound to `BlankPagesViewModel` listing findings with thumbnails and reasons, running detection on a worker thread with cancel, and two actions (`Select in grid`, `Remove from document`). Cheaper substitute that fails the checkpoint: a one-click delete with no list and no reasons.
**Chrome:** consume the dialog host (`D02 T01 §10`), the thumbnail tile pipeline (`D02 T03 §4`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Implement `BlankPagesViewModel` and the review list. Done when: `AppTests.BlankPagesUi` detects the fixture's blank pages at each sensitivity and lists reasons.
2. Add the grid actions and entry points. Done when: `AppTests.BlankPagesUi` removes the chosen pages, undoes, and saves a copy without them.

- [ ] Implement `BlankPagesViewModel` in `src/services/blank_pages_view_model.h/.cpp` running `detect_blank_pages` on a worker thread with a `CancelToken` and progress, exposing findings, the `Sensitivity` choice and the two policy toggles (`Treat pages with comments as blank`, `Treat hidden text as content`). Done when: changing the sensitivity reruns detection and cancels the previous run.
- [ ] Build the review list: thumbnail, page number, verdict badge with text (`Blank`, `Nearly blank`), reason, and a checkbox that defaults on for `Blank` and off for `Nearly blank`. Done when: `AppTests.BlankPagesUi` shows each badge and reason.
- [ ] Implement `Select in grid` and `Remove from document` in `OrganizeEditModel` through `plan_blank_page_deletion`, refusing to remove every page, as one undo step with the change summary counting deleted pages. Done when: `OrganizeUi.BlankPageRemoval` removes, undoes and shows `"3 deleted"` then `"No changes"`. Cheaper substitute: deleting from the file at once.
- [ ] Wire `Find blank pages...` into the Organize header, the viewer `Tools` menu and `organize.findBlankPages` in `CommandRegistry` with synonyms `empty pages`, `delete blank`, `remove blank`; add `Find Blank Pages` to the section 9.6 tool list of `design/SPEC.md`. Done when: `Ctrl+K` "empty pages" opens the review. Cheaper substitute: no command-box entry.
- [ ] Give every control an automation name and tooltip and operate the list by keyboard (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `BlankPagesUi.*`, `OrganizeUi.BlankPageRemoval` and `AppTests.BlankPagesUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=OrganizeUi.BlankPageRemoval:AppTests.BlankPagesUi` passes.
- [ ] Commit: `app: add blank page review to Organize`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OrganizeUi.BlankPageRemoval:AppTests.BlankPagesUi` passes on the display-session host.

## 22. Drag pages between open documents and out to Explorer

UPDF lets a person drag pages from one document to another and out to a folder as a new PDF. This section makes the Organize grid a drag source and target. Dropping on another document's Organize grid inserts the pages as a pending, undoable edit (as a new source in that document's model, `§3`); dragging to Explorer or the desktop writes a new PDF of the dragged pages in a private temp folder only when a target first asks for the data, and removes it when the drag ends without a completed drop. Permission flags are honoured: a document whose permissions forbid assembling pages or extracting content cannot be dragged out, and an encrypted document is never written out unencrypted.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.6 (`Organize` grid and the drop indicator of `D-Organize.png`); cross-document and Explorer drag are genuinely new interactions that reuse the existing drop-insertion indicator and source chips.
**Job:** The user can move or copy pages between open documents and out to a folder by dragging, without any file being modified until they save. Consumer: `OrganizeEditModel` (`§1`, `§3`), `extract_pages` (`D05 T01 §3`), `DocumentActor` permissions (`D01 T02 §8`).
**Treatment:** A drag source on selected cards that offers a private in-process `Recto.PageSet` format and a lazily produced `StorageItems` file (created on first request, removed if the drag ends without a drop), and a drop target on grids and tab headers that inserts the pages as a new source. Cheaper substitute that fails the checkpoint: writing a temp PDF for every drag start (slow and leaves files), or dropping pages straight into the other open document in place.
**Chrome:** consume the drop indicator (`§2`), source chips (`§3`), the tab strip (`D02 T01 §3`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Implement the drag payload, the in-process drop and the permission checks. Done when: `OrganizeUi.CrossDocumentDrop` drags two pages from document `A` to the grid of document `B` and `B`'s model shows them as a new source with undo.
2. Implement the Explorer drop and the lifecycle of the temp files. Done when: `AppTests.DragToExplorer` drops pages onto a folder and the new PDF contains exactly those pages.

- [ ] Start a drag from selected `PageGrid` cards (`§1`) with a `DataPackage` offering `Recto.PageSet` (a document session id and page indices, valid only inside the process), a rendered drag image of up to three stacked thumbnails with a count badge, and `StorageItems` supplied by a data provider that runs on first request. Done when: starting a drag creates no file on disk.
- [ ] Implement `OrganizeEditModel::insert_pages_from(source_ref, indices, position)` adding the pages as a new source (`§3` chips and colours) at the drop position as one undo step, with the source document opened by its own `pdf::Document` for thumbnails. Done when: `OrganizeUi.CrossDocumentDrop` shows the pages, the new chip, and undo removing them.
- [ ] Accept the drop on another document's Organize grid (insertion bar at the gap), on a tab header (opens Organize for that document with the pages inserted at the end), and in the same grid (reorder as `§2`); show a polite message when the target document is read-only or has no Organize mode. Done when: `AppTests.CrossDocumentDropUi` covers the three targets.
- [ ] Supply the Explorer data through a `DataProvider` that runs when a target requests it (which can happen before any drop): the first request runs `extract_pages` (`D05 T01 §3`) once to `AppPaths::temp_dir()/drag/<guid>/<source stem> (pages 3-5).pdf` through `AtomicFileWriter` and later requests reuse that file; when the drag operation ends cancelled or with no copy, delete the folder at once, and after a completed transfer delete it at app exit and at next launch; nothing is ever written outside the temp folder by the provider. Done when: `AppTests.DragToExplorer` drops onto a folder and finds the new PDF with the dragged pages, `AppTests.DragCancelled` has a target request the data and then cancel and finds the temp folder removed and no file in any user folder, and the temp folder is gone after exit.
- [ ] Honour permissions: disable dragging out of the app when the document forbids assembling pages or copying content, and when the document is encrypted show `"Pages cannot be dragged out of a password-protected document. Use Extract."` instead of writing unencrypted pages; in-app drops follow the same permission check. Done when: `OrganizeUi.DragPermissions` covers a restricted and an encrypted fixture.
- [ ] Support keyboard parity: `Ctrl+C` on selected cards and `Ctrl+V` in another document's Organize grid copy the pages through the same in-process path, and a `Copy to document...` command lists open documents. Done when: `OrganizeUi.KeyboardCopyParity` yields the same model state as the pointer drop. Cheaper substitute: pointer-only drag.
- [ ] Give the drag source and targets automation names and announce results (`"2 pages added from report.pdf"`) to assistive technology (`D03 T02 §5`). Done when: the control inventory test passes and the announcement is produced.
- [ ] Add `OrganizeUi.CrossDocumentDrop`, `OrganizeUi.DragPermissions`, `OrganizeUi.KeyboardCopyParity`, `AppTests.CrossDocumentDropUi`, `AppTests.DragToExplorer` and `AppTests.DragCancelled` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=OrganizeUi.CrossDocumentDrop:OrganizeUi.DragPermissions:OrganizeUi.KeyboardCopyParity:AppTests.CrossDocumentDropUi:AppTests.DragToExplorer:AppTests.DragCancelled` passes.
- [ ] Commit: `organize: drag pages between open documents and out to Explorer`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OrganizeUi.CrossDocumentDrop:OrganizeUi.DragPermissions:OrganizeUi.KeyboardCopyParity:AppTests.CrossDocumentDropUi:AppTests.DragToExplorer:AppTests.DragCancelled` passes on the display-session host.

## 23. Copy a page as an image from the Organize grid

UPDF copies a page as an image to the clipboard. Selecting one page card and choosing `Copy page as image` (or `Ctrl+Shift+C` on the focused card) renders that page at a chosen resolution and puts a bitmap on the clipboard, so it can be pasted into a chat or a document. The render uses the same tile rasterizer as the viewer on a worker thread and is capped so a huge page cannot exhaust memory. The region snapshot tool belongs to the text domain; this command copies whole pages.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.6 (`Organize` selection action bar and card context menu); a small new command that follows the existing menu items.
**Job:** The user can copy one page as an image and paste it elsewhere. Consumer: `RenderPool` (`D01 T02 §9`), the Windows clipboard `DataPackage`.
**Treatment:** A command that renders the selected page at the setting `organize.copyImageDpi` (default `150`) with the `4096 x 4096` tile limit lifted only by stitching bounded bands, limited to `100` megapixels, and sets `DataPackage::SetBitmap`. Cheaper substitute that fails the checkpoint: copying the grid thumbnail, which is low resolution.
**Chrome:** consume the selection bar and card menu (`§2`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Implement `render_page_bitmap(doc, page, dpi, limit)` and the clipboard command. Done when: `OrganizeUi.CopyPageAsImage` renders page 2 at `150 DPI` with the expected pixel size and sets a bitmap on a test clipboard.
2. Add the controls, setting and entry points. Done when: `AppTests.CopyPageAsImageUi` copies from the menu and from `Ctrl+Shift+C`.

- [ ] Implement `render_page_bitmap` in `src/services/page_image_renderer.h/.cpp` rendering through `RenderPool` in bands, honouring page rotation and `/UserUnit`, refusing a result over `100` megapixels by lowering the DPI and telling the person the DPI used. Done when: `OrganizeUi.CopyPageAsImage` covers a normal page, a rotated page and an oversize page that is lowered.
- [ ] Implement `Copy page as image` on the selection bar, the card context menu and `Ctrl+Shift+C`, enabled for exactly one selected card (with the reason `"Select one page."` otherwise), showing a toast when done. Done when: the command copies a bitmap whose pixels equal a render of that page.
- [ ] Respect permissions: disabled with a stated reason when the document forbids copying content. Done when: `OrganizeUi.CopyPageAsImage` covers a restricted fixture.
- [ ] Add the setting `organize.copyImageDpi` (`72` to `600`, default `150`) read by the command, registered in `SettingsStore` through the settings pattern of `D02 T04 §2`. Done when: changing the setting changes the pixel size of the next copy.
- [ ] Register `organize.copyPageAsImage` in `CommandRegistry` with synonyms `screenshot page`, `page to clipboard`, `copy as picture`. Done when: `Ctrl+K` "copy as picture" runs it. Cheaper substitute: no command-box entry.
- [ ] Give the command an automation name and tooltip (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `OrganizeUi.CopyPageAsImage` and `AppTests.CopyPageAsImageUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=OrganizeUi.CopyPageAsImage:AppTests.CopyPageAsImageUi` passes.
- [ ] Commit: `organize: copy a page as an image`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OrganizeUi.CopyPageAsImage:AppTests.CopyPageAsImageUi` passes on the display-session host.

## 24. Combine mode: table of contents page and a rebuildable binder project

Foxit PDF Editor's PDF Binder combines files with a generated table of contents and a saved project. This section adds both to Combine mode (`§3`) as options rather than a fourth mode, driven by `build_binder` (`D05 T01 §30`): a `Create table of contents page` toggle with its options, `Save binder project...`, `Open binder project...`, and a changed-sources notice that offers `Rebuild`. The grid remains the truth for page order; the project stores the files and options, never passwords.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.6 (`Combine` header and source chips) with `design/screenshots/explorations/A-Organize.png`; the table-of-contents options flyout and project commands are new controls that follow the Combine header styling.
**Job:** The user can combine files with a linked table of contents, save the setup as a project, and rebuild it later when a source file changes. Consumer: `OrganizeEditModel` (`§3`), `build_binder` and `check_binder` (`D05 T01 §30`), the save flow (`§5`).
**Treatment:** A flyout on the Combine header holding the table options, project commands in the header overflow, and a notice bar when `check_binder` reports changed or missing sources. Cheaper substitute that fails the checkpoint: a plain merge with a separate text list, or a project that stores passwords.
**Chrome:** consume the Combine header and source chips (`§3`), `InfoBar`, `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Implement the binder view model, table options and project save and open. Done when: `OrganizeUi.BinderModel` saves a project from a three-source Combine and reopens it to the same sources and options.
2. Add the build, check and rebuild flow and entry points. Done when: `AppTests.BinderUi` builds a binder with a table of contents, edits a source, sees the changed notice and rebuilds to a new file.

- [ ] Implement `BinderViewModel` in `src/services/binder_view_model.h/.cpp` mapping the Combine model to a `BinderProject` (`D05 T01 §30`) and back, writing the grid's exact order, rotations and deletions as `page_sequence` so interleaved and reordered grids rebuild identically, with relative paths where possible and no passwords. Done when: `OrganizeUi.BinderModel` round-trips an interleaved, reordered and rotated three-source grid to the same page sequence and finds no password text in the file.
- [ ] Build the `Table of contents` flyout on the Combine header: toggle, title, depth, dot leaders, font and size, with a note that the table pages are labelled `i, ii` and the body starts at `1`. Done when: toggling the table changes the build input and the page count shown.
- [ ] Add `Save binder project...` and `Open binder project...` (`.rectobinder`) to the header overflow, using `FileSavePicker` and `FileOpenPicker`, and open a project by double click or `Ctrl+O` association through `OpenDocumentPipeline`. Done when: `AppTests.BinderUi` saves and reopens a project.
- [ ] Prompt for the password of each encrypted source through `PasswordDialog` (`D02 T01 §10`) on open and build, keeping passwords in memory only. Done when: `AppTests.BinderUi` builds a project with one encrypted source after the prompt.
- [ ] Run `check_binder` when a project opens and show an `InfoBar` listing changed, missing and new sources with `Rebuild`, `Update list` and `Dismiss`; `Rebuild` writes a new file, never replacing the previous output or any source. Done when: editing a source shows `"1 source changed"` and rebuilding leaves every earlier file byte-identical. Cheaper substitute: silently overwriting the previous output.
- [ ] Register `organize.binderNew`, `organize.binderOpen` and `organize.binderRebuild` in `CommandRegistry` with synonyms `binder`, `table of contents`, `combine with contents`. Done when: `Ctrl+K` "binder" lists the three. Cheaper substitute: no command-box entry.
- [ ] Give every control an automation name and tooltip and operate the flyout by keyboard (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `OrganizeUi.BinderModel` and `AppTests.BinderUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=OrganizeUi.BinderModel:AppTests.BinderUi` passes.
- [ ] Commit: `organize: table of contents and binder projects in Combine mode`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OrganizeUi.BinderModel:AppTests.BinderUi` passes on the display-session host.

## 25. ID Card Copy dialog: front and back images on one page

UPDF's ID card copy as a dialog over `make_id_card_copy` (`D05 T01 §23`). The person chooses the front and back photos (file pickers or drag and drop), adjusts a crop on each, picks the card and paper size, sees the page, and saves a new one-page PDF. Everything is local; no image is uploaded.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new dialog; it follows `design/SPEC.md` section 9.9 (two-column `640 epx` live-preview dialog as in `Crop & Resize Pages`, `§6`) and adds `ID Card Copy` to the section 9.6 tool list.
**Job:** The user can make a one-page copy of an identity card from two photos at true size and save it. Consumer: `ConversionRunner` (`D04 T01 §1`), `make_id_card_copy` (`D05 T01 §23`).
**Treatment:** A `640 epx` `ContentDialog` with two image slots, per-image crop handles, size controls and a page preview. Cheaper substitute that fails the checkpoint: a file list that stacks images at their pixel size, or no crop.
**Chrome:** consume the dialog host (`D02 T01 §10`), the crop handle conventions of `CropAndResizeDialog` (`§6`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Implement `IdCardCopyViewModel` with image slots, crop and the page preview. Done when: `AppTests.IdCardCopyUi` loads two fixtures, crops the front and sees the page preview at card size.
2. Add save and entry points. Done when: `AppTests.IdCardCopyUi` saves a PDF whose placed rectangles match the preview.

- [ ] Implement `IdCardCopyViewModel` in `src/services/id_card_copy_view_model.h/.cpp` building `IdCardCopyOptions` (`D05 T01 §23`) and rendering the page preview through the engine on a worker thread. Done when: every change updates the preview and an unreadable image shows the engine's message naming the file.
- [ ] Build two image slots (`Front`, `Back`) with `Choose image...`, drag and drop, orientation handled automatically, and eight-handle crop overlays that are keyboard adjustable. Done when: dropping a phone photo shows it upright with a crop box.
- [ ] Build the size controls (`Card size`, `Paper`, `Gap`, `Border`) and the page preview showing the true-size layout. Done when: changing the card size to `ID-3` moves the cards as in the engine.
- [ ] Save through `ConversionRunner` as a new file (default `ID card copy.pdf`), offering `Open file`, and warn that the output contains the images the person chose. Done when: `AppTests.IdCardCopyUi` leaves the source images untouched. Cheaper substitute: overwriting an existing file without asking.
- [ ] Wire the entry points: Organize header `ID card copy...`, the Home screen tools row, and `organize.idCardCopy` in `CommandRegistry` with synonyms `id copy`, `identity card`, `licence copy`; add `ID Card Copy` to the section 9.6 tool list of `design/SPEC.md`. Done when: `Ctrl+K` "identity card" opens the dialog. Cheaper substitute: no command-box entry.
- [ ] Give every control an automation name and tooltip and operate the whole dialog by keyboard (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `IdCardCopyDialog.*` and `AppTests.IdCardCopyUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=IdCardCopyDialog.*:AppTests.IdCardCopyUi` passes.
- [ ] Commit: `app: add the ID Card Copy dialog`

**Test checkpoint:** `recto-tests.exe --gtest_filter=IdCardCopyDialog.*:AppTests.IdCardCopyUi` passes on the display-session host.

## 26. Paging Seal dialog: a seal image split across page edges

PDFelement's cross-page seal as a dialog over `add_paging_seal` (`D05 T01 §22`). The person picks a seal image, the edge, the pages and the size, sees the slices on a strip of page previews that shows how they line up, and saves a copy. A note states that a paging seal is a visual aid and not a digital signature. Existing seals can be listed and removed through the Backgrounds dialog's `Existing marks` tab (`§13`).

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new dialog; it follows `design/SPEC.md` section 9.9 (two-column `640 epx` live-preview dialog as in `Watermark & Stamp`, `§7`) and adds `Paging Seal` to the section 9.6 tool list.
**Job:** The user can place a seal across the edges of several pages and see the result before saving. Consumer: `ConversionRunner` (`D04 T01 §1`), `add_paging_seal` (`D05 T01 §22`), `§13`.
**Treatment:** A `640 epx` `ContentDialog` with image, edge, page-scope, size and anchor controls and a preview strip of the first pages with their slices, plus a joined-seal inset. Cheaper substitute that fails the checkpoint: one preview page that never shows how the slices relate.
**Chrome:** consume the dialog host (`D02 T01 §10`), the preview conventions of `WatermarkDialog` (`§7`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Implement `PagingSealViewModel` and the preview strip. Done when: `AppTests.PagingSealUi` loads a seal, picks the right edge and `5` pages and sees five slices that sum to the seal.
2. Add apply, list and remove integration, and entry points. Done when: `AppTests.PagingSealUi` applies, then removes the seal through `§13`.

- [ ] Implement `PagingSealViewModel` in `src/services/paging_seal_view_model.h/.cpp` building `PagingSealOptions` (`D05 T01 §22`) and rendering the preview strip with a joined-seal inset. Done when: each property change updates the strip and a refused value shows the engine's text.
- [ ] Build the controls: image picker with a transparent-background hint, `Edge` (`Right`, `Left`, `Top`, `Bottom`), `Pages` (`All`, `Range`, `Selected`) with the minimum-two-pages rule, `Width`, `Anchor` slider and `Opacity`. Done when: switching the edge rotates the strip layout.
- [ ] Show the note `"A paging seal is a visual aid. It is not a digital signature."` and an `Apply` button that submits through `ConversionRunner` to a copy with the `§5` Replace-original behaviour. Done when: the source is byte-identical after `Save as copy`. Cheaper substitute: presenting the seal as a signature.
- [ ] List applied seals in the `Existing marks` tab of the Backgrounds dialog (`§13`) with `Remove`, using `list_marks` and `remove_mark` for kind `PagingSeal`. Done when: `AppTests.PagingSealUi` removes a seal and finds the pages unsealed.
- [ ] Wire the entry points: Organize header `Paging seal...`, the viewer `Tools` menu and `organize.pagingSeal` in `CommandRegistry` with synonyms `cross page seal`, `edge seal`, `riding seal`; add `Paging Seal` to the section 9.6 tool list of `design/SPEC.md`. Done when: `Ctrl+K` "edge seal" opens the dialog. Cheaper substitute: no command-box entry.
- [ ] Give every control an automation name and tooltip and operate the whole dialog by keyboard (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `PagingSealDialog.*` and `AppTests.PagingSealUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=PagingSealDialog.*:AppTests.PagingSealUi` passes.
- [ ] Commit: `app: add the Paging Seal dialog`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PagingSealDialog.*:AppTests.PagingSealUi` passes on the display-session host.

## 27. Redaction Bar: saved profiles, country packs and a match review list

Foxit PDF Editor and PDFelement save redaction patterns and detect personal data by country. This section adds to the `Find & Redact` menu of the Redaction Bar (`§9`): a `Profiles` submenu (apply, save current, manage, import, export, from `D05 T01 §26`), country pack checkboxes (`D05 T01 §25`), and a review list of matches with validation and confidence, `Low` confidence unticked, so nothing is marked without the person's review. Matches become pending marks like any other; applying still goes through the confirmation of `§9`.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 8.2 (`Redaction & Sanitize Bar`) and 9.9 (dialogs); the review list and profile manager follow the Sanitize dialog and `FindCard` styling and are new controls.
**Job:** The user can find personal data by country pack or saved profile, review each match, and mark the accepted ones for redaction. Consumer: `RedactionViewModel` (`§9`), `PiiDetector` (`D05 T01 §25`), `RedactionProfileStore` (`D05 T01 §26`).
**Treatment:** A review panel that lists `PiiMatch`es and profile matches with page, text, pack, validation badge and confidence, with `Mark selected` adding `RedactionMark`s as undoable pending marks. Cheaper substitute that fails the checkpoint: marking every match automatically, or listing only the six regular-expression presets.
**Chrome:** consume `RedactionBar` and `RedactionViewModel` (`§9`), `FindCard` (`D03 T01 §6`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Extend `RedactionViewModel` with pack scans, profile resolution and the review list. Done when: `RedactionUi.PacksAndProfiles` scans a fixture with a pack, resolves a profile and marks only the ticked matches.
2. Build the review panel, profile manager and entry points. Done when: `AppTests.RedactionProfilesUi` saves a profile, applies it, reviews matches and marks them.

- [ ] Extend `RedactionViewModel` (`src/services/redaction_view_model.h/.cpp`) with `scan_packs(pack_ids)` and `apply_profile(profile)` running `PiiDetector` and `resolve_profile` on a worker thread with a `CancelToken`, producing review rows (not marks) and, on `Mark selected`, undoable pending marks. Done when: `RedactionUi.PacksAndProfiles` shows no marks before acceptance and one undo step after.
- [ ] Build the review list: columns `Mark`, `Page`, `Text` (with the matched text masked to its first and last characters until the row is focused), `Kind`, `Validation` (text badge, not colour alone) and `Confidence`, with `Select all high confidence` and `Clear`. Done when: `Low` rows are unticked and the masking toggles on focus.
- [ ] Add country pack checkboxes to the `Find & Redact` menu grouped by region, remembering the last selection in `SettingsStore`, and `Scan` with a progress ring and cancel. Done when: scanning a multi-page fixture can be cancelled and leaves no marks.
- [ ] Build the `Profiles` submenu and a `Manage profiles` dialog: apply, `Save current patterns as profile...`, rename, delete (user profiles only), `Import...` and `Export...` through file pickers, and show the shipped read-only profiles. Done when: `AppTests.RedactionProfilesUi` saves, exports, deletes and re-imports a profile.
- [ ] Show profile load errors from the parser (`D05 T01 §26`) plainly, naming the field, and never apply a profile that failed to parse. Done when: a malformed profile file shows its error and changes nothing.
- [ ] Register `redact.scanPii`, `redact.applyProfile` and `redact.manageProfiles` in `CommandRegistry` with synonyms `find personal data`, `redaction profile`, `iban`, `national id`. Done when: `Ctrl+K` "personal data" opens the scan. Cheaper substitute: no command-box entry.
- [ ] Give every control an automation name and tooltip and operate the review list by keyboard (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `RedactionUi.PacksAndProfiles` and `AppTests.RedactionProfilesUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=RedactionUi.PacksAndProfiles:AppTests.RedactionProfilesUi` passes.
- [ ] Commit: `app: redaction profiles, country packs and a match review list`

**Test checkpoint:** `recto-tests.exe --gtest_filter=RedactionUi.PacksAndProfiles:AppTests.RedactionProfilesUi` passes on the display-session host.

## 28. Redaction Bar: suggested marks, whiteout and whole-page redaction

This section completes the Redaction Bar (`§9`) in three ways. First, a `Suggested` mark state: other features (the AI assistant's personal-data detection, `D08 T02`) can propose marks, which appear as dashed outlines that `Apply Redactions` ignores until the person accepts them, with `Accept` and `Reject` controls per suggestion and for all. Second, a `Whiteout` tool beside the redaction tools that covers content with a plain box and is labelled everywhere as not redaction (`D05 T01 §24`). Third, `Mark pages` uses the engine's whole-page redaction so entire pages are really removed.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 8.2 (`Redaction & Sanitize Bar`) and 9.9 (`Apply Redactions` confirmation); the suggestion state, whiteout button and its label are new controls that use `RectoWarningBrush` and `RectoDangerBrush` with text labels.
**Job:** The user can accept or reject proposed marks, cover content with a clearly labelled whiteout, and redact whole pages for real. Consumer: `RedactionViewModel` (`§9`), `whiteout` and `redact_pages` (`D05 T01 §24`).
**Treatment:** A third mark state in `RedactionViewModel` (`Pending`, `Suggested`, `Applied`) that the apply step filters on, a distinct dashed rendering and review controls, a separate `Whiteout` button whose result dialog repeats the warning, and routing of whole-page marks to `redact_pages`. Cheaper substitute that fails the checkpoint: suggestions that apply on `Apply Redactions`, or a whiteout presented as a redaction.
**Chrome:** consume `RedactionBar`, `RedactionCanvasOverlay` and the confirmation dialog (`§9`), `RectoWarningBrush`, `RectoDangerBrush`, `CommandRegistry` (`D02 T03 §11`).

**Build order:**

1. Add the `Suggested` state, the accept and reject controls and the apply filter. Done when: `RedactionUi.SuggestedMarks` shows suggestions excluded from `Apply Redactions` until accepted, and accepted ones applied.
2. Add the whiteout tool, whole-page routing and entry points. Done when: `AppTests.WhiteoutAndPageRedactionUi` covers a whiteout with its warning and a redacted page with nothing left in the raw bytes.

- [ ] Add `MarkState` (`Pending`, `Suggested`, `Applied`) and `RedactionViewModel::add_suggested_marks(marks, source_label)`, `accept_suggestion(id)`, `reject_suggestion(id)`, `accept_all_suggestions()` and `reject_all_suggestions()` in `src/services/redaction_view_model.h/.cpp`, undoable, with `Apply Redactions (N)` counting and applying only `Pending` marks. Done when: `RedactionUi.SuggestedMarks` applies zero suggested marks and applies a mark after `accept_suggestion`.
- [ ] Render suggested marks on `DocumentView` as dashed `RectoWarningBrush` outlines with a `Suggested` text tag and the `source_label`, distinct from solid pending marks, and show `Accept` and `Reject` buttons on hover and on keyboard focus. Done when: `AppTests.WhiteoutAndPageRedactionUi` shows both renderings and the buttons work from the keyboard.
- [ ] Add a `Suggestions (N)` control to `RedactionBar` with `Accept all`, `Reject all` and `Review...` (stepping through each suggestion), shown only when suggestions exist, and block closing the document with an unreviewed suggestion count in the unapplied-marks prompt (`D06 T03 §6`). Done when: closing with suggestions asks first and counts them separately.
- [ ] Add the `Whiteout` tool to `RedactionBar` as a separate button with the label `Whiteout (does not remove content)`, a rubber-band rectangle, and a result dialog that shows `WhiteoutReport` (covered marks, characters still under the cover) and the sentence `"This hides content. It does not remove it. Use Redact to remove content."`. Done when: `AppTests.WhiteoutAndPageRedactionUi` shows the dialog and the page text still extractable. Cheaper substitute: a whiteout button with a redaction label.
- [ ] Route `Mark Pages for Redaction` to `redact_pages` (`D05 T01 §24`) when the marks cover whole pages, show the `references_to_review` list (bookmarks and destinations naming those pages) after apply, and confirm with the non-bypassable dialog of `§9`. Done when: applying a page range leaves no former text in the raw output and lists the referencing bookmark.
- [ ] Register `redact.whiteout` and `redact.acceptSuggestions` in `CommandRegistry` with synonyms `cover`, `white out`, `accept suggestions`. Done when: `Ctrl+K` "white out" arms the tool. Cheaper substitute: no command-box entry.
- [ ] Give every new control an automation name and tooltip and keep suggestion review fully keyboard operable (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `RedactionUi.SuggestedMarks` and `AppTests.WhiteoutAndPageRedactionUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=RedactionUi.SuggestedMarks:AppTests.WhiteoutAndPageRedactionUi` passes.
- [ ] Commit: `app: suggested redaction marks, labelled whiteout and whole-page redaction`

**Test checkpoint:** `recto-tests.exe --gtest_filter=RedactionUi.SuggestedMarks:AppTests.WhiteoutAndPageRedactionUi` passes on the display-session host.

## 29. Inspect Document panel: what the file carries, with a path to Sanitize

Acrobat Pro and Foxit PDF Editor list the hidden content of a file before it is removed. This panel shows the report of `inspect_document` (`D05 T01 §27`) in the side panel host: a category list with counts, expandable items with location and bounded detail, a `Go to` action that jumps to the page, and `Sanitize...` that opens the Sanitize dialog (`§9`) with the matching categories ticked. Scripts and links are shown as text and never run or opened. The report can be exported as JSON or text.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new panel; it follows `design/SPEC.md` section 8.3 (side-panel card container, switcher header) and the list styling of the `Signatures` panel (`§11`), and adds `Inspect Document` to the section 8.3 panel list.
**Job:** The user can see what a PDF hides, jump to each item, and go straight to removing it. Consumer: `SidePanelHost` (`D02 T03 §3`), `inspect_document` (`D05 T01 §27`), `SanitizeDocumentDialog` (`§9`).
**Treatment:** A virtualized grouped list bound to `InspectorViewModel` that runs the inspection on a worker thread, with risk shown by icon and text, plus export and Sanitize actions. Cheaper substitute that fails the checkpoint: a summary line of counts with no item detail, or a panel that runs scripts to show their effect.
**Chrome:** consume `SidePanelHost` (`D02 T03 §3`), `SanitizeDocumentDialog` (`§9`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` status tokens.

**Build order:**

1. Implement `InspectorViewModel` and the grouped list. Done when: `AppTests.InspectorPanelUi` inspects the fixture and shows each category with its count.
2. Add detail, jump, export, Sanitize hand-off and entry points. Done when: `AppTests.InspectorPanelUi` jumps to a page, exports JSON and opens Sanitize with the right boxes ticked.

- [ ] Implement `InspectorViewModel` in `src/services/inspector_view_model.h/.cpp` running `inspect_document` on a worker thread with a `CancelToken` and progress, grouping items by category, and exposing counts, risk filters and `truncated`. Done when: the panel shows a progress ring and a cancel leaves it empty without error.
- [ ] Build the panel in `src/app/panels/InspectorPanel.xaml/.cpp`: grouped virtualized list with category header counts, item rows with risk icon and text, an expander for the bounded escaped detail, and a notice when the report is truncated. Done when: a `10000`-item fixture scrolls smoothly.
- [ ] Add `Go to` for items with a page location, `Copy details` and a note on script and attachment rows that they are shown, not run or opened. Done when: `Go to` scrolls `DocumentView` to the page and no script runs. Cheaper substitute: a `Run` or `Open` action on attachments and scripts.
- [ ] Add `Sanitize...` mapping each category's `removable_by` flag to the checkboxes of `SanitizeDocumentDialog` (`§9`), and feed the dialog its item counts from the report. Done when: `AppTests.InspectorPanelUi` opens the dialog with the matching boxes ticked and the same counts.
- [ ] Add `Export report...` writing JSON or plain text through `AtomicFileWriter`, with no document content beyond the bounded details. Done when: the exported JSON equals `recto-cli inspect --json` for the same file.
- [ ] Register the panel in `SidePanelHost` and `inspect.document` in `CommandRegistry` with synonyms `hidden data`, `metadata`, `what is in this pdf`, `before sharing`. Done when: `Ctrl+K` "hidden data" opens the panel. Cheaper substitute: no command-box entry.
- [ ] Give every control an automation name and tooltip and operate the list by keyboard (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `InspectorPanel.*` and `AppTests.InspectorPanelUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=InspectorPanel.*:AppTests.InspectorPanelUi` passes.
- [ ] Commit: `app: add the Inspect Document panel with a Sanitize hand-off`

**Test checkpoint:** `recto-tests.exe --gtest_filter=InspectorPanel.*:AppTests.InspectorPanelUi` passes on the display-session host.

## 30. Signatures panel: view the signed version and compare it with the current document

Foxit PDF Editor and Acrobat Pro show the document as it was when a signature was applied. This section adds two actions to each signature card of the `Signatures` panel (`§11`): `View signed version`, which opens the exact signed bytes read-only in a new tab labelled `Signed version (read-only)`, and `Compare with current`, which shows the report of `D05 T01 §28` (what changed after signing, with allowed and unauthorized changes told apart) and opens the comparison view of `D03 T01 §10`. Nothing is modified; the signed copy is a temp file removed on close.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 8.3 (`Signatures & Standards` panel signature cards); two new card actions and a report flyout that follow the card button styling.
**Job:** The user can see exactly what a signer signed and what changed afterwards. Consumer: `SignaturesAndStandardsPanel` (`§11`), `extract_signed_revision` and `compare_signed_with_current` (`D05 T01 §28`), `OpenDocumentPipeline` (`D02 T01 §5`).
**Treatment:** Card buttons that run the engine on a worker thread, open the temp revision through the normal open pipeline as a read-only tab with a persistent banner, and show the comparison report with a classification badge (text and icon). Cheaper substitute that fails the checkpoint: re-saving the old version through the engine, which changes its bytes and breaks its signature.
**Chrome:** consume the Signatures panel and `SignatureStatusBanner` (`§10`, `§11`), the tab strip (`D02 T01 §3`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` status tokens.

**Build order:**

1. Implement the two actions and the read-only tab. Done when: `AppTests.SignedVersionUi` opens a signed file's first signature version and the tab shows its pages with the read-only banner.
2. Add the comparison report and entry points. Done when: `AppTests.SignedVersionUi` shows the classification and opens the comparison for a file edited after signing.

- [ ] Add `View signed version` and `Compare with current` buttons to each signature card in `SignaturesAndStandardsPanel.xaml/.cpp`, disabled with a stated reason when the signature is invalid or its `/ByteRange` fails validation. Done when: both buttons enable for a valid signature and disable with the engine's reason for a bad range.
- [ ] Run `extract_signed_revision` on a worker thread and open the result through `OpenDocumentPipeline` as a read-only tab titled `<name> (signed version)` with a banner `"This is the version the signer signed. It cannot be edited."` and Save disabled. Done when: the tab opens, Save and Edit are disabled, and the temp file is removed when the tab closes.
- [ ] Show the `SignedVsCurrentReport` in a flyout: classification badge (`No changes`, `Allowed changes only`, `Unauthorized changes`) with an icon and text, counts of added, removed and changed pages, annotations and fields, and later signatures, with `Open comparison` launching `D03 T01 §10`'s view between the two files. Done when: `AppTests.SignedVersionUi` shows each classification for the matching fixture.
- [ ] Keep the signed bytes safe: the temp copy is read-only, outside any user folder, never offered by `Save as` without an explicit copy dialog that warns it is the signed version, and the signed file's signature verifies in its own tab. Done when: `SignatureUi.SignedVersionSafety` finds the temp file read-only and the tab banner reporting the signature valid.
- [ ] Register `signature.viewSignedVersion` and `signature.compareWithCurrent` in `CommandRegistry` with synonyms `original version`, `what changed since signing`, `signed copy`. Done when: `Ctrl+K` "signed copy" opens the version of the first signature. Cheaper substitute: no command-box entry.
- [ ] Give every new control an automation name and tooltip (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `SignatureUi.SignedVersionSafety` and `AppTests.SignedVersionUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=SignatureUi.SignedVersionSafety:AppTests.SignedVersionUi` passes.
- [ ] Commit: `app: view the signed version and compare it with the current document`

**Test checkpoint:** `recto-tests.exe --gtest_filter=SignatureUi.SignedVersionSafety:AppTests.SignedVersionUi` passes on the display-session host.

## 31. Trusted Lists dialog: opt-in download, manual import and per-list control

The UI for `TrustStore` (`D05 T01 §29`), reachable from the Signatures panel (`§11`), `Ctrl+K` and the settings link of `D02 T04 §6`. It shows the lists the person has imported (name, kind, source, last update, next update, certificate count, enabled), imports certificate bundles and trusted-list files by file picker with a fingerprint confirmation, and, only after the person turns on `Download trusted lists`, offers `Update now`. It says plainly that a downloaded list's signature is not verified in this version and that nothing is fetched unless asked.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new dialog; it follows `design/SPEC.md` section 9.9 form-dialog chrome and the settings card styling of section 9.7, and adds `Trusted Lists` to the section 9.9 dialog list.
**Job:** The user can decide which extra trust anchors Recto uses to verify signatures, and keep the network off unless they choose otherwise. Consumer: `SignatureStatusBanner` and `SignaturesAndStandardsPanel` (`§10`, `§11`), `TrustStore` and `TrustListDownloader` (`D05 T01 §29`).
**Treatment:** A `ContentDialog` bound to `TrustListsViewModel` with a list, import and confirm flows, and an opt-in toggle for downloads that is off by default and writes `trust.downloadEnabled`. Cheaper substitute that fails the checkpoint: trusting an imported list without showing its fingerprint, or downloading on first run.
**Chrome:** consume the dialog host (`D02 T01 §10`), `CertificateDetailsDialog` (`§11`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Implement `TrustListsViewModel` and the list, import and confirm flows. Done when: `AppTests.TrustListsUi` imports a bundle after confirming its fingerprint and the list appears enabled.
2. Add the opt-in download flow, the verifier refresh and entry points. Done when: `AppTests.TrustListsUi` shows no network call with the toggle off and a signature turning trusted after import.

- [ ] Implement `TrustListsViewModel` in `src/services/trust_lists_view_model.h/.cpp` over `TrustStore` (list, enable, disable, remove, import) with errors from the engine shown by name. Done when: removing a list updates the view and a bad file shows its refusal.
- [ ] Build the list with name, kind, source, issue and next-update dates, certificate count, an enable switch and `Details...` (opening the certificates in `CertificateDetailsDialog`), plus `Import file...`. Done when: the list matches the store contents.
- [ ] Build the import confirmation step: show the file's SHA-256, the source, the certificate count and the sentence `"Recto cannot verify the signature of this list. Only continue if you trust where it came from."`, and enable `Add list` only after the person ticks the confirmation. Done when: `AppTests.TrustListsUi` cannot add without the tick. Cheaper substitute: importing without showing the fingerprint.
- [ ] Add the `Download trusted lists` switch (off by default, writes `trust.downloadEnabled`), an editable list address field pre-filled with the EU default and empty for the Adobe-style list, and `Update now` with progress, per-list results and cancel, enabled only when the switch is on. Done when: `AppTests.TrustListsUi` shows `Update now` disabled and no request with the switch off.
- [ ] Show downloaded lists as `Pending confirmation` with their fingerprint, source and the difference from the active version, enable them only through an explicit `Confirm` that repeats the fingerprint, and keep the previous version active until then. Done when: `AppTests.TrustListsUi` shows an unconfirmed update leaving verification results unchanged.
- [ ] Refresh signature verification after a change: re-run `verify_signatures` on open documents so banners and cards update, and show the trust source on trusted cards (`"Trusted through Recto list: <name>"`). Done when: a fixture signature turns from untrusted to trusted after import and back after disable.
- [ ] Register `signature.trustedLists` in `CommandRegistry` with synonyms `trust list`, `eu trusted list`, `aatl`, `trust anchors` and add the link in the `Signatures` panel header. Done when: `Ctrl+K` "trust list" opens the dialog. Cheaper substitute: no command-box entry.
- [ ] Give every control an automation name and tooltip and operate the dialog by keyboard (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `TrustLists.*` and `AppTests.TrustListsUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=TrustLists.*:AppTests.TrustListsUi` passes.
- [ ] Commit: `app: add the Trusted Lists dialog with opt-in download and manual import`

**Test checkpoint:** `recto-tests.exe --gtest_filter=TrustLists.*:AppTests.TrustListsUi` passes on the display-session host.

## 32. Pressure-sensitive pen capture for handwritten signatures

Foxit PDF Editor can take a signature from a signature pad. Recto's `Draw Handwritten Ink` canvas (`§10`) takes pointer input only; this section makes it use Windows Ink pen input (`Microsoft.UI.Input.Inking` `InkPresenter` with pen pressure), so a pen tablet or a Windows Ink digitizer produces a signature whose stroke width follows pressure. Devices that need a vendor SDK to draw on their own display, such as LCD signature pads, are not supported and the dialog says that device input arrives only as a pen on a normal digitizer. Strokes are stored as vector paths in the appearance stream, as before.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.9 (`Sign Document Dialog`, ink appearance canvas) with the existing `Draw Handwritten Ink` mode of `§10`; the pressure control and device note are small additions.
**Job:** A person with a pen tablet can sign with natural pressure variation and still get a crisp vector signature. Consumer: `SignDocumentDialog` (`§10`), the appearance library (`§10`), `SignatureAppearanceConfig` (`D05 T01 §8`).
**Treatment:** Replace the pointer-only canvas with an `InkCanvas` and `InkPresenter` configured for pen and mouse, convert `InkStroke` points with pressure into variable-width vector outlines for the `/AP /N` stream, and keep mouse and touch working. Cheaper substitute that fails the checkpoint: constant-width strokes that ignore pressure, or a bitmap signature.
**Chrome:** consume the ink pad of `SignDocumentDialog` (`§10`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Implement `InkToAppearance` converting pressure strokes to vector paths. Done when: `SignatureUi.PressureStrokes` turns a synthetic pressure-varying stroke into an outline whose width follows pressure.
2. Wire the canvas, the fallback and the device note. Done when: `AppTests.PenCaptureUi` draws with simulated pen and mouse input and the appearance previews both.

- [ ] Implement `ink_to_appearance(strokes, options)` in `src/services/ink_appearance.h/.cpp` converting `InkStroke` points with pressure into smoothed variable-width closed paths (minimum width `0.4 pt`, maximum `3 pt`), at most `20000` points per signature, as Bezier paths for the appearance stream. Done when: `SignatureUi.PressureStrokes` verifies width ranges, the point limit and smooth joins.
- [ ] Replace the pointer canvas of `SignDocumentDialog` with an `InkCanvas` using `InkPresenter` for pen, mouse and touch, with `Clear` and `Undo stroke`, and fall back to constant width when the device reports no pressure. Done when: `AppTests.PenCaptureUi` draws with simulated pen pressure and with mouse input.
- [ ] Show a one-line device note `"Pen tablets and Windows Ink screens work. Signature pads that need their own software are not supported."` and detect whether a pen digitizer is present to show `"Pen detected"` or `"No pen detected. The mouse works."`. Done when: the note and the detected state appear.
- [ ] Store the strokes in the signature appearance library (`§10`) as vector paths and keep the live preview identical to the saved `/AP`. Done when: `SignatureUi.AppearanceLibrary` still passes, the preview pixels equal the signed appearance, and `SignatureUi.PressureAppearanceRestart` saves a pressure-sensitive appearance, restarts the app and reopens it with the same variable-width outline. Cheaper substitute: storing a bitmap of the canvas.
- [ ] Give the canvas and its buttons automation names, tooltips and a keyboard alternative (typed or image appearance) so signing never requires a pen (`D03 T02 §3`). Done when: the control inventory test passes.
- [ ] Add `SignatureUi.PressureStrokes` and `AppTests.PenCaptureUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=SignatureUi.PressureStrokes:AppTests.PenCaptureUi` passes.
- [ ] Commit: `app: pressure-sensitive pen capture for handwritten signatures`

**Test checkpoint:** `recto-tests.exe --gtest_filter=SignatureUi.PressureStrokes:AppTests.PenCaptureUi` passes on the display-session host.

## Verification

- Every SPEC 8.2, 8.3, 9.5, 9.6 and 9.9 element across the three Organize modes, authoring dialogs, Redaction/Sanitize bar, and Digital Signature surfaces is accounted for on rendered surfaces (`D-Organize.png`, `explorations/A-Organize.png`, `B-Organize.png`, `D-Dialogs.png`).
- Outputs of all modes verified by rendering, originals provably untouched until an atomic replace the user asked for.
- `recto-tests` full suite green; `python scripts/todo-graph.py validate` passes.
