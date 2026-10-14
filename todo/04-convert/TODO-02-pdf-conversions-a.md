---
schema_version: 1
id: pdf-conversions-a
domain: 04-convert
status: active
title: "TODO-02 -- Conversions A: PDF Writer, Images/SVG/XPS, Text, OCR, Table/Image Extraction and Optimization"
track: M4
---

# TODO-02 -- Conversions A: PDF Writer, Images/SVG/XPS, Text, OCR, Table/Image Extraction and Optimization

> **Goal:** Build `recto-pdf`'s native `PdfWriter` and the atomic temp-then-replace file helper, then deliver the M4 conversions and extraction/optimization tools that run on them: PDF to images, multipage TIFF (`LZW`, `CCITT Group 4`, `JPEG`), animated GIF, vector SVG (`.svg`) and OpenXPS (`.xps`) at 72 to 1200 DPI; images, SVG and XPS to PDF (EXIF orientation honored, lossless JPEG `DCTDecode` pass-through); PDF to plain text (`.txt` with automatic OCR fallback); Searchable PDF (OCR) injecting an invisible (`3 Tr`) selectable text layer; structured Table & Image Extraction (`src/core/table_extractor.cpp`, `src/core/image_extractor.cpp`) with the Viewer `Table & Image Extractor Bar`; and PDF Optimization, Compression, Font Subsetting and Fast Web View Linearization (`src/core/pdf_optimizer.cpp`, `src/pdf/linearizer.cpp`).

> **Current state:** Measured 2026-10-14. No writer or converter code exists yet. `D01` provides `recto-pdf` parsing and rendering, `D03 T01` provides `PageText` (`§1`) and `OcrPipeline` (`§4`), and `D04 T01` provides the isolated `JobQueue` and 3-step Convert page.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 4.1 item 6 (`PdfWriter`, `linearizer.cpp`), 4.7 (job isolation), 5 (PDF <-> Images/SVG/XPS, PDF -> Plain text, Searchable PDF OCR, Table & Image Extraction, PDF Optimization & Linearization, atomic replace rule).
- `design/SPEC.md` sections 8.2 (`Table & Image Extractor Bar`), 9.5 (Convert step 2 and step 3 options for Images/TIFF/SVG/XPS, Plain text, Searchable PDF OCR, Extract Tables & Images, Optimize & Compress PDF) and 9.9 (Conversion options dialog, Optimize & Compress Audit dialog, Replace original dialog).
- -> XREF: D01 T01 §1 -- `PdfWriter` in `src/pdf/writer.cpp` serializes `PdfValue` objects, `FlateDecode` streams, and cross-reference tables using `D01 T01`'s object and filter primitives.
- -> XREF: D04 T01 §2 -- each converter registers its option schema and job factory into `D04 T01 §2`'s 3-step Convert page and queue.
- -> XREF: D04 T05 §1 -- `TableExtractor` in §5 feeds detected table grids to `D04 T05 §1`'s native `XlsxWriter`, `.csv` and `.xml` exporters.
- -> XREF: D05 T01 §1 -- `D05 T01`'s merge, split, reorder and AES-256 encrypt operations build directly on `PdfWriter` and the `AtomicFileWriter` created in §1 here.

## Outcome

