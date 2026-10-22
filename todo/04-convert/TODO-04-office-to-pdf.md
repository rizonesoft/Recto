---
schema_version: 1
id: office-to-pdf
domain: 04-convert
status: active
title: "TODO-04 -- Office, Legacy Binary Office, HTML and Markdown to PDF: 100% Native C++23 Engines"
track: M5
---

# TODO-04 -- Office, Legacy Binary Office, HTML and Markdown to PDF: 100% Native C++23 Engines

> **Goal:** Deliver 100% pure native C++23 `Office, Legacy Binary Office, HTML, Markdown & Data -> PDF` conversion that works out of the box on every Windows 11 machine with zero external installs, zero COM automation, and zero child subprocesses: Recto's built-in C++23 OpenXML (`.docx`, `.docm`, `.xlsx`, `.xlsm`, `.pptx`, `.pptm`), legacy binary OLE2 Compound File (`[MS-CFB]` + `[MS-DOC]` `.doc`, `[MS-XLS]` BIFF8 `.xls`, `[MS-PPT]` `.ppt`), `.rtf`, `HTML/CSS` (`.html`), `Markdown` (`.md`), `CSV` (`.csv`) and `XML/Text` parsers + DirectWrite/Direct2D PDF layout engines (`src/core/office_to_pdf.cpp`, `src/core/ole2_cfb.cpp`, `src/core/legacy_office_to_pdf.cpp`, and `src/core/html_md_to_pdf.cpp`).

> **Current state:** Measured 2026-10-14. No Office-to-PDF or HTML/Markdown-to-PDF code exists yet. On 2026-10-19 decisions D3, D7, D9, D10 and D12 were locked so Recto is 100% self-contained in native C++23 across both modern OpenXML and legacy 97-2003 binary Office formats with zero Microsoft Office or LibreOffice dependencies. 2026-10-22: WinUI 3 dropped for `recto-ui` (foundation plan D4); UI sections rewritten against `todo/10-ui/`.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D3, D7, D9, D10, D12), 5 (`Office, Legacy Office, HTML, Markdown & Data -> PDF` row) and 5.1 (`100% Pure Native C++23 OpenXML, Legacy Binary OLE2, RTF, HTML, Markdown, XPS & SVG Engine`), 7 (M5 done-when: `.docx`, `.docm`, `.doc`, `.xlsx`, `.xlsm`, `.xls`, `.pptx`, `.pptm`, `.ppt`, `.rtf`, `.html`, `.md`, `.csv` convert cleanly in 100% pure native C++23 and macro-bearing `.docm`/`.doc` verified never to execute macros), 9 (security rules 2, 3 and 7).
- `design/SPEC.md` sections 2 (Principle 5: macros never run), 9.5 (`PDF from Office, HTML, Markdown & Images` card), 9.7 (Converting: Default Office to PDF page setup).
- -> XREF: D02 T04 §5 -- the `Default Office to PDF page setup` selector in Settings (`D02 T04 §5`) seeds §7's default page size (`Match original document`, `A4`, `Letter`).
- -> XREF: D04 T01 §4 -- registers the `PDF from Office, HTML, Markdown & Images` card and file-type applicability rules into the Convert page (`§4`).
- -> XREF: D04 T02 §2 -- `office_to_pdf.cpp` (§1, §2), `legacy_office_to_pdf.cpp` (§5, §6), and `html_md_to_pdf.cpp` (§3) emit vector PDFs via `D04 T02 §2`'s `PdfWriter` and `D04 T02 §1`'s `AtomicFileWriter`.
- -> XREF: D04 T03 §1 -- `office_to_pdf.cpp` (§1, §2) reads OpenXML `.docx`, `.xlsx`, and `.pptx` ZIP packages via `D04 T03 §1`'s `zip_archive.cpp`.
- -> XREF: D06 T01 §6 -- `ConvertSuite.OfficeAndHtmlRoundTrip` and `ConvertSuite.LegacyBinaryOfficeRoundTrip` (`D06 T01 §6`) verify multi-format Office and legacy binary Office conversion quality built in §6 and §7 here.
- -> XREF: D06 T02 §2 -- the security audit in `D06 T02 §2` re-runs the macro-stream stripping (`vbaProject.bin`, `_VBA_PROJECT_CUR`, `Macros`, `VBA` on `macro-canary.docm` and `macro-canary.doc`), HTML external network/script blocking, and zero-external-subprocess proofs built here, and `fuzz_openxml_zip` (`D06 T02 §3`) fuzzes both the OpenXML (`ZipReader`) and OLE2 Compound File (`Ole2Reader`) parsers (`§1`, `§2`, `§4`).
- -> XREF: D04 T05 §12 -- validates PDF to PowerPoint output (D04 T05 §12).
- -> XREF: D02 T01 §5 -- open dialog accepts .ofd.
- -> XREF: D06 T02 §3 -- fuzz_odf.
- -> XREF: D06 T02 §3 -- fuzz_ofd.
- -> XREF: D06 T02 §3 -- fuzz_mime.
- -> XREF: D06 T02 §3 -- fuzz_msg.
- -> XREF: D02 T03 §8 -- attachments ask before opening.
- -> XREF: D04 T03 §1 -- ZIP reader.
- -> XREF: D10 T01 §8 -- the Office to PDF Step 3 options control reads the generated `recto_tokens.g.h` tokens.
- -> XREF: D10 T02 §9 -- the page size and orientation choices are `ui::ComboBox` and button controls.
- -> XREF: D10 T03 §1 -- the options control exposes automation names through the `recto-ui` UIA provider tree.

## Outcome

- Any Windows 11 machine converts `.docx`, `.docm`, and `.rtf` (`§1`), `.xlsx`, `.xlsm`, `.csv`, `.pptx`, and `.pptm` (`§2`), `.html` and `.md` (`§3`), and legacy binary `.doc`, `.xls`, and `.ppt` (`§4`-`§6`) files to vector PDFs out of the box using Recto's 100% built-in C++23 layout engines (`src/core/office_to_pdf.cpp`, `src/core/ole2_cfb.cpp`, `src/core/legacy_office_to_pdf.cpp`, and `src/core/html_md_to_pdf.cpp`), with zero external software installed, zero COM calls, zero child processes, and zero remote network or script execution.
- `Ole2Reader` (`§4`) parses `[MS-CFB]` Compound File Binary containers with strict sector-chain cycle detection and a `256 MB` allocation cap while stripping `_VBA_PROJECT_CUR`, `Macros`, and `VBA` macro storages by construction.
- Legacy Word 97-2003 `.doc` (`§5`), Excel 97-2003 `.xls` BIFF8, and PowerPoint 97-2003 `.ppt` (`§6`) binary streams lower directly into the shared `FlowDocumentModel`, spreadsheet model, and slide model, with `macro-canary.docm` and `macro-canary.doc` proving that embedded VBA macros are completely inert.
- The Convert page (`§7`, `SPEC 9.5`) ties all native C++23 engines together cleanly.
- OpenDocument (`.odt`, `.ods`, `.odp`), OFD, Outlook `.msg` and `.eml` email, MHTML and plain-text-family files convert to PDF through Recto's native engines (`§8` to `§22`), with macros stripped, nothing fetched from the network, and each refusal or approximation named, matching what PDFelement and Foxit PDF Editor accept beyond Acrobat.

