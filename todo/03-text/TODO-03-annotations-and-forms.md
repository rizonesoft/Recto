---
schema_version: 1
id: annotations-and-forms
domain: 03-text
status: active
title: "TODO-03 -- Interactive Annotations, Markup, AcroForms and Form Designer"
track: M3
---

# TODO-03 -- Interactive Annotations, Markup, AcroForms and Form Designer

> **Goal:** Recto reads, creates, edits, flattens and exports all 15 standard PDF annotation and markup types (`Highlight`, `Underline`, `Squiggly`, `StrikeOut`, `Sticky Note`, `FreeText`, `Ink`, `Line` / `Arrow`, `Rectangle`, `Circle`, `Polygon` / `PolyLine`, `Stamp`, `FileAttachment`, `Link`, and `Caret`) and provides full AcroForm and static/dynamic XFA-to-AcroForm reading, interactive filling, form field creation and layout (`Form Designer`), calculation-order evaluation, appearance-stream (`/AP`) synthesis, form flattening, and `JSON`/`FDF`/`XFDF`/`XML` data import and export across `recto-pdf`, `recto-services`, `recto-cli` and the WinUI 3 viewer.

> **Current state:** Measured 2026-10-14. No code exists; `design/SPEC.md` sections 8.2 (`Markup & Annotation Bar` and `Form Fill & Form Designer Bar`), 8.3 (`Comments / Annotations` side panel), 9.3 (`Selection floating toolbar` quick highlight and sticky note actions), and 10 (`Ctrl+Shift+A`, `Ctrl+Z`, `Ctrl+Y`) together with foundation plan decisions D9 and D12 and sections 4.1, 5, 6 and 7 define the complete engine and visual contract.

## Inputs

- `design/SPEC.md` sections 8.2 (Markup & Annotation Bar, Form Fill & Form Designer Bar), 8.3 (Comments / Annotations side panel), 9.3 (floating selection toolbar), 10 (keyboard shortcuts).
- `docs/plans/2026-10-13-recto-foundation.md` decisions D9 and D12; sections 3.2, 4.1 (`annotations.cpp`, `forms.cpp`), 5, 6 and 7 (M3 row).
- ISO 32000-1 section 12.5 (Annotations) and section 12.7 (Interactive Forms), ISO 32000-2, and ISO 19444-1 (XFDF).
- -> XREF: D02 T02 §8 -- the Floating Page Toolbar's `Annotate / Tools` and `Form designer` actions toggle the contextual mode bars built here.
- -> XREF: D02 T03 §3 -- Comments side panel and outline/layer/attachment shell slots share `SidePanelHost` (`§3`) and the undo/redo history stack (`§6`).
- -> XREF: D02 T04 §5 -- the `Highlight form fields` toggle in Settings (`D02 T04 §5`) governs required/fillable field highlighting in §8 here.
- -> XREF: D03 T01 §4 -- Selection floating toolbar quick `Highlight`, `Strikeout`, and `Add Note` buttons delegate to the annotation creation actions in §3 here.
- -> XREF: D03 T02 §2 -- UI Automation tree exposes interactive form fields and annotation comments alongside page text via §10 here.
- -> XREF: D06 T02 §3 -- `fuzz_forms` (`D06 T02 §3`) fuzzes the `/Annots` and `/AcroForm`/`/XFA` parsers (`§1`, `§6`, `§7`) built here.
- -> XREF: D03 T04 §13 -- Edit PDF reuses these handles for page objects (§13 there).
- -> XREF: D05 T01 §3 -- page edits remap or remove named destinations through `remap_page_references`.
- -> XREF: D02 T02 §9 -- authored links activate through the existing link prompt.
- -> XREF: D09 T03 §1 -- Recto Cloud reviews use this annotation JSON model (D09 T03 §1).
- -> XREF: D09 T05 §11 -- shows live Recto Cloud review comments (D09 T05 §11).
- -> XREF: D09 T05 §20 -- stamp library sync.
- -> XREF: D08 T02 §27 -- comments.
- -> XREF: D03 T01 §14 -- spell service in comment editors.
- -> XREF: D03 T01 §14 -- spell service in form fields.
- -> XREF: D03 T02 §10 -- article threads for reading order.
- -> XREF: D03 T04 §22 -- flows as threads.
- -> XREF: D04 T03 §2 -- DocxWriter write_blocks.
- -> XREF: D07 T01 §11 -- pages_with_comments for print.
- -> XREF: D04 T05 §1 -- XlsxWriter.
- -> XREF: D04 T07 §7 -- combine form data step.
- -> XREF: D03 T01 §7 -- ruling-line removal feeds detection.
- -> XREF: D02 T04 §5 -- pen settings rows.
- -> XREF: D02 T04 §5 -- touch mode setting.
- -> XREF: D02 T02 §21 -- ViewSnapProvider.
- -> XREF: D04 T07 §21 -- combine form data step.
- -> XREF: D01 T03 §10 -- annot verb follows the registration rule.

## Outcome

- `src/pdf/annotations.cpp` reads, creates, modifies, deletes and flattens all 15 annotation types (`§1`) with deterministic `/AP` appearance stream synthesis (`§2`) so annotations render identically in Recto and third-party PDF viewers, plus `XFDF` and `JSON` import/export and `recto-cli annot <list|import|export|flatten>`.
- The viewer's `Markup & Annotation Bar` (`Ctrl+Shift+A`, `§3`), on-canvas 8-handle selection/resize frame, floating style popover, `Ctrl+Z` / `Ctrl+Y` undo/redo stack (`§4`), and `Comments / Annotations` side panel (`§5`) let users annotate pages, filter comments, reply, and jump to any markup.
- `src/pdf/forms.cpp` reads and writes AcroForms (`Tx`, `Btn` checkboxes/radios/pushbuttons, `Ch` combo/listboxes, `Sig` signature placeholders) with `/AP` synthesis and flattening (`§6`), converts XFA packets (`/XFA` template + datasets) to standard AcroForm fields, evaluates `/CO` numeric calculation expressions safely without JavaScript, imports/exports `JSON`, `FDF`, `XFDF` and `XML`, and powers `recto-cli forms <list|fill|export|import|flatten>` (`§7`).
- The viewer renders interactive form overlays on the page canvas in `Fill Form` mode (`§8`), provides visual drag-and-drop field placement, alignment guides, and tab-order badges in `Design Form` mode (`§9`), and offers the `Field Properties` inspector with UIA accessibility exposure (`§10`).
- `src/pdf/links.cpp` and the `Link` tool (`§11`, `§13`) author page-view, named-destination, other-PDF, web and `file:` links with a flyout to edit and delete them, and refuse every executable action; `src/pdf/destinations.cpp` and the `Destinations` panel (`§12`, `§14`) list, create, rename and delete named destinations and keep them valid when pages change.
- `src/pdf/articles.cpp`, the `Articles` panel and the `Article` tool (`§15`, `§16`) read, follow and author article threads.
- `src/pdf/measure.cpp`, the vector snap index and the `Measure` bar (`§17`, `§18`, `§19`) measure distance, perimeter and area against a page scale typed in drawing units, snap to endpoints, midpoints and intersections, and export measurements to CSV and JSON (`recto-cli annot measure`).
- `src/services/stamp_library.cpp`, `src/pdf/stamps.cpp` and the `Stamp Manager` (`§20`, `§21`) keep a library of custom image and dynamic stamps whose date, time, user and file tokens resolve once at application without JavaScript, with a `Keep stamp tool selected` mode.
- Review status (`§23`, `§24`), comment summary reports as list, PDF, Word and commented-pages PDF (`§25` to `§29`), bulk web link creation and removal (`§30`, `§31`), QR Code, Data Matrix and PDF417 placement and barcode form fields without JavaScript (`§32` to `§35`), flat-form field detection (`§36`, `§37`), image and date picker fields (`§38`, `§39`), combined form data to CSV, JSON and `.xlsx` (`§40`, `§41`), pen pressure and touch mode (`§42` to `§44`), cloud, arc, group and circle-measure markups (`§45` to `§51`), sticker packs (`§52`, `§53`) and accounting stamps (`§54`, `§55`) match the Foxit PDF Editor, PDFelement and UPDF features that Acrobat lacks, offline, on copies and without PDF scripts.

**Adjacency:** list=applicable @ D03 T03 §5; document=applicable @ D03 T03 §1; settings=applicable @ D02 T04 §5; reporting=not-applicable (comment counts appear in the side panel header, §5); notifications=applicable @ D03 T02 §5; permissions=not-applicable (PDF annotation/form permission flags are reported in Info, D02 T03 §9); audit=not-applicable (annotation author and modification timestamps are standard PDF dictionary entries in §1, not an external audit log); exchange=applicable @ D03 T03 §2; reverse=applicable @ D03 T03 §4

**Adjacency rationale:** The Comments side panel (`§5`) is the list surface, saving annotated or form-filled PDFs (`§1`, `§6`) is the document surface, `XFDF`/`JSON`/`FDF`/`XML` import and export (`§2`, `§7`) are the exchange surfaces, and the session `Undo`/`Redo` stack (`Ctrl+Z`/`Ctrl+Y` in `§4`) plus `Reset Form` (`§8`) are the reverse surfaces.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | PDF `/Annots` parser and serializer for all 15 annotation subtypes (`annotations.cpp`) | D01 T02 §5, D01 T02 §8 | [ ] |
| 2 | §2 | Deterministic `/AP /N` appearance synthesis, flattening, `XFDF`/`JSON` & `cli annot` | §1, D01 T03 §1 | [ ] |
| 3 | §3 | Contextual `40 epx` `Markup & Annotation Bar` (`Ctrl+Shift+A`), style & stamp pickers | §2, D02 T02 §8, D03 T01 §4 | [ ] |
| 4 | §4 | On-canvas annotation creation, 8-handle selection/resize, style popover & `Undo`/`Redo` | §3 | [ ] |
| 5 | §5 | Virtualized `Comments / Annotations` side panel (`SPEC 8.3`), replies & `XFDF`/`JSON` UI | §4, D02 T03 §3 | [ ] |
| 6 | §6 | `/AcroForm` field tree parser/writer (`Tx`, `Btn`, `Ch`, `Sig`), `/AP` & `flatten_forms` | §2 | [ ] |
| 7 | §7 | XFA-to-AcroForm converter, safe `/CO` evaluator, `JSON`/`FDF`/`XFDF` & `recto-cli forms` | §6 | [ ] |
| 8 | §8 | On-canvas interactive `Fill Form` mode (`SPEC 8.2`), `Tab` order, `/CO` & form data I/O | §4, §7, D02 T04 §5 | [ ] |
| 9 | §9 | Visual `Design Form` mode (`SPEC 8.2`), 7 widget tools, alignment guides & tab badges | §8 | [ ] |
| 10 | §10 | `Field Properties` inspector flyout (`Name`, `/TU`, masks, `Calculation`) and UIA peers | §9, D03 T02 §2 | [ ] |
| 11 | §11 | `/Link` authoring engine: page, named-destination, file and URI targets, never Launch | §1 | [ ] |
| 12 | §12 | Named destinations engine (`/Names /Dests`): list, create, rename, delete, remap | §1, D01 T01 §10, D01 T02 §5 | [ ] |
| 13 | §13 | `Link` tool, Link Properties flyout, and link edit and delete on the canvas | §3, §4, §11, §12, D02 T02 §9 | [ ] |
| 14 | §14 | `Destinations` side panel: list, create, rename, delete and jump | §12, §4, D02 T03 §3, D02 T03 §11, D03 T02 §5 | [ ] |
| 15 | §15 | Article thread engine (`/Threads` beads): read flow and thread authoring | §1 | [ ] |
| 16 | §16 | `Articles` panel, follow-the-article reading flow and `Article` authoring tool | §15, §3, §4, D02 T03 §3, D03 T02 §5 | [ ] |
| 17 | §17 | `/Measure` engine: scale, distance, perimeter and area, units and `Measure` export | §2 | [ ] |
| 18 | §18 | Snap point index from page vector content: endpoints, midpoints and intersections | D01 T02 §1, D01 T02 §7, D01 T02 §8 | [ ] |
| 19 | §19 | `Measure` tool bar, scale dialog, snapping, live readout and `Measurements` panel | §17, §18, §4, D02 T01 §10, D02 T03 §3 | [ ] |
| 20 | §20 | Stamp library store and dynamic stamp token resolution (no JavaScript) | §2, D02 T04 §2 | [ ] |
| 21 | §21 | Stamp Manager, custom image stamps, dynamic stamp editor and sticky Stamp tool | §20, §3, D02 T04 §5, D02 T01 §10 | [ ] |
| 22 | §22 | Fill and Sign: typed text, symbols and signatures on flat forms | §1, §2, §3, §4, §8, D05 T02 §10, D06 T03 §7 | [ ] |
| 23 | §23 | Review status state model engine (`/StateModel`, `/State`) in `annotations.cpp` | §1, §2 | [ ] |
| 24 | §24 | Review status in the Comments panel: show, set and filter | §23, §4, §5 | [ ] |
| 25 | §25 | Comment summary model, list exports and `recto-cli annot summarize` | §2, §23, D01 T03 §1, D03 T01 §1 | [ ] |
| 26 | §26 | Comment summary PDF: comments beside pages and a comments-only list | §25, D06 T03 §7 | [ ] |
| 27 | §27 | `Summarize Comments` dialog in the Comments panel | §5, §25, §26, D02 T01 §10 | [ ] |
| 28 | §28 | Export comments to Word `.docx` through the shared `DocxWriter` | §25, §27, D04 T03 §2 | [ ] |
| 29 | §29 | Save only the pages with comments as a new PDF | §25, §27, D05 T01 §2 | [ ] |
| 30 | §30 | Create web links from URLs in page text and remove all links | §11, D01 T03 §1, D03 T01 §1 | [ ] |
| 31 | §31 | `Create Links from URLs` and `Remove All Links` commands with a review list | §13, §30 | [ ] |
| 32 | §32 | QR Code, Data Matrix and PDF417 encoders with vector output | -- | [ ] |
| 33 | §33 | `Add Barcode` tool: place a QR Code, Data Matrix or PDF417 on a page | §2, §3, §4, §32, D02 T01 §10 | [ ] |
| 34 | §34 | Barcode form field engine: a text field whose barcode is built from other fields | §6, §7, §32 | [ ] |
| 35 | §35 | `Add Barcode Field` designer tool, field properties and live fill refresh | §9, §10, §34 | [ ] |
| 36 | §36 | Flat-form field detection engine and `recto-cli forms detect` | §6, D01 T02 §1, D01 T03 §1, D03 T01 §1, D03 T01 §7 | [ ] |
| 37 | §37 | `Detect Fields` in `Design Form` mode: review proposals and accept | §4, §9, §36 | [ ] |
| 38 | §38 | Image field engine: a button field whose appearance is a user-chosen image | §6, §20 | [ ] |
| 39 | §39 | Image field and date picker in `Design Form` and `Fill Form` modes | §8, §9, §10, §38 | [ ] |
| 40 | §40 | Combine form data from many filled PDFs: engine, CSV and `recto-cli forms combine` | §6, §7, D01 T03 §1 | [ ] |
| 41 | §41 | `Combine Form Data` dialog and `.xlsx` output | §8, §40, D02 T01 §10, D04 T05 §1 | [ ] |
| 42 | §42 | Pressure-sensitive ink appearance engine | §2 | [ ] |
| 43 | §43 | Pen input in the Ink tool: pressure, pen eraser and touch rejection | §4, §42, D02 T04 §5 | [ ] |
| 44 | §44 | Touch mode: larger targets for the annotation bar, handles and popovers | §3, §4, §5, D02 T04 §5 | [ ] |
| 45 | §45 | Cloud border effect (`/BE`) for Square, Circle, Polygon and FreeText | §2 | [ ] |
| 46 | §46 | Arc annotation engine | §2 | [ ] |
| 47 | §47 | Group and ungroup markups engine (`/RT /Group`) | §1, §46 | [ ] |
| 48 | §48 | Circle measurement engine: radius, diameter, circumference and area | §17 | [ ] |
| 49 | §49 | Multi-select and `Group` and `Ungroup` commands for markups | §4, §5, §47 | [ ] |
| 50 | §50 | `Cloud` and `Arc` tools on the Markup & Annotation Bar | §3, §4, §45, §46 | [ ] |
| 51 | §51 | `Circle` tool in the `Measure` bar | §19, §48 | [ ] |
| 52 | §52 | Sticker packs: shipped sticker sets and import of user sticker folders | §20 | [ ] |
| 53 | §53 | `Stickers` tab in the Stamp Picker, import and export of packs | §21, §52 | [ ] |
| 54 | §54 | Accounting tickmark stamp set and calculator stamp tokens | §20 | [ ] |
| 55 | §55 | Accounting set in the Stamp Picker and the `Sum of selection` stamp | §21, §54 | [ ] |

## 1. PDF `/Annots` parser and serializer for all 15 annotation subtypes (`annotations.cpp`)

Implement `src/pdf/annotations.h` and `src/pdf/annotations.cpp` in `recto-pdf`: parse, create, modify, and delete page `/Annots` dictionary entries covering all 15 standard PDF annotation subtypes (`Highlight`, `Underline`, `Squiggly`, `StrikeOut`, `Text` sticky note, `FreeText` callout/textbox, `Ink`, `Line` / `Arrow`, `Square` rectangle, `Circle` ellipse, `Polygon`, `PolyLine`, `Stamp`, `FileAttachment`, `Link`, and `Caret`) and persist mutations via `IncrementalWriter` (`D01 T02 §5`).

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Define typed `PdfAnnotation` models in `src/pdf/annotations.h` for all 15 subtypes and parse page `/Annots` arrays (including `/Rect`, `/QuadPoints`, `/C` RGB/CMYK/Gray colour, `/IC` interior colour, `/CA` opacity, `/BS` border width/dash, `/LE` line endings, `/InkList`, `/Vertices`, `/Contents`, `/T` author, `/M` PDF date string, `/NM` unique ID, `/IRT` in-reply-to reference, `/RT` `/R` (reply) or `/Group`, and `/Popup`). Done when: `PdfAnnotations.ParseAllSubtypes` reads every property from a 15-subtype fixture under ASan.
2. Implement `create_annotation`, `update_annotation`, and `delete_annotation` on `pdf::Document` and `DocumentActor`, writing modified `/Annots` arrays and annotation objects through `IncrementalWriter`. Done when: `PdfAnnotations.RoundTripAllSubtypes` creates all 15 subtypes, saves incrementally, re-opens, and verifies identical fields.

- [ ] Define `AnnotationSubtype` enum (all 15 subtypes) and `PdfAnnotation` struct with subtype-specific payloads (`QuadMarkupData`, `StickyNoteData`, `FreeTextData`, `InkData`, `LineData`, `ShapeData`, `PolyData`, `StampData`, `FileAttachmentData`, `LinkData`, `CaretData`) in `src/pdf/annotations.h`. Done when: `src/pdf/annotations.h` compiles with zero warnings across `recto-pdf` and `recto-core`. Cheaper substitute: parsing only `/Subtype /Highlight` and `/Text` and dropping all other 13 ISO 32000-1 annotation subtypes.
- [ ] Parse `/QuadPoints` (8 floats per quad, normalizing both counter-clockwise ISO 32000-1 and clockwise Acrobat quad vertex orderings) for `Highlight`, `Underline`, `Squiggly`, and `StrikeOut` annotations. Done when: `PdfAnnotations.QuadPointsOrdering` normalizes both vertex conventions into valid bounding quads.
- [ ] Parse and serialize `/InkList` (multi-stroke polyline/Bezier coordinate arrays), `/Vertices` (`Polygon` / `PolyLine`), and `/LE` start/end line-ending styles (`None`, `OpenArrow`, `ClosedArrow`, `Circle`, `Diamond`, `Butt`) for `Ink`, `Line`, `Polygon`, and `PolyLine`. Done when: multi-stroke ink and arrow annotations round-trip all coordinates and line endings losslessly.
- [ ] Parse and serialize `FreeText` (`/DA` default appearance font/size/colour, `/CL` callout knee-line coordinates, `/Q` quadding alignment) and `Stamp` (`/Name` standard names `Approved`, `Confidential`, `Draft`, `Final`, `ForComment`, `NotApproved`, `Void`, `SignHere` plus custom stamp text/date). Done when: `FreeText` callouts and `Stamp` dictionaries round-trip cleanly.
- [ ] Parse and serialize reply thread metadata (`/NM` unique UUID name, `/IRT` indirect reference to parent annotation, `/RT /R`, `/T` author string, `/CreationDate` and `/M` `D:YYYYMMDDHHmmSS` timestamps). Done when: nested reply chains reconstruct their exact parent-child comment tree.
- [ ] Expose `list_annotations(page_index)`, `add_annotation(page_index, PdfAnnotation)`, `update_annotation(page_index, PdfAnnotation)`, and `remove_annotation(page_index, nm_id)` on `DocumentActor` (`src/core/document_actor.cpp`). Done when: actor calls mutate the in-memory object graph and invalidate the page's `DisplayList` cache entry.
- [ ] Add `PdfAnnotations.ParseAllSubtypes` and `PdfAnnotations.RoundTripAllSubtypes` in `tests/recto-tests/pdf_annotations_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfAnnotations.Parse*:PdfAnnotations.RoundTrip*` passes under ASan.
- [ ] Commit: `feat(pdf): implement /Annots parser and serializer for all 15 annotation subtypes`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfAnnotations.Parse*:PdfAnnotations.RoundTrip*` passes under ASan across all 15 annotation subtypes.

## 2. Deterministic `/AP /N` appearance synthesis, flattening, `XFDF`/`JSON` & `cli annot`

Implement deterministic `/AP /N` normal appearance-stream synthesis for every created or modified annotation in `src/pdf/annotations.cpp` so `DisplayList` compilation (`D01 T02 §4`) renders them natively and third-party PDF viewers display identical visuals; add annotation flattening (`flatten_annotations`), `XFDF` (ISO 19444-1 XML) and `JSON` import/export, and `recto-cli annot <list|import|export|flatten>`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `/AP /N` Form XObject stream synthesis (`/Type /XObject /Subtype /Form`, `/BBox`, `/Matrix`, `/Resources` with `/ExtGState` `/CA`/`/ca` multiply blend mode for highlights) for all 15 annotation subtypes, and update `DisplayList` compilation (`D01 T02 §4`) to replay `/AP /N` streams during page rendering. Done when: created annotations rasterize through `RenderPool` and match reference renders in third-party viewers.
2. Implement `flatten_annotations(page_range)` (merging each annotation's `/AP /N` Form XObject into the page `/Contents` stream with `/Matrix` + `/Rect` placement and removing `/Annots`), `XFDF` (ISO 19444-1) and `JSON` annotation import/export, and `recto-cli annot <list|import|export|flatten>`. Done when: flattened pages have zero mutable `/Annots` while preserving pixel-identical appearance (`SSIM >= 0.99`), and `XFDF`/`JSON` round-trip preserves all comments and replies under ASan.

- [ ] Synthesize `/AP /N` Form XObject streams for text markup (`Highlight` with `/BM /Multiply` translucent rounded-cap quad fill, `Underline` straight baseline stroke, `Squiggly` sawtooth wave stroke, `StrikeOut` mid-x-height stroke). Done when: highlighted text remains crisp and dark underneath the highlight colour via `/BM /Multiply`. Cheaper substitute: writing `/Annots` dictionaries without `/AP /N` streams so third-party PDF readers render blank/invisible annotations.
- [ ] Synthesize `/AP /N` Form XObject streams for geometric and ink annotations (`Ink` smooth cubic Bezier/polyline strokes, `Line` with rotated arrowhead polygons for `/LE`, `Square`, `Circle` 4-arc cubic Bezier ellipse, `Polygon`, `PolyLine`, `Caret`, and `Sticky Note` speech-bubble/note icon). Done when: `PdfAnnotations.AppearanceStreams` verifies non-empty valid content streams for each subtype.
- [ ] Synthesize `/AP /N` Form XObject streams for `FreeText` (word-wrapped text runs using `/DA` font/size/colour, callout leader line and border box) and `Stamp` (pill border + bold uppercase stamp label and optional author/date line). Done when: `FreeText` and `Stamp` annotations rasterize legible text and borders.
- [ ] Implement `flatten_annotations(pdf::Document&, PageRange)` appending `q <cm matrix> /AnnotFmN Do Q` operators to each page's `/Contents` stream, registering `/AnnotFmN` in `/Resources /XObject`, and removing flattened non-link annotations from `/Annots`. Done when: `PdfAnnotations.FlattenPixelParity` confirms `SSIM >= 0.99` before vs after flattening and `0` remaining markup `/Annots`.
- [ ] Implement ISO 19444-1 `XFDF` (`<xfdf><annots>...</annots></xfdf>`) and structured `JSON` import and export in `src/pdf/annotations_xfdf.cpp` covering all 15 subtypes, `/QuadPoints`, `/InkList`, colours, authors, dates, and `/IRT` reply threads. Done when: exporting a multi-page annotated PDF to `XFDF` and `JSON` and importing into an clean copy reproduces every annotation and reply thread losslessly.
- [ ] Implement `recto-cli annot <list|import|export|flatten> <file.pdf> [--xfdf <path> | --json <path>] [--out <out.pdf>]` in `src/cli/cmd_annot.cpp`. Done when: `Cli.AnnotCommand` tests pass for all four subcommands under ASan.
- [ ] Commit: `pdf: annotations engine, /AP synthesis, flattening and XFDF/JSON I/O`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfAnnotations.*:Cli.AnnotCommand` passes under ASan covering all 15 subtypes, `/AP` appearance streams, flattening (`SSIM >= 0.99`), and `XFDF`/`JSON` round-tripping.

## 3. Contextual `40 epx` `Markup & Annotation Bar` (`Ctrl+Shift+A`), style & stamp pickers

