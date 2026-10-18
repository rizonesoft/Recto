# Recto: Foundation Plan

- **Date:** 2026-10-13 (revised 2026-10-14 for native `recto-pdf` engine, built-in OCR, native converters, and `GPL-3.0-or-later`)
- **Status:** Approved foundation for implementation
- **Target:** Open-source Windows 11 PDF viewer, OCR tool and document converter (WinUI 3, C++20, native `recto-pdf` engine, Inno Setup full & self-contained portable installer + optional MSIX for the Microsoft Store)
- **Design reference:** `design/SPEC.md` (direction "D . Merged")

This document records the technical decisions, architecture, threading contract, milestones, and acceptance criteria for Recto v1. It exists so implementation does not stall on undecided fundamentals and so known hard problems (`recto-pdf` parsing and rendering fidelity, multi-threaded actor and tile safety, built-in OCR, native OpenXML conversions, GPLv3 compliance, untrusted PDF input) are designed in from the start instead of retrofitted.

---

## 1. Decisions

Every row below is a binding decision for v1. Changing one requires updating this table with the date, reason, and cost of reversing.

| # | Topic | Decision | Reason | Cost to reverse |
| - | ----- | -------- | ------ | --------------- |
| D1 | Product name | **Recto** | Matches `design/SPEC.md` and all generated brand and MSIX assets. Run a USPTO and EUIPO trademark search and reserve the Microsoft Store name in M0 before investing in public listings. | Low before Store submission (`build_resources.py` regenerates wordmarks); high after a Store listing exists. |
| D2 | Project license | **GPL-3.0-or-later** (revised 2026-10-14 from `AGPL-3.0-or-later`) | Retiring Artifex MuPDF (D3, D9) removes the sole AGPL dependency; `GPL-3.0-or-later` gives strong copyleft protection for a desktop application while simplifying library compatibility and distribution. Copyright stays with the project owner via a lightweight CLA so future licensing remains possible. | High once external contributors land without a CLA; low with a CLA in place from commit 1. |
| D3 | PDF engine | **Recto's own native C++20 PDF engine (`recto-pdf`)** (revised 2026-10-14 from Artifex MuPDF) | Full ownership of the PDF 1.7 and 2.0 parser, stream filters, decryptor, content-stream interpreter, display-list builder, structured text/table/image extractor, interactive AcroForm/XFA engine, annotations, true redaction, X.509/PAdES digital signatures, OCG layers, portfolios, PDF/A & PDF/UA & ZUGFeRD/Factur-X compliance, optimizer/linearizer, and writer; zero C `setjmp`/`longjmp` boundary, zero external engine licensing constraints, and direct integration with Direct2D, DirectWrite, WIC, Windows CNG/CryptoAPI and `Windows.Media.Ocr`. | High (core engine module), mitigated by keeping `recto-pdf` behind a clean static-library boundary (`src/pdf`). |
| D4 | UI framework | **WinUI 3 (Windows App SDK 2.4.0) in C++20 / C++/WinRT** | Matches `design/SPEC.md` ("Native first. Use stock WinUI 3 controls..."). Same language as the engine; no .NET runtime or P/Invoke marshalling for tile buffers. | High (full UI rewrite). |
| D5 | Language boundary | **`recto-pdf` (C++20) -> `recto-core` (C++20) -> `recto-services` (C++20) -> `Recto` (C++20/WinRT)** (revised 2026-10-14) | Every module is native C++20. `recto-pdf` uses typed `PdfResult<T>` / `PdfError` values (never exceptions or `longjmp` for malformed document data); `recto-core` owns actors, tile rendering, OCR, converters, comparison, and document authoring; `recto-services` owns GPU presentation, job queues and settings; `Recto` owns WinUI XAML surfaces. | Low-medium (`recto-pdf` is a self-contained static library). |
| D6 | Distribution & Portable mode | **Inno Setup (`installer/recto.iss` via latest `ISCC.exe`) as primary distribution supporting both Full Install and Self-Contained Portable Install, plus an in-app Portable mode setting (`<exe-dir>\data\`), Portable `.zip` archive, and optional MSIX / Microsoft Store packaging** (revised 2026-10-14) | Inno Setup installs cleanly on any Windows 11 machine in either standard desktop mode (`.pdf` file association, icons, uninstaller, `%LOCALAPPDATA%\Recto`) or zero-registry self-contained portable mode (`<exe-dir>\data\`), and users can toggle Portable mode live in `Settings`; MSIX and Microsoft Store submission (with custom `GPL-3.0-or-later` license terms) remain supported as an optional extra. | Low. |
| D7 | v1 conversion, authoring & compliance scope | **3-mode PDF -> Word (`.docx`/`.rtf`: Flowing, Exact, Continuous + orthogonal-line & grid table reconstruction), PDF -> Excel/Data (`.xlsx`/`.csv`/`.xml`), PDF -> Web/Docs (`.html` Fixed/Flowing, `.md`, `.epub`, `.tex`, `.svg`, `.xps`, `.txt`), PDF <-> Images (PNG, JPEG, BMP, GIF/Animated GIF, single & multipage TIFF), Searchable PDF (OCR + vector-outlined text OCR), Extract Tables & Images, Office/HTML/Markdown/XPS/SVG -> PDF, Merge/Split/Reorder/Rotate/Crop/Resize/Insert Blank, Watermarks/Headers/Footers/Bates Numbering, Interactive Forms (AcroForms/XFA + JSON/XFDF/FDF/XML), Annotations & Markup, Find & Replace (with Regex), True Redaction & Sanitization, X.509/PAdES Digital Signatures & Encryption, Layers (OCG), Attachments & Portfolios, PDF/A (`1a..3u`) & PDF/UA & ZUGFeRD/Factur-X E-Invoicing, Optimization/Compression/Linearization, and Document Comparison** (revised 2026-10-14) | Matches the expanded `design/SPEC.md` sections 8.2, 8.3, 9.2, 9.3, 9.5, and 9.6 and incorporates the full feature set of SautinSoft PDF Focus / PDF .NET, Aspose.PDF, and Syncfusion PDF. | Low-medium (modular engines behind `recto-pdf` and `JobQueue`). |
| D8 | Build system | **MSBuild (`.sln` + `.vcxproj`) as primary** for all C++20 modules | First-class support for WinUI 3, XAML compiler, MIDL3, PRI resources, self-contained unpackaged deployment, and optional MSIX packaging in Visual Studio 2026 (`v145`) and GitHub Actions. | Medium. |
| D9 | PDF engine dependencies | **In-tree C++20 `recto-pdf` over Windows 11 platform APIs (`Direct2D`, `DirectWrite`, `WIC`, `bcrypt.dll` CNG, `crypt32.dll` CryptoAPI/CMS, `winhttp.dll` RFC 3161 TSA, `Windows.Media.Ocr`)** (revised 2026-10-14; MuPDF submodule retired) | Zero external C/C++ engine submodule; cryptography (`AES-128`, `AES-256`, `RC4`, `SHA-256`/`384`/`512`, X.509 PKCS#7/CMS/PAdES signatures), image codecs (`JPEG`, `JPEG2000`, `PNG`, `TIFF` single/multipage, `BMP`, `GIF`), font rasterization, and OCR use hardened OS components that ship with Windows 11. | Low-medium. |
| D10 | To-PDF conversion strategy | **Built-in C++20 OpenXML (`.docx`, `.xlsx`, `.pptx`), `.rtf`, `.csv`, `.html`/CSS, `.md`, `.xps`, `.svg`, and `.txt`/`.xml` parser + DirectWrite/Direct2D PDF layout engine first; optional Microsoft Office COM automation (`AutomationSecurity = ForceDisable`) or LibreOffice headless when installed** (revised 2026-10-14) | Users convert Office, HTML, Markdown, XPS, SVG, and data documents out of the box with zero external installs while keeping 1:1 native Microsoft Office COM pagination available when Office is installed. Never bundle LibreOffice (saves ~300 MB). | Low (strategy pattern behind `IOfficeConverter`). |
| D11 | Contributor model | **DCO + lightweight CLA** granting the project owner a license to relicense contributions | Needed so the project can respond to Store policy changes or offer dual licensing later without tracking down every contributor. | Irreversible once contributions land without it. |
| D12 | Untrusted-input posture | **No PDF JavaScript execution in `recto-pdf` by construction; Office macros forced off during COM conversion; HTML-to-PDF scripts disabled; external links and portfolio attachments always prompt; one-click Document Sanitization** (revised 2026-10-14) | Eliminates the largest class of PDF, Office, and HTML exploits at design time (`recto-pdf` never implements a JS interpreter; `/JS` and `/JavaScript` actions are parsed as inert metadata and stripped by Sanitize). | Trivial. |
| D13 | Process model | **Single process in v1**, with `recto-pdf` and `recto-core` kept strictly free of UI types; **pre-approved post-v1 broker design (`D06 T02 §6`)** uses a restricted AppContainer worker process exchanging `DisplayList`, `PageText`, and `Bitmap` tiles over shared memory | Full multi-process sandboxing in v1 doubles IPC complexity before the viewer exists; pre-approving the shared-memory `DisplayList`/`PageText`/`Bitmap` broker architecture and keeping `recto-pdf` narrow preserves the exact boundary at low cost. | Medium if the boundary is respected; high if UI types leak into `recto-pdf` or `recto-core`. |
| D14 | Compiler, warnings, sanitizers | **MSVC (`v145`), `/std:c++20 /permissive- /Zc:preprocessor /utf-8 /W4 /WX`, `/sdl` in Release, AddressSanitizer CI job on `recto-pdf`, `recto-core` and `recto-tests`** | `recto-pdf` parses untrusted binary PDFs and Office ZIP containers; `/W4 /WX` and ASan catch memory errors when introduced. | Low. |
| D15 | Target OS and architectures | **Windows 11 22H2+ (build 22621+), x64 and ARM64 from M0** | Matches `design/SPEC.md` (Mica Alt, Segoe UI Variable, Segoe Fluent Icons, `Windows.Media.Ocr`). Building ARM64 from M0 prevents x64-only assumptions from accumulating. | Low in M0; high if deferred to M6. |
| D16 | Logging and diagnostics | **Structured ring-buffer logger + ETW (`TraceLoggingWrite`) + opt-in local minidump-plus-log save-and-share `.zip` bundle (`D07 T02 §4`)** | Multi-threaded actor, tile and job bugs are nearly impossible to diagnose without a low-overhead timeline. Opt-in crash reporting writes a local inspectable `.zip` bundle (minidump + ring-buffer log + version metadata, zero document bytes, zero network telemetry) in `%LOCALAPPDATA%\Recto\crash-reports` or `<exe-dir>\data\crash-reports` in Portable mode. | Low. |
| D17 | Layout presets | **One `ShellPage`, five layout settings, named slots** (`design/SPEC.md` section 8) | Focus, Classic, and Reading room are three preset combinations of five settings, never three separate shells. | High if violated. |

---

## 2. Tech Stack

| Layer | Choice | Notes |
| ----- | ------ | ----- |
| OS | Windows 11 22H2+ (`10.0.22621.0` min, `10.0.26100.0` target SDK) | Required for Mica Alt, Segoe UI Variable, Segoe Fluent Icons, `Windows.Media.Ocr` |
| Architectures | `x64`, `ARM64` | Both built in CI on every push |
| Toolchain | Visual Studio 2026 (`v145`), MSVC C++20 | `/W4 /WX /permissive- /utf-8 /Zc:preprocessor` |
| UI | WinUI 3, Windows App SDK `2.4.0`, C++/WinRT | Stock controls + `design/tokens/RectoColors.xaml` |
| Page surface | `SwapChainPanel` + Direct3D 11 + Direct2D 1.3 + DirectWrite | Tiled rendering from background presentation thread, plus interactive overlays for selection, annotations, form fields, redaction marks, table regions, crop handles, and diff highlights |
| PDF engine | `recto-pdf` (`src/pdf`, in-tree C++20) | PDF 1.7 / 2.0 lexer, XRef tables and streams, incremental updates, damaged-file recovery, Flate/Predictor/LZW/ASCII85/ASCIIHex/RunLength/CCITT filters, WIC JPEG/JPX/PNG/TIFF/BMP/GIF codecs, CNG `bcrypt.dll` RC4/AES-128/AES-256 crypto, `crypt32.dll` X.509 PKCS#7/CMS/PAdES digital signatures & verification, content-stream interpreter & operator editor, Direct2D/DirectWrite display-list builder, `PageText` & orthogonal-line table extractor, AcroForm/XFA engine, annotations & redaction burner, OCG layers, embedded files & portfolios, PDF/A (`1a..3u`) & PDF/UA structure tree & ZUGFeRD/Factur-X engine, optimizer/linearizer, `PdfWriter` and `GraftMap` |
| Built-in OCR | `Windows.Media.Ocr` (`OcrEngine`) + C++20 image preprocessor + vector-outlined-text detector + `recto-pdf` searchable-PDF writer | Deskew, contrast normalization, adaptive thresholding, `OcrEngine` word/line bounding boxes, OCR on vector-curve-outlined text, invisible text (`3 Tr`) PDF layer synthesis, and live `PageText` population |
| Native converters | In-tree C++20 OpenXML (`.docx`, `.xlsx`, `.pptx`), `.rtf`, `.csv`, `.xml`, `.html`/CSS, `.md`, `.epub`, `.tex`, `.svg`, `.xps` engines + optional Office COM / LibreOffice | 3-mode `PDF -> Word` (`.docx`/`.rtf`: Flowing, Exact, Continuous), `PDF -> Excel` (`.xlsx`/`.csv`/`.xml`), `PDF -> HTML5/Markdown/EPUB/LaTeX/SVG/XPS`, and built-in `Office/HTML/Markdown/XPS/SVG -> PDF`, plus optional Microsoft Office COM (`AutomationSecurity = ForceDisable`) or LibreOffice headless |
| Packaging & Installer | Inno Setup (`installer/recto.iss`: Full Install + Self-Contained Portable) + Portable `.zip` + Optional MSIX | Self-contained Windows App SDK deployment (`WindowsAppSDKSelfContained`) with `<exe-dir>\data\` portable mode and optional MSIX packaging (`resources/msix/Assets/`) |
| Unit / integration tests | GoogleTest via vcpkg manifest mode (test-only dependency) | Runs in CI on every push, with and without ASan |
| Fuzzing | MSVC `/fsanitize=fuzzer` + `/fsanitize=address` | Harnesses for `recto-pdf` parser, stream filters, decryptor, content streams, forms/annotations, and OpenXML ZIP reader |

---

## 3. Solution Structure

### 3.1 Modules and Dependency Rule

Dependencies flow strictly downward. No upward includes, no friend hacks, no XAML headers below `src/app`.

```text
+-------------------------------------------------------------+
|  Recto (src/app, WinUI 3 EXE, C++20 / C++/WinRT)            |
|  ShellPage, DocumentView, ConvertPage, OrganizePage,        |
|  AnnotationBar, FormDesignerBar, RedactBar, SignDialog, ... |
+------------------------------+------------------------------+
                               |
                               v
+-------------------------------------------------------------+
|  recto-services (src/services, static lib, C++20)           |
|  PresentationThread, JobQueue, OfficeComAdapter,            |
|  SettingsService, RecentFilesStore, UiaTextRangeModel       |
+------------------------------+------------------------------+
                               |
                               v
+-------------------------------------------------------------+
|  recto-core (src/core, static lib, C++20)                   |
|  Runtime, DocumentActor, RenderPool, TileScheduler,         |
|  OcrPipeline, TableExtractor, DocxWriter (3 modes + RTF),   |
|  XlsxWriter, HtmlMdEpubTexWriter, OfficeHtmlToPdfEngine,    |
|  PageOps, StampWatermarkBates, PdfCompare, PdfOptimizer     |
+------------------------------+------------------------------+
                               |
                               v
+-------------------------------------------------------------+
|  recto-pdf (src/pdf, static lib, C++20)                     |
|  Lexer, XRefParser, StreamFilters, Crypto (CNG),            |
|  Signatures (PKCS#7/PAdES/TSA), Document, Page, OcgLayers,  |
|  ContentInterpreter, OperatorEditor, FontLoader,            |
|  DisplayList, PageText, AcroFormXfa, Annotations,           |
|  RedactorSanitizer, PdfAUaZugferd, Portfolios,              |
|  PdfWriter, Linearizer, GraftMap                            |
+-------------------------------------------------------------+
```

Two additional executables link against `recto-core` and `recto-pdf` (never `src/app`):

- **`recto-cli`** (`src/cli`): command-line tool (`recto-cli info`, `render`, `text`, `find`, `replace`, `ocr`, `tables`, `extract-images`, `pdf-to-images`, `images-to-pdf`, `pdf-to-docx`, `pdf-to-rtf`, `pdf-to-xlsx`, `pdf-to-csv`, `pdf-to-html`, `pdf-to-md`, `pdf-to-epub`, `pdf-to-tex`, `pdf-to-svg`, `pdf-to-xps`, `office-to-pdf`, `html-to-pdf`, `md-to-pdf`, `merge`, `split`, `crop`, `stamp`, `bates`, `forms`, `annot`, `redact`, `sanitize`, `sign`, `verify`, `pdfa`, `zugferd`, `optimize`, `compare`, `encrypt`, `decrypt`, `bench`, `stress`). Built in M1 and expanded across M3 to M5 so every engine, OCR, authoring, compliance, and conversion feature is testable and scriptable alongside its UI.
- **`recto-tests`** (`tests/recto-tests`): GoogleTest runner for `recto-pdf`, `recto-core`, and `recto-services`.

### 3.2 Repository Layout

```text
RectoPDF/
|-- Recto.sln
|-- Directory.Build.props          # Shared warnings, /utf-8, /permissive-, SDK version, ASan toggle
|-- Directory.Build.targets
|-- vcpkg.json                     # Test-only dependencies (gtest)
|-- LICENSE                        # GPL-3.0-or-later full text
|-- NOTICE.md                      # Third-party attributions and licenses
|-- CLA.md                         # Contributor License Agreement
|-- build/
|   \-- build.ps1                  # One-command build and test driver
|-- src/
|   |-- pdf/                       # recto-pdf (C++20 static lib: PDF 1.7/2.0 parser, filters, crypto, signatures, forms, annotations, redaction, PDF/A & PDF/UA & ZUGFeRD, display lists, text, writer, linearizer)
|   |-- core/                      # recto-core (C++20 static lib: actors, tile render pool, OCR pipeline, table/image extractors, Word/Excel/HTML/MD/EPUB/LaTeX/SVG/XPS/Office converters, stamps/Bates, optimizer, comparison)
|   |-- services/                  # recto-services (C++20 static lib: D3D11/D2D presentation, job queue, settings, recents, UIA)
|   |-- cli/                       # recto-cli (C++20 console app)
|   \-- app/                       # Recto (WinUI 3 packaged app)
|-- tests/
|   |-- recto-tests/               # GoogleTest suite
|   \-- corpus/                    # Small, redistributable test PDFs & Office files + LICENSE per file
|-- fuzz/                          # libFuzzer harnesses (fuzz_open, fuzz_filters, fuzz_render, fuzz_text, fuzz_forms, fuzz_openxml_zip)
|-- resources/                     # Generated icons and MSIX assets (do not hand-edit)
|-- design/                        # SPEC.md, tokens, mockups, screenshots
\-- docs/
    \-- plans/
        \-- 2026-10-13-recto-foundation.md
```

### 3.3 Key Core Types (`src/pdf` and `src/core`)

```cpp
namespace recto::pdf {

enum class ErrorCode {
    Ok,
    FileNotFound,
    AccessDenied,
    NeedsPassword,
    WrongPassword,
    DamagedFormat,
    UnsupportedFeature,
    SignatureInvalid,
    ComplianceFailed,
    Cancelled,
    IoError,
    OutOfMemory,
    InternalError,
};

struct PdfError {
    ErrorCode code;
    std::string message; // Safe to log and show in "Copy error details"
};

template <typename T>
using PdfResult = std::expected<T, PdfError>; // Or equivalent C++20 result value type

struct PageSizePt { float width_pt; float height_pt; int rotation_deg; };

} // namespace recto::pdf

namespace recto::core {

struct DocumentId { uint64_t value; };

struct TileKey {
    DocumentId doc;
    int32_t page_index;
    int32_t zoom_bucket;   // Discrete bucket index, e.g. 100%, 125%, 150%, 200%
    int32_t tile_x;        // In 512x512 px tile units at zoom_bucket
    int32_t tile_y;
    uint16_t rotation_deg; // 0, 90, 180, 270
    uint8_t colour_mode;   // Original, Dimmed, Inverted (applied at composite time; key stays 0 unless baked)
};

// Reference-counted, immutable once recorded on the DocumentActor thread.
// Safe to read concurrently from any number of RenderPool worker threads.
class DisplayList;
class PageText;     // Immutable structured text + quad geometry for one page (native PDF text or OCR)
class CancelToken;  // Wraps std::atomic<bool> for cooperative render/job cancellation

} // namespace recto::core
```

---

## 4. `recto-pdf` Engine and Threading Contract

This is the most important technical section in this plan. Every contributor and every review checks engine changes against these rules.

### 4.1 What `recto-pdf` Implements (`src/pdf`)

1. **Lexer, Indirect Objects, and Cross-Reference Parser:**
   - Binary-safe PDF 1.0 to 2.0 tokenizer supporting comments, booleans, integers, reals, literal strings (nested balanced parentheses, octal escapes), hex strings, names (`#XX` escapes), arrays, dictionaries, streams, and indirect references (`N G R`).
   - Classic cross-reference (`xref`) tables, PDF 1.5+ compressed cross-reference streams (`/Type /XRef`, `/W` field widths, object streams `/Type /ObjStm`), hybridized XRef tables, linearized ("Fast Web View") documents, and multi-generation incremental updates (`/Prev` chain with cycle detection).
   - Damaged-file recovery scanner: if `startxref` or the XRef chain is corrupt or truncated, scans the file byte stream for `N G obj` headers and reconstructs the object index on the fly.
2. **Stream Filter Pipeline:**
   - Composable, bounded-memory decoding and encoding pipeline supporting chained filters (`/Filter` arrays with `/DecodeParms`):
     - `FlateDecode` (RFC 1950/1951 zlib/deflate) and `LZWDecode` (early/late code-size change) with TIFF Predictor 2 and PNG Predictors 10 to 15 (None, Sub, Up, Average, Paeth).
     - `ASCIIHexDecode`, `ASCII85Decode`, and `RunLengthDecode`.
     - `CCITTFaxDecode` (ITU-T T.4 Group 3 1D/2D and T.6 Group 4 monochrome facsimile decompression and encoding for scanned documents and multipage TIFFs).
     - `DCTDecode` (JPEG) and `JPXDecode` (JPEG 2000) decoded via Windows Imaging Component (WIC) into premultiplied BGRA or grayscale bitmaps.
   - Zip-bomb and decompression ratio guards: every stream decode enforces a configurable byte ceiling (default 256 MB per stream) and recursion limit (default 64 nested objects) so hostile inputs fail with `ErrorCode::DamagedFormat`, never an OOM crash.
3. **Standard Security Handler & X.509 / PAdES Digital Signatures (`bcrypt.dll` & `crypt32.dll`):**
   - Full support for PDF Standard Security Handler revisions `R=2, 3, 4` (`RC4` 40-bit to 128-bit and `AES-128` CBC with per-object MD5 key derivation) and `R=5, 6` (`AES-256` CBC with Algorithm 2.B SHA-256/SHA-384/SHA-512 iterative password validation and file encryption key unwrapping), password modification, and `/EncryptMetadata` handling.
   - **X.509 / PKCS#7 / CMS / PAdES Digital Signatures:** Sign PDFs (visible or invisible appearance stream, incremental-update signing preserving prior multi-signatures, RFC 3161 TSA timestamp token via `winhttp.dll`, `/DSS` Document Security Store for PAdES B-LT) using `.pfx`/`.p12` PKCS#12 files or the Windows Certificate Store, and verify existing signatures (byte-range digest, certificate trust chain, revocation, and post-signature incremental modifications) via `crypt32.dll` and `bcrypt.dll`.
4. **Page Tree, Optional Content Groups (Layers), Resources, and Content-Stream Interpreter & Operator Editor:**
   - Flattens `/Pages` hierarchy with cycle detection, resolving inherited `/MediaBox`, `/CropBox`, `/BleedBox`, `/TrimBox`, `/ArtBox`, `/Rotate`, and `/Resources`.
   - Parses Optional Content Groups (`/OCProperties`, `/OCGs`, `/D` default state, `BDC /OC ... EMC`) so layers can be toggled live or flattened.
   - Stack-based content-stream state machine and low-level operator inspector/editor executing PDF graphics operators:
     - Graphics state stack (`q`, `Q`, `cm`, `w`, `J`, `j`, `M`, `d`, `ri`, `i`, `gs` / `/ExtGState` with stroke/fill alpha `CA`/`ca` and blend modes).
     - Path construction and painting (`m`, `l`, `c`, `v`, `y`, `h`, `re`, `S`, `s`, `f`, `F`, `f*`, `B`, `B*`, `b`, `b*`, `n`) and clipping paths (`W`, `W*`), recording orthogonal vector ruling segments (`m`, `l`, `re`) for table reconstruction and detecting dense small curve subpaths for vector-outlined text OCR.
     - Colour spaces (`/DeviceGray`, `/DeviceRGB`, `/DeviceCMYK` with ICC/standard RGB conversion, `/Indexed`, `/Separation`, `/ICCBased` via WCS/Direct2D, and tiling/shading patterns).
     - Text state and positioning (`BT`, `ET`, `Tc`, `Tw`, `Tz`, `TL`, `Tf`, `Tr`, `Ts`, `Td`, `TD`, `Tm`, `T*`, `Tj`, `TJ`, `'`, `"`), plus content-stream text find-and-replace (literal and regex) with glyph width reflow.
     - Images and XObjects (`Do` for `/Subtype /Image`, inline images `BI`/`ID`/`EI`, recursive `/Subtype /Form` XObjects with depth bounds, and embedded image extraction/replacement/deletion).
5. **Font Loading, Subsetting, and Glyph Mapping:**
   - Maps PDF font descriptors (`/Subtype /TrueType`, `/Type1`, `/MMType1`, `/Type0` CIDFontType0/CIDFontType2, `/OpenType`, and `/Type3` procedural fonts) to DirectWrite (`IDWriteFontFace` via a custom in-memory `IDWriteFontFileLoader` for embedded font streams, CFF/Type 1 charstring-to-path outline conversion where needed, and system fallback fonts for the Standard 14 PDF fonts: Helvetica, Times, Courier, Symbol, ZapfDingbats).
   - Decodes character codes to Unicode via `/ToUnicode` CMap streams, predefined Adobe encodings (`WinAnsiEncoding`, `MacRomanEncoding`, `StandardEncoding`), and glyph names (`Adobe Glyph List`) so both glyph rendering and `PageText` extraction share exact character positions.
   - Subsets and embeds TrueType/OpenType fonts with generated `/ToUnicode` CMaps when writing new text, forms, annotations, watermarks, or PDF/A documents.
6. **Interactive Forms (AcroForms & XFA), Annotations, Bookmarks, Attachments & Portfolios:**
   - **AcroForms & XFA:** Parse, fill, create, modify, delete, and flatten interactive form fields (`Tx` text, `Btn` checkbox/radio/pushbutton, `Ch` combobox/listbox, `Sig` signature), synthesize `/AP` appearance streams so filled or flattened forms render identically across all viewers, convert XFA form datasets/templates into AcroForm fields, and import/export form data in `JSON`, `FDF`, `XFDF`, and `XML`.
   - **Annotations & Markup:** Parse, render, create, edit, reply to, delete, flatten, and import/export (`XFDF`/`JSON`) all standard annotation types (`Highlight`, `Underline`, `StrikeOut`, `Squiggly`, `Text` sticky note, `FreeText` box/callout, `Ink`, `Square`, `Circle`, `Line`/arrow, `Polygon`, `PolyLine`, `Stamp`, `Link`, `FileAttachment`, `Redact`).
   - **Bookmarks, Attachments & Portfolios:** Full CRUD over `/Outlines` bookmark hierarchies, `/Names /EmbeddedFiles` document attachments, and `/Collection` PDF Portfolios.
7. **True Redaction, Sanitization, Standards Compliance (PDF/A, PDF/UA, ZUGFeRD/Factur-X), Writer, Optimizer & Linearizer:**
   - **True Redaction & Sanitization:** Permanently strips glyphs, vector paths, and raster image pixels intersecting redaction boxes from page content streams and Form XObjects before painting the redaction appearance overlay; `Sanitize` removes all hidden metadata, XMP, embedded files, scripts (`/JS`/`/JavaScript`), `/Launch` and `/OpenAction` triggers, and incremental history.
   - **PDF/A, PDF/UA & ZUGFeRD/Factur-X:** Validates and converts documents to `PDF/A-1a/1b/2a/2b/2u/3a/3b/3u` (font embedding, sRGB/CMYK `/OutputIntents`, XMP `pdfaid` sync, transparency flattening for PDF/A-1), manages `/StructTreeRoot` tagged PDF structure and `/Alt` text for `PDF/UA-1` (ISO 14289), and reads/validates/embeds EN 16931 `factur-x.xml` / `zugferd-invoice.xml` electronic invoice payloads with `/AF` Associated Files dictionaries.
   - **PDF Writer, Graft Organizer, Optimizer & Linearizer:** Serializes clean PDF 1.7 / 2.0 documents (`PdfWriter`), incremental updates (for multi-signing), and **Linearized PDFs** (Fast Web View hint tables); `GraftMap` copies indirect object subgraphs across source documents while deduplicating shared resources; `PdfOptimizer` downsamples/recompresses images, subsets fonts, deduplicates streams by SHA-256, and strips unused objects.

### 4.2 Thread Ownership Rules

1. **`recto::pdf::Document` is single-owner per thread:** A parsed `pdf::Document` and its lazy object cache belong to the single `DocumentActor` thread (or isolated conversion job thread) that opened it. No mutex sits inside `pdf::Document`'s inner object parser.
2. **Cross-thread page representations are immutable:** The `DocumentActor` interprets a page's content stream once and produces an immutable, reference-counted `DisplayList` (and, when requested, an immutable `PageText` snapshot).
3. **`RenderPool` workers only read immutable `DisplayList`s:** Worker threads rasterize `DisplayList`s into 512x512 BGRA tile bitmaps (via per-worker Direct2D `ID2D1DeviceContext` / WIC bitmap render targets or software rasterizer) in parallel, checking `CancelToken` between command batches.
4. **Strict error values, zero exceptions across boundaries:** Malformed PDF objects, truncated streams, and bad passwords return `PdfResult<T>` with a descriptive `PdfError`. No parser failure ever throws an unhandled exception or terminates the process.

### 4.3 Runtime and Thread Map

Recto creates one `recto::core::Runtime` at startup and destroys it after all documents, views, and jobs have closed.

```text
+-------------------------------------------------------------------+
| UI Thread (WinUI 3 DispatcherQueue)                               |
|   Never calls recto-pdf. Never blocks on an engine thread.        |
|   Sends messages to DocumentActors; receives callbacks via queue. |
+-----------------+----------------------------------+--------------+
                  |                                  ^
                  | commands (open, load page,       | SwapChainPanel
                  | extract text, outline, search)   | presents
                  v                                  |
+---------------------------------+  +---------------+--------------+
| DocumentActor Thread (1 / doc)  |  | Presentation Thread (1/view) |
|   Owns pdf::Document            |  |   Owns D3D11/D2D context     |
|   Parses XRef, pages, outlines  |  |   Owns tile texture cache    |
|   Builds DisplayList & PageText |  |   Composites visible tiles   |
+-----------------+---------------+  +---------------+--------------+
                  |                                  ^
                  | Arc<const DisplayList>           | completed tiles
                  v                                  |
+----------------------------------------------------+--------------+
| RenderPool Worker Threads (N = clamp(cores - 2, 2, 8))            |
|   Each worker owns its own D2D/WIC raster target                  |
|   Renders 512x512 tiles from DisplayList with CancelToken         |
|   Runs OCR preprocessing + Windows.Media.Ocr off the UI thread    |
+-------------------------------------------------------------------+

+-------------------------------------------------------------------+
| Conversion Job Threads (bounded pool, max 2 concurrent jobs)      |
|   Each job opens its OWN pdf::Document (never shares viewer's)    |
+-------------------------------------------------------------------+
```

### 4.4 Opening a Document and Recording Display Lists

1. UI thread calls `Runtime::open(path)` -> spawns a `DocumentActor` thread for that tab and returns a lightweight `DocumentHandle`.
2. On the actor thread:
   - Opens the memory-mapped or buffered file stream and parses the header, trailer, and XRef table/stream (with damaged-file fallback if needed).
   - If the document is encrypted and the empty user password does not authenticate, posts `NeedsPassword` to the UI thread and waits on its actor queue for `ProvidePassword(utf8)` or `Cancel`.
   - Reads page count, page dimensions/rotations, metadata, security summary, signature summary, OCG layer list, embedded files/portfolio inventory, AcroForm field descriptors, font inventory, and outline tree into plain C++ structs and posts `DocumentOpened` to the UI thread.
3. Immediately after `DocumentOpened`, the UI can size the scroll canvas and draw page outlines (`design/SPEC.md` section 9.2: "show page outlines at the right size immediately and fill them as tiles arrive").
4. When the viewport needs page `p`, the `TileScheduler` asks the actor for `DisplayList(p)` (cached in an LRU of ~32 pages on the actor). The actor interprets page `p`'s content stream (plus active OCG layer visibility and appearance streams) into an immutable `DisplayList` and hands `std::shared_ptr<const DisplayList>` to the `RenderPool`.

### 4.5 Rendering Tiles and Zoom

1. Each `RenderPool` worker pops the highest-priority `TileRequest` from the `TileScheduler`:
   - Priority order: **(1)** visible tiles of the current page at current zoom bucket, **(2)** visible tiles of adjacent visible pages, **(3)** a 1-tile ring around the viewport (prefetch), **(4)** thumbnail strip requests.
   - Requests for tiles that scrolled out of the viewport or belong to an old zoom bucket have their `CancelToken` tripped before or during rendering.
2. The worker clears a `512 x 512` premultiplied BGRA target (white for normal pages; alpha preserved) and replays the `DisplayList` with the tile transform (zoom bucket scale + page rotation + tile offset), checking `CancelToken` periodically so a cancelled high-DPI tile aborts within a few milliseconds.
3. **GPU upload rule:** `ID3D11Device` is free-threaded; `ID3D11DeviceContext` is not. Workers create an immutable `ID3D11Texture2D` via `ID3D11Device::CreateTexture2D(&desc, &initialData, &tex)` (passing the rendered BGRA buffer and explicit row stride in `D3D11_SUBRESOURCE_DATA`) and post the texture handle to the presentation thread.
4. **Zooming without blanking (`design/SPEC.md` section 9.2 and section 12):** While the user zooms, the presentation thread immediately scales the nearest already-cached zoom bucket's tiles on the GPU, then swaps in sharp tiles from the new zoom bucket as workers finish them.

### 4.6 Presentation Thread (`SwapChainPanel`)

Each `DocumentView` owns one presentation thread:

- Owns the `IDXGISwapChain1` attached to the `SwapChainPanel` via `ISwapChainPanelNative::SetSwapChain`, plus its own `ID3D11DeviceContext` and `ID2D1DeviceContext` (created from a `D2D1_FACTORY_TYPE_MULTI_THREADED` factory).
- Owns the **tile texture cache** (`TileKey -> ComPtr<ID3D11Texture2D>`), budgeted by byte size (`Settings -> Performance and privacy -> Page cache`, default **512 MB**, range 128 MB to 2 GB per `design/SPEC.md` section 9.7), with LRU eviction weighted by distance from the current viewport.
- Applies dark-theme page colour modes (`Original`, `Dimmed` towards `#D9D5CE`, `Inverted` with image preservation) in the presentation shader/blend stage so switching page colour modes re-renders zero tiles.
- UI thread never touches the D3D11/D2D context; it only posts viewport state (`scroll_offset`, `zoom`, `viewport_size`, `dpi_scale`, `page_colour_mode`) to the presentation thread.

### 4.7 Conversion, Authoring, and OCR Jobs

A conversion, batch OCR, optimization, redaction, signing, or compliance job **never** shares a `pdf::Document` with an open viewer tab. Each job runs on a job thread and opens its own `pdf::Document` from disk (prompting for a password in the queue UI if needed, per `design/SPEC.md` section 9.5). This guarantees that:

- A slow conversion, OCR pass, or optimization never stalls scrolling or tile rendering in the viewer.
- Page-tree and content-stream mutations (merge, split, rotate, delete, reorder, crop, watermark, Bates, redact, sanitize, sign, encrypt, OCR layer injection, PDF/A conversion) cannot corrupt the viewer's open document.

### 4.8 Shutdown Order

`Runtime::~Runtime()` enforces a strict teardown order:

1. Trip all active `CancelToken`s.
2. Join all `DocumentActor` threads (each actor destroys its `pdf::Document` and drops its `DisplayList` cache).
3. Join all `ConversionJob` threads.
4. Drain and join all `RenderPool` worker threads (every `shared_ptr<const DisplayList>` is now released).
5. Join all `PresentationThread`s.
6. Assert in Debug builds that live `Document`, `DisplayList`, and worker-context counters equal zero.

### 4.9 Thread-Safety Verification Battery

These checks are part of **M1's definition of done**, not deferred to M6:

- **Determinism test:** Render every page of 10 corpus PDFs with 1 worker thread and with 8 worker threads shuffling tile order; assert byte-for-byte identical RGBA output for every page.
- **Cancellation storm test:** Open a 200-page PDF, issue 1,000 random viewport jumps and zoom changes across 8 workers while tripping `CancelToken`s every 5 ms for 30 seconds under **AddressSanitizer**; assert zero crashes, zero leaks, and clean shutdown.
- **Multi-document stress test:** Open and close 20 documents concurrently on separate `DocumentActor` threads while rendering tiles and extracting text; assert the live-object counters return to zero on close.

---

## 5. Conversions, Built-in OCR, Authoring, Security, and Standards Compliance

Every operation runs as a cancellable job on `JobQueue` (`src/services`), reports per-page progress, and writes output via **write-to-temp-file-then-atomic-replace** (`ReplaceFileW` / `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)`) in the target directory so a cancelled or failed job never leaves a half-written file or clobbers the user's original.

| Operation | Milestone | Mechanism | Notes |
| --------- | --------- | --------- | ----- |
| **PDF -> Images, Multipage TIFF, Animated GIF, SVG & XPS** | M4 | `recto-pdf` `DisplayList` rasterization at 72 to 1200 DPI -> WIC encoders (`PNG`, `JPEG`, `BMP`, `GIF`/multi-frame `Animated GIF`, single & **Multipage `TIFF`** with `LZW`/`CCITT Group 4`/`JPEG`) + vector `DisplayList` serializer for `.svg` and OpenXPS `.xps` | Streamed page-by-page so a 500-page export at 600 DPI holds only one page bitmap in RAM. |
| **Images, SVG & XPS -> PDF** | M4 | WIC decode (preserving original JPEG stream bytes for lossless `DCTDecode` embedding, CCITT Group 4 for 1-bit TIFFs, `FlateDecode` with alpha `/SMask` for PNG) + SVG/XPS vector parser -> `PdfWriter` | Preserves EXIF orientation (`IWICMetadataQueryReader`); supports multi-frame TIFF input and fit-to-A4/Letter. |
| **Extract Embedded Images & Tables** | M4 / M5 | Direct XObject image extractor (`src/core/image_extractor.cpp` with min-size filter) + Orthogonal-Line & Aligned-Grid `TableExtractor` (`src/core/table_extractor.cpp`) -> `.xlsx`, `.csv`, `.xml`, `.png`, `.jpg`, `.svg` | Works both as a batch Convert card and interactively in the Viewer via the **Table & Image Extractor Bar** (`design/SPEC.md` section 8.2). |
| **PDF -> Plain text (`.txt`) & Find/Replace (`Ctrl+H`)** | M3 / M4 | `recto-pdf` `PageText` extractor + automatic `OcrPipeline` fallback on scanned pages; content-stream literal & **Regex** Find-and-Replace (`src/pdf/text_replace.cpp`) | Find & Replace updates glyph runs and font subsets in a copy via `PdfWriter`. |
| **Searchable PDF (OCR + Vector-Outlined Text OCR)** | M3 / M4 | `OcrPipeline` (`src/core`): renders scanned page or **vector-curve-outlined text regions** at 300 DPI -> C++20 image preprocessing (deskew, contrast, Sauvola/Otsu binarization) -> `Windows.Media.Ocr` -> `PdfWriter` injects invisible text (`3 Tr`) | Works offline using installed Windows 11 language packs. Feeds live viewer `PageText` in M3 and converts vector-outlined curves into selectable/convertible text. |
| **Merge, Split, Reorder, Rotate, Delete, Extract & Insert Blank** | M4 | `PdfWriter` + one `GraftMap` per source document (`src/pdf/graft.cpp`, `src/core/page_ops.cpp`) | Preserves shared font/image resources without re-rendering; copies or generates outlines; supports splitting by ranges, every N pages, per page, or by top-level bookmark. |
| **Crop, Resize Page Boxes, Watermarks, Headers/Footers & Bates Numbering** | M4 | `src/core/page_ops.cpp` & `src/core/stamp_watermark.cpp`: edits `/MediaBox`/`/CropBox`/`/BleedBox`/`/TrimBox`, scales content to target paper sizes, and appends calibrated Form XObject overlays for Headers/Footers (`{Page}`, `{TotalPages}`, `{Date}`), sequential **Bates Numbering**, and rotated/translucent text/image/PDF **Watermarks** | Backed by live-preview dialogs in Organize and Viewer (`design/SPEC.md` sections 9.6 and 9.9). |
| **Encrypt / Decrypt (`AES-256`) & X.509 / PAdES Digital Signatures** | M4 | Standard Security Handler `R=6` (`AES-256` via `bcrypt.dll`), plus X.509 / PKCS#7 / CMS / PAdES signing and verification (`src/pdf/signature.cpp` via `crypt32.dll`, `bcrypt.dll`, and `winhttp.dll` for RFC 3161 TSA timestamps) | Supports `.pfx`/`.p12` and Windows Certificate Store, visible drawn/typed/image signature boxes, multi-signature incremental updates, LTV (`/DSS`), and the Viewer **Signature Banner & Panel**. |
| **Interactive Forms (AcroForms & XFA) & Annotations / Markup** | M3 / M4 | `src/pdf/forms.cpp` (AcroForm field CRUD, `/AP` appearance synthesis, XFA-to-AcroForm conversion, `JSON`/`FDF`/`XFDF`/`XML` data import/export, form flattening) and `src/pdf/annotations.cpp` (15 annotation types, reply threads, `XFDF`/`JSON` exchange, flattening) | Backed by on-canvas interactive form filling, the **Form Designer Bar**, the **Markup & Annotation Bar**, and the **Comments Side Panel** (`design/SPEC.md` sections 8.2, 8.3). |
| **True Redaction, Sanitization, Layers (OCG), Attachments, Portfolios & Comparison** | M4 | `src/pdf/redactor.cpp` (strips glyphs, vector paths, and raster pixels inside redaction rects + `Sanitize` scrubber), `src/pdf/ocg.cpp` (layer visibility & flattening), `src/pdf/portfolio.cpp` (`/EmbeddedFiles` & `/Collection`), and `src/core/pdf_compare.cpp` (visual + semantic text diff) | Backed by the **Redaction & Sanitize Bar**, **Layers Panel**, **Attachments & Portfolio Panel**, **Editable Outline & Info/XMP Panels**, and **Compare PDFs View** (`design/SPEC.md` sections 8.2, 8.3, 9.2, 9.3.1). |
| **PDF -> Word (`.docx` & `.rtf` in 3 Modes)** | M5 | Native C++20 `DocxWriter` & `RtfWriter` (`src/core/docx_writer.cpp`, `src/core/rtf_writer.cpp`) supporting **Flowing** (paragraphs, headings, lists, multi-column sections), **Exact** (positioned frames), and **Continuous** reconstruction modes + orthogonal-line & aligned-grid table builder | Builds standards-compliant ECMA-376 `.docx` and `.rtf` files with embedded/mapped fonts, images, merged-cell tables, and OCR fallback. |
| **PDF -> Excel (`.xlsx`, `.csv`, `.xml`) & Web/Docs (`.html`, `.md`, `.epub`, `.tex`)** | M5 | Native C++20 `XlsxWriter` (`src/core/xlsx_writer.cpp` emitting OpenXML SpreadsheetML `.xlsx`, `.csv`, `.xml` from `TableExtractor`) and `WebDocWriter` (`src/core/web_doc_writer.cpp` emitting Fixed/Flowing `HTML5+CSS3`, `Markdown`, `EPUB 3`, and `LaTeX`) | Reconstructs multi-page tables with typed numbers/dates/merged cells for Excel, and semantic or exact-positioned HTML5/EPUB/Markdown/LaTeX. |
| **Office, HTML, Markdown, XPS & Text -> PDF** | M5 | **Path A (built-in default):** Native C++20 OpenXML (`.docx`, `.xlsx`, `.pptx`), `.rtf`, `.csv`, `.html`/CSS, `.md`, `.xps`, `.svg`, and `.txt`/`.xml` parser + DirectWrite/Direct2D page layout -> `PdfWriter`. **Path B:** Microsoft Office COM (`AutomationSecurity = msoAutomationSecurityForceDisable`). **Path C:** LibreOffice headless | Works out of the box on a clean Windows 11 machine with zero external installs for all modern Office, web, and markup formats. |
| **PDF/A (`1a..3u`), PDF/UA Accessibility, ZUGFeRD / Factur-X & Optimization / Linearization** | M5 | `src/pdf/compliance.cpp` (PDF/A `1a/1b/2a/2b/2u/3a/3b/3u` converter & validator, `/StructTreeRoot` PDF/UA auto-tagger & checker, ZUGFeRD / Factur-X EN 16931 XML attachment & validator) and `src/core/pdf_optimizer.cpp` (image downsampling, font subsetting, stream deduplication, Fast Web View linearization) | Backed by the **Standards & E-Invoice Side Panel**, **PDF/A & E-Invoice Convert Card**, and **Optimize & Compress PDF Card / Dialog** (`design/SPEC.md` sections 8.3, 9.5, 9.9). |

### 5.1 Office, HTML & Markup -> PDF Security and Converter Rules

- **Built-in OpenXML / RTF / HTML / Markdown / XPS / SVG Engine (always available):**
  - Parses `.docx` (paragraphs, runs, tables, lists, headers/footers, inline images, page setup), `.xlsx`/`.csv` (shared strings, worksheets, cell formatting, column widths, gridlines), `.pptx` (slides, master shapes, text frames, images), `.rtf`, `.html`/CSS (semantic blocks, tables, inline/stylesheet CSS rules, images, with `<script>` tags and external network fetches strictly disabled), `.md` (CommonMark + GFM tables/code blocks), `.xps`, and `.svg` into Recto's layout primitives, shapes text with DirectWrite, and emits vector PDF pages via `PdfWriter`.
  - Enforces zip-bomb and XML entity expansion limits on every OpenXML and XPS container.
- **Microsoft Office COM Automation (optional high-fidelity path for `.docx`/`.doc`, `.xlsx`/`.xls`, `.pptx`/`.ppt`):**
  - Runs on a dedicated STA worker thread in `recto-services`.
  - Sets `Application.Visible = VARIANT_FALSE`, `Application.DisplayAlerts = 0`, and **`Application.AutomationSecurity = msoAutomationSecurityForceDisable` (3)** so macros in untrusted Office files **never** execute during conversion.
  - Opens read-only (`ReadOnly = VARIANT_TRUE`, `AddToRecentFiles = VARIANT_FALSE`), exports to PDF, closes without saving, and calls `Quit()` on the instance Recto spawned, enforced by a per-job timeout.
- **LibreOffice Headless (optional fallback path):**
  - Invokes `soffice.exe --headless --norestore --nolockcheck -env:UserInstallation=<isolated-temp-profile-url> --convert-to pdf --outdir <temp>` with `CreateProcessW` (no `cmd.exe` shell interpolation) and a job object (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`) so cancelling kills child processes cleanly.

---

## 6. Interface and Accessibility

`design/SPEC.md` is the authoritative UI specification. Key engineering points that affect `src/services` and `src/app`:

### 6.1 Tokens and Assets

- Merge `design/tokens/RectoColors.xaml` into `App.xaml`. No XAML or C++ file under `src/app` may hard-code a hex colour.
- Copy `resources/msix/Assets/` into the app project and wire `Package.appxmanifest` exactly as documented in `resources/README.md` (including the `.pdf` `uap:FileTypeAssociation` pointing to `Assets\FileTypes\PdfFile.png` and splash screen background `#1B1A19`).
- Load in-app SVGs (`wordmark-*.svg`, `layout-*.svg`, `drop-files-*.svg`, `ribbon-marker*.svg`) via `SvgImageSource` switched by theme dictionaries.

### 6.2 One Shell, Five Settings (`design/SPEC.md` section 8)

`ShellPage` declares the eight named slots (`TitleBarSlot`, `TopBarSlot`, `BottomOverlaySlot`, `LeftDockSlot`, `RightDockSlot`, `LeftOverlaySlot`, `RightOverlaySlot`, `BottomDockSlot`). The same `PageToolbar`, contextual mode bars (`AnnotationBar`, `FormDesignerBar`, `RedactionBar`, `TableExtractorBar`), `SidePanel` (switching between `Pages`, `Outline`, `Comments`, `Layers`, `Attachments`, and `Signatures & Standards`), `ThumbnailList`, and `FindCard` instances are reparented between slots when `LayoutSettings` changes. Switching presets must never recreate `DocumentView` or lose scroll, zoom, selection, form, or annotation state.

### 6.3 Text Selection, Search, Find & Replace, Built-in OCR, Forms, Structure Tree, and UI Automation (`design/SPEC.md` sections 8.2, 8.3, 9.3, 11)

- `DocumentActor` extracts a `PageText` structure per page (`Blocks -> Lines -> Spans -> Chars` with quad bounding boxes in page points).
- When a page has no extractable text (`chars.empty()`) and contains page-covering raster images or dense vector-outlined text curves, `DocumentActor` can transparently run `OcrPipeline` (`Windows.Media.Ocr`) on the page so `PageText` is populated with recognized words and quads.
- The **same** `PageText` instance (combined with `/StructTreeRoot` tags and AcroForm field descriptors when present) drives:
  1. Pointer text selection + floating selection toolbar (`Copy`, `Highlight`, `Strikeout`, `Redact`, `Find`, `Link`).
  2. `Ctrl+F` / `Ctrl+H` search and replace (`Match case`, `Whole word`, `Regex`) highlights (`RectoSearchHitBrush`, `RectoSearchHitCurrentBrush`) and result snippets.
  3. Interactive table region extraction (`TableExtractor`) and side-by-side / overlay document comparison (`PdfCompare`).
  4. A custom `AutomationPeer` implementing `ITextProvider` / `ITextRangeProvider` and exposing interactive form controls and `/StructTreeRoot` semantic headings/tables/alt-text so Narrator and NVDA can read, navigate, fill forms, and select page text on both digital and scanned PDFs.

---

## 7. Milestones

Each milestone ends with a runnable build and an explicit "done when" check.

| Milestone | Scope | Done when |
| --------- | ----- | --------- |
| **M0: Repository & Build** | Git repo, `LICENSE` (`GPL-3.0-or-later`), `NOTICE.md`, `CLA.md`, `Recto.sln` (`recto-pdf`, `recto-core`, `recto-services`, `Recto`, `recto-cli`, `recto-tests`, `recto-fuzz`), GoogleTest + initial test corpus, GitHub Actions CI building `x64` and `ARM64` + ASan job, trademark check and Store name reservation. | A clean clone builds `x64` and `ARM64` in one command and CI is green. |
| **M1: Engine Core (`recto-pdf` & `recto-core`)** | `recto-pdf` lexer, XRef tables/streams, damaged-file recovery, stream filters, CNG `bcrypt.dll` RC4/AES-128/AES-256 decryption, page tree & OCG layers, content-stream interpreter & operator editor, DirectWrite font loader & subsetter, `DisplayList` and `PageText` builder; `DocumentActor`, `RenderPool`, `TileScheduler` with cancellation; `recto-cli info\|render\|bench\|stress`; initial `fuzz_open`, `fuzz_filters` and `fuzz_render` targets. | All three tests in the section 4.9 Thread-Safety Verification Battery pass under ASan, and `fuzz_open` runs 1 hour clean on the seed corpus. |
| **M2: Viewer & Rich Panels** | `ShellPage` with title-bar tabs (`TabView`), `RectoColors.xaml`, all three layout presets (Focus, Classic, Reading room), `DocumentView` (`SwapChainPanel` + presentation thread + tile cache + stretched-tile zoom + continuous/single/two-page layouts + rotation + dark page modes), Home with Recent files and remembered reading position, Pages (thumbnails), Outline (with interactive Bookmark Editor), Layers (OCG) panel, Attachments & Portfolio panel, editable Info/XMP/ViewerPreferences panel, Command box (`Ctrl+K`), Settings (`design/SPEC.md` section 9.7, including **Portable mode** setting with live `<exe-dir>\data\` migration), password & error dialogs, link prompt dialog, drag-and-drop & file activation. | Can open, read, zoom, rotate, toggle OCG layers, inspect/extract attachments and portfolios, edit bookmarks/metadata, toggle Portable mode, and switch presets on a 500-page PDF in light, dark, and high-contrast themes at the performance targets in section 8. |
| **M3: Text, OCR, Annotations, Forms & Accessibility** | `PageText` integration, text selection + floating selection toolbar + context menu, `Ctrl+F` / `Ctrl+H` Find & Replace card (with `Regex`) and Reading room Search inspector tab, built-in `Windows.Media.Ocr` pipeline (for scanned pages and vector-outlined text curves), interactive **Annotations & Markup Bar + Comments Panel**, interactive **AcroForms / XFA Filling + Form Designer Bar** (`JSON`/`XFDF`/`FDF`/`XML` data exchange & flattening), **Compare PDFs View** (`Ctrl+Shift+D`), UI Automation `ITextProvider` + structure/form accessibility, full keyboard navigation audit (`design/SPEC.md` section 10) and screen-reader announcements. | Search & Replace, scanned/vector-outlined OCR, interactive form filling/design, all 15 annotation types, and side-by-side/overlay PDF comparison work smoothly without blocking scrolling; Narrator can read page text and form fields; every toolbar/menu command is reachable from `Ctrl+K`. |
| **M4: Conversions A, Organize, Redaction & Signatures** | `JobQueue` + bottom-right queue card + Windows toast / in-app toast notifications; Convert 3-step flow (`design/SPEC.md` section 9.5) for PDF <-> Images (PNG, JPEG, BMP, GIF/Animated GIF, single & Multipage TIFF, SVG, XPS), PDF -> Text, Searchable PDF (OCR), Extract Tables & Images (with interactive Viewer **Table & Image Extractor Bar**), Optimize & Compress PDF (with **Linearization / Fast Web View**), One combined PDF, Split files, Protected & Signed PDF (`AES-256` + **X.509 / PKCS#12 / Windows Cert Store / PAdES / RFC 3161 Digital Signatures** + Viewer **Signature Banner & Panel**), Unlocked PDF; **Redaction & Sanitize Bar** (true content-stream redaction burning & document sanitization); Organize page (`design/SPEC.md` section 9.6) with Combine, Split, Reorder, **Insert Blank Page, Crop & Resize Page Boxes, Watermark, Header/Footer & Bates Numbering** live-preview dialogs, undo, and save guards. | Each conversion, extraction, optimization, signature verification/signing, true redaction, and organize operation round-trips the test corpus with no errors; cancelling a job mid-way leaves no partial output files. |
| **M5: Conversions B (Word, Excel, Web/Docs, Office/HTML to PDF, PDF/A, PDF/UA & ZUGFeRD)** | Native C++20 3-mode `DocxWriter` & `RtfWriter` (`PDF -> Word .docx/.rtf` in **Flowing, Exact, and Continuous** modes + orthogonal-line & grid table reconstruction) with 20-document quality review; `XlsxWriter` (`PDF -> .xlsx/.csv/.xml`); `WebDocWriter` (`PDF -> HTML5` Fixed/Flowing, `.md`, `.epub`, `.tex`); built-in C++20 OpenXML (`.docx`, `.xlsx`, `.pptx`), `.rtf`, `.html`/CSS, `.md`, `.xps`, `.svg` -> PDF engine plus optional Microsoft Office COM (`AutomationSecurity = ForceDisable`) and LibreOffice headless adapters; **PDF/A (`1a..3u`), PDF/UA Accessibility, and ZUGFeRD / Factur-X E-Invoice** converter, validator, and Viewer **Standards & E-Invoice Inspector Panel**. | 20-document PDF -> Word review meets the quality bar across Flowing/Exact/Continuous modes; PDF -> Excel/HTML/Markdown/EPUB/LaTeX and Office/HTML/Markdown -> PDF convert cleanly out of the box; PDF/A-1..3, PDF/UA, and ZUGFeRD/Factur-X invoices validate and round-trip cleanly; macro-bearing `.docm` is proven not to execute its macro. |
| **M6: Polish, Printing, Inno Setup & Portable Release (+ Optional Store MSIX)** | Printing (`Ctrl+P` via the Windows print dialog using Direct2D print control / `PrintManagerInterop`), About screen (`design/SPEC.md` section 9.8) with full third-party notices and source link, opt-in local minidump-plus-log save-and-share crash bundle, localization-ready `.resw` strings, Inno Setup installer (`installer/recto.iss` for `x64` and `ARM64` supporting both **Full Install** and **Self-Contained Portable** modes) plus standalone Portable `.zip` archive, automated silent Full Install / Portable Install / upgrade / uninstall verification, and optional MSIX packaging & Microsoft Store submission kit with custom `GPL-3.0-or-later` license terms. | Automated silent Inno Setup Full Install, Self-Contained Portable Install (`<exe-dir>\data\`), upgrade, and clean-uninstall tests pass, `.pdf` file association registers Recto's icon in Full Install mode, and the release bundle + optional Store MSIX submission kit are complete. |

---

## 8. Performance Targets

Measured in Release builds on the primary reference Windows 11 host machine (`11th Gen Intel Core i5-11600K`, 6C/12T, 32 GB RAM, NVIDIA GeForce RTX 2060, NVMe SSD, Windows 11 Build `26300` at `3840 x 2160` `144 DPI`, recorded in [`docs/reference-machines.md`](../reference-machines.md) in `D00 T01 §5`; `ARM64` is compile-and-link verified only, and the `ARM64` column in [`docs/reference-machines.md`](../reference-machines.md) is a non-binding target profile until `D99 T01 §4` names a physical device):

| Metric | Target |
| ------ | ------ |
| Cold launch to interactive Home window | **< 400 ms** |
| Open a 100-page text PDF to first visible page rendered | **< 300 ms** |
| Scroll frame time with cached tiles | **<= 16.6 ms (60 fps)** at p95; **0 blank frames** at normal reading scroll speed |
| Zoom change | Stretched preview in the **same frame (< 16 ms)**; sharp tiles replace within **150 ms** |
| Working set with one 500-page PDF open (default 512 MB tile cache cap) | **< 600 MB** |

---

## 9. Security and Untrusted Input

Treat every PDF, Office document, HTML file, and portfolio attachment as hostile input:

1. **No PDF or HTML JavaScript:** `recto-pdf` and the HTML-to-PDF parser do not contain a JavaScript engine (`D12`). `/JS` and `/JavaScript` actions in PDF catalogs, annotations, and form fields (and `<script>` tags in HTML inputs) are never executed, and one-click **Sanitize Document** strips them completely.
2. **Bounded parsing and decoding:** Every `recto-pdf` recursive parser, `/Prev` XRef chain, `/Form` XObject nesting, stream decompression filter, and OpenXML/XPS ZIP container enforces explicit depth and byte caps so cyclic references and decompression bombs fail cleanly with `ErrorCode::DamagedFormat`.
3. **True Redaction (no black-rectangle coverups):** Applying redactions in `recto-pdf` permanently removes underlying glyph runs, vector paths, and raster image pixels intersecting the redaction region from the content stream and XObjects before drawing the redaction box, and scrubs document metadata.
4. **No Office macros:** COM conversion sets `AutomationSecurity = msoAutomationSecurityForceDisable`. Built-in OpenXML conversion ignores `vbaProject.bin` macro streams completely.
5. **No silent external actions:** Clicking a web link, file link, or embedded portfolio/attachment file in a PDF opens the confirmation dialog (`design/SPEC.md` section 9.9) showing the full URL or filename in monospace unless the user explicitly turns off "Ask before opening links" in Settings. PDF `Launch` actions (`/S /Launch`) are **never** executed, even if that setting is off.
6. **Safe subprocess execution:** When invoking `soffice.exe`, use `CreateProcessW` with an explicit application path and argument formatting, never `cmd.exe /c` or `system()`.
7. **Fuzzing:** `fuzz_open`, `fuzz_filters`, `fuzz_render`, `fuzz_text`, `fuzz_forms`, and `fuzz_openxml_zip` run under ASan + libFuzzer; any crash becomes a regression test in `tests/corpus/broken/`.
8. **Post-v1 sandboxing path (D13, pre-approved 2026-10-14 for `D06 T02 §6`):** Because `recto-pdf` is a pure C++20 library with no UI dependencies, a post-v1 release can move `recto-pdf` into a restricted AppContainer worker process (`lpacNoigher` / no network / no filesystem access, fed file handles via broker IPC and returning `DisplayList`, `PageText`, and `Bitmap` tiles over shared memory) without touching `src/app`.

---

## 10. `GPL-3.0-or-later` and Third-Party Compliance Checklist

Must all be true before the M6 release (and any optional Microsoft Store submission):

- [ ] Root `LICENSE` is the unmodified `GPL-3.0` text; `About` (`design/SPEC.md` section 9.8) and the Inno Setup installer license page display "GNU General Public License, version 3 or later" and open the license text.
- [ ] `About -> View source` links to the public GitHub repository at the exact git tag corresponding to the shipped installer / portable / MSIX version (`vX.Y.Z`).
- [ ] `NOTICE.md` and `About -> Built with` list every shipped component and its license (`recto-pdf` under `GPL-3.0-or-later`, Windows App SDK under MIT, Newsreader wordmark outlines under SIL OFL 1.1).
- [ ] Every linked library is verified compatible with `GPL-3.0-or-later` (no proprietary or GPL-incompatible static libraries).
- [ ] Optional Microsoft Store submission kit (`docs/release-and-store-runbook.md`) provides Recto's own `GPL-3.0-or-later` license terms for Partner Center rather than the default Microsoft Store Standard Application License Terms (which conflict with GPLv3 section 7).
- [ ] `CLA.md` is in the repo before any external pull request is merged.

---

## 11. Open Items to Resolve During Implementation

1. **Trademark & optional Store name reservation** for "Recto" (owner-only manual action in `D99 T01 §1` and `§3`).
2. **Reference benchmark machine (primary `x64` host resolved 2026-10-14 in `D00 T01 §5`; physical `ARM64` device deferred to `D99 T01 §4`):** The primary development host machine (`11th Gen Intel Core i5-11600K`, 32 GB RAM, RTX 2060, NVMe SSD, Windows 11 Build `26300`) and the non-binding `ARM64` target profile are recorded in [`docs/reference-machines.md`](../reference-machines.md), with naming a specific physical `ARM64` benchmark device deferred to `D99 T01 §4`.
3. **20-document PDF -> Word test corpus** assembled before M5 (`D04 T03 §6`) to evaluate `DocxWriter` layout and table reconstruction quality.
4. **WinUI 3 printing API verification** at the start of M6 (`D07 T01 §1`: confirm `PrintManagerInterop::GetForWindow` + `ID2D1PrintControl` vs WinRT print document source behavior on Windows App SDK 2.4.0).
5. **Opt-in crash reporting & Post-v1 broker architecture (resolved 2026-10-14):** `D07 T02 §4` uses a local minidump-plus-log save-and-share `.zip` bundle (zero network telemetry), and `D06 T02 §6` pre-approves the D13 shared-memory `DisplayList`/`PageText`/`Bitmap` AppContainer broker architecture.