- `PdfWriter` (`src/pdf/writer.cpp`) writes valid, compressed PDF 1.7 and 2.0 documents, and `AtomicFileWriter` guarantees that every save writes to a sibling temporary file and atomically replaces the target only on clean completion, leaving zero partial files on cancellation or error.
- PDF to Images/Vector streams pages at 72, 150, 300, 600 or 1200 DPI through WIC encoders (`PNG`, `JPEG`, single/multipage `TIFF` with `LZW`/`CCITT Group 4`/`JPEG`, `BMP`, `Animated GIF`) and vector `SVG` (`.svg`) / `OpenXPS` (`.xps`) writers (`src/core/svg_xps_writer.cpp`) with bounded memory.
- Images/SVG/XPS to PDF packs one or many images, multipage TIFFs, vector SVGs or XPS files into a clean PDF, honoring EXIF orientation and embedding JPEG streams directly via `DCTDecode` without lossy re-encoding.
- PDF to Text and Searchable PDF (OCR) export UTF-8 `.txt` and sandwich-layer searchable PDFs (`3 Tr` invisible text aligned to `OcrPipeline` word boxes), exposed both in the Convert UI and in `recto-cli`.
- `TableExtractor` and `ImageExtractor` extract embedded XObject images without re-compression and detect bordered and borderless tables on canvas (`Table & Image Extractor Bar`) and in batch (`Extract Tables & Images` Convert card).
- `PdfOptimizer` and `Linearizer` downsample images, recompress colour/grayscale/monochrome streams, subset fonts, strip unused objects, linearize for Fast Web View, and present a per-category byte-savings audit dialog.

**Adjacency:** all=not-applicable (converter engines and option bindings inside `D04 T01`'s Convert frame plus the viewer's contextual extractor bar; queue, notifications and save-location settings are owned by `D04 T01` and `D02 T04 §2`)

**Adjacency rationale:** The Convert page list, queue card, completion notifications, and Replace-original dialog belong to `D04 T01`. This file supplies the `PdfWriter` foundation and six concrete conversion/extraction/optimization engines and surfaces that plug into that frame and the viewer.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `PdfWriter` and atomic temp-then-replace file helper | D01 T02 §4, D04 T01 §1 | [ ] |
| 2 | §2 | PDF to images, multipage TIFF, animated GIF, vector SVG and OpenXPS | §1, D04 T01 §2 | [ ] |
| 3 | §3 | Images, SVG and XPS to PDF with EXIF orientation and lossless JPEG pass-through | §1, D04 T01 §2 | [ ] |
| 4 | §4 | PDF to plain text and Searchable PDF (OCR) sandwich writer | §1, D03 T01 §4, D04 T01 §2 | [ ] |
| 5 | §5 | Table and Image Extractor engine, Convert card and Viewer Extractor Bar | §1, D03 T01 §1, D04 T01 §2 | [ ] |
| 6 | §6 | PDF Optimizer, Compressor, Font Subsetter and Fast Web View Linearizer | §1, D01 T02 §4, D04 T01 §2 | [ ] |

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

## 2. PDF to images, multipage TIFF, animated GIF, vector SVG and OpenXPS

Implement `src/core/pdf_to_images.cpp`, `src/core/svg_xps_writer.cpp`, `recto-cli pdf-to-images`, `recto-cli pdf-to-svg`, `recto-cli pdf-to-xps`, and the Convert step 3 options panel for **Images, TIFF, SVG & XPS** (`design/SPEC.md` section 9.5 and `D-Dialogs.png` top right):
- **Raster formats (`src/core/pdf_to_images.cpp`):** `PNG` (with optional transparent page background), `JPEG` (with quality slider `1-100`), single-page or **Multipage `TIFF`** (with compression selector: `LZW`, `CCITT Group 4` fax 1-bit monochrome, `JPEG`, or `Uncompressed`), `BMP`, and **Animated `GIF`** (frame delay configurable), at `72`, `150`, `300`, `600`, or `1200` DPI and colour spaces (`RGB`, `Grayscale`, `Black & White 1-bit`). Pages render and encode one at a time so a 500-page export holds only one page bitmap in memory.
- **Vector formats (`src/core/svg_xps_writer.cpp`):** Export `DisplayList` paths, positioned glyph runs/`<text>` or outlined `<path>` glyphs, gradients, clip paths, and images to standalone per-page **SVG 1.1 (`.svg`)** files and packaged **OpenXPS (`.xps`)** documents (`FixedDocumentSequence`, `FixedDocument`, `.fpage` XML, subset fonts, and image resources in a ZIP container).

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.5 (Images, TIFF, SVG & XPS card and step 3 options) and 9.9 (`D-Dialogs.png` top-right Conversion options card: `"Save 9 images"` button label reflecting the job count).
**Job:** The user can export any page range of a PDF to standard raster image files, multipage archival/fax TIFFs, animated GIFs, scalable vector SVGs, or OpenXPS packages. Consumer: external image/vector tools, and `D06 T01 §3`'s round-trip suite.
**Treatment:** Streamed page-by-page `DisplayList` rasterization at target DPI into WIC encoders (`GUID_ContainerFormatPng`, `Jpeg`, multi-frame `Tiff` with `WICTiffCompressionOption`, `Bmp`, multi-frame `Gif`) plus direct `DisplayList` vector serialization to `.svg` and `.xps` with `AtomicFileWriter` per output file. Cheaper substitute that fails the checkpoint: rendering all pages into RAM before writing, omitting multipage TIFF (`CCITT4`/`LZW`) or vector SVG/XPS, or ignoring DPI metadata.
**Chrome:** consume the Convert step 3 host from `D04 T01 §2` and the shared page-range parser.