**Adjacency:** all=not-applicable (converter engines inside `D04 T01`'s Convert frame; default page-setup preference is owned by `D02 T04 §5` and read here)

**Adjacency rationale:** The `PDF from Office, HTML, Markdown & Images` card, queue UI, and settings selector live in `D04 T01` and `D02 T04 §5`. This file owns the 100% native C++23 OpenXML, OLE2 legacy binary Office, RTF, HTML, Markdown, and CSV engines and their macro/script-safety proofs.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Built-in C++23 Word (`.docx`, `.docm`) and Rich Text (`.rtf`) to PDF layout engine | D04 T02 §2, D04 T03 §1 | [ ] |
| 2 | §2 | Built-in C++23 Excel (`.xlsx`), `.csv`, and PowerPoint (`.pptx`) to PDF layout engines | §1 | [ ] |
| 3 | §3 | Built-in C++23 `HTML/CSS` (`.html`) and `Markdown` (`.md`) to PDF engine and CLI verbs | §1 | [ ] |
| 4 | §4 | Native C++23 OLE2 Compound File (`[MS-CFB]`) reader and VBA macro stream stripper | §1 | [ ] |
| 5 | §5 | Native C++23 legacy Word 97-2003 (`.doc`) binary parser to `FlowDocumentModel` & PDF | §4 | [ ] |
| 6 | §6 | Native C++23 legacy Excel (`.xls` BIFF8) and PowerPoint (`.ppt`) binary parsers to PDF | §2, §4 | [ ] |
| 7 | §7 | Convert **PDF from Office, HTML, Markdown & Images** card and Step 3 UI (`SPEC 9.5`) | §3, §5, §6, D04 T01 §4, D02 T04 §5, D04 T01 §6, D10 T02 §2 | [ ] |
| 8 | §8 | ODF package reader, style resolver and macro stripper (`odf_package.cpp`) | §1, D04 T03 §1 | [ ] |
| 9 | §9 | OpenDocument Text (`.odt`) to PDF layout engine | §8, §1 | [ ] |
| 10 | §10 | OpenDocument Spreadsheet (`.ods`) to PDF layout engine | §8, §2 | [ ] |
| 11 | §11 | OpenDocument Presentation (`.odp`) to PDF layout engine | §8, §9, §2 | [ ] |
| 12 | §12 | Native C++23 OFD (GB/T 33190) package and document reader | §1, D04 T03 §1 | [ ] |
| 13 | §13 | OFD page graphics to PDF: paths, images, clips, colours and templates | §12, D04 T02 §2 | [ ] |
| 14 | §14 | OFD text and fonts to PDF with font subsets and Unicode mapping | §13, D03 T04 §4, D04 T02 §11 | [ ] |
| 15 | §15 | OFD outlines, links, attachments, annotations and seal report | §14 | [ ] |
| 16 | §16 | MIME message parser (RFC 5322, RFC 2045 to 2049) with bounds and fuzz target | -- | [ ] |
| 17 | §17 | Outlook `.msg` ([MS-OXMSG]) reader over the CFB reader | §4 | [ ] |
| 18 | §18 | Email (`.eml`, `.msg`) to PDF with attachments that ask before opening | §16, §17, §3, §1, §20, D04 T02 §2 | [ ] |
| 19 | §19 | MHTML (`.mht`, `.mhtml`) to PDF through the HTML engine | §16, §3 | [ ] |
| 20 | §20 | Plain-text family (`.txt`, `.log`, `.ini`, `.cfg`, `.xml`) to PDF, encoding detected | §1 | [ ] |
| 21 | §21 | `recto-cli office-to-pdf` dispatch for the new input formats | §9, §10, §11, §15, §18, §19, §20, D04 T02 §23 | [ ] |
| 22 | §22 | Convert card inputs for ODF, OFD, email, MHTML, text and images, and open `.ofd` | §7, §21, D04 T02 §15, D10 T01 §8, D10 T01 §11, D10 T02 §2, D10 T02 §3, D10 T03 §1 | [ ] |

## 1. Built-in C++23 Word (`.docx`, `.docm`) and Rich Text (`.rtf`) to PDF layout engine

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
- [ ] Commit: `feat(convert): implement built-in C++23 Word (.docx, .docm) and RTF to PDF layout engine`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.BuiltinDocx*:OfficeToPdf.BuiltinRtf*` passes under ASan.

## 2. Built-in C++23 Excel (`.xlsx`), `.csv`, and PowerPoint (`.pptx`) to PDF layout engines

Extend `src/core/office_to_pdf.cpp` with native C++23 layout engines for **Excel (`.xlsx`, `.xlsm`) and `.csv`** and **PowerPoint (`.pptx`, `.pptm`)**, plus `recto-cli office-to-pdf <in.docx|doc|xlsx|xls|pptx|ppt|rtf|csv> --out <out.pdf>`:
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
- [ ] Commit: `convert: implement built-in C++23 Excel (.xlsx, .csv) and PowerPoint (.pptx) to PDF engines`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.Builtin*:Cli.OfficeToPdf` passes under ASan across `.docx`, `.docm`, `.rtf`, `.xlsx`, `.csv`, and `.pptx` corpus files.

## 3. Built-in C++23 `HTML/CSS` (`.html`) and `Markdown` (`.md`) to PDF engine and CLI verbs

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
- [ ] Define `HtmlResourceProvider` in `src/core/html_md_to_pdf.h` (`resolve(url) -> bytes` or a typed miss) with a folder provider (the existing rule: relative files strictly under the input's folder) and an in-memory provider that serves only the parts of a MIME archive or email, so MHTML (`§19`) and email (`§18`) resolve `cid:` and `Content-Location` references from inside the file and nothing else. Done when: `HtmlMdToPdf.ResourceProviders` renders an image from each provider and a reference outside the provider is a typed miss with no file or network access.
- [ ] Commit output PDFs via `AtomicFileWriter` (`D04 T02 §1`) and check `CancelToken` across layout and pagination passes. Done when: cancelling mid-render leaves zero temp files.
- [ ] Implement `recto-cli html-to-pdf <in.html> --out <out.pdf> [--page-size a4|letter]` and `recto-cli md-to-pdf <in.md> --out <out.pdf> [--page-size a4|letter]` in `src/cli/`. Done when: `recto-tests.exe --gtest_filter=HtmlMdToPdf.*:Cli.HtmlAndMdToPdf` passes under ASan.
- [ ] Commit: `feat(convert): implement built-in C++23 HTML/CSS and Markdown to PDF layout engine`

**Test checkpoint:** `recto-tests.exe --gtest_filter=HtmlMdToPdf.*:Cli.HtmlAndMdToPdf` passes under ASan.

## 4. Native C++23 OLE2 Compound File (`[MS-CFB]`) reader and VBA macro stream stripper

Implement `src/core/ole2_cfb.h` and `src/core/ole2_cfb.cpp` in `recto-core`: a 100% pure native C++23 reader for Microsoft Compound File Binary (`[MS-CFB]`, `D0 CF 11 E0 A1 B1 1A E1`) containers used by legacy `.doc`, `.xls`, and `.ppt` files, parsing v3 (`512`-byte sectors) and v4 (`4096`-byte sectors) headers, DIFAT, FAT, MiniFAT (`64`-byte mini-sectors below `4096` bytes), and the red-black directory tree with strict cycle detection, bounds checks, and a `256 MB` total stream allocation cap, while **stripping all VBA macro and ActiveX storages (`_VBA_PROJECT_CUR`, `Macros`, `VBA`, `\x01CompObj` executable controls)** during directory enumeration so macro bytecode is never exposed to higher layers.

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
- [ ] Commit: `feat(convert): implement native C++23 OLE2 Compound File ([MS-CFB]) reader and macro stripper`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Ole2Cfb.*` passes under ASan across valid, MiniStream, macro-bearing, and cyclic/malformed `[MS-CFB]` fixtures.

## 5. Native C++23 legacy Word 97-2003 (`.doc`) binary parser to `FlowDocumentModel` & PDF

Implement the native C++23 legacy Word 97-2003 (`.doc`, `[MS-DOC]`) binary parser in `src/core/legacy_office_to_pdf.h` and `src/core/legacy_office_to_pdf.cpp` over §4's `Ole2Reader`, lowering `.doc` files directly into §1's `FlowDocumentModel` and `PdfWriter`: parse the `WordDocument` stream File Information Block (`Fib`), locate the `0Table` or `1Table` stream (`fWhichTblStm`), parse the `Clx` piece table (`Prc` + `PlcfPcd` `Pcd` descriptors mapping CP character positions to compressed 8-bit Windows-1252 or uncompressed 16-bit UTF-16LE runs in `WordDocument`), apply `PlcfBtePapx` (`PAPX` paragraph formatting, heading styles, lists, and `inTable`/`itap` table cell/row markers `0x0007`) and `PlcfBteChpx` (`CHPX` character bold/italic/underline/font-size/colour sprms), and verify macro-inertness on both `macro-canary.docm` and `macro-canary.doc`.

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
- [ ] Commit: `feat(convert): implement native C++23 legacy Word (.doc) parser and macro-inertness proof`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.LegacyDoc*:OfficeToPdf.MacroCanaryInert` passes under ASan.

## 6. Native C++23 legacy Excel (`.xls` BIFF8) and PowerPoint (`.ppt`) binary parsers to PDF

Extend `src/core/legacy_office_to_pdf.cpp` with 100% native C++23 binary parsers for **legacy Excel 97-2003 (`.xls`, `[MS-XLS]` BIFF8)** and **legacy PowerPoint 97-2003 (`.ppt`, `[MS-PPT]`)** over §4's `Ole2Reader`, lowering them into §2's `SpreadsheetWorkbookModel` and `PresentationDeckModel`:
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
- [ ] Commit: `convert: add native C++23 legacy Excel (.xls BIFF8) and PowerPoint (.ppt) binary parsers`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.Legacy*:Cli.OfficeToPdf` passes under ASan across `.doc`, `.xls`, and `.ppt` fixtures.

## 7. Convert **PDF from Office, HTML, Markdown & Images** card and Step 3 UI (`SPEC 9.5`)

Register the **PDF from Office, HTML, Markdown & Images** card in `ConverterRegistry` (`D04 T01 §3`) and build its Step 3 options control (`src/app/views/convert/office_to_pdf_options_control.h/.cpp`, `SPEC 9.5`): page size (`Match original document`, `A4`, `Letter`, seeded from `D02 T04 §5`'s `Default Office to PDF page setup` setting), orientation (`Auto`, `Portrait`, `Landscape`), margins (`Default`, `Narrow`, `None`), `Fit all spreadsheet columns on one page` toggle, `Generate PDF bookmarks from headings and slide titles` toggle, `Combine all input files into a single PDF` toggle, and the SPEC Principle 4 capability hint line.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 9.5 (`PDF from Office, HTML, Markdown & Images` card enable/disable rule and Step 3 options) and 9.7 (Converting: `Default Office to PDF page setup`) with `D-Convert.png`.
**Job:** The user can drop `.docx`, `.docm`, `.doc`, `.xlsx`, `.xlsm`, `.xls`, `.pptx`, `.pptm`, `.ppt`, `.rtf`, `.html`, `.md`, `.csv`, `.svg`, `.xps`, or image files into the Convert page, configure page setup, spreadsheet column fitting, bookmark generation, and single-PDF combining, and convert them to PDF through the queue using Recto's 100% built-in C++23 engines. Consumer: `ConvertPage` (`D04 T01 §3`-`§4`), `ConversionRunner` (`D04 T01 §1`), and `office_to_pdf` / `legacy_office_to_pdf` / `html_md_to_pdf` / `images_to_pdf`.
**Treatment:** Step 3 options control bound to `OfficeToPdfOptionsViewModel`, reading the default page size from `SettingsModel::default_office_page_setup()` (`D02 T04 §5`) and dispatching `.docx`/`.docm`/`.xlsx`/`.xlsm`/`.pptx`/`.pptm`/`.rtf`/`.csv` to `office_to_pdf` (`§1`, `§2`), `.doc`/`.xls`/`.ppt` to `legacy_office_to_pdf` (`§5`, `§6`), `.html`/`.md` to `html_md_to_pdf` (`§3`), and images/SVG/XPS to `images_to_pdf` (`D04 T02 §5`), optionally merging all outputs into a single combined PDF when `Combine all input files into a single PDF` is checked. Cheaper substitute that fails the checkpoint: requiring any external Office suite to be installed, or omitting spreadsheet column fitting and heading bookmark toggles.
**Chrome:** consume the Convert Step 2 card and Step 3 options host from `D04 T01 §3`-`§4` and the default page setup setting from `D02 T04 §5`.

**Build order:**

1. Implement `OfficeToPdfOptionsViewModel` and `office_to_pdf_options_control.h/.cpp` (`SPEC 9.5`), seeding its initial `Page size` from `D02 T04 §5`'s `Default Office to PDF page setup` preference and registering the **PDF from Office, HTML, Markdown & Images** card in `ConverterRegistry`. Done when: loading `.docx`, `.doc`, `.xlsx`, `.xls`, `.pptx`, `.ppt`, `.rtf`, `.html`, `.md`, `.csv`, or image files in Step 1 enables the card and renders the Step 3 options control.
2. Wire the job factory to dispatch each input file to Recto's native C++23 engines (`office_to_pdf`, `legacy_office_to_pdf`, `html_md_to_pdf`, `images_to_pdf`) and merge outputs when `Combine all input files into a single PDF` is enabled. Done when: `OfficeToPdf.ConvertPageUi` converts `.docx`, `.doc`, `.xlsx`, `.xls`, `.pptx`, `.ppt`, `.rtf`, `.html`, and `.md` files through the Convert page with zero external dependencies.

- [ ] Register the **PDF from Office, HTML, Markdown & Images** card in `ConverterRegistry` (`D04 T01 §3`), enabled whenever Step 1 contains at least one `.docx`, `.docm`, `.doc`, `.xlsx`, `.xlsm`, `.xls`, `.pptx`, `.pptm`, `.ppt`, `.rtf`, `.html`, `.htm`, `.md`, `.csv`, `.xml`, `.txt`, `.png`, `.jpg`, `.jpeg`, `.tif`, `.tiff`, `.bmp`, `.gif`, `.svg`, or `.xps` file, with disable reason `"Add Word, Excel, PowerPoint, HTML or Markdown files to use this"` when only PDFs are loaded. Done when: `OfficeToPdf.CardEnablement` verifies enablement and disable reasons.
- [ ] Build `OfficeToPdfOptionsControl` (`src/app/views/convert/office_to_pdf_options_control.h/.cpp`) exposing `Page size` (`Original`, `A4`, `Letter`, initialized from `D02 T04 §5`), `Orientation` (`Auto`, `Portrait`, `Landscape`), `Margins` (`Default`, `Narrow`, `None`), `Fit all spreadsheet columns on one page` (`true`), `Generate PDF bookmarks from headings` (`true`), `HTML/Markdown CSS print styling` (`true`), and `Combine all files into one PDF` (`false` for single input, toggleable for `>= 2` inputs). Done when: all controls pass `D03 T02 §3` accessibility checks.
- [ ] Render the SPEC Principle 4 capability hint line (`"Converts Office (.docx, .doc, .xlsx, .xls, .pptx, .ppt, .rtf), HTML, Markdown, CSV and images to vector PDF natively with zero external dependencies."`). Done when: the hint line is visible in Step 3.
- [ ] Dispatch queued jobs across `office_to_pdf` (`§1`, `§2`), `legacy_office_to_pdf` (`§5`, `§6`), `html_md_to_pdf` (`§3`), and `images_to_pdf` (`D04 T02 §5`). Done when: `OfficeToPdf.ConvertPageUi` runs `.docx`, `.doc`, `.xlsx`, `.xls`, `.pptx`, `.ppt`, `.rtf`, `.html`, and `.md` conversions through `ConversionRunner` and verifies the produced PDFs. Cheaper substitute that fails the checkpoint: ignoring Step 3 page-setup options or failing on legacy `.doc`/`.xls`/`.ppt` inputs.
- [ ] Support `Open file` in `QueueCard` and completion toasts (`D04 T01 §6`) so clicking `Open file` on a finished `Office -> PDF` job opens the newly created PDF directly in a Recto viewer tab through `JobOutputActions::open_output_file` (`D04 T01 §6`), which opens it with `OpenDocumentPipeline` (`D02 T01 §5`). Done when: clicking `Open file` opens the converted PDF in a new tab.
- [ ] Add `OfficeToPdf.CardEnablement` and `OfficeToPdf.ConvertPageUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=OfficeToPdf.CardEnablement:OfficeToPdf.ConvertPageUi` passes.
- [ ] Commit: `convert: wire 100% native Office, Legacy Office, HTML and Markdown to PDF Convert UI`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.*:HtmlMdToPdf.*:Ole2Cfb.*` passes, and display-session screenshots confirm the `PDF from Office, HTML, Markdown & Images` card and Step 3 options.

## 8. ODF package reader, style resolver and macro stripper (`odf_package.cpp`)

WPS and LibreOffice users hold OpenDocument files, and PDFelement converts `.odt`, `.ods` and `.odp`; Acrobat does not. This section is the safe front door for all three: `OdfPackage` (`src/core/odf_package.h/.cpp`, `recto::core`) opens an ODF ZIP package through `ZipReader` (`D04 T03 §1`), identifies the kind from the `mimetype` entry (`application/vnd.oasis.opendocument.text`, `.spreadsheet`, `.presentation`) rather than the extension, and exposes only the parts a layout engine needs (`content.xml`, `styles.xml`, `meta.xml`, `META-INF/manifest.xml`, and the images and replacement pictures the manifest lists). Macros never run and are never read: the `Basic/`, `Scripts/` and `Dialogs/` folders, `<office:scripts>`, every `script:event-listener`, embedded OLE objects (only their `ObjectReplacements/` picture is used) and `Configurations2/` are stripped by construction, external links are never fetched, and password-protected packages (`manifest:encryption-data`) are refused with a typed `PasswordProtectedOdf` error and a plain message, which records the gap. The shared `OdfStyleSheet` resolves automatic and common styles, parent styles, list styles and ODF lengths (`cm`, `mm`, `in`, `pt`, `pc`, `px`) for the three engines. It matches PDFelement's OpenDocument input and keeps Recto's native, no-Office, no-macro rule (D7, D9, D10).

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `OdfPackage::open`, kind detection, the part allow-list, the macro and script stripping and the manifest checks in `src/core/odf_package.cpp` over `ZipReader` and the bounded XML parser of `§1`. Done when: `OdfPackage.OpenAndKinds` opens an `.odt`, `.ods` and `.odp` fixture, identifies each by `mimetype`, and `OdfPackage.MacroCanaryInert` proves no stripped part is ever opened.
2. Implement `OdfStyleSheet` (style inheritance, list styles, length units, page layouts) and the encrypted-package refusal. Done when: `OdfPackage.StyleResolution` and `OdfPackage.EncryptedRefused` pass under ASan.

- [ ] Define `OdfKind`, `OdfPackage` (`open`, `kind`, `part`, `manifest`, `meta`) and `OdfStyleSheet` in `src/core/odf_package.h`, returning `PdfResult<OdfPackage>`. Done when: the header compiles across `recto-core`, `recto-services` and `recto-cli` with no COM, Office or WebView type.
- [ ] Read the `mimetype` entry (the first entry, stored uncompressed) and the manifest, set the kind from the media type, and refuse a package whose `mimetype` is missing, wrong, or disagrees with a manifest media type with `CorruptStructure`. Done when: `OdfPackage.OpenAndKinds` covers a renamed `.odt` that is really a spreadsheet (opened as a spreadsheet) and a ZIP without `mimetype` (refused).
- [ ] Allow-list the readable parts and make the rest unreachable: `Basic/`, `Scripts/`, `Dialogs/`, `Configurations2/`, `Thumbnails/` and any manifest entry with a script media type return `NotFound` from `part()`, `<office:scripts>` and `script:event-listener` elements are dropped during the XML pass, and OLE object folders expose only `ObjectReplacements/`. Done when: `OdfPackage.MacroCanaryInert` opens `tests/corpus/convert/macro-canary.odt` (with `Basic/Standard/Module1.xml`, a document-open event listener and a `Scripts/python/` file), converts it, and shows through an instrumented reader that no stripped part was opened and the canary file was never created. Cheaper substitute: relying on the layout engine to ignore script content it has already read.
- [ ] Refuse any package whose manifest holds `manifest:encryption-data` with `PasswordProtectedOdf`, the message `"This OpenDocument file is password protected. Recto cannot open protected OpenDocument files yet."`, and a gap line in `docs/user-demand.md`. Done when: `OdfPackage.EncryptedRefused` returns the error without reading the encrypted entries.
- [ ] Enforce limits shared with `ZipReader` and the XML parser: at most `256 MB` per part, a decompression ratio cap, XML depth `128`, `5,000,000` elements, no external entity or DTD loading, and `xlink:href` accepted only when it names a package entry (no `http:`, `file:` or UNC target). Done when: `OdfPackage.Limits` rejects a zip bomb, a billion-laughs entity and an external image link without reading outside the package.
- [ ] Implement `OdfStyleSheet` resolving `style:parent-style-name` chains (cycle-safe, depth capped at `32`), automatic versus common styles, `text:list-style`, `style:master-page` and page layouts, and ODF lengths to points. Done when: `OdfPackage.StyleResolution` resolves inherited bold, font size, margins and a `1.5in` length to `108 pt`.
- [ ] Add `fuzz/fuzz_odf.cpp` opening and style-resolving arbitrary bytes, and the `OdfPackage.*` tests in `tests/recto-tests/odf_package_tests.cpp` with fixtures from `scripts/build-corpus.py` recorded in `tests/corpus/PROVENANCE.md`. Done when: `recto-tests.exe --gtest_filter=OdfPackage.*` passes under ASan and `fuzz_odf` runs `60 s` with no finding.
- [ ] Commit: `convert: add the ODF package reader, style resolver and macro stripping`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OdfPackage.*` passes under ASan, and reading the `Basic/` folder makes `OdfPackage.MacroCanaryInert` fail.

## 9. OpenDocument Text (`.odt`) to PDF layout engine

Lower an `.odt` (`§8`) into the shared `FlowDocumentModel` of `§1` and render it through the same DirectWrite paginator, so ODT output has the quality and the `/Outlines` bookmarks of `.docx`. The engine reads `office:text` in `content.xml`: `text:p` and `text:h` (outline level to heading bookmarks), `text:span`, `text:a` (links become `/Link` annotations and are never fetched), `text:tab`, `text:line-break`, `text:s`, lists with their list styles, tables with column and row spans, frames and images inside the package, headers and footers from the master page, page size and margins from the page layout, and the page-number field. Tracked changes show the final text. Footnotes and endnotes are rendered as numbered notes at the end of the section with a `FootnotesMovedToEnd` warning. Objects it cannot draw (formulas, charts, OLE) use their stored replacement picture, or a labelled placeholder and a warning, never a silent gap. It matches PDFelement's `.odt` input without LibreOffice.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `parse_odt_to_flow_model(const OdfPackage&, CancelToken)` in `src/core/odf_to_pdf.cpp` producing `FlowDocumentModel` for paragraphs, headings, spans, lists, tables, frames and images. Done when: `OdfToPdf.OdtParse` parses single-column, list, table and image fixtures under ASan.
2. Add page layout, headers, footers, fields, links, notes and the render through `§1`'s paginator, then route `.odt` through `convert_office_to_pdf`. Done when: `OdfToPdf.OdtRender` converts fixtures into multi-page vector PDFs whose `PageText` holds every source word.

- [ ] Parse `text:p`, `text:h`, spans, `text:tab`, `text:line-break`, `text:s` and `text:a` with the style resolution of `§8` into `FlowDocumentModel`. Done when: `OdfToPdf.OdtParse` keeps bold, italic, underline, colour, font and size for a styled fixture.
- [ ] Parse lists (`text:list`, list styles, numbering formats, nesting) and tables (`table:table`, `number-columns-spanned`, `number-rows-spanned`, covered cells, borders and background) into the model, with repeated-column and repeated-row counts capped at `16384` and `1048576` and an aggregate budget of `4,000,000` expanded cells per table, checked with overflow-safe multiplication before any expansion. Done when: `OdfToPdf.OdtListsAndTables` renders a nested list and a table with merged cells, and a fixture that repeats non-empty rows and cells together (a `16384` by `1048576` expansion) is stopped by the aggregate budget with a warning and bounded memory.
- [ ] Apply the page layout (`fo:page-width`, `fo:page-height`, margins, orientation), master-page headers and footers, and the page-number and page-count fields in the paginator of `§1`. Done when: `OdfToPdf.OdtPageSetup` produces the fixture's page size to `0.1 pt` with header and footer text on every page.
- [ ] Place images and frames from the package (`draw:frame` with `draw:image` anchored as character, paragraph or page), use `ObjectReplacements/` pictures for objects, and emit a labelled placeholder with a warning for an object without one. Done when: `OdfToPdf.OdtImagesAndObjects` shows the image position, a replacement picture, and a placeholder warning for a chart without a replacement.
- [ ] Emit `/Outlines` from heading levels, `/Link` annotations for `text:a` (internal bookmarks to page destinations, external IRIs as URI actions that the viewer asks about), and the notes as numbered end-of-section notes with the `FootnotesMovedToEnd` warning. Done when: `OdfToPdf.OdtLinksAndNotes` populates the Bookmarks panel, resolves an internal link, and reports the warning.
- [ ] Commit the PDF through `AtomicFileWriter` (`D04 T02 §1`) checking `CancelToken` through parse, layout and pagination, and add `OdfToPdf.Odt*` tests in `tests/recto-tests/odf_to_pdf_tests.cpp`. Done when: cancelling mid-render leaves no temp file and `recto-tests.exe --gtest_filter=OdfToPdf.Odt*` passes under ASan. Cheaper substitute: converting through an installed office suite.
- [ ] Commit: `convert: add the OpenDocument Text to PDF layout engine`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OdfToPdf.Odt*` passes under ASan, and an engine that launches an office suite makes the zero-subprocess assertion in `OdfToPdf.OdtRender` fail.

## 10. OpenDocument Spreadsheet (`.ods`) to PDF layout engine

Lower an `.ods` (`§8`) into the `SpreadsheetWorkbookModel` of `§2` and render it through the spreadsheet paginator, matching PDFelement's `.ods` input. Cells use their stored values and number formats; formulas are never evaluated (the cached `office:value` or `office:string-value` is shown, and a formula with no cached value shows an empty cell with a `FormulaWithoutCachedValue` warning). Value types (`float`, `percentage`, `currency`, `date`, `time`, `boolean`, `string`), `number:*-style` formats, merged cells, column widths, row heights, hidden sheets, rows and columns, and print ranges are honoured. Repeated cells and rows are expanded with a cap so a hostile repeat count cannot allocate without bound. Charts use their replacement picture.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `parse_ods_to_workbook_model(const OdfPackage&, CancelToken)` in `src/core/odf_to_pdf.cpp` for sheets, rows, cells, value types and number styles. Done when: `OdfToPdf.OdsParse` reads strings, floats, percentages, currencies, dates and booleans with their displayed text under ASan.
2. Add merges, sizes, hidden items, print ranges, bookmarks, the render through `§2`'s paginator and the repeat caps. Done when: `OdfToPdf.OdsRender` converts a multi-sheet fixture into a vector PDF whose `PageText` and `TableExtractor` readback match the source cells.

- [ ] Parse `table:table` sheets, `table:table-row`, `table:table-cell` and covered cells, with `number-columns-repeated` and `number-rows-repeated` capped at `16384` columns and `1048576` rows per sheet. Keep a trailing empty run (empty cells after the last non-empty cell of a row, and empty rows after the last non-empty row of a sheet) as a sparse run length that is never materialized or iterated; every other cell position, empty or not, counts against an aggregate budget of `4,000,000` materialized cell positions per sheet, computed as row repeat times the larger of `1` and the row's positions up to its last non-empty cell (so interior empty rows count too) with overflow-safe multiplication before any expansion, and the sheet is cut at the budget with a `RepeatBudgetExceeded` warning. Done when: `OdfToPdf.OdsRepeatCaps` stops each of these in bounded time and memory with the warning: a fixture that repeats non-empty cells across `16384` columns and non-empty rows `1048576` times; a row of `16383` repeated empty cells followed by one non-empty cell, repeated `1048576` times; a row with interior empty repeats between non-empty cells, repeated likewise; and a repeat of `2^31`. The test also asserts that `1048575` repeated empty rows before one non-empty single-cell row count `1048576` positions against the budget, and that a trailing `1048576`-row empty repeat of `16384` empty cells converts with no warning.
- [ ] Map `office:value-type` and the `number:number-style`, `number:currency-style`, `number:percentage-style`, `number:date-style` and `number:time-style` definitions to display strings through the number format path of `§2`, using the cached values and never evaluating a formula. Done when: `OdfToPdf.OdsFormats` shows a currency, a percentage, a date and a boolean as the spreadsheet displays them and a formula without a cached value yields an empty cell and the warning.
- [ ] Apply merges (`number-columns-spanned`, `number-rows-spanned`), column widths, row heights, cell borders and fills, and skip hidden sheets, rows and columns (`table:visibility`). Done when: `OdfToPdf.OdsLayout` renders merged headers and omits hidden columns.
- [ ] Honour `table:print-ranges` when present and the `fit_spreadsheet_columns` option of `§1`, emit one `/Outlines` entry per visible sheet name, and use chart replacement pictures. Done when: `OdfToPdf.OdsPrintRange` prints only the print range and the Bookmarks panel lists the sheet names.
- [ ] Add `OdfToPdf.Ods*` tests in `tests/recto-tests/odf_to_pdf_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=OdfToPdf.Ods*` passes under ASan.
- [ ] Commit: `convert: add the OpenDocument Spreadsheet to PDF layout engine`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OdfToPdf.Ods*` passes under ASan, evaluating formulas instead of reading cached values makes `OdfToPdf.OdsFormats` fail, and counting only non-empty cells against the budget makes the leading-empty-repeat case of `OdfToPdf.OdsRepeatCaps` fail.

## 11. OpenDocument Presentation (`.odp`) to PDF layout engine

Lower an `.odp` (`§8`) into the `PresentationDeckModel` of `§2` and render one PDF page per slide, matching PDFelement's `.odp` input. Slide size comes from the page layout, backgrounds and shapes from the master page and the slide (`draw:page`, `style:master-page`), and the engine draws `draw:frame`, `draw:text-box`, `draw:rect`, `draw:ellipse`, `draw:line`, `draw:polygon`, `draw:path` (SVG path data), `draw:custom-shape` presets it can resolve, `draw:image` and tables inside frames. Speaker notes are skipped unless the option is on. Transitions and animations are ignored and reported once. Each slide gets a bookmark from its title frame.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `parse_odp_to_deck_model(const OdfPackage&, CancelToken)` in `src/core/odf_to_pdf.cpp` for slides, masters, backgrounds and shapes. Done when: `OdfToPdf.OdpParse` resolves slide size, background fills and positioned shapes under ASan.
2. Render text frames, images, tables and notes through `§2`'s presentation renderer, with bookmarks. Done when: `OdfToPdf.OdpRender` converts a multi-slide fixture into one PDF page per slide with selectable text.

- [ ] Parse `draw:page` slides, their master pages, `presentation:class` placeholders and drawing page backgrounds, with the slide size from `style:page-layout-properties`. Done when: `OdfToPdf.OdpParse` reads a 16:9 and a 4:3 fixture at the right page sizes.
- [ ] Parse shapes: `draw:frame` and `draw:text-box` (paragraphs and lists through the text path of `§9`), `draw:rect`, `draw:ellipse`, `draw:line`, `draw:polygon`, `draw:path` (`svg:d` path data and `svg:viewBox`) and resolvable `draw:custom-shape` presets, with `draw:transform`. Done when: `OdfToPdf.OdpShapes` renders each shape kind at its position and an unresolved preset becomes a labelled outline with a warning.
- [ ] Draw images and tables inside frames, gradient and solid fills, and strokes with dash styles via `PdfWriter` (`D04 T02 §2`). Done when: `OdfToPdf.OdpImagesAndTables` shows a picture and a 3 x 3 table with their text selectable.
- [ ] Add an `include_speaker_notes` option (default off) appending each slide's notes below the slide on a following page when on, and report ignored transitions and animations once. Done when: `OdfToPdf.OdpNotes` adds the notes pages only when on and records `TransitionsIgnored`.
- [ ] Emit one `/Outlines` entry per slide from its title frame, falling back to `Slide N`. Done when: `OdfToPdf.OdpBookmarks` lists every slide in the Bookmarks panel.
- [ ] Add `OdfToPdf.Odp*` tests in `tests/recto-tests/odf_to_pdf_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=OdfToPdf.Odp*` passes under ASan.
- [ ] Commit: `convert: add the OpenDocument Presentation to PDF layout engine`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OdfToPdf.Odp*` passes under ASan, and rasterizing the slides into pictures makes the `PageText` assertion in `OdfToPdf.OdpRender` fail.

## 12. Native C++23 OFD (GB/T 33190) package and document reader

OFD is China's open fixed-layout document standard (GB/T 33190), used for e-invoices, government documents and archives, and PDFelement converts it to PDF; Acrobat does not open it. `OfdReader` (`src/core/ofd_reader.h/.cpp`, `recto::core`) opens the ZIP package through `ZipReader` (`D04 T03 §1`), reads `OFD.xml` (`DocBody`, `DocInfo`, `DocRoot`), `Document.xml` (`CommonData` with `PageArea` and its `PhysicalBox`, `PublicRes` and `DocumentRes`, `Pages`, `Outlines`, `Attachments`), each page's `Content.xml` (`Template` references, `Content` layers) and the resource files (fonts, colour spaces, draw parameters, multimedia), into a typed `OfdDocument` model, and records signatures and seals as present without verifying them. OFD lengths are millimetres; the model converts them to points. Everything is bounded and treated as hostile: no script, no external fetch, ID references resolved with cycle and depth limits. It keeps Recto's native, offline, no-subprocess rule.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `OfdReader::open` for the package, `OFD.xml`, `Document.xml`, resources and the page list in `src/core/ofd_reader.cpp` over the bounded XML parser of `§1`. Done when: `OfdReader.OpenAndPages` opens a multi-page fixture, reports page boxes in points and resolves resource IDs.
2. Add page content parsing into object trees and the limits. Done when: `OfdReader.PageContent` and `OfdReader.Limits` pass under ASan.

- [ ] Define `OfdDocument` (`info`, `pages`, `public_res`, `document_res`, `outlines`, `attachments`, `signatures`), `OfdPage` (`physical_box_pt`, `content_box_pt`, `templates`, `layers`) and `OfdObject` variants (`Text`, `Path`, `Image`, `Composite`, `PageBlock`) in `src/core/ofd_reader.h`. Done when: the header compiles across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Parse `OFD.xml` and `Document.xml`, resolve `BaseLoc` paths inside the package only (no `..`, no absolute or UNC path), convert millimetres to points, and report a missing required element as `CorruptStructure`. Done when: `OfdReader.OpenAndPages` covers a valid package, a `BaseLoc` that escapes the package (refused) and a missing `DocRoot`.
- [ ] Parse resource files into id-keyed tables (`Fonts`, `ColorSpaces`, `DrawParams`, `MultiMedias`, `CompositeGraphicUnits`) with duplicate-ID detection and a reference-depth cap of `32`. Done when: `OfdReader.Resources` resolves a `DrawParam` chain and rejects a cyclic one without recursion overflow.
- [ ] Parse page `Content.xml` into the object tree (`Layer`, `PageBlock`, `TextObject`, `PathObject`, `ImageObject`, `CompositeObject`) with each object's `Boundary`, `CTM`, `Alpha`, `DrawParam` and clip, capping nesting at `32` levels and objects at `2,000,000` per page. Done when: `OfdReader.PageContent` loads each object kind and `OfdReader.Limits` refuses the nesting and object-count bombs and a decompression bomb.
- [ ] Record `Signs` and seal files (`Seal.esl`, signature XML) as `OfdSignatureInfo` (present, signer name text if plain, not verified) and ignore `CustomTags` and `Actions` of kind sound or movie. Done when: `OfdReader.SignaturesRecorded` lists a signed invoice fixture's signatures as unverified and runs no code.
- [ ] Add `fuzz/fuzz_ofd.cpp` and the `OfdReader.*` tests in `tests/recto-tests/ofd_reader_tests.cpp`, with a synthetic OFD built by `scripts/build-corpus.py` (generated from the public standard's structure, recorded in `tests/corpus/PROVENANCE.md`). Done when: `recto-tests.exe --gtest_filter=OfdReader.*` passes under ASan and `fuzz_ofd` runs `60 s` with no finding.
- [ ] Commit: `convert: add the native OFD (GB/T 33190) package and document reader`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfdReader.*` passes under ASan, and resolving a `BaseLoc` outside the package makes `OfdReader.OpenAndPages` fail.

## 13. OFD page graphics to PDF: paths, images, clips, colours and templates

Render the page object trees of `§12` into PDF content through `PdfWriter` (`D04 T02 §2`), so an OFD page keeps its geometry and look. OFD has its origin at the top left with y downward and lengths in millimetres, so each page gets one explicit flip matrix and text matrices are counter-flipped (text is `§14`). Paths come from `AbbreviatedData` (`S`, `M`, `L`, `Q`, `B`, `A`, `C` commands; quadratic curves are raised to cubic and arcs are split into cubic segments), with fill and stroke colours (RGB, grey, CMYK, alpha), line width, dash, cap, join and miter limit from the `DrawParam`. Images use the multimedia resource; PNG, JPEG, BMP, TIFF and GIF are embedded (JPEG unchanged with `DCTDecode`, others decoded through WIC and written lossless) and an unsupported image format becomes a labelled placeholder with a warning. Composite objects expand their graphic unit with the object's `CTM`; clips apply as PDF clip paths; layers keep their z-order and page templates are drawn first. Pattern fills draw as the average colour with a `PatternFillApproximated` warning; axial and radial shadings become PDF shadings.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the page transform, path conversion, colour and stroke mapping and clip handling in `src/core/ofd_to_pdf.cpp`. Done when: `OfdToPdf.PathsAndColours` renders a fixture of filled, stroked, dashed and clipped paths whose pixels match the expected render within `SSIM >= 0.98`.
2. Add images, composites, layers, templates and the warnings. Done when: `OfdToPdf.ImagesAndTemplates` passes under ASan.

- [ ] Convert each page to a PDF page with `/MediaBox` from the `PhysicalBox` and `/CropBox` from the `ContentBox`, one flip matrix `1 0 0 -1 0 H` and millimetre-to-point scaling applied once. Done when: `OfdToPdf.PageGeometry` checks the box sizes to `0.01 pt` and an object at a known millimetre position lands at the expected point.
- [ ] Convert `AbbreviatedData` (`S`, `M`, `L`, `Q`, `B`, `A`, `C`) to `m`, `l`, `c` and `h` with quadratic-to-cubic and arc-to-cubic conversion, honouring the `Fill` and `Stroke` flags and the even-odd or non-zero rule. Done when: `OfdToPdf.PathsAndColours` renders every command including an arc and a quadratic.
- [ ] Map colours and strokes (RGB, grey and CMYK `Value`, `Alpha` as `/ca` and `/CA`, `LineWidth`, `DashPattern`, `Cap`, `Join`, `MiterLimit`) through the `DrawParam` inheritance chain. Done when: `OfdToPdf.DrawParamInheritance` resolves a parameter inherited through two levels.
- [ ] Embed images from `MultiMedia` resources, passing JPEG through with `/DCTDecode` and writing PNG, BMP, TIFF and GIF losslessly with `/SMask` for alpha, and draw a labelled placeholder plus an `UnsupportedImageFormat` warning for a format it cannot decode. Done when: `OfdToPdf.ImagesAndTemplates` embeds a JPEG byte-identically and shows a placeholder for an unsupported format. Cheaper substitute: rasterizing the whole page.
- [ ] Expand `CompositeObject` graphic units with the object's `CTM`, apply clips, keep `Layer` z-order, and draw `Template` pages first by their z-order. Done when: `OfdToPdf.CompositesAndTemplates` shows a stamped composite, a clipped image and a template header under the body.
- [ ] Convert axial and radial shadings to PDF shadings, draw pattern fills as the average colour with `PatternFillApproximated`, and return the warnings in the job note. Done when: `OfdToPdf.Shadings` renders a gradient and records the pattern warning.
- [ ] Commit through `AtomicFileWriter`, check `CancelToken` per page, and add `OfdToPdf.*` tests in `tests/recto-tests/ofd_to_pdf_tests.cpp`. Done when: cancelling leaves no temp file and `recto-tests.exe --gtest_filter=OfdToPdf.*` passes under ASan.
- [ ] Commit: `convert: render OFD page graphics to PDF`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfdToPdf.*` passes under ASan, and skipping the flip matrix makes `OfdToPdf.PageGeometry` fail.

## 14. OFD text and fonts to PDF with font subsets and Unicode mapping

OFD text is the part that makes a converted invoice searchable or garbage, so it has its own section. A `TextObject` has a font reference, a size and a `TextCode` run with per-glyph positions (`X`, `Y`, `DeltaX`, `DeltaY`) and an optional `CGTransform` that maps code points to glyph indices. This section writes each run as PDF text with exact glyph positions, embeds the font as a Type 0 font (`CIDFontType2` for TrueType outlines, `CIDFontType0` for CFF outlines) with `Identity-H` and a `/ToUnicode` CMap when the package carries a font file (OpenType or TrueType, subset by `font_subsetter.cpp`, `D04 T02 §11`), and otherwise resolves the named font through DirectWrite with the `EditFontResolver` fallback order (`D03 T04 §4`), so CJK text never becomes `?`. Text colour, size, horizontal scale, weight and italic come from the object and its `DrawParam`. Extracted text from the converted PDF must equal the OFD text.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement run layout and the PDF text emission with per-glyph positions in `src/core/ofd_text.cpp`. Done when: `OfdText.GlyphPositions` places each glyph of a run at its `X`/`Y`/`DeltaX`/`DeltaY` position within `0.05 pt` and upright after the page flip.
2. Implement font resolution, embedding and the `/ToUnicode` map. Done when: `OfdText.FontsAndUnicode` extracts the same Unicode text back, for embedded and system fonts, under ASan.

- [ ] Parse `TextCode` runs into glyph lists applying `X`, `Y` and the `DeltaX`/`DeltaY` series (including the `g` repeat form), and convert them to `Tm`/`Tj` or `TJ` with the counter-flipped text matrix. Done when: `OfdText.GlyphPositions` covers absolute positions, delta series and a repeat form.
- [ ] Apply `CGTransform` glyph mappings (code position, glyph count and glyph indices) so ligatures and many-to-one mappings keep the right glyphs and the right Unicode. Done when: `OfdText.CgTransform` renders a fixture with a ligature and extracts the original characters.
- [ ] Embed package fonts by outline technology: a TrueType-outline font (`glyf`) becomes a `CIDFontType2` subset with `/FontFile2` through `font_subsetter.cpp` and `Identity-H`, keeping only the glyphs used, and a CFF-outline OpenType font becomes a `CIDFontType0` with `/FontFile3` `/Subtype /OpenType`, embedded whole because the subsetter handles `glyf` only, with a `FontSubsetUnavailable` warning, and refuse a font whose `OS/2 fsType` forbids embedding by falling back to a system font with a warning. Done when: `OfdText.EmbeddedFont` embeds a TrueType subset smaller than the source font and renders identical glyphs, `OfdText.CffFont` embeds a CFF fixture as `CIDFontType0` with the warning and renders and extracts it correctly, and a no-embedding font falls back with `FontEmbeddingRestricted`.
- [ ] Resolve fonts named without a file through DirectWrite (`FontName`, `FamilyName`, `Serif`, `Bold`, `Italic`, `FixedWidth` flags), then the per-character fallback of `EditFontResolver` for characters the font lacks, and never write `?` or `.notdef`. Done when: `OfdText.FontFallback` converts SimSun-named, Kai-named and Latin text on a machine without those fonts and extracts every character.
- [ ] Emit a `/ToUnicode` CMap for every font and carry text fill and stroke colour, size, `HScale`, weight and italic from the object and its `DrawParam`. Done when: `OfdText.FontsAndUnicode` extracts the OFD text exactly through `PageText` and the colour and size match.
- [ ] Add `OfdText.*` tests in `tests/recto-tests/ofd_text_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=OfdText.*` passes under ASan.
- [ ] Commit: `convert: convert OFD text and fonts to PDF with Unicode mapping`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfdText.*` passes under ASan, and writing glyph indices without a `/ToUnicode` map makes `OfdText.FontsAndUnicode` fail on extraction.

## 15. OFD outlines, links, attachments, annotations and seal report

Complete the OFD conversion with the document-level parts so nothing is silently dropped. `Outlines` become `/Outlines` bookmarks, `Goto` and `URI` actions become `/Link` annotations (URI links are only written, and the viewer asks before opening them), `Attachments` become PDF embedded files that ask before opening, `Annotations` with an appearance are drawn into the page as non-editable content, `DocInfo` fills `/Info` (title, author, creation date) and the XMP basics, and signatures and seals found by `§12` are listed in the job note as `"Contains N digital seals that Recto did not verify and that the PDF does not carry."`. The conversion never claims the PDF is signed.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement outlines, link actions, attachments and document info in `src/core/ofd_to_pdf.cpp`. Done when: `OfdToPdf.OutlinesLinksAttachments` shows the bookmark tree, a working internal and a URI link, and an embedded file.
2. Implement appearance annotations and the seal report. Done when: `OfdToPdf.AnnotationsAndSeals` draws an annotation appearance and lists the seals as unverified.

- [ ] Convert `Outlines` to `/Outlines` with destinations to the converted page index and top coordinate, and keep titles as Unicode. Done when: `OfdToPdf.OutlinesLinksAttachments` shows a two-level tree that opens the right pages.
- [ ] Convert `Goto` actions to internal `/Link` destinations and `URI` actions to `/Link` URI actions, ignoring every other action type and recording it once. Done when: `OfdToPdf.Links` resolves an internal link and a URI link and records `ActionsIgnored` for a movie action.
- [ ] Embed `Attachments` as PDF embedded files with sanitized names (no path, no reserved Windows names, no control characters) and the original media type, so the viewer asks before opening them (`D02 T03 §8`). Done when: `OfdToPdf.Attachments` embeds two files with safe names and no attachment is opened or run.
- [ ] Draw `Annotations` that carry an appearance object into the page content, and skip those without one with a warning. Done when: `OfdToPdf.AnnotationsAndSeals` shows a stamp annotation on the converted page.
- [ ] Fill `/Info` and the XMP basics from `DocInfo` and write the seal report to the job note and to the `recto-cli` JSON output. Done when: `OfdToPdf.AnnotationsAndSeals` finds the title and author in `/Info` and the seal count in the report, and the PDF has no signature field.
- [ ] Commit: `convert: add OFD outlines, links, attachments, annotations and seal report`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfdToPdf.OutlinesLinksAttachments:OfdToPdf.Links:OfdToPdf.Attachments:OfdToPdf.AnnotationsAndSeals` passes under ASan, and writing a signature field for a seal makes `OfdToPdf.AnnotationsAndSeals` fail.

## 16. MIME message parser (RFC 5322, RFC 2045 to 2049) with bounds and fuzz target

Email `.eml` files, `.mht` pages and Outlook attachments are all MIME. `MimeParser` (`src/core/mime_parser.h/.cpp`, `recto::core`) parses an RFC 5322 header block and an RFC 2045 to 2049 body tree into a `MimeEntity` tree: folded headers, RFC 2047 encoded words in headers, RFC 2231 parameter continuations and charsets, `Content-Type` and `Content-Disposition` parameters, `Content-Transfer-Encoding` (`7bit`, `8bit`, `binary`, `quoted-printable`, `base64`), nested `multipart/*` with boundary handling, and `message/rfc822` parts. Charsets convert to UTF-16 through the Windows code-page tables (`IMultiLanguage2::GetCharsetInfo`), with an unknown charset falling back to UTF-8 then Windows-1252 and a warning. It is a hostile-input parser: depth, part count, header and total sizes are capped, nothing is executed or fetched, and it returns `PdfResult<MimeEntity>`.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement header parsing, encoded words and parameters in `src/core/mime_parser.cpp`. Done when: `Mime.Headers` decodes folded, RFC 2047 and RFC 2231 headers from a fixture set including non-ASCII names.
2. Implement transfer decoding, multipart nesting, the limits and the fuzz target. Done when: `Mime.*` passes under ASan and `fuzz_mime` runs `60 s` with no finding.

- [ ] Define `MimeEntity` (`headers`, `content_type`, `disposition`, `filename`, `content_id`, `charset`, `decoded_bytes`, `children`, `warnings`) and `MimeParseOptions` (`max_depth` `16`, `max_parts` `2000`, `max_header_bytes` `1 MiB`, `max_total_bytes` `512 MiB`) in `src/core/mime_parser.h`. Done when: the header compiles across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Parse header blocks (unfolding, case-insensitive names, duplicate headers kept in order), RFC 2047 encoded words (`B` and `Q`) and RFC 2231 parameters (continuations, `charset'language'` percent encoding). Done when: `Mime.Headers` and `Mime.Rfc2231` decode a UTF-8 Chinese filename given as continuations and a Q-encoded subject.
- [ ] Decode `quoted-printable` and `base64` (tolerating line breaks and trailing garbage with a warning) and convert text parts from their charset through `IMultiLanguage2`, with the UTF-8 then Windows-1252 fallback and a warning for an unknown charset. Done when: `Mime.TransferAndCharset` decodes each encoding and a `charset=gb2312` and `iso-8859-1` part to the right Unicode.
- [ ] Parse `multipart/*` with boundary matching (including a boundary-like line inside a part), preamble and epilogue, and `message/rfc822` as a child entity, within the depth, part and size caps. Done when: `Mime.Multipart` and `Mime.Limits` parse nested multiparts and refuse a depth-17 nest, a 3000-part message and a 600 MiB body with typed errors.
- [ ] Sanitize attachment names (strip any path, drop control characters, replace reserved Windows names and `:`, cap length, keep the extension) in `MimeEntity::safe_filename()`. Done when: `Mime.SafeFilename` maps `..\..\evil.exe`, `CON.txt` and a 400-character name to safe names.
- [ ] Add `fuzz/fuzz_mime.cpp` and the `Mime.*` tests in `tests/recto-tests/mime_parser_tests.cpp` with fixtures from `scripts/build-corpus.py` recorded in `tests/corpus/PROVENANCE.md`. Done when: `recto-tests.exe --gtest_filter=Mime.*` passes under ASan and `fuzz_mime` runs `60 s` with no finding.
- [ ] Commit: `convert: add the bounded MIME message parser`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Mime.*` passes under ASan, and removing the part-count cap makes `Mime.Limits` fail.

## 17. Outlook `.msg` ([MS-OXMSG]) reader over the CFB reader

Foxit PDF Editor converts Outlook `.msg` messages, and Outlook users save mail that way. `MsgReader` (`src/core/msg_reader.h/.cpp`, `recto::core`) reads the property streams of a `[MS-OXMSG]` message from the `Ole2Reader` of `§4`: the top-level `__properties_version1.0` stream, the `__substg1.0_<tag><type>` variable-length property streams (Unicode `001F`, 8-bit `001E`, binary `0102`), the recipient storages `__recip_version1.0_#nnnnnnnn` and the attachment storages `__attach_version1.0_#nnnnnnnn`, into an `EmailMessageModel` (subject, sender, To, Cc, sent time, plain body, HTML body, compressed RTF body, recipients, attachments with name, media type, content ID and bytes, and embedded messages). The compressed RTF body is expanded by a bounded `[MS-OXRTFCP]` decompressor. Property tags are taken from `[MS-OXPROPS]` and recorded in one table that was checked against the specification when written. Because `Ole2Reader` already hides macro storages and `.msg` has none to run, nothing executes; rights-managed and S/MIME-encrypted messages are refused with a typed error.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the property stream and `__substg1.0_` readers and the recipient and attachment storages in `src/core/msg_reader.cpp`, with the verified tag table in `src/core/msg_props.h`. Done when: `MsgReader.Properties` reads subject, sender, recipients, body and a attachment from a fixture `.msg` under ASan.
2. Implement the `[MS-OXRTFCP]` decompressor, embedded messages and the refusals. Done when: `MsgReader.*` passes under ASan and `fuzz_msg` runs `60 s` with no finding.

- [ ] Define `EmailMessageModel` (`subject`, `from`, `to`, `cc`, `bcc`, `sent`, `plain_body`, `html_body`, `rtf_body`, `attachments`, `inline_images`, `embedded_messages`, `warnings`) and `MsgReader::open(const Ole2Reader&)` in `src/core/msg_reader.h`, shared by the EML adapter of `§18`. Done when: the header compiles across `recto-core` and `recto-cli`.
- [ ] Record each property tag and type used (subject, plain body, HTML body, compressed RTF, sender name and address, display To and Cc, sent time, message code page, recipient display name and SMTP address, attachment data, long and short file name, media type, content ID, attach method and flags) in `src/core/msg_props.h` and check each against `[MS-OXPROPS]`. Done when: `MsgReader.TagTable` passes and the header comment names the specification section for every tag.
- [ ] Read string properties as UTF-16LE (`001F`) or as 8-bit text in the message code page (`001E`), refusing a stream larger than `256 MB`, and read the recipient and attachment storages with their counts bounded at `2000`. Done when: `MsgReader.Properties` reads a Unicode message, a legacy 8-bit message in a `windows-1251` code page and a message with `2001` attachments (refused with a typed error).
- [ ] Implement a bounded `[MS-OXRTFCP]` decompressor (`LZFu` with the prefilled dictionary, the `MELA` uncompressed form, CRC check, `RAWSIZE` cap `256 MB`) and use the HTML body, then the RTF body, then the plain body. Done when: `MsgReader.CompressedRtf` expands a fixture to the original RTF and rejects a header with a lying `RAWSIZE`.
- [ ] Read attachments by value (attach method `1`) with their file name and content ID, and embedded messages (attach method `5`) as nested `EmailMessageModel`s to a depth of `4`; list OLE-object attachments (method `6`) by name without loading them. Done when: `MsgReader.Attachments` yields a file, an inline image, a nested message and a listed OLE object.
- [ ] Refuse messages whose class marks rights management or S/MIME encryption (`IPM.Note.SMIME`, `.RPMSG`) with `EncryptedMessage` and a plain message. Done when: `MsgReader.EncryptedRefused` returns the error and reads no body.
- [ ] Add `fuzz/fuzz_msg.cpp` and the `MsgReader.*` tests in `tests/recto-tests/msg_reader_tests.cpp`, with `.msg` fixtures built by `scripts/build-corpus.py` and recorded in `tests/corpus/PROVENANCE.md`. Done when: `recto-tests.exe --gtest_filter=MsgReader.*` passes under ASan and `fuzz_msg` runs `60 s` with no finding.
- [ ] Commit: `convert: add the Outlook .msg ([MS-OXMSG]) reader`

**Test checkpoint:** `recto-tests.exe --gtest_filter=MsgReader.*` passes under ASan, and trusting `RAWSIZE` without the cap makes `MsgReader.CompressedRtf` fail.

## 18. Email (`.eml`, `.msg`) to PDF with attachments that ask before opening

Foxit PDF Editor converts email to PDF; Acrobat does this only inside Outlook. This section renders an `EmailMessageModel` (from `.msg`, `§17`, or built from a `MimeEntity` tree for `.eml`, `§16`) as a PDF: a header block (From, To, Cc, Date as written with its offset, Subject), then the body, preferring HTML (through the HTML engine of `§3` with scripts off and nothing fetched) over RTF (the engine of `§1`) over plain text, with `cid:` images resolved from inside the message only and remote images replaced by a labelled placeholder and counted in a warning. Non-inline attachments are embedded in the PDF as embedded files with safe names, listed in a block under the header, and the viewer asks before opening them (`D02 T03 §8`); inline images stay in the body; nested messages are rendered as their own section after the parent, to nested levels `1` to `4` below the top-level message. Signed and encrypted messages are handled honestly: a `multipart/signed` body or an opaque signed `application/pkcs7-mime` body is shown and the signature is reported as not verified, and an encrypted (enveloped) body is refused with a typed error. Nothing is sent, opened, or looked up on the network.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Build the `.eml` adapter (`MimeEntity` to `EmailMessageModel`) and the header block and body renderer in `src/core/email_to_pdf.cpp`. Done when: `EmailToPdf.EmlRender` converts plain, HTML and multipart fixtures into PDFs whose `PageText` holds the headers and body.
2. Add the attachment embedding, inline images, nested messages, and the signed and encrypted handling, then `.msg` through the same renderer. Done when: `EmailToPdf.*` passes under ASan for `.eml` and `.msg`.

- [ ] Implement the `.eml` adapter choosing the body by `multipart/alternative` preference (HTML, then plain), collecting inline images by `Content-ID` and attachments by disposition and name, and mapping a `message/rfc822` child to a nested message. Done when: `EmailToPdf.EmlAdapter` builds the model for an alternative, a related and a mixed message.
- [ ] Render the header block and body: format the headers in a fixed layout with the display names and addresses escaped, show the Date header as written with its offset, route HTML through `html_md_to_pdf` with the in-message `HtmlResourceProvider` (`§3`), RTF through the RTF engine (`§1`) with `\objdata` objects skipped and reported, and plain text through the text path (`§20`). Done when: `EmailToPdf.BodyRoutes` renders each body kind and a hostile HTML body runs no script and makes no network request.
- [ ] Resolve `cid:` images from the message only, replace remote images (`http:`, `https:`) with a labelled placeholder and count them in the `RemoteImagesBlocked` warning. Done when: `EmailToPdf.InlineAndRemoteImages` shows an inline image drawn and a tracking pixel blocked and counted.
- [ ] Embed non-inline attachments as PDF embedded files with `MimeEntity::safe_filename()` names and their media type, list them in a block under the headers (name, size, type), and cap each at `512 MiB` and the total at `2 GiB`, reporting any skipped by the cap. Done when: `EmailToPdf.Attachments` embeds three attachments, lists them, opens none, and records a skipped oversized one. Cheaper substitute: dropping attachments, or opening or converting them during the conversion.
- [ ] Render nested `message/rfc822` and `.msg` attachments as following sections with a separator heading, counting the top-level message as level `0` and rendering nested levels `1` to `4`, and embed a message at level `5` or deeper as an attachment with a warning. Done when: `EmailToPdf.NestedMessages` renders a level-4 message, embeds a level-5 message, and leaves a level-6 message inside it unrendered.
- [ ] Handle `multipart/signed` (show the body, report `SignatureNotVerified`) and `application/pkcs7-mime` by inspecting the CMS content type through `CryptMsgOpenToDecode` and `CryptMsgGetParam` with `CMSG_TYPE_PARAM` rather than trusting the media type: signed-data (an opaque signed message) has its inner MIME content extracted without verification and shown with `SignatureNotVerified`, enveloped-data is refused with the typed `EncryptedMessage`, and unreadable CMS is refused as `CorruptStructure`. Done when: `EmailToPdf.SignedAndEncrypted` shows an opaque-signed body with the warning, refuses an enveloped one, and refuses a corrupt one.
- [ ] Commit through `AtomicFileWriter` (`D04 T02 §1`) checking `CancelToken`, and add `EmailToPdf.*` tests in `tests/recto-tests/email_to_pdf_tests.cpp`. Done when: cancelling leaves no temp file and `recto-tests.exe --gtest_filter=EmailToPdf.*` passes under ASan.
- [ ] Commit: `convert: add email (.eml and .msg) to PDF with embedded attachments`

**Test checkpoint:** `recto-tests.exe --gtest_filter=EmailToPdf.*` passes under ASan, and fetching a remote image makes the zero-network assertion in `EmailToPdf.InlineAndRemoteImages` fail.

## 19. MHTML (`.mht`, `.mhtml`) to PDF through the HTML engine

PDFelement converts web archives saved by Edge and Chrome. An MHTML file is a MIME `multipart/related` message whose first part is the page and whose other parts are its images and styles, found by `Content-Location` or `Content-ID`. `mhtml_to_pdf` (`src/core/mhtml_to_pdf.cpp`) parses it with the `MimeParser` of `§16`, hands the HTML part to the engine of `§3` through the in-memory `HtmlResourceProvider`, and resolves every resource from the archive only. A resource not in the file is not fetched: its image becomes a labelled placeholder and its stylesheet is skipped, both counted in a warning. Scripts stay off. This is the offline answer to saving a page: a saved page converts, while live web-page capture stays excluded by plan section 9.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the archive resolver and `mhtml_to_pdf` in `src/core/mhtml_to_pdf.cpp`. Done when: `Mhtml.Convert` converts an archive with an embedded image and stylesheet into a PDF showing both, with zero network requests.
2. Add the missing-resource handling and the security checks. Done when: `Mhtml.*` passes under ASan.

- [ ] Choose the main HTML part by the `type` and `start` parameters or the first `text/html` part, decode its charset, and index the other parts by `Content-Location` (absolute and relative) and `Content-ID`. Done when: `Mhtml.MainPartAndIndex` selects the right part in an archive with the page second.
- [ ] Resolve `src`, `href`, CSS `url()` and `@import` references against the archive index only, normalizing `cid:` and `Content-Location` values, and ignore `<base>` for fetching. Done when: `Mhtml.ResolveFromArchive` draws an image referenced by absolute URL and a stylesheet referenced by `cid:`.
- [ ] Replace a resource that is not in the archive with a labelled placeholder (images) or skip it (stylesheets, fonts) and report `ResourcesNotInArchive` with the count, without any fetch. Done when: `Mhtml.MissingResources` shows placeholders and the warning and records zero network calls. Cheaper substitute: letting the HTML engine fetch the live URL.
- [ ] Keep scripts, `<iframe>` and `<object>` off through the engine's existing isolation (`§3`) and cap the archive at the MIME limits of `§16`. Done when: `Mhtml.HostileArchive` runs no script and refuses an oversized archive.
- [ ] Add `Mhtml.*` tests in `tests/recto-tests/mhtml_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Mhtml.*` passes under ASan.
- [ ] Commit: `convert: add MHTML to PDF through the HTML engine`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Mhtml.*` passes under ASan, and resolving a missing resource over the network makes `Mhtml.MissingResources` fail.

## 20. Plain-text family (`.txt`, `.log`, `.ini`, `.cfg`, `.xml`) to PDF, encoding detected

PDFelement converts plain text and log-style files. Recto's Convert card already lists `.txt` and `.xml` as inputs (`§7`) and the clipboard path renders pasted text (`D04 T02 §16`), but no section converted a text file, so this section does. `text_to_pdf` (`src/core/text_to_pdf.h/.cpp`) converts `.txt`, `.text`, `.log`, `.ini`, `.cfg`, `.conf`, `.properties` and `.xml` into a paginated monospace PDF through the paginator of `§1`: encoding is detected in one fixed order (an explicit override, then byte-order mark, then BOM-less UTF-16 by its NUL pattern, then strict UTF-8, then `IMultiLanguage2::DetectInputCodepage`, with Windows-1252 as the last resort), a file whose bytes are mostly binary is refused rather than printed as garbage, long lines wrap or shrink to fit by option, tabs expand, and line numbers, a header with the file name, and page numbers are optional. `.xml` is shown as text, not rendered. `.csv` is not handled here: it stays a table through `§2`, and a test pins that routing. Very large logs stop at a page cap with an explicit last-page notice, never silently.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement encoding detection, the binary check and the monospace layout in `src/core/text_to_pdf.cpp`. Done when: `TextToPdf.Encodings` converts UTF-8 with and without a BOM, UTF-16LE and BE, Windows-1252, Shift-JIS and GB18030 fixtures to the right Unicode text in the PDF.
2. Add the options, the page cap and `recto-cli` wiring (`§21`). Done when: `TextToPdf.*` passes under ASan.

- [ ] Define `TextToPdfOptions` (`encoding` `Auto` or a name, `font_family` default `Consolas`, `font_size_pt` default `10`, `wrap` `Wrap|Shrink|Clip`, `tab_width`, `line_numbers`, `header_file_name`, `page_numbers`, `page_size`, `margins`, `max_pages` default `20000`) in `src/core/text_to_pdf.h`. Done when: the header compiles across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Detect the encoding with `detect_text_encoding` in this exact order, stopping at the first step that decides, and record the choice in the job note: (1) an explicit `encoding` option wins and skips detection; (2) a BOM (`EF BB BF`, `FF FE`, `FE FF`); (3) BOM-less UTF-16 when at least `80%` of the byte pairs in the first `64 KiB` have exactly one NUL byte, LE when the NUL is the second byte of the pair and BE when it is the first; (4) strict UTF-8 validation; (5) MLang detection on the `64 KiB` sample; (6) Windows-1252. UTF-16 recognition runs before UTF-8 validation because mostly-ASCII UTF-16 bytes, NULs included, are valid UTF-8. Done when: `TextToPdf.Encodings` covers each step, `TextToPdf.EncodingOverride` forces `windows-1251` on a file that would otherwise detect as UTF-8, and `TextToPdf.DetectionOrder` asserts that a BOM-less mostly-ASCII UTF-16LE file is detected as UTF-16LE, not UTF-8.
- [ ] Run the binary check after the encoding is chosen, on the decoded characters of the first `64 KiB`, never on raw bytes: refuse the file with `NotATextFile` and the message `"This file looks like binary data, not text."` when more than `5%` of the decoded characters are control characters other than tab, CR, LF and form feed, or U+FFFD; decoded U+0000 counts as a control character, so the check never sees the raw NUL bytes of UTF-16. Done when: `TextToPdf.BinaryRefused` refuses an executable renamed `.log`, and `TextToPdf.Utf16Ascii` accepts mostly-ASCII UTF-16LE and UTF-16BE files with and without a BOM.
- [ ] Lay out the text in the monospace font with per-script font fallback and subset embedding, no `?` or `.notdef` (`D06 T03 §7`), wrapping, shrinking or clipping long lines per option, and optional line numbers, header and page numbers. Done when: `TextToPdf.Layout` converts Latin, CJK and Arabic text and `PageText` returns the same Unicode, and a 5000-character line wraps without loss.
- [ ] Stop at `max_pages`, add a last-page notice (`"This file was cut after 20,000 pages."`) and the `TextTruncated` warning, and stream the layout so memory stays bounded for a `1 GiB` log. Done when: `TextToPdf.PageCap` converts a generated 300 MiB log within the memory bound and shows the notice. Cheaper substitute: reading the whole file into memory.
- [ ] Pin the routing so `.csv` goes to the spreadsheet engine (`§2`) and `.md`, `.html` to `§3`, and every other listed extension here. Done when: `TextRouting.Extensions` asserts the engine chosen for each extension.
- [ ] Add `TextToPdf.*` tests in `tests/recto-tests/text_to_pdf_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=TextToPdf.*:TextRouting.*` passes under ASan.
- [ ] Commit: `convert: add the plain-text family to PDF with encoding detection`

**Test checkpoint:** `recto-tests.exe --gtest_filter=TextToPdf.*:TextRouting.*` passes under ASan, decoding every file as Windows-1252 makes `TextToPdf.Encodings` fail, and moving strict UTF-8 validation ahead of BOM-less UTF-16 recognition makes `TextToPdf.DetectionOrder` and `TextToPdf.Utf16Ascii` fail.

## 21. `recto-cli office-to-pdf` dispatch for the new input formats

Extend the one conversion verb of `§2` so scripts, the Action Wizard and the local MCP server reach the new engines through the same exit-code contract, instead of adding a verb per format. `recto-cli office-to-pdf <in> --out <out.pdf>` identifies the input by signature first and extension second, and now routes `.odt`, `.ods`, `.odp` (`§9` to `§11`), `.ofd` (`§13` to `§15`), `.eml`, `.msg` (`§18`), `.mht`, `.mhtml` (`§19`), the text family (`§20`) and the image formats of `D04 T02 §23`, besides the Office, RTF, CSV, HTML and Markdown inputs it already takes. New flags carry the per-format options. A format whose engine refuses the file (password-protected ODF, encrypted email, binary data) exits non-zero with the typed reason in `--json`.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add signature-first format identification and the dispatch table in `src/cli/cmd_office_to_pdf.cpp` and `src/core/convert_dispatch.cpp`, shared with the Convert page job factory. Done when: `Cli.OfficeToPdfFormats` converts one fixture per new format and a renamed file by signature.
2. Add the flags and the typed exit codes. Done when: `Cli.OfficeToPdfFormats` and `Cli.OfficeToPdfRefusals` pass under ASan.

- [ ] Implement `identify_input(path)` returning the engine from magic bytes (ZIP plus `mimetype` or `OFD.xml`, CFB plus `__properties_version1.0`, MIME headers, text) before the extension, in `src/core/convert_dispatch.cpp`. Done when: `ConvertDispatch.Signatures` routes a `.docx`-named ODT, a `.txt`-named EML and an extensionless OFD correctly.
- [ ] Route each identified format to its engine and keep the existing routes unchanged. Done when: the OFD dispatch fixture is an invoice with selectable text, an attachment and a seal, and the output has the text, the embedded file and the seal count in `--json`; `Cli.OfficeToPdfFormats` converts `.odt`, `.ods`, `.odp`, `.ofd`, `.eml`, `.msg`, `.mhtml`, `.log`, `.ini`, `.xml` and a `.webp` (skipped with the codec message when absent) and still converts `.docx`, `.xlsx` and `.html`.
- [ ] Add `--include-notes` (ODP), `--prefer-plain-text` (email), `--encoding <name>` and `--max-pages <n>` (text), `--ico-frames largest|all` (images), and print `warnings` and the seal and attachment counts in `--json`. Done when: `Cli.OfficeToPdfFormats` asserts each flag changes the output as documented.
- [ ] Map typed refusals (`PasswordProtectedOdf`, `EncryptedMessage`, `NotATextFile`, `UnsupportedImageFormat`, a missing codec) to distinct non-zero exit codes with the plain message on stderr and the code in `--json`. Done when: `Cli.OfficeToPdfRefusals` asserts each exit code and message.
- [ ] Commit: `convert: dispatch ODF, OFD, email, MHTML, text and more images in recto-cli office-to-pdf`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ConvertDispatch.*:Cli.OfficeToPdfFormats:Cli.OfficeToPdfRefusals` passes under ASan, and routing by extension only makes `ConvertDispatch.Signatures` fail.

## 22. Convert card inputs for ODF, OFD, email, MHTML, text and image formats, and open `.ofd`

Make the new formats usable on the Convert page and in the Open dialog. The **PDF from Office, HTML, Markdown & Images** card (`§7`) now accepts `.odt`, `.ods`, `.odp`, `.ofd`, `.eml`, `.msg`, `.mht`, `.mhtml`, the text family and the extra image types, dispatches them through `§21`'s table, shows the new option rows only when a file that uses them is loaded (speaker notes for ODP, prefer plain text for email, encoding for text, icon frames for ICO), and surfaces each job's warnings and typed refusals in the queue card. A `.ofd` file opened from the Open dialog or dropped on the window converts to a temporary PDF and opens as an unsaved tab through `OpenDocumentPipeline::open_generated_async` (`D04 T02 §15`), titled `<name>.ofd (converted)`, so OFD is readable in Recto and the original is never modified.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 (the card enable and disable rule and the Step 3 options of the `PDF from Office, HTML, Markdown & Images` card) with `D-Convert.png`, and section 9.1 (the Home drop overlay and Open dialog) for opening `.ofd`; the conditional option rows are a genuinely new addition that follow the existing option row and hint line chrome.
**Job:** The user can drop an ODF file, an OFD document, a saved email, a saved web page or a log file onto the Convert page and get a PDF, with the options that matter for that format and a clear message when a file cannot be converted, and can open an `.ofd` straight into the viewer. Consumer: `ConvertPage` (`D04 T01 §3`, `§4`), `ConversionRunner` (`D04 T01 §1`), the queue card (`D04 T01 §5`) and `OpenDocumentPipeline` (`D02 T01 §5`).
**Treatment:** The extension list and disable reason of the card, per-format conditional option rows bound to the existing `OfficeToPdfOptionsViewModel`, the dispatch of `§21`, and a converted-on-open flow for `.ofd`. Cheaper substitute that fails the checkpoint: listing the new extensions without options or messages, or an `.ofd` open that writes the PDF next to the original.
**Chrome:** consume the Convert Step 2 card registry and Step 3 options host (`D04 T01 §3`, `§4`), the queue card and notices (`D04 T01 §5`, `§6`), the Open pipeline (`D02 T01 §5`), `recto_tokens.g.h` tokens and Fluent UI System Icons (`RectoIcons` via `ui::Icon`).

**Build order:**

1. Extend the card's enablement, hint line and conditional option rows in `OfficeToPdfOptionsControl` (`§7`) and the job factory dispatch. Done when: `OfficeToPdf.NewFormatsCardUi` loads one file per new format and sees the card enabled with the right option rows.
2. Add the warnings and refusals display and the converted-on-open flow for `.ofd`. Done when: `AppTests.OpenOfdUi` opens an `.ofd` as an unsaved converted tab and shows a refusal message for an encrypted ODF.

- [ ] Extend the card's enabling extension list with `.odt`, `.ods`, `.odp`, `.ofd`, `.eml`, `.msg`, `.mht`, `.mhtml`, `.txt`, `.text`, `.log`, `.ini`, `.cfg`, `.conf`, `.properties`, `.xml`, `.ico`, `.jfif`, `.dib`, `.heic`, `.heif`, `.webp` and `.jxl`, and update the disable reason and hint line to name the new kinds. Done when: `OfficeToPdf.NewFormatsCardUi` verifies enablement and the new hint line.
- [ ] Add conditional option rows to `OfficeToPdfOptionsControl`: `Include speaker notes` (ODP loaded), `Prefer plain text` (email loaded), `Text encoding` (`Auto` and a list) and `Wrap long lines` (text loaded), and `Icon sizes` (`Largest`, `All`) (ICO loaded), each with an automation name and tooltip and shown only when a file that uses it is in Step 1. Done when: the rows appear and disappear with the file mix and pass the `D03 T02 §3` inventory. Cheaper substitute: always showing every row.
- [ ] Dispatch queued jobs through `convert_dispatch` (`§21`) and show job warnings (`FootnotesMovedToEnd`, `RemoteImagesBlocked`, `ResourcesNotInArchive`, `PatternFillApproximated`, `TextTruncated`, a missing-codec message) as a count and expandable list in the queue card. Done when: `OfficeToPdf.NewFormatsCardUi` shows the warnings of a seeded email and a seeded OFD.
- [ ] Show the typed refusals (`PasswordProtectedOdf`, `EncryptedMessage`, `NotATextFile`, `UnsupportedImageFormat` with the codec name) as failed queue cards with their plain message and no retry loop. Done when: `OfficeToPdf.NewFormatsCardUi` shows each message for a seeded refusal.
- [ ] Add `.ofd` to the Open dialog filter and the Home drop target: converting through `convert_office_to_pdf` into a private temp file as a queue job, then `open_generated_async` as `<name>.ofd (converted)`, marked unsaved with the temp file owned by the tab, `Save a copy` defaulting to `<name>.pdf`, and the `.ofd` never modified. Done when: `AppTests.OpenOfdUi` opens an OFD, closes it with the unsaved prompt, and finds the source `.ofd` hash unchanged. Cheaper substitute: writing the PDF beside the `.ofd`.
- [ ] Add `OfficeToPdf.NewFormatsCardUi` and `AppTests.OpenOfdUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=OfficeToPdf.NewFormatsCardUi:AppTests.OpenOfdUi` passes on the display-session host.
- [ ] Commit: `convert: accept ODF, OFD, email, MHTML, text and more image types on the Convert page and open OFD`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.NewFormatsCardUi:AppTests.OpenOfdUi` passes on the display-session host, and display-session screenshots confirm the card and the conditional option rows.

## Verification

- M5 Office/HTML/Markdown-to-PDF obligations (`docs/plans/2026-10-13-recto-foundation.md` section 7): `.docx`, `.docm`, `.doc`, `.xlsx`, `.xlsm`, `.xls`, `.pptx`, `.pptm`, `.ppt`, `.rtf`, `.html`, `.md`, and `.csv` convert cleanly to PDF via Recto's 100% built-in C++23 engines with zero external dependencies, and `macro-canary.docm` and `macro-canary.doc` are proven never to parse or execute VBA macros.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.

