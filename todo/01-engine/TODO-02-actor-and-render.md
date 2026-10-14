---
schema_version: 1
id: actor-and-render
domain: 01-engine
status: active
title: "TODO-02 -- Content Interpreter, Display Lists, Document Actor and Render Pool"
frozen: true
track: M1
---

# TODO-02 -- Content Interpreter, Display Lists, Document Actor and Render Pool

> **Goal:** Complete the rendering engine in `src/pdf` and the multi-threaded actor and tile-rendering runtime in `src/core`: the PDF content-stream interpreter (paths, clipping, graphics state, colour spaces, images, Form XObjects, text positioning), DirectWrite font loading (embedded TrueType/OpenType/CFF, Type 1 charstrings, Type 3 procedural glyphs, Standard 14 fonts, `/ToUnicode` and encoding maps), immutable `DisplayList` recording and Direct2D tile rasterization, `DocumentActor` threads, `RenderPool` workers with `CancelToken` abort, `TileScheduler`, and ordered `Runtime` shutdown.

> **Current state:** Measured 2026-10-14. `src/core/recto-core.vcxproj` compiles the scaffold `recto::core::abi_version()` entry point. `D01 T01` provides `recto::pdf::Document`, decoded streams, resources, and page geometry.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 3.3 (`TileKey`, `DisplayList`, `CancelToken`), 4.1 items 4 and 5 (content-stream interpreter and font loading), 4.2 to 4.8 (thread ownership, runtime map, actor, tile rendering, shutdown order).
- `design/SPEC.md` sections 9.2 (page outlines immediately, tiles fill in, stretched zoom preview, dark-theme page colour modes).
- -> XREF: D01 T01 §1 -- consumes `recto::pdf::Document`, `PdfResult<T>`, and decoded page content and resource streams from `src/pdf`.
- -> XREF: D01 T03 §3 -- the thread-safety verification battery in `D01 T03 §3` stress-tests the actor, display-list, render-pool and cancellation machinery built here.
- -> XREF: D02 T02 §2 -- the viewer's presentation thread and texture cache consume the `TileKey`, BGRA premultiplied tile buffers, and scheduler callbacks defined here.

## Outcome

- `recto-pdf` interprets page content streams (vector paths, strokes, fills, clipping, `/ExtGState` alpha and blend modes, `/DeviceGray`/`RGB`/`CMYK`/`Indexed`/`ICCBased` colours, image and Form XObjects, and embedded/Standard-14 fonts via DirectWrite) into immutable, reference-counted `DisplayList`s.
- Each open document runs on its own `DocumentActor` thread (`src/core/document_actor.cpp`) with a bounded LRU cache of `DisplayList`s; the UI thread never blocks on PDF parsing or content-stream interpretation.
- `RenderPool` worker threads rasterize `512 x 512` premultiplied BGRA tiles from `std::shared_ptr<const DisplayList>` in parallel via per-worker Direct2D / WIC bitmap targets, honoring `CancelToken` between command batches and producing byte-identical output regardless of thread count.
- `Runtime::~Runtime()` tears down actors, jobs, workers and presentation hooks in the exact order of foundation plan section 4.8, verified by zeroed Debug live-object counters.

**Adjacency:** all=not-applicable (engine rendering and concurrency runtime in `src/pdf` and `src/core`; UI presentation and controls live in domain 02)

**Adjacency rationale:** Tile rendering, caching and scheduling are internal engine mechanisms whose outputs feed `DocumentView` in `D02 T02`. No user-editable settings or persistent records are owned in this file (the texture cache budget is owned by `D02 T02 §2` and `D02 T04 §2`).

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Content-stream interpreter, DirectWrite font loader and immutable `DisplayList` | D01 T01 §4 | [ ] |
| 2 | §2 | Direct2D tile rasterizer and cooperative `CancelToken` | §1 | [ ] |
| 3 | §3 | `DocumentActor` thread and message queue | §1 | [ ] |
| 4 | §4 | `RenderPool`, `TileScheduler` priority queue and ordered `Runtime` shutdown | §2, §3 | [ ] |

