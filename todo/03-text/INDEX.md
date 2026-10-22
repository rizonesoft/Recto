# 03 Text

> **Phase 3 (M3 Text, OCR, Annotations, Forms and Accessibility)**

Making page text, annotations and interactive forms usable on both digital and scanned documents: native extraction from `recto-pdf` into one shared `PageText` model, built-in High-Accuracy In-Process C++23 Neural OCR + `Windows.Media.Ocr` recognition (with deskew, Sauvola binarization, morphological line removal, recursive XY-cut/table layout segmentation, and per-character CTC quad alignment) for scanned pages and vector-outlined text, selection and copy, find with regex, content-stream Find and Replace (`Ctrl+H`), visual and text PDF Comparison (`Ctrl+Shift+D`), full interactive annotations and markup (`Ctrl+Shift+A`), AcroForm and XFA-to-AcroForm filling and visual Form Designer, and the accessibility layer (UIA text provider, keyboard order audit, announcements) that makes all of it operable without sight. Every surface is built from `recto-ui` controls (`todo/10-ui/`) and the accessibility layer sits on the `recto-ui` UI Automation provider tree; 2026-10-22: WinUI 3 dropped for `recto-ui` (foundation plan D4), sections rewritten against `todo/10-ui/`.

## TODOs

| TODO | Title | Status |
| ---- | ----- | :----: |
| [TODO-01](./TODO-01-selection-and-search.md) | Text Extraction, Built-in OCR, Selection and Search | active |
| [TODO-02](./TODO-02-accessibility.md) | Accessibility: UIA Text, Keyboard Order and Announcements | active |
| [TODO-03](./TODO-03-annotations-and-forms.md) | Interactive Annotations, Markup, AcroForms and Form Designer | active |
| [TODO-04](./TODO-04-edit-pdf.md) | Edit PDF: Text, Image and Vector Object Editing | active |
| [TODO-05](./TODO-05-multi-file-search.md) | Search Across Files and Local Index | active |
| [TODO-06](./TODO-06-attachment-pin.md) | Attachment Pin Markup Tool | active |
| [TODO-07](./TODO-07-command-box-sources.md) | Command Box Rows for Comments, Form Fields and Indexed Files | active |

## Completed

| TODO | Title | Completed |
| ---- | ----- | :-------: |

(none yet)

## In scope

- Native `recto-pdf` text extraction and the `PageText` model (blocks, lines, words, character quads, reading order)
- Built-in High-Accuracy C++23 Neural OCR + `Windows.Media.Ocr` (`OcrPipeline`) for scanned pages and vector-outlined text with image preprocessing (deskew, contrast, Sauvola thresholding, morphological ruling-line removal, XY-cut/table layout segmentation, and CTC per-character quad alignment) populating `PageText` transparently
- Selection rendering, floating selection toolbar, `ui::Menu` context menu, `ui::Clipboard` copy, page links
- Find engine (including Regex), content-stream Find and Replace (`Ctrl+H`), and `Compare PDFs` (`Ctrl+Shift+D`) side-by-side and visual diff overlay view
- All 15 PDF annotation and markup types (`src/pdf/annotations.cpp`), `/AP` appearance synthesis, flattening, `XFDF`/`JSON` I/O, `Markup & Annotation Bar` (`Ctrl+Shift+A`), and `Comments / Annotations` side panel
- AcroForm and XFA-to-AcroForm engine (`src/pdf/forms.cpp`), `/CO` calculations, `JSON`/`FDF`/`XFDF`/`XML` data I/O, on-canvas interactive form filling, and `Form Designer` mode
- `Attachment Pin` markup tool (`FileAttachment` pins with ask-before-open and sanitized extraction)
- Command box (`Ctrl+K`) `Go to` rows for comments and form fields and `Files` rows for indexed files
- UIA text pattern on `ui::TextRangeAdapter`, form/annotation UIA providers, keyboard order audit on `ui::FocusManager`, announcements

## Out of scope

- Text file and searchable-PDF file export as batch conversion jobs (owned by 04-convert over §1 and §4 of TODO-01 here)

---

Format spec: [../README.md](../README.md) · Root index: [../TODO-00-INDEX.md](../TODO-00-INDEX.md)