Build the contextual `40 epx` `Markup & Annotation Bar` (`src/app/controls/MarkupAnnotationBar.xaml/.cpp`, toggled via the main toolbar `Annotate` button, floating page toolbar `Annotate / Tools` button in `D02 T02 §8`, or `Ctrl+Shift+A`, `SPEC 8.2`), including tool buttons for all markup/drawing/stamp tools, the `Colour, Opacity & Stroke Picker` flyout (`5` theme-aware swatches + custom colour + stroke thickness `1-12 pt` + opacity `25-100%`), the `Stamp Picker` flyout, and quick-action wiring from `D03 T01 §4`'s floating selection toolbar (`Highlight`, `Underline`, `Strikeout`, `Add Note`).

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Markup & Annotation Bar`), 9.3 (`SelectionFloatingToolbar` quick markup actions), 10 (`Ctrl+Shift+A`, `Ctrl+Z`, `Ctrl+Y`), and 11 (`32 epx` hit targets, automation names and tooltips).
**Job:** The user can toggle the annotation strip with `Ctrl+Shift+A`, pick any markup, drawing, shape, or stamp tool, choose colour/opacity/stroke width, or immediately highlight/strikeout/note selected text from the floating selection toolbar. Consumer: `DocumentView` annotation canvas (`§4`), `SelectionFloatingToolbar` (`D03 T01 §4`), and `CommandRegistry` (`Ctrl+K`).
**Treatment:** `40 epx` contextual bar below the main toolbar (or top of the canvas in `Focus` preset) bound to `AnnotationToolViewModel`, with theme-aware swatch tokens from `RectoColors.xaml`. Cheaper substitute that fails the checkpoint: burying annotation tools inside nested submenus with no contextual bar or missing stroke/opacity controls.
**Chrome:** consume `RectoColors.xaml` surface/border/swatch tokens, `D02 T02 §8`'s `Annotate / Tools` button, and `D03 T01 §4`'s selection toolbar hooks.

**Build order:**

1. Implement `AnnotationToolViewModel` (`src/services/annotation_tool_view_model.cpp`) managing active tool selection (`Select`, `Highlight`, `Underline`, `Squiggly`, `StrikeOut`, `StickyNote`, `FreeText`, `Callout`, `Ink`, `Eraser`, `Rectangle`, `Ellipse`, `Line`, `Arrow`, `Polygon`, `Stamp`), active colour swatch, opacity (`25%..100%`), stroke width (`1..12 pt`), stamp preset, and quick-selection markup commands. Done when: `AnnotationUi.ToolViewModel` verifies tool state transitions and quick-highlight creation from `SelectionModel` quads.
2. Build `MarkupAnnotationBar` (`src/app/controls/MarkupAnnotationBar.xaml/.cpp`) with `Ctrl+Shift+A` toggle, colour/stroke/opacity flyout, stamp flyout (`Approved`, `Confidential`, `Draft`, `Final`, `Sign Here`, custom text/date), `Flatten Annotations`, and `Undo`/`Redo` buttons. Done when: `Ctrl+Shift+A` opens the `40 epx` bar and every button carries an automation name and tooltip.

- [ ] Implement `AnnotationToolViewModel` in `src/services/annotation_tool_view_model.h/.cpp` holding active `AnnotationToolMode`, active colour (`5` presets: Yellow, Green, Blue, Pink, Red + custom RGB), opacity (`0.25f..1.0f`), stroke thickness (`1.0f..12.0f pt`), and stamp template. Done when: changing swatch, opacity, or stroke updates the default for new annotations or applies immediately to the currently selected annotation (`§4`).
- [ ] Build `MarkupAnnotationBar` (`40 epx` height, `RectoSurfaceBrush` background, `1 epx` `RectoBorderSubtleBrush` bottom border) with grouped tool toggle buttons: Text Markup (`Highlight`, `Underline`, `Squiggly`, `StrikeOut`), Notes & Text (`Sticky Note`, `FreeText`, `Callout`), Freehand (`Ink Draw`, `Eraser`), Shapes (`Rectangle`, `Ellipse`, `Line`, `Arrow`, `Polygon`), `Stamp`, `Colour & Stroke Picker`, `Flatten Annotations`, and `Undo` / `Redo`. Done when: `MarkupAnnotationBar` renders at `40 epx` height and all buttons pass `D03 T02 §3` hit-target and automation-name checks.
- [ ] Build the `Colour, Opacity & Stroke Picker` flyout with `5` theme-aware colour swatches, custom colour picker, stroke thickness slider/chips (`1, 2, 4, 8, 12 pt`), and opacity slider (`25% - 100%`). Done when: selecting a swatch and opacity updates the toolbar swatch indicator and active tool state.
- [ ] Build the `Stamp Picker` flyout listing standard stamps (`Approved`, `Confidential`, `Draft`, `Final`, `Sign Here`, `Void`) plus a `Custom Stamp...` inline input (custom label + optional current date/author toggle). Done when: picking a standard or custom stamp arms `AnnotationToolMode::Stamp` with a live stamp preview. Cheaper substitute that fails the checkpoint: fixed hard-coded stamps with no custom text/date stamp option.
- [ ] Wire `SelectionFloatingToolbar` (`D03 T01 §4`) `Highlight`, `Strikeout`, and context-menu `Highlight` / `Underline` / `Strikeout` / `Add Note` directly to `AnnotationToolViewModel::apply_selection_markup(subtype)` creating a quad-precise `PdfAnnotation` over `SelectionModel`'s current `CharQuad`s. Done when: selecting text and clicking `Highlight` on the floating selection toolbar immediately creates a `/QuadPoints` highlight annotation and clears the selection.
- [ ] Wire `Ctrl+Shift+A`, the floating page toolbar's `Annotate / Tools` button (`D02 T02 §8`), and `Ctrl+K` (`Toggle Markup & Annotation Bar`, `Flatten Annotations`) to `MarkupAnnotationBar`. Done when: `Ctrl+Shift+A` toggles the bar open and `Esc` returns to the `Select` tool or closes the bar.
- [ ] Add `AnnotationUi.MarkupBarAndQuickSelection` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=AnnotationUi.MarkupBarAndQuickSelection` passes.
- [ ] Commit: `feat(app): add contextual Markup & Annotation Bar (Ctrl+Shift+A), colour/stroke picker, and stamp picker`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AnnotationUi.MarkupBarAndQuickSelection` passes, verifying `Ctrl+Shift+A` bar toggle, swatch/stroke/stamp pickers, and floating-selection quick highlight creation.

## 4. On-canvas annotation creation, 8-handle selection/resize, style popover & `Undo`/`Redo`

Implement interactive on-canvas annotation creation, hit-testing, 8-handle selection/resize/endpoint manipulation, the floating style/delete popover, and the per-document `Undo` / `Redo` (`Ctrl+Z` / `Ctrl+Y`) command stack on `DocumentView` (`src/app/views/DocumentView.xaml.cpp` and `src/services/annotation_interaction.cpp`).

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (on-canvas 8-handle selection box in `--accent`, floating style/delete popover, live creation preview) and 10 (`Ctrl+Z`, `Ctrl+Y`, `Delete`, `Esc`).
**Job:** The user can draw ink strokes, shapes, arrows, callouts, sticky notes, or text markups directly on the page canvas, click any existing annotation to move or resize it via 8 handles, change its colour/thickness or delete it from a floating popover, and undo/redo any annotation change with `Ctrl+Z` / `Ctrl+Y`. Consumer: `DocumentActor` annotation store (`§1`-`§2`) and the `Comments / Annotations` side panel (`§5`).
**Treatment:** Live vector preview overlay on `DocumentView` during pointer drag, committing to `DocumentActor` on pointer release so `/AP /N` synthesizes and re-renders tiles cleanly, backed by a reversible `AnnotationUndoStack` (`Add`, `Modify`, `Delete`, `BatchErase`). Cheaper substitute that fails the checkpoint: toolbar buttons that drop fixed-size shapes without interactive drag/resize handles, or missing `Ctrl+Z`/`Ctrl+Y` undo/redo.
**Chrome:** consume `RectoAccentBrush` for the selection bounding box and `8` (`8 x 8 epx`) resize/endpoint handles, and `RectoFloatingBarBackgroundBrush` for the floating style/delete popover.

**Build order:**

1. Implement on-canvas creation gestures for all tool modes (`Highlight`/`Underline`/`Squiggly`/`StrikeOut` text-quad drag, `StickyNote` click-to-place + inline note editor, `FreeText`/`Callout` box drag + inline `TextBox`, `Ink` pointer stroke capture + Ramer-Douglas-Peucker smoothing + `Eraser` stroke hit-test, `Rectangle`/`Ellipse`/`Line`/`Arrow`/`Polygon` drag preview, `Stamp` click/drag placement). Done when: every tool mode creates a valid `PdfAnnotation` in page-point coordinates on pointer completion.
2. Implement annotation hit-testing, the `8`-handle selection/resize/endpoint overlay, the floating style/delete popover, and the per-document `Undo`/`Redo` stack (`Ctrl+Z` / `Ctrl+Y`). Done when: `AppTests.AnnotationsCanvasAndUndo` creates, moves, resizes, restyles, deletes, undoes, and redoes annotations on canvas.

- [ ] Implement on-canvas text-markup drag (`Highlight`, `Underline`, `Squiggly`, `StrikeOut`) snapping pointer drag endpoints to `PageText` character quads (`D03 T01 §3`) and committing a `/QuadPoints` annotation on pointer release. Done when: dragging with the `Highlight` tool highlights exact character quads across wrapped lines.
- [ ] Implement on-canvas `Ink` freehand drawing (capturing pointer samples in page points, smoothing via Catmull-Rom / Ramer-Douglas-Peucker, and grouping strokes painted within `800 ms` into a single `/InkList` annotation) and `Eraser` (removing intersected ink strokes or whole ink annotations). Done when: drawing and erasing freehand ink updates the page smoothly at 60 fps.
- [ ] Implement on-canvas drag creation for `Rectangle`, `Ellipse` (`Shift` constrains to square/circle), `Line` / `Arrow` (`Shift` snaps to `45°` increments), `Polygon` / `PolyLine` (click vertices, double-click or click start vertex to close), `Sticky Note` (click to place + open inline comment flyout), `FreeText` / `Callout` (drag knee + text box with inline text editing), and `Stamp`. Done when: all shape, note, callout, and stamp tools author valid annotations on canvas.
- [ ] Implement annotation hit-testing and the active selection frame: `8` square (`8 x 8 epx`) `--accent` resize handles around rectangular annotations (`Rect`, `Ellipse`, `FreeText`, `Stamp`, `Ink` bounds) or vertex/endpoint handles on `Line`/`Arrow`/`Polygon`/`Callout`. Done when: dragging any handle or the annotation body moves or resizes the annotation in page space and regenerates its `/AP /N` stream on release. Cheaper substitute that fails the checkpoint: static annotations that cannot be moved or resized after placement.
- [ ] Build the floating annotation popover anchored above the selected annotation with quick colour swatches, opacity/thickness sliders, `Reply / Edit Note`, and `Delete` (`Delete` / `Backspace` key). Done when: clicking an existing annotation shows the selection handles and floating style/delete popover.
- [ ] Implement `AnnotationUndoStack` (`src/services/annotation_undo.h/.cpp`, integrated with `D02 T03 §6`'s per-document history stack) recording reversible `CreateAnnotation`, `ModifyAnnotation`, and `DeleteAnnotation` commands wired to `Ctrl+Z` and `Ctrl+Y`. Done when: `Ctrl+Z` and `Ctrl+Y` undo and redo creation, move/resize, style change, and deletion across multiple steps.
- [ ] Turn on offline spell checking in the sticky note, comment reply and `FreeText` inline editors by binding `IsSpellCheckEnabled` to `CheckSpellingWhileTyping` and `Language` to `SpellingLanguage` (`SpellService`, `D03 T01 §14`), re-applied when either setting changes. Done when: `AppTests.AnnotationsCanvasAndUndo` asserts the three editors report `IsSpellCheckEnabled` true with the configured `Language`, and false after the setting is turned off.
- [ ] Add `AppTests.AnnotationsCanvasAndUndo` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=AppTests.AnnotationsCanvasAndUndo` passes.
- [ ] Commit: `feat(app): implement on-canvas annotation creation, 8-handle resize, style popover, and Undo/Redo`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.AnnotationsCanvasAndUndo` passes, verifying on-canvas creation, 8-handle resize, style popover editing, and `Ctrl+Z`/`Ctrl+Y` undo/redo.

## 5. Virtualized `Comments / Annotations` side panel (`SPEC 8.3`), replies & `XFDF`/`JSON` UI

Build the `Comments / Annotations` side panel (`src/app/views/panels/CommentsPanel.xaml/.cpp`, `SPEC 8.3`) hosted inside `SidePanelHost` (`D02 T03 §3`): virtualized page-grouped comment cards, inline `/IRT` reply threads, type and author filter chips, search box, click-to-jump-and-select synchronization with `DocumentView`, and `Import / Export Comments (XFDF or JSON)` toolbar actions.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.3 (`Comments / Annotations` side panel) and 11 (accessible card labels, keyboard navigation).
**Job:** A reviewer can browse all annotations and comments across the document grouped by page, filter by annotation type or author, search comment text, post inline replies, jump to any annotation on canvas, or import/export comments as `XFDF` or `JSON`. Consumer: `SidePanelHost` (`D02 T03 §3`), `DocumentView` (`§4`), and `XFDF`/`JSON` serializer (`§2`).
**Treatment:** Virtualized `ItemsRepeater` / `ListView` grouped by page number, synchronized two-way with `AnnotationToolViewModel` and `DocumentActor`, with inline reply boxes and `FileOpenPicker` / `FileSavePicker` for `XFDF` and `JSON`. Cheaper substitute that fails the checkpoint: a flat ungrouped list with no replies, no type/author filtering, or no `XFDF` import/export.
**Chrome:** consume `SidePanelHost` (`D02 T03 §3`), `RectoColors.xaml` card/border tokens, and Segoe Fluent Icons per annotation subtype.

**Build order:**

1. Implement `CommentsPanelViewModel` (`src/services/comments_panel_view_model.cpp`) grouping annotations by page, assembling `/IRT` parent-child reply trees, filtering by text query, subtype chip (`All`, `Highlights`, `Notes`, `Text`, `Ink & Shapes`, `Stamps`), and author, and synchronizing active selection with `DocumentView`. Done when: `AnnotationUi.CommentsViewModel` verifies grouping, reply threading, and filtering across 100+ annotations.
2. Build `CommentsPanel.xaml/.cpp` with the search box, filter chips, comment count badge, page-grouped cards with inline edit/reply/delete actions, and `Import Comments...` / `Export Comments...` (`XFDF` / `JSON`) buttons. Done when: `AppTests.AnnotationsUi` drives creating a highlight and sticky note, replying in the side panel, filtering by type, exporting `XFDF`, and flattening.

- [ ] Implement `CommentsPanelViewModel` in `src/services/comments_panel_view_model.h/.cpp` observing `DocumentActor` annotation mutations so adding, editing, undoing, or deleting an annotation on canvas updates the panel list live. Done when: canvas mutations reflect in `CommentsPanelViewModel` within the same frame.
- [ ] Group comment cards by page (`Page N` sticky/collapsible group header with per-page count) ordered top-to-bottom by page `/Rect` Y coordinate, and nest `/IRT` `/RT /R` annotations under their parent card in chronological order. Done when: `AnnotationUi.CommentsGroupingAndReplies` verifies page grouping and multi-level reply threads.
- [ ] Implement filter chips (`All`, `Markup`, `Notes`, `FreeText`, `Drawings`, `Stamps`), author filter dropdown, and instant text search across `/Contents` and `/T` author strings. Done when: filtering by `Notes` or typing a search phrase narrows the visible groups immediately and shows a clean empty state when 0 items match.
- [ ] Render each comment card with subtype icon + colour swatch dot, author name, relative/formatted `/M` timestamp, editable `/Contents` text (or quoted highlighted `PageText` snippet when `/Contents` is empty), `Reply` inline input, and `Delete` button. Done when: adding a reply creates a linked `/IRT` `/RT /R` annotation in `DocumentActor`. Cheaper substitute that fails the checkpoint: read-only cards with no inline comment editing or reply threading.
- [ ] Synchronize selection two ways: clicking a comment card in `CommentsPanel` scrolls `DocumentView` to that page and selects the annotation on canvas (`§4`); clicking an annotation on canvas scrolls `CommentsPanel` to its card and highlights it. Done when: two-way canvas/panel selection sync passes in `AppTests.AnnotationsUi`.
- [ ] Wire `Import Comments (XFDF / JSON)` and `Export Comments (XFDF / JSON)` header menu buttons to §2's `XFDF` and `JSON` serializer. Done when: exporting `XFDF` from the panel writes a valid ISO 19444-1 file and importing it into an unannotated copy restores all cards.
- [ ] Commit: `app: markup and annotation bar, canvas handles and comments side panel`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AnnotationUi.*:AppTests.AnnotationsUi` passes, quoting tool activation (`Ctrl+Shift+A`), highlight and shape creation, handle resize, `Ctrl+Z`/`Ctrl+Y` undo/redo, Comments panel filtering, reply threading, and `XFDF` export.

## 6. `/AcroForm` field tree parser/writer (`Tx`, `Btn`, `Ch`, `Sig`), `/AP` & `flatten_forms`

Implement the core `/AcroForm` engine in `src/pdf/forms.h` and `src/pdf/forms.cpp`: parse and mutate `/AcroForm` field hierarchies (`/Fields`, parent/child `/Kids`, inherited `/FT`, `/T`, `/TU` tooltip, `/TM`, `/Ff` field flags, `/V` value, `/DV` default value, `/DA` default appearance, `/DR` default resources, `/Opt` choice options, `/MaxLen`, `/Q` alignment) for all four PDF field types (`Tx` text, `Btn` checkbox/radio/pushbutton, `Ch` combo/listbox, `Sig` signature placeholder), synthesize deterministic `/AP /N` appearance streams on value or property change, and implement `flatten_forms(page_range)`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Parse `/AcroForm` field trees (`Tx`, `Btn`, `Ch`, `Sig`) with full inheritance along `/Parent` -> `/Kids` chains and implement field value mutation (`set_field_value`) and new field creation (`create_form_field`, `update_form_field`, `delete_form_field`). Done when: `PdfForms.ParseAndMutate` reads inherited attributes, updates text/checkbox/radio/choice values, and creates new widget fields across all 4 `/FT` types under ASan.
2. Implement deterministic `/AP /N` appearance-stream regeneration for modified and newly created form widgets (single-line, multiline, password `*`, comb cells, checkmark/circle glyphs, combo/list selection highlights) and `flatten_forms(page_range)`. Done when: filled and flattened forms render identically to filled unflattened forms (`SSIM >= 0.99`) and have zero remaining interactive `/AcroForm /Fields`.

- [ ] Define `FormFieldType` (`Text`, `CheckBox`, `RadioButton`, `PushButton`, `ComboBox`, `ListBox`, `Signature`), `FormFieldFlags` (`ReadOnly`, `Required`, `NoExport`, `Multiline`, `Password`, `Comb`, `Radio`, `Pushbutton`, `Combo`, `Edit`, `MultiSelect`), and `PdfFormField` in `src/pdf/forms.h`. Done when: `src/pdf/forms.h` models all ISO 32000-1 section 12.7 field properties. Cheaper substitute: reading only flat top-level `/Fields` arrays and ignoring `/Parent` -> `/Kids` inheritance or `/AP /N` synthesis.
- [ ] Walk `/AcroForm /Fields` and recursive `/Kids` arrays in `src/pdf/forms.cpp` (bounded at depth 32 against cyclic `/Kids` graphs) resolving inherited `/FT`, `/Ff`, `/DA`, `/DR`, `/Q`, `/V`, `/DV`, and `/Opt`, and building the fully qualified dotted field name (`parent.child`). Done when: `PdfForms.HierarchyAndCycles` resolves nested field names and rejects cyclic `/Kids` chains with `PdfError::MalformedPdf`.
- [ ] Implement `set_field_value` and `reset_form_fields` (restoring `/V` to `/DV` or clearing `/V`), enforcing radio-button group mutual exclusion (`/Ff` `Radio` + `/AS` `/Off` on sibling widgets) and `/MaxLen` character caps. Done when: toggling one radio widget in a group automatically sets sibling widgets' `/AS` appearance states to `/Off`.
- [ ] Implement `create_form_field`, `update_form_field`, and `delete_form_field` (creating `/AcroForm` and default `/DR` `/Helv` / `/ZaDb` resources when adding the first field to a non-form PDF, and updating page `/Tabs` order). Done when: `PdfForms.AuthorNewForm` creates a fillable form on a blank PDF from scratch.
- [ ] Synthesize `/AP /N` Form XObject appearance streams (`Tx ... EMC`) for `Tx` fields (single-line vertically centered, multiline word-wrapped, password bullets, and `Comb` evenly spaced cell dividers with centered characters), `Btn` checkboxes/radios (`/ZaDb` checkmark/dot or vector path), and `Ch` combo/list boxes using `/DA` font/size/colour and `D01 T02 §2` metrics. Done when: filled form fields render their updated values through `DisplayList` rasterization even when `/NeedAppearances` is `false`.
- [ ] Implement `flatten_forms(pdf::Document&, PageRange)` baking every widget's `/AP /N` stream into the page `/Contents` stream, removing widget annotations from `/Annots`, and clearing `/AcroForm /Fields`. Done when: `PdfForms.FlattenForms` confirms `SSIM >= 0.99` pixel parity and `0` remaining interactive fields.
- [ ] Add `PdfForms.ParseAndMutate`, `PdfForms.AuthorNewForm`, and `PdfForms.FlattenForms` in `tests/recto-tests/pdf_forms_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfForms.*` passes under ASan.
- [ ] Commit: `feat(pdf): implement /AcroForm field tree parser/writer, /AP synthesis, and flatten_forms`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfForms.ParseAndMutate:PdfForms.AuthorNewForm:PdfForms.FlattenForms` passes under ASan.

## 7. XFA-to-AcroForm converter, safe `/CO` evaluator, `JSON`/`FDF`/`XFDF` & `recto-cli forms`

Extend `src/pdf/forms.cpp` with:
1. A static and dynamic **XFA-to-AcroForm converter** (`/AcroForm /XFA` XML `template` + `datasets` -> positioned standard AcroForm widget annotations with populated values so XFA forms can be viewed, filled, and flattened without JavaScript).
2. A safe **calculation-order (`/CO`) evaluator** (`SUM`, `PRD`, `AVG`, `MIN`, `MAX`, and basic arithmetic field formulas evaluated deterministically in `/CO` dependency order with zero JavaScript execution).
3. **`JSON`, `FDF`, `XFDF`, and `XML` form data import and export**, plus `recto-cli forms <list|fill|export|import|flatten>`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Implement the safe `/CO` calculation evaluator (`src/pdf/forms_calc.cpp`) and the XFA XML (`template` + `datasets`) to AcroForm widget converter (`src/pdf/forms_xfa.cpp`). Done when: `PdfForms.CalcOrder` evaluates `SUM`/`PRD`/`AVG`/`MIN`/`MAX` and arithmetic formulas without executing JS, and `PdfForms.XfaConversion` converts an XFA fixture into fillable AcroForm widgets with `/AP` streams under ASan.
2. Implement `JSON`, `FDF` (ISO 32000-1 section 12.7.7), `XFDF` (`<fields>`), and `XML` form data import/export and `recto-cli forms <list|fill|export|import|flatten>`. Done when: `PdfForms.DataIo` and `Cli.FormsCommand` round-trip field values across all 4 formats under ASan.

- [ ] Implement the safe `/CO` calculation parser and evaluator in `src/pdf/forms_calc.cpp` recognizing standard Acrobat `AFSimple_Calculate("SUM"|"PRD"|"AVG"|"MIN"|"MAX", [...])` expressions and explicit designer formulas (`SUM(f1, f2)`, `f1 * f2`, `f1 + f2 - f3`) while never executing arbitrary PDF JavaScript (D9, D12). Done when: modifying a line-item quantity or unit price field automatically recalculates subtotal, tax, and grand total fields in `/CO` order. Cheaper substitute: ignoring `/CO` calculated fields or embedding a JavaScript interpreter to run untrusted form scripts.
- [ ] Implement numeric, currency, percentage, and date formatting for calculated and masked fields (`AFNumber_Format`, `AFDate_Format`, decimal places, currency symbol, thousands separator). Done when: `PdfForms.CalcAndFormat` formats `$1,234.50` and `2026-10-14` accurately in synthesized `/AP /N` streams.
- [ ] Implement `convert_xfa_to_acroform` in `src/pdf/forms_xfa.cpp`: extract the `/AcroForm /XFA` stream (single stream or `[preamble, config, template, datasets, postamble]` array), parse the XML `<template>` (`<subform>`, `<field>`, `<draw>`, `<ui><textEdit|checkButton|choiceList|numericEdit|dateTimeEdit>`) and `<datasets><xfa:data>` values, lay out subforms into PDF page coordinates, and synthesize standard `/AcroForm` widget annotations. Done when: `PdfForms.XfaConversion` converts both static and dynamic XFA fixtures into standard fillable `/AcroForm` fields.
- [ ] Implement form data import and export for `JSON` (`{"fields": {"name": value}}`), `FDF` (`%FDF-1.2` `/FDF << /Fields [...] >>`), `XFDF` (`<xfdf><fields><field name="..."><value>...</value></field></fields></xfdf>`), and `XML` (`<fields>`). Done when: `PdfForms.DataIo` exports and re-imports form values losslessly across all 4 formats, recalculating `/CO` fields and regenerating `/AP /N` streams on import.
- [ ] Implement `recto-cli forms <list|fill|export|import|flatten> <file.pdf> [--data <path>] [--format json|fdf|xfdf|xml] [--set <name=value>...] [--out <out.pdf>]` in `src/cli/cmd_forms.cpp`. Done when: `Cli.FormsCommand` verifies all 5 subcommands and exit codes under ASan.
- [ ] Expose `FormFillCommand`, `FormResetCommand`, `FormImportDataCommand`, `FormExportDataCommand`, and `FormFlattenCommand` on `DocumentActor` (`src/core/document_actor.cpp`). Done when: actor operations execute off the UI thread and return updated field snapshots.
- [ ] Commit: `pdf: AcroForms, XFA-to-AcroForm converter, /AP synthesis and form data I/O`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfForms.*:Cli.FormsCommand` passes under ASan covering AcroForm field types, XFA conversion, `/CO` calculation order, `/AP` synthesis, flattening, and `JSON`/`FDF`/`XFDF`/`XML` import/export.

## 8. On-canvas interactive `Fill Form` mode (`SPEC 8.2`), `Tab` order, `/CO` & form data I/O

Build the interactive `Fill Form` experience specified in `design/SPEC.md` section 8.2 (`Form Fill & Form Designer Bar`): when a PDF containing `/AcroForm` or `/XFA` opens (or when `Forms` is invoked from the toolbar or `Ctrl+K`), display the `40 epx` contextual `Form Fill & Form Designer Bar` and render live WinUI 3 interactive field overlays aligned to each field's page quad (`TextBox` for single/multiline/password/comb text, `CheckBox`, `RadioButton`, `ComboBox`, `ListBox`, and clickable `Signature Field` banner), with `Highlight Required Fields` (`--accent-subtle` fill and `--danger` border on required empty fields), `Tab` / `Shift+Tab` field navigation in `/Tabs` (`/R`, `/C`, `/S`) order, live `/CO` calculated field updates, `Clear / Reset Form`, `Import Data` (`JSON`, `FDF`, `XFDF`, `XML`), `Export Data`, and `Flatten Form`.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Form Fill & Form Designer Bar` -- Fill Mode), 9.7 (`Highlight form fields` setting), 10 (`Tab` / `Shift+Tab` field traversal), and 11 (accessibility labels from `/TU` tooltips).
**Job:** Users can fill out interactive AcroForm and XFA-converted PDF forms directly on the page canvas with keyboard and mouse, watch calculated totals update live, reset or flatten the form, and import or export form data in `JSON`, `FDF`, `XFDF`, or `XML`. Consumer: `DocumentView` (`D02 T02`), `DocumentActor` (`§6`-`§7`), and `Settings` (`D02 T04 §5`).
**Treatment:** Live on-canvas field overlay layer scaled and positioned via `D02 T02 §5`'s `page_to_viewport` transform, synchronized with `FormViewModel` and `DocumentActor`. Cheaper substitute that fails the checkpoint: static rendering that requires opening a modal dialog per field, or ignoring `/Tabs` navigation order and `/CO` calculations.
**Chrome:** consume `RectoAccentSubtleBrush` (fillable field tint), `RectoDangerBrush` (required empty field border), and `D02 T04 §5`'s `Highlight form fields` setting.

**Build order:**

1. Implement `FormViewModel` (`src/services/form_view_model.cpp`) managing `FillMode` vs `DesignMode`, visible page field overlays, `/Tabs` (`Row`, `Column`, `Structure`) focus traversal order, live `/CO` recalculation on field edit, `Highlight Required Fields` state (seeded from `D02 T04 §5`), `Reset Form`, `Import Data`, `Export Data`, and `Flatten Form`. Done when: `FormUi.FillViewModel` verifies field edits, live `/CO` updates, and `/Tabs` traversal order.
2. Build `FormBar` (`src/app/controls/FormBar.xaml/.cpp`, `40 epx` strip) and `FormCanvasOverlay` (`src/app/controls/FormCanvasOverlay.xaml/.cpp`) rendering interactive `TextBox`, `CheckBox`, `RadioButton`, `ComboBox`, `ListBox`, and `Signature` overlays over `DocumentView`. Done when: `AppTests.FormFillUi` tabs through a multi-field form fixture, types text, toggles checkboxes, selects a combo option, verifies calculated total update, and exports `JSON`.

