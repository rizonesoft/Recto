---
schema_version: 1
id: excel-web-and-compliance
domain: 04-convert
status: active
title: "TODO-05 -- PDF to Excel/CSV/XML, HTML5/Markdown/EPUB/LaTeX, and PDF/A, PDF/UA & ZUGFeRD Compliance"
track: M5
---

# TODO-05 -- PDF to Excel/CSV/XML, HTML5/Markdown/EPUB/LaTeX, and PDF/A, PDF/UA & ZUGFeRD Compliance

> **Goal:** Complete the M5 native C++20 conversion and archival compliance suite: `PDF -> Excel (.xlsx), .csv & .xml` (`XlsxWriter` in `src/core/xlsx_writer.cpp`), `PDF -> HTML5 (Fixed & Flowing), Markdown (.md), EPUB 3 (.epub) & LaTeX (.tex)` (`src/core/web_doc_writer.cpp`), and `PDF/A (1a, 1b, 2a, 2b, 2u, 3a, 3b, 3u), PDF/UA-1 Accessibility & ZUGFeRD / Factur-X E-Invoicing` validation and conversion (`src/pdf/compliance.cpp`), together with their Convert step 2/3 cards (`SPEC 9.5`) and the Viewer `Signatures & Standards` side panel (`SPEC 8.3`).