**Build order:**

1. Implement `pdf_to_images` in `src/core/pdf_to_images.cpp` (PNG, JPEG, single/multipage TIFF with `LZW`/`CCITT4`/`JPEG`, BMP, Animated GIF at `72-1200` DPI) and `svg_xps_writer` in `src/core/svg_xps_writer.cpp` (`.svg` and `.xps`), plus `recto-cli pdf-to-images`, `recto-cli pdf-to-svg`, and `recto-cli pdf-to-xps`. Done when: exporting a corpus file in all raster and vector formats produces valid files (including multi-frame TIFF with `CCITT4` and vector `.svg`/`.xps` with `<path>` and `<text>`/`Glyphs` elements) and cancelling mid-job removes any partial file.
2. Wire the **Images, TIFF, SVG & XPS** option panel in `ConvertPage` (`src/app`), updating the primary action button text dynamically (`"Save N images"`). Done when: the rendered options card matches `D-Dialogs.png` and completes raster, multipage TIFF, SVG and XPS exports through the queue.

- [ ] Streamed PDF-to-images converter (`PNG`, `JPEG`, single/multipage `TIFF` with `LZW`/`CCITT4`, `BMP`, `Animated GIF`) at `72-1200` DPI and vector `SVG`/`OpenXPS` exporter. Done when: `ConvertImages.*` verifies pixel dimensions, DPI headers, multipage TIFF frame counts, valid SVG/XPS XML, and bounded memory under ASan. Cheaper substitute: supporting single-page PNG only or rasterizing pages inside SVG/XPS instead of emitting vector paths.
- [ ] Convert step 3 Images/TIFF/SVG/XPS UI wired to `JobQueue` with dynamic `"Save N images"` verb. Done when: display-session screenshot matches `D-Dialogs.png` and cancellation mid-run leaves zero partial files.
- [ ] Commit: `convert: implement PDF to images, multipage TIFF, GIF, vector SVG and OpenXPS`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ConvertImages.*` passes under ASan, and the Convert Images UI screenshot matches `D-Dialogs.png`.

## 3. Images, SVG and XPS to PDF with EXIF orientation and lossless JPEG pass-through

Implement `src/core/images_to_pdf.cpp`, `recto-cli images-to-pdf`, and the Convert flow when image or vector files (`.png`, `.jpg`, `.jpeg`, `.tif`, `.tiff`, `.bmp`, `.gif`, `.svg`, `.xps`) are dropped or picked in step 1: pack one or multiple images, all frames of a multipage TIFF, vector `.svg` elements, or `.xps` pages into a single PDF via `PdfWriter`, reading EXIF orientation (`System.Photo.Orientation` via `IWICMetadataQueryReader`) so rotated camera photos appear upright, embedding JPEG streams directly as `/Filter /DCTDecode` image XObjects without lossy re-compression, encoding non-JPEG images with `FlateDecode`, and translating SVG/XPS vector paths and text into native PDF content streams.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 (Convert flow accepting image, SVG and XPS inputs to build a PDF).
**Job:** The user can turn a set of images, multipage TIFF scans, SVGs or XPS files into a clean, standards-compliant PDF without degrading JPEG quality or rasterizing vector SVGs. Consumer: the viewer (can open the result immediately) and `D06 T01 §3`'s round-trip test.
**Treatment:** Direct `DCTDecode` stream embedding for JPEGs, `FlateDecode` RGB/Gray/Alpha SoftMask for PNG/TIFF/BMP/GIF (expanding all frames of multipage TIFFs), vector path/text translation for `.svg` and `.xps`, EXIF orientation transform applied to the page/image matrix, and original-size vs fit-to-A4/Letter page sizing. Cheaper substitute that fails the checkpoint: decoding JPEGs to bitmaps and re-encoding them as lossy JPEGs, reading only frame 0 of a multipage TIFF, or ignoring EXIF orientation tags.
**Chrome:** consume the Convert step 1 file chips and step 3 options host from `D04 T01 §2`.

**Build order:**

1. Implement `images_to_pdf` in `src/core/images_to_pdf.cpp` (supporting single/multipage raster images, `.svg`, and `.xps`) and `recto-cli images-to-pdf <in1> [in2...] --out <out.pdf>`. Done when: converting a JPEG with an EXIF 90-degree orientation tag, a 3-frame TIFF, a PNG with alpha, and a vector SVG produces a valid multi-page PDF whose embedded JPEG stream bytes match the source JPEG and whose SVG page preserves vector paths.
2. Wire image, SVG and XPS file selection and options into `ConvertPage`. Done when: dropping mixed images and SVGs onto Convert produces a combined PDF through the queue.

- [ ] `images_to_pdf` with lossless JPEG `DCTDecode` pass-through, multipage TIFF frame expansion, PNG alpha `/SMask` support, EXIF orientation handling, and vector `.svg`/`.xps` import. Done when: `ConvertImagesToPdf.*` verifies upright rendering of EXIF-rotated JPEGs, lossless stream identity, and vector SVG/XPS fidelity under ASan. Cheaper substitute: ignoring EXIF orientation or re-compressing JPEGs.
- [ ] Convert UI flow for image/SVG/XPS inputs wired to `JobQueue`. Done when: a multi-file drop converts cleanly and opens in a viewer tab.
- [ ] Commit: `convert: implement images, multipage TIFF, SVG and XPS to PDF`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ConvertImagesToPdf.*` passes under ASan.

