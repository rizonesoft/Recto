---
schema_version: 1
id: pdf-to-word
domain: 04-convert
status: active
title: "TODO-03 -- PDF to Word (DOCX): Native OpenXML Writer and Quality Review"
track: M5
---

# TODO-03 -- PDF to Word (DOCX): Native OpenXML Writer and Quality Review

> **Goal:** Build Recto's own native C++20 `PDF -> Word (.docx)` converter (`DocxWriter` in `src/core/docx_writer.cpp`) generating standards-compliant ECMA-376 OpenXML `.docx` packages directly from `recto-pdf`'s `PageText`, vector paths, and page images (plus `OcrPipeline` for scanned pages), then evaluate and tune its layout and table reconstruction across the foundation plan's 20-document corpus.

> **Current state:** Measured 2026-10-14. No DOCX writer exists yet. With MuPDF retired on 2026-10-14 (`D3`, `D7`), Recto owns the entire `PDF -> DOCX` pipeline in-house in C++20 rather than wrapping a third-party `extract` library: `D01` provides vector paths and image XObjects, `D03 T01` provides `PageText` (`§1`) and `OcrPipeline` (`§4`), and `D04 T02 §1` provides `Flate`/ZIP compression and `AtomicFileWriter`.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D7), 5 (`PDF -> Word (.docx)` row: native C++20 `DocxWriter`), 7 (M5 done-when), 11 (20-document PDF -> Word test corpus).
- `design/SPEC.md` sections 2 (Principle 4: "Honest conversion. Say what a conversion is good at and what it is not (PDF to Word works best on text documents)"), 9.5 (Word document card, step 3 options, hint text), and `design/screenshots/explorations/B-Convert.png`.
- ECMA-376 WordprocessingML (`[Content_Types].xml`, `_rels/.rels`, `word/document.xml`, `word/styles.xml`, `word/numbering.xml`, `word/_rels/document.xml.rels`, `word/media/*`).
- -> XREF: D03 T01 §1 -- `DocxWriter` consumes the `PageText` blocks, lines, spans, font attributes and character quads produced by `D03 T01 §1` (and OCR-populated `PageText` from `D03 T01 §4`).
- -> XREF: D04 T01 §2 -- plugs into the Convert page's **Word document** card, step 3 options (`explorations/B-Convert.png`), and `JobQueue`.

## Outcome

- `DocxWriter` (`src/core/docx_writer.cpp`) writes valid ECMA-376 `.docx` ZIP packages that open cleanly in Microsoft Word and LibreOffice Writer without repair prompts: paragraphs and headings reconstructed in reading order, inline formatting (font family, size, bold, italic, colour), multi-column section breaks (`w:cols`), tables reconstructed from vector ruling lines and aligned text grids (`w:tbl`, `w:tr`, `w:tc`), embedded page images (`w:drawing`), and automatic OCR for scanned pages.
- The 20-document quality review (`docs/reviews/pdf-to-word-corpus.md`) measures paragraph flow, heading hierarchy, table structure, and editability across 20 diverse PDFs, fixes reconstruction defects found in `src/core/docx_writer.cpp`, and verifies that the Convert UI hint line states the measured capabilities honestly.