- [ ] Implement `FormViewModel` in `src/services/form_view_model.h/.cpp` detecting when an opened document has `/AcroForm` or `/XFA` fields (automatically running §7's XFA-to-AcroForm converter when `/XFA` is present and displaying a polite `"Converted XFA form for interactive filling"` banner). Done when: opening an AcroForm or XFA PDF activates `FormViewModel` automatically.
- [ ] Build the `40 epx` contextual `FormBar` (`SPEC 8.2`) with mode switch (`Fill Form` | `Design Form`), `Highlight Fields` toggle (initialized from Settings `Highlight form fields` in `D02 T04 §5`), `Clear / Reset Form`, `Import Data...` (`JSON`/`FDF`/`XFDF`/`XML`), `Export Data...`, and `Flatten Form`. Done when: `FormBar` renders at `40 epx` height and all controls pass `D03 T02 §3` accessibility checks.
- [ ] Build `FormCanvasOverlay` positioning interactive WinUI 3 controls over each visible page's widget `/Rect` using `D02 T02 §5`'s `page_to_viewport` transform (`TextBox` for `Tx` single-line/multiline/password/comb, `CheckBox` and `RadioButton` for `Btn`, `ComboBox` and `ListBox` for `Ch`, and a clickable `Sign Here` badge for `Sig` linked to `D05 T02 §10`). Done when: zooming or scrolling keeps all active form controls anchored to their exact page coordinates. Cheaper substitute that fails the checkpoint: modal popup editing per field instead of on-canvas controls.
- [ ] Paint `--accent-subtle` (`RectoAccentSubtleBrush`) background tint on fillable fields when `Highlight Fields` is active, and a `1.5 epx` `--danger` (`RectoDangerBrush`) border + required badge on empty `/Ff Required` fields. Done when: toggling `Highlight Fields` toggles field tints and required indicators immediately.
- [ ] Implement `Tab` and `Shift+Tab` keyboard navigation across fields on the current page and across page boundaries following the page's `/Tabs` (`/R` row order, `/C` column order, `/S` structure/annotation order, or designer tab-order index). Done when: pressing `Tab` steps through every non-read-only field in exact `/Tabs` order.
- [ ] Re-evaluate `/CO` calculated fields (`§7`) live as the user types or changes checkboxes/combos, updating dependent read-only total fields on canvas and recording each field edit in the document `Undo`/`Redo` stack (`Ctrl+Z`/`Ctrl+Y`). Done when: typing a quantity and unit price updates the total field immediately without pressing Save.
- [ ] Turn on offline spell checking in the `FormCanvasOverlay` `TextBox` editors for `Tx` fields (not password or read-only fields) by binding `IsSpellCheckEnabled` to `CheckSpellingWhileTyping` and `Language` to `SpellingLanguage` (`D03 T01 §14`). Done when: `AppTests.FormFillUi` asserts a multiline text field has spell checking on with the configured `Language`, a password field has it off, and a setting change updates both.
- [ ] Wire `Clear / Reset Form`, `Import Data`, `Export Data`, and `Flatten Form` to `DocumentActor` (`§6`-`§7`). Done when: `AppTests.FormFillUi` passes end-to-end.
- [ ] Commit: `feat(app): implement interactive Fill Form mode, Tab traversal, live /CO totals, and data I/O`

**Test checkpoint:** `recto-tests.exe --gtest_filter=FormUi.FillViewModel:AppTests.FormFillUi` passes, quoting `Tab` field traversal, live `/CO` calculation updates, required-field highlighting, and `JSON`/`XFDF` data export.

## 9. Visual `Design Form` mode (`SPEC 8.2`), 7 widget tools, alignment guides & tab badges

Implement the visual **Form Designer** (`Design Form` mode in `FormBar`, `SPEC 8.2`, toggled from the toolbar, floating page toolbar `Form designer` button in `D02 T02 §8`, or `Ctrl+K`): drag-and-drop / click-drag placement tools for all 7 widget types (`Add Text Field`, `Add Checkbox`, `Add Radio Group`, `Add Combo Box`, `Add List Box`, `Add Button`, `Add Signature Field`), 8-handle field resize/move, smart alignment and equal-spacing guides when dragging fields on canvas, and visual tab-order number badges (`1, 2, 3...`) with drag-to-reorder.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Form Fill & Form Designer Bar` -- Designer Mode) and 11 (accessible designer tool buttons and keyboard field nudging).
**Job:** A user can turn any static PDF into a fillable interactive form (or edit an existing form's layout) by placing text fields, checkboxes, radio groups, dropdowns, list boxes, buttons, and signature fields on the page, aligning them with smart snap guides, and setting their keyboard tab order visually. Consumer: `DocumentActor` `/AcroForm` writer (`§6`) and the `Field Properties` inspector (`§10`).
**Treatment:** Visual canvas authoring overlay with 7 widget placement tools, `4 pt` snap-to-edge/center alignment guides against sibling fields and `PageText` baselines, 8-handle resize frames, arrow-key `1 pt` (`Shift+Arrow` `10 pt`) nudging, and numbered tab-order badges. Cheaper substitute that fails the checkpoint: read-only form inspection with no visual widget placement, alignment guides, or tab-order reordering.
**Chrome:** consume `FormBar` (`§8`), `RectoAccentBrush` for alignment snap lines and tab-order badges, and `D02 T02 §8`'s `Form designer` toggle.

**Build order:**

1. Extend `FormBar` and `FormViewModel` with `Design Form` mode tools (`Select/Move`, `Add Text Field`, `Add Checkbox`, `Add Radio Group`, `Add Combo Box`, `Add List Box`, `Add Button`, `Add Signature Field`, `Show Tab Order` toggle, `Align Left/Center/Top/Distribute`). Done when: selecting any widget tool and dragging a rectangle on canvas creates a new `PdfFormField` in `DocumentActor` with an auto-generated unique `/T` name (`TextField_1`, `CheckBox_1`, etc.).
2. Implement smart alignment guides (edge/center/baseline snap lines while dragging or resizing fields), arrow-key nudging, `Ctrl+C`/`Ctrl+V` field duplication, `Delete` removal, `Ctrl+Z`/`Ctrl+Y` undo/redo, and interactive numbered tab-order badges. Done when: `FormUi.DesignerPlacementAndTabOrder` places all 7 widget types, aligns them via snap guides, and reorders tab stops.

- [ ] Add the 7 widget creation tool buttons (`Add Text Field`, `Add Checkbox`, `Add Radio Group`, `Add Combo Box`, `Add List Box`, `Add Button`, `Add Signature Field`) and `Show Tab Order` toggle to `FormBar` when switched to `Design Form` mode (`SPEC 8.2`). Done when: switching `FormBar` to `Design Form` reveals all 7 placement tools with automation names and tooltips.
- [ ] Implement click-or-drag placement on `FormCanvasOverlay` for all 7 widget types, auto-sizing single-click placements to sensible defaults (`140 x 22 pt` for text/combo, `14 x 14 pt` for checkbox/radio, `180 x 44 pt` for signature) and assigning sequential unique `/T` field names. Done when: all 7 widget types can be placed on any page and immediately persisted to `DocumentActor`.
- [ ] Implement 8-handle resize, pointer drag-move, `Arrow` (`1 pt`) / `Shift+Arrow` (`10 pt`) keyboard nudging, `Ctrl+D` / `Ctrl+C` + `Ctrl+V` field duplication, and `Delete` removal with full `Ctrl+Z` / `Ctrl+Y` undo/redo support. Done when: moving, resizing, duplicating, or deleting a form field is fully reversible via `Ctrl+Z`.
- [ ] Compute and render live `--accent` smart alignment guides when dragging or resizing a field within `4 pt` of another field's left/center/right/top/middle/bottom edge or a `PageText` line baseline. Done when: `FormUi.SmartAlignmentGuides` snaps a dragged field's top edge to an adjacent label baseline or sibling field edge. Cheaper substitute that fails the checkpoint: unassisted freehand placement with no alignment guides or snap behavior.
- [ ] Render numbered tab-order badges (`1, 2, 3...` in `RectoAccentBrush` pills at the top-left of each widget) when `Show Tab Order` is active, and support clicking or dragging badges to reorder the page's `/Annots` widget sequence and `/Tabs` entry. Done when: reordering tab-order badges in `Design Form` mode changes the exact `Tab` key sequence in `Fill Form` mode (`§8`).
- [ ] Wire the floating page toolbar's `Form designer` button (`D02 T02 §8`) and `Ctrl+K` (`Design Form`) to open `FormBar` directly in `Design Form` mode. Done when: clicking `Form designer` on a blank PDF opens the designer bar ready to place fields.
- [ ] Add `FormUi.DesignerPlacementAndTabOrder` and `FormUi.SmartAlignmentGuides` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=FormUi.Designer*:FormUi.Smart*` passes.
- [ ] Commit: `feat(app): implement visual Design Form mode, 7 widget placement tools, alignment guides, and tab-order badges`

**Test checkpoint:** `recto-tests.exe --gtest_filter=FormUi.DesignerPlacementAndTabOrder:FormUi.SmartAlignmentGuides` passes, verifying placement of all 7 widget types, snap guides, and tab-order badge reordering.

## 10. `Field Properties` inspector flyout (`Name`, `/TU`, masks, `Calculation`) and UIA peers

Build the right-hand `Field Properties` inspector (`src/app/controls/FieldPropertiesPane.xaml/.cpp`, `SPEC 8.2`) for `Design Form` mode (`Field Name` `/T`, `Tooltip / Accessibility Label` `/TU`, `Default Value` `/DV`, `Required`, `Read-Only`, `Multiline` / `Password` / `Comb` + `MaxLen`, `Font Family / Size / Colour` `/DA`, `Border / Fill Colour`, `Choice Options` `/Opt` add/remove/reorder, `Number / Currency / Date / Mask Format`, and `Calculation Formula` builder for `Sum`, `Product`, `Average`, `Min`, `Max`, or custom expression), and expose interactive form fields and annotation comments as child UIA `AutomationPeer`s (`D03 T02 §2`).

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Field Properties` inspector in Designer Mode) and 11 (form field UIA peers exposing `/TU` tooltip as `AutomationProperties.Name`, required/read-only states, and value pattern).
**Job:** A form author can configure every property of a selected form field (name, accessibility tooltip, required flag, comb boxes, dropdown options, number/date formatting, and live calculation formulas), and a screen-reader user in `Fill Form` mode hears each field's `/TU` label, type, value, and required status. Consumer: `DocumentActor` `/AcroForm` and `/CO` writer (`§6`-`§7`) and `DocumentViewAutomationPeer` (`D03 T02 §2`).
**Treatment:** Structured `FieldPropertiesPane` bound to the currently selected field in `FormViewModel`, plus WinUI 3 `AutomationPeer` wiring on every `FormCanvasOverlay` control (`IValueProvider` / `IToggleProvider` / `ISelectionProvider`) registered with `DocumentViewAutomationPeer` (`D03 T02 §2`). Cheaper substitute that fails the checkpoint: a designer mode that cannot edit field names, dropdown options, or calculation formulas, or form fields whose `/TU` tooltips are ignored by screen readers.
**Chrome:** consume `SidePanelHost` / right flyout slot (`D02 T03 §3`), `RectoColors.xaml` tokens, and `D03 T02 §2`'s child peer registration API.

**Build order:**

1. Build `FieldPropertiesPane` (`src/app/controls/FieldPropertiesPane.xaml/.cpp`) editing General (`Field Name` `/T` with duplicate-name validation, `Tooltip / Accessibility Label` `/TU`, `Default Value` `/DV`, `Required`, `Read-Only`), Appearance (`Font Family`, `Font Size`, `Text Colour`, `Alignment`, `Border & Fill Colour`), Options (`Multiline`, `Password`, `Comb of N characters`, `Choice Options` `/Opt` list editor), Format (`None`, `Number`, `Currency`, `Percentage`, `Date`), and Calculation (`None`, `Sum (+)`, `Product (x)`, `Average`, `Minimum`, `Maximum` with source-field checkboxes, or custom formula). Done when: `AppTests.FormDesignerUi` authors a multi-field form with dropdown options and a `Sum` calculation formula from scratch, saves the PDF, and re-opens it in `Fill Form` mode as a working interactive form.
2. Bind every `FormCanvasOverlay` widget and on-canvas annotation to its WinUI `AutomationPeer` (`AutomationProperties.Name` from `/TU` fallback to `/T`, `AutomationProperties.IsRequiredForForm` from `/Ff Required`, `IsEnabled` / `IValueProvider::IsReadOnly` from `/Ff ReadOnly`) and register them with `DocumentViewAutomationPeer` (`D03 T02 §2`). Done when: an automation client queries form widgets on canvas and reads their `/TU` accessible names, values, and required states.

- [ ] Build the **General & Validation** section of `FieldPropertiesPane`: `Field Name` (`/T`), `Tooltip / Accessibility Label` (`/TU`), `Default Value` (`/DV`), `Required` toggle, and `Read-Only` toggle, updating `DocumentActor` through `AnnotationUndoStack`. Done when: editing `/T` or `/TU` updates the field dictionary and can be undone with `Ctrl+Z`.
- [ ] Build the **Appearance & Typography** section: `Font Family` (`Helvetica`, `Times`, `Courier`, or embedded system font), `Font Size` (`Auto` or `6..72 pt`), `Text Alignment` (`Left`, `Center`, `Right`), `Text Colour`, `Border Colour / Width`, and `Fill Colour`, regenerating `/DA` and `/AP /N` live. Done when: changing font size or border colour updates the field's appearance on canvas immediately.
- [ ] Build the **Type-Specific Options** section: for `Tx` (`Multiline`, `Password`, `Limit of N characters` `/MaxLen`, `Comb of N characters`), for `Btn` (`Radio Group Name`, `Export Value` `/AS`, `Checked by default`), and for `Ch` (`Choice Options` `/Opt` add, remove, and drag-reorder list + `Allow custom text` `/Ff Edit`). Done when: adding 4 options to a `ComboBox` field in `FieldPropertiesPane` populates the dropdown in `Fill Form` mode.
- [ ] Build the **Format & Calculation Formula** section: number/currency/percentage/date format presets and the `Calculation Formula` builder (`Sum`, `Product`, `Average`, `Min`, `Max` with a checklist of numeric fields in the document, plus custom arithmetic expression input with syntax validation), updating `/AcroForm /CO` (`§7`). Done when: configuring `Total = Sum(Item1, Item2)` in `FieldPropertiesPane` writes `/CO` and evaluates live in `Fill Form` mode. Cheaper substitute that fails the checkpoint: omitting calculation formula authoring or `/Opt` choice editing from the designer inspector.
- [ ] Expose every `FormCanvasOverlay` widget and on-canvas annotation comment to UI Automation (`D03 T02 §2`) with `AutomationProperties.Name` set to `/TU` (or `/T` when `/TU` is empty), `AutomationProperties.IsRequiredForForm` set to `/Ff Required`, and `ValuePattern` / `TogglePattern` / `SelectionPattern` bound to the field value. Done when: `A11y.FormFieldsAndAnnotations` verifies UIA name, value, and required properties across all widget types.
- [ ] Add `AppTests.FormDesignerUi` and `A11y.FormFieldsAndAnnotations` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=AppTests.FormDesignerUi:A11y.FormFieldsAndAnnotations` passes.
- [ ] Commit: `app: interactive form filling, form designer bar and field properties inspector`

**Test checkpoint:** `recto-tests.exe --gtest_filter=FormUi.*:AppTests.FormFillUi:AppTests.FormDesignerUi:A11y.FormFieldsAndAnnotations` passes, quoting field traversal, calculation updates, form designer field authoring, `JSON`/`XFDF` export, and UIA peer verification.

## 11. `/Link` authoring engine: page, named-destination, file and URI targets, never Launch

Add the write side of `Link` annotations to `recto-pdf`: build, validate and mutate `/Subtype /Link` annotations whose target is a page view, a named destination, another PDF file or a web URI, matching the Acrobat Pro `Link` tool (`Edit PDF` > `Link`). `§1` only parses and round-trips `LinkData`, and `D02 T02 §9` only follows links, so nothing in Recto can yet create one. It must not break the zero-JavaScript rule (D9): authoring never writes a `/Launch`, `/JavaScript`, `/SubmitForm`, `/ImportData` or `/Rendition` action, and refuses them with a typed error. Recto keeps its improvement over Acrobat that every external target is shown in full and asked about before it opens (`D02 T02 §9`), so the engine also normalises targets into a form that dialog can display without ambiguity.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Define `LinkTarget` (a variant of `PageView`, `NamedDestination`, `RemotePdf`, `WebUri`, `FileUri`) and `LinkAppearance` (border width `0..12 pt`, dash, `/C` colour, highlight mode `/H` `N`, `I`, `O`, `P`) in `src/pdf/links.h`, and implement `make_link_annotation(rect, target, appearance)` returning `PdfResult<PdfAnnotation>` with the matching `/A` action dictionary or `/Dest` entry. Done when: `PdfLinks.BuildTargets` builds all five target kinds and re-parses each through `§1` to an equal `LinkTarget`.
2. Implement target validation and the executable-action refusal, then wire create, update and delete through `DocumentActor` (`§1`). Done when: `PdfLinks.RefuseExecutableActions` shows every refused action returns `ErrorCode::InvalidArgument` and `PdfLinks.RoundTripEditDelete` creates, edits and deletes links across an incremental save and re-open.

- [ ] Add `src/pdf/links.h` and `src/pdf/links.cpp` with `LinkTarget`, `LinkAppearance` and `make_link_annotation` writing `/A << /S /GoTo /D [page /XYZ left top zoom] >>`, `/S /GoTo /D (name)`, `/S /GoToR /F <filespec> /D [page /Fit]`, and `/S /URI /URI (...)`; `PageView` accepts `XYZ`, `Fit`, `FitH`, `FitV` and `FitR`. Done when: `src/pdf/links.cpp` compiles with zero warnings and `PdfLinks.BuildTargets` passes.
- [ ] Validate `WebUri` as 7-bit ASCII (non-ASCII is percent-encoded as UTF-8) with a maximum of `2048` bytes, no control characters, and an allowed scheme of `http`, `https` or `mailto`; validate `FileUri` as an absolute `file:` URI with the same length and control rules. Done when: `PdfLinks.UriValidation` rejects `javascript:`, `data:`, `vbscript:`, a `3000`-byte URI and a URI containing `U+0000` with `ErrorCode::InvalidArgument`, and accepts a percent-encoded non-ASCII `https` URI.
- [ ] Write a link to another PDF as `/S /GoToR` with `/NewWindow` omitted (the viewer opens it in a new tab after asking, `§13`), and write a link to any other file type as a `FileUri` `/URI` action, never as `/Launch`. Done when: `PdfLinks.FileTargets` shows a `.pdf` target serialises to `/GoToR` and a `.xlsx` target to `/URI file:///...` with no `/Launch` key anywhere in the saved bytes. Cheaper substitute: writing `/Launch` for non-PDF files because Acrobat does.
- [ ] Refuse to author `/Launch`, `/JavaScript`, `/JS`, `/SubmitForm`, `/ImportData` and `/Rendition` actions in `make_link_annotation` and in `update_annotation` for a `Link` payload, recording the refusal in `recto::pdf::diagnostics_tail()`. Done when: `PdfLinks.RefuseExecutableActions` passes and no saved byte stream in the test contains any of those action names.
- [ ] Resolve a `PageView` target to the destination page object reference at write time and reject a page index outside the document or a non-finite coordinate. Done when: `PdfLinks.PageBounds` returns `ErrorCode::InvalidArgument` for page `-1`, page `page_count` and a `NaN` `left`, and writes a valid `[ref /XYZ ...]` array otherwise.
- [ ] Add `DocumentActor::add_link`, `update_link` and `remove_link` in `src/core/document_actor.cpp` delegating to `add_annotation`, `update_annotation` and `remove_annotation` (`§1`) so undo (`§4`) and the `DisplayList` invalidation stay in one path. Done when: `PdfLinks.RoundTripEditDelete` creates, edits and deletes a link on `text/two-column-links-outline.pdf`, saves incrementally, re-opens and sees the final state.
- [ ] Mark each parsed link whose existing action is `/Launch`, `/JavaScript`, `/SubmitForm` or `/ImportData` as `inert` in `LinkData` and preserve it byte for byte unless the user edits or deletes it. Done when: `PdfLinks.InertLinksPreserved` opens a crafted fixture, saves without touching the link, and finds the original action bytes unchanged while `inert` is true.
- [ ] Add `PdfLinks.*` tests in `tests/recto-tests/pdf_links_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfLinks.*` passes under ASan.
- [ ] Commit: `feat(pdf): add Link annotation authoring for page, destination, file and URI targets`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfLinks.*` passes under ASan, including the proof that no authoring path can write an executable action.

## 12. Named destinations engine (`/Names /Dests`): list, create, rename, delete, remap

Add the write side of named destinations to `recto-pdf`: enumerate, create, rename and delete entries in the catalog `/Names /Dests` name tree and the legacy catalog `/Dests` dictionary, and keep every destination reference valid when pages are deleted, moved or merged. `D01 T01 §10` only reads them. The matching Acrobat Pro surface is the `Destinations` navigation panel (`§14`). It must not break the open viewer document (edits are staged on the actor's copy, `§1`) and keeps Recto's improvement of a raw-byte check that no deleted page stays reachable. The remap rule exists because a page delete must never leave a named destination with a null page target or a bookmark with no destination.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `list_destinations`, `create_destination`, `rename_destination` and `delete_destination` in `src/pdf/destinations.h/.cpp` over a bounded name-tree reader and a name-tree writer that emits a balanced `/Kids` and `/Limits` tree with names in byte-sorted order. Done when: `PdfDestinations.NameTreeRoundTrip` writes `1`, `300` and `5000` names, re-opens, and finds every name in sorted order with valid `/Limits`.
2. Implement `remap_page_references(Document&, PageRemap)` rewriting every destination that names a page (named destinations, `/Outlines` item `/Dest` and `/A` `GoTo`, `Link` annotation `/Dest` and `/A` `GoTo`, and `/OpenAction`) when pages are deleted, moved or replaced, and reporting each reference it had to drop. Done when: `PdfDestinations.RemapOnDelete` deletes page 3 of a 5-page fixture and finds zero references to the deleted page object and every later reference pointing at its new page.

- [ ] Add `src/pdf/destinations.h/.cpp` with `NamedDestination` (`name` as a PDF string in PDFDocEncoding or UTF-16BE, `page_index`, `view` as in `LinkTarget::PageView`) and the four operations returning `PdfResult<T>`. Done when: `src/pdf/destinations.cpp` compiles with zero warnings and the four operations are declared on `pdf::Document`.
- [ ] Read both the `/Names /Dests` name tree and the legacy `/Dests` dictionary, merge them with the name tree winning on duplicates, bound tree depth to `32` levels with a visited-node set, and cap the listed entries at `100000`. Done when: `PdfDestinations.HostileTree` opens a fixture with a cyclic `/Kids` and a `200000`-name tree and returns a typed error or the capped list under ASan with no hang.
- [ ] Write new and changed entries to the `/Names /Dests` tree only, migrate a legacy `/Dests` dictionary into the tree on first write, reject deleting a name that is still referenced unless the caller passes `DeletePolicy::DropReferences`, in which case every `Link`, outline and `/OpenAction` reference to it (action or direct `/Dest`) loses its dead target and is listed in the `RemapReport`, and reject an empty name, a duplicate name, a name over `255` bytes, and a rename onto an existing name. Done when: `PdfDestinations.CreateRenameDelete` and `PdfDestinations.DeleteReferenced` cover each rejection, the refusal of a referenced delete, and the dropped-reference report for direct and action references including a catalog `/OpenAction` naming the deleted destination, with `ErrorCode::InvalidArgument` and a successful create, rename and delete.
- [ ] Rewrite references on rename: every `Link` `/A` `GoTo` with a string `/D`, every direct `Link` `/Dest` string, every outline item `/A` `GoTo` string `/D`, every outline item direct `/Dest` string, and `/OpenAction`, so a rename never breaks a link or bookmark. Done when: `PdfDestinations.RenameRewritesReferences` renames `Intro` to `Start` and finds all five reference kinds (action and direct form, links and outlines, plus `/OpenAction`) pointing at `Start`.
- [ ] Implement `remap_page_references` with a `PageRemap` (old page index to new page index or removed) and report dropped references in a `RemapReport`; a destination whose page was removed is deleted from the name tree, and an outline item or link whose target was removed keeps its entry but loses the dead `/Dest` and is listed in the report. Done when: `PdfDestinations.RemapOnDelete` and `PdfDestinations.RemapOnMove` pass and the report names each dropped reference. Cheaper substitute: leaving a null page entry in the destination array.
- [ ] Expose `list_destinations`, `create_destination`, `rename_destination`, `delete_destination` and `remap_page_references` on `DocumentActor` as staged edits that the page operations of `D05 T01` and `D05 T02` call for every page they delete, move or replace. Done when: `PdfDestinations.ActorStaging` mutates the staged graph and leaves the opened file byte-identical until `Save`.
- [ ] Add `PdfDestinations.*` tests in `tests/recto-tests/pdf_destinations_tests.cpp` using a generated `tests/corpus/text/named-dests.pdf` (see the next item). Done when: `recto-tests.exe --gtest_filter=PdfDestinations.*` passes under ASan.
- [ ] Add `build_named_dests` to `scripts/build-corpus.py` writing `text/named-dests.pdf` (5 pages, a `/Names /Dests` tree with 6 names, a legacy `/Dests` dictionary entry, an outline with two items, and two `Link` annotations referencing names) with its `PROVENANCE.md` row. Done when: `python scripts/build-corpus.py --check` passes and `Corpus.Provenance` lists the file.
- [ ] Commit: `feat(pdf): add named destination write support and page reference remapping`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfDestinations.*` passes under ASan, and the page-delete cases find no object reference to a deleted page in the saved bytes.

## 13. `Link` tool, Link Properties flyout, and link edit and delete on the canvas

Add the `Link` region tool to the `Markup & Annotation Bar` (`SPEC 8.2` already lists a `Link` region tool) so the user can drag a rectangle on the page, choose where it goes, and later select, edit, move, resize and delete it, matching the Acrobat Pro `Link` tool. It must not break the safe-link boundary: a link authored here opens through the same `OpenLinkDialog` path as any other (`D02 T02 §9`), asks first for every web and file target, and never launches a program. It keeps Recto's improvement that the destination is shown in full before it opens, and the flyout shows the same full text while the user is authoring.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Markup & Annotation Bar`, `Link` region tool), 9.9 (`"Open this link?"` dialog chrome) and 11 (`32 epx` targets, automation names); there is no board for the Link flyout, so it follows the `Colour, Opacity & Stroke Picker` flyout chrome from `§3`.
**Job:** The user draws a link rectangle over a word, an image or an empty area, picks `Go to a page view`, `Go to a named destination`, `Open another PDF` or `Open a web page or file`, and sees the link work. They can reselect any link, change its target or appearance, or delete it. Consumer: `DocumentView` annotation canvas (`§4`), `CommandRegistry` (`Ctrl+K`), and `D02 T02 §9` link activation.
**Treatment:** A `LinkTool` mode on `AnnotationToolViewModel` (`§3`) drawing a rubber-band rectangle, then a `LinkPropertiesFlyout` anchored to it with a target-type selector, a page picker with `Set to current view`, a named-destination combo box fed by `§12`, a file picker for PDFs, a single-line URL box, and a border and highlight section. Cheaper substitute that fails the checkpoint: a dialog that accepts only a URL, or a tool that creates links with no way to select, edit or delete them afterwards.
**Chrome:** consume `RectoColors.xaml` surface, border and swatch tokens, `§3`'s bar and `§4`'s 8-handle selection frame, `D02 T01 §10` dialog styles, and Segoe Fluent Icons `E71B` (Link).

**Build order:**

1. Add `AnnotationToolMode::Link` to `AnnotationToolViewModel`, draw the rubber-band rectangle on `DocumentView`, and create the annotation through `DocumentActor::add_link` (`§11`) with the chosen target. Done when: `LinkUi.DrawAndCreate` drags a rectangle on page 2 and finds one `Link` annotation with the expected `/Rect` after `Save`.
2. Build `LinkPropertiesFlyout` (`src/app/controls/LinkPropertiesFlyout.xaml/.h/.cpp`) and the select, edit and delete behaviour, and route activation of authored links through the `OpenLinkDialog` path. Done when: `LinkUi.EditDeleteAndAskFirst` changes a target, deletes a link and proves an authored web link and file link both raise `OpenLinkDialog` before any launch.

- [ ] Add a `Link` button (glyph `E71B`, automation name `Link tool`, tooltip `Link`) to `MarkupAnnotationBar` after the `Stamp` button, with `Ctrl+K` entries `Add Link` and `Link Properties`. Done when: the button is `>= 32 epx`, passes the `D03 T02 §3` inventory and the command box lists `Add Link`.
- [ ] Draw the rubber-band rectangle with a `1 epx` `RectoAccentBrush` outline and `RectoAccentTintBrush` fill, clamp it to the page bounds, and require a minimum size of `8 x 8 pt` before it commits. Done when: `LinkUi.DrawAndCreate` rejects a `3 x 3 pt` drag and commits a `120 x 24 pt` drag.
- [ ] Build `LinkPropertiesFlyout` with a target-type `ComboBox`, the page picker (`Page N` number box and `Set to current view`), the named-destination combo box listing `§12` names, a PDF file picker, a URL `TextBox` showing the normalised URL from `§11`, and `Border width`, `Colour` and `Highlight` controls. Done when: choosing each of the four target types in `LinkUi.TargetTypes` writes the matching `LinkTarget` kind. Cheaper substitute: one free-text box for every target type.
- [ ] Select an authored or existing link with a click while an annotation tool or `Select` is the active tool (never in the plain reading view), showing the `§4` 8-handle frame, open the flyout on `Enter` or double-click (`Enter` only ever opens the flyout), and delete on `Delete`. Done when: `LinkUi.EditDeleteAndAskFirst` selects a link by click, changes its URL, deletes a second link, `Ctrl+Z` restores it, and no `OpenLinkDialog` appeared during any of those steps.
- [ ] Show an inert imported link (`inert` from `§11`) with a `RectoWarningBrush` dashed outline and a flyout notice `This link runs a program or script. Recto never runs it. Delete or replace it.` with a `Replace target` button. Done when: `LinkUi.InertLinkNotice` shows the notice for a crafted `/Launch` link and clicking the link starts no process.
- [ ] Keep activation on the existing path with separate gestures: in the plain reading view a click on an authored `WebUri`, `FileUri` or `RemotePdf` link opens `OpenLinkDialog` first (full URL in monospace, `Cancel`, `Open in browser`), and a `PageView` or `NamedDestination` link scrolls and pushes the `Alt+Left` history entry; while an annotation tool or `Select` is active a click only selects, `Enter` only opens the properties flyout, and `Ctrl+click` or `Ctrl+Enter` on the selected link is the only way to activate it. Done when: `LinkUi.EditDeleteAndAskFirst` shows no `Launcher::LaunchUriAsync` call before the dialog is confirmed, a plain click in `Select` mode never prompts, `Enter` on a selected external link opens the flyout and requests no activation, and `file:` targets prompt with `Ask before opening links` both on and off. Cheaper substitute: a second launch code path inside the flyout, or one click that both selects and activates.
- [ ] Add `LinkUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=LinkUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add the Link tool, Link Properties flyout and link edit and delete`

**Test checkpoint:** `recto-tests.exe --gtest_filter=LinkUi.*` passes on the display-session host, quoting the `OpenLinkDialog` call log for a web link and a `file:` link.

## 14. `Destinations` side panel: list, create, rename, delete and jump

Add a `Destinations` panel to the side-panel family so the user can see every named destination, jump to it, add one at the current view, rename it and delete it, matching the Acrobat Pro `Destinations` panel. It must not break `SidePanelHost` (the panel is one more card in the same container, `D02 T03 §3`) and keeps Recto's improvement that every command is also reachable from `Ctrl+K`.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.3 (floating side panel and docked panels share one control instance) and 11; genuinely new surface, following the `Outline (with Bookmark Editor)` panel chrome (`D02 T03 §6`): `TreeView`-style rows, header actions, `RectoAccentTintBrush` current row.
**Job:** The user opens `Destinations`, sees names sorted alphabetically with their page numbers, clicks one to jump, uses `Add destination` to name the current view, double-clicks or presses `F2` to rename, and presses `Delete` to remove one. Consumer: `SidePanelHost`, the `LinkPropertiesFlyout` name picker (`§13`) and `CommandRegistry`.
**Treatment:** A virtualized `ItemsRepeater` list bound to `DestinationsViewModel` (`src/services/destinations_view_model.h/.cpp`) over the `DocumentActor` calls from `§12`, with inline rename and a confirm-free delete backed by `Ctrl+Z`. Cheaper substitute that fails the checkpoint: a read-only list, or a rename that does not update the links that use the name.
**Chrome:** consume `SidePanelHost` (`D02 T03 §3`), `RectoColors.xaml` tokens, Segoe Fluent Icons, and the shared undo stack (`§4`).

**Build order:**

1. Implement `DestinationsViewModel` with sorted, filterable rows (`name`, `page_number`), `add_at_current_view`, `rename`, `remove` and `jump_to`, all through `DocumentActor` without blocking the UI thread. Done when: `DestinationUi.ViewModel` lists, adds, renames and removes names on `text/named-dests.pdf` and reports the actor results.
2. Build `DestinationsPanel` (`src/app/panels/DestinationsPanel.xaml/.h/.cpp`), register it in `SidePanelHost`'s switcher, and bind commands. Done when: `DestinationUi.PanelAccessibility` finds every control with an automation name and tooltip and the panel opens from the switcher and from `Ctrl+K`.

- [ ] Add `src/services/destinations_view_model.h/.cpp` with a case-insensitive `Find destination` filter, ordinal sort, and a `broken` flag for any entry whose page could not be resolved. Done when: `DestinationUi.ViewModel` filters `intro` to the names containing it and flags a crafted unresolved entry.
- [ ] Build `DestinationsPanel` with a header (title `Destinations`, `Add destination` button glyph `E710`, close), the filter box, and rows showing the name and `Page N`. Done when: the panel renders at the same width as the `Outline` panel in `Focus`, `Classic` and `Reading room`. Cheaper substitute: a modal dialog instead of a panel.
- [ ] Implement `Add destination` using the current page and top-left of the viewport (`PageLayoutGeometry`, `D02 T02 §5`), proposing the first free `Destination N` name in an inline edit box. Done when: `DestinationUi.AddAtView` creates one entry at the visible page and view and `Ctrl+Z` removes it.
- [ ] Implement inline rename (`F2`, double-click) and delete (`Delete`, context menu; when the name is still used, a confirmation reads `N links, M bookmarks and K opening actions use this destination` with `Delete and remove those references` and `Cancel`) with validation messages for an empty or duplicate name shown beside the box. Done when: `DestinationUi.RenameDelete` renames a name used by a link, sees the link still jump to it, then deletes it and sees the row disappear.
- [ ] Jump on single click and `Enter`, push the previous viewport onto the `Alt+Left` history (`D02 T02 §6`), and announce `Jumped to <name>, page N` through `AnnouncementService` (`D03 T02 §5`). Done when: `DestinationUi.Jump` scrolls to the destination and `Alt+Left` returns.
- [ ] Register `Show Destinations`, `Add Destination`, `Rename Destination` and `Delete Destination` in `CommandRegistry` so each appears in `Ctrl+K`. Done when: `DestinationUi.PanelAccessibility` finds the four commands in the command box.
- [ ] Add `DestinationUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=DestinationUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add the Destinations side panel`

**Test checkpoint:** `recto-tests.exe --gtest_filter=DestinationUi.*` passes on the display-session host, including a rename that keeps a linked jump working.

## 15. Article thread engine (`/Threads` beads): read flow and thread authoring

Add the `recto-pdf` model for article threads (ISO 32000 section 12.4.3 Articles): read the catalog `/Threads` array and its circular bead lists, produce an ordered reading flow, and create, extend and delete threads. This matches the Acrobat Pro `Article` tool and `Articles` panel, which let a long multi-column page be read in the author's intended order. It must not break `PageText` reading order (`D03 T01 §1`): the article flow is a separate, optional ordering the viewer offers, never a replacement for it. Recto keeps its improvement of bounded traversal, so a hostile bead cycle ends in a typed error instead of a hang.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Define `ArticleThread` (`/I` info title and author, ordered `ArticleBead` list with `page_index`, `rect_pt`) and implement `list_articles` reading `/Threads`, `/F` first bead, `/N` and `/V` links, and `/P` and `/R` per bead, with a visited set and a `10000`-bead cap. Done when: `PdfArticles.ParseThreads` reads a 3-thread fixture in author order and `PdfArticles.HostileBeads` returns a typed error for a broken `/N` ring and a ring with an unreachable bead.
2. Implement `create_article`, `add_bead`, `end_article` and `delete_article`, writing the doubly linked circular bead ring and each page's `/B` array. Done when: `PdfArticles.AuthorRoundTrip` creates a 4-bead thread across 2 pages, saves, re-opens and reads the same boxes in the same order, and `delete_article` leaves no bead object reachable.

- [ ] Add `src/pdf/articles.h/.cpp` with `ArticleThread`, `ArticleBead`, `list_articles`, `create_article`, `add_bead`, `insert_bead`, `end_article` and `delete_article` returning `PdfResult<T>`. Done when: the files compile with zero warnings and the operations are declared on `pdf::Document`.
- [ ] Validate a ring on read: every `/N` and `/V` pair must agree, every bead `/T` thread reference must match its thread, and an inconsistent ring is reported as `ErrorCode::CorruptStructure` for that thread only while other threads still list. Done when: `PdfArticles.HostileBeads` lists the healthy thread beside a corrupt one.
- [ ] Reject a bead rectangle that is empty, non-finite or outside the page's crop box, and cap a thread at `10000` beads. Done when: `PdfArticles.BeadValidation` returns `ErrorCode::InvalidArgument` for each rejected rectangle.
- [ ] Implement `insert_bead` between two existing beads and `remove_bead` while keeping the ring closed, and keep the page `/B` arrays in sync. Done when: `PdfArticles.InsertRemoveBead` leaves a valid ring and `/B` arrays after inserting after bead 2 and removing bead 1.
- [ ] Expose `list_articles`, `create_article`, `add_bead`, `insert_bead`, `remove_bead`, `end_article` and `delete_article` on `DocumentActor` with staged edits and `DisplayList` invalidation for the affected pages. Done when: `PdfArticles.ActorStaging` leaves the opened file byte-identical until `Save`.
- [ ] Add `build_articles` to `scripts/build-corpus.py` writing `text/articles.pdf` (3 pages, two-column layout, 2 threads whose beads cross the columns and pages, one with `/I` title and author) with its `PROVENANCE.md` row, and add `PdfArticles.*` tests in `tests/recto-tests/pdf_articles_tests.cpp`. Done when: `python scripts/build-corpus.py --check` passes and `recto-tests.exe --gtest_filter=PdfArticles.*` passes under ASan.
- [ ] Commit: `feat(pdf): add article thread reading and authoring`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfArticles.*` passes under ASan, with the hostile-ring case ending in a typed error inside the test time limit.

## 16. `Articles` panel, follow-the-article reading flow and `Article` authoring tool

Add the viewer side of article threads: an `Articles` panel listing threads, a `Follow article` mode that moves the view bead by bead in the author's order, and an `Article` authoring tool that draws boxes, links them into a thread, and ends the thread, matching Acrobat Pro's `Articles` panel and `Article` tool. It must not break the shared `SidePanelHost` or the keyboard model (`Esc` closes the mode). It keeps Recto's improvement that a screen reader hears the reading flow, because `Follow article` announces each bead through `AnnouncementService`.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (contextual mode bars), 8.3 (side panels) and 11; genuinely new surface, following the `Markup & Annotation Bar` for the authoring tool and the `Outline` panel for the list.
**Job:** The user opens `Articles`, picks a thread, presses `Follow article` and steps through it with `Space` or `Enter` (next bead) and `Shift+Space` (previous bead), with the view zooming to fit the bead box. To author, they choose `Article`, drag a box on each part of the story in order, and press `Enter` to end the thread, naming it. Consumer: `DocumentView`, `SidePanelHost`, `AnnouncementService`, `CommandRegistry`.
**Treatment:** `ArticlesViewModel` (`src/services/articles_view_model.h/.cpp`) over `§15`, an `ArticlesPanel` list, a `FollowArticle` mode in `DocumentView` that sets the viewport to the bead rectangle through `PageLayoutGeometry`, and an `ArticleTool` mode drawing numbered bead boxes. Cheaper substitute that fails the checkpoint: a list that only jumps to the first bead, or an authoring tool that cannot end or reorder a thread.
**Chrome:** consume `SidePanelHost`, `RectoColors.xaml` tokens (bead boxes `RectoAccentBrush` with a numbered badge), the `§4` selection frame, and Segoe Fluent Icons.

**Build order:**

1. Implement `ArticlesViewModel`, the `ArticlesPanel`, and `Follow article` stepping, including the fit-to-bead viewport and the end-of-thread behaviour (`Reached the end of the article`). Done when: `ArticleUi.FollowFlow` steps through every bead of `text/articles.pdf` thread 1 in order and stops at the last bead.
2. Implement the `ArticleTool` authoring mode with numbered boxes, `Enter` to end, a name box, bead delete and reorder, and `Ctrl+Z`. Done when: `ArticleUi.Authoring` creates a 3-bead thread, ends it with a name, reopens it in the panel and undoes the whole creation in one step.

- [ ] Build `ArticlesPanel` (`src/app/panels/ArticlesPanel.xaml/.h/.cpp`) listing thread titles and bead counts with `Follow article`, `Rename`, `Delete` row actions, registered in the `SidePanelHost` switcher. Done when: `ArticleUi.PanelAccessibility` finds every control with an automation name and tooltip.
- [ ] Implement `Follow article` in `DocumentView` with `Space` or `Enter` for next, `Shift+Space` for previous, `Esc` to leave, and a status chip `Bead 3 of 8` on `RectoRaisedBrush`. Done when: `ArticleUi.FollowFlow` verifies keyboard stepping and the chip text. Cheaper substitute: scrolling to each page instead of fitting the bead box.
- [ ] Announce `Bead N of M, page P` through `AnnouncementService` (`D03 T02 §5`) on each step without moving keyboard focus. Done when: `ArticleUi.FollowAnnouncements` records one polite notification per step.
- [ ] Add an `Article` button (automation name `Article tool`, tooltip `Article`) to the `Markup & Annotation Bar` and the `Ctrl+K` entries `Add Article`, `Follow Article`, `End Article`. Done when: the button passes the `D03 T02 §3` inventory and the command box lists the three commands.
- [ ] Implement `ArticleTool`: each drag adds a bead box via `add_bead` with a numbered badge, `Enter` ends the thread and opens an inline name box, and a bead can be dragged to resize or deleted with `Delete` (re-linking the ring). Done when: `ArticleUi.Authoring` creates, names and edits a thread and the saved file re-opens with the same ring.
- [ ] Group the whole authoring session as one undo entry on the `§4` stack. Done when: one `Ctrl+Z` removes the thread and its `/B` entries, and `Ctrl+Y` restores them.
- [ ] Add `ArticleUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=ArticleUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add the Articles panel, Follow article mode and Article tool`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ArticleUi.*` passes on the display-session host, quoting the bead announcement log.

## 17. `/Measure` engine: scale, distance, perimeter and area, units and `Measure` export

Add the measurement model to `recto-pdf` and `recto-services`: a page measurement scale stored as a `/VP` viewport with an `/RL` `/Measure` dictionary (ISO 32000-2 section 12.9), measurement annotations (`Line` distance, `PolyLine` perimeter, `Polygon` area carrying `/Measure`), exact geometry for length and area in real units, and export of every measurement. This matches the Acrobat Pro `Measuring` tools. It must not break `§1` and `§2` (measurements are ordinary `Line`, `PolyLine` and `Polygon` annotations that third-party viewers still draw) and keeps Recto's improvement of typed, bounded numbers: a hostile scale (zero, negative, `NaN`) is rejected. The scale is entered in drawing units because users cannot trust a ratio they cannot state in the units of their drawing.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Define `MeasureScale` (`page_length`, `page_unit` among `pt`, `in`, `mm`, `cm`, `m`; `real_length`, `real_unit` among `mm`, `cm`, `m`, `km`, `in`, `ft`, `yd`, `mi`; `area_unit`; `decimals`) and implement `scale_to_measure_dict` and `measure_dict_to_scale` for the `/RL` `/Measure` dictionary (`/R`, `/X`, `/Y`, `/D`, `/A`, `/T`, `/S`, `/O` and `NumberFormat` `/U`, `/C`, `/F`, `/D`, `/FD`, `/RT`) and the page `/VP` entry with `/BBox`. Done when: `PdfMeasure.ScaleRoundTrip` writes and re-reads a `1 in = 10 ft` scale and a `1 cm = 2.5 m` scale with the same values.
2. Implement `measure_distance`, `measure_perimeter` and `measure_area` over page-space points with `UserUnit` and page `/Rotate` handled, formatting the result with the scale's number format, and `export_measurements` to CSV and JSON. Done when: `PdfMeasure.GeometryAndExport` matches hand-computed lengths and areas for a `3-4-5` triangle, a rotated rectangle and a concave polygon, and the exports match golden files.

- [ ] Add `src/pdf/measure.h/.cpp` with `MeasureScale`, `MeasureResult` (`value`, `unit`, `text`), and the dictionary read and write functions returning `PdfResult<T>`. Done when: both files compile with zero warnings.
- [ ] Reject a scale with a non-finite or non-positive length, a length over `1e9`, or a unit outside the allowed set with `ErrorCode::InvalidArgument`, and bound `decimals` to `0..6`. Done when: `PdfMeasure.ScaleValidation` rejects zero, negative, `NaN`, `1e30` and `decimals = 9`.
- [ ] Compute length as the sum of segment lengths and area by the shoelace formula on the polygon vertices, scale area by the square of the length ratio, apply `UserUnit` (`/UserUnit` on the page), and report a self-intersecting polygon as `area_is_approximate` rather than a wrong exact value. Done when: `PdfMeasure.GeometryAndExport` passes including a bow-tie polygon flagged approximate.
- [ ] Write a measurement as an annotation with `/IT /LineDimension` (`Line`, `PolyLine`) or `/IT /PolygonDimension` (`Polygon`), a `/Measure` entry, and `/Contents` holding the formatted result so viewers that ignore `/Measure` still show the value, and synthesise the `/AP` label through `§2`'s generator. Done when: `PdfMeasure.AnnotationRoundTrip` creates all three kinds, saves, re-opens, recomputes the same value and the rendered page shows the label.
- [ ] Read measurement annotations written by other tools, recompute from their own `/Measure` and `/Vertices`, and list any whose stored `/Contents` differs from the recomputed value as `stale`. Done when: `PdfMeasure.ForeignMeasurements` reads a fixture with one stale value and flags it.
- [ ] Implement `export_measurements(pdf::Document&, format)` to CSV (`page`, `kind`, `value`, `unit`, `label`, `author`) and JSON, add `recto-cli annot measure <file> [--page N] [--format csv|json] [--scale "1 in = 10 ft"]`, and make the CLI write through temp-file-then-replace. Done when: `Cli.AnnotMeasure` exports the fixture to both formats and rejects an invalid `--scale` with exit code `2`.
- [ ] Add `build_measure_plan` to `scripts/build-corpus.py` writing `text/measure-plan.pdf` (one page, a vector floor plan with axis-aligned and diagonal lines, a 1 in = 10 ft scale `/VP`, one correct and one stale measurement annotation) with its `PROVENANCE.md` row, and add `PdfMeasure.*` tests in `tests/recto-tests/pdf_measure_tests.cpp`. Done when: `python scripts/build-corpus.py --check` passes and `recto-tests.exe --gtest_filter=PdfMeasure.*:Cli.AnnotMeasure` passes under ASan.
- [ ] Commit: `feat(pdf): add /Measure scale, measurement annotations and measurement export`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfMeasure.*:Cli.AnnotMeasure` passes under ASan, with golden CSV and JSON exports compared byte for byte.

## 18. Snap point index from page vector content: endpoints, midpoints and intersections

Add the geometry service the `Measure` tool snaps to: from a page's compiled `DisplayList` build an immutable index of path endpoints, segment midpoints and segment intersections, and answer nearest-snap queries in page space. This matches the object snapping in Acrobat Pro's measuring tools. It must not break the thread contract: the index is built from the immutable `DisplayList` snapshot (`D01 T02 §1`) off the UI thread, never from the live `recto::pdf::Document`, and crosses threads as `std::shared_ptr<const SnapIndex>`. It keeps Recto's improvement of bounded work on hostile vector content, so a page with millions of segments degrades to a capped index instead of freezing.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `build_snap_index(const DisplayList&, SnapOptions, CancelToken)` in `src/services/snap_index.h/.cpp` extracting stroked and filled path vertices, segment midpoints and pairwise segment intersections through a uniform grid, with caps of `200000` segments and `50000` intersections. Done when: `PdfSnap.BuildIndex` finds the known endpoints, midpoints and the crossing on `text/measure-plan.pdf` and `render/images-and-vectors.pdf`.
2. Implement `nearest_snap(point_pt, radius_pt, kinds)` returning the best candidate by kind priority (endpoint, intersection, midpoint) then distance, and cancellation through `CancelToken`. Done when: `PdfSnap.NearestAndCancel` returns the expected candidate for twenty probe points and aborts a large build inside `10 ms` of cancellation.

- [ ] Define `SnapKind`, `SnapCandidate` (`point_pt`, `kind`, `source_path_index`) and the immutable `SnapIndex` in `src/services/snap_index.h` with no pointer into `pdf::Document`. Done when: the header compiles in `recto-services` and `SnapIndex` has no mutable public members.
- [ ] Flatten cubic Bezier segments to line segments with a `0.05 pt` tolerance before indexing, keep their true end points as `Endpoint` candidates, and ignore clip-only paths. Done when: `PdfSnap.BezierFlatten` finds the curve end points and no candidate from a clipping path.
- [ ] Intersect segments with a robust orientation test that treats parallel and collinear segments as non-intersecting and ignores crossings farther than `0.001 pt` outside either segment. Done when: `PdfSnap.Intersections` finds the crossing of two diagonals and none for parallel lines or shared end points counted twice.
- [ ] Cap the index with the segment and intersection limits, mark the result `truncated` when a cap is hit, and keep the first-found candidates deterministic by path order. Done when: `PdfSnap.CapsAndTruncation` builds a `300000`-segment fixture to a truncated index within the memory budget under ASan.
- [ ] Bound intersection-search work, not only output: cap grid-cell occupancy at `256` segments (a fuller cell is subdivided up to `6` levels, then its pairs are skipped and the index marked `truncated`), cap total segment-pair tests at `5000000` per build, and count collinear overlapping segments toward that budget. Done when: `PdfSnap.ComparisonBudget` builds `200000` overlapping collinear segments in one cell with at most `5000000` pair tests, no cancellation needed, a `truncated` result, and identical output on two runs.
- [ ] Cache one `SnapIndex` per page in the `DocumentActor` page cache keyed by the `DisplayList` generation so an edit to the page rebuilds it. Done when: `PdfSnap.CacheInvalidation` shows a rebuild after an annotation change and a hit otherwise.
- [ ] Add `PdfSnap.*` tests in `tests/recto-tests/pdf_snap_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfSnap.*` passes under ASan.
- [ ] Commit: `feat(services): add the vector snap point index`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfSnap.*` passes under ASan, including the cancellation timing assertion.

## 19. `Measure` tool bar, scale dialog, snapping, live readout and `Measurements` panel

Add the viewer surface for measuring: a `Measure` contextual mode bar with `Distance`, `Perimeter` and `Area` tools, a scale dialog where the user types the scale in drawing units, snapping to endpoints, midpoints and intersections, a live readout while drawing, and a `Measurements` list with export, matching Acrobat Pro's `Measure` tool. It must not break `SPEC 8.2` mode-bar behaviour (`Esc` closes the bar) and keeps Recto's improvement that every measurement is an ordinary annotation other viewers still show and that the numbers are exportable from `recto-cli` too.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (contextual mode bars) and 11; genuinely new surface, following the `Markup & Annotation Bar` chrome (`40 epx`, `RectoSurfaceBrush`, `1 epx` `RectoBorderSubtleBrush`) and the `Comments` panel list chrome.
**Job:** The user opens `Measure`, sets the scale once for the page (`1 in = 10 ft`), clicks two points to measure a distance, several for a perimeter, or closes a polygon for an area, watching the value update live and snapping to corners. They can see all measurements in a list, edit the scale, and export the list. Consumer: `DocumentView` annotation canvas, `SidePanelHost`, `CommandRegistry`.
**Treatment:** A `MeasureBar` bound to `MeasureViewModel` (`src/services/measure_view_model.h/.cpp`) using `§17` for values and `§18` for snapping, a `ScaleDialog` on the shared dialog chrome, a snap marker (square for endpoint, triangle for midpoint, cross for intersection, each with a text label in the readout so colour is never the only signal) and a `MeasurementsPanel`. Cheaper substitute that fails the checkpoint: a fixed unit with no scale dialog, snapping that only snaps to the grid, or a readout that is not stored as an annotation.
**Chrome:** consume `RectoColors.xaml` tokens, `D02 T01 §10` dialog styles, `SidePanelHost`, the `§4` selection frame, and Segoe Fluent Icons.

**Build order:**

1. Implement `MeasureViewModel` and `MeasureBar` with the three tools, live readout, and `Esc` to cancel the current measurement. Done when: `MeasureUi.DistancePerimeterArea` draws all three measurements on `text/measure-plan.pdf` and reads the expected values from the stored annotations.
2. Implement `ScaleDialog`, snap markers with a `Hold Alt to snap off` override, and the `MeasurementsPanel` with `Export CSV` and `Export JSON`. Done when: `MeasureUi.ScaleSnapExport` sets a new scale, snaps to a known corner within `1 pt`, and exports files equal to the `§17` golden files.

- [ ] Build `MeasureBar` (`src/app/controls/MeasureBar.xaml/.h/.cpp`, `40 epx`) with `Distance`, `Perimeter`, `Area`, `Scale...`, `Snap` toggle and `Measurements` buttons, each with an automation name and tooltip, opened by the floating toolbar `Annotate / Tools` menu and `Ctrl+K` (`Measure`). Done when: the bar opens, and every button passes the `D03 T02 §3` inventory.
- [ ] Build `ScaleDialog` with `Page length` and `Real length` number boxes and unit combo boxes (`1 [in] = 10 [ft]`), a `Use two points` helper that measures a known page length and asks for its real length, and a live preview of the resulting scale text. Done when: `MeasureUi.ScaleSnapExport` sets `1 in = 10 ft` by typing and `1 cm = 2.5 m` through `Use two points`. Cheaper substitute: a preset list of scales with no typed values.
- [ ] Draw the live segment, the running value in a pill near the pointer (`12.5 ft`), and, for area, the closing edge preview and the area in the pill. Done when: `MeasureUi.DistancePerimeterArea` reads the pill text while a polygon is open.
- [ ] Snap to the `§18` candidates within `8 epx`, show the snap marker and a text label (`Endpoint`, `Midpoint`, `Intersection`) in the pill, and fall back to no snap when the index is still building without blocking the pointer. Done when: `MeasureUi.ScaleSnapExport` snaps to an endpoint, a midpoint and an intersection and the pointer stays responsive while the index builds.
- [ ] Record each completed measurement as one `§4` undo entry through `DocumentActor` (`§17` annotation), with `Enter` finishing a perimeter or area and `Backspace` removing the last point. Done when: `MeasureUi.UndoAndEdit` undoes a polygon in one step and removes a last point with `Backspace`.
- [ ] Build `MeasurementsPanel` listing every measurement annotation (page, kind, value, label) with jump, delete, `Export CSV` and `Export JSON` writing through temp-file-then-replace, and a `stale` badge with the text `Scale changed` for entries whose stored value differs from the page scale. Done when: `MeasureUi.ScaleSnapExport` exports both files and shows the badge after a scale edit.
- [ ] Add `MeasureUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=MeasureUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add the Measure tool bar, scale dialog, snapping and Measurements panel`

**Test checkpoint:** `recto-tests.exe --gtest_filter=MeasureUi.*` passes on the display-session host, quoting the pill text and the exported CSV rows.

## 20. Stamp library store and dynamic stamp token resolution (no JavaScript)

Add the stamp library to `recto-services` and the stamp builder to `recto-pdf`: a persistent library of custom image stamps and dynamic stamps whose text carries tokens for date, time, user name and file name, resolved once when the stamp is applied. This matches the Acrobat Pro `Stamps` palette, `Custom Stamps` and `Dynamic Stamps`. Acrobat dynamic stamps are driven by JavaScript; Recto resolves tokens in native code at application time (D9), and the applied stamp is a static annotation with a static `/AP`, so it never changes when the file is reopened. Users need dynamic stamps with date, time and user metadata, and reusable dynamic image stamps.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `StampLibrary` (`src/services/stamp_library.h/.cpp`) persisting definitions under `AppPaths::data_dir()\stamps\` (`stamps.json` plus one PNG or PDF file per image stamp) with single-writer temp-file-then-replace saves, a `64` stamp limit, a `5 MB` per-image limit, and typed definitions (`ImageStamp`, `TextStamp`, `DynamicStamp`). Done when: `PdfStamps.LibraryPersistence` adds, edits, deletes and reloads all three kinds, survives a corrupt `stamps.json` by keeping the last good copy, and rejects a `6 MB` image.
2. Implement `resolve_stamp_tokens(template, StampContext)` and `build_stamp_annotation(definition, StampContext, rect)` in `src/pdf/stamps.h/.cpp`, producing a `Stamp` annotation (`§1`) with a static `/AP /N` form and `/Contents` holding the resolved text. Done when: `PdfStamps.TokenResolution` resolves every token once, and `PdfStamps.AppliedStampIsStatic` re-opens the saved file later and finds the resolved text unchanged.

- [ ] Define the token set in `src/pdf/stamps.h`: `{date}` (locale short date), `{date:yyyy-MM-dd}` (explicit pattern), `{time}` (locale short time), `{time:HH:mm}`, `{user}`, `{file}` (file name without folder), and `{{` and `}}` for literal braces; unknown tokens are kept as literal text and reported. Done when: `PdfStamps.TokenResolution` covers each token, an escaped brace and an unknown token.
- [ ] Resolve `{user}` from the stamp definition's saved author name, falling back to the Windows display name and then the account name, and never from the network; resolve `{file}` from the document's file name (`Untitled` when unsaved) and `{date}`/`{time}` from one `StampContext` clock value captured at application so every token in one stamp shows the same instant. Done when: `PdfStamps.OneClockPerStamp` shows a stamp with `{date}` and `{time}` built across a minute boundary uses a single instant.
- [ ] Reject a template over `500` characters or more than `20` tokens with `ErrorCode::InvalidArgument`, and never evaluate any expression, script or format string beyond the fixed token patterns. Done when: `PdfStamps.TemplateLimits` rejects both and `PdfStamps.NoScripting` shows a template containing `javascript:` or `app.alert` is rendered as literal text.
- [ ] Build an image stamp from a PNG or a single-page PDF as a Form XObject (`/Subtype /Form`) with a `/Matrix` and `/BBox` that keep aspect ratio, decoding PNG through WIC and rejecting images over `8000 x 8000` pixels, and add the text lines of a dynamic image stamp (image plus resolved text) below or beside it. Done when: `PdfStamps.ImageStampAP` produces a valid `/AP /N` for a PNG and a PDF stamp and the rendered output matches a golden image within SSIM `0.99` (`D00 T03 §2`).
- [ ] Ship the six standard stamps from `§1` (`Approved`, `Confidential`, `Draft`, `Final`, `Sign Here`, `Void`) as read-only library entries beside the user's, each with a preview PNG generated at build time. Done when: `PdfStamps.StandardStamps` lists the six read-only entries and refuses to edit them.
- [ ] Apply a stamp through `DocumentActor::add_annotation` with `/Name` set to the library id and `/Contents` set to the resolved text, so the library entry can be recognised later but the page never depends on it. Done when: `PdfStamps.AppliedStampIsStatic` deletes the library entry and the applied stamp still renders.
- [ ] Add `PdfStamps.*` tests in `tests/recto-tests/pdf_stamps_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfStamps.*` passes under ASan.
- [ ] Commit: `feat(core): add the stamp library, custom image stamps and dynamic stamp tokens`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfStamps.*` passes under ASan, with the single-instant and no-scripting cases quoted.

## 21. Stamp Manager, custom image stamps, dynamic stamp editor and sticky Stamp tool

Add the stamp surfaces: extend the `Stamp Picker` flyout (`§3`) with the user's library, a `Stamp Manager` dialog to add image stamps, create and edit dynamic stamps with a live preview of the resolved tokens, and a `Keep stamp tool selected` mode so the user can place several stamps without picking again, matching Acrobat Pro's `Stamps` palette and `Create Custom Stamp`. It must not break the `§3` flyout contract and keeps Recto's improvement that no stamp text is ever executed, so the editor previews exactly what will be written.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Stamp` picker with custom text and image stamps), 9.9 (dialog chrome) and 11; the picker follows `§3`'s `Stamp Picker` flyout and the manager follows the `D02 T01 §10` dialog chrome (no board exists for the manager).
**Job:** The user picks a stamp (standard, image, or dynamic), clicks the page to place it, and with `Keep stamp tool selected` on keeps placing it until `Esc`. In the manager they import a PNG or PDF as a stamp, write `Reviewed by {user} on {date} at {time}`, see the live preview, name it and save it. Consumer: `MarkupAnnotationBar`, `AnnotationToolViewModel` (`§3`), `StampLibrary` (`§20`).
**Treatment:** `StampManagerDialog` (`src/app/dialogs/StampManagerDialog.xaml/.h/.cpp`) over `StampLibrary` with a token chip row (`{user}`, `{date}`, `{time}`, `{file}`) inserting at the caret, a live preview, and a sticky flag on `AnnotationToolViewModel`. Cheaper substitute that fails the checkpoint: a stamp that can only show fixed text, a preview that differs from the applied result, or a tool that deselects after every placement.
**Chrome:** consume `RectoDialogStyles.xaml`, `RectoColors.xaml` tokens, the `§3` flyout, and Segoe Fluent Icons.

**Build order:**

1. Extend the `Stamp Picker` flyout with `My stamps` and `Manage stamps...` and add the sticky-tool flag to `AnnotationToolViewModel`. Done when: `StampUi.PickerAndSticky` places three stamps in a row with the flag on and one with it off, and `Esc` releases the tool.
2. Build `StampManagerDialog` with import, dynamic editor, token chips, live preview and delete. Done when: `StampUi.ManagerAndPreview` imports a PNG, creates a dynamic stamp, and the applied annotation text equals the preview text.

- [ ] Add a `My stamps` section and a `Manage stamps...` entry to the `Stamp Picker` flyout, each library entry showing its preview image and name, with the standard six under `Standard`. Done when: `StampUi.PickerAndSticky` lists the library entries and picking one arms `AnnotationToolMode::Stamp` with that definition.
- [ ] Add `keep_tool_selected` to `AnnotationToolViewModel`, a `Keep stamp tool selected` toggle in the flyout (persisted in `SettingsStore` as `KeepStampToolSelected`, default `false`, shown in Settings under Reading), and release on `Esc`, on choosing another tool, or on closing the bar. Done when: `StampUi.PickerAndSticky` verifies the setting persists and is read by the tool, and no setting is left unread.
- [ ] Build `StampManagerDialog` with a list of library stamps, `Import image or PDF...` (file picker for `.png`, `.jpg`, `.pdf`), `New dynamic stamp`, `Rename` and `Delete`, and show size errors from `§20` as inline messages. Done when: `StampUi.ManagerAndPreview` imports a PNG, rejects an oversized file with a message and deletes a stamp.
- [ ] Build the dynamic stamp editor: a multi-line text box, a token chip row inserting `{user}`, `{date}`, `{time}`, `{file}` at the caret, an optional image, and a live preview that calls `resolve_stamp_tokens` with the current clock and shows unknown tokens in `RectoWarningBrush` with the text `Unknown token`. Done when: `StampUi.ManagerAndPreview` shows the resolved text and flags `{usr}`. Cheaper substitute: previewing the raw template text.
- [ ] Apply a dynamic stamp by resolving once through `build_stamp_annotation` at click time, so placing the same stamp a minute later shows a different time and each placed copy is static. Done when: `StampUi.DynamicPlacement` places two copies with a faked clock a minute apart and reads two different `/Contents` values that stay unchanged after save and re-open.
- [ ] Add `Ctrl+K` entries `Manage Stamps`, `Keep Stamp Tool Selected` and one `Stamp: <name>` entry per library stamp, and give every control an automation name and tooltip. Done when: `StampUi.PickerAndSticky` finds the entries in the command box and the dialog passes the `D03 T02 §3` inventory.
- [ ] Add `StampUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=StampUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add the Stamp Manager, dynamic stamp editor and sticky Stamp tool`

**Test checkpoint:** `recto-tests.exe --gtest_filter=StampUi.*` passes on the display-session host, quoting the two resolved stamp texts from the one-minute-apart placements.

## 22. Fill and Sign: typed text, symbols and signatures on flat forms

Acrobat Pro's Fill and Sign lets a user complete a form that has no fields: type text anywhere, place a check, cross, dot, line or rounded box, and add a saved signature or initials. Recto has typewriter text (§2) and a signature appearance library (`D05 T02 §10`), but no single fill tool for flat forms (foundation plan D18; `docs/acrobat-parity.md`). The failure modes to avoid are typed text that becomes `?`, a placed signature that cannot be moved over a field, text boxes eaten when dragged across each other, and a flatten-on-save default buried in settings. This section adds a Fill and Sign mode that writes annotations through the existing engine, fills real AcroForm fields when the page has them, and flattens only when the user turns on `Lock on save`.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new contextual bar following the `Markup & Annotation Bar` chrome of §3 (`design/SPEC.md` section 8.2).
**Job:** A user fills a scanned or flat form, places check marks and signs it, then saves a copy that others see identically in any viewer. Consumer: the annotation engine (§1, §2), the form engine (§6) when fields exist, and the save paths.
**Treatment:** A `Fill & Sign` bar with `Text`, `Check`, `Cross`, `Dot`, `Line`, `Box`, `Sign` and `Initials` tools, size and colour, a visible `Lock on save` toggle, and click-to-place with a live preview at the pointer. Cheaper substitute that fails the checkpoint: a separate free-text annotation tool with no symbols and no field awareness.
**Chrome:** The §3 bar container, the §4 handles and undo stack, the `D05 T02 §10` signature library picker, Segoe Fluent Icons, `RectoColors.xaml` tokens.

**Build order:**

1. Add `FillSignTool` in `src/app/annotations/FillSignTool.h/.cpp` placing `FreeText` (typewriter intent) and symbol `Stamp` annotations through §1 and §2, with symbol appearances drawn as vector paths, and routing a click inside an AcroForm widget to that field's fill path (§8) instead. Done when: `Annot.FillSignPlacement` places each tool's object at the click point and fills a field when the click lands in one.
2. Add the `Fill & Sign` bar, `Lock on save` and Ctrl+K commands. Done when: `Annot.FillSignBar` passes and every tool is reachable from `Ctrl+K`.

- [ ] Draw check, cross, dot, line and rounded-box symbols as deterministic `/AP` vector streams with size and colour, never as font glyphs. Done when: each symbol renders identically in Recto and in the PDFium oracle of `D06 T01 §1`.
- [ ] Place typed text through the Unicode authoring path so any script round-trips and nothing becomes `?` (`D06 T03 §7`). Done when: a Czech, Greek and CJK entry extracts exactly after save and reopen.
- [ ] Route clicks inside an AcroForm widget to that field (§8), and let a placed signature or symbol move over a field without being captured by it. Done when: a signature dragged across a text field stays a signature and the field keeps its value.
- [ ] Keep overlapping fill objects independent: dragging one across another never merges or deletes either. Done when: two overlapping text objects both survive a drag and an undo.
- [ ] Add `Sign` and `Initials` tools that place an appearance from the `D05 T02 §10` library as an annotation, with no cryptographic signature, and say so in the tooltip. Done when: the placed appearance is a stamp annotation and the document's signature panel lists no signature. Cheaper substitute: rasterizing the signature into the page.
- [ ] Add a visible `Lock on save` toggle (off by default) that flattens Fill and Sign objects into page content on Save As only, never in place. Done when: the saved copy contains no Fill and Sign annotations when the toggle is on and the open document is unchanged.
- [ ] Register `fillSign.*` commands in `CommandRegistry` (`D02 T03 §11`) with a single-key tool switch only while the bar is active and no text field has focus. Done when: typing in a field never switches tools.
- [ ] Add `Annot.FillSignPlacement`, `Annot.FillSignBar` and `Annot.FillSignLockOnSave` in `tests/recto-tests/`. Done when: the suites pass on the display-session host.
- [ ] Commit: `annot: add Fill and Sign for flat forms with symbols, signatures and Lock on save`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Annot.FillSign*` passes on the display-session host, and a filled, locked copy renders identically in Recto and the PDFium oracle.

## 23. Review status state model engine (`/StateModel`, `/State`) in `annotations.cpp`

Add the review status model to `recto-pdf`: a `Text` annotation that records a state change as a reply to another annotation (ISO 32000-1 section 12.5.6.3: `/StateModel` `Review` with `/State` `Accepted`, `Rejected`, `Cancelled`, `Completed` or `None`, and `/StateModel` `Marked` with `/State` `Marked` or `Unmarked`, always with `/IRT` naming the annotation it applies to). It matches the comment status menu of Foxit PDF Editor and PDFelement, and Acrobat has it too. `§1` already round-trips `/IRT` replies, but nothing reads or writes a state, so a status set in Acrobat is lost on the next save. It must not break the reply tree of `§1` and keeps three Recto rules: the change is staged on the actor's copy and written with `IncrementalWriter` (never in place), every state change is kept as history instead of overwriting the last one, and no JavaScript is involved (D9).

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Define `ReviewState` (`None`, `Accepted`, `Rejected`, `Cancelled`, `Completed`), `MarkedState` (`Unmarked`, `Marked`) and `StateChange` (`model`, `state`, `author`, `date`, `nm`, `parent_nm`) in `src/pdf/annotations.h`, parse state annotations into the reply tree of `§1`, and compute `current_state(parent_nm, model)` as the newest change by `/M`, then by `/CreationDate`, then by array order. Done when: `PdfReviewState.ParseHistoryAndCurrent` reads a fixture with three Review changes and one Marked change on one comment and reports the newest of each model.
2. Implement `set_review_state` and `set_marked` writing a hidden `Text` state annotation through `DocumentActor` and `IncrementalWriter`, with `recto-cli annot state <list|set>`. Done when: `PdfReviewState.SetAndRoundTrip` and `Cli.AnnotState` set, save, re-open and list the same history under ASan.

- [ ] Parse `/StateModel` and `/State` on `Text` annotations in `src/pdf/annotations.cpp`, reject a state that does not belong to its model (`Accepted` under `Marked`) with `ErrorCode::InvalidArgument` on write and keep it as `unknown_state` on read without dropping the bytes. Done when: `PdfReviewState.ModelValidation` rejects the mismatched write and preserves a crafted foreign state through a save.
- [ ] Implement `set_review_state(parent_nm, state, author)` and `set_marked(parent_nm, marked, author)` on `pdf::Document` and `DocumentActor` that append a new `Text` annotation with `/IRT` set to the parent, `/RT /R`, the `Hidden` flag (bit 2, value `2`) and no `/AP`, so no viewer draws a second icon, with `/Contents` set to `<State> set by <author>` so viewers that ignore states still show a reply line, and `/CreationDate` and `/M` from one clock value. Done when: `PdfReviewState.SetAndRoundTrip` finds the appended annotation with each field and the parent comment unchanged. Cheaper substitute: overwriting `/State` on the parent, which loses the history.
- [ ] Refuse a state change whose parent is itself a state annotation or does not exist, bound the `/IRT` chain walk at depth `32` with a visited set, and cap the history at `1000` changes per parent. Done when: `PdfReviewState.HostileChains` returns `ErrorCode::InvalidArgument` or `ErrorCode::CorruptStructure` for a cyclic chain, a state on a state, and the 1001st change, with no hang under ASan.
- [ ] Carry states through the `§2` exchange formats: write and read the XFDF `state` and `statemodel` attributes and the JSON `state` and `stateModel` members, keeping `/IRT` links. Done when: `PdfReviewState.XfdfJsonRoundTrip` exports a commented fixture, imports it into a clean copy and finds the same current states and history.
- [ ] Add `recto-cli annot state list <file>` and `recto-cli annot state set <file> --nm <id> --model Review|Marked --state <name> --author <name> --out <out.pdf>` in `src/cli/cmd_annot.cpp`, writing through temp-file-then-replace and exiting `2` on an invalid model or state. Done when: `Cli.AnnotState` covers both verbs, the exit code `2` case and refusal to overwrite the input.
- [ ] Add `build_review_states` to `scripts/build-corpus.py` writing `text/review-states.pdf` (2 pages, 4 comments, one with Review history `None`, `Accepted`, `Rejected`, one with a Marked change, one reply thread) with its `PROVENANCE.md` row, and add `PdfReviewState.*` tests in `tests/recto-tests/pdf_review_state_tests.cpp`. Done when: `python scripts/build-corpus.py --check` passes and `recto-tests.exe --gtest_filter=PdfReviewState.*:Cli.AnnotState` passes under ASan.
- [ ] Commit: `feat(pdf): add the review status state model with history`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfReviewState.*:Cli.AnnotState` passes under ASan, quoting the history of the comment with three Review changes.

## 24. Review status in the Comments panel: show, set and filter

Show the review status on each comment card of the `Comments / Annotations` panel (`§5`), let the reviewer set it from the card and the on-canvas popover, and filter the list by status, matching the `Set Status` menu in Foxit PDF Editor and PDFelement (Acrobat has the same menu). It must not break the card layout or reply threading of `§5`, and keeps Recto's rules that a status is never shown by colour alone, every action is reachable from `Ctrl+K`, and each change is one entry on the `§4` undo stack.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.3 (`Comments / Annotations` side panel) and 11 (accessible card labels); the status chip and menu are a genuinely new control that follows the type and author filter chip chrome of `§5`.
**Job:** A reviewer marks a comment `Accepted`, `Rejected`, `Cancelled` or `Completed`, ticks `Marked` to flag it, and filters the panel to only the comments that are still open, so a long review can be worked through in order. Consumer: `CommentsPanelViewModel` (`§5`), the annotation popover (`§4`), `CommandRegistry`.
**Treatment:** A status chip (icon plus text) on every card opening a menu with the five Review states and a `Marked` toggle, a `Status` multi-select filter beside the type and author filters, a status count in the panel header, and the same menu in the `§4` popover. Cheaper substitute that fails the checkpoint: a colour-only dot with no text, a status that can be set but not filtered, or a setter that overwrites instead of calling `set_review_state` (`§23`).
**Chrome:** consume `SidePanelHost` (`D02 T03 §3`), the `§5` card and chip styles, `RectoColors.xaml` tokens, and Segoe Fluent Icons for each state.

**Build order:**

1. Extend `CommentsPanelViewModel` with `current_state` per card, a `StatusFilter` set (`None`, `Accepted`, `Rejected`, `Cancelled`, `Completed`, `Marked`) and `set_status` calls into `DocumentActor` that record undo entries. Done when: `ReviewUi.ViewModel` filters 100 cards by status and one `Ctrl+Z` reverses one status change.
2. Build the card chip, menu, filter dropdown, header count and popover entry, plus the `Ctrl+K` commands. Done when: `ReviewUi.PanelAccessibility` finds every control with an automation name and tooltip and the five set-status commands in the command box.

- [ ] Add `StatusFilter` and `status_counts()` to `src/services/comments_panel_view_model.h/.cpp` so the filter combines with the type, author and text filters of `§5` by intersection. Done when: `ReviewUi.ViewModel` shows `Accepted` plus author `Jane` returns only that intersection and an empty result shows the `§5` empty state.
- [ ] Render the status chip on each card in `src/app/views/panels/CommentsPanel.xaml` with a distinct glyph and the state name as text, hidden when the state is `None` and no `Marked` flag is set, and read as `Status: Accepted` by UI Automation. Done when: `ReviewUi.PanelAccessibility` reads the chip text and the card name includes the status. Cheaper substitute: colour only.
- [ ] Add the status menu (`None`, `Accepted`, `Rejected`, `Cancelled`, `Completed`, `Marked`) to the card and to the `§4` annotation popover, calling `DocumentActor::set_review_state` and `set_marked` with the user's annotation author name. Done when: choosing `Rejected` adds one state annotation, updates the chip in the same frame and `Ctrl+Z` removes it.
- [ ] Add the `Status` filter dropdown with counts per state to the panel header and show a header summary such as `12 comments, 4 open`. Done when: `ReviewUi.ViewModel` checks the counts after three status changes.
- [ ] Register `Set Status: Accepted`, `Set Status: Rejected`, `Set Status: Cancelled`, `Set Status: Completed`, `Clear Status` and `Toggle Marked` in `CommandRegistry`, acting on the selected comment. Done when: `ReviewUi.PanelAccessibility` finds the six commands in the command box and they are disabled with no selected comment.
- [ ] Add `ReviewUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=ReviewUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): show, set and filter review status in the Comments panel`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ReviewUi.*` passes on the display-session host, quoting the status counts before and after the filter.

## 25. Comment summary model, list exports and `recto-cli annot summarize`

Add the comment summary engine to `recto-services`: gather every comment into ordered rows with its page, author, type, date, text, the page text it marks, review status and replies, filter and sort them, report which pages carry comments, and write the rows as CSV, JSON or plain text. It matches `Summarize Comments` in Acrobat and the comment summary exports of Foxit PDF Editor and PDFelement. It must not break `§5` (the rows come from the same `/IRT` tree) and keeps Recto's rules that the job opens its own `recto::pdf::Document`, the input is never modified, and output is written temp-file-then-replace. The PDF, Word and extract outputs are `§26`, `§28` and `§29` and plug into the exporter registry defined here.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Define `CommentRow`, `SummaryOptions` (`authors`, `types`, `states`, `date_from`, `date_to`, `only_with_text`, `sort` among `Page`, `Author`, `Date`, `Type`), `CommentSummary` and `summarize_comments(Document&, SummaryOptions)` in `src/services/comment_summary.h/.cpp`, with `pages_with_comments(Document&, SummaryOptions)` returning a `PageRange`. Done when: `PdfCommentSummary.RowsFilterSort` returns the expected rows and page set from `text/review-states.pdf` for four option sets.
2. Implement the `SummaryExporter` registry (`id`, `label`, `extension`, `export(const ExportRequest&)`) with `csv`, `json` and `txt` exporters and `recto-cli annot summarize`. `ExportRequest` carries the `CommentSummary`, `SummaryOptions`, an exporter options map (`layout` and similar), the destination path, a `SourceSnapshot` (the bytes of the document's current staged revision serialized in memory by the actor, or the file path plus `password` when the CLI runs on a saved file), a `CancelToken` and a progress callback, so a job never reads stale comments from the file on disk and never shares the viewer's `recto::pdf::Document`. Done when: `PdfCommentSummary.ListExports` matches golden files byte for byte, `PdfCommentSummary.UnsavedEdits` exports a comment added after the last save and finds it in the output, and `Cli.AnnotSummarize` covers the three formats under ASan.

- [ ] Resolve each row's quoted text from `PageText` (`D03 T01 §1`) under the annotation's `/QuadPoints` or `/Rect`, truncated to `500` characters with an ellipsis flag, and leave it empty when the page has no text layer. Done when: `PdfCommentSummary.QuotedText` quotes the highlighted words of a two-column fixture and returns empty for an image-only page.
- [ ] Nest replies and state changes under their parent row in chronological order, with a `reply_depth` column, and exclude hidden state annotations from the row list while reporting the current status on the parent. Done when: `PdfCommentSummary.RepliesAndStates` shows one parent row with two replies and its current status.
- [ ] Implement the filters (`authors`, `types`, `states`, date range, `only_with_text`) and the four sort orders with a stable tie-break by page then `/NM`. Done when: `PdfCommentSummary.RowsFilterSort` checks each filter alone and combined.
- [ ] Bound the work: at most `100000` annotations are summarized, with `truncated = true` and a count of skipped annotations when exceeded, and cancellation through `CancelToken`. Done when: `PdfCommentSummary.Caps` summarizes a `150000`-annotation generated document to a truncated result and aborts within `50 ms` of cancel under ASan.
- [ ] Write CSV (RFC 4180, UTF-8, header row `page,author,type,status,date,text,quoted_text,reply_depth`), JSON and plain text through one shared `csv_escape_cell` in `src/services/csv_writer.h/.cpp` that quotes any cell holding a comma, quote, CR or LF, doubles embedded quotes, and prefixes a single quote to any untrusted cell (values and header names alike) whose first character is `=`, `+`, `-`, `@`, tab, CR or LF, unless `--raw` is passed, writing through temp-file-then-replace. Done when: `PdfCommentSummary.ListExports` matches goldens and `PdfCommentSummary.CsvEscaping` covers cells starting with each of those seven characters, embedded tabs, CR, LF, commas and quotes, and a raw-mode run. Cheaper substitute: checking only a leading `=`.
- [ ] Add `recto-cli annot summarize <file.pdf> --format csv|json|txt|pdf|docx [--author A]... [--type T]... [--status S]... [--from DATE] [--to DATE] [--sort page|author|date|type] [--only-pages-with-comments] --out <path>` in `src/cli/cmd_annot.cpp`, where formats registered by later sections are accepted only when their exporter is built, exit code `2` for an unknown format. Done when: `Cli.AnnotSummarize` covers each registered format, the filters and the exit code `2` case.
- [ ] Add `PdfCommentSummary.*` tests in `tests/recto-tests/pdf_comment_summary_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfCommentSummary.*:Cli.AnnotSummarize` passes under ASan.
- [ ] Commit: `feat(services): add the comment summary model, list exports and annot summarize`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCommentSummary.*:Cli.AnnotSummarize` passes under ASan, with the CSV formula-neutralizing case quoted.

## 26. Comment summary PDF: comments beside pages and a comments-only list

Add the PDF exporter for the comment summary: a new PDF with each commented page on the left and its numbered comments on the right, or a paginated table of comments, matching `Create Comment Summary` in Acrobat and the comment summary PDF of Foxit PDF Editor and PDFelement. It must not modify the source (the pages are wrapped as Form XObjects in a new document), must show any script (comment text goes through the same Unicode authoring path as `§22`), and keeps Recto's rule that the report is written temp-file-then-replace.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `write_summary_pdf` in `src/services/comment_summary_pdf.h/.cpp` for two layouts, `BesidePages` and `ListOnly`, drawing numbered markers on the page and a numbered column of comment text, status, author and date, flowing long comment columns onto continuation sheets. Done when: `PdfCommentReport.BesidePages` and `PdfCommentReport.ListOnly` produce PDFs that re-open with the expected page counts and extract the comment text exactly.
2. Register the `pdf` exporter in the `§25` registry and add `--layout beside|list` to the CLI verb. Done when: `Cli.AnnotSummarize` writes both layouts and the output opens in the PDFium oracle of `D06 T01 §1` without error.

- [ ] Wrap each commented source page in a Form XObject (`/BBox` from the crop box, the page `/Resources` and concatenated `/Contents`), then draw the page's annotation `/AP /N` streams on top with the `/Matrix` and `/Rect` placement used by `flatten_annotations` (`§2`) so the report shows markup exactly as the page does. Done when (the source pages come from `ExportRequest::source`, never from the file on disk): `PdfCommentReport.PageFidelity` rasterizes the embedded page and its markup with SSIM `>= 0.99` against the source page flattened with `§2`.
- [ ] Place a numbered marker (filled circle with the number as text) at each comment's anchor and print the same number before its text in the right column, never relying on colour. Done when: `PdfCommentReport.Markers` finds marker `3` on the page and comment `3` in the column for a fixture with five comments.
- [ ] Write comment text through the Unicode authoring path (`D06 T03 §7`) with font subsetting and embedding, wrapping at the column width and breaking only at word boundaries or inside a word longer than the column. Done when: a Czech, Greek and CJK comment each extracts exactly from the report.
- [ ] Draw the `ListOnly` layout as a table on a page size chosen from `Letter` or `A4` (default by the Windows region, recorded as a default) with columns `No.`, `Page`, `Author`, `Status`, `Date`, `Comment`, repeating the header on each continuation page. Done when: `PdfCommentReport.ListOnly` shows the header on every sheet of a 60-comment run.
- [ ] Add a document `/Info` `/Title` (`Comment summary of <file name>`), a `/Lang`, and a first-page footer naming the source file name, the export date and the filter in words. Done when: `PdfCommentReport.Metadata` reads the three values.
- [ ] Write the report with `AtomicFileWriter` and refuse an output path equal to the input path with `ErrorCode::InvalidArgument`. Done when: `PdfCommentReport.NeverOverwritesInput` returns the error and leaves the input byte-identical. Cheaper substitute: writing next to the input with a fixed name, which can overwrite a prior report.
- [ ] Add `PdfCommentReport.*` tests in `tests/recto-tests/pdf_comment_report_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfCommentReport.*` passes under ASan.
- [ ] Commit: `feat(services): add the comment summary PDF with comments beside pages`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCommentReport.*:Cli.AnnotSummarize` passes under ASan, with the marker and SSIM cases quoted.

## 27. `Summarize Comments` dialog in the Comments panel

Add the dialog that drives the comment summary: choose a format, filters, sort and layout, see how many comments and pages will be included, and save. It matches `Summarize Comments` in Acrobat and the comment export dialogs of Foxit PDF Editor and PDFelement. It must not break the Comments panel (`§5`) and keeps Recto's rules that the export runs as a cancellable job on its own `recto::pdf::Document`, the open document is never modified, and the command is reachable from `Ctrl+K`.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 9.9 (shared `ContentDialog` chrome) and 11; genuinely new surface that follows `D02 T01 §10` dialog styles and the filter controls of `§5`.
**Job:** A reviewer or project lead exports the review as a report to send on, choosing only open comments from one author, for example, and sees the count before saving. Consumer: `CommentsPanel` header menu (`§5`), `CommandRegistry`, and the `§25` exporter registry.
**Treatment:** A `SummarizeCommentsDialog` listing the formats the registry offers, a layout choice for PDF, the filters of `§25` including status, a live count line, and a `Save` button that starts a job with progress and `Cancel`. Cheaper substitute that fails the checkpoint: a fixed single format with no filters, or an export that blocks the UI thread.
**Chrome:** consume `RectoDialogStyles.xaml` (`D02 T01 §10`), `RectoColors.xaml` tokens, the `§5` header menu, and `FileSavePicker`.

**Build order:**

1. Implement `SummarizeCommentsViewModel` in `src/services/summarize_comments_view_model.h/.cpp` holding options, the live `pages_with_comments` count and the job. Done when: `SummaryUi.ViewModel` updates the count on each filter change within one frame for a 5000-comment fixture.
2. Build `SummarizeCommentsDialog`, the `Comments` panel menu entry and the `Ctrl+K` command. Done when: `SummaryUi.DialogAccessibility` finds every control with an automation name and tooltip.

- [ ] Build `SummarizeCommentsDialog` (`src/app/dialogs/SummarizeCommentsDialog.xaml/.h/.cpp`) with a format `ComboBox` fed from the registry, a layout control shown only for PDF, filter controls for author, type, status and dates, a sort selector, an `Only pages with comments` toggle and a live line `18 comments on 7 pages`. Done when: `SummaryUi.DialogAccessibility` passes the `D03 T02 §3` inventory.
- [ ] Run the export as a job on its own `recto::pdf::Document` with a progress bar and `Cancel`, showing the saved path and an `Open` and `Show in folder` button when done. Done when: `SummaryUi.JobAndCancel` cancels a large export and leaves no partial file. Cheaper substitute: exporting on the UI thread.
- [ ] Add `Summarize Comments...` to the `§5` header menu and register `Summarize Comments` and `Export Comments to Report` in `CommandRegistry`. Done when: `SummaryUi.DialogAccessibility` finds both commands in the command box.
- [ ] Remember the last chosen format and layout for the session only (no new setting), defaulting to PDF beside pages. Done when: `SummaryUi.ViewModel` reopens the dialog with the previous choices in the same session and with the defaults in a new one.
- [ ] Add `SummaryUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=SummaryUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add the Summarize Comments dialog`

**Test checkpoint:** `recto-tests.exe --gtest_filter=SummaryUi.*` passes on the display-session host, quoting the live count line before and after a filter change.

## 28. Export comments to Word `.docx` through the shared `DocxWriter`

Add the Word exporter for the comment summary: a `.docx` with one heading per commented page, each comment as a paragraph with author, date, status and quoted text, and replies indented, matching the export of comments to Word in Foxit PDF Editor and PDFelement (Acrobat exports comments to Word through its Word add-in). It reuses the `DocxWriter` and OpenXML scaffold of `D04 T03 §1` and `§2` through a block-document entry point instead of a second writer, and keeps Recto's rules that no macro part is ever written and the file is written temp-file-then-replace.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Convert a `CommentSummary` into a `BlockDocument` (`Heading1`, `Heading2`, `Paragraph`, `Quote`, `Table` blocks) in `src/services/comment_summary_blocks.h/.cpp` and hand it to `DocxWriter::write_blocks`. Done when: `DocxCommentExport.Blocks` yields the expected block sequence for `text/review-states.pdf`.
2. Add `DocxWriter::write_blocks(const BlockDocument&, path)` to `src/core/docx_writer.h/.cpp` if `D04 T03 §2` has not already provided an equivalent, reusing its `styles.xml` and `numbering.xml` emitters, register the `docx` exporter in the `§25` registry and extend `recto-cli annot summarize --format docx`. Done when: `DocxCommentExport.ReopenValidation` writes the file, re-opens it with the `D04 T04 §1` reader and finds every comment text.

- [ ] Define `BlockDocument` and its block types in `src/core/docx_blocks.h` as plain data with no PDF types, so the Word writer stays independent of `recto-pdf`. Done when: the header compiles in `recto-core` and `recto-cli` and has no include of `src/pdf`.
- [ ] Emit headings as `Heading1` (`Comments on <file name>`) and `Heading2` (`Page N`), each comment as a paragraph with a bold `author, date, status` run, then the quoted text in a `Quote` style and replies at a deeper `w:ind` level, and a table layout option with columns `No.`, `Page`, `Author`, `Status`, `Comment`. Done when: `DocxCommentExport.Structure` finds the styles and indent levels in `word/document.xml`.
- [ ] Write comment text through the `XmlWriter` of `D04 T03 §1` so control characters are stripped and entities escaped, and never write a `vbaProject.bin`, field code or external relationship. Done when: `DocxCommentExport.HostileText` writes a comment containing `<`, `&`, a NUL and `{ INCLUDETEXT }` as inert text and the package has no external relationship.
- [ ] Register the `docx` exporter and add `--layout paragraphs|table` for it in `recto-cli annot summarize`. Done when: `Cli.AnnotSummarize` writes both layouts and exits `2` for `--layout beside` with `docx`.
- [ ] Add `DocxCommentExport.*` tests in `tests/recto-tests/docx_comment_export_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=DocxCommentExport.*` passes under ASan.
- [ ] Commit: `feat(core): export comments to Word through the shared DocxWriter`

**Test checkpoint:** `recto-tests.exe --gtest_filter=DocxCommentExport.*:Cli.AnnotSummarize` passes under ASan, with the hostile-text package inspection quoted.

## 29. Save only the pages with comments as a new PDF

Add the `Pages with comments` output: a copy of the document holding only the pages that carry comments (optionally filtered by author or status), with their annotations intact, matching the `Print only pages with comments` and extract options of Foxit PDF Editor and PDFelement. It reuses `extract_pages` of `D05 T01 §2` and `pages_with_comments` of `§25`, never edits the open document, and exposes the page set so printing (`D07 T01`) can use the same list.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `extract_commented_pages(const ExportRequest&)` (using `request.source`, so unsaved comments count) in `src/services/comment_summary.cpp` calling `pages_with_comments` then `extract_pages`. Done when: `PdfExtractCommented.KeepsAnnotations` extracts pages 2 and 5 of a 6-page fixture and finds all annotations and replies on them.
2. Register the `pages-pdf` exporter in the `§25` registry (shown in the `§27` dialog as `Pages with comments (PDF)`) and add `recto-cli annot extract-commented`. Done when: `Cli.AnnotSummarize` and `PdfExtractCommented.Cli` write the file and exit `3` with a clear message when the document has no comments.

- [ ] Return the page set as a `PageRange` from `pages_with_comments` for other consumers, with a documented ordering (ascending, no duplicates). Done when: `PdfExtractCommented.PageSet` checks the ordering and that a state annotation alone does not make a page count.
- [ ] Refuse an output path equal to the input and write through temp-file-then-replace. Done when: `PdfExtractCommented.NeverOverwritesInput` returns `ErrorCode::InvalidArgument` and leaves the input byte-identical. Cheaper substitute: deleting the other pages from a copy in place.
- [ ] Add `recto-cli annot extract-commented <file.pdf> --out <out.pdf> [--author A]... [--status S]...` in `src/cli/cmd_annot.cpp` with exit code `3` for no comments. Done when: `Cli.AnnotSummarize` covers the verb and both exit codes.
- [ ] Add `PdfExtractCommented.*` tests in `tests/recto-tests/pdf_extract_commented_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfExtractCommented.*` passes under ASan.
- [ ] Commit: `feat(services): save only the pages with comments as a new PDF`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfExtractCommented.*` passes under ASan, with the kept annotation count quoted.

## 30. Create web links from URLs in page text and remove all links

Add two bulk link operations to `recto-pdf` and `recto-cli`: find URLs and email addresses written as plain text on a page and turn each into a `Link` annotation, and remove every link annotation from chosen pages. It matches `Create Links from URLs` and `Remove All Links` in Foxit PDF Editor (Acrobat has `Remove All Links` and PDF export that makes links). It builds on the link authoring engine (`§11`), so every target passes the same validation, never writes a `/Launch` or script action, and each link still asks before it opens (`D02 T02 §9`). Detection runs on the immutable `PageText` snapshot (`D03 T01 §1`) off the UI thread.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `find_url_candidates(const PageText&, UrlOptions)` in `src/pdf/auto_links.h/.cpp` recognizing `http://`, `https://`, `www.`, `mailto:` and bare email addresses, trimming trailing punctuation with a balanced-bracket rule, and returning one `UrlCandidate` per line fragment with `quad_pt`, `target` and `wrapped`. Done when: `PdfAutoLinks.Detect` finds the expected 14 candidates in a fixture and none in prose with no URLs.
2. Implement `create_links_from_candidates` and `remove_links(PageRange, RemoveScope)` through `DocumentActor` (`§11`) and `recto-cli annot links`. Done when: `PdfAutoLinks.CreateAndRemove` creates links, saves, re-opens, removes them by scope and the saved bytes hold no remaining link of that scope.

- [ ] Trim trailing `.`, `,`, `;`, `:`, `!`, `?`, quotes and an unbalanced `)` or `]` from a candidate, keep a balanced pair inside it, and treat `www.` as `https://www.` (recorded default; changing it costs one constant), never inventing a scheme for other text. Done when: `PdfAutoLinks.Trimming` covers `(see https://a.example/b).`, `https://a.example/q?x=(1)` and `www.example.org,`.
- [ ] Join a URL that wraps across two lines only when the first fragment ends at the right edge of its text block with no space, the next line starts at the same left edge and the joined text validates, and mark the candidate `wrapped` so the review list can show it separately. Done when: `PdfAutoLinks.WrappedUrl` joins one such URL and leaves two unrelated lines alone.
- [ ] Skip a candidate whose rectangle overlaps an existing `Link` annotation by more than `50%`, validate every target with the `WebUri` rules of `§11` (length `2048`, allowed schemes `http`, `https`, `mailto`, no control characters), and report each rejected candidate with its reason. Done when: `PdfAutoLinks.SkipAndValidate` skips an existing link, rejects a `javascript:` string and a `3000`-byte URL and reports both.
- [ ] Bound the work at `5000` candidates per page and `200000` text runs per page with a `truncated` flag. Done when: `PdfAutoLinks.Caps` returns a truncated result for a generated 20000-URL page within the time limit under ASan.
- [ ] Implement `remove_links(PageRange, RemoveScope)` with scopes `All`, `WebAndEmail`, `Internal` and `Inert`, leaving named destinations and non-link annotations untouched, and return the count removed per page. Done when: `PdfAutoLinks.CreateAndRemove` removes only the chosen scope and a following `§12` destination listing is unchanged.
- [ ] Add `recto-cli annot links <list|add-from-text|remove> <file.pdf> [--pages 1-5] [--scope all|web|internal|inert] [--dry-run] --out <out.pdf>` in `src/cli/cmd_annot.cpp`, with `--dry-run` printing the counts and writing nothing. Done when: `Cli.AnnotLinks` covers the three verbs, `--dry-run` and exit code `2` for a bad scope.
- [ ] Add `build_url_text` to `scripts/build-corpus.py` writing `text/url-text.pdf` (2 pages, 14 URLs and addresses including trailing punctuation, brackets, one wrapped URL and one existing link) with its `PROVENANCE.md` row, and add `PdfAutoLinks.*` tests in `tests/recto-tests/pdf_auto_links_tests.cpp`. Done when: `python scripts/build-corpus.py --check` passes and `recto-tests.exe --gtest_filter=PdfAutoLinks.*:Cli.AnnotLinks` passes under ASan.
- [ ] Commit: `feat(pdf): create links from URLs in page text and remove all links`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfAutoLinks.*:Cli.AnnotLinks` passes under ASan, with the trimming cases quoted.

## 31. `Create Links from URLs` and `Remove All Links` commands with a review list

Add the viewer commands for the bulk link operations: a review dialog listing the URLs found with a checkbox each, a confirmed removal with a count, both reachable from the `Link` button's menu and `Ctrl+K`. It matches Foxit PDF Editor's `Create Links from URLs` and the `Remove All Links` commands of Foxit and Acrobat. It must not break the `§13` Link tool and keeps Recto's rules that nothing is created without the user seeing the list, every change is one undo entry, and a created link still asks before opening.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 9.9 (dialog chrome), 8.2 (`Link` region tool) and 11; genuinely new dialog following `D02 T01 §10` dialog styles and the `§13` flyout chrome.
**Job:** A user opens a document whose web addresses are plain text, runs `Create Links from URLs`, unticks the ones they do not want and applies, or removes every link from a range of pages in one step. Consumer: `DocumentActor` link calls (`§30`), the `§4` undo stack, `CommandRegistry`.
**Treatment:** A `CreateLinksDialog` with a page scope, a list of candidates (URL in monospace, page, a `Wrapped` note), `Select all`, a count line and `Create`, plus a `Remove Links` confirmation naming the scope and count. Cheaper substitute that fails the checkpoint: creating every link silently, or a removal with no count and no undo.
**Chrome:** consume `RectoDialogStyles.xaml` (`D02 T01 §10`), `RectoColors.xaml` tokens, the `§13` `Link` button, and Segoe Fluent Icons.

**Build order:**

1. Implement `AutoLinksViewModel` in `src/services/auto_links_view_model.h/.cpp` running detection as a background job with cancel and exposing the candidate list, selection and counts. Done when: `AutoLinkUi.ViewModel` lists candidates for `text/url-text.pdf`, toggles selection and reports the count.
2. Build `CreateLinksDialog` and `RemoveLinksDialog`, the `Link` button menu and the `Ctrl+K` commands. Done when: `AutoLinkUi.DialogAccessibility` finds every control with an automation name and tooltip.

- [ ] Build `CreateLinksDialog` (`src/app/dialogs/CreateLinksDialog.xaml/.h/.cpp`) with scope (`This page`, `All pages`, `Page range`), a virtualized candidate list with checkboxes, and a `Create 12 links` primary button. Done when: `AutoLinkUi.DialogAccessibility` passes the `D03 T02 §3` inventory and the list stays responsive with `5000` candidates.
- [ ] Apply the ticked candidates as one `§4` undo entry through `DocumentActor::add_link` and announce `Created N links` through `AnnouncementService` (`D03 T02 §5`). Done when: `AutoLinkUi.CreateAndUndo` creates links and one `Ctrl+Z` removes all of them.
- [ ] Build `RemoveLinksDialog` with a scope choice (`All links`, `Web and email links`, `Internal links`, `Unsafe imported links`), a page scope and a count line, and apply as one undo entry. Done when: `AutoLinkUi.RemoveAndUndo` removes the chosen scope and `Ctrl+Z` restores every link. Cheaper substitute: removing without a count or an undo.
- [ ] Add a `More link actions` menu to the `§13` `Link` button and register `Create Links from URLs` and `Remove All Links` in `CommandRegistry`. Done when: `AutoLinkUi.DialogAccessibility` finds both commands in the command box.
- [ ] Add `AutoLinkUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=AutoLinkUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add Create Links from URLs and Remove All Links with a review list`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AutoLinkUi.*` passes on the display-session host, quoting the created count and the restored count after undo.

## 32. QR Code, Data Matrix and PDF417 encoders with vector output

Add native 2D barcode encoders to `recto-pdf`: QR Code (ISO/IEC 18004), Data Matrix ECC 200 (ISO/IEC 16022) and PDF417 (ISO/IEC 15438), each producing a module matrix and a vector content stream. This is the shared engine for PDFelement's QR code stamps and the barcode form fields of Foxit PDF Editor and Acrobat (`§33`, `§34`). Encoding is plain native code with no script, no network and no third-party runtime (D9, D16), the output is vector paths so it prints and scales without blur, and every input size is bounded.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `encode_qr(bytes, QrOptions)` (error correction `L`, `M`, `Q`, `H`; versions `1` to `40` chosen automatically; mask chosen by the standard's penalty rules; numeric, alphanumeric and byte modes) returning a `BitMatrix`. Done when: `PdfBarcode.QrKnownAnswers` matches the golden matrix of the worked `01234567` version 1-M example in ISO/IEC 18004 and a second recorded golden for a byte-mode URL, and every codeword block has zero Reed-Solomon syndromes.
2. Implement `encode_data_matrix` and `encode_pdf417` and `matrix_to_content_stream(BitMatrix, BarcodeStyle)`. Done when: `PdfBarcode.DataMatrixAndPdf417` matches recorded goldens and `PdfBarcode.ContentStream` renders a matrix whose modules re-sampled from the raster equal the matrix.

