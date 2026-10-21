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

## Outcome

- `src/pdf/annotations.cpp` reads, creates, modifies, deletes and flattens all 15 annotation types (`§1`) with deterministic `/AP` appearance stream synthesis (`§2`) so annotations render identically in Recto and third-party PDF viewers, plus `XFDF` and `JSON` import/export and `recto-cli annot <list|import|export|flatten>`.
- The viewer's `Markup & Annotation Bar` (`Ctrl+Shift+A`, `§3`), on-canvas 8-handle selection/resize frame, floating style popover, `Ctrl+Z` / `Ctrl+Y` undo/redo stack (`§4`), and `Comments / Annotations` side panel (`§5`) let users annotate pages, filter comments, reply, and jump to any markup.
- `src/pdf/forms.cpp` reads and writes AcroForms (`Tx`, `Btn` checkboxes/radios/pushbuttons, `Ch` combo/listboxes, `Sig` signature placeholders) with `/AP` synthesis and flattening (`§6`), converts XFA packets (`/XFA` template + datasets) to standard AcroForm fields, evaluates `/CO` numeric calculation expressions safely without JavaScript, imports/exports `JSON`, `FDF`, `XFDF` and `XML`, and powers `recto-cli forms <list|fill|export|import|flatten>` (`§7`).
- The viewer renders interactive form overlays on the page canvas in `Fill Form` mode (`§8`), provides visual drag-and-drop field placement, alignment guides, and tab-order badges in `Design Form` mode (`§9`), and offers the `Field Properties` inspector with UIA accessibility exposure (`§10`).
- `src/pdf/links.cpp` and the `Link` tool (`§11`, `§13`) author page-view, named-destination, other-PDF, web and `file:` links with a flyout to edit and delete them, and refuse every executable action; `src/pdf/destinations.cpp` and the `Destinations` panel (`§12`, `§14`) list, create, rename and delete named destinations and keep them valid when pages change.
- `src/pdf/articles.cpp`, the `Articles` panel and the `Article` tool (`§15`, `§16`) read, follow and author article threads.
- `src/pdf/measure.cpp`, the vector snap index and the `Measure` bar (`§17`, `§18`, `§19`) measure distance, perimeter and area against a page scale typed in drawing units, snap to endpoints, midpoints and intersections, and export measurements to CSV and JSON (`recto-cli annot measure`).
- `src/services/stamp_library.cpp`, `src/pdf/stamps.cpp` and the `Stamp Manager` (`§20`, `§21`) keep a library of custom image and dynamic stamps whose date, time, user and file tokens resolve once at application without JavaScript, with a `Keep stamp tool selected` mode.

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

## 1. PDF `/Annots` parser and serializer for all 15 annotation subtypes (`annotations.cpp`)

Implement `src/pdf/annotations.h` and `src/pdf/annotations.cpp` in `recto-pdf`: parse, create, modify, and delete page `/Annots` dictionary entries covering all 15 standard PDF annotation subtypes (`Highlight`, `Underline`, `Squiggly`, `StrikeOut`, `Text` sticky note, `FreeText` callout/textbox, `Ink`, `Line` / `Arrow`, `Square` rectangle, `Circle` ellipse, `Polygon`, `PolyLine`, `Stamp`, `FileAttachment`, `Link`, and `Caret`) and persist mutations via `IncrementalWriter` (`D01 T02 §5`).

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Define typed `PdfAnnotation` models in `src/pdf/annotations.h` for all 15 subtypes and parse page `/Annots` arrays (including `/Rect`, `/QuadPoints`, `/C` RGB/CMYK/Gray colour, `/IC` interior colour, `/CA` opacity, `/BS` border width/dash, `/LE` line endings, `/InkList`, `/Vertices`, `/Contents`, `/T` author, `/M` PDF date string, `/NM` unique ID, `/IRT` in-reply-to reference, `/RT` `/Reply` or `/Group`, and `/Popup`). Done when: `PdfAnnotations.ParseAllSubtypes` reads every property from a 15-subtype fixture under ASan.
2. Implement `create_annotation`, `update_annotation`, and `delete_annotation` on `pdf::Document` and `DocumentActor`, writing modified `/Annots` arrays and annotation objects through `IncrementalWriter`. Done when: `PdfAnnotations.RoundTripAllSubtypes` creates all 15 subtypes, saves incrementally, re-opens, and verifies identical fields.

