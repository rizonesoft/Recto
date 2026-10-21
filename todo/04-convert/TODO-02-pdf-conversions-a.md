---
schema_version: 1
id: pdf-conversions-a
domain: 04-convert
status: active
title: "TODO-02 -- Conversions A: PDF Writer, Images/SVG/XPS, Text, OCR, Table/Image Extraction and Optimization"
track: M4
---

# TODO-02 -- Conversions A: PDF Writer, Images/SVG/XPS, Text, OCR, Table/Image Extraction and Optimization

> **Goal:** Build `recto-pdf`'s native `PdfWriter` and the atomic temp-then-replace file helper (`AtomicFileWriter`), then deliver the M4 conversions and extraction/optimization tools that run on them: PDF to images, multipage TIFF (`LZW`, `CCITT Group 4`, `JPEG`), animated GIF, vector SVG (`.svg`) and OpenXPS (`.xps`) at `72` to `1200` DPI; images, SVG and XPS to PDF (EXIF orientation honored, lossless JPEG `DCTDecode` pass-through); PDF to plain text (`.txt` with automatic OCR fallback); Searchable PDF (OCR) injecting an invisible (`3 Tr`) selectable text layer; structured Table & Image Extraction (`src/core/table_extractor.cpp`, `src/core/image_extractor.cpp`) with the Viewer `Table & Image Extractor Bar`; and PDF Optimization, Compression, Font Subsetting and Fast Web View Linearization (`src/core/pdf_optimizer.cpp`, `src/pdf/linearizer.cpp`).

> **Current state:** Measured 2026-10-14. No writer or converter code exists yet. `D01` provides `recto-pdf` parsing and rendering, `D03 T01` provides `PageText` (`§1`) and `OcrPipeline` (`§8`), and `D04 T01` provides the isolated `ConversionRunner` (`§1`) and 3-step Convert page (`§3`-`§4`).

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 4.1 item 6 (`PdfWriter`, `linearizer.cpp`), 4.7 (job isolation), 5 (PDF <-> Images/SVG/XPS, PDF -> Plain text, Searchable PDF OCR, Table & Image Extraction, PDF Optimization & Linearization, atomic replace rule).
- `design/SPEC.md` sections 8.2 (`Table & Image Extractor Bar`), 9.5 (Convert step 2 and step 3 options for Images/TIFF/SVG/XPS, Plain text, Searchable PDF OCR, Extract Tables & Images, Optimize & Compress PDF) and 9.9 (Conversion options dialog, Optimize & Compress Audit dialog, Replace original dialog).
- -> XREF: D01 T01 §10 -- `PdfWriter` in `src/pdf/writer.cpp` (§2 here) serializes `PdfValue` objects, `FlateDecode` streams, and cross-reference tables using `D01 T01`'s object and filter primitives.
- -> XREF: D02 T01 §10 -- the `Optimize & Compress Audit Dialog` and `Preview Table` modal in §10 and §12 use `D02 T01 §10`'s single-active-dialog host.
- -> XREF: D02 T02 §8 -- the Floating Page Toolbar's `Extract tables / images` action toggles §10's `Table & Image Extractor Bar`.
- -> XREF: D03 T01 §1 -- `TableExtractor` in §8 consumes `PageText` character and span quads from `D03 T01 §1`.
- -> XREF: D03 T01 §4 -- the right-click selection context menu's `Export Selection as Table` action invokes §10's table extractor preview.
- -> XREF: D03 T01 §8 -- `PDF -> Plain Text` (§6) and `Searchable PDF (OCR)` (§7) consume `OcrPipeline` from `D03 T01 §8`.
- -> XREF: D04 T01 §1 -- `AtomicFileWriter` (§1) and every converter job execute inside `D04 T01 §1`'s isolated `ConversionRunner`.
- -> XREF: D04 T01 §4 -- each converter registers its card and Step 3 option schema into `D04 T01 §4`'s 3-step Convert page.
- -> XREF: D04 T03 §3 -- `DocxWriter` in `D04 T03 §3` consumes `TableExtractor` grids from §8 here.
- -> XREF: D04 T04 §1 -- `OfficeToPdf` (`D04 T04 §1`-`§3`) emits paginated PDFs through `PdfWriter` (`§2`) and `AtomicFileWriter` (`§1`) built here.
- -> XREF: D04 T05 §1 -- `XlsxWriter` (`D04 T05 §1`) and `WebDocWriter` (`D04 T05 §3`) consume `TableExtractor` grids from §8 here.
- -> XREF: D05 T01 §1 -- `D05 T01`'s graft, merge, split, reorder, crop, watermark, Bates, AES-256 encrypt, digital signature, and redaction operations build directly on `AtomicFileWriter` (§1) and `PdfWriter` (§2) created here.
- -> XREF: D06 T01 §5 -- `RoundTrips.Formats` (`D06 T01 §5`) exercises `PDF <-> Images/SVG/XPS` (`§3`-`§5`), `PDF -> Plain Text` (`§6`), `Searchable PDF (OCR)` (`§7`), `TableExtractor`/`ImageExtractor` (`§8`-`§10`), and `PdfOptimizer`/`Linearizer` (`§11`, `§12`) built here.
- -> XREF: D06 T03 §5 -- the save integrity fault-injection suite that fails every step of `AtomicFileWriter` (§1 here) and checks originals survive.
- -> XREF: D03 T04 §4 -- Edit PDF's font resolver embeds fallback subsets with this subsetter.
- -> XREF: D02 T01 §5 -- clipboard PDFs open as new unsaved tabs through `OpenDocumentPipeline`.
- -> XREF: D03 T01 §13 -- stitch_page_areas reuse.
- -> XREF: D03 T01 §19 -- searchable word writer.
- -> XREF: D07 T01 §17 -- XPS to PDF for spool.
- -> XREF: D03 T04 §1 -- EditModel accepts OCR text.
- -> XREF: D03 T04 §2 -- paragraph model.
- -> XREF: D03 T04 §4 -- EditFontResolver.
- -> XREF: D03 T01 §8 -- OcrPipeline layout.
- -> XREF: D03 T01 §16 -- suspects review input.
- -> XREF: D03 T01 §12 -- snapshot tool owns clipboard copy.
- -> XREF: D03 T01 §13 -- stitch_page_areas.
- -> XREF: D02 T01 §5 -- new tab path.
- -> XREF: D02 T04 §5 -- size default setting.
- -> XREF: D05 T01 §30 -- binder writer.

## Outcome

- `AtomicFileWriter` (`src/core/atomic_file.cpp`, `§1`) guarantees that every save writes to a sibling temporary file and atomically replaces the target only on clean completion, and `PdfWriter` (`src/pdf/writer.cpp`, `§2`) writes valid, compressed PDF 1.7 and 2.0 documents.
- PDF to Images/Vector (`§3`, `§4`) streams pages at `72`, `150`, `300`, `600` or `1200` DPI through WIC encoders (`PNG`, `JPEG`, single/multipage `TIFF` with `LZW`/`CCITT Group 4`/`JPEG`, `BMP`, `Animated GIF`) and vector `SVG` (`.svg`) / `OpenXPS` (`.xps`) writers (`src/core/svg_xps_writer.cpp`) with bounded memory.
- Images/SVG/XPS to PDF (`§5`) packs one or many images, multipage TIFFs, vector SVGs or XPS files into a clean PDF, honoring EXIF orientation and embedding JPEG streams directly via `DCTDecode` without lossy re-encoding.
- PDF to Text (`§6`) and Searchable PDF (OCR) (`§7`) export UTF-8 `.txt` and sandwich-layer searchable PDFs (`3 Tr` invisible text aligned to `OcrPipeline` word boxes), exposed both in the Convert UI and in `recto-cli`.
- `TableExtractor` (`§8`) and `ImageExtractor` (`§9`) detect bordered and borderless tables and extract embedded XObject images without re-compression, powering the Viewer `Table & Image Extractor Bar` and the `Extract Tables & Images` Convert card (`§10`).
- `PdfOptimizer` and `Linearizer` (`§11`, `§12`) downsample images, recompress colour/grayscale/monochrome streams, subset fonts, strip unused objects, linearize for Fast Web View, and present a per-category byte-savings audit dialog.
- After the owner's decision, a `From scanner` source acquires pages through WIA 2.0 into the Images to PDF and searchable OCR paths (`§13`).
- Create PDF from clipboard (`§14`-`§16`) turns an image, text, rich text, HTML or a copied file list on the Windows clipboard into a new unsaved PDF tab through the `Images, SVG & XPS -> PDF` path (`§5`) and the Office, HTML and text paths, from `Ctrl+V`, Home, the tab strip, the `File` menu or `Ctrl+K`, and never produces a silent empty document.
- Image-only PDF (`§17`, `§18`), editable OCR output with live text in matched fonts (`§19`, `§20`), page area export to an image or text (`§21`, `§22`), more image inputs through installed WIC codecs (`§23`) and a blank PDF with page-size presets and paper styles (`§24`, `§25`) match the Foxit PDF Editor, PDFelement and UPDF conversion features that Acrobat lacks, offline and on copies.

**Adjacency:** all=not-applicable (converter engines and option bindings inside `D04 T01`'s Convert frame plus the viewer's contextual extractor bar; queue, notifications and save-location settings are owned by `D04 T01` and `D02 T04 §5`)

**Adjacency rationale:** The Convert page list, queue card, completion notifications, and Replace-original dialog belong to `D04 T01`. This file supplies the `AtomicFileWriter` and `PdfWriter` foundations and twelve focused conversion/extraction/optimization sections that plug into that frame and the viewer.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `AtomicFileWriter` (`src/core/atomic_file.cpp`) sibling temp-then-replace and rollback | D01 T02 §10, D04 T01 §1 | [ ] |
| 2 | §2 | Native `PdfWriter` (`writer.cpp`: object allocation, `FlateDecode`, `/Pages`, `xref`) | §1, D01 T01 §10 | [ ] |
| 3 | §3 | Streamed `PDF -> Images` (`PNG`, `JPEG`, `BMP`, Multipage `TIFF`, `GIF`) and CLI command | §2, D01 T03 §2 | [ ] |
| 4 | §4 | Vector `PDF -> SVG (.svg)` and `PDF -> OpenXPS (.xps)` exporters and Convert Step 3 UI | §3, D04 T01 §4 | [ ] |
| 5 | §5 | `Images, SVG & XPS -> PDF` converter (`DCTDecode` pass-through, TIFF, EXIF) & Convert UI | §2, D04 T01 §4 | [ ] |
| 6 | §6 | `PDF -> Plain Text (.txt)` exporter with automatic OCR fallback, CLI and Convert UI | §1, D03 T01 §8, D04 T01 §4 | [ ] |
| 7 | §7 | `Searchable PDF (OCR)` sandwich writer (`3 Tr` invisible text), `recto-cli ocr` & UI | §2, §6 | [ ] |
| 8 | §8 | Native `TableExtractor` (ruled-grid & borderless clustering, `.csv`/`.tsv`/`.json`, CLI) | §1, D03 T01 §1 | [ ] |
| 9 | §9 | Native lossless `ImageExtractor` (`DCTDecode` `.jpg`, `/SMask` `.png`/`.tiff`, CLI) | §1, D01 T01 §8 | [ ] |
| 10 | §10 | Viewer `Table & Image Extractor Bar` (`SPEC 8.2`), `Preview Table` modal & Convert card | §8, §9, D02 T02 §8, D03 T01 §4, D04 T01 §4 | [ ] |
| 11 | §11 | `PdfOptimizer` (audit, downsampling, font subsetting) and Fast Web View `Linearizer` | §2 | [ ] |
| 12 | §12 | `Optimize & Compress PDF` Convert card (`SPEC 9.5`) and `Audit Dialog` (`SPEC 9.9`) | §11, D04 T01 §4, D02 T01 §10 | [ ] |
| 13 | §13 | Create PDF from a scanner (Windows Image Acquisition) | §5, §7, D04 T01 §3 | [ ] |
| 14 | §14 | `ClipboardPdfBuilder`: clipboard images and image or PDF file lists to PDF | §5, D04 T01 §1 | [ ] |
| 15 | §15 | **Create PDF from clipboard** command, new unsaved tab, `Ctrl+V` and Home entry points | §14, D02 T01 §3, D02 T01 §5, D02 T01 §8, D02 T01 §12, D02 T03 §1, D02 T03 §11, D04 T01 §5, D02 T04 §9 | [ ] |
| 16 | §16 | Clipboard text, rich text, HTML and Office, Markdown file lists through their converters | §14, D04 T04 §1, D04 T04 §2, D04 T04 §3, D04 T04 §5, D04 T04 §6 | [ ] |
| 17 | §17 | Image-only PDF engine (`image_only_pdf.cpp`) and `recto-cli rasterize-pdf` | §2, §3, §11 | [ ] |
| 18 | §18 | Image-only PDF Convert card and Step 3 options | §17, D04 T01 §4 | [ ] |
| 19 | §19 | Editable OCR writer: recognized text as live text objects in matched fonts | §7, D03 T01 §7, D03 T01 §8, D03 T04 §1, D03 T04 §2, D03 T04 §4 | [ ] |
| 20 | §20 | Editable text output mode on the Searchable PDF (OCR) card and `Ctrl+K` | §19, D04 T01 §4, D03 T04 §11 | [ ] |
| 21 | §21 | Page area export engine (image or text) and `recto-cli export-area` | §1, D03 T01 §8 | [ ] |
| 22 | §22 | Export Area tool on the extractor bar, `Ctrl+K` commands and save dialog | §10, §21, D02 T03 §11 | [ ] |
| 23 | §23 | More image inputs to PDF: ICO, JFIF, DIB, HEIC, WebP, JPEG XL via installed codecs | §5 | [ ] |
| 24 | §24 | New blank PDF engine: size presets, paper styles and `recto-cli new-pdf` | §2 | [ ] |
| 25 | §25 | New blank PDF dialog, File menu entry and `Ctrl+K` command | §24, §15, D02 T01 §10, D02 T03 §1, D02 T03 §11, D02 T04 §5 | [ ] |

## 1. `AtomicFileWriter` (`src/core/atomic_file.cpp`) sibling temp-then-replace and rollback

Implement `src/core/atomic_file.h` and `src/core/atomic_file.cpp` (`recto::core::AtomicFileWriter`): enforce the non-negotiable foundation plan rule that every file write in Recto creates a uniquely named sibling temporary file in the destination folder (`.<filename>.recto-<random>.tmp`), streams bytes into it, flushes OS buffers via `FlushFileBuffers`, and atomically replaces the target file via `ReplaceFileW` / `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` only when `commit()` is called, while its destructor unlinks the temporary file automatically if `commit()` was not reached (cancellation, error, or early return).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `AtomicFileWriter` in `src/core/atomic_file.h/.cpp` using Win32 `CreateFileW`, `WriteFile`, `FlushFileBuffers`, `ReplaceFileW` / `MoveFileExW`, and `DeleteFileW` in the destructor on uncommitted exit. Done when: writing and committing a new file or replacing an existing file succeeds atomically on NTFS/ReFS volumes.
2. Prove the atomic cancellation and rollback guarantees in `AtomicFile.*` tests: aborting mid-write or tripping `CancelToken` deletes the sibling `.tmp` file and leaves any pre-existing target file byte-for-byte untouched (verified by SHA-256). Done when: `AtomicFile.*` passes 100% under ASan.

- [ ] Define `AtomicFileWriter` in `src/core/atomic_file.h` (non-copyable, move-enabled RAII class with `open(target_path, CancelToken)`, `write(span<const uint8_t>)`, `commit()`, `abort()`, and `temp_path()`). Done when: `src/core/atomic_file.h` compiles across `recto-core`, `recto-services`, and `recto-cli`.
- [ ] Generate collision-free sibling temporary paths in the same directory as `target_path` (`<parent>/.<stem>.recto-<crypt-random-hex>.tmp`) using `BCryptGenRandom` so `MoveFileExW` is always an atomic same-volume rename and never crosses mount points. Done when: `AtomicFile.SiblingTempPath` confirms `temp_path().parent_path() == target_path.parent_path()`.
- [ ] Check `CancelToken::is_cancelled()` on every `write()` call and before `commit()`, returning `PdfErrorCode::Cancelled` immediately when tripped. Done when: tripping `CancelToken` mid-stream aborts `write()` on the next chunk.
- [ ] Flush file buffers to disk via `FlushFileBuffers` and close the handle inside `commit()`, then branch on the target: a new file is published with `MoveFileExW(temp_path, target_path, MOVEFILE_WRITE_THROUGH)`, and an existing file only through the `ReplaceFileW` step below, never `MOVEFILE_REPLACE_EXISTING`. Done when: `AtomicFile.CommitNewAndReplace` writes new files with `MoveFileExW` and replaces existing files only through `ReplaceFileW`.
- [ ] Ensure `~AtomicFileWriter()` closes any open handle and calls `DeleteFileW(temp_path)` whenever `committed_ == false`. Done when: `AtomicFile.DestructorRollback` proves that scope exit without `commit()` leaves zero `.tmp` files in the directory and leaves an existing target file's SHA-256 unchanged. Cheaper substitute: writing directly to `target_path` with `std::ofstream`, which truncates and corrupts the user's file on cancel or error.
- [ ] Surface descriptive `PdfError::IoError` messages when the destination directory is read-only or the target file is locked by another process (`ERROR_SHARING_VIOLATION`). Done when: `AtomicFile.LockedTargetError` verifies an open sharing lock on `target_path` fails `commit()` cleanly, deletes the `.tmp` file, and leaves the locked file intact.
- [ ] When the target already exists, call `ReplaceFileW(target, temp, backup, 0, nullptr, nullptr)` (no `REPLACEFILE_WRITE_THROUGH`, which Microsoft documents as unsupported; durability comes from the earlier `FlushFileBuffers`) so the destination keeps its ACL, attributes, creation time and alternate streams, and reconcile each documented failure state on its own terms: `ERROR_UNABLE_TO_REMOVE_REPLACED` (1175) and `ERROR_UNABLE_TO_MOVE_REPLACEMENT` (1176, with a backup name given) leave both files under their original names, so delete the temp file and report the failure with the original untouched; `ERROR_UNABLE_TO_MOVE_REPLACEMENT_2` (1177) leaves the original under the backup name, so move the backup back to `target` and delete the temp file, reporting the backup path if that move fails. Done when: `AtomicFile.ReplacePreservesMetadata` keeps the ACL and creation time, and `AtomicFile.ReplacePartialFailure` ends each of the three injected states with the original content at `target`, or a reported backup path holding it.
- [ ] Accept an optional `verify(temp_path)` callback that `commit()` runs after `FlushFileBuffers` and before the replace, aborting with the original untouched when it fails. Done when: `AtomicFile.VerifyBeforeReplace` proves a failing callback leaves the destination hash unchanged.
- [ ] Add `AtomicFile.*` unit tests in `tests/recto-tests/atomic_file_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=AtomicFile.*` passes under ASan.