- [ ] Add `src/pdf/barcode.h/.cpp` with `BitMatrix`, `BarcodeKind`, `QrOptions`, `BarcodeStyle` (`module_size_pt`, `quiet_zone_modules`, `foreground`, `background`) and the encoders returning `PdfResult<BitMatrix>`. Done when: both files compile with zero warnings.
- [ ] Implement Reed-Solomon over GF(256) once (primitive polynomials `0x11D` for QR and `0x12D` for Data Matrix) and GF(929) for PDF417, with table generation at startup rather than hand-copied tables. Done when: `PdfBarcode.ReedSolomon` re-computes syndromes of random codewords to zero for all three fields.
- [ ] Reject empty input and any payload over the symbol limit (QR version 40-L byte mode `2953` bytes, Data Matrix `1556` bytes, PDF417 `1850` bytes) with `ErrorCode::InvalidArgument`, and cap accepted input at `4096` bytes before encoding. Done when: `PdfBarcode.Limits` rejects 2954 QR bytes and an empty payload and accepts 2953.
- [ ] Emit `matrix_to_content_stream` as filled rectangles with horizontal runs merged, a quiet zone of at least `4` modules for QR, `1` for Data Matrix and `2` for PDF417 (the standards' minimums), and `foreground` and `background` as DeviceRGB, refusing a contrast ratio below `3:1` between them. Done when: `PdfBarcode.ContentStream` emits fewer than `1.2` rectangles per dark module and `PdfBarcode.Contrast` rejects light grey on white, and `PdfBarcode.QuietZone` measures the blank border of each emitted stream and finds exactly `4`, `1` and `2` modules at the default style and rejects a style asking for less.
- [ ] Add the independent-decoder check: encode `20` fixed payloads per symbology, rasterize at 8 pixels per module and decode with the reference decoder recorded in `tests/corpus/PROVENANCE.md`, comparing the payload. Done when: `PdfBarcode.IndependentDecode` recovers all 60 payloads. Cheaper substitute: comparing only against Recto's own encoder output.
- [ ] Add `PdfBarcode.*` tests in `tests/recto-tests/pdf_barcode_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfBarcode.*` passes under ASan.
- [ ] Commit: `feat(pdf): add QR Code, Data Matrix and PDF417 encoders with vector output`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfBarcode.*` passes under ASan, quoting the ISO example match and the independent decode count.

## 33. `Add Barcode` tool: place a QR Code, Data Matrix or PDF417 on a page

Add the viewer tool that places a barcode image on a page from text the user types, matching PDFelement's QR code tool (Foxit PDF Editor and Acrobat also place barcodes, as form fields in `§35`). The result is a static `Stamp` annotation with a vector `/AP` from `§32`, so any viewer shows and prints it. It must not break the annotation model of `§1` to `§4`, never contacts a network (the text is encoded locally), and keeps Recto's rule that the content is also stored as `/Contents` so a screen reader and a text search can read what the code says.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Markup & Annotation Bar`, `Stamp` picker) and 9.9 (dialog chrome); genuinely new dialog following `D02 T01 §10` dialog styles and the `Stamp Manager` of `§21`.
**Job:** A user adds a QR code for a web address, a Wi-Fi sign-in or a short note, sees it update as they type, places it, and later moves, resizes or edits it. Consumer: `MarkupAnnotationBar` (`§3`), the `§4` selection frame, the `§20` stamp builder pattern.
**Treatment:** A `Barcode` button on the bar opening a `BarcodeDialog` with type, content, error correction, size and colours, a live preview, a size-in-modules readout, and a `Place` button that arms click-to-place. Cheaper substitute that fails the checkpoint: a fixed QR type with no editing after placement, or a preview that differs from the placed result.
**Chrome:** consume `RectoDialogStyles.xaml`, `RectoColors.xaml` tokens, the `§3` bar and `§4` frame, and Segoe Fluent Icons (the `QRCode` glyph of the Segoe Fluent Icons table).

