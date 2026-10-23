---
schema_version: 1
id: excel-web-and-compliance
domain: 04-convert
status: active
title: "TODO-05 -- PDF to Excel/CSV/XML, HTML5/Markdown/EPUB/LaTeX, and PDF/A, PDF/UA & ZUGFeRD Compliance"
track: M5
---

# TODO-05 -- PDF to Excel/CSV/XML, HTML5/Markdown/EPUB/LaTeX, and PDF/A, PDF/UA & ZUGFeRD Compliance

> **Goal:** Complete the M5 native C++23 conversion and archival compliance suite: `PDF -> Excel (.xlsx), .csv & .xml` (`XlsxWriter` in `src/core/xlsx_writer.cpp`), `PDF -> HTML5 (Fixed & Flowing), Markdown (.md), EPUB 3 (.epub) & LaTeX (.tex)` (`src/core/web_doc_writer.cpp`), and `PDF/A (1a, 1b, 2a, 2b, 2u, 3a, 3b, 3u), PDF/UA-1 Accessibility & ZUGFeRD / Factur-X E-Invoicing` validation and conversion (`src/pdf/compliance.cpp`), together with their Convert Step 2/3 cards (`SPEC 9.5`) and the Viewer `Signatures & Standards` side panel (`SPEC 8.3`).

> **Current state:** Measured 2026-10-14. No Excel, Web/EPUB/LaTeX, or PDF/A compliance code exists yet. `D04 T02 §8` provides `TableExtractor`, `D04 T03 §1` provides `zip_archive.cpp` for OpenXML/EPUB packages, and `D01` / `D04 T02 §2` provide `recto-pdf` object/font/XMP manipulation and `PdfWriter`. 2026-10-22: WinUI 3 dropped for `recto-ui` (foundation plan D4); UI sections rewritten against `todo/10-ui/`.

> **Groomed 2026-10-23:** The LaTeX verb is `recto-cli pdf-to-tex` (the foundation plan section 3.1 name; the file said `pdf-to-latex`); Adjacency was corrected from `all=not-applicable` to owners that exist (the WCAG and compliance reports are the reporting and document surface, the e-invoice XML is the exchange surface).

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` decisions D7, D9, D12; sections 3.2, 4.1 item 10 (`compliance.cpp`), 5 (`PDF -> Excel`, `PDF -> HTML5, Markdown, EPUB 3 & LaTeX`, `PDF/A, PDF/UA & ZUGFeRD / Factur-X`), 6 and 7 (M5 row).
- `design/SPEC.md` sections 8.3 (`Signatures & Standards` side panel), 9.5 (`Excel & Data`, `HTML, Markdown, EPUB & LaTeX`, `PDF/A & E-Invoice` Convert cards and Step 3 options).
- ECMA-376 SpreadsheetML (`.xlsx`), W3C HTML5/CSS3 and EPUB 3.2 specifications, ISO 19005-1/2/3 (`PDF/A`), ISO 14289-1 (`PDF/UA-1`), and EN 16931 (`ZUGFeRD 2.x / Factur-X`).
- -> XREF: D01 T02 §2 -- `ComplianceEngine` in §5 and §6 uses `D01 T02 §2`'s DirectWrite font loader and `/ToUnicode` generator to embed missing fonts.
- -> XREF: D02 T03 §3 -- the Viewer `Signatures & Standards` side panel (§9 here) plugs into `SidePanelHost` (`D02 T03 §3`).
- -> XREF: D03 T01 §1 -- `WebDocWriter` (§3) and the `PDF/UA-1` auto-tagger (§7) consume `PageText` blocks and reading order from `D03 T01 §1`.
- -> XREF: D04 T01 §4 -- registers the `Excel & Data` (§2), `HTML, Markdown, EPUB & LaTeX` (§4), and `PDF/A & E-Invoice` (§9) cards and Step 3 options into the Convert page and `ConversionRunner`.
- -> XREF: D04 T02 §2 -- `ComplianceEngine` (§6, §7, §8) writes compliant PDFs via `D04 T02 §2`'s `PdfWriter` and `D04 T02 §1`'s `AtomicFileWriter`.
- -> XREF: D04 T02 §8 -- `XlsxWriter` (§1), `.csv`/`.xml` exporters, and `WebDocWriter` (§3) consume `TableExtractor` grids from `D04 T02 §8`.
- -> XREF: D04 T02 §10 -- `TablePreviewDrawer`'s `Save as .xlsx` action (`D04 T02 §10`) delegates to §1's `XlsxWriter`.
- -> XREF: D04 T03 §1 -- `XlsxWriter` (§1) and `WebDocWriter`'s EPUB 3 packager (§4) use `D04 T03 §1`'s `zip_archive.cpp` and `OpenXmlPackageBuilder`.
- -> XREF: D04 T03 §2 -- the `PDF/UA-1` auto-tagger (§7) shares `D04 T03 §2`'s heading, list, and running-header/footer structure inference rules.
- -> XREF: D05 T02 §11 -- the `Signatures` section of the `Signatures & Standards` side panel (`D05 T02 §11`) shares the panel host with §9 here.
- -> XREF: D08 T01 §14 -- the AI assistant's undoable alt-text Apply (D08 T01 §14) feeds this tagger.
- -> XREF: D04 T04 §2 -- the `.pptx` reader reopens every PowerPoint export as its validator.
- -> XREF: D06 T03 §4 -- the Reading Order engine reads content through the one interpreter.
- -> XREF: D05 T01 §13 -- backgrounds (D05 T01 §13) stay artifacts under PDF/UA.
- -> XREF: D04 T06 §2 -- preflight and PDF/X (D04 T06) build on this validator.
- -> XREF: D03 T03 §41 -- XlsxWriter.
- -> XREF: D03 T01 §1 -- page text for math detection.
- -> XREF: D04 T06 §7 -- ICC conversion for contrast.
- -> XREF: D04 T06 §17 -- inventory method.
- -> XREF: D04 T06 §15 -- shared exit codes.
- -> XREF: D04 T07 §15 -- pdfua step.
- -> XREF: D01 T03 §10 -- a11y verb follows the registration rule.
- -> XREF: D06 T04 §4 -- the `D-Signatures.png` board and `design/SPEC.md` section 8.3.3 that §9, §16, §17, §27 here build against.
- -> XREF: D10 T01 §8 -- the Step 3 options controls, Tags tree and Reading Order bar read the generated `recto_tokens.g.h` tokens.
- -> XREF: D10 T02 §12 -- the Tags section is a `ui::TreeView` and the options use `ui::SegmentedControl`, `ui::ComboBox` and `ui::Dialog`.
- -> XREF: D10 T03 §1 -- every control built here exposes its automation name and tooltip through the `recto-ui` UIA provider tree.

## Outcome

- `XlsxWriter` (`src/core/xlsx_writer.cpp`, `§1`, `§2`) converts PDF tables and pages into native ECMA-376 `.xlsx` workbooks with automatic numeric, currency, percentage and date type parsing, merged cells (`<mergeCells>`), cell borders/fills, and 3 sheet organization modes (`One worksheet per table`, `One worksheet per page`, `Single combined worksheet`), plus `.csv` and structured `.xml` exports.
- `WebDocWriter` (`src/core/web_doc_writer.cpp`, `§3`, `§4`) exports PDFs to **HTML5** (`Fixed-Layout Exact Positioning` and `Flowing Semantic HTML` with single-file `data:` URI option), **GitHub-Flavored Markdown (`.md`)**, **EPUB 3 (`.epub`)** reflowable e-books, and **LaTeX (`.tex`)**.
- `ComplianceEngine` (`src/pdf/compliance.cpp`, `§5`-`§8`) validates and converts PDFs to **PDF/A-1a, 1b, 2a, 2b, 2u, 3a, 3b, 3u**, validates/auto-tags **PDF/UA-1** (`/StructTreeRoot` + `MCID`), and reads/writes **ZUGFeRD 2.x / Factur-X** (`PDF/A-3` + `factur-x.xml` + `/AF`).
- The Viewer's `Signatures & Standards` side panel and `PDF/A & E-Invoice` Convert card (`§9`) expose live compliance audit checks, structured e-invoice inspection, and one-click conversion.
- `PptxWriter` (`src/core/pptx_writer.cpp`, `§10`-`§12`, card in `§13`) converts a PDF into a native ECMA-376 PresentationML `.pptx` with one slide per page, editable text boxes in reading order, pictures, vector shapes and tables, validates every output by reopening it with Recto's own OOXML reader, and never reports an empty or unreadable result as success.
- `StructTreeEditor` (`src/pdf/struct_tree_editor.cpp`, `§14`) and the Viewer `Tags` panel (`§16`) add, retag, reorder and delete tags and edit `/Alt` and `/ActualText` while keeping every `/K`, `/P`, `/Pg`, MCID and `/ParentTree` entry consistent, as Acrobat Pro's Tags panel does.
- `ReadingOrderEditor` (`src/pdf/reading_order.cpp`, `§15`) and the Viewer `Reading Order` tool (`§17`) draw content regions, tag them as heading, paragraph, figure, table or artifact, and reorder them, building on the `PDF/UA-1` validator and auto-tagger of `§7`.
- `MathRegionDetector`, `MathExporter` and `tag_formulas` (`§18`-`§21`) find equations, export them as MathML or LaTeX and tag them as `Formula` elements with MathML associated files, keeping uncertain regions as text.
- `ContrastChecker`, the PDF/UA-2 rule set, the WCAG 2.2 report and `recto-cli a11y` (`§22`-`§27`) check accessibility beyond PDF/UA-1, and never report `Passed` or conformance for content they could not evaluate.

**Adjacency:** list=not-applicable (inputs are files the user picks; the queue list is D04 T01 §5); document=applicable @ D04 T05 §25; settings=not-applicable (no setting of its own; the default output folder is D02 T04 §5); reporting=applicable @ D04 T05 §25; notifications=applicable @ D04 T01 §6; permissions=applicable @ D04 T01 §7; audit=not-applicable (no conversion history is kept on disk, D04 T01 §5); exchange=applicable @ D04 T05 §8; reverse=applicable @ D04 T01 §2

**Adjacency rationale:** Queue management and notifications live in `D04 T01`; this file owns the spreadsheet, web/e-book, and compliance/e-invoicing engines and their Convert/Viewer surfaces.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Native ECMA-376 `XlsxWriter` (`.xlsx`, `.csv`, `.xml`) over `TableExtractor` and CLI | D04 T02 §8, D04 T03 §1 | [ ] |
| 2 | §2 | Convert page **Excel & Data (`.xlsx`, `.csv`, `.xml`)** card and Step 3 UI (`SPEC 9.5`) | §1, D04 T01 §4, D04 T02 §10, D10 T02 §2, D10 T02 §3 | [ ] |
| 3 | §3 | `WebDocWriter` HTML5 (`Fixed` & `Flowing`) and GitHub-Flavored Markdown (`.md`) export | D04 T02 §8, D03 T01 §1, D04 T03 §1 | [ ] |
| 4 | §4 | `WebDocWriter` EPUB 3 (`.epub`), LaTeX (`.tex`) and **HTML, Markdown, EPUB & LaTeX** UI | §3, D04 T01 §4, D10 T02 §2 | [ ] |
| 5 | §5 | `PDF/A` validator (`1a/1b/2a/2b/2u/3a/3b/3u` ISO 19005 clauses in `compliance.cpp`) | D04 T02 §2, D01 T02 §2 | [ ] |
| 6 | §7 | `PDF/UA-1` (`ISO 14289-1`) validator and auto-tagger (`/StructTreeRoot` & `/MCID` tags) | §5, D03 T01 §1, D04 T02 §8, D04 T03 §2 | [ ] |
| 7 | §6 | `PDF/A` converter (font embedding, `/OutputIntents`, `PDF/A-1` flattening, XMP `pdfaid`) | §5, §7 | [ ] |
| 8 | §8 | `ZUGFeRD 2.x / Factur-X` (`EN 16931`) hybrid `PDF/A-3` e-invoice engine & `recto-cli` | §6 | [ ] |
| 9 | §9 | Viewer `Signatures & Standards` side panel (`SPEC 8.3`) and `PDF/A & E-Invoice` card | §6, §7, §8, D02 T03 §3, D04 T01 §4, D06 T04 §4, D10 T01 §8, D10 T02 §2 | [ ] |
| 10 | §10 | `PptxWriter` PresentationML package: one slide per page, text boxes, reading order | D04 T02 §8, D04 T03 §1, D04 T03 §2, D03 T01 §1 | [ ] |
| 11 | §11 | `PptxWriter` pictures, vector shapes and `a:tbl` tables with raster fallback | §10, D04 T02 §9, D01 T02 §1, D01 T02 §6, D03 T01 §7, D03 T01 §8 | [ ] |
| 12 | §12 | `PptxWriter` reopen validation, empty-result refusal and `recto-cli pdf-to-pptx` | §11, D04 T04 §2, D01 T03 §1, D01 T02 §24 | [ ] |
| 13 | §13 | Convert page **PowerPoint presentation (`.pptx`)** card and Step 3 UI (`SPEC 9.5`) | §12, D04 T01 §4, D02 T03 §11, D03 T02 §3, D10 T01 §8, D10 T02 §2, D10 T02 §3, D10 T02 §18, D10 T03 §1 | [ ] |
| 14 | §14 | `StructTreeEditor` engine: add, retag, move, delete tags, edit `/Alt` and `/ActualText` | §7, D04 T02 §1, D04 T02 §2, D03 T01 §1, D01 T03 §4 | [ ] |
| 15 | §15 | `ReadingOrderEditor` engine: content regions, roles, artifacts and reorder | §14, D06 T03 §4, D01 T02 §4, D04 T02 §8 | [ ] |
| 16 | §16 | Viewer **Tags** tree editor panel in `Signatures & Standards` (`SPEC 8.3`) | §9, §14, §15, D02 T03 §3, D02 T03 §11, D02 T01 §8, D06 T04 §4, D10 T01 §8, D10 T01 §11, D10 T02 §2, D10 T02 §12, D10 T02 §17, D10 T03 §1 | [ ] |
| 17 | §17 | Viewer **Reading Order** tool: draw regions, assign roles, reorder, mark artifacts | §15, §16, D05 T02 §9, D03 T01 §3, D02 T03 §11, D06 T04 §4, D10 T01 §8, D10 T01 §11, D10 T02 §2, D10 T02 §3, D10 T03 §1 | [ ] |
| 18 | §18 | `MathRegionDetector` engine: math zones, symbols and expression trees | D03 T01 §1, D01 T02 §1, §14 | [ ] |
| 19 | §19 | `MathExporter`: MathML 3 and LaTeX serializers, web equations, `recto-cli math` | §18, §3, §4 | [ ] |
| 20 | §20 | Tag detected formulas as `Formula` elements with MathML associated files | §18, §19, §14, §15 | [ ] |
| 21 | §21 | Equations option on the web Convert card and the Reading Order bar | §4, §17, §19, §20, D02 T03 §11, D10 T01 §8, D10 T01 §11, D10 T02 §2, D10 T02 §3, D10 T03 §1 | [ ] |
| 22 | §22 | Colour contrast check engine (WCAG 2.2 success criterion 1.4.3) | §5, §7, D03 T01 §1, D01 T02 §1, D01 T02 §6, D04 T06 §7 | [ ] |
| 23 | §23 | PDF/UA-2 (ISO 14289-2) rule set, requirement inventory and validator | §5, §7, D04 T06 §2 | [ ] |
| 24 | §24 | PDF/UA-2 output: the auto-tagger writes PDF 2.0 structure | §7, §23 | [ ] |
| 25 | §25 | WCAG 2.2 report model: criteria mapping, statuses and exporters | §7, §22, §23, D04 T06 §2 | [ ] |
| 26 | §26 | `recto-cli a11y` check and report commands | §25, D01 T03 §1 | [ ] |
| 27 | §27 | Accessibility checks and WCAG report in the Standards & Accessibility panel | §9, §16, §22, §24, §25, D02 T03 §11, D06 T04 §4, D10 T01 §8, D10 T01 §11, D10 T02 §2, D10 T02 §3, D10 T03 §1 | [ ] |

## 1. Native ECMA-376 `XlsxWriter` (`.xlsx`, `.csv`, `.xml`) over `TableExtractor` and CLI

Implement `src/core/xlsx_writer.h` and `src/core/xlsx_writer.cpp` (`recto::core::XlsxWriter`), plus `recto-cli pdf-to-xlsx`, `recto-cli pdf-to-csv`, and `recto-cli pdf-to-xml`: consume `TableExtractor` (`D04 T02 §8`) and `OpenXmlPackageBuilder` / `ZipWriter` (`D04 T03 §1`) to emit valid ECMA-376 `.xlsx` packages (`[Content_Types].xml`, `_rels/.rels`, `xl/workbook.xml`, `xl/_rels/workbook.xml.rels`, `xl/sharedStrings.xml`, `xl/styles.xml`, `xl/worksheets/sheet1..N.xml`) with `<cols>` auto-fitted column widths, `<mergeCells>` for `rowspan`/`colspan` cells, cell borders and fill colours, and automatic numeric/currency/percentage/ISO-date cell value and number-format (`numFmtId`) detection across 3 sheet organization modes (`OneSheetPerTable`, `OneSheetPerPage`, `SingleCombinedSheet`), plus RFC 4180 `.csv` and structured `.xml` exports.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `XlsxWriter` in `src/core/xlsx_writer.cpp` generating SpreadsheetML `.xlsx` packages with deduplicated `sharedStrings.xml`, `styles.xml` (`numFmts` for currency/percentage/date, `fonts`, `fills`, `borders`, `cellXfs`), typed `<c r="A1" t="n|s"><v>...</v></c>` cells, `<mergeCells>`, and all 3 sheet-grouping modes. Done when: `XlsxWriter.*` converts bordered and borderless table corpus PDFs into `.xlsx` files whose numeric/currency cells have `t="n"` (summable in Excel) and round-trip through `D04 T04 §2`'s `OfficeToPdf` reader under ASan.
2. Implement the multi-table `.csv` and structured `<document><page><table>...` `.xml` exporters and `recto-cli pdf-to-xlsx`, `recto-cli pdf-to-csv`, and `recto-cli pdf-to-xml`. Done when: `Cli.PdfToXlsxCsvXml` passes under ASan.

- [ ] Define `SheetOrganizationMode` (`OneSheetPerTable`, `OneSheetPerPage`, `SingleCombinedSheet`), `DataExportFormat` (`Xlsx`, `Csv`, `Xml`), and `PdfToExcelOptions` (`format`, `sheet_mode`, `parse_numeric_and_date_types`, `include_non_table_text`, `auto_ocr_scanned_pages`, `utf8_bom_for_csv`, `page_range`) in `src/core/xlsx_writer.h`. Done when: `src/core/xlsx_writer.h` compiles cleanly across `recto-core`, `recto-services`, and `recto-cli`.
- [ ] Emit `xl/workbook.xml` (sanitizing sheet names to `<= 31` chars and stripping `:?/\*[]`), `xl/_rels/workbook.xml.rels`, and deduplicated `xl/sharedStrings.xml` via `OpenXmlPackageBuilder` (`D04 T03 §1`). Done when: `XlsxWriter.PackageStructure` produces valid SpreadsheetML relationships and sheet names.
- [ ] Emit typed SpreadsheetML `<c>` cells in `xl/worksheets/sheetK.xml` using `TableCell::value_kind` (`D04 T02 §8`): `Number`, `Currency`, and `Percentage` cells emit `<c r="B2" s="<xf_idx>"><v><double></v></c>` (`t="n"` default), `IsoDate` cells emit Excel 1900 serial dates with date `numFmtId = 14`, and `Text` cells reference `sharedStrings.xml` (`t="s"`). Done when: `XlsxWriter.NumericAndDateCells` verifies numeric/currency/date cells are written as numbers (`t="n"`) rather than text strings. Cheaper substitute: storing every cell as an unformatted string (`t="s"`) that `SUM()` in Excel ignores, or renaming a `.csv` file to `.xlsx`.
- [ ] Emit `<cols><col min="C" max="C" width="..." customWidth="1"/></cols>` auto-fitted to cell character widths and `<mergeCells count="M"><mergeCell ref="A1:C1"/></mergeCells>` for every cell with `row_span > 1` or `col_span > 1`, plus cell background fills (`<fills>`) and grid borders (`<borders>`) in `xl/styles.xml`. Done when: `XlsxWriter.MergedCellsAndStyles` verifies `<mergeCell ref="..."/>` entries match `TableExtractor` spans.
- [ ] Implement all 3 sheet organization modes (`OneSheetPerTable`, `OneSheetPerPage`, and `SingleCombinedSheet` stacking tables with a blank separator row), plus optional inclusion of non-table page paragraphs when `include_non_table_text == true`. Done when: `XlsxWriter.SheetModes` verifies worksheet counts for each mode on a multi-page, multi-table PDF.
- [ ] Implement RFC 4180 `.csv` (with optional UTF-8 BOM `\xEF\xBB\xBF` so Microsoft Excel opens UTF-8 characters cleanly on double-click) and structured `.xml` (`<document><page number="1"><table index="1"><row><cell type="currency" value="1234.50">$1,234.50</cell>...`) committed via `AtomicFileWriter` (`D04 T02 §1`). Done when: `XlsxWriter.CsvAndXmlExport` validates `.csv` and `.xml` outputs.
- [ ] Implement `recto-cli pdf-to-xlsx`, `recto-cli pdf-to-csv`, and `recto-cli pdf-to-xml` in `src/cli/cmd_pdf_to_excel.cpp`. Done when: `recto-tests.exe --gtest_filter=XlsxWriter.*:Cli.PdfToXlsxCsvXml` passes under ASan.

- [ ] Commit: native ECMA-376 `XlsxWriter` (`.xlsx`, `.csv`, `.xml`) over `TableExtractor` and `recto-cli pdf-to-xlsx`/`csv`/`xml`

**Test checkpoint:** `recto-tests.exe --gtest_filter=XlsxWriter.*:Cli.PdfToXlsxCsvXml` passes under ASan.

## 2. Convert page **Excel & Data (`.xlsx`, `.csv`, `.xml`)** card and Step 3 UI (`SPEC 9.5`)

Register the **Excel & Data (`.xlsx`, `.csv`, `.xml`)** card in `ConverterRegistry` (`D04 T01 §3`), build its Step 3 options control (`src/app/views/convert/excel_convert_options_control.h/.cpp`, `SPEC 9.5`), and wire `TablePreviewDialog`'s `Save as .xlsx` button (`D04 T02 §10`) to `XlsxWriter`.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.5 (`Excel & Data (.xlsx, .csv, .xml)` card and Step 3 options) with `D-Convert.png`.
**Job:** The user can convert financial statements, invoices, schedules, and data tables in PDFs into native Excel `.xlsx` workbooks, `.csv` spreadsheets, or `.xml` data files from the Convert page or directly from the viewer's `Preview Table` modal. Consumer: `ConvertPage` (`D04 T01 §3`-`§4`), `TablePreviewDialog` (`D04 T02 §10`), and `ConversionRunner`.
**Treatment:** Step 3 options control bound to `ExcelConvertOptionsViewModel` exposing format, sheet organization mode, numeric/currency/date parsing toggle, non-table text toggle, and OCR toggle. Cheaper substitute that fails the checkpoint: hard-coding a single sheet mode with no `.csv`/`.xml` or sheet-organization selector.
**Chrome:** consume the Convert Step 2 card and Step 3 options host from `D04 T01 §3`-`§4`.

**Build order:**

1. Implement `ExcelConvertOptionsViewModel` and `excel_convert_options_control.h/.cpp` (`SPEC 9.5`), register the **Excel & Data** card in `ConverterRegistry`, and wire `TablePreviewDialog`'s `Save as .xlsx` action (`D04 T02 §10`). Done when: `AppTests.ConvertExcelUi` runs `.xlsx` (across all 3 sheet modes), `.csv`, and `.xml` exports through `ConversionRunner`.

- [ ] Implement `ExcelConvertOptionsViewModel` in `src/services/excel_convert_options_view_model.h/.cpp` exposing `OutputFormat` (`Xlsx`, `Csv`, `Xml`), `SheetMode` (`OneSheetPerTable`, `OneSheetPerPage`, `SingleCombinedSheet`), `ParseNumericAndDateValues` (`true`), `IncludeNonTableText` (`false`), and `AutoOcrScannedPages` (`true`). Done when: selecting `Csv` or `Xml` disables the Excel-only `SheetMode` dropdown cleanly.
- [ ] Build `ExcelConvertOptionsControl` (`src/app/views/convert/excel_convert_options_control.h/.cpp`) with accessible labels, tooltips, and the SPEC Principle 4 honest capability hint line (`"Reconstructs bordered and borderless tables into native Excel cells; numeric and currency columns are formatted for formulas."`). Done when: all controls pass `D03 T02 §3` accessibility checks. Cheaper substitute that fails the checkpoint: omitting sheet organization options or the capability hint line.
- [ ] Register the **Excel & Data (`.xlsx`, `.csv`, `.xml`)** card in `ConverterRegistry` (`D04 T01 §3`) with dynamic primary button labels (`"Convert to Excel (.xlsx)"`, `"Export CSV (.csv)"`, `"Export XML (.xml)"`). Done when: selecting a PDF in Step 1 enables the card and updates the primary action button.
- [ ] Connect `TablePreviewDrawer`'s `Save as .xlsx` button (`D04 T02 §10`) directly to `XlsxWriter::write_single_table` via `AtomicFileWriter`. Done when: clicking `Save as .xlsx` in the viewer's table preview writes a valid `.xlsx` workbook for the lassoed/detected table.
- [ ] Add `AppTests.ConvertExcelUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=AppTests.ConvertExcelUi` passes.
- [ ] Commit: `convert: implement native C++23 XlsxWriter (.xlsx, .csv, .xml) and Convert UI`

**Test checkpoint:** `recto-tests.exe --gtest_filter=XlsxWriter.*:AppTests.ConvertExcelUi` passes under ASan.

## 3. `WebDocWriter` HTML5 (`Fixed` & `Flowing`) and GitHub-Flavored Markdown (`.md`) export

Implement the HTML5 and Markdown exporters of `WebDocWriter` in `src/core/web_doc_writer.h` and `src/core/web_doc_writer.cpp`, plus `recto-cli pdf-to-html` and `recto-cli pdf-to-md`:
- **HTML5 (`Fixed-Layout Exact Positioning` & `Flowing Semantic HTML`):**
  - *Fixed-Layout:* Each page emitted as an accurately sized `<section class="pdf-page" style="width:...pt;height:...pt;">` with an `<svg>` backdrop for vector paths/shapes and positioned `<span>` text runs, supporting either a single self-contained `.html` file (`data:image/png;base64,...` inline assets) or an `.html` + `<stem>_assets/` folder.
  - *Flowing Semantic HTML:* Reconstructs `<article>`, `<h1>`..`<h6>`, `<p>`, `<ul>`/`<ol>`, `<table>` (`<thead>`, `<tbody>`, `<tr>`, `<th/td colspan rowspan>`), and `<figure><img/></figure>` in reading order using `PageText` (`D03 T01 §1`) and `TableExtractor` (`D04 T02 §8`).
- **GitHub-Flavored Markdown (`.md`):** Clean GFM with `#`..`######` headings, flowing paragraphs, `*` / `1.` lists, `**bold**` / `*italic*` / `` `code` `` spans, pipe `|---|---|` tables, and extracted image links.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `export_pdf_to_html` (`FixedLayout` and `FlowingSemantic`, single-file `data:` URI mode vs external asset folder) and `export_pdf_to_markdown` in `src/core/web_doc_writer.cpp`, plus `recto-cli pdf-to-html` and `recto-cli pdf-to-md`. Done when: `WebDocWriter.HtmlAndMarkdown` converts prose, multi-column, table, and illustrated corpus PDFs into valid HTML5 and GFM `.md` files that round-trip through `D04 T04 §3`'s `HtmlMdToPdf` engine under ASan.

