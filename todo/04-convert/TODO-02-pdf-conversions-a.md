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

## Outcome

- `AtomicFileWriter` (`src/core/atomic_file.cpp`, `§1`) guarantees that every save writes to a sibling temporary file and atomically replaces the target only on clean completion, and `PdfWriter` (`src/pdf/writer.cpp`, `§2`) writes valid, compressed PDF 1.7 and 2.0 documents.
- PDF to Images/Vector (`§3`, `§4`) streams pages at `72`, `150`, `300`, `600` or `1200` DPI through WIC encoders (`PNG`, `JPEG`, single/multipage `TIFF` with `LZW`/`CCITT Group 4`/`JPEG`, `BMP`, `Animated GIF`) and vector `SVG` (`.svg`) / `OpenXPS` (`.xps`) writers (`src/core/svg_xps_writer.cpp`) with bounded memory.
- Images/SVG/XPS to PDF (`§5`) packs one or many images, multipage TIFFs, vector SVGs or XPS files into a clean PDF, honoring EXIF orientation and embedding JPEG streams directly via `DCTDecode` without lossy re-encoding.
- PDF to Text (`§6`) and Searchable PDF (OCR) (`§7`) export UTF-8 `.txt` and sandwich-layer searchable PDFs (`3 Tr` invisible text aligned to `OcrPipeline` word boxes), exposed both in the Convert UI and in `recto-cli`.
- `TableExtractor` (`§8`) and `ImageExtractor` (`§9`) detect bordered and borderless tables and extract embedded XObject images without re-compression, powering the Viewer `Table & Image Extractor Bar` and the `Extract Tables & Images` Convert card (`§10`).
- `PdfOptimizer` and `Linearizer` (`§11`, `§12`) downsample images, recompress colour/grayscale/monochrome streams, subset fonts, strip unused objects, linearize for Fast Web View, and present a per-category byte-savings audit dialog.
- After the owner's decision, a `From scanner` source acquires pages through WIA 2.0 into the Images to PDF and searchable OCR paths (`§13`).

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
- [ ] Flush file buffers to disk via `FlushFileBuffers` and close the handle before invoking `MoveFileExW(temp_path, target_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` inside `commit()`. Done when: `AtomicFile.CommitNewAndReplace` writes new files and overwrites existing files cleanly.
- [ ] Ensure `~AtomicFileWriter()` closes any open handle and calls `DeleteFileW(temp_path)` whenever `committed_ == false`. Done when: `AtomicFile.DestructorRollback` proves that scope exit without `commit()` leaves zero `.tmp` files in the directory and leaves an existing target file's SHA-256 unchanged. Cheaper substitute: writing directly to `target_path` with `std::ofstream`, which truncates and corrupts the user's file on cancel or error.
- [ ] Surface descriptive `PdfError::IoError` messages when the destination directory is read-only or the target file is locked by another process (`ERROR_SHARING_VIOLATION`). Done when: `AtomicFile.LockedTargetError` verifies an open sharing lock on `target_path` fails `commit()` cleanly, deletes the `.tmp` file, and leaves the locked file intact.
- [ ] When the target already exists, replace it with `ReplaceFileW(target, temp, backup, REPLACEFILE_WRITE_THROUGH, ...)` so the destination keeps its ACL, attributes, creation time and alternate streams, using `MoveFileExW` only for new files, and reconcile the documented partial failures `ERROR_UNABLE_TO_REMOVE_REPLACED`, `ERROR_UNABLE_TO_MOVE_REPLACEMENT` and `ERROR_UNABLE_TO_MOVE_REPLACEMENT_2` from the backup (`docs/research/2026-10-20-pdfcraft-issue-lessons.md`, pdfcraft #2658, #2921). Done when: `AtomicFile.ReplacePreservesMetadata` keeps the ACL and creation time and `AtomicFile.ReplacePartialFailure` restores the original in each of the three states.
- [ ] Accept an optional `verify(temp_path)` callback that `commit()` runs after `FlushFileBuffers` and before the replace, aborting with the original untouched when it fails (pdfcraft #1698, #2366). Done when: `AtomicFile.VerifyBeforeReplace` proves a failing callback leaves the destination hash unchanged.
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

"Create PDF from scanner" was one of pdfcraft's most repeated requests, filed at least eight times (`docs/research/2026-10-20-pdfcraft-issue-lessons.md`; pdfcraft #312, #1774, #2010, #3256, #3384). Recto can acquire pages through Windows Image Acquisition 2.0 (`IWiaDevMgr2`), feed them to the `Images -> PDF` converter (§5 here) and optionally to the searchable-PDF OCR writer (§7 here), all locally. This is new v1-plus scope, so the owner decides whether and when it ships before any code is written.

**Phase:** 7
**Needs:** windows-host, display-session, owner-decision
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.5 (Convert page cards and Step 3 options); a `Scanner` source card is a genuinely new surface on that page with no screenshot.
**Job:** A user with a scanner creates a PDF (optionally searchable) from one or more scanned pages without another app. Consumer: the `Images -> PDF` converter (§5) and `OcrPipeline` (§7).
**Treatment:** A `From scanner` source card on the Convert page with device, source (flatbed or feeder), colour mode, resolution and duplex options, a running page list, and `Make PDF`. Cheaper substitute that fails the checkpoint: shelling out to the Windows Scan app or a vendor tool.
**Chrome:** Convert page card registry and Step 3 options host (`D04 T01 §3`, `§4`), queue card for the job (`D04 T01 §5`), `RectoColors.xaml` tokens and Segoe Fluent Icons `E8FE` (scan).

**Build order:**

1. Record the owner decision (ship in which release, supported device classes) in the foundation plan with its date and cost of reversing, then remove `owner-decision` from this section's Needs. Done when: the decision line exists in the plan.
2. Implement `src/services/scanner/WiaScanner.h/.cpp` enumerating WIA 2.0 devices and acquiring pages on a job worker with `CancelToken`, then convert through §5 and optionally §7. Done when: `Convert.ScannerToPdf` passes against the WIA test driver.

- [ ] Record the owner decision in `docs/plans/2026-10-13-recto-foundation.md` and in `docs/user-demand.md`. Done when: both files name the decision and its date.
- [ ] Implement `WiaScanner::list_devices()` through `IWiaDevMgr2::EnumDeviceInfo` off the UI thread with a 3 s timeout per device. Done when: an unresponsive device shows as unavailable instead of blocking the card.
- [ ] Implement page acquisition for flatbed and feeder sources with colour mode, resolution and duplex, streaming each page to a temp file through `AtomicFileWriter`. Done when: a five-page feeder job yields five page images.
- [ ] Map WIA errors (paper empty, paper jam, device busy, cover open) to plain messages with next steps. Done when: each mapped `WIA_ERROR_*` code shows its message in `Convert.ScannerErrors`.
- [ ] Add the `From scanner` card and options to the Convert page and the `convert.fromScanner` command to `CommandRegistry`. Done when: `Ctrl+K` "scan" finds the command. Cheaper substitute: a hidden menu entry with no command-box reach.
- [ ] Feed acquired pages to §5's converter with EXIF orientation honoured and, when chosen, to §7's searchable OCR writer. Done when: the produced PDF has one page per scan and searchable text when OCR was selected.
- [ ] Commit: `convert: create PDFs from WIA scanners with optional OCR`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Convert.ScannerToPdf:Convert.ScannerErrors` passes against the WIA sample test driver on the display-session host.

## Verification

- `AtomicFileWriter`, `PdfWriter`, PDF to images/TIFF/GIF/SVG/XPS, images/SVG/XPS to PDF, PDF to text, Searchable PDF (OCR), `TableExtractor`, `ImageExtractor`, `PdfOptimizer`, and `Linearizer` all pass under ASan and round-trip in `D06 T01 §5`.
- Cancelling any conversion mid-job leaves zero temporary or partial files in the output directory.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