- [ ] Commit: `AtomicFileWriter` (`src/core/atomic_file.cpp`) sibling temp-then-replace and RAII rollback helper

**Test checkpoint:** `recto-tests.exe --gtest_filter=AtomicFile.*` passes under ASan.

## 2. Native `PdfWriter` (`writer.cpp`: object allocation, `FlateDecode`, `/Pages`, `xref`)

Implement `src/pdf/writer.h` and `src/pdf/writer.cpp` (`recto::pdf::PdfWriter`): allocate indirect objects, serialize all `PdfValue` types (`Null`, `Bool`, `Int`, `Real`, literal/hex `String`, `Name`, `Array`, `Dict`, `Stream`), compress stream payloads with `FlateDecode` (RFC 1950/1951 zlib/deflate), construct `/Catalog` and balanced `/Pages` trees (`<= 16` kids per intermediate `/Pages` node), and write standard PDF 1.7 / 2.0 files with byte-exact `xref` tables and trailer dictionaries (`/Size`, `/Root`, `/Info`, `/ID` SHA-256 file identifiers).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement RFC 1950/1951 `flate_compress(span<const uint8_t>)` in `src/pdf/filters.cpp` and implement `PdfWriter` (`alloc_object_id`, `write_object`, `write_compressed_stream`, `add_page`, `finalize`) in `src/pdf/writer.cpp`. Done when: `PdfWriter.*` creates a multi-page PDF from scratch that `recto::pdf::Document::open` parses with zero XRef warnings and renders cleanly.
2. Integrate `PdfWriter` with `AtomicFileWriter` (`§1`) so streaming a multi-hundred-page PDF flushes indirect objects incrementally to the sibling temp file without buffering the entire output PDF in RAM. Done when: `PdfWriter.StreamingAndRoundTrip` writes a 100-page document and verifies all objects, streams, and `/ID` hashes under ASan.

