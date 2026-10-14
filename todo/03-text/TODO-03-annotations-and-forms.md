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
- -> XREF: D02 T03 §5 -- Comments side panel and outline/layer/attachment shell slots share the side-panel container and undo/redo history stack
- -> XREF: D03 T01 §2 -- Selection floating toolbar quick Highlight and Add Note buttons delegate to the annotation creation actions here
- -> XREF: D03 T02 §1 -- UI Automation tree exposes interactive form fields and annotation comments alongside page text

## Outcome

- `src/pdf/annotations.cpp` reads, creates, modifies, deletes and flattens all 15 annotation types with deterministic `/AP` appearance stream synthesis so annotations render identically in Recto and third-party PDF viewers, plus `XFDF` and `JSON` import/export and `recto-cli annot <list|import|export|flatten>`.
- The viewer's `Markup & Annotation Bar` (`Ctrl+Shift+A`), on-canvas 8-handle selection/resize frame, floating style popover, `Ctrl+Z` / `Ctrl+Y` undo/redo stack, and `Comments / Annotations` side panel let users annotate pages, filter comments, reply, and jump to any markup.
- `src/pdf/forms.cpp` reads and writes AcroForms (`Tx`, `Btn` checkboxes/radios/pushbuttons, `Ch` combo/listboxes, `Sig` signature placeholders), converts XFA packets (`/XFA` template + datasets) to standard AcroForm fields, evaluates `/CO` numeric calculation expressions safely without JavaScript, synthesizes `/AP` streams, imports/exports `JSON`, `FDF`, `XFDF` and `XML`, flattens forms into static page content, and powers `recto-cli forms <list|fill|export|import|flatten>`.
- The viewer renders interactive form overlays on the page canvas, shows the `Form Fill & Form Designer Bar` when a form document opens or `Design Form` is toggled, and provides on-canvas field placement, alignment guides, tab-order badges and a `Field Properties` inspector.

**Adjacency:** list=applicable @ D03 T03 §2; document=applicable @ D03 T03 §1; settings=not-applicable (annotation and form tool state is session-scoped per window); reporting=not-applicable (comment counts appear in the side panel header, section 2); notifications=applicable @ D03 T02 §3; permissions=not-applicable (PDF annotation/form permission flags are reported in Info, D02 T03 §3); audit=not-applicable (annotation author and modification timestamps are standard PDF dictionary entries in section 1, not an external audit log); exchange=applicable @ D03 T03 §1; reverse=applicable @ D03 T03 §2

**Adjacency rationale:** The Comments side panel (`§2`) is the list surface, saving annotated or form-filled PDFs (`§1`, `§3`) is the document surface, `XFDF`/`JSON`/`FDF`/`XML` import and export (`§1`, `§3`) are the exchange surfaces, and the session `Undo`/`Redo` stack (`Ctrl+Z`/`Ctrl+Y` in `§2`) plus `Reset Form` (`§4`) are the reverse surfaces.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Annotations and markup engine, `/AP` synthesis and `XFDF`/`JSON` I/O | D01 T02 §4 | [ ] |
| 2 | §2 | Viewer Markup and Annotation Bar (`Ctrl+Shift+A`), canvas handles and Comments panel | §1, D02 T03 §5, D03 T01 §2 | [ ] |
| 3 | §3 | AcroForms, XFA-to-AcroForm converter, calculation order and form data I/O | §1, D01 T02 §4 | [ ] |
| 4 | §4 | On-canvas interactive form filling, Form Designer Bar and Field Properties Inspector | §2, §3 | [ ] |

## 1. Annotations and markup engine, `/AP` synthesis and `XFDF`/`JSON` I/O

