---
schema_version: 1
id: office-to-pdf
domain: 04-convert
status: active
title: "TODO-04 -- Office, Legacy Binary Office, HTML and Markdown to PDF: 100% Native C++20 Engines"
track: M5
---

# TODO-04 -- Office, Legacy Binary Office, HTML and Markdown to PDF: 100% Native C++20 Engines

> **Goal:** Deliver 100% pure native C++20 `Office, Legacy Binary Office, HTML, Markdown & Data -> PDF` conversion that works out of the box on every Windows 11 machine with zero external installs, zero COM automation, and zero child subprocesses: Recto's built-in C++20 OpenXML (`.docx`, `.docm`, `.xlsx`, `.xlsm`, `.pptx`, `.pptm`), legacy binary OLE2 Compound File (`[MS-CFB]` + `[MS-DOC]` `.doc`, `[MS-XLS]` BIFF8 `.xls`, `[MS-PPT]` `.ppt`), `.rtf`, `HTML/CSS` (`.html`), `Markdown` (`.md`), `CSV` (`.csv`) and `XML/Text` parsers + DirectWrite/Direct2D PDF layout engines (`src/core/office_to_pdf.cpp`, `src/core/ole2_cfb.cpp`, `src/core/legacy_office_to_pdf.cpp`, and `src/core/html_md_to_pdf.cpp`).

> **Current state:** Measured 2026-10-14. No Office-to-PDF or HTML/Markdown-to-PDF code exists yet. On 2026-10-19 decisions D3, D7, D9, D10 and D12 were locked so Recto is 100% self-contained in native C++20 across both modern OpenXML and legacy 97-2003 binary Office formats with zero Microsoft Office or LibreOffice dependencies.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D3, D7, D9, D10, D12), 5 (`Office, Legacy Office, HTML, Markdown & Data -> PDF` row) and 5.1 (`100% Pure Native C++20 OpenXML, Legacy Binary OLE2, RTF, HTML, Markdown, XPS & SVG Engine`), 7 (M5 done-when: `.docx`, `.docm`, `.doc`, `.xlsx`, `.xlsm`, `.xls`, `.pptx`, `.pptm`, `.ppt`, `.rtf`, `.html`, `.md`, `.csv` convert cleanly in 100% pure native C++20 and macro-bearing `.docm`/`.doc` verified never to execute macros), 9 (security rules 2, 3 and 7).
- `design/SPEC.md` sections 2 (Principle 5: macros never run), 9.5 (`PDF from Office, HTML, Markdown & Images` card), 9.7 (Converting: Default Office to PDF page setup).
- -> XREF: D02 T04 §5 -- the `Default Office to PDF page setup` selector in Settings (`D02 T04 §5`) seeds §7's default page size (`Match original document`, `A4`, `Letter`).
- -> XREF: D04 T01 §4 -- registers the `PDF from Office, HTML, Markdown & Images` card and file-type applicability rules into the Convert page (`§4`).
- -> XREF: D04 T02 §2 -- `office_to_pdf.cpp` (§1, §2), `legacy_office_to_pdf.cpp` (§5, §6), and `html_md_to_pdf.cpp` (§3) emit vector PDFs via `D04 T02 §2`'s `PdfWriter` and `D04 T02 §1`'s `AtomicFileWriter`.
- -> XREF: D04 T03 §1 -- `office_to_pdf.cpp` (§1, §2) reads OpenXML `.docx`, `.xlsx`, and `.pptx` ZIP packages via `D04 T03 §1`'s `zip_archive.cpp`.
- -> XREF: D06 T01 §6 -- `ConvertSuite.OfficeAndHtmlRoundTrip` and `ConvertSuite.LegacyBinaryOfficeRoundTrip` (`D06 T01 §6`) verify multi-format Office and legacy binary Office conversion quality built in §6 and §7 here.
- -> XREF: D06 T02 §2 -- the security audit in `D06 T02 §2` re-runs the macro-stream stripping (`vbaProject.bin`, `_VBA_PROJECT_CUR`, `Macros`, `VBA` on `macro-canary.docm` and `macro-canary.doc`), HTML external network/script blocking, and zero-external-subprocess proofs built here, and `fuzz_openxml_zip` (`D06 T02 §3`) fuzzes both the OpenXML (`ZipReader`) and OLE2 Compound File (`Ole2Reader`) parsers (`§1`, `§2`, `§4`).
- -> XREF: D04 T05 §12 -- validates PDF to PowerPoint output (D04 T05 §12).

## Outcome

- Any Windows 11 machine converts `.docx`, `.docm`, and `.rtf` (`§1`), `.xlsx`, `.xlsm`, `.csv`, `.pptx`, and `.pptm` (`§2`), `.html` and `.md` (`§3`), and legacy binary `.doc`, `.xls`, and `.ppt` (`§4`-`§6`) files to vector PDFs out of the box using Recto's 100% built-in C++20 layout engines (`src/core/office_to_pdf.cpp`, `src/core/ole2_cfb.cpp`, `src/core/legacy_office_to_pdf.cpp`, and `src/core/html_md_to_pdf.cpp`), with zero external software installed, zero COM calls, zero child processes, and zero remote network or script execution.
- `Ole2Reader` (`§4`) parses `[MS-CFB]` Compound File Binary containers with strict sector-chain cycle detection and a `256 MB` allocation cap while stripping `_VBA_PROJECT_CUR`, `Macros`, and `VBA` macro storages by construction.
- Legacy Word 97-2003 `.doc` (`§5`), Excel 97-2003 `.xls` BIFF8, and PowerPoint 97-2003 `.ppt` (`§6`) binary streams lower directly into the shared `FlowDocumentModel`, spreadsheet model, and slide model, with `macro-canary.docm` and `macro-canary.doc` proving that embedded VBA macros are completely inert.
- The Convert page (`§7`, `SPEC 9.5`) ties all native C++20 engines together cleanly.