**Adjacency:** all=not-applicable (a converter engine and its quality evaluation inside `D04 T01`'s Convert frame; queue, notifications, and settings belong to `D04 T01` and `D02 T04 §2`)

**Adjacency rationale:** The Word document card, step 3 options panel, queue entry, and completion toast all live inside `D04 T01`'s Convert page and `JobQueue`. This file owns the native C++20 `.docx` generator and the 20-document M5 quality gate.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Native C++20 OpenXML `.docx` ZIP writer and layout/table reconstructor | D04 T02 §1, D03 T01 §1, D03 T01 §4 | [ ] |
| 2 | §2 | Convert UI wiring, 20-document quality review and layout tuning | §1, D04 T01 §2 | [ ] |

## 1. Native C++20 OpenXML `.docx` ZIP writer and layout/table reconstructor

Implement `src/core/zip_archive.cpp` (a compact C++20 ZIP archive reader/writer using `recto-pdf`'s RFC 1951 deflate codec and CRC-32, shared with `D04 T04 §1`) and `src/core/docx_writer.cpp` (`recto::core::DocxWriter`):
1. **Document structure & styles:** Emit `[Content_Types].xml`, `_rels/.rels`, `word/styles.xml` (`Normal`, `Heading1`..`Heading3`, `ListParagraph`, table styles), `word/document.xml` (`w:sectPr` with page size and margins derived from the PDF page box and text bounding margins), and `word/_rels/document.xml.rels`.
2. **Paragraph, heading, and column reconstruction:** Group `PageText` lines into flowing paragraphs (`w:p`) with line-spacing, left/right/first-line indentation, alignment (`left`, `center`, `right`, `both` justified), heading level inference from font-size/weight ratios, bullet/numbered list detection, and multi-column section breaks (`w:cols`).
3. **Table grid reconstruction:** Detect tables both from horizontal/vertical vector ruling paths on the page and from multi-row column-aligned text spans, emitting native WordprocessingML tables (`w:tbl`, `w:tblGrid`, `w:tr`, `w:tc` with cell widths and borders).
4. **Inline runs and images:** Emit `w:r` / `w:rPr` (font name, half-point `w:sz`, `w:b`, `w:i`, `w:color`) with XML-escaped `w:t` text, embed page images (`word/media/imageN.png` or `.jpg`) as `w:drawing` inline/anchored shapes, and run `OcrPipeline` automatically on scanned pages when the OCR option is enabled.
5. Expose `recto-cli pdf-to-docx <in.pdf> --out <out.docx> [--pages <range>] [--ocr]`.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/core/zip_archive.h/.cpp` (ZIP local headers, deflate/store entries, CRC-32, central directory, and bounded extraction for OpenXML) and the core `DocxWriter` XML emitter (`[Content_Types].xml`, `_rels`, `styles.xml`, `document.xml` paragraphs, runs, headings, and images). Done when: `DocxWriter.*` converts single-column and two-column text+image corpus PDFs into `.docx` files whose XML validates and whose unzipped `word/document.xml` contains every expected paragraph, style and media relationship.
2. Implement vector-rule and aligned-grid table detection (`w:tbl`) and scanned-page OCR integration in `src/core/docx_writer.cpp`, plus `recto-cli pdf-to-docx`. Done when: converting a table-bearing PDF produces native `w:tbl`/`w:tc` cells in `word/document.xml`, converting `scanned.pdf` with `--ocr` produces editable paragraphs, and `AtomicFileWriter` protects output saves on cancellation.

- [ ] `zip_archive.cpp` and `docx_writer.cpp` generating valid ECMA-376 `.docx` packages with paragraphs, headings, runs, multi-column sections, tables (`w:tbl`), images, and OCR support. Done when: `DocxWriter.*` verifies ZIP structure, well-formed XML, paragraph text fidelity, and `w:tbl` cell extraction under ASan. Cheaper substitute: placing every line inside an absolute-positioned text frame, which makes the resulting Word document impossible to edit as flowing text.
- [ ] `recto-cli pdf-to-docx` command and atomic save cancellation proof. Done when: cancelling mid-conversion leaves zero partial `.docx` files.
- [ ] Commit: `convert: implement native C++20 DocxWriter with paragraph, column and table reconstruction`

**Test checkpoint:** `recto-tests.exe --gtest_filter=DocxWriter.*` passes under ASan across text, multi-column, table, image and scanned corpus files.

## 2. Convert UI wiring, 20-document quality review and layout tuning

Wire the **Word document** card and step 3 options (`explorations/B-Convert.png`: page range, OCR for scanned pages toggle, save location, file-exists behavior, and the honest hint line per SPEC principle 4) in `ConvertPage`, then assemble and run the foundation plan section 11 **20-document PDF -> Word evaluation corpus** (prose articles, two-column academic papers, financial tables, invoices, forms, mixed image+text brochures, and scanned pages), verify every output opens cleanly in Microsoft Word (or the built-in OpenXML reader), tune `DocxWriter` heuristics for any layout/table defects discovered, and record the per-document results in `docs/reviews/pdf-to-word-corpus.md`.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 2 (Principle 4: honest conversion), 9.5 (Word document card and step 3 options) with `design/screenshots/D-Convert.png` and `design/screenshots/explorations/B-Convert.png`.
**Job:** The user can convert PDFs into editable `.docx` files with clear expectations and verified quality on real-world documents. Consumer: Microsoft Word, LibreOffice Writer, and Recto's own `Office -> PDF` round-trip in `D04 T04`.
**Treatment:** `ConvertPage` Word options wired to `JobQueue` + empirical 20-document quality review and heuristic tuning in `src/core/docx_writer.cpp`. Cheaper substitute that fails the checkpoint: declaring `DocxWriter` done on 1 toy test file without running and grading the 20-document corpus required by foundation plan sections 7 and 11.
**Chrome:** consume the Convert step 2 card and step 3 options host from `D04 T01 §2`.

**Build order:**

1. Wire the **Word document** option panel in `ConvertPage` matching `explorations/B-Convert.png` (pages, OCR toggle, save location, file-exists handling, honest hint text). Done when: UI screenshot matches `B-Convert.png` and a queued job produces a `.docx` file with completion notification.
2. Run `recto-cli pdf-to-docx` across the 20-document evaluation set, verify every output `.docx` opens without repair errors, fix paragraph/column/table reconstruction issues found in `src/core/docx_writer.cpp`, and commit the scored 20-document table in `docs/reviews/pdf-to-word-corpus.md` (updating foundation plan section 11 item 3). Done when: all 20 documents convert cleanly and `docs/reviews/pdf-to-word-corpus.md` records per-file scores and any honest limitations reflected in the UI hint line.

- [ ] Word document card and step 3 options (`explorations/B-Convert.png`) live in `ConvertPage`. Done when: display-session screenshot matches `B-Convert.png` and job progress/cancellation works end to end. Cheaper substitute: omitting the step 3 options or the SPEC principle 4 hint text.
- [ ] 20-document quality review (`docs/reviews/pdf-to-word-corpus.md`) completed with layout/table heuristics tuned in `src/core/docx_writer.cpp`. Done when: all 20 outputs open cleanly without corruption and the review table is committed.
- [ ] Commit: `convert: wire PDF to Word UI and complete 20-document DocxWriter quality review`

**Test checkpoint:** `recto-tests.exe --gtest_filter=DocxWriter.*` green under ASan, `docs/reviews/pdf-to-word-corpus.md` records all 20 documents passing validity and quality checks, and the UI screenshot matches `explorations/B-Convert.png`.

## Verification

- Every corpus PDF converted by `DocxWriter` produces a valid `.docx` ZIP package whose XML parses cleanly and opens without repair prompts.
- The 20-document evaluation record in `docs/reviews/pdf-to-word-corpus.md` is complete and foundation plan section 11 item 3 is closed.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