- [ ] Define `AnnotationSubtype` enum (all 15 subtypes) and `PdfAnnotation` struct with subtype-specific payloads (`QuadMarkupData`, `StickyNoteData`, `FreeTextData`, `InkData`, `LineData`, `ShapeData`, `PolyData`, `StampData`, `FileAttachmentData`, `LinkData`, `CaretData`) in `src/pdf/annotations.h`. Done when: `src/pdf/annotations.h` compiles with zero warnings across `recto-pdf` and `recto-core`. Cheaper substitute: parsing only `/Subtype /Highlight` and `/Text` and dropping all other 13 ISO 32000-1 annotation subtypes.
- [ ] Parse `/QuadPoints` (8 floats per quad, normalizing both counter-clockwise ISO 32000-1 and clockwise Acrobat quad vertex orderings) for `Highlight`, `Underline`, `Squiggly`, and `StrikeOut` annotations. Done when: `PdfAnnotations.QuadPointsOrdering` normalizes both vertex conventions into valid bounding quads.
- [ ] Parse and serialize `/InkList` (multi-stroke polyline/Bezier coordinate arrays), `/Vertices` (`Polygon` / `PolyLine`), and `/LE` start/end line-ending styles (`None`, `OpenArrow`, `ClosedArrow`, `Circle`, `Diamond`, `Butt`) for `Ink`, `Line`, `Polygon`, and `PolyLine`. Done when: multi-stroke ink and arrow annotations round-trip all coordinates and line endings losslessly.
- [ ] Parse and serialize `FreeText` (`/DA` default appearance font/size/colour, `/CL` callout knee-line coordinates, `/Q` quadding alignment) and `Stamp` (`/Name` standard names `Approved`, `Confidential`, `Draft`, `Final`, `ForComment`, `NotApproved`, `Void`, `SignHere` plus custom stamp text/date). Done when: `FreeText` callouts and `Stamp` dictionaries round-trip cleanly.
- [ ] Parse and serialize reply thread metadata (`/NM` unique UUID name, `/IRT` indirect reference to parent annotation, `/RT /Reply`, `/T` author string, `/CreationDate` and `/M` `D:YYYYMMDDHHmmSS` timestamps). Done when: nested reply chains reconstruct their exact parent-child comment tree.
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
- [ ] Group comment cards by page (`Page N` sticky/collapsible group header with per-page count) ordered top-to-bottom by page `/Rect` Y coordinate, and nest `/IRT` `/Reply` annotations under their parent card in chronological order. Done when: `AnnotationUi.CommentsGroupingAndReplies` verifies page grouping and multi-level reply threads.
- [ ] Implement filter chips (`All`, `Markup`, `Notes`, `FreeText`, `Drawings`, `Stamps`), author filter dropdown, and instant text search across `/Contents` and `/T` author strings. Done when: filtering by `Notes` or typing a search phrase narrows the visible groups immediately and shows a clean empty state when 0 items match.
- [ ] Render each comment card with subtype icon + colour swatch dot, author name, relative/formatted `/M` timestamp, editable `/Contents` text (or quoted highlighted `PageText` snippet when `/Contents` is empty), `Reply` inline input, and `Delete` button. Done when: adding a reply creates a linked `/IRT` `/Reply` annotation in `DocumentActor`. Cheaper substitute that fails the checkpoint: read-only cards with no inline comment editing or reply threading.
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