## 4. PDF to plain text and Searchable PDF (OCR) sandwich writer

Implement `src/core/pdf_to_text.cpp` and `src/core/searchable_pdf.cpp`:
- **PDF to Plain Text (`.txt`):** Extracts reading-order UTF-8 text per page via `PageText` (`D03 T01 §1`), automatically running `OcrPipeline` (`D03 T01 §4`) on scanned pages so scanned PDFs export real text instead of an empty `.txt` file.
- **Searchable PDF (OCR):** Takes a scanned (or mixed) PDF, runs `OcrPipeline` (`Windows.Media.Ocr`) on pages that lack text (or pages with vector-outlined text when selected), and uses `PdfWriter` (`§1`) to write a new PDF that preserves the original page visual stream and appends a calibrated invisible text layer (`BT 3 Tr ... Tf ... Tm (...) Tj ET`) aligned to each recognized word quad so the resulting PDF is selectable and searchable in every PDF reader.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 (Plain text and Searchable PDF OCR cards and options).
**Job:** The user can extract clean UTF-8 text from any PDF or turn a scanned image-only PDF into a permanently searchable, selectable PDF. Consumer: external editors and any standard PDF viewer opening the saved searchable PDF.
**Treatment:** `PageText` + `OcrPipeline` for `.txt`, and `PdfWriter` + `3 Tr` invisible text operators for Searchable PDF. Cheaper substitute that fails the checkpoint: producing a 0-byte `.txt` file on scanned PDFs, or rasterizing digital vector pages when only scanned pages needed an OCR text layer.
**Chrome:** consume the Convert step 2 cards (`Plain text`, `Searchable PDF (OCR)`) and step 3 options host from `D04 T01 §2`.

**Build order:**