> **Current state:** Measured 2026-10-14. No Excel, Web/EPUB/LaTeX, or PDF/A compliance code exists yet. `D04 T02 §5` provides `TableExtractor`, `D04 T03 §1` provides `zip_archive.cpp` for OpenXML/EPUB packages, and `D01` / `D04 T02 §1` provide `recto-pdf` object/font/XMP manipulation and `PdfWriter`.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` decisions D7, D9, D12; sections 3.2, 4.1 item 10 (`compliance.cpp`), 5 (`PDF -> Excel`, `PDF -> HTML5, Markdown, EPUB 3 & LaTeX`, `PDF/A, PDF/UA & ZUGFeRD / Factur-X`), 6 and 7 (M5 row).
- `design/SPEC.md` sections 8.3 (`Signatures & Standards` side panel), 9.5 (`Excel & Data`, `HTML, Markdown, EPUB & LaTeX`, `PDF/A & E-Invoice` Convert cards and step 3 options).
- ECMA-376 SpreadsheetML (`.xlsx`), W3C HTML5/CSS3 and EPUB 3.2 specifications, ISO 19005-1/2/3 (`PDF/A`), ISO 14289-1 (`PDF/UA-1`), and EN 16931 (`ZUGFeRD 2.x / Factur-X`).
- -> XREF: D04 T01 §2 -- registers the Excel & Data, HTML5/Markdown/EPUB/LaTeX, and PDF/A & E-Invoice cards and step 3 options into the Convert page and `JobQueue`.
- -> XREF: D04 T02 §5 -- `XlsxWriter`, `.csv` and `.xml` exporters consume `TableExtractor` grids from `D04 T02 §5`.

## Outcome

- `XlsxWriter` (`src/core/xlsx_writer.cpp`) converts PDF tables and pages into native ECMA-376 `.xlsx` workbooks (`xl/workbook.xml`, `xl/worksheets/sheet*.xml`, `xl/sharedStrings.xml`, `xl/styles.xml`) with automatic numeric, currency, percentage and date type parsing, merged cells (`<mergeCells>`), cell borders/fills, and sheet organization (`One worksheet per table`, `One worksheet per page`, or `Single combined worksheet`), plus `.csv` and structured `.xml` exports.
- `WebDocWriter` (`src/core/web_doc_writer.cpp`) exports PDFs to **HTML5** (`Fixed-Layout Exact Positioning` with embedded SVG/WOFF2 or `Flowing Semantic HTML` with `<article>`, `<h1>`-`<h6>`, `<p>`, `<table>` and single-file self-contained `data:` URI option), **GitHub-Flavored Markdown (`.md`)**, **EPUB 3 (`.epub`)** reflowable e-books (`META-INF/container.xml`, `content.opf`, `toc.ncx` / `nav.xhtml`, chapter `.xhtml`), and **LaTeX (`.tex`)**.
- `src/pdf/compliance.cpp` validates and converts PDFs to **PDF/A-1a, 1b, 2a, 2b, 2u, 3a, 3b, 3u** (font embedding, `/OutputIntents` sRGB/CMYK ICC profile injection, `pdfaid:part` / `pdfaid:conformance` XMP metadata, transparency flattening for `PDF/A-1`, and forbidden action removal), validates/synthesizes **PDF/UA-1** (`/StructTreeRoot` tagged reading order, `/MarkInfo << /Marked true >>`, `/Lang`, `/Alt` text), and reads/writes **ZUGFeRD 2.x / Factur-X** (`PDF/A-3` with embedded `factur-x.xml` / `zugferd-invoice.xml` and `/AF` Associated Files).
- The Viewer's `Signatures & Standards` side panel (`SPEC 8.3`) displays live compliance audit checks with click-to-highlight diagnostics and one-click `Convert to PDF/A`, plus a structured invoice viewer for ZUGFeRD / Factur-X PDFs.

**Adjacency:** all=not-applicable (converter engines, compliance validators and side-panel views inside `D04 T01`'s Convert frame and `D02 T03`'s side-panel host)

**Adjacency rationale:** Queue management and notifications live in `D04 T01`; this file owns the spreadsheet, web/e-book, and compliance/e-invoicing engines and their Convert/Viewer surfaces.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | PDF to Excel (`.xlsx`), `.csv` and `.xml` (`XlsxWriter`) and Convert UI | D04 T01 §2, D04 T02 §5, D04 T03 §1 | [ ] |
| 2 | §2 | PDF to HTML5 (Fixed and Flowing), Markdown (`.md`), EPUB 3 (`.epub`) and LaTeX (`.tex`) and Convert UI | D04 T01 §2, D04 T03 §1 | [ ] |
| 3 | §3 | PDF/A (`1a/1b/2a/2b/2u/3a/3b/3u`), PDF/UA-1 (`/StructTreeRoot`) and ZUGFeRD / Factur-X engine | D04 T02 §1, D01 T02 §4 | [ ] |
| 4 | §4 | Viewer Standards and E-Invoice Side Panel and `PDF/A & E-Invoice` Convert Card | §3, D02 T03 §5, D04 T01 §2 | [ ] |

## 1. PDF to Excel (`.xlsx`), `.csv` and `.xml` (`XlsxWriter`) and Convert UI

Implement `src/core/xlsx_writer.cpp` (`recto::core::XlsxWriter`), `recto-cli pdf-to-xlsx`, `recto-cli pdf-to-csv`, `recto-cli pdf-to-xml`, and the **Excel & Data (`.xlsx`, `.csv`, `.xml`)** card and step 3 options in `ConvertPage` (`design/SPEC.md` section 9.5):
- **SpreadsheetML `.xlsx` generation:** Consume `TableExtractor` (`D04 T02 §5`) and `zip_archive.cpp` (`D04 T03 §1`) to emit valid ECMA-376 `.xlsx` packages (`[Content_Types].xml`, `_rels/.rels`, `xl/workbook.xml`, `xl/_rels/workbook.xml.rels`, `xl/sharedStrings.xml`, `xl/styles.xml`, `xl/worksheets/sheet1..N.xml`) with `<cols>` auto-fitted column widths, `<mergeCells>` for `rowspan`/`colspan` cells, cell borders and fill colours, and automatic numeric/currency/percentage/ISO-date cell value and number-format (`numFmtId`) detection so numbers are immediately summable in Excel.
- **Sheet organization modes & non-table content:** Support `One worksheet per table`, `One worksheet per page`, and `Single combined worksheet`, plus optional inclusion of non-table page text, `OcrPipeline` for scanned pages, RFC 4180 `.csv` (UTF-8 with BOM option), and structured `.xml` (`<document><page><table|paragraph>...`).

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.5 (`Excel & Data (.xlsx, .csv, .xml)` card and step 3 options).
**Job:** Users can convert financial statements, invoices, schedules, and data tables in PDFs into native Excel workbooks with real numeric cells and merged headers, or export `.csv` and `.xml`. Consumer: Microsoft Excel, LibreOffice Calc, data pipelines, and Recto's own `.xlsx -> PDF` round-trip (`D04 T04 §1`).
**Treatment:** Native ECMA-376 `.xlsx` ZIP package writer over `TableExtractor` with numeric/currency/date type parsing and 3 sheet-grouping modes. Cheaper substitute that fails the checkpoint: writing `.csv` with a `.xlsx` extension, or storing every numeric cell as an unformatted text string (`t="s"`) that `SUM()` ignores.
**Chrome:** consume the Convert step 2 card and step 3 options host from `D04 T01 §2`.

**Build order:**

1. Implement `XlsxWriter` (`src/core/xlsx_writer.cpp`) plus `.csv` and `.xml` exporters and `recto-cli pdf-to-xlsx`, `recto-cli pdf-to-csv`, and `recto-cli pdf-to-xml`. Done when: `XlsxWriter.*` converts bordered and borderless table corpus PDFs into `.xlsx` files whose XML validates, whose numeric/currency cells have numeric types (`t="n"`), whose merged headers appear in `<mergeCells>`, and whose worksheets honour all 3 sheet-organization modes under ASan.
2. Wire the **Excel & Data (`.xlsx`, `.csv`, `.xml`)** card and step 3 options (format, sheet organization, parse numeric/currency/date values toggle, include non-table text toggle, OCR toggle) in `ConvertPage`. Done when: `AppTests.ConvertExcelUi` runs an `.xlsx` and `.csv` export through the queue and verifies output files.

- [ ] `XlsxWriter` (`.xlsx`), `.csv`, and `.xml` exporters with numeric/currency/date parsing, `<mergeCells>`, cell styles, and 3 sheet-organization modes. Done when: `XlsxWriter.*` passes under ASan and round-trips through `OfficeToPdf.BuiltinXlsx` (`D04 T04 §1`). Cheaper substitute: text-only cells or single-sheet-only output.
- [ ] **Excel & Data** Convert card and step 3 options wired in `ConvertPage`. Done when: `AppTests.ConvertExcelUi` passes end-to-end.
- [ ] Commit: `convert: implement native C++20 XlsxWriter (.xlsx, .csv, .xml) and Convert UI`

**Test checkpoint:** `recto-tests.exe --gtest_filter=XlsxWriter.*:AppTests.ConvertExcelUi` passes under ASan.

## 2. PDF to HTML5 (Fixed and Flowing), Markdown (`.md`), EPUB 3 (`.epub`) and LaTeX (`.tex`) and Convert UI

Implement `src/core/web_doc_writer.cpp` (`recto::core::WebDocWriter`), `recto-cli pdf-to-html`, `recto-cli pdf-to-md`, `recto-cli pdf-to-epub`, `recto-cli pdf-to-latex`, and the **HTML, Markdown, EPUB & LaTeX** card and step 3 options in `ConvertPage` (`design/SPEC.md` section 9.5):
- **HTML5 (`Fixed` & `Flowing` modes):**
  - *Fixed-Layout (Exact Positioning):* Each page rendered as an accurately sized `<section class="page">` with embedded `<svg>` vector paths/backdrops, subset `@font-face` WOFF2/TTF fonts, and positioned text spans matching the PDF layout, with either a single self-contained `.html` file (`data:` URI assets) or an `.html` + asset folder.
  - *Flowing (Responsive Semantic HTML):* Reconstructs `<article>`, `<h1>`-`<h6>`, `<p>`, `<ul>`/`<ol>`, `<table>`, and `<figure>`/`<img>` in reading order using `PageText` and `TableExtractor`.
- **Markdown (`.md`):** Clean GitHub-Flavored Markdown with `#` headings, paragraphs, `*`/`1.` lists, pipe `|---|---|` tables, and extracted image links.
- **EPUB 3 (`.epub`):** Valid EPUB 3.2 ZIP container (`mimetype` uncompressed first entry, `META-INF/container.xml`, `OEBPS/content.opf`, `OEBPS/nav.xhtml` + `toc.ncx` derived from PDF `/Outlines` or inferred headings, reflowable chapter `.xhtml` files, stylesheet, and `OEBPS/images/*`).
- **LaTeX (`.tex`):** Structured LaTeX document (`\documentclass{article}`, `\section`, `\subsection`, `\textbf`, `\textit`, `tabular` environments, and `\includegraphics`).

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.5 (`HTML, Markdown, EPUB & LaTeX` card and step 3 options).
**Job:** Users can convert PDFs into responsive or fixed-layout web pages, Markdown notes, reflowable EPUB 3 e-books for e-readers, or editable LaTeX manuscripts. Consumer: browsers, Markdown editors, e-readers, TeX engines, and `D04 T04 §1`'s `HTML/Markdown -> PDF` round-trip.
**Treatment:** Native C++20 emitters in `src/core/web_doc_writer.cpp` sharing `PageText`, `TableExtractor`, and `zip_archive.cpp`. Cheaper substitute that fails the checkpoint: rendering whole pages as flat `.png` images inside an HTML or EPUB wrapper with no selectable text or semantic tags.
**Chrome:** consume the Convert step 2 card and step 3 options host from `D04 T01 §2`.

