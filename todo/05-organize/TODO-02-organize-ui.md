---
schema_version: 1
id: organize-ui
domain: 05-organize
status: active
title: "TODO-02 -- Organize UI, Crop/Watermark/Bates Dialogs, Redaction Bar and Digital Signatures UI"
track: M4
---

# TODO-02 -- Organize UI, Crop/Watermark/Bates Dialogs, Redaction Bar and Digital Signatures UI

> **Goal:** SPEC 9.6's one page grid with three modes (drag to reorder with a selection action bar, combine many files with per-source colours, and split with click-between-pages breaks and named parts), plus the interactive Organize authoring dialogs with live page preview (`Insert Blank Page`, `Crop & Resize Pages`, `Watermark & Stamp`, `Header, Footer & Bates Numbering`) and the Viewer's security & redaction surfaces (`Redaction & Sanitize Bar` `Ctrl+Shift+X`, `Signature Status Banner & Sign Mode` `Ctrl+Shift+G`, `Sign Document Dialog`, `Signatures & Standards` side panel, and `Protected & Signed PDF` Convert card), all compiling to D05 T01's operations and guarded so unsaved work cannot be lost silently.

> **Current state:** Measured 2026-10-14. No code exists. The boards are `D-Organize.png` (reorder, the chosen design), `explorations/A-Organize.png` (combine) and `explorations/B-Organize.png` (split); SPEC 8.2, 8.3, 9.5, 9.6 and 9.9 define the Organize header, dark floating selection bar, live-preview authoring dialogs, redaction/sanitization bar, and digital signature surfaces. Page thumbnails reuse D02 T03 §2's list machinery at grid size; saves run as D04 T01 jobs by that file's XREF agreement.

## Inputs

- `design/SPEC.md` sections 8.2 (`Redaction & Sanitize Bar`, `Signature Status Banner & Sign Mode`), 8.3 (`Signatures & Standards` side panel), 9.5 (`Protected & Signed PDF` card), 9.6 (`Organize` modes and tools), 9.9 (`unsaved-changes`, `Replace original`, `Watermark`, `Header/Footer & Bates`, `Crop & Resize`, `Sanitize Document`, and `Sign Document` dialogs), 7 (grid shape), 10 (`Ctrl+Shift+X`, `Ctrl+Shift+G`), 11 (keyboard operation).
- -> XREF: D05 T01 §1 -- the operation vocabulary every mode, dialog and security bar compiles to; new edit kinds need an operation there first.
- -> XREF: D04 T01 §2 -- saves from this page and the `Protected & Signed PDF` Convert card run as queue jobs with progress and cancel on that frame.

## Outcome

- A user can reorder, rotate, delete, insert blank pages, and extract pages by direct manipulation and keyboard, see an accurate unsaved-change summary, undo within the session, and save with the original protected by the atomic rule.
- Combining files and splitting into named parts produce exactly the outputs the grid showed.
- Interactive `Crop & Resize Pages`, `Watermark & Stamp`, and `Header, Footer, Page Numbers & Bates Numbering` dialogs (`SPEC 9.6`, `9.9`) provide a live miniature page preview and compile to `D05 T01 §4` operations.
- The Viewer's `Redaction & Sanitize Bar` (`Ctrl+Shift+X`), `Sanitize Document` dialog, `Signature Status Banner`, `Sign Document` dialog (`Ctrl+Shift+G`), `Signatures` side panel, and `Protected & Signed PDF` Convert card (`SPEC 8.2`, `8.3`, `9.5`, `9.9`) provide end-to-end visual redaction, sanitization, cryptographic signing, and signature verification.
- Leaving with unsaved changes always asks; nothing is ever written over the open viewer document in place.

**Adjacency:** list=not-applicable (the grid is the list, in this file); document=applicable @ D05 T02 §4; settings=not-applicable (no Organize tunables in SPEC 9.7; save defaults are D02 T04 §2); reporting=not-applicable (the unsaved-change summary is in-place state, section 1); notifications=applicable @ D04 T01 §3; permissions=not-applicable (no roles); audit=not-applicable (session undo is in-memory state, not a history record); exchange=not-applicable (file in and out are the modes themselves, sections 2 and 3); reverse=applicable @ D05 T02 §1

