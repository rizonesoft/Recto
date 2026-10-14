---
schema_version: 1
id: pdf-to-word
domain: 04-convert
status: active
title: "TODO-03 -- PDF to Word (DOCX and RTF): 3-Mode Native OpenXML/RTF Writer and Quality Review"
track: M5
---

# TODO-03 -- PDF to Word (DOCX and RTF): 3-Mode Native OpenXML/RTF Writer and Quality Review

> **Goal:** Build Recto's own native C++20 `PDF -> Word (.docx & .rtf)` converters (`DocxWriter` in `src/core/docx_writer.cpp` and `RtfWriter` in `src/core/rtf_writer.cpp`) with three reconstruction modes (**Flowing** editable paragraphs/lists/tables/columns, **Exact** fixed-position frames for brochures/forms, and **Continuous** single-stream layout), orthogonal-line + aligned-grid table reconstruction, font embedding/mapping, image DPI controls, and automatic OCR for scanned pages and vector-outlined text, then evaluate and tune reconstruction quality across the foundation plan's 20-document corpus.

> **Current state:** Measured 2026-10-14. No DOCX or RTF writer exists yet. Recto owns the entire `PDF -> DOCX / RTF` pipeline in-house in C++20: `D01` provides vector paths, fonts and image XObjects, `D03 T01` provides `PageText` (`§1`) and `OcrPipeline` (`§4`, including vector-outlined text OCR), and `D04 T02` provides `Flate`/ZIP compression (`§1`) and `TableExtractor` (`§5`).

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D7), 5 (`PDF -> Word (.docx & .rtf)` row: native C++20 `DocxWriter` and `RtfWriter` with Flowing, Exact, and Continuous modes), 7 (M5 done-when), 11 (20-document PDF -> Word test corpus).
- `design/SPEC.md` sections 2 (Principle 4: "Honest conversion. Say what a conversion is good at and what it is not (PDF to Word works best on text documents)"), 9.5 (Word document `.docx`/`.rtf` card, step 3 options including `Flowing` / `Exact` / `Continuous` mode selector, table detection, image handling, OCR for scanned and vector-outlined text, hint text), and `design/screenshots/explorations/B-Convert.png`.
- ECMA-376 WordprocessingML (`[Content_Types].xml`, `_rels/.rels`, `word/document.xml`, `word/styles.xml`, `word/numbering.xml`, `word/header*.xml`, `word/footer*.xml`, `word/_rels/document.xml.rels`, `word/media/*`) and Rich Text Format (RTF 1.9.1) specification.
- -> XREF: D03 T01 §1 -- `DocxWriter` and `RtfWriter` consume the `PageText` blocks, lines, spans, font attributes and character quads produced by `D03 T01 §1` (and OCR-populated `PageText` from `D03 T01 §4`).
- -> XREF: D04 T01 §2 -- plugs into the Convert page's **Word document** card, step 3 options (`explorations/B-Convert.png`), and `JobQueue`.

## Outcome

- `DocxWriter` (`src/core/docx_writer.cpp`) and `RtfWriter` (`src/core/rtf_writer.cpp`) write valid ECMA-376 `.docx` ZIP packages and RTF 1.9.1 `.rtf` documents that open cleanly in Microsoft Word and LibreOffice Writer without repair prompts across all three reconstruction modes:
  1. **Flowing (Editable Paragraphs, Tables & Lists -- default):** Reconstructs reading order, multi-column section breaks (`w:cols`), headings (`Heading1`..`Heading3`), flowing paragraphs (`w:p`), bulleted/numbered lists (`w:numbering`), running headers/footers, and native tables (`w:tbl`, `w:tr`, `w:tc` with merged cells and borders).
  2. **Exact (Fixed-Position Frames):** Preserves exact 2D visual layout using positioned frames (`w:framePr` / DrawingML textboxes in `.docx` and `\absw`/`\posx`/`\posy` frames in `.rtf`) for complex brochures, forms and certificates.
  3. **Continuous:** Single continuous document stream without hard page breaks.
- The 20-document quality review (`docs/reviews/pdf-to-word-corpus.md`) measures paragraph flow, heading hierarchy, table structure, and editability across 20 diverse PDFs, fixes reconstruction defects found in `src/core/docx_writer.cpp` and `src/core/rtf_writer.cpp`, and verifies that the Convert UI hint line states the measured capabilities honestly.