Implement `src/pdf/annotations.cpp` in `recto-pdf` and its `DocumentActor` messages in `recto-core`. Parse page `/Annots` arrays into typed `PdfAnnotation` models covering `Highlight`, `Underline`, `Squiggly`, `StrikeOut` (`/QuadPoints`), `Text` (`Sticky Note`), `FreeText` (callout and text box), `Ink` (`/InkList` bezier/polyline strokes), `Line` (with start/end `/LE` arrowheads), `Square` (`Rectangle`), `Circle` (`Ellipse`), `Polygon`, `PolyLine`, `Stamp` (standard and custom text/date stamps), `FileAttachment`, `Link`, and `Caret`. Synthesize `/AP /N` normal appearance streams for every created or modified annotation so that `DisplayList` compilation in `D01 T02 §3` renders them natively and third-party viewers display identical visuals. Implement annotation flattening (merging `/AP /N` Form XObjects into the page `/Contents` stream and removing the `/Annots` entry), `XFDF` (ISO 19444-1 XML) and `JSON` annotation import/export, and `recto-cli annot <list|import|export|flatten>`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Parse and serialize `/Annots` dictionaries in `src/pdf/annotations.cpp` for all 15 annotation subtypes, including `/Rect`, `/QuadPoints`, `/C` (colour), `/CA` (opacity), `/BS` (border style/width), `/Contents`, `/T` (author), `/M` (modification date), `/NM` (unique name), `/IRT` (in-reply-to thread link), and `/Popup`. Done when: `recto-tests --gtest_filter=PdfAnnotations.ParseAndWrite` round-trips all 15 subtypes through incremental save (`D01 T02 §4`) and re-parses identical fields.
2. Implement deterministic `/AP /N` appearance-stream synthesis and annotation flattening (`flatten_annotations(page_range)`), plus `XFDF` and `JSON` import/export and `recto-cli annot <list|import|export|flatten>`. Done when: created annotations render in `DisplayList` rasterization, flattened pages have zero mutable `/Annots` while preserving pixel-identical appearance (`SSIM >= 0.99`), and `XFDF`/`JSON` round-trip preserves all comments and replies.

- [ ] Read, create, edit and delete all 15 PDF annotation subtypes with `/AP /N` synthesis. Done when: `PdfAnnotations.AllSubtypes` creates each subtype on a blank page, saves, re-opens, and rasterizes non-empty appearance graphics.
- [ ] Annotation flattening, `XFDF`/`JSON` import/export, and `recto-cli annot`. Done when: `PdfAnnotations.FlattenAndXfdf` verifies flattened pixel parity and lossless `XFDF`/`JSON` comment round-tripping.
- [ ] Commit: `pdf: annotations engine, /AP synthesis, flattening and XFDF/JSON I/O`

**Test checkpoint:** `recto-tests --gtest_filter=PdfAnnotations.*` passes under AddressSanitizer (`-fsanitize=address`) covering all 15 subtypes, `/AP` appearance streams, flattening, and `XFDF`/`JSON` round-tripping.

## 2. Viewer Markup and Annotation Bar (`Ctrl+Shift+A`), canvas handles and Comments panel

Build the visual annotation authoring and review experience specified in `design/SPEC.md` sections 8.2, 8.3 and 9.3. Add the contextual `Markup & Annotation Bar` (`40 epx` strip below the main toolbar, toggled via `Annotate` button or `Ctrl+Shift+A`) with tool buttons for `Highlight`, `Underline`, `Squiggly`, `StrikeOut`, `Sticky Note`, `FreeText / Callout`, `Ink Draw` + `Eraser`, `Shapes` (`Rectangle`, `Ellipse`, `Line`, `Arrow`, `Polygon`), `Stamp` (`Approved`, `Confidential`, `Draft`, `Final`, `Sign Here`, or custom text/date stamp), `Colour & Opacity Picker` (`5` theme-aware swatches + custom colour + stroke thickness `1-12 pt` + opacity `25-100%`), `Flatten Annotations`, and `Undo` / `Redo` (`Ctrl+Z` / `Ctrl+Y`). Wire the floating selection toolbar's (`D03 T01 §2`) quick `Highlight` and `Add Note` buttons to this engine. Add on-canvas selection handles (`8` resize/endpoint handles in `--accent` plus floating style/delete popover) and the `Comments / Annotations` side panel (`SPEC 8.3`) with page-grouped cards, reply threads, type/author filters, search, and `Import/Export XFDF or JSON`.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (Markup & Annotation Bar), 8.3 (Comments / Annotations side panel), 9.3 (Selection floating toolbar quick markup actions), and 10 (`Ctrl+Shift+A`, `Ctrl+Z`, `Ctrl+Y`).
**Job:** A reader can highlight text, drop sticky notes, draw freehand ink or shapes, stamp pages, edit or delete existing annotations on canvas, and review or export all comments in the side panel. Consumer: viewer window (`D02 T02`), selection toolbar (`D03 T01 §2`), and command box (`D02 T03 §4`).
**Treatment:** Contextual `40 epx` `Markup & Annotation Bar` using `RectoColors.xaml` tokens, interactive canvas overlay with 8-handle selection boxes and live preview while dragging, `Undo`/`Redo` command history per `DocumentActor`, and virtualized `Comments / Annotations` side panel. Cheaper substitute that fails the checkpoint: read-only annotations with no on-canvas creation, missing `Undo`/`Redo`, or annotations that do not persist `/AP` streams when saved.
**Chrome:** consume `DocumentView` (`D02 T02`), the side-panel host (`D02 T03 §5`), and the floating selection toolbar (`D03 T01 §2`). Do not invent a separate annotation window or modal editor.

