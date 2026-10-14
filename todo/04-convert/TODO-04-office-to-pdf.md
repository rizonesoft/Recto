---
schema_version: 1
id: office-to-pdf
domain: 04-convert
status: active
title: "TODO-04 -- Office, HTML and Markdown to PDF: Built-in C++20 Engine, Office COM and LibreOffice"
track: M5
---

# TODO-04 -- Office, HTML and Markdown to PDF: Built-in C++20 Engine, Office COM and LibreOffice

> **Goal:** Deliver `Office, HTML, Markdown & Data -> PDF` conversion that works out of the box on every Windows 11 machine with zero external installs via Recto's built-in C++20 OpenXML (`.docx`, `.xlsx`, `.pptx`), `.rtf`, `HTML/CSS` (`.html`), `Markdown` (`.md`), `CSV` (`.csv`) and `XML/Text` parser + DirectWrite/Direct2D PDF layout engine (`src/core/office_to_pdf.cpp` and `src/core/html_md_to_pdf.cpp`), while also supporting optional 1:1 native Microsoft Office COM automation (`AutomationSecurity = msoAutomationSecurityForceDisable`) and LibreOffice headless execution when installed and selected.

> **Current state:** Measured 2026-10-14. No Office-to-PDF or HTML/Markdown-to-PDF code exists yet. On 2026-10-14 decisions D7 and D10 were expanded so Recto includes its own native C++20 OpenXML, RTF, HTML/CSS, Markdown and CSV to PDF engine as the always-available built-in converter, alongside optional Microsoft Office COM and LibreOffice adapters behind `IOfficeConverter`.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D7, D10, D12), 5 (`Office, HTML, Markdown & Data -> PDF` row) and 5.1 (`Office -> PDF` security and converter rules), 7 (M5 done-when: `.docx`, `.xlsx`, `.pptx`, `.rtf`, `.html`, `.md`, `.csv` convert cleanly and macro-bearing `.docm` verified not to execute its macro), 9 (security rules 3 and 5).
- `design/SPEC.md` sections 2 (Principle 5: macros never run), 9.5 (`PDF from Office, HTML, Markdown & Images` card), 9.7 (Converting: Office to PDF converter selector showing Automatic, Built-in OpenXML engine, Microsoft Office, LibreOffice), 9.9 (`Install Office or LibreOffice` dialog when an external suite is explicitly forced or needed for legacy binary `.doc`/`.xls`/`.ppt`).
- -> XREF: D02 T01 §10 -- the `Install Office or LibreOffice` dialog in §8 uses `D02 T01 §10`'s single-active-dialog host.
- -> XREF: D02 T04 §5 -- the `Office to PDF converter` selector in Settings (`D02 T04 §5`) reads §4's converter detection and governs §8's adapter choice.
- -> XREF: D04 T01 §4 -- registers the `PDF from Office, HTML, Markdown & Images` card, file-type applicability rules, and `NeedsConverter` recovery path into the Convert page (`§4`) and `QueueCard` (`§7`).
- -> XREF: D04 T02 §2 -- `office_to_pdf.cpp` (§1, §2) and `html_md_to_pdf.cpp` (§3) emit vector PDFs via `D04 T02 §2`'s `PdfWriter` and `D04 T02 §1`'s `AtomicFileWriter`.
- -> XREF: D04 T03 §1 -- `office_to_pdf.cpp` (§1, §2) reads OpenXML `.docx`, `.xlsx`, and `.pptx` ZIP packages via `D04 T03 §1`'s `zip_archive.cpp`.
- -> XREF: D06 T02 §2 -- the security audit in `D06 T02 §2` re-runs the macro-suppression (`msoAutomationSecurityForceDisable` and `vbaProject.bin` ignore), HTML external network/script blocking, and safe-subprocess (`CreateProcessW`) proofs built here.

## Outcome

- Any Windows 11 machine converts `.docx` and `.rtf` (`§1`), `.xlsx`, `.csv`, and `.pptx` (`§2`), and `.html` and `.md` (`§3`) files to vector PDFs out of the box using Recto's built-in C++20 layout engines (`src/core/office_to_pdf.cpp` and `src/core/html_md_to_pdf.cpp`), with zero external software installed and zero remote network or script execution.
- `detect_office_converters()` (`§4`) probes installed Microsoft Office CLSIDs and LibreOffice `soffice.exe` paths with injectable test overrides.
- When Microsoft Office is installed and selected, the STA COM adapter (`§5`) converts via Word, Excel, and PowerPoint with `AutomationSecurity = msoAutomationSecurityForceDisable` (3) so VBA macros never execute (proven on `macro-canary.docm`).
- When LibreOffice is installed and selected, the headless adapter (`§6`) runs `soffice.exe` via `CreateProcessW` in an isolated temp profile and a kill-on-close Win32 Job Object.
- The Convert page (`§7`) and Settings/missing-suite dialog (`§8`, `D-Dialogs.png`) tie all three engines together cleanly.