## 1. Content-stream interpreter, DirectWrite font loader and immutable `DisplayList`

Implement `src/pdf/font.cpp`, `src/pdf/display_list.cpp`, and `src/pdf/interpreter.cpp`:
- **Font loader (`src/pdf/font.cpp`):** Load embedded `/FontFile`, `/FontFile2` (TrueType), and `/FontFile3` (OpenType/CFF) streams into DirectWrite `IDWriteFontFace` instances via a custom in-memory `IDWriteFontFileLoader`; convert legacy Type 1 / CFF charstrings to vector path geometries where DirectWrite does not load a bare Type 1 program directly; interpret `/Type3` charprocs into sub-display-lists; map Standard 14 font names (`Helvetica`, `Times-Roman`, `Courier`, `Symbol`, `ZapfDingbats`, etc.) to metric-compatible Windows 11 fonts; and parse encodings (`WinAnsiEncoding`, `MacRomanEncoding`, `/Differences`, `/ToUnicode` CMap) for glyph indices and Unicode codepoints.
- **Content-stream interpreter (`src/pdf/interpreter.cpp`):** Execute PDF graphics operators (`q`/`Q`, `cm`, line width/cap/join/dash, `/ExtGState` alpha/blend, path construction `m`/`l`/`c`/`v`/`y`/`h`/`re`, painting `S`/`s`/`f`/`f*`/`B`/`B*`/`b`/`b*`/`n`, clipping `W`/`W*`, colour spaces `DeviceGray`/`RGB`/`CMYK`/`Indexed`/`ICCBased`, text positioning and showing `BT`/`ET`/`Tf`/`Td`/`TD`/`Tm`/`T*`/`Tj`/`TJ`/`'`/`"`, `/Subtype /Image` and inline images `BI`/`ID`/`EI`, and recursive `/Subtype /Form` XObjects capped at depth 32) into an immutable `DisplayList` command buffer.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that once `DisplayListBuilder::finish()` returns `std::shared_ptr<const DisplayList>`, the `DisplayList` and all its referenced glyph runs, path geometries and decoded image bitmaps are deeply immutable and safe for concurrent lock-free reads from any number of `RenderPool` worker threads.

**Build order:**

1. Implement `DisplayList` (immutable command buffer of state pushes/pops, transforms, clips, paths, glyph runs, and immutable decoded image bitmaps) and `src/pdf/font.cpp` (in-memory `IDWriteFontFileLoader`, Standard 14 fallback table, encoding and `/ToUnicode` CMap parser, Type 1/Type 3 path fallback). Done when: `PdfFont.*` tests load embedded TrueType/OpenType fonts from memory and map character codes to both glyph indices and Unicode strings.
2. Implement `src/pdf/interpreter.cpp` interpreting page content streams and `/Form` XObjects into `DisplayList`. Done when: `PdfInterpreter.*` records display lists for all `tests/corpus/render/` pages (text, vector shapes, clipping, CMYK/RGB colours, rotated pages, embedded images) without error under ASan, and a cyclic `/Form` XObject (`F1 -> F1`) stops at the depth-32 guard.