- [ ] Define `HtmlLayoutMode` (`FlowingSemantic`, `FixedExactLayout`), `WebExportFormat` (`Html5`, `Markdown`, `Epub3`, `Latex`), and `WebDocExportOptions` (`format`, `html_layout_mode`, `embed_assets_as_data_uri`, `include_images`, `auto_ocr_scanned_pages`, `page_range`) in `src/core/web_doc_writer.h`. Done when: `src/core/web_doc_writer.h` compiles cleanly across `recto-core`, `recto-services`, and `recto-cli`.
- [ ] Implement `HtmlLayoutMode::FlowingSemantic` in `src/core/web_doc_writer.cpp`: emit responsive semantic HTML5 (`<!DOCTYPE html>`, `<html lang="...">`, `<head><meta charset="utf-8"/><style>...</style></head>`, `<body><main><article>...`) with `<h1>`..`<h3>` headings, `<p>`, `<ul>`/`<ol>`, `<table border="1">` with `colspan`/`rowspan` from `TableExtractor`, and `<nav class="toc">` from PDF `/Outlines`. Done when: `WebDocWriter.HtmlFlowingSemantic` produces valid semantic HTML5 with selectable text and `<table>` elements.
- [ ] Implement `HtmlLayoutMode::FixedExactLayout`: emit each PDF page as a positioned `<section class="page" data-page="N">` with an inline `<svg>` containing the page's vector paths/strokes/fills and positioned `<span style="left:...pt;top:...pt;font-size:...pt;">` text runs matching `PageText` quads. Done when: `WebDocWriter.HtmlFixedLayout` preserves exact 2D coordinates and vector graphics. Cheaper substitute: rendering whole pages as flat `.png` screenshots inside an HTML file with no selectable text or DOM elements.
- [ ] Support both **Single self-contained `.html` file** (encoding extracted PNG/JPEG images as `data:image/png;base64,...` URIs) and **HTML + `<stem>_files/` asset folder** modes, committed via `AtomicFileWriter` (`D04 T02 §1`). Done when: `WebDocWriter.HtmlDataUriAndFolder` verifies zero external file dependencies in single-file mode.
- [ ] Implement `export_pdf_to_markdown` in `src/core/web_doc_writer.cpp` generating clean GitHub-Flavored Markdown (`.md`) with ATX headings (`#`..`###`), reflowed paragraphs, bulleted/numbered lists, inline `**bold**` / `*italic*` / `` `code` `` formatting, pipe tables (`| Header | ... |\n| --- | --- |`), and `![Figure](<asset_path>)` links. Done when: `WebDocWriter.MarkdownGfm` converts a report with headings, lists, and a table into GFM that round-trips back to PDF via `D04 T04 §3`.
- [ ] Implement `recto-cli pdf-to-html <in.pdf> --out <out.html> [--mode flowing|fixed] [--single-file]` and `recto-cli pdf-to-md <in.pdf> --out <out.md>` in `src/cli/cmd_pdf_to_web.cpp`. Done when: `recto-tests.exe --gtest_filter=WebDocWriter.Html*:WebDocWriter.Markdown*:Cli.PdfToHtmlAndMd` passes under ASan.

