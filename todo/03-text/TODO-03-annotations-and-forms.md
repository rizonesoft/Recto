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

## Outcome

- `src/pdf/annotations.cpp` reads, creates, modifies, deletes and flattens all 15 annotation types (`§1`) with deterministic `/AP` appearance stream synthesis (`§2`) so annotations render identically in Recto and third-party PDF viewers, plus `XFDF` and `JSON` import/export and `recto-cli annot <list|import|export|flatten>`.
- The viewer's `Markup & Annotation Bar` (`Ctrl+Shift+A`, `§3`), on-canvas 8-handle selection/resize frame, floating style popover, `Ctrl+Z` / `Ctrl+Y` undo/redo stack (`§4`), and `Comments / Annotations` side panel (`§5`) let users annotate pages, filter comments, reply, and jump to any markup.
- `src/pdf/forms.cpp` reads and writes AcroForms (`Tx`, `Btn` checkboxes/radios/pushbuttons, `Ch` combo/listboxes, `Sig` signature placeholders) with `/AP` synthesis and flattening (`§6`), converts XFA packets (`/XFA` template + datasets) to standard AcroForm fields, evaluates `/CO` numeric calculation expressions safely without JavaScript, imports/exports `JSON`, `FDF`, `XFDF` and `XML`, and powers `recto-cli forms <list|fill|export|import|flatten>` (`§7`).
- The viewer renders interactive form overlays on the page canvas in `Fill Form` mode (`§8`), provides visual drag-and-drop field placement, alignment guides, and tab-order badges in `Design Form` mode (`§9`), and offers the `Field Properties` inspector with UIA accessibility exposure (`§10`).

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

## Verification

- `recto-tests --gtest_filter=PdfAnnotations.*:Cli.AnnotCommand:AnnotationUi.*:AppTests.AnnotationsCanvasAndUndo:AppTests.AnnotationsUi:PdfForms.*:Cli.FormsCommand:FormUi.*:AppTests.FormFillUi:AppTests.FormDesignerUi:A11y.FormFieldsAndAnnotations` all pass (engine suites under ASan).
- Every control in the `Markup & Annotation Bar`, `Comments` side panel, `Form Fill & Form Designer Bar`, and `Field Properties` inspector carries an automation name and tooltip (`D03 T02 §3`).
- `python scripts/todo-graph.py validate` exits 0.