- [ ] DirectWrite in-memory font loader, Standard 14 metric mapping, Type 1/Type 3 support, and `/ToUnicode` CMap parser in `src/pdf/font.cpp`. Done when: embedded and Standard 14 text runs resolve glyph indices and advances accurately across the corpus. Cheaper substitute: GDI `AddFontMemResourceEx` or ignoring embedded fonts and rendering everything in Arial.
- [ ] Content-stream interpreter (`src/pdf/interpreter.cpp`) producing deeply immutable `DisplayList`s. Done when: every corpus render page records a `DisplayList` and cyclic `/Form` XObjects stop at the recursion cap.
- [ ] Commit: `pdf: add DirectWrite font loader, content-stream interpreter and immutable DisplayList`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFont.*:PdfInterpreter.*` passes under ASan across all corpus render and broken files.

## 2. Direct2D tile rasterizer and cooperative `CancelToken`

Implement `src/pdf/rasterizer.cpp` and `CancelToken`: each worker owns a thread-local or per-worker Direct2D `ID2D1Factory1` (`D2D1_FACTORY_TYPE_MULTI_THREADED`) / WIC bitmap render target (`GUID_WICPixelFormat32bppPBGRA`), clears the target tile (`512 x 512` or full-page render buffer) to white (or transparent when requested), applies the page-to-tile transform (zoom scale, page `/Rotate` plus view rotation `0/90/180/270`, and tile origin offset), and replays the `DisplayList` while checking `CancelToken::is_cancelled()` every 64 commands so a cancelled high-DPI tile aborts within milliseconds.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that each rasterizer context (`ID2D1RenderTarget` / WIC bitmap) is strictly owned by one worker thread (never shared across threads), `DisplayList` replay is read-only, and tripping `CancelToken` returns `ErrorCode::Cancelled` cleanly without leaking D2D or WIC resources.

**Build order:**

1. Implement `CancelToken` and the `Rasterizer` class (`src/pdf/rasterizer.cpp`) rendering any rectangular tile or full page of a `DisplayList` into a caller-supplied BGRA premultiplied buffer with explicit row stride. Done when: rendering a page as one full bitmap versus stitching `512 x 512` tiles produces pixel-identical output across `0`, `90`, `180` and `270` degree rotations.
2. Wire cooperative cancellation checks inside `Rasterizer::render_tile` and test pre-cancelled and mid-replay cancellation on a dense vector page. Done when: tripping `CancelToken` mid-render aborts in under 10 ms and returns `ErrorCode::Cancelled` with zero ASan leaks.

- [ ] Per-worker Direct2D `DisplayList` tile rasterizer supporting arbitrary DPI, tile offsets, and `0/90/180/270` rotations. Done when: stitched `512 x 512` tiles match full-page renders pixel for pixel. Cheaper substitute: rendering the entire page at high zoom and cropping out a 512x512 rect, which explodes memory at 400% zoom.
- [ ] Cooperative `CancelToken` checked during `DisplayList` replay. Done when: mid-render cancellation returns `ErrorCode::Cancelled` in `< 10 ms` under ASan.
- [ ] Commit: `pdf: implement Direct2D DisplayList tile rasterizer with cooperative cancellation`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfRasterizer.*` passes under ASan (full-page vs stitched-tile pixel identity at 4 rotations, plus mid-render cancellation).

## 3. `DocumentActor` thread and message queue

Implement `src/core/document_actor.cpp`: one dedicated thread per open document (`DocumentHandle`), owning the `recto::pdf::Document` and a 32-page LRU cache of `std::shared_ptr<const DisplayList>`. All requests from the UI thread or `TileScheduler` (`open`, `authenticate`, `get_description`, `get_display_list`, `get_outline`, `get_page_links`, `close`) arrive over a bounded, thread-safe MPSC command queue and reply via callbacks/futures without blocking the caller.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `recto::pdf::Document` never leaves its `DocumentActor` thread, only immutable value structs (`DocumentDescription`, `OutlineNode`, `LinkAnnotation`) and `std::shared_ptr<const DisplayList>` cross out of the actor, and `DocumentActor` increments/decrements the Debug live-document counter.

**Build order:**

1. Implement the MPSC actor queue and `DocumentActor` lifecycle (`open`, `authenticate` state machine for password-protected PDFs, `get_description`, `get_display_list` with 32-entry LRU cache, and deterministic teardown on `close`). Done when: opening an encrypted corpus PDF transitions `NeedsPassword -> WrongPassword -> DocumentOpened` purely via async messages without blocking the calling thread.
2. Add Debug atomic counters (`live_documents`, `live_display_lists`) and test concurrent requests from multiple threads against one `DocumentActor` plus 20 simultaneous `DocumentActor` instances opening and closing documents. Done when: all actors shut down with `live_documents == 0` and `live_display_lists == 0` under ASan.