- [ ] Commit: `WebDocWriter` HTML5 (`Fixed-Layout` & `Flowing Semantic`) and GitHub-Flavored Markdown (`.md`) exporters (`recto-cli pdf-to-html`/`md`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=WebDocWriter.Html*:WebDocWriter.Markdown*:Cli.PdfToHtmlAndMd` passes under ASan.

## 4. `WebDocWriter` EPUB 3 (`.epub`), LaTeX (`.tex`) and **HTML, Markdown, EPUB & LaTeX** UI

Complete `WebDocWriter` in `src/core/web_doc_writer.cpp` with native **EPUB 3 (`.epub`)** (`mimetype` uncompressed first ZIP entry, `META-INF/container.xml`, `OEBPS/content.opf`, `OEBPS/nav.xhtml` + `OEBPS/toc.ncx`, reflowable chapter `.xhtml` files split at `/Outlines` or `H1` boundaries, `OEBPS/styles.css`, and `OEBPS/images/*`) and **LaTeX (`.tex`)** (`\documentclass{article}`, `\usepackage[utf8]{inputenc}`, `\usepackage{graphicx,booktabs,longtable,hyperref}`, `\section`/`\subsection`, `\textbf`/`\textit`, `tabular` environments, and `\includegraphics`) exporters, add `recto-cli pdf-to-epub` and `recto-cli pdf-to-tex`, and build the **HTML, Markdown, EPUB & LaTeX** card and Step 3 options UI in `ConvertPage` (`SPEC 9.5`).

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.5 (`HTML, Markdown, EPUB & LaTeX` card and Step 3 options) with `D-Convert.png`.
**Job:** Users can convert PDFs into reflowable EPUB 3 e-books for e-readers, editable LaTeX `.tex` manuscripts, HTML5 web pages, or Markdown notes through the Convert page. Consumer: e-readers, TeX compilers, browsers, Markdown tools, and `ConversionRunner`.
**Treatment:** Standards-compliant EPUB 3.2 ZIP packager (enforcing uncompressed `mimetype` at byte offset `0..58` with zero extra fields per OCF 3.2) and LaTeX emitter with special-character escaping (`\`, `{`, `}`, `$`, `&`, `#`, `_`, `%`, `~`, `^`), paired with the Step 3 options control. Cheaper substitute that fails the checkpoint: compressing the EPUB `mimetype` entry (which fails EPUBCheck/e-readers) or dumping unescaped PDF text into `.tex` without `\section` and `tabular` structure.
**Chrome:** consume the Convert Step 2 card and Step 3 options host from `D04 T01 §3`-`§4`.

**Build order:**

1. Implement `export_pdf_to_epub` and `export_pdf_to_latex` in `src/core/web_doc_writer.cpp`, plus `recto-cli pdf-to-epub` and `recto-cli pdf-to-tex`. Done when: `WebDocWriter.Epub3AndLatex` verifies the EPUB `mimetype` magic bytes at offset `30..58` (`"application/epub+zip"` stored uncompressed), valid XHTML chapters/navigation, and valid LaTeX `\section` and `\begin{tabular}` environments under ASan.
2. Register the **HTML, Markdown, EPUB & LaTeX** card and build `WebConvertOptionsControl` (`src/app/views/convert/web_convert_options_control.h/.cpp`) in `ConvertPage` (`SPEC 9.5`). Done when: `AppTests.ConvertWebDocUi` runs HTML (Flowing and Fixed), Markdown, EPUB 3, and LaTeX jobs through `ConversionRunner`.

- [ ] Implement `export_pdf_to_epub` in `src/core/web_doc_writer.cpp` using `ZipWriter::add_stored_entry` (`D04 T03 §1`) to write `"mimetype"` (`"application/epub+zip"`, 20 bytes uncompressed, method `0`, first entry in the archive), `META-INF/container.xml`, `OEBPS/content.opf` (`package version="3.0"`, Dublin Core `<dc:title>`, `<dc:creator>`, `<dc:language>`, `<dc:identifier>`, `<manifest>`, `<spine>`), `OEBPS/nav.xhtml` (`epub:type="toc"`), and chapter `OEBPS/chapter-1..N.xhtml` split at top-level PDF `/Outlines` or `Heading1` boundaries. Done when: `WebDocWriter.Epub3PackageCompliance` verifies offset `30` is `"mimetype"`, compression method is `0`, and all manifest items exist and parse as strict XML. Cheaper substitute that fails the checkpoint: deflate-compressing `mimetype` or omitting `nav.xhtml`.
- [ ] Implement `export_pdf_to_latex` in `src/core/web_doc_writer.cpp` escaping all 10 LaTeX reserved characters (`\textbackslash{}`, `\{`, `\}`, `\$`, `\&`, `\#`, `\_`, `\%`, `\textasciitilde{}`, `\textasciicircum{}`), mapping headings to `\section{...}` / `\subsection{...}` / `\subsubsection{...}`, lists to `\begin{itemize|enumerate}`, tables from `TableExtractor` to `\begin{tabular}{...}` with `\multicolumn`, and images to `\includegraphics`. Done when: `WebDocWriter.LatexStructureAndEscaping` produces balanced `\begin{...}` / `\end{...}` environments and escapes `$100% & #1_test` cleanly.
- [ ] Implement `recto-cli pdf-to-epub <in.pdf> --out <out.epub>` and `recto-cli pdf-to-tex <in.pdf> --out <out.tex>` in `src/cli/cmd_pdf_to_web.cpp`. Done when: `Cli.PdfToEpubAndLatex` passes under ASan.
- [ ] Build `WebConvertOptionsControl` (`src/app/views/convert/web_convert_options_control.h/.cpp`, `SPEC 9.5`) exposing `Format` (`HTML5 (.html)`, `Markdown (.md)`, `EPUB 3 E-Book (.epub)`, `LaTeX (.tex)`), `HTML layout` (`Flowing semantic HTML` | `Fixed exact page layout`), `Self-contained single HTML file (embed images as data URIs)` toggle, `Include images` toggle, and `OCR scanned pages` toggle. Done when: switching between `HTML5`, `Markdown`, `EPUB 3`, and `LaTeX` updates the visible option rows and primary button label (`"Convert to HTML"`, `"Convert to Markdown"`, `"Convert to EPUB"`, `"Convert to LaTeX"`).
- [ ] Register the **HTML, Markdown, EPUB & LaTeX** card in `ConverterRegistry` (`D04 T01 §3`) with the SPEC Principle 4 honest capability hint line. Done when: `AppTests.ConvertWebDocUi` runs all 4 formats through the queue and verifies output files.
- [ ] Commit: `convert: implement PDF to HTML5, Markdown, EPUB 3 and LaTeX writer and UI`

**Test checkpoint:** `recto-tests.exe --gtest_filter=WebDocWriter.*:Cli.PdfTo*:AppTests.ConvertWebDocUi` passes under ASan.

## 5. `PDF/A` validator (`1a/1b/2a/2b/2u/3a/3b/3u` ISO 19005 clauses in `compliance.cpp`)

Implement the `PDF/A` compliance validator in `src/pdf/compliance.h` and `src/pdf/compliance.cpp` (`recto::pdf::ComplianceEngine::validate_pdfa`): check all ISO 19005-1 (`PDF/A-1a`, `1b`), ISO 19005-2 (`PDF/A-2a`, `2b`, `2u`), and ISO 19005-3 (`PDF/A-3a`, `3b`, `3u`) requirements -- font embedding completeness (`/FontFile`, `/FontFile2`, `/FontFile3`), `/ToUnicode` CMap completeness (`u` and `a` levels), device-independent colour spaces or `/OutputIntents` ICC profile presence, XMP `pdfaid:part` and `pdfaid:conformance` metadata synchronization with `/Info`, absence of encryption (`/Encrypt`), absence of prohibited `/JavaScript` / `/Launch` / `/Sound` / `/Movie` actions and `LZWDecode` streams, and transparency prohibition in `PDF/A-1` (`/CA == 1.0`, `/ca == 1.0`, `/BM /Normal`, no `/SMask`) -- returning a structured list of `ComplianceFinding` diagnostics (`clause_id`, `category`, `severity`, `page_index`, `object_id`, `message`, `is_auto_fixable`).

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `PdfStandardLevel` (`PdfA_1b`, `PdfA_1a`, `PdfA_2b`, `PdfA_2u`, `PdfA_2a`, `PdfA_3b`, `PdfA_3u`, `PdfA_3a`, `PdfUa_1`, `Zugferd_2`), `ComplianceCategory` (`Fonts`, `ColourAndOutputIntent`, `MetadataAndXmp`, `Transparency`, `ActionsAndEncryption`, `TaggedStructure`, `EmbeddedFiles`), `ComplianceFinding`, and `ComplianceReport` in `src/pdf/compliance.h`. Done when: `src/pdf/compliance.h` compiles cleanly across `recto-pdf`, `recto-core`, `recto-services`, and `recto-cli`.
2. Implement `ComplianceEngine::validate_pdfa` in `src/pdf/compliance.cpp` auditing every clause across `PDF/A-1`, `PDF/A-2`, and `PDF/A-3`. Done when: `PdfCompliance.ValidatePdfAClauses` detects unembedded Standard-14 fonts, missing `/OutputIntents`, missing `pdfaid` XMP packets, `PDF/A-1` transparency (`0.5 ca`), missing `/ToUnicode` maps on `2u`/`3u`, and missing `/StructTreeRoot` on `1a`/`2a`/`3a` with exact object IDs and page numbers under ASan.

- [ ] Implement XMP `/Metadata` packet inspection in `src/pdf/compliance.cpp` parsing `pdfaid:part` (`1`, `2`, `3`) and `pdfaid:conformance` (`A`, `B`, `U`) and checking synchronization between `/Info` (`/Title`, `/Author`, `/Creator`, `/Producer`, `/CreationDate`, `/ModDate`) and XMP Dublin Core / `xmp:` / `pdf:` properties (`ISO 19005-1 clause 6.7`). Done when: `PdfCompliance.XmpAndInfoSync` detects mismatched `/Info` vs XMP metadata or missing `pdfaid` declarations.
- [ ] Audit every font dictionary across all pages, Form XObjects, and `/AP` appearance streams (`ISO 19005-1 clause 6.3`): verify `/FontDescriptor` contains an embedded `/FontFile`, `/FontFile2`, or `/FontFile3` stream, `/CIDToGIDMap` is present on Type 2 CIDFonts, glyph widths in content streams match `/Widths` / `/W`, and every glyph has a valid Unicode mapping in `/ToUnicode` when validating `a` or `u` conformance. Done when: `PdfCompliance.FontAudit` flags an unembedded `/Helvetica` font with its exact page index and font object ID.
- [ ] Audit colour spaces and `/OutputIntents` (`ISO 19005-1 clause 6.2`): verify that any `DeviceRGB`, `DeviceCMYK`, or uncalibrated colour operator is accompanied by a matching `/OutputIntent` (`/S /GTS_PDFA1`, `/DestOutputProfile` ICC profile stream with matching `/N 3` or `/N 4` components). Done when: `PdfCompliance.ColourAudit` flags `DeviceRGB` usage when `/OutputIntents` is absent.
- [ ] Audit transparency rules for `PDF/A-1a` and `PDF/A-1b` (`ISO 19005-1 clause 6.4`): flag any `/ExtGState` with `/CA < 1.0`, `/ca < 1.0`, `/BM` other than `/Normal` or `/Compatible`, any `/SMask` dictionary/stream, or any `/Group << /S /Transparency >>`. Done when: `PdfCompliance.PdfA1Transparency` flags translucent highlights and `/SMask` images under `PDF/A-1b` while permitting them under `PDF/A-2b` and `PDF/A-3b`. Cheaper substitute: checking only the XMP `pdfaid:part` string without inspecting fonts, colour spaces, or transparency operators.
- [ ] Audit prohibited features across all PDF/A levels: `/Encrypt` in trailer, `LZWDecode` stream filter, `/JavaScript` / `/JS` / `/Launch` / `/Sound` / `/Movie` / `/ResetForm` / `/ImportData` actions, external stream `/F` file specifications, and embedded files (`/EmbeddedFiles` prohibited in `PDF/A-1`, restricted to PDF/A files in `PDF/A-2`, and required to carry `/AF` + `/AFRelationship` in `PDF/A-3`). Done when: `PdfCompliance.ProhibitedFeatures` detects each violation accurately.
- [ ] Audit logical structure requirements for `1a`, `2a`, and `3a` conformance (`/MarkInfo << /Marked true >>`, `/StructTreeRoot` hierarchy, and `/Lang`). Done when: `PdfCompliance.LevelAStructure` distinguishes `1b`/`2b` from `1a`/`2a`.
- [ ] Add `PdfCompliance.Validate*` unit tests in `tests/recto-tests/pdf_compliance_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfCompliance.Validate*` passes under ASan.

- [ ] Commit: `PDF/A` validator (`1a/1b/2a/2b/2u/3a/3b/3u` ISO 19005-1/2/3 clauses in `src/pdf/compliance.cpp`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCompliance.Validate*` passes under ASan across all 8 PDF/A conformance levels.

## 6. `PDF/A` converter (font embedding, `/OutputIntents`, `PDF/A-1` flattening, XMP `pdfaid`)

Implement `ComplianceEngine::convert_to_pdfa` in `src/pdf/compliance.cpp` and `recto-cli pdfa <validate|convert> <in.pdf> [--level 1b|1a|2b|2u|2a|3b|3u|3a|ua1] [--out <out.pdf>] [--json]`: convert arbitrary PDFs into compliant `PDF/A-1a`, `1b`, `2a`, `2b`, `2u`, `3a`, `3b`, or `3u` documents by embedding and subsetting any unembedded fonts via DirectWrite system font lookup (`D01 T02 §2`), generating `/ToUnicode` CMap streams (`u` / `a` levels), invoking `§7`'s `/StructTreeRoot` auto-tagger whenever `1a`, `2a`, or `3a` is requested on an untagged input PDF, attaching a compact standard sRGB (or CMYK) ICC `/OutputIntent` profile, flattening transparency groups when targeting `PDF/A-1a`/`1b`, stripping prohibited actions/LZW filters, and writing synchronized `/Info` and XMP `pdfaid:part` / `pdfaid:conformance` metadata via `PdfWriter` (`D04 T02 §2`).

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `ComplianceEngine::convert_to_pdfa` in `src/pdf/compliance.cpp` remediating every fixable `ComplianceFinding` from `§5` (font embedding + subsetting + `/ToUnicode` generation, `§7` `/StructTreeRoot` auto-tagging for `1a`/`2a`/`3a`, `/OutputIntents` ICC injection, `LZWDecode -> FlateDecode` transcoding, `PDF/A-1` transparency flattening, `/AP /N` appearance stream completion, and XMP `pdfaid` packet synthesis). Done when: converting non-compliant (including untagged) corpus PDFs to each of the 8 `PDF/A` levels (`1a`, `1b`, `2a`, `2b`, `2u`, `3a`, `3b`, `3u`) and running `validate_pdfa` (§5) on the output produces `0` compliance findings under ASan.
2. Implement `recto-cli pdfa <validate|convert>` (supporting `1b|1a|2b|2u|2a|3b|3u|3a|ua1`) in `src/cli/cmd_pdfa.cpp`. Done when: `Cli.PdfACommand` validates and converts PDFs across `PDF/A` and `PDF/UA-1` with deterministic exit codes on the `D01 T03 §1` contract (`0` compliant, `4` non-compliant findings, the same code `recto-cli preflight` uses in `D04 T06 §15`; `1` stays invalid arguments) under ASan.

- [ ] Embed and subset unembedded fonts (including Standard 14 `/Helvetica`, `/Times-Roman`, `/Courier`, etc.) during `convert_to_pdfa` by resolving the matching system TrueType font via DirectWrite (`D01 T02 §2`), subsetting used glyphs (`D04 T02 §11`), attaching `/FontDescriptor /FontFile2` and `/CIDToGIDMap /Identity`, and synthesizing a complete `/ToUnicode` CMap stream. Done when: `PdfCompliance.ConvertEmbedsFontsAndToUnicode` turns an unembedded Standard-14 PDF into a fully font-embedded PDF/A-2u document with `0` font findings. Cheaper substitute: writing `pdfaid:part` into XMP while leaving `/Helvetica` unembedded.
- [ ] Invoke `§7`'s `/StructTreeRoot` and `BDC`/`EMC` `/MCID` auto-tagger during `convert_to_pdfa` whenever `PdfA_1a`, `PdfA_2a`, or `PdfA_3a` is requested on an untagged input PDF. Done when: `PdfCompliance.PdfALevelAAutoTag` converts an untagged PDF to `PDF/A-1a`, `PDF/A-2a`, and `PDF/A-3a` with `0` findings.
- [ ] Inject a compact, standards-compliant ICC v2 sRGB (`/N 3`) or CMYK (`/N 4`) `/DestOutputProfile` stream inside `/Root /OutputIntents [<< /Type /OutputIntent /S /GTS_PDFA1 /OutputConditionIdentifier (sRGB IEC61966-2.1) /DestOutputProfile ... >>]`. Done when: `PdfCompliance.ConvertOutputIntent` satisfies all device-colour clauses.
- [ ] Flatten transparent page objects and `/SMask` images when targeting `PDF/A-1a` or `PDF/A-1b` (compositing transparent regions at `300 DPI` or converting `/BM /Multiply` highlights and clamping `/CA`/`/ca` to `1.0` after blending, and removing `/Group << /S /Transparency >>`). Done when: converting a PDF with translucent highlights and PNG `/SMask` images to `PDF/A-1b` passes `validate_pdfa(PdfA_1b)` with `0` transparency findings and `SSIM >= 0.98`.
- [ ] Transcode any `LZWDecode` streams to `FlateDecode`, ensure every annotation and form widget has an explicit `/AP /N` stream (`D03 T03 §2`, `§6`), and strip prohibited `/JavaScript`, `/Launch`, `/Sound`, and `/Movie` actions. Done when: `PdfCompliance.ConvertStripsProhibited` removes prohibited actions and LZW streams cleanly.
- [ ] Synthesize an uncompressed (`PDF/A` requirement for `/Type /Metadata /Subtype /XML`) UTF-8 XMP packet containing `pdfaid:part`, `pdfaid:conformance`, Dublin Core `dc:title`/`dc:creator`, `xmp:CreateDate`/`xmp:ModifyDate`/`xmp:MetadataDate`, and `pdf:Producer` synchronized with the trailer `/Info` dictionary. Done when: `validate_pdfa` confirms `0` XMP/`/Info` synchronization errors across all 8 levels.
- [ ] Implement `recto-cli pdfa <validate|convert> <in.pdf> [--level 1b|1a|2b|2u|2a|3b|3u|3a|ua1] [--out <out.pdf>] [--json]` in `src/cli/cmd_pdfa.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfCompliance.Convert*:PdfCompliance.PdfALevelAAutoTag:Cli.PdfACommand` passes under ASan.

- [ ] Commit: `PDF/A` converter (DirectWrite font embedding, sRGB/CMYK `/OutputIntents`, `PDF/A-1` transparency flattening, XMP `pdfaid`, `recto-cli pdfa`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCompliance.Convert*:PdfCompliance.PdfALevelAAutoTag:Cli.PdfACommand` passes under ASan across all 8 PDF/A levels.

## 7. `PDF/UA-1` (`ISO 14289-1`) validator and auto-tagger (`/StructTreeRoot` & `/MCID` tags)

Implement `PDF/UA-1` (`ISO 14289-1` Universal Accessibility) validation and automatic structure tagging in `src/pdf/compliance.cpp`:
- **Validator (`validate_pdfua`):** Audits `/MarkInfo << /Marked true >>`, document `/Lang` BCP-47 tag, `/ViewerPreferences << /DisplayDocTitle true >>`, non-empty `dc:title` in XMP, `/StructTreeRoot` logical structure tree (`/Document`, `/Part`, `/Sect`, `/H1`..`/H6`, `/P`, `/L`, `/LI`, `/Lbl`, `/LBody`, `/Table`, `/TR`, `/TH`, `/TD`, `/Figure`), Marked Content Identifiers (`BDC` / `EMC` with `/MCID` or `/Artifact` for headers/footers/decorative rules), `/Alt` text on `/Figure` elements and `/TU` tooltips on form fields, and `/Tabs /S` on every page.
- **Auto-Tagger (`convert_to_pdfua`):** Uses `PageText` reading-order blocks/headings/lists (`D03 T01 §1`, `D04 T03 §2`) and `TableExtractor` grids (`D04 T02 §8`) to wrap untagged page content-stream operators in `/P << /MCID K >> BDC ... EMC` (and `/Artifact BMC ... EMC` for running headers/footers and decorative table borders) and build a complete `/StructTreeRoot` and `/ParentTree` number tree.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `validate_pdfua` and `convert_to_pdfua` in `src/pdf/compliance.cpp` (establishing the `/StructTreeRoot` and `BDC`/`EMC` `/MCID` auto-tagger also consumed by `§6` when converting untagged inputs to `PDF/A-1a`, `2a`, or `3a`). Done when: `PdfCompliance.PdfUaAutoTagAndValidate` takes an untagged corpus PDF with headings, paragraphs, lists, a table, and a figure, runs `convert_to_pdfua`, and passes `validate_pdfua` with `0` findings under ASan.

- [ ] Implement `ComplianceEngine::validate_pdfua` in `src/pdf/compliance.cpp` checking ISO 14289-1 clauses: `/MarkInfo << /Marked true >>`, `/Root /Lang`, `/ViewerPreferences << /DisplayDocTitle true >>`, `pdfuaid:part = 1` and `dc:title` in XMP, `/StructTreeRoot` + `/ParentTree` integrity, heading nesting order (`H1 -> H2 -> H3` without skipped levels), `/Table` `/TH` headers, `/Figure` `/Alt` attributes, and `/Page /Tabs /S`. Done when: `PdfCompliance.ValidatePdfUa` flags untagged content, missing `/Lang`, missing `/DisplayDocTitle`, and `/Figure` nodes lacking `/Alt`.
- [ ] Rewrite untagged page `/Contents` streams during `convert_to_pdfua`: wrap each reading-order text block, heading, list item, table cell, and image `Do` operator in `<tag> << /MCID <k> >> BDC ... EMC` and wrap running headers/footers and background ruling paths in `/Artifact BMC ... EMC`. Done when: every visible content operator on the page is either bound to an `/MCID` in `/StructTreeRoot` or marked as `/Artifact`. Cheaper substitute: wrapping an entire page in a single `<P>` tag with no heading, list, table, or artifact differentiation.
- [ ] Build the `/StructTreeRoot` dictionary (`/Type /StructTreeRoot`, `/K` root `/Document` element, `/ParentTree` number tree mapping `(StructParents, MCID) -> StructElem` indirect reference, and `/RoleMap`) with semantic `/H1`..`/H3`, `/P`, `/L` -> `/LI` -> `/Lbl` + `/LBody`, `/Table` -> `/TR` -> `/TH`/`/TD`, and `/Figure` (populated with `/Alt` text from caption inference or `"Figure on page N"` fallback). Done when: `PdfCompliance.StructTreeHierarchy` verifies bidirectional `/P` parent and `/K` child links and `/ParentTree` lookups across a multi-page document.
- [ ] Set `/MarkInfo << /Marked true >>`, `/Root /Lang (en-US)` (or detected OCR/document language), `/ViewerPreferences << /DisplayDocTitle true >>`, `/Tabs /S` on each `/Page`, and `pdfuaid:part` (`1`) in the XMP packet. Done when: `validate_pdfua` returns `0` findings on the auto-tagged output PDF.
- [ ] Expose the reusable `auto_tag_structure_tree(pdf::Document&)` helper in `src/pdf/compliance.h/.cpp` so `convert_to_pdfa` (`§6`) can tag untagged inputs before embedding fonts and XMP metadata for `PDF/A-1a`, `2a`, and `3a`. Done when: `PdfCompliance.PdfUaAutoTagAndValidate` passes under ASan.

- [ ] Commit: `PDF/UA-1` (`ISO 14289-1`) validator and auto-tagger (`/StructTreeRoot` + `BDC`/`EMC` `/MCID` streams)

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCompliance.*PdfUa*:PdfCompliance.StructTree*` passes under ASan.

## 8. `ZUGFeRD 2.x / Factur-X` (`EN 16931`) hybrid `PDF/A-3` e-invoice engine & `recto-cli`

Implement the **ZUGFeRD 2.x / Factur-X** (`EN 16931`) hybrid electronic invoice reader, validator, and generator in `src/pdf/compliance.cpp` and `src/cli/cmd_einvoice.cpp` (`recto-cli einvoice <inspect|attach|validate>`):
- **Generator (`attach_einvoice_xml`):** Converts the input PDF to `PDF/A-3b` or `PDF/A-3u` (`§6`), embeds the Cross-Industry Invoice (`CII` `factur-x.xml` / `zugferd-invoice.xml`) or `UBL 2.1` XML file in `/Root /Names /EmbeddedFiles` (`/Type /Filespec`, `/F (factur-x.xml)`, `/UF (factur-x.xml)`, `/AFRelationship /Alternative` or `/Data`, `/EF << /F <stream> /UF <stream> >>` with `/Subtype /text#2Fxml` and `/Params << /ModDate ... /Size ... >>`), links the filespec in `/Root /AF [...]`, and injects the mandatory Factur-X / ZUGFeRD XMP extension schema (`xmlns:fx="urn:factur-x:pdfa:CrossIndustryDocument:invoice:1p0#"`, `fx:DocumentType="INVOICE"`, `fx:DocumentFileName="factur-x.xml"`, `fx:Version="1.0"`, `fx:ConformanceLevel="EN 16931"`).
- **Parser & Validator (`inspect_einvoice`):** Detects embedded `factur-x.xml` / `zugferd-invoice.xml` / `xrechnung.xml` streams, validates `PDF/A-3` + `/AF` + `fx:` XMP conformance, and parses UN/CEFACT CII (`rsm:CrossIndustryInvoice`) and OASIS UBL 2.1 (`Invoice`) XML into a typed `InvoiceModel` (invoice number, issue date, due date, currency code, profile level, seller name/VAT/address, buyer name/VAT/address, line items `id`/`description`/`quantity`/`unit_price`/`vat_rate`/`line_total`, tax breakdown, net total, tax total, grand total, payable amount, and IBAN/BIC payment terms).

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `InvoiceModel`, CII (`rsm:CrossIndustryInvoice`) and UBL 2.1 XML parsers, and `attach_einvoice_xml` (`PDF/A-3` + `/EmbeddedFiles` + `/AF` + `fx:` XMP extension schema) in `src/pdf/compliance.cpp`. Done when: `PdfCompliance.ZugferdFacturX` attaches a `factur-x.xml` payload to a PDF invoice, validates `PDF/A-3b` + `/AF` + `fx:` XMP with `0` findings, and parses every seller, buyer, line-item, tax, and total field back into `InvoiceModel` under ASan.
2. Implement `recto-cli einvoice <inspect|attach|validate> <in.pdf> [--xml <factur-x.xml>] [--profile minimum|basic|en16931|extended] [--out <out.pdf>] [--json]` in `src/cli/cmd_einvoice.cpp`. Done when: `Cli.EInvoiceCommand` passes under ASan.

- [ ] Define `EInvoiceProfile` (`Minimum`, `BasicWl`, `Basic`, `En16931Comfort`, `Extended`, `XRechnung`), `InvoiceParty` (`name`, `tax_id`, `street`, `city`, `postal_code`, `country_code`), `InvoiceLineItem` (`id`, `name`, `quantity`, `unit_code`, `unit_price`, `tax_rate_percent`, `line_total`), and `InvoiceModel` (`invoice_number`, `issue_date`, `due_date`, `currency`, `profile`, `seller`, `buyer`, `line_items`, `tax_basis_total`, `tax_total`, `grand_total`, `due_payable_amount`, `payment_iban`, `payment_bic`, `raw_xml`) in `src/pdf/compliance.h`. Done when: `InvoiceModel` serializes cleanly to JSON for CLI and UI consumption.
- [ ] Implement `attach_einvoice_xml(pdf::Document&, span<const uint8_t> xml_bytes, EInvoiceProfile profile, PdfStandardLevel pdfa3_level)` in `src/pdf/compliance.cpp`: convert to `PDF/A-3b` or `PDF/A-3u` (`§6`), create the `/Filespec` object with `/AFRelationship` (`/Alternative` for `EN 16931`/`BASIC`, `/Data` for `MINIMUM`/`BASIC WL`) and `/Subtype /text#2Fxml`, register it in `/Names /EmbeddedFiles /Names` and `/Root /AF`, and write the `pdfaExtension:schemas` + `fx:` XMP properties. Done when: `PdfCompliance.AttachFacturX` produces a hybrid PDF/A-3 invoice with `0` PDF/A-3 or Factur-X schema findings. Cheaper substitute: attaching the XML file to `/EmbeddedFiles` without converting to `PDF/A-3`, setting `/AF`, or writing the `fx:` XMP extension schema.
- [ ] Implement the UN/CEFACT Cross-Industry Invoice (`rsm:CrossIndustryInvoice` / `ram:SupplyChainTradeTransaction`) and OASIS UBL 2.1 (`<Invoice>`) XML parser in `src/pdf/compliance.cpp` (enforcing entity-expansion and size bounds against hostile XML) populating `InvoiceModel` and verifying arithmetic consistency (`tax_basis_total + tax_total == grand_total`). Done when: `PdfCompliance.ParseCiiAndUbl` parses both CII `factur-x.xml` and UBL 2.1 invoice fixtures into identical `InvoiceModel` values.
- [ ] Implement `validate_einvoice(const pdf::Document&)` checking `PDF/A-3` compliance (`§5`), `/Root /AF` presence, `/Filespec /AFRelationship`, `fx:` XMP metadata synchronization with the embedded XML filename and profile, and XML arithmetic validity. Done when: `PdfCompliance.ValidateEInvoice` detects missing `/AF` arrays or mismatched `fx:ConformanceLevel` tags.
- [ ] Implement `recto-cli einvoice <inspect|attach|validate>` in `src/cli/cmd_einvoice.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfCompliance.*FacturX*:PdfCompliance.*Cii*:PdfCompliance.*EInvoice*:Cli.EInvoiceCommand` passes under ASan.
- [ ] Commit: `pdf: add PDF/A (1-3 a/b/u), PDF/UA-1 auto-tagging and ZUGFeRD/Factur-X compliance engine`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCompliance.*:Cli.PdfACommand:Cli.EInvoiceCommand` passes under ASan.

## 9. Viewer `Signatures & Standards` side panel (`SPEC 8.3`) and `PDF/A & E-Invoice` card

Build the visual compliance and e-invoicing surfaces specified in `design/SPEC.md` sections 8.3 and 9.5:
- **Viewer `Signatures & Standards` Side Panel (`SPEC 8.3`, Standards & E-Invoice sections in `src/app/views/panels/signatures_and_standards_panel.h/.cpp`):**
  - Displays detected conformance badges (`PDF/A-1b`, `PDF/A-2u`, `PDF/A-3b`, `PDF/UA-1`, `ZUGFeRD 2.2 / Factur-X`), an interactive `Validate PDF/A & PDF/UA` checklist grouped by category (`Fonts Embedded`, `Colour Spaces & OutputIntent`, `XMP Metadata`, `Transparency Rules`, `Tagged Structure /StructTreeRoot`, `Alt Text`), click-to-jump to offending pages/objects, and a one-click `Convert to PDF/A...` button.
  - When a ZUGFeRD / Factur-X invoice is open, displays a formatted `E-Invoice Summary` card (seller, buyer, invoice number, issue date, line items table, tax breakdown, grand total, payment IBAN) and an `Export Invoice XML...` button.
- **`PDF/A & E-Invoice (ZUGFeRD / Factur-X)` Convert Card (`SPEC 9.5` in `src/app/views/convert/compliance_convert_options_control.h/.cpp`):**
  - Step 3 options for target standard (`PDF/A-1b`, `PDF/A-1a`, `PDF/A-2b`, `PDF/A-2u`, `PDF/A-2a`, `PDF/A-3b`, `PDF/A-3u`, `PDF/A-3a`, `PDF/UA-1`), colour profile (`sRGB`, `CMYK`), auto-tag structure tree toggle, embed missing fonts toggle, flatten transparency toggle, and optional `Attach ZUGFeRD / Factur-X XML` picker with profile selector (`MINIMUM`, `BASIC`, `EN 16931 / COMFORT`, `EXTENDED`).

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.3 (`Signatures & Standards` side panel) and 9.5 (`PDF/A & E-Invoice` Convert card and Step 3 options). Design board: `design/SPEC.md` section 8.3.3 with `design/screenshots/D-Signatures.png` (drawn by `D06 T04 §4`).
**Job:** Users can verify whether a PDF meets archival (`PDF/A`), accessibility (`PDF/UA`), or European e-invoicing (`ZUGFeRD / Factur-X`) standards, inspect structured invoice line items, or convert documents to compliant `PDF/A` / `PDF/UA` / `ZUGFeRD` files. Consumer: the viewer `SidePanelHost` (`D02 T03 §3`) and `ConvertPage` (`D04 T01 §3`-`§4`).
**Treatment:** Live compliance audit checklist and structured e-invoice viewer in the `Signatures & Standards` side panel paired with the `PDF/A & E-Invoice` Convert card. Cheaper substitute that fails the checkpoint: showing only a static XMP string without running clause validation or dumping raw XML instead of rendering structured ZUGFeRD invoice fields.
**Chrome:** consume `SidePanelHost` (`D02 T03 §3`), `ConvertPage` (`D04 T01 §4`), and `recto_tokens.g.h` status badges (`RectoSuccessBrush`, `RectoWarningBrush`, `RectoDangerBrush` paired with check/warning icons per `SPEC 11`).

**Build order:**

1. Implement `StandardsPanelViewModel` and the Standards & E-Invoice sections of `signatures_and_standards_panel.h/.cpp` (`SPEC 8.3`, hosted in `D02 T03 §3` and shared with `D05 T02 §11`'s `Signatures` section). Done when: opening a ZUGFeRD fixture in `AppTests.StandardsAndInvoiceUi` displays its `PDF/A-3b` and `Factur-X EN 16931` badges, seller/buyer/line-item summary table, `Export Invoice XML` action, and all-green clause validation rows.
2. Build `compliance_convert_options_control.h/.cpp` and register the **PDF/A & E-Invoice (ZUGFeRD / Factur-X)** card in `ConverterRegistry` (`D04 T01 §3`). Done when: `AppTests.StandardsAndInvoiceUi` converts a regular PDF into `PDF/A-2u` and `PDF/A-3b` + `factur-x.xml` through `ConversionRunner` and verifies the output in `SignaturesAndStandardsPanel`.

- [ ] Implement `StandardsPanelViewModel` in `src/services/standards_panel_view_model.h/.cpp` running `ComplianceEngine::validate_pdfa`, `validate_pdfua`, and `inspect_einvoice` on the background actor/worker thread and exposing conformance badges, grouped clause rows (`Pass` / `Fail` with count and page link), and `InvoiceModel` fields. Done when: `StandardsPanelViewModel` populates without blocking the UI thread.
- [ ] Build the **Standards & Compliance** section of `signatures_and_standards_panel.h/.cpp` (`SPEC 8.3`) with detected standard badges (`PDF/A-2u`, `PDF/UA-1`, etc.), a target-standard selector to run an on-demand audit against any `PDF/A` or `PDF/UA` level, expandable category checklists (`Fonts`, `Colour & OutputIntent`, `XMP Metadata`, `Transparency`, `Tagged Structure`), click-to-jump to the offending page on `DocumentView`, and a `Convert to PDF/A...` button that navigates to `ConvertPage` with this card selected. Done when: clicking a failed font finding jumps `DocumentView` to the page using that font.
- [ ] Build the **E-Invoice Summary (`ZUGFeRD / Factur-X`)** card inside `SignaturesAndStandardsPanel` displaying Invoice Number, Issue Date, Due Date, Seller (`Name`, `VAT ID`, `Address`), Buyer (`Name`, `VAT ID`, `Address`), a formatted Line Items list (`Description`, `Qty`, `Unit Price`, `VAT %`, `Total`), Net / Tax / Grand Total summary, Payment IBAN, and `Export Invoice XML...` (`IFileSaveDialog`). Done when: opening a Factur-X PDF displays all structured invoice fields and exports the embedded `factur-x.xml` byte-identically. Cheaper substitute that fails the checkpoint: showing a raw unparsed XML text dump instead of structured invoice fields and line items.
- [ ] Build `ComplianceConvertOptionsControl` (`src/app/views/convert/compliance_convert_options_control.h/.cpp`, `SPEC 9.5`) exposing `Target standard` (`PDF/A-2b (Recommended archival)`, `PDF/A-2u (Unicode searchable)`, `PDF/A-2a (Accessible tagged)`, `PDF/A-1b`, `PDF/A-1a`, `PDF/A-3b`, `PDF/A-3u`, `PDF/A-3a`, `PDF/UA-1 (Universal Accessibility)`), `Colour profile` (`sRGB IEC61966-2.1`, `CMYK`), `Auto-tag untagged structure tree` toggle, `Attach ZUGFeRD / Factur-X e-invoice XML` toggle + XML file picker, and `E-Invoice profile` (`MINIMUM`, `BASIC`, `EN 16931 / COMFORT`, `EXTENDED`). Done when: enabling `Attach ZUGFeRD / Factur-X e-invoice XML` automatically selects `PDF/A-3b` / `PDF/A-3u` as required by EN 16931.
- [ ] Register the **PDF/A & E-Invoice** card in `ConverterRegistry` (`D04 T01 §3`) with its SPEC Principle 4 honest capability hint line (`"Embeds missing fonts, attaches ICC colour profiles, and builds tagged structure or hybrid Factur-X / ZUGFeRD e-invoices."`). Done when: `AppTests.StandardsAndInvoiceUi` runs both a `PDF/A-2u` conversion and a `Factur-X` attachment job through `ConversionRunner`.
- [ ] Commit: `app: add Standards and E-Invoice side panel and PDF/A Convert card`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.StandardsAndInvoiceUi` passes.

## 10. `PptxWriter` PresentationML package: one slide per page, text boxes, reading order

Acrobat Pro exports a PDF to a PowerPoint presentation, and Recto matches that with its own writer and no Office install, so the Convert page can offer it beside Word and Excel. `PptxWriter` (`src/core/pptx_writer.h/.cpp`, `recto::core::PptxWriter`) builds an ECMA-376 PresentationML package on the shared ZIP and OpenXML scaffold of `D04 T03 §1`: one slide per PDF page sized to the page, with editable text boxes that keep font family, size, weight, italics, colour and reading order. It consumes `PageText` (`D03 T01 §1`) and the heading and list inference of `D04 T03 §2`, never parses content streams itself, and writes `.pptx` only, so no macro stream can exist in the output (D10). Comparable tools ship presentations with missing text or lost formatting, so this section fixes the text and ordering contract and `§12` enforces that an empty result is never reported as success. Pictures, shapes and tables follow in `§11`.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `PdfToPptxOptions` and emit the fixed package parts through `OpenXmlPackageBuilder` (`D04 T03 §1`): `[Content_Types].xml`, `_rels/.rels`, `ppt/presentation.xml`, `ppt/_rels/presentation.xml.rels`, one `ppt/slideMasters/slideMaster1.xml`, one `ppt/slideLayouts/slideLayout1.xml` with a title placeholder, `ppt/theme/theme1.xml`, `docProps/core.xml` and `docProps/app.xml`. Done when: `PptxWriter.PackageStructure` writes a package whose every part parses as strict XML and whose every relationship target exists, under ASan.
2. Emit one `ppt/slides/slideN.xml` per selected page with text boxes built from `PageText` blocks in reading order. Done when: `PptxWriter.TextBoxesFontsColours` converts a corpus report PDF into slides whose text, font names, sizes and colours match `PageText`, and `PptxWriter.ReadingOrder` proves a two-column page lists the left column before the right in `p:spTree`, under ASan.

- [ ] Define `PptxTextGranularity` (`Block`, `Line`) and `PdfToPptxOptions` (`page_range`, `slide_size_policy`, `text_granularity`, `include_images`, `include_vector_shapes`, `reconstruct_tables`, `auto_ocr_scanned_pages`) in `src/core/pptx_writer.h`. Done when: `src/core/pptx_writer.h` compiles cleanly across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Emit the package parts listed in build step 1 in `src/core/pptx_writer.cpp`, registering each content type and relationship through `OpenXmlPackageBuilder` and writing through `ZipWriter` (`D04 T03 §1`) into an `AtomicFileWriter` temp file (`D04 T02 §1`). Done when: `PptxWriter.PackageStructure` passes and no part is written outside the declared list.
- [ ] Choose one presentation-wide canvas in `ppt/presentation.xml` (`p:sldSz cx cy`, `1 pt = 12700 EMU`, because PresentationML has a single slide size): `PptxSlideSizePolicy` is `LargestPage` (default) or `FirstPage`, every page is applied with `/Rotate` so it is upright, scaled uniformly to fit the canvas and centred, the ECMA-376 `ST_SlideSizeCoordinate` limits are respected by uniform scaling, and the summary reports `mixed_page_sizes`. Done when: `PptxWriter.SlideSizeAndRotation` converts an A4 portrait page followed by an A3 landscape page, a `/Rotate 90` page and a `0.5 in` square page and reads back one canvas, correct `cx` and `cy`, and shape offsets that place each page's content inside its centred area.
- [ ] Emit one `p:sld` per page in page order, honouring `page_range` through `parse_page_range_spec` (`D05 T01 §1`), and add each slide to `p:sldIdLst` with unique ids from `256` and a relationship to `slideLayout1.xml`. Done when: `PptxWriter.SlidePerPage` converts a 12-page PDF to 12 slides and `3-5` to 3 slides.
- [ ] Emit text boxes (`p:sp` with `txBox="1"`, `a:xfrm` from block quads, `a:bodyPr wrap="square"`) with one `a:p` per paragraph from `D04 T03 §2`'s heading and list inference (`a:buChar` or `a:buAutoNum` for lists) and one `a:r` per run carrying `sz` in hundredths of a point, `b`, `i`, `a:solidFill` `a:srgbClr` and `a:latin`, `a:ea` and `a:cs` typefaces chosen by the run's script (subset prefixes such as `ABCDEF+` stripped from names). Done when: `PptxWriter.TextBoxesFontsColours` verifies text, sizes, weights and colours for Latin, CJK and Arabic runs. Cheaper substitute: one text box per page holding all text in a single run and font.
- [ ] Order `p:spTree` children so text boxes appear in `PageText` reading order, keeping the paint-order slots occupied by non-text shapes, and promote the first heading block of a slide to the layout's title placeholder (`p:ph type="title"`). Done when: `PptxWriter.ReadingOrder` verifies two-column order, and a slide with a heading exposes it as the title placeholder.
- [ ] Set `rtl="1"` on right-to-left paragraphs, keep text in logical order, set `rot` on lines whose baseline is rotated, and replace characters illegal in XML 1.0 with `U+FFFD` while counting them in `PptxWriteSummary::lost_characters`. Done when: `PptxWriter.UnicodeAndRotation` converts Arabic, Hebrew and a `45` degree line and reads back logical-order text and the lost-character count.
- [ ] Expose `PptxWriteSummary` (`slides`, `mixed_page_sizes`, `ocr_pages`, `text_flattened_chars`, `text_boxes`, `pictures`, `shapes`, `tables`, `fallback_raster_regions`, `lost_characters`, `blank_pages`) from `convert_pdf_to_pptx` in `src/core/pptx_writer.h`. Done when: `PptxWriter.SummaryCounts` matches a fixture whose element counts are known.
- [ ] Add `PptxWriter.*` fixtures to `scripts/build-corpus.py`, recording them in `tests/corpus/PROVENANCE.md`, and tests in `tests/recto-tests/pptx_writer_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PptxWriter.PackageStructure:PptxWriter.SlideSize*:PptxWriter.SlidePerPage:PptxWriter.Text*:PptxWriter.ReadingOrder:PptxWriter.Unicode*:PptxWriter.SummaryCounts` passes under ASan.
- [ ] Commit: `convert: add native PresentationML PptxWriter with slides, text boxes and reading order`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PptxWriter.PackageStructure:PptxWriter.SlideSize*:PptxWriter.SlidePerPage:PptxWriter.Text*:PptxWriter.ReadingOrder:PptxWriter.Unicode*:PptxWriter.SummaryCounts` passes under ASan with the extracted text compared to `PageText` on the corpus report.

## 11. `PptxWriter` pictures, vector shapes and `a:tbl` tables with raster fallback

Complete the slide content of `PptxWriter` (`src/core/pptx_writer.cpp`): embedded images become `p:pic` parts without re-compression, vector paths become native DrawingML shapes where DrawingML can express them, and tables from `TableExtractor` become `a:tbl` graphic frames. Anything DrawingML cannot express (shadings, tiling patterns, soft masks, non-`Normal` blend modes, non-rectangular clips) is rendered to a PNG and placed as a picture at its bounds instead of being dropped, and the count is reported so the Convert page can be honest about it. This keeps Acrobat Pro's behaviour of preserving appearance and adds the report Acrobat does not give. It uses `ImageExtractor` (`D04 T02 §9`), `TableExtractor` (`D04 T02 §8`), the immutable `DisplayList` (`D01 T02 §1`) and the tile rasterizer (`D01 T02 §6`) in the conversion job's own `pdf::Document`, never the viewer's.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Emit `p:pic` parts for page images through `ImageExtractor` and native shapes and `a:tbl` frames for vector paths and table grids. Done when: `PptxWriter.PicturesShapesTables` converts a corpus PDF holding a JPEG, a PNG with alpha, a rectangle, a bezier outline and a bordered table into one slide with a `p:pic` for each image, matching `a:prstGeom` or `a:custGeom` shapes and an `a:tbl` with the right grid, under ASan.
2. Rasterize unrepresentable objects and cap per-slide shape counts. Done when: `PptxWriter.RasterFallback` converts a page with a shading and a 5000-path map into slides whose rendered output stays within `SSIM >= 0.95` of the PDF render and whose summary counts the fallback regions.

- [ ] Emit `ppt/media/imageN.jpg` from the original `/DCTDecode` bytes and `imageN.png` (with alpha from `/SMask`) for other images, add `p:pic` with `a:blip r:embed`, position, size and `rot` from the image CTM, and write each distinct image object once even when it appears on many slides. Done when: `PptxWriter.PicturesLossless` confirms the embedded JPEG bytes equal the PDF stream bytes and a logo repeated on 20 slides yields one media part. Cheaper substitute: rasterizing every image or the whole page to PNG.
- [ ] Set `descr` on each `p:cNvPr` of a picture from the tagged `/Figure` `/Alt` text when `PageText::struct_nodes` has one, and leave it empty otherwise. Done when: `PptxWriter.PictureAltText` reads back `/Alt` text on a tagged corpus PDF.
- [ ] Emit rectangles as `a:prstGeom prst="rect"`, straight lines as `p:cxnSp` with `prst="line"` and other paths as `a:custGeom` with `a:moveTo`, `a:lnTo`, `a:cubicBezTo` and `a:close`, with fill and stroke colours converted from `DeviceGray`, `DeviceRGB` and `DeviceCMYK` to sRGB, line width, dash pattern and `a:alpha` from `/ca` and `/CA`. Done when: `PptxWriter.VectorShapes` compares fill, stroke and geometry for each path kind against the `DisplayList`.
- [ ] Rasterize shadings, tiling patterns, soft masks, non-`Normal` blend modes and non-rectangular clips as backdrop-aware regions: a region is the object's bounds plus everything painted beneath it within those bounds (so a `Multiply` object over a coloured image is flattened together with that image), regions that overlap or chain merge into one, the result is a `200` DPI PNG placed as `p:pic`, text painted inside a flattened region is flattened with it and counted in `text_flattened_chars`, and each region adds one to `fallback_raster_regions`. Done when: `PptxWriter.BackdropFlatten` converts a `Multiply` object over a coloured image, two overlapping blend groups, a clipped group and a soft-masked image and keeps each render within `SSIM >= 0.95` of the PDF render, and `PptxWriter.RasterFallback` still places a gradient region as a picture and counts it. Cheaper substitute: rasterizing only the dependent object's bounds as an independent PNG composited normally.
- [ ] Bound allocation: when a slide would exceed `2000` shapes, rasterize the remaining vector objects of that slide into one picture region, and cap total decoded image bytes per job at the memory limit `ConversionRunner` sets for a job (`D04 T01 §1`), returning a typed error instead of exhausting memory. Done when: `PptxWriter.ShapeAndMemoryCaps` converts a `50000`-path page without exceeding the cap and refuses a decompression-bomb image with `PdfError`.
- [ ] Emit tables from `TableExtractor` grids as `p:graphicFrame` with `a:tbl`, `a:tblGrid` `a:gridCol w`, `a:tr h`, `a:tc` with `gridSpan`, `rowSpan`, `hMerge` and `vMerge` continuation cells, cell fills, `a:lnL`, `a:lnR`, `a:lnT`, `a:lnB` borders and run formatting, for bordered and borderless tables alike. Done when: `PptxWriter.Tables` verifies a merged-header table keeps its spans and cell text. Cheaper substitute: placing table text as loose text boxes.
- [ ] Keep paint order in `p:spTree` for pictures, shapes and tables, and fill the slots held by text boxes in `PageText` reading order. Done when: `PptxWriter.PaintOrderAndReadingOrder` shows a shape behind text stays behind it and text boxes remain in reading order.
- [ ] Orchestrate OCR when `auto_ocr_scanned_pages` is set: for each page the scanned detector (`D03 T01 §7`) flags, run `OcrPipeline` (`D03 T01 §8`) on the job's own document, feed the OCR `PageText` (words, quads, confidence) through the same text-box path as `§10` so the recognised text becomes editable text boxes positioned over the page picture, keep the scan as the picture beneath, and count the page in `ocr_pages`; with OCR off or unavailable the page keeps only its picture and the summary says so. Done when: `PptxWriter.ScannedPageOcr` converts an image-only fixture and finds editable text boxes whose text matches the fixture's known words at the right positions, and `PptxWriter.ScannedPageNoOcr` finds only the picture and a summary note.
- [ ] Honour `include_images`, `include_vector_shapes` and `reconstruct_tables`: a disabled kind is omitted and its text still appears as text boxes. Done when: `PptxWriter.OptionToggles` produces text-only slides with all three off and no text lost.
- [ ] Commit: `convert: add pictures, vector shapes and tables to PptxWriter with raster fallback`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PptxWriter.Pictures*:PptxWriter.PictureAltText:PptxWriter.VectorShapes:PptxWriter.RasterFallback:PptxWriter.ShapeAndMemoryCaps:PptxWriter.Tables:PptxWriter.PaintOrderAndReadingOrder:PptxWriter.OptionToggles:PptxWriter.ScannedPageOcr:PptxWriter.ScannedPageNoOcr:PptxWriter.BackdropFlatten` passes under ASan.

## 12. `PptxWriter` reopen validation, empty-result refusal and `recto-cli pdf-to-pptx`

Make `PptxWriter` trustworthy: before `AtomicFileWriter::commit()` (`D04 T02 §1`) every presentation is reopened with Recto's own OOXML reader (the `.pptx` path of `OfficeToPdf`, `D04 T04 §2`) and checked against structural and schema rules and against the source `PageText`, so a presentation that is empty, missing text, or unreadable never reaches the destination folder and never shows as complete. This answers the empty and unformatted exports users report elsewhere. The section also adds `recto-cli pdf-to-pptx`.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `validate_pptx_package` in `src/core/pptx_writer.cpp`: reopen the temp file with the `OfficeToPdf` pptx reader, check ECMA-376 structure (content types, relationships, unique shape ids, ascending `p:sldId` ids from `256`, child element order of the `p:sp`, `p:pic` and `a:tbl` sequences we emit) and compare slide text to the source. Done when: `PptxWriter.ReopenValidation` accepts good output and rejects four hand-broken packages (missing relationship target, duplicate shape id, wrong child order, missing slide) with the first violation named, under ASan.
2. Wire refusal and the CLI. Done when: `PptxWriter.EmptyResultRefused` fails a conversion whose slides lost all text with no file written, and `Cli.PdfToPptx` passes under ASan.

- [ ] Compare reopened slide text with the source: for every source page with `PageText` (or OCR) characters, require at least `99%` of its words to appear in order on the matching slide, else return `ErrorCode::ConversionFailed` with its `RefusalReason` (`TextRecall` or `EmptyResult`) and page (`D01 T02 §24`) naming the page. Done when: `PptxWriter.TextRecall` fails a package with one page's text removed and passes the untouched one. **Corrected 2026-10-23:** (`D01 T02 §22`: the scope is `ErrorCode`).
- [ ] Refuse an empty result: if the source has visible content and the presentation has no text box, picture, shape or table, or any content page produced an empty slide, delete the temp file and return `ErrorCode::ConversionFailed` with its `RefusalReason` (`TextRecall` or `EmptyResult`) and page (`D01 T02 §24`) (`"No content could be converted"`). Pages that are blank in the PDF produce blank slides and are counted in `blank_pages`. Done when: `PptxWriter.EmptyResultRefused` covers an injected writer fault, an image-only page with images disabled and a fully blank PDF, which succeeds with `blank_pages` equal to the page count. **Corrected 2026-10-23:** (`D01 T02 §22`: the scope is `ErrorCode`).
- [ ] Guarantee the original and destination are untouched on any failure or `CancelToken` abort (no partial `.pptx`, no leftover temp file). Done when: `PptxWriter.NoPartialOutputOnFailure` checks the destination folder is unchanged after each failure injected by the `D06 T03 §5` fault-injection hook.
- [ ] Pass a password-protected source through `ErrorCode::NeedsPassword` and `WrongPassword` unchanged so the queue can prompt (`D04 T01 §7`). Done when: `PptxWriter.EncryptedSource` returns `NeedsPassword` and writes nothing. **Corrected 2026-10-23:** (`D01 T02 §22`: was `PasswordRequired`, which `error.h` does not have).
- [ ] Implement `recto-cli pdf-to-pptx <in.pdf> --out <out.pptx> [--pages <range>] [--granularity block|line] [--no-images] [--no-shapes] [--no-tables] [--ocr] [--password <pw>] [--json]` in `src/cli/cmd_pdf_to_pptx.cpp`, returning the exit-code contract of `D01 T03 §1` and, with `--json`, the `PptxWriteSummary` fields. Done when: `Cli.PdfToPptx` verifies exit `0` with a valid file and the summary, and a non-zero typed exit for empty, encrypted and missing inputs.
- [ ] Add a libFuzzer target `fuzz_pptx_validate` in `fuzz/fuzz_pptx_validate.cpp` feeding mutated `.pptx` bytes to `validate_pptx_package` under ASan. Done when: a 60 second smoke run (`D01 T03 §5` CI mode) finds no crash, hang or unbounded allocation.
- [ ] Commit: `convert: validate PptxWriter output by reopening and add recto-cli pdf-to-pptx`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PptxWriter.ReopenValidation:PptxWriter.TextRecall:PptxWriter.EmptyResultRefused:PptxWriter.NoPartialOutputOnFailure:PptxWriter.EncryptedSource:Cli.PdfToPptx` passes under ASan.

## 13. Convert page **PowerPoint presentation (`.pptx`)** card and Step 3 UI (`SPEC 9.5`)

Add the **PowerPoint presentation (`.pptx`)** card to the Convert page beside **Word document** and **Excel & Data**, matching Acrobat Pro's PowerPoint export. The card is a separate section from the writer so the writer ships and is tested first. It registers in `ConverterRegistry` (`D04 T01 §3`), builds its Step 3 options control, runs through `ConversionRunner`, and tells the user plainly how many regions were kept as pictures. It must not hide a failure: a refused or empty conversion shows its typed reason in the queue card, never a completed job.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new card on `design/SPEC.md` section 9.5 (the card list does not name PowerPoint yet); it follows the **Word document** card pattern of `D04 T03 §5` (`design/screenshots/D-Convert.png`, `explorations/B-Convert.png`), and this section adds the card to the SPEC 9.5 list.
**Job:** The user can turn a PDF into an editable PowerPoint presentation from the Convert page and see exactly what was kept as editable and what was kept as pictures. Consumer: `ConvertPage` (`D04 T01 §3`-`§4`), `ConversionRunner` and `PptxWriter` (`§10`-`§12`).
**Treatment:** A dedicated `PptxConvertOptionsControl` bound to `PptxConvertOptionsViewModel` with the honest capability hint and a result summary in the completion toast. Cheaper substitute that fails the checkpoint: reusing the Word card with a `.pptx` extension choice, or a card with no options and no summary of pictures kept.
**Chrome:** consume the Convert Step 2 card and Step 3 options host from `D04 T01 §3`-`§4`, the completion notice of `D04 T01 §6`, `CommandRegistry` (`D02 T03 §11`) and `recto_tokens.g.h` tokens.

**Build order:**

1. Implement `PptxConvertOptionsViewModel` and `pptx_convert_options_control.h/.cpp`, register the card in `ConverterRegistry`, and enqueue `convert_pdf_to_pptx` through `ConversionRunner`. Done when: `AppTests.ConvertPptxUi` selects the card, runs a job, opens the completion notice and reads the slide count from the output.
2. Register the `Ctrl+K` command and SPEC entry. Done when: `Ctrl+K` "powerpoint" selects the card with the loaded file, and `design/SPEC.md` section 9.5 lists the card.

- [ ] Implement `PptxConvertOptionsViewModel` in `src/services/pptx_convert_options_view_model.h/.cpp` exposing `TextGranularity` (`Block`), `IncludeImages` (`true`), `IncludeVectorShapes` (`true`), `ReconstructTables` (`true`) and `AutoOcrScannedPages` (`true`) mapped to `PdfToPptxOptions`. Done when: each property round-trips into `PdfToPptxOptions` in `AppTests.ConvertPptxUi`.
- [ ] Build `PptxConvertOptionsControl` (`src/app/views/convert/pptx_convert_options_control.h/.cpp`) with labelled toggles, a `Text boxes` `ui::SegmentedControl` (`One per paragraph block` or `One per line`), tooltips and automation names (`D03 T02 §3`). Done when: the control passes the accessibility inventory. Cheaper substitute: unlabelled toggles or no granularity choice.
- [ ] Show the SPEC Principle 4 hint line `"PDF to PowerPoint makes one slide per page with editable text, pictures, shapes and tables; effects PowerPoint cannot draw are kept as pictures."` in Step 3. Done when: the line is visible and read by Narrator.
- [ ] Register the **PowerPoint presentation (`.pptx`)** card in `ConverterRegistry` with primary button `"Convert to PowerPoint (.pptx)"`, enabled for PDF inputs only. Done when: selecting a non-PDF input disables the card with a stated reason.
- [ ] Show the completion toast and `QueueCard` summary from `PptxWriteSummary` (`"12 slides, 3 regions kept as pictures"`), and show the typed failure reason of `§12` when the job is refused. Done when: `AppTests.ConvertPptxUi` shows both the summary and the empty-result refusal text, and no job reaches `Completed` after a refusal.
- [ ] Register the `convert.toPowerPoint` command in `CommandRegistry` (`D02 T03 §11`) with synonyms `pptx`, `slides`, `presentation`. Done when: `Ctrl+K` "slides" finds it. Cheaper substitute: a card with no command-box entry.
- [ ] Add the card to the Step 2 list of `design/SPEC.md` section 9.5. Done when: the SPEC list names the card and `AppTests.ConvertPptxUi` matches it.
- [ ] Add `AppTests.ConvertPptxUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=AppTests.ConvertPptxUi` passes.
- [ ] Commit: `convert: add the PowerPoint presentation card and Step 3 options`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.ConvertPptxUi` passes on the display-session host.

## 14. `StructTreeEditor` engine: add, retag, move, delete tags, edit `/Alt` and `/ActualText`

Acrobat Pro's Tags panel lets a person repair a tagged PDF by hand, and `§7` can only validate and auto-tag. `StructTreeEditor` (`src/pdf/struct_tree_editor.h/.cpp`, `recto::pdf::StructTreeEditor`) adds the manual verbs: add an element, retag it, move or reorder it, delete it, and edit its `/Alt` and `/ActualText`, on an isolated working copy of the document (never the viewer's `pdf::Document`, and saved as a full rewrite so deleted elements leave no trace). Its one job beyond the verbs is consistency: after every operation `/K`, `/P`, `/Pg`, marked-content references, object references, `/StructParents`, `/StructParent` and the `/ParentTree` number tree agree, or the whole operation is rolled back with a typed error. Single edits can break trees in two known ways: a decorative-marking tool that ignores named `BDC` property lists, and an edit that removes a `BDC` with its `/MCID` so the new text is untagged and its element points at nothing. Recto keeps `§7`'s validator as the arbiter and adds no new concept of structure.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `StructNodeId` (the element's object number), `StructEditOp` and `StructTreeEditor::open(pdf::Document& working_copy)` with iterative tree walking, a cycle set and node and depth caps, and implement `add_element`, `retag`, `move`, `delete`, `set_alt` and `set_actual_text`. Done when: `StructTreeEditor.Operations` applies each verb to a tagged corpus PDF and `check_integrity` passes after each, under ASan.
2. Implement `check_integrity`, rollback, undo and redo, commit through `PdfWriter` and `recto-cli tags`. Done when: `StructTreeEditor.RollbackOnFailure`, `StructTreeEditor.UndoRedo` and `Cli.TagsCommand` pass under ASan.

- [ ] Define `StructNodeId`, `StructEditOp`, `DeletePolicy` (`PromoteChildren`, `DeleteSubtreeMarkContentAsArtifact`, `DeleteSubtreeLeaveContentUntagged`) and `StructTreeEditor` in `src/pdf/struct_tree_editor.h`, with every function returning `PdfResult<T>`. Done when: the header compiles cleanly across `recto-pdf`, `recto-core`, `recto-services` and `recto-cli`.
- [ ] Load the tree iteratively with a visited set, a depth cap of `256` and a node cap of `1000000`, and refuse a malformed tree (cycle, `/P` mismatch, dangling `/Pg`, `/ParentTree` hole) with `ErrorCode::DamagedFormat` naming the first violation instead of guessing a repair. Done when: `StructTreeEditor.MalformedTreeRefused` refuses a cyclic and a dangling fixture without crash or allocation growth. **Corrected 2026-10-23:** (`D01 T02 §22`: was `StructTreeMalformed`, which `error.h` does not have).
- [ ] Implement `add_element(parent, index, role, attributes)` and `retag(node, role)` accepting the standard structure types and custom roles that resolve through `/RoleMap` to a standard type, refusing a role-map cycle and an unmapped custom role. Done when: `StructTreeEditor.AddAndRetag` adds a `/Sect` and retags a `/P` to `/H2`, and a cyclic `/RoleMap` is refused.
- [ ] Implement `move(node, new_parent, new_index)` for reorder and reparent, refusing a move into the node's own subtree, and keep `/P` back-pointers current. Done when: `StructTreeEditor.MoveAndReorder` reorders siblings and reparents a subtree and `check_integrity` passes.
- [ ] Implement `delete(node, DeletePolicy)`: `PromoteChildren` re-parents kids to the deleted node's parent at its position, `DeleteSubtreeMarkContentAsArtifact` rewrites the subtree's marked-content sequences to `/Artifact` (handling both inline `/P << /MCID n >> BDC` and named property lists resolved through `/Resources /Properties`, and removing the matching `/ParentTree` entries), and `DeleteSubtreeLeaveContentUntagged` unwraps them. Done when: `StructTreeEditor.DeletePolicies` covers all three policies including a named `/MC0 BDC` property list and no `/ParentTree` entry or `/MCID` is left dangling. Cheaper substitute: removing the element dictionary and leaving `/ParentTree` and the `BDC` behind.
- [ ] Implement `set_alt` and `set_actual_text` on the element, and when the element's own marked content carries `/ActualText` in its property list, update that too so `PageText` and the tree agree; never remove a `BDC` or its `/MCID` when only text changes. Done when: `StructTreeEditor.AltAndActualText` edits both, extracts the new text through `PageText`, and finds the `BDC` and `/MCID` unchanged.
- [ ] Implement `check_integrity` returning `ComplianceFinding`s (`§5`, category `TaggedStructure`) for `/K`, `/P`, `/Pg`, `MCR`, `OBJR`, `/StructParents`, `/StructParent`, `/ParentTree` and `/ParentTreeNextKey` consistency, run it after every operation, and roll the whole operation back on any finding. Done when: `StructTreeEditor.RollbackOnFailure` injects a fault mid-operation and the tree equals its pre-operation state.
- [ ] Record each operation as an invertible command that stores only the changed objects (before and after), so `undo()` and `redo()` restore the exact prior object state, bounded to the last `200` operations and to a retained-history budget of `64 MiB`: the oldest entries are evicted first, and an operation whose own inverse exceeds the budget is refused with `ErrorCode::ResourceLimit` unless the caller passes `allow_irreversible`, which applies it and clears the history. Done when: `StructTreeEditor.UndoRedo` applies ten mixed operations, undoes all of them and equals the original byte for byte after commit, and `StructTreeEditor.UndoBudget` deletes and restores a document-sized subtree repeatedly and stays under the budget with the refusal and the irreversible path both exercised. **Corrected 2026-10-23:** (`D01 T02 §22`: was `UndoBudgetExceeded`, which `error.h` does not have).
- [ ] Implement `commit(dst_path, CancelToken)` writing the working copy through `PdfWriter` (`D04 T02 §2`) as a full rewrite and `AtomicFileWriter` (`D04 T02 §1`), then reopen it and rerun `check_integrity` before reporting success. Done when: `StructTreeEditor.CommitFullRewrite` finds no deleted element text or `/Alt` string in the raw output bytes.
- [ ] Implement `recto-cli tags <list|add|retag|move|delete|set-alt|set-actual-text|check> <in.pdf> [--node <obj>] [--parent <obj>] [--index <n>] [--role <type>] [--policy promote|artifact|untag] [--text <str>] [--out <out.pdf>] [--json]` in `src/cli/cmd_tags.cpp`. Done when: `Cli.TagsCommand` verifies each verb, exit codes and `--json` tree output.
- [ ] Add a libFuzzer target `fuzz_struct_edit` in `fuzz/fuzz_struct_edit.cpp` applying random operation sequences to corpus tagged PDFs and asserting `check_integrity` after each. Done when: a 60 second smoke run (`D01 T03 §5` CI mode) finds no crash, hang, unbounded allocation or integrity failure.
- [ ] Add tagged and hostile fixtures to `scripts/build-corpus.py` (recorded in `tests/corpus/PROVENANCE.md`) and the `StructTreeEditor.*` tests in `tests/recto-tests/struct_tree_editor_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=StructTreeEditor.*:Cli.TagsCommand` passes under ASan.
- [ ] Commit: `pdf: add StructTreeEditor with consistent tag add, retag, move, delete and alt text edits`

**Test checkpoint:** `recto-tests.exe --gtest_filter=StructTreeEditor.*:Cli.TagsCommand` passes under ASan.

## 15. `ReadingOrderEditor` engine: content regions, roles, artifacts and reorder

Acrobat Pro's Reading Order tool lets a person draw a box over page content, say what it is (heading, paragraph, figure, table, artifact) and fix the order screen readers follow. `ReadingOrderEditor` (`src/pdf/reading_order.h/.cpp`, `recto::pdf::ReadingOrderEditor`) is the engine for that: it turns page areas into `ContentRegion`s, binds the content under a region to a new structure element by wrapping the right painting operators in `BDC`/`EMC` with a fresh `/MCID`, marks decorative content as `/Artifact`, and reorders regions, all through `StructTreeEditor` (`§14`) so the tree stays consistent. It works on an isolated working copy, uses the single content-stream interpreter that every consumer shares (`D06 T03 §4`) to know which operators paint where, resolves named property lists as well as inline ones, and never discards an existing `/ActualText` or `/MCID`. It builds on the `PDF/UA-1` validator and auto-tagger of `§7`, which remain the arbiter of whether the result is accessible.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `ContentRegion`, `detect_regions` and the interpreter-backed `attribute_operators` that gives each painting operator (text show, `Do`, path paint, shading, inline image) a page-space bounding box and byte span in the concatenated `/Contents`. Done when: `ReadingOrder.DetectAndAttribute` finds the heading, paragraph, table and figure regions of a corpus page and attributes every operator under ASan.
2. Implement `tag_region`, `mark_artifact`, `untag_region` and `reorder_regions`, plus `recto-cli reading-order`. Done when: `ReadingOrder.TagArtifactReorder` and `Cli.ReadingOrderCommand` pass under ASan.

- [ ] Define `RegionRole` (`Paragraph`, `Heading1` to `Heading6`, `Figure`, `Table`, `List`, `FormField`, `Formula`, `Artifact`), `ArtifactKind` (`Header`, `Footer`, `Background`, `Other`), `ContentRegion` (`region_id`, `page_index`, `rect_user_space`, `role`, `node`, `order`) and `ReadingOrderEditor` in `src/pdf/reading_order.h`. Done when: the header compiles cleanly across `recto-pdf`, `recto-core`, `recto-services` and `recto-cli`.
- [ ] Implement `detect_regions(page)` from existing tags (marked-content bounds per element) when the page is tagged, else from `PageText` blocks (`D03 T01 §1`), `TableExtractor` grids and image `Do` bounds, in reading order. Done when: `ReadingOrder.DetectAndAttribute` returns the expected regions for a tagged and an untagged page.
- [ ] Implement `attribute_operators(page)` through the shared interpreter, including operators inside Form XObjects (attributed at the enclosing `Do`), with a recursion cap of `32` and a per-page operator cap, returning a typed `Truncated` result at the cap. Done when: `ReadingOrder.NestedFormsAndCaps` attributes a nested-form page and reports truncation on a hostile page.
- [ ] Implement `tag_region(region_rect, role, parent, index)`: select the operators whose bounds fall inside the rect, expand to the enclosing balanced `q`/`Q` or `BT`/`ET` group so no pair is split, report `expanded_beyond_region`, wrap the span in `<tag> << /MCID n >> BDC ... EMC`, create the element and `/ParentTree` entry through `StructTreeEditor`, and refuse when the span is already inside tagged marked content unless `retag_existing` is set. Done when: `ReadingOrder.TagRegion` tags a paragraph, a figure with `/Alt` and a table, and `validate_pdfua` plus `check_integrity` pass on the output.
- [ ] Implement `mark_artifact(region_rect, ArtifactKind)` wrapping the span in an `/Artifact` marked-content sequence and removing any `/MCID` binding and `/ParentTree` entry it replaces, and `untag_region` as its inverse, resolving named `/Properties` entries rather than assuming inline dictionaries. Done when: `ReadingOrder.ArtifactNamedProperties` marks and unmarks content wrapped by `/MC0 BDC` and leaves no dangling `/MCID`.
- [ ] Preserve `/ActualText` and `/MCID` of existing marked content when regions are retagged, split or merged. Done when: `ReadingOrder.PreservesActualText` retags a region whose `BDC` carries `/ActualText` and finds both kept.
- [ ] Implement `reorder_regions(page, ordered_region_ids)`: regions sharing a parent reorder in place through `StructTreeEditor::move`, and regions with different parents are first moved under their lowest common ancestor in the requested order. Done when: `ReadingOrder.Reorder` reorders siblings and cross-parent regions and the `PageText` tagged reading order follows.
- [ ] Implement `recto-cli reading-order <list|tag|artifact|untag|reorder> <in.pdf> --page <n> [--rect x0,y0,x1,y1] [--role <role>] [--kind header|footer|background|other] [--order <ids>] [--out <out.pdf>] [--json]` in `src/cli/cmd_reading_order.cpp`. Done when: `Cli.ReadingOrderCommand` verifies each verb and exit code.
- [ ] Extend `fuzz/fuzz_struct_edit.cpp` (`§14`) with region operations on hostile content streams. Done when: a 60 second smoke run finds no crash, hang or integrity failure.
- [ ] Add `ReadingOrder.*` tests in `tests/recto-tests/reading_order_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ReadingOrder.*:Cli.ReadingOrderCommand` passes under ASan.
- [ ] Commit: `pdf: add ReadingOrderEditor for content regions, roles, artifacts and reorder`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ReadingOrder.*:Cli.ReadingOrderCommand` passes under ASan.

## 16. Viewer **Tags** tree editor panel in `Signatures & Standards` (`SPEC 8.3`)

Build the Tags panel that Acrobat Pro users know, as the structure tag inspector that `design/SPEC.md` section 8.3 already places in the **Standards & Accessibility** tab of the `Signatures & Standards` panel (`§9` here): a tree of the document's tags with an editor for the selected tag, driven by `StructTreeEditor` (`§14`). It edits a working copy, never the viewer's document, and applies by saving a copy and reloading the tab (the pattern of `D03 T01 §9`). The `PDF/UA` findings of `§7` re-run after each change so the person sees whether the repair worked.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 8.3, `Signatures & Standards` panel, *Standards & Accessibility tab* (the `Structure Tag Tree (/StructTreeRoot)` inspector); no screenshot exists, so it follows the panel chrome and the checklist rows built in `§9`. Design board: `design/SPEC.md` section 8.3.3 with `design/screenshots/D-Signatures.png` (drawn by `D06 T04 §4`).
**Job:** An accessibility remediator can see the tag tree, select a tag to see its content highlighted on the page, add, retag, reorder and delete tags, edit Alt text and actual text, undo, and save a corrected copy, using only the keyboard if needed. Consumer: `SidePanelHost` (`D02 T03 §3`), `StructTreeEditor` (`§14`), `ReadingOrderEditor` bounds (`§15`) and the save pipeline (`D02 T01 §8`).
**Treatment:** A `ui::TreeView` of tags (lazy children loaded off the UI thread) beside a details pane with `Role`, `Alt text`, `Actual text` and `Language`, a `Show content` highlight, `Add tag`, `Delete tag` (asking for the `DeletePolicy`), `Move up`, `Move down`, `Indent` and `Outdent` buttons mirrored by `Alt+Up`, `Alt+Down`, `Alt+Right` and `Alt+Left`, and a findings strip from `validate_pdfua`. Cheaper substitute that fails the checkpoint: a read-only tree, an Alt-text-only editor, or reorder by drag-and-drop with no keyboard route.
**Chrome:** consume `SignaturesAndStandardsPanel` (`§9`), `SidePanelHost` (`D02 T03 §3`), the `ui::Dialog` host (`D02 T01 §10`), `CommandRegistry` (`D02 T03 §11`) and `recto_tokens.g.h` tokens with Fluent UI System Icons (`RectoIcons` via `ui::Icon`); no second panel host.

**Build order:**

1. Implement `TagsPanelViewModel` in `src/services/tags_panel_view_model.h/.cpp` opening a `TagEditSession` (an isolated working copy and a `StructTreeEditor`) on a worker thread, exposing lazy children, selection, edit commands, undo and redo, and the findings. Done when: `TagsPanel.ViewModel` loads a 20000-element tree without blocking the UI thread and applies each verb.
2. Build the Tags section of the panel and wire save and reload. Done when: `AppTests.TagsPanelUi` adds, retags, reorders and deletes tags by pointer and by keyboard, edits Alt text, saves a copy, and shows the findings drop.

- [ ] Implement `TagEditSession` in `src/services/tag_edit_session.h/.cpp` that copies the document to a scratch file, opens a private `pdf::Document` and `StructTreeEditor` on a job thread, and discards the scratch copy on close. Done when: `TagsPanel.SessionIsolation` shows the viewer's `DocumentActor` and source file unchanged after edits, failure and cancel.
- [ ] Build the `ui::TreeView` in `src/app/views/panels/tags_section.h/.cpp` with lazy expansion, a role glyph plus a text role name (never colour alone, `SPEC 11`), and the element's page number. Done when: expanding a node of a `50000`-element tree stays responsive and every row has an automation name.
- [ ] Build the details pane (`Role` combo of standard types plus mapped custom roles, `Alt text`, `Actual text`, `Language`) applying edits through the view model with one undo step per edit. Done when: editing Alt text then pressing `Ctrl+Z` restores the prior text.
- [ ] Implement `Show content`: ask `ReadingOrderEditor` (`§15`) for the selected element's page rectangles and draw them on the canvas overlay, jumping to its first page. Done when: selecting a figure tag highlights the figure and scrolls to it.
- [ ] Implement `Add tag`, `Delete tag` (a dialog offering the three `DeletePolicy` choices with the effect of each in plain words), `Move up`, `Move down`, `Indent` and `Outdent` with the `Alt` arrow shortcuts. Done when: `AppTests.TagsPanelUi` performs each action by button and by keyboard and `check_integrity` passes. Cheaper substitute: drag-and-drop only.
- [ ] Re-run `validate_pdfua` (`§7`) on the working copy after each change and show the findings count and the first findings in a strip, each opening the offending tag. Done when: fixing a missing `/Alt` removes its finding live.
- [ ] Save through `Save a copy` and `Replace original` (`D02 T01 §8`), then reload the tab onto the saved copy; closing with unsaved tag edits shows the Unsaved-Changes dialog. Done when: `AppTests.TagsPanelUi` saves, reloads and finds the tag edits in the reopened file, and cancelling leaves the original byte-identical.
- [ ] Register `accessibility.tags.show`, `accessibility.tags.add`, `accessibility.tags.retag`, `accessibility.tags.delete` and `accessibility.tags.editAlt` in `CommandRegistry` with synonyms `tags`, `alt text`, `accessibility`. Done when: `Ctrl+K` "alt text" opens the panel on the selected tag.
- [ ] Add `TagsPanel.*` and `AppTests.TagsPanelUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=TagsPanel.*:AppTests.TagsPanelUi` passes.
- [ ] Commit: `app: add the Tags tree editor panel with undoable tag edits`

**Test checkpoint:** `recto-tests.exe --gtest_filter=TagsPanel.*:AppTests.TagsPanelUi` passes on the display-session host.

## 17. Viewer **Reading Order** tool: draw regions, assign roles, reorder, mark artifacts

Build the Reading Order tool that matches Acrobat Pro: a contextual mode bar that shows the page's content regions as numbered boxes, lets the person draw a new region, choose its role (heading, paragraph, figure, table, artifact), reorder regions and mark decorative content, driven by `ReadingOrderEditor` (`§15`) and shown beside the Tags panel (`§16`). Drawing a box is not the only route: a keyboard and screen-reader user can tag the current text selection or step through detected regions. Edits happen on the same `TagEditSession` working copy as the Tags panel and save the same way.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows the contextual mode bar of `design/SPEC.md` section 8.2 (same `RectoRaisedBrush` strip, `OverlayCornerRadius` token of 8 epx, docked above the floating toolbar in Focus or below the top command bar in Classic and Reading room, `Esc` closes) and draws its overlay the way the Redaction bar does (`RedactionCanvasOverlay`, `D05 T02 §9`). Design board: `design/SPEC.md` section 8.3.3 with `design/screenshots/D-Signatures.png` (drawn by `D06 T04 §4`).
**Job:** An accessibility remediator can see the reading order of the page, draw or select a region, set its role, move it earlier or later, mark headers, footers and backgrounds as artifacts, undo, and save a corrected copy, by pointer or by keyboard. Consumer: `ReadingOrderEditor` (`§15`), `TagEditSession` (`§16`) and the save pipeline (`D02 T01 §8`).
**Treatment:** A `Reading Order` bar with a role picker, `Draw region`, `Tag selected text`, `Next untagged block`, `Auto-detect regions`, `Move earlier`, `Move later`, `Mark as artifact` and `Undo` / `Redo`, plus numbered region badges on the canvas that show the number as text and a role glyph. Cheaper substitute that fails the checkpoint: draw-only with no keyboard route, regions coloured without numbers, or tagging that wraps the whole page in one element.
**Chrome:** consume the contextual mode bar pattern and canvas overlay of `D05 T02 §9`, `SidePanelHost` and the Tags section (`§16`), the text `SelectionModel` (`D03 T01 §3`), `CommandRegistry` (`D02 T03 §11`) and `recto_tokens.g.h` tokens with Fluent UI System Icons (`RectoIcons` via `ui::Icon`).

**Build order:**

1. Implement `ReadingOrderViewModel` in `src/services/reading_order_view_model.h/.cpp` over the shared `TagEditSession`, with regions per page, the draw, select and keyboard flows, and undo. Done when: `ReadingOrderUi.ViewModel` draws a region, tags it, reorders it and undoes each step.
2. Build `reading_order_bar.h/.cpp` and `ReadingOrderCanvasOverlay` and register the commands. Done when: `AppTests.ReadingOrderUi` completes the task by pointer and by keyboard only, then saves a copy that passes `validate_pdfua` for the page.

- [ ] Build `ReadingOrderBar` (`src/app/controls/reading_order_bar.h/.cpp`, `48 epx` strip) with the role picker, `Draw region`, `Tag selected text`, `Next untagged block`, `Auto-detect regions`, `Move earlier`, `Move later`, `Mark as artifact`, `Undo` and `Redo`, every icon-only button with an automation name and tooltip. Done when: the bar passes the accessibility inventory (`D03 T02 §3`).
- [ ] Build `ReadingOrderCanvasOverlay` drawing region boxes with the order number as text and a role glyph, selected-region handles for resize, and a distinct artifact style, mapped through the page-to-viewport transform. Done when: the overlay stays aligned at zoom `25%` to `400%` and on rotated pages.
- [ ] Implement `Draw region`: a rubber band creates a `ContentRegion`, shows the true extent returned by `tag_region` (including `expanded_beyond_region`) before applying, and asks the role. Done when: drawing over a heading and choosing `Heading 1` tags it and shows the expanded extent when it differs.
- [ ] Implement the keyboard route: `Tag selected text` tags the current `SelectionModel` selection (`D03 T01 §3`), `Next untagged block` and `Previous untagged block` cycle detected regions, and `Alt+Up` / `Alt+Down` reorder the selected region. Done when: `AppTests.ReadingOrderUi` tags and reorders using only the keyboard.
- [ ] Implement `Mark as artifact` (header, footer, background, other) and `Auto-detect regions`, with the numbering updating live after reorder. Done when: marking a running header removes its number and its `/MCID` binding.
- [ ] Share `TagEditSession` with the Tags panel so a change in either appears in both, and save through `D02 T01 §8` with the Unsaved-Changes guard. Done when: tagging a region shows its element selected in the Tags tree, and saving reloads the tab with the edits.
- [ ] Register `accessibility.readingOrder.toggle`, `accessibility.readingOrder.autoDetect` and `accessibility.readingOrder.markArtifact` in `CommandRegistry` and add `Reading order` to the toolbar `More` menu, with synonyms `reading order`, `artifact`, `tag order`. Done when: `Ctrl+K` "reading order" opens the bar and `Esc` closes it.
- [ ] Add `ReadingOrderUi.*` and `AppTests.ReadingOrderUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=ReadingOrderUi.*:AppTests.ReadingOrderUi` passes.
- [ ] Commit: `app: add the Reading Order tool with regions, roles, artifacts and keyboard route`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ReadingOrderUi.*:AppTests.ReadingOrderUi` passes on the display-session host.

## 18. `MathRegionDetector` engine: math zones, symbols and expression trees

Foxit PDF Editor recognises equations in a PDF and exports them as MathML or LaTeX; Acrobat Pro has no equation recognition (its Tags panel only lets a person mark a `Formula` element and type Alt text). `MathRegionDetector` (`src/core/math_detector.h/.cpp`, `recto::core::MathRegionDetector`) finds math on a page and builds an expression tree for each region, so `§19` can serialize it and `§20` can tag it. It reads existing `/Formula` tags first and falls back to geometry. It never invents math: a region whose layout cannot be parsed into a complete tree is reported as `Uncertain` with its unparsed fragments, and exporters keep the original text for it. It runs on the conversion job's own `pdf::Document` off the UI thread, consumes the immutable `PageText` and `DisplayList` snapshots, bounds recursion and allocation against hostile pages, and works fully offline.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `MathNode`, `MathRegion` and `MathDetectOptions` in `src/core/math_detector.h`, and implement region detection (existing `/Formula` elements, math fonts and symbols, display and inline zones). Done when: `MathDetect.Regions` finds every planted display and inline region in the `math` fixtures and returns `0` regions on the prose fixture, under ASan.
2. Implement the layout parser that turns a region into a `MathNode` tree and the confidence rule. Done when: `MathDetect.Trees` reproduces the planted expression tree of each fixture and `MathDetect.UncertainNeverGuesses` returns `Uncertain` with fragments for a deliberately overlapping layout, under ASan.

- [ ] Define `MathNodeKind` (`Row`, `Identifier`, `Number`, `Operator`, `Text`, `Fraction`, `Root`, `Superscript`, `Subscript`, `SubSuperscript`, `UnderOver`, `Fenced`, `Table`, `TableRow`, `TableCell`), `MathNode` (`kind`, `text`, `children`, `stretchy`, `bbox_pt`), `MathRegion` (`page_index`, `bbox_pt`, `display` of `Block` or `Inline`, `source` of `FormulaTag`, `Heuristic`, `confidence` `0..1`, `status` of `Parsed` or `Uncertain`, `tree`, `unparsed_fragments`, `equation_label`) and `MathDetectOptions` (`min_confidence` default `0.6`, `detect_inline` default `true`, `pages`) in `src/core/math_detector.h`. Done when: the header compiles cleanly across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Read existing `/Formula` structure elements through the read-only listing of `StructTreeEditor` (`§14`) with its depth and node caps, take their page rectangles and `/ActualText` or `/Alt`, and mark the region `source = FormulaTag`. Done when: `MathDetect.ExistingFormulaTags` returns the region and the tag text for a tagged fixture and ignores a cyclic tree without crash.
- [ ] Detect math zones on untagged pages from `PageText` spans (`D03 T01 §1`): a span is a math signal when its font family matches the editable table `src/core/resources/math_fonts.json` (Cambria Math, STIX, Latin Modern Math, the TeX `CMMI`, `CMSY`, `CMEX` and `CMR` families, Symbol, MT Extra), or its characters fall in Unicode Mathematical Operators `U+2200` to `U+22FF`, Supplemental Mathematical Operators `U+2A00` to `U+2AFF` or Mathematical Alphanumeric Symbols `U+1D400` to `U+1D7FF`. A line is a display zone when it is centred, alone on its band, and carries at least `2` signals; an inline zone is a run of at least `2` signals joined by adjacent single italic letters inside a prose line. A single Greek letter or unit symbol in prose (`5 µm`, `α-helix`) is not a zone. Done when: `MathDetect.ProseNotMath` returns `0` regions on a prose page full of Greek letters and units and `MathDetect.Regions` finds every planted zone.
- [ ] Read geometry from the `DisplayList` (`D01 T02 §1`) for the structures text cannot show: a horizontal rule whose x-range overlaps text above and below is a fraction bar, a `U+221A` glyph with an overline path is a radical, tall delimiter pieces from the extension fonts group into one stretchy fence, and large operators (`U+2211`, `U+220F`, `U+222B`) take limits placed above, below or beside them. Done when: `MathDetect.Geometry` recognises a fraction bar, a radical with an index, a stretched parenthesis and a summation with limits.
- [ ] Parse each zone into a `MathNode` tree by baseline analysis with a recursion depth cap of `32` and a node cap of `20000` per region: stacked groups around a bar become `Fraction`, size-reduced baseline-shifted groups become `Superscript`, `Subscript` or `SubSuperscript`, grids of aligned cells inside a fence become `Table`, italic single letters become `Identifier`, digits with one decimal point become `Number`, multi-letter upright names such as `sin` and `log` become one `Identifier` with upright style, and everything else becomes `Operator`. Done when: `MathDetect.Trees` matches the planted canonical tree for fraction, superscript, subscript, radical, summation with limits, integral with limits, a `2 x 2` matrix and a `cases` block.
- [ ] Account for every semantic glyph of a zone (everything except whitespace and the stripped equation label): a region is `Parsed` only when the tree consumed all of them and no sibling boxes overlap, whatever its `confidence`; any glyph left over makes the region `Uncertain` and is listed in `unparsed_fragments`. `confidence` (the consumed share and the absence of overlaps) only ranks `Uncertain` regions and gates `Parsed` ones at `min_confidence`. Done when: `MathDetect.UncertainNeverGuesses` reports `Uncertain` and the fragments for an overlapping layout and for an otherwise valid expression with an unparsed trailing term that consumes 80 percent of the glyphs, and no exporter output is produced for either in `§19`.
- [ ] Attach a trailing equation label such as `(3.1)` at the right margin of a display zone to the region as `equation_label` and exclude it from the tree. Done when: `MathDetect.EquationLabel` separates `(3.1)` from the expression and leaves a parenthesised sub-expression inside a formula untouched.
- [ ] Bound work: stop on `CancelToken` every `64` zones, cap regions per page at `5000` and glyphs inspected per page at `500000`, and return a typed `Truncated` outcome naming the cap that fired. Done when: `MathDetect.CapsAndCancel` truncates a page of `2 million` glyphs without unbounded allocation under ASan and cancels within `50 ms`.
- [ ] Add a `math` group to `scripts/build-corpus.py` that writes `tests/corpus/math/*.pdf` with `.expect.json` sidecars holding the planted canonical tree, MathML and LaTeX of each expression (emitted by the generator and never read back from Recto), a prose-with-Greek-letters page, a tagged `/Formula` page and an overlapping layout, and record them in `tests/corpus/PROVENANCE.md`. Done when: `python scripts/build-corpus.py --group math` run twice yields byte-identical files and `Corpus.Provenance` passes.
- [ ] Add `MathDetect.*` tests in `tests/recto-tests/math_detector_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=MathDetect.*` passes under ASan.
- [ ] Commit: `core: add MathRegionDetector for math zones and expression trees`

**Test checkpoint:** `recto-tests.exe --gtest_filter=MathDetect.*` passes under ASan, and making `MathDetect.ProseNotMath` count a lone `µm` as a zone fails it.

## 19. `MathExporter`: MathML 3 and LaTeX serializers, web equations, `recto-cli math`

Foxit PDF Editor exports recognised equations as MathML or LaTeX; this section does the same from the trees of `§18`. `MathExporter` (`src/core/math_export.h/.cpp`) serializes a `MathNode` tree to MathML 3 and to LaTeX, and `WebDocWriter` (`§3`, `§4`) gains an `equations` option that places each `Parsed` region as MathML in HTML5 and EPUB 3, as LaTeX in Markdown and `.tex`, or keeps the text. A symbol that has no LaTeX macro is never dropped: it is written as its Unicode character and listed in `unmapped_symbols`, so the output stays honest. Regions marked `Uncertain` are left as the original text. All output is escaped for its target, so hostile PDF text cannot inject markup, and nothing runs scripts.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `to_mathml` and `to_latex` in `src/core/math_export.cpp` with escaping, the symbol table and `unmapped_symbols`. Done when: `MathExport.MathMl` and `MathExport.Latex` equal the planted MathML and LaTeX of every `math` fixture after canonicalisation, under ASan.
2. Wire `equations` into `WebDocWriter` and add `recto-cli math`. Done when: `WebDocWriter.EquationsHtmlEpubMdTex` and `Cli.MathCommand` pass under ASan.

- [ ] Implement `to_mathml(const MathNode&, MathMlOptions)` emitting MathML 3 core elements (`math`, `mrow`, `mi`, `mn`, `mo`, `mtext`, `mfrac`, `msqrt`, `mroot`, `msup`, `msub`, `msubsup`, `munderover`, `mfenced` replaced by `mo` fences, `mtable`) in the `http://www.w3.org/1998/Math/MathML` namespace with `display` of `block` or `inline` and an `alttext` holding the linear text. Done when: `MathExport.MathMl` parses its own output as strict XML and matches each planted MathML.
- [ ] Implement `to_latex(const MathNode&, LatexOptions)` with `\frac`, `^{}`, `_{}`, `\sqrt[n]{}`, `\sum`, `\int`, `\left` and `\right` fences, `pmatrix` and `cases` environments, a Greek and operator macro table, escaping of the LaTeX reserved characters inside `\text{}`, and `unmapped_symbols`. Done when: `MathExport.Latex` matches each planted LaTeX and `MathExport.UnmappedSymbolsListed` reports a symbol with no macro rather than dropping it.
- [ ] Add a bounded structural check `check_latex_balanced` (braces, `\begin` and `\end`, `\left` and `\right`) and a MathML re-parse used by the writer before it embeds a result. Done when: `MathExport.ValidateBeforeEmbed` rejects a hand-broken tree serialization and the writer falls back to the original text for that region.
- [ ] Add `EquationMode` (`KeepText`, `Auto`, `MathMl`, `Latex`) and `equations` (default `Auto`: MathML for HTML5 and EPUB 3, LaTeX for Markdown and `.tex`) to `WebDocExportOptions` in `src/core/web_doc_writer.h`. Done when: the header compiles in `recto-core`, `recto-services` and `recto-cli`.
- [ ] Emit equations in `src/core/web_doc_writer.cpp`: inline `<math>` in Flowing HTML with the `alttext` attribute, `$...$` and `$$...$$` in Markdown, `\(...\)` and `equation` environments in `.tex` with `amsmath` and `amssymb` added to the preamble, and MathML in EPUB 3 chapters with `properties="mathml"` on the manifest items that contain it. Fixed-layout HTML keeps its positioned text spans and adds the MathML as a visually hidden sibling. Done when: `WebDocWriter.EquationsHtmlEpubMdTex` finds each format's equation form on a math fixture and the EPUB manifest property is present. Cheaper substitute: rendering each equation as a picture, which loses the structure.
- [ ] Leave `Uncertain` regions and regions below `min_confidence` as the original text in every format and count them in `WebDocWriteSummary::equations_kept_as_text`. Done when: `WebDocWriter.UncertainEquationKeptAsText` shows the overlapping-layout fixture unchanged and counted.
- [ ] Escape every string that reaches XML or LaTeX output, including `alttext`. Done when: `MathExport.HostileText` converts a region whose text contains `<script>`, `&`, `\input{x}` and `%` and finds them inert in the HTML, MathML and `.tex` outputs.
- [ ] Implement `recto-cli math <detect|export> <in.pdf> [--pages <range>] [--min-confidence <0..1>] [--format json|mathml|latex] [--out <path>] [--password <pw>]` in `src/cli/cmd_math.cpp`: `detect` prints regions with page, box, confidence and status; `export --format json` writes both serializations per region, `--format mathml` writes one `.mml` file per region into the `--out` folder, `--format latex` writes one `.tex` with one equation per region. Done when: `Cli.MathCommand` verifies each verb, the exit-code contract of `D01 T03 §1` and that no output is written for a PDF with no regions beyond an empty report.
- [ ] Add `MathExport.*` tests in `tests/recto-tests/math_export_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=MathExport.*:WebDocWriter.Equation*:WebDocWriter.UncertainEquation*:Cli.MathCommand` passes under ASan.
- [ ] Commit: `core: add MathML and LaTeX equation export and recto-cli math`

**Test checkpoint:** `recto-tests.exe --gtest_filter=MathExport.*:WebDocWriter.Equation*:WebDocWriter.UncertainEquation*:Cli.MathCommand` passes under ASan, and dropping the escaping in `to_latex` fails `MathExport.HostileText`.

## 20. Tag detected formulas as `Formula` elements with MathML associated files

Foxit PDF Editor tags recognised equations as `Formula` structure elements that carry MathML; Acrobat Pro can mark a `Formula` only by hand and type Alt text. This section tags `§18` regions on a working copy through `ReadingOrderEditor::tag_region` (`§15`) and `StructTreeEditor` (`§14`): the painting operators are wrapped in `BDC`/`EMC` with a fresh `/MCID`, a `Formula` element is created with `/Alt` text, and the MathML from `§19` is embedded as an associated file (`/AF`) of the element, which is the PDF 2.0 way to give assistive technology the structure of an equation. Because PDF/A-1 and PDF/A-2 restrict embedded files, the MathML file is skipped and only `/Alt` is written when the document claims one of those levels. Everything is on a copy, saved as a full rewrite, and the open viewer document is never touched.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `tag_formulas(pdf::Document& working_copy, span<const MathRegion>, TagFormulaOptions)` in `src/pdf/formula_tagging.h/.cpp`. Done when: `FormulaTag.TagAndAttach` tags the fixture equations, `check_integrity` of `§14` passes and each `Formula` element carries `/Alt` and a MathML `/AF` entry, under ASan.
2. Wire the option into the auto-tagger and add `recto-cli math tag`. Done when: `FormulaTag.AutoTaggerOption` and `Cli.MathTag` pass under ASan.

- [ ] Define `TagFormulaOptions` (`min_confidence`, `attach_mathml`, `allow_version_upgrade` default `true`, `retag_existing` default `false` (forwarded to `ReadingOrderEditor::tag_region`, `§15`), `alt_text_source` of `Linear`, `Latex`) and `TagFormulaReport` (`tagged`, `skipped_uncertain`, `skipped_already_tagged`, `mathml_attached`, `mathml_skipped_reason`, `version_upgraded`) in `src/pdf/formula_tagging.h`. Done when: the header compiles across `recto-pdf`, `recto-core`, `recto-services` and `recto-cli`.
- [ ] Tag each `Parsed` region at or above `min_confidence` with `ReadingOrderEditor::tag_region(rect, RegionRole::Formula, parent, index)` (`§15`), refuse a region already inside tagged marked content unless the caller passes `retag_existing`, and preserve existing `/ActualText` and `/MCID` values. Done when: `FormulaTag.TagAndAttach` tags three equations and `validate_pdfua` plus `check_integrity` pass on the output, `FormulaTag.RetagRefusedByDefault` counts a region inside tagged marked content in `skipped_already_tagged`, and `FormulaTag.RetagExisting` retags it with `retag_existing` set while keeping its `/MCID` and `/ActualText`.
- [ ] Write `/Alt` on the element from the region's linear text (or its LaTeX when `alt_text_source = Latex`). Done when: `FormulaTag.AltText` reads the `/Alt` of each element through `PageText::struct_nodes`.
- [ ] Embed each MathML string as an `/EmbeddedFile` stream (`/Subtype /application#2Fmathml+xml`, `/Params` with `/Size` and `/ModDate`), wrap it in a `/Filespec` with `/F`, `/UF`, `/EF` and `/AFRelationship /Supplement`, and list it in the `/AF` array of the `Formula` element, capped at `256 KiB` per file and `5000` files per document. Done when: `FormulaTag.MathMlAssociatedFile` finds each file reachable from its element and byte-identical to `to_mathml` output. Cheaper substitute: putting MathML into `/Alt`, which screen readers read as plain text.
- [ ] Skip the MathML file and record `mathml_skipped_reason = PdfA12` when XMP `pdfaid:part` is `1` or `2`, so the claimed conformance is not broken (`§5` embedded-file rules), and still write `/Alt`. Done when: `FormulaTag.PdfA2Respected` tags a PDF/A-2b fixture and `validate_pdfa(PdfA_2b)` still reports no embedded-file finding.
- [ ] Raise the output to PDF 2.0 through `PdfWriter` when `attach_mathml` applies and the source is older, report it in `version_upgraded`, and refuse the upgrade when `allow_version_upgrade` is `false`, falling back to `/Alt` only. Done when: `FormulaTag.VersionUpgrade` shows a 1.7 source written as 2.0 with the report flag and the refusal path writing no `/AF`.
- [ ] Add the `tag_math` option to `convert_to_pdfua` (`§7`, `§24`) so equation regions become `Formula` elements instead of paragraphs, and commit through `AtomicFileWriter` as a full rewrite. Done when: `FormulaTag.AutoTaggerOption` converts a math fixture and its equations are `Formula` elements while the surrounding text keeps its paragraph tags.
- [ ] Implement `recto-cli math tag <in.pdf> --out <out.pdf> [--pages <range>] [--min-confidence <0..1>] [--no-mathml] [--json]` in `src/cli/cmd_math.cpp`, refusing an `--out` equal to the input. Done when: `Cli.MathTag` writes a tagged copy, never the input, and prints the `TagFormulaReport` with `--json`.
- [ ] Add `FormulaTag.*` tests in `tests/recto-tests/formula_tagging_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=FormulaTag.*:Cli.MathTag` passes under ASan.
- [ ] Commit: `pdf: tag detected equations as Formula elements with MathML associated files`

**Test checkpoint:** `recto-tests.exe --gtest_filter=FormulaTag.*:Cli.MathTag` passes under ASan, and writing the MathML into `/Alt` instead of `/AF` fails `FormulaTag.MathMlAssociatedFile`.

## 21. Equations option on the web Convert card and the Reading Order bar

Make the equation features of `§18` to `§20` reachable by a person. The **HTML, Markdown, EPUB & LaTeX** card of `§4` gains an `Equations` choice, and the Reading Order bar of `§17` gains `Detect equations`, which tags the page's equations on the shared `TagEditSession`. It follows Foxit PDF Editor's equation export and keeps Recto's rules: offline, edits on a copy, every command reachable from `Ctrl+K`, every icon-only control named, colours only from `recto_tokens.g.h` tokens.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.5 (the `HTML, Markdown, EPUB & LaTeX` Step 3 options, `D-Convert.png`) for the option row, and the Reading Order bar of `§17` (SPEC section 8.2 contextual bar) for the button; no screenshot shows either addition, so both follow the existing controls beside them.
**Job:** A person converting a paper to Markdown or EPUB chooses how equations are written and reads how many were kept as text, and an accessibility remediator tags a page's equations in one action and reviews them before saving.
**Treatment:** An `Equations` combo (`Detect and convert`, `Keep as text`, `MathML`, `LaTeX`) in `WebConvertOptionsControl` with a result line `"12 equations converted, 2 kept as text"` in the completion toast, and a `Detect equations` button in `ReadingOrderBar` that shows the detected regions as numbered boxes (with confidence as text) before applying. Cheaper substitute that fails the checkpoint: a hidden default with no choice or count, or applying detected tags without a review step.
**Chrome:** consume `WebConvertOptionsControl` (`§4`), the completion notice of `D04 T01 §6`, `ReadingOrderBar` and `ReadingOrderCanvasOverlay` (`§17`), `CommandRegistry` (`D02 T03 §11`) and `recto_tokens.g.h` tokens with Fluent UI System Icons (`RectoIcons` via `ui::Icon`).

**Build order:**

1. Add the `Equations` option to `WebConvertOptionsControl` and the completion summary. Done when: `AppTests.ConvertWebDocUi` converts a math fixture with each choice and reads the count line.
2. Add `Detect equations` to the Reading Order bar. Done when: `AppTests.ReadingOrderUi` detects, reviews and tags equations by pointer and by keyboard and saves a copy that passes `validate_pdfua`.

- [ ] Add the `Equations` combo to `src/app/views/convert/web_convert_options_control.h/.cpp`, bound to `WebDocExportOptions::equations`, visible for HTML5, Markdown, EPUB 3 and LaTeX only, with an automation name and tooltip. Done when: choosing `Keep as text` produces output identical to the pre-feature writer. Cheaper substitute: an unlabelled toggle.
- [ ] Show the equation counts from `WebDocWriteSummary` in the completion toast and `QueueCard` summary. Done when: `AppTests.ConvertWebDocUi` reads `"N equations converted, M kept as text"`.
- [ ] Add `Detect equations` to `src/app/controls/reading_order_bar.h/.cpp` calling `MathRegionDetector` on the `TagEditSession` worker thread and drawing the regions in `ReadingOrderCanvasOverlay` with number and confidence as text, with `Apply to all`, `Apply to selected` and `Cancel`. Done when: no tag changes until `Apply`, and `Cancel` leaves the copy unchanged.
- [ ] Apply through `tag_formulas` (`§20`) with one undo step per apply and show `mathml_skipped_reason` as a plain sentence when MathML was not attached. Done when: `Ctrl+Z` removes the applied tags and a PDF/A-2 fixture shows the sentence.
- [ ] Register `convert.equations.mode`, `accessibility.readingOrder.detectEquations` and `accessibility.formula.tagPage` in `CommandRegistry` with synonyms `equation`, `math`, `mathml`, `latex`, `formula`. Done when: `Ctrl+K` "equation" finds the Reading Order command with a document open.
- [ ] Add `AppTests.EquationsUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=AppTests.EquationsUi` passes on the display-session host.
- [ ] Commit: `app: add the Equations option and Detect equations to the Reading Order bar`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.EquationsUi:AppTests.ConvertWebDocUi:AppTests.ReadingOrderUi` passes on the display-session host, and applying tags without the review step fails `AppTests.EquationsUi`.

## 22. Colour contrast check engine (WCAG 2.2 success criterion 1.4.3)

Foxit PDF Editor's accessibility checks go beyond PDF/UA-1 with a colour contrast check; Acrobat Pro's checker leaves contrast to manual review. `ContrastChecker` (`src/core/contrast_check.h/.cpp`) measures every visible text run against the background it is drawn on and reports the WCAG contrast ratio: `4.5:1` for normal text and `3:1` for large text (at least `18` pt, or `14` pt bold). The foreground colour comes from the run's paint state, and the background comes from rendering the page with all text removed, so gradients, images and filled boxes under text are measured as drawn. It is honest about what it could not measure: text painted with a pattern or shading, text on top of other text, and scanned pages are listed as `NotEvaluated` with a reason, and a page with any such text can never make the verdict `Passed`. It runs on the job's own `pdf::Document`, never on the UI thread.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the WCAG ratio functions and the run collector in `src/core/contrast_check.cpp`. Done when: `Contrast.Ratio` returns `21.0` for black on white, `4.54` for `#767676` on white and `4.48` for `#777777` on white, and `Contrast.LargeTextThreshold` applies `3:1` at `18` pt and `14` pt bold only.
2. Implement background sampling from a text-free render and the verdict. Done when: `Contrast.Fixtures` matches every sidecar fact of the `contrast` fixture and `Contrast.NeverPassesUnevaluated` shows pattern-filled text makes the verdict `Incomplete`, under ASan.

- [ ] Implement `relative_luminance` and `contrast_ratio` in `src/core/contrast_check.cpp` using the WCAG 2.2 relative luminance definition (sRGB linearisation, weights `0.2126`, `0.7152`, `0.0722`) and compare against the threshold without rounding, so `4.499` fails `4.5`. Done when: `Contrast.Ratio` and `Contrast.NoRounding` pass on the known pairs.
- [ ] Collect visible text runs from `PageText` (`D03 T01 §1`) with fill colour, size, bold flag, rendering mode and alpha, skipping invisible runs (rendering mode `3`, including the OCR layer of `D04 T02 §7`) and clipping-only modes, converting the fill colour to sRGB through the colour engine of `D04 T06 §7` for ICC-based spaces, and applying fill alpha over the sampled background. Done when: `Contrast.RunCollection` skips an invisible OCR layer and converts a CMYK and an ICC-based fill.
- [ ] Build the background plane for each page by copying the immutable `DisplayList` without its text items and rasterizing it with the tile rasterizer (`D01 T02 §6`) at `150` DPI on the job thread, then, for each glyph quad of a run (inflated by one pixel), read every background pixel inside it and compute the ratio against the run's fill for each distinct colour found, with no coverage threshold, so a small patch behind one character counts. The reported ratio is the lowest, and `background_varies` is set when more than one colour is found. A glyph quad smaller than one pixel at `150` DPI is sampled at the pixel containing its centre, and a run whose quads fall outside the rasterized page or past the pixel cap is `NotEvaluated` with the reason. Done when: `Contrast.Fixtures` measures text on a solid fill, on a gradient (lowest end reported), on an image, and with a small low-contrast patch behind a single character (reported as failing), and `Contrast.CoverageIncomplete` marks a run whose quads cannot be sampled `NotEvaluated`.
- [ ] Mark a run `NotEvaluated` with a reason when its fill is a pattern or shading, when it overlaps other text, or when it is image-based text, and count it in the ledger. Image-based text is tracked independently of any text layer: a page whose visible content includes an image that carries text (an image-only page, or an image under invisible OCR runs of `D04 T02 §7`) is `NotEvaluated(image of text)` for the area of that image. Never let a page with such runs report `Passed`. Done when: `Contrast.NeverPassesUnevaluated` returns `Incomplete` for the pattern-filled fixture, for a scanned fixture, and for the same low-contrast scan after OCR added an invisible text layer.
- [ ] Define `ContrastFinding` (`page_index`, `bbox_pt`, `text_sample` of at most `40` characters, `foreground_srgb`, `background_srgb`, `ratio`, `required`, `large_text`, `background_varies`, `in_artifact`) with severity `Error` for content text and `Warning` for text inside an `/Artifact`, add `ComplianceCategory::Contrast` to `src/pdf/compliance.h`, and convert a finding to the `ComplianceFinding` of `§5`. Done when: `Contrast.FindingAdapter` converts a finding without losing its ratio.
- [ ] Compute the verdict with the rule of `D04 T06 §6`: `Failed` if any evaluated run is under its threshold, else `Incomplete` if any run was `NotEvaluated`, else `Passed`, and report `runs_total`, `runs_evaluated`, `runs_failed` and per-page counts. Done when: `Contrast.Verdict` covers the three outcomes.
- [ ] Bound work: cap runs per page at `200000`, rasterized pixels per page at `16 million`, glyph quads per page at `1 million`, and cumulative background pixel visits per page at `64 million` (four visits per raster pixel), checking the remaining visit budget before each glyph quad is read; poll the `CancelToken` every `256` runs and also inside the sampling loop every `65536` pixel visits; keep the lowest ratio and the `background_varies` flag as running values per quad so sampling allocates nothing per pixel; and report `pixels_visited` and `quads_sampled` in the result. When any cap is reached, the run being sampled and every later run on the page are `NotEvaluated(sampling budget)` (never partially measured), the result is `Truncated` with the cap named, and the verdict can be at most `Incomplete`. Done when: `Contrast.CapsAndCancel` truncates an oversized page without unbounded allocation under ASan, `Contrast.SamplingBudget` runs a single pathological run of `50000` page-sized glyph quads on a page below the raster cap and stops with `pixels_visited <= 64 million`, a `Truncated` result naming the visit cap and verdict `Incomplete`, and `Contrast.CancelInsideRun` trips the token after the first quad of that same single run and finds `pixels_visited` grew by at most `65536` more (it fails if cancellation is polled only between runs).
- [ ] Add an `a11y` group to `scripts/build-corpus.py` that writes `tests/corpus/a11y/contrast-pairs.pdf` with planted pairs (`#767676` and `#777777` on white, an `18` pt and a `14` pt bold `3:1` case, text on a gradient, on an image, a semi-transparent fill, an invisible OCR layer, a pattern-filled run, text over text, a small low-contrast patch behind one character, and a low-contrast scan with and without an invisible OCR layer) and a sidecar listing each expected ratio to two decimals, and record it in `tests/corpus/PROVENANCE.md`. Done when: `python scripts/build-corpus.py --group a11y` run twice yields byte-identical files and `Corpus.Provenance` passes.
- [ ] Add `Contrast.*` tests in `tests/recto-tests/contrast_check_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Contrast.*` passes under ASan.
- [ ] Commit: `core: add WCAG colour contrast check over text-free page renders`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Contrast.*` passes under ASan, rounding the ratio to one decimal before the comparison fails `Contrast.NoRounding`, and removing the in-loop cancellation poll fails `Contrast.CancelInsideRun`.

## 23. PDF/UA-2 (ISO 14289-2) rule set, requirement inventory and validator

Foxit PDF Editor checks accessibility beyond PDF/UA-1; this section adds the PDF/UA-2 (ISO 14289-2, built on ISO 32000-2) rule set to the `ComplianceEngine` of `§5`. The ISO text is paywalled, so the section follows the honest-coverage method of `D04 T06 §17`: each rule records in its `source` field the clause the implementer read, a requirement inventory generated from the standard's clause index marks every clause `implemented`, `not implemented` or `not applicable` with a reason, and `inventory_complete` stays `false` until a second reader has compared it with the standard. The report status is `NotClaimed` (no identification), `IdentifiedOnly` (identified, no checks run), `PartialChecksPassed` or `PartialChecksFailed`; the word `Conformant` is reachable only when `inventory_complete` is `true`, no entry is `not implemented` and every check passes, which no v1 build satisfies. Acrobat Pro's checker is not extended here; this is a Recto addition.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `PdfStandardLevel::PdfUa_2`, the XMP identification reader and `resources/compliance/pdfua-2.json` (rules plus inventory) on the preflight schema of `D04 T06 §2`. Done when: `PdfUa2.Profile` loads the rule file, every rule has a non-empty `source`, and an unclaimed fixture reports `NotClaimed`.
2. Implement `ComplianceEngine::validate_pdfua2` and the status matrix. Done when: `PdfUa2.ValidFixture` reports `PartialChecksPassed`, each broken fixture fails its named rule, and `PdfUa2.StatusMatrix` refuses `Conformant`, under ASan.

- [ ] Add `PdfUa_2` to `PdfStandardLevel` (`§5`) and read the claim from XMP (`pdfuaid:part` equal to `2` and the revision property the standard defines, recorded in the rule's `source`), reporting `NotClaimed` rather than guessing from content. Done when: `PdfUa2.Identification` reads the claim from a claimed fixture and reports `NotClaimed` for a PDF/UA-1 fixture.
- [ ] Author `resources/compliance/pdfua-2.json` with the rules the engine can check, each with `source`: PDF version `2.0` in the header or catalog, `/MarkInfo /Marked true`, `/Lang` on the catalog, `dc:title` in XMP with `/ViewerPreferences /DisplayDocTitle true`, a single root structure element and `/ParentTree` integrity, PDF 2.0 structure namespace use, `/Tabs /S` on every page, `/Alt` on figures, MathML associated files on `Formula` elements where the clause read requires them, heading nesting, table header cells, links and form fields represented in the structure tree with a tooltip, and every content operator either tagged or marked as an artifact. Done when: `PdfUa2.Profile` fails when any rule's `source` is empty.
- [ ] Write the requirement inventory `resources/compliance/inventory/pdfua-2.json` from the standard's clause index (clause id, title as read, `implemented` of `true`, `false` or `not_applicable` with a reason, rule id) with `inventory_complete: false`. Done when: `PdfUa2.Inventory` asserts every rule id appears in the inventory and every `implemented: true` entry names an existing rule.
- [ ] Implement `ComplianceEngine::validate_pdfua2(const pdf::Document&)` returning `ComplianceFinding`s with clause ids, object ids and page indices, reusing the structure readers and caps of `§7` (iterative walk, depth `64`, nodes `1000000`) and returning `Truncated` with a finding at a cap. Done when: `PdfUa2.ValidateRules` detects each planted fault in the `ua2-broken-*` fixtures by rule id.
- [ ] Derive `ConformanceStatus` with the matrix above in the report and in `recto-cli pdfa validate --level ua2`, and extend the `--level` list of `recto-cli pdfa` (`§6`) with `ua2` for `validate` only. Done when: `PdfUa2.StatusMatrix` covers a table of cases (`PartialChecksPassed` with unimplemented entries accepted, `Conformant` with an unimplemented entry or `inventory_complete: false` refused) and `Cli.PdfACommand` validates a UA-2 fixture.
- [ ] Add `ua2-valid.pdf` and one `ua2-broken-*.pdf` per rule to the `a11y` group of `scripts/build-corpus.py` with sidecars naming the planted rule id, and record in `tests/corpus/PROVENANCE.md` that no independent PDF/UA-2 validator was available and that an externally validated sample (for example from a PDF/UA-capable validator or a commercial checker) is a future owner-supplied addition, not a blocker. Done when: `Corpus.Provenance` passes with the rows.
- [ ] Add `PdfUa2.*` tests in `tests/recto-tests/pdf_ua2_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfUa2.*:Cli.PdfACommand` passes under ASan.
- [ ] Commit: `pdf: add the PDF/UA-2 rule set, requirement inventory and validator`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfUa2.*:Cli.PdfACommand` passes under ASan, and letting the status matrix return `Conformant` with `inventory_complete: false` fails `PdfUa2.StatusMatrix`.

## 24. PDF/UA-2 output: the auto-tagger writes PDF 2.0 structure

Let `convert_to_pdfua` (`§7`) target PDF/UA-2 as well as PDF/UA-1, so a person who finds UA-2 findings can produce a copy that clears the ones the engine can check. The auto-tagger keeps its reading-order, heading, list and table inference and changes only what the part requires: the output is written as PDF 2.0, the structure tree uses the PDF 2.0 standard structure namespace (`http://iso.org/pdf2/ssn`) with the `/Namespaces` entry and `/NS` on elements as ISO 32000-2 defines, `Formula` elements get MathML when `§20` runs, and XMP identifies the file with `pdfuaid:part` `2`. The honest ceiling stays: the best claim Recto makes for the result is `PartialChecksPassed` from `§23`, and placeholder Alt text is reported, never counted as accessible.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `PdfUaPart` (`Ua1`, `Ua2`) to `convert_to_pdfua` and write the PDF 2.0 structure output. Done when: `PdfUa2.AutoTagOutput` converts an untagged corpus PDF with headings, a list, a table, a figure and an equation and `validate_pdfua2` reports `PartialChecksPassed` with no findings for implemented rules, under ASan.
2. Report placeholder Alt text and wire `recto-cli pdfa convert --level ua2`. Done when: `PdfUa2.PlaceholderAltReported` and `Cli.PdfACommand` pass under ASan.

- [ ] Extend `convert_to_pdfua` in `src/pdf/compliance.cpp` with a `PdfUaPart` parameter (default `Ua1`, unchanged behaviour). Done when: `PdfCompliance.PdfUaAutoTagAndValidate` still passes with the default.
- [ ] For `Ua2`, write `/Version /2.0` through `PdfWriter`, add the `/Namespaces` array and `/NS` entries for the PDF 2.0 standard structure namespace, map the elements the auto-tagger already creates, and refuse inputs that use features PDF 2.0 removed with a typed error naming the feature. Done when: `PdfUa2.AutoTagOutput` finds the namespace on the root and elements, and a source with a removed feature is refused with no output.
- [ ] Write `pdfuaid:part` `2` and the revision property into XMP, keep `/Lang`, the document title and `/DisplayDocTitle`, and set `/Tabs /S` on every page as in `§7`. Done when: `validate_pdfua2` finds no identification, language, title or tab-order finding on the output.
- [ ] Count Alt text the tagger synthesised as a placeholder (`"Figure on page N"`) in `PdfUaConvertReport::placeholder_alt_count` and never treat it as satisfying a human-quality Alt text check in `§25`. Done when: `PdfUa2.PlaceholderAltReported` returns the count for a fixture with three figures.
- [ ] Extend `recto-cli pdfa convert --level ua2` in `src/cli/cmd_pdfa.cpp`, with the exit codes of `§6`, and refuse combining `ua2` with every PDF/A level Recto produces (`1a`, `1b`, `2a`, `2b`, `2u`, `3a`, `3b`, `3u`): PDF/A-1, PDF/A-2 and PDF/A-3 are based on PDF 1.x and PDF/UA-2 requires PDF 2.0, so a combined claim would need PDF/A-4, which this file does not produce. Done when: `Cli.PdfACommand` converts to `ua2` alone and refuses `ua2` with each of the eight levels, naming the reason.
- [ ] Add `PdfUa2.AutoTag*` tests in `tests/recto-tests/pdf_ua2_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfUa2.AutoTag*:PdfUa2.PlaceholderAltReported:Cli.PdfACommand` passes under ASan.
- [ ] Commit: `pdf: let the auto-tagger write PDF/UA-2 structure in PDF 2.0`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfUa2.AutoTag*:PdfUa2.PlaceholderAltReported:Cli.PdfACommand` passes under ASan, and omitting the structure namespace fails `PdfUa2.AutoTagOutput`.

## 25. WCAG 2.2 report model: criteria mapping, statuses and exporters

Foxit PDF Editor can produce an accessibility report against WCAG; this section builds Recto's. `WcagReport` (`src/core/wcag_report.h/.cpp`) gathers the findings of the PDF/UA-1 validator (`§7`), the PDF/UA-2 validator (`§23`) and the contrast check (`§22`), maps each rule through a data table to the WCAG 2.2 success criteria it evidences (for example 1.1.1 Non-text Content, 1.3.1 Info and Relationships, 1.3.2 Meaningful Sequence, 1.4.3 Contrast (Minimum), 2.4.2 Page Titled, 2.4.3 Focus Order, 3.1.1 Language of Page, 4.1.2 Name, Role, Value), and reports each criterion as `Pass`, `Fail`, `NotTested` or `ManualReview`. Automated checks cannot establish WCAG conformance, so the report never says a document conforms: criteria that need a human (Alt text quality, reading order correctness, use of colour) are `ManualReview`, and the summary states how many Level A and AA criteria the automated checks covered. The report is the document an accessibility reviewer files and the audit record of which checks ran.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `WcagReport::build` and the mapping table `resources/compliance/wcag-map.json`. Done when: `WcagReport.Mapping` maps every rule id of `§7`, `§22` and `§23` to at least one criterion or to an explicit `no_wcag_mapping` reason.
2. Implement the exporters. Done when: `WcagReport.Exports` writes valid JSON, a script-free HTML file and text for a failing and a passing run.

- [ ] Define `WcagCriterionStatus` (`Pass`, `Fail`, `NotTested`, `ManualReview`), `WcagCriterionResult` (`id`, `name`, `level`, `status`, `evidence` rule ids, `finding_count`, `reason`) and `WcagReport` (tool version, input name, size and SHA-256, standard parts checked, per-criterion results, summary counts, the contrast ledger, `statement`) in `src/core/wcag_report.h`. Done when: the header compiles across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Write `resources/compliance/wcag-map.json` (rule id to criterion ids, with the WCAG 2.2 version stated) and load it with the bounded reader of `D04 T06 §2`. Done when: `WcagReport.Mapping` fails when a rule id has neither a mapping nor a `no_wcag_mapping` reason.
- [ ] Derive each criterion status: `Fail` if any evidencing finding is an error, `ManualReview` if the criterion needs human judgement even when automated rules pass, `Pass` only when every evidencing check was evaluated and passed and no human judgement is needed, else `NotTested`; a truncated or `Incomplete` check makes its criteria `NotTested`. Done when: `WcagReport.Status` covers the four outcomes and an `Incomplete` contrast run never yields `Pass` for 1.4.3.
- [ ] Always include the `statement` `"Automated checks cannot establish WCAG conformance. Criteria marked Manual review need a person."` and the count of Level A and AA criteria covered by an automated check. Done when: `WcagReport.NeverClaimsConformance` fails if any export contains `conforms` or `compliant` for the document as a whole.
- [ ] Export JSON against a versioned `wcag-report.schema.json`, a self-contained static HTML file with every value escaped, no script and `prefers-color-scheme` styling, and a text summary, through `AtomicFileWriter`. Done when: `WcagReport.Exports` validates the JSON, the HTML parser finds no `<script>`, and a failed write leaves no partial file. Cheaper substitute: concatenating unescaped HTML, which lets a hostile text sample inject markup.
- [ ] Make the report deterministic (findings sorted by page, object and rule, timestamps excluded from comparison). Done when: `WcagReport.Deterministic` produces byte-identical JSON from a 1-worker and an 8-worker run.
- [ ] Add `WcagReport.*` tests in `tests/recto-tests/wcag_report_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=WcagReport.*` passes under ASan.
- [ ] Commit: `core: add the WCAG 2.2 report model with criteria mapping and exports`

**Test checkpoint:** `recto-tests.exe --gtest_filter=WcagReport.*` passes under ASan, and mapping an `Incomplete` contrast run to `Pass` fails `WcagReport.Status`.

## 26. `recto-cli a11y` check and report commands

Make the accessibility checks scriptable and give tests a seam below the UI. `recto-cli a11y` runs the PDF/UA validators, the contrast check and the WCAG report as one job on the same engine as the app, follows the dispatcher and exit-code contract of `D01 T03 §1`, and uses the verdict codes of `recto-cli preflight` (`D04 T06 §15`) so a script can tell a failed file from an unchecked one. It never modifies the input.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `recto-cli a11y <check|report>` in `src/cli/cmd_a11y.cpp`. Done when: `Cli.A11yCheck` returns the documented exit code for passing, failing and incomplete fixtures.
2. Implement report output and the exit-code table. Done when: `Cli.A11yReport` and `Cli.A11yExitCodes` pass under ASan.

- [ ] Implement `recto-cli a11y check <in.pdf> [--part ua1|ua2|both] [--contrast on|off] [--pages <range>] [--password <pw>] [--json]` running `validate_pdfua`, `validate_pdfua2` and `ContrastChecker` as one job, printing findings by rule, page and object. Done when: `Cli.A11yCheck` lists the planted faults of the `a11y` fixtures by rule id.
- [ ] Implement `recto-cli a11y report <in.pdf> --report <out.json|.html|.txt> [same options]` writing the `WcagReport` of `§25`. Done when: `Cli.A11yReport` writes all three formats and refuses a `--report` path equal to the input.
- [ ] Map exit codes: `0` when no error finding exists and no check was `Incomplete`, `4` when an error finding exists, `5` when a check was `Incomplete` and none failed, with `1`, `2` and `3` as in `D01 T03 §1`; write the requested report for exits `0`, `4` and `5` and nothing for `1`, `2`, `3`. Done when: `Cli.A11yExitCodes` verifies each code and the output rule.
- [ ] Add `Cli.A11y*` tests in `tests/recto-tests/cli_a11y_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Cli.A11y*` passes under ASan.
- [ ] Commit: `cli: add recto-cli a11y check and report`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cli.A11y*` passes under ASan, and mapping `Incomplete` to exit `0` fails `Cli.A11yExitCodes`.

## 27. Accessibility checks and WCAG report in the Standards & Accessibility panel

Surface the checks of `§22` to `§26` in the Standards & Accessibility tab of the `Signatures & Standards` panel (`§9`, `§16`), beside the PDF/UA-1 checklist and the Tags tree: a standard selector that gains `PDF/UA-2 (partial checks)`, a contrast section whose rows show the measured ratio as text with the page and a jump to the run, a WCAG criteria table with `Manual review` items clearly marked, and `Save WCAG report...`. The Convert card of `§9` gains `PDF/UA-2` as a target through `§24`. Findings are never shown by colour alone, and the panel states plainly when a check was `Incomplete`.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 8.3, `Signatures & Standards` panel, *Standards & Accessibility tab*; no screenshot exists, so it follows the panel chrome and the checklist rows built in `§9` and `§16`. Design board: `design/SPEC.md` section 8.3.3 with `design/screenshots/D-Signatures.png` (drawn by `D06 T04 §4`).
**Job:** An accessibility reviewer runs the checks on the open document, reads which WCAG criteria failed, which need manual review and which were not tested, jumps from a contrast finding to the text on the page, and saves a report to file.
**Treatment:** Expandable sections `PDF/UA`, `Colour contrast` and `WCAG 2.2`, each row with an icon and a text status (`Pass`, `Fail`, `Manual review`, `Not tested`), the ratio and required ratio as numbers on contrast rows, a verdict banner that reads `Incomplete` when it is, and `Save WCAG report...`. Cheaper substitute that fails the checkpoint: a colour-only status dot, a green banner on `Incomplete`, or a single pass or fail line with no criteria.
**Chrome:** consume `SignaturesAndStandardsPanel` and `StandardsPanelViewModel` (`§9`), `SidePanelHost` (`D02 T03 §3`), `CommandRegistry` (`D02 T03 §11`), `AnnouncementService` (`D03 T02 §5`) and `recto_tokens.g.h` status brushes with Fluent UI System Icons (`RectoIcons` via `ui::Icon`).

**Build order:**

1. Extend `StandardsPanelViewModel` with the accessibility job and results. Done when: `StandardsPanel.A11yViewModel` runs the checks off the UI thread on a fixture and exposes sections, rows and the verdict.
2. Build the panel sections and the export. Done when: `AppTests.A11yPanelUi` runs the checks, jumps from a contrast row to the page, shows an `Incomplete` verdict and saves a report.

- [ ] Extend `src/services/standards_panel_view_model.h/.cpp` with `run_accessibility_checks(part, contrast)` using the job runner and the engines of `§22`, `§23` and `§25`, with progress and cancel. Done when: `StandardsPanel.A11yViewModel` populates without blocking the UI thread and cancels within `100 ms`.
- [ ] Add the `PDF/UA-2 (partial checks)` option to the target-standard selector of `SignaturesAndStandardsPanel` and to `ComplianceConvertOptionsControl` (`§9`), showing `PartialChecksPassed` as the sentence `"Passed the checks Recto can run; not a full conformance claim"`. Done when: `AppTests.A11yPanelUi` shows the sentence and never the word conformant.
- [ ] Build the `Colour contrast` section: each failing run as a row with the page, a short text sample, the measured and required ratio as numbers, and a click-to-jump that scrolls `DocumentView` to the run and highlights its box. Done when: clicking a row highlights the run. Cheaper substitute: a list of page numbers with no jump.
- [ ] Build the `WCAG 2.2` section from `WcagReport`: a table of criteria with level, text status and evidence count, a `Manual review` filter, and the report statement. Done when: `AppTests.A11yPanelUi` filters to `Manual review` and shows the statement.
- [ ] Show `Incomplete` and `Truncated` outcomes as a plain sentence with the reason, never as `Passed`, and announce the verdict through `AnnouncementService`. Done when: the pattern-filled fixture shows `Incomplete` and the announcement is recorded.
- [ ] Add `Save WCAG report...` with a `IFileSaveDialog` for `.json`, `.html` and `.txt` writing through `§25`. Done when: the three exports open correctly.
- [ ] Register `accessibility.check`, `accessibility.contrast`, `accessibility.wcagReport` and `accessibility.checkUa2` in `CommandRegistry` with synonyms `wcag`, `contrast`, `accessibility check`, `ua2`. Done when: `Ctrl+K` "contrast" runs the contrast section on the open document.
- [ ] Give every control an automation name and tooltip and make the sections keyboard operable. Done when: `A11yInventory` (`D03 T02 §3`) passes on the new sections.
- [ ] Add `StandardsPanel.A11y*` and `AppTests.A11yPanelUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=StandardsPanel.A11y*:AppTests.A11yPanelUi` passes on the display-session host.
- [ ] Commit: `app: add accessibility checks and the WCAG report to the Standards panel`

**Test checkpoint:** `recto-tests.exe --gtest_filter=StandardsPanel.A11y*:AppTests.A11yPanelUi` passes on the display-session host, and showing a green banner on `Incomplete` fails `AppTests.A11yPanelUi`.

## Verification

- `XlsxWriter`, `WebDocWriter`, and `ComplianceEngine` all pass under ASan and round-trip in `D06 T01 §6`.
- Every control in the new Convert cards and `Signatures & Standards` side panel carries an automation name and tooltip (`D03 T02 §3`).
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
