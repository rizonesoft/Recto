# Recto: foundation plan

Status: draft for implementation. Written 2026-10-13. Review again at the end of milestone M1, when the engine core has run under load.

Recto is an open-source PDF viewer and converter for Windows, written in C and C++ with a WinUI 3 interface. This plan records every decision made so far, the architecture, the MuPDF thread-safety setup, and the milestones to version 1.

## 1. Decisions

| # | Decision | Choice | Reversal cost |
|---|---|---|---|
| D1 | Product name | **Recto** (the right-hand page of an open book) | Low before the Store listing, high after. Run a USPTO/EUIPO trademark search before reserving the Store name. |
| D2 | License | **AGPL-3.0-or-later**, open source from the first public commit | High. Relicensing later needs consent from every contributor unless a CLA is in place (see D11). |
| D3 | PDF engine | **MuPDF** (AGPL), pinned to a release tag | Very high. The engine shapes the bridge, the threading model and every converter. |
| D4 | UI | **WinUI 3** on Windows App SDK **2.4.0** (latest stable, released 2026-08-13), C++/WinRT | High. |
| D5 | Languages | C17 for the MuPDF bridge, C++20 for everything else, intrinsics for hot loops, MASM only where profiling proves a gain | Low per module. |
| D6 | Distribution | **MSIX through the Microsoft Store**, with the AGPL given as the app's own license terms in Partner Center | Medium. |
| D7 | v1 conversions | PDF to images, images to PDF, PDF to text, merge, split, Office to PDF, PDF to Word (DOCX) | Per feature. |
| D8 | Target OS and CPUs | Windows 11, x64 and ARM64 | Low. Adding Windows 10 later only needs testing, not design changes. |
| D9 | MuPDF integration | Git submodule built from source with Recto's own MuPDF config, not the vcpkg port and not the generated C++ bindings (`mupdfcpp`) | Medium. See section 4.1 for why. |
| D10 | Office to PDF source | Use Microsoft Office through COM when installed, else an installed LibreOffice, else tell the user what to install. Recto does not bundle LibreOffice. | Low. Bundling LibreOffice later adds several hundred MB and a download flow, but no core change. |
| D11 | Contributor terms | Contributor License Agreement (CLA) required from the first outside contribution | Low now, impossible to add retroactively for past contributions. |
| D12 | PDF JavaScript | MuPDF built with JavaScript **disabled** (`FZ_ENABLE_JS 0`) | Low. Forms that rely on JS calculations will not compute until it is enabled. |
| D13 | Process isolation | v1 parses PDFs in-process. A sandboxed broker process is planned after v1 (section 9). | Medium. The bridge boundary in section 4 is designed so it can move behind IPC later. |
| D14 | Build system | Visual Studio with MSBuild for all projects, vcpkg manifest mode for test-only dependencies | Medium. WinUI 3 C++ templates are MSBuild and NuGet based. |
| D15 | Visual identity | Icon: a white page with a vermilion bookmark ribbon on a charcoal tile. Wordmark: Newsreader (SIL OFL 1.1). Interface text: Segoe UI Variable. | Low before the Store listing, medium after. |
| D16 | Colour | One accent, vermilion, in both themes: `#C8431F` on light surfaces, `#F0603C` on dark. Light theme uses cool Fluent neutrals; dark theme uses warm charcoal neutrals. Full tokens in section 6.1. | Low. Tokens live in one resource dictionary. |
| D17 | Layout | One app shell whose layout comes from independent settings (toolbar position, docked or floating panels, thumbnail placement, inspector side). Three named presets: **Focus** (default), **Classic** and **Reading room**. Not three separate shells. | Medium. Three separate shells would triple UI work for every feature. |

## 2. Tech stack