**Build order:**

1. Build the `Markup & Annotation Bar` (`Ctrl+Shift+A`), on-canvas creation tools (text-quad markup, sticky notes, free-text callouts, smooth ink strokes, geometric shapes, stamps), 8-handle selection/resize overlay, floating style/delete popover, and `Undo`/`Redo` stack (`Ctrl+Z` / `Ctrl+Y`). Done when: creating, moving, resizing, restyling, undoing, and redoing annotations on canvas updates the rendered page immediately and saves cleanly via temp-file-then-replace.
2. Build the `Comments / Annotations` side panel (`SPEC 8.3`) with page-grouped comment cards, inline reply threads, type/author filter chips, search box, click-to-jump-and-select, and `Import/Export XFDF or JSON` commands. Done when: adding or editing an annotation on canvas updates the Comments panel live, clicking a card scrolls to and highlights the annotation on canvas, and every toolbar/panel control has an automation name and tooltip.

- [ ] `Markup & Annotation Bar` (`Ctrl+Shift+A`), on-canvas creation/selection/resize handles, style popover, and `Undo`/`Redo` (`Ctrl+Z`/`Ctrl+Y`). Done when: all 15 annotation types can be inspected or authored on canvas and undone/redone cleanly. Cheaper substitute that fails the checkpoint: toolbar buttons that only drop fixed-size stamps without interactive drag, resize handles, or style editing.
- [ ] `Comments / Annotations` side panel with filtering, replies, and `XFDF`/`JSON` import/export. Done when: `AppTests.AnnotationsUi` drives creating a highlight and sticky note, replying in the side panel, filtering by type, exporting `XFDF`, and flattening. Cheaper substitute that fails the checkpoint: a flat ungrouped list with no replies or `XFDF` export.
- [ ] Commit: `app: markup and annotation bar, canvas handles and comments side panel`

**Test checkpoint:** `recto-tests --gtest_filter=AppTests.AnnotationsUi` quotes tool activation (`Ctrl+Shift+A`), highlight and shape creation, handle resize, `Ctrl+Z`/`Ctrl+Y` undo/redo, Comments panel filtering, and `XFDF` export.

## 3. AcroForms, XFA-to-AcroForm converter, calculation order and form data I/O