**Adjacency:** all=not-applicable (converter engines inside `D04 T01`'s Convert frame; default page-setup preference is owned by `D02 T04 §5` and read here)

**Adjacency rationale:** The `PDF from Office, HTML, Markdown & Images` card, queue UI, and settings selector live in `D04 T01` and `D02 T04 §5`. This file owns the 100% native C++20 OpenXML, OLE2 legacy binary Office, RTF, HTML, Markdown, and CSV engines and their macro/script-safety proofs.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Built-in C++20 Word (`.docx`, `.docm`) and Rich Text (`.rtf`) to PDF layout engine | D04 T02 §2, D04 T03 §1 | [ ] |
| 2 | §2 | Built-in C++20 Excel (`.xlsx`), `.csv`, and PowerPoint (`.pptx`) to PDF layout engines | §1 | [ ] |
| 3 | §3 | Built-in C++20 `HTML/CSS` (`.html`) and `Markdown` (`.md`) to PDF engine and CLI verbs | §1 | [ ] |
| 4 | §4 | Native C++20 OLE2 Compound File (`[MS-CFB]`) reader and VBA macro stream stripper | §1 | [ ] |
| 5 | §5 | Native C++20 legacy Word 97-2003 (`.doc`) binary parser to `FlowDocumentModel` & PDF | §4 | [ ] |
| 6 | §6 | Native C++20 legacy Excel (`.xls` BIFF8) and PowerPoint (`.ppt`) binary parsers to PDF | §2, §4 | [ ] |
| 7 | §7 | Convert **PDF from Office, HTML, Markdown & Images** card and Step 3 UI (`SPEC 9.5`) | §3, §5, §6, D04 T01 §4, D02 T04 §5 | [ ] |

## 1. Built-in C++20 Word (`.docx`, `.docm`) and Rich Text (`.rtf`) to PDF layout engine

Implement the WordprocessingML (`.docx` / `.docm`) and Rich Text Format (`.rtf`) to PDF layout engine in `src/core/office_to_pdf.h` and `src/core/office_to_pdf.cpp` using `ZipReader` (`D04 T03 §1`), a bounded XML/RTF parser (enforcing entity-expansion caps and stripping any `word/vbaProject.bin` macro streams by construction), DirectWrite text layout (`IDWriteTextLayout`), and `PdfWriter` (`D04 T02 §2`): parse page setup (`w:pgSz`, `w:pgMar`), styles (`word/styles.xml`), numbering/lists (`word/numbering.xml`), paragraphs, formatted runs, positioned frames (`w:framePr`), tables (`w:tbl` with merged cells, borders, and shading), headers/footers, and embedded images (`word/media/*`); paginate into vector PDF pages and emit `/Outlines` bookmarks from `Heading1`..`Heading3` paragraphs.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the `.docx`/`.docm` OpenXML reader and `.rtf` parser in `src/core/office_to_pdf.cpp` building an intermediate `FlowDocumentModel` (sections, headers/footers, paragraphs, styled spans, lists, tables, frames, and images) while stripping `vbaProject.bin` and ActiveX parts. Done when: `OfficeToPdf.BuiltinDocxAndRtfParse` parses single-column, multi-column, table-bearing, and `.docm` macro-bearing fixtures cleanly under ASan.
2. Implement the DirectWrite pagination and `PdfWriter` emission pass (line breaking via `IDWriteTextLayout`, table row/page splitting with repeating header rows, header/footer rendering, image XObject embedding, and `/Outlines` bookmark tree generation). Done when: `OfficeToPdf.BuiltinDocxAndRtfRender` converts `.docx`, `.docm`, and `.rtf` files into multi-page vector PDFs whose extracted `PageText` preserves all source text under ASan.

- [ ] Define `OfficeToPdfOptions` (`page_size_override`, `orientation_override`, `margin_preset`, `fit_spreadsheet_columns`, `generate_heading_bookmarks`, `combine_inputs_into_single_pdf`) and `FlowDocumentModel` in `src/core/office_to_pdf.h`. Done when: `src/core/office_to_pdf.h` compiles cleanly across `recto-core`, `recto-services`, and `recto-cli` with zero external runtime or COM types.
- [ ] Parse `.docx` / `.docm` packages (`word/document.xml`, `word/styles.xml`, `word/numbering.xml`, `word/header*.xml`, `word/footer*.xml`, `word/_rels/document.xml.rels`, `word/media/*`) using `ZipReader` (`D04 T03 §1`), explicitly ignoring `word/vbaProject.bin`, `vbaData.xml`, and external relationship targets (`TargetMode="External"`). Done when: a `.docm` containing `word/vbaProject.bin` converts its document text while never reading or executing VBA macro bytecode. Cheaper substitute: requiring Microsoft Office or LibreOffice to be installed for `.docx` conversion.
- [ ] Parse RTF 1.9.1 `.rtf` documents (`\fonttbl`, `\colortbl`, `\stylesheet`, `\paperw`/`\paperh`, `\pard`, `\trowd`/`\cellx`/`\intbl`, `\posx`/`\posy` frames, `\pict\pngblip`/`\jpegblip`, and `\uN?` Unicode escapes) into the shared `FlowDocumentModel`. Done when: `.rtf` files produced by `D04 T03 §4`'s `RtfWriter` parse back into `FlowDocumentModel` with 100% text and table fidelity.
- [ ] Paginate paragraphs, lists, and multi-column sections (`w:cols`) using DirectWrite (`IDWriteTextFormat` and `IDWriteTextLayout`) to compute exact glyph positions, line heights, widow/orphan control, and page breaks, emitting vector text (`BT ... Tj/TJ ET`), underlines, highlights, and embedded fonts via `PdfWriter` (`D04 T02 §2`). Done when: `OfficeToPdf.BuiltinDocxTypography` verifies selectable vector text and font metrics on the output PDF.
- [ ] Layout and paginate `<w:tbl>` and RTF `\trowd` tables (calculating column widths from `w:tblGrid` / `\cellx`, handling `w:gridSpan` and `w:vMerge` merged cells, drawing cell background fills and border strokes, and splitting multi-page tables across page boundaries). Done when: `OfficeToPdf.BuiltinDocxTables` renders multi-row tables with borders and cell shading into vector PDF pages.
- [ ] Emit PDF `/Outlines` bookmark entries (`D01 T01 §10` format) pointing to the destination page and top Y coordinate of every `Heading1`, `Heading2`, and `Heading3` paragraph when `generate_heading_bookmarks == true`. Done when: opening the generated PDF in Recto populates the `Bookmarks` side panel (`D02 T03 §5`) with the document's heading tree.
- [ ] Add `OfficeToPdf.BuiltinDocx*` and `OfficeToPdf.BuiltinRtf*` unit tests in `tests/recto-tests/office_to_pdf_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=OfficeToPdf.BuiltinDocx*:OfficeToPdf.BuiltinRtf*` passes under ASan.
- [ ] Commit: `feat(convert): implement built-in C++20 Word (.docx, .docm) and RTF to PDF layout engine`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.BuiltinDocx*:OfficeToPdf.BuiltinRtf*` passes under ASan.

## 2. Built-in C++20 Excel (`.xlsx`), `.csv`, and PowerPoint (`.pptx`) to PDF layout engines

Extend `src/core/office_to_pdf.cpp` with native C++20 layout engines for **Excel (`.xlsx`, `.xlsm`) and `.csv`** and **PowerPoint (`.pptx`, `.pptm`)**, plus `recto-cli office-to-pdf <in.docx|doc|xlsx|xls|pptx|ppt|rtf|csv> --out <out.pdf>`:
- **Excel (`.xlsx` / `.xlsm`) & `.csv`:** Parse `xl/workbook.xml`, `xl/sharedStrings.xml`, `xl/styles.xml` (`numFmts`, `fonts`, `fills`, `borders`, `cellXfs`), and `xl/worksheets/sheet*.xml` (plus `<mergeCells>` and `<cols>`) or RFC 4180 `.csv` into `SpreadsheetWorkbookModel`; format numeric, currency, percentage, and date cells; compute column widths and row heights; and paginate worksheets onto portrait or landscape PDF pages (with `Fit all columns on one page` scaling and sheet-name `/Outlines` bookmarks).
- **PowerPoint (`.pptx` / `.pptm`):** Parse `ppt/presentation.xml` (`p:sldSz` slide dimensions) and `ppt/slides/slide1..N.xml` (plus `slideLayouts` / `slideMasters` backgrounds, `<p:sp>` shapes, `<p:txBody>` DrawingML text frames, `<p:graphicFrame>` tables, and `<p:pic>` images) into `PresentationDeckModel`; emit one vector PDF page per slide via `PdfWriter`.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the `.xlsx`/`.xlsm` and `.csv` spreadsheet-to-PDF layout engine (`SpreadsheetWorkbookModel`) and the `.pptx`/`.pptm` slide-to-PDF layout engine (`PresentationDeckModel`) in `src/core/office_to_pdf.cpp` (ignoring any `xl/vbaProject.bin` or `ppt/vbaProject.bin` macro streams). Done when: `OfficeToPdf.BuiltinXlsxCsvAndPptx` converts corpus `.xlsx`, `.csv`, and `.pptx` files into valid multi-page vector PDFs under ASan.
2. Implement `recto-cli office-to-pdf <in> --out <out.pdf> [--fit-columns] [--landscape]` in `src/cli/cmd_office_to_pdf.cpp`. Done when: `Cli.OfficeToPdf` passes under ASan across `.docx`, `.rtf`, `.xlsx`, `.csv`, and `.pptx`.

- [ ] Parse ECMA-376 SpreadsheetML (`.xlsx` / `.xlsm`: `xl/workbook.xml`, `xl/sharedStrings.xml`, `xl/styles.xml`, `xl/worksheets/sheet*.xml`) and RFC 4180 `.csv` into `SpreadsheetWorkbookModel` in `src/core/office_to_pdf.cpp`, ignoring `xl/vbaProject.bin`. Done when: `OfficeToPdf.BuiltinXlsxParse` reads shared strings, inline strings (`t="inlineStr"`), numeric/formula cached values (`<v>`), number formats (`numFmtId`), cell fills/borders, and `<mergeCells>`.
- [ ] Format Excel numeric, currency, percentage, and serial-date values (`1900` date system) according to `numFmtId` / custom format codes, and paginate worksheets onto PDF pages with repeating top header rows, gridlines/borders, and `fit_spreadsheet_columns` horizontal scaling so wide tables fit cleanly onto one page width without awkward 1-column orphan pages. Done when: `OfficeToPdf.BuiltinXlsxAndCsvRender` converts `.xlsx` (including files written by `D04 T05 §1`'s `XlsxWriter`) and `.csv` into vector PDFs whose `PageText` and `TableExtractor` readback match the source cells.
- [ ] Parse ECMA-376 PresentationML (`.pptx` / `.pptm`: `ppt/presentation.xml` `p:sldSz` in EMU, `ppt/slides/slide*.xml`, `ppt/slideLayouts/*`, `ppt/slideMasters/*`, and `ppt/media/*`) into `PresentationDeckModel`, ignoring `ppt/vbaProject.bin`. Done when: `OfficeToPdf.BuiltinPptxParse` resolves slide dimensions, background fills, positioned shapes (`a:xfrm` `a:off` / `a:ext`), text bodies (`a:p` / `a:r`), tables (`a:tbl`), and images (`p:pic`).
- [ ] Render each slide in `PresentationDeckModel` to a PDF page matching the slide's aspect ratio (`16:9` widescreen or `4:3` standard in points: `EMU / 12700`), drawing vector shape geometries, solid/gradient fills, DirectWrite text frames, tables, and slide images via `PdfWriter`, plus a `/Outlines` bookmark per slide title. Done when: `OfficeToPdf.BuiltinPptxRender` converts a multi-slide `.pptx` into a multi-page vector PDF with selectable slide text and images. Cheaper substitute: failing on `.xlsx` and `.pptx` unless Microsoft Office is installed.
- [ ] Support `combine_inputs_into_single_pdf` in `office_to_pdf` so a mixed batch of Office, RTF, and CSV files can be emitted into one combined PDF or individual PDFs, committed via `AtomicFileWriter` (`D04 T02 §1`). Done when: `OfficeToPdf.CombineMixedBatch` merges a `.docx`, `.xlsx`, and `.pptx` into one multi-page PDF.
- [ ] Implement `recto-cli office-to-pdf <in> --out <out.pdf> [--fit-columns] [--orientation auto|portrait|landscape]` in `src/cli/cmd_office_to_pdf.cpp`. Done when: `recto-tests.exe --gtest_filter=OfficeToPdf.Builtin*:Cli.OfficeToPdf` passes under ASan.
- [ ] Commit: `convert: implement built-in C++20 Excel (.xlsx, .csv) and PowerPoint (.pptx) to PDF engines`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.Builtin*:Cli.OfficeToPdf` passes under ASan across `.docx`, `.docm`, `.rtf`, `.xlsx`, `.csv`, and `.pptx` corpus files.

## 3. Built-in C++20 `HTML/CSS` (`.html`) and `Markdown` (`.md`) to PDF engine and CLI verbs

Implement `src/core/html_md_to_pdf.h` and `src/core/html_md_to_pdf.cpp` in `recto-core`, plus `recto-cli html-to-pdf` and `recto-cli md-to-pdf`: parse **HTML5 + CSS** (including `@page` size/margins, block/inline/table/flex box layout, typography, borders, backgrounds, local relative images and `data:` URI images, `<svg>` inline graphics, and `<a href>` links) and **GitHub-Flavored Markdown (`.md`)** (headings `#`..`######`, paragraphs, bold/italic/strikethrough/inline code, fenced code blocks, task lists `- [x]`, blockquotes `>`, pipe tables `|---|`, and images), paginate via DirectWrite, and emit vector PDFs via `PdfWriter` (`D04 T02 §2`) with clickable `/Link` annotations and `/Outlines` heading bookmarks, while strictly disabling all `<script>` execution and blocking all remote `http://` / `https://` network fetches (`foundation plan section 9`).

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the HTML5 tokenizer/DOM builder and CSS cascade/box layout engine in `src/core/html_md_to_pdf.cpp` (stripping `<script>`, `<iframe>`, `<object>`, and blocking any remote `http://`/`https://` stylesheet or image fetches by construction). Done when: `HtmlMdToPdf.HtmlLayoutAndSecurity` converts styled HTML5 pages (headings, CSS `@page`, tables, flexbox cards, `data:` URI images, and `<svg>`) into vector PDFs while ignoring `<script>` tags and making zero network requests under ASan.
2. Implement the GitHub-Flavored Markdown (`.md`) parser in `src/core/html_md_to_pdf.cpp` lowering GFM AST nodes into the styled box layout tree, and add `recto-cli html-to-pdf <in.html> --out <out.pdf>` and `recto-cli md-to-pdf <in.md> --out <out.pdf>`. Done when: `HtmlMdToPdf.MarkdownGfm` and `Cli.HtmlAndMdToPdf` convert Markdown documents with tables, fenced code blocks, task lists, and heading `/Outlines` into vector PDFs under ASan.

- [ ] Implement the HTML5 parser and CSS selector/property cascade (`font-family`, `font-size`, `font-weight`, `font-style`, `color`, `background-color`, `margin`, `padding`, `border`, `width`, `height`, `text-align`, `display: block|inline|table|flex|none`, `page-break-before`/`after`, `@page { size; margin; }`) in `src/core/html_md_to_pdf.cpp`. Done when: both `Flowing` and `Fixed-Layout` HTML5 files exported by `D04 T05 §3`'s `WebDocWriter` round-trip back to PDF with preserved text and tables.
- [ ] Enforce hostile-HTML isolation (`foundation plan section 9`): strip `<script>`, `<noscript>`, `<iframe>`, `<frame>`, `<object>`, `<embed>`, and `on*` event attributes during DOM construction, and forbid opening any `http://`, `https://`, `ftp://`, or `\\` UNC path (loading only `data:` URIs and relative local assets resolved strictly underneath the input `.html` file's parent directory without `..` traversal). Done when: `HtmlMdToPdf.HostileHtmlIsolation` proves `<script>` tags, remote `<img src="https://...">`, and `<img src="../../secret.png">` never execute or read outside the document folder. Cheaper substitute: invoking an external browser/WebView that fetches remote URLs or runs JavaScript during conversion.
- [ ] Implement the GitHub-Flavored Markdown (`.md`) parser in `src/core/html_md_to_pdf.cpp` supporting ATX/Setext headings (`H1`..`H6`), paragraphs, nested bullet/numbered lists, task-list checkboxes (`- [ ]` / `- [x]` rendered as crisp vector checkbox glyphs), pipe tables (`|---|---|` with left/center/right column alignment), blockquotes (`>` with left accent bar), horizontal rules, and monospace shaded fenced code blocks (` ``` `). Done when: `HtmlMdToPdf.MarkdownGfm` converts a comprehensive `.md` fixture into a paginated vector PDF.
- [ ] Emit clickable PDF `/Subtype /Link` annotations (`D01 T01 §10`) for `<a href="...">` and Markdown `[label](url)` links (internal `#anchor` destinations mapped to PDF page coordinates, external `https://`/`mailto:` URIs mapped to `/A << /S /URI /URI (...) >>`), and generate PDF `/Outlines` bookmarks from `<h1>`..`<h3>` / `#`..`###` headings. Done when: `HtmlMdToPdf.LinksAndBookmarks` verifies clickable `/Link` annotations and `/Outlines` hierarchy in the output PDF.
- [ ] Commit output PDFs via `AtomicFileWriter` (`D04 T02 §1`) and check `CancelToken` across layout and pagination passes. Done when: cancelling mid-render leaves zero temp files.
- [ ] Implement `recto-cli html-to-pdf <in.html> --out <out.pdf> [--page-size a4|letter]` and `recto-cli md-to-pdf <in.md> --out <out.pdf> [--page-size a4|letter]` in `src/cli/`. Done when: `recto-tests.exe --gtest_filter=HtmlMdToPdf.*:Cli.HtmlAndMdToPdf` passes under ASan.
- [ ] Commit: `feat(convert): implement built-in C++20 HTML/CSS and Markdown to PDF layout engine`

**Test checkpoint:** `recto-tests.exe --gtest_filter=HtmlMdToPdf.*:Cli.HtmlAndMdToPdf` passes under ASan.

## 4. Native C++20 OLE2 Compound File (`[MS-CFB]`) reader and VBA macro stream stripper

Implement `src/core/ole2_cfb.h` and `src/core/ole2_cfb.cpp` in `recto-core`: a 100% pure native C++20 reader for Microsoft Compound File Binary (`[MS-CFB]`, `D0 CF 11 E0 A1 B1 1A E1`) containers used by legacy `.doc`, `.xls`, and `.ppt` files, parsing v3 (`512`-byte sectors) and v4 (`4096`-byte sectors) headers, DIFAT, FAT, MiniFAT (`64`-byte mini-sectors below `4096` bytes), and the red-black directory tree with strict cycle detection, bounds checks, and a `256 MB` total stream allocation cap, while **stripping all VBA macro and ActiveX storages (`_VBA_PROJECT_CUR`, `Macros`, `VBA`, `\x01CompObj` executable controls)** during directory enumeration so macro bytecode is never exposed to higher layers.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `Ole2Reader::open(std::span<const uint8_t>)` and `read_stream(std::u16string_view path)` in `src/core/ole2_cfb.cpp` validating the `[MS-CFB]` magic signature, sector shift (`9` or `12`), DIFAT/FAT/MiniFAT chains with visited-sector bitsets (rejecting cyclic or out-of-bounds sector chains cleanly), and directory tree traversal (capped at `4096` entries and `64` depth levels). Done when: `Ole2Cfb.ValidAndCyclicChains` reads normal and MiniFAT streams and rejects cyclic FAT/MiniFAT chains, truncated sectors, and `> 256 MB` bomb streams in `< 5 ms` under ASan.
2. Enforce built-in VBA macro storage filtering (`is_blocked_macro_entry`) so `_VBA_PROJECT_CUR`, `Macros`, `VBA`, and `_VBA_PROJECT` storages and streams are skipped during directory enumeration and rejected by `read_stream`. Done when: `Ole2Cfb.MacroStorageStripped` proves that macro storages in a synthetic `[MS-CFB]` container are invisible and inaccessible to callers under ASan.

- [ ] Define `Ole2Entry` (`name_utf8`, `is_storage`, `size_bytes`, `start_sector`), `Ole2Reader` (`open`, `list_entries`, `has_stream`, `read_stream`), and `PdfResult<Ole2Reader>` error reporting in `src/core/ole2_cfb.h`. Done when: `src/core/ole2_cfb.h` compiles cleanly across `recto-core` and `recto-tests`.
- [ ] Parse the 512-byte `[MS-CFB]` header (`D0 CF 11 E0 A1 B1 1A E1`), assemble the full FAT from the 109 header DIFAT entries plus chained DIFAT sectors (guarding against DIFAT loops with a visited-sector bitset), and validate every sector index against the physical file length. Done when: `Ole2Cfb.FatAndDifatAssembly` reads multi-DIFAT compound files and rejects corrupted sector indices cleanly.
- [ ] Implement FAT and MiniFAT stream extraction (`follow_chain` with a `max_sectors` visited count and `256 MB` total allocation cap), reading streams `< 4096` bytes from the root entry's `MiniStream` via `MiniFAT` and streams `>= 4096` bytes directly from main sectors. Done when: `Ole2Cfb.MiniStreamAndRegularStream` extracts both `< 4096 B` and multi-sector streams byte-for-byte. Cheaper substitute: calling Win32 `StgOpenStorage` on untrusted files without sector-loop or allocation caps.
- [ ] Traverse the directory entry red-black tree (`left_sibling`, `right_sibling`, `child_id`) with a visited-node bitset and `64`-level recursion limit, automatically filtering out any storage or stream named `Macros`, `VBA`, `_VBA_PROJECT_CUR`, or `_VBA_PROJECT` (case-insensitive UTF-16 comparison). Done when: `Ole2Cfb.MacroStorageStripped` verifies that VBA storages are stripped by construction and cyclic directory trees return `PdfErrorCode::CorruptStructure` without stack overflow.
- [ ] Add `Ole2Cfb.*` unit tests in `tests/recto-tests/ole2_cfb_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Ole2Cfb.*` passes under ASan.
- [ ] Commit: `feat(convert): implement native C++20 OLE2 Compound File ([MS-CFB]) reader and macro stripper`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Ole2Cfb.*` passes under ASan across valid, MiniStream, macro-bearing, and cyclic/malformed `[MS-CFB]` fixtures.

## 5. Native C++20 legacy Word 97-2003 (`.doc`) binary parser to `FlowDocumentModel` & PDF

Implement the native C++20 legacy Word 97-2003 (`.doc`, `[MS-DOC]`) binary parser in `src/core/legacy_office_to_pdf.h` and `src/core/legacy_office_to_pdf.cpp` over §4's `Ole2Reader`, lowering `.doc` files directly into §1's `FlowDocumentModel` and `PdfWriter`: parse the `WordDocument` stream File Information Block (`Fib`), locate the `0Table` or `1Table` stream (`fWhichTblStm`), parse the `Clx` piece table (`Prc` + `PlcfPcd` `Pcd` descriptors mapping CP character positions to compressed 8-bit Windows-1252 or uncompressed 16-bit UTF-16LE runs in `WordDocument`), apply `PlcfBtePapx` (`PAPX` paragraph formatting, heading styles, lists, and `inTable`/`itap` table cell/row markers `0x0007`) and `PlcfBteChpx` (`CHPX` character bold/italic/underline/font-size/colour sprms), and verify macro-inertness on both `macro-canary.docm` and `macro-canary.doc`.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `parse_legacy_doc_to_flow_model(const Ole2Reader&, CancelToken)` in `src/core/legacy_office_to_pdf.cpp` parsing the `[MS-DOC]` `Fib`, `0Table`/`1Table` `Clx` piece table (`PlcfPcd`), `FFN` font table (`SttbfFfn`), `Stsh` style sheet, and `PAPX`/`CHPX` FKP formatted disk pages into `FlowDocumentModel` paragraphs, headings, runs, and tables, and render to PDF via §1's DirectWrite paginator. Done when: `OfficeToPdf.LegacyDocRoundTrip` converts multi-page `.doc` fixtures (with headings, bold/italic runs, and tables) into selectable vector PDFs under ASan.
2. Build `tests/corpus/convert/macro-canary.docm` and `tests/corpus/convert/macro-canary.doc` (`scripts/build-corpus.py`) containing embedded VBA `AutoOpen` / `Document_Open` macro streams and prove in `OfficeToPdf.MacroCanaryInert` that converting both files produces valid PDFs while zero macro code is parsed or executed and zero external processes are spawned. Done when: `OfficeToPdf.MacroCanaryInert` passes under ASan.

- [ ] Parse the `[MS-DOC]` `FibBase` and `FibRgFcLcb97` offsets from the `WordDocument` stream in `src/core/legacy_office_to_pdf.cpp`, selecting `1Table` or `0Table` via `fWhichTblStm` and validating all stream offset/length pairs (`fcClx`/`lcbClx`, `fcPlcfBteChpx`, `fcPlcfBtePapx`, `fcSttbfFfn`, `fcStshf`) against the table stream size. Done when: `OfficeToPdf.LegacyDocFibAndPieceTable` parses CP text ranges from both compressed (CP1252) and Unicode (UTF-16LE) piece descriptors (`Pcd`).
- [ ] Parse `PlcfBtePapx` and `PlcfBteChpx` 512-byte Formatted Disk Pages (`FKP`) and Single Property Modifiers (`sprm`: font family index `sprmCRgFtc0`, half-point size `sprmCHps`, bold `sprmCFBold`, italic `sprmCFItalic`, underline `sprmCKul`, RGB colour `sprmCCv`/`sprmCIco`, paragraph alignment `sprmPJc`, heading `istd` style IDs `1..6`, and table flags `sprmPFInTable` / `sprmPFTtp`). Done when: `OfficeToPdf.LegacyDocFormattingAndTables` reconstructs styled paragraphs, headings (`Heading 1`..`Heading 3`), and `\x07`-delimited table rows/cells in `FlowDocumentModel`. Cheaper substitute: scraping raw ASCII strings from the `.doc` file without parsing the `[MS-DOC]` piece table (`Clx`) or `PAPX`/`CHPX` formatting.
- [ ] Route `.doc` files in `convert_office_to_pdf` and `recto-cli office-to-pdf` through `Ole2Reader` -> `parse_legacy_doc_to_flow_model` -> §1's DirectWrite PDF layout engine. Done when: `recto-cli office-to-pdf` converts `.doc` files to vector PDFs with `/Outlines` bookmarks and selectable text.
- [ ] Generate `tests/corpus/convert/macro-canary.docm` (OpenXML with `word/vbaProject.bin`) and `tests/corpus/convert/macro-canary.doc` (OLE2 `[MS-CFB]` with `Macros`/`VBA`/`_VBA_PROJECT_CUR` streams) and verify in `OfficeToPdf.MacroCanaryInert` that both convert cleanly to PDF while never touching the macro streams or creating the canary file. Done when: `OfficeToPdf.MacroCanaryInert` passes under ASan.
- [ ] Add `OfficeToPdf.LegacyDoc*` and `OfficeToPdf.MacroCanaryInert` tests in `tests/recto-tests/office_to_pdf_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=OfficeToPdf.LegacyDoc*:OfficeToPdf.MacroCanaryInert` passes under ASan.
- [ ] Commit: `feat(convert): implement native C++20 legacy Word (.doc) parser and macro-inertness proof`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.LegacyDoc*:OfficeToPdf.MacroCanaryInert` passes under ASan.

## 6. Native C++20 legacy Excel (`.xls` BIFF8) and PowerPoint (`.ppt`) binary parsers to PDF

Extend `src/core/legacy_office_to_pdf.cpp` with 100% native C++20 binary parsers for **legacy Excel 97-2003 (`.xls`, `[MS-XLS]` BIFF8)** and **legacy PowerPoint 97-2003 (`.ppt`, `[MS-PPT]`)** over §4's `Ole2Reader`, lowering them into §2's `SpreadsheetWorkbookModel` and `PresentationDeckModel`:
- **Excel 97-2003 (`.xls` BIFF8):** Parse the `Workbook` (or `Book`) stream BIFF8 record sequence (`BOF` `0x0809`, `BoundSheet8` `0x0085`, `SST` `0x00FC` Shared String Table with `Continue` `0x003C` record spanning, `Font` `0x0031`, `Format` `0x041E`, `XF` `0x00E0`, `Dimension` `0x0200`, `ColInfo` `0x007D`, `LabelSst` `0x00FD`, `Label` `0x0204`, `RK` `0x027E`, `MulRK` `0x00BD`, `Number` `0x0203`, `Formula` `0x0006`, and `MergeCells` `0x00E5`) and paginate via §2's spreadsheet PDF layout engine.
- **PowerPoint 97-2003 (`.ppt`):** Parse the `PowerPoint Document` (and `Pictures`) stream record tree (`RecordHeader` `recVer`/`recInstance`/`recType`/`recLen`: `DocumentContainer` `1000`, `SlideContainer` `1006`, `DrawingContainer` / OfficeArt `SpContainer` `0xF004`, `ClientTextbox` `0xF00D`, `TextHeaderAtom` `3999`, `TextCharsAtom` `4000` UTF-16LE, and `TextBytesAtom` `4008` CP1252) and render each slide via §2's presentation PDF layout engine.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `parse_legacy_xls_to_workbook_model(const Ole2Reader&, CancelToken)` in `src/core/legacy_office_to_pdf.cpp` decoding BIFF8 globals (`BoundSheet8`, `SST` with `Continue` boundary handling, `Format`, `XF`) and per-sheet cell records (`LabelSst`, `RK`, `MulRK`, `Number`, `Formula`, `MergeCells`) into `SpreadsheetWorkbookModel`. Done when: `OfficeToPdf.LegacyXlsRoundTrip` converts multi-sheet `.xls` files (strings, IEEE floats, RK encoded numbers, dates, and merged cells) into vector PDFs under ASan.
2. Implement `parse_legacy_ppt_to_deck_model(const Ole2Reader&, CancelToken)` in `src/core/legacy_office_to_pdf.cpp` walking the `[MS-PPT]` container/atom hierarchy (`SlideContainer`, OfficeArt shapes, `TextCharsAtom`, `TextBytesAtom`, and embedded PNG/JPEG pictures) into `PresentationDeckModel`. Done when: `OfficeToPdf.LegacyPptRoundTrip` converts multi-slide `.ppt` files into multi-page vector PDFs with slide `/Outlines` bookmarks under ASan.

- [ ] Implement the BIFF8 record reader (`uint16_t rec_id`, `uint16_t rec_len`) with automatic `Continue` (`0x003C`) record stitching for the `SST` (`0x00FC`) Shared String Table (handling compressed 8-bit Latin-1 and uncompressed 16-bit UTF-16LE runs across `Continue` boundaries). Done when: `OfficeToPdf.LegacyXlsSstAndContinue` decodes shared strings spanning multiple `Continue` records without buffer over-read under ASan.
- [ ] Decode BIFF8 cell records (`LabelSst` `0x00FD`, `Label` `0x0204`, `RK` `0x027E` 30-bit integer/float encoding, `MulRK` `0x00BD`, `Number` `0x0203` IEEE 754 double, `Formula` `0x0006` cached numeric/string values, and `MergeCells` `0x00E5`) and `XF`/`Format` number format indices into §2's `SpreadsheetWorkbookModel`. Done when: `OfficeToPdf.LegacyXlsRoundTrip` renders `.xls` workbooks to vector PDFs whose extracted `PageText` and `TableExtractor` output match the source cells. Cheaper substitute: failing on `.xls` files unless external spreadsheet software is installed.
- [ ] Implement the `[MS-PPT]` 8-byte `RecordHeader` walker (`recVer == 0xF` container recursion capped at `32` levels, atom payload bounds-checked against parent container length) extracting slide dimensions (`DocumentAtom` `1001`), slide order (`SlidePersistAtom` / `SlideContainer` `1006`), shape bounding boxes (`OfficeArtFSPGR` / `OfficeArtClientAnchor`), slide titles and body runs (`TextHeaderAtom` `3999` + `TextCharsAtom` `4000` / `TextBytesAtom` `4008`), and BLIP images from the `Pictures` stream into §2's `PresentationDeckModel`. Done when: `OfficeToPdf.LegacyPptRoundTrip` converts `.ppt` presentations into vector PDFs with selectable text and slide bookmarks.
- [ ] Wire `.xls` and `.ppt` extensions into `convert_office_to_pdf` and `recto-cli office-to-pdf`. Done when: `Cli.OfficeToPdf` converts `.doc`, `.xls`, and `.ppt` alongside `.docx`, `.xlsx`, `.pptx`, `.rtf`, and `.csv` under ASan.
- [ ] Add `OfficeToPdf.LegacyXls*` and `OfficeToPdf.LegacyPpt*` unit tests in `tests/recto-tests/office_to_pdf_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=OfficeToPdf.LegacyXls*:OfficeToPdf.LegacyPpt*` passes under ASan.
- [ ] Commit: `convert: add native C++20 legacy Excel (.xls BIFF8) and PowerPoint (.ppt) binary parsers`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.Legacy*:Cli.OfficeToPdf` passes under ASan across `.doc`, `.xls`, and `.ppt` fixtures.

## 7. Convert **PDF from Office, HTML, Markdown & Images** card and Step 3 UI (`SPEC 9.5`)

Register the **PDF from Office, HTML, Markdown & Images** card in `ConverterRegistry` (`D04 T01 §3`) and build its Step 3 options control (`src/app/views/convert/OfficeToPdfOptionsControl.xaml/.cpp`, `SPEC 9.5`): page size (`Match original document`, `A4`, `Letter`, seeded from `D02 T04 §5`'s `Default Office to PDF page setup` setting), orientation (`Auto`, `Portrait`, `Landscape`), margins (`Default`, `Narrow`, `None`), `Fit all spreadsheet columns on one page` toggle, `Generate PDF bookmarks from headings and slide titles` toggle, `Combine all input files into a single PDF` toggle, and the SPEC Principle 4 capability hint line.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 9.5 (`PDF from Office, HTML, Markdown & Images` card enable/disable rule and Step 3 options) and 9.7 (Converting: `Default Office to PDF page setup`) with `D-Convert.png`.
**Job:** The user can drop `.docx`, `.docm`, `.doc`, `.xlsx`, `.xlsm`, `.xls`, `.pptx`, `.pptm`, `.ppt`, `.rtf`, `.html`, `.md`, `.csv`, `.svg`, `.xps`, or image files into the Convert page, configure page setup, spreadsheet column fitting, bookmark generation, and single-PDF combining, and convert them to PDF through the queue using Recto's 100% built-in C++20 engines. Consumer: `ConvertPage` (`D04 T01 §3`-`§4`), `ConversionRunner` (`D04 T01 §1`), and `office_to_pdf` / `legacy_office_to_pdf` / `html_md_to_pdf` / `images_to_pdf`.
**Treatment:** Step 3 options control bound to `OfficeToPdfOptionsViewModel`, reading the default page size from `SettingsModel::default_office_page_setup()` (`D02 T04 §5`) and dispatching `.docx`/`.docm`/`.xlsx`/`.xlsm`/`.pptx`/`.pptm`/`.rtf`/`.csv` to `office_to_pdf` (`§1`, `§2`), `.doc`/`.xls`/`.ppt` to `legacy_office_to_pdf` (`§5`, `§6`), `.html`/`.md` to `html_md_to_pdf` (`§3`), and images/SVG/XPS to `images_to_pdf` (`D04 T02 §5`), optionally merging all outputs into a single combined PDF when `Combine all input files into a single PDF` is checked. Cheaper substitute that fails the checkpoint: requiring any external Office suite to be installed, or omitting spreadsheet column fitting and heading bookmark toggles.
**Chrome:** consume the Convert Step 2 card and Step 3 options host from `D04 T01 §3`-`§4` and the default page setup setting from `D02 T04 §5`.

**Build order:**

1. Implement `OfficeToPdfOptionsViewModel` and `OfficeToPdfOptionsControl.xaml/.cpp` (`SPEC 9.5`), seeding its initial `Page size` from `D02 T04 §5`'s `Default Office to PDF page setup` preference and registering the **PDF from Office, HTML, Markdown & Images** card in `ConverterRegistry`. Done when: loading `.docx`, `.doc`, `.xlsx`, `.xls`, `.pptx`, `.ppt`, `.rtf`, `.html`, `.md`, `.csv`, or image files in Step 1 enables the card and renders the Step 3 options control.
2. Wire the job factory to dispatch each input file to Recto's native C++20 engines (`office_to_pdf`, `legacy_office_to_pdf`, `html_md_to_pdf`, `images_to_pdf`) and merge outputs when `Combine all input files into a single PDF` is enabled. Done when: `OfficeToPdf.ConvertPageUi` converts `.docx`, `.doc`, `.xlsx`, `.xls`, `.pptx`, `.ppt`, `.rtf`, `.html`, and `.md` files through the Convert page with zero external dependencies.

- [ ] Register the **PDF from Office, HTML, Markdown & Images** card in `ConverterRegistry` (`D04 T01 §3`), enabled whenever Step 1 contains at least one `.docx`, `.docm`, `.doc`, `.xlsx`, `.xlsm`, `.xls`, `.pptx`, `.pptm`, `.ppt`, `.rtf`, `.html`, `.htm`, `.md`, `.csv`, `.xml`, `.txt`, `.png`, `.jpg`, `.jpeg`, `.tif`, `.tiff`, `.bmp`, `.gif`, `.svg`, or `.xps` file, with disable reason `"Add Word, Excel, PowerPoint, HTML or Markdown files to use this"` when only PDFs are loaded. Done when: `OfficeToPdf.CardEnablement` verifies enablement and disable reasons.
- [ ] Build `OfficeToPdfOptionsControl` (`src/app/views/convert/OfficeToPdfOptionsControl.xaml/.cpp`) exposing `Page size` (`Original`, `A4`, `Letter`, initialized from `D02 T04 §5`), `Orientation` (`Auto`, `Portrait`, `Landscape`), `Margins` (`Default`, `Narrow`, `None`), `Fit all spreadsheet columns on one page` (`true`), `Generate PDF bookmarks from headings` (`true`), `HTML/Markdown CSS print styling` (`true`), and `Combine all files into one PDF` (`false` for single input, toggleable for `>= 2` inputs). Done when: all controls pass `D03 T02 §3` accessibility checks.
- [ ] Render the SPEC Principle 4 capability hint line (`"Converts Office (.docx, .doc, .xlsx, .xls, .pptx, .ppt, .rtf), HTML, Markdown, CSV and images to vector PDF natively with zero external dependencies."`). Done when: the hint line is visible in Step 3.
- [ ] Dispatch queued jobs across `office_to_pdf` (`§1`, `§2`), `legacy_office_to_pdf` (`§5`, `§6`), `html_md_to_pdf` (`§3`), and `images_to_pdf` (`D04 T02 §5`). Done when: `OfficeToPdf.ConvertPageUi` runs `.docx`, `.doc`, `.xlsx`, `.xls`, `.pptx`, `.ppt`, `.rtf`, `.html`, and `.md` conversions through `ConversionRunner` and verifies the produced PDFs. Cheaper substitute that fails the checkpoint: ignoring Step 3 page-setup options or failing on legacy `.doc`/`.xls`/`.ppt` inputs.
- [ ] Support `Open file` in `QueueCard` and completion toasts (`D04 T01 §6`) so clicking `Open file` on a finished `Office -> PDF` job opens the newly created PDF directly in a Recto viewer tab (`D02 T01 §9`). Done when: clicking `Open file` opens the converted PDF in a new tab.
- [ ] Add `OfficeToPdf.CardEnablement` and `OfficeToPdf.ConvertPageUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=OfficeToPdf.CardEnablement:OfficeToPdf.ConvertPageUi` passes.
- [ ] Commit: `convert: wire 100% native Office, Legacy Office, HTML and Markdown to PDF Convert UI`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.*:HtmlMdToPdf.*:Ole2Cfb.*` passes, and display-session screenshots confirm the `PDF from Office, HTML, Markdown & Images` card and Step 3 options.

## Verification

- M5 Office/HTML/Markdown-to-PDF obligations (`docs/plans/2026-10-13-recto-foundation.md` section 7): `.docx`, `.docm`, `.doc`, `.xlsx`, `.xlsm`, `.xls`, `.pptx`, `.pptm`, `.ppt`, `.rtf`, `.html`, `.md`, and `.csv` convert cleanly to PDF via Recto's 100% built-in C++20 engines with zero external dependencies, and `macro-canary.docm` and `macro-canary.doc` are proven never to parse or execute VBA macros.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.