**Build order:**

1. Implement `build_barcode_annotation(BarcodeSpec, rect)` in `src/pdf/barcode_annotation.h/.cpp` creating a `Stamp` annotation (`/Name /Recto#20Barcode` custom name, `/Contents` the encoded text, a private `Recto`-prefixed dictionary holding kind, error correction and colours so it can be edited) with the `§32` content stream as `/AP /N`. Done when: `PdfBarcode.AnnotationRoundTrip` places a QR, saves, re-opens and reads back the spec.
2. Build `BarcodeDialog`, the `Barcode` button, click-to-place, and edit on double-click. Done when: `BarcodeUi.PlaceAndEdit` places a QR code, edits its text and finds the new matrix in the saved file.

- [ ] Build `BarcodeDialog` (`src/app/dialogs/BarcodeDialog.xaml/.h/.cpp`) with a type `ComboBox`, a multi-line content box, helper entries that insert `https://`, `mailto:` and `WIFI:T:WPA;S:<name>;P:<password>;;` templates, error correction, size and colour pickers, a live preview and a `Too much data for this symbol` message from the `§32` limits. Done when: `BarcodeUi.DialogAccessibility` finds every control with an automation name and tooltip and the limit message appears at 2954 bytes.
- [ ] Keep the aspect ratio when the annotation is resized with the `§4` frame, snapping to whole module sizes on release, and regenerate the `/AP` on release. Done when: `BarcodeUi.PlaceAndEdit` resizes a QR and finds a square `/Rect` with a whole number of points per module.
- [ ] Warn in the dialog when the chosen size would put a module under `0.5 mm` (about `1.4 pt`, a printing default recorded here) so the user knows it may not scan, without blocking. Done when: `BarcodeUi.SizeWarning` shows the warning for a 20 pt QR of version 10 and not for 120 pt.
- [ ] Register `Add Barcode`, `Add QR Code` and `Edit Barcode` in `CommandRegistry` and give the button an automation name `Barcode tool` and tooltip `Barcode`. Done when: `BarcodeUi.DialogAccessibility` finds the three commands in the command box.
- [ ] Add `BarcodeUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=BarcodeUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add the Add Barcode tool for QR Code, Data Matrix and PDF417`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfBarcode.AnnotationRoundTrip:BarcodeUi.*` passes on the display-session host, quoting the decoded payload of the placed code.

## 34. Barcode form field engine: a text field whose barcode is built from other fields

Add the barcode form field to `recto-pdf`: a `Tx` field whose appearance is a barcode generated from the values of other fields, matching the barcode field of Acrobat and Foxit PDF Editor. Those products generate it with a JavaScript format action. Recto never runs form JavaScript (D9, D12), so the values are encoded by Recto's own code (`§32`) whenever a source field changes, the field stores its encoded text in `/V`, and the widget keeps a static `/AP /N` so any viewer shows the last generated code. Other viewers do not regenerate it when a field changes; the section says so and does not hide it. It must not break `/CO` ordering (`§7`) and bounds every size.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Define `BarcodeFieldSpec` (`kind`, `error_correction`, `template`, `delimiter`, `max_bytes`) stored in a `Recto`-prefixed private dictionary on the widget, with `set_barcode_field`, `clear_barcode_field` and `refresh_barcode_fields(Document&)` in `src/pdf/forms_barcode.h/.cpp`. Done when: `PdfBarcodeField.SpecRoundTrip` writes, saves, re-opens and reads the spec.
2. Resolve the template from source field values (tokens `{field:<fully qualified name>}` and `{{`, `}}` only, no expressions), encode with `§32` and regenerate `/V` and `/AP /N` after `/CO` evaluation in the same pass. Done when: `PdfBarcodeField.RefreshAfterCalc` changes a quantity, sees the total recalculate and the barcode text and matrix update.

- [ ] Resolve `{field:Name}` tokens from the field tree of `§6`, report an unknown field name as a `BarcodeFieldWarning` and keep the literal text, and reject a template over `500` characters or more than `20` tokens with `ErrorCode::InvalidArgument`. Done when: `PdfBarcodeField.TemplateRules` covers unknown names, both limits and escaped braces.
- [ ] Mark the widget `ReadOnly`, keep `/V` as the encoded text so `JSON`, `FDF`, `XFDF` and `XML` exports (`§7`) carry it, and store the content-bytes cap `max_bytes` (default `1000`) so an oversize value shows an error appearance instead of a broken code. Done when: `PdfBarcodeField.DataIo` exports `/V` and `PdfBarcodeField.TooLarge` shows the error appearance for 1001 bytes.
- [ ] Call `refresh_barcode_fields` from `set_field_value`, `reset_form_fields` and the data import of `§7` after calculation, and make `flatten_forms` (`§6`) bake the current appearance. Done when: `PdfBarcodeField.RefreshAfterCalc` and `PdfBarcodeField.Flatten` pass with SSIM `>= 0.99` before and after flattening.
- [ ] Guard against refresh loops: a barcode field may not name itself or another barcode field as a source, checked at write time and read time. Done when: `PdfBarcodeField.NoSelfReference` rejects both with `ErrorCode::InvalidArgument` and a crafted cyclic file lists a warning and does not hang.
- [ ] Preserve, byte for byte, an Acrobat or Foxit barcode field whose barcode is produced by a script action: keep its `/AA` untouched, never run it, mark the field `inert_script` and keep its last `/AP`. Done when: `PdfBarcodeField.ForeignBarcodeInert` opens a crafted fixture, saves without touching it and finds the action bytes unchanged.
- [ ] Add `build_barcode_form` to `scripts/build-corpus.py` writing `text/barcode-form.pdf` (a 1-page order form with 3 source fields, a `Sum` total and one QR field) with its `PROVENANCE.md` row, and add `PdfBarcodeField.*` tests in `tests/recto-tests/pdf_barcode_field_tests.cpp`. Done when: `python scripts/build-corpus.py --check` passes and `recto-tests.exe --gtest_filter=PdfBarcodeField.*` passes under ASan.
- [ ] Commit: `feat(pdf): add barcode form fields generated by Recto's own encoders`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfBarcodeField.*` passes under ASan, with the foreign-script field preserved unchanged.