1. Implement `pdf_to_text` (`src/core/pdf_to_text.cpp`) and `searchable_pdf` (`src/core/searchable_pdf.cpp`, writing `3 Tr` invisible text runs using a compact embedded or Standard-14 font scaled to each `OcrWord` quad), plus `recto-cli pdf-to-text` and `recto-cli ocr <in.pdf> --out <searchable.pdf>`. Done when: running `searchable_pdf` on `tests/corpus/text/scanned.pdf` produces a PDF that has native extractable `PageText` (without re-running OCR) and renders pixel-identically to the input scan.
2. Wire the **Plain text** and **Searchable PDF (OCR)** cards and step 3 options (page range, OCR language, deskew/denoise preprocessing toggle, OCR vector-outlined text toggle, save location) into `ConvertPage`. Done when: both conversions run through the queue with progress and cancellation.

- [ ] `pdf_to_text` (with automatic OCR fallback on scanned pages) and `searchable_pdf` (`3 Tr` invisible text sandwich writer) in `src/core/`. Done when: `tests/corpus/text/scanned.pdf` converts to a searchable PDF whose native `PageText` matches the OCR text and whose visual pixels are unchanged (`ConvertTextAndOcr.*`). Cheaper substitute: visible text overlays that deface the scanned page image.
- [ ] Plain text and Searchable PDF (OCR) cards and options wired in `ConvertPage`. Done when: driven queue tests complete both jobs and verify cancellation leaves no temp files.
- [ ] Commit: `convert: add PDF to text and Searchable PDF (OCR) invisible-text writer`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ConvertTextAndOcr.*` passes under ASan (verifying both `.txt` output and `3 Tr` searchable PDF round-trip on `scanned.pdf`).

## 5. Table and Image Extractor engine, Convert card and Viewer Extractor Bar

Implement `src/core/table_extractor.cpp` (`recto::core::TableExtractor`) and `src/core/image_extractor.cpp` (`recto::core::ImageExtractor`), plus their CLI verbs (`recto-cli extract-images`, `recto-cli extract-tables`), the **Extract Tables & Images** Convert card (`SPEC 9.5`), and the interactive Viewer **Table & Image Extractor Bar** (`SPEC 8.2`):
- **TableExtractor (`src/core/table_extractor.cpp`):** Detects tables on any PDF page using a dual algorithm: (a) **Orthogonal vector ruling lines** (horizontal and vertical path segments and thin filled rectangles forming grid intersections, cell spans, `rowspan`/`colspan` merges, and cell background shading) and (b) **Whitespace projection & column/row baseline clustering** for borderless tables (invoices, financial statements, schedules). Supports scanned pages via `OcrPipeline` (`D03 T01 §4`). Outputs structured `ExtractedTable` objects (rows, columns, merged cell spans, cell text, numeric/currency/date type tags, bounding boxes) and serializes directly to `.csv`, `.tsv`, `.json`, or clipboard HTML/TSV (and feeds `.xlsx` in `D04 T05 §1`).
- **ImageExtractor (`src/core/image_extractor.cpp`):** Walks page resources and Form XObjects to extract embedded `/Subtype /Image` streams at native resolution without re-rendering the page: `/DCTDecode` streams save directly as lossless `.jpg` files, and `/FlateDecode` / `/JPXDecode` / `/CCITTFaxDecode` / `/JBIG2Decode` + `/SMask` streams decode and save as lossless `.png` or `.tiff` with minimum pixel-dimension filtering (`Skip decorative icons smaller than N x N px`).
- **Visual Surfaces (`src/app`):** Add the viewer's `40 epx` contextual **Table & Image Extractor Bar** (`SPEC 8.2`: `Auto-Detect Tables on Page`, `Draw Table Region` with interactive row/column divider handles on canvas, `Preview Table` modal data grid with `Copy to Clipboard`, `Export to .xlsx`, `Export to .csv`, `Export to .json`, and `Extract Page Images`) and the **Extract Tables & Images** card in `ConvertPage` (`SPEC 9.5`).

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Table & Image Extractor Bar`) and 9.5 (`Extract Tables & Images` Convert card and step 3 options).
**Job:** Users can auto-detect or lasso tables on a PDF page, adjust row/column dividers visually, preview and copy/export structured data, or batch-extract all embedded images and tables from a PDF without quality loss. Consumer: spreadsheet/data tools, `D04 T05 §1` (`XlsxWriter`), and `DocxWriter` (`D04 T03 §1`).
**Treatment:** Dual ruled-grid + whitespace-clustering `TableExtractor` and direct XObject stream `ImageExtractor` paired with an on-canvas interactive table-grid overlay and preview dialog. Cheaper substitute that fails the checkpoint: rendering whole pages as screenshots instead of extracting embedded image XObjects, or splitting text only on spaces without column alignment and merged-cell detection.
**Chrome:** consume `DocumentView` (`D02 T02`) for the interactive canvas overlay and `ConvertPage` (`D04 T01 §2`) for batch extraction.

