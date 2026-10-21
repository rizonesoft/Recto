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

> **Current state:** Measured 2026-10-14. No DOCX or RTF writer exists yet. Recto owns the entire `PDF -> DOCX / RTF` pipeline in-house in C++20: `D01` provides vector paths, fonts and image XObjects, `D03 T01` provides `PageText` (`§1`) and `OcrPipeline` (`§8`, including vector-outlined text OCR), and `D04 T02` provides `AtomicFileWriter` (`§1`), `Flate` compression (`§2`), and `TableExtractor` (`§8`).

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D7), 5 (`PDF -> Word (.docx & .rtf)` row: native C++20 `DocxWriter` and `RtfWriter` with Flowing, Exact, and Continuous modes), 7 (M5 done-when), 11 (20-document PDF -> Word test corpus).
- `design/SPEC.md` sections 2 (Principle 4: `"Honest conversion. Say what a conversion is good at and what it is not (PDF to Word works best on text documents)"`), 9.5 (Word document `.docx`/`.rtf` card, Step 3 options including `Flowing` / `Exact` / `Continuous` mode selector, table detection, image handling, OCR for scanned and vector-outlined text, hint text), and `design/screenshots/explorations/B-Convert.png`.
- ECMA-376 WordprocessingML (`[Content_Types].xml`, `_rels/.rels`, `word/document.xml`, `word/styles.xml`, `word/numbering.xml`, `word/header*.xml`, `word/footer*.xml`, `word/_rels/document.xml.rels`, `word/media/*`) and Rich Text Format (RTF 1.9.1) specification.
- -> XREF: D02 T04 §5 -- the `Default PDF to Word reconstruction mode` row in Settings (`D02 T04 §5`) seeds §2 and §5's default mode (`Flowing`, `Exact`, or `Continuous`).
- -> XREF: D03 T01 §1 -- `DocxWriter` and `RtfWriter` consume the `PageText` blocks, lines, spans, font attributes and character quads produced by `D03 T01 §1`.
- -> XREF: D03 T01 §8 -- `DocxWriter` and `RtfWriter` consume OCR-populated `PageText` on scanned and vector-outlined pages from `D03 T01 §8`.
- -> XREF: D04 T01 §4 -- plugs into the Convert page's **Word document** card, Step 3 options (`explorations/B-Convert.png`), and `ConversionRunner`.
- -> XREF: D04 T02 §1 -- `zip_archive.cpp` (§1) and `DocxWriter`/`RtfWriter` commit output packages through `D04 T02 §1`'s `AtomicFileWriter`.
- -> XREF: D04 T02 §8 -- `DocxWriter` (§3) and `RtfWriter` (§4) consume `TableExtractor` grids from `D04 T02 §8`.
- -> XREF: D04 T04 §1 -- `office_to_pdf.cpp` (`D04 T04 §1`) shares the C++20 `zip_archive.h/.cpp` reader/writer built in §1 here.
- -> XREF: D04 T05 §1 -- `XlsxWriter` (`D04 T05 §1`) and `WebDocWriter` (`D04 T05 §4` EPUB 3) share the `zip_archive.h/.cpp` reader/writer built in §1 here.
- -> XREF: D06 T01 §6 -- `RoundTrips.DocumentsAndSecurity` (`D06 T01 §6`) exercises `DocxWriter` (`§2`, `§3`) and `RtfWriter` (`§4`) built here.
- -> XREF: D07 T02 §4 -- the local diagnostic crash-report `.zip` bundler (`D07 T02 §4`) reuses `zip_archive.h/.cpp` built in §1 here.
- -> XREF: D04 T04 §8 -- ZIP reader.
- -> XREF: D03 T03 §28 -- DocxWriter write_blocks.

## Outcome

- `zip_archive.cpp` (`§1`) reads and writes standard PKZIP archives using `recto-pdf`'s RFC 1951 deflate codec and CRC-32 with zip-bomb decompression bounds.
- `DocxWriter` (`src/core/docx_writer.cpp`, `§2`, `§3`) and `RtfWriter` (`src/core/rtf_writer.cpp`, `§4`) write valid ECMA-376 `.docx` ZIP packages and RTF 1.9.1 `.rtf` documents that open cleanly in Microsoft Word and LibreOffice Writer without repair prompts across all three reconstruction modes:
  1. **Flowing (Editable Paragraphs, Tables & Lists -- default):** Reconstructs reading order, multi-column section breaks (`w:cols`), headings (`Heading1`..`Heading3`), flowing paragraphs (`w:p`), bulleted/numbered lists (`w:numbering`), running headers/footers, and native tables (`w:tbl`, `w:tr`, `w:tc` with merged cells and borders).
  2. **Exact (Fixed-Position Frames):** Preserves exact 2D visual layout using positioned frames (`w:framePr` / DrawingML textboxes in `.docx` and `\absw`/`\posx`/`\posy` frames in `.rtf`) for complex brochures, forms and certificates.
  3. **Continuous:** Single continuous document stream without hard page breaks.
- The Convert page's **Word document (`.docx` & `.rtf`)** card (`§5`) exposes all options matching `explorations/B-Convert.png`, and the 20-document quality review (`docs/reviews/pdf-to-word-corpus.md`, `§6`) measures and tunes paragraph flow, heading hierarchy, table structure, and editability across 20 diverse PDFs.