**Adjacency rationale:** The grid's reverse is session Undo (section 1) and its document is the saved output (section 4); both are owned here because Organize is where Recto authors PDF structure interactively. Everything around it (queue notices, save-location setting) belongs to the convert frame and settings files and is declared there.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Page grid, selection model and Reorder | D05 T01 §2, D02 T03 §2 | [ ] |
| 2 | §2 | Combine mode | §1, D05 T01 §1 | [ ] |
| 3 | §3 | Split mode | §1, D05 T01 §1 | [ ] |
| 4 | §4 | Save flows and the unsaved-changes guard | §1, D04 T01 §2 | [ ] |
| 5 | §5 | Interactive Crop/Resize, Watermark, Header/Footer and Bates Numbering dialogs with live preview | §4, D05 T01 §4 | [ ] |
| 6 | §6 | Viewer Redaction and Sanitize Bar (`Ctrl+Shift+X`), Signature Banner, Sign Dialog (`Ctrl+Shift+G`) and Signatures panel | §4, D05 T01 §5, D02 T03 §5, D03 T01 §3 | [ ] |

## 1. Page grid, selection model and Reorder

The grid over the working copy's page list (thumbnails through the shared tile path at grid bucket): click, Ctrl+click and Shift+click selection, drag reorder with drop indicator, keyboard move (arrows plus the action bar's Move to), the dark floating action bar ("N pages selected", Rotate, Move to, Extract to new PDF, Delete in danger colour, clear), 90-degree badges on rotated pages, and session undo for every edit. The header summary counts moves, rotations and deletions against the opened state and is computed from the edit model, never hand-tracked.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.6 Reorder with `design/screenshots/D-Organize.png`.
**Job:** The user can put a document's pages in the order they want by hand and by keyboard. Consumer: §4 compiles the edit model to D05 T01 operations on save.
**Treatment:** One edit model (ordered page entries with source identity and rotation delta) rendered by the grid; drag and keyboard mutate the model only. Cheaper substitute that fails the checkpoint: applying edits to the file per drag, making undo and the unsaved summary fiction.
**Chrome:** consume the thumbnail tile path, accent selection and check badges (SPEC 11: never colour alone), danger token for Delete. Do not invent a second thumbnail or selection system.

**Build order:**