Implement `src/pdf/forms.cpp` in `recto-pdf` and form-actor commands in `recto-core`. Parse `/AcroForm` field hierarchies (`/Fields`, parent/child `/Kids`, inherited `/FT`, `/T`, `/TU` tooltip, `/TM` mapping name, `/Ff` field flags, `/V` value, `/DV` default value, `/DA` default appearance, `/DR` default resources, `/Opt` choice options, `/MaxLen`). Support all four PDF field types: `Tx` (single-line, multiline, password, comb text), `Btn` (checkbox, radio button group, pushbutton), `Ch` (dropdown combo box, scrollable list box), and `Sig` (signature field dictionary linked to `D05 T01 §5`). Implement a static and dynamic XFA-to-AcroForm converter that parses `/AcroForm /XFA` XML streams (`template` + `datasets`), maps `<subform>`, `<field>` and `<draw>` nodes into positioned standard AcroForm widget annotations, and populates their values so XFA forms can be viewed, filled and flattened without JavaScript. Implement safe numeric calculation-order (`/CO`) evaluation (`SUM`, `PRD`, `AVG`, `MIN`, `MAX` and basic arithmetic field formulas without executing untrusted JS), `/AP /N` appearance-stream regeneration for every modified field, form flattening (`flatten_forms()`), `JSON`, `FDF`, `XFDF` and `XML` form data import/export, and `recto-cli forms <list|fill|export|import|flatten>`.

**Phase:** 3
**Needs:** none
**Surface:** no

**Build order:**

1. Parse and write `/AcroForm` field trees (`Tx`, `Btn`, `Ch`, `Sig`), regenerate `/AP /N` streams on value change using `/DA` font/size/colour and `D01 T02 §4` font metrics, and implement safe `/CO` calculation-order evaluation and `flatten_forms()`. Done when: `recto-tests --gtest_filter=PdfForms.AcroFormAndCalc` fills text, checkbox, radio and choice fields, recalculates dependent total fields, synthesizes `/AP` streams, and flattens the form into static page content.
2. Implement the XFA-to-AcroForm converter (`/XFA` XML `template` + `datasets` -> standard `/AcroForm` widget annotations), `JSON`/`FDF`/`XFDF`/`XML` data import and export, and `recto-cli forms <list|fill|export|import|flatten>`. Done when: `recto-tests --gtest_filter=PdfForms.XfaAndDataIo` converts an XFA fixture to fillable AcroForm fields and round-trips field values through `JSON`, `FDF`, `XFDF` and `XML`.

- [ ] `/AcroForm` reading, writing, field creation, `/AP` synthesis, `/CO` calculation evaluation, and form flattening. Done when: filled and flattened forms render identically to unfilled + overlay and pass `PdfForms.AcroFormAndCalc`.
- [ ] XFA-to-AcroForm conversion, `JSON`/`FDF`/`XFDF`/`XML` data I/O, and `recto-cli forms`. Done when: `PdfForms.XfaAndDataIo` passes on AcroForm and XFA fixtures.
- [ ] Commit: `pdf: AcroForms, XFA-to-AcroForm converter, /AP synthesis and form data I/O`

**Test checkpoint:** `recto-tests --gtest_filter=PdfForms.*` passes under AddressSanitizer (`-fsanitize=address`) covering AcroForm field types, field creation, XFA conversion, `/CO` calculation order, `/AP` synthesis, flattening, and `JSON`/`FDF`/`XFDF`/`XML` import/export.

## 4. On-canvas interactive form filling, Form Designer Bar and Field Properties Inspector