**Adjacency:** all=not-applicable (converter engines and their quality evaluation inside `D04 T01`'s Convert frame; queue, notifications, and settings belong to `D04 T01` and `D02 T04 §2`)

**Adjacency rationale:** The Word document card, step 3 options panel, queue entry, and completion toast all live inside `D04 T01`'s Convert page and `JobQueue`. This file owns the native C++20 `.docx` and `.rtf` generators and the 20-document M5 quality gate.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Native C++20 `.docx` and `.rtf` writers (Flowing, Exact, Continuous) and table reconstructor | D04 T02 §1, D04 T02 §5, D03 T01 §1, D03 T01 §4 | [ ] |
| 2 | §2 | Convert UI wiring, 20-document quality review and layout tuning | §1, D04 T01 §2 | [ ] |

## 1. Native C++20 `.docx` and `.rtf` writers (Flowing, Exact, Continuous) and table reconstructor

Implement `src/core/zip_archive.cpp` (a compact C++20 ZIP archive reader/writer using `recto-pdf`'s RFC 1951 deflate codec and CRC-32, shared with `D04 T04 §1`), `src/core/docx_writer.cpp` (`recto::core::DocxWriter`), and `src/core/rtf_writer.cpp` (`recto::core::RtfWriter`):
1. **Document structure & styles:** Emit `[Content_Types].xml`, `_rels/.rels`, `word/styles.xml` (`Normal`, `Heading1`..`Heading3`, `ListParagraph`, table styles), `word/numbering.xml`, `word/header*.xml`, `word/footer*.xml`, `word/document.xml` (`w:sectPr` with page size and margins derived from the PDF page box and text bounding margins), and `word/_rels/document.xml.rels` for `.docx`; emit RTF 1.9.1 headers (`\rtf1\ansi\deff0`, `\fonttbl`, `\colortbl`, `\stylesheet`, `\paperw`, `\paperh`) for `.rtf`.
2. **Three reconstruction modes (`Flowing`, `Exact`, `Continuous`):**
   - **Flowing:** Group `PageText` lines into flowing paragraphs (`w:p` / `\pard`) with line-spacing, left/right/first-line indentation, alignment (`left`, `center`, `right`, `both` justified), heading level inference from font-size/weight ratios, bullet/numbered list detection, running header/footer detection, and multi-column section breaks (`w:cols`).
   - **Exact:** Emit positioned text frames (`w:framePr` / DrawingML textboxes in `.docx`, `\posx`/`\posy` frames in `.rtf`) and anchored graphics matching the PDF coordinates within `<= 0.5 pt`.
   - **Continuous:** Emit flowing paragraphs and tables continuously without section/page breaks.
3. **Table grid reconstruction:** Consume `TableExtractor` (`D04 T02 §5`) to detect tables both from horizontal/vertical orthogonal vector ruling paths and from multi-row column-aligned text spans, emitting native WordprocessingML tables (`w:tbl`, `w:tblGrid`, `w:tr`, `w:tc` with `w:gridSpan`/`w:vMerge` merged cells, widths, borders, and cell shading) and RTF tables (`\trowd`, `\cellx`, `\intbl`, `\cell`, `\row`).
4. **Inline runs, fonts, images, and vector-outlined text OCR:** Emit `w:r` / `w:rPr` (mapped system font family, half-point `w:sz`, `w:b`, `w:i`, `w:u`, `w:strike`, `w:color`, `w:shd`), embed or downsample page images (`word/media/imageN.png` or `.jpg` / RTF `\pict\pngblip` or `\jpegblip`), and run `OcrPipeline` (`D03 T01 §4`) automatically on scanned pages and vector-outlined text paths when enabled.
5. Expose `recto-cli pdf-to-docx <in.pdf> --out <out.docx> [--mode <flowing|exact|continuous>] [--pages <range>] [--ocr]` and `recto-cli pdf-to-rtf <in.pdf> --out <out.rtf> [--mode <flowing|exact|continuous>] [--pages <range>] [--ocr]`.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/core/zip_archive.h/.cpp` and the 3-mode `DocxWriter` (`src/core/docx_writer.cpp`) and `RtfWriter` (`src/core/rtf_writer.cpp`) emitters (paragraphs, runs, headings, lists, headers/footers, multi-column sections, positioned `Exact` frames, and images). Done when: `DocxWriter.*` and `RtfWriter.*` convert single-column, two-column and brochure corpus PDFs in `Flowing`, `Exact` and `Continuous` modes into valid `.docx` and `.rtf` files.
2. Integrate `TableExtractor` (`w:tbl` and `\trowd` with merged cells, borders and shading) and scanned/vector-outlined OCR in `src/core/docx_writer.cpp` and `src/core/rtf_writer.cpp`, plus `recto-cli pdf-to-docx` and `recto-cli pdf-to-rtf`. Done when: converting a table-bearing PDF produces native `w:tbl`/`w:tc` and RTF table cells, converting `scanned.pdf` or vector-outlined text with `--ocr` produces editable paragraphs, and `AtomicFileWriter` protects output saves on cancellation.

- [ ] `zip_archive.cpp`, `docx_writer.cpp`, and `rtf_writer.cpp` generating valid ECMA-376 `.docx` and RTF 1.9.1 `.rtf` documents across `Flowing`, `Exact`, and `Continuous` modes with headings, lists, multi-column sections, tables (`w:tbl` / `\trowd`), images, and OCR support. Done when: `DocxWriter.*` and `RtfWriter.*` pass under ASan. Cheaper substitute: implementing only one mode or omitting `.rtf` and native table cells.
- [ ] `recto-cli pdf-to-docx` and `recto-cli pdf-to-rtf` commands and atomic save cancellation proof. Done when: cancelling mid-conversion leaves zero partial `.docx` or `.rtf` files.
- [ ] Commit: `convert: implement 3-mode native C++20 DocxWriter and RtfWriter with table reconstruction`

**Test checkpoint:** `recto-tests.exe --gtest_filter=DocxWriter.*:RtfWriter.*` passes under ASan across text, multi-column, table, image, brochure and scanned corpus files.

## 2. Convert UI wiring, 20-document quality review and layout tuning

Wire the **Word document (`.docx` & `.rtf`)** card and step 3 options (`SPEC 9.5` and `explorations/B-Convert.png`: output format `.docx` / `.rtf`, reconstruction mode `Flowing` / `Exact` / `Continuous`, detect & reconstruct tables toggle, include images & vector graphics toggle, OCR for scanned pages & vector-outlined text toggle, page range, save location, file-exists behavior, and the honest hint line per SPEC principle 4) in `ConvertPage`, then assemble and run the foundation plan section 11 **20-document PDF -> Word evaluation corpus** (prose articles, two-column academic papers, financial tables, invoices, forms, mixed image+text brochures, and scanned pages), verify every output opens cleanly in Microsoft Word (or the built-in OpenXML/RTF reader), tune `DocxWriter` and `RtfWriter` heuristics for any layout/table defects discovered, and record the per-document results in `docs/reviews/pdf-to-word-corpus.md`.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 2 (Principle 4: honest conversion), 9.5 (Word document card and step 3 options) with `design/screenshots/D-Convert.png` and `design/screenshots/explorations/B-Convert.png`.
**Job:** The user can convert PDFs into editable `.docx` or `.rtf` files in `Flowing`, `Exact` or `Continuous` mode with clear expectations and verified quality on real-world documents. Consumer: Microsoft Word, LibreOffice Writer, and Recto's own `Office -> PDF` round-trip in `D04 T04`.
**Treatment:** `ConvertPage` Word options wired to `JobQueue` + empirical 20-document quality review and heuristic tuning in `src/core/docx_writer.cpp` and `src/core/rtf_writer.cpp`. Cheaper substitute that fails the checkpoint: declaring `DocxWriter` done on 1 toy test file without running and grading the 20-document corpus required by foundation plan sections 7 and 11.
**Chrome:** consume the Convert step 2 card and step 3 options host from `D04 T01 §2`.

**Build order:**

1. Wire the **Word document** option panel in `ConvertPage` matching `SPEC 9.5` and `explorations/B-Convert.png` (`.docx`/`.rtf` format selector, `Flowing`/`Exact`/`Continuous` mode selector, table/image/OCR toggles, pages, save location, file-exists handling, honest hint text). Done when: UI screenshot matches `B-Convert.png` and queued jobs produce `.docx` and `.rtf` files with completion notifications.
2. Run `recto-cli pdf-to-docx` and `recto-cli pdf-to-rtf` across the 20-document evaluation set, verify every output opens without repair errors, fix paragraph/column/table reconstruction issues found in `src/core/docx_writer.cpp` and `src/core/rtf_writer.cpp`, and commit the scored 20-document table in `docs/reviews/pdf-to-word-corpus.md` (updating foundation plan section 11 item 3). Done when: all 20 documents convert cleanly and `docs/reviews/pdf-to-word-corpus.md` records per-file scores and any honest limitations reflected in the UI hint line.

- [ ] Word document (`.docx` & `.rtf`) card and step 3 options (`Flowing` / `Exact` / `Continuous`, table/image/OCR toggles, `explorations/B-Convert.png`) live in `ConvertPage`. Done when: display-session screenshot matches `B-Convert.png` and job progress/cancellation works end to end. Cheaper substitute: omitting the reconstruction mode selector or the SPEC principle 4 hint text.
- [ ] 20-document quality review (`docs/reviews/pdf-to-word-corpus.md`) completed with layout/table heuristics tuned in `src/core/docx_writer.cpp` and `src/core/rtf_writer.cpp`. Done when: all 20 outputs open cleanly without corruption and the review table is committed.
- [ ] Commit: `convert: wire 3-mode PDF to Word/RTF UI and complete 20-document quality review`

**Test checkpoint:** `recto-tests.exe --gtest_filter=DocxWriter.*:RtfWriter.*` green under ASan, `docs/reviews/pdf-to-word-corpus.md` records all 20 documents passing validity and quality checks, and the UI screenshot matches `explorations/B-Convert.png`.

## Verification

- Every corpus PDF converted by `DocxWriter` and `RtfWriter` produces a valid `.docx` or `.rtf` file that parses cleanly and opens without repair prompts.
- The 20-document evaluation record in `docs/reviews/pdf-to-word-corpus.md` is complete and foundation plan section 11 item 3 is closed.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
