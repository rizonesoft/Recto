# 03 Text

> **Phase 3 (M3 Text, OCR and Accessibility)**

Making page text usable on both digital and scanned documents: native extraction from `recto-pdf` into one shared `PageText` model, built-in `Windows.Media.Ocr` recognition for scanned pages, selection and copy, find with true counts, and the accessibility layer (UIA text provider, keyboard order audit, announcements) that makes all of it operable without sight.

## TODOs

| TODO | Title | Status |
| ---- | ----- | :----: |
| [TODO-01](./TODO-01-selection-and-search.md) | Text Extraction, Built-in OCR, Selection and Search | active |
| [TODO-02](./TODO-02-accessibility.md) | Accessibility: UIA Text, Keyboard Order and Announcements | active |

## Completed

| TODO | Title | Completed |
| ---- | ----- | :-------: |

(none yet)

## In scope

- Native `recto-pdf` text extraction and the `PageText` model (blocks, lines, words, character quads, reading order)
- Built-in `Windows.Media.Ocr` (`OcrPipeline`) for scanned pages with image preprocessing (deskew, contrast, adaptive thresholding) populating `PageText` transparently
- Selection rendering, floating selection toolbar, context menu, clipboard, page links
- Find engine and card in both SPEC forms, UIA text pattern, announcements

## Out of scope

- Text file and searchable-PDF file export as conversion jobs (owned by 04-convert over §1 and §4 of TODO-01 here)
- Editing document text: Recto v1 reads, OCRs, converts and organizes; it does not edit page content in place

---

Format spec: [../README.md](../README.md) · Root index: [../TODO-00-INDEX.md](../TODO-00-INDEX.md)