## 35. `Add Barcode Field` designer tool, field properties and live fill refresh

Add the designer and fill surface for barcode fields: an `Add Barcode Field` tool in `Design Form` mode, a `Barcode` section in `Field Properties`, and a live refresh while filling. It matches the barcode field of Acrobat and Foxit PDF Editor without their JavaScript. It must not break `§9`, `§10` or `§8`, and keeps Recto's rules that every control has an automation name and tooltip, a field edit is one undo entry, and the preview shown is the code that will be saved.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Form Fill & Form Designer Bar`) and 11; genuinely new control following the `Field Properties` sections of `§10` and the placement tools of `§9`.
**Job:** A form author drops a barcode field, ticks the fields it encodes and the symbol type, sees a preview, and a person filling the form watches the code change as they type. Consumer: `FormBar` (`§9`), `FieldPropertiesPane` (`§10`), `FormCanvasOverlay` (`§8`), `§34`.
**Treatment:** An `Add Barcode Field` button in the designer tool group, a `Barcode` section in the inspector with type, error correction, a source-field checklist with a template box and a live preview, and a refreshed appearance on the canvas on each committed edit. Cheaper substitute that fails the checkpoint: a barcode field that needs hand-written JSON, or one that updates only after save.
**Chrome:** consume `FormBar`, `FieldPropertiesPane`, `FormCanvasOverlay`, `RectoColors.xaml` tokens and Segoe Fluent Icons.

**Build order:**

1. Add the tool to `FormBar` and the placement default (`120 x 120 pt`) to `FormCanvasOverlay`, creating a `Tx` field with a default QR spec. Done when: `BarcodeFieldUi.Placement` places a barcode field and finds `TextField`-style unique naming `BarcodeField_1`.
2. Add the `Barcode` inspector section and the live refresh in `Fill Form`. Done when: `BarcodeFieldUi.InspectorAndFill` sets sources, fills a source field and reads the updated code.

- [ ] Add `Add Barcode Field` (automation name `Add barcode field`, tooltip `Barcode field`) to the `§9` designer tool group and the `Ctrl+K` command `Add Barcode Field`. Done when: `BarcodeFieldUi.Placement` finds the button, the command and the 8-handle frame on the placed field.
- [ ] Add the `Barcode` section to `FieldPropertiesPane`: type, error correction, source-field checklist from the document's fields, template text box with `{field:Name}` insertion chips, delimiter and a live preview with the `§34` size and unknown-field messages. Done when: `BarcodeFieldUi.InspectorAndFill` edits each control and each edit is undoable. Cheaper substitute: a single text box that expects raw template syntax.
- [ ] Refresh barcode appearances on the canvas when a source field commits an edit (on focus loss or `Enter`, not per keystroke) and announce `Barcode updated` politely once per commit. Done when: `BarcodeFieldUi.InspectorAndFill` sees one refresh per commit and one announcement.
- [ ] Expose the field to UI Automation with the `/TU` or `/T` name, the type and the encoded text as its value. Done when: `A11y.BarcodeField` reads the three properties.
- [ ] Add `BarcodeFieldUi.*` and `A11y.BarcodeField` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=BarcodeFieldUi.*:A11y.BarcodeField` passes on the display-session host.
- [ ] Commit: `feat(app): add the Add Barcode Field tool, properties and live refresh`

**Test checkpoint:** `recto-tests.exe --gtest_filter=BarcodeFieldUi.*:A11y.BarcodeField` passes on the display-session host, quoting the decoded payload before and after a source edit.

## 36. Flat-form field detection engine and `recto-cli forms detect`

Add automatic field detection to `recto-pdf` and `recto-services`: from the vector lines, boxes and text of a flat page (or the ruling lines of a scanned page), propose form fields with a kind, rectangle, label-derived name and confidence. It matches the automatic form field recognition of UPDF, PDFelement and Foxit PDF Editor, and Acrobat's `Prepare Form` auto-detect. It only proposes: nothing is written until the user accepts (`§37`) or passes `--apply` to the CLI, which writes a copy. It reads the immutable `DisplayList` and `PageText` snapshots off the UI thread (never the live document), bounds its work on hostile vector pages, and keeps Recto's rule that originals are never modified in place.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `detect_form_fields(const DetectInput&, DetectOptions, CancelToken)` in `src/services/field_detect.h/.cpp`, where `DetectInput` holds the `DisplayList`, the `PageText`, the existing widget rectangles of the page, the set of existing fully qualified field names in the document, and optional extra rule-line segments from the scanned-page feed, and returning `FieldProposal` rows (`kind` among `Text`, `MultilineText`, `CheckBox`, `RadioButton`, `Comb`, `TableCell`; `rect_pt`; `label`; `suggested_name` unique against the input name set and earlier proposals; `confidence` `0..1`; `max_len` for `Comb`; `radio_group` and `export_value` for `RadioButton`). Done when: `PdfFieldDetect.FixtureRecall` finds at least 90% of the 24 known fields of `text/flat-form.pdf` with at most 10% extra proposals, and `PdfFieldDetect.AcceptSaveFill` creates every proposal through `create_form_field`, saves, re-opens, fills the comb, the radio group and a text field and reads the values back.
2. Add the scanned-page feed, name derivation, bounded work and `recto-cli forms detect`. Done when: `PdfFieldDetect.ScannedFeed`, `PdfFieldDetect.Caps` and `Cli.FormsDetect` pass under ASan.

- [ ] Detect underline fields: a horizontal stroke or run of underscore or dot glyphs at least `24 pt` long and at most `2 pt` thick, with no text glyph in the `12 pt` band above it, proposing a text field `18 pt` high above the line. Done when: `PdfFieldDetect.Underlines` finds underline fields and ignores an underlined heading.
- [ ] Detect box fields: empty closed rectangles at least `24 x 12 pt` (multiline when `40 pt` or taller) and empty squares of `6` to `20 pt` with sides within 10% as check boxes, and four-arc circles of the same size as radio buttons. Done when: `PdfFieldDetect.BoxesAndChecks` classifies each kind on the fixture.
- [ ] Detect comb fields: `4` or more adjacent equal boxes with gaps of at most `2 pt` become one `Comb` proposal with `MaxLen` set to the box count. Done when: `PdfFieldDetect.Comb` returns one proposal with `max_len = 9` for a nine-cell row, and `PdfFieldDetect.RadioGroups` returns radio buttons sharing one `radio_group` with distinct `export_value`s for a row of three circles.
- [ ] Detect table cells: build the grid from long horizontal and vertical lines and propose one text field per empty cell, skipping cells that hold text, named `Table1_R2C3`. Done when: `PdfFieldDetect.TableCells` proposes only the empty cells of a 4 x 5 ruled table with a text header row.
- [ ] Derive `suggested_name` from the nearest label to the left or above (within `36 pt`), reduce it to letters, digits and underscore, make it unique within the document and existing fields, and fall back to `TextField_N`. Done when: `PdfFieldDetect.Names` yields `Date_of_birth` and `Date_of_birth_2` for two equal labels.
- [ ] Skip proposals that overlap an existing widget by more than `30%`, score confidence from line length, text clearance and label presence (rubric documented in the header comment), and feed `OcrDetectedRuleLine` segments from `remove_ruling_lines_for_ocr` (`D03 T01 §7`) into the same detector for scanned pages. Done when: `PdfFieldDetect.ScannedFeed` finds the lines of a scanned form fixture and `PdfFieldDetect.ExistingWidgets` skips a field that exists.
- [ ] Bound the work: at most `200000` segments, `256` segments per grid cell and `5000` proposals per page, `truncated` when a cap is hit, cooperative cancellation through `CancelToken`. Done when: `PdfFieldDetect.Caps` finishes a `300000`-segment page truncated, and aborts inside `20 ms` of cancel under ASan.
- [ ] Add `recto-cli forms detect <file.pdf> [--pages 1-3] [--min-confidence 0.6] [--json <report.json>] [--apply --out <out.pdf>]`, where `--apply` creates fields through `create_form_field` (`§6`) on a copy written temp-file-then-replace. Done when: `Cli.FormsDetect` covers the report, `--apply` and refusal to overwrite the input.
- [ ] Add `build_flat_form` to `scripts/build-corpus.py` writing `text/flat-form.pdf` (2 pages, 24 known fields: underlines, boxes, check boxes, radios, one comb row, one 4 x 5 table, one underlined heading and one prose block as negatives) with its `PROVENANCE.md` row, and add `PdfFieldDetect.*` tests in `tests/recto-tests/pdf_field_detect_tests.cpp`. Done when: `python scripts/build-corpus.py --check` passes and `recto-tests.exe --gtest_filter=PdfFieldDetect.*:Cli.FormsDetect` passes under ASan.
- [ ] Commit: `feat(services): add flat-form field detection and forms detect`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFieldDetect.*:Cli.FormsDetect` passes under ASan, quoting recall and extra-proposal counts on `text/flat-form.pdf`.

## 37. `Detect Fields` in `Design Form` mode: review proposals and accept