**Build order:**

1. Implement `WebDocWriter` in `src/core/web_doc_writer.cpp` for HTML5 (`Fixed` and `Flowing`, single-file `data:` URI and external asset folder), Markdown (`.md`), EPUB 3 (`.epub`), and LaTeX (`.tex`), plus the four `recto-cli` verbs. Done when: `WebDocWriter.*` converts prose, table and illustrated corpus PDFs into all four formats, verifying valid HTML5/XHTML structure, GFM Markdown tables, EPUB 3 `mimetype`/`container.xml`/`content.opf`/`nav.xhtml` package rules, and LaTeX `tabular` output under ASan.
2. Wire the **HTML, Markdown, EPUB & LaTeX** card and step 3 options in `ConvertPage`. Done when: `AppTests.ConvertWebDocUi` runs HTML (Fixed and Flowing), Markdown, EPUB and LaTeX jobs through the queue.

- [ ] `WebDocWriter` exporting HTML5 (`Fixed` & `Flowing` + single-file `data:` URI mode), GFM Markdown (`.md`), EPUB 3 (`.epub`), and LaTeX (`.tex`). Done when: `WebDocWriter.*` passes under ASan and round-trips `.html` and `.md` through `HtmlMdToPdf` (`D04 T04 §1`). Cheaper substitute: image-only HTML/EPUB or omitting `Fixed` vs `Flowing` HTML modes.
- [ ] **HTML, Markdown, EPUB & LaTeX** Convert card and step 3 options wired in `ConvertPage`. Done when: `AppTests.ConvertWebDocUi` passes end-to-end.
- [ ] Commit: `convert: implement PDF to HTML5, Markdown, EPUB 3 and LaTeX writer and UI`