1. Grid and edit model with virtualization for large documents, selection semantics and drag reorder with the drop indicator. Done when: a 200-page document's grid scrolls and reorders fluidly with bounded thumbnail memory.
2. Action bar operations (rotate selection, move to position, extract (wired through §4's save as its own output), delete, clear) plus session undo and the header summary. Done when: a scripted edit sequence and its undo return the model to the opened state exactly, summary reading no changes.

- [ ] Grid with selection and drag reorder. Done when: keyboard-only reordering (focus, select, Move to) achieves the same model as the drag path in driven tests. Cheaper substitute that fails the checkpoint: mouse-only drag with keyboard as a later promise.
- [ ] Undo and accurate change summary. Done when: rotate-then-undo shows no phantom "1 rotated" in the header.
- [ ] Commit: `organize: page grid with Reorder, selection actions and undo`

**Test checkpoint:** screenshots against `D-Organize.png` in findings (selection bar, rotation badge, drop indicator), scripted edit-and-undo model equality quoted.

## 2. Combine mode

File chips with a colour per source, Add files, every page tinted along its bottom edge by source, the same grid interactions across the merged list, an output file name field and Save combined PDF compiling to D05 T01 §1's merge with the grid's final order and deletions. Encrypted additions go through the password flow before their pages appear.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.6 Combine with `explorations/A-Organize.png`.
**Job:** The user can build one PDF from several, seeing which pages came from where. Consumer: the merge operation and the queue save.
**Treatment:** The §1 grid over a multi-source edit model; source colours assigned from a fixed distinguishable palette also legible in high contrast (colour plus the chip label, never colour alone). Cheaper substitute that fails the checkpoint: merging whole files end to end with no page-level control.
**Chrome:** consume the grid, chips as the file-list pattern for Convert step 1 parity, and the save flow from §4. Do not invent a second multi-file picker.

**Build order:**

1. Multi-source model (sources with colours and page providers through their own actor-opened descriptions and thumbnails), Add files and per-source removal. Done when: three corpus files combine in the grid with correct tints and page counts.
2. Save combined through §4 compiling to merge with selections. Done when: the output renders page-for-page as the grid showed, verified by render comparison in D06 T01 §3's suite (run here for the combine fixtures).

- [ ] Combine over the shared grid. Done when: removing a source removes exactly its pages and updates the summary. Cheaper substitute that fails the checkpoint: a file list with an order box and no page visibility.
- [ ] Encrypted source handling through the password dialog before pages appear. Done when: cancelling the password adds nothing and says so.
- [ ] Commit: `organize: Combine mode with per-source colour`

**Test checkpoint:** screenshot against `A-Organize.png`, combined output render-compared page for page, encrypted-source cancel path driven.

## 3. Split mode

Click between two pages toggles a split; parts take colours; the side panel lists parts with page ranges and editable file names; methods (by the placed breaks, every N pages, one file per page) recompute parts; saving runs D05 T01 §1's split as one job producing all parts atomically per file, reporting per-part success.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.6 Split with `explorations/B-Organize.png`.
**Job:** The user can break one PDF into the exact set of files they intend, named as they intend. Consumer: the split job and its per-part outputs.
**Treatment:** Breaks as model state over the §1 grid; part list derived, names editable with collision validation before the job starts. Cheaper substitute that fails the checkpoint: a ranges text box with no visual breaks (the Convert card's split is that surface; Organize is the visual one, both over the shared range parser semantics).
**Chrome:** consume the grid and panel patterns, part colours from the §2 palette. Do not invent a second range language: methods produce the shared parser's ranges.

**Build order:**

1. Break toggling, part derivation, coloured part bands and the parts panel with editable names and ranges as readback. Done when: breaks, every-N and per-page methods interconvert without losing names where parts survive.
2. Split save through §4 with per-part outcomes. Done when: the produced set matches the panel's names and ranges exactly, verified by info and renders.

- [ ] Visual split with derived parts. Done when: keyboard can place and remove a break at a focused boundary. Cheaper substitute that fails the checkpoint: mouse-only between-page targets a few pixels wide.
- [ ] Name-collision refusal before the job. Done when: duplicate part names block Save in the panel with the fault named.
- [ ] Commit: `organize: Split mode with visual breaks and named parts`

**Test checkpoint:** screenshot against `B-Organize.png`, split outputs verified by name, page count and renders, collision refusal driven.

## 4. Save flows and the unsaved-changes guard

Save (accent) and Save as copy compile the mode's model to D05 T01 operations and run them as D04 T01 jobs through the atomic helper: Save targets the original path via temp-then-replace (the SPEC 9.9 Replace original understanding: the copy is protected because the replace is atomic, and the dialog appears where the SPEC places it), Save as copy prompts for the destination. A saved-then-reopened result becomes the grid's new baseline (summary clears). Leaving Organize (mode switch, tab close, window close) with a dirty model raises "Save your changes first?" (Discard, Cancel, Save); a failed save keeps the model dirty and the dialog truth.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.6 and 9.9 with `D-Organize.png` header and `D-Dialogs.png` (unsaved changes, replace original).
**Job:** The user can keep their reorganized document without risking the original or losing work by accident. Consumer: the viewer reopens the saved file fresh (its actor never saw the edits in place).
**Treatment:** Compile-to-operations on save, executed as queue jobs; the grid never writes files itself. Cheaper substitute that fails the checkpoint: writing on every edit (no unsaved state to guard, and a crash mid-drag corrupts the only copy).
**Chrome:** consume the job frame, atomic helper and dialog family. Do not invent Organize-private saving.

**Build order:**

1. Compilation per mode (reorder permutation plus rotations; combine selections; split ranges) into D05 T01 operations, submitted as jobs with the grid locked against edits to the saving model snapshot. Done when: each mode's saved output render-matches its grid state (the D06 T01 §3 fixtures run here).
2. The guard: dirty tracking driving the header summary, the leave dialog on every exit path, Discard truly discarding (model rebuilt from file), Save from the dialog completing before the leave proceeds. Done when: closing the window mid-edit and choosing Discard leaves the file byte-identical (hash quoted), and Save leaves it equal to the grid.

- [ ] Mode compilation to operations as jobs. Done when: a cancelled save job leaves the model dirty and the file untouched. Cheaper substitute that fails the checkpoint: marking clean at job submission instead of completion.
- [ ] Unsaved-changes guard on tab close, mode switch and window close. Done when: all three paths are driven in tests with all three answers.
- [ ] Commit: `organize: save flows as jobs with the unsaved-changes guard`

**Test checkpoint:** dialog screenshots against `D-Dialogs.png`, discard byte-identity hash quoted, per-mode save render comparisons green.

## 5. Interactive Crop/Resize, Watermark, Header/Footer and Bates Numbering dialogs with live preview

Build the Organize authoring tools and live-preview dialogs specified in `design/SPEC.md` sections 9.6 and 9.9, wired to `D05 T01 §4`:
- **Insert Blank Page (`E8A5`):** Action bar and header button inserting a blank page before/after the selected page with size (`Match current page`, `A4`, `Letter`, `Legal`, `Custom`) and orientation (`Portrait` / `Landscape`).
- **Crop & Resize Pages Dialog (`E7A8`, `SPEC 9.6` & `9.9`):** Interactive crop handles overlaid on a live page preview, numeric margin inputs (`Top`, `Bottom`, `Left`, `Right` in `pt`, `mm`, `in`), target box selector (`CropBox`, `MediaBox`, `TrimBox`, `BleedBox`, `ArtBox`), `Auto-Detect White Margins`, page-size rescaling (`Scale content to fit`), and scope (`Selected pages`, `All pages`, `Even/Odd pages`).
- **Watermark & Stamp Dialog (`SPEC 9.6` & `9.9`):** Two-column modal (`640 epx` wide: form controls on the left, live miniature page preview on the right) for text or image/PDF vector watermarks, font/size/colour, rotation (`-90` to `+90` degrees), opacity (`0-100%`), layer (`Behind page content` / `In front of page content`), 9-point position grid or `Tile across page`, and page range.
- **Header, Footer, Page Numbers & Bates Numbering Dialog (`SPEC 9.6` & `9.9`):** Two-column modal with live preview, 6 text zones (`Top-Left`, `Top-Center`, `Top-Right`, `Bottom-Left`, `Bottom-Center`, `Bottom-Right`), one-click token insertion chips (`Insert Page Number`, `Insert Page of Total`, `Insert Date`, `Insert File Name`, `Insert Bates Number`), Bates configuration (`Prefix`, `Suffix`, `Start Number`, `Digits 4-10`), and `Shrink page content to avoid overlapping existing text` toggle.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 9.6 (`Organize` tools) and 9.9 (`Watermark & Background Dialog`, `Header, Footer, Page Numbers & Bates Numbering Dialog`, `Crop & Resize Pages Dialog`).
**Job:** Users can insert blank pages, crop margins with visual handles, rescale page sizes, and apply watermarks, page numbers, or legal Bates numbers with an immediate live preview before saving. Consumer: the Organize edit model (`§1`, `§4`) and Viewer `Tools` menu.
**Treatment:** Two-column `640 epx` `ContentDialog`s with live Direct2D/WinUI miniature page preview updating on every control change. Cheaper substitute that fails the checkpoint: blind numeric text inputs with no live page preview, or omitting Bates numbering and `Auto-Detect White Margins`.
**Chrome:** consume the shared `ContentDialog` host (`D02 T01 §4`) and `D05 T01 §4` engine operations.

**Build order:**

1. Wire `Insert Blank Page` and the `Crop & Resize Pages Dialog` (interactive crop handles on preview, `Auto-Detect White Margins`, box selector, paper-size rescaling) into Organize and the Viewer Tools menu. Done when: `AppTests.OrganizeCropAndBlankUi` inserts a blank page, auto-detects margins on a corpus page, previews the crop box, and saves through the queue.
2. Build the `Watermark & Stamp Dialog` and `Header, Footer, Page Numbers & Bates Numbering Dialog` (`640 epx` two-column layout with live page preview and token chips). Done when: `AppTests.WatermarkAndBatesUi` configures a diagonal watermark and 6-digit Bates number, verifies live preview updates, and saves a stamped PDF whose extracted `PageText` contains the Bates sequence.

- [ ] `Insert Blank Page` and `Crop & Resize Pages Dialog` with interactive crop handles and `Auto-Detect White Margins`. Done when: `AppTests.OrganizeCropAndBlankUi` passes end-to-end. Cheaper substitute that fails the checkpoint: numeric-only crop inputs without visual handles or auto-margin detection.
- [ ] `Watermark & Stamp Dialog` and `Header, Footer & Bates Numbering Dialog` with live miniature page preview. Done when: `AppTests.WatermarkAndBatesUi` passes end-to-end.
- [ ] Commit: `organize: add Insert Blank Page, Crop/Resize, Watermark and Bates Numbering dialogs`

**Test checkpoint:** `recto-tests --gtest_filter=AppTests.OrganizeCropAndBlankUi:AppTests.WatermarkAndBatesUi` passes.

## 6. Viewer Redaction and Sanitize Bar (`Ctrl+Shift+X`), Signature Banner, Sign Dialog (`Ctrl+Shift+G`) and Signatures panel

Build the visual redaction, sanitization, and digital signature surfaces specified in `design/SPEC.md` sections 8.2, 8.3, 9.5 and 9.9, wired to `D05 T01 §3` and `D05 T01 §5`:
- **Redaction & Sanitize Bar (`Ctrl+Shift+X`, `SPEC 8.2` & `9.9`):**
  - `40 epx` contextual bar with `Mark Text & Area for Redaction` (draws translucent red `--danger` preview boxes with crosshair cursor), `Mark Pages for Redaction`, `Find & Redact` (opens the `D03 T01 §3` Find card in `Mark all matches for redaction` mode with Regex preset patterns for Emails, Phone Numbers, SSNs/IDs, Credit Cards, and Dates), `Redaction Appearance` (burn-in fill colour + overlay exemption text e.g. `REDACTED`, `FOIA (b)(6)`), `Sanitize Document` (`SPEC 9.9` checklist dialog stripping metadata, XMP, JavaScript, embedded files, comments, hidden layers, and incremental history), and `Apply Redactions` (`--danger` primary button with confirmation dialog that runs `D05 T01 §5`'s physical redactor into a new/replaced file).
- **Signature Status Banner, Sign Mode (`Ctrl+Shift+G`), Sign Dialog & Signatures Side Panel (`SPEC 8.2`, `8.3`, `9.5`, `9.9`):**
  - Automatic `36 epx` status banner when a signed PDF opens (`All digital signatures are valid` in `--success`, `Signed, but certificate is untrusted or unsigned changes were made` in `--warning`, or `Signature is invalid -- document has been altered` in `--danger`), with `View Signatures` and `Sign Document` (`Ctrl+Shift+G`).
  - Interactive `Draw Signature Box` mode on canvas opening the `Sign Document Dialog` (`SPEC 9.9`): Certificate source (`Windows Certificate Store` personal cert picker, `PKCS#12 .pfx / .p12 file` with password, or `Create Self-Signed Certificate`), Signature Appearance (`Draw handwritten ink`, `Upload signature image`, or `Standard text stamp` + `Show signer name, date, reason & location`), Metadata (`Reason`, `Location`, `Contact Info`), and Cryptographic Standards (`PAdES B-B / B-T / B-LT` or `PKCS#7 Detached`, `SHA-256` / `SHA-384` / `SHA-512`, `RFC 3161 Timestamp Server URL`, `Lock document after signing /DocMDP`).
  - `Signatures` section of the `Signatures & Standards` side panel (`SPEC 8.3`) showing per-signature cards (signer identity, issuer, chain trust, timestamp, byte-range coverage, `Jump to Signature Field`, `Certificate Details...`) and the **Protected & Signed PDF** Convert card (`SPEC 9.5`) combining AES-256 encryption and batch X.509 digital signing.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Redaction & Sanitize Bar`, `Signature Status Banner & Sign Mode`), 8.3 (`Signatures & Standards` side panel), 9.5 (`Protected & Signed PDF` Convert card), and 9.9 (`Sanitize Document Dialog`, `Sign Document Dialog`).
**Job:** Users can mark and permanently redact sensitive text/images or regex patterns, sanitize hidden metadata, cryptographically sign PDFs on canvas or in batch, and inspect signature validity in the viewer. Consumer: viewer window (`D02 T02`), side-panel host (`D02 T03 §5`), and Convert page (`D04 T01 §2`).
**Treatment:** Translucent red `--danger` redaction preview overlays with explicit `Apply Redactions` confirmation and physical stream scrubbing (`D05 T01 §5`), plus Windows Certificate Store / `.pfx` signing dialog with live signature appearance preview and automatic verification banner. Cheaper substitute that fails the checkpoint: applying redactions as removable black annotations, or omitting the signature verification banner and certificate picker.
**Chrome:** consume `DocumentView` (`D02 T02`), the Find engine (`D03 T01 §3`), the side-panel container (`D02 T03 §5`), and `ConvertPage` (`D04 T01 §2`).

**Build order:**

1. Build the `Redaction & Sanitize Bar` (`Ctrl+Shift+X`), on-canvas redaction mark overlays, `Find & Redact` regex pattern presets, `Sanitize Document Dialog`, and `Apply Redactions` confirmation flow wired to `D05 T01 §5`. Done when: `AppTests.RedactionAndSanitizeUi` marks text via regex (`Emails`) and area lasso, applies redactions, and verifies the saved PDF has zero extractable redacted text and zero XMP/`/Info` metadata after sanitization.
2. Build the `Signature Status Banner`, on-canvas `Sign Document` (`Ctrl+Shift+G`) placement and `Sign Document Dialog` (`SPEC 9.9`), the `Signatures` side panel (`SPEC 8.3`), and the `Protected & Signed PDF` Convert card (`SPEC 9.5`). Done when: `AppTests.DigitalSignaturesUi` signs a PDF on canvas with a test certificate, re-opens it, verifies the green `All digital signatures are valid` banner and Signatures side-panel card, and runs a batch sign+encrypt job in Convert.

- [ ] `Redaction & Sanitize Bar` (`Ctrl+Shift+X`), `Find & Redact` regex presets, `Sanitize Document Dialog`, and `Apply Redactions` flow. Done when: `AppTests.RedactionAndSanitizeUi` passes end-to-end. Cheaper substitute that fails the checkpoint: black annotation overlays instead of invoking `Redactor`.
- [ ] `Signature Status Banner`, `Sign Document Dialog` (`Ctrl+Shift+G`), `Signatures` side panel, and `Protected & Signed PDF` Convert card. Done when: `AppTests.DigitalSignaturesUi` passes end-to-end.
- [ ] Commit: `app: add Redaction and Sanitize bar, Sign Document dialog and Signatures side panel`

**Test checkpoint:** `recto-tests --gtest_filter=AppTests.RedactionAndSanitizeUi:AppTests.DigitalSignaturesUi` passes.

## Verification

- Every SPEC 8.2, 8.3, 9.5, 9.6 and 9.9 element across the three Organize modes, authoring dialogs, Redaction/Sanitize bar, and Digital Signature surfaces is accounted for on rendered surfaces.
- Outputs of all modes verified by rendering, originals provably untouched until an atomic replace the user asked for.
- `recto-tests` full suite green; `python scripts/todo-graph.py validate` passes.