**Build order:**

1. Implement `src/core/table_extractor.cpp` (ruled-line intersection graph + borderless whitespace column/row clustering + merged cell detection + `.csv`/`.json`/TSV export) and `src/core/image_extractor.cpp` (lossless JPEG `DCTDecode` pass-through + PNG/TIFF with `/SMask` alpha + minimum size filter), plus `recto-cli extract-tables` and `recto-cli extract-images`. Done when: `TableAndImageExtractor.*` extracts both bordered and borderless multi-column tables with merged header cells and extracts embedded JPEG/PNG XObjects byte-identically under ASan.
2. Build the Viewer `Table & Image Extractor Bar` (`SPEC 8.2`, interactive table region box with draggable row/column splitters and `Preview Table` modal grid) and the `Extract Tables & Images` card in `ConvertPage` (`SPEC 9.5`). Done when: `AppTests.ExtractorUi` auto-detects a table on canvas, adjusts a column divider, previews the cells, copies TSV to the clipboard, and runs batch image/table extraction through the Convert queue.

- [ ] `TableExtractor` (ruled and borderless tables with `rowspan`/`colspan`) and `ImageExtractor` (native XObject stream extraction with lossless JPEG pass-through and `/SMask` composition). Done when: `TableAndImageExtractor.*` passes under ASan on bordered, borderless, and image-bearing corpus PDFs. Cheaper substitute: full-page rasterization or naive space-delimited line splitting.
- [ ] Viewer `Table & Image Extractor Bar` (on-canvas row/column handles + `Preview Table` modal) and `Extract Tables & Images` Convert card. Done when: `AppTests.ExtractorUi` passes end-to-end.
- [ ] Commit: `convert: add TableExtractor, ImageExtractor, viewer extractor bar and Convert card`

**Test checkpoint:** `recto-tests.exe --gtest_filter=TableAndImageExtractor.*:AppTests.ExtractorUi` passes under ASan.

## 6. PDF Optimizer, Compressor, Font Subsetter and Fast Web View Linearizer

