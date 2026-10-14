---
schema_version: 1
id: pdf-conversions-a
domain: 04-convert
status: active
title: "TODO-02 -- Conversions A: PDF Writer, Images, Text and Searchable OCR PDF"
track: M4
---

# TODO-02 -- Conversions A: PDF Writer, Images, Text and Searchable OCR PDF

> **Goal:** Build `recto-pdf`'s native `PdfWriter` and the atomic temp-then-replace file helper, then deliver the M4 conversions that run on them: PDF to images (PNG, JPEG, TIFF, BMP at 72/150/300/600 DPI), images to PDF (EXIF orientation honored, lossless JPEG `DCTDecode` pass-through), PDF to plain text (`.txt` with automatic OCR fallback on scanned pages), and Searchable PDF (OCR) injecting an invisible (`3 Tr`) selectable text layer over scanned pages.

> **Current state:** Measured 2026-10-14. No writer or converter code exists yet. `D01` provides `recto-pdf` parsing and rendering, `D03 T01` provides `PageText` (`§1`) and `OcrPipeline` (`§4`), and `D04 T01` provides the isolated `JobQueue` and 3-step Convert page.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 4.1 item 6 (`PdfWriter`), 4.7 (job isolation), 5 (PDF -> Images, Images -> PDF, PDF -> Plain text, Searchable PDF OCR, atomic replace rule).
- `design/SPEC.md` sections 9.5 (Convert step 2 and step 3 options for Images, Plain text, Searchable PDF OCR) and 9.9 (Conversion options dialog, Replace original dialog).
- -> XREF: D01 T01 §1 -- `PdfWriter` in `src/pdf/writer.cpp` serializes `PdfValue` objects, `FlateDecode` streams, and cross-reference tables using `D01 T01`'s object and filter primitives.
- -> XREF: D04 T01 §2 -- each converter registers its option schema and job factory into `D04 T01 §2`'s 3-step Convert page and queue.
- -> XREF: D05 T01 §1 -- `D05 T01`'s merge, split, reorder and AES-256 encrypt operations build directly on `PdfWriter` and the `AtomicFileWriter` created in §1 here.

## Outcome

- `PdfWriter` (`src/pdf/writer.cpp`) writes valid, compressed PDF 1.7 documents, and `AtomicFileWriter` guarantees that every save writes to a sibling temporary file and atomically replaces the target only on clean completion, leaving zero partial files on cancellation or error.
- PDF to Images streams pages at 72, 150, 300 or 600 DPI through WIC encoders (PNG, JPEG, TIFF, BMP) with bounded memory.
- Images to PDF packs one or many images into a clean PDF, honoring EXIF orientation and embedding JPEG streams directly via `DCTDecode` without lossy re-encoding.
- PDF to Text and Searchable PDF (OCR) export UTF-8 `.txt` and sandwich-layer searchable PDFs (`3 Tr` invisible text aligned to `OcrPipeline` word boxes), exposed both in the Convert UI and in `recto-cli`.