Add the designer command that runs `§36` on a flat page and shows the proposals on the canvas for the user to accept. It matches the auto-detect step in UPDF, PDFelement, Foxit PDF Editor and Acrobat `Prepare Form`. It must not break `§9`, never creates a field without the user's accept, and keeps Recto's rules that detection runs as a cancellable background job, accepting is one undo entry, and every control has an automation name and tooltip.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Form Fill & Form Designer Bar`, Designer Mode) and 11; genuinely new overlay that follows the alignment-guide and tab-badge chrome of `§9`.
**Job:** A user opens a scanned or typed form with no fields, runs `Detect Fields`, unticks wrong proposals, renames a few, and accepts to get a working fillable form. Consumer: `FormBar` (`§9`), `DocumentActor` form calls (`§6`), the `§4` undo stack.
**Treatment:** A `Detect Fields` button in the designer bar, dashed ghost boxes in `RectoAccentBrush` with a checkbox and kind icon each, a side list with inline name editing, a `Confidence` slider and counts, and `Accept 18 fields` and `Cancel` buttons. Cheaper substitute that fails the checkpoint: creating every field immediately with no review, or a result list with no on-canvas preview.
**Chrome:** consume `FormBar`, `FormCanvasOverlay`, `RectoColors.xaml` tokens, `AnnouncementService` (`D03 T02 §5`) and Segoe Fluent Icons.

**Build order:**

1. Implement `DetectFieldsViewModel` in `src/services/detect_fields_view_model.h/.cpp` running `§36` as a job with progress and cancel, holding the proposals, selection, names and the confidence threshold. Done when: `DetectUi.ViewModel` runs on `text/flat-form.pdf`, filters by threshold and renames a proposal.
2. Build the ghost overlay, side list, buttons and commands. Done when: `DetectUi.AcceptAndUndo` accepts proposals, finds fillable fields in `Fill Form` mode and one `Ctrl+Z` removes them all.

- [ ] Add `Detect Fields` (automation name `Detect fields`, tooltip `Detect fields on this page or document`) with a page scope menu to the designer tool group and register `Detect Form Fields` in `CommandRegistry`. Done when: `DetectUi.Accessibility` finds the button and command.
- [ ] Draw ghost boxes on `FormCanvasOverlay` with a kind glyph and a checkbox, dimming those under the threshold, and keep them anchored through zoom and scroll. Done when: `DetectUi.OverlayAnchoring` keeps a ghost box within `1 epx` of its page rectangle after a zoom change.
- [ ] Build the review list with inline name editing (validated against existing field names), `Select all`, `Select none`, kind change per row and a live count, announcing `18 fields found` politely once. Done when: `DetectUi.Review` edits a name and rejects a duplicate with a message.
- [ ] Accept creates the ticked fields through `create_form_field` as one `§4` undo entry, opens the first one in the `§10` inspector and leaves `Design Form` mode active. Done when: `DetectUi.AcceptAndUndo` passes. Cheaper substitute: one undo entry per field.
- [ ] Show a clear message when no fields are found (`No fields found on this page. Add fields with the tools above.`) and when the page was truncated by the caps. Done when: `DetectUi.EmptyAndTruncated` shows both messages.
- [ ] Add `DetectUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=DetectUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add Detect Fields with on-canvas review in Design Form mode`

**Test checkpoint:** `recto-tests.exe --gtest_filter=DetectUi.*` passes on the display-session host, quoting the accepted count and the single undo.

## 38. Image field engine: a button field whose appearance is a user-chosen image

Add the image field to `recto-pdf`: a pushbutton form field whose icon is a picture the user picks, for photos, logos and stamps on a form. It matches the image field of UPDF, PDFelement and Foxit PDF Editor. Acrobat does it with JavaScript `importIcon`; Recto sets the icon natively with no script (D9), using the standard `/MK /I` icon and `/IF` fit dictionary (ISO 32000-1 section 12.7.4.2). Other viewers show the picture but cannot replace it, and the section records that. It reuses the image-to-Form-XObject builder of `§20`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `create_image_field`, `set_field_image(field, ImageSource, FitOptions)` and `clear_field_image` in `src/pdf/forms_image.h/.cpp`, writing a `Btn` pushbutton with `/MK << /TP 1 /I <Form XObject> /IF << /SW /A /S /P /A [0.5 0.5] >> >>` (the `/IF` icon fit dictionary nested inside `/MK`, as ISO 32000-1 Table 189 defines) and a private `Recto`-prefixed marker so the viewer offers `Choose image`. Done when: `PdfImageField.SetAndRoundTrip` sets a PNG, saves, re-opens and renders the image inside the field rectangle, and `PdfImageField.MkNesting` parses the saved bytes and asserts `/IF` is a key of `/MK` and not of the widget.
2. Implement fit modes, limits and flattening. Done when: `PdfImageField.FitModes`, `PdfImageField.Limits` and `PdfImageField.Flatten` pass under ASan.

- [ ] Decode PNG and JPEG through WIC, keep a baseline or progressive RGB or grey JPEG as `/DCTDecode` and re-encode anything else (including CMYK) as `/FlateDecode` RGB, rejecting images over `8000 x 8000` pixels or `20 MB` with `ErrorCode::InvalidArgument`. Done when: `PdfImageField.Limits` rejects oversize input and `PdfImageField.Encoding` keeps a JPEG's bytes and re-encodes a CMYK one.
- [ ] Implement the fit options `Proportional` (`/S /P`), `Stretch` (`/S /A`), `Never scale` and an alignment `/A [x y]`, with `/SW /A`, `/SW /B`, `/SW /S` and `/SW /N` for when to scale. Done when: `PdfImageField.FitModes` checks the placed rectangle for a wide image in a square field under each mode.
- [ ] Reuse the `§20` image-to-Form-XObject builder (`/Matrix` and `/BBox` keeping aspect ratio) rather than a second one, and synthesize the widget `/AP /N` so flattening and non-form viewers show the image. Done when: `PdfImageField.Flatten` keeps SSIM `>= 0.99` before and after `flatten_forms`.
- [ ] Preserve, byte for byte, an existing pushbutton icon and any script action on it, never running the action, and mark a script-driven import button `inert_script`. Done when: `PdfImageField.ForeignButtonInert` opens a crafted fixture and saves it with the action bytes unchanged.
- [ ] Add `recto-cli forms set-image <file.pdf> --field <name> --image <path> [--fit proportional|stretch] --out <out.pdf>` in `src/cli/cmd_forms.cpp`. Done when: `Cli.FormsSetImage` sets and clears an image and exits `2` for an unknown field.
- [ ] Add `PdfImageField.*` tests in `tests/recto-tests/pdf_image_field_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfImageField.*:Cli.FormsSetImage` passes under ASan.
- [ ] Commit: `feat(pdf): add image form fields with native icon setting`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfImageField.*:Cli.FormsSetImage` passes under ASan, with the unchanged script bytes quoted.

## 39. Image field and date picker in `Design Form` and `Fill Form` modes

Add the two missing field conveniences to the form surfaces: an `Add Image Field` tool with a `Choose image` and `Clear image` action while filling, and a calendar picker on any text field with a date format, which writes the formatted value with no script. It matches the image field and date field of UPDF, PDFelement and Foxit PDF Editor. It must not break `§8` to `§10` and keeps Recto's rules that the picker uses the field's own format from `§7`, every control has an automation name and tooltip, and a field edit is one undo entry.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Form Fill & Form Designer Bar`) and 11; genuinely new controls that follow the `FormCanvasOverlay` controls of `§8` and the placement tools of `§9`.
**Job:** A form author adds a photo box, and a person filling the form clicks it to choose a picture, or opens a calendar on a date field and picks a day. Consumer: `FormCanvasOverlay` (`§8`), `FormBar` (`§9`), `FieldPropertiesPane` (`§10`), `§38` and the date formatter of `§7`.
**Treatment:** An `Add Image Field` designer tool with fit options in the inspector, a click or `Enter` on the field in fill mode opening `FileOpenPicker` for `.png`, `.jpg`, `.jpeg`, and a `CalendarDatePicker` flyout on date-formatted text fields that writes the value through the `§7` formatter. Cheaper substitute that fails the checkpoint: a date field that only validates typed text, or an image field that needs a script or a separate editor.
**Chrome:** consume `FormCanvasOverlay`, `FormBar`, `FieldPropertiesPane`, `RectoColors.xaml` tokens and Segoe Fluent Icons.

**Build order:**

1. Add the image field tool, inspector fit controls and fill-mode picker. Done when: `FormPickerUi.ImageField` places an image field, fills it from a PNG, clears it and undoes both.
2. Add the date calendar to date-formatted text fields. Done when: `FormPickerUi.DatePicker` picks a day and finds the `/V` string in the field's own format, and typed text that parses still opens on that date.

- [ ] Add `Add Image Field` (automation name `Add image field`, tooltip `Image field`) to the `§9` designer tool group with a default size of `120 x 120 pt` and the `Ctrl+K` command `Add Image Field`. Done when: `FormPickerUi.ImageField` finds the button, the command and a unique name `ImageField_1`.
- [ ] Add `Fit` and `Alignment` controls to `FieldPropertiesPane` for image fields calling `set_field_image` options. Done when: `FormPickerUi.ImageField` changes fit and re-renders the field. Cheaper substitute: fixed stretch.
- [ ] In `Fill Form`, open `FileOpenPicker` (initialized with the window handle) on click or `Enter` over an image field, show `Choose image` and `Clear image` in its context menu, show an inline error message for a rejected file, and record the change on the `Ctrl+Z` stack. Done when: `FormPickerUi.ImageField` rejects a `9000 x 9000` image with a message and accepts a PNG.
- [ ] Show a calendar button on a `Tx` field whose format is a date (`§7`), open a `CalendarDatePicker` flyout with `Today`, `Alt+Down` to open, first day of week from the Windows locale, and write the date with the field's format pattern. Done when: `FormPickerUi.DatePicker` writes `2026-10-14` for pattern `yyyy-mm-dd` and `14/10/2026` for `dd/mm/yyyy`.
- [ ] Parse the existing value with the field's pattern to preselect the date, leave unparsable text alone, and do not show the calendar on read-only fields. Done when: `FormPickerUi.DateEdgeCases` covers an empty value, an unparsable value and a read-only field.
- [ ] Expose both controls to UI Automation with the `/TU` or `/T` name and a value pattern. Done when: `A11y.FormPickers` reads name, value and the `Choose image` and `Open calendar` actions.
- [ ] Add `FormPickerUi.*` and `A11y.FormPickers` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=FormPickerUi.*:A11y.FormPickers` passes on the display-session host.
- [ ] Commit: `feat(app): add image fields and a date picker to the form modes`

**Test checkpoint:** `recto-tests.exe --gtest_filter=FormPickerUi.*:A11y.FormPickers` passes on the display-session host, quoting the written date strings.

## 40. Combine form data from many filled PDFs: engine, CSV and `recto-cli forms combine`

Add the combine engine to `recto-services`: read the field values of many filled forms and merge them into one table with a row per file, written as CSV or JSON. It matches `Merge data files into spreadsheet` in Acrobat and the form data combine in Foxit PDF Editor and PDFelement, and extends the single-form export of `§7`. Each file is opened by its own job on its own `recto::pdf::Document`, nothing in a file runs (D9), originals are never touched, and a bad file is reported without stopping the batch. The `.xlsx` output and dialog are `§41`; the Action Wizard step is owned by `D04 T07`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `combine_form_data(span<const path>, CombineOptions, CancelToken)` in `src/services/form_combine.h/.cpp` returning `CombinedFormData` (`columns`, `rows`, `skipped` with reasons). Done when: `PdfFormCombine.ThreeFiles` merges three filled variants of one form into three rows with the union of field names as columns.
2. Add CSV and JSON writers and `recto-cli forms combine`. Done when: `PdfFormCombine.CsvJson` matches golden files and `Cli.FormsCombine` covers the verb under ASan.

- [ ] Read values with the `§6` field tree and the `§7` XFA converter, name columns by fully qualified field name in first-seen order, keep a leading `Source file` column, flatten check boxes and radios to their export value or `Off`, join multi-select values with `; `, and skip `Sig` and push-button fields. Done when: `PdfFormCombine.FieldKinds` checks each field kind and an XFA fixture.
- [ ] Handle columns that differ between files (blank cells for missing fields) and a duplicate field name inside one file (suffix `_2`), with an option `ColumnMatch` of `Name` (default) or `Position` for forms with renamed fields. Done when: `PdfFormCombine.MismatchedForms` covers both modes.
- [ ] Skip files that are encrypted without a supplied password, corrupt, not PDFs or without fields, recording `path` and `reason` in `skipped`, and continue. Done when: `PdfFormCombine.SkippedFiles` skips four crafted files and merges the rest.
- [ ] Bound the work: at most `5000` files, `2000` columns and `500000` cells per run with `ErrorCode::LimitExceeded` beyond them, open at most `N = hardware_concurrency` files at once and release each document before the next, with cooperative cancel. Done when: `PdfFormCombine.Caps` rejects 5001 files and 2001 columns and `PdfFormCombine.Cancel` aborts a 1000-file run within `100 ms` under ASan.
- [ ] Write CSV through the shared `csv_escape_cell` of `§25` for every cell, including the header row built from untrusted field names, and JSON as an array of objects, through temp-file-then-replace; `--raw` disables only the leading-character prefix. Done when: `PdfFormCombine.CsvJson` matches goldens and `PdfFormCombine.HostileHeaders` combines a form with fields named `=cmd|' /C calc'!A0`, `@SUM(1)` and a name containing a tab, comma and quote, finding each header cell quoted and prefixed. Cheaper substitute: protecting values but not headers.
- [ ] Add `recto-cli forms combine <files|folder|glob>... --out <data.csv|data.json> [--recurse] [--sort name|path|none] [--match name|position] [--raw]`, exit code `2` for no input files and `3` when every file was skipped. Done when: `Cli.FormsCombine` covers both exit codes and a folder run.
- [ ] Add `build_filled_forms` to `scripts/build-corpus.py` writing `text/filled-forms/` (5 filled variants of `text/barcode-form.pdf`, one with a renamed field, one encrypted, one corrupt) with `PROVENANCE.md` rows, and add `PdfFormCombine.*` tests in `tests/recto-tests/pdf_form_combine_tests.cpp`. Done when: `python scripts/build-corpus.py --check` passes and `recto-tests.exe --gtest_filter=PdfFormCombine.*:Cli.FormsCombine` passes under ASan.
- [ ] Commit: `feat(services): combine form data from many PDFs into CSV and JSON`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFormCombine.*:Cli.FormsCombine` passes under ASan, with the skipped-file reasons quoted.

## 41. `Combine Form Data` dialog and `.xlsx` output

Add the viewer dialog for combining form data and the Excel output, reusing `XlsxWriter` (`D04 T05 §1`) so no second spreadsheet writer exists. It matches the same command in Acrobat, Foxit PDF Editor and PDFelement. It must not break the form bar (`§8`) and keeps Recto's rules that values are written as text cells (never formulas), the job runs on its own documents with progress and cancel, and the output is written temp-file-then-replace.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 9.9 (dialog chrome) and 11; genuinely new dialog that follows `D02 T01 §10` dialog styles.
**Job:** A person collects 200 filled response forms into one spreadsheet, sees the first rows before saving, and learns which files were skipped and why. Consumer: `FormBar` (`§8`), `CommandRegistry`, `§40`, `XlsxWriter`.
**Treatment:** A `CombineFormDataDialog` with an add files and add folder control (drag and drop too), an ordered file list, match mode, output format `CSV`, `Excel (.xlsx)` or `JSON`, a preview of the first `20` rows, a progress bar with `Cancel`, and a result summary listing skipped files. Cheaper substitute that fails the checkpoint: CSV only, or a run with no preview and no list of skipped files.
**Chrome:** consume `RectoDialogStyles.xaml`, `RectoColors.xaml` tokens, `FileOpenPicker`, `FolderPicker`, `FileSavePicker` and Segoe Fluent Icons.

**Build order:**

1. Add the `xlsx` writer to the `§40` output set using `XlsxWriter::write_single_table`, with every cell as a string cell. Done when: `PdfFormCombine.XlsxOutput` writes a workbook that re-opens with the `D04 T04 §1` reader and holds the same cells, with `=1+1` as text.
2. Build `CombineFormDataDialog`, its view model and the entry points. Done when: `CombineUi.RunAndSkipped` combines the `text/filled-forms/` set, shows the preview and lists the skipped files.

- [ ] Add the `.xlsx` output to `combine_form_data` writers, set the sheet name to `Form data`, freeze the header row and keep column order. Done when: `PdfFormCombine.XlsxOutput` passes under ASan.
- [ ] Implement `CombineFormDataViewModel` in `src/services/combine_form_data_view_model.h/.cpp` holding files, options, the preview rows and the job. Done when: `CombineUi.ViewModel` previews the first 20 rows without writing a file.
- [ ] Build the dialog (`src/app/dialogs/CombineFormDataDialog.xaml/.h/.cpp`) with drag and drop, file reordering, per-file removal and the result summary with a `Show in folder` button. Done when: `CombineUi.DialogAccessibility` passes the `D03 T02 §3` inventory.
- [ ] Add `Combine Form Data...` to the `FormBar` menu and register `Combine Form Data` in `CommandRegistry`. Done when: `CombineUi.DialogAccessibility` finds the command in the command box.
- [ ] Add `CombineUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=CombineUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add the Combine Form Data dialog and Excel output`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFormCombine.XlsxOutput:CombineUi.*` passes on the display-session host, quoting the preview rows and the skipped list.

## 42. Pressure-sensitive ink appearance engine

Add variable-width ink to `recto-pdf`: strokes that keep a pressure value per point and render with a width that follows it, for pen input. It matches the Windows Ink pressure pen of Foxit PDF Editor. The standard `/InkList` stays the centre line so every viewer can read the stroke; the pressures are stored under a `Recto`-prefixed private key, and the `/AP /N` is a filled outline so other viewers show the same width. A mouse stroke has no pressure and keeps the constant width of `§2`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Define `InkStroke` (`points`, optional `pressures` in `0..1`) and `PressureStyle` (`base_width_pt`, `min_ratio`, `max_ratio`, `gamma`), and implement `build_pressure_ap` producing a smooth filled outline with round caps. Done when: `PdfInkPressure.OutlineWidth` checks the outline width at points of pressure `0.2` and `0.9` against the formula.
2. Persist and read `pressures` beside `/InkList`, validate them, and fall back to constant width when absent or invalid. Done when: `PdfInkPressure.RoundTripAndFallback` round-trips a stroke and renders constant width after the pressures are removed by another tool.

- [ ] Define the width as `base * (min_ratio + (max_ratio - min_ratio) * pressure^gamma)` with defaults `min_ratio 0.3`, `max_ratio 1.6` and `gamma 1.0` (recorded defaults, changeable in `PressureStyle` at the cost of re-rendering existing strokes only when edited). Done when: `PdfInkPressure.OutlineWidth` passes for the default and for `gamma = 1.5`.
- [ ] Build the outline from offset curves of the Catmull-Rom smoothed centre line with flat-cap-free round ends, merge overlapping per-segment quads with the non-zero winding rule, and emit one filled path per stroke into the `/AP /N` stream. Done when: `PdfInkPressure.NoGaps` finds no gap or notch larger than `0.1 pt` along a sharp-turn stroke.
- [ ] Store the pressures as one array per stroke under the `Recto`-prefixed key, require the same length as the stroke's points, clamp to `0..1`, reject non-finite values and cap an annotation at `50000` points. Done when: `PdfInkPressure.Validation` rejects mismatched, `NaN` and oversize arrays with `ErrorCode::InvalidArgument`.
- [ ] Keep the pressure array aligned when a stroke is simplified (Ramer-Douglas-Peucker, `§4`), split by the eraser or moved, interpolating at kept points. Done when: `PdfInkPressure.SimplifyAndErase` keeps the pressure of retained points and interpolates the cut ends.
- [ ] Verify cross-viewer parity: render the annotation in Recto and in the PDFium oracle of `D06 T01 §1`. Done when: `PdfInkPressure.OracleParity` reaches SSIM `>= 0.99`.
- [ ] Add `PdfInkPressure.*` tests in `tests/recto-tests/pdf_ink_pressure_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfInkPressure.*` passes under ASan.
- [ ] Commit: `feat(pdf): add pressure-sensitive ink appearance`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInkPressure.*` passes under ASan, quoting the two outline widths.

## 43. Pen input in the Ink tool: pressure, pen eraser and touch rejection

Wire pen hardware into the `Ink` tool: read pressure from `PointerPointProperties.Pressure` for pen pointers, switch to the eraser when the pen is turned over (`IsEraser`), and ignore touch contacts while a pen is in range. It matches the Windows Ink pen support in Foxit PDF Editor. It must not break mouse and touch ink of `§4`, and keeps Recto's rules that a setting is read by the code that uses it and the stroke is committed on pointer release.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Ink` freehand pen and eraser), 9.7 (settings) and 11; genuinely new behaviour with no new chrome beyond one setting row and one toggle on the existing `Colour, Opacity & Stroke Picker` flyout.
**Job:** A pen user writes notes with natural pressure variation, flips the pen to erase, and rests a palm on the screen without leaving marks. Consumer: `AnnotationInteraction` (`§4`), `§42`, `SettingsStore` (`D02 T04 §2`).
**Treatment:** A pointer sample type carrying device, pressure and eraser flags into `AnnotationInteraction`, a `Pressure` toggle on the stroke flyout (on by default for pens), a `Pen pressure sensitivity` setting (`Soft`, `Medium` default, `Firm`, mapped to `gamma 0.7`, `1.0`, `1.5`), and a `Reject touch while using a pen` setting (on by default). Cheaper substitute that fails the checkpoint: treating a pen as a mouse, or a setting that nothing reads.
**Chrome:** consume the `§3` stroke flyout, the Settings Reading group (`D02 T04 §5`) and `RectoColors.xaml` tokens.

**Build order:**

1. Add `PointerSample` (`device`, `pressure`, `is_eraser`, `time_ms`) to `src/services/annotation_interaction.h`, fed from `PointerRoutedEventArgs` in `DocumentView` (`PointerDeviceType` `Pen`, `Touch`, `Mouse`), and record pressures for pen strokes only. Done when: `PenUi.Interaction` feeds synthetic pen samples and finds a stroke with pressures and a mouse stroke without.
2. Add the two settings, the flyout toggle, eraser switching and touch rejection. Done when: `PenUi.SettingsAndRejection` shows both settings change the result and touch input is ignored within `2 s` of a pen in range.

- [ ] Treat a pressure of exactly `0.5` from a non-pen device as no pressure information, and clamp pen pressure to `0..1`. Done when: `PenUi.Interaction` covers mouse, touch and pen samples.
- [ ] Add `InkPressureSensitivity` (`Soft`, `Medium`, `Firm`) and `RejectTouchWithPen` (default `true`) to `SettingsStore` and the Settings Reading group, and read them in the interaction layer. Done when: `PenUi.SettingsAndRejection` verifies both are persisted and read, and no setting is left unread.
- [ ] Add a `Pressure` toggle to the `§3` stroke flyout with automation name `Use pen pressure` and tooltip `Vary stroke width with pen pressure`. Done when: `PenUi.FlyoutAccessibility` passes the `D03 T02 §3` inventory.
- [ ] Switch to the `Eraser` for the duration of a stroke when `IsEraser` is true and restore the previous tool on release. Done when: `PenUi.EraserEnd` erases with a flipped-pen sample and returns to `Ink`.
- [ ] Ignore touch pointers for ink creation while a pen was seen in range within the last `2 s`, leaving pan and zoom touch gestures to `D02 T02`. Done when: `PenUi.SettingsAndRejection` ignores touch ink after a pen sample and accepts it after `3 s`.
- [ ] Add `PenUi.*` tests in `tests/recto-tests/` using synthetic `PointerSample` sequences. Done when: `recto-tests.exe --gtest_filter=PenUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add pen pressure, pen eraser and touch rejection to the Ink tool`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PenUi.*` passes on the display-session host, quoting the stored pressures of a pen stroke.

## 44. Touch mode: larger targets for the annotation bar, handles and popovers

Add a touch mode that enlarges the markup surfaces for finger use: bar buttons, selection handles, popover controls and list rows reach at least `44 epx`. It matches the touch mode of Foxit PDF Editor. It must not change mouse layouts when off, keeps Recto's rules that sizes come from one resource dictionary (no per-control hard-coded numbers) and every setting is read, and leaves touch scrolling and pinch zoom to `D02 T02`.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2, 8.3 and 11 (`32 epx` hit targets); genuinely new mode with no new chrome, only a second metrics set applied to existing controls.
**Job:** A tablet user taps the annotation bar, drags a handle and edits a comment without mis-taps. Consumer: `MarkupAnnotationBar` (`§3`), the `§4` handles and popover, the `§5` cards, `SettingsStore`.
**Treatment:** A `TouchMode` setting (`Auto` default, `On`, `Off`) where `Auto` switches on after a touch pointer event and off after a mouse or pen event, and a `RectoTouchMetrics` resource dictionary supplying `44 epx` button and row sizes, `16 epx` handle visuals with `44 epx` hit areas, and wider popover spacing. Cheaper substitute that fails the checkpoint: scaling the whole window, or a setting that only changes one bar.
**Chrome:** consume `src/app/controls/RectoTouchMetrics.xaml` (new), `RectoColors.xaml` tokens and the Settings Reading group (`D02 T04 §5`).

**Build order:**

1. Add `TouchModeController` in `src/services/touch_mode.h/.cpp` resolving the effective mode from the setting and the last pointer device. Done when: `TouchUi.ModeResolution` covers `Auto` after touch, mouse and pen events, `On` and `Off`.
2. Add `RectoTouchMetrics.xaml`, apply it to the `§3` bar, `§4` handles and popover and `§5` rows. Done when: `TouchUi.Targets` measures every target at `>= 44 epx` in touch mode and unchanged in mouse mode.

- [ ] Add `TouchMode` (`Auto`, `On`, `Off`; default `Auto`) to `SettingsStore` and the Settings Reading group. Done when: `TouchUi.ModeResolution` verifies persistence and that the controller reads it.
- [ ] Create `src/app/controls/RectoTouchMetrics.xaml` with named size keys and bind the `§3`, `§4` and `§5` controls to those keys through `ThemeResource` or a merged dictionary swap, adding no hex or literal sizes outside it. Done when: `TouchUi.NoLiteralSizes` finds no touch-sized literal in the three XAML files. Cheaper substitute: per-control `Width` overrides.
- [ ] Enlarge the `§4` handle hit area to `44 epx` while keeping the `8 x 8 epx` visual for mouse and `16 epx` for touch, and add long-press as the context-menu gesture on annotations. Done when: `TouchUi.Handles` hits a handle from `20 epx` away in touch mode and not in mouse mode.
- [ ] Re-flow the `§3` bar into a wrapping two-row layout when the touch targets do not fit the window width. Done when: `TouchUi.BarWraps` shows all tools reachable at a `800 epx` width.
- [ ] Add `TouchUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=TouchUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add touch mode with larger annotation targets`

**Test checkpoint:** `recto-tests.exe --gtest_filter=TouchUi.*` passes on the display-session host, quoting the smallest measured target in each mode.

## 45. Cloud border effect (`/BE`) for Square, Circle, Polygon and FreeText

Add the cloudy border to the appearance engine: read and write the `/BE` border effect dictionary (`/S /C`, intensity `/I` `0`, `1` or `2`, with `/RD` insets, ISO 32000-1 section 12.5.4) and draw the scalloped outline in the `/AP /N`. It matches the cloud markup of Foxit PDF Editor and PDFelement (Acrobat has the cloud tool). `§2` draws plain borders only, so a cloud made in another product loses its shape on the next save. Third-party viewers show the same cloud because the shape is baked into the `/AP`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Parse and preserve `/BE` and `/RD` on `Square`, `Circle`, `Polygon` and `FreeText` in `src/pdf/annotations.cpp`. Done when: `PdfCloudBorder.ParseAndPreserve` round-trips intensities `0`, `1` and `2` and a foreign `/RD`.
2. Implement `synthesize_cloud_outline` in `src/pdf/annotation_ap.cpp` for rectangles, ellipses and polygons. Done when: `PdfCloudBorder.Outline` closes every scallop path and `PdfCloudBorder.OracleParity` matches the PDFium oracle with SSIM `>= 0.99`.

- [ ] Define the scallop arc length as `4 pt * (1 + intensity)` of border-line distance (recorded defaults for intensity `0`, `1`, `2` giving `4`, `8` and `12 pt`), distributing arcs evenly along the perimeter so the last arc meets the first, with each arc a cubic Bezier bulging outward by `0.55` of its chord. Done when: `PdfCloudBorder.Outline` checks the arc count and closure for a rectangle, an ellipse and a concave polygon.
- [ ] Orient arcs outward for either vertex winding of a polygon and keep the cloud inside the inset `/RD` rectangle so the `/Rect` encloses the bulges. Done when: `PdfCloudBorder.Winding` renders the same outward cloud for clockwise and counter-clockwise polygons and the `/Rect` encloses all arcs.
- [ ] Apply `/BS` width, dash is ignored for clouds, `/C` colour, `/IC` fill and `/CA` opacity as for plain shapes. Done when: `PdfCloudBorder.Style` checks stroke width, fill and opacity in the content stream.
- [ ] Bound the arc count at `2000` per annotation by raising the arc length for very large shapes and report `ErrorCode::InvalidArgument` for a degenerate (zero-area or non-finite) shape. Done when: `PdfCloudBorder.Caps` covers a `10000 pt` rectangle and a zero-area polygon under ASan.
- [ ] Add `PdfCloudBorder.*` tests in `tests/recto-tests/pdf_cloud_border_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfCloudBorder.*` passes under ASan.
- [ ] Commit: `feat(pdf): add the cloud border effect to shape annotations`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCloudBorder.*` passes under ASan, with the PDFium oracle parity quoted.

## 46. Arc annotation engine

Add arcs to the annotation model: an open curve defined by centre, radii, start angle and sweep, drawn with a smooth `/AP` and stored as an `Ink` annotation so every viewer shows it. It matches the arc markup of Foxit PDF Editor and PDFelement (Acrobat has no arc tool). PDF has no arc annotation subtype, so the standard `/InkList` holds a flattened sample of the arc and a `Recto`-prefixed dictionary holds the exact parameters for editing; a stroke whose points no longer match the parameters (edited by another tool) becomes plain ink.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `make_arc_annotation(ArcSpec, style)` and `arc_from_annotation` in `src/pdf/annotation_arc.h/.cpp`, with `ArcSpec` (`center`, `rx`, `ry`, `rotation_deg`, `start_deg`, `sweep_deg`). Done when: `PdfArc.RoundTrip` writes, saves, re-opens and reads back the spec.
2. Synthesize the `/AP /N` with Bezier arcs of at most `90` degrees and detect foreign edits. Done when: `PdfArc.AppearanceAccuracy` keeps the Bezier within `0.05 pt` of the true ellipse and `PdfArc.ForeignEdit` demotes a moved-point stroke.