| Layer | Technology | Purpose |
|---|---|---|
| UI framework | WinUI 3, Windows App SDK 2.4.0, C++/WinRT | Window, controls, navigation, dialogs |
| Page surface | `SwapChainPanel`, Direct3D 11, Direct2D (multithreaded factory), DXGI flip-model swap chain | Tiled page display with smooth zoom and scroll |
| PDF engine | MuPDF (C library) | Parsing, rendering, text, search, merge, split, encryption, document writers |
| Image encode and decode | Windows Imaging Component (WIC), plus MuPDF's own image loaders | PNG, JPEG, TIFF, BMP output; image input for images to PDF |
| PDF to DOCX | MuPDF `docx` document writer (built on Artifex's `extract` library) | v1 Word export |
| Office to PDF | Office COM automation (`ExportAsFixedFormat`), LibreOffice headless (`soffice --convert-to pdf`) | v1 Office import |
| OCR (after v1) | `Windows.Media.Ocr`, with Tesseract as the option for more languages | Scanned documents |
| SIMD | AVX2 intrinsics (x64), NEON intrinsics (ARM64), scalar fallback | Pixel format conversion, downscaling |
| Unit tests | GoogleTest (via vcpkg) | Engine and services |
| Security testing | MSVC AddressSanitizer, libFuzzer | Bridge and parser inputs |
| CI | GitHub Actions, Windows runners, x64 and ARM64 builds | Build, test, fuzz smoke run, MSIX package |
| Packaging | MSIX, Microsoft Store | Install, update, `.pdf` file association |

## 3. Architecture

### 3.1 Modules

| Module | Type | Language | Depends on | Must not depend on |
|---|---|---|---|---|
| `mupdf` | static libs (`libmupdf`, `libthirdparty`) | C | nothing | everything else |
| `recto-bridge` | static lib | C17 | `mupdf` | C++, WinRT |
| `recto-core` | static lib | C++20 | `recto-bridge` | WinRT, XAML, MuPDF headers |
| `recto-services` | static lib | C++20 | `recto-core`, Win32, D3D11/D2D, WIC, COM | XAML |
| `Recto` | WinUI 3 packaged app | C++/WinRT | `recto-services` | `recto-bridge`, MuPDF headers |
| `recto-cli` | console exe | C++20 | `recto-core` | WinRT |
| `recto-tests` | GoogleTest exe | C++20 | `recto-core`, `recto-services` | |
| `recto-fuzz` | libFuzzer exe | C17/C++20 | `recto-bridge` | |

The rule behind the table: only `recto-bridge` includes MuPDF headers. Everything above it sees Recto's own types. This keeps `setjmp`/`longjmp` error handling out of C++ (section 4.4) and lets the bridge move into a separate sandbox process later (D13).

`recto-cli` exists so the engine can be tested, profiled and fuzz-reproduced without the UI: `recto-cli render in.pdf --page 3 --zoom 2 out.png`, `recto-cli convert in.pdf out.docx`.

### 3.2 Repository layout

```
Recto/
  Recto.sln
  vcpkg.json
  LICENSE                   AGPL-3.0 text
  NOTICE.md                 third-party licenses (MuPDF, Windows App SDK, etc.)
  CONTRIBUTING.md
  CLA.md
  third_party/mupdf/        git submodule, pinned release tag
  build/mupdf-config.h      Recto's MuPDF feature config (FZ_ENABLE_JS 0; all other upstream defaults kept, decided D00 T01 §2)
  build/mupdf-win32.targets MSBuild wiring excluding the MuJS unity file from the win32 build (D12, D00 T01 §2)
  src/bridge/               C only
  src/core/
  src/services/
  src/app/                  WinUI 3 project, Package.appxmanifest
  src/cli/
  tests/
  tests/corpus/             PDFs for regression tests
  fuzz/
  docs/plans/
  docs/specs/
```

### 3.3 Core types (`recto-core`)

| Type | Responsibility |
|---|---|
| `Runtime` | Process-wide singleton. Owns the lock array and the base `fz_context` through the bridge. Created at app start, destroyed last. |
| `DocumentHandle` | The UI's reference to one open document. Thin, copyable, posts requests to the document's actor thread. |
| `DocumentActor` | One thread per open document. Owns that document's `fz_context` clone, its `fz_document`, its `fz_page` objects and its display list cache. |
| `DisplayList` | Ref-counted, immutable, shareable wrapper over `fz_display_list`. The only page data that crosses threads. |
| `TileKey` | `(documentId, pageIndex, zoomBucket, rotation, tileX, tileY, renderFlags)` |
| `Tile` | Rendered BGRA premultiplied pixels for one `TileKey`. |
| `RenderPool` | Fixed worker threads, each with its own `fz_context` clone. Rasterizes `DisplayList` regions into `Tile`s. |
| `CancelToken` | Wraps an `fz_cookie`. Cancel sets `abort`, progress reads `progress` and `progress_max`. |
| `ConversionJob` | One conversion. Runs on its own thread with its own context clone and its own `fz_document` instance. |
| `PdfError` | C++ exception carrying the MuPDF error code and message, thrown only above the bridge. |

## 4. MuPDF thread-safety setup

MuPDF is safe to use from many threads only when the application follows its rules. This section is the contract every engine change must keep.

### 4.1 Why the C bridge and not `mupdfcpp`

MuPDF ships generated C++ bindings that turn errors into C++ exceptions and manage per-thread contexts automatically. Recto does not use them, for two reasons. First, generating them needs Python and libclang in the build, which adds a fragile step to every MuPDF update. Second, they hide which context a call runs on, and Recto's design depends on controlling exactly that: one owning thread per document, and render workers that never touch a document. A small hand-written C bridge gives that control.

### 4.2 MuPDF's rules, as Recto applies them

1. **Locks first.** The base context is created with a lock table (`fz_locks_context`) of `FZ_LOCK_MAX` locks. Without it, cloning a context is not allowed and nothing below is safe.
2. **One context per thread.** Every thread that calls MuPDF has its own `fz_context`, made with `fz_clone_context(base)` at thread start. Clones share the resource store (fonts, decoded images, glyph cache), protected by those locks.
3. **A document belongs to one thread.** An `fz_document` and every `fz_page` loaded from it are used only on that document's `DocumentActor` thread. No other thread calls any function on them.
4. **Display lists are the hand-off.** The actor builds an `fz_display_list` for a page. Once built, a display list is read-only and can be rendered by many threads at the same time, each with its own context. This is how rendering runs in parallel while the document stays single-threaded.
5. **Other objects move, but are never shared.** A pixmap, buffer or stext page made on one thread may be passed to another, provided only one thread uses it at a time.
6. **Reference counts are thread-safe; the objects are not.** `fz_keep_*` and `fz_drop_*` may run on any thread with that thread's context. This is what lets a worker drop a display list the actor created.
7. **Clones die before the base.** All clones are dropped, and all their threads joined, before the base context is dropped.

### 4.3 Lock implementation

Use Win32 `SRWLOCK`s. MuPDF never takes the same lock twice on one thread, so non-recursive slim locks are correct and cheaper than critical sections. The callbacks must never throw and never block on anything but the lock.

```c
/* src/bridge/runtime.c */
#include <windows.h>
#include <mupdf/fitz.h>

static SRWLOCK g_fz_locks[FZ_LOCK_MAX];

static void rc_lock(void *user, int lock)   { AcquireSRWLockExclusive(&((SRWLOCK *)user)[lock]); }
static void rc_unlock(void *user, int lock) { ReleaseSRWLockExclusive(&((SRWLOCK *)user)[lock]); }

static fz_locks_context g_locks = { g_fz_locks, rc_lock, rc_unlock };

fz_context *rc_runtime_create(size_t store_bytes)
{
    for (int i = 0; i < FZ_LOCK_MAX; i++)
        InitializeSRWLock(&g_fz_locks[i]);

    fz_context *base = fz_new_context(NULL, &g_locks, store_bytes);
    if (!base)
        return NULL;

    fz_try(base)
        fz_register_document_handlers(base);
    fz_catch(base)
    {
        fz_drop_context(base);
        return NULL;
    }
    return base;
}
```

The lock array and the `fz_locks_context` are static so they outlive every context. Start with `store_bytes` at 256 MB (`FZ_STORE_DEFAULT`) and tune in M2 against the tile cache budget.

### 4.4 Error handling across the C/C++ line

MuPDF reports errors with `fz_try`/`fz_always`/`fz_catch`, built on `setjmp`/`longjmp`. A `longjmp` that skips C++ frames with destructors is undefined behaviour under MSVC's default `/EHsc`. The bridge exists to make that impossible:

- `recto-bridge` is compiled as C (`/TC`). No C++ object can exist between an `fz_try` and the code that throws.
- Every bridge function catches every MuPDF error and returns a status: `typedef struct { int code; char message[256]; } rc_status;`. `recto-core` turns a non-zero status into a `PdfError` exception.
- Inside bridge code: never `return`, `break` or `goto` out of an `fz_try` block; put cleanup in `fz_always`; mark every local that is changed inside `fz_try` and read in `fz_catch` or after with `fz_var(x)`.
- Callbacks from MuPDF into Recto (locks, warnings, allocation) are plain C functions or `noexcept` C++ functions that never throw.
- Each context gets warning and error callbacks (`fz_set_warning_callback`, `fz_set_error_callback`) that write to Recto's log, tagged with the thread name.

### 4.5 Thread map

| Thread | Count | Context | Owns | Never does |
|---|---|---|---|---|
| UI (XAML dispatcher) | 1 | none | XAML tree, view models | Call the bridge, wait on any engine thread |
| Presentation | 1 per open document view | none | `SwapChainPanel` swap chain, D2D device context | Call the bridge |
| `DocumentActor` | 1 per open document | clone | `fz_document`, `fz_page`s, display list cache, stext cache | Rasterize tiles |
| Render worker | `max(2, cores - 2)`, shared by all documents | clone, made at thread start | the pixmap it is rendering | Touch an `fz_document` or `fz_page` |
| Conversion job | 1 per running job, at most 2 at once | clone | its own `fz_document` instance and writer | Share anything with the viewer's actor |
| Office bridge | 1 STA thread while an Office conversion runs | none | COM objects for Word, Excel, PowerPoint | Call the bridge |

### 4.6 Opening and viewing a document

1. The UI calls `Runtime::open(path)`. This starts a `DocumentActor` thread, which clones the base context and calls `fz_open_document`.
2. If `fz_needs_password` is true, the actor reports it; the UI asks for the password and posts it back for `fz_authenticate_password`.
3. The actor reports page count, page sizes and the outline (`fz_load_outline`) to the UI.
4. For each page the viewport needs, the actor loads the `fz_page`, builds an `fz_display_list` (`fz_new_display_list_from_page`, which includes annotations and form widgets), keeps it in an LRU cache of about 16 pages, and hands a kept reference to the render scheduler.
5. The scheduler queues tiles in priority order: visible tiles at the current zoom, then visible tiles at neighbouring zoom buckets, then the next and previous pages, then thumbnails.

### 4.7 Rendering one tile (render worker)

```c
/* src/bridge/render.c, runs on a render worker with its own ctx */
rc_status rc_render_tile(fz_context *ctx, fz_display_list *list,
                         fz_matrix page_ctm, fz_irect tile,
                         fz_cookie *cookie, fz_pixmap **out)
{
    rc_status st = {0};
    fz_pixmap *pix = NULL;
    fz_device *dev = NULL;
    fz_var(pix);
    fz_var(dev);

    fz_try(ctx)
    {
        /* BGRA with alpha: premultiplied, matches DXGI_FORMAT_B8G8R8A8_UNORM */
        pix = fz_new_pixmap_with_bbox(ctx, fz_device_bgr(ctx), tile, NULL, 1);
        fz_clear_pixmap_with_value(ctx, pix, 0xFF);           /* opaque white paper */
        dev = fz_new_draw_device(ctx, page_ctm, pix);
        fz_run_display_list(ctx, list, dev, page_ctm, fz_rect_from_irect(tile), cookie);
        fz_close_device(ctx, dev);
    }
    fz_always(ctx)
        fz_drop_device(ctx, dev);
    fz_catch(ctx)
    {
        fz_drop_pixmap(ctx, pix);
        pix = NULL;
        rc_status_from_error(ctx, &st);
    }
    *out = pix;
    return st;
}
```

Notes on the tile path:

- The pixmap's bbox is the tile's rectangle in device space, so the whole-page matrix (`fz_scale(zoom, zoom)` combined with rotation) is used unchanged. No per-tile translation is needed.
- Tiles are 512 by 512 device pixels.
- Zoom renders at discrete buckets. While the exact zoom renders, the presentation thread stretches the nearest cached bucket so the page never goes blank.
- When a tile scrolls out of view before it finishes, the scheduler sets `cookie->abort` (write it with `InterlockedExchange` on a `volatile long` mirror; MuPDF polls the field). If `cookie->incomplete` or `cookie->abort` is set when the run returns, the tile is discarded and never cached.
- After the run, the worker creates the GPU texture itself with `ID3D11Device::CreateTexture2D`, passing the pixmap samples as initial data (the D3D11 device is free-threaded; the immediate context is not). It then drops the pixmap and posts the texture to the presentation thread.
- `fz_pixmap` stride can be larger than `width * 4`; always pass `fz_pixmap_stride` as the row pitch.

### 4.8 Presentation thread

`SwapChainPanel` allows drawing from a background thread, so each document view has its own presentation thread. The UI thread only sends it viewport state (scroll offset, zoom, size, DPI scale). The presentation thread draws visible tiles with Direct2D (`D2D1_FACTORY_TYPE_MULTI_THREADED`) and presents. It is the only user of that view's `ID3D11DeviceContext` and `ID2D1DeviceContext`. The tile texture cache lives here, with an LRU budget of 512 MB to start.

### 4.9 Conversion jobs

A conversion never shares the viewer's document. Each `ConversionJob` thread clones the base context, opens its own `fz_document` from the file, runs, and drops everything before it exits. This costs one extra parse per job and removes every possible contention with viewing. Progress comes from the job's `fz_cookie` and is posted to the UI with `DispatcherQueue::TryEnqueue`.

### 4.10 Shutdown order

1. Cancel every cookie (render, conversion).
2. Stop the scheduler and join the render workers. Each drops its clone on the way out.
3. Join conversion jobs. Each drops its document, writer and clone.
4. Tell each `DocumentActor` to drop its display lists, pages and document, then its clone, and join it.
5. Release D3D and D2D resources on the presentation threads, then join them.
6. Drop the base context last.

A debug build asserts that the count of live clones is zero before step 6.

### 4.11 Thread-safety tests

| Test | Proves |
|---|---|
| 8 workers render the same display list 1,000 times; compare pixels to a single-threaded render | Display lists are safe to share; output is deterministic |
| Open 20 documents at once, scroll all of them, close in random order | Actor isolation and shutdown order |
| Cancel tiles at random points during rendering | Abort path leaks nothing and never caches partial tiles |
| Run conversion jobs while viewing the same file | Job isolation |
| All of the above under AddressSanitizer | No use-after-free across threads |
| Debug lock wrapper records lock order per thread and asserts on inversion | No new lock ordering bugs from Recto's own locks |

## 5. Conversions (v1)

All MuPDF conversions use the same document writer pattern on a `ConversionJob` thread:

```c
fz_document_writer *w = fz_new_document_writer(ctx, out_path, format, options);
for each page:
    fz_page *page = fz_load_page(ctx, doc, i);
    fz_device *dev = fz_begin_page(ctx, w, fz_bound_page(ctx, page));
    fz_run_page(ctx, page, dev, fz_identity, cookie);
    fz_end_page(ctx, w);
    fz_drop_page(ctx, page);
fz_close_document_writer(ctx, w);
fz_drop_document_writer(ctx, w);
```

| Conversion | How | Notes |
|---|---|---|
| PDF to images | Render each page at the chosen DPI with the tile path (whole page as one tile), encode with WIC | PNG, JPEG (quality setting), TIFF (multi-page option), BMP. WIC gives ARM64-native encoders and colour management. |
| Images to PDF | Open each image with MuPDF (it opens PNG, JPEG, TIFF and others as one-page documents) and write them through the `pdf` document writer | Page size options: fit to image, A4, Letter, with margins. |
| PDF to text | `text` document writer, or `fz_new_stext_page_from_page` for custom output | Plain text v1. HTML output is a small addition later (`html` writer). |
| Merge | Create a PDF with `pdf_create_document`, copy pages from each source with `pdf_graft_mapped_page` and one `pdf_graft_map` per source, save with `pdf_save_document` | Keep bookmarks from sources as top-level outline entries (stretch goal for v1). |
| Split | Same grafting into one new document per range | Ranges, every N pages, or one file per page. |
| Rotate, delete, reorder pages | `pdf_*` page tree functions on a copy, then save | Never modify the open viewer document in place. |
| Encrypt, decrypt | `pdf_write_options` with `do_encrypt = PDF_ENCRYPT_AES_256`, passwords and permissions | Save to a new file, then offer to replace. |
| PDF to Word | `docx` document writer | Good for flowing text; multi-column pages, tables and floating images come out rough. Ship it labelled as "editable Word (best for text documents)". Measure quality on the test corpus in M5 and decide whether layout work is needed. |
| Office to PDF | See 5.1 | |

Saving rules for every write: write to a temporary file in the destination folder, then replace with `ReplaceFileW` or `MoveFileExW`, so a crash never leaves a half-written file.

### 5.1 Office to PDF

1. Detect Microsoft Office: query the COM ProgIDs `Word.Application`, `Excel.Application`, `PowerPoint.Application`.
2. If present, run on a dedicated STA thread. Before opening any file set `AutomationSecurity = msoAutomationSecurityForceDisable` (3) so macros in untrusted documents never run. Open read-only, call `ExportAsFixedFormat` with the PDF format constant, close without saving, and `Quit` the app if Recto started it.
3. If Office is missing, detect LibreOffice from its registry install path. Run `soffice --headless --convert-to pdf --outdir <temp dir> <file>` with `-env:UserInstallation=file:///<Recto temp profile>` so it cannot collide with a LibreOffice window the user has open. Kill it after a timeout (default 120 s).
4. If neither is installed, show a message with links to both and stop.
5. Supported inputs: `.doc`, `.docx`, `.rtf`, `.odt`, `.xls`, `.xlsx`, `.ods`, `.ppt`, `.pptx`, `.odp`.

Both paths run separate programs, so they do not affect Recto's AGPL obligations.

## 6. UI design

The full interface specification, tokens, icon files and screenshots are in `design/` (start with `design/SPEC.md`). This section summarises it; where they differ, `design/SPEC.md` wins.

- Custom title bar holding a `TabView` (one tab per open document) and a command box (Ctrl+K) that searches files, pages and commands.
- Centre: the document view (`SwapChainPanel`), continuous vertical scroll by default, single page and two-page spread as options.
- Home: the command box, a drop target, and recent files with reading position.
- Convert: a three-step flow (files, what to make, options) with a job queue that stays visible.
- Organize: Combine, Split and Reorder modes over one page grid.
- Settings: theme, page colour in dark theme, layout preset and its four settings, default zoom, links, Office converter, save location, cache size, crash reports.
- Accessibility: keyboard navigation everywhere, high contrast, and a UI Automation text provider for page text (M3).

### 6.1 Colour tokens

| Token | Light | Dark |
|---|---|---|
| Window (title bar) | `#EEF1F6` | `#1B1A19` |
| Surface (panes, toolbar) | `#FAFAFA` | `#232120` |
| Raised (cards, menus) | `#FFFFFF` | `#2A2826` |
| Canvas behind pages | `#E6E7EA` | `#141312` |
| Border | `#E5E5E5` | `#3A3734` |
| Text | `#1B1B1B` | `#EDEAE4` |
| Secondary text | `#5C5C5C` | `#B8B2A8` |
| Accent (fills, focus, selection) | `#C8431F` | `#F0603C` |
| Text on accent | `#FFFFFF` | `#1B1A19` |
| Accent as text or icon | `#A93717` | `#F48A6E` |
| Accent tint (selected row) | `#FBE4DC` | `#3A2620` |

All text pairs meet WCAG AA (4.5:1). The tokens live in one `ResourceDictionary` with `ThemeDictionaries` for Light, Dark and HighContrast; no control sets a colour directly.

### 6.2 Layout presets

| Setting | Focus (default) | Classic | Reading room |
|---|---|---|---|
| Page toolbar | Floating, bottom centre | Docked command bar, top | Docked strip, top |
| Side panels | Floating cards over the canvas | Docked left pane | Docked right inspector |
| Thumbnails | In the left card | In the left pane | Filmstrip along the bottom |
| Navigation (Home, Convert, Organize) | Title bar tabs and command box | Left rail | Title bar mode switcher |

Implementation: one `ShellPage` hosts the same `DocumentView`, `PageToolbar`, `ThumbnailList`, `OutlineTree` and `SearchPanel` controls in every preset. A `LayoutSettings` view model holds the four settings; the shell moves the controls between named host slots (top bar, bottom overlay, left dock, right dock, left overlay, bottom dock) when a setting changes. `ThumbnailList` switches its `ItemsRepeater` layout between vertical and horizontal. Changing any single setting turns the preset name to "Custom".

## 7. Milestones

| Milestone | Scope | Done when |
|---|---|---|
| **M0 Repository and build** | Git repo, AGPL LICENSE, NOTICE, CLA, CONTRIBUTING; MuPDF submodule at a pinned tag with `mupdf-config.h`; solution with all module projects; x64 and ARM64 builds; GitHub Actions CI | A clean clone builds both architectures in CI with one command |
| **M1 Engine core** | Bridge, runtime and locks (4.3), error bridge (4.4), `DocumentActor`, display list cache, `RenderPool`, cancel tokens, `recto-cli render`; thread-safety tests (4.11); fuzz target for open and render | All 4.11 tests pass under ASan; `recto-cli` renders the corpus with no crashes; fuzzer runs 1 hour clean |
| **M2 Viewer** | WinUI shell, tabs, `SwapChainPanel` presentation thread, tiles, scroll, zoom, rotate, thumbnails, outline, links, password prompt, recent files | Targets in section 8 met on the reference machine |
| **M3 Text** | Text selection, copy, find with highlight and next/previous, UI Automation text provider | Search finds every hit in the corpus text set |
| **M4 Conversions A** | PDF to images, images to PDF, PDF to text, merge, split, page reorder/rotate/delete, encrypt/decrypt, job queue UI | Each conversion round-trips the corpus with no errors |
| **M5 Conversions B** | PDF to DOCX, Office to PDF (both paths) | DOCX quality reviewed on 20 corpus files; Office path tested with Office present, LibreOffice only, and neither |
| **M6 Ship** | Printing, MSIX packaging, `.pdf` file association, Store listing with AGPL terms, About dialog with source link, crash reporting, localization-ready resource files | Store certification passes |

Printing note: confirm in M6 which print path works in a Windows App SDK 2.x desktop app (`PrintManagerInterop` with the window handle, or Direct2D's `ID2D1PrintControl`) before designing the print UI.

## 8. Performance targets

| Measure | Target |
|---|---|
| First page visible after opening a 100-page text PDF | under 300 ms |
| Scrolling | 60 fps with no blank tiles at normal scroll speed |
| Zoom change | Stretched preview in the same frame, sharp tiles within 150 ms |
| Memory with one 500-page document open | under 600 MB |
| App start to empty window | under 1 s |

The reference machine is to be chosen in M0 (one mid-range x64 laptop and one ARM64 laptop).

## 9. Security

- Treat every PDF and Office file as hostile.
- Fuzz the bridge from M1, and keep a fuzz job in CI.
- JavaScript disabled in MuPDF (D12). Office macros disabled during conversion (5.1).
- Links to web addresses and files ask before opening. Launch actions in PDFs are never executed.
- After v1: move the bridge into a broker process running in an AppContainer with no file or network access, talking to the app over a shared-memory channel. The module rules in 3.1 keep this a contained change.

## 10. AGPL compliance checklist

- [ ] LICENSE (AGPL-3.0) at the repo root, and the license shown in the About dialog.
- [ ] About dialog links to the exact source of the running version (tag per release).
- [ ] NOTICE lists MuPDF and its bundled third-party libraries, Windows App SDK (MIT) and every other dependency, each with its license.
- [ ] Every dependency checked for AGPL compatibility before it is added.
- [ ] Partner Center listing uses Recto's own license terms (AGPL), not the Store's standard license.
- [ ] CLA in place before the first outside pull request is merged.
- [ ] Each release tag includes the MuPDF submodule commit, so the full corresponding source is reproducible.

## 11. Open items

| Item | Needed by |
|---|---|
| Trademark search for "Recto" (USPTO, EUIPO) and Store name reservation | M0 |
| MuPDF release tag to pin: chosen 1.28.5 (`8ad45e92`), pinned 2026-10-13 in D00 T01 §2 | M0 |
| Reference machines for section 8 | M0 |
| Print API choice | M6 |
| Whether PDF to DOCX needs Recto's own layout work after the M5 review | M5 |

## Sources

- [Windows App SDK release channels](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/release-channels)
- [Windows App SDK 2.0 release notes](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/release-notes/windows-app-sdk-2-0)
- MuPDF documentation on multi-threading and the `multi-threaded.c` example in the MuPDF source tree (`docs/examples/`)