- [ ] `DocumentActor` thread owning `pdf::Document` and a 32-page `DisplayList` LRU cache. Done when: repeated tile requests for the same page reuse the cached `shared_ptr<const DisplayList>` (cache hit counter verified in tests). Cheaper substitute: a global mutex around `pdf::Document` called from arbitrary threads.
- [ ] Password state machine and clean actor teardown verified with Debug live-object counters. Done when: 20 concurrent open/close cycles leave all counters at 0.
- [ ] Commit: `core: add DocumentActor thread with DisplayList LRU cache and password state machine`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Actor.*` passes under ASan covering async open, password retry, `DisplayList` caching, and 20-document lifecycle teardown.

## 4. `RenderPool`, `TileScheduler` priority queue and ordered `Runtime` shutdown

Implement `src/core/render_pool.cpp`, `src/core/tile_scheduler.cpp`, and `src/core/runtime.cpp` per foundation plan sections 4.3, 4.5, and 4.8:
- `RenderPool`: `N = std::clamp(hardware_concurrency - 2, 2, 8)` worker threads, each owning its own `pdf::Rasterizer` instance.
- `TileScheduler`: prioritizes visible current-page tiles, visible adjacent-page tiles, 1-tile prefetch ring, and thumbnails; automatically trips `CancelToken` on queued or in-flight requests whose zoom bucket or viewport relevance is superseded by a new viewport snapshot.
- `Runtime`: coordinates `DocumentActor`s, `TileScheduler`, `RenderPool`, conversion job threads, and presentation shutdown hooks in the strict teardown sequence of section 4.8, asserting zero live objects in Debug builds.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that `Runtime::~Runtime()` executes the exact 6-step shutdown sequence of foundation plan section 4.8 (trip `CancelToken`s -> join `DocumentActor`s -> join jobs -> drain and join `RenderPool` workers -> join presentation hooks -> assert Debug counters == 0).

**Build order:**

1. Implement `RenderPool` and `TileScheduler` with priority ordering (1: visible current page, 2: visible adjacent, 3: prefetch ring, 4: thumbnails) and automatic `CancelToken` cancellation when `update_viewport` moves away or changes zoom bucket. Done when: submitting a viewport jump cancels stale queued and in-flight tiles and delivers the new viewport's priority-1 tiles first.
2. Implement `Runtime` (`src/core/runtime.cpp`) owning the actor registry, `RenderPool`, and ordered shutdown sequence with Debug counter assertions. Done when: destroying `Runtime` while tiles are actively rendering drains and joins every thread cleanly without deadlock or ASan use-after-free.

- [ ] `RenderPool` (`clamp(cores - 2, 2, 8)` workers) and 4-tier `TileScheduler` with automatic stale-request cancellation. Done when: priority-1 tiles complete before priority-3/4 tiles in `Scheduler.PriorityAndCancel`. Cheaper substitute: a FIFO thread pool where scrolling 50 pages forces the user to wait for all 49 intermediate pages to render first.
- [ ] `Runtime` enforcing the section 4.8 shutdown order and Debug counter assertions. Done when: `Runtime.OrderedShutdownUnderLoad` passes 50 iterations under ASan.
- [ ] Commit: `core: implement RenderPool, priority TileScheduler and ordered Runtime shutdown`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Scheduler.*:Runtime.*` passes under ASan with priority ordering, viewport-jump cancellation, and shutdown-under-load verified.

## Verification

- `recto-tests.exe --gtest_filter=PdfFont.*:PdfInterpreter.*:PdfRasterizer.*:Actor.*:Scheduler.*:Runtime.*` passes in Debug, Release and ASan builds.
- Debug live-object counters (`live_documents`, `live_display_lists`, `live_workers`) end at 0 after every test.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
