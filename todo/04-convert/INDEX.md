# 04 Convert

> **Phases 4 and 5 (M4 Conversions A, M5 Conversions B)**

One job frame for every conversion, then Recto's native C++20 converters: `PdfWriter` and atomic saves, PDF to images, images to PDF, PDF to text, and Searchable PDF (OCR) in M4; native C++20 `DocxWriter` (`PDF -> Word .docx`) with its 20-document quality review and built-in C++20 OpenXML/RTF `Office -> PDF` engine (plus optional Microsoft Office COM with macros off or LibreOffice) in M5. Honest conversion (SPEC principle 4) is this domain's standing rule.

## TODOs

| TODO | Title | Status |
| ---- | ----- | :----: |
| [TODO-01](./TODO-01-jobs-and-queue.md) | Conversion Jobs and the Queue | active |
| [TODO-02](./TODO-02-pdf-conversions-a.md) | Conversions A: PDF Writer, Images, Text and Searchable OCR PDF | active |
| [TODO-03](./TODO-03-pdf-to-word.md) | PDF to Word (DOCX): Native OpenXML Writer and Quality Review | active |
| [TODO-04](./TODO-04-office-to-pdf.md) | Office to PDF: Built-in OpenXML/RTF Engine, Office COM and LibreOffice | active |

## Completed

| TODO | Title | Completed |
| ---- | ----- | :-------: |

(none yet)

## In scope

- Isolated job runtime (`JobQueue`), the three-step Convert page, the queue card, completion notices
- Native `PdfWriter`, atomic saves, Searchable PDF (OCR) invisible-text sandwich export, and all conversions of decision D7
- Native C++20 `DocxWriter` (`PDF -> Word`) and built-in C++20 OpenXML/RTF `Office -> PDF` engine plus optional COM/LibreOffice adapters

## Out of scope

- Page-level interactive authoring UX (`05-organize`, over the shared `PdfWriter` and `GraftMap` operations)
- Bundling LibreOffice (refused by D10)

---

Format spec: [../README.md](../README.md) · Root index: [../TODO-00-INDEX.md](../TODO-00-INDEX.md)