Add the write side of named destinations to `recto-pdf`: enumerate, create, rename and delete entries in the catalog `/Names /Dests` name tree and the legacy catalog `/Dests` dictionary, and keep every destination reference valid when pages are deleted, moved or merged. `D01 T01 §10` only reads them. The matching Acrobat Pro surface is the `Destinations` navigation panel (`§14`). It must not break the open viewer document (edits are staged on the actor's copy, `§1`) and keeps Recto's improvement of a raw-byte check that no deleted page stays reachable. The remap rule exists because pdfcraft left named destinations with null page targets after a page delete (#817) and a bookmark with no destination after a page delete (#2902).

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
- [ ] Implement `remap_page_references` with a `PageRemap` (old page index to new page index or removed) and report dropped references in a `RemapReport`; a destination whose page was removed is deleted from the name tree, and an outline item or link whose target was removed keeps its entry but loses the dead `/Dest` and is listed in the report. Done when: `PdfDestinations.RemapOnDelete` and `PdfDestinations.RemapOnMove` pass and the report names each dropped reference. Cheaper substitute: leaving a null page entry in the destination array (the pdfcraft #817 and #2902 failure).
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

Add the measurement model to `recto-pdf` and `recto-services`: a page measurement scale stored as a `/VP` viewport with an `/RL` `/Measure` dictionary (ISO 32000-2 section 12.9), measurement annotations (`Line` distance, `PolyLine` perimeter, `Polygon` area carrying `/Measure`), exact geometry for length and area in real units, and export of every measurement. This matches the Acrobat Pro `Measuring` tools. It must not break `§1` and `§2` (measurements are ordinary `Line`, `PolyLine` and `Polygon` annotations that third-party viewers still draw) and keeps Recto's improvement of typed, bounded numbers: a hostile scale (zero, negative, `NaN`) is rejected. The scale is entered in drawing units because pdfcraft's measuring ratio report (#2752) showed users cannot trust a ratio they cannot state in the units of their drawing.

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
- [ ] Build `ScaleDialog` with `Page length` and `Real length` number boxes and unit combo boxes (`1 [in] = 10 [ft]`), a `Use two points` helper that measures a known page length and asks for its real length, and a live preview of the resulting scale text. Done when: `MeasureUi.ScaleSnapExport` sets `1 in = 10 ft` by typing and `1 cm = 2.5 m` through `Use two points`. Cheaper substitute: a preset list of scales with no typed values (the pdfcraft #2752 failure).
- [ ] Draw the live segment, the running value in a pill near the pointer (`12.5 ft`), and, for area, the closing edge preview and the area in the pill. Done when: `MeasureUi.DistancePerimeterArea` reads the pill text while a polygon is open.
- [ ] Snap to the `§18` candidates within `8 epx`, show the snap marker and a text label (`Endpoint`, `Midpoint`, `Intersection`) in the pill, and fall back to no snap when the index is still building without blocking the pointer. Done when: `MeasureUi.ScaleSnapExport` snaps to an endpoint, a midpoint and an intersection and the pointer stays responsive while the index builds.
- [ ] Record each completed measurement as one `§4` undo entry through `DocumentActor` (`§17` annotation), with `Enter` finishing a perimeter or area and `Backspace` removing the last point. Done when: `MeasureUi.UndoAndEdit` undoes a polygon in one step and removes a last point with `Backspace`.
- [ ] Build `MeasurementsPanel` listing every measurement annotation (page, kind, value, label) with jump, delete, `Export CSV` and `Export JSON` writing through temp-file-then-replace, and a `stale` badge with the text `Scale changed` for entries whose stored value differs from the page scale. Done when: `MeasureUi.ScaleSnapExport` exports both files and shows the badge after a scale edit.
- [ ] Add `MeasureUi.*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=MeasureUi.*` passes on the display-session host.
- [ ] Commit: `feat(app): add the Measure tool bar, scale dialog, snapping and Measurements panel`

**Test checkpoint:** `recto-tests.exe --gtest_filter=MeasureUi.*` passes on the display-session host, quoting the pill text and the exported CSV rows.

## 20. Stamp library store and dynamic stamp token resolution (no JavaScript)

Add the stamp library to `recto-services` and the stamp builder to `recto-pdf`: a persistent library of custom image stamps and dynamic stamps whose text carries tokens for date, time, user name and file name, resolved once when the stamp is applied. This matches the Acrobat Pro `Stamps` palette, `Custom Stamps` and `Dynamic Stamps`. Acrobat dynamic stamps are driven by JavaScript; Recto resolves tokens in native code at application time (D9), and the applied stamp is a static annotation with a static `/AP`, so it never changes when the file is reopened. The need comes from pdfcraft requests for dynamic stamps with date, time and user metadata (#1741) and reusable dynamic image stamps (#1825).

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

Acrobat Pro's Fill and Sign lets a user complete a form that has no fields: type text anywhere, place a check, cross, dot, line or rounded box, and add a saved signature or initials. Recto has typewriter text (§2) and a signature appearance library (`D05 T02 §10`), but no single fill tool for flat forms (foundation plan D18; `docs/acrobat-parity.md`). pdfcraft users asked for exactly this and hit its failure modes: typed text that became `?`, a placed signature that could not be moved over a field, text boxes eaten when dragged across each other, and a flatten-on-save default buried in settings (`docs/research/2026-10-20-pdfcraft-issue-lessons.md`; pdfcraft #506, #594, #825, #916, #1696, #2149). This section adds a Fill and Sign mode that writes annotations through the existing engine, fills real AcroForm fields when the page has them, and flattens only when the user turns on `Lock on save`.

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
- [ ] Place typed text through the Unicode authoring path so any script round-trips and nothing becomes `?` (`D06 T03 §7`; pdfcraft #506). Done when: a Czech, Greek and CJK entry extracts exactly after save and reopen.
- [ ] Route clicks inside an AcroForm widget to that field (§8), and let a placed signature or symbol move over a field without being captured by it (pdfcraft #825, #916). Done when: a signature dragged across a text field stays a signature and the field keeps its value.
- [ ] Keep overlapping fill objects independent: dragging one across another never merges or deletes either (pdfcraft #594). Done when: two overlapping text objects both survive a drag and an undo.
- [ ] Add `Sign` and `Initials` tools that place an appearance from the `D05 T02 §10` library as an annotation, with no cryptographic signature, and say so in the tooltip. Done when: the placed appearance is a stamp annotation and the document's signature panel lists no signature. Cheaper substitute: rasterizing the signature into the page.
- [ ] Add a visible `Lock on save` toggle (off by default) that flattens Fill and Sign objects into page content on Save As only, never in place (pdfcraft #2149). Done when: the saved copy contains no Fill and Sign annotations when the toggle is on and the open document is unchanged.
- [ ] Register `fillSign.*` commands in `CommandRegistry` (`D02 T03 §11`) with a single-key tool switch only while the bar is active and no text field has focus. Done when: typing in a field never switches tools.
- [ ] Add `Annot.FillSignPlacement`, `Annot.FillSignBar` and `Annot.FillSignLockOnSave` in `tests/recto-tests/`. Done when: the suites pass on the display-session host.
- [ ] Commit: `annot: add Fill and Sign for flat forms with symbols, signatures and Lock on save`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Annot.FillSign*` passes on the display-session host, and a filled, locked copy renders identically in Recto and the PDFium oracle.

## Verification

- `recto-tests --gtest_filter=PdfAnnotations.*:Cli.AnnotCommand:AnnotationUi.*:AppTests.AnnotationsCanvasAndUndo:AppTests.AnnotationsUi:PdfForms.*:Cli.FormsCommand:FormUi.*:AppTests.FormFillUi:AppTests.FormDesignerUi:A11y.FormFieldsAndAnnotations:PdfLinks.*:PdfDestinations.*:PdfArticles.*:PdfMeasure.*:PdfSnap.*:PdfStamps.*:Cli.AnnotMeasure:LinkUi.*:DestinationUi.*:ArticleUi.*:MeasureUi.*:StampUi.*` all pass (engine suites under ASan).
- Every control in the `Markup & Annotation Bar`, `Comments` side panel, `Form Fill & Form Designer Bar`, and `Field Properties` inspector carries an automation name and tooltip (`D03 T02 §3`).
- `python scripts/todo-graph.py validate` exits 0.