**Adjacency:** all=not-applicable (converter engines and their quality evaluation inside `D04 T01`'s Convert frame; queue, notifications, and settings belong to `D04 T01` and `D02 T04 §5`)

**Adjacency rationale:** The Word document card, Step 3 options panel, queue entry, and completion toast all live inside `D04 T01`'s Convert page and `ConversionRunner`. This file owns the C++20 ZIP package layer, the `.docx` and `.rtf` generators, and the 20-document M5 quality gate.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | C++20 ZIP archive reader/writer (`zip_archive.cpp`) and ECMA-376 OpenXML scaffold | D04 T02 §1, D04 T02 §2 | [ ] |
| 2 | §2 | `DocxWriter` **Flowing** and **Continuous** layout engines (paragraphs, lists, `w:cols`) | §1, §7, D03 T01 §1, D03 T01 §8 | [ ] |
| 3 | §3 | `DocxWriter` **Exact** frame mode, `w:tbl` table reconstruction, images & `pdf-to-docx` | §2, D04 T02 §8 | [ ] |
| 4 | §4 | Native RTF 1.9.1 writer (`rtf_writer.cpp`: `Flowing`, `Exact` frames, `\trowd` tables) | §3, D04 T04 §1 | [ ] |
| 5 | §5 | Convert page **Word document (`.docx` & `.rtf`)** card and Step 3 UI (`B-Convert.png`) | §4, D04 T01 §4, D02 T04 §5 | [ ] |
| 6 | §7 | 20-document PDF -> Word evaluation corpus, assembled before M5 | D00 T03 §3 | [ ] |
| 7 | §6 | Foundation plan section 11 **20-document PDF -> Word quality review** and layout tuning | §5, §7, D04 T04 §1 | [ ] |

## 1. C++20 ZIP archive reader/writer (`zip_archive.cpp`) and ECMA-376 OpenXML scaffold

Implement `src/core/zip_archive.h` and `src/core/zip_archive.cpp` in `recto-core`: a self-contained C++20 PKZIP reader and writer using `recto-pdf`'s RFC 1951 raw deflate codec and IEEE 802.3 CRC-32 (shared across `.docx` here, `.xps` in `D04 T02 §4`, `.docx`/`.xlsx`/`.pptx` reader in `D04 T04 §1`, and `.xlsx`/`.epub` in `D04 T05`), enforcing a `256 MB` decompression-bomb cap and path-traversal (`..`) rejection when reading untrusted archives, plus the ECMA-376 OpenXML package builder (`[Content_Types].xml`, `_rels/.rels`, XML escaping helpers, and `AtomicFileWriter` commit).

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `ZipWriter` (supporting both `Store` method `0` for EPUB `mimetype` / pre-compressed JPEG media and `Deflate` method `8` for XML streams, writing local file headers, central directory headers, and End of Central Directory record) and `ZipReader` (central directory parser, entry lookup, bounded decompression, and `..` path sanitization) in `src/core/zip_archive.cpp`. Done when: `ZipArchive.*` round-trips multi-entry archives (stored + deflated) and rejects zip bombs and `../` traversal paths under ASan.
2. Implement `OpenXmlPackageBuilder` in `src/core/openxml_package.h/.cpp` managing content-type registrations, package/part `.rels` relationships, and deterministic UTF-8 XML element/attribute escaping. Done when: `ZipArchive.OpenXmlScaffold` builds a minimal `.docx` package that passes ECMA-376 part and relationship validation.

- [ ] Implement IEEE 802.3 `crc32_compute(span<const uint8_t>)` and `ZipWriter` (`add_stored_entry`, `add_deflated_entry`, `finalize_to_atomic_file`) in `src/core/zip_archive.h/.cpp`. Done when: `ZipWriter` writes valid PKZIP (`PK\x03\x04`, `PK\x01\x02`, `PK\x05\x06`) archives readable by Windows Explorer and `Expand-Archive`.
- [ ] Implement `ZipReader` (`open_file`, `open_bytes`, `list_entries`, `read_entry(name, max_uncompressed_bytes = 256 MB)`) in `src/core/zip_archive.cpp`. Done when: `ZipReader` extracts stored and deflated entries byte-identically.
- [ ] Enforce hostile-archive defenses in `ZipReader`: reject entry names containing `..` segments or leading `/` / `\`, cap total entry count (`<= 65535`), and abort decompression immediately with `PdfErrorCode::LimitExceeded` if uncompressed output exceeds `max_uncompressed_bytes` (`256 MB`) regardless of what the header claims. Done when: `ZipArchive.HostileBombAndTraversal` rejects a `1000:1` zip bomb and a `../../evil.dll` entry under ASan. Cheaper substitute: trusting `uncompressed_size` in the local file header without bounding actual decompressed bytes.
- [ ] Implement `XmlWriter` in `src/core/openxml_package.h/.cpp` handling UTF-8 XML prolog (`<?xml version="1.0" encoding="UTF-8" standalone="yes"?>`), attribute/text entity escaping (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&apos;`), and stripping illegal XML 1.0 control characters (`0x00..0x08`, `0x0B`, `0x0C`, `0x0E..0x1F`) so stray PDF control codes never corrupt Word XML. Done when: `ZipArchive.XmlControlCharSanitization` proves control characters never produce malformed XML.
- [ ] Implement `OpenXmlPackageBuilder` assembling `[Content_Types].xml`, `_rels/.rels`, and part-level `_rels/<part>.rels` with sequential `rId1..rIdN` relationship allocation. Done when: `OpenXmlPackageBuilder` serializes deterministic relationship tables.
- [ ] Add `ZipArchive.*` unit tests in `tests/recto-tests/zip_archive_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ZipArchive.*` passes under ASan.