- [ ] Implement RFC 1950/1951 `flate_compress` (LZ77 sliding window + Huffman block encoding with Adler-32 trailer) in `src/pdf/filters.cpp`, verified by round-tripping through `D01 T01 §7`'s `flate_decompress`. Done when: `PdfFilters.FlateCompressRoundTrip` compresses and decompresses empty, repetitive, and random binary buffers byte-identically.
- [ ] Implement `PdfValue` canonical serializer in `src/pdf/writer.cpp` (formatting reals with bounded precision and no scientific notation `1e-5` per ISO 32000-1 section 7.3.3, escaping `()`, `\`, and `#XX` in strings and names, and tracking exact byte offsets of every `N 0 obj`). Done when: `PdfWriter.ValueSerialization` round-trips every `PdfValue` variant through `D01 T01 §4`'s parser.
- [ ] Build balanced `/Pages` trees (`add_page(page_dict)`) automatically grouping page leaves into intermediate `/Pages` nodes (`max_kids = 16`) with inherited or explicit `/MediaBox` and accurate `/Count` totals. Done when: a 50-page document written by `PdfWriter` produces a 2-level `/Pages` tree with `/Count 50` at the root.
- [ ] Serialize the `%PDF-1.7\n%\xE2\xE3\xCF\xD3\n` binary header, indirect object bodies, `20-byte` fixed-width `xref` table entries (`%010llu %05u n \r\n`), `trailer << /Size ... /Root ... /Info ... /ID [<hex> <hex>] >>` (computed via CNG SHA-256 over document metadata and byte size), and `startxref\n<offset>\n%%EOF\n`. Done when: `PdfWriter.XrefExactness` opens the written PDF and confirms `used_xref_recovery() == false`. Cheaper substitute: writing uncompressed streams or invalid placeholder `xref` offsets that rely on damaged-file recovery.
- [ ] Stream indirect objects directly to an `ISink` / `AtomicFileWriter` (`§1`) as they are finalized so writing large image or multi-page documents keeps memory proportional to a single page rather than the total file size. Done when: `PdfWriter.StreamToAtomicFile` writes a multi-page PDF and verifies atomic rollback on mid-write cancel.
- [ ] Add `PdfWriter.*` unit tests in `tests/recto-tests/pdf_writer_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfWriter.*` passes under ASan.
- [ ] Commit: `pdf: add PdfWriter and AtomicFileWriter temp-then-replace helper`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AtomicFile.*:PdfWriter.*` passes under ASan.

## 3. Streamed `PDF -> Images` (`PNG`, `JPEG`, `BMP`, Multipage `TIFF`, `GIF`) and CLI command

Implement `src/core/pdf_to_images.h/.cpp` in `recto-core` and `recto-cli pdf-to-images`: stream PDF pages one at a time at `72`, `150`, `300`, `600`, or `1200` DPI through `D01 T02 §6`'s Direct2D rasterizer and WIC encoders (`GUID_ContainerFormatPng` with optional transparent page background, `Jpeg` with quality `1-100`, `Bmp`, single-page or **Multipage `TIFF`** with `LZW`, `CCITT Group 4` 1-bit fax monochrome, `JPEG`, or `Uncompressed` compression, and **Animated `GIF`** with configurable frame delay) in `RGB`, `Grayscale`, or `Black & White 1-bit` colour spaces, committing every output file through `AtomicFileWriter` (`§1`).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement single-page raster encoders (`PNG`, `JPEG`, `BMP`, single-page `TIFF`) in `src/core/pdf_to_images.cpp` with `IWICBitmapFrameEncode::SetResolution(dpi, dpi)` and colour-space conversion (`BGRA32 -> RGB24 / Gray8 / BlackWhite1Bit` Otsu/dithered). Done when: `ConvertImages.SinglePageFormats` exports corpus pages at `72`, `150`, `300`, `600`, and `1200` DPI and verifies pixel dimensions and WIC DPI metadata headers.
2. Implement multi-frame **Multipage `TIFF`** (`WICTiffCompressionLZW`, `WICTiffCompressionCCITT4` 1-bit, `WICTiffCompressionNone`) and multi-frame **Animated `GIF`** (GCE `/grctlext` `/Delay` frame timing and 256-colour palette quantization), plus `recto-cli pdf-to-images`. Done when: `ConvertImages.MultipageTiffAndGif` verifies frame counts, `CCITT4` compression tags, and bounded single-page memory under ASan.

- [ ] Define `ImageExportFormat` (`Png`, `Jpeg`, `TiffSinglePage`, `TiffMultipage`, `Bmp`, `AnimatedGif`), `TiffCompressionMode` (`Lzw`, `CcittGroup4`, `Jpeg`, `None`), `ImageColourSpace` (`Rgb`, `Grayscale`, `Monochrome1Bit`), and `PdfToImagesOptions` (`format`, `dpi`, `jpeg_quality`, `tiff_compression`, `colour_space`, `transparent_background`, `gif_frame_delay_ms`, `page_range`) in `src/core/pdf_to_images.h`. Done when: `src/core/pdf_to_images.h` compiles cleanly across `recto-core`, `recto-services`, and `recto-cli`.
- [ ] Rasterize pages one at a time on the job worker thread at target `dpi` (`72..1200`) and encode immediately via WIC (`IWICBitmapEncoder`) into `AtomicFileWriter` so only one page bitmap resides in RAM at any instant. Done when: exporting a 50-page PDF at `300 DPI` holds at most 1 page bitmap buffer in memory. Cheaper substitute: rendering all pages into RAM before encoding.
- [ ] Write exact `dpi x dpi` resolution metadata via `IWICBitmapFrameEncode::SetResolution(dpi, dpi)` and support `JPEG` `ImageQuality` (`0.01f..1.0f`) via `IPropertyBag2` and `PNG` transparent background (`clear_color = transparent` when `transparent_background == true`). Done when: WIC decoder readback of exported `.png` and `.jpg` files reports the exact requested DPI and alpha channel.
- [ ] Implement colour-space conversion from `32-bpp BGRA` to `8-bpp Grayscale` (`GUID_WICPixelFormat8bppGray`) and `1-bpp Monochrome` (`GUID_WICPixelFormat1bppIndexed` with adaptive thresholding) for `PNG`, `BMP`, and `TIFF`. Done when: `1-bit` monochrome exports produce true `1-bpp` pixel formats.
- [ ] Implement **Multipage `TIFF`** encoding appending one `IWICBitmapFrameEncode` per page into a single `.tiff` container with `TiffCompressionMethod` set to `WICTiffCompressionLZW` or `WICTiffCompressionCCITT4` (automatically converting frames to `1-bpp` for `CCITT4`). Done when: `ConvertImages.MultipageTiff` exports a 5-page PDF to a single `.tiff` and `IWICBitmapDecoder::GetFrameCount` returns `5`.
- [ ] Implement **Animated `GIF`** encoding (`GUID_ContainerFormatGif`) writing global/frame palettes (`IWICPalette::InitializeFromBitmap`) and Graphic Control Extension (`" /grctlext/Delay"`) metadata per page frame. Done when: `ConvertImages.AnimatedGif` exports a 3-page PDF to `.gif` with `3` frames and verified frame delay metadata.
- [ ] Implement `recto-cli pdf-to-images <in.pdf> --out <dir_or_file> [--format png|jpg|tiff|multipage-tiff|bmp|gif] [--dpi <72..1200>] [--tiff-compression lzw|ccitt4|none] [--pages <range>]` in `src/cli/cmd_pdf_to_images.cpp`. Done when: `recto-tests.exe --gtest_filter=ConvertImages.*:Cli.PdfToImages` passes under ASan.
- [ ] Add the `LongImage` format to `ImageExportFormat` with `long_image_container` (`Png`, `Jpeg`, `Tiff`), `long_image_gap_pt` (`0`, `8`, `16`) and `long_image_max_height_px` (default `30000`, hard caps `65500` for JPEG and `1000000` for PNG and TIFF): stitch the requested pages top to bottom into one tall image at the widest page's pixel width, centre narrower pages on a white background, compute the final height from the page sizes first, and stream each page into the WIC encoder with `IWICBitmapFrameEncode::WritePixels` in bands so memory stays at one page. Done when: `ConvertImages.LongImage` stitches a 5-page PDF at `150` DPI into one image whose height equals the sum of the page heights and gaps, with at most one page bitmap alive. Cheaper substitute: rendering all pages into one huge bitmap in RAM.
- [ ] When the stitched height would exceed `long_image_max_height_px`, close the file at the last page that fits and continue in `name-part-2` and so on, reporting the split in the job note instead of failing or truncating. A single page whose height at the chosen DPI exceeds the cap cannot be split and is refused with a typed `PageTooTallForLongImage` error naming the page and the DPI that would fit, so no part is ever empty or over the cap. Done when: `ConvertImages.LongImageSplit` splits a 40-page export into numbered parts that together contain every page once, and `ConvertImages.LongImageOversizedPage` refuses a single page taller than the cap with the typed error and writes no part.
- [ ] Add `--format long-png|long-jpg|long-tiff [--gap <pt>] [--max-height <px>]` to `recto-cli pdf-to-images`. Done when: `Cli.PdfToImages` stitches a corpus PDF and the output height is asserted.

- [ ] Commit: streamed `PDF -> Images` converter (`PNG`, `JPEG`, `BMP`, single/multipage `TIFF` `LZW`/`CCITT4`, `Animated GIF`) and `recto-cli pdf-to-images`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ConvertImages.*:Cli.PdfToImages` passes under ASan.

## 4. Vector `PDF -> SVG (.svg)` and `PDF -> OpenXPS (.xps)` exporters and Convert Step 3 UI

Implement `src/core/svg_xps_writer.h/.cpp` in `recto-core` (`PDF -> SVG (.svg)` and `PDF -> OpenXPS (.xps)`: exporting `DisplayList` vector paths, strokes, fills, gradients, clip paths, positioned `<text>` / `<Glyphs>` runs, and embedded images to standalone per-page **SVG 1.1 (`.svg`)** files and packaged **OpenXPS (`.xps`)** documents), add `recto-cli pdf-to-svg` and `recto-cli pdf-to-xps`, and wire the **Images, TIFF, SVG & XPS** card and Step 3 options UI in `ConvertPage` (`SPEC 9.5` and `D-Dialogs.png` top-right `"Save 9 images"` button).

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.5 (`Images, TIFF, SVG & XPS` card and Step 3 options) and 9.9 (`D-Dialogs.png` top-right Conversion options card with dynamic `"Save N images"` button label).
**Job:** The user can export any page range of a PDF to scalable vector `.svg` files, packaged `.xps` documents, or raster images/multipage TIFFs/GIFs through the Convert page. Consumer: web browsers, vector illustration tools, XPS viewers, and `D06 T01 §5`'s round-trip suite.
**Treatment:** Direct `DisplayList` command-stream translation into SVG 1.1 XML (`<svg>`, `<g clip-path="...">`, `<path d="...">`, `<text>`, `<image xlink:href="data:image/png;base64,...">`) and ECMA-388 OpenXPS ZIP packages (`FixedDocumentSequence.fdseq`, `FixedDocument.fdoc`, `Pages/1.fpage` with `<Path>`, `<Glyphs>`, `<ImageBrush>`, and embedded font/image resources), paired with the Step 3 options UI. Cheaper substitute that fails the checkpoint: rasterizing whole PDF pages into a single `<image>` tag inside `.svg` or `.xps` instead of emitting real vector `<path>` and `<text>`/`<Glyphs>` elements.
**Chrome:** consume the Convert Step 2 card and Step 3 options host from `D04 T01 §3`-`§4`.

**Build order:**

1. Implement `pdf_to_svg` and `pdf_to_xps` in `src/core/svg_xps_writer.cpp` plus `recto-cli pdf-to-svg` and `recto-cli pdf-to-xps`. Done when: `ConvertSvgXps.*` exports vector-and-text corpus pages to `.svg` and `.xps`, verifies valid XML structure containing `<path>` and `<text>`/`<Glyphs>` elements, and round-trips through §5's `images_to_pdf` reader under ASan.
2. Register the **Images, TIFF, SVG & XPS** card and Step 3 options view (`Format`, `DPI`, `Colour space`, `TIFF compression`, `Transparent background`, `Pages`, `Save to`, `If file exists`, and dynamic `"Save N images"` primary action button) in `ConvertPage`. Done when: the rendered Step 3 options card matches `D-Dialogs.png` and completes raster, multipage TIFF, SVG, and XPS exports through `ConversionRunner`.

- [ ] Implement `pdf_to_svg` in `src/core/svg_xps_writer.cpp` translating `DisplayList` operations (`Save`, `Restore`, `Transform`, `ClipPath`, `FillPath`, `StrokePath`, `DrawGlyphRun`, `DrawImage`, `LinearGradient`, `RadialGradient`) into standalone UTF-8 SVG 1.1 files (`viewBox="0 0 <w> <h>"`) committed via `AtomicFileWriter`. Done when: `ConvertSvgXps.SvgVectorElements` confirms exported `.svg` files contain vector `<path>` and `<text>` nodes rather than a full-page raster bitmap. Cheaper substitute that fails the checkpoint: wrapping a full-page PNG screenshot inside an `<svg><image/></svg>` shell.
- [ ] Implement `pdf_to_xps` in `src/core/svg_xps_writer.cpp` generating a valid ECMA-388 OpenXPS package (`[Content_Types].xml`, `_rels/.rels`, `FixedDocumentSequence.fdseq`, `Documents/1/FixedDocument.fdoc`, `Documents/1/Pages/1..N.fpage` with `<FixedPage>`, `<Canvas>`, `<Path Data="...">`, `<Glyphs UnicodeString="..." FontUri="..."/>`, and embedded font/image resources). Done when: `ConvertSvgXps.OpenXpsPackage` validates package structure and `.fpage` XML across multi-page corpus PDFs.
- [ ] Implement `recto-cli pdf-to-svg <in.pdf> --out <dir_or_file> [--pages <range>]` and `recto-cli pdf-to-xps <in.pdf> --out <out.xps> [--pages <range>]` in `src/cli/`. Done when: `Cli.PdfToSvgAndXps` passes under ASan.
- [ ] Build the **Images, TIFF, SVG & XPS** Step 3 options control (`src/app/views/convert/ImagesConvertOptionsControl.xaml/.cpp`) exposing `Format` (`PNG`, `JPEG`, `Multipage TIFF`, `Single-page TIFF`, `Animated GIF`, `BMP`, `Vector SVG (.svg)`, `OpenXPS (.xps)`), `Resolution` (`72`, `150`, `300`, `600`, `1200 DPI` for raster formats), `Colour` (`RGB`, `Grayscale`, `Black & White 1-bit`), `TIFF compression` (`LZW`, `CCITT Group 4`, `JPEG`, `None` when TIFF is selected), and `JPEG quality` slider. Done when: selecting `Vector SVG` or `OpenXPS` hides raster-only DPI controls and selecting `Multipage TIFF` reveals the compression dropdown.
- [ ] Update the Step 3 primary action button label dynamically (`"Save 9 images"` for a 9-page single-page image export, `"Save Multipage TIFF"`, `"Save 9 SVG files"`, `"Save XPS document"`) matching `D-Dialogs.png`. Done when: `ConvertImagesUi.ButtonLabelAndOptions` verifies dynamic button labels and screenshot parity with `D-Dialogs.png`.
- [ ] Add `Long image (one tall picture)` to the `Format` list of the Images options control with `Page gap` (`None`, `Small`, `Medium`) and `Maximum height` fields shown only for it, and the primary action label `"Save 1 long image"` (or `"Save N long image parts"` when the split applies). Done when: `ConvertImagesUi.LongImageOptions` shows the fields only for that format and the label changes with the page count. Cheaper substitute: a CLI-only format with no Step 3 option.
- [ ] Verify that cancelling a multi-page image, SVG, or XPS export mid-job via `QueueCard` (`D04 T01 §5`) removes all uncommitted temp files. Done when: `ConvertImagesUi.CancelMidJob` passes.
- [ ] Commit: `convert: implement PDF to images, multipage TIFF, GIF, vector SVG and OpenXPS`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ConvertImages.*:ConvertSvgXps.*:ConvertImagesUi.*` passes under ASan, and the Convert Images UI screenshot matches `D-Dialogs.png`.

## 5. `Images, SVG & XPS -> PDF` converter (`DCTDecode` pass-through, TIFF, EXIF) & Convert UI

Implement `src/core/images_to_pdf.h/.cpp` (`Images, SVG & XPS -> PDF`), `recto-cli images-to-pdf`, and the Convert UI flow when image or vector files (`.png`, `.jpg`, `.jpeg`, `.tif`, `.tiff`, `.bmp`, `.gif`, `.svg`, `.xps`) are loaded in Step 1: pack one or multiple images, all frames of a multipage TIFF, vector `.svg` graphics, or `.xps` pages into a single PDF (or one PDF per file) via `PdfWriter` (`§2`), reading EXIF orientation (`System.Photo.Orientation`) so rotated camera photos appear upright, embedding JPEG streams directly as `/Filter /DCTDecode` image XObjects without lossy re-compression, preserving PNG/TIFF alpha via `/SMask`, and translating `.svg` and `.xps` vector paths and text into native PDF content streams.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 (Convert flow accepting image, multipage TIFF, SVG and XPS inputs to build a PDF).
**Job:** The user can turn a set of photos, multipage TIFF scans, vector SVGs, or XPS files into a clean, standards-compliant PDF without degrading JPEG quality or rasterizing vector SVGs. Consumer: the viewer (can open the result immediately via `Open file`) and `D06 T01 §5`'s round-trip test.
**Treatment:** Direct `DCTDecode` byte-stream embedding for JPEGs, `FlateDecode` RGB/Gray + `/SMask` soft-mask alpha for PNG/TIFF/BMP/GIF (expanding all frames of multipage TIFFs), vector path/text translation for `.svg` and `.xps`, EXIF orientation transform applied to the page/image `cm` matrix, and `Original image size` vs `Fit to A4 / Letter` page sizing with configurable margins. Cheaper substitute that fails the checkpoint: decoding JPEGs to bitmaps and re-encoding them as lossy JPEGs, reading only frame 0 of a multipage TIFF, rasterizing `.svg`/`.xps` inputs, or ignoring EXIF orientation tags.
**Chrome:** consume the Convert Step 1 file chips and Step 3 options host from `D04 T01 §3`-`§4`.

**Build order:**

1. Implement `images_to_pdf` in `src/core/images_to_pdf.cpp` (lossless JPEG `DCTDecode` stream pass-through, EXIF `System.Photo.Orientation` `1..8` matrix transform, multipage TIFF frame loop, PNG/TIFF alpha `/SMask` separation, and `.svg` / `.xps` vector-to-PDF content-stream translation) plus `recto-cli images-to-pdf <in1> [in2...] --out <out.pdf>`. Done when: `ConvertImagesToPdf.*` verifies byte-for-byte JPEG stream identity, upright EXIF rendering, 3-frame TIFF expansion into 3 PDF pages, and vector SVG/XPS preservation under ASan.
2. Wire image, TIFF, SVG, and XPS inputs and Step 3 options (`Page size`: `Fit to image`, `A4`, `Letter`; `Orientation`: `Auto`, `Portrait`, `Landscape`; `Margins`: `None`, `Small`, `Standard`; `Combine all files into one PDF` toggle) into `ConvertPage`. Done when: dropping mixed images and SVGs onto `ConvertPage` produces a combined PDF through `ConversionRunner`.

- [ ] Implement lossless JPEG pass-through in `src/core/images_to_pdf.cpp`: parse JPEG SOF0/SOF2 markers for pixel dimensions and colour components (`DeviceGray`, `DeviceRGB`, `DeviceCMYK`) and embed the raw JPEG file bytes directly as `/Subtype /Image /Filter /DCTDecode` without WIC bitmap decoding/re-encoding. Done when: `ConvertImagesToPdf.LosslessJpegPassThrough` confirms the embedded `/DCTDecode` stream bytes inside the output PDF match the input `.jpg` byte-for-byte. Cheaper substitute that fails the checkpoint: decoding JPEGs to bitmaps and re-compressing them.
- [ ] Query EXIF orientation (`System.Photo.Orientation` `1..8` via `IWICMetadataQueryReader`) and apply the corresponding rotation/mirroring `cm` transformation matrix and swapped page `/MediaBox` dimensions so rotated camera JPEGs and TIFFs render upright. Done when: `ConvertImagesToPdf.ExifOrientation` converts a `90° CW` EXIF-tagged JPEG and verifies the rendered PDF page is upright.
- [ ] Expand all frames (`0..frame_count-1`) of multipage `.tif`/`.tiff` and `.gif` files into sequential PDF pages, and split `32-bpp BGRA` PNG/TIFF images with non-opaque alpha into an RGB `/FlateDecode` image XObject plus an `8-bpp DeviceGray` `/SMask` soft-mask stream. Done when: `ConvertImagesToPdf.MultipageTiffAndAlpha` converts a 3-frame TIFF and a translucent PNG into a 4-page PDF with preserved transparency.
- [ ] Translate vector `.svg` (`<path>`, `<rect>`, `<circle>`, `<ellipse>`, `<line>`, `<polyline>`, `<polygon>`, `<text>`, `<g transform="...">`) and OpenXPS `.xps` (`.fpage` `<Path>`, `<Glyphs>`, `<Canvas>`) inputs into native PDF vector content-stream operators (`m`, `l`, `c`, `h`, `f`, `S`, `BT ... Tj ET`) via `PdfWriter`. Done when: `ConvertImagesToPdf.VectorSvgAndXps` produces a PDF from `.svg` and `.xps` inputs whose `PageText` and vector paths are preserved without rasterization.
- [ ] Support page layout options (`FitPageToImage`, `A4`, `Letter`, `Auto`/`Portrait`/`Landscape` orientation, margin inset `0 pt` / `18 pt` / `36 pt`, and `Combine into one PDF` vs `One PDF per file`). Done when: `ConvertImagesToPdf.PageSizing` verifies exact `/MediaBox` dimensions for each layout mode.
- [ ] Implement `recto-cli images-to-pdf <in1> [in2...] --out <out.pdf> [--page-size original|a4|letter] [--margin <pt>]` in `src/cli/cmd_images_to_pdf.cpp` and wire the Step 3 UI in `ConvertPage`. Done when: `recto-tests.exe --gtest_filter=ConvertImagesToPdf.*:Cli.ImagesToPdf` passes under ASan.
- [ ] Commit: `convert: implement images, multipage TIFF, SVG and XPS to PDF`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ConvertImagesToPdf.*:Cli.ImagesToPdf` passes under ASan.

## 6. `PDF -> Plain Text (.txt)` exporter with automatic OCR fallback, CLI and Convert UI

Implement `src/core/pdf_to_text.h/.cpp` in `recto-core`, `recto-cli pdf-to-text <in.pdf> --out <out.txt> [--pages <range>] [--ocr]`, and the **Plain text (`.txt`)** card and Step 3 options in `ConvertPage` (`SPEC 9.5`): extract reading-order UTF-8 text per page via `PageText` (`D03 T01 §1`), automatically running `OcrPipeline` (`D03 T01 §8`) on scanned and vector-outlined-text pages so scanned PDFs export complete UTF-8 text instead of an empty `.txt` file, and commit the output via `AtomicFileWriter` (`§1`).

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 (`Plain text (.txt)` card and Step 3 options).
**Job:** The user can export clean reading-order UTF-8 text from any digital, scanned, or mixed PDF document. Consumer: text editors, scripts, and `D06 T01 §5`'s round-trip suite.
**Treatment:** Streamed page-by-page `PageText` extraction with automatic `OcrPipeline` (`D03 T01 §8`) invocation on pages classified as `ScannedRaster` or `VectorOutlinedText` (`D03 T01 §7`), writing UTF-8 text through `AtomicFileWriter`. Cheaper substitute that fails the checkpoint: producing a 0-byte `.txt` file on scanned PDFs.
**Chrome:** consume the Convert Step 2 `Plain text` card and Step 3 options host from `D04 T01 §3`-`§4`.

**Build order:**

1. Implement `pdf_to_text` in `src/core/pdf_to_text.cpp` (reading-order `PageText` extraction, automatic `OcrPipeline` fallback on scanned/vector-outlined pages, configurable page-break separators, and UTF-8 / UTF-8-with-BOM / CRLF vs LF options) plus `recto-cli pdf-to-text`. Done when: `ConvertTextAndOcr.PlainText` exports both digital multi-column corpus PDFs and `tests/corpus/text/scanned.pdf` into non-empty reading-order `.txt` files under ASan.
2. Register the **Plain text (`.txt`)** card and Step 3 options (`Include page break markers`, `Automatically OCR scanned pages`, `OCR language`, `Pages`, `Save to`, `If file exists`) in `ConvertPage`. Done when: `ConvertTextAndOcr.PlainTextUi` runs a `.txt` export through `ConversionRunner` and verifies cancellation leaves no partial files.

- [ ] Define `PdfToTextOptions` (`page_range`, `auto_ocr_scanned_pages`, `ocr_vector_outlines`, `ocr_language_bcp47`, `insert_page_break_headers`, `use_crlf`, `utf8_bom`) in `src/core/pdf_to_text.h`. Done when: `src/core/pdf_to_text.h` compiles cleanly across `recto-core`, `recto-services`, and `recto-cli`.
- [ ] Implement `pdf_to_text` in `src/core/pdf_to_text.cpp` iterating requested pages, extracting `PageText` (`D03 T01 §1`), classifying the page via `classify_page_for_ocr` (`D03 T01 §7`), and running `OcrPipeline::recognize_page` (`D03 T01 §8`) whenever a page is `ScannedRaster` or `VectorOutlinedText` and `auto_ocr_scanned_pages` is enabled. Done when: converting `tests/corpus/text/scanned.pdf` writes the recognized text with `> 98%` character accuracy. Cheaper substitute that fails the checkpoint: skipping OCR and writing an empty `.txt` file on scanned PDFs.
- [ ] Format output text with optional `--- Page N ---` separators or form-feed `\f` characters between pages, and write through `AtomicFileWriter` (`§1`) while reporting per-page progress to `CancelToken`. Done when: cancelling a multi-page `.txt` export deletes the sibling `.tmp` file cleanly.
- [ ] Implement `recto-cli pdf-to-text <in.pdf> --out <out.txt> [--pages <range>] [--no-ocr] [--lang <tag>]` in `src/cli/cmd_pdf_to_text.cpp`. Done when: `Cli.PdfToText` passes under ASan.
- [ ] Register the **Plain text** card (`ConverterCardDescriptor`) and Step 3 options control in `ConvertPage` (`D04 T01 §3`-`§4`) with the SPEC Principle 4 honest hint line (`"Extracts reading-order UTF-8 text; scanned pages use built-in Windows OCR."`). Done when: `ConvertTextAndOcr.PlainTextUi` passes.
- [ ] Add `ConvertTextAndOcr.PlainText*` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=ConvertTextAndOcr.PlainText*` passes under ASan.

- [ ] Commit: `PDF -> Plain Text (.txt)` exporter with automatic OCR fallback, `recto-cli pdf-to-text`, and Convert Step 3 UI

**Test checkpoint:** `recto-tests.exe --gtest_filter=ConvertTextAndOcr.PlainText*:Cli.PdfToText` passes under ASan.

## 7. `Searchable PDF (OCR)` sandwich writer (`3 Tr` invisible text), `recto-cli ocr` & UI

Implement `src/core/searchable_pdf.h/.cpp` in `recto-core`, `recto-cli ocr <in.pdf> --out <searchable.pdf>` (`recto-cli ocr --out`), and the **Searchable PDF (OCR)** card and Step 3 options in `ConvertPage` (`SPEC 9.5`): take a scanned or mixed PDF, run `OcrPipeline` (`D03 T01 §8`) on pages that lack extractable text (or pages with vector-outlined text when enabled), and write a new PDF via `PdfWriter` (`§2`) that preserves the original page visual stream untouched while appending a calibrated invisible text layer (`BT 3 Tr ... Tf ... Tz ... Tm (...) Tj ET`) aligned to each recognized `OcrWord` quad so the output PDF is selectable and searchable in every PDF viewer.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 (`Searchable PDF (OCR)` card and Step 3 options).
**Job:** The user can turn a scanned image-only PDF (or a PDF with outlined curve text) into a permanently selectable, searchable PDF without altering a single visible pixel of the original pages. Consumer: Recto's viewer and any third-party PDF reader opening the saved searchable PDF.
**Treatment:** Non-destructive original `/Contents` preservation wrapped in `q ... Q` plus an appended `3 Tr` (neither fill nor stroke text rendering mode) invisible text content stream using an embedded Unicode-mapped font (`/ToUnicode` CMap) with horizontal scaling (`Tz`) calibrated so every word's advance width matches its `OcrWord` bounding box in PDF page points. Cheaper substitute that fails the checkpoint: drawing visible text over the scan, or rasterizing existing digital vector pages when only the scanned pages needed an OCR layer.
**Chrome:** consume the Convert Step 2 `Searchable PDF (OCR)` card and Step 3 options host from `D04 T01 §3`-`§4`.

**Build order:**

1. Implement `create_searchable_pdf` in `src/core/searchable_pdf.cpp` and `recto-cli ocr <in.pdf> --out <searchable.pdf> [--lang <tag>] [--deskew] [--vector-outlines] [--pages <range>]`. Done when: running `create_searchable_pdf` on `tests/corpus/text/scanned.pdf` produces a PDF that (a) has native extractable `PageText` matching the OCR text without re-running OCR, (b) has word quads aligned to the scan image, and (c) renders pixel-identically (`compare_rgba` `0` diff pixels) to the input scan under ASan.
2. Register the **Searchable PDF (OCR)** card and Step 3 options (`OCR language`, `Skip pages that already have selectable text`, `OCR vector-outlined text curves`, `Deskew & enhance scan before OCR`, `Pages`, `Save to`, `If file exists`) in `ConvertPage`. Done when: `ConvertTextAndOcr.SearchablePdfUi` completes a searchable-PDF job through `ConversionRunner`.

- [ ] Define `SearchablePdfOptions` (`page_range`, `ocr_language_bcp47`, `skip_pages_with_existing_text`, `ocr_vector_outlined_pages`, `preprocess_deskew_and_enhance`) in `src/core/searchable_pdf.h`. Done when: `src/core/searchable_pdf.h` compiles cleanly.
- [ ] Preserve pages that already contain digital selectable text untouched when `skip_pages_with_existing_text == true` so mixed digital + scanned PDFs never get duplicate text layers or rasterized vector pages. Done when: `ConvertTextAndOcr.MixedDigitalAndScanned` leaves digital page streams untouched while adding `3 Tr` text only to the scanned pages.
- [ ] Synthesize the `3 Tr` invisible text stream (`q BT 3 Tr /F_OCR <font_size> Tf <h_scale> Tz <a b c d e f> Tm <hex_glyphs> Tj ET Q`) for each recognized `OcrWord` in `src/core/searchable_pdf.cpp`, calibrating `<font_size>` to the word box height and `<h_scale> Tz` so the word's glyph advance width matches `word_box.width` to within `0.1 pt`. Done when: selecting a recognized word in the output PDF highlights the exact bounding box of the word in the scanned image. Cheaper substitute that fails the checkpoint: visible text (`0 Tr`) that defaces the scanned image or unscaled `100 Tz` runs whose selection boxes drift off the scanned words.
- [ ] Attach a compact Unicode `/Type0` / `/CIDFontType2` font resource with a complete `/ToUnicode` CMap to `/Resources /Font /F_OCR` on each OCR'd page so non-ASCII and accented characters extract cleanly as UTF-8 in any PDF reader. Done when: `PageText` extraction (`D03 T01 §1`, with OCR disabled) on the saved searchable PDF extracts all recognized UTF-8 words and quads directly from the content stream.
- [ ] Verify pixel-exact visual preservation: rasterizing `scanned.pdf` before and after `create_searchable_pdf` produces `0` differing pixels (`expect_rgba_equal`) because `3 Tr` performs neither fill nor stroke. Done when: `ConvertTextAndOcr.SearchablePdfVisualIdentity` passes.
- [ ] Extend `recto-cli ocr <in.pdf> --out <out.pdf>` in `src/cli/cmd_ocr.cpp` and register the **Searchable PDF (OCR)** card and Step 3 options in `ConvertPage`. Done when: `ConvertTextAndOcr.SearchablePdfUi` passes end-to-end.
- [ ] Commit: `convert: add PDF to text and Searchable PDF (OCR) invisible-text writer`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ConvertTextAndOcr.*` passes under ASan (verifying both `.txt` output and `3 Tr` searchable PDF visual identity and native `PageText` readback on `scanned.pdf`).

## 8. Native `TableExtractor` (ruled-grid & borderless clustering, `.csv`/`.tsv`/`.json`, CLI)

Implement `src/core/table_extractor.h/.cpp` (`recto::core::TableExtractor`) and `recto-cli extract-tables <in.pdf> --out <dir_or_file> [--format csv|tsv|json] [--pages <range>]`: detect tables on any PDF page using a dual algorithm -- (a) **Orthogonal vector ruling lines** (horizontal and vertical path segments and thin filled rectangles forming grid intersections, `rowspan`/`colspan` merged cells, borders, and cell background shading) and (b) **Whitespace projection & column/row baseline clustering** for borderless tables (invoices, financial statements, schedules) -- plus scanned-page support via `OcrPipeline` (`D03 T01 §8`).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the orthogonal ruling-line grid detector and the borderless whitespace column/row clusterer in `src/core/table_extractor.cpp`, producing `ExtractedTable` models (`rows`, `cols`, `cells` with `row_span`, `col_span`, `text`, `value_kind` `Text`/`Number`/`Currency`/`Percentage`/`Date`, `bounds_pt`, `bg_color`, `borders`). Done when: `TableExtractor.RuledAndBorderless` extracts both bordered grid tables (with merged `colspan`/`rowspan` header cells) and borderless multi-column financial tables under ASan.
2. Implement `.csv` (RFC 4180), `.tsv`, and structured `.json` serializers plus `recto-cli extract-tables` and manual region/divider override support (`extract_table_in_rect(page_index, rect_pt, custom_col_splits, custom_row_splits)` for §10's interactive lasso). Done when: `TableExtractor.*` and `Cli.ExtractTables` pass under ASan.

- [ ] Define `CellValueKind` (`Text`, `Number`, `Currency`, `Percentage`, `IsoDate`), `TableCell` (`row`, `col`, `row_span`, `col_span`, `rect_pt`, `text`, `numeric_value`, `currency_symbol`, `value_kind`, `background_rgba`, `border_mask`), and `ExtractedTable` (`page_index`, `table_index`, `bounds_pt`, `row_dividers_y`, `col_dividers_x`, `cells`) in `src/core/table_extractor.h`. Done when: `ExtractedTable` satisfies the input requirements of `DocxWriter` (`D04 T03 §3`), `XlsxWriter` (`D04 T05 §1`), and `WebDocWriter` (`D04 T05 §3`).
- [ ] Implement Algorithm A (**Orthogonal vector ruling-line grid reconstruction**): collect horizontal and vertical stroke segments (`m`/`l`, `re`) and thin filled rule bars (`<= 3 pt` thick) from the page `DisplayList`, snap near-collinear segments within `1.5 pt`, find H-V intersections, build minimal closed grid rectangles, and detect `rowspan` / `colspan` merged cells where an interior ruling segment is absent. Done when: `TableExtractor.RuledGridWithMerges` reconstructs a bordered table with a 3-column merged title header and 2-row merged category cells.
- [ ] Implement Algorithm B (**Borderless whitespace projection & baseline clustering**): group `PageText` spans into horizontal row bands, project vertical x-intervals across multi-span rows to identify consistent vertical whitespace gutters (`>= 6 pt` wide across `>= 3` aligned rows), and segment borderless financial/invoice tables into columns and rows without false-positive triggering on two-column prose paragraphs. Done when: `TableExtractor.BorderlessFinancialTable` extracts a 4-column borderless invoice line-item table while returning `0` tables on a two-column academic prose page. Cheaper substitute: naive space-delimited line splitting that breaks multi-word cell descriptions into separate columns.
- [ ] Classify each `TableCell::text` into `CellValueKind` (`Currency` e.g. `$1,234.50` / `(€45.00)`, `Percentage` `12.5%`, `Number` `-4,500.25`, `IsoDate` `2026-10-14` / `10/14/2026`, or `Text`) with parsed `double numeric_value`. Done when: `TableExtractor.CellTypeParsing` parses positive/negative/parenthesized currencies, percentages, numbers, and dates accurately.
- [ ] Implement `extract_table_in_rect(page_index, rect_pt, custom_col_x, custom_row_y)` allowing §10's interactive viewer lasso and draggable row/column splitters to re-slice any user-chosen region into an `ExtractedTable`. Done when: adding or dragging a column divider x-coordinate splits cells along that vertical line deterministically.
- [ ] Implement RFC 4180 `.csv`, tab-separated `.tsv`, HTML `<table>` clipboard fragment, and structured `.json` serializers in `src/core/table_extractor.cpp`, plus `recto-cli extract-tables <in.pdf> --out <dir_or_file> [--format csv|tsv|json] [--pages <range>]`. Done when: `Cli.ExtractTables` exports valid `.csv`, `.tsv`, and `.json` files under ASan.
- [ ] Add `TableExtractor.*` unit tests in `tests/recto-tests/table_extractor_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=TableExtractor.*:Cli.ExtractTables` passes under ASan.

- [ ] Commit: native `TableExtractor` engine (ruled-grid + borderless whitespace clustering, `rowspan`/`colspan`, `.csv`/`.tsv`/`.json`, `recto-cli extract-tables`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=TableExtractor.*:Cli.ExtractTables` passes under ASan across ruled, merged-cell, and borderless table fixtures.

## 9. Native lossless `ImageExtractor` (`DCTDecode` `.jpg`, `/SMask` `.png`/`.tiff`, CLI)

Implement `src/core/image_extractor.h/.cpp` (`recto::core::ImageExtractor`) and `recto-cli extract-images <in.pdf> --out <dir> [--min-size <px>] [--pages <range>]`: walk page `/Resources /XObject` dictionaries and nested `/Subtype /Form` XObjects to extract embedded `/Subtype /Image` streams at their native pixel resolution without re-rendering the page -- saving `/DCTDecode` streams without `/SMask` directly as lossless `.jpg` files, decoding `/FlateDecode` / `/JPXDecode` / `/CCITTFaxDecode` / `/JBIG2Decode` + `/SMask` alpha streams into lossless `.png` or `.tiff` files, deduplicating shared XObject references across pages, and filtering out tiny decorative icons (`min_width_px` / `min_height_px`).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `ImageExtractor::list_images` and `extract_images` in `src/core/image_extractor.cpp` walking page and nested Form XObject resources, deduplicating by `(obj_num, gen_num)`, applying minimum pixel-dimension filters, writing lossless `.jpg` / `.png` / `.tiff` files through `AtomicFileWriter` (`§1`), and exposing `recto-cli extract-images`. Done when: `ImageExtractor.*` extracts an embedded JPEG byte-identically to the original JPEG stream and composes an `/SMask` image into a lossless RGBA `.png` under ASan.

- [ ] Define `ExtractedImageInfo` (`page_index`, `image_index_on_page`, `obj_id`, `width_px`, `height_px`, `bits_per_component`, `color_space_name`, `filter_name`, `has_smask`, `rendered_bounds_pt`, `suggested_extension`) and `ImageExtractOptions` (`page_range`, `min_width_px`, `min_height_px`, `deduplicate_shared_xobjects`) in `src/core/image_extractor.h`. Done when: `src/core/image_extractor.h` compiles cleanly.
- [ ] Walk `/Resources /XObject` and recursive `/Subtype /Form` XObjects (bounded to depth 16) in `src/core/image_extractor.cpp`, tracking visited `(obj_num, gen_num)` IDs so a company logo XObject referenced on 50 pages is extracted once when `deduplicate_shared_xobjects == true`. Done when: `ImageExtractor.DeduplicateSharedXObjects` extracts a repeated header XObject once instead of 50 times.
- [ ] Save `/Filter /DCTDecode` (`DeviceRGB` or `DeviceGray`, no `/SMask` or `/Mask`) image streams by writing the raw undecoded JPEG stream bytes directly to `page-<P>-img-<K>.jpg` via `AtomicFileWriter`. Done when: `ImageExtractor.LosslessJpegStream` proves the extracted `.jpg` SHA-256 matches the embedded stream bytes with zero re-encoding loss. Cheaper substitute: rendering the whole PDF page or decoding and re-compressing the JPEG XObject.
- [ ] Decode `/FlateDecode`, `/JPXDecode`, `/LZWDecode`, and `/DCTDecode` + `/SMask` (or `/Mask`) image XObjects at their native `Width x Height` pixel dimensions, composite the `/SMask` alpha channel, and encode to lossless `32-bpp BGRA` / `24-bpp RGB` `.png` via WIC. Done when: `ImageExtractor.PngWithSoftMask` extracts an image with an `/SMask` into a `.png` with intact alpha transparency.
- [ ] Save `1-bit` monochrome `/CCITTFaxDecode` and `/JBIG2Decode` bi-level image XObjects as lossless `1-bpp` `.tiff` (`CCITT Group 4`) or `.png` files at native resolution. Done when: `ImageExtractor.MonochromeFaxImage` extracts a `1-bit` scan XObject at full native resolution.
- [ ] Filter out decorative 1x1 spacer pixels and tiny UI icons when `width_px < min_width_px || height_px < min_height_px` (default `32 x 32 px` when filter toggle is enabled). Done when: `ImageExtractor.MinDimensionFilter` skips a `16 x 16` bullet icon while extracting a `600 x 400` figure.
- [ ] Implement `recto-cli extract-images <in.pdf> --out <dir> [--min-size <px>] [--pages <range>]` in `src/cli/cmd_extract_images.cpp`. Done when: `recto-tests.exe --gtest_filter=ImageExtractor.*:Cli.ExtractImages` passes under ASan.

- [ ] Commit: native `ImageExtractor` engine (lossless XObject `DCTDecode` `.jpg` and `Flate`/`JPX`/`CCITT` + `/SMask` `.png`/`.tiff`, `recto-cli extract-images`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=ImageExtractor.*:Cli.ExtractImages` passes under ASan.

## 10. Viewer `Table & Image Extractor Bar` (`SPEC 8.2`), `Preview Table` modal & Convert card

Build the visual table and image extraction surfaces specified in `design/SPEC.md` sections 8.2 (`Table & Image Extractor Bar`), 9.3 (`Export Selection as Table`), and 9.5 (`Extract Tables & Images` Convert card):
- **Viewer `Table & Image Extractor Bar` (`40 epx` contextual bar, toggled from the floating page toolbar `D02 T02 §8`, Tools menu, `Export Selection as Table` in `D03 T01 §4`, or `Ctrl+K`):** `Auto-Detect Tables on Page`, `Draw Table Region` interactive lasso with draggable row (`-`) and column (`|`) divider lines on canvas, `Preview Table` modal data grid (`Copy to Clipboard`, `Export to .xlsx`, `Export to .csv`, `Export to .json`), and `Extract Page Images`.
- **`Extract Tables & Images` Convert Card (`SPEC 9.5`):** Batch extraction of `All Tables (.xlsx / .csv / .json)`, `All Embedded Images (.png / .jpg / .tiff)`, or `Both` across any page range, with `Skip decorative images smaller than 64 x 64 px` and `OCR scanned tables` toggles.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 8.2 (`Table & Image Extractor Bar`), 9.3 (`Export Selection as Table` context menu action), and 9.5 (`Extract Tables & Images` Convert card and Step 3 options).
**Job:** A reader viewing a page with a table or figure can auto-detect or lasso the table, tweak column/row dividers visually, preview the parsed cells in a grid, and copy/export to Excel/CSV/JSON, or batch-extract every table and embedded image from a PDF in the Convert tab. Consumer: `DocumentView` (`D02 T02`), clipboard, `ConvertPage` (`D04 T01 §4`), and `XlsxWriter` (`D04 T05 §1`).
**Treatment:** Interactive canvas table-grid overlay with draggable `--accent` row/column splitters feeding `TableExtractor::extract_table_in_rect` (`§8`), a modal `TablePreviewDialog` (`ContentDialog` hosted via `D02 T01 §10`), and a registered `Extract Tables & Images` Convert card. Cheaper substitute that fails the checkpoint: batch-only extraction with no interactive on-canvas table region/splitter adjustment or preview grid.
**Chrome:** consume `DocumentView` (`D02 T02`), the floating page toolbar's `Extract tables / images` button (`D02 T02 §8`), the modal dialog host (`D02 T01 §10`), and `ConvertPage` (`D04 T01 §4`).

**Build order:**

1. Build `TableImageExtractorBar` (`src/app/controls/TableImageExtractorBar.xaml/.cpp`), the on-canvas interactive table region & row/column splitter overlay (`TableCanvasOverlay`), and `TablePreviewDialog` (`src/app/dialogs/TablePreviewDialog.xaml/.cpp`). Done when: `AppTests.ExtractorViewerUi` auto-detects a table on canvas, drags a column divider, opens `Preview Table`, copies TSV/HTML to the clipboard, and exports `.csv`/`.json`.
2. Register the **Extract Tables & Images** card and Step 3 options in `ConvertPage` (`SPEC 9.5`). Done when: `AppTests.ExtractorConvertUi` runs batch table and image extraction through `ConversionRunner`.

- [ ] Build the `40 epx` contextual `TableImageExtractorBar` (`SPEC 8.2`) with `Auto-Detect Tables on Page`, `Draw Table Region`, `Add/Remove Column Divider`, `Add/Remove Row Divider`, `Preview Table...`, `Copy Table`, and `Extract Page Images...` buttons, wired to `D02 T02 §8` and `Ctrl+K`. Done when: `TableImageExtractorBar` renders at `40 epx` and passes `D03 T02 §3` accessibility checks.
- [ ] Build `TableCanvasOverlay` rendering detected `ExtractedTable` bounding boxes and interior row (`row_dividers_y`) and column (`col_dividers_x`) grid lines in `RectoAccentBrush`, supporting pointer drag to resize the table box or drag/add/delete interior column and row splitters. Done when: dragging a column divider immediately re-runs `TableExtractor::extract_table_in_rect` (`§8`) and updates the cell partitioning. Cheaper substitute that fails the checkpoint: static non-interactive table boxes where the user cannot fix a missed or extra column split.
- [ ] Wire `D03 T01 §4`'s right-click context menu `Export Selection as Table` to compute the bounding box of `SelectionModel`'s selected quads, open `TableImageExtractorBar` with that region selected, and launch `TablePreviewDialog`. Done when: selecting rows of a table and clicking `Export Selection as Table` opens the structured table preview for that region.
- [ ] Build `TablePreviewDialog` (`src/app/dialogs/TablePreviewDialog.xaml/.cpp`, hosted via `D02 T01 §10`) displaying the extracted rows, columns, and merged headers in a scrollable data grid with `Copy to Clipboard (TSV & HTML)`, `Save as .csv`, `Save as .json`, and `Save as .xlsx` (`-> XREF: D04 T05 §1`) actions. Done when: `Copy to Clipboard` places both tab-separated text and an HTML `<table>` fragment on the clipboard so pasting into Excel or Word preserves cells.
- [ ] Wire `Extract Page Images...` on `TableImageExtractorBar` (and right-click on an image) to save all embedded XObject images on the current page via `ImageExtractor` (`§9`). Done when: clicking `Extract Page Images...` exports the page's images to the chosen folder.
- [ ] Register the **Extract Tables & Images** card and Step 3 options (`Extract`: `Tables & Images`, `Tables only`, `Images only`; `Table format`: `.csv`, `.json`, `.tsv`, `.xlsx`; `Image filter`: `Skip decorative icons smaller than 64 x 64 px`; `OCR scanned tables` toggle) in `ConvertPage` (`D04 T01 §3`-`§4`). Done when: `AppTests.ExtractorUi` drives both the viewer extractor bar and the Convert batch card end-to-end.
- [ ] Commit: `convert: add TableExtractor, ImageExtractor, viewer extractor bar and Convert card`

**Test checkpoint:** `recto-tests.exe --gtest_filter=TableExtractor.*:ImageExtractor.*:AppTests.ExtractorUi` passes under ASan.

## 11. `PdfOptimizer` (audit, downsampling, font subsetting) and Fast Web View `Linearizer`

Implement `src/core/pdf_optimizer.h/.cpp` (`recto::core::PdfOptimizer`), `src/pdf/linearizer.h/.cpp` (`recto::pdf::Linearizer`), and `recto-cli optimize <in.pdf> --out <out.pdf> [--preset web|balanced|print] [--linearize] [--audit-json]`:
- **Space Usage Auditor (`audit_document_space`):** Categorizes every indirect object and stream in the PDF into `Images`, `Fonts`, `ContentStreams`, `MetadataAndXmp`, `AnnotationsAndForms`, and `StructureAndXref` with exact byte counts and percentages.
- **Image Downsampling & Recompression:** Computes each `/Subtype /Image` XObject's effective rendered DPI on page (`pixel_size / displayed_inch_size`), bicubic-downsamples images exceeding preset/custom thresholds (e.g., `> 225 DPI -> 150 DPI` for `Web / Email`, `> 450 DPI -> 300 DPI` for `Balanced`), and recompresses colour (`JPEG`), grayscale (`JPEG`/`Flate`), and `1-bit` monochrome (`CCITT Group 4` / `Flate`) streams whenever the recompressed stream is smaller.
- **Font Subsetting & Object Deduplication:** Subsets embedded TrueType (`/FontFile2`) tables (`glyf`, `loca`, `hmtx`, `cmap`) to only the glyph IDs referenced in the document, deduplicates identical streams by SHA-256 digest, compresses uncompressed streams with `FlateDecode`, and strips unreferenced orphan objects.
- **Fast Web View Linearizer (`src/pdf/linearizer.cpp`):** Reorders objects and writes a valid ISO 32000-1 Annex F `/Linearized 1` header dictionary (`/L` exact file length, `/H` primary hint stream offset/length, `/O` first-page object number, `/E` end of first-page data, `/N` page count, `/T` main xref offset), first-page cross-reference table, primary hint stream, and first-page object group at the front of the file.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `audit_document_space`, image DPI downsampling/recompression, SHA-256 stream deduplication, TrueType glyph subsetting, and orphan-object garbage collection in `src/core/pdf_optimizer.cpp`. Done when: `PdfOptimizer.*` shrinks an image-heavy and unsubsetted-font corpus PDF by `>= 50%` under the `web` preset while preserving `PageText` and visual legibility (`SSIM >= 0.95`) under ASan.
2. Implement ISO 32000-1 Annex F Fast Web View linearization in `src/pdf/linearizer.cpp` and `recto-cli optimize`. Done when: `Linearizer.*` verifies `/Linearized 1`, exact `/L` byte length match, valid `/H` hint stream, and first-page object placement before `/E` under ASan.

- [ ] Implement `SpaceAuditReport` (`total_bytes`, category byte counts and percentages for `Images`, `Fonts`, `ContentStreams`, `MetadataAndXmp`, `AnnotationsAndForms`, `StructureAndXref`, plus `estimated_optimized_bytes` per preset) and `audit_document_space` in `src/core/pdf_optimizer.h/.cpp`. Done when: `PdfOptimizer.SpaceAudit` confirms category byte counts sum to 100% of accounted file bytes.
- [ ] Compute effective rendered DPI for every `/Subtype /Image` XObject across all page `cm` CTM placements (`min_rendered_dpi`), downsample images exceeding target DPI using WIC `WICBitmapInterpolationModeHighQualityCubic`, and recompress (`DCTDecode` for colour/gray, `CCITTFaxDecode` Group 4 for 1-bit) only when `new_bytes < original_bytes`. Done when: `PdfOptimizer.ImageDownsample` reduces a `600 DPI` embedded photo to `150 DPI` under the `Web` preset and never inflates an already-small image. Cheaper substitute: re-saving the PDF with `FlateDecode` only while ignoring oversized embedded images.
- [ ] Implement SHA-256 stream deduplication and unreachable-object garbage collection: merge duplicate indirect streams (such as repeated logos or font descriptors) into a single object ID and omit unreferenced objects not reachable from `/Root` or `/Info`. Done when: `PdfOptimizer.DeduplicateAndGc` collapses 10 duplicate image streams into 1.
- [ ] Implement TrueType (`/FontFile2`) glyph subsetting in `src/pdf/font_subsetter.cpp` retaining only the glyph IDs (`.notdef` + used GIDs and composite component GIDs) referenced by page content and appearance streams, rebuilding `glyf`, `loca`, and `hmtx`. Done when: `PdfOptimizer.FontSubsetting` shrinks a full embedded TrueType font while rendered pages remain pixel-identical (`SSIM >= 0.999`).
- [ ] Implement `Linearizer::write_linearized` in `src/pdf/linearizer.cpp` ordering Part 1 (`%PDF-1.7` header), Part 2 (`/Linearized 1` dictionary with two-pass exact `/L`, `/H`, `/O`, `/E`, `/N`, `/T` values), Part 3 (first-page `xref` table and trailer), Part 4 (`/Catalog` and `/ViewerPreferences`), Part 5 (primary hint stream `/S` page offset table), Part 6 (Page 0 `/Page` object, `/Contents`, and resources), and Part 7-11 (remaining pages, shared objects, and main `xref`). Done when: `Linearizer.AnnexFValidation` verifies `file_size == dict["/L"]`, all Page 0 objects reside at byte offsets `< dict["/E"]`, and `recto::pdf::Document::open` parses both xref tables cleanly.
- [ ] Implement `recto-cli optimize <in.pdf> --out <out.pdf> [--preset web|balanced|print] [--linearize] [--audit-json]` in `src/cli/cmd_optimize.cpp`. Done when: `Cli.OptimizeCommand` passes under ASan.
- [ ] Add `PdfOptimizer.*` and `Linearizer.*` unit tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=PdfOptimizer.*:Linearizer.*:Cli.OptimizeCommand` passes under ASan.

- [ ] Commit: `PdfOptimizer` (space audit, image downsampling, font subsetting, stream dedup) and Fast Web View `Linearizer` (`recto-cli optimize`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfOptimizer.*:Linearizer.*:Cli.OptimizeCommand` passes under ASan.

## 12. `Optimize & Compress PDF` Convert card (`SPEC 9.5`) and `Audit Dialog` (`SPEC 9.9`)

Wire the **Optimize & Compress PDF** card and Step 3 options in `ConvertPage` (`design/SPEC.md` section 9.5: presets `Web / Email (150 DPI)`, `Balanced (200 DPI -- default)`, `Print Ready (300 DPI)`, `Custom`, `Linearize for Fast Web View` toggle, `Subset embedded fonts` toggle, `Strip unreferenced objects & metadata` toggle, and `Audit Space Usage...` button) and build the **Optimize & Compress Audit Dialog** (`SPEC 9.9`, hosted via `D02 T01 §10`: per-category byte and percentage breakdown table + stacked usage bar for `Images`, `Fonts`, `Content Streams`, `Metadata & XMP`, `Annotations & Forms`, and `Structure / XRef`, plus estimated output size and savings badge).

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 9.5 (`Optimize & Compress PDF` Convert card and Step 3 options) and 9.9 (`Optimize & Compress Audit Dialog`).
**Job:** Users can inspect what is taking up space inside a large PDF with a clear category breakdown, see the estimated size reduction before converting, and shrink + linearize the PDF through the Convert queue or `File -> Optimize & Compress...`. Consumer: `ConvertPage` (`D04 T01 §4`), viewer `File` menu / `Ctrl+K`, and `ConversionRunner`.
**Treatment:** Step 3 optimization control with live estimated-size badge plus the modal `OptimizeAuditDialog` showing exact byte/percentage rows and a proportional segmented bar. Cheaper substitute that fails the checkpoint: a blind `"Compress"` button with no space audit breakdown, no DPI presets, or no Fast Web View linearization toggle.
**Chrome:** consume `ConvertPage` (`D04 T01 §4`), `D02 T01 §10`'s single-active-dialog host, and `RectoColors.xaml` chart/badge tokens.

**Build order:**

1. Build `OptimizeAuditDialog` (`src/app/dialogs/OptimizeAuditDialog.xaml/.cpp`) displaying §11's `SpaceAuditReport` (6 category rows with byte size, percentage of total, proportional bar, and estimated post-optimization size for the selected preset). Done when: `AppTests.OptimizerAuditDialog` opens the dialog on a corpus PDF and verifies category percentages sum to `100.0%`.
2. Register the **Optimize & Compress PDF** card and Step 3 options control (`src/app/views/convert/OptimizeConvertOptionsControl.xaml/.cpp`) in `ConvertPage`, and wire `File -> Optimize & Compress...` and `Ctrl+K` (`Optimize & Compress PDF`). Done when: `AppTests.OptimizerUi` audits space usage, runs an optimization + linearization job through `ConversionRunner`, and verifies the output PDF is smaller and linearized.

- [ ] Build `OptimizeAuditDialog` (`src/app/dialogs/OptimizeAuditDialog.xaml/.cpp`, hosted via `D02 T01 §10`) rendering the 6 space-usage categories (`Images`, `Fonts`, `Content Streams`, `Metadata & XMP`, `Annotations & Forms`, `Structure / XRef`) with formatted byte sizes (`KB`/`MB`), percentage column (`0.0%..100.0%`), horizontal segmented bar, and `Current size -> Estimated size (Save N%)` summary card. Done when: `AppTests.OptimizerAuditDialog` verifies all 6 rows and confirms percentages sum to `100%`.
- [ ] Build `OptimizeConvertOptionsControl` (`SPEC 9.5`) with preset selector (`Web / Email -- smallest size`, `Balanced -- recommended`, `Print Ready -- high resolution`, `Custom`), custom DPI/quality inputs when `Custom` is selected, `Linearize for Fast Web View` toggle (on by default), `Subset embedded fonts` toggle, `Remove unused objects & private metadata` toggle, and the `Audit Space Usage...` button. Done when: switching presets updates the estimated output size badge live. Cheaper substitute that fails the checkpoint: offering no presets or omitting the `Audit Space Usage...` breakdown dialog.
- [ ] Register the **Optimize & Compress PDF** card in `ConverterRegistry` (`D04 T01 §3`) with its SPEC Principle 4 honest hint line (`"Shrinks oversized images and unsubsetted fonts; already-compact text PDFs may shrink only slightly."`). Done when: selecting a PDF in Step 1 enables the `Optimize & Compress PDF` card.
- [ ] Wire `File -> Optimize & Compress...` in `Classic` menu bar (`D02 T04 §2`) and `Optimize & Compress PDF` in `CommandRegistry` (`Ctrl+K`) to navigate to `ConvertPage` with the current document preloaded and the `Optimize & Compress PDF` card selected. Done when: invoking `Optimize & Compress PDF` from `Ctrl+K` opens the exact flow.
- [ ] Add `AppTests.OptimizerAuditDialog` and `AppTests.OptimizerUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=AppTests.Optimizer*` passes.
- [ ] Commit: `convert: add PdfOptimizer, font subsetter, Fast Web View linearizer and audit UI`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfOptimizer.*:Linearizer.*:AppTests.Optimizer*` passes under ASan.

## 13. Create PDF from a scanner (Windows Image Acquisition)

"Create PDF from scanner" is one of the most repeated requests users make of PDF tools. Recto can acquire pages through Windows Image Acquisition 2.0 (`IWiaDevMgr2`), feed them to the `Images -> PDF` converter (§5 here) and optionally to the searchable-PDF OCR writer (§7 here), all locally. This is new v1-plus scope; the owner decided on 2026-10-21 (foundation plan, owner decisions after the decision table) that it ships in v1.1, after v1, through WIA 2.0 only (flatbed and document feeder, duplex included); TWAIN is not planned.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.5 (Convert page cards and Step 3 options); a `Scanner` source card is a genuinely new surface on that page with no screenshot.
**Job:** A user with a scanner creates a PDF (optionally searchable) from one or more scanned pages without another app. Consumer: the `Images -> PDF` converter (§5) and `OcrPipeline` (§7).
**Treatment:** A `From scanner` source card on the Convert page with device, source (flatbed or feeder), colour mode, resolution and duplex options, a running page list, and `Make PDF`. Cheaper substitute that fails the checkpoint: shelling out to the Windows Scan app or a vendor tool.
**Chrome:** Convert page card registry and Step 3 options host (`D04 T01 §3`, `§4`), queue card for the job (`D04 T01 §5`), `RectoColors.xaml` tokens and Segoe Fluent Icons `E8FE` (scan).

**Build order:**

1. Confirm the owner decision recorded 2026-10-21 in the foundation plan (v1.1, WIA 2.0 flatbed and feeder with duplex, no TWAIN) and keep this section in phase 7 (After v1). Done when: the decision line exists in the plan and this section's Phase is 7.
2. Implement `src/services/scanner/WiaScanner.h/.cpp` enumerating WIA 2.0 devices and acquiring pages on a job worker with `CancelToken`, then convert through §5 and optionally §7. Done when: `Convert.ScannerToPdf` passes against the WIA test driver.

- [ ] Record the owner decision in `docs/plans/2026-10-13-recto-foundation.md` and in `docs/user-demand.md`. Done when: both files name the decision and its date.
- [ ] Implement `WiaScanner::list_devices()` through `IWiaDevMgr2::EnumDeviceInfo` off the UI thread with a 3 s timeout per device. Done when: an unresponsive device shows as unavailable instead of blocking the card.
- [ ] Implement page acquisition for flatbed and feeder sources with colour mode, resolution and duplex, streaming each page to a temp file through `AtomicFileWriter`. Done when: a five-page feeder job yields five page images.
- [ ] Map WIA errors (paper empty, paper jam, device busy, cover open) to plain messages with next steps. Done when: each mapped `WIA_ERROR_*` code shows its message in `Convert.ScannerErrors`.
- [ ] Add the `From scanner` card and options to the Convert page and the `convert.fromScanner` command to `CommandRegistry`. Done when: `Ctrl+K` "scan" finds the command. Cheaper substitute: a hidden menu entry with no command-box reach.
- [ ] Feed acquired pages to §5's converter with EXIF orientation honoured and, when chosen, to §7's searchable OCR writer. Done when: the produced PDF has one page per scan and searchable text when OCR was selected.
- [ ] Commit: `convert: create PDFs from WIA scanners with optional OCR`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Convert.ScannerToPdf:Convert.ScannerErrors` passes against the WIA sample test driver on the display-session host.

## 14. `ClipboardPdfBuilder`: clipboard images and image or PDF file lists to PDF

Acrobat Pro creates a PDF from whatever is on the clipboard, and users ask for the same, because a screenshot or a copied picture is the quickest way into a PDF. `ClipboardPdfBuilder` (`src/core/clipboard_pdf.h/.cpp`, `recto::core::ClipboardPdfBuilder`) turns a `ClipboardSnapshot` into a PDF through the existing `Images, SVG & XPS -> PDF` converter (`§5`): bitmaps (`CF_DIBV5`, `CF_DIB`, `CF_BITMAP`), the registered `PNG` format, and `CF_HDROP` file lists of images, SVG and XPS files. PDF files in a list are not rebuilt; they are reported so the app opens them as tabs. The builder reads nothing from the clipboard itself: a dedicated clipboard acquisition thread copies the clipboard into an owned, size-bounded snapshot with a time limit and cancellation (rendering a format can wait on the application that owns the clipboard, so the UI thread never calls `GetClipboardData`), and the build runs as a job with its own state (`D04 T01 §1`). Text, rich text, HTML and Office files are routed by `§16` through the same `ClipboardRoute` registry, so this section never grows a second converter. A paste with nothing usable is a typed refusal that names the formats found, never an empty document.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `ClipboardSnapshot`, `ClipboardRoute`, `ClipboardRouteRegistry` and `ClipboardPdfResult`, and implement the image route (PNG first, then `CF_DIBV5`, `CF_DIB`, `CF_BITMAP`) and the file-list route over `images_to_pdf` (`§5`). Done when: `ClipboardPdf.ImageRoute` builds a one-page PDF from each of the four bitmap formats with the right pixel size and, for the PNG format, preserved alpha, under ASan.
2. Add refusal, bounds and `recto-cli clipboard-to-pdf`. Done when: `ClipboardPdf.EmptyAndUnsupportedRefused`, `ClipboardPdf.Bounds` and `Cli.ClipboardToPdf` pass under ASan.

- [ ] Define `ClipboardSnapshot` (`formats`, owned `bytes` per format, `file_paths`, `taken_at`), `ClipboardRoute` (`can_handle(snapshot)`, `build(snapshot, options, CancelToken)`), `ClipboardRouteRegistry`, `ClipboardOutput` (`pdf_path`, `display_name`, `source_kind`, `owns_temp_file`, `status` of `Built` or `Failed` with a typed reason) and `ClipboardPdfResult` (`outputs` as a list of `ClipboardOutput`, `pdfs_to_open` for existing PDFs, `unrouted` with reasons, `warnings`) in `src/core/clipboard_pdf.h`, so one paste can yield several generated PDFs and partial failure is explicit. Done when: the header compiles cleanly across `recto-core`, `recto-services`, `recto-cli` and `recto-app`.
- [ ] Implement `ClipboardSource` as an interface with a Win32 implementation owned by one long-lived clipboard thread that runs at most one request at a time (`OpenClipboard`, `EnumClipboardFormats`, `GetClipboardData`, copying into owned memory and calling `CloseClipboard` before any decode). Windows controls how long a delayed render can block, so a `5 s` caller deadline never kills or abandons the thread: the caller gets a typed `ClipboardTimeout` or `Cancelled` at once, the call stays outstanding and keeps its request token, the clipboard stays open until that call returns, and the late result is discarded and its memory freed when it arrives. While a call is outstanding the source is `Busy`: new requests are refused immediately with a typed `ClipboardBusy` (no queue, no second thread) and the command shows `"The clipboard is still being read by another application."`; when the late call returns the thread closes the clipboard and becomes idle again. On window close or app exit the source stops accepting requests, signals cancel, and waits at most `2 s` for an outstanding call before letting the process exit with the thread detached (no further UI access); the snapshot is bounded to `512 MiB` with a typed `ClipboardTooLarge` refusal. An in-memory fake supports a blocked call for tests. Done when: `ClipboardPdf.SnapshotBounds` refuses an oversized fake, `ClipboardPdf.DelayedRenderingDoesNotBlockUi` shows a delayed owner leaves the UI responsive and ends in `ClipboardTimeout`, `ClipboardPdf.CancelThenPasteAgain` cancels during a blocked call and a second paste is refused with `ClipboardBusy` until the late call returns and then succeeds with no leaked memory or open clipboard, `ClipboardPdf.CloseWindowDuringBlockedCall` closes the window mid-call and exits cleanly, and the Win32 source reads a real clipboard on the display-session host.
- [ ] Implement the image route in `src/core/clipboard_pdf.cpp`: prefer the registered `PNG` format (bytes passed through to `§5` unchanged), then `CF_DIBV5`, `CF_DIB` and `CF_BITMAP`, decoding `BI_RGB` and `BI_BITFIELDS`, palettes and top-down DIBs through WIC, and treating a `CF_DIB` alpha channel as opaque unless the alpha is genuinely present (`CF_DIBV5` with an alpha mask) so screenshots do not become transparent. Done when: `ClipboardPdf.ImageRoute` and `ClipboardPdf.DibAlphaPolicy` pass for 8, 24 and 32 bits per pixel. Cheaper substitute: always decoding `CF_DIB` and losing the PNG alpha, or treating every `32` bit DIB as transparent.
- [ ] Write each image to a private temp file under the app temp folder through `AtomicFileWriter` (`D04 T02 §1`), call `images_to_pdf` (`§5`) with the page size and margin defaults of its Step 3 options, and delete the temp files on completion, failure and cancel. Done when: `ClipboardPdf.TempCleanup` finds no temp file after success, failure and `CancelToken` abort.
- [ ] Implement the file-list route for `CF_HDROP`: sniff each file by signature rather than extension, combine images, SVG and XPS files into one PDF in list order, add `.pdf` files to `pdfs_to_open` without copying them, skip folders with a warning, cap the list at `500` files and each file at `2 GiB`, and put every other file in `unrouted` with its reason. Done when: `ClipboardPdf.FileList` passes a mixed list (two images, an SVG, a PDF, a folder, a `.docx`) and a renamed `.png` that is really a JPEG.
- [ ] Refuse an unusable clipboard with `PdfErrorCode::ClipboardEmpty` or `ClipboardUnsupported` listing the format names found, refuse a zero-size or all-transparent-and-empty bitmap, and cap bitmaps at `16384` pixels on a side and `268` megapixels with a typed error. Done when: `ClipboardPdf.EmptyAndUnsupportedRefused` and `ClipboardPdf.Bounds` cover an empty clipboard, text only (before `§16`), a zero-size DIB and an oversized DIB, and none produces a file.
- [ ] Run the build as a `ConversionJob` on its own `pdf::Document` and `CancelToken` and write the output through `AtomicFileWriter`, so cancel leaves nothing behind. Done when: `ClipboardPdf.CancelLeavesNothing` aborts mid-build and finds the output folder unchanged.
- [ ] Implement `recto-cli clipboard-to-pdf --out <out.pdf> [--page-size original|a4|letter] [--json]` in `src/cli/cmd_clipboard_to_pdf.cpp` reading the live clipboard, with the typed refusals as non-zero exit codes. Done when: `Cli.ClipboardToPdf` passes with a fake source and with a real clipboard on the display-session host.
- [ ] Add `ClipboardPdf.*` tests in `tests/recto-tests/clipboard_pdf_tests.cpp` with DIB, PNG and file-list fixtures from `scripts/build-corpus.py` (recorded in `tests/corpus/PROVENANCE.md`). Done when: `recto-tests.exe --gtest_filter=ClipboardPdf.*:Cli.ClipboardToPdf` passes under ASan.
- [ ] Commit: `convert: build PDFs from clipboard bitmaps and image file lists`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ClipboardPdf.*:Cli.ClipboardToPdf` passes under ASan with the four bitmap formats, the file-list mix and the refusal cases quoted.

## 15. **Create PDF from clipboard** command, new unsaved tab, `Ctrl+V` and Home entry points

Make the clipboard builder (`§14`) usable: one command, **Create PDF from clipboard**, reachable from `Ctrl+V` (when nothing else owns paste), the Home screen, the tab strip `+` menu, the Classic `File` menu and `Ctrl+K`, which opens the result as a new tab. The tab is an unsaved new document: it is backed by a private temp file, is treated as dirty so closing asks `Save your changes first?`, offers `Save a copy` with a dated default name, and is covered by crash recovery (`D02 T01 §12`) so a pasted screenshot is not lost to a crash. Nothing is ever pasted silently or into an existing document; when the clipboard has nothing usable the user is told why and what was found. PDF files in a copied file list open as tabs through the normal pipeline.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new command on existing chrome: it adds an entry to the Home screen (`design/SPEC.md` section 9.1, `design/screenshots/D-Home.png`), the `+` New Tab menu of the title bar (section 8.1), the Classic `File` menu (section 9.2) and the command box (section 9.4), with a standard WinUI `InfoBar` for refusals.
**Job:** A user who copied a screenshot, a picture or files can press `Ctrl+V` or choose **Create PDF from clipboard** and have the result open as a new tab ready to read, save or convert, and is told clearly when the clipboard cannot be made into a PDF. Consumer: `OpenDocumentPipeline` (`D02 T01 §5`), `SavePipeline` (`D02 T01 §8`) and crash recovery (`D02 T01 §12`).
**Treatment:** A `file.createFromClipboard` command in `CommandRegistry` that is always invocable while a window is open, reads the clipboard on the clipboard thread of `§14`, runs `ClipboardPdfBuilder` as a queue job, and opens the output through a new `OpenDocumentPipeline::open_generated_async` entry as unsaved tabs. Cheaper substitute that fails the checkpoint: a paste that does nothing when the clipboard is unusable, writes the PDF next to the executable, or saves a file without asking.
**Chrome:** consume `ShellPage` and the `TabView` (`D02 T01 §3`), `OpenDocumentPipeline` (`D02 T01 §5`), `SavePipeline` and the Unsaved-Changes dialog (`D02 T01 §8`), the Home screen (`D02 T03 §1`), `CommandRegistry` (`D02 T03 §11`), `QueueCard` (`D04 T01 §5`) and `RectoColors.xaml` tokens.

**Build order:**

1. Implement `open_generated_async(temp_path, display_name)` in `src/app/OpenPipeline.h/.cpp` and the `file.createFromClipboard` command with its job, tab and refusal flows. Done when: `AppTests.CreateFromClipboardUi` copies a PNG, runs the command, and finds a new tab titled `Clipboard 1.pdf` showing the image, marked unsaved.
2. Bind the entry points and the save, close and recovery behaviour. Done when: `AppTests.CreateFromClipboardUi` pastes with `Ctrl+V`, closes with the Unsaved-Changes prompt, saves a copy with the dated name, and restores the tab after a simulated crash.

- [ ] Add `OpenDocumentPipeline::open_generated_async(temp_path, display_name)` in `src/app/OpenPipeline.h/.cpp` that opens a generated file as a new tab marked `is_new_unsaved`, without deduplicating by path and with the temp file owned by the tab. Done when: opening two generated files with the same name yields `Clipboard 1.pdf` and `Clipboard 2.pdf`.
- [ ] Register `file.createFromClipboard` in `CommandRegistry` (`D02 T03 §11`) as always enabled while the shell is open (its enablement never depends on the clipboard, so a refusal can be shown on invocation), with synonyms `paste`, `clipboard`, `screenshot`, `new pdf`. Done when: `Ctrl+K` "screenshot" finds it and invoking it on an empty clipboard shows the refusal `InfoBar` instead of doing nothing.
- [ ] Bind `Ctrl+V` on `ShellPage` to the command whenever no text input has focus and no mode that owns paste (Form Designer field paste, `D03 T03 §9`) is active, whatever the clipboard holds, so text fields keep `Ctrl+V` (`D02 T04 §9`) and an unusable clipboard reaches the refusal message. Done when: `AppTests.CreateFromClipboardUi` shows `Ctrl+V` in a text box pastes text, `Ctrl+V` on the canvas creates the PDF, and `Ctrl+V` with an empty clipboard shows the refusal through the real key binding. Cheaper substitute: claiming `Ctrl+V` globally, or disabling the command when the clipboard is empty.
- [ ] Add the entry to the Home screen action row, the `+` New Tab menu and the Classic `File` menu, each routed to the same command with an automation name and tooltip. Done when: all three entry points create the same tab.
- [ ] Run the build as a queue job (`D04 T01 §5`), taking the snapshot on the clipboard thread of `§14`, and open every `Built` entry of `ClipboardPdfResult::outputs` as its own unsaved tab (each tab owning its temp file), opening any `pdfs_to_open` through the normal pipeline and reporting each `Failed` entry in the message. Done when: pasting a copied list of one image, one PDF and two documents opens all the resulting tabs, and one failing document is reported while the others open.
- [ ] Show an `InfoBar` for refusals (`"Nothing on the clipboard can be made into a PDF."` with `"Found: <format names>."`) and for `unrouted` files naming each file and reason, and announce it to assistive technology (`D03 T02 §5`). Done when: an empty clipboard, a text-only clipboard (before `§16`) and a list with a `.docx` each show the message and create no tab or file.
- [ ] Treat the new tab as dirty: closing or exiting shows `Save your changes first?` (`D02 T01 §8`), `Save a copy` defaults to `Clipboard <yyyy-MM-dd HH-mm>.pdf` in the user's Documents folder, and the temp file is deleted after save or discard. Done when: `AppTests.CreateFromClipboardUi` covers Discard, Cancel and Save and finds no temp file afterwards.
- [ ] Include the unsaved clipboard tab in crash-recovery snapshots (`D02 T01 §12`). Done when: a simulated crash and relaunch restores the pasted PDF as an unsaved tab.
- [ ] Add `AppTests.CreateFromClipboardUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=AppTests.CreateFromClipboardUi` passes.
- [ ] Commit: `app: add Create PDF from clipboard with Ctrl+V and unsaved new tabs`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.CreateFromClipboardUi` passes on the display-session host.

## 16. Clipboard text, rich text, HTML and Office, Markdown file lists through their converters

Complete the clipboard routes with the converters that arrive in phase 5. This section registers routes in the `ClipboardRouteRegistry` of `§14` so text and documents copied from Word, a browser, a spreadsheet or Explorer become PDFs without a second converter: `Rich Text Format` goes to the RTF engine (`D04 T04 §1`), `HTML Format` and Markdown or plain text go to the HTML and Markdown engine (`D04 T04 §3`), and copied files go to the Office and legacy engines (`D04 T04 §1`, `§2`, `§5`, `§6`). Clipboard content is hostile like any file: HTML scripts stay off, nothing is fetched from the network (D16), and RTF embedded objects and Office macro streams are never loaded or run (D10). Every file the routes cannot convert is reported with its reason, never dropped.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Register the text routes (`Rich Text Format`, `HTML Format`, `CF_UNICODETEXT`) with `ClipboardTextMode` (`Auto`, `PlainText`, `Html`) and a `CF_HTML` fragment parser. Done when: `ClipboardPdf.TextRoutes` builds a PDF from each format with the right text, scripts not run and no network request made, under ASan.
2. Register the document file-list route. Done when: `ClipboardPdf.OfficeFileList` converts a copied `.docx`, `.xlsx`, `.pptx`, `.html`, `.md`, `.txt` and legacy `.doc` list into one PDF per file and reports an unsupported file in `unrouted`, under ASan.

- [ ] Define `ClipboardTextMode` and add it to the `ClipboardPdf` options in `src/core/clipboard_pdf.h`, defaulting to `Auto` (prefer `Rich Text Format`, then `HTML Format`, then plain text). Done when: `ClipboardPdf.TextModeSelection` picks the documented format for each combination.
- [ ] Parse the `HTML Format` clipboard header (`StartHTML`, `EndHTML`, `StartFragment`, `EndFragment`, `SourceURL`) with bounds checks, convert the fragment through the HTML engine of `D04 T04 §3` with scripts disabled and external resources unfetched (`SourceURL` is metadata only), and warn about images that could not be loaded. Done when: `ClipboardPdf.HtmlFormat` rejects out-of-range offsets, converts a Word and a browser fragment, and shows no network access.
- [ ] Convert `Rich Text Format` through the RTF engine (`D04 T04 §1`) with embedded objects (`\objdata`) skipped and reported. Done when: `ClipboardPdf.RtfFormat` converts formatted text and reports a skipped OLE object.
- [ ] Convert `CF_UNICODETEXT` through the HTML and Markdown engine as escaped, wrapped, paginated text with the page size of `§5`'s Step 3 defaults, per-script font fallback and subset embedding, and no `?` or `.notdef` substitution (`D06 T03 §7`). Done when: `ClipboardPdf.PlainTextUnicode` converts Latin, CJK, Arabic and emoji text and `PageText` returns the same Unicode.
- [ ] Add the document file-list route: sniff `.docx`, `.docm`, `.rtf`, `.xlsx`, `.xlsm`, `.csv`, `.pptx`, `.pptm`, `.html`, `.md`, `.txt` and legacy `.doc`, `.xls`, `.ppt` by signature, convert each through its engine (`D04 T04 §1`, `§2`, `§3`, `§5`, `§6`) as its own job, append one `ClipboardOutput` per file to `outputs` (with `Failed` and a reason for a file that cannot convert, never aborting the others), strip macro streams by construction, and leave images to the `§14` route. Done when: `ClipboardPdf.OfficeFileList` covers every type, a mixed list of two documents and an image yields three outputs, and a macro-bearing `.docm` converts with its macros stripped.
- [ ] Report every file and format no route handles in `unrouted` with a plain reason, and let the `§15` message list them. Done when: `ClipboardPdf.UnroutedReported` shows a `.zip` and a corrupt `.docx` named with reasons and no silent drop.
- [ ] Extend `AppTests.CreateFromClipboardUi` (`§15`) with text, HTML and Office clipboard cases and add `ClipboardPdf.*` tests in `tests/recto-tests/clipboard_pdf_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ClipboardPdf.*:AppTests.CreateFromClipboardUi` passes under ASan.
- [ ] Commit: `convert: route clipboard text, rich text, HTML and Office file lists to PDF`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ClipboardPdf.TextModeSelection:ClipboardPdf.HtmlFormat:ClipboardPdf.RtfFormat:ClipboardPdf.PlainTextUnicode:ClipboardPdf.OfficeFileList:ClipboardPdf.UnroutedReported` passes under ASan.

## 17. Image-only PDF engine (`image_only_pdf.cpp`) and `recto-cli rasterize-pdf`

PDFelement's and UPDF's "image-only PDF" is the reverse of OCR: every page is drawn at a chosen resolution and rebuilt as one picture, so the file looks the same in every viewer and carries no text, fonts, hidden layers or form logic. Acrobat has no dedicated tool for this. `rasterize_pdf` (`src/core/image_only_pdf.h/.cpp`, `recto::core`) renders each page through the Direct2D rasterizer of `D01 T02 §6` (annotation and form appearances included, as the viewer shows them), and writes a new PDF through `PdfWriter` (`§2`) and `AtomicFileWriter` (`§1`) with one image XObject per page, at the source page size and rotation. It keeps the rules that make Recto safe: it runs as a job on its own `recto::pdf::Document`, never modifies the source, holds one page bitmap in memory at a time, and says plainly what the output no longer contains (text, links, form fields, annotations as objects, attachments), because the result cannot be searched, selected or edited. Bookmarks, page labels and the document title and author are kept by default.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `ImageOnlyOptions` and `ImageOnlyReport` and implement the page loop (render, encode, write, release) in `src/core/image_only_pdf.cpp`. Done when: `ImageOnlyPdf.PageLoop` converts a 50-page corpus PDF at `150` DPI with at most one page bitmap alive at any moment (counted by the test allocator) and the output page count matches and each output page's `/MediaBox` equals the source page as displayed (CropBox size with `/Rotate` already applied, so `90` and `270` swap width and height), with origin `0 0` and `/Rotate 0` so the image is never rotated twice. `ImageOnlyPdf.Geometry` covers a nonzero CropBox origin, a CropBox smaller than the MediaBox, and rotations of `0`, `90`, `180` and `270`.
2. Add the colour modes and compressions (`Colour`, `Grayscale`, `Monochrome` with `Auto` Otsu or a fixed threshold `1..254`; `Auto`, `Jpeg`, `Flate`, `CcittG4`), the kept metadata and bookmarks, the report, and `recto-cli rasterize-pdf`. Done when: `ImageOnlyPdf.*` and `Cli.RasterizePdf` pass under ASan and the output of every mode re-renders within the stated tolerance.

- [ ] Define `ImageOnlyOptions` (`page_range`, `dpi` `72..1200` default `150`, `colour_mode` `Colour|Grayscale|Monochrome`, `mono_threshold` `Auto|1..254`, `compression` `Auto|Jpeg|Flate|CcittG4`, `jpeg_quality` `1..100` default `85`, `keep_bookmarks`, `keep_page_labels`, `keep_document_info`, `include_annotations`) and `ImageOnlyReport` (`pages_written`, `text_characters_removed`, `links_removed`, `form_fields_flattened`, `annotations_flattened`, `attachments_dropped`, `bookmarks_kept`, `warnings`) in `src/core/image_only_pdf.h`. Done when: the header compiles across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Before rendering, compute the pixel size with checked arithmetic and refuse a page above `16384` pixels on a side or `268` megapixels with a typed `PageTooLarge` error that names the page and the DPI that would fit (the same limits as `§14`). Done when: `ImageOnlyPdf.PageTooLarge` refuses a large-format page at `1200` DPI before any bitmap is allocated and reports it per page. Then render each requested page on the job worker at `dpi` with the CropBox and `/Rotate` applied, then encode it at once and release the bitmap: `Colour` uses 8-bit RGB, `Grayscale` uses `GUID_WICPixelFormat8bppGray`, `Monochrome` thresholds to 1 bit. Done when: `ImageOnlyPdf.ColourModes` shows the three pixel formats in the written XObjects (`/ColorSpace`, `/BitsPerComponent`). Cheaper substitute: rendering every page into RAM before writing, which fails `ImageOnlyPdf.PageLoop`.
- [ ] Write each page as `q <w> 0 0 <h> 0 0 cm /Im0 Do Q` over a page whose `/MediaBox` equals the source CropBox size, choosing the stream filter per mode: `DCTDecode` (WIC JPEG bytes embedded unchanged) for colour and grey `Jpeg`, `FlateDecode` for lossless, and `CCITTFaxDecode` Group 4 for `Monochrome` through the encoder of `PdfOptimizer` (`§11`), falling back to `FlateDecode` when it is smaller. Done when: `ImageOnlyPdf.Compressions` re-renders each output and compares it with the source render (exact for `Flate`, `SSIM >= 0.98` for `Jpeg` at quality `85`, identical bits for `CcittG4`).
- [ ] Keep `/Outlines` (re-pointing each destination to the same page index), `/PageLabels` and `/Info` `Title` and `Author` when the options say so, and drop everything else (text, `/Annots`, `/AcroForm`, `/Names` embedded files, XMP other than a minimal `dc:title`). Done when: `ImageOnlyPdf.KeptAndDropped` finds the bookmark tree and labels intact and finds no `/Annots`, `/AcroForm`, `/EmbeddedFiles` or `/Font` in the output, and `PageText` of every page is empty.
- [ ] Fill `ImageOnlyReport` and show it as the job's completion note (`D04 T01 §6`): `"Made 12 pages into pictures. Removed 4,310 characters of text, 9 links and 2 form fields. This file can no longer be searched or edited; use Searchable PDF (OCR) to add text back."`. Done when: `ImageOnlyPdf.Report` matches the counts of a fixture with known text, links and fields.
- [ ] Commit the output only through `AtomicFileWriter` (`§1`), check `CancelToken` between pages, and refuse to write over the source path. Done when: `ImageOnlyPdf.CancelLeavesNothing` aborts mid-job and finds the folder unchanged, and `ImageOnlyPdf.RefusesSourcePath` returns a typed error. Cheaper substitute: writing next to the source with `std::ofstream`.
- [ ] Implement `recto-cli rasterize-pdf <in.pdf> --out <out.pdf> [--dpi <72..1200>] [--colour colour|grayscale|mono] [--threshold auto|<1..254>] [--compression auto|jpeg|flate|ccitt4] [--pages <range>] [--drop-bookmarks] [--json]` in `src/cli/cmd_rasterize_pdf.cpp`. Done when: `Cli.RasterizePdf` passes under ASan with the report in `--json`.
- [ ] Add `ImageOnlyPdf.*` tests in `tests/recto-tests/image_only_pdf_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ImageOnlyPdf.*:Cli.RasterizePdf` passes under ASan.
- [ ] Commit: `convert: add image-only PDF engine and recto-cli rasterize-pdf`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ImageOnlyPdf.*:Cli.RasterizePdf` passes under ASan, and an output that keeps a `/Font` resource, a text operator, an `/Annots` array or an `/AcroForm` (seeded by a test double that leaves one in place) makes `ImageOnlyPdf.KeptAndDropped` fail.

## 18. Image-only PDF Convert card and Step 3 options

Put the image-only engine (`§17`) on the Convert page as a card named **Image-only PDF**, matching PDFelement's and UPDF's flatten-to-picture output while keeping Recto's honest hint line and its rule that the original is never replaced without the Replace-original dialog. The card is enabled when Step 1 holds at least one PDF; its Step 3 options are resolution, colour mode (with the threshold for monochrome), compression, and the keep-bookmarks and keep-metadata toggles, and it shows the plain warning that the result has no text. The command **Make image-only PDF** is reachable from `Ctrl+K`.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 (the card grid and the Step 3 options host, the same pattern as the `Optimize & Compress PDF` card); the card is a genuinely new option with no screenshot, so it follows the existing card, option row and hint line chrome of `explorations/A-Convert.png` and `D-Convert.png`.
**Job:** The user can turn a PDF into a picture-only copy at a chosen resolution, see before converting what will be lost, and find the result beside the source without the source being touched. Consumer: `ConvertPage` (`D04 T01 §4`), `ConversionRunner` (`D04 T01 §1`) and the `Ctrl+K` command box.
**Treatment:** A registered card, `ImageOnlyOptionsControl.xaml/.cpp` bound to `ImageOnlyOptionsViewModel`, a monochrome threshold slider with a live thumbnail of page 1 at the chosen settings, and the capability hint line. Cheaper substitute that fails the checkpoint: a card with no preview of the threshold and no warning that text is removed.
**Chrome:** consume the Convert Step 2 card registry and the Step 3 options host (`D04 T01 §3`, `§4`), the queue card (`D04 T01 §5`), `RectoColors.xaml` tokens and Segoe Fluent Icons.

**Build order:**

1. Register the **Image-only PDF** card and build `ImageOnlyOptionsControl` with its view model and the thumbnail preview. Done when: `AppTests.ImageOnlyUi` selects a PDF, opens the card and sees all options and the warning line.
2. Wire the job factory, the completion note and the `Ctrl+K` command. Done when: `AppTests.ImageOnlyUi` runs a conversion through `ConversionRunner` and finds the output without text.

- [ ] Register the **Image-only PDF** card in `ConverterRegistry` (`D04 T01 §3`), enabled when Step 1 contains a PDF, with the disable reason `"Add a PDF to use this"` otherwise and the hint line `"Turns each page into a picture. The file looks identical everywhere but has no selectable text, links or fields."`. Done when: `AppTests.ImageOnlyUi` verifies enablement and the reason text.
- [ ] Build `ImageOnlyOptionsControl` (`src/app/views/convert/ImageOnlyOptionsControl.xaml/.cpp`) with `Resolution` (`72`, `150`, `300`, `600`, `1200` DPI), `Colour` (`Colour`, `Grayscale`, `Black and white`), a `Threshold` slider (`Auto` or `1..254`) shown only for black and white, `Compression`, `Keep bookmarks`, `Keep title and author` and `Save to` and `If file exists`, with automation names and tooltips on every control. Done when: the controls pass the `D03 T02 §3` inventory and the threshold slider appears only in black and white mode. Cheaper substitute: an unlabelled icon-only slider.
- [ ] Render the page 1 preview at the chosen settings on a worker through the `§17` loop with a `CancelToken` and a 300 ms debounce, never on the UI thread. Done when: moving the threshold updates the thumbnail and the UI thread never blocks over 16 ms in `AppTests.ImageOnlyUi`.
- [ ] Show the estimated output size from a one-page sample and the removal warning (`"Text, links and form fields will be removed."`) in the options card before the primary action. Done when: the estimate and the warning render and the warning is announced to assistive technology (`D03 T02 §5`).
- [ ] Register `convert.imageOnly` in `CommandRegistry` (`D02 T03 §11`) with the title `Make image-only PDF` and synonyms `flatten`, `rasterize`, `picture only`, opening Convert with the current document and this card selected. Done when: `Ctrl+K` "flatten" finds it and starts the flow. Cheaper substitute: a card reachable only by scrolling the Convert page.
- [ ] Add `AppTests.ImageOnlyUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=AppTests.ImageOnlyUi` passes on the display-session host.
- [ ] Commit: `convert: add the Image-only PDF card and options`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.ImageOnlyUi` passes, and a display-session screenshot of the card sits beside `D-Convert.png` with the same card, option row and hint line chrome.

## 19. Editable OCR writer: recognized text as live text objects in matched fonts

Acrobat's "Editable text and images" OCR output and the "Editable PDF" output of PDFelement, UPDF and Foxit PDF Editor turn a scan into a document whose text is real text that Edit PDF can change. Recto's Searchable PDF (`§7`) keeps the scan and hides the text, so the words cannot be edited. This section adds the second output: `create_editable_pdf` (`src/core/editable_ocr_pdf.h/.cpp`) takes the `OcrPipeline` result (`D03 T01 §8`) and, for each text region the layout segmenter found (`D03 T01 §7`), paints that region's pixels out of a copy of the page image with the local background colour and writes the recognized lines as visible text objects (`0 Tr`) in a matched font. Fonts are chosen per line as serif, sans or monospace, bold and italic, at the size that matches the ink height, using the standard fonts for Latin text and the `EditFontResolver` (`D03 T04 §4`) for anything outside them, so no character is ever written as `?`. Regions that are not text (pictures, line art) stay as image pixels. A line with any word below the confidence floor, or a region whose background is not a flat colour, is left as image under an invisible text layer exactly as `§7` writes it, and is listed in the report, so a wrong guess is never baked in and nothing is silently lost. The source file is never touched and the output is a new file. Recto keeps its offline, on-a-copy, temp-file-then-replace rules and adds no script or external OCR service.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement font matching (`OcrFontMatcher`) and live-text synthesis in `src/core/editable_ocr_pdf.cpp`: one `BT ... ET` per recognized line with a single `Tf`, real space characters, `Tz` fitted to the line width and the baseline from the x-height ink. Done when: `EditableOcr.LiveText` makes a PDF from `tests/corpus/text/scanned.pdf` whose text objects are visible (`Tr 0`), selectable, and whose `PageText` readback (OCR disabled) equals the recognized text.
2. Implement background cleanup, the hybrid fallback for low-confidence lines and non-flat backgrounds, the report, and `recto-cli ocr --editable`. Done when: `EditableOcr.*` and `Cli.OcrEditable` pass under ASan, and `EditableOcr.EditModelAccepts` shows `EditModel` (`D03 T04 §1`) lists the recreated paragraphs as editable text objects.

- [ ] Define `EditableOcrOptions` (`page_range`, `ocr_language_bcp47`, `engine`, `min_word_confidence` default `0.85`, `preprocess_deskew_and_enhance`, `replace_text_regions`, `keep_unrecognized_as_image`) and `EditableOcrReport` (`pages`, `lines_replaced`, `lines_kept_as_image` with the reason per line, `fonts_used`, `fallback_fonts_embedded`, `warnings`) in `src/core/editable_ocr_pdf.h`. Done when: the header compiles across `recto-core`, `recto-services` and `recto-cli`.
- [ ] Implement `OcrFontMatcher` classifying each line as serif, sans or monospace (stroke contrast and advance regularity), bold (stroke thickness against x-height) and italic (dominant slant of connected components), mapping it to the matching standard font (Times, Helvetica or Courier family with its bold and italic faces) and sizing it so the line's x-height equals the measured ink height. Done when: `EditableOcr.FontMatch` classifies a fixture of serif, sans, monospace, bold and italic lines correctly and sizes each within `5%` of the ink height.
- [ ] Write each line as live text and route every character the standard font cannot draw through `EditFontResolver::cover` (`D03 T04 §4`) into an embedded subset fallback with a `/ToUnicode` CMap, refusing `?` and `.notdef`. Done when: `EditableOcr.UnicodeFallback` writes Cyrillic and accented text and extracts the same Unicode back. Cheaper substitute: forcing a Latin-1 font and writing `?` for other characters.
- [ ] Paint each replaced region out of a copy of the page image using the median colour of the region border and write that copy as a new image XObject with lossless `FlateDecode` for every colour mode (so pixels outside replaced regions equal the decoded scan pixels exactly; the file can grow, and the report says so), leaving the original image stream untouched in the source file. Done when: `EditableOcr.BackgroundCleanup` shows the replaced region is the border colour, every pixel of a figure region equals the decoded scan pixel, and the source file hash is unchanged. Cheaper substitute: drawing the visible text over the unmodified scan, which doubles every word and fails `EditableOcr.NoDoubleImage`.
- [ ] Keep any line with a word under `min_word_confidence`, any region whose border colour varies more than a set tolerance, and any region whose interior holds pixels that are neither background nor ink of a recognized glyph (a diagram, watermark, stamp or coloured fill under the text), as image pixels with the invisible `3 Tr` layer of `§7`, and list each in `lines_kept_as_image`. Done when: `EditableOcr.LowConfidenceKept` keeps a smudged line as image, lists it with the reason, and replaces its neighbours, and `EditableOcr.FigureUnderText` keeps a region whose border is uniform but whose interior holds a figure overlapped by text.
- [ ] Group the lines into paragraphs with the regular line pitch and shared left edge that `D03 T04 §2`'s paragraph model reads, and keep one `/Contents` stream order that paints the cleaned image first and text after. Done when: `EditableOcr.EditModelAccepts` builds an `EditModel` from the output and finds one editable paragraph per recognized paragraph.
- [ ] Write the output through `PdfWriter` (`§2`) and `AtomicFileWriter` (`§1`), checking `CancelToken` per page, and implement `recto-cli ocr <in.pdf> --out <out.pdf> --editable [--min-confidence <0..1>] [--lang <tag>] [--pages <range>] [--json]` in `src/cli/cmd_ocr.cpp`. Done when: `EditableOcr.CancelLeavesNothing` and `Cli.OcrEditable` pass under ASan.
- [ ] Add `EditableOcr.*` tests in `tests/recto-tests/editable_ocr_tests.cpp` with the scan fixtures recorded in `tests/corpus/PROVENANCE.md`. Done when: `recto-tests.exe --gtest_filter=EditableOcr.*:Cli.OcrEditable` passes under ASan.
- [ ] Commit: `convert: add editable OCR output with live text in matched fonts`

**Test checkpoint:** `recto-tests.exe --gtest_filter=EditableOcr.*:Cli.OcrEditable` passes under ASan, and drawing text over the unmodified scan makes `EditableOcr.NoDoubleImage` fail.

## 20. Editable text output mode on the Searchable PDF (OCR) card and `Ctrl+K`

Expose the editable OCR writer (`§19`) on the existing **Searchable PDF (OCR)** card as a second output, so a user picks between a searchable scan that looks identical and an editable document whose recognized text is real text. The choice is explicit, defaults to the searchable output, and states the trade-off in the card (editable output changes how the page looks slightly, keeps low-confidence lines as image, and is never applied over the source). The command **Make scan editable (OCR)** is reachable from `Ctrl+K`, and a finished editable job offers `Edit text` in its completion notice, which opens the result in Edit PDF mode (`D03 T04 §11`).

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 (the `Searchable PDF (OCR)` card and its Step 3 options); the `Output` selector and the editable-mode notes are a genuinely new option row that follows the card's existing option row and hint line chrome.
**Job:** A user with a scanned contract or letter can make it editable in one conversion, understand what changes, see which lines could not be made editable, and open the result in Edit PDF. Consumer: `ConvertPage` (`D04 T01 §4`), `ConversionRunner` (`D04 T01 §1`), the completion notice (`D04 T01 §6`) and Edit PDF (`D03 T04 §11`).
**Treatment:** An `Output` segmented control (`Searchable, keeps the scan` default, `Editable text`), a `Minimum confidence` slider shown for the editable mode, a report dialog listing the lines kept as image, and the `Edit text` completion action. Cheaper substitute that fails the checkpoint: a hidden CLI-only flag, or a mode that overwrites the searchable output without a choice.
**Chrome:** consume the Convert Step 3 options host (`D04 T01 §4`), the completion notice (`D04 T01 §6`), the dialog host (`D02 T01 §10`), `CommandRegistry` (`D02 T03 §11`) and `RectoColors.xaml` tokens.

**Build order:**

1. Add the `Output` selector, the confidence slider and the hint text to the Searchable PDF card options and bind them to `EditableOcrOptions`. Done when: `AppTests.EditableOcrUi` switches the mode and sees the editable-only controls appear and the searchable-only ones stay.
2. Add the report dialog, the `Edit text` completion action and the `Ctrl+K` command. Done when: `AppTests.EditableOcrUi` runs an editable job, opens the report and opens the result in Edit PDF mode.

- [ ] Extend the Searchable PDF Step 3 options control (`src/app/views/convert/SearchableOcrOptionsControl.xaml/.cpp`, the control `§7` registers) with the `Output` segmented control and the `Minimum confidence` slider (`0.50..0.99`, default `0.85`), each with an automation name and tooltip. Done when: both controls pass the `D03 T02 §3` inventory and the slider shows only in editable mode.
- [ ] Add the honest hint line for the editable mode (`"Replaces recognized text with real text so you can edit it. Lines Recto is unsure about stay as pictures. The page may look slightly different."`). Done when: the line is visible in editable mode and the searchable hint of `§7` is unchanged in the other mode.
- [ ] Dispatch the job to `create_editable_pdf` when the mode is `Editable text` and to `create_searchable_pdf` otherwise, writing to a new file and never to the source. Done when: `AppTests.EditableOcrUi` finds an editable output whose text objects are visible and a searchable output whose text is invisible from the same input.
- [ ] Build `EditableOcrReportDialog` (`src/app/dialogs/EditableOcrReportDialog.xaml/.cpp`, hosted via `D02 T01 §10`) listing the lines kept as image with their page, a thumbnail and the reason, with `Open in Edit PDF` and `Close`. Done when: the dialog lists a seeded low-confidence line and `Open in Edit PDF` opens the page. Cheaper substitute: a count in a toast with no list.
- [ ] Add the `Edit text` action to the completion notice of an editable job (`D04 T01 §6`), opening the output in a new tab with Edit PDF mode on (`D03 T04 §11`). Done when: clicking it opens the file in Edit PDF mode.
- [ ] Register `convert.editableOcr` in `CommandRegistry` (`D02 T03 §11`) as `Make scan editable (OCR)` with synonyms `editable`, `ocr edit`, `scan to editable`, opening Convert with the current document and the editable mode selected. Done when: `Ctrl+K` "editable" finds it.
- [ ] Add `AppTests.EditableOcrUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=AppTests.EditableOcrUi` passes on the display-session host.
- [ ] Commit: `convert: add the editable text output mode to the Searchable PDF card`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.EditableOcrUi` passes on the display-session host, and a build with the `Output` selector removed fails it.

## 21. Page area export engine (image or text) and `recto-cli export-area`

Foxit PDF Editor's snapshot and area export lets a reader save a chosen rectangle of a page as a picture or as its text. `export_area` (`src/core/area_export.h/.cpp`, `recto::core`) is the engine for it: the rectangle is given in points on the page as displayed (top-left origin, `/Rotate` applied, inside the CropBox), the image output renders only that rectangle through the Direct2D rasterizer of `D01 T02 §6` at `72..1200` DPI into `PNG`, `JPEG`, `TIFF` or `BMP`, and the text output collects the `PageText` characters whose quad centres fall inside the rectangle in reading order, running `OcrPipeline` on the cropped area when the area is a scanned image or holds no text (`D03 T01 §7`, `§8`). It never renders the whole page to crop it, bounds the pixel size, runs as a job on its own `recto::pdf::Document`, and commits files through `AtomicFileWriter` (`§1`). It does not touch the clipboard; the viewer tool of `§22` and the snapshot tool of `D03 T01` own that.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `export_area_image` (clip-rect rasterization with banded output for large areas) and `export_area_text` (quad-centre selection, reading order, OCR fallback) in `src/core/area_export.cpp`. Done when: `AreaExport.Image` renders a known `100 x 50 pt` area at `300` DPI to exactly `417 x 208` pixels with the expected pixels, and `AreaExport.Text` returns exactly the words inside a rectangle that cuts a paragraph.
2. Add the bounds, the units and `recto-cli export-area`. Done when: `AreaExport.*` and `Cli.ExportArea` pass under ASan.

- [ ] Define `AreaExportOptions` (`page_index`, `rect_pt`, `kind` `Image|Text`, `image_format`, `dpi`, `colour_mode`, `transparent_background`, `ocr` `Auto|Never|Always`, `ocr_language_bcp47`, `keep_line_breaks`) and `AreaExportResult` (`bytes` or `text`, `pixel_size`, `ocr_used`, `warnings`) in `src/core/area_export.h`. Done when: the header compiles across `recto-core`, `recto-services`, `recto-cli` and `recto-app`.
- [ ] Normalize `rect_pt` (swap reversed corners, clamp to the CropBox, refuse an empty or under `1 x 1 pt` rectangle with a typed error) and convert it to the page's user space for the renderer. Done when: `AreaExport.RectNormalization` covers a reversed rectangle, a rectangle partly off the page, a rotated page (`/Rotate 90`) and an empty rectangle.
- [ ] Render only the rectangle (translate and clip the `DisplayList` replay, never render the full page and crop), in horizontal bands of at most `256` pixel rows when the area exceeds `64` megapixels, and refuse an area above `268` megapixels or `16384` pixels on a side with a typed error. Done when: `AreaExport.LargeAreaBanded` exports a `600` DPI area of a large-format page with bounded memory and `AreaExport.Bounds` refuses the oversized request. Cheaper substitute: rasterizing the whole page and cropping the bitmap.
- [ ] Select text by quad centre inside the rectangle, order it by the reading order of `PageText` (`D03 T01 §1`), join lines with `\r\n` or a space per `keep_line_breaks`, and fall back to `OcrPipeline` on the cropped area when `ocr` is `Auto` and the area has no text or lies on a `ScannedRaster` page. Done when: `AreaExport.TextAndOcr` returns the digital words for a text area and the recognized words for a scanned area, and marks `ocr_used`.
- [ ] Write image files through WIC with exact `SetResolution(dpi, dpi)` and text files as UTF-8 through `AtomicFileWriter`, checking `CancelToken` per band. Done when: `AreaExport.WriteAndCancel` reads back the DPI and, on cancel, leaves no temp file.
- [ ] Implement `recto-cli export-area <in.pdf> --page <n> --rect <x0,y0,x1,y1> [--unit pt|mm|in] --as image|text --out <file> [--format png|jpg|tiff|bmp] [--dpi <72..1200>] [--ocr auto|never|always] [--lang <tag>] [--json]` in `src/cli/cmd_export_area.cpp`. Done when: `Cli.ExportArea` passes under ASan for an image export and a text export.
- [ ] Add `AreaExport.*` tests in `tests/recto-tests/area_export_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=AreaExport.*:Cli.ExportArea` passes under ASan.
- [ ] Commit: `convert: add the page area export engine and recto-cli export-area`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AreaExport.*:Cli.ExportArea` passes under ASan, and cropping a full-page render instead of clipping makes `AreaExport.LargeAreaBanded` fail its memory bound.

## 22. Export Area tool on the extractor bar, `Ctrl+K` commands and save dialog

Give the area engine (`§21`) a tool in the viewer: **Export Area** draws a rectangle on the page, then offers `Save as image` or `Save as text` with the format, resolution and OCR choices, and saves to a file the user picks. It reuses the region drag of `TableCanvasOverlay` (`§10`) so the page has one rectangle-drawing behaviour, adds an `Export Area...` button to the `TableImageExtractorBar` (`§10`), and is reachable from `Ctrl+K` as `Export area as image` and `Export area as text`. Foxit PDF Editor offers the same area export; Acrobat has only the snapshot copy. Recto keeps its rules: the tool never changes the document, the save is atomic, and OCR on a scanned area runs locally.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 8.2 (contextual mode bars, the `Table & Image Extractor Bar` and its on-canvas region drag); the `Export Area...` button and the save popover are a genuinely new control on that bar and follow its 40 epx chrome.
**Job:** A reader can drag a rectangle over any part of a page, see its pixel size and a preview, and save it as a PNG, JPEG, TIFF or BMP picture at a chosen resolution, or as the text inside it, without exporting the whole page. Consumer: `DocumentView` (`D02 T02`), the extractor bar (`§10`) and the area engine (`§21`).
**Treatment:** The shared rectangle drag of `TableCanvasOverlay`, an `AreaExportPopover` anchored to the rectangle with `Image` and `Text` tabs, a live pixel-size line, and `Save...` through the file picker. Cheaper substitute that fails the checkpoint: exporting the whole page, or a dialog with typed coordinates instead of an on-canvas rectangle.
**Chrome:** consume `DocumentView` (`D02 T02`), `TableImageExtractorBar` and `TableCanvasOverlay` (`§10`), the modal host (`D02 T01 §10`), `CommandRegistry` (`D02 T03 §11`), `RectoAccentBrush` and Segoe Fluent Icons.

**Build order:**

1. Add the `Export Area` mode to `TableCanvasOverlay`, the bar button and the `AreaExportPopover`. Done when: `AppTests.ExportAreaUi` draws a rectangle on a page and the popover shows the size in pixels at the chosen DPI.
2. Wire `Save...`, the `Ctrl+K` commands and the OCR indicator. Done when: `AppTests.ExportAreaUi` saves an image and a text file and finds the expected content.

- [ ] Add an `Export Area` mode to `TableCanvasOverlay` that reuses its rectangle create, move and resize handles in `RectoAccentBrush` and reports the rectangle to a new `AreaExportViewModel`. Done when: dragging, moving and resizing the rectangle updates the view model and `Esc` clears it.
- [ ] Add the `Export Area...` button to `TableImageExtractorBar` (`§10`) with an automation name and tooltip, and open the bar in this mode. Done when: the button passes the `D03 T02 §3` inventory and opens the mode.
- [ ] Build `AreaExportPopover` (`src/app/controls/AreaExportPopover.xaml/.cpp`) with `Image` (`Format`, `Resolution`, `Colour`, `Transparent background` for PNG, pixel-size line) and `Text` (`Keep line breaks`, `OCR scanned areas` with a note when OCR will run) tabs and a `Save...` button. Done when: changing the DPI updates the pixel-size line within 100 ms and the controls are keyboard reachable in the order of `D03 T02 §4`. Cheaper substitute: typed coordinates in a modal dialog.
- [ ] Render a small preview of the rectangle on a worker with `CancelToken` and show it in the popover. Done when: the preview shows the area and never blocks the UI thread over 16 ms.
- [ ] Save through the `FileSavePicker`, run `export_area` as a queue job (`D04 T01 §5`), and report success or the typed error in an `InfoBar`. Done when: `AppTests.ExportAreaUi` saves a `PNG` and a `.txt` and a refusal for an oversized area shows its message and writes nothing.
- [ ] Register `view.exportAreaImage` and `view.exportAreaText` in `CommandRegistry` (`D02 T03 §11`) as `Export area as image` and `Export area as text` with synonyms `crop`, `snapshot`, `save region`. Done when: `Ctrl+K` "crop" finds both and starts the mode.
- [ ] Add `AppTests.ExportAreaUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=AppTests.ExportAreaUi` passes on the display-session host.
- [ ] Commit: `viewer: add the Export Area tool for page areas as image or text`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.ExportAreaUi` passes on the display-session host, and exporting the whole page instead of the rectangle makes it fail on the saved pixel size.

## 23. More image inputs to PDF: ICO, JFIF, DIB, HEIC, WebP, JPEG XL via installed codecs

PDFelement converts more picture types than Recto's `Images -> PDF` path (`§5`): ICO, JFIF, DIB, HEIC, WebP and JPEG XL. Recto reads them through the Windows Imaging Component, so a format works when the machine has a codec for it and otherwise fails with a message that names the missing codec, never a bare error and never a blank page. `ImageCodecProbe` (`src/core/image_codec_probe.h/.cpp`) identifies a file by its signature rather than its extension (a renamed `.png` that is really a WebP still converts), asks WIC whether a decoder exists, and `images_to_pdf` (`§5`) then decodes and embeds the pixels. Recto bundles no codec and downloads nothing (offline by default, D16); it says which Windows extension provides the missing one. JFIF files are ordinary JPEGs and take the lossless `DCTDecode` pass-through of `§5`. Formats that need a decode are written losslessly with `FlateDecode` (never a lossy re-encode), alpha goes to an `/SMask`, EXIF orientation is honoured, and images with a wide gamut or more than 8 bits per channel are converted to 8-bit sRGB through the WIC colour context with a recorded warning.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `ImageCodecProbe::identify` (signature sniffing for ICO, JPEG/JFIF, DIB, HEIC/HEIF, WebP, JPEG XL, plus the formats of `§5`) and `has_decoder` (WIC `CreateDecoderFromStream` failure classification) with the missing-codec message table. Done when: `ConvertImagesToPdf.CodecProbe` identifies a renamed file by signature and returns the right missing-codec message when a fake `IWICImagingFactory` reports no component.
2. Extend `images_to_pdf` (`§5`) to use the probe: ICO frame choice, JFIF pass-through, headerless DIB, and the decoded formats, plus `recto-cli images-to-pdf --list-codecs`. Done when: `ConvertImagesToPdf.MoreFormats` and `Cli.ImagesToPdf` pass under ASan on every format whose codec is present and skip, with the codec message asserted, for each one that is absent.

- [ ] Implement signature sniffing in `src/core/image_codec_probe.cpp`: ICO (`00 00 01 00`), JFIF/JPEG (`FF D8 FF`), DIB (a `BITMAPINFOHEADER`, `BITMAPV4HEADER` or `BITMAPV5HEADER` size field with sane dimensions and no `BM` file header), HEIC/HEIF (an ISO base media `ftyp` box with a `heic`, `heix`, `mif1` or `msf1` brand), WebP (`RIFF....WEBP`) and JPEG XL (the `FF 0A` codestream or the `JXL ` container box). Done when: `ConvertImagesToPdf.CodecProbe` identifies each, and a text file named `.png` is refused with `UnsupportedImageFormat`.
- [ ] Map a WIC decoder failure (`WINCODEC_ERR_COMPONENTNOTFOUND`, `WINCODEC_ERR_UNKNOWNIMAGEFORMAT`) to a message that names the format and the Windows codec extension that provides it, kept in one table (`src/core/image_codec_names.h`) checked against Microsoft Learn when written, and state that Recto does not download codecs. Done when: `ConvertImagesToPdf.MissingCodecMessage` returns the message with the format and extension name for HEIC, WebP and JPEG XL on a machine double with no codecs, and the message appears in the queue card error (`D04 T01 §7`). Cheaper substitute: a generic `Could not read image` message.
- [ ] Treat `.jfif`, `.jpe` and JFIF-signed files as JPEG: parse SOF for dimensions and components and embed the bytes unchanged with `/DCTDecode`. Done when: `ConvertImagesToPdf.JfifPassThrough` finds the embedded stream byte-identical to the input.
- [ ] Decode `.ico` through WIC and add `ico_frames` (`Largest` default, `All`) to the options: the largest frame as one page, or every frame as its own page, with alpha as an `/SMask`. Done when: `ConvertImagesToPdf.IcoFrames` converts a 4-size icon into one page (`Largest`) and four pages (`All`) with transparency preserved.
- [ ] Wrap a headerless `.dib` in a `BITMAPFILEHEADER` in memory and decode it through the BMP decoder, applying the alpha policy of `D04 T02 §14` (opaque unless a real alpha mask exists). Done when: `ConvertImagesToPdf.DibFile` converts 8, 24 and 32 bit DIBs and a 32-bit screenshot DIB stays opaque.
- [ ] Decode HEIC, WebP and JPEG XL through WIC when the codec is present, apply EXIF orientation, convert 16-bit or wide-gamut pixels to 8-bit sRGB with the image's colour context and record `ColourConvertedToSrgb` as a warning, and write the pixels with `FlateDecode` (and an `/SMask` for alpha). Done when: `ConvertImagesToPdf.DecodedFormats` converts a fixture of each format on a machine that has the codec, never uses a lossy re-encode, and records the warning for a 16-bit fixture.
- [ ] Add `recto-cli images-to-pdf --list-codecs [--json]` printing which of the formats this machine can read and the extension that adds each missing one. Done when: `Cli.ImagesToPdf` asserts the list against the machine double.
- [ ] Add fixtures to `scripts/build-corpus.py` (ICO, DIB and JFIF built from stdlib; HEIC, WebP and JPEG XL as small committed binaries) and record each in `tests/corpus/PROVENANCE.md` with its origin and licence. Done when: `python scripts/build-corpus.py --check` passes and every new fixture has a provenance row.
- [ ] Commit: `convert: read ICO, JFIF, DIB, HEIC, WebP and JPEG XL images through installed codecs`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ConvertImagesToPdf.CodecProbe:ConvertImagesToPdf.MissingCodecMessage:ConvertImagesToPdf.JfifPassThrough:ConvertImagesToPdf.IcoFrames:ConvertImagesToPdf.DibFile:ConvertImagesToPdf.MoreFormats:Cli.ImagesToPdf` passes under ASan, and re-encoding the JFIF as a lossy JPEG makes `ConvertImagesToPdf.JfifPassThrough` fail.

## 24. New blank PDF engine: size presets, paper styles and `recto-cli new-pdf`

UPDF and PDFelement create a new, empty PDF from a page size and a paper style, and Acrobat offers a blank page insert but not paper styles. `create_blank_pdf` (`src/core/blank_pdf.h/.cpp`, `recto::core`) writes a new PDF through `PdfWriter` (`§2`) and `AtomicFileWriter` (`§1`) with `1..1000` pages of a chosen size, orientation and paper style: `Plain`, `Lined` (narrow, college and wide rule spacing), `Grid`, `Dot` and `Music` (five-line staves, a set number per page). The ruling is vector content in one shared Form XObject reused on every page, so a 1000-page notebook stays small, drawn in light grey stroke values, with an optional margin line. Presets cover ISO A0 to A6, B4 and B5, Letter, Legal, Tabloid, Executive and Statement, plus a custom width and height in points, millimetres or inches within the `3..14400` point limit of PDF 1.7 implementations. Recto keeps its rules: the file is a new document, written temp-file-then-replace, with no script, and the sizes are exact.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `BlankPdfOptions`, the size preset table and the style parameters, and implement the page and shared-style Form XObject writer in `src/core/blank_pdf.cpp`. Done when: `BlankPdf.PagesAndSizes` writes `3` pages for each preset and orientation and the `/MediaBox` equals the preset to `0.01 pt`.
2. Implement each paper style and `recto-cli new-pdf`. Done when: `BlankPdf.*` and `Cli.NewPdf` pass under ASan and the style pixels re-render as the style requires.

- [ ] Define `BlankPageSize` (the preset enum and `Custom` with width, height and unit), `BlankPaperStyle` (`Plain`, `Lined`, `Grid`, `Dot`, `Music`), `BlankPdfOptions` (`size`, `landscape`, `page_count` `1..1000`, `style`, `spacing_pt`, `line_weight_pt`, `margin_line`, `staves_per_page`, `title`) and the preset table in millimetres from the ISO 216 and North American sizes in `src/core/blank_pdf.h`. Done when: `BlankPdf.PresetTable` checks A4 `595.28 x 841.89 pt` and Letter `612 x 792 pt` to `0.01 pt`.
- [ ] Refuse a custom size outside `3..14400` points on a side, a page count outside `1..1000`, any non-finite number (NaN or infinity), `spacing_pt` outside `2..720`, `line_weight_pt` outside `0.1..10`, `staves_per_page` outside `1..40`, and any style that would draw more than `200,000` primitives on one page (computed with checked arithmetic before drawing), with typed errors before writing. Done when: `BlankPdf.Bounds` covers every limit, zero and tiny spacing, and a dot grid at the minimum spacing on the largest page, and writes no file.
- [ ] Write each style as vector content in one shared Form XObject (`/Subtype /Form`, `/BBox` equal to the page) painted on every page with `Do`: `Lined` horizontal rules at the chosen spacing, `Grid` crossing rules, `Dot` small filled circles at the grid points, `Music` groups of five staff lines with a gap between staves, and an optional vertical margin line. Done when: `BlankPdf.Styles` re-renders each style and finds the expected rule count and spacing, and a 1000-page `Lined` file stays under `200 KB`. Cheaper substitute: one content stream per page, which fails the size bound.
- [ ] Draw the ruling in a fixed light grey (`0.75 G`) with the requested line weight, clipped to the printable area (a `36 pt` inner margin), so printing and dark mode both show it. Done when: `BlankPdf.Printable` finds no ruling outside the margin box.
- [ ] Write `/Info` `Title` (default `Untitled`), `/ID` and the page tree through `PdfWriter` and commit through `AtomicFileWriter`, checking `CancelToken` per page. Check `CancelToken` every `1000` primitives while generating the style and between pages. Done when: `BlankPdf.CancelDuringStyle` cancels inside style generation of a dense dot grid within `100 ms`, and `BlankPdf.CancelLeavesNothing` aborts mid-write and finds the folder unchanged, and `Document::open` parses the output without recovery.
- [ ] Implement `recto-cli new-pdf --out <file.pdf> [--size a0..a6|b4|b5|letter|legal|tabloid|executive|statement|<w>x<h><pt|mm|in>] [--landscape] [--pages <1..1000>] [--style plain|lined|grid|dot|music] [--spacing <pt>] [--margin-line] [--title <text>]` in `src/cli/cmd_new_pdf.cpp`. Done when: `Cli.NewPdf` passes under ASan for each style.
- [ ] Add `BlankPdf.*` tests in `tests/recto-tests/blank_pdf_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=BlankPdf.*:Cli.NewPdf` passes under ASan.
- [ ] Commit: `convert: add the blank PDF engine with page-size presets and paper styles`

**Test checkpoint:** `recto-tests.exe --gtest_filter=BlankPdf.*:Cli.NewPdf` passes under ASan, and writing the style per page instead of through the shared Form XObject makes `BlankPdf.Styles` fail its size bound.

## 25. New blank PDF dialog, File menu entry and `Ctrl+K` command

Make the blank PDF engine (`§24`) a command: **New blank PDF** opens a small dialog with the page size, orientation, number of pages and paper style with a live preview, and opens the result as a new unsaved tab through `OpenDocumentPipeline::open_generated_async` (`§15`), so it behaves exactly like a pasted clipboard PDF (dirty on close, `Save a copy`, crash recovery). It is reachable from the Home screen, the `+` New Tab menu, the Classic `File > New` menu and `Ctrl+K`. The dialog's first page size is seeded from the `Default Office to PDF page setup` setting (`D02 T04 §5`), and the last choices are remembered in settings. UPDF and PDFelement offer this; Recto keeps its rule that nothing is written until the user saves.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new command on existing chrome: it adds an entry to the Home screen (`design/SPEC.md` section 9.1, `design/screenshots/D-Home.png`), the `+` New Tab menu of the title bar (section 8.1), the Classic `File` menu (section 9.2) and the command box (section 9.4), and a standard WinUI `ContentDialog` hosted as in section 9.9.
**Job:** A user can create a notebook, a ruled sheet, a music pad or a blank page in any common size in two clicks, see it before creating it, and start writing or annotating on it. Consumer: `OpenDocumentPipeline` (`D02 T01 §5`), `SavePipeline` (`D02 T01 §8`) and crash recovery (`D02 T01 §12`).
**Treatment:** A `file.newBlank` command that opens `NewBlankPdfDialog` (preview rendered by the `§24` engine at low DPI on a worker), runs `create_blank_pdf` as a job into a private temp file, and opens it with `open_generated_async`. Cheaper substitute that fails the checkpoint: a dialog that writes into the Documents folder without the user saving, or a preview that is a static picture.
**Chrome:** consume `ShellPage` and `TabView` (`D02 T01 §3`), `OpenDocumentPipeline` (`D02 T01 §5`), the dialog host (`D02 T01 §10`), the Home screen (`D02 T03 §1`), `CommandRegistry` (`D02 T03 §11`), the settings store (`D02 T04 §5`) and `RectoColors.xaml` tokens.

**Build order:**

1. Build `NewBlankPdfDialog` with the size, orientation, pages and style controls and the live preview. Done when: `AppTests.NewBlankPdfUi` changes each control and the preview updates.
2. Wire the command, the four entry points, the settings memory and the open-as-new-tab flow. Done when: `AppTests.NewBlankPdfUi` creates a notebook through each entry point and finds an unsaved tab with the chosen page count and size.

- [ ] Build `NewBlankPdfDialog` (`src/app/dialogs/NewBlankPdfDialog.xaml/.cpp`, hosted via `D02 T01 §10`) with `Page size` (the presets plus `Custom` with unit), `Orientation`, `Pages` (`1..1000`), `Paper style` and its spacing option, each with an automation name and tooltip. Done when: the dialog passes the `D03 T02 §3` inventory and a custom size outside `3..14400 pt` shows an inline error and disables `Create`.
- [ ] Render the preview of page 1 with `create_blank_pdf` at low DPI on a worker with a `CancelToken` and a 150 ms debounce. Done when: changing the style updates the preview and the UI thread never blocks over 16 ms. Cheaper substitute: a static sample image per style.
- [ ] Register `file.newBlank` in `CommandRegistry` (`D02 T03 §11`) as `New blank PDF` with synonyms `notebook`, `ruled`, `grid paper`, `music paper`, `empty pdf`, always enabled while the shell is open. Done when: `Ctrl+K` "notebook" finds it.
- [ ] Add the entry to the Home screen action row, the `+` New Tab menu and the Classic `File > New` menu, each routed to the same command with an automation name and tooltip. Done when: all three entry points open the dialog.
- [ ] Seed `Page size` from `Default Office to PDF page setup` (`D02 T04 §5`) the first time and store the last choices under a `newBlankPdf.*` settings key that the dialog reads on the next open. Done when: `AppTests.NewBlankPdfUi` shows the first-use default follows the setting and a second open restores the last choices. Cheaper substitute: storing the choices and never reading them.
- [ ] Run `create_blank_pdf` as a queue job into a private temp file, then open it with `OpenDocumentPipeline::open_generated_async` (`§15`) as `Notebook 1.pdf` style display names, marked unsaved, with the temp file owned by the tab and covered by crash recovery. Done when: closing the tab asks `Save your changes first?`, a simulated crash restores it, and `AppTests.NewBlankPdfUi` finds no new file outside the private temp folder before the user chooses `Save a copy`.
- [ ] Add `AppTests.NewBlankPdfUi` in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=AppTests.NewBlankPdfUi` passes on the display-session host.
- [ ] Commit: `app: add New blank PDF with page-size presets and paper styles`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.NewBlankPdfUi` passes on the display-session host, and a tab that opens clean instead of dirty, or any file appearing in Documents or another user folder before Save, makes it fail.

## Verification

- `AtomicFileWriter`, `PdfWriter`, PDF to images/TIFF/GIF/SVG/XPS, images/SVG/XPS to PDF, PDF to text, Searchable PDF (OCR), `TableExtractor`, `ImageExtractor`, `PdfOptimizer`, and `Linearizer` all pass under ASan and round-trip in `D06 T01 §5`.
- Cancelling any conversion mid-job leaves zero temporary or partial files in the output directory.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