**Test checkpoint:** `recto-tests.exe --gtest_filter=WebDocWriter.*:AppTests.ConvertWebDocUi` passes under ASan.

## 3. PDF/A (`1a/1b/2a/2b/2u/3a/3b/3u`), PDF/UA-1 (`/StructTreeRoot`) and ZUGFeRD / Factur-X engine

Implement `src/pdf/compliance.cpp` (`recto::pdf::ComplianceEngine`), `recto-cli pdfa <validate|convert>`, and `recto-cli einvoice <inspect|attach>`:
- **PDF/A Validation & Conversion (`PDF/A-1a`, `1b`, `2a`, `2b`, `2u`, `3a`, `3b`, `3u`):**
  - *Validator:* Checks all ISO 19005-1/2/3 clauses: embedded font completeness and `/ToUnicode` maps (`2u`/`3u` and `1a`/`2a`/`3a`), device-independent colour spaces or `/OutputIntents` ICC profile presence, XMP `pdfaid:part` and `pdfaid:conformance` metadata synchronization with `/Info`, absence of encryption (`/Encrypt`), absence of `/JavaScript` / `/Launch` / `/Sound` / `/Movie` actions, LZW filter prohibition, and transparency prohibition in `PDF/A-1` (`/CA 1.0`, `/ca 1.0`, `/BM /Normal`, no `/SMask`). Returns a structured list of `ComplianceFinding` diagnostics with object ID, page index, clause code, and fixability flag.
  - *Converter:* Embeds and subsets any unembedded fonts via DirectWrite system font lookup (`D01 T02 §4`), attaches a standard sRGB (or CMYK) ICC `/OutputIntent` profile, generates `/ToUnicode` CMap streams (`u` conformance), synthesizes or preserves `/StructTreeRoot` logical structure (`a` conformance), flattens transparent page groups when targeting `PDF/A-1a`/`1b`, strips prohibited actions, and writes synchronized XMP `pdfaid` metadata.