**Adjacency:** all=not-applicable (converter engines and option bindings inside `D04 T01`'s Convert frame; queue, notifications and save-location settings are owned by `D04 T01` and `D02 T04 §2`)

**Adjacency rationale:** The Convert page list, queue card, completion notifications, and Replace-original dialog belong to `D04 T01`. This file supplies the `PdfWriter` foundation and four concrete conversion engines that plug into that frame.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `PdfWriter` and atomic temp-then-replace file helper | D01 T02 §4, D04 T01 §1 | [ ] |
| 2 | §2 | PDF to images (PNG, JPEG, TIFF, BMP at 72-600 DPI) | §1, D04 T01 §2 | [ ] |
| 3 | §3 | Images to PDF with EXIF orientation and lossless JPEG pass-through | §1, D04 T01 §2 | [ ] |
| 4 | §4 | PDF to plain text and Searchable PDF (OCR) sandwich writer | §1, D03 T01 §4, D04 T01 §2 | [ ] |

## 1. `PdfWriter` and atomic temp-then-replace file helper

Implement `src/pdf/writer.cpp` (`recto::pdf::PdfWriter`) and `src/core/atomic_file.cpp` (`recto::core::AtomicFileWriter`):
- `PdfWriter`: allocates indirect objects, compresses streams with `FlateDecode` (RFC 1950/1951 deflate), builds `/Catalog` and balanced `/Pages` trees, writes standard `xref` tables and trailer dictionaries with `/ID` hashes, and supports streaming objects to disk.
- `AtomicFileWriter`: creates a uniquely named sibling temporary file in the destination folder, flushes (`FlushFileBuffers`), and commits via `ReplaceFileW` / `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` only when `commit()` is called; its destructor deletes the temp file automatically if `commit()` was not reached (cancellation, error, or scope exit).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `AtomicFileWriter` in `src/core/atomic_file.cpp` and `PdfWriter` in `src/pdf/writer.cpp` (object allocation, `FlateDecode` encoder, page tree builder, `xref` and trailer serializer). Done when: `PdfWriter.*` creates a new multi-page PDF from scratch that `recto::pdf::Document::open` parses and renders cleanly.
2. Prove the atomic cancellation guarantee in `AtomicFile.*`: aborting a write mid-stream or tripping `CancelToken` leaves the destination directory free of temp files and leaves any pre-existing target file byte-for-byte untouched. Done when: both new-file cancel and replace-existing cancel pass under ASan.

- [ ] `PdfWriter` in `src/pdf/writer.cpp` serializing valid PDF 1.7 files with `FlateDecode` stream compression. Done when: a written document reopens in `recto-pdf` with zero XRef warnings. Cheaper substitute: writing uncompressed streams or invalid placeholder `xref` offsets that rely on damaged-file recovery.
- [ ] `AtomicFileWriter` enforcing sibling temp-file write, flush, and atomic replace or automatic cleanup on cancel/error. Done when: `AtomicFile.*` proves an existing target file's SHA-256 is unchanged after a cancelled overwrite.
- [ ] Commit: `pdf: add PdfWriter and AtomicFileWriter temp-then-replace helper`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfWriter.*:AtomicFile.*` passes under ASan.

## 2. PDF to images (PNG, JPEG, TIFF, BMP at 72-600 DPI)

Implement `src/core/pdf_to_images.cpp`, `recto-cli pdf-to-images`, and the Convert step 3 options panel for **Images** (`design/SPEC.md` section 9.5 and `D-Dialogs.png` top right): format (`PNG`, `JPEG`, `TIFF`, `BMP` encoded via WIC `IWICBitmapEncoder`), resolution (`72`, `150`, `300`, `600` DPI), page range, and save location. Pages render and encode one at a time so a 500-page export at 600 DPI holds only one page bitmap in memory at a time.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.5 (Images card and step 3 options) and 9.9 (`D-Dialogs.png` top-right Conversion options card: `"Save 9 images"` button label reflecting the job count).
**Job:** The user can export any page range of a PDF to standard image files at their chosen DPI. Consumer: external image tools, and `D06 T01 §3`'s round-trip suite.
**Treatment:** Streamed page-by-page `DisplayList` rasterization at target DPI into WIC encoders (`GUID_ContainerFormatPng`, `Jpeg`, `Tiff`, `Bmp`) with DPI metadata written into each image header and `AtomicFileWriter` per output file. Cheaper substitute that fails the checkpoint: rendering all pages into RAM before writing, or ignoring the DPI metadata field on the output image.
**Chrome:** consume the Convert step 3 host from `D04 T01 §2` and the shared page-range parser.

**Build order:**

1. Implement `pdf_to_images` in `src/core/pdf_to_images.cpp` and `recto-cli pdf-to-images <in.pdf> --out <dir> --format <png|jpeg|tiff|bmp> --dpi <72|150|300|600> [--pages <range>]`. Done when: exporting a corpus file in all four formats at 150 and 300 DPI produces valid image files whose decoded dimensions and DPI metadata match the page size * DPI, and cancelling mid-job removes any partial current page file.
2. Wire the Images option panel in `ConvertPage` (`src/app`), updating the primary action button text dynamically (`"Save N images"`). Done when: the rendered options card matches `D-Dialogs.png` and completes an export through the queue.

- [ ] Streamed PDF-to-images converter across PNG, JPEG, TIFF and BMP at 72/150/300/600 DPI with WIC DPI metadata. Done when: `ConvertImages.*` verifies pixel dimensions, DPI headers, and bounded memory on a multi-page export under ASan. Cheaper substitute: supporting PNG only or hard-coding 96 DPI.
- [ ] Convert step 3 Images UI wired to `JobQueue` with dynamic `"Save N images"` verb. Done when: display-session screenshot matches `D-Dialogs.png` and cancellation mid-run leaves zero partial files.
- [ ] Commit: `convert: implement PDF to images (PNG, JPEG, TIFF, BMP) at 72-600 DPI`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ConvertImages.*` passes under ASan, and the Convert Images UI screenshot matches `D-Dialogs.png`.

## 3. Images to PDF with EXIF orientation and lossless JPEG pass-through

Implement `src/core/images_to_pdf.cpp`, `recto-cli images-to-pdf`, and the Convert flow when image files (`.png`, `.jpg`, `.jpeg`, `.tif`, `.tiff`, `.bmp`) are dropped or picked in step 1: pack one or multiple images into a single PDF via `PdfWriter`, reading EXIF orientation (`System.Photo.Orientation` via `IWICMetadataQueryReader`) so rotated camera photos appear upright, embedding JPEG streams directly as `/Filter /DCTDecode` image XObjects without lossy re-compression, and encoding non-JPEG images with `FlateDecode`.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 (Convert flow accepting image inputs to build a PDF).
**Job:** The user can turn a set of images or scans into a clean, standards-compliant PDF without degrading JPEG quality or ending up with sideways photos. Consumer: the viewer (can open the result immediately) and `D06 T01 §3`'s image round-trip test.
**Treatment:** Direct `DCTDecode` stream embedding for JPEGs, `FlateDecode` RGB/Gray/Alpha SoftMask for PNG/TIFF/BMP, EXIF orientation transform applied to the page/image matrix, and optional original-size vs fit-to-A4/Letter page sizing. Cheaper substitute that fails the checkpoint: decoding JPEGs to bitmaps and re-encoding them as lossy JPEGs or huge uncompressed streams, or ignoring EXIF orientation tags.
**Chrome:** consume the Convert step 1 file chips and step 3 options host from `D04 T01 §2`.

**Build order:**

1. Implement `images_to_pdf` in `src/core/images_to_pdf.cpp` and `recto-cli images-to-pdf <img1> [img2...] --out <out.pdf>`. Done when: converting a JPEG with an EXIF 90-degree orientation tag and a PNG with alpha produces a valid 2-page PDF whose embedded JPEG stream bytes match the source JPEG and whose rendered pages match the upright source images.
2. Wire image file selection and options into `ConvertPage`. Done when: dropping three images onto Convert produces a 3-page PDF through the queue.

- [ ] `images_to_pdf` with lossless JPEG `DCTDecode` pass-through, PNG alpha `/SMask` support, and EXIF orientation handling. Done when: `ConvertImagesToPdf.*` verifies upright rendering of EXIF-rotated JPEGs and lossless stream identity under ASan. Cheaper substitute: ignoring EXIF orientation or re-compressing JPEGs.
- [ ] Convert UI flow for image inputs wired to `JobQueue`. Done when: a multi-image drop converts cleanly and opens in a viewer tab.
- [ ] Commit: `convert: implement images to PDF with EXIF orientation and JPEG pass-through`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ConvertImagesToPdf.*` passes under ASan.

## 4. PDF to plain text and Searchable PDF (OCR) sandwich writer

Implement `src/core/pdf_to_text.cpp` and `src/core/searchable_pdf.cpp`:
- **PDF to Plain Text (`.txt`):** Extracts reading-order UTF-8 text per page via `PageText` (`D03 T01 §1`), automatically running `OcrPipeline` (`D03 T01 §4`) on scanned pages so scanned PDFs export real text instead of an empty `.txt` file.
- **Searchable PDF (OCR):** Takes a scanned (or mixed) PDF, runs `OcrPipeline` (`Windows.Media.Ocr`) on pages that lack text, and uses `PdfWriter` (`§1`) to write a new PDF that preserves the original page visual stream and appends a calibrated invisible text layer (`BT 3 Tr ... Tf ... Tm (...) Tj ET`) aligned to each recognized word quad so the resulting PDF is selectable and searchable in every PDF reader.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 (Plain text and Searchable PDF OCR cards and options).
**Job:** The user can extract clean UTF-8 text from any PDF or turn a scanned image-only PDF into a permanently searchable, selectable PDF. Consumer: external editors and any standard PDF viewer opening the saved searchable PDF.
**Treatment:** `PageText` + `OcrPipeline` for `.txt`, and `PdfWriter` + `3 Tr` invisible text operators for Searchable PDF. Cheaper substitute that fails the checkpoint: producing a 0-byte `.txt` file on scanned PDFs, or rasterizing digital vector pages when only scanned pages needed an OCR text layer.
**Chrome:** consume the Convert step 2 cards (`Plain text`, `Searchable PDF (OCR)`) and step 3 options host from `D04 T01 §2`.

**Build order:**

1. Implement `pdf_to_text` (`src/core/pdf_to_text.cpp`) and `searchable_pdf` (`src/core/searchable_pdf.cpp`, writing `3 Tr` invisible text runs using a compact embedded or Standard-14 font scaled to each `OcrWord` quad), plus `recto-cli pdf-to-text` and `recto-cli ocr <in.pdf> --out <searchable.pdf>`. Done when: running `searchable_pdf` on `tests/corpus/text/scanned.pdf` produces a PDF that has native extractable `PageText` (without re-running OCR) and renders pixel-identically to the input scan.
2. Wire the **Plain text** and **Searchable PDF (OCR)** cards and step 3 options (page range, OCR language, save location) into `ConvertPage`. Done when: both conversions run through the queue with progress and cancellation.

- [ ] `pdf_to_text` (with automatic OCR fallback on scanned pages) and `searchable_pdf` (`3 Tr` invisible text sandwich writer) in `src/core/`. Done when: `tests/corpus/text/scanned.pdf` converts to a searchable PDF whose native `PageText` matches the OCR text and whose visual pixels are unchanged (`ConvertTextAndOcr.*`). Cheaper substitute: visible text overlays that deface the scanned page image.
- [ ] Plain text and Searchable PDF (OCR) cards and options wired in `ConvertPage`. Done when: driven queue tests complete both jobs and verify cancellation leaves no temp files.
- [ ] Commit: `convert: add PDF to text and Searchable PDF (OCR) invisible-text writer`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ConvertTextAndOcr.*` passes under ASan (verifying both `.txt` output and `3 Tr` searchable PDF round-trip on `scanned.pdf`).

## Verification

- `PdfWriter`, `AtomicFileWriter`, PDF to images, images to PDF, PDF to text, and Searchable PDF (OCR) all pass under ASan and round-trip in `D06 T01 §3`.
- Cancelling any conversion mid-job leaves zero temporary or partial files in the output directory.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