- [ ] Commit: c++20 ZIP archive reader/writer (`src/core/zip_archive.cpp`) and ECMA-376 OpenXML package scaffold

**Test checkpoint:** `recto-tests.exe --gtest_filter=ZipArchive.*` passes under ASan.

## 2. `DocxWriter` **Flowing** and **Continuous** layout engines (paragraphs, lists, `w:cols`)

Implement the **Flowing** and **Continuous** reconstruction modes of `DocxWriter` in `src/core/docx_writer.h/.cpp`: analyze `PageText` (`D03 T01 §1`, plus OCR-populated `PageText` from `D03 T01 §8` on scanned/vector-outlined pages) across the document to detect running headers and footers (`word/header1.xml`, `word/footer1.xml`), infer document typography and heading hierarchy (`Title`, `Heading1`..`Heading3`, `Normal`, `ListParagraph` in `word/styles.xml`), reconstruct bulleted and numbered lists (`word/numbering.xml`), coalesce wrapped lines into flowing paragraphs (`w:p`) with accurate line spacing, left/right/first-line indentation (`w:ind`), and alignment (`left`, `center`, `right`, `both` justified), and emit multi-column section breaks (`w:sectPr` `w:cols`) in **Flowing** mode or a single unbroken stream in **Continuous** mode.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement document-wide layout analysis in `src/core/docx_writer.cpp`: modal body font/size estimation, heading level classification (`Heading1`..`Heading3` from font size/weight ratios and vertical spacing), running header/footer identification (top/bottom margin bands repeating across `>= 3` pages), and bullet/numbered list marker recognition. Done when: `DocxWriter.FlowingStructure` classifies titles, H1-H3 headings, bulleted/numbered lists, and running headers/footers on prose and academic corpus PDFs.
2. Implement `Flowing` (with page/column `w:sectPr` breaks, `w:pgSz`, `w:pgMar`, and `w:cols`) and `Continuous` (single continuous stream with no hard page breaks) WordprocessingML emission (`word/document.xml`, `word/styles.xml`, `word/numbering.xml`, `word/header1.xml`, `word/footer1.xml`). Done when: `DocxWriter.FlowingAndContinuous` produces valid ECMA-376 `.docx` packages with well-formed XML and relationship tables under ASan (full `D04 T04 §1` round-trip verified in `§4` and `§6`).

- [ ] Define `WordReconstructionMode` (`Flowing`, `Exact`, `Continuous`) and `PdfToWordOptions` (`mode`, `reconstruct_tables`, `include_images`, `ocr_scanned_and_outlined`, `ocr_language_bcp47`, `page_range`) in `src/core/docx_writer.h`. Done when: `src/core/docx_writer.h` compiles cleanly across `recto-core`, `recto-services`, and `recto-cli`.
- [ ] Detect running headers and footers by matching top/bottom margin text blocks (`top 8%` and `bottom 8%` of page height) across multiple pages (allowing varying page numbers `1`, `2`, `Page N of M`), emitting them into `word/header1.xml` and `word/footer1.xml` with `w:fldSimple w:instr="PAGE"` rather than repeating them inside body text on every page. Done when: `DocxWriter.RunningHeadersFooters` moves repeating page headers/footers out of body paragraphs into native Word headers/footers.
- [ ] Infer paragraph styles (`Normal`, `Heading1`, `Heading2`, `Heading3`) from each block's font size ratio relative to the document's modal body font size, bold weight, and preceding vertical whitespace, writing matching style definitions in `word/styles.xml`. Done when: `DocxWriter.HeadingInference` emits `<w:pStyle w:val="Heading1"/>`..`Heading3` on structured articles.
- [ ] Detect bulleted (`•`, `-`, `*`, `▪`) and numbered (`1.`, `1)`, `(a)`, `i.`, `A.`) list items and emit `word/numbering.xml` (`w:abstractNum`, `w:num`) and `<w:numPr><w:ilvl/><w:numId/></w:numPr>` on list paragraphs while stripping the hard-coded prefix glyph. Done when: `DocxWriter.Lists` converts bulleted and numbered PDF lists into native editable Word lists.
- [ ] Reconstruct paragraph properties (`w:pPr`): alignment (`w:jc` `left`, `center`, `right`, `both` when both left and right edges of multi-line blocks align within `3 pt`), indentation (`w:ind` `w:left`, `w:right`, `w:firstLine` / `w:hanging` in twentieths of a point `dxa`), and spacing (`w:spacing` `w:before`, `w:after`, `w:line`), plus inline run properties (`w:rPr`: `w:rFonts` mapped to clean Windows font family names, half-point `w:sz`, `w:b`, `w:i`, `w:u`, `w:strike`, `w:color`). Done when: `DocxWriter.ParagraphAndRunFormatting` preserves bold/italic/colour spans and justified/indented paragraphs. Cheaper substitute: emitting every PDF line as a separate `<w:p>` with `<w:br/>` so editing a sentence in Word does not reflow the paragraph.
- [ ] Emit multi-column section properties (`<w:sectPr><w:cols w:num="2" w:space="..."/></w:sectPr>`) when transitioning between full-width title blocks and 2-column or 3-column body layouts in `Flowing` mode, and suppress hard page/section breaks in `Continuous` mode. Done when: `DocxWriter.TwoColumnAndContinuous` verifies `w:cols w:num="2"` in `Flowing` mode and zero page breaks in `Continuous` mode.
- [ ] Automatically invoke `OcrPipeline` (`D03 T01 §8`) on `ScannedRaster` and `VectorOutlinedText` pages when `ocr_scanned_and_outlined == true`. Done when: `DocxWriter.OcrScannedPage` converts `tests/corpus/text/scanned.pdf` into editable flowing Word paragraphs under ASan.