**Adjacency:** all=not-applicable (converter engines and adapters inside `D04 T01`'s Convert frame; converter preference setting is owned by `D02 T04 §5` and read here)

**Adjacency rationale:** The `PDF from Office, HTML, Markdown & Images` card, queue UI, and settings selector live in `D04 T01` and `D02 T04 §5`. This file owns the built-in OpenXML/RTF/HTML/Markdown/CSV engines, the optional COM and LibreOffice adapters, converter detection, and the macro/script-safety proofs.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Built-in C++20 Word (`.docx`, `.docm`) and Rich Text (`.rtf`) to PDF layout engine | D04 T02 §2, D04 T03 §1 | [ ] |
| 2 | §2 | Built-in C++20 Excel (`.xlsx`), `.csv`, and PowerPoint (`.pptx`) to PDF layout engines | §1 | [ ] |
| 3 | §3 | Built-in C++20 `HTML/CSS` (`.html`) and `Markdown` (`.md`) to PDF engine and CLI verbs | §1 | [ ] |
| 4 | §4 | Installed converter detection (`detect_office_converters()` for Office & LibreOffice) | §2 | [ ] |
| 5 | §5 | STA Office COM adapter (`office_com.cpp`) with `msoAutomationSecurityForceDisable` | §4 | [ ] |
| 6 | §6 | LibreOffice headless adapter (`office_libreoffice.cpp`) with profile & Win32 Job Object | §4 | [ ] |
| 7 | §7 | Convert **PDF from Office, HTML, Markdown & Images** card and Step 3 UI (`SPEC 9.5`) | §2, §3, §4, D04 T01 §4 | [ ] |
| 8 | §8 | Settings converter selector (`D02 T04 §5`) and `Install Office or LibreOffice` dialog | §5, §6, §7, D02 T04 §5, D02 T01 §10 | [ ] |

## 1. Built-in C++20 Word (`.docx`, `.docm`) and Rich Text (`.rtf`) to PDF layout engine

Implement the WordprocessingML (`.docx` / `.docm`) and Rich Text Format (`.rtf`) to PDF layout engine in `src/core/office_to_pdf.h` and `src/core/office_to_pdf.cpp` using `ZipReader` (`D04 T03 §1`), a bounded XML/RTF parser (enforcing entity-expansion caps and ignoring any `word/vbaProject.bin` macro streams by construction), DirectWrite text layout (`IDWriteTextLayout`), and `PdfWriter` (`D04 T02 §2`): parse page setup (`w:pgSz`, `w:pgMar`), styles (`word/styles.xml`), numbering/lists (`word/numbering.xml`), paragraphs, formatted runs, positioned frames (`w:framePr`), tables (`w:tbl` with merged cells, borders, and shading), headers/footers, and embedded images (`word/media/*`); paginate into vector PDF pages and emit `/Outlines` bookmarks from `Heading1`..`Heading3` paragraphs.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the `.docx`/`.docm` OpenXML reader and `.rtf` parser in `src/core/office_to_pdf.cpp` building an intermediate `FlowDocumentModel` (sections, headers/footers, paragraphs, styled spans, lists, tables, frames, and images) while ignoring `vbaProject.bin` and ActiveX parts. Done when: `OfficeToPdf.BuiltinDocxAndRtfParse` parses single-column, multi-column, table-bearing, and `.docm` macro-bearing fixtures cleanly under ASan.
2. Implement the DirectWrite pagination and `PdfWriter` emission pass (line breaking via `IDWriteTextLayout`, table row/page splitting with repeating header rows, header/footer rendering, image XObject embedding, and `/Outlines` bookmark tree generation). Done when: `OfficeToPdf.BuiltinDocxAndRtfRender` converts `.docx`, `.docm`, and `.rtf` files into multi-page vector PDFs whose extracted `PageText` preserves all source text under ASan.

- [ ] Define `OfficeToPdfOptions` (`engine_preference` `Auto`/`Builtin`/`MicrosoftOffice`/`LibreOffice`, `page_size_override`, `orientation_override`, `margin_preset`, `fit_spreadsheet_columns`, `generate_heading_bookmarks`, `combine_inputs_into_single_pdf`) in `src/core/office_to_pdf.h`. Done when: `src/core/office_to_pdf.h` compiles cleanly across `recto-core`, `recto-services`, and `recto-cli`.
- [ ] Parse `.docx` / `.docm` packages (`word/document.xml`, `word/styles.xml`, `word/numbering.xml`, `word/header*.xml`, `word/footer*.xml`, `word/_rels/document.xml.rels`, `word/media/*`) using `ZipReader` (`D04 T03 §1`), explicitly ignoring `word/vbaProject.bin`, `vbaData.xml`, and external relationship targets (`TargetMode="External"`). Done when: a `.docm` containing `word/vbaProject.bin` converts its document text while never reading or executing VBA macro bytecode. Cheaper substitute: requiring Microsoft Office or LibreOffice to be installed for `.docx` conversion.
- [ ] Parse RTF 1.9.1 `.rtf` documents (`\fonttbl`, `\colortbl`, `\stylesheet`, `\paperw`/`\paperh`, `\pard`, `\trowd`/`\cellx`/`\intbl`, `\posx`/`\posy` frames, `\pict\pngblip`/`\jpegblip`, and `\uN?` Unicode escapes) into the shared `FlowDocumentModel`. Done when: `.rtf` files produced by `D04 T03 §4`'s `RtfWriter` parse back into `FlowDocumentModel` with 100% text and table fidelity.
- [ ] Paginate paragraphs, lists, and multi-column sections (`w:cols`) using DirectWrite (`IDWriteTextFormat` and `IDWriteTextLayout`) to compute exact glyph positions, line heights, widow/orphan control, and page breaks, emitting vector text (`BT ... Tj/TJ ET`), underlines, highlights, and embedded fonts via `PdfWriter` (`D04 T02 §2`). Done when: `OfficeToPdf.BuiltinDocxTypography` verifies selectable vector text and font metrics on the output PDF.
- [ ] Layout and paginate `<w:tbl>` and RTF `\trowd` tables (calculating column widths from `w:tblGrid` / `\cellx`, handling `w:gridSpan` and `w:vMerge` merged cells, drawing cell background fills and border strokes, and splitting multi-page tables across page boundaries). Done when: `OfficeToPdf.BuiltinDocxTables` renders multi-row tables with borders and cell shading into vector PDF pages.
- [ ] Emit PDF `/Outlines` bookmark entries (`D01 T01 §10` format) pointing to the destination page and top Y coordinate of every `Heading1`, `Heading2`, and `Heading3` paragraph when `generate_heading_bookmarks == true`. Done when: opening the generated PDF in Recto populates the `Bookmarks` side panel (`D02 T03 §5`) with the document's heading tree.
- [ ] Add `OfficeToPdf.BuiltinDocx*` and `OfficeToPdf.BuiltinRtf*` unit tests in `tests/recto-tests/office_to_pdf_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=OfficeToPdf.BuiltinDocx*:OfficeToPdf.BuiltinRtf*` passes under ASan.

- [ ] Commit: built-in C++20 Word (`.docx`, `.docm`) and Rich Text (`.rtf`) to PDF layout engine (`src/core/office_to_pdf.cpp`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.BuiltinDocx*:OfficeToPdf.BuiltinRtf*` passes under ASan.

## 2. Built-in C++20 Excel (`.xlsx`), `.csv`, and PowerPoint (`.pptx`) to PDF layout engines

Extend `src/core/office_to_pdf.cpp` with native C++20 layout engines for **Excel (`.xlsx`, `.xlsm`) and `.csv`** and **PowerPoint (`.pptx`, `.pptm`)**, plus `recto-cli office-to-pdf <in.docx|xlsx|pptx|rtf|csv> --out <out.pdf> [--engine builtin|office|libreoffice]`:
- **Excel (`.xlsx` / `.xlsm`) & `.csv`:** Parse `xl/workbook.xml`, `xl/sharedStrings.xml`, `xl/styles.xml` (`numFmts`, `fonts`, `fills`, `borders`, `cellXfs`), and `xl/worksheets/sheet*.xml` (plus `<mergeCells>` and `<cols>`) or RFC 4180 `.csv`; format numeric, currency, percentage, and date cells; compute column widths and row heights; and paginate worksheets onto portrait or landscape PDF pages (with `Fit all columns on one page` scaling and sheet-name `/Outlines` bookmarks).
- **PowerPoint (`.pptx` / `.pptm`):** Parse `ppt/presentation.xml` (`p:sldSz` slide dimensions) and `ppt/slides/slide1..N.xml` (plus `slideLayouts` / `slideMasters` backgrounds, `<p:sp>` shapes, `<p:txBody>` DrawingML text frames, `<p:graphicFrame>` tables, and `<p:pic>` images); emit one vector PDF page per slide via `PdfWriter`.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the `.xlsx`/`.xlsm` and `.csv` spreadsheet-to-PDF layout engine and the `.pptx`/`.pptm` slide-to-PDF layout engine in `src/core/office_to_pdf.cpp` (ignoring any `xl/vbaProject.bin` or `ppt/vbaProject.bin` macro streams). Done when: `OfficeToPdf.BuiltinXlsxCsvAndPptx` converts corpus `.xlsx`, `.csv`, and `.pptx` files into valid multi-page vector PDFs under ASan.
2. Implement `recto-cli office-to-pdf <in> --out <out.pdf> [--engine builtin|office|libreoffice] [--fit-columns] [--landscape]` in `src/cli/cmd_office_to_pdf.cpp`. Done when: `Cli.OfficeToPdf` passes under ASan across `.docx`, `.rtf`, `.xlsx`, `.csv`, and `.pptx`.

- [ ] Parse ECMA-376 SpreadsheetML (`.xlsx` / `.xlsm`: `xl/workbook.xml`, `xl/sharedStrings.xml`, `xl/styles.xml`, `xl/worksheets/sheet*.xml`) and RFC 4180 `.csv` in `src/core/office_to_pdf.cpp`, ignoring `xl/vbaProject.bin`. Done when: `OfficeToPdf.BuiltinXlsxParse` reads shared strings, inline strings (`t="inlineStr"`), numeric/formula cached values (`<v>`), number formats (`numFmtId`), cell fills/borders, and `<mergeCells>`.
- [ ] Format Excel numeric, currency, percentage, and serial-date values (`1900` date system) according to `numFmtId` / custom format codes, and paginate worksheets onto PDF pages with repeating top header rows, gridlines/borders, and `fit_spreadsheet_columns` horizontal scaling so wide tables fit cleanly onto one page width without awkward 1-column orphan pages. Done when: `OfficeToPdf.BuiltinXlsxAndCsvRender` converts `.xlsx` (including files written by `D04 T05 §1`'s `XlsxWriter`) and `.csv` into vector PDFs whose `PageText` and `TableExtractor` readback match the source cells.
- [ ] Parse ECMA-376 PresentationML (`.pptx` / `.pptm`: `ppt/presentation.xml` `p:sldSz` in EMU, `ppt/slides/slide*.xml`, `ppt/slideLayouts/*`, `ppt/slideMasters/*`, and `ppt/media/*`), ignoring `ppt/vbaProject.bin`. Done when: `OfficeToPdf.BuiltinPptxParse` resolves slide dimensions, background fills, positioned shapes (`a:xfrm` `a:off` / `a:ext`), text bodies (`a:p` / `a:r`), tables (`a:tbl`), and images (`p:pic`).
- [ ] Render each `.pptx` slide to a PDF page matching the slide's aspect ratio (`16:9` widescreen or `4:3` standard in points: `EMU / 12700`), drawing vector shape geometries, solid/gradient fills, DirectWrite text frames, tables, and slide images via `PdfWriter`, plus a `/Outlines` bookmark per slide title. Done when: `OfficeToPdf.BuiltinPptxRender` converts a multi-slide `.pptx` into a multi-page vector PDF with selectable slide text and images. Cheaper substitute: failing on `.xlsx` and `.pptx` unless Microsoft Office is installed.
- [ ] Support `combine_inputs_into_single_pdf` in `office_to_pdf` so a mixed batch of `.docx`, `.xlsx`, `.pptx`, `.rtf`, and `.csv` files can be emitted into one combined PDF or individual PDFs, committed via `AtomicFileWriter` (`D04 T02 §1`). Done when: `OfficeToPdf.CombineMixedBatch` merges a `.docx`, `.xlsx`, and `.pptx` into one multi-page PDF.
- [ ] Implement `recto-cli office-to-pdf <in> --out <out.pdf> [--engine builtin|office|libreoffice] [--fit-columns] [--orientation auto|portrait|landscape]` in `src/cli/cmd_office_to_pdf.cpp`. Done when: `recto-tests.exe --gtest_filter=OfficeToPdf.Builtin*:Cli.OfficeToPdf` passes under ASan.
- [ ] Commit: `convert: implement built-in C++20 OpenXML, RTF, HTML/CSS, Markdown and CSV to PDF engines`

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

- [ ] Commit: built-in C++20 `HTML/CSS` (`.html`) and `Markdown` (`.md`) to PDF layout engine (`src/core/html_md_to_pdf.cpp`, `recto-cli html-to-pdf`, `recto-cli md-to-pdf`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=HtmlMdToPdf.*:Cli.HtmlAndMdToPdf` passes under ASan.

## 4. Installed converter detection (`detect_office_converters()` for Office & LibreOffice)

Implement `src/services/office_converter.h` and `src/services/office_converter.cpp`: define the `IOfficeConverter` adapter interface and `detect_office_converters()` probing the Windows registry and filesystem for installed Microsoft Office (`Word.Application`, `Excel.Application`, `PowerPoint.Application` COM CLSIDs and local server executables) and LibreOffice (`soffice.exe` via `HKLM\SOFTWARE\LibreOffice`, `App Paths\soffice.exe`, and standard `Program Files\LibreOffice\program\soffice.exe` locations), while always reporting Recto's **Built-in OpenXML engine** (`§1`-`§3`) as available, with injectable detection overrides for deterministic UI and service testing.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `OfficeConverterKind` (`Automatic`, `BuiltinOpenXml`, `MicrosoftOfficeCom`, `LibreOfficeHeadless`), `InstalledConvertersReport` (`builtin_available = true`, `ms_office_word_available`, `ms_office_excel_available`, `ms_office_powerpoint_available`, `libreoffice_available`, `libreoffice_soffice_path`), and `detect_office_converters()` in `src/services/office_converter.h/.cpp`. Done when: `OfficeToPdf.ConverterDetection` accurately probes the host machine and supports `set_converter_detection_override_for_testing` to simulate all 4 installation states (`BuiltinOnly`, `OfficeInstalled`, `LibreOfficeInstalled`, `BothInstalled`).

- [ ] Define `OfficeConverterKind`, `InstalledConvertersReport`, `IOfficeConverter`, and `select_active_converter(OfficeConverterKind preference, InputFileKind input_kind, const InstalledConvertersReport&)` in `src/services/office_converter.h`. Done when: `src/services/office_converter.h` compiles cleanly across `recto-services` and `src/app`.
- [ ] Probe Microsoft Office COM availability in `detect_office_converters()` by checking `CLSIDFromProgID(L"Word.Application")`, `L"Excel.Application"`, and `L"PowerPoint.Application"` and verifying the registered `LocalServer32` executable path exists on disk without launching the Office process during detection. Done when: calling `detect_office_converters()` completes in `< 5 ms` and spawns zero background processes.
- [ ] Probe LibreOffice availability by inspecting `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\soffice.exe`, `HKLM\SOFTWARE\LibreOffice\UNO\InstallPath`, and `%ProgramFiles%\LibreOffice\program\soffice.exe`, verifying `std::filesystem::exists(soffice_path)`. Done when: `detect_office_converters()` locates `soffice.exe` when installed and reports `false` otherwise.
- [ ] Implement `select_active_converter` resolution rules: when `preference == BuiltinOpenXml`, use the built-in engine (`§1`-`§3`) for `.docx`, `.docm`, `.xlsx`, `.xlsm`, `.pptx`, `.pptm`, `.rtf`, `.html`, `.md`, `.csv`, `.xml`, `.txt` and return `NeedsConverter` only for legacy binary `.doc`, `.xls`, `.ppt` OLE2 files; when `preference == Automatic`, use the built-in engine by default for modern OpenXML/RTF/HTML/Markdown/CSV (or Microsoft Office COM / LibreOffice automatically when converting legacy binary `.doc`/`.xls`/`.ppt` if installed); and when `preference == MicrosoftOfficeCom` or `LibreOfficeHeadless`, use that suite if installed or return `JobState::NeedsConverter` (`D04 T01 §2`) if the explicitly forced suite is not installed. Done when: `OfficeToPdf.StrategySelection` verifies all combinations of user preference, file extension, and installed suites. Cheaper substitute: failing all Office conversions when Microsoft Office is absent even though the built-in engine is available.
- [ ] Provide `set_converter_detection_override_for_testing(std::optional<InstalledConvertersReport>)` so automated tests in `§8` and `D02 T04 §5` can test every UI state deterministically on any machine. Done when: `OfficeToPdf.ConverterDetection` tests all 4 simulated host states.
- [ ] Add `OfficeToPdf.ConverterDetection` and `OfficeToPdf.StrategySelection` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=OfficeToPdf.ConverterDetection:OfficeToPdf.StrategySelection` passes under ASan.

- [ ] Commit: installed converter detection (`detect_office_converters()` probing Microsoft Office CLSIDs and LibreOffice `soffice.exe`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.ConverterDetection:OfficeToPdf.StrategySelection` passes under ASan.

## 5. STA Office COM adapter (`office_com.cpp`) with `msoAutomationSecurityForceDisable`

Implement the STA worker-thread Microsoft Office COM adapter (`src/services/office_com.h/.cpp`) per foundation plan section 5.1 and security rule 3:
- Initialize a dedicated Single-Threaded Apartment (`CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)`) on the conversion worker thread.
- Instantiate `Word.Application`, `Excel.Application`, or `PowerPoint.Application` via `IDispatch`, set `Visible = VARIANT_FALSE`, `DisplayAlerts = 0`, and **`AutomationSecurity = msoAutomationSecurityForceDisable` (`3`)** before opening any file.
- Open the document with `ReadOnly = VARIANT_TRUE` and `AddToRecentFiles = VARIANT_FALSE`, export to a sibling temporary PDF path (`ExportAsFixedFormat` `wdExportFormatPDF = 17` / `xlTypePDF = 0` / `ppSaveAsPDF = 32`), commit via `AtomicFileWriter` (`D04 T02 §1`), close the document without saving changes, call `Quit()` on the spawned Office instance, and verify under a `macro-canary.docm` test fixture that `AutoOpen` / `Document_Open` VBA macros never execute.

**Phase:** 5
**Needs:** windows-host, office
**Surface:** no

**Build order:**

1. Implement `OfficeComConverter` in `src/services/office_com.cpp` driving Word, Excel, and PowerPoint `IDispatch` automation with `AutomationSecurity = 3` (`msoAutomationSecurityForceDisable`), RAII `Quit()` + process-handle watchdog cleanup on cancel/timeout, and `AtomicFileWriter` commit. Done when: on an Office-equipped host, `.docx`, `.xlsx`, `.pptx`, and `.rtf` convert to PDF via `--engine office` with zero orphaned `WINWORD.EXE` / `EXCEL.EXE` / `POWERPNT.EXE` processes after completion or cancellation.
2. Generate `tests/corpus/convert/macro-canary.docm` (a Word macro-enabled document containing a `word/vbaProject.bin` with an `AutoOpen` / `Document_Open` routine that attempts to write a canary file in `TempDir`) and run `OfficeToPdf.ComMacroBlocked`. Done when: the PDF is produced cleanly and the canary file does **not** exist on disk.

- [ ] Implement RAII STA COM apartment guard (`ScopedComApartment`) and `IDispatch` property/method invocation helpers (`disp_put_bool`, `disp_put_i4`, `disp_invoke`) in `src/services/office_com.cpp`. Done when: `office_com.cpp` compiles with zero external Office PIA/type-library SDK dependencies.
- [ ] Set `AutomationSecurity = 3` (`msoAutomationSecurityForceDisable`), `Visible = VARIANT_FALSE`, and `DisplayAlerts = 0` (`wdAlertsNone` / `ppAlertsNone`) on the Office `Application` object **before** accessing `Documents` / `Workbooks` / `Presentations` or calling `Open`, and verify `disp_get_i4(app, L"AutomationSecurity") == 3` before proceeding (aborting the job immediately if setting `AutomationSecurity` fails). Done when: `OfficeToPdf.ComSecurityPrecheck` verifies `AutomationSecurity == 3` is asserted before any file open call. Cheaper substitute: leaving `AutomationSecurity` at its default value or opening the file before setting it.
- [ ] Implement Word (`Documents.Open` with `ReadOnly=true`, `AddToRecentFiles=false`, `NoEncodingDialog=true` -> `Document.ExportAsFixedFormat(temp_pdf, 17)` -> `Document.Close(0)` -> `Application.Quit(0)`), Excel (`Workbooks.Open` -> `Workbook.ExportAsFixedFormat(0, temp_pdf)` -> `Close(false)` -> `Quit()`), and PowerPoint (`Presentations.Open` with `ReadOnly=msoTrue`, `WithWindow=msoFalse` -> `Presentation.SaveAs(temp_pdf, 32)` -> `Close()` -> `Quit()`). Done when: `OfficeToPdf.ComRoundTrip` converts `.docx`, `.xlsx`, `.pptx`, and `.rtf` to valid PDFs via `AtomicFileWriter`.
- [ ] Monitor `CancelToken` and a configurable per-document timeout (`120 s` default); if cancelled or timed out, close/quit the COM server (and terminate the specific spawned PID if hung) and unlink the temporary output via `AtomicFileWriter`. Done when: `OfficeToPdf.ComCancelCleanup` leaves zero orphaned Office processes or temp files.
- [ ] Build the `macro-canary.docm` test fixture (`scripts/build-corpus.py`) and add `OfficeToPdf.ComMacroBlocked` verifying that converting `macro-canary.docm` through `OfficeComConverter` produces the expected PDF while the canary file path remains absent on disk. Done when: `OfficeToPdf.ComMacroBlocked` passes on the Office-equipped host.
- [ ] Add `OfficeToPdf.Com*` tests in `tests/recto-tests/office_to_pdf_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=OfficeToPdf.Com*` passes.
- [ ] Commit: `convert: add Microsoft Office COM adapter with AutomationSecurity forced off`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.Com*` passes on the Office-equipped host, confirming PDF output and zero canary file creation from `macro-canary.docm`.

## 6. LibreOffice headless adapter (`office_libreoffice.cpp`) with profile & Win32 Job Object

Implement the optional LibreOffice headless adapter (`src/services/office_libreoffice.h/.cpp`) per foundation plan section 5.1 and security rule 5: invoke `soffice.exe --headless --norestore --nolockcheck --nodefault -env:UserInstallation=<isolated-temp-file-url> --convert-to pdf --outdir <temp_dir> <input_path>` using `CreateProcessW` directly (never `cmd.exe`, `powershell.exe`, or `std::system()`), assign the process to a Win32 Job Object configured with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, monitor `CancelToken` and a timeout, and commit the resulting PDF through `AtomicFileWriter` (`D04 T02 §1`).

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `LibreOfficeConverter` in `src/services/office_libreoffice.cpp` with `CreateProcessW` (`CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_BREAKAWAY_FROM_JOB`), Win32 Job Object (`CreateJobObjectW`, `SetInformationJobObject` `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, `AssignProcessToJobObject`, `ResumeThread`), per-job isolated `TempDir` `UserInstallation` URI (`file:///...`), `CancelToken` / timeout wait loop (`WaitForSingleObject`), and `AtomicFileWriter` commit. Done when: unit tests using a controllable child process stub (and real `soffice.exe` when installed) prove that tripping `CancelToken` terminates the child process tree immediately and cleans up the temporary profile and output directory under ASan.

- [ ] Implement `build_libreoffice_command_line` in `src/services/office_libreoffice.cpp` quoting executable and file arguments safely per Win32 `CommandLineToArgvW` rules and constructing an isolated `file:///` `UserInstallation` profile URL inside a per-job `TempDir` so concurrent jobs or an already-open desktop LibreOffice instance never deadlock on profile locks. Done when: `OfficeToPdf.LibreOfficeCommandLine` verifies safe quoting on paths containing spaces, quotes, and `&`/`|` shell metacharacters.
- [ ] Write a pre-configured `registrymodifications.xcu` into the temporary `UserInstallation/user/` profile setting `MacroSecurityLevel` to `3` (Very High / disabled) as defense-in-depth alongside `--headless`. Done when: `OfficeToPdf.LibreOfficeMacroConfig` verifies `MacroSecurityLevel == 3` in the generated profile before process launch.
- [ ] Launch the converter process via `CreateProcessW(soffice_path.c_str(), cmd_line.data(), ...)` with `lpApplicationName` explicitly set (never invoking `cmd.exe` or `system()`), attach it to a `ScopedJobObject` with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, and resume the thread. Done when: closing the `ScopedJobObject` handle terminates the child process and any grandchildren immediately. Cheaper substitute: calling `std::system()` or using the user's default `%APPDATA%\LibreOffice` profile.
- [ ] Poll `WaitForSingleObject(pi.hProcess, 50)` in a loop checking `CancelToken::is_cancelled()` and the per-job timeout; on cancel or timeout, call `TerminateJobObject(job_handle, 1)` and delete the temporary profile and output directory. Done when: `OfficeToPdf.LibreOfficeCancelAndIsolation` spawns a long-running stub process, trips `CancelToken` after `100 ms`, and verifies the child PID is terminated within `< 50 ms` and zero temp files remain.
- [ ] Move the produced `<temp_dir>/<stem>.pdf` into the target destination via `AtomicFileWriter` (`D04 T02 §1`). Done when: `OfficeToPdf.LibreOfficeCommit` verifies atomic output placement.
- [ ] Add `OfficeToPdf.LibreOffice*` tests in `tests/recto-tests/office_to_pdf_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=OfficeToPdf.LibreOffice*` passes under ASan.
- [ ] Commit: `convert: add LibreOffice headless adapter with Job Object process isolation`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.LibreOffice*` passes under ASan.

## 7. Convert **PDF from Office, HTML, Markdown & Images** card and Step 3 UI (`SPEC 9.5`)

Register the **PDF from Office, HTML, Markdown & Images** card in `ConverterRegistry` (`D04 T01 §3`) and build its Step 3 options control (`src/app/views/convert/OfficeToPdfOptionsControl.xaml/.cpp`, `SPEC 9.5`): page size (`Match original document`, `A4`, `Letter`), orientation (`Auto`, `Portrait`, `Landscape`), margins (`Default`, `Narrow`, `None`), `Fit all spreadsheet columns on one page` toggle, `Generate PDF bookmarks from headings and slide titles` toggle, `Combine all input files into a single PDF` toggle, and the SPEC Principle 4 honest capability hint line.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.5 (`PDF from Office, HTML, Markdown & Images` card enable/disable rule and Step 3 options) with `D-Convert.png`.
**Job:** The user can drop `.docx`, `.xlsx`, `.pptx`, `.rtf`, `.html`, `.md`, `.csv`, or image files into the Convert page, configure page setup, spreadsheet column fitting, bookmark generation, and single-PDF combining, and convert them to PDF through the queue. Consumer: `ConvertPage` (`D04 T01 §3`-`§4`), `ConversionRunner` (`D04 T01 §1`), and `office_to_pdf` / `html_md_to_pdf` / `images_to_pdf`.
**Treatment:** Step 3 options control bound to `OfficeToPdfOptionsViewModel`, dispatching `.docx`/`.xlsx`/`.pptx`/`.rtf`/`.csv` to `IOfficeConverter` (`§1`, `§2`, `§5`, `§6`), `.html`/`.md` to `html_md_to_pdf` (`§3`), and images/SVG/XPS to `images_to_pdf` (`D04 T02 §5`), optionally merging all outputs into a single combined PDF when `Combine all input files into a single PDF` is checked. Cheaper substitute that fails the checkpoint: disabling `PDF from Office` when Microsoft Office is not installed, or omitting spreadsheet column fitting and heading bookmark toggles.
**Chrome:** consume the Convert Step 2 card and Step 3 options host from `D04 T01 §3`-`§4`.

**Build order:**

1. Implement `OfficeToPdfOptionsViewModel` and `OfficeToPdfOptionsControl.xaml/.cpp` (`SPEC 9.5`) and register the **PDF from Office, HTML, Markdown & Images** card in `ConverterRegistry`. Done when: loading `.docx`, `.xlsx`, `.pptx`, `.rtf`, `.html`, `.md`, `.csv`, or image files in Step 1 enables the card and renders the Step 3 options control.
2. Wire the job factory to dispatch each input file to the selected converter engine and merge outputs when `Combine all input files into a single PDF` is enabled. Done when: `OfficeToPdf.ConvertPageUi` converts `.docx`, `.xlsx`, `.pptx`, `.rtf`, `.html`, and `.md` files through the Convert page on a machine with zero external suites using the built-in C++20 engine.

- [ ] Register the **PDF from Office, HTML, Markdown & Images** card in `ConverterRegistry` (`D04 T01 §3`), enabled whenever Step 1 contains at least one `.docx`, `.docm`, `.doc`, `.xlsx`, `.xlsm`, `.xls`, `.pptx`, `.pptm`, `.ppt`, `.rtf`, `.html`, `.htm`, `.md`, `.csv`, `.xml`, `.txt`, `.png`, `.jpg`, `.jpeg`, `.tif`, `.tiff`, `.bmp`, `.gif`, `.svg`, or `.xps` file, with disable reason `"Add an Office, HTML, Markdown, CSV, or image file to convert to PDF"` when only PDFs are loaded. Done when: `OfficeToPdf.CardEnablement` verifies enablement and disable reasons.
- [ ] Build `OfficeToPdfOptionsControl` (`src/app/views/convert/OfficeToPdfOptionsControl.xaml/.cpp`) exposing `Page size` (`Original`, `A4`, `Letter`), `Orientation` (`Auto`, `Portrait`, `Landscape`), `Margins` (`Default`, `Narrow`, `None`), `Fit all spreadsheet columns on one page` (`true`), `Generate PDF bookmarks from headings` (`true`), and `Combine all files into one PDF` (`false` for single input, toggleable for `>= 2` inputs). Done when: all controls pass `D03 T02 §3` accessibility checks.
- [ ] Render the SPEC Principle 4 honest capability hint line (`"Uses Recto's built-in OpenXML, HTML & Markdown engine out of the box, or Microsoft Office / LibreOffice when selected in Settings."`). Done when: the hint line is visible in Step 3.
- [ ] Dispatch queued jobs according to `SettingsModel::office_to_pdf_converter()` (`D02 T04 §5`) and §4's `select_active_converter`. Done when: `OfficeToPdf.ConvertPageUi` runs `.docx`, `.xlsx`, `.pptx`, `.rtf`, `.html`, and `.md` conversions through `ConversionRunner` and verifies the produced PDFs. Cheaper substitute that fails the checkpoint: hard-coding one converter path and ignoring Step 3 options.
- [ ] Support `Open file` in `QueueCard` and completion toasts (`D04 T01 §6`) so clicking `Open file` on a finished `Office -> PDF` job opens the newly created PDF directly in a Recto viewer tab (`D02 T01 §9`). Done when: clicking `Open file` opens the converted PDF in a new tab.
- [ ] Add `OfficeToPdf.CardEnablement` and `OfficeToPdf.ConvertPageUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=OfficeToPdf.CardEnablement:OfficeToPdf.ConvertPageUi` passes.

- [ ] Commit: convert page **PDF from Office, HTML, Markdown & Images** card and Step 3 options UI (`SPEC 9.5`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.CardEnablement:OfficeToPdf.ConvertPageUi` passes.

## 8. Settings converter selector (`D02 T04 §5`) and `Install Office or LibreOffice` dialog

Connect §4's `detect_office_converters()` live detection state to the `Office to PDF converter` selector in Settings (`D02 T04 §5`: `Automatic (Recommended)`, `Built-in OpenXML engine (Always available)`, `Microsoft Office`, `LibreOffice` with installed/not-installed status subtitles), and build the **`Install Office or LibreOffice` dialog** (`SPEC 9.9`, `D-Dialogs.png` bottom-left, hosted via `D02 T01 §10`) shown when a user explicitly forces an external suite that is not installed or attempts to convert a legacy binary OLE2 `.doc`/`.xls`/`.ppt` file on a machine without Microsoft Office or LibreOffice, offering a one-click `Use built-in converter` fallback for OpenXML/RTF/HTML/Markdown files or `Open Settings` / `Dismiss`.

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 9.7 (Converting group: `Office to PDF converter` selector) and 9.9 (`Install Office or LibreOffice` dialog) with `design/screenshots/D-Dialogs.png`.
**Job:** The user can see in Settings which external suites are detected on their PC, choose their preferred converter engine, and get a clear, actionable recovery dialog if a job requires an external suite that is not installed. Consumer: Settings page (`D02 T04 §5`), `QueueCard` `NeedsConverter` recovery hook (`D04 T01 §7`), and `ConvertPage` (`§7`).
**Treatment:** Live bindings between `detect_office_converters()` and `SettingsViewModel` (`D02 T04 §5`), plus `MissingConverterDialog` (`src/app/dialogs/MissingConverterDialog.xaml/.cpp`) registered as the `IMissingConverterRecoveryHandler` in `D04 T01 §7`. Cheaper substitute that fails the checkpoint: failing silently in the queue when an external suite is missing, or showing a raw COM `REGDB_E_CLASSNOTREG` HRESULT to the user.
**Chrome:** consume the Settings row from `D02 T04 §5`, the `NeedsConverter` hook from `D04 T01 §7`, and the shared `ContentDialog` host from `D02 T01 §10`.

**Build order:**

1. Wire `detect_office_converters()` into `SettingsViewModel` (`D02 T04 §5`) so the `Office to PDF converter` dropdown shows live detection badges (`"Detected"` / `"Not installed"`) for Microsoft Office and LibreOffice, and implement `MissingConverterDialog` (`src/app/dialogs/MissingConverterDialog.xaml/.cpp`) matching `D-Dialogs.png`. Done when: `OfficeToPdf.SettingsAndMissingDialogUi` drives all three detection states (`BuiltinOnly`, `OfficeInstalled`, `LibreOfficeInstalled`) via `set_converter_detection_override_for_testing`.
2. Register `MissingConverterDialog` with `D04 T01 §7`'s `NeedsConverter` recovery hook so clicking `Use built-in converter` resubmits the queued job in place (`NeedsConverter -> Running -> Done`) using Recto's built-in engine. Done when: `OfficeToPdf.MissingSuiteFallbackFlow` passes end-to-end and the dialog screenshot matches `D-Dialogs.png`.

- [ ] Bind `detect_office_converters()` (`§4`) to `SettingsViewModel` (`D02 T04 §5`) displaying the cuatro options (`Automatic`, `Built-in OpenXML engine`, `Microsoft Office`, `LibreOffice`) with live installation status (`"Always available"`, `"Detected on this PC"`, or `"Not installed"`). Done when: `OfficeToPdf.SettingsDetectionBinding` verifies option labels across simulated detection states.
- [ ] Build `MissingConverterDialog` (`src/app/dialogs/MissingConverterDialog.xaml/.cpp`, hosted via `D02 T01 §10`) matching `D-Dialogs.png` (`"Office converter needed"`, body explaining that Microsoft Office or LibreOffice is not installed, primary button `Use built-in converter` when the file is OpenXML/RTF/HTML/MD/CSV, secondary button `Open Settings`, and `Cancel`). Done when: the rendered dialog matches `D-Dialogs.png`. Cheaper substitute that fails the checkpoint: displaying an unhandled COM error message instead of the SPEC 9.9 dialog.
- [ ] Register `MissingConverterDialog` as the `IMissingConverterRecoveryHandler` for `D04 T01 §7` so when a job enters `JobState::NeedsConverter` (or before submission when an unavailable suite is forced), the dialog opens and clicking `Use built-in converter` calls `ConversionRunner::resubmit_with_builtin_engine(job_id)` (`D04 T01 §2`). Done when: `OfficeToPdf.MissingSuiteFallbackFlow` forces `MicrosoftOfficeCom` with simulated `OfficeInstalled = false`, triggers `NeedsConverter`, clicks `Use built-in converter` in `MissingConverterDialog`, and completes the job in place.
- [ ] Handle legacy binary `.doc`, `.xls`, and `.ppt` files when neither Microsoft Office nor LibreOffice is installed by explaining in `MissingConverterDialog` that legacy 97-2003 binary formats require Microsoft Office or LibreOffice (while `.docx`, `.xlsx`, `.pptx`, and `.rtf` work with the built-in converter). Done when: `OfficeToPdf.LegacyBinaryMessage` verifies the dialog text on a `.doc` input when no external suite is installed.
- [ ] Verify all three host states (`Built-in`, `Office COM`, and `Missing-suite dialog`) in `OfficeToPdf.Ui*` automated tests. Done when: `recto-tests.exe --gtest_filter=OfficeToPdf.Ui*:OfficeToPdf.Settings*:OfficeToPdf.Missing*` passes.
- [ ] Commit: `convert: wire Office, HTML and Markdown to PDF UI, Settings selector and missing-suite dialog`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.*:HtmlMdToPdf.*` passes, and display-session screenshots confirm the `PDF from Office, HTML, Markdown & Images` card states and the `D-Dialogs.png` missing-converter dialog.

## Verification

- M5 Office/HTML/Markdown-to-PDF obligations (`docs/plans/2026-10-13-recto-foundation.md` section 7): `.docx`, `.xlsx`, `.pptx`, `.rtf`, `.html`, `.md`, and `.csv` convert cleanly to PDF via the built-in C++20 engine and via Microsoft Office COM when installed, and `macro-canary.docm` is proven not to execute its macro.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