Implement `src/core/pdf_optimizer.cpp` (`recto::core::PdfOptimizer`) and `src/pdf/linearizer.cpp` (`recto::pdf::Linearizer`), plus `recto-cli optimize`, the **Optimize & Compress PDF** Convert card (`SPEC 9.5`), and the **Optimize & Compress Audit Dialog** (`SPEC 9.9`):
- **Image downsampling and recompression:** Inspect all `/Subtype /Image` XObjects, compute effective rendered DPI on page, downsample images exceeding target thresholds (e.g., downsample above `225 DPI` to `150 DPI` for Web/Email, or `450 DPI` -> `300 DPI` for High Quality), and recompress colour images (`JPEG` quality), grayscale (`JPEG`/`Flate`), and 1-bit monochrome (`CCITT Group 4` / `Flate`).
- **Font subsetting and object deduplication:** Deduplicate identical streams by SHA-256 hash, subset embedded TrueType/CID fonts (`/FontFile2`, `/FontFile3`) to only the glyphs referenced in the document, compress uncompressed streams with `FlateDecode`, and optionally strip unreferenced objects, metadata, thumbnails, annotations, or form fields.
- **Fast Web View Linearization (`src/pdf/linearizer.cpp`):** Reorder objects and write a valid PDF 1.7 Section F `/Linearized 1` header dictionary, primary hint stream (`/H`), first-page cross-reference table, and first-page object group at the front of the file for byte-range web streaming.
- **Visual Surfaces (`src/app`):** Wire the **Optimize & Compress PDF** card in `ConvertPage` (`SPEC 9.5`: presets `Web / Email`, `Balanced`, `Print Ready`, `Custom` + `Linearize for Fast Web View` toggle + `Audit Space Usage`) and the **Optimize & Compress Audit Dialog** (`SPEC 9.9`: per-category byte and percentage breakdown for `Images`, `Fonts`, `Content Streams`, `Metadata & XMP`, `Structure / XRef`, and `Estimated Output Size`).

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 9.5 (`Optimize & Compress PDF` card and options) and 9.9 (`Optimize & Compress Audit Dialog`).
**Job:** Users can inspect what is taking up space in a bloated PDF, shrink it with clear presets or custom DPI/compression rules, and linearize it for Fast Web View streaming. Consumer: the Convert queue (`D04 T01 §2`), viewer File menu (`Optimize & Compress...`), and `recto-cli optimize`.
**Treatment:** Stream-level byte audit, WIC high-quality bicubic image downsampling, TrueType glyph subsetting, SHA-256 stream deduplication, and Section F `/Linearized` hint-stream generation. Cheaper substitute that fails the checkpoint: re-saving the PDF with `FlateDecode` only while ignoring oversized embedded images, duplicate streams, font subsetting, or `/Linearized` tables.
**Chrome:** consume `ConvertPage` (`D04 T01 §2`) and the shared modal `ContentDialog` host (`D02 T01 §4`).

**Build order:**

1. Implement space-usage auditing, image downsampling/recompression, stream deduplication, font subsetting, and object stripping in `src/core/pdf_optimizer.cpp`, plus `/Linearized 1` Fast Web View serialization in `src/pdf/linearizer.cpp` and `recto-cli optimize <in.pdf> --out <out.pdf> [--preset <web|balanced|print>] [--linearize]`. Done when: `PdfOptimizer.*` shrinks an image-heavy and unsubsetted-font corpus PDF by `>= 50%` under the `web` preset while preserving text and visual readability, and `Linearizer.*` verifies `/Linearized 1` dictionary and first-page object ordering.
2. Wire the **Optimize & Compress PDF** card in `ConvertPage` and the **Optimize & Compress Audit Dialog** (`SPEC 9.9`). Done when: `AppTests.OptimizerUi` opens the Audit Space Usage breakdown, verifies byte percentages sum to 100%, and runs an optimization + linearization job through the queue.

- [ ] `PdfOptimizer` (space audit, image downsampling, font subsetting, stream deduplication) and `Linearizer` (`/Linearized 1` Fast Web View). Done when: `PdfOptimizer.*` and `Linearizer.*` pass under ASan. Cheaper substitute: no-op re-save that does not downsample images or write a `/Linearized` header.
- [ ] `Optimize & Compress PDF` Convert card and `Audit Space Usage` breakdown dialog (`SPEC 9.9`). Done when: `AppTests.OptimizerUi` passes end-to-end.
- [ ] Commit: `convert: add PdfOptimizer, font subsetter, Fast Web View linearizer and audit UI`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfOptimizer.*:Linearizer.*:AppTests.OptimizerUi` passes under ASan.

## Verification

- `PdfWriter`, `AtomicFileWriter`, PDF to images/TIFF/GIF/SVG/XPS, images/SVG/XPS to PDF, PDF to text, Searchable PDF (OCR), `TableExtractor`, `ImageExtractor`, `PdfOptimizer`, and `Linearizer` all pass under ASan and round-trip in `D06 T01 §3`.
- Cancelling any conversion mid-job leaves zero temporary or partial files in the output directory.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
