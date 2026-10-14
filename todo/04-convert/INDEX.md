# 04 Convert

> **Phases 4 and 5 (M4 Conversions A, M5 Conversions B)**

One job frame for every conversion, then Recto's native C++20 converters: `PdfWriter` and atomic saves, PDF to images, multipage TIFF, animated GIF, vector SVG and OpenXPS, images/SVG/XPS to PDF, PDF to text, Searchable PDF (OCR), Table and Image Extraction, and PDF Optimization/Linearization in M4; 3-mode native C++20 `DocxWriter` and `RtfWriter` (`PDF -> Word .docx & .rtf`) with its 20-document quality review, built-in C++20 OpenXML/RTF/HTML/Markdown/CSV `-> PDF` engine (plus optional Microsoft Office COM with macros off or LibreOffice), `XlsxWriter` (`PDF -> Excel .xlsx, .csv, .xml`), `WebDocWriter` (`PDF -> HTML5, Markdown, EPUB 3, LaTeX`), and `ComplianceEngine` (`PDF/A-1/2/3 a/b/u`, `PDF/UA-1`, `ZUGFeRD / Factur-X`) in M5. Honest conversion (SPEC principle 4) is this domain's standing rule.

## TODOs

| TODO | Title | Status |
| ---- | ----- | :----: |
| [TODO-01](./TODO-01-jobs-and-queue.md) | Conversion Jobs and the Queue | active |
| [TODO-02](./TODO-02-pdf-conversions-a.md) | Conversions A: PDF Writer, Images/SVG/XPS, Text, OCR, Table/Image Extraction and Optimization | active |
| [TODO-03](./TODO-03-pdf-to-word.md) | PDF to Word (DOCX and RTF): 3-Mode Native OpenXML/RTF Writer and Quality Review | active |
| [TODO-04](./TODO-04-office-to-pdf.md) | Office, HTML and Markdown to PDF: Built-in C++20 Engine, Office COM and LibreOffice | active |
| [TODO-05](./TODO-05-excel-web-and-compliance.md) | PDF to Excel/CSV/XML, HTML5/Markdown/EPUB/LaTeX, and PDF/A, PDF/UA & ZUGFeRD Compliance | active |

## Completed

| TODO | Title | Completed |
| ---- | ----- | :-------: |

(none yet)

## In scope

- Isolated job runtime (`JobQueue`), the three-step Convert page, the queue card, completion notices
- Native `PdfWriter`, atomic saves, Searchable PDF (OCR) invisible-text sandwich export, `TableExtractor` and `ImageExtractor` (plus the Viewer `Table & Image Extractor Bar`), `PdfOptimizer`, font subsetter, and Fast Web View `Linearizer`
- 3-mode (`Flowing`, `Exact`, `Continuous`) native C++20 `DocxWriter` and `RtfWriter` (`PDF -> Word .docx & .rtf`) and built-in C++20 OpenXML/RTF/HTML/Markdown/CSV `-> PDF` engine plus optional COM/LibreOffice adapters
- Native C++20 `XlsxWriter` (`PDF -> .xlsx, .csv, .xml`), `WebDocWriter` (`PDF -> HTML5, .md, .epub, .tex`), and `ComplianceEngine` (`PDF/A`, `PDF/UA-1`, `ZUGFeRD / Factur-X` + Viewer `Standards & E-Invoice` side panel)

## Out of scope

- Page-level interactive authoring UX (`05-organize`, over the shared `PdfWriter` and `GraftMap` operations)
- Bundling LibreOffice (refused by D10)

---

Format spec: [../README.md](../README.md) · Root index: [../TODO-00-INDEX.md](../TODO-00-INDEX.md)