Build the interactive form filling and visual Form Designer surfaces specified in `design/SPEC.md` section 8.2 (`Form Fill & Form Designer Bar`). When a PDF containing `/AcroForm` or `/XFA` opens, or when the user invokes `Forms` / `Design Form` from the toolbar or command box (`Ctrl+K`), show the `40 epx` contextual `Form Fill & Form Designer Bar`:
- **Fill Mode (`Fill Form`):** Renders interactive WinUI 3 overlays aligned to each field's page quad (`TextBox` for single/multiline/comb text, `CheckBox`, `RadioButton`, `ComboBox`, `ListBox`, and clickable `Signature Field` banner), with `Highlight Required Fields` (`--accent-subtle` fill and `--danger` border on required empty fields), `Tab` / `Shift+Tab` field navigation in `/Tabs` (`/R`, `/C`, `/S`) order, live `/CO` calculated field updates, `Clear / Reset Form`, `Import Data` (`JSON`, `FDF`, `XFDF`, `XML`), `Export Data`, and `Flatten Form`.
- **Designer Mode (`Design Form`):** Provides drag-and-drop placement tools (`Add Text Field`, `Add Checkbox`, `Add Radio Group`, `Add Combo Box`, `Add List Box`, `Add Button`, `Add Signature Field`), on-canvas smart alignment guides, visual tab-order number badges with drag-to-reorder, and a right-hand `Field Properties` flyout/inspector (`Field Name`, `Tooltip / Accessibility Label`, `Default Value`, `Required`, `Read-Only`, `Multiline / Comb`, `Font Family / Size / Colour`, `Border / Fill Colour`, `Choice Options`, `Number / Date / Mask Format`, and `Calculation Formula`: `Sum`, `Product`, `Average`, `Min`, `Max`).

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 8.2 (`Form Fill & Form Designer Bar`), section 10 (keyboard navigation), and section 11 (accessibility names from `/TU` tooltips).
**Job:** Users can fill out interactive PDF forms with keyboard and mouse, import/export form data, or visually design new fillable PDF forms with calculated fields and tab order. Consumer: viewer window (`D02 T02`), UIA accessibility tree (`D03 T02 §1`), and command box (`D02 T03 §4`).
**Treatment:** Two-mode (`Fill Form` / `Design Form`) contextual bar over live canvas widget overlays and a structured `Field Properties` inspector. Cheaper substitute that fails the checkpoint: dialog-only form editing disconnected from the page canvas, missing XFA banner/conversion prompt, or a designer mode that cannot set field names, tab order, or calculation formulas.
**Chrome:** consume `DocumentView` (`D02 T02`) for coordinate mapping and `D03 T03 §3` for `/AcroForm` mutations. Do not block the UI thread while regenerating `/AP` streams or saving.

**Build order:**

1. Implement `Fill Form` mode: on-canvas field overlays (`Tx`, `Btn`, `Ch`, `Sig`), `Tab`/`Shift+Tab` field traversal, required-field highlighting, live `/CO` calculated field updates, `Reset Form`, `Import/Export Data` (`JSON`/`FDF`/`XFDF`/`XML`), and `Flatten Form`. Done when: `AppTests.FormFillUi` tabs through a multi-field form fixture, types text, toggles checkboxes, selects a combo option, verifies calculated total update, and exports `JSON`.
2. Implement `Design Form` mode: tool buttons for placing all 7 widget types on canvas, alignment guides, tab-order badges, and the `Field Properties` inspector (`Name`, `Tooltip`, `Required`, `Read-Only`, `Font`, `Options`, `Calculation Formula`), plus UIA exposure (`D03 T02 §1`) for form fields. Done when: `AppTests.FormDesignerUi` places new text, checkbox, combo and calculated total fields on a blank page, saves the PDF, and re-opens it in `Fill Form` mode as a working interactive form.

- [ ] On-canvas `Fill Form` mode with `Tab` order, required highlights, live calculations, data import/export, and flattening. Done when: `AppTests.FormFillUi` passes end-to-end. Cheaper substitute that fails the checkpoint: static rendering that requires a modal popup per field.
- [ ] `Design Form` mode with on-canvas placement of all 7 field types, tab-order badges, and `Field Properties` inspector. Done when: `AppTests.FormDesignerUi` authors a multi-field form with a `Sum` calculation formula from scratch and verifies it after save/reopen. Cheaper substitute that fails the checkpoint: read-only field inspection without creation or property editing.
- [ ] Commit: `app: interactive form filling, form designer bar and field properties inspector`

**Test checkpoint:** `recto-tests --gtest_filter=AppTests.FormFillUi:AppTests.FormDesignerUi` passes and quotes field traversal, calculation updates, form designer field authoring, and `JSON`/`XFDF` export.

## Verification

- `recto-tests --gtest_filter=PdfAnnotations.*:PdfForms.*:AppTests.AnnotationsUi:AppTests.FormFillUi:AppTests.FormDesignerUi` all pass (engine suites under ASan).
- Every control in the `Markup & Annotation Bar`, `Comments` side panel, `Form Fill & Form Designer Bar`, and `Field Properties` inspector carries an automation name and tooltip (`D03 T02 §2`).
- `python scripts/todo-graph.py validate` exits 0.