- **PDF/UA-1 Validation & Auto-Tagging (`ISO 14289-1`):**
  - Validates and synthesizes `/MarkInfo << /Marked true >>`, document `/Lang`, `/ViewerPreferences << /DisplayDocTitle true >>`, `/Title` in XMP, `/StructTreeRoot` hierarchy (`/Document`, `/Part`, `/Sect`, `/H1`-`/H6`, `/P`, `/L`, `/LI`, `/Table`, `/TR`, `/TH`, `/TD`, `/Figure` with `/Alt` text) mapped to Marked Content Identifiers (`BDC` / `EMC` `/MCID`) in page streams, and `/Tabs /S` on pages.
- **ZUGFeRD 2.x / Factur-X Hybrid E-Invoicing (`EN 16931`):**
  - Produces a `PDF/A-3b` or `PDF/A-3u` invoice with an embedded Cross-Industry Invoice (`CII` `factur-x.xml` / `zugferd-invoice.xml`) or `UBL 2.1` XML stream in `/Names /EmbeddedFiles`, linked via `/AF` (Associated Files, `/AFRelationship /Alternative` or `/Data`) and required `fx:` XMP extension schema (`fx:DocumentType`, `fx:DocumentFileName`, `fx:Version`, `fx:ConformanceLevel`), and parses embedded e-invoice XML into a structured `InvoiceModel` (seller, buyer, invoice number, issue date, currency, line items, tax breakdown, totals, IBAN/payment terms).

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the `PDF/A` (`1a/1b/2a/2b/2u/3a/3b/3u`) and `PDF/UA-1` validator and converter in `src/pdf/compliance.cpp` (font embedding, `/ToUnicode`, sRGB ICC `/OutputIntents`, XMP `pdfaid` / `pdfuaid` schema, `PDF/A-1` transparency flattening, and `/StructTreeRoot` + `MCID` auto-tagging from `PageText` and `TableExtractor`), plus `recto-cli pdfa <validate|convert>`. Done when: `PdfCompliance.PdfAAndPdfUa` converts non-compliant corpus PDFs into each `PDF/A` level and `PDF/UA-1` and verifies zero remaining compliance findings under ASan.
2. Implement ZUGFeRD 2.x / Factur-X `PDF/A-3` embedding, `fx:` XMP extension schema generation, and CII/UBL XML parsing in `src/pdf/compliance.cpp`, plus `recto-cli einvoice <inspect|attach>`. Done when: `PdfCompliance.ZugferdFacturX` embeds a `factur-x.xml` payload into an invoice PDF, validates `PDF/A-3b` + `/AF` + `fx:` XMP conformance, and parses all seller/buyer/line-item/tax fields back into `InvoiceModel`.