- [ ] Commit: `DocxWriter` **Flowing** and **Continuous** layout engines (paragraphs, headings, lists, headers/footers, multi-column `w:cols`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=DocxWriter.Flowing*:DocxWriter.Heading*:DocxWriter.Lists:DocxWriter.Paragraph*:DocxWriter.TwoColumn*:DocxWriter.Ocr*` passes under ASan.

## 3. `DocxWriter` **Exact** frame mode, `w:tbl` table reconstruction, images & `pdf-to-docx`

Complete `DocxWriter` in `src/core/docx_writer.cpp`:
1. **Native Table Reconstruction (`w:tbl`):** Consume `TableExtractor` (`D04 T02 §8`) to emit native WordprocessingML tables (`w:tbl`, `w:tblGrid`, `w:gridCol`, `w:tr`, `w:tc`) with `w:gridSpan` horizontal merges, `w:vMerge` (`restart` / `continue`) vertical merges, cell borders (`w:tcBorders`), background shading (`w:shd`), and cell padding at their exact reading-order position in `Flowing` and `Continuous` modes.
2. **Exact (Fixed-Position Frames) Mode:** Emit positioned paragraph frames (`w:framePr` with exact `w:x`, `w:y`, `w:w`, `w:h` in `dxa` relative to `w:hAnchor="page"` and `w:vAnchor="page"`) and anchored DrawingML graphics so complex brochures, forms, and certificates preserve their 2D layout within `<= 0.5 pt`.
3. **Embedded Images & Vector Graphics:** Extract page images (`word/media/imageN.png` or `.jpg` via `D04 T02 §9`), register relationships in `word/_rels/document.xml.rels`, and emit inline (`wp:inline`) or anchored (`wp:anchor`) DrawingML `<w:drawing>` elements in EMU (`1 pt = 12700 EMU`), plus `recto-cli pdf-to-docx`.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Integrate `TableExtractor` (`D04 T02 §8`) into `DocxWriter` (`w:tbl` with `w:gridSpan`, `w:vMerge`, borders, and shading, excluding table-interior text spans from duplicate paragraph emission) and add `word/media/*` image embedding (`wp:inline` and `wp:anchor`). Done when: `DocxWriter.TablesAndImages` converts bordered and borderless table PDFs and illustrated PDFs into `.docx` files with native `<w:tbl>` cells and embedded `word/media/*` images.
2. Implement `WordReconstructionMode::Exact` (`w:framePr` page-anchored frames) and `recto-cli pdf-to-docx <in.pdf> --out <out.docx> [--mode flowing|exact|continuous] [--pages <range>] [--ocr]`. Done when: `DocxWriter.ExactMode` and `Cli.PdfToDocx` pass under ASan.

- [ ] Integrate `TableExtractor::extract_tables_on_page` (`D04 T02 §8`) into `DocxWriter`, marking `PageText` blocks inside detected table bounding boxes as consumed so table text is emitted once inside `<w:tbl>` and never duplicated as body paragraphs. Done when: `DocxWriter.TablesNoDuplicateText` verifies table cell strings appear inside `<w:tc>` and nowhere else on the page.
- [ ] Emit ECMA-376 `<w:tbl>` elements with `<w:tblGrid><w:gridCol w:w="..."/></w:tblGrid>`, `<w:tr>`, `<w:tc>`, `<w:tcPr>` (`<w:tcW>`, `<w:gridSpan w:val="N"/>` for `col_span > 1`, `<w:vMerge w:val="restart"/>` and `<w:vMerge/>` for `row_span > 1`, `<w:tcBorders>`, and `<w:shd w:fill="RRGGBB"/>`). Done when: `DocxWriter.TablesWithMergedCells` converts a table with `colspan` and `rowspan` merges into valid WordprocessingML `<w:tbl>`. Cheaper substitute: tab-separated plain text paragraphs instead of real `<w:tbl>` elements.
- [ ] Implement `WordReconstructionMode::Exact` in `src/core/docx_writer.cpp`: emit each text line/block as a page-anchored `<w:p><w:pPr><w:framePr w:w="..." w:h="..." w:hSpace="0" w:vSpace="0" w:wrap="notBeside" w:hAnchor="page" w:vAnchor="page" w:x="..." w:y="..."/></w:pPr>...</w:p>` with `dxa` (`20 * pt`) coordinates matching the PDF page box within `<= 0.5 pt` (`10 dxa`). Done when: `DocxWriter.ExactModeCoordinates` verifies `w:x` and `w:y` match source `PageText` quad coordinates within `<= 0.5 pt`.
- [ ] Embed page images into `word/media/imageN.png` or `.jpg`, register `http://schemas.openxmlformats.org/officeDocument/2006/relationships/image` entries in `word/_rels/document.xml.rels`, and emit `<w:drawing>` (`<wp:inline>` in `Flowing`/`Continuous` mode, `<wp:anchor behindDoc="1">` in `Exact` mode) with exact EMU dimensions (`pt * 12700`). Done when: `DocxWriter.EmbeddedImages` verifies `word/media/` image parts and `<a:blip r:embed="rId..."/>` links.
- [ ] Commit all `.docx` archives through `AtomicFileWriter` (`D04 T02 §1`) and respect `CancelToken` between pages. Done when: cancelling `DocxWriter` mid-document leaves zero partial `.docx` files on disk.
- [ ] Implement `recto-cli pdf-to-docx <in.pdf> --out <out.docx> [--mode flowing|exact|continuous] [--pages <range>] [--no-tables] [--no-images] [--ocr]` in `src/cli/cmd_pdf_to_docx.cpp`. Done when: `recto-tests.exe --gtest_filter=DocxWriter.*:Cli.PdfToDocx` passes under ASan.

- [ ] Commit: `DocxWriter` **Exact** fixed-position frame mode, native `w:tbl` table reconstruction, inline images, and `recto-cli pdf-to-docx`

**Test checkpoint:** `recto-tests.exe --gtest_filter=DocxWriter.*:Cli.PdfToDocx` passes under ASan across `Flowing`, `Exact`, and `Continuous` modes.

## 4. Native RTF 1.9.1 writer (`rtf_writer.cpp`: `Flowing`, `Exact` frames, `\trowd` tables)

Implement `src/core/rtf_writer.h/.cpp` (`recto::core::RtfWriter`) and `recto-cli pdf-to-rtf <in.pdf> --out <out.rtf> [--mode flowing|exact|continuous] [--pages <range>] [--ocr]`: generate standard Rich Text Format (RTF 1.9.1) documents (`\rtf1\ansi\ansicpg1252\uc1\deff0`, `\fonttbl`, `\colortbl`, `\stylesheet`, `\paperw`, `\paperh`, `\margl`, `\margr`, `\margt`, `\margb`) supporting all three reconstruction modes (`Flowing` paragraphs/lists/columns, `Exact` `\absw`/`\posx`/`\posy`/`\pvpg`/`\phpg` positioned frames, and `Continuous`), native RTF tables (`\trowd`, `\cellx`, `\clmgf`/`\clmrg`, `\clvmgf`/`\clvmrg`, `\intbl ... \cell`, `\row`), and inline `\pict\pngblip` / `\jpegblip` images.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `RtfWriter` in `src/core/rtf_writer.cpp` sharing §2's document layout analyzer and `TableExtractor` (`D04 T02 §8`), emitting RTF 1.9.1 headers, Unicode escapes (`\uN?`), `Flowing` / `Exact` / `Continuous` paragraphs and frames, `\trowd` tables, and `\pict` hex-encoded PNG/JPEG images via `AtomicFileWriter`. Done when: `RtfWriter.*` converts prose, multi-column, table, image, and non-ASCII Unicode corpus PDFs across all 3 modes into valid `.rtf` files that round-trip through `D04 T04 §1`'s RTF-to-PDF reader under ASan.
2. Implement `recto-cli pdf-to-rtf` in `src/cli/cmd_pdf_to_rtf.cpp`. Done when: `Cli.PdfToRtf` passes under ASan.

- [ ] Implement RTF 1.9.1 header generation in `src/core/rtf_writer.cpp`: `\fonttbl` mapping document fonts to `\f0..\fN`, `\colortbl` mapping text/shading/border RGB colours to `\cf1..\cfM` / `\cbpat`, `\stylesheet` (`Normal`, `heading 1`..`heading 3`), and page dimensions in twips (`\paperw`, `\paperh`, `\margl`, `\margr`, `\margt`, `\margb`). Done when: `RtfWriter.HeaderAndTables` produces well-formed balanced `{ ... }` RTF groups.
- [ ] Implement UTF-8 to RTF 1.9.1 signed-16-bit Unicode escaping (`\uc1\u<signed_int16>?` with UTF-16 surrogate-pair encoding for codepoints `> U+FFFF` and escaping of `\`, `{`, `}`) so CJK, accented European, mathematical, and currency glyphs never corrupt or get lost. Done when: `RtfWriter.UnicodeEscaping` round-trips accented, Cyrillic, Greek, and CJK strings losslessly.
- [ ] Emit `Flowing` and `Continuous` paragraphs (`\pard\plain`, `\ql`/`\qc`/`\qr`/`\qj`, `\li`/`\ri`/`\fi`, `\sb`/`\sa`/`\sl`, `\colsN` multi-column sections, and `\fN\fs<half_pt>\b\i\ul\strike\cfM` run formatting). Done when: `RtfWriter.FlowingAndContinuous` preserves paragraph alignment, indentation, and run styles.
- [ ] Emit `Exact` mode fixed-position frames using `\pard\pvpg\phpg\posx<twips>\posy<twips>\absw<twips>\absh<twips>\nowrap` anchored to page coordinates within `<= 0.5 pt` (`10 twips`). Done when: `RtfWriter.ExactFrames` verifies exact `\posx`/`\posy` frame coordinates. Cheaper substitute: ignoring `Exact` mode in `RtfWriter` or writing plain unformatted text with an `.rtf` extension.
- [ ] Emit native RTF tables (`\trowd\trgaph108`, cumulative right-edge `\cellx<twips>` definitions, `\clmgf`/`\clmrg` horizontal merges, `\clvmgf`/`\clvmrg` vertical merges, `\clbrdrt`/`\clbrdrb`/`\clbrdrl`/`\clbrdrr` borders, `\clcbpat` cell shading, `\intbl ... \cell`, `\row`). Done when: `RtfWriter.TablesWithMerges` converts a bordered and borderless table PDF into valid RTF table rows.
- [ ] Embed page images as `\pict\pngblip\picw<px>\pich<px>\picwgoal<twips>\pichgoal<twips> <lowercase_hex>` or `\jpegblip` groups and commit output via `AtomicFileWriter` (`D04 T02 §1`). Done when: `RtfWriter.EmbeddedImages` embeds PNG and JPEG images cleanly.
- [ ] Implement `recto-cli pdf-to-rtf <in.pdf> --out <out.rtf> [--mode flowing|exact|continuous] [--pages <range>] [--ocr]` in `src/cli/cmd_pdf_to_rtf.cpp` and add `RtfWriter.*` tests. Done when: `recto-tests.exe --gtest_filter=RtfWriter.*:Cli.PdfToRtf` passes under ASan.
- [ ] Commit: `convert: implement 3-mode native C++20 DocxWriter and RtfWriter with table reconstruction`

**Test checkpoint:** `recto-tests.exe --gtest_filter=DocxWriter.*:RtfWriter.*:Cli.PdfToDocx:Cli.PdfToRtf` passes under ASan.

## 5. Convert page **Word document (`.docx` & `.rtf`)** card and Step 3 UI (`B-Convert.png`)

Register the **Word document (`.docx` & `.rtf`)** card and build its Step 3 options control (`src/app/views/convert/WordConvertOptionsControl.xaml/.cpp`, `SPEC 9.5` and `design/screenshots/explorations/B-Convert.png`): output format selector (`Word Document (.docx)` | `Rich Text Format (.rtf)`), reconstruction mode radio/segmented selector (`Flowing -- Editable paragraphs, lists & tables`, `Exact -- Fixed-position frames matching page layout`, `Continuous -- Single continuous stream`), seeded from Settings `Default PDF to Word reconstruction mode` (`D02 T04 §5`), plus toggles for `Detect and reconstruct tables`, `Include images and graphics`, and `Recognize text (OCR) on scanned pages & vector outlines`, page range, save location, file-exists policy, and the SPEC Principle 4 honest capability hint line.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 2 (Principle 4: honest conversion), 9.5 (Word document card and Step 3 options), and 9.7 (`Default PDF to Word reconstruction mode` setting) with `design/screenshots/D-Convert.png` and `design/screenshots/explorations/B-Convert.png`.
**Job:** The user can convert any PDF into an editable `.docx` or `.rtf` document in `Flowing`, `Exact`, or `Continuous` mode through the Convert tab with clear expectations. Consumer: `ConversionRunner` (`D04 T01 §1`), `DocxWriter` (`§2`-`§3`), and `RtfWriter` (`§4`).
**Treatment:** Dedicated `WordConvertOptionsControl` bound to `WordConvertOptionsViewModel`, reading the initial reconstruction mode from `D02 T04 §5` and submitting a `ConversionJob` that runs `DocxWriter` or `RtfWriter` on the background job pool. Cheaper substitute that fails the checkpoint: omitting the `Flowing` / `Exact` / `Continuous` mode selector, omitting `.rtf`, or hiding the SPEC Principle 4 honest capability hint text.
**Chrome:** consume the Convert Step 2 card and Step 3 options host from `D04 T01 §3`-`§4` and Settings from `D02 T04 §5`.

**Build order:**

1. Implement `WordConvertOptionsViewModel` and `WordConvertOptionsControl.xaml/.cpp` matching `SPEC 9.5` and `explorations/B-Convert.png`, and register the **Word document** card in `ConverterRegistry` (`D04 T01 §3`). Done when: selecting the **Word document** card displays the exact Step 3 options from `B-Convert.png` with the default reconstruction mode seeded from Settings (`D02 T04 §5`).
2. Wire job submission for `.docx` and `.rtf` into `ConversionRunner` (`D04 T01 §1`) and verify progress, completion notifications (`D04 T01 §6`), and cancellation. Done when: `AppTests.ConvertWordUi` runs both `.docx` (`Flowing` and `Exact`) and `.rtf` jobs through the queue and verifies screenshot parity with `explorations/B-Convert.png`.

- [ ] Implement `WordConvertOptionsViewModel` in `src/services/word_convert_options_view_model.h/.cpp` exposing `OutputFormat` (`Docx`, `Rtf`), `ReconstructionMode` (`Flowing`, `Exact`, `Continuous`, initialized from `SettingsModel::default_pdf_to_word_mode()` in `D02 T04 §5`), `ReconstructTables` (`true`), `IncludeImages` (`true`), and `OcrScannedAndVectorOutlines` (`true`). Done when: changing the default mode in Settings updates the initial selection for new Word conversion sessions.
- [ ] Build `WordConvertOptionsControl` (`src/app/views/convert/WordConvertOptionsControl.xaml/.cpp`) matching `explorations/B-Convert.png` with accessible labels, tooltips, and keyboard navigation (`D03 T02 §3`). Done when: `WordConvertOptionsControl` visual layout matches `explorations/B-Convert.png`. Cheaper substitute that fails the checkpoint: hard-coding `Flowing` `.docx` only with no mode or `.rtf` selector in Step 3.
- [ ] Display the SPEC Principle 4 honest capability hint line (`"PDF to Word works best on text documents and reports. Use Exact mode for complex brochures or forms; scanned pages use built-in Windows OCR."`). Done when: the hint text is visible in Step 3 and exposed to screen readers.
- [ ] Update the primary action button label dynamically (`"Convert to Word (.docx)"` or `"Convert to Rich Text (.rtf)"`) and enqueue a `ConversionJob` invoking `DocxWriter` or `RtfWriter` on click. Done when: clicking the primary button starts the background job and updates `QueueCard`.
- [ ] Verify that `Open file` on a completed `.docx` or `.rtf` job in `QueueCard` / toast (`D04 T01 §6`) launches the system `.docx`/`.rtf` handler (or opens in Recto if requested). Done when: `AppTests.ConvertWordUi` verifies job completion and output validity.
- [ ] Add `AppTests.ConvertWordUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=AppTests.ConvertWordUi` passes.

- [ ] Commit: convert page **Word document (`.docx` & `.rtf`)** card and Step 3 options UI (`explorations/B-Convert.png`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.ConvertWordUi` passes, and the Step 3 screenshot matches `design/screenshots/explorations/B-Convert.png`.

## 6. Foundation plan section 11 **20-document PDF -> Word quality review** and layout tuning

Assemble and execute the foundation plan section 11 **20-document PDF -> Word evaluation corpus** across 7 document categories (single-column prose articles, two-column academic papers, bordered financial tables, borderless invoices/schedules, multi-field forms, mixed image+text brochures/certificates, and scanned/vector-outlined pages), run `DocxWriter` (`.docx`) and `RtfWriter` (`.rtf`) across all 20 documents, verify every output opens without repair prompts and round-trips through `D04 T04 §1`, tune paragraph/column/table reconstruction heuristics in `src/core/docx_writer.cpp` and `src/core/rtf_writer.cpp` for any defects found, and commit the scored 20-document report in `docs/reviews/pdf-to-word-corpus.md`.

> **Corrected 2026-10-21:** assembling the corpus moved to `§7` (Phase 4), because foundation plan section 11 item 3 requires it before M5 and the converter sections tune against it; this section keeps the evaluation, tuning and report. Cost of reversing: the corpus would again arrive after the code it should shape.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Using the `§7` corpus (it previously said: assemble the deterministic 20-document evaluation suite (`tests/corpus/convert/word-eval-01..20.pdf` generated by `scripts/build-corpus.py` or sourced from `tests/corpus/`) covering all 7 layout archetypes, and add an automated evaluation harness (`DocxWriter.TwentyDocumentCorpusEval`) that converts all 20 PDFs in `Flowing`, `Exact`, and `Continuous` modes, validates ECMA-376 XML schema/relationships, and measures paragraph coalescing, heading accuracy, column reading order, and table cell counts. Done when: all 20 documents convert with zero XML/package errors under ASan.
2. Tune paragraph line-joining, multi-column gutter detection, and table boundary heuristics in `src/core/docx_writer.cpp` and `src/core/rtf_writer.cpp` for any issues surfaced by the 20-document run, write `docs/reviews/pdf-to-word-corpus.md` with per-document metrics, and mark foundation plan section 11 item 3 resolved. Done when: `DocxWriter.TwentyDocumentCorpusEval` passes all quality thresholds and `docs/reviews/pdf-to-word-corpus.md` is committed.

- [ ] Implement `DocxWriter.TwentyDocumentCorpusEval` in `tests/recto-tests/pdf_to_word_corpus_tests.cpp` converting all 20 PDFs to `.docx` and `.rtf`, validating XML well-formedness, relationship integrity, zero orphan `rId`s, expected heading counts, expected `w:tbl` row/column counts, and 100% text preservation (`PageText` word recall `>= 99%`). Done when: `DocxWriter.TwentyDocumentCorpusEval` runs across all 20 PDFs under ASan. Cheaper substitute: declaring `DocxWriter` done on 1 toy test file without running and grading the 20-document corpus required by foundation plan sections 7 and 11.
~~Moved to `§7` (2026-10-21): Define and build the 20-document PDF -> Word evaluation corpus (`word-eval-01` through `word-eval-20`) spanning: (1-4) single-column reports/contracts with headings and lists, (5-7) two-column and three-column academic papers with full-width abstracts/headers, (8-10) bordered tables with `rowspan`/`colspan` merges, (11-13) borderless financial statements and invoices, (14-15) forms and certificates (`Exact` mode target), (16-18) illustrated brochures with captions and running headers/footers, and (19-20) scanned and vector-curve-outlined pages. Done when: all 20 fixtures are tracked with SHA-256 entries in `tests/corpus/PROVENANCE.md`.~~
- [ ] Tune paragraph line-spacing, indentation, column-gutter projection, and table header/cell heuristics in `src/core/docx_writer.cpp` and `src/core/rtf_writer.cpp` to resolve any layout regressions discovered across the 20 documents. Done when: all 20 documents meet their target structure assertions in `DocxWriter.TwentyDocumentCorpusEval`.
- [ ] Author `docs/reviews/pdf-to-word-corpus.md` recording the 20-document table (filename, category, page count, recommended mode, heading/list/table/image fidelity scores, word recall %, and any honest limitations). Done when: `docs/reviews/pdf-to-word-corpus.md` is complete and contains zero em dashes.
- [ ] Verify that the Convert Step 3 honest capability hint line (`§5`) accurately reflects the empirical findings from `docs/reviews/pdf-to-word-corpus.md`. Done when: the UI hint line and the review document agree.
- [ ] Update `docs/plans/2026-10-13-recto-foundation.md` section 11 item 3 with a dated resolution note pointing to `docs/reviews/pdf-to-word-corpus.md`. Done when: `recto-tests.exe --gtest_filter=DocxWriter.TwentyDocumentCorpusEval` passes under ASan.
- [ ] Commit: `convert: wire 3-mode PDF to Word/RTF UI and complete 20-document quality review`

**Test checkpoint:** `recto-tests.exe --gtest_filter=DocxWriter.*:RtfWriter.*:AppTests.ConvertWordUi` passes under ASan, and `docs/reviews/pdf-to-word-corpus.md` records all 20 documents passing validity and quality checks.

## 7. 20-document PDF -> Word evaluation corpus, assembled before M5

Foundation plan section 11 item 3 requires the 20-document PDF -> Word test corpus to exist before M5, so the `DocxWriter` and `RtfWriter` sections (`§2` to `§4`) can be built and tuned against it and `§6` can grade them. This section builds the corpus only: deterministic generated fixtures across the seven layout archetypes, tracked in the corpus provenance. Evaluation, tuning and the scored report stay in `§6`.

**Phase:** 4
**Needs:** none
**Surface:** no

**Build order:**

1. Generate `tests/corpus/convert/word-eval-01.pdf` to `word-eval-20.pdf` from `scripts/build-corpus.py`. Done when: `python scripts/build-corpus.py --check` lists all 20 as present and reproducible.
2. Record each fixture's archetype, expected structure and SHA-256 in `tests/corpus/PROVENANCE.md`. Done when: `recto-tests.exe --gtest_filter=Corpus.*` verifies the 20 new files.

- [ ] Add generators for `word-eval-01` to `word-eval-20` in `scripts/build-corpus.py`, deterministic byte for byte: (1-4) single-column reports and contracts with headings and lists, (5-7) two- and three-column papers with full-width abstracts, (8-10) bordered tables with merged cells, (11-13) borderless statements and invoices, (14-15) forms and certificates, (16-18) illustrated brochures with captions and running headers and footers, (19-20) image-only and vector-outlined pages. Done when: two runs of `python scripts/build-corpus.py` produce identical bytes for all 20.
- [ ] Write each fixture's expected structure (heading count, list items, table rows and columns, column count, reading order) to `tests/corpus/convert/word-eval-expected.json`, the oracle `§6` grades against. Done when: the file has one entry per fixture and `python scripts/build-corpus.py --check` validates it against the fixtures.
- [ ] Add the 20 fixtures and the oracle to `tests/corpus/PROVENANCE.md` with their SHA-256. Done when: `Corpus.Provenance` passes with the new files.
- [ ] Commit: `corpus: generate the 20-document PDF to Word evaluation corpus`

**Test checkpoint:** `python scripts/build-corpus.py --check` exits 0 with all 20 `word-eval` fixtures and the oracle verified, and `recto-tests.exe --gtest_filter=Corpus.*` passes.

## Verification

- Every corpus PDF converted by `DocxWriter` and `RtfWriter` produces a valid `.docx` or `.rtf` file that parses cleanly and opens without repair prompts.
- The 20-document evaluation record in `docs/reviews/pdf-to-word-corpus.md` is complete and foundation plan section 11 item 3 is closed.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
