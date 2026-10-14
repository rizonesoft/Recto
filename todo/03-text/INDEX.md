# 03 Text

> **Phase 3 (M3 Text, OCR, Annotations, Forms and Accessibility)**

Making page text, annotations and interactive forms usable on both digital and scanned documents: native extraction from `recto-pdf` into one shared `PageText` model, built-in `Windows.Media.Ocr` recognition for scanned pages and vector-outlined text, selection and copy, find with regex, content-stream Find and Replace (`Ctrl+H`), visual and text PDF Comparison (`Ctrl+Shift+D`), full interactive annotations and markup (`Ctrl+Shift+A`), AcroForm and XFA-to-AcroForm filling and visual Form Designer, and the accessibility layer (UIA text provider, keyboard order audit, announcements) that makes all of it operable without sight.

## TODOs

| TODO | Title | Status |
| ---- | ----- | :----: |
| [TODO-01](./TODO-01-selection-and-search.md) | Text Extraction, Built-in OCR, Selection and Search | active |
| [TODO-02](./TODO-02-accessibility.md) | Accessibility: UIA Text, Keyboard Order and Announcements | active |
| [TODO-03](./TODO-03-annotations-and-forms.md) | Interactive Annotations, Markup, AcroForms and Form Designer | active |

## Completed

| TODO | Title | Completed |
| ---- | ----- | :-------: |

(none yet)

## In scope

- Native `recto-pdf` text extraction and the `PageText` model (blocks, lines, words, character quads, reading order)
- Built-in `Windows.Media.Ocr` (`OcrPipeline`) for scanned pages and vector-outlined text with image preprocessing (deskew, contrast, adaptive thresholding) populating `PageText` transparently
- Selection rendering, floating selection toolbar, context menu, clipboard, page links
- Find engine (including Regex), content-stream Find and Replace (`Ctrl+H`), and `Compare PDFs` (`Ctrl+Shift+D`) side-by-side and visual diff overlay view
- All 15 PDF annotation and markup types (`src/pdf/annotations.cpp`), `/AP` appearance synthesis, flattening, `XFDF`/`JSON` I/O, `Markup & Annotation Bar` (`Ctrl+Shift+A`), and `Comments / Annotations` side panel
- AcroForm and XFA-to-AcroForm engine (`src/pdf/forms.cpp`), `/CO` calculations, `JSON`/`FDF`/`XFDF`/`XML` data I/O, on-canvas interactive form filling, and `Form Designer` mode
- UIA text pattern, form/annotation accessibility peers, keyboard order audit, announcements

## Out of scope

- Text file and searchable-PDF file export as batch conversion jobs (owned by 04-convert over §1 and §4 of TODO-01 here)

---

Format spec: [../README.md](../README.md) · Root index: [../TODO-00-INDEX.md](../TODO-00-INDEX.md)