- [ ] `PDF/A` (`1a/1b/2a/2b/2u/3a/3b/3u`) and `PDF/UA-1` (`/StructTreeRoot` + `MCID` tagging) validator and converter in `src/pdf/compliance.cpp`. Done when: `PdfCompliance.PdfAAndPdfUa` passes under ASan across all 8 PDF/A conformance levels and PDF/UA-1. Cheaper substitute: setting `pdfaid:part` in XMP without embedding fonts, attaching `/OutputIntents`, or building `/StructTreeRoot`.
- [ ] ZUGFeRD 2.x / Factur-X hybrid e-invoice (`PDF/A-3` + `/AF` + `fx:` XMP + CII/UBL XML reader/writer) and `recto-cli pdfa` / `recto-cli einvoice`. Done when: `PdfCompliance.ZugferdFacturX` passes under ASan.
- [ ] Commit: `pdf: add PDF/A (1-3 a/b/u), PDF/UA-1 auto-tagging and ZUGFeRD/Factur-X compliance engine`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCompliance.*` passes under ASan.

## 4. Viewer Standards and E-Invoice Side Panel and `PDF/A & E-Invoice` Convert Card

Build the visual compliance and e-invoicing surfaces specified in `design/SPEC.md` sections 8.3 and 9.5:
- **Viewer `Signatures & Standards` Side Panel (`SPEC 8.3`, Standards & E-Invoice sections):**
  - Displays detected conformance badges (`PDF/A-1b`, `PDF/A-2u`, `PDF/A-3b`, `PDF/UA-1`, `ZUGFeRD 2.2 / Factur-X`), an interactive `Validate PDF/A & PDF/UA` checklist grouped by category (`Fonts Embedded`, `Colour Spaces & OutputIntent`, `XMP Metadata`, `Transparency Rules`, `Tagged Structure /StructTreeRoot`, `Alt Text`), click-to-highlight offending page objects, and a one-click `Convert to PDF/A...` action.
  - When a ZUGFeRD / Factur-X invoice is open, displays a formatted `E-Invoice Summary` card (seller, buyer, invoice number, date, line items table, tax breakdown, grand total, payment IBAN) and an `Export Invoice XML` button.
- **`PDF/A & E-Invoice (ZUGFeRD / Factur-X)` Convert Card (`SPEC 9.5`):**
  - Step 3 options for target standard (`PDF/A-1b`, `PDF/A-1a`, `PDF/A-2b`, `PDF/A-2u`, `PDF/A-2a`, `PDF/A-3b`, `PDF/A-3u`, `PDF/A-3a`, `PDF/UA-1`), colour profile (`sRGB`, `CMYK`), auto-tag structure tree toggle, embed missing fonts toggle, flatten transparency toggle, and optional `Attach ZUGFeRD / Factur-X XML` picker with profile selector (`MINIMUM`, `BASIC`, `EN 16931 / COMFORT`, `EXTENDED`).

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.3 (`Signatures & Standards` side panel) and 9.5 (`PDF/A & E-Invoice` Convert card and step 3 options).
**Job:** Users can verify whether a PDF meets archival (`PDF/A`), accessibility (`PDF/UA`), or European e-invoicing (`ZUGFeRD / Factur-X`) standards, inspect structured invoice line items, or convert documents to compliant `PDF/A` / `PDF/UA` / `ZUGFeRD` files. Consumer: the viewer side-panel host (`D02 T03 §5`) and Convert queue (`D04 T01 §2`).
**Treatment:** Live compliance audit checklist and structured e-invoice viewer in the `Signatures & Standards` side panel paired with the `PDF/A & E-Invoice` Convert card. Cheaper substitute that fails the checkpoint: showing only a static XMP string without running clause validation or rendering ZUGFeRD invoice fields.
**Chrome:** consume the side-panel container from `D02 T03 §5` and `ConvertPage` from `D04 T01 §2`.

**Build order:**

1. Wire the Standards & E-Invoice sections of the `Signatures & Standards` side panel (`SPEC 8.3`) in `src/app`, including conformance badges, interactive clause validation checklist, click-to-jump diagnostics, and the structured ZUGFeRD / Factur-X invoice viewer. Done when: opening a ZUGFeRD fixture in `AppTests.StandardsAndInvoiceUi` displays its `PDF/A-3b` badge, seller/buyer/line-item summary, and validation pass state.
2. Wire the **PDF/A & E-Invoice (ZUGFeRD / Factur-X)** card and step 3 options in `ConvertPage` (`SPEC 9.5`). Done when: `AppTests.StandardsAndInvoiceUi` converts a regular PDF into `PDF/A-2u` and `PDF/A-3b` + `factur-x.xml` through the queue and verifies the output in the Standards panel.

- [ ] `Signatures & Standards` side panel compliance validator checklist and ZUGFeRD / Factur-X structured invoice view (`SPEC 8.3`). Done when: `AppTests.StandardsAndInvoiceUi` verifies clause diagnostics and e-invoice rendering. Cheaper substitute that fails the checkpoint: raw XML dump instead of structured invoice fields and clause rows.
- [ ] `PDF/A & E-Invoice` Convert card and step 3 options (`SPEC 9.5`) wired to `JobQueue`. Done when: queued `PDF/A` and `ZUGFeRD` conversions complete and pass validation.
- [ ] Commit: `app: add Standards and E-Invoice side panel and PDF/A Convert card`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.StandardsAndInvoiceUi` passes.

## Verification

- `XlsxWriter`, `WebDocWriter`, and `ComplianceEngine` all pass under ASan and round-trip in `D06 T01 §3`.
- Every control in the new Convert cards and `Signatures & Standards` side panel carries an automation name and tooltip.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