- [ ] Sample the `/InkList` polyline so the chord error is at most `0.05 pt` (steps from `acos(1 - tolerance / radius)`), with at least `8` points and at most `720` (a full circle of `5000 pt` needs `717` points, so the radius limit below keeps the bound reachable). Done when: `PdfArc.Sampling` checks the error bound for radii `5`, `50`, `500` and `5000 pt` and a 360 degree sweep.
- [ ] Use cubic Bezier pieces with control distance `4/3 * tan(theta/4)` for the `/AP`, starting at `90` degrees and halving a piece until its measured maximum distance from the true curve is at most `0.05 pt` (so a `5000 pt` radius gets many pieces), handle elliptical radii and rotation by one transform, and draw optional end styles (`None`, `OpenArrow`, `ClosedArrow`) reusing the `/LE` shapes of `§2`. Done when: `PdfArc.AppearanceAccuracy` passes for a circle arc, a rotated ellipse arc, an arrowed end and a `5000 pt` radius.
- [ ] Reject non-finite values, radii of zero or over `5000 pt` (the largest radius whose full circle meets the `0.05 pt` bound within the `720` point cap, recorded with the derivation in the header comment), and a sweep of zero or over `360`, with `ErrorCode::InvalidArgument`. Done when: `PdfArc.Validation` covers each rejection, including radius `5000.01`.
- [ ] Recompute the sample on read and compare with the stored `/InkList` within `0.1 pt`; when it differs, drop the private dictionary from the model and treat the annotation as plain `Ink`. Done when: `PdfArc.ForeignEdit` shows the demotion and that the points are kept.
- [ ] Add `PdfArc.*` tests in `tests/recto-tests/pdf_arc_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfArc.*` passes under ASan.
- [ ] Commit: `feat(pdf): add arc annotations stored as ink with editable parameters`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfArc.*` passes under ASan, quoting the maximum measured Bezier error.

## 47. Group and ungroup markups engine (`/RT /Group`)

Add annotation groups to `recto-pdf`: several annotations on one page that move, copy and delete together, using the standard group relationship (`/IRT` naming the primary annotation with `/RT /Group`, ISO 32000-1 section 12.5.6.2). It matches the group and ungroup markup commands of Foxit PDF Editor and PDFelement (Acrobat groups markups the same way). The primary annotation's text and properties apply to the group, so viewers that understand groups show one comment.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `group_annotations(page, nm_list)`, `ungroup(nm)` and `group_of(nm)` in `src/pdf/annotations.cpp`, with the first annotation as primary and members pointing at it. Done when: `PdfAnnotGroup.GroupUngroup` groups three annotations, saves, re-opens and finds the group, then ungroups with no `/IRT` left on the members.
2. Make move, resize, delete and copy act on whole groups. Done when: `PdfAnnotGroup.GroupOperations` moves a group by a vector, deletes it with all members and copies it with fresh `/NM` values.

- [ ] Validate groups: members on one page, at least `2` and at most `200` members, no nesting, no state annotation as primary, with `ErrorCode::InvalidArgument` otherwise. Done when: `PdfAnnotGroup.Validation` covers each rule.
- [ ] Keep `/RT /R` replies and `/RT /Group` members distinct when both point at one annotation, and keep replies attached to the primary when ungrouping. Done when: `PdfAnnotGroup.RepliesAndGroups` ungroups a primary with two replies and keeps both.
- [ ] Bound the group walk at depth `2` with a visited set so a crafted cycle ends in `ErrorCode::CorruptStructure`. Done when: `PdfAnnotGroup.Hostile` handles a cyclic group file under ASan with no hang.
- [ ] Transform a group through one subtype-aware `transform_annotation(annotation, Matrix2D)` (translation and uniform or non-uniform scale; rotation only for annotations that support it) that rewrites `/Rect`, `/QuadPoints`, `/Vertices`, `/InkList`, `/L` and `/RD` and also the private geometry that a mismatch check depends on: the `ArcSpec` of `§46` (centre, radii, rotation), the `§42` pressure array (kept unchanged) and the `§33` and `§48` private dictionaries, then re-synthesizes `/AP`, all as one `DocumentActor` operation. Done when: `PdfAnnotGroup.GroupOperations` checks the geometry of every member after a move, and `PdfAnnotGroup.ArcSurvivesTransform` groups an arc with a rectangle, moves and scales the group, saves, re-opens and finds the arc still editable (not demoted to plain ink) with the expected `ArcSpec`.
- [ ] Add `PdfAnnotGroup.*` tests in `tests/recto-tests/pdf_annot_group_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfAnnotGroup.*` passes under ASan.
- [ ] Commit: `feat(pdf): add annotation groups`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfAnnotGroup.*` passes under ASan, with the hostile-cycle case ending in a typed error.

## 48. Circle measurement engine: radius, diameter, circumference and area

Extend the measurement engine (`§17`) with circles: a circle drawn by centre and radius reports radius, diameter, circumference and area in the page scale's real units, matching the circle measurement of Foxit PDF Editor and PDFelement (Acrobat measures distance, perimeter and area only). The measurement is an ordinary `Circle` annotation whose `/Contents` holds the formatted values, so other viewers show them; the scale reference is a `Recto`-prefixed private key because PDF defines no circle measure type. It reuses `MeasureScale` validation, the `UserUnit` and `/Rotate` handling and the stale check of `§17`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `measure_circle(center_pt, radius_pt, MeasureScale, page)` in `src/pdf/measure.cpp` returning a `CircleMeasurement` and formatted text. Done when: `PdfMeasureCircle.Values` matches hand-computed values for a `1 in = 10 ft` scale and a `1 cm = 2.5 m` scale.
2. Write, read and export circle measurements. Done when: `PdfMeasureCircle.AnnotationAndExport` round-trips one, flags a stale one and matches the golden export.

- [ ] Compute `diameter = 2r`, `circumference = 2*pi*r` and `area = pi*r^2` with the area scaled by the square of the length ratio, and apply `UserUnit` and the number format of the scale. Done when: `PdfMeasureCircle.Values` passes to the scale's decimals.
- [ ] Write a `Circle` annotation with the private measure key naming the scale, `/Contents` text such as `r 2.5 m, d 5 m, C 15.71 m, A 19.63 m2` and an `/AP` that also draws the radius line. Done when: `PdfMeasureCircle.AnnotationAndExport` re-opens the file and recomputes the same text.
- [ ] Require equal width and height within `0.5%` for a measured circle and report an ellipse as not measurable instead of guessing. Done when: `PdfMeasureCircle.EllipseRefused` returns `ErrorCode::InvalidArgument` for a `2:1` ellipse.
- [ ] Add circle rows to `export_measurements` (kind `circle`, value the radius, label the four values) and `recto-cli annot measure`, updating the golden files. Done when: `Cli.AnnotMeasure` exports the new row to CSV and JSON.
- [ ] Add `PdfMeasureCircle.*` tests in `tests/recto-tests/pdf_measure_circle_tests.cpp` and a circle to `text/measure-plan.pdf` through `build_measure_plan`. Done when: `python scripts/build-corpus.py --check` passes and `recto-tests.exe --gtest_filter=PdfMeasureCircle.*:Cli.AnnotMeasure` passes under ASan.
- [ ] Commit: `feat(pdf): add circle measurement`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfMeasureCircle.*:Cli.AnnotMeasure` passes under ASan, with the formatted circle text quoted.

## 49. Multi-select and `Group` and `Ungroup` commands for markups

Add multi-selection to the annotation canvas and the group commands built on `§47`. It matches the group and ungroup markup commands of Foxit PDF Editor and PDFelement. It must not break the single-selection handles of `§4`, shows one frame around a group and one card for it in the Comments panel (`§5`), and keeps Recto's rules that each command is reachable from `Ctrl+K` and is one undo entry.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (8-point selection handles), 8.3 and 11; genuinely new interaction that follows the `§4` selection frame chrome.
**Job:** A reviewer selects several markups with `Ctrl+click` or a drag rectangle, groups them, moves them as one and ungroups them later. Consumer: `AnnotationInteraction` (`§4`), `CommentsPanel` (`§5`), `CommandRegistry`.
**Treatment:** `Ctrl+click`, `Shift+click` and marquee selection in `Select` mode, a shared bounding frame with the `§4` handles, `Group` (`Ctrl+Shift+G`) and `Ungroup` (`Ctrl+Alt+G`) commands, and a Comments card showing the group as one entry with its member count. Cheaper substitute that fails the checkpoint: grouping that exists only in the file with no way to select several, or a group that moves only its primary.
**Chrome:** consume the `§4` frame and popover, the `§5` card styles, `RectoColors.xaml` tokens and Segoe Fluent Icons.

**Build order:**

1. Add multi-selection to `AnnotationInteraction` (`Ctrl+click` toggles, `Shift+click` adds, marquee selects fully enclosed annotations) with a combined frame, and align, move and delete on the set. Done when: `GroupUi.MultiSelect` selects three annotations three ways and moves them with one drag.
2. Add `Group` and `Ungroup` commands, group-aware selection and the Comments card. Done when: `GroupUi.GroupUngroup` groups, moves, ungroups and undoes each step.

- [ ] Implement marquee and modifier selection in `src/services/annotation_interaction.cpp`, with `Esc` clearing and `Ctrl+A` selecting all annotations on the page in `Select` mode. Done when: `GroupUi.MultiSelect` covers each gesture and `Ctrl+A`.
- [ ] Draw one dashed bounding frame with the `§4` handles for a multi-selection and a thin outline per member, resizing members proportionally when a handle is dragged. Done when: `GroupUi.Frame` resizes two shapes proportionally and one `Ctrl+Z` reverses it.
- [ ] Register `Group Markups`, `Ungroup Markups` and `Select All Markups` in `CommandRegistry` with `Ctrl+Shift+G`, `Ctrl+Alt+G` and `Ctrl+A` (in `Select` mode), enabling them only for valid selections. Done when: `GroupUi.GroupUngroup` finds the commands and their enabled state.
- [ ] Select a whole group when any member is clicked, show it as one `Comments` card with a `Group of N` note, and delete or move it as one. Done when: `GroupUi.CommentsCard` shows one card for a three-member group.
- [ ] Add `GroupUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=GroupUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add multi-select and group commands for markups`

**Test checkpoint:** `recto-tests.exe --gtest_filter=GroupUi.*` passes on the display-session host, quoting the member geometry after a group move.

## 50. `Cloud` and `Arc` tools on the Markup & Annotation Bar

Add the two shape tools built on `§45` and `§46` to the `Shapes` group of the bar. They match the cloud and arc tools of Foxit PDF Editor and PDFelement. They must not break the shape tools of `§4`, use the same style flyout and undo stack, and keep Recto's rules that every button has an automation name and tooltip and every tool is reachable from `Ctrl+K`.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Markup & Annotation Bar`, `Shapes`) and 11; genuinely new tools that follow the `Rectangle` and `Ellipse` tools of `§3` and `§4`.
**Job:** A reviewer clouds a region of a drawing by dragging a rectangle or clicking polygon corners, or draws an arc by choosing centre, start and end, and can change the cloud intensity afterwards. Consumer: `AnnotationToolViewModel` (`§3`), the `§4` canvas, `CommandRegistry`.
**Treatment:** `Cloud` (rectangle drag; `Shift` for polygon mode), `Cloud intensity` (`Light`, `Normal`, `Heavy` mapping to `/I` `0`, `1`, `2`) in the style flyout, and `Arc` (drag the chord, then a third click for the bulge, with live preview), with `8` handles plus a sweep handle. Cheaper substitute that fails the checkpoint: a cloud that is a fixed image, or an arc that cannot be edited after placement.
**Chrome:** consume the `§3` bar and style flyout, the `§4` frame and `RectoColors.xaml` tokens, and Segoe Fluent Icons.

**Build order:**

1. Add `Cloud` and `Arc` to `AnnotationToolMode`, creation gestures and previews. Done when: `MarkupShapeUi.Creation` creates a cloud rectangle, a cloud polygon and an arc.
2. Add intensity, arc sweep handles and the commands. Done when: `MarkupShapeUi.EditAndUndo` changes intensity, drags the sweep handle and undoes both.

- [ ] Add `Cloud` and `Arc` buttons (automation names `Cloud tool` and `Arc tool`) to the `Shapes` group of `MarkupAnnotationBar`. Done when: `MarkupShapeUi.Accessibility` passes the `D03 T02 §3` inventory.
- [ ] Implement the cloud gestures through `create_annotation` with `/BE`, with live scalloped preview and `Enter` to close polygon mode. Done when: `MarkupShapeUi.Creation` finds a `/BE` entry with the chosen intensity.
- [ ] Implement the arc gestures through `make_arc_annotation` with a live preview, and add a sweep handle and radius handles to the `§4` frame for arcs. Done when: `MarkupShapeUi.EditAndUndo` changes the sweep from `90` to `180` degrees and the `/AP` follows.
- [ ] Add the `Cloud intensity` control to the style flyout and popover. Done when: `MarkupShapeUi.EditAndUndo` changes intensity on a selected cloud and `Ctrl+Z` restores it.
- [ ] Register `Add Cloud`, `Add Arc` and `Cloud Intensity: Light`, `Normal`, `Heavy` in `CommandRegistry`. Done when: `MarkupShapeUi.Accessibility` finds the commands in the command box.
- [ ] Add `MarkupShapeUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=MarkupShapeUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add the Cloud and Arc tools`

**Test checkpoint:** `recto-tests.exe --gtest_filter=MarkupShapeUi.*` passes on the display-session host, quoting the `/BE` intensity and arc sweep read back from the saved file.

## 51. `Circle` tool in the `Measure` bar

Add the circle measuring tool to the `Measure` bar (`§19`) using `§48`. It matches the circle measurement of Foxit PDF Editor and PDFelement. It must not break the distance, perimeter and area tools of `§19`, snaps to the same candidates (`§18`), and keeps Recto's rules that the result is an ordinary annotation and the live readout never relies on colour alone.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 and 11; genuinely new tool that follows the `MeasureBar` of `§19`.
**Job:** A user clicks a centre (snapping to a corner or intersection), drags to a radius and reads radius, diameter, circumference and area in real units, then finds the circle in the `Measurements` list. Consumer: `MeasureBar` and `MeasurementsPanel` (`§19`), `§48`.
**Treatment:** A `Circle` button in the bar, a live pill reading `r 2.5 m, A 19.63 m2` while dragging, a radius line, and a row in the panel with all four values. Cheaper substitute that fails the checkpoint: drawing a plain circle annotation with no measurement.
**Chrome:** consume the `§19` bar, snap markers and pill, the `§4` frame and `RectoColors.xaml` tokens.

**Build order:**

1. Add the `Circle` mode to `MeasureViewModel` (centre click, radius drag, `Esc` cancel, `Enter` or release to finish). Done when: `MeasureCircleUi.Draw` measures a known circle on `text/measure-plan.pdf` and reads the stored annotation.
2. Add the pill, the panel row and the export. Done when: `MeasureCircleUi.PanelAndExport` shows the row and exports the golden CSV row.

- [ ] Add a `Circle` button (automation name `Measure circle`, tooltip `Circle`) to `MeasureBar` and the `Ctrl+K` command `Measure Circle`. Done when: `MeasureCircleUi.Accessibility` passes the `D03 T02 §3` inventory.
- [ ] Snap the centre with `§18` and show the radius line and pill during the drag, holding `Shift` to lock to a whole-unit radius. Done when: `MeasureCircleUi.Draw` snaps the centre to an intersection and `Shift` rounds the radius.
- [ ] Record the circle as one `§4` undo entry through `DocumentActor` (`§48` annotation) and list it in `MeasurementsPanel` with the `Scale changed` badge when stale. Done when: `MeasureCircleUi.PanelAndExport` undoes a circle in one step and shows the badge after a scale edit.
- [ ] Add `MeasureCircleUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=MeasureCircleUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add the Circle tool to the Measure bar`

**Test checkpoint:** `recto-tests.exe --gtest_filter=MeasureCircleUi.*` passes on the display-session host, quoting the pill text and the exported circle row.

## 52. Sticker packs: shipped sticker sets and import of user sticker folders

Extend the stamp library (`§20`) with stickers: image stamps grouped into named packs, five shipped packs of original vector artwork, and import of a folder of pictures as a pack. It matches the sticker libraries of UPDF and PDFelement. `§20` and `§21` already apply any image or single-page PDF as a static stamp, so stickers add grouping, search and shipped content, not a new annotation type. All shipped artwork is made for Recto and released under the repository licence, so no third-party licence is bundled, and a sticker applied to a page stays a static `Stamp` with its own `/AP`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Define `StickerPack` and `StickerEntry` (`id`, `name`, `tags`, `file`) with a `pack.json` format (`schema_version`, `id`, `name`, `author`, `license`, `stickers`), loaded and saved by `StickerLibrary` in `src/services/sticker_library.h/.cpp` under `AppPaths::data_dir()\stickers\<pack-id>\`. Done when: `PdfStickers.PackPersistence` adds, reloads and removes a pack and survives a corrupt `pack.json` by skipping that pack with a diagnostic.
2. Generate and ship the five packs and implement folder import and search. Done when: `PdfStickers.ShippedPacks` lists five read-only packs of the expected sizes and `PdfStickers.ImportFolder` imports a safe folder and rejects an unsafe one.

- [ ] Add `scripts/build-stickers.py` (stdlib Python, deterministic) generating single-page vector PDFs under `resources/stickers/<pack>/` for five packs, `reactions`, `arrows-and-marks`, `shapes-and-flags`, `office-and-stamps` and `stars-and-ratings`, `12` to `24` stickers each, with `pack.json` files naming the repository licence. Done when: `python scripts/build-stickers.py --check` regenerates the files byte for byte.
- [ ] Copy the shipped packs to the installer and Portable payload as read-only content and list them as `Built in`, refusing edits and deletes. Done when: `PdfStickers.ShippedPacks` verifies read-only status and that the packs exist beside the executable.
- [ ] Import a folder as a pack: accept `.pdf` (single page) and `.png` files, at most `200` per pack and `2 MB` each, reject path traversal, links and reparse points that leave the folder, unknown extensions and a `pack.json` with oversize or non-string fields. Done when: `PdfStickers.ImportFolder` imports five files, skips a `.exe` and a symlinked file and rejects a `3 MB` file.
- [ ] Confine every manifest-derived path: a pack `id` must match `[a-z0-9][a-z0-9-]{0,63}`, a sticker `file` must be a bare file name matching `[A-Za-z0-9._ -]{1,100}` with an allowed extension (no separators, drive letters, `..`, alternate data stream colons or reserved device names), and the loader, persistence, removal, export and apply paths each re-validate the id and file name from the manifest and check that the canonical resolved path stays under `AppPaths::data_dir()\stickers\` and is not a reparse point, so a modified on-disk `pack.json` cannot reach elsewhere. Done when: `PdfStickers.PathConfinement` loads, removes and exports packs whose manifest `id` or `file` is `..\x`, `C:\x`, `a/b`, `con.png` or `x.png:stream`, finds each refused with `ErrorCode::InvalidArgument` and finds no file touched outside the stickers folder.
- [ ] Search stickers by name and tag with case-insensitive substring matching and keep a library cap of `2000` stickers outside the `64` stamp limit of `§20`. Done when: `PdfStickers.SearchAndCaps` finds `arrow` across packs and rejects the 2001st sticker.
- [ ] Apply a sticker with the `§20` image-stamp builder, setting `/Contents` to the sticker name as alternative text and `/Name` to its id. Done when: `PdfStickers.AppliedIsStatic` deletes the pack and the applied sticker still renders.
- [ ] Add `PdfStickers.*` tests in `tests/recto-tests/pdf_stickers_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfStickers.*` passes under ASan.
- [ ] Commit: `feat(core): add sticker packs, shipped sets and folder import`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfStickers.*` passes under ASan, and `python scripts/build-stickers.py --check` exits 0.

## 53. `Stickers` tab in the Stamp Picker, import and export of packs

Add the stickers surface to the stamp picker: a `Stickers` tab with a pack selector, search and a grid, a `Recent` row, and import and export of packs. It matches the sticker pickers of UPDF and PDFelement. It must not break the `§3` flyout and `§21` manager, uses the sticky stamp tool of `§21` so several stickers can be placed in a row, and keeps Recto's rules that every item has an automation name (the sticker name) and tooltip and each command is reachable from `Ctrl+K`.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Stamp` picker) and 11; genuinely new tab that follows the `Stamp Picker` flyout of `§3` and `§21`.
**Job:** A user opens `Stickers`, searches `arrow`, clicks a sticker and places it several times, or imports a folder of their own pictures as a pack. Consumer: `MarkupAnnotationBar` (`§3`), `AnnotationToolViewModel`, `StickerLibrary` (`§52`).
**Treatment:** A virtualized grid of previews with the name as tooltip, a pack combo, a search box, a `Recent` row of the last `12` placed (kept in `SettingsStore` as `RecentStickers` and read by the picker), `Import sticker folder...` and `Export pack...`. Cheaper substitute that fails the checkpoint: a flat unsearchable list, or stickers that must be imported before any exist.
**Chrome:** consume the `§3` flyout, `RectoDialogStyles.xaml`, `RectoColors.xaml` tokens and Segoe Fluent Icons.

**Build order:**

1. Implement `StickersViewModel` in `src/services/stickers_view_model.h/.cpp` (packs, filter, recent list, arming the stamp tool). Done when: `StickerUi.ViewModel` filters, records recents and arms the tool with a definition.
2. Build the tab, import and export and the commands. Done when: `StickerUi.TabAccessibility` finds every control with an automation name and tooltip.

- [ ] Build the `Stickers` tab in the `Stamp Picker` flyout with a pack combo, search box, grid and `Recent` row. Done when: `StickerUi.TabAccessibility` passes the `D03 T02 §3` inventory and the grid stays smooth with `2000` stickers.
- [ ] Add `Import sticker folder...` (`FolderPicker`) showing the `§52` rejection reasons inline, and `Export pack...` writing a folder (not an archive) through temp-file-then-replace per file. Done when: `StickerUi.ImportExport` imports a folder, exports it and re-imports equal content.
- [ ] Persist `RecentStickers` (at most `12` ids) in `SettingsStore` and read it in the view model. Done when: `StickerUi.ViewModel` verifies persistence and that nothing is left unread.
- [ ] Register `Show Stickers`, `Import Sticker Pack` and `Sticker: <name>` for recent items in `CommandRegistry`. Done when: `StickerUi.TabAccessibility` finds the commands in the command box.
- [ ] Add `StickerUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=StickerUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add the Stickers tab to the Stamp Picker`

**Test checkpoint:** `recto-tests.exe --gtest_filter=StickerUi.*` passes on the display-session host, quoting the search result count for `arrow`.

## 54. Accounting tickmark stamp set and calculator stamp tokens

Extend the stamp engine (`§20`) with an accounting set and calculator tokens. The set holds the audit tickmarks reviewers use (checked, double checked, crossed, footed, cross-footed, agreed to source, recalculated, traced to ledger, agreed to prior year, query), drawn as vector paths, and the tokens `{sum}`, `{count}`, `{average}`, `{min}` and `{max}` resolve over the numbers in the text the user has selected. It matches the accounting tickmarks and calculator stamps of Foxit PDF Editor. Tokens resolve once, in native code, at application (D9), and the placed stamp is static.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `parse_numbers(span<const std::u16string_view>, NumberLocale)` in `src/services/number_parse.h/.cpp` and the five calculator tokens in `resolve_stamp_tokens` (`src/pdf/stamps.cpp`) with a `StampContext::selection_numbers` input. Done when: `PdfAccounting.Tokens` resolves `{sum}`, `{count}`, `{average}`, `{min}` and `{max}` for a fixture selection.
2. Ship the vector tickmark set as read-only library entries. Done when: `PdfAccounting.TickmarkSet` lists the entries, each with a non-empty `/AP` and an alternative text.

- [ ] Parse numbers with the locale decimal and thousands separators (also `1,234.50` and `1.234,50` by the chosen locale), parentheses and a trailing or leading minus as negatives, currency symbols (`$`, `EUR`, `GBP`, `R`, and the locale's own), and percent signs, counting every skipped non-numeric string in `skipped`. Done when: `PdfAccounting.NumberParsing` covers each form and a skipped word.
- [ ] Format token results with `{sum}` using two decimals and `{sum:0}` to `{sum:6}` for other decimals (an integer `0..6` after the colon, nothing else), keeping unknown token forms literal and reported as in `§20`. Done when: `PdfAccounting.Tokens` checks `{sum:0}`, `{sum:4}` and an invalid `{sum:abc}`.
- [ ] Sum with exact decimal arithmetic (scaled `int128` for up to `6` decimals) rather than binary floating point, reject more than `10000` numbers or a magnitude over `1e15` with `ErrorCode::InvalidArgument`. Done when: `PdfAccounting.ExactSum` sums `0.1` ten thousand times to exactly `1000.00`.
- [ ] Draw the ten tickmarks and the three calculator stamp shells (`Sum`, `Average`, `Count`) as vector paths with a letter drawn as outlined paths (no font glyph), and register them as read-only entries beside the standard six of `§20`. Done when: `PdfAccounting.TickmarkSet` renders each entry identically in Recto and the PDFium oracle with SSIM `>= 0.99`.
- [ ] Refuse to place a calculator stamp with no numeric selection by returning `ErrorCode::InvalidArgument` and a message `Select numbers first`, never placing an unresolved token. Done when: `PdfAccounting.EmptySelection` returns the error and writes no annotation.
- [ ] Add `PdfAccounting.*` tests in `tests/recto-tests/pdf_accounting_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfAccounting.*` passes under ASan.
- [ ] Commit: `feat(core): add accounting tickmark stamps and calculator tokens`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfAccounting.*` passes under ASan, quoting the exact-sum result.

## 55. Accounting set in the Stamp Picker and the `Sum of selection` stamp

Add the accounting surface to the picker: an `Accounting` section with the tickmarks, and calculator stamps armed from the current text selection with a preview of the total. It matches the accounting tickmark and calculator stamps of Foxit PDF Editor. It must not break the `§3` flyout or `§21` manager, and keeps Recto's rules that the preview equals the placed text and every control has an automation name and tooltip.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Stamp` picker) and 11; genuinely new section that follows the `Stamp Picker` flyout of `§3` and the dynamic stamp preview of `§21`.
**Job:** An auditor selects a column of figures, picks `Sum`, sees `Sum 1,234.50 (12 numbers, 1 skipped)` in the preview, clicks to place it, then places tickmarks beside the rows. Consumer: `AnnotationToolViewModel` (`§3`), `SelectionModel` (`D03 T01 §3`), `StampLibrary` (`§20`).
**Treatment:** An `Accounting` section in the picker, calculator entries enabled only with a text selection, a live preview with count and skipped count, and sticky placement for tickmarks through `keep_tool_selected` (`§21`). Cheaper substitute that fails the checkpoint: a total typed by hand, or a preview that differs from the placed value.
**Chrome:** consume the `§3` flyout, `RectoColors.xaml` tokens, `AnnouncementService` (`D03 T02 §5`) and Segoe Fluent Icons.

**Build order:**

1. Feed the current `SelectionModel` text into `StampContext::selection_numbers` when a calculator stamp is armed. Done when: `AccountingUi.SelectionSum` selects a fixture column and reads the preview text.
2. Build the picker section, previews and commands. Done when: `AccountingUi.PickerAccessibility` finds every control with an automation name and tooltip.

- [ ] Add the `Accounting` section to the `Stamp Picker` with the ten tickmarks and `Sum`, `Average` and `Count` entries, the calculator entries disabled with the hint `Select numbers on the page first` when nothing is selected. Done when: `AccountingUi.PickerAccessibility` checks enabled states with and without a selection.
- [ ] Show the live preview with the resolved text and `N numbers, M skipped`, updating as the selection changes, and announce the total politely once on arming. Done when: `AccountingUi.SelectionSum` changes the selection and reads the new preview.
- [ ] Place the stamp through `build_stamp_annotation` so the placed `/Contents` equals the preview, with the stamp unchanged after save and re-open. Done when: `AccountingUi.PlacedEqualsPreview` compares the two strings. Cheaper substitute: re-resolving at render time.
- [ ] Register `Stamp: Sum of Selection`, `Stamp: Average of Selection`, `Stamp: Count of Selection` and one `Tickmark: <name>` command per tickmark. Done when: `AccountingUi.PickerAccessibility` finds the commands in the command box.
- [ ] Add `AccountingUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=AccountingUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add the Accounting stamps and Sum of selection`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AccountingUi.*` passes on the display-session host, quoting the preview and placed strings.

## Verification

- `recto-tests --gtest_filter=PdfAnnotations.*:Cli.AnnotCommand:AnnotationUi.*:AppTests.AnnotationsCanvasAndUndo:AppTests.AnnotationsUi:PdfForms.*:Cli.FormsCommand:FormUi.*:AppTests.FormFillUi:AppTests.FormDesignerUi:A11y.FormFieldsAndAnnotations:PdfLinks.*:PdfDestinations.*:PdfArticles.*:PdfMeasure.*:PdfSnap.*:PdfStamps.*:Cli.AnnotMeasure:LinkUi.*:DestinationUi.*:ArticleUi.*:MeasureUi.*:StampUi.*:PdfReviewState.*:Cli.AnnotState:ReviewUi.*:PdfCommentSummary.*:Cli.AnnotSummarize:PdfCommentReport.*:SummaryUi.*:DocxCommentExport.*:PdfExtractCommented.*:PdfAutoLinks.*:Cli.AnnotLinks:AutoLinkUi.*:PdfBarcode.*:BarcodeUi.*:PdfBarcodeField.*:BarcodeFieldUi.*:A11y.BarcodeField:PdfFieldDetect.*:Cli.FormsDetect:DetectUi.*:PdfImageField.*:Cli.FormsSetImage:FormPickerUi.*:A11y.FormPickers:PdfFormCombine.*:Cli.FormsCombine:CombineUi.*:PdfInkPressure.*:PenUi.*:TouchUi.*:PdfCloudBorder.*:PdfArc.*:PdfAnnotGroup.*:PdfMeasureCircle.*:GroupUi.*:MarkupShapeUi.*:MeasureCircleUi.*:PdfStickers.*:StickerUi.*:PdfAccounting.*:AccountingUi.*` all pass (engine suites under ASan).
- Every control in the `Markup & Annotation Bar`, `Comments` side panel, `Form Fill & Form Designer Bar`, and `Field Properties` inspector carries an automation name and tooltip (`D03 T02 §3`).
- `python scripts/todo-graph.py validate` exits 0.
