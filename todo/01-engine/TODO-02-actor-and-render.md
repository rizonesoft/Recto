---
schema_version: 1
id: actor-and-render
domain: 01-engine
status: active
title: "TODO-02 -- Content Interpreter, Display Lists, Incremental Writer, Document Actor and Render Pool"
frozen: true
track: M1
---

# TODO-02 -- Content Interpreter, Display Lists, Incremental Writer, Document Actor and Render Pool

> **Goal:** Complete the rendering and incremental-save engine in `src/pdf` and the multi-threaded actor and tile-rendering runtime in `src/core`: the immutable `DisplayList` command buffer, DirectWrite in-memory font loader (embedded TrueType/OpenType/CFF, Type 1 charstrings, Type 3 procedural glyphs, Standard 14 fonts, `/ToUnicode` and encoding maps), the PDF content-stream interpreter (graphics state, paths, clipping, colour spaces, text positioning, images, Form XObjects), the incremental PDF revision writer (`src/pdf/incremental_writer.cpp`), Direct2D tile rasterization with sub-10 ms cooperative `CancelToken` abort, `DocumentActor` threads, `RenderPool` workers, 4-tier `TileScheduler`, and foundation plan section 4.8 ordered `Runtime` shutdown.

> **Current state:** **Corrected 2026-10-20:** (was: Corrected 2026-10-19 noting `D01 T01 §8-§10` pending) `src/core/recto-core.vcxproj` compiles the scaffold `recto::core::abi_version()` entry point, and `D01 T01 §1-§10` (`todo/01-engine/TODO-01-bridge-runtime.md`, `status: done`) are all verified (`PdfResult<T>`, `PdfValue`, `Lexer`, `Parser`, `XrefTable`, `ObjStm` & damaged-file recovery, stream filters, Windows CNG `bcrypt.dll` decryption, and `recto::pdf::Document`).

> **Groomed 2026-10-23:** Measured against the tree: `src/pdf/display_list.h/.cpp` (`§1`) is the only part of this file built so far (with `tests/recto-tests/pdf_display_list_tests.cpp`); `font`, `interpreter`, `incremental_writer`, `rasterizer` and `src/core/document_actor`, `render_pool`, `tile_scheduler`, `runtime` do not exist yet. The build is C++23 (`/std:c++23preview`, `Directory.Build.targets`), `recto-ui` replaces WinUI (`todo/10-ui/`), and the engine has no UI dependency. Gaps filed as `§15` to `§21`: transparency and blend compositing, shadings, patterns and text clip, non-embedded fonts and CJK CMaps, the missing `ErrorCode::ResourceLimit` and `InvalidArgument`, interpreter work ceilings, tile failure isolation, and page `/UserUnit`.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 3.3 (`TileKey`, `DisplayList`, `CancelToken`), 4.1 items 4 to 6 (content-stream interpreter, font loading, incremental save), 4.2 to 4.8 (thread ownership, runtime map, actor, tile rendering, shutdown order).
- `design/SPEC.md` section 9.2 (page outlines immediately, tiles fill in, stretched zoom preview, dark-theme page colour modes).
- -> XREF: D01 T01 §1 -- consumes `recto::pdf::Document` (`§10`), `PdfResult<T>` (`§1`), and decoded page content and resource streams (`§7`, `§8`) from `src/pdf`.
- -> XREF: D01 T03 §6 -- the thread-safety verification battery in `D01 T03 §6` and `§7` stress-tests the actor, display-list, render-pool and cancellation machinery built here.
- -> XREF: D01 T03 §1 -- the CLI exit-code contract in `D01 T03 §1` maps the `ResourceLimit` and `InvalidArgument` codes added in `§18`.
- -> XREF: D02 T01 §3 -- `DocumentTabSession` in `D02 T01 §3` owns a per-tab `DocumentHandle` and closes its `DocumentActor` (`§8`) on tab close; `OpenDocumentPipeline` in `D02 T01 §5` drives the actor's async open/authenticate flow, and `Save` in `D02 T01 §8` invokes `§5`'s incremental writer.
- -> XREF: D02 T02 §3 -- the viewer's presentation thread, `TileTextureCache` (`D02 T02 §3`), and viewport-to-scheduler driver (`D02 T02 §4`) consume the `TileKey`, BGRA premultiplied tile buffers, and `TileScheduler` (`§9`) built here.
- -> XREF: D02 T03 §7 -- Optional Content Groups (`D02 T03 §7`) filter `BDC`/`EMC` layer blocks during `DisplayList` compilation (`§3`, `§4`), and `FontInventory` (`D02 T03 §9`) inspects `§2`'s font descriptors.
- -> XREF: D04 T05 §5 -- the `PDF/A` font-subsetting compliance engine (`D04 T05 §5`) inspects `§2`'s font loader and embedded font streams.
- -> XREF: D05 T01 §8 -- `SignatureEngine` (`D05 T01 §8`) uses `§5`'s `IncrementalWriter` when counter-signing documents.
- -> XREF: D06 T03 §1 -- user-reported hostile-input fixtures (inline `EI`, short operators, overflowing operands) and the recovered-error contract (§2 there) gate this interpreter and `DisplayList` builder.
- -> XREF: D03 T04 §1 -- Edit PDF's `EditModel` consumes this interpreter's operator stream and graphics state.
- -> XREF: D05 T01 §12 -- the actor accepts a certificate for `/Adobe.PubSec` files (D05 T01 §12).
- -> XREF: D04 T06 §3 -- print production observes paint events here (D04 T06 §3).
- -> XREF: D02 T02 §22 -- line-weight option joins the tile identity.
- -> XREF: D02 T02 §3 -- TileTextureCache keys by the full TileKey.
- -> XREF: D02 T03 §7 -- line_weight_mode sits beside ocg_generation in TileKey.
- -> XREF: D02 T02 §12 -- page transition parser and duration accessor.
- -> XREF: D02 T02 §14 -- transitions read through the actor query.
- -> XREF: D02 T03 §23 -- layer edits are OcgEdit builders on the actor session.
- -> XREF: D02 T03 §24 -- import as layer uses the same builder contract.
- -> XREF: D02 T03 §25 -- Layers panel drives stage, undo, redo and discard.
- -> XREF: D02 T01 §8 -- SavePipeline writes the exported revision and marks it saved.

## Outcome

- `recto-pdf` interprets page content streams (vector paths, strokes, fills, clipping, `/ExtGState` alpha and blend modes, `/DeviceGray`/`RGB`/`CMYK`/`Indexed`/`ICCBased`/`Separation` colours, image and Form XObjects, and embedded/Standard-14 fonts via DirectWrite) into immutable, reference-counted `DisplayList`s, and writes non-destructive incremental PDF revisions (`src/pdf/incremental_writer.cpp`).
- Each open document runs on its own `DocumentActor` thread (`src/core/document_actor.cpp`) with a 32-page LRU cache of `DisplayList`s; the UI thread never blocks on PDF parsing or content-stream interpretation.
- `RenderPool` worker threads rasterize `512 x 512` premultiplied BGRA tiles from `std::shared_ptr<const DisplayList>` in parallel via per-worker Direct2D / WIC bitmap targets, honoring `CancelToken` every 64 commands and producing byte-identical output regardless of thread count.
- `Runtime::~Runtime()` tears down actors, jobs, workers and presentation hooks in the exact 6-step order of foundation plan section 4.8, verified by zeroed Debug live-object counters.
- Parity additions (D18): a `line_weight_mode` render option carried in `TileKey` (`§11`), a page transition query (`§12`), in-memory staged revisions (`§13`) and an undoable optional-content edit session on the actor that publishes a new `ocg_generation` (`§14`).

**Adjacency:** all=not-applicable (engine rendering, incremental writing, and concurrency runtime in `src/pdf` and `src/core`; UI presentation and controls live in domain 02)

**Adjacency rationale:** Tile rendering, caching and scheduling are internal engine mechanisms whose outputs feed `DocumentView` in `D02 T02`. No user-editable settings or persistent records are owned in this file (the texture cache budget is owned by `D02 T02 §3` and `D02 T04 §6`).

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Immutable `DisplayList` command buffer and `DisplayListBuilder` (`display_list.cpp`) | D01 T01 §10, D06 T05 §24 | [x] |
| 2 | §2 | DirectWrite in-memory font loader, `LoadedFont`, glyph metrics and `glyf` fallback | §1 | [x] |
| 3 | §3 | Content-stream interpreter: graphics state, paths, painting, clipping and device colour | §1 | [x] |
| 4 | §4 | Content-stream text positioning, `/Subtype /Image`, inline images and `/Form` XObjects | §2, §3, §27, §28, §29, §30, §32, §33, D06 T05 §32, D06 T05 §33 | [ ] |
| 5 | §5 | Incremental PDF revision writer (`src/pdf/incremental_writer.cpp`) | D01 T01 §10, D06 T05 §24, §34, §35, §36 | [ ] |
| 6 | §6 | Per-worker Direct2D / WIC `DisplayList` tile rasterizer (`src/pdf/rasterizer.cpp`) | §4 | [ ] |
| 7 | §7 | Cooperative `CancelToken` and sub-10 ms mid-replay tile cancellation | §6 | [ ] |
| 8 | §8 | `DocumentActor` thread, MPSC queue, 32-page `DisplayList` cache and password state | §4, §5 | [ ] |
| 9 | §9 | `RenderPool` worker threads and 4-tier priority `TileScheduler` with jump cancellation | §7, §8 | [ ] |
| 10 | §10 | `Runtime` coordinator, section 4.8 ordered shutdown sequence and live-object counters | §9 | [ ] |
| 11 | §11 | `RenderOptions::line_weight_mode` in the rasterizer and the `TileKey` identity | §9 | [ ] |
| 12 | §12 | `DocumentActor::get_page_transition` query for the parsed `/Trans` and `/Dur` | §8, D02 T02 §12 | [ ] |
| 13 | §13 | `ObjectChangeSet` and the in-memory `StagedRevisionChain` over `IncrementalWriter` | §5, §18 | [ ] |
| 14 | §14 | `DocumentActor` optional-content edit session: stage, undo, redo, `ocg_generation` | §8, §13, §18, D02 T03 §7 | [ ] |
| 15 | §15 | Transparency groups, blend modes and soft masks in the DisplayList and rasterizer | §3, §4, §6 | [ ] |
| 16 | §16 | Shading types 1 to 7, tiling patterns and text clip modes in the rasterizer | §4, §6 | [ ] |
| 17 | §17 | Non-embedded font substitution, predefined CJK CMaps and vertical metrics | §2, §4, §27, §30 | [ ] |
| 18 | §18 | `ErrorCode::ResourceLimit` and `ErrorCode::InvalidArgument` in `error.h` | D01 T01 §1 | [x] |
| 19 | §19 | Interpreter work, memory and wall-clock ceilings on hostile content streams | §4, §7, §8, §18 | [ ] |
| 20 | §20 | Rasterizer failure isolation: typed tile errors, target recreation and retry | §7, §9, §19 | [ ] |
| 21 | §21 | Page `/UserUnit` in `PageDescription`, physical page size and CLI output size | D01 T01 §10, §6 | [ ] |
| 22 | §22 | Error codes named by later sections reconciled with `error.h` | §18 | [x] |
| 23 | §23 | Closed refusal reasons and numeric context on limit and argument errors | §18 | [x] |
| 24 | §24 | Conversion-check failures name the check, the page and the measured value | §22, §23 | [x] |
| 25 | §25 | Decoder failures keep their step and Windows HRESULT in the diagnostic ring and ETW | §23 | [x] |
| 26 | §26 | Image decoder recovery outcomes in diagnostics: substituted rows and refusal causes | §25 | [x] |
| 27 | §27 | Standard 14 font metrics from the Adobe Core14 AFM files, with system-face fallback | §2 | [ ] |
| 28 | §28 | Simple-font encodings, the Adobe Glyph List, `/Differences` and `/ToUnicode` CMaps | §2 | [ ] |
| 29 | §29 | Type 1 and CFF charstrings to glyph outlines (`eexec`, Type 2, `seac`, subroutines) | §2 | [ ] |
| 30 | §30 | CID fonts: `/Type0`, `/DescendantFonts`, `/CIDToGIDMap`, `/W`, `/DW` and `/W2` | §28, §29 | [ ] |
| 31 | §31 | Type 3 fonts: `/CharProcs` recorded as per-glyph sub-display-lists | §4, §19 | [ ] |
| 32 | §32 | Colour spaces: CalGray, CalRGB, Lab, ICCBased and calibrated CMYK through Windows colour | §3 | [ ] |
| 33 | §33 | Indexed, Separation and DeviceN colour with PDF functions 0, 2, 3 and 4 | §32 | [ ] |
| 34 | §34 | Native Flate compressor (`flate_compress`: LZ77, Huffman blocks, zlib wrapper) | D01 T01 §10 | [ ] |
| 35 | §35 | Canonical `PdfValue` serializer for indirect objects and streams | D01 T01 §10 | [ ] |
| 36 | §36 | Encrypting new objects with a document's `SecurityHandler` (RC4, AES-128, AES-256) | D01 T01 §10 | [ ] |

## 1. Immutable `DisplayList` command buffer and `DisplayListBuilder` (`display_list.cpp`)

Implement the thread-safe, immutable display-list representation in `src/pdf/display_list.h` and `src/pdf/display_list.cpp`:
- Value types for recorded commands: `SaveState`, `RestoreState`, `ConcatMatrix` (`PdfMatrix`), `SetStrokeStyle` (width, cap, join, miter limit, dash array/phase), `SetAlphaAndBlend` (`stroke_alpha`, `fill_alpha`, `BlendMode`), `PushClipPath` (path index, fill rule `NonZero` vs `EvenOdd`), `DrawPath` (path index, stroke colour, fill colour, stroke/fill flags, fill rule), `DrawGlyphRun` (font face handle/Type 3 sub-list, positioned glyph indices and `(x, y)` origins in text/user space, font size, text rendering mode `Tr`, fill/stroke colours, CTM/text matrix), and `DrawImage` (immutable `std::shared_ptr<const DecodedBitmap>`, destination transform, interpolation hint, soft-mask).
- `DisplayListBuilder`: records commands, interns path geometry segments (`MoveTo`, `LineTo`, `CubicBezierTo`, `CloseFigure`) and glyph arrays into contiguous flat vectors, computes the content bounding box (`content_bounds_pt` for `Auto-Detect White Margins` in `D05 T01 §4`), and seals the result into a `std::shared_ptr<const DisplayList>` via `finish()`.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that once `DisplayListBuilder::finish()` returns `std::shared_ptr<const DisplayList>`, the `DisplayList` exposes `const`-only accessors over contiguous immutable vectors and `std::shared_ptr<const DecodedBitmap>` buffers, with zero mutable members or lazy caches.

> **Started:** 2026-10-20

> **Corrected 2026-10-24:** the earlier review stopped at round 3 (`docs/reviews/d01-t02-s1-r3.md`) with six findings open, and the plan review (`docs/reviews/01-engine/D01-T02-s1.md`) found the contract underspecified. "Exact ink bounding box" is replaced by the bounding-box-device contract MuPDF's `fz_bbox_device` uses, a recorded default: `content_bounds_pt` contains every painted point (it never cuts ink, which a crop must never do) and is each primitive's exact box (analytic cubic extrema; strokes widened by half the line width, projecting caps and miter joins up to the miter limit; glyph ink boxes; the opaque-pixel box of an image intersected with its soft mask's in unit-image space), intersected with the bounding boxes of the active clips and the page box. It is tight under rectangular clips and looser under curved or non-rectangular clips and dashes, which the consumer's 12 pt padding absorbs; changing to exact clipped geometry later costs the analytic clipper the earlier rounds could not finish. Rounds 1 to 3 had ticked the content-bounds item against the exact contract; it is reopened below. ExtGState soft masks over paths and text belong to `§15`, text clip modes 4 to 7 to `§16`, glyph outlines to `§2`, `/UserUnit` to `§21`.

> **Corrected 2026-10-24 (defaults taken in the build):** `PositionedGlyph::ink_bounds_pt` is in text-space points with the font size already applied by the caller, like `origin_pt` and `advance_pt` (only the text matrix and CTM are applied); an interpolated image's coverage reaches half a source cell beyond its opaque cells (the bilinear footprint), which replay in `§6` must stay inside; bitmap row 0 is the image's first row, at the top of the unit square (ISO 32000-1 8.9.4).

**Build order:**

1. Author `src/pdf/display_list.h` and `src/pdf/display_list.cpp` implementing `DisplayCommand`, `RecordedPath`, `RecordedGlyphRun`, `DecodedBitmap`, `DisplayList`, and `DisplayListBuilder` (with Debug atomic `live_display_lists` counter). Done when: `display_list.cpp` compiles cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/pdf_display_list_tests.cpp` verifying command recording, path/glyph contiguous storage, bounding-box computation, `live_display_lists` counter increment/decrement, and concurrent read access from 8 threads. Done when: `PdfDisplayList.*` passes under ASan.

- [x] Define `DisplayCommand` and contiguous command/path/glyph storage in `DisplayList` so replaying a page walks flat arrays with minimal pointer chasing. Done when: `DisplayList` contains zero non-`const` methods or `mutable` fields.
- [x] Implement `RecordedPath` storing `MoveTo`, `LineTo`, `CubicBezierTo`, `Rect`, and `CloseSubpath` verbs in a shared coordinate pool on `DisplayList`. Done when: `PdfDisplayList.PathRecording` records multi-subpath geometries and computes exact bounding boxes.
- [x] Implement `DecodedBitmap` (`width`, `height`, `stride`, immutable `std::vector<uint8_t>` premultiplied BGRA pixels, `is_monochrome_mask`) wrapped in `std::shared_ptr<const DecodedBitmap>`. Done when: image bitmaps are shared zero-copy across commands and worker threads.
- [x] Implement `DisplayListBuilder::finish()` sealing the builder and computing `page_box_pt` and `content_bounds_pt` under the bounding-box-device contract above in `src/pdf/display_list.cpp`. Done when: `PdfDisplayList.ContentBounds` returns each primitive's exact box for unclipped paths, strokes (butt, round, projecting caps; miter, round, bevel joins; a transformed, sheared stroke), glyph runs and images, and the clipped box under rectangular clips. **Corrected 2026-10-24:** reopened from `[x]` (round 3 found ink lost under curved clips and dashes, R3-3 and R3-4).
- [x] Never cut ink: a primitive under a curved clip, a dashed stroke, a run without per-glyph ink boxes and a sliver inside a cubic clip all keep bounds that contain the true ink. Done when: `PdfDisplayList.BoundsNeverCutInk` reproduces R3-3, R3-4 and R3-5 and each box contains the analytic ink box.
- [x] Image coverage from the opaque-pixel box of the image and of its soft mask, intersected in unit-image space, with the opaque box computed once in `DecodedBitmap::create`. Done when: `PdfDisplayList.ImageCoverageUsesMaskCells` gives `[50, 0, 100, 100]` for a fully opaque 1x1 image under a 2x1 mask with alpha `[0, 255]` (R3-6), and a fully transparent image or mask contributes nothing.
- [x] Bounds work is bounded: no dash expansion, polygon clipping or per-pixel rectangle is built for bounds, every builder pool and per-state copy is charged by its capacity before it grows, and glyph limits are checked before reservation. Done when: `PdfDisplayList.TinyDashPatternsRecordInBoundedTime` (R3-1) records `[1e-13, 1e-13]` and other sub-epsilon patterns at once, and `PdfDisplayList.SmallBudgetsRefuseBeforeAllocating` (R3-2) refuses with `ResourceLimit` under small `max_total_bytes` for clips, saved states, glyph runs and command reservations with the builder's heap use under the budget.
- [x] Per-command painted bounds: `DisplayList::command_bounds()` gives each draw command's clipped page-space box (or none), so `D05 T01 §4` can leave out a full-page white background fill. Done when: `PdfDisplayList.CommandBoundsPerDraw` checks a white full-page fill and inset content separately.
- [x] Page geometry convention: `page_box_pt` is the page's crop box in unrotated default user space as the caller passes it (non-zero origins kept; `/Rotate` applied by the consumer; `/UserUnit` in `§21`), and `content_bounds_pt` is in the same space. Done when: `PdfDisplayList.PageBoxConvention` covers a blank page, a shifted page box and content partly outside the box.
- [x] Dash patterns per ISO 32000-1 section 8.4.3.6: an empty array is solid, a negative entry is refused, a non-empty all-zero array is recorded as solid (Acrobat behaviour) instead of refused, odd lengths repeat, and the phase is recorded unchanged. Done when: `PdfDisplayList.DashPatternRecording` covers each case.
- [x] `finish()` control-block allocation failure (round 3 record finding): the injection fails the `shared_ptr` control block itself and publishes nothing. Done when: `PdfDisplayList.FinishControlBlockFailurePublishesNothing` passes and fails if the injection point moves before the storage move.
- [x] Invisible content: alpha 0 fills and strokes, `Tr 3` and `Tr 7` runs, and fully clipped primitives contribute no bounds. Done when: `PdfDisplayList.InvisibleContentHasNoBounds` passes.
- [x] Malformed records are refused with typed errors and nothing is recorded: verb and point counts that disagree, non-finite points, matrices, widths and alphas, out-of-range enum values and path indices, a `restore_state` with no saved state, and bitmap dimensions, strides and buffer lengths that overflow or disagree (padded rows accepted). Done when: `PdfDisplayList.MalformedRecordsRefused` covers each case and checks the command count is unchanged.
- [x] Sealing and ownership: mutation or a second `finish()` after sealing returns `InternalError`, the builder can be destroyed while lists are read (`PdfDisplayList.OneShotFinishSemantics`), and `PdfDisplayList.ConcurrentRead` compares every reader's walk with the expected payloads (command kinds, pool ranges, bitmap identity). Done when: both tests pass under ASan.
- [x] Track active `DisplayList` instances via an atomic counter `recto::pdf::debug_live_display_lists()` so `§8` and `§10` can assert zero leaked display lists on shutdown. Done when: constructing and destroying `DisplayList` increments and decrements the counter to `0`.
- [x] Verify concurrent lock-free reading of one `std::shared_ptr<const DisplayList>` across 8 threads in `PdfDisplayList.ConcurrentRead`. Done when: 8 threads iterating 10,000 commands simultaneously pass under ASan.
- [x] Commit: `pdf: add immutable DisplayList command buffer and DisplayListBuilder`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfDisplayList.*` passes under ASan with 0 warnings.

> **Verified:** 2026-10-24 | §1 | 753ff65d3dcbc478e8635940509dd2faf0a86908
> **Review:** `docs/reviews/d01-t02-s1.json`

## 2. DirectWrite in-memory font loader, `LoadedFont`, glyph metrics and `glyf` fallback

> **Started:** 2026-10-24T12:38Z

> **Corrected 2026-10-24:** split before the build (AGENTS.md: small, single-responsibility sections). The section had six large items, one of which could not be built here: Type 3 `/CharProcs` are content streams and need the interpreter of `§3` and `§4`. It now covers loading and glyph metrics only; the Standard 14 metrics moved to `§27`, encodings and `/ToUnicode` to `§28`, Type 1 and CFF charstrings to `§29`, CID fonts to `§30` and Type 3 to `§31`. Two facts the old text missed: DirectWrite cannot load a bare CFF program (`/FontFile3 /Type1C` or `/CIDFontType0C`; `§29` reads those), and it refuses many embedded TrueType subsets that lack tables it requires (`OS/2`, `name`, `post`), which every other reader tolerates, so this section reads `glyf` outlines itself when DirectWrite refuses a font. Windows 11 provides the in-memory loader (`IDWriteFactory5::CreateInMemoryFontFileLoader`), so no custom `IDWriteFontFileLoader` is written. **Corrected 2026-10-24 (build):** that was wrong. The built-in loader keeps every font program it was ever given until the loader itself is released (`IDWriteInMemoryFontFileLoader::GetFileCount` counts every reference it created), so one kept for the process would hold the fonts of every document a user opened. The custom loader the section first named is built: one `IDWriteFontFileLoader` registered for the process, whose 64-bit keys name entries in a registry of weak references, streams that hold a font's bytes while DirectWrite reads them, and each entry removed when its font is destroyed. `dwrite.lib` is linked by `#pragma comment(lib, "dwrite.lib")` in `src/pdf/font.cpp`, as `filters.cpp` links WIC, so every consumer of the static library links it.

> **Corrected 2026-10-24 (plan review, `docs/reviews/01-engine/D01-T02-s2.md`):** six gaps folded in: `hhea.numberOfHMetrics` read for `hmtx`; an OpenType font with CFF outlines loaded and measured through DirectWrite; outline geometry proven here and its replay given to `§6`; composite expansion bounded per call; fixtures for both `loca` formats, implied on-curve points and transformed and point-attached composites against hand-computed geometry; loader lifetime, injected creation failures and the error codes named. Outlines are computed per call from the immutable bytes (no cache), so a font retains no expanded geometry.

Load embedded font programs into immutable `LoadedFont` objects in `src/pdf/font.h` and `src/pdf/font.cpp` (ISO 32000-1 sections 9.6.3, 9.8 and 9.9) that the interpreter (`§4`) asks for glyph advances, ink boxes and outlines, and that the rasterizer (`§6`) draws through DirectWrite or as paths.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `IDWriteFactory` is created once with `DWRITE_FACTORY_TYPE_SHARED`, that each font's bytes outlive its `IDWriteFontFace` through the owner object passed to `CreateInMemoryFontFileReference`, and that a `LoadedFont` has only `const` accessors and no lazy caches, so `RenderPool` workers can draw it concurrently.

**Build order:**

1. Author `src/pdf/font.h` and `src/pdf/font.cpp` with `LoadedFont`, the shared factory and the in-memory loader, linking `dwrite.lib` in `src/pdf/recto-pdf.vcxproj`. Done when: both compile cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/pdf_font_tests.cpp` with the tests named below. Done when: `PdfFont.*` passes under ASan.

- [x] Define the immutable `LoadedFont` in `src/pdf/font.h`: the font program bytes (`std::shared_ptr<const std::vector<uint8_t>>`), its kind (`TrueType`, `OpenType`, `OutlineOnly`), `units_per_em`, glyph count, and `const` queries `advance(glyph)` and `ink_box(glyph)` in glyph units, plus `outline(glyph)` as `RecordedPath`-ready verbs and points for the path renderer. Done when: `PdfFont.LoadedFontIsImmutable` checks the type has no non-`const` member function and no `mutable` field, and two threads querying one font get identical results.
- [x] Load `/FontFile2` (TrueType) and `/FontFile3 /Subtype /OpenType` through one process-wide `IDWriteFactory5` (`DWRITE_FACTORY_TYPE_SHARED`) and one custom `IDWriteFontFileLoader`, registered with the factory once on first use and kept registered for the process (it is never unregistered while a face could exist), whose `CreateCustomFontFileReference` keys name a registry entry for the font's shared bytes, so nothing is written to disk and nothing is kept after the font is destroyed. Done when: `PdfFont.InMemoryDirectWriteLoader` loads an embedded TrueType buffer and an OpenType buffer with CFF outlines, resolves glyph indices, design-unit advances and ink boxes (`GetDesignGlyphMetrics`) and non-empty outlines (`GetGlyphRunOutline` into a geometry sink) for each, draws on a face after the caller dropped its copy of the bytes, and the owner is released once the last face is. Cheaper substitute: GDI `AddFontMemResourceEx`, or a temporary file per font.
- [x] Creation failures are typed and told apart: a program DirectWrite rejects as a font (`DWRITE_E_FILEFORMAT`, `DWRITE_E_UNSUPPORTEDOPERATION`, `E_INVALIDARG` on the face) goes to the `glyf` fallback or, with no `glyf`, returns `DamagedFormat`; `E_OUTOFMEMORY` and `std::bad_alloc` return `OutOfMemory`; any other `HRESULT` from the factory, loader, reference or face returns `InternalError` with the step and the `HRESULT` kept (`D01 T02 §25`). Done when: `PdfFont.LoaderFailuresAreTyped` injects each failure at each of the four steps and checks the code and that no fallback is tried after a resource failure. Cheaper substitute: treating every failure as a bad font.
- [x] **Corrected 2026-10-24 (build):** the premise that DirectWrite refuses subsets lacking `OS/2`, `name` or `post` is false on Windows 11: a probe (`docs/reviews/01-engine/D01-T02-s2.md`, writer note) found it accepts missing or broken `OS/2`, `name`, `post`, wrong checksums, an unsorted directory and the `true` tag, and refuses a `cmap` whose header claims more subtables than it holds and an `hhea.numberOfHMetrics` past the glyph count. The fixture is a subset with the broken `cmap` (no `OS/2`, `name` or `post`), and the reader clamps `numberOfHMetrics` to the glyph count so that font loads too. When DirectWrite refuses a TrueType program, read its `head` (`unitsPerEm`, `indexToLocFormat`), `maxp`, `hhea` (`numberOfHMetrics`), `hmtx` (advances, the last advance repeated for later glyphs, trailing left side bearings), `loca` (short and long formats) and `glyf` tables directly, simple glyphs (implied on-curve points between off-curve points, quadratic contours converted exactly to cubic verbs) and composite glyphs (offsets, `ARGS_ARE_XY_VALUES` or point-matched anchors, scale, x/y scale and 2 by 2 transforms), and load it as `OutlineOnly`. Done when: `PdfFont.RefusedTrueTypeFallsBackToGlyfOutlines` loads a subset with no `OS/2`, `name` or `post` table and fewer `hmtx` records than glyphs, and matches hand-computed advances and outlines for a short- and a long-`loca` fixture, an empty glyph with a non-zero advance, a contour of only off-curve points, a transformed composite and a point-attached composite. Replaying `OutlineOnly` glyphs as filled paths belongs to `§6`. Cheaper substitute: failing the font and drawing every glyph in a system face.
- [x] Glyph advances and ink boxes in the `§1` convention: glyph units scaled by `1 / units_per_em` into text space, so the interpreter multiplies by the font size and `PositionedGlyph::ink_bounds_pt` follows `D01 T02 §1` (text-space points with the font size applied). Done when: `PdfFont.GlyphMetricsInTextSpace` checks advances and ink boxes against the font's `hmtx` and `glyf` boxes, including a glyph with a negative left side bearing and one that overhangs its advance.
- [x] Hostile font programs are refused with typed errors and bounded work: a program larger than `kMaxDecodedStreamBytes` (`ResourceLimit`); table offsets or lengths outside the buffer, `loca` offsets that run backwards or past `glyf`, and a component naming a glyph past `numGlyphs` (`DamagedFormat`); composite nesting past 16 levels, more than 4,096 component visits or 65,535 expanded points in one `outline()` call, counted before each expansion, and more than 65,535 points in a simple glyph (`ResourceLimit`). Done when: `PdfFont.HostileFontProgramsRefused` covers each case, including a compact font whose shared components stay within the per-glyph nesting limit but exceed the visit and point budgets, without a crash under ASan and with the code named here. Cheaper substitute: trusting DirectWrite to reject them.
- [x] Commit: `pdf: load embedded TrueType and OpenType fonts in memory, with a glyf fallback`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFont.*` passes under ASan with 0 warnings.

> **Verified:** 2026-10-24 | §2 | 25694b1ae97d7a52982d44ea761474dc2108b8d1
> **Review:** `docs/reviews/d01-t02-s2.json`

## 3. Content-stream interpreter: graphics state, paths, painting, clipping and device colour

> **Started:** 2026-10-24T15:11Z

> **Corrected 2026-10-24:** split before the build (AGENTS.md: small, single-responsibility sections). Two items could not be built here: `sh` and pattern fills need the `FillShading` and `FillTilingPattern` commands `§16` adds (moved to `§16`), and the `/ExtGState /SMask` belongs to `§15`, which already parses it. The colour spaces beyond the device ones moved to `§32` (CalGray, CalRGB, Lab, `ICCBased`, calibrated CMYK and image profiles) and `§33` (`Indexed`, `Separation`, `DeviceN` and PDF functions). This section is the interpreter's core.

> **Corrected 2026-10-24 (plan review, `docs/reviews/01-engine/D01-T02-s3.md`):** five gaps folded in: the entry point's empty-page and failure contract, `cm` order proven on recorded output, clipping order (paint with the clip in force, then intersect the pending clip), closing operators, fill rules and path clearing proven separately, and colour-space names resolved through the resources, with `/DefaultGray`, `/DefaultRGB` and `/DefaultCMYK` given to `§32`.

Interpret a page's content stream into a `DisplayList` (`D01 T02 §1`) in `src/pdf/interpreter.h` and `src/pdf/interpreter.cpp` (ISO 32000-1 sections 7.8 and 8): operator dispatch over the lexer's tokens, the graphics state, paths, painting, clipping and the device colour spaces. Text, images and Form XObjects are `§4`, the ceilings on hostile streams `§19`.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `Interpreter` mutates only its own graphics state stack and its `DisplayListBuilder`, never the `pdf::Document`'s objects or resource dictionaries, and runs on the `DocumentActor` thread that owns the document.

**Build order:**

1. Implement `src/pdf/interpreter.h` and `src/pdf/interpreter.cpp`: `interpret_page` over `Document::decoded_page_contents` and `page_resources`, the operand stack, operator dispatch, and the graphics-state, path and device-colour operators. Done when: `interpreter.cpp` compiles cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/pdf_interpreter_tests.cpp` with the tests named below. Done when: they pass under ASan.

- [x] `interpret_page(const Document&, size_t page_index)` returning `PdfResult<std::shared_ptr<const DisplayList>>`, reading the page's decoded contents and resources and the page box (`D01 T02 §1` convention, `/CropBox` in default user space), with an operand stack of numbers, names, strings, arrays and dictionaries from the lexer and parser; an unknown operator is skipped with its operands, an operator given the wrong operands is skipped, and neither stops the page (ISO 32000-1 7.8.2, Annex A). Done when: `PdfInterpreter.OperatorDispatch` interprets a stream mixing known, unknown and malformed operators and records exactly the well-formed ones, and `PdfInterpreter.EntryContract` checks that a page with no or empty `/Contents` gives a valid empty `DisplayList`, an index past the last page and a content stream that fails to decode return their typed errors, and a builder failure (an injected `DisplayListLimits` refusal) is returned rather than a partial page reported as success.
- [x] The graphics state stack: `q` saves (at most 64 deep; a `q` past that is ignored and its matching `Q` too, so the state stays balanced), `Q` restores and ignores underflow, `cm` concatenates the CTM. Done when: `PdfInterpreter.GraphicsStateStack` checks restoration of every field, the 64-level cap with balanced recovery, an unmatched `Q`, and `cm` order on recorded output: a translation then a scale against the reverse order, and a rotation, each checked through the recorded path and clip bounds, and undone by `Q`.
- [x] Stroke parameters and `/ExtGState` through `gs`: `w`, `J`, `j`, `M`, `d [array] phase` (ISO 32000-1 8.4.3.6, recorded per `D01 T02 §1`), `ri` and `i` kept in the state, and `/LW`, `/LC`, `/LJ`, `/ML`, `/D`, `/CA`, `/ca` and `/BM` from the named dictionary (an unknown `/BM` name is `Normal`; `/SMask` is `§15`). Done when: `PdfInterpreter.ExtGState` records the stroke styles and alphas into the `DisplayList` and an `/ExtGState` missing from the resources is skipped.
- [x] Path construction `m`, `l`, `c`, `v`, `y`, `h`, `re`, with `v` and `y` expanded to full cubics and a segment without a current point skipped. Done when: `PdfInterpreter.PathOperators` records exact control points for each operator.
- [x] Painting and clipping `S`, `s`, `f`, `F`, `f*`, `B`, `B*`, `b`, `b*`, `n`, with a pending `W` or `W*` applied when the path is painted or ended (ISO 32000-1 8.5.4), then the path cleared. The paint uses the clip in force, and the pending clip is intersected after it, so `W S` keeps the stroke outside the new clip and only later painting is bounded by it. Done when: `PdfInterpreter.ClippingPaths` checks that `W n` pushes a clip without painting, `W S` records the `DrawPath` before the `PushClipPath` and a later fill is bounded by the clip, and `W* f` clips with the even-odd rule; and `PdfInterpreter.PaintingOperators` checks that `s`, `b` and `b*` close the path, that `f` and `f*` record the non-zero and even-odd rules on a two-subpath path, that each operator records the fill and stroke it names, and that painting and `n` clear the path so it is never painted twice.
- [x] Device colour: `g`/`G`, `rg`/`RG`, `k`/`K`, and `cs`/`CS` with `sc`/`SC`/`scn`/`SCN` for `DeviceGray`, `DeviceRGB` and `DeviceCMYK`, with the initial colour of each space and DeviceCMYK converted by ISO 32000-1 10.4.2 (calibrated conversion is `§32`); a colour space named in `/ColorSpace` resources resolves to the space it names (through inherited resources and indirect references), and a space this section does not read leaves black, noted for `§32` and `§33`. Done when: `PdfInterpreter.DeviceColors` checks each operator's recorded colour, the initial colours after `cs`, and `/C1 cs` with `/ColorSpace << /C1 /DeviceRGB >>` behaving as `/DeviceRGB cs`.
- [x] Commit: `pdf: interpret content-stream graphics state, paths, painting, clipping and device colour`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.OperatorDispatch:PdfInterpreter.EntryContract:PdfInterpreter.Graphics*:PdfInterpreter.ExtGState:PdfInterpreter.Path*:PdfInterpreter.Clip*:PdfInterpreter.PaintingOperators:PdfInterpreter.DeviceColors` passes under ASan with 0 warnings. **Corrected 2026-10-24:** the filter now names `EntryContract` and `PaintingOperators`, which the plan review added and the earlier filter did not match.

> **Verified:** 2026-10-24 | §3 | 4253e3bae1fab95c230b7b4b4b5f630d0ab8c045
> **Review:** `docs/reviews/d01-t02-s3.json`

## 4. Content-stream text positioning, `/Subtype /Image`, inline images and `/Form` XObjects

> **Groomed 2026-10-23:** added `D06 T05 §32` (JPX per-component decode) and `D06 T05 §33` (DCT `/ColorTransform`, Adobe CMYK and YCCK) to `Depends On`: `/Do` image decoding renders wrong colours and sizes until those decoder repairs ship, and the corpus sweep below would pin the wrong pixels.

Complete `src/pdf/interpreter.cpp` with text state and positioning operators, image XObjects, inline images, and recursive Form XObjects (ISO 32000-1 sections 8.8, 8.9, and 9):
- Text state and positioning (`BT` begin text, `ET` end text, `Tc` char spacing, `Tw` word spacing for ASCII `0x20`, `Tz` horizontal scaling, `TL` leading, `Tf` font and size, `Tr` rendering mode `0..7` including `3` invisible text and `4..7` text clipping, `Ts` text rise, `Td`, `TD`, `Tm`, `T*`, `Tj`, `TJ` with thousandths-of-em adjustments, `'`, `"`).
- External `/Do` operator:
  - `/Subtype /Image`: decode pixel buffer via `D01 T01 §7-§8`, apply `/ImageMask` stencil tinting with current fill colour, `/Mask` explicit mask or colour-key range, `/SMask` soft-mask alpha channel, and `/Interpolate` flag, and record `DrawImage` transformed by the `1x1` unit-square `CTM`.
  - `/Subtype /Form`: save state, apply `/Matrix` and `/BBox` clip, resolve `/Resources` (falling back to parent page resources), and recursively interpret the Form XObject stream with a strict **recursion cap of 32 levels** to stop cyclic Form XObjects (`F1 -> F1`).
- Inline images (`BI ... ID <bytes> EI`): map abbreviated keys (`W`/`Width`, `H`/`Height`, `BPC`/`BitsPerComponent`, `CS`/`ColorSpace` `G`/`RGB`/`CMYK`/`I`, `F`/`Filter` `AHx`/`A85`/`LZW`/`Fl`/`RL`/`CCF`/`DCT`, `DP`/`DecodeParms`, `IM`/`ImageMask`) and decode into `DrawImage`.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that decoded image bitmaps are stored as `std::shared_ptr<const DecodedBitmap>` in `DisplayList` and recursive `/Subtype /Form` interpretation enforces the 32-level depth cap without mutating shared `Document` dictionaries.

**Build order:**

1. Implement text state/positioning/showing (`BT`/`ET`/`Tf`/`Td`/`TD`/`Tm`/`T*`/`Tj`/`TJ`/`'`/`"`), `/Do` (`/Image` with `/SMask`/`/ImageMask` and `/Form` with depth-32 cap), inline images (`BI`/`ID`/`EI`), and `BDC`/`EMC` optional-content layer visibility filtering in `src/pdf/interpreter.cpp`. Done when: `interpreter.cpp` compiles cleanly on `x64` and `ARM64`.
2. Extend `tests/recto-tests/pdf_interpreter_tests.cpp` testing all `tests/corpus/render/` files, `TJ` kerning adjustments, `/Image` + `/SMask`, `BI`/`ID`/`EI` inline images, and cyclic `/Form` XObject (`F1 -> F1`) recursion protection. Done when: `PdfInterpreter.*` passes 100% under ASan.

- [ ] Implement text state operators (`BT`, `ET`, `Tc`, `Tw`, `Tz`, `TL`, `Tf`, `Tr`, `Ts`) and text matrix positioning (`Td`, `TD`, `Tm`, `T*`). Done when: `PdfInterpreter.TextPositioning` computes exact user-space glyph positions across multi-line `Td`/`T*` sequences.
- [ ] Implement text-showing operators (`Tj`, `TJ`, `'`, `"`) computing per-glyph horizontal/vertical advances: `tx = ((w0 - TJ_num / 1000.0) * font_size + Tc + (char == 0x20 ? Tw : 0)) * (Tz / 100.0)`. Done when: `PdfInterpreter.TjKerningAndSpacing` places glyphs at exact ISO 32000-1 section 9.4.4 coordinates and skips drawing when `Tr == 3` (invisible OCR text).
- [ ] Implement `/Do` for `/Subtype /Image` XObjects supporting 1/2/4/8/16-bit components, `/Indexed` palettes, `/ImageMask true` (painting 1-bit stencil masks in the current non-stroking colour), explicit `/Mask`, and grayscale `/SMask` alpha compositing. Done when: `render/images-and-vectors.pdf` decodes and records all embedded JPEG, Flate+SMask, and CCITT mask images.
- [ ] Implement inline image parsing (`BI ... ID <data> EI`) expanding abbreviated dictionary keys (`W`, `H`, `BPC`, `CS`, `F`, `DP`, `IM`, `I`) and locating the `EI` delimiter safely via expected decoded row size + filter length. Done when: `PdfInterpreter.InlineImages` decodes `A85`, `Fl`, and raw inline images without misidentifying `EI` bytes inside binary pixel data. **Corrected 2026-10-24:** `D01 T02 §3` already parses the dictionary and frames the data (by length unfiltered, by decoding to the full sample size filtered, a 64 MiB work budget, CR LF after `ID`) and refuses an image whose end it cannot verify; this item decodes the image on that framing and keeps the rule that no `EI` is taken on trust (the reviews of `§3` showed every printable-bytes heuristic runs pixels as operators).
- [ ] Implement `/Do` for `/Subtype /Form` XObjects (pushing graphics state, concatenating `/Matrix`, clipping to `/BBox`, binding `/Resources` with parent fallback, and enforcing the **32-level recursion ceiling**). Done when: `PdfInterpreter.FormXObjectCycle` stops a self-referencing `F1 -> F1` Form XObject at depth 32 and returns cleanly under ASan. Cheaper substitute: unbounded recursive `interpret()` calls that overflow the stack on cyclic Form XObjects.
- [ ] Record optional-content marked-content blocks (`BDC` / `BMC` ... `EMC` with `/OC` property references) so `D02 T03 §7` can toggle OCG layer visibility without re-parsing the PDF file. Done when: hidden OCG groups are skipped when an OCG visibility predicate marks them off.
- [ ] Interpret every page of `tests/corpus/render/` (`simple-1p.pdf`, `compressed-xref-2p.pdf`, `incremental-2rev.pdf`, `rotated-pages.pdf`, `images-and-vectors.pdf`, and unlocked `encrypted-*.pdf`) into `DisplayList`s with zero errors under ASan. Done when: `PdfInterpreter.CorpusRenderSweep` passes 100%.
- [ ] Commit: `pdf: add content-stream text operators, image XObjects, inline images and Form XObjects`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.*` passes under ASan across all corpus render and broken files.

## 5. Incremental PDF revision writer (`src/pdf/incremental_writer.cpp`)

> **Corrected 2026-10-24:** split before the build. The engine had no object serializer, no Flate encoder and no encryption path (only `decode_flate`, `SecurityHandler::decrypt_bytes`), so this section would have built three shared modules inside a revision writer. They are `§34` (the Flate compressor), `§35` (the `PdfValue` serializer) and `§36` (encrypting new objects); `D04 T02 §2`'s `PdfWriter` consumes the same three instead of building its own. This section is the revision writer on top of them.

Implement `src/pdf/incremental_writer.h` and `src/pdf/incremental_writer.cpp` (`recto::pdf::IncrementalWriter`, foundation plan section 4.1 item 6):
- Append a non-destructive PDF revision to the end of an existing PDF byte buffer (preserving the original bytes `0..original_size` untouched so existing cryptographic digital signatures `/ByteRange` remain valid when adding a counter-signature or filling allowed form/annotation fields).
- Allocate new object numbers (`>= prev_size`) and write modified indirect objects (`N G obj ... endobj` through `§35`'s serializer, with `FlateDecode` compression from `§34` for modified streams that carry no filter, and `§36`'s encryption matching the document's active `SecurityHandler` when encrypted).
- Write a new `xref` subsection (or `/Type /XRef` stream when the source file uses cross-reference streams) chained to the previous `startxref` via `/Prev`, updated `/ID [<id0> <new_id1>]`, and `startxref` / `%%EOF`.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `IncrementalWriter` produces a new buffer or writes to a destination stream without mutating the source buffer in place, and re-encrypts modified objects with the document's active encryption key when the source PDF is encrypted.

**Build order:**

1. Author `src/pdf/incremental_writer.h` and `src/pdf/incremental_writer.cpp` supporting modified object replacement, new object allocation, classic `xref` and `/Type /XRef` incremental trailer chaining (`/Prev`), and encrypted-document object encryption. Done when: `incremental_writer.cpp` compiles cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/pdf_incremental_writer_tests.cpp` testing single and multi-revision incremental updates on classic `xref`, compressed `/Type /XRef`, and `AES-256` encrypted corpus PDFs. Done when: `PdfIncrementalWriter.*` passes under ASan.

- [ ] Preserve bytes `0..original_eof` of the source PDF byte-for-byte when appending an incremental revision. Done when: `std::memcmp(output.data(), source.data(), source.size()) == 0` in `PdfIncrementalWriter.BytePrefixPreserved`. Cheaper substitute: full-file rewrite that invalidates existing digital signatures (`/ByteRange`).
- [ ] Track dirty/modified indirect objects (`set_object(IndirectRef, PdfValue)`) and newly allocated objects (`allocate_object(PdfValue)` starting at `max_object_id + 1`). Done when: unmodified objects are not duplicated in the appended revision.
- [ ] Emit sorted `xref` subsections covering only the modified and newly allocated object IDs (plus `0 1` free head) and a `trailer` dictionary with `/Prev <old_startxref>`, updated `/Size`, and updated `/ID[1]`. Done when: `Document::open_memory` on the incremental output resolves both the modified objects and all untouched objects from the base revision.
- [ ] Emit a `/Type /XRef` cross-reference stream (instead of an ASCII `xref` table) when the source PDF's previous revision used a `/Type /XRef` stream (required by ISO 32000-1 section 7.5.8.4 for pure XRef-stream files). Done when: `PdfIncrementalWriter.CompressedXrefFile` updates `render/compressed-xref-2p.pdf` and re-opens cleanly.
- [ ] Encrypt modified strings and streams in the appended revision through `§36`'s `SecurityHandler::encrypt_object_values` (`RC4`, `AES-128`, or `AES-256` `R=6`) when the base document is encrypted, leaving the cross-reference stream and the trailer `/ID` unencrypted (ISO 32000-1 7.5.8.1, 7.6.1). Done when: `PdfIncrementalWriter.EncryptedDocument` incrementally updates `render/encrypted-aes-256.pdf` and re-opens it with `"recto-test"`.
- [ ] Support chaining 3 consecutive incremental revisions (`Rev 1 -> Rev 2 -> Rev 3`) and verify `Document::open_memory` sees the latest state from each revision. Done when: `PdfIncrementalWriter.MultiRevisionChain` passes under ASan.
- [ ] Commit: `pdf: add IncrementalWriter for non-destructive PDF revisions`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfIncrementalWriter.*` passes under ASan.

## 6. Per-worker Direct2D / WIC `DisplayList` tile rasterizer (`src/pdf/rasterizer.cpp`)

Implement `src/pdf/rasterizer.h` and `src/pdf/rasterizer.cpp` (`recto::pdf::Rasterizer`):
- Each `Rasterizer` instance owns its own thread-local/per-worker `IWICImagingFactory`, `ID2D1Factory1` (`D2D1_FACTORY_TYPE_MULTI_THREADED`), and reusable WIC bitmap render target (`GUID_WICPixelFormat32bppPBGRA`).
- Given a `std::shared_ptr<const DisplayList>`, a target `TileSpec` (`dpi_scale`, `rotation_deg` `0/90/180/270`, `tile_x_px`, `tile_y_px`, `tile_width_px`, `tile_height_px`, `background_rgba`), and a caller-supplied premultiplied BGRA output buffer + stride:
  - Compute the exact PDF user-space (bottom-left origin, `1/72"` points) to tile pixel-space (top-left origin) affine matrix `M = Translate(-tile_x_px, -tile_y_px) * Scale(dpi / 72.0) * RotateAndFlipPageBox(page_box, rotation_deg)`.
  - Replay `DisplayList` commands (`SaveState`/`RestoreState` with D2D layers/axis-aligned clips, `DrawPath` via `ID2D1PathGeometry`, `DrawGlyphRun` via `ID2D1RenderTarget::DrawGlyphRun` with `DWRITE_MEASURING_MODE_NATURAL`, and `DrawImage` via `ID2D1Bitmap`).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that each `Rasterizer` instance is owned by a single worker thread (never shared concurrently across threads) and reads `const DisplayList` without mutating it.

**Build order:**

1. Implement `src/pdf/rasterizer.h` and `src/pdf/rasterizer.cpp` (page-to-tile matrix math for `0`, `90`, `180`, `270` rotations, D2D path/clip/layer stack, `DrawGlyphRun`, `DrawImage`, and pixel copy to caller buffer). Done when: `rasterizer.cpp` compiles cleanly on `x64` and `ARM64` linking `d2d1.lib` and `windowscodecs.lib`.
2. Add `tests/recto-tests/pdf_rasterizer_tests.cpp` verifying non-blank rendering of all `tests/corpus/render/` pages and pixel-for-pixel equality between a full-page render and stitched `512 x 512` tiles across `0`, `90`, `180`, and `270` degree rotations. Done when: `PdfRasterizer.TileStitchIdentity` passes under ASan.

- [ ] Compute the exact affine transform mapping PDF page coordinates (`[llx lly urx ury]`, Y-up) and page `/Rotate` + view rotation (`0`, `90`, `180`, `270`) into tile pixel coordinates (`[tile_x, tile_y, tile_w, tile_h]`, Y-down). Done when: all 4 rotations place page corners at the exact expected pixel corners in `PdfRasterizer.RotationTransforms`.
- [ ] Reuse the worker's `IWICBitmap` and `ID2D1RenderTarget` when `tile_width_px == 512 && tile_height_px == 512` so rendering thousands of `512 x 512` tiles does not re-create D2D render targets per tile. Done when: rendering 100 tiles reuses a single `ID2D1RenderTarget` allocation per worker.
- [ ] Replay `RecordedPath` geometries (`MoveTo`, `LineTo`, `CubicBezierTo`, `CloseSubpath`), stroke styles (`ID2D1StrokeStyle` cap, join, miter, dash array), and `NonZero` / `EvenOdd` fills and clips (`PushAxisAlignedClip` for rectangular clips, `PushLayer` with `geometricMask` for arbitrary vector clips). Done when: stroked, filled, and clipped vector shapes in `render/images-and-vectors.pdf` rasterize accurately.
- [ ] Replay `RecordedGlyphRun` commands via `ID2D1RenderTarget::DrawGlyphRun` (applying the combined glyph-to-device transform `Tm * CTM * PageToTile` and rendering Type 3 procedural glyph sub-lists). Done when: embedded and Standard 14 text on `render/simple-1p.pdf` rasterizes crisp anti-aliased glyphs.
- [ ] Call `LoadedFont::check_glyph` before drawing a TrueType glyph through DirectWrite and skip a glyph it refuses, recording the refusal (`D01 T02 §2`). Done when: `PdfRasterizer.GlyphBudgetsBeforeDirectWrite` draws a page with a glyph from `fonts/truetype-hostile-budgets.ttf` past its budgets without DirectWrite expanding it. **Corrected 2026-10-24:** added from the `D01 T02 §2` completion review (gap 4).
- [ ] Replay glyph runs whose `LoadedFont` is `OutlineOnly` (`D01 T02 §2` `glyf` fallback, `§29` Type 1 and CFF) as filled paths from `LoadedFont::outline`, under the run's text matrix and CTM, with the same render-mode handling as DirectWrite runs. Done when: `PdfRasterizer.OutlineOnlyGlyphRuns` renders a `glyf`-fallback and a Type 1 run to golden images within the SSIM threshold of the DirectWrite rendering of the same glyphs. **Corrected 2026-10-24:** added from the `D01 T02 §2` plan review (gap 3).
- [ ] Replay `DrawImage` commands (`ID2D1Bitmap` created from `DecodedBitmap`, honoring `/Interpolate` via `D2D1_BITMAP_INTERPOLATION_MODE_LINEAR` vs `NEAREST_NEIGHBOR` and `/SMask` opacity masks). Done when: JPEG, Flate+SMask, and CCITT images in `render/images-and-vectors.pdf` render at their exact page rectangles.
- [ ] Verify that rendering a page as a single full-page bitmap vs rendering and stitching `512 x 512` tiles produces **pixel-identical** RGBA output (`expect_rgba_equal` with `max_delta = 0`) across `0`, `90`, `180`, and `270` rotations. Done when: `PdfRasterizer.TileStitchIdentity` passes on `simple-1p.pdf` and `images-and-vectors.pdf`. Cheaper substitute: rendering the entire page at high zoom and cropping out a `512 x 512` rect, which explodes memory at 400-800% zoom.
- [ ] Commit: `pdf: implement Direct2D DisplayList tile rasterizer with seam-free tile stitching`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfRasterizer.Rotation*:PdfRasterizer.TileStitch*` passes under ASan.

## 7. Cooperative `CancelToken` and sub-10 ms mid-replay tile cancellation

Implement `CancelToken` and `CancelSource` in `src/pdf/cancel_token.h` (`recto::pdf::CancelToken`, re-exported into `recto::core` via `src/core/cancel_token.h` so `src/pdf/` never includes upward from `src/core/`) and wire cooperative cancellation into both `Interpreter` (`DisplayList` compilation) and `Rasterizer::render_tile`:
- `CancelToken`: lightweight, lock-free `std::shared_ptr<std::atomic<bool>>` handle (`CancelSource::token()`, `CancelSource::cancel()`, `CancelToken::is_cancelled()` using `std::memory_order_relaxed`).
- Check `cancel_token.is_cancelled()` at entry and every **64 commands** inside `Rasterizer::render_tile` (and inside `Interpreter::interpret_page`), popping any active D2D clips/layers cleanly, calling `EndDraw()` / discarding partial pixels, and returning `ErrorCode::Cancelled` in under `10 ms` with zero D2D/WIC resource leaks.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that `CancelToken` is defined inside `src/pdf/cancel_token.h` with zero upward includes from `src/core`, uses `std::atomic<bool>` with zero locks, and cancelling mid-replay unwinds the D2D clip/layer stack balanced (`PopAxisAlignedClip` / `PopLayer`) so the worker's `ID2D1RenderTarget` remains valid for the very next tile.

**Build order:**

1. Implement `CancelToken` / `CancelSource` in `src/pdf/cancel_token.h` (with `src/core/cancel_token.h` re-exporting `recto::pdf::CancelToken` and `recto::pdf::CancelSource`) and wire 64-command batch cancellation checks and balanced D2D clip/layer unwinding into `Rasterizer::render_tile` and `Interpreter::interpret_page`. Done when: `src/pdf` and `src/core` compile cleanly on `x64` and `ARM64` with zero upward includes from `src/pdf`.
2. Add `PdfRasterizer.PreCancelledTile`, `PdfRasterizer.MidReplayCancelUnder10Ms`, and `PdfRasterizer.RenderTargetReusableAfterCancel` in `tests/recto-tests/pdf_rasterizer_tests.cpp`. Done when: all 3 tests pass under ASan.

- [ ] Implement `CancelToken` and `CancelSource` in `src/pdf/cancel_token.h` over `std::shared_ptr<std::atomic<bool>>` (`is_cancelled()` using `memory_order_relaxed`, `cancel()` using `memory_order_release`) and re-export them in `src/core/cancel_token.h`. Done when: `CancelToken` is copyable in `O(1)` and safe to poll from any number of worker threads with zero upward header dependencies.
- [ ] Check `cancel_token.is_cancelled()` at the start of `Rasterizer::render_tile` so already-cancelled queued requests return `ErrorCode::Cancelled` in `< 10 microseconds` without touching D2D. Done when: `PdfRasterizer.PreCancelledTile` returns `ErrorCode::Cancelled` immediately.
- [ ] Check `cancel_token.is_cancelled()` every 64 commands inside `Rasterizer::render_tile` and inside sub-list loops (`Type3` glyphs and `/Form` XObjects). Done when: a synthetic 50,000-command vector `DisplayList` cancelled from another thread after command 256 aborts in `< 10 ms` in `PdfRasterizer.MidReplayCancelUnder10Ms`.
- [ ] Unwind all active `PushAxisAlignedClip` and `PushLayer` calls on the worker's `ID2D1RenderTarget` before `EndDraw()` when aborting mid-replay so the render target never enters `D2DERR_PUSH_POP_UNBALANCED`. Done when: `PdfRasterizer.RenderTargetReusableAfterCancel` cancels a clipped tile mid-draw and immediately renders the next tile on the same `Rasterizer` instance with pixel-exact output.
- [ ] Wire `CancelToken` into `Interpreter::interpret_page` so cancelling a document or page compilation aborts cleanly with `ErrorCode::Cancelled`. Done when: `PdfInterpreter.CancelCompilation` passes under ASan.
- [ ] Poll the `CancelToken` in the native JPX decoder (per packet, per code-block, per wavelet level) and in DCT and JBIG2 decoding, returning `Cancelled` with nothing cached. Done when: a cancel during each JPX stage returns `Cancelled` within 10 ms. **Corrected 2026-10-23:** (`D06 T05 §32` completion review: filed here).
- [ ] Commit: `pdf: add cooperative CancelToken with sub-10ms mid-replay tile cancellation`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfRasterizer.*Cancel*` passes under ASan with abort latency `< 10 ms` and clean subsequent tile renders on the same worker.

## 8. `DocumentActor` thread, MPSC queue, 32-page `DisplayList` cache and password state

Implement `src/core/document_actor.h` and `src/core/document_actor.cpp` (`recto::core::DocumentActor`, foundation plan sections 4.2, 4.4):
- One dedicated `std::thread` per open document (`DocumentHandle`), owning the `std::unique_ptr<recto::pdf::Document>` and a bounded **32-page LRU cache** of `std::shared_ptr<const recto::pdf::DisplayList>`.
- Bounded, thread-safe MPSC message queue accepting non-blocking requests (`open`, `authenticate`, `get_description`, `get_display_list`, `get_outline`, `get_page_links`, `get_fonts`, `save_incremental`, `close`) and delivering results via `std::future` or completion callbacks without ever exposing a reference or pointer to `pdf::Document`.
- Password state machine (`Unopened -> Opening -> NeedsPassword -> WrongPassword -> Opened -> Closed`) and Debug atomic `live_documents` counter.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `recto::pdf::Document` is constructed, accessed, and destroyed exclusively on its owning `DocumentActor` thread, only immutable value structs and `std::shared_ptr<const DisplayList>` cross out of the actor, and `live_documents` returns to `0` when all actors close.

**Build order:**

1. Author `src/core/document_actor.h` and `src/core/document_actor.cpp` implementing the MPSC queue, actor thread loop, password state machine, 32-page `DisplayList` LRU cache with cache-hit/miss stats, and deterministic `close()` join. Done when: `document_actor.cpp` compiles cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/actor_tests.cpp` (`Actor.AsyncOpenAndDescription`, `Actor.PasswordStateMachine`, `Actor.DisplayListLruCache`, `Actor.ConcurrentRequests`, `Actor.TwentyDocumentsLifecycle`). Done when: all `Actor.*` tests pass under ASan with `live_documents == 0` and `live_display_lists == 0`.

- [ ] Implement the thread-safe MPSC command queue and dedicated `std::thread` inside `DocumentActor` so every method on `DocumentHandle` enqueues a message and returns immediately without blocking the caller thread. Done when: `std::this_thread::get_id()` inside `pdf::Document` operations is always the actor thread's ID, never the caller's. Cheaper substitute: a mutex around `pdf::Document` called synchronously from the UI thread.
- [ ] Implement the password authentication state machine (`NeedsPassword -> authenticate(wrong) -> WrongPassword -> authenticate("recto-test") -> Opened`) over `render/encrypted-aes-256.pdf`. Done when: `Actor.PasswordStateMachine` drives the full retry sequence asynchronously and retrieves the unlocked `DocumentDescription`.
- [ ] Implement the 32-page `DisplayList` LRU cache inside `DocumentActor`: `get_display_list(page_index)` returns the cached `std::shared_ptr<const DisplayList>` on hit (bumping its recency) and compiles + inserts on miss (evicting the least-recently-used entry when size exceeds 32). Done when: `Actor.DisplayListLruCache` proves 10 repeated requests for page 0 compile once (`cache_hits == 9`) and requesting 40 distinct pages keeps cached count `<= 32`.
- [ ] Expose async queries for `get_description()`, `get_outline()`, `get_page_links(page_index)`, and `get_fonts()` returning immutable value structs. Done when: `Actor.AsyncOpenAndDescription` verifies page count, dimensions, outlines, links, and fonts on `text/two-column-links-outline.pdf`.
- [ ] Increment `debug_live_documents()` on `DocumentActor` open and decrement on actor thread exit, joining the actor thread deterministically on `close()` or destructor. Done when: `Actor.TwentyDocumentsLifecycle` opens and closes 20 actors concurrently and ends with `debug_live_documents() == 0` and `debug_live_display_lists() == 0` under ASan.
- [ ] Drop queued pending requests cleanly with `ErrorCode::Cancelled` when `close()` is called while requests are still in the actor queue. Done when: enqueuing 50 `get_display_list` requests and immediately calling `close()` resolves all 50 futures cleanly without hanging or leaking.
- [ ] Commit: `core: implement DocumentActor thread, 32-page DisplayList LRU cache and password state machine`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Actor.*` passes under ASan with `debug_live_documents() == 0` and `debug_live_display_lists() == 0`.

## 9. `RenderPool` worker threads and 4-tier priority `TileScheduler` with jump cancellation

Implement `src/core/render_pool.h/.cpp` and `src/core/tile_scheduler.h/.cpp` (`recto::core::RenderPool` and `recto::core::TileScheduler`, foundation plan sections 4.3, 4.5, 4.7):
- `RenderPool`: spawns `N = std::clamp(std::thread::hardware_concurrency() - 2u, 2u, 8u)` worker threads (or an explicit `worker_count` override for determinism testing in `D01 T03 §6`). Each worker thread initializes COM (`COINIT_MULTITHREADED`) and owns one dedicated `recto::pdf::Rasterizer` instance for its entire lifetime.
- `TileKey` (`doc_id`, `page_index`, `zoom_bucket_milli`, `rotation_deg`, `tile_x`, `tile_y`) and 4 priority tiers:
  1. `Priority 1 (VisibleCurrentPage)`: visible tiles of the current page at current zoom.
  2. `Priority 2 (VisibleAdjacentPage)`: visible tiles of adjacent pages in continuous/two-page view.
  3. `Priority 3 (PrefetchRing)`: 1-tile prefetch ring around the visible viewport.
  4. `Priority 4 (Thumbnail)`: sidebar/grid page thumbnails (`160 epx` bucket).
- When `TileScheduler::update_viewport(ViewportRequest)` is called with a new scroll position or zoom bucket, any queued or in-flight tile request whose `TileKey` is no longer in the requested set has its `CancelSource::cancel()` tripped immediately, and surviving requests are re-prioritized so Priority 1 tiles always dequeue first.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that `RenderPool` workers never touch `pdf::Document`, each worker uses only its own thread-local `pdf::Rasterizer` and a `std::shared_ptr<const DisplayList>` obtained from `DocumentActor`, and `debug_live_workers()` tracks active worker threads.

**Build order:**

1. Author `src/core/render_pool.h/.cpp` and `src/core/tile_scheduler.h/.cpp` implementing `TileKey`, `TilePriority`, the `RenderPool` priority work queue, `DocumentActor` `DisplayList` acquisition, and automatic `CancelToken` invalidation on `update_viewport`. Done when: `render_pool.cpp` and `tile_scheduler.cpp` compile cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/scheduler_tests.cpp` (`Scheduler.PriorityOrdering`, `Scheduler.ViewportJumpCancelsStaleTiles`, `Scheduler.DeduplicatesInFlightTileKeys`). Done when: all `Scheduler.*` tests pass under ASan.

- [ ] Implement `RenderPool` spawning `std::clamp(hc - 2, 2, 8)` worker threads by default (and accepting an explicit `1..16` worker count override for testing), with each worker owning one `pdf::Rasterizer` instance and tracking `debug_live_workers()`. Done when: `Scheduler.WorkerLifecycle` verifies worker initialization and clean join with `debug_live_workers() == 0`.
- [ ] Define `TileKey` (`doc_id`, `page_index`, `zoom_bucket_milli`, `rotation_deg`, `tile_x`, `tile_y`) with equality and `std::hash<TileKey>` support. Done when: `TileKey` uniquely addresses every `512 x 512` tile across documents, pages, zooms, and rotations.
- [ ] Implement the 4-tier priority queue in `TileScheduler` (`1: VisibleCurrentPage`, `2: VisibleAdjacentPage`, `3: PrefetchRing`, `4: Thumbnail`) with FIFO tie-breaking within each tier. Done when: `Scheduler.PriorityOrdering` submits Thumbnail, Prefetch, Adjacent, and VisibleCurrentPage jobs while workers are paused, resumes workers, and verifies `VisibleCurrentPage` tiles complete before `Thumbnail` tiles. Cheaper substitute: a FIFO queue where scrolling past 50 pages forces the user to wait for all 49 intermediate pages to render first.
- [ ] Deduplicate identical in-flight `TileKey` requests so consecutive `update_viewport` calls overlapping the same visible tiles keep the running work instead of enqueuing duplicate renders. Done when: `Scheduler.DeduplicatesInFlightTileKeys` submits overlapping viewports and renders each unique `TileKey` once.
- [ ] Trip `CancelSource::cancel()` on all queued and in-flight tile tasks whose `TileKey` falls outside the new `ViewportRequest` when `update_viewport` or `cancel_document(doc_id)` is called. Done when: `Scheduler.ViewportJumpCancelsStaleTiles` jumps from page 0 to page 50 and verifies page 0's pending tiles abort with `ErrorCode::Cancelled` while page 50's tiles deliver.
- [ ] Fetch `std::shared_ptr<const DisplayList>` asynchronously from the target `DocumentActor` before enqueuing tile rasterization on `RenderPool` so workers never wait on PDF parsing inside a `RenderPool` slot. Done when: `DisplayList` cache misses happen on the `DocumentActor` thread and `RenderPool` workers only execute `Rasterizer::render_tile`.
- [ ] Commit: `core: implement RenderPool and 4-tier priority TileScheduler with viewport cancellation`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Scheduler.*` passes under ASan.

## 10. `Runtime` coordinator, section 4.8 ordered shutdown sequence and live-object counters

Implement `src/core/runtime.h` and `src/core/runtime.cpp` (`recto::core::Runtime`, foundation plan sections 4.3, 4.8):
- Top-level coordinator owning the `DocumentActor` registry, `TileScheduler`, `RenderPool`, registered conversion/background job handles (`register_job_thread`), and presentation shutdown hooks (`register_presentation_hook`).
- Enforce the exact **6-step ordered shutdown sequence** of foundation plan section 4.8 in `Runtime::shutdown()` / `Runtime::~Runtime()`:
  1. Trip every open `CancelToken` (all in-flight tile renders, actor requests, and jobs).
  2. Close and `join()` every `DocumentActor` thread.
  3. Wait for and `join()` any registered conversion/background job threads.
  4. Drain and `join()` all `RenderPool` worker threads (destroying their thread-local `pdf::Rasterizer` D2D/WIC targets).
  5. Run and `join()` any registered presentation thread shutdown hooks.
  6. Assert `debug_live_documents() == 0`, `debug_live_display_lists() == 0`, and `debug_live_workers() == 0`.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that `Runtime::~Runtime()` executes steps 1 to 6 in strict order, idempotently (`shutdown()` can be called explicitly and then again in `~Runtime()`), and asserts all Debug live-object counters equal `0`.

**Build order:**

1. Implement `src/core/runtime.h` and `src/core/runtime.cpp` with actor management (`open_document`, `close_document`), `TileScheduler` and `RenderPool` access, job/presentation shutdown registration, and the 6-step `shutdown()` sequence. Done when: `runtime.cpp` compiles cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/runtime_tests.cpp` (`Runtime.OrderedShutdownSequence`, `Runtime.ShutdownUnderActiveTileLoad`, `Runtime.IdempotentShutdown`). Done when: 50 iterations of `Runtime.ShutdownUnderActiveTileLoad` pass under ASan with zero deadlocks, zero leaks, and all live counters at `0`.

- [ ] Implement `Runtime` owning the `DocumentActor` map, `RenderPool`, `TileScheduler`, job thread registry, and presentation hook list. Done when: `Runtime::open_document` and `Runtime::close_document` manage actor lifetimes cleanly.
- [ ] Execute Step 1 & Step 2 of `Runtime::shutdown()`: trip all active `CancelSource` tokens across `TileScheduler` and actors, then close and `join()` every `DocumentActor` thread. Done when: `debug_live_documents()` reaches `0` at the end of Step 2.
- [ ] Execute Step 3 & Step 4 of `Runtime::shutdown()`: join registered job threads, then drain and `join()` all `RenderPool` worker threads (releasing all `DisplayList` shared pointers held by in-flight tasks). Done when: `debug_live_workers()` and `debug_live_display_lists()` both reach `0` at the end of Step 4.
- [ ] Execute Step 5 & Step 6 of `Runtime::shutdown()`: invoke registered presentation shutdown hooks in deterministic order and verify `debug_live_documents() == 0 && debug_live_display_lists() == 0 && debug_live_workers() == 0`. Done when: `Runtime.OrderedShutdownSequence` records step timestamps/sequence IDs `[1, 2, 3, 4, 5, 6]` in exact order.
- [ ] Make `Runtime::shutdown()` idempotent so calling `runtime.shutdown()` explicitly before `~Runtime()` runs the teardown once and is a safe no-op on the second call. Done when: `Runtime.IdempotentShutdown` passes under ASan.
- [ ] Stress-test `Runtime::~Runtime()` across 50 iterations while 4 `DocumentActor`s and 8 `RenderPool` workers are actively compiling `DisplayList`s and rasterizing tiles (`Runtime.ShutdownUnderActiveTileLoad`). Done when: all 50 iterations exit cleanly with zero ASan findings and zeroed counters.
- [ ] Commit: `core: implement Runtime coordinator and section 4.8 ordered shutdown sequence`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Runtime.*` passes under ASan across all 50 shutdown-under-load iterations.

## 11. `RenderOptions::line_weight_mode` in the rasterizer and the `TileKey` identity

Acrobat Pro's `Line Weights` view draws every stroke one device pixel wide so dense CAD drawings and maps stay readable; the viewer toggle for it is `D02 T02 §22`, and it needs the engine to support a per-render option. Because the option changes pixels, it must be part of the tile identity the same way the layer state is (`ocg_generation`, which `D02 T03 §7` adds to `TileKey`), or a cached or in-flight tile from the other mode would be shown after a toggle. The option is applied at rasterization time over the same immutable `DisplayList`, so a toggle never recompiles a page on the `DocumentActor`. It must not break the default rendering (an `Actual` render stays byte-identical), seam-free tile stitching (`§6`), 64-command cooperative cancellation (`§7`) or worker-count determinism (`D01 T03 §6`). The Recto improvement it keeps: the toggle is a per-tab view option served from the shared tile cache, and print, export and `recto-cli render` always use `Actual`.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `RenderOptions` is a plain value copied into every `TileSpec` and `TileKey`, that `Rasterizer::render_tile` reads it without mutating the `const DisplayList` (one `DisplayList` serves both modes, with no per-mode mutable cache or lazy state), that `line_weight_mode` takes part in `TileKey` equality and `std::hash<TileKey>` so the scheduler never deduplicates and the texture cache never serves a tile across modes, that the 64-command `CancelToken` polling of `§7` is unchanged in `Uniform`, and that the UI thread only sets the option on a `ViewportRequest` and never rasterizes.

**Build order:**

1. Add `enum class LineWeightMode : uint8_t { Actual, Uniform }` and `struct RenderOptions { LineWeightMode line_weight_mode = LineWeightMode::Actual; }` to `src/pdf/rasterizer.h`, carry them as `TileSpec::options`, and apply `Uniform` inside `Rasterizer::render_tile` to `DrawPath` strokes only. Done when: `PdfRasterizer.UniformLineWeight` passes under ASan.
2. Add `line_weight_mode` to `recto::core::TileKey` and `RenderOptions` to `ViewportRequest` in `src/core/tile_scheduler.h`, and build every `TileSpec` from the request's options. Done when: `Scheduler.LineWeightModeInTileKey` and `Scheduler.LineWeightModeSwitchCancelsStaleTiles` pass under ASan.

- [ ] Define `LineWeightMode` and `RenderOptions` in `src/pdf/rasterizer.h` with `Actual` as the default and add `RenderOptions options` to `TileSpec`. Done when: `PdfRasterizer.RenderOptionsDefault` shows `RenderOptions{}` is `Actual`, and every page of `render/images-and-vectors.pdf` at 150 DPI renders with the same SHA-256 that the test records from the build before the change.
- [ ] In `Uniform`, draw every `DrawPath` stroke (from `S`, `s`, `B`, `B*`, `b` and `b*`) exactly one device pixel wide whatever the CTM, `dpi_scale` and `/LW` are, keeping cap, join and dash pattern, in `src/pdf/rasterizer.cpp` (for example with an `ID2D1StrokeStyle1` created with `D2D1_STROKE_TRANSFORM_TYPE_HAIRLINE`, or a width computed from the device transform). Done when: `PdfRasterizer.UniformLineWeight` renders horizontal strokes of `0` (hairline), `0.25 pt` and `4 pt`, each centred on a device pixel centre, at 18 DPI (25 percent) and 576 DPI (800 percent), and finds exactly one non-background row in the sampled column for each. Cheaper substitute: thinning or blurring finished tiles in a pixel post-process.
- [ ] Leave fills, clips, glyph runs (including stroked text with `Tr` `1`, `2`, `5` and `6`), images and shadings unchanged by `Uniform` in `src/pdf/rasterizer.cpp`. Done when: `PdfRasterizer.UniformLeavesNonStrokesAlone` renders a `DisplayList` holding fills, glyph runs and an image but no path strokes byte-identically in both modes.
- [ ] Keep tiling seam-free in `Uniform` across `0`, `90`, `180` and `270` degree rotations. Done when: `PdfRasterizer.UniformTileStitchIdentity` finds a full-page render and stitched `512 x 512` tiles pixel-identical (`max_delta = 0`) on `render/images-and-vectors.pdf`.
- [ ] Add `uint8_t line_weight_mode` to `recto::core::TileKey` in `src/core/tile_scheduler.h` and include it in `operator==` and `std::hash<TileKey>` exactly as every other identity field is included. Done when: `Scheduler.LineWeightModeInTileKey` shows two keys differing only in `line_weight_mode` compare unequal and hash differently.
- [ ] Carry `RenderOptions` on `ViewportRequest` and copy it into every `TileSpec` and `TileKey` the `TileScheduler` builds, so `update_viewport` with a changed mode trips `CancelSource::cancel()` on queued and in-flight tiles of the old mode (they leave the requested set) and in-flight deduplication never merges keys of different modes. Done when: `Scheduler.LineWeightModeSwitchCancelsStaleTiles` switches the mode while tiles are in flight and every delivered tile's key carries the mode its pixels were rendered with.
- [ ] Commit: `pdf: add the line weight render option and carry it in the tile identity`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfRasterizer.Uniform*:PdfRasterizer.RenderOptions*:Scheduler.LineWeightMode*` passes under ASan, and removing `line_weight_mode` from `std::hash<TileKey>` and `operator==` fails `Scheduler.LineWeightModeInTileKey`.

## 12. `DocumentActor::get_page_transition` query for the parsed `/Trans` and `/Dur`

Acrobat Pro plays page transitions and honours automatic page timing in Full Screen mode; Recto's presentation surfaces (`D02 T02 §13`, `§14`) need the same data. `D02 T02 §12` owns the parser (`Document::page_transition` over the page's `/Trans` dictionary and the clamped page `/Dur`, ISO 32000-1 section 12.4.4), but `recto::pdf::Document` lives on its `DocumentActor`, so the viewer needs an actor query that returns the parsed result as an immutable value. This section adds only that query; it parses nothing itself. It must not break the rule that the UI thread never parses a PDF and never blocks on the actor, and it must never expose a pointer or reference into the `Document`. The Recto improvement it keeps: the value carries no action, so a transition can never run PDF JavaScript or a launch action (D9), and hostile values arrive already clamped by the parser.

**Phase:** 2
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `get_page_transition` is an MPSC queue message like every other actor query, that `Document::page_transition` and the `/Dur` read run only on the owning actor thread, that the reply is a `PageTransitionInfo` value copied into the future with no pointer, reference or view into the `Document`, and that requests still queued when `close()` runs resolve with `ErrorCode::Cancelled`.

**Build order:**

1. Add `struct PageTransitionInfo { std::optional<recto::pdf::PageTransition> transition; std::optional<float> auto_advance_s; }` and `DocumentActor::get_page_transition(size_t page_index) -> std::future<PdfResult<PageTransitionInfo>>` to `src/core/document_actor.h/.cpp`, filling `transition` from `Document::page_transition(page_index)` and `auto_advance_s` from the page `/Dur` accessor of `D02 T02 §12`. Done when: `document_actor.cpp` compiles cleanly on `x64` and `ARM64`.
2. Add `Actor.PageTransition*` tests to `tests/recto-tests/actor_tests.cpp` over `tests/corpus/transitions.pdf` (12 pages, one per style, `/Dur` on pages 2 and 3, from `D02 T02 §12`). Done when: `recto-tests.exe --gtest_filter=Actor.PageTransition*` passes under ASan.

- [ ] Add the `GetPageTransition` message to the actor queue in `src/core/document_actor.cpp` and call the parser only from the actor loop. Done when: `Actor.PageTransitionOnActorThread` records that the parser ran on the actor thread's id, never the caller's.
- [ ] Return `PageTransitionInfo` by value and assert it at compile time with `static_assert(std::is_trivially_copyable_v<recto::pdf::PageTransition>)` in `src/core/document_actor.cpp`. Done when: the assertion compiles, and a reply read after `close()` still holds its values.
- [ ] Report a page with neither `/Trans` nor `/Dur` as both fields empty, and a page with only `/Dur` as `transition` empty and `auto_advance_s` set. Done when: `Actor.PageTransitionValues` reads the matching style enum on every page of `transitions.pdf`, `auto_advance_s` on pages 2 and 3 only, and empty fields on a page of `render/simple-1p.pdf`.
- [ ] Return the same typed `PdfError` that `Document::page_description` returns for a page index at or past the page count. Done when: `Actor.PageTransitionBounds` gets that error for index `page_count` and for `SIZE_MAX` without a crash under ASan.
- [ ] Resolve queued `get_page_transition` requests with `ErrorCode::Cancelled` when the actor closes. Done when: `Actor.PageTransitionCloseCancels` enqueues 20 requests, closes at once, and every future resolves without hanging, with `debug_live_documents() == 0`.
- [ ] Commit: `core: add the DocumentActor page transition query`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Actor.PageTransition*` passes under ASan, and calling the parser on the caller's thread instead of the actor fails `Actor.PageTransitionOnActorThread`.

## 13. `ObjectChangeSet` and the in-memory `StagedRevisionChain` over `IncrementalWriter`

> **Groomed 2026-10-23:** the chain bound returned `ErrorCode::UnsupportedFeature`; it now returns `ErrorCode::ResourceLimit`, which `§18` adds (the code did not exist), and `§13` and `§14` depend on `§18`.

Acrobat Pro keeps an edit (a new layer, a renamed destination) unsaved until the user saves, and undoes it on request. Recto's rule is stricter: authoring always works on a copy and never modifies the open viewer document in place, and `recto::pdf::Document` is read-only. This section gives the engine the two pieces an actor needs for that: `ObjectChangeSet`, a plain value listing replaced and newly allocated indirect objects, and `StagedRevisionChain`, which appends each change set as one incremental revision (`§5`) to an in-memory copy of the opened bytes, reopens a working `Document` from that copy, undoes by truncating the last revision, and exports one squashed revision for the save path. It writes no file. It must not break `§5`'s byte-prefix rule (existing signatures stay valid), the `PdfResult<T>` contract, or the bounds on hostile input. The Recto improvement it keeps: the original file and the viewer's opened bytes are never touched until the save path (`D02 T01 §8`) writes a new file by temp-file-then-replace.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `ObjectChangeSet` holds only values (`IndirectRef` keys and `PdfValue`s) with no pointer into any `Document`, that a `StagedRevisionChain` and its working `Document` are owned and used by one thread only (the `DocumentActor` that creates it) and never shared, that it never mutates the base byte buffer or any other open `Document`, and that every buffer it exports is an immutable `std::shared_ptr<const std::vector<uint8_t>>`.

**Build order:**

1. Implement `src/pdf/staged_revisions.h` and `src/pdf/staged_revisions.cpp`: `ObjectChangeSet` (`replace(IndirectRef, PdfValue)`, `allocate(PdfValue) -> IndirectRef`, `merge(const ObjectChangeSet& later)`), `RevisionId` (a `uint64_t` value type), and `StagedRevisionChain` (`create(std::span<const uint8_t> base, std::string_view password, RevisionId base_revision, RevisionId first_new_revision)`, `push(ObjectChangeSet)`, `undo()`, `redo()`, `working() -> const Document&`, `next_free_object()`, `depth()`, `current_revision() -> RevisionId`, `export_squashed() -> PdfResult<ExportedRevision { RevisionId revision; std::shared_ptr<const std::vector<uint8_t>> bytes; }>`), every fallible call returning `PdfResult`. Done when: `staged_revisions.cpp` compiles cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/pdf_staged_revisions_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfStagedRevisions.*` passes under ASan.

- [ ] Implement `ObjectChangeSet` in `src/pdf/staged_revisions.h` with `allocate` numbering new objects from the working document's `next_free_object()`, and `merge` keeping the later value for a reference set twice and every allocated reference. Done when: `PdfStagedRevisions.MergeOrder` shows the later value wins and no allocated reference is lost.
- [ ] Implement `StagedRevisionChain::push` writing the change set through `IncrementalWriter` (`§5`) on the current working document, appending the revision to the in-memory working bytes, and reopening the working `Document` from them with the retained password. Done when: `PdfStagedRevisions.PushPopRedo` sets page 1 `/Rotate 90` on `render/simple-1p.pdf` and the working document reports rotation `90`.
- [ ] Implement `undo` by truncating the working bytes to the length before the last revision and `redo` by re-appending the stored revision bytes, with a new `push` after an `undo` clearing the redo stack. Done when: `PdfStagedRevisions.PushPopRedo` reads rotation `0` after `undo`, `90` after `redo`, and `redo` fails with a typed error after a fresh `push`.
- [ ] Give every state of the chain a `RevisionId`: the base state carries `base_revision`, each `push` assigns the next unused id starting at `first_new_revision` (never reusing an id within the chain, including ids dropped from the redo stack), `undo` returns `current_revision()` to the previous state's id and `redo` restores the same id it had before the `undo`. Done when: `PdfStagedRevisions.RevisionIds` pushes A and B, undoes B, pushes C, and reads ids `base`, `a`, `b`, `a`, `c` with `c` greater than `b`.
- [ ] Keep the base bytes as an untouched prefix of every working buffer. Done when: `PdfStagedRevisions.BasePrefixPreserved` finds `std::memcmp` of the base length equal to `0` after 3 pushes, 2 undos and 1 redo. Cheaper substitute: rewriting the whole file on each edit.
- [ ] Implement `export_squashed` as the base bytes plus one revision holding the merged change set, writing allocated objects at their recorded numbers (extend `IncrementalWriter` in `src/pdf/incremental_writer.cpp` with `set_new_object(IndirectRef, PdfValue)` if `set_object` refuses numbers at or past the base `/Size`). Done when: `PdfStagedRevisions.SquashEquivalent` reopens the squashed buffer and resolves every changed and allocated object equal to the working document after 3 pushes that each allocate one object, at the same object numbers, and the export carries `current_revision()`.
- [ ] Support classic `xref`, `/Type /XRef` stream and encrypted bases through `IncrementalWriter`'s existing paths. Done when: `PdfStagedRevisions.CompressedAndEncrypted` pushes and squashes on `render/compressed-xref-2p.pdf` and on `render/encrypted-aes-256.pdf` with `recto-test`, and both reopen.
- [ ] Bound the chain at 1,000 pushed revisions, 100,000 objects per change set and 256 MiB of working bytes beyond the base, returning a typed `PdfError` with `ErrorCode::ResourceLimit` (`§18`) naming the limit, and zero the retained password with `SecureZeroMemory` on destruction. Done when: `PdfStagedRevisions.Bounds` hits each limit with a typed error and no partial revision left in the working bytes.
- [ ] Commit: `pdf: add ObjectChangeSet and the in-memory StagedRevisionChain`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfStagedRevisions.*` passes under ASan, and making `undo` reopen the working document without truncating the bytes fails `PdfStagedRevisions.PushPopRedo`.

## 14. `DocumentActor` optional-content edit session: stage, undo, redo, `ocg_generation`

> **Groomed 2026-10-23:** `ErrorCode::ResourceLimit` did not exist in `src/pdf/error.h`; `§18` now adds it and this section depends on `§18`. The pending-export refusal below uses it.

Acrobat Pro creates, renames, deletes, merges and imports layers from its Layers panel. Recto builds those operations in `D02 T03 §23` (create, rename, delete, usage, reorder, merge) and `§24` (import a page or an image as a layer), and the panel in `§25`; all of them must run where the `Document` lives, on a copy, undoably, and must make the canvas re-render at once. This section adds that edit session to the `DocumentActor`: each edit is a value describing the operation and a builder that runs on the actor thread against the working `Document` of a `StagedRevisionChain` (`§13`) and returns an `ObjectChangeSet` plus the reference of any layer it creates; the session tracks which revision was last saved, so dirty state, save acknowledgements and discard all refer to a revision identity rather than a flag; every stage, undo, redo or discard clears the `DisplayList` cache and publishes a new `ocg_generation` (the per-actor counter `D02 T03 §7` introduced for visibility toggles), so no stale tile or display list survives. It must not break the actor contract (`§8`), the visibility state of `D02 T03 §7`, or the shutdown order (`§10`). The Recto improvement it keeps: the original file and the opened bytes are never modified; the save path (`D02 T01 §8`) writes the exported revision to a new file by temp-file-then-replace.

**Phase:** 2
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the session's `StagedRevisionChain` and working `Document` are created, used and destroyed only on the owning actor thread, alongside the opened instance; that each `OcgEdit` builder runs on the actor thread and captures only values (strings, numbers, paths, `std::shared_ptr<const std::vector<uint8_t>>` bytes), never a reference to a `Document`, a UI object or another thread's state, and that a builder needing another file opens its own `recto::pdf::Document` inside the builder on the actor thread and destroys it before returning; that only `OcgEditState` and `OcgStageResult` values, `std::shared_ptr<const DisplayList>` and the exported `std::shared_ptr<const std::vector<uint8_t>>` revision leave the actor, and the saved snapshot the actor keeps is that same immutable buffer; that every publish increments the one monotonic `ocg_generation` and clears the 32-page `DisplayList` cache before replying, with each `get_display_list` reply carrying the generation it was compiled at so tiles are keyed by the generation of their content; and that `close()` trips the running builder's `CancelToken` and joins within the `§10` shutdown order.

**Build order:**

1. Add `src/pdf/ocg_build.h` (`recto::pdf::OcgBuildResult`, so the engine's layer builders in `D02 T03 §23` and `§24` return an engine type and `recto-pdf` never includes a `recto-core` header) and `src/core/ocg_edit.h` (`OcgEditKind`, `OcgEditOutput` as an alias of `recto::pdf::OcgBuildResult`, `OcgEdit`, `OcgEditState`, `OcgStageResult`) and the actor messages `begin_ocg_edit`, `stage_ocg_edit`, `undo_ocg_edit`, `redo_ocg_edit`, `export_ocg_revision`, `mark_ocg_saved`, `abandon_ocg_export` and `discard_ocg_edits` in `src/core/document_actor.h/.cpp`, each returning a `std::future<PdfResult<...>>`. Done when: `document_actor.cpp` compiles cleanly on `x64` and `ARM64`.
2. Add `Actor.OcgEdit*` tests to `tests/recto-tests/actor_tests.cpp` with test-only builders over a two-layer fixture; these prove the session mechanism, and the real builders are staged through this session by the integration tests of `D02 T03 §23` and `§24`. Done when: `recto-tests.exe --gtest_filter=Actor.OcgEdit*` passes under ASan.

- [ ] Define `OcgEditKind` (`CreateLayer`, `RenameLayer`, `DeleteLayer`, `SetLayerUsage`, `ReorderLayers`, `MergeLayers`, `ImportPageAsLayer`, `ImportImageAsLayer`, one per builder `build_create_layer`, `build_rename_layer`, `build_delete_layer`, `build_set_layer_usage`, `build_reorder_layers` and `build_merge_layers` of `D02 T03 §23` and `build_import_page_as_layer` and `build_import_image_as_layer` of `D02 T03 §24`), `recto::pdf::OcgBuildResult { ObjectChangeSet changes; std::optional<IndirectRef> created_layer; }` in `src/pdf/ocg_build.h`, `using OcgEditOutput = recto::pdf::OcgBuildResult;` and `OcgEdit { OcgEditKind kind; std::string label; std::function<PdfResult<OcgEditOutput>(const recto::pdf::Document& working, const CancelToken&)> build; }` in `src/core/ocg_edit.h`, with `label` UTF-8 of at most 128 characters used as the undo label. A builder never mutates `working`, allocates every new object through `ObjectChangeSet::allocate` (numbered from `working.next_free_object()` via the chain, `§13`) so `created_layer` names the same object in the working document and in the export, and takes any second file (the import source of `D02 T03 §24`) as a path and password captured by value, opening its own `Document` inside `build`. Done when: the header compiles in `recto-core` and `recto-tests`, and `src/pdf/ocg_build.h` compiles in `recto-pdf` with no include path to `src/core`.
- [ ] Add `tests/corpus/render/layers-2ocg.pdf` (one page, OCGs `Base` and `Overlay` both in `/D /ON`, each drawing one filled rectangle inside its own `BDC /OC ... EMC`) and `tests/corpus/render/encrypted-aes-256-no-modify.pdf` (AES-256 `R 6`, user password `recto-test`, owner password `recto-owner`, `/P` with bit 4 cleared) to `scripts/build-corpus.py` with `tests/corpus/PROVENANCE.md` rows; if `D02 T03 §7` already added a two-layer fixture, reuse it and record its path in the tests instead of adding the first file. Done when: `python scripts/build-corpus.py --check` regenerates both byte for byte.
- [ ] Implement `begin_ocg_edit()` creating the `StagedRevisionChain` over `Document::file_bytes()` and the password the actor already holds, idempotent while a session is open, and refusing with `ErrorCode::AccessDenied` when the authenticated document reports `permissions().can_modify == false`. Done when: `Actor.OcgEditPermissions` refuses on `encrypted-aes-256-no-modify.pdf` opened with `recto-test` and opens a session on `layers-2ocg.pdf`.
- [ ] Reply to `stage_ocg_edit` with `OcgStageResult { OcgEditState state; std::optional<recto::pdf::IndirectRef> created_layer; }`, refusing with a typed `PdfError` and pushing nothing when `created_layer` is not an object allocated in the same change set or that object's `/Type` is not `/OCG`. Done when: `Actor.OcgEditCreatedLayer` stages a test builder that allocates an OCG, reads the returned reference in the working document's `/OCProperties /OCGs`, and a builder returning a reference outside its change set is refused with the state unchanged.
- [ ] Implement `stage_ocg_edit(OcgEdit)` running `build` on the actor thread against the working document, pushing the returned change set, re-reading `/OCProperties` from the working document, and keeping the user's `set_ocg_visibility` overrides for layers that still exist. Done when: `Actor.OcgEditStageAndRender` stages a test builder that moves `Overlay` from `/D /ON` to `/D /OFF`, gets page 0's `DisplayList` from the actor, renders it without the overlay rectangle, and finds the opened file's SHA-256 unchanged. Cheaper substitute: mutating the opened `Document` in place.
- [ ] Leave the chain, the generation and the cache untouched when a builder returns an error or is cancelled, and return its `PdfError`. Done when: `Actor.OcgEditFailedBuildIsNoOp` stages a failing builder and reads the same `OcgEditState` as before.
- [ ] Publish on every stage, undo, redo and discard: increment the actor's single `ocg_generation` (shared with `set_ocg_visibility`), clear the 32-page `DisplayList` cache, and reply with `OcgEditState { ocg_generation, revision, saved_revision, undo_depth, redo_depth, dirty, undo_label, redo_label }`, where `revision` is the chain's `current_revision()` and `dirty` is exactly `revision != saved_revision`. Done when: `Actor.OcgEditGenerationMonotonic` interleaves visibility toggles and staged edits and reads strictly increasing generations, and the next `get_display_list` after each publish is a cache miss.
- [ ] Tag every `get_display_list` reply with the `ocg_generation` it was compiled at, and make `TileScheduler` (`§9`) key tiles with the reply's generation rather than the request's. Done when: `Scheduler.TileKeyUsesCompiledGeneration` queues a request before a stage, serves it after, and finds the delivered tile keyed with the new generation.
- [ ] Implement `undo_ocg_edit` and `redo_ocg_edit` over the chain's `undo` and `redo`. Done when: `Actor.OcgEditUndoRedo` stages 3 edits, undoes 3 and renders page 0 byte-identical to the original, redoes 1 and sees it again, and a new stage after an undo leaves `redo_depth == 0`.
- [ ] Implement `export_ocg_revision()` returning the chain's `ExportedRevision { revision, bytes }` (`§13`) for `SavePipeline` (`D02 T01 §8`) to write by temp-file-then-replace; the actor itself writes no file. The session keeps every exported buffer that is still pending (neither acknowledged by `mark_ocg_saved` nor released by `abandon_ocg_export(revision)`, which `SavePipeline` calls when its write fails or is cancelled), at most 4 pending exports, refusing a fifth with `ErrorCode::ResourceLimit` until one is acknowledged or abandoned; acknowledging a revision releases every pending export older than it, and close releases all. Done when: `Actor.OcgEditExport` reopens the exported bytes with the edited layer state, finds the original bytes as their prefix, and finds the test's temporary folder listing unchanged by the actor; and `Actor.OcgEditPendingExports` exports `a`, stages and exports `b`, then calls `mark_ocg_saved(a)` and succeeds with `a`'s bytes as the saved snapshot, abandons `b`, and is refused a fifth concurrent pending export. Cheaper substitute: writing the revision onto the source file from the actor thread.
- [ ] Implement `mark_ocg_saved(RevisionId revision)` as a revision-bound acknowledgement: it sets `saved_revision` to that id and keeps that export's bytes as the saved snapshot, refuses with a typed `PdfError` an id this session never exported, and recomputes `dirty` against the current revision, so an edit staged while the file was being written stays dirty. Done when: `Actor.OcgEditSaveRevisions` stages A, exports `a`, stages B before acknowledging, calls `mark_ocg_saved(a)` and reads `dirty == true`; undoes to `a` and reads `dirty == false`; redoes to `b` and reads `dirty == true`; and `mark_ocg_saved` of an unknown id is refused. Cheaper substitute: a parameterless acknowledgement that clears a dirty flag.
- [ ] Implement `discard_ocg_edits()` returning to the saved revision: with a saved snapshot, replace the chain by a new `StagedRevisionChain` over the snapshot bytes with `base_revision = saved_revision` and `first_new_revision` above every id the session has assigned, clearing undo and redo history; with no save in the session, drop the chain and render from the opened instance again; publish a new generation either way. Done when: `Actor.OcgEditDiscard` runs A, export, `mark_ocg_saved(a)`, B, discard and renders page 0 byte-identical to the A state (not the opened file) with `dirty == false`, and in a session with no save renders page 0 byte-identical to the original after discard.
- [ ] Pass each builder a `CancelToken` tripped by `close()` and `Runtime::shutdown()`, and resolve queued session messages with `ErrorCode::Cancelled`. Done when: `Actor.OcgEditCloseCancels` closes during a builder that polls its token, every future resolves within 1 second, and `debug_live_documents() == 0` and `debug_live_display_lists() == 0` after close.
- [ ] Commit: `core: add the DocumentActor optional-content edit session`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Actor.OcgEdit*:Scheduler.TileKeyUsesCompiledGeneration` passes under ASan, and skipping the `DisplayList` cache clear on publish fails `Actor.OcgEditStageAndRender`.

## 15. Transparency groups, blend modes and soft masks in the DisplayList and rasterizer

> **Groomed 2026-10-23:** new section. `§3` records `/ca`, `/CA`, `/BM` and `/SMask` from `gs`, and `§6` replays paths, glyphs and images with plain alpha only, so a Word or PowerPoint export (luminosity soft masks, multiply shadows, isolated groups) would render wrong or as solid black, the failure class `D06 T03 §10` gates. Nothing in the tree owned the compositing half of ISO 32000-1 section 11, so this section does.

Transparency is where exported Office and design-tool PDFs go wrong, and it must stay deterministic across worker counts and tile seams. This section adds the group and soft-mask commands to the immutable `DisplayList` (`§1`), parses them in the interpreter (`§3`, `§4`), and composites them in the rasterizer (`§6`) with Recto's own pixel formulas for the 16 PDF blend modes, so output does not depend on a Direct2D effect's rounding. Compositing is pixel-local, so a group is rendered into a bitmap clipped to the current tile and the group's `/BBox`, which keeps tile stitching seam-free. It must not break the freeze contract (`DisplayList` stays const, a sub-list is an immutable `std::shared_ptr<const DisplayList>`), 64-command cancellation (`§7`) or byte-identical output at any worker count. The Recto improvement it keeps: blend math lives in one tested file, `src/pdf/blend.cpp`, shared by the rasterizer and the print and export paths.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that group and soft-mask sub-lists are stored as `std::shared_ptr<const DisplayList>` inside the parent list, that the compositor allocates group bitmaps only inside the owning worker's `Rasterizer` and frees them before `render_tile` returns (also on cancel), and that no blend, group or mask state is cached on the `DisplayList`.

**Build order:**

1. Add `BeginGroup`, `EndGroup` and `SetSoftMask` commands and the sub-list storage to `src/pdf/display_list.h/.cpp`, and parse `/Group`, `/BM`, `/SMask` in `src/pdf/interpreter.cpp`. Done when: `PdfDisplayList.GroupCommands` and `PdfInterpreter.TransparencyOperators` pass under ASan.
2. Add `src/pdf/blend.h/.cpp` and the group and mask compositing to `src/pdf/rasterizer.cpp`. Done when: `PdfBlend.*` and `PdfRasterizer.Transparency*` pass under ASan.

- [ ] Add `BeginGroup { bbox, isolated, knockout, alpha, blend_mode }`, `EndGroup` and `SetSoftMask { sub_list, subtype, backdrop, transfer }` (or `None`) to `DisplayCommand` in `src/pdf/display_list.h`. Done when: `PdfDisplayList.GroupCommands` records a group nested three deep with a soft mask, reads it back from 8 threads under ASan, and finds zero non-`const` members.
- [ ] Parse `/Group << /S /Transparency /I /K >>` on a page and on a `/Subtype /Form` XObject, the `/BM` name or array (first known name wins, unknown means `Normal`) and the `/SMask` dictionary (`/S /Alpha` or `/Luminosity`, `/G` form, `/BC`, `/TR` function, and `/None`) in `src/pdf/interpreter.cpp`. Done when: `PdfInterpreter.TransparencyOperators` records each variant and ignores a `/SMask` whose `/G` is not a form. (ISO 32000-1 11.4, 11.6.5)
- [ ] Implement the 16 PDF blend modes (`Normal`, `Multiply`, `Screen`, `Overlay`, `Darken`, `Lighten`, `ColorDodge`, `ColorBurn`, `HardLight`, `SoftLight`, `Difference`, `Exclusion`, `Hue`, `Saturation`, `Color`, `Luminosity`) as integer premultiplied-BGRA functions in `src/pdf/blend.cpp`. Done when: `PdfBlend.AllModesMatchFormula` matches the table `scripts/build-corpus.py --blend-table` computes from the ISO 32000-1 11.3.5 formulas for 3 backdrop and source pairs per mode within 1 LSB. Cheaper substitute: the Direct2D blend effect with no formula check, whose rounding differs between GPUs and architectures.
- [ ] Composite isolated, non-isolated and knockout groups in `Rasterizer::render_tile` into a bitmap clipped to the tile and the group `/BBox`, then blend it into the parent with the group's alpha and mode. Done when: `PdfRasterizer.GroupIsolatedKnockout` finds the expected probe pixels from ISO 32000-1 11.4.8 on `render/transparency-groups.pdf`. (ISO 32000-1 11.4.5, 11.4.6)
- [ ] Apply `Alpha` and `Luminosity` soft masks (with `/BC` backdrop and `/TR` transfer function) per pixel inside the same tile bitmap, and apply image `/SMask` with `/Matte` un-premultiplication. Done when: `PdfRasterizer.SoftMaskLuminosityAlpha` renders a gradient under a luminosity mask with no solid black polygon and the probe pixels the formula predicts.
- [ ] Cap group nesting at `kMaxGroupDepth = 16` in the interpreter and flatten deeper groups into their parent, setting `DisplayList::group_depth_truncated`. Done when: `PdfInterpreter.GroupDepthCap` records 16 groups for a 40-deep file and returns success with the flag set, under ASan.
- [ ] Keep cancellation and tiling correct inside groups: poll `CancelToken` every 64 commands inside group and mask sub-lists, pop every group layer on abort, and render stitched tiles identically to a full page. Done when: `PdfRasterizer.CancelInsideGroup` aborts under 10 ms and renders the next tile exactly, and `PdfRasterizer.GroupTileStitchIdentity` is pixel-identical (`max_delta = 0`) at `0`, `90`, `180` and `270` degrees.
- [ ] Add `render/transparency-groups.pdf`, `render/blend-modes-16.pdf` and `render/softmask-luminosity.pdf` to `scripts/build-corpus.py` with `tests/corpus/PROVENANCE.md` rows. Done when: `python scripts/build-corpus.py --check` regenerates them byte for byte and `Corpus.Provenance` passes.
- [ ] Commit: `pdf: add transparency groups, the 16 blend modes and soft masks to the display list and rasterizer`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfBlend.*:PdfRasterizer.Transparency*:PdfRasterizer.GroupIsolatedKnockout:PdfRasterizer.SoftMask*:PdfRasterizer.CancelInsideGroup:PdfRasterizer.GroupTileStitchIdentity:PdfInterpreter.Transparency*:PdfInterpreter.GroupDepthCap:PdfDisplayList.GroupCommands` passes under ASan, and swapping the `Multiply` and `Screen` formulas fails `PdfBlend.AllModesMatchFormula`.

## 16. Shading types 1 to 7, tiling patterns and text clip modes in the rasterizer

> **Groomed 2026-10-23:** new section. `§3` records axial and radial shadings and tiling patterns into the `DisplayList` but `§6` never replays them, function-based shading (type 1) and the four mesh types (4 to 7) are in no section, and `§4` accepts `Tr` `4` to `7` without accumulating the clip. Fixes the gap by giving each its owner here.

Gradients, patterns and clipped text are common in brochures, maps and CAD exports. This section parses all seven ISO 32000-1 shading types, replays shadings and tiling patterns in the rasterizer, and implements text clipping (`Tr` `4` to `7`). Axial and radial gradients use Direct2D gradient brushes fed from the sampled `/Function`; function-based and mesh shadings are rasterized on the CPU into the tile bitmap so output is identical at any worker count. Pattern cells render once per `(pattern, scale)` per worker and tile by `/XStep` and `/YStep`. It must not break seam-free tiling, the 64-command cancellation of `§7`, or the bounds that keep a hostile pattern from allocating without limit.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that shading and pattern data lives in immutable `std::shared_ptr<const ...>` objects inside the `DisplayList`, that pattern-cell bitmaps are per-worker and never shared, and that text-clip outlines are copied into `RecordedPath` at interpretation time so no `IDWriteFontFace` is touched by the rasterizer for clipping.

**Build order:**

1. Add the shading and pattern commands to `src/pdf/display_list.h`, the type 1 to 7 parsers to `src/pdf/shading.h/.cpp`, and text-clip accumulation to `src/pdf/interpreter.cpp`. Done when: `PdfShading.*` and `PdfInterpreter.TextClipModes` pass under ASan.
2. Replay shadings, patterns and clips in `src/pdf/rasterizer.cpp`. Done when: `PdfRasterizer.Shading*`, `PdfRasterizer.TilingPattern*` and `PdfRasterizer.TextClip*` pass under ASan.

- [ ] Add `FillShading { shading, matrix, bbox, background, alpha }` and `FillTilingPattern { cell_list, bbox, matrix, xstep, ystep, paint_type, color }` to `DisplayCommand` in `src/pdf/display_list.h`. Done when: `PdfDisplayList.ShadingAndPatternCommands` records both and reads them from 8 threads under ASan.
- [ ] Record `sh` and `/Pattern` colour fills from the interpreter: `sh /Name` as `FillShading` clipped to the current clip, `scn` with a pattern name for `/PatternType 2` (shading) and `/PatternType 1` (tiling, the cell recorded as a sub-list, nested at most 8 levels) fills and strokes. Done when: `PdfInterpreter.ShadingAndPatterns` records each into the `DisplayList`. **Corrected 2026-10-24:** moved here from `D01 T02 §3`, which cannot record these before this section adds the commands.
- [ ] Parse `/ShadingType` 1 (function), 2 (axial), 3 (radial, with `/Extend`), and 4, 5, 6, 7 (free-form, lattice, Coons and tensor meshes with `/BitsPerCoordinate`, `/BitsPerFlag`, `/Decode`) in `src/pdf/shading.cpp`, refusing a mesh over `kMaxMeshPatches = 100000` or with bit sizes outside ISO 32000-1 Table 83 with a typed `PdfError`. Done when: `PdfShading.ParseAllTypes` parses one fixture of each type and `PdfShading.HostileMeshRefused` refuses an oversized patch count and a `/BitsPerCoordinate 40` in under 50 ms. (ISO 32000-1 8.7.4.5)
- [ ] Replay types 2 and 3 through `ID2D1LinearGradientBrush` and `ID2D1RadialGradientBrush` with the `/Function` sampled to at most 256 stops, honouring `/Extend`, `/Domain` and `/Background`. Done when: `PdfRasterizer.ShadingAxialRadial` finds probe pixels at 0, 25, 50, 75 and 100 percent within 1 LSB of the function's value. Cheaper substitute: a two-colour gradient that ignores the `/Function`.
- [ ] Rasterize types 1, 4, 5, 6 and 7 on the CPU into the tile bitmap (function evaluation per pixel for type 1, Gouraud triangles for types 4 and 5, Coons and tensor patches subdivided to at most 16 by 16 triangles each), clipped to the tile. Done when: `PdfRasterizer.ShadingMeshAndFunction` matches probe pixels computed in `scripts/build-corpus.py` for each type and is pixel-identical at 1 and 8 workers.
- [ ] Render tiling pattern cells (`/PaintType 1` and `2`, `/TilingType`) once per `(pattern, device scale)` per worker and tile them by `/XStep` and `/YStep`, bounding the cell bitmap at 16 megapixels and a step of zero or beyond `2^31` by drawing nothing and setting `DisplayList::pattern_truncated`. Done when: `PdfRasterizer.TilingPatternSeamFree` is pixel-identical between a full page and stitched tiles at all four rotations, and `PdfRasterizer.TilingPatternBounds` handles a step of `2147483647` and a `/BBox` of `100000 x 10` in under 50 ms.
- [ ] Accumulate glyph outlines for `Tr` `4` to `7` into a clip path applied at `ET` (ISO 32000-1 9.3.6), taking outlines from `IDWriteFontFace::GetGlyphRunOutline` and ignoring clipping for Type 3 fonts as the standard says. Done when: `PdfInterpreter.TextClipModes` and `PdfRasterizer.TextClipRender` show an image visible only inside the glyphs of `render/text-clip-modes.pdf`, and the state after `ET` restores correctly.
- [ ] Add `render/shadings-1-to-7.pdf` and `render/text-clip-modes.pdf` to `scripts/build-corpus.py` with `tests/corpus/PROVENANCE.md` rows. Done when: `python scripts/build-corpus.py --check` regenerates them byte for byte and `Corpus.Provenance` passes.
- [ ] Commit: `pdf: add shading types 1 to 7, tiling pattern replay and text clip modes`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfShading.*:PdfRasterizer.Shading*:PdfRasterizer.TilingPattern*:PdfRasterizer.TextClip*:PdfInterpreter.TextClipModes:PdfDisplayList.ShadingAndPatternCommands` passes under ASan, and ignoring `/Extend` fails `PdfRasterizer.ShadingAxialRadial`.

## 17. Non-embedded font substitution, predefined CJK CMaps and vertical metrics

> **Groomed 2026-10-23:** new section. `§2` resolves only embedded fonts and the Standard 14, so a PDF that names `ArialMT`, `Calibri` or an Adobe CJK collection without embedding it renders in the wrong face or with missing glyphs, and no section owned the predefined CMaps (`UniJIS-UCS2-H` and the rest) or vertical metrics. `D06 T03 §10` gates the result.

A large share of real PDFs reference fonts they do not embed. This section adds the substitution resolver (system font by `/BaseFont` and `/FontDescriptor` flags, per code point system fallback for missing glyphs), the predefined CMaps for the four Adobe CJK collections, and vertical writing metrics. It must not break `§2`'s embedded and Standard 14 paths, and a substituted glyph must never be reported as an embedded one. The Recto improvement it keeps: a character no font can draw is counted on the `DisplayList` (`unmapped_glyph_count`, `substituted_font_count`) instead of drawing a missing-glyph box or nothing silently, so `D06 T03 §2` can turn it into a page notice.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the resolver returns an immutable `LoadedFont` that shares the process-wide `DWRITE_FACTORY_TYPE_SHARED` factory, that CMap tables are `constexpr` or immutable static data safe to read from any worker, and that fallback resolution runs at `DisplayList` build time on the actor thread, never inside `Rasterizer::render_tile`.

**Build order:**

1. Add the substitution resolver to `src/pdf/font.cpp` and the CMap tables to `src/pdf/cmap_data.cpp` (generated by `scripts/gen-cmap-data.py`). Done when: `PdfFont.NonEmbeddedSubstitution` and `PdfFont.PredefinedCMapsCjk` pass under ASan.
2. Add vertical metrics and the glyph-count fields. Done when: `PdfFont.VerticalMetrics` passes under ASan.

- [ ] Resolve a non-embedded `/BaseFont` by stripping the subset tag, matching the family in the DirectWrite system collection (`ArialMT` to Arial, `TimesNewRomanPS-BoldMT` to Times New Roman, `Calibri` to Calibri), and otherwise choosing Times New Roman, Arial or Courier New from `/FontDescriptor /Flags` (Serif, FixedPitch) with weight and slant from `/FontWeight`, `/ItalicAngle`, `ForceBold` and Italic. Done when: `PdfFont.NonEmbeddedSubstitution` resolves the fixture's six font names to the expected family, weight and style on this host. (ISO 32000-1 9.6.2, 9.8.1)
- [ ] Keep each substituted glyph's advance from `/Widths` and scale the glyph horizontally to fit when its natural advance differs by more than 1 percent, clamped to 0.5 to 2.0, in `src/pdf/font.cpp`. Done when: `PdfFont.SubstitutedAdvanceFromWidths` places a 20-glyph run at the `/Widths` positions to within 0.01 pt. Cheaper substitute: using the substitute font's own advances, which shifts every following glyph.
- [ ] Resolve a code point that the chosen face lacks through `IDWriteFontFallback::MapCharacters` per code point (never by probing one character), and when no font draws it, skip the glyph and increment `DisplayList::unmapped_glyph_count`. Done when: `PdfFont.PerCodePointFallback` draws Cyrillic from non-embedded `ArialMT` with zero unmapped glyphs and counts exactly the one private-use code point planted in the fixture.
- [ ] Generate `src/pdf/cmap_data.cpp` with `scripts/gen-cmap-data.py` from the pinned Adobe `cmap-resources` release (BSD-3-Clause, commit and SHA-256 recorded in `third_party/cmap-resources/SOURCE.md`, notice added to `NOTICE.md`) for `Identity-H/V`, `UniJIS-UCS2-H/V`, `UniJIS-UTF16-H/V`, `UniGB-UCS2-H/V`, `UniGB-UTF16-H/V`, `UniCNS-UCS2-H/V`, `UniCNS-UTF16-H/V`, `UniKS-UCS2-H/V`, `UniKS-UTF16-H/V` and the legacy `90ms-RKSJ`, `GBK-EUC`, `ETen-B5` and `KSCms-UHC` families, with `usecmap` resolved, and the reverse `Adobe-Japan1`, `Adobe-GB1`, `Adobe-CNS1` and `Adobe-Korea1` CID-to-Unicode maps. Done when: `python scripts/gen-cmap-data.py --check` regenerates the file byte for byte and the compiled data adds under 2 MB to `recto-pdf.lib`.
- [ ] Decode a Type 0 font whose `/Encoding` is a predefined CMap name or an embedded CMap stream (with `usecmap`) to CIDs, and draw non-embedded CJK collections with a system face chosen by `/Registry-/Ordering` (Japan1 to Yu Gothic or MS Gothic, GB1 to Microsoft YaHei, CNS1 to Microsoft JhengHei, Korea1 to Malgun Gothic) by mapping CID to Unicode. Done when: `PdfFont.PredefinedCMapsCjk` draws the Japan1, GB1, CNS1 and Korea1 fixtures with zero unmapped glyphs and extracts the expected Unicode text.
- [ ] Read vertical metrics (`/WMode 1`, `/DW2`, `/W2` and the position vector) and advance vertically in `src/pdf/font.cpp` and the text operators of `src/pdf/interpreter.cpp`. Done when: `PdfFont.VerticalMetrics` places an `Identity-V` run at the ISO 32000-1 9.7.4.3 positions. (ISO 32000-1 9.7.4.3)
- [ ] Add `render/non-embedded-fonts.pdf` (Latin, Cyrillic, the four CJK collections, vertical and horizontal) to `scripts/build-corpus.py` with a `tests/corpus/PROVENANCE.md` row. Done when: `python scripts/build-corpus.py --check` regenerates it and `Corpus.Provenance` passes.
- [ ] Commit: `pdf: substitute non-embedded fonts, add predefined CJK CMaps and vertical metrics`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFont.NonEmbeddedSubstitution:PdfFont.SubstitutedAdvanceFromWidths:PdfFont.PerCodePointFallback:PdfFont.PredefinedCMapsCjk:PdfFont.VerticalMetrics` passes under ASan, and mapping Cyrillic through a single probe character instead of per code point fails `PdfFont.PerCodePointFallback`.

## 18. `ErrorCode::ResourceLimit` and `ErrorCode::InvalidArgument` in `error.h`

> **Started:** 2026-10-23T07:05:00Z

> **Groomed 2026-10-23:** new section. `src/pdf/error.h` has 16 codes and neither `ResourceLimit` nor `InvalidArgument`, yet `§14` here and sections in `D03 T03` already return them. A section that returns a code that does not exist cannot compile, so the codes get one owner and every consumer waits for it.

The owning layer is `src/pdf/error.h` (stamped in `D01 T01 §1`), so the fix is an additive change to the enum, its name table and the diagnostic mapping, with a regression test; no stamped checklist is rewritten. `ResourceLimit` means a bound that is not a decompression or recursion bound was reached (command count, change-set size, pending exports, wall-clock budget). `InvalidArgument` means the caller passed a value outside the documented domain (a page index past the end, a malformed password argument), distinct from a damaged file.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the change adds enumerators after `InternalError` so every existing numeric value is unchanged, adds no state and touches no threading, and that `error_code_name` stays `constexpr noexcept`.

**Build order:**

1. Add the enumerators, names and `DiagnosticId` entries in `src/pdf/error.h/.cpp`. Done when: `PdfError.ErrorCodeNamesCoverEveryCode` passes.
2. Map the codes in the diagnostic ring and ETW names in `src/pdf/error.cpp`. Done when: `PdfError.DiagnosticRingRecordsNewCodes` passes. **Corrected 2026-10-23:** (was: also the CLI exit-code table, which moved to `D01 T03 §1`).

- [x] Append `ResourceLimit` and `InvalidArgument` after `InternalError` in `enum class ErrorCode` in `src/pdf/error.h`, with matching `DiagnosticId` entries and `error_code_name` cases (`"ResourceLimit"`, `"InvalidArgument"`). Done when: `PdfError.ErrorCodeNamesCoverEveryCode` iterates every enumerator, finds a non-empty unique name for each, and finds the 16 pre-existing numeric values unchanged.
- [x] Extend the existing diagnostic-ring and ETW code mapping in `src/pdf/error.cpp` so both new codes are recorded and traced like the others. Done when: `PdfError.DiagnosticRingRecordsNewCodes` records one `ResourceLimit` and one `InvalidArgument` and reads them back with their names.
- [x] ~~Map `InvalidArgument` to CLI exit `1` and `ResourceLimit` to exit `2` in `src/cli/main.cpp`.~~ **Corrected 2026-10-23:** struck here and moved to `D01 T03 §1`, which owns the CLI exit-code contract: `src/cli/main.cpp` is still the `D00 T01 §3` scaffold with no dispatcher or exit-code table, so the mapping can only land where that table is built. -> XREF: D01 T03 §1. Done when: `D01 T03 §1` carries the mapping item and depends on `§18`.
- [x] Decide which new code records itself: `should_auto_record_diagnostic` in `src/pdf/error.h` returns `true` for `ResourceLimit` (a bound reached on document content, like `DecompressionLimitExceeded`) and `false` for `InvalidArgument` (a caller's error, not a damaged file, like `FileNotFound`). Done when: `PdfError.DiagnosticRingRecordsNewCodes` sees `make_pdf_error(ResourceLimit, ...)` in the ring and `make_pdf_error(InvalidArgument, ...)` not, until it is recorded explicitly. Default taken 2026-10-23 (industry practice: trace hostile-input bounds, not caller misuse); changing it is one `case` line.
- [x] Commit: `pdf: add ErrorCode ResourceLimit and InvalidArgument`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfError.*` passes under ASan, and renumbering an existing enumerator fails `PdfError.ErrorCodeNamesCoverEveryCode`. **Corrected 2026-10-23:** (was: also `Cli.ExitCodeFor*`, now `D01 T03 §1`'s).

> **Verified:** 2026-10-23 | §18 | d3365a93e7cd56b6e26ec695871cdd7bac9387cc
> **Review:** `docs/reviews/d01-t02-s18.json`

## 19. Interpreter work, memory and wall-clock ceilings on hostile content streams

> **Groomed 2026-10-23:** new section. `§3` and `§4` cap only graphics-state depth (`64`), Form nesting (`32`) and pattern depth (`8`); nothing bounds operator count, `DisplayList` size, path size, operand stack, decoded image memory or time, so a hostile or merely huge page can exhaust memory or run for minutes on a `DocumentActor`. `D06 T03 §1` and `§2` expect these ceilings and their truncation evidence to exist.

Foundation plan section 9 requires every recursive or data-driven walk to be bounded and to fail cleanly. This section gives the interpreter one options struct of ceilings, a cooperative wall-clock budget, and a defined result when a ceiling is reached: the page keeps what was interpreted and the `DisplayList` carries a truncation record that consumers must propagate (`D06 T03 §2` maps it to `Truncated` and a page notice). Very large images are decoded at a reduced scale instead of failing, so a 600 dpi A0 scan opens inside the working-set target. It must not change output for any file under the ceilings, and it must not weaken the cancellation of `§7`.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `InterpretOptions` is a plain value copied into the interpreter, that the budget clock and counters are members of the stack-allocated `Interpreter` (no globals), that a truncated `DisplayList` is still sealed immutable by `finish()`, and that reduced-scale image decoding allocates only inside the interpreter call.

**Build order:**

1. Add `src/pdf/interpreter_limits.h` (`InterpretOptions` and the `kMax*` defaults) and enforce them in `src/pdf/interpreter.cpp`. Done when: `PdfInterpreter.Ceiling*` pass under ASan.
2. Add truncation reporting to `DisplayList` and reduced-scale image decoding. Done when: `PdfInterpreter.ImageReduction` and `PdfDisplayList.TruncationRecord` pass under ASan.

- [ ] Define `InterpretOptions { max_operators = 20'000'000; max_commands = 8'000'000; max_path_segments = 1'000'000; max_operand_stack = 8192; max_inline_image_bytes = 64 MiB; max_image_bytes = 128 MiB; max_page_image_bytes = 512 MiB; wall_clock_ms = 10'000 }` in `src/pdf/interpreter_limits.h`, with the named `constexpr` defaults documented against foundation plan section 9. Done when: the header compiles in `recto-pdf` and `PdfInterpreter.DefaultOptions` reads each default.
- [ ] Count operators, commands and path segments in `Interpreter` and, when one is reached, stop interpreting that content stream, seal the `DisplayList` with what was recorded, and set `DisplayList::truncation()` to `{ reason, limit, observed }`. Done when: `PdfInterpreter.CeilingOperators`, `PdfInterpreter.CeilingCommands` and `PdfInterpreter.CeilingPathSegments` each render the content before the ceiling and report the reason, with options lowered to 1,000 so the tests run in under 1 second.
- [ ] Treat a `DisplayListBuilder` `ResourceLimit` refusal (`max_commands`, `max_paths`, `max_clip_depth` and the other `DisplayListLimits`), and the interpreter's own page refusals (an inline image whose end cannot be verified, `DamagedFormat` or `RefusalReason::InlineImageFraming`; a resource that does not resolve for a reason other than damage), the same way: stop, seal what was recorded and report the reason, instead of failing the page as `D01 T02 §3` does (`interpret_page` returns the builder's error; a page with 65 clips at one level is refused whole today). Done when: `PdfInterpreter.BuilderLimitsTruncate` interprets a page past `max_clip_depth` and past `max_commands` into a sealed, truncated list naming the limit. **Corrected 2026-10-24:** added from the `D01 T02 §3` self-review.
- [ ] Bound the operand stack and inline image sizes: an operator with more than `max_operand_stack` operands is skipped with a note, and an inline image over `max_inline_image_bytes` ends the stream. Done when: `PdfInterpreter.CeilingOperandStack` handles 100,000 operands without growth beyond the cap under ASan.
- [ ] Check a `std::chrono::steady_clock` deadline every 4,096 operators and at every image decode and Form entry, returning a truncated list with reason `WallClock`; take the clock through an injectable `ClockSource` so the test needs no sleeping. Done when: `PdfInterpreter.WallClockBudget` advances a fake clock past the budget mid-page and finds the truncation reason and the content recorded before it.
- [ ] Decode an image whose bitmap would exceed `max_image_bytes`, or push the page total over `max_page_image_bytes`, at the largest power-of-two reduction that fits (WIC `IWICBitmapScaler` for DCT, resolution-level decode for JPX, box filter otherwise) and record `DrawImage::reduction`. Done when: `PdfInterpreter.ImageReduction` opens a generated 20,000 x 20,000 one-bit image with a peak working-set increase under 600 MB and a reduction of at least 2, and a bitmap under the cap has reduction 1. Cheaper substitute: failing the page with `OutOfMemory`.
- [ ] Return `ErrorCode::ResourceLimit` (`§18`) from `get_display_list` only when not even the page's first command could be recorded, and otherwise return success with the truncation record. Done when: `Actor.TruncatedPageIsDelivered` receives a truncated list with its record and `Actor.UnrenderablePageIsAnError` receives `ResourceLimit`.
- [ ] Commit: `pdf: bound interpreter work, memory and time and report truncation`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.Ceiling*:PdfInterpreter.WallClockBudget:PdfInterpreter.ImageReduction:PdfInterpreter.DefaultOptions:PdfDisplayList.TruncationRecord:Actor.TruncatedPageIsDelivered:Actor.UnrenderablePageIsAnError` passes under ASan, and removing the operator counter fails `PdfInterpreter.CeilingOperators`.

## 20. Rasterizer failure isolation: typed tile errors, target recreation and retry

> **Groomed 2026-10-23:** new section. `§6` and `§7` cover the happy path and cooperative abort only. A Direct2D `EndDraw` failure (`D2DERR_RECREATE_TARGET`), an out-of-memory tile buffer or a WIC error on one tile had no defined outcome, and the review failure mode "a tile cached after abort" has no counterpart for a failed tile.

One bad tile must never take down a worker, poison the cache or loop forever. This section defines what `Rasterizer::render_tile` returns for each failure, recreates the render target once, and defines how `RenderPool` and `TileScheduler` report, retry and stop. It must not break `§7` (abort is not failure and is never retried) or the live-object counters of `§10`.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that a failed tile result carries no pixel buffer the scheduler could cache, that retry state is owned by the `TileScheduler` and never by a worker, that the fault-injection seam is compiled only into `recto-tests`, and that a worker thread survives every tile failure with its `Rasterizer` valid.

**Build order:**

1. Return typed errors from `Rasterizer::render_tile` and recreate the target once in `src/pdf/rasterizer.cpp`. Done when: `PdfRasterizer.RecreateTargetOnce` and `PdfRasterizer.TypedTileErrors` pass under ASan.
2. Add result, retry and backoff handling to `src/core/render_pool.cpp` and `src/core/tile_scheduler.cpp`. Done when: `Scheduler.FailedTileNotCached`, `Scheduler.RetryBackoff` and `Runtime.WorkerSurvivesTileFailure` pass under ASan.

- [ ] Map `EndDraw`, bitmap-creation and WIC `HRESULT`s in `Rasterizer::render_tile` to `OutOfMemory` (`E_OUTOFMEMORY`), `Cancelled` (never an error to retry) or `InternalError` with the `HRESULT` in the diagnostic ring, catching `std::bad_alloc` at the function boundary. Done when: `PdfRasterizer.TypedTileErrors` injects `E_OUTOFMEMORY`, `D2DERR_WRONG_FACTORY` and a `bad_alloc` through the test-only fault seam and finds the matching `ErrorCode` for each, with zero resource leaks.
- [ ] On `D2DERR_RECREATE_TARGET` discard the worker's `ID2D1RenderTarget`, recreate it once and re-render the tile, returning `InternalError` if the second attempt fails. Done when: `PdfRasterizer.RecreateTargetOnce` injects one failure and gets a pixel-exact tile, injects two and gets `InternalError`, and renders the next tile normally.
- [ ] Deliver a `TileResult` that holds either pixels or an `ErrorCode`, and never insert a failed or cancelled tile into the tile cache or mark its `TileKey` complete in `TileScheduler`. Done when: `Scheduler.FailedTileNotCached` fails a tile, then finds the key absent from the cache and requestable again.
- [ ] Retry a failed tile at most 3 times per `TileKey` with a 250 ms, 500 ms, 1 s backoff, then leave the key in an `Error` state that `TileScheduler::tile_state(key)` exposes to the viewer, and clear every `Error` state of a document when `TileScheduler::invalidate_document(doc_id)` is called (the actor calls it when it publishes a new `ocg_generation`, `§14`). Done when: `Scheduler.RetryBackoff` (with an injected clock) counts exactly 3 retries, then the `Error` state, then a fresh attempt after `invalidate_document`.
- [ ] Resolve every pending tile of a page with the page's `DisplayList` acquisition error (`§19`) without retrying it until `invalidate_document` is called. Done when: `Scheduler.DisplayListErrorFailsPendingTiles` fails page 3 and finds its tiles in `Error` with page 4's tiles delivered.
- [ ] Keep workers alive: a failed tile leaves `debug_live_workers()` unchanged, and `Runtime::shutdown` after failures still reaches zero live objects. Done when: `Runtime.WorkerSurvivesTileFailure` fails 100 random tiles under load and shuts down with all three counters at `0`.
- [ ] Commit: `core: isolate tile failures with typed errors, one target recreation and bounded retry`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfRasterizer.RecreateTargetOnce:PdfRasterizer.TypedTileErrors:Scheduler.FailedTileNotCached:Scheduler.RetryBackoff:Scheduler.DisplayListErrorFailsPendingTiles:Runtime.WorkerSurvivesTileFailure` passes under ASan, and caching a failed tile fails `Scheduler.FailedTileNotCached`.

## 21. Page `/UserUnit` in `PageDescription`, physical page size and CLI output size

> **Groomed 2026-10-23:** new section (coordinator gap from the viewer groom). `src/pdf/document.h` `PageDescription` has no `/UserUnit`, and `width_pt` is the unrotated crop-box width in default user-space units, so a page with `/UserUnit 2` reports half its physical size and renders at the wrong zoom-100 size. The owner `D01 T01 §10` is stamped, so the repair is a new section that adds to its struct without rewriting its checklist.

ISO 32000-1 14.11.2 defines `/UserUnit` as the size of one default user-space unit in 1/72 inch (default 1.0), so the physical page size is the crop box size times `UserUnit`. Content coordinates stay in default user space, so `width_pt` and `height_pt` keep their meaning and a new `user_unit` plus derived physical sizes are added; the viewer and `recto-cli render` use the physical size for actual-size zoom and pixel dimensions. It must not change tile geometry (`§6`) or any file without `/UserUnit`.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `user_unit` is a plain `double` in the value struct `PageDescription` copied out of the actor, that parsing happens on the owning thread inside `Document`, and that the rasterizer and tile identity are untouched.

**Build order:**

1. Add `user_unit`, `physical_width_pt` and `physical_height_pt` to `PageDescription` and parse `/UserUnit` in `src/pdf/document.cpp`. Done when: `PdfDocument.UserUnit*` pass under ASan.
2. Use the physical size in `recto-cli info` and `recto-cli render`. Done when: `Cli.RenderUserUnit` and `Cli.InfoUserUnit` pass.

- [ ] Add `double user_unit{1.0}`, `physical_width_pt()` and `physical_height_pt()` (unrotated crop size times `user_unit`) to `PageDescription` in `src/pdf/document.h`, keeping `operator==` defaulted. Done when: `PdfDocument.UserUnitDefaults` finds `user_unit == 1.0` and physical equal to `width_pt` for every page of `tests/corpus/render/`.
- [ ] Parse the page `/UserUnit` (a direct or indirect number, inherited nowhere because it is page-level) in `src/pdf/document.cpp`, treating a non-finite, zero, negative or over `10000` value as `1.0` and recording a `DamagedDictionary` diagnostic. Done when: `PdfDocument.UserUnitParsing` reads `2.0` and `0.5`, and `PdfDocument.UserUnitHostile` reads `1.0` plus a diagnostic for `1e20`, `-1`, `0` and a name object. (ISO 32000-1 14.11.2)
- [ ] Add `render/user-unit.pdf` (`/MediaBox [0 0 300 400]`, `/UserUnit 2`, plus a rotated variant) to `scripts/build-corpus.py` with a `tests/corpus/PROVENANCE.md` row. Done when: `python scripts/build-corpus.py --check` regenerates it and `Corpus.Provenance` passes.
- [ ] Print `user_unit` and the physical size in `recto-cli info` text and `--json` (`user_unit`, `physical_width_pt`, `physical_height_pt`) in `src/cli/main.cpp`. Done when: `Cli.InfoUserUnit` reads `600 x 800` physical points for the fixture and `1.0` for `simple-1p.pdf`.
- [ ] Size `recto-cli render` output as `round(physical_width_pt * dpi / 72)` by `round(physical_height_pt * dpi / 72)` (swapped at 90 and 270 degrees), keeping tile geometry in default user space. Done when: `Cli.RenderUserUnit` renders the fixture at `72` dpi to a `600 x 800` PNG and the rotated variant to `800 x 600`.
- [ ] Commit: `pdf: read page UserUnit and report physical page size`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfDocument.UserUnit*:Cli.InfoUserUnit:Cli.RenderUserUnit` passes under ASan, and ignoring `/UserUnit` fails `Cli.RenderUserUnit`.

## 22. Error codes named by later sections reconciled with `error.h`

> **Started:** 2026-10-23T11:41Z

The completion review of `D01 T02 §18` (`docs/reviews/01-engine/D01-T02-s18.md`, gap 8) found TODO contracts naming error codes `src/pdf/error.h` does not have: a `PdfErrorCode` scope throughout `todo/04-convert/`, `todo/05-organize/` and `todo/06-quality/` (`InvalidArgument`, `PasswordRequired`, `InvalidPassword`, `CertificateRequired`, `UnsupportedRecipientType`, `RollbackIncomplete`, `LimitExceeded`, `CorruptStructure`, `Cancelled`, `ClipboardEmpty`, `AlgorithmNotPermittedByFips`, `ConversionFailed`, `StructTreeMalformed`, `UndoBudgetExceeded`), and `CorruptStructure`, `LimitExceeded`, `InvalidPassword` and `OutputInvalid` as `ErrorCode` members in `todo/02-viewer/`, `todo/03-text/` and `todo/04-convert/`. A section built from such a contract either fails to compile or invents a code on the spot. This section decides each name once and keeps the plan honest with a check.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that any code added is appended after `InvalidArgument` so every existing numeric value is unchanged, adds no state and touches no threading, and that `error_code_name` stays `constexpr noexcept`.

**Build order:**

1. Decide each name: an existing code where its meaning matches (for example `PasswordRequired` to `NeedsPassword`, `InvalidPassword` to `WrongPassword`, `LimitExceeded` and `UndoBudgetExceeded` to `ResourceLimit`, `CorruptStructure` and `StructTreeMalformed` to `DamagedFormat`), or a new appended code where no existing one distinguishes a case a caller must act on differently. Done when: the decision table is in `docs/reviews/01-engine/D01-T02-s22.md`.
2. Correct every TODO reference and add a check. Done when: the check passes on the tree and fails on a planted wrong name.

- [x] Write the decision table (name, files, the code it becomes, why) in `docs/reviews/01-engine/D01-T02-s22.md`, preferring an existing code and adding one only with the caller behaviour that needs it. Done when: every name listed above has a row.
- [x] Append any new codes to `ErrorCode` in `src/pdf/error.h` with their names, `DiagnosticId` entries, messages and recording decision, as `§18` did, and extend `PdfError.ErrorCodeNamesCoverEveryCode`. Done when: the test names every value.
- [x] Replace each wrong name in the TODO files with `ErrorCode::<code>` from the table, with a dated `**Corrected YYYY-MM-DD:**` note naming the old name without its scope prefix, so the check below does not read the note as a use. Done when: no `PdfErrorCode` scope remains under `todo/`.
- [x] Add `tests/scripts/test_error_code_names.py`, which reads the `ErrorCode` enumerators from `src/pdf/error.h` and fails on any `ErrorCode::<name>` or `PdfErrorCode::<name>` under `todo/` (outside `implementation-plan.md`) that is not one of them. Done when: it passes on the tree and fails when an unknown qualified name, assembled in the test so no TODO text holds one, is planted in a TODO file. **Corrected 2026-10-23:** (round 2 of `D01 T02 §18`: this item and the context paragraph named unknown codes in their qualified form, which the check would refuse).
- [x] Commit: `pdf: error codes named by later sections reconciled with error.h`

**Test checkpoint:** `python -m unittest tests.scripts.test_error_code_names` passes and fails with a planted unknown code name, and `recto-tests.exe --gtest_filter=PdfError.*` passes under ASan.

> **Verified:** 2026-10-23 | §22 | 9117d02df00ebe9b8a319c92602b7dbe65798baf
> **Review:** `docs/reviews/d01-t02-s22.json`

## 23. Closed refusal reasons and numeric context on limit and argument errors

> **Started:** 2026-10-23T12:14Z

The completion review of `D01 T02 §18` (`docs/reviews/01-engine/D01-T02-s18.md`, Part 2 shortfall 2) found that support cannot tell which limit or which argument caused a `ResourceLimit` or `InvalidArgument`: the diagnostic ring replaces the caller's text with a generic message (by design, D16: no document bytes), and ETW carries no reason, observed value or bound. Acrobat's Diagnostics tool lets support see what failed; Recto should say which bound stopped the work, with numbers only, never a path, password or document text.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the change adds fields to `PdfError` and `DiagnosticEntry` without changing any existing field or numeric value, touches no threading, and keeps the ring free of document bytes.

**Build order:**

1. Add a closed `RefusalReason` catalogue and optional numeric context to `PdfError`, the diagnostic ring and the ETW `Diagnostic` event in `src/pdf/error.h` and `src/pdf/error.cpp`. Done when: a refusal carries reason, observed value and bound end to end.
2. Prove it through the ring and ETW capture. Done when: the tests below pass.

- [x] Add `enum class RefusalReason : uint16_t` (starting with `None`, `CommandCount`, `ChangeSetSize`, `PendingExports`, `WallClock`, `WorkingBytes`, `PageIndex`, `PageRangePastEnd`, `OutputSameAsInput`, then appended `PageRangeEmpty`, `PageRangeReversed`, `OutputUnwritable`) with a `constexpr noexcept` name table, and `refusal_reason`, `observed` and `bound` (optional `uint64_t`) on `PdfError` and `DiagnosticEntry` in `src/pdf/error.h`. Done when: `PdfError.RefusalReasonNamesCoverEveryReason` passes. **Corrected 2026-10-23:** (completion review: `PageRange` and `OutputPath` each covered several causes with different remedies, so each cause has its own reason, with a description giving units and remedy in `refusal_reason_description`).
- [x] Carry them into the ring entry, `DiagnosticEntry::format()` and the ETW `Diagnostic` event (`RefusalReason`, `RefusalReasonName`, `Observed`, `Bound` fields) in `src/pdf/error.cpp`. Done when: `PdfError.RefusalContextReachesRingAndEtw` reads them back from the ring and the captured packet, and finds no caller text in either.
- [x] Commit: `pdf: closed refusal reasons with numeric context`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfError.*` passes under ASan, including `PdfError.RefusalContextReachesRingAndEtw`.

> **Verified:** 2026-10-23 | §23 | 3d14d5b5f8e51b5bddf6b8a8a1a34583dece2ad9
> **Review:** `docs/reviews/d01-t02-s23.json`

## 24. Conversion-check failures name the check, the page and the measured value

> **Started:** 2026-10-23T22:44Z

The completion review of `D01 T02 §22` (`docs/reviews/01-engine/D01-T02-s22.md`, gaps 7 and 12) found that a `ConversionFailed` error cannot say which check failed, on which page, or by how much: the ring and ETW keep only the code (D16: never the message), and Acrobat's Preflight names each failed check and its location. This section appends the conversion checks to `RefusalReason` (`§23`) and gives `PdfError`, the ring and the ETW `Diagnostic` event a page index, so a failure reads, for example, `TextRecall` on page 3 with `observed=910` and `bound=990` (per mille). Consumers: `D04 T03 §8` (`DocxWriter` and `RtfWriter` checks) and `D04 T05 §12` (`PptxWriter` checks), and `D07 T03 §2`'s report.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that reasons are appended after the last reason (`OutputUnwritable`) and the page field after `prior_status` (the last field since `§25`), so every existing numeric value and field is unchanged, that no threading changes, and that the ring stays free of document bytes. **Corrected 2026-10-23:** was "the page field after `bound`", written before `§25` appended the decoder fields after `bound`.

**Build order:**

1. Append the conversion checks to `RefusalReason` and add the page index to `PdfError`, `DiagnosticEntry`, the ring slot and the ETW `Diagnostic` event in `src/pdf/error.h` and `src/pdf/error.cpp`. Done when: a conversion failure carries check, page, observed and bound end to end.
2. Prove it through the ring and ETW capture. Done when: the tests below pass.

- [x] Append `TextRecall`, `ReadingOrder`, `EmptyResult`, `PackageInvalid` and `RelationshipMissing` to `RefusalReason` after `OutputUnwritable`, each with its `refusal_reason_description`, with their names, and extend `PdfError.RefusalReasonNamesCoverEveryReason`. Done when: the test names every value.
- [x] Add `page_index` (optional `uint32_t`) to `PdfError` and `DiagnosticEntry`, carry it in the ring slot, `format()` (`page=3`, one-based for people) and the ETW event (`HasPageIndex`, `PageIndex`), and accept it in `make_refusal_error` (a trailing `page_index` parameter, so no existing call changes). The ring and ETW keep the page zero-based as `D01 T02 §16` spans do; only `format()` adds one. Extend `CapturedEtwEvent` and its capture slot with `has_diagnostic_page` and `diagnostic_page_index` (separate from the span `page_index`), decode `HasPageIndex` and `PageIndex` after `PriorPlatformStatus`, raise `kMaxCapturedFields` to cover them, and copy them in the snapshot. Done when: `PdfError.ConversionCheckContextReachesRingAndEtw` reads check, page, observed and bound back from the returned `PdfError`, the ring entry and the captured packet, and finds no caller text in any of them; it covers a page of `0` (both `format()` strings read `page=1`), a page of `2` (`page=3`), and no page (no `page=` in either string, `has_page_index` false in the packet), each through an auto-recorded `ResourceLimit` refusal and an explicit `record_diagnostic(err)` of a `ConversionFailed` refusal, and a page-less entry recorded after a paged one so a reused ring or capture slot never keeps the earlier page. **Corrected 2026-10-23:** (plan review): the test read only the ring and packet, so a `format()` that dropped or misnumbered the page and a missing presence flag in capture would have passed.
- [x] Name the reason each consumer returns: `D04 T03 §8` and `D04 T05 §12` return `make_refusal_error(ErrorCode::ConversionFailed, <check>, observed, bound)` with the page, per mille for ratios. Map: every structural violation (a missing part, slide or content type, malformed XML, an unbalanced RTF group or invalid RTF header, a `\trowd` row inconsistent with its `\cell` count, a duplicate shape id, a missing or non-ascending `p:sldId` from `256`, a wrong child element order) is `PackageInvalid`, with no observed or bound, and the zero-based source page of the slide part that fails or no page for a package-level part and for every Word or RTF check; an undeclared `rId` or missing relationship target is `RelationshipMissing`, same page rule. Text compares the source words `S` with the re-read words `R` per page: `TextRecall` when the multiset match `M` is short, observed `floor(1000 * M / max(|S|, |R|))` so missing, extra and altered words all count, bound `1000` for Word and RTF (which require equal text) and `990` for PowerPoint, page of the first failing page; only when recall passes, `ReadingOrder` when words are out of order, observed `floor(1000 * L / M)` with `L` the longest common subsequence of `S` and `R`, the same bounds, page of the first failing page; an empty slide or presentation is `EmptyResult` (page of the first empty content slide, none when the whole output is empty). `D07 T03 §2` reports the page beside the numbers and proves both a paged and a page-less refusal. **Corrected 2026-10-23:** (plan review): the map left RTF header, table, shape id, slide id and child order failures and non-missing text differences unnamed, and gave PowerPoint's order check `TextRecall`. `D07 T03 §2` reports the page beside the numbers. Done when: their items name the reason (written here so the consumers build against it). **Corrected 2026-10-23:** the consumers said only "its `RefusalReason`", or offered `TextRecall` or `EmptyResult` for both `D04 T05 §12` checks, so the mapping is fixed here. Cheaper substitute that fails the checkpoint: a reason only in the English message, which the ring drops.
- [x] Commit: `pdf: conversion-check failures name the check, page and measured value`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfError.*` passes under ASan, including `PdfError.ConversionCheckContextReachesRingAndEtw`.

> **Verified:** 2026-10-24 | §24 | 2bebc59dc7eac76d78c5048bcfde536562399f9b
> **Review:** `docs/reviews/d01-t02-s24.json`

## 25. Decoder failures keep their step and Windows HRESULT in the diagnostic ring and ETW

> **Started:** 2026-10-23T14:19Z

The completion review of `D06 T05 §37` (`docs/reviews/06-quality/D06-T05-s37.md`, gap G4) found that a WIC decode failure reaches the user with its step and HRESULT in the message, but the diagnostic ring and ETW keep only the code (D16: never the message), so support cannot see which step failed, which HRESULT Windows returned, or whether JPXDecode handed the stream to the native decoder. Acrobat's log collection records the failing component; Recto should keep the same evidence as numbers and closed names, never document bytes.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that fields are appended to `PdfError`, `DiagnosticEntry` and the ETW `Diagnostic` event after the existing ones, so every existing numeric value and field is unchanged, that no threading changes, and that the ring stays free of document bytes.

**Build order:**

1. Add a closed decoder step and an optional `platform_status` (`uint32_t`, the HRESULT) to `PdfError`, the ring and the ETW event in `src/pdf/error.h` and `src/pdf/error.cpp`, set by `wic_failure` in `src/pdf/filters.cpp`. Done when: a WIC failure carries step and HRESULT end to end.
2. Record the JPX hand-over. Done when: a native failure after a WIC data failure keeps both steps.

- [x] Add `enum class DecoderStep : uint16_t` (`None`, `WicFactory`, `WicCreateDecoder`, `WicGetFrame`, `WicConvertFormat`, `WicCopyPixels`, `NativeJpx`) with a `constexpr noexcept` name table, and `decoder_step` and `platform_status` (optional `uint32_t`) on `PdfError` and `DiagnosticEntry`. Done when: `PdfError.DecoderStepNamesCoverEveryStep` passes.
- [x] Carry them into the ring entry, `DiagnosticEntry::format()` (`[step=WicCopyPixels hresult=0x8007000E]`) and the ETW `Diagnostic` event (`DecoderStep`, `DecoderStepName`, `HasPlatformStatus`, `PlatformStatus`), and set them in `wic_failure` (`src/pdf/filters.cpp`). Done when: `PdfError.DecoderContextReachesRingAndEtw` injects a failure at each WIC step (`detail::ScopedWicFailure`) and reads step and HRESULT back from the ring and the captured packet.
- [x] When JPXDecode falls back and the native decoder also fails, keep WIC's step and HRESULT on the returned error beside the native failure. Done when: the test reads both from the ring entry. Cheaper substitute that fails the checkpoint: the HRESULT only in the English message, which the ring drops.
- [x] Commit: `pdf: decoder failures keep their step and HRESULT in diagnostics`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfError.*:PdfFilters.WicFailureClasses` passes under ASan, including `PdfError.DecoderContextReachesRingAndEtw`.

> **Verified:** 2026-10-23 | §25 | d5625d80cb1160d755aaef8bfca6c780d2eeab91
> **Review:** `docs/reviews/d01-t02-s25.json`

## 26. Image decoder recovery outcomes in diagnostics: substituted rows and refusal causes

> **Started:** 2026-10-23T23:39Z

The completion review of `D06 T05 §34` (`docs/reviews/06-quality/D06-T05-s34.md`, gap 10) found that CCITT damaged-row recovery is silent: a successful substitution records nothing, and a refusal's cause (allowance used up, no EOL to resynchronize at, recovery not allowed without `/EndOfLine`) lives only in the English message, which the ring and ETW never keep (D16). Support therefore cannot tell a page that opened with substituted rows from one that opened intact, or why one was refused.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that reasons and fields are appended after the existing ones, so every existing numeric value and field is unchanged, that no threading changes, and that the ring stays free of document bytes.

**Build order:**

1. Append the CCITT refusal causes to `RefusalReason` and record a recovery outcome (rows substituted, the first substituted row) as a diagnostic with numbers only, in `src/pdf/error.h` and `src/pdf/filters.cpp`. Done when: a recovered stream and each refusal cause reach the ring and ETW.

- [x] Append `DamagedRowAllowance`, `DamagedRowNoSync` and `DamagedRowRecoveryOff` to `RefusalReason` with their descriptions (cause, units, a safe next step: another scan or the original), and return them on the matching CCITT refusals with the row as `observed` and the allowance as `bound`. Done when: `PdfError.RefusalReasonNamesCoverEveryReason` names them and the CCITT refusals carry them. **Corrected 2026-10-24:** the plan assumed the CCITT refusals already reach the ring; they do not, since `make_filter_error` in `src/pdf/filters.cpp` discards its diagnostic id and filter errors are never auto-recorded, so today no CCITT refusal reaches the ring or ETW at all. The `refuse` lambda in `decode_ccitt_fax` now records each refusal once, through `record_diagnostic(ErrorCode::DamagedFormat, DiagnosticId::DamagedStream, byte_offset, std::nullopt, 0, reason, row, allowance)`, and returns a `PdfError` carrying the same reason, `observed` (the zero-based row, the same number the message's "row N" names) and `bound` (`/DamagedRowsBeforeError`). The causes map as: `/EndOfLine` false or K negative is `DamagedRowRecoveryOff`, the allowance spent is `DamagedRowAllowance`, no EOL after the row is `DamagedRowNoSync`. The comment on `RefusalReason` in `src/pdf/error.h` widens from ResourceLimit, InvalidArgument and ConversionFailed to include DamagedFormat image-decoder refusals. **Corrected 2026-10-24 (plan review gap 1):** a names test and the existing message checks do not prove the contract. `PdfFilters.CcittRecoveryReachesRingAndEtw` drives each refusal branch, the allowance spent, no EOL to resynchronize at, `/EndOfLine` false and K negative (the two recovery-off conditions, each its own fixture), and for each asserts the returned `PdfError`'s `refusal_reason`, `observed` (the zero-based row) and `bound` (the allowance), exactly one matching ring entry and exactly one captured ETW `Diagnostic` event with the same three values, the entry's `format()` naming the reason with ` observed=` and ` bound=`, and `refusal_reason_description` of the reason naming the cause and a next step. **Corrected 2026-10-24 (build):** K negative (Group 4) never reached the recovery code: a corrupt Group 4 codeword returned a plain filter error, so the K-negative recovery-off fixture the plan names could not exist. `decode_ccitt_fax` now refuses a corrupt Group 4 row through the same `refuse_damaged_row` lambda as `DamagedRowRecoveryOff` (row as `observed`, allowance as `bound`), recorded once in the ring and ETW, and the Group 4 fixture in `PdfFilters.CcittRecoveryReachesRingAndEtw` proves it.
- [x] Record one diagnostic per recovered stream (a new `DiagnosticId::RecoveredDamagedRows`, never automatic for a refusal) carrying the rows substituted and the first such row as numbers. Done when: `PdfFilters.CcittRecoveryReachesRingAndEtw` reads the count and the row back from the ring and the captured ETW packet, and finds none for an intact stream. **Corrected 2026-10-24:** the plan named no field for the count and the first row, and the existing `observed` and `bound` mean a refusal's value and limit, so reusing them would make one field mean two things. A new `struct RecoveryContext { uint64_t count; std::optional<uint64_t> first_unit; std::optional<uint32_t> tile; std::optional<uint32_t> component; }` in `src/pdf/error.h` is appended as the trailing default parameter of the full `record_diagnostic` overload and carried, field for field, by `DiagnosticSlot`, `DiagnosticEntry` (`recovered_count`, `first_recovered`, `tile_index`, `component_index`), `CapturedEtwEvent`, and the ETW `Diagnostic` event after `PageIndex` (`RecoveredCount`, `HasFirstRecovered`, `FirstRecovered`, `HasTile`, `Tile`, `HasComponent`, `Component`, so every earlier field keeps its position; `PdfError.EtwTraceLoggingProviderAndSpans`'s expected field list grows by those seven). The event grows to 34 fields, past the capture's `kMaxCapturedFields` of 32 in `src/pdf/error.cpp`, which rises to 48. `DiagnosticEntry::format` appends ` [recovered=N first=M tile=T component=C]`, showing only the fields present. `RecoveredDamagedRows` is recorded with `ErrorCode::DamagedFormat`, once per successful `decode_ccitt_fax` call (an image decoded twice records twice; deduplicating across calls would need per-document state the filter does not have), and only when at least one row was substituted. It is written at ETW level ERROR like every `Diagnostic` event: `TraceLoggingLevel` is a compile-time constant and an Error-level session would otherwise miss recoveries; this is a default, and changing it costs a second event name. **Corrected 2026-10-24 (plan review gaps 2 and 4):** each of the three new ids gets a fixed, document-free entry in `diagnostic_id_name` (the ETW `DiagnosticId` string) and `diagnostic_id_message` (the ring's readable message), asserted by name from the captured ETW event and from the entry's `format()`. `PdfError.RecoveryContextFormatsOnlyPresentFields` (in `tests/recto-tests/pdf_error_tests.cpp`) records entries whose `first_unit`, `tile` and `component` are present with value 0 and absent, and asserts `format()` prints `first=0`, `tile=0` and `component=0` for the present zeros and omits each absent one, and that a refusal or a plain diagnostic (count 0) prints no ` [recovered=` suffix at all. Recording only on a successful return gets a negative proof: a CCITT stream that substitutes a row and then exhausts its allowance on a later one records exactly the refusal and no `RecoveredDamagedRows` in either sink.
- [x] Record a recovery outcome for JPEG 2000 too: a code-block whose segmentation symbol is wrong (its bitplane dropped) and an empty subsampled tile-component (mid value written), with the code-block or tile and component as numbers. Done when: a JPX decode with a wrong symbol records one outcome and an intact decode none. **Corrected 2026-10-23:** (`D06 T05 §32` completion review: filed here). **Corrected 2026-10-24:** the plan named no diagnostic, numbering or test. Two ids are appended after `RecoveredDamagedRows`: `RecoveredJpxCodeBlocks` (`ErrorCode::DamagedFormat`; count is the code-blocks whose bitplane was dropped in the decode, first the zero-based ordinal of the first such code-block in Tier-1 order within its tile-component (resolution, subband, then code-block raster), with its tile and component) and `RecoveredJpxEmptyComponents` (`ErrorCode::Ok`, since T.800 B.2 permits a tile with no sample of a component; count is such tile-components, with the tile and component of the first). `decode_j2k_codeblock` returns whether it dropped a bitplane, and the empty tile-component is counted once per tile and component, not per pixel. Both are recorded by Recto's own decoder (`decode_jpx_native`) only when it returns an image, never on a refusal; WIC reports nothing of its own recovery. Done when: `PdfFilters.JpxRecoveryReachesRingAndEtw`, on the native path, reads one `RecoveredJpxCodeBlocks` from the ring and ETW for the existing wrong-last-symbol fixture, one `RecoveredJpxEmptyComponents` for the existing 1 x 1 subsampled fixture, and none for an intact decode. **Corrected 2026-10-24 (plan review gaps 3 and 4):** presence alone would pass on a constant count and zero locations, so the test asserts exact payloads in the ring and ETW. The C1 fixture has two empty components, so its one `RecoveredJpxEmptyComponents` carries count 2, tile 0, component 1 and no first unit. The test builder's `J2kTestOptions::wrong_last_symbol` (component 0 only) becomes a component bit mask `wrong_symbol_components`, so a three-component fixture damaged in components 1 and 2 records one `RecoveredJpxCodeBlocks` with count 2, tile 0, component 1 and first 0 (a nonzero location; the builder writes one code-block per component, so the ordinal within a tile-component is 0 in every fixture it can make, and a nonzero ordinal is a default left unproven, changing it costs a multi-code-block builder). The existing single-component case keeps its pixel check and records count 1, component 0, first 0. A negative case wraps a damaged codestream in a JP2 the decoder refuses after the tiles decode (a channel definition the decoder rejects) and asserts the refusal and no recovery outcome in either sink. **Corrected 2026-10-24 (completion review gaps 1 to 3):** the nonzero ordinal is no longer left unproven: `make_j2k_two_tile_codestream` writes two 8 x 4 tiles of two 4 x 4 code-blocks each, and `encode_j2k_block` takes the bitplane whose symbol is wrong, so the test asserts first 1 in tile 1 and first 1 in tile 0 (count 2) in both sinks, an intact control decodes exactly, and damage at a code-block's top bitplane leaves it all mid-grey. The two messages said the fax row is replaced by the row above and the JPEG 2000 area is "slightly less precise"; they now say a row after a damaged row is white and a code-block keeps only the bitplanes above its damage, up to losing all detail, and the tests assert the decoded rows (row 2 white after damaged row 1) and the wording.
- [x] Commit: `pdf: image decoder recovery outcomes in diagnostics`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfError.*:PdfFilters.Ccitt*:PdfFilters.JpxRecovery*` passes under ASan, including `PdfFilters.CcittRecoveryReachesRingAndEtw` and `PdfFilters.JpxRecoveryReachesRingAndEtw`. **Corrected 2026-10-24:** the filter gains `PdfFilters.JpxRecovery*`, since item 3 had no test in the checkpoint. **Corrected 2026-10-24 (plan review gap 5):** the filter also gains `PdfFilters.JpxComponentsAndCodingStyles`, which holds the dropped-bitplane pixel check and the empty-component mid values in the code item 3 changes, so correct diagnostics over wrong pixels cannot pass: `recto-tests.exe --gtest_filter=PdfError.*:PdfFilters.Ccitt*:PdfFilters.JpxRecovery*:PdfFilters.JpxComponentsAndCodingStyles`.

**Ambition (2026-10-24):** Acrobat Pro's documented support path (Acrobat Diagnostics: start, reproduce, stop, share the logs) hands logs to support, but nothing published says whether those logs record a substituted fax row or a dropped JPEG 2000 bitplane, so that is unknown, not an observed absence (**Corrected 2026-10-24, completion review excellence 2:** this line claimed both vendors recover silently, which was never measured). Recto's bar is measured by its own tests instead: every recovery and every refusal cause reaches the ring and ETW with its count and location, and each refusal names a next step. Showing the recovery to the user is the recovered-content page notice's job (`D06 T03 §3`), not this section's: Surface is no.

> **Verified:** 2026-10-24 | §26 | f78e3d7be5df1fcda3132913be36f874dbfebeb9
> **Review:** `docs/reviews/d01-t02-s26.json`

## 27. Standard 14 font metrics from the Adobe Core14 AFM files, with system-face fallback

The fourteen standard fonts (ISO 32000-1 section 9.6.2.2) need their exact widths and font boxes even when a PDF does not embed them, and a face to draw with. Adobe's Core14 AFM files are the primary source; their licence allows use, copying and distribution with the notice kept, which is compatible with Recto's GPL-3.0-or-later.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the generated metrics are `constexpr` tables with no mutable state, and that a standard font resolves to an immutable `LoadedFont` shared by every thread.

**Build order:**

1. Vendor the 14 Adobe Core14 AFM files with their licence notice under `third_party/adobe-core14-afm/` and generate `src/pdf/standard14_metrics.inc` with `scripts/build-font-metrics.py`. Done when: `python scripts/build-font-metrics.py --check` regenerates the file byte for byte.
2. Expose the metrics and the system-face mapping from `src/pdf/font.h`. Done when: `PdfFont.Standard14Metrics` passes under ASan.

- [ ] Vendor `Courier`, `Courier-Bold`, `Courier-Oblique`, `Courier-BoldOblique`, `Helvetica` (four), `Times-Roman`, `Times-Bold`, `Times-Italic`, `Times-BoldItalic`, `Symbol` and `ZapfDingbats` AFM files under `third_party/adobe-core14-afm/` with Adobe's licence text and a `README.md` naming the source and its SHA-256 sums. Done when: the files and sums are committed and `tests/scripts` checks the sums.
- [ ] Generate `src/pdf/standard14_metrics.inc` (glyph name, code, width, glyph bounding box per glyph; `FontBBox`, `Ascender`, `Descender`, `CapHeight`, `ItalicAngle`, `IsFixedPitch` per font) with `scripts/build-font-metrics.py`, with a `--check` mode run by `tests/scripts`. Done when: `--check` passes and fails after a one-byte edit of an AFM.
- [ ] Resolve a `/BaseFont` to its standard font, including the common aliases (`Arial`, `Arial,Bold`, `ArialMT`, `TimesNewRoman`, `TimesNewRomanPS-BoldMT`, `CourierNew` and their style suffixes) and subset prefixes (`ABCDEF+`). Done when: `PdfFont.Standard14Aliases` resolves each alias to its font and leaves unrelated names alone.
- [ ] Map each standard font to a Windows 11 face for drawing (`Times New Roman`, `Arial`, `Courier New` with matching weight and style; `Symbol` and `ZapfDingbats` to `Segoe UI Symbol`), keeping the AFM widths for layout so text never reflows. Done when: `PdfFont.Standard14Metrics` matches the AFM widths for every ASCII and Latin-1 glyph and loads each mapped face.
- [ ] Commit: `pdf: Standard 14 font metrics from the Adobe Core14 AFM files`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFont.Standard14*` passes under ASan with 0 warnings, and `python scripts/build-font-metrics.py --check` exits 0.

## 28. Simple-font encodings, the Adobe Glyph List, `/Differences` and `/ToUnicode` CMaps

Character codes in a simple font (`/Type1`, `/TrueType`, `/Type3`) map to glyphs through the font's encoding and to Unicode for search, copy and accessibility (ISO 32000-1 sections 9.6.6, 9.10). This section owns both maps.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the encoding, glyph-name and `/ToUnicode` maps are immutable once built and are read without locks by every thread that holds the font.

**Build order:**

1. Generate the base encodings (ISO 32000-1 Annex D) and the Adobe Glyph List into `src/pdf/glyph_names.inc` with `scripts/build-glyph-names.py`, and implement encoding and `/ToUnicode` parsing in `src/pdf/font_encoding.cpp`. Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below to `tests/recto-tests/pdf_font_tests.cpp`. Done when: they pass under ASan.

- [ ] Base encodings `StandardEncoding`, `WinAnsiEncoding`, `MacRomanEncoding`, `MacExpertEncoding`, and the built-in encodings of `Symbol` and `ZapfDingbats`, generated from ISO 32000-1 Annex D tables, and the Adobe Glyph List (`glyphlist.txt`, `zapfdingbats.txt`, BSD-3-Clause, vendored under `third_party/adobe-glyph-list/` with sums). Done when: `python scripts/build-glyph-names.py --check` passes and `PdfFont.BaseEncodings` checks every code of each table.
- [ ] `/Encoding` dictionaries: `/BaseEncoding` and `/Differences [code /name ...]`, names resolved through the AGL, `uniXXXX`, `uXXXX[XX]` and ligature names (`f_i`). Done when: `PdfFont.EncodingAndDifferences` resolves remapped codes, unknown names and runs of consecutive codes.
- [ ] TrueType glyph selection for simple fonts (ISO 32000-1 9.6.6.4): `(3,1)` with the encoding's Unicode for non-symbolic fonts, `(3,0)` with `0xF000` offsets and `(1,0)` for symbolic ones, and the code itself when no table fits. Done when: `PdfFont.TrueTypeCmapSelection` covers each branch with fixtures.
- [ ] `/ToUnicode` CMap streams: `begincodespacerange` with 1- to 4-byte ranges, `beginbfchar`, `beginbfrange` with the increment and array forms, UTF-16 surrogate pairs and multi-character targets (`<00660069>` is `fi`), `usecmap` refused with a typed error, and bounds on ranges, entries and target length. Done when: `PdfFont.ToUnicodeCMap` decodes each form and refuses malformed and oversized CMaps without a crash.
- [ ] Unicode for each code in the order readers use: `/ToUnicode`, then the encoding's glyph name through the AGL, then the TrueType `cmap`, else none (never a guessed character). Done when: `PdfFont.UnicodeFallbackOrder` checks each step.
- [ ] Commit: `pdf: simple-font encodings, glyph names and ToUnicode CMaps`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFont.*Encoding*:PdfFont.ToUnicode*:PdfFont.TrueTypeCmap*:PdfFont.UnicodeFallback*` passes under ASan with 0 warnings.

## 29. Type 1 and CFF charstrings to glyph outlines (`eexec`, Type 2, `seac`, subroutines)

Type 1 fonts (`/FontFile`) and bare CFF programs (`/FontFile3` with `/Type1C` or `/CIDFontType0C`) cannot be loaded by DirectWrite, so their glyphs are read into outlines here and drawn as paths (ISO 32000-1 sections 9.6.2 and 9.9; Adobe Type 1 Font Format; Adobe Technical Notes 5176 and 5177).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that outlines are decoded once into the immutable `LoadedFont` on the `DocumentActor` thread, with no lazy decoding on `RenderPool` workers.

**Build order:**

1. Implement `src/pdf/font_charstrings.cpp` reading Type 1 and CFF programs into `LoadedFont` (`OutlineOnly`). Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below. Done when: they pass under ASan.

- [ ] Type 1 programs: the cleartext and `eexec` portions by `/Length1`, `/Length2`, `/Length3` (binary and hexadecimal), `eexec` and charstring decryption with `lenIV`, `/Subrs`, `/CharStrings`, `/FontMatrix` and the built-in `/Encoding`. Done when: `PdfFont.Type1Outlines` reads a PFA-style and a PFB-style fixture to the same outlines.
- [ ] Type 1 charstrings (`hsbw`, `sbw`, `rmoveto`, `rlineto`, `rrcurveto`, the short forms, `closepath`, `callsubr` and `return`, `seac` accented glyphs from `StandardEncoding`, flex through `callothersubr` 0 to 2, hints skipped), with a 10-level subroutine depth and a stack of 24. Done when: `PdfFont.Type1Outlines` matches reference outlines and advances, including an `seac` glyph.
- [ ] CFF programs: header, `INDEX` and `DICT` parsing, charsets, encodings, `FDArray` and `FDSelect` for CID-keyed fonts, and Type 2 charstrings (all path operators, `hintmask` and `cntrmask` skipped, `callsubr` and `callgsubr` with the bias, `endchar` with `seac` arguments), with a 10-level depth and a stack of 48. Done when: `PdfFont.CffOutlines` matches reference outlines for a name-keyed and a CID-keyed fixture.
- [ ] Hostile programs refused with typed errors and bounded work: offsets outside the buffer, `INDEX` counts past the data, subroutine recursion, stack overflow and underflow, a charstring longer than 65,535 bytes, a glyph past 100,000 path operators. Done when: `PdfFont.HostileCharstringsRefused` covers each without a crash under ASan.
- [ ] Commit: `pdf: Type 1 and CFF charstrings to glyph outlines`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFont.Type1*:PdfFont.Cff*:PdfFont.HostileCharstrings*` passes under ASan with 0 warnings.

## 30. CID fonts: `/Type0`, `/DescendantFonts`, `/CIDToGIDMap`, `/W`, `/DW` and `/W2`

Composite fonts (ISO 32000-1 section 9.7) carry most CJK and many Western PDFs from Office: a `/Type0` font with one `/DescendantFonts` entry, a CMap from codes to CIDs and widths per CID.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the CID maps and widths are immutable once built and shared read-only across threads with the font.

**Build order:**

1. Implement composite fonts in `src/pdf/font_cid.cpp` on top of `§2`, `§28` and `§29`. Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below. Done when: they pass under ASan.

- [ ] `/Type0` with `/Encoding /Identity-H` and `/Identity-V`, and embedded CMap streams (`begincidrange`, `begincidchar`, `usecmap` of an identity CMap); the predefined CJK CMaps stay with `§17`. Done when: `PdfFont.Type0CodeToCid` maps 2-byte codes through each form.
- [ ] `/CIDFontType2` with `/CIDToGIDMap` (`/Identity` or a stream of big-endian glyph indices) over a `§2` font, and `/CIDFontType0` over a `§29` CFF program. Done when: `PdfFont.CidToGidMap` resolves glyphs through both forms and refuses a map stream of odd length.
- [ ] Widths: `/DW` and `/W` in both forms (`c [w1 w2 ...]` and `c_first c_last w`), and vertical `/DW2` and `/W2`, with bounds on entries and on CID ranges. Done when: `PdfFont.CidFontWidths` computes exact advances for an Identity-H string and refuses an oversized range.
- [ ] Commit: `pdf: CID fonts with CIDToGIDMap and W widths`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFont.Type0*:PdfFont.CidTo*:PdfFont.CidFontWidths` passes under ASan with 0 warnings.

## 31. Type 3 fonts: `/CharProcs` recorded as per-glyph sub-display-lists

A Type 3 font draws each glyph with its own content stream (ISO 32000-1 section 9.6.5), so its glyphs are recorded by the interpreter of `§3` and `§4` into sub-display-lists that `D01 T02 §1` places at each glyph origin.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that each glyph sub-list is an immutable `std::shared_ptr<const DisplayList>` recorded on the `DocumentActor` thread and only read by `RenderPool` workers.

**Build order:**

1. Record each `/CharProcs` stream through the interpreter into a `DisplayList` per glyph, in `src/pdf/interpreter.cpp`. Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below to `tests/recto-tests/pdf_interpreter_tests.cpp`. Done when: they pass under ASan.

- [ ] Record `/CharProcs` with `/FontMatrix` applied (so the sub-list is in text space with the font size applied, the `D01 T02 §1` convention), `d0` glyphs keeping their own colours and `d1` glyphs painting in the run's colour, `/Resources` from the font or else the page, and widths from `/Widths`. Done when: `PdfInterpreter.Type3Glyphs` draws a `d0` and a `d1` glyph with their bounds and colours.
- [ ] Bound Type 3 recursion (a glyph showing text in a Type 3 font) at 8 levels and charge glyph content to the interpreter's work ceilings (`§19`). Done when: `PdfInterpreter.Type3RecursionBounded` refuses a self-referencing glyph with a typed error.
- [ ] Commit: `pdf: Type 3 fonts as per-glyph sub-display-lists`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.Type3*` passes under ASan with 0 warnings.

## 32. Colour spaces: CalGray, CalRGB, Lab, ICCBased and calibrated CMYK through Windows colour

Calibrated and ICC-based colour (ISO 32000-1 sections 8.6.5 and 8.6.5.5) so colours from scanners, cameras and print workflows render as their authors saw them, through the Windows Color System (`mscms.dll`: `OpenColorProfileW`, `CreateMultiProfileTransform`, `TranslateColors`) rather than an approximation.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that colour transforms are built on the `DocumentActor` thread and cached per document only in immutable form, and that no colour state is shared across documents.

**Build order:**

1. Implement `src/pdf/color.h` and `src/pdf/color.cpp` and wire them into `cs`/`CS`/`sc`/`SC`/`scn`/`SCN` in `src/pdf/interpreter.cpp`. Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below. Done when: they pass under ASan.

- [ ] `CalGray` and `CalRGB` (`/WhitePoint`, `/BlackPoint`, `/Gamma`, `/Matrix`) and `Lab` (`/WhitePoint`, `/Range`) converted to sRGB with the CIE formulas of ISO 32000-1 8.6.5.2 to 8.6.5.4 and Bradford adaptation to D65. Done when: `PdfInterpreter.CalibratedColors` matches reference sRGB values to within 1/255 per channel.
- [ ] `ICCBased` streams (`/N`, `/Alternate`, `/Range`) through Windows Color System transforms to sRGB, built once per profile per document, with the `/Alternate` (or the device space for `/N`) when the profile does not load, and bounded profile size. Done when: `PdfInterpreter.IccBasedColors` converts through an embedded sRGB and a CMYK profile, and falls back on a damaged profile.
- [ ] Calibrated `DeviceCMYK`: through a CMYK output profile (the document's `/OutputIntents` `/DestOutputProfile` when present, else a recorded default) instead of the `§3` formula. Done when: `PdfInterpreter.CalibratedCmyk` checks a CMYK colour against the profile's sRGB result and the `§3` formula is used only when no profile loads.
- [ ] An image's own ICC profile (from a JPX colr box, `WicDecodedImage::icc_profile`) when its dictionary names no colour space, through the same transforms. **Corrected 2026-10-24:** moved here from `§3` (it was filed there by the `D06 T05 §32` completion review). Done when: a JPX image with a restricted ICC profile renders through that profile in `PdfInterpreter.JpxIccProfile`.
- [ ] `/DefaultGray`, `/DefaultRGB` and `/DefaultCMYK` in the page or Form resources replace the device space they name (ISO 32000-1 8.6.5.6), for colours and images alike, except inside their own definitions. Done when: `PdfInterpreter.DefaultColorSpaces` checks a `/DefaultRGB` `ICCBased` space applied to `rg` and to an image, and a device space without a default left as it is. **Corrected 2026-10-24:** added from the `D01 T02 §3` plan review (gap 5).
- [ ] Commit: `pdf: calibrated, Lab and ICC-based colour through Windows colour management`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.Calibrated*:PdfInterpreter.IccBased*:PdfInterpreter.JpxIccProfile` passes under ASan with 0 warnings.

## 33. Indexed, Separation and DeviceN colour with PDF functions 0, 2, 3 and 4

Spot colours and palettes (ISO 32000-1 sections 7.10, 8.6.6 and 8.6.6.5): `Indexed`, `Separation` and `DeviceN` colour spaces with the four PDF function types their tint transforms use.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that parsed functions and colour spaces are immutable once built and evaluated without shared mutable state.

**Build order:**

1. Implement PDF functions in `src/pdf/function.h` and `src/pdf/function.cpp` and the three colour spaces in `src/pdf/color.cpp`. Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below. Done when: they pass under ASan.

- [ ] PDF functions: type 0 (sampled, 1 to 32 bits per sample, `/Encode`, `/Decode`, multilinear interpolation, bounded sample table size), type 2 (exponential), type 3 (stitching, nested at most 8 levels), type 4 (PostScript calculator: the ISO 32000-1 7.10.5 operator set, a 100-entry stack and a bounded operation count per evaluation), each clipping inputs to `/Domain` and outputs to `/Range`. Done when: `PdfInterpreter.Functions` checks each type against hand-computed values and refuses malformed and over-budget functions with typed errors.
- [ ] `Indexed` (`[/Indexed base hival lookup]` with `hival` at most 255 and the lookup as a string or stream of the needed length) over any base space `§3` and `§32` read. Done when: `PdfInterpreter.IndexedColors` resolves palette indices, clamps out-of-range ones and refuses a short lookup.
- [ ] `Separation` and `DeviceN` (with `/Attributes`, `/Process` and `/Colorants` read but colourants drawn through the tint transform) with `/None` painting nothing and `/All` painting every separation. Done when: `PdfInterpreter.SeparationAndDeviceN` resolves spot tints through type 2 and type 4 tint transforms and handles `/None` and `/All`.
- [ ] Commit: `pdf: Indexed, Separation and DeviceN colour with PDF functions`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.Functions:PdfInterpreter.IndexedColors:PdfInterpreter.SeparationAndDeviceN` passes under ASan with 0 warnings.

## 34. Native Flate compressor (`flate_compress`: LZ77, Huffman blocks, zlib wrapper)

Recto writes PDFs (`§5`'s revisions, `D04 T02 §2`'s `PdfWriter`, the optimizer) and needs its own RFC 1950 and 1951 compressor: the engine takes no external runtime (D5, D7), and `filters.cpp` holds only the inflater (`decode_flate`, `inflate_raw_deflate`). -> XREF: D04 T02 §2 (its `PdfWriter` compresses through this function)

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `flate_compress` is a pure function of its input and level, allocates against its stated bound, and shares no state between calls or threads.

**Build order:**

1. Implement `flate_compress` in `src/pdf/filters.h` and `src/pdf/filters.cpp`. Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below to `tests/recto-tests/pdf_filters_tests.cpp`. Done when: they pass under ASan.

- [ ] `flate_compress(std::span<const uint8_t> input, FlateLevel level = FlateLevel::Default)` returning `PdfResult<std::vector<uint8_t>>`: a zlib stream (RFC 1950 header, Adler-32 trailer) of RFC 1951 blocks, with LZ77 over a 32 KiB window through hash chains bounded per level, lazy matching, and each block written stored, fixed-Huffman or dynamic-Huffman (length-limited to 15 bits, RFC 1951 3.2.7), whichever is smallest. Done when: `PdfFilters.FlateCompressRoundTrip` round-trips empty, one-byte, repetitive, random and 1 MiB inputs through `decode_flate` and checks each block type occurs.
- [ ] Bounded output and work: incompressible input grows by at most 5 bytes per 65,535 plus 6; input over `kMaxDecodedStreamBytes` is refused with `ResourceLimit`; memory is the window, the chains and the output. Done when: `PdfFilters.FlateCompressBounds` checks the growth bound on random data and the refusal.
- [ ] Ratio and speed measured: on the content streams and fonts in `tests/corpus/`, compressed size is at most 1.05 times the size Python's `zlib.compress(level=6)` gives (sizes written to `tests/corpus/flate/reference-sizes.json` by `scripts/build-corpus.py`), and Release x64 compresses the 1 MiB text sample at 20 MB/s or more. Done when: `PdfFilters.FlateCompressRatio` passes and records both numbers.
- [ ] Commit: `pdf: add a native Flate compressor`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFilters.FlateCompress*` passes under ASan with 0 warnings.

## 35. Canonical `PdfValue` serializer for indirect objects and streams

Every PDF Recto writes goes through one serializer of the object model (ISO 32000-1 sections 7.3 and 7.5), shared by `§5` and `D04 T02 §2`, so a value written is always a value read back the same. -> XREF: D04 T02 §2 (its `PdfWriter` writes every object through it)

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that serialization reads the value without changing it and bounds its nesting.

**Build order:**

1. Implement `src/pdf/serialize.h` and `src/pdf/serialize.cpp`. Done when: they compile on `x64` and `ARM64`.
2. Add `tests/recto-tests/pdf_serialize_tests.cpp`. Done when: its tests pass under ASan.

- [ ] `serialize_value(const PdfValue&, std::string& out)`: null, booleans, integers, reals in the shortest form that reads back exactly, with no exponent (ISO 32000-1 7.3.3), names with `#XX` for delimiters, whitespace, `#` and bytes outside `!`..`~` (7.3.5), strings as literal strings with `(`, `)`, `\` and non-printing bytes escaped or as hex strings when most bytes are binary (7.3.4), arrays, dictionaries and references; nesting refused past 64 levels. Done when: `PdfSerialize.ValuesRoundTrip` parses every serialized value back equal (all object kinds, every corpus file's objects) and `PdfSerialize.RealsAndNames` checks exponent-free reals and name escapes.
- [ ] `serialize_indirect(IndirectRef, const PdfValue&, std::string& out)` writing `N G obj ... endobj`, with a stream's `/Length` set to its exact byte count and `stream` followed by a line feed (7.3.8.1). Done when: `PdfSerialize.IndirectAndStreams` reopens a document built from serialized objects and reads each stream's bytes back exactly.
- [ ] Commit: `pdf: add the canonical PdfValue serializer`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfSerialize.*` passes under ASan with 0 warnings.

## 36. Encrypting new objects with a document's `SecurityHandler` (RC4, AES-128, AES-256)

The decrypting half exists (`SecurityHandler::decrypt_bytes`, `decrypt_object_values`, `D01 T01`); writing into an encrypted document (`§5`) and writing an encrypted one (`D05 T01`) need the encrypting half, with the same exemptions (ISO 32000-1 7.6, ISO 32000-2 7.6.3).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that encryption reads the authenticated handler's key without changing the handler, takes a fresh IV from `BCryptGenRandom` for every AES string and stream, and leaves the value unchanged on failure.

**Build order:**

1. Add the encrypting functions to `src/pdf/crypto.h` and `src/pdf/crypto.cpp`. Done when: they compile on `x64` and `ARM64`.
2. Add the tests below to `tests/recto-tests/pdf_crypto_tests.cpp`. Done when: they pass under ASan.

- [ ] `SecurityHandler::encrypt_bytes(std::span<const uint8_t> plaintext, IndirectRef ref, CryptFilterMethod method)`: RC4 and AESV2 with the per-object key (ISO 32000-1 Algorithm 1, `sAlT` for AES), AESV3 with the file key, AES in CBC with PKCS#7 padding behind a fresh 16-byte IV; refused before authentication. Done when: `PdfCrypto.EncryptRoundTrip` checks each method decrypts back through `decrypt_bytes`, two encryptions of one string differ, and an unauthenticated handler refuses.
- [ ] `SecurityHandler::encrypt_object_values(PdfValue&, IndirectRef)`: every string and stream payload, with `decrypt_object_values`'s exemptions (the `/Encrypt` dictionary, cross-reference streams, `/Sig` and `/DocTimeStamp` `/Contents`, `/Metadata` when `/EncryptMetadata` is false, the `Identity` crypt filter). Done when: `PdfCrypto.EncryptExemptions` checks each exemption and that `decrypt_object_values` restores the original value.
- [ ] Commit: `pdf: encrypt new objects with the document's security handler`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCrypto.Encrypt*` passes under ASan with 0 warnings.

## Verification

- `recto-tests.exe --gtest_filter=PdfDisplayList.*:PdfFont.*:PdfInterpreter.*:PdfIncrementalWriter.*:PdfRasterizer.*:Actor.*:Scheduler.*:Runtime.*` passes in Debug, Release, and ASan builds.
- Debug live-object counters (`debug_live_documents()`, `debug_live_display_lists()`, `debug_live_workers()`) end at `0` after every test.
- `recto-tests.exe --gtest_filter=PdfBlend.*:PdfShading.*:PdfRasterizer.Transparency*:PdfRasterizer.Shading*:PdfRasterizer.TilingPattern*:PdfFont.NonEmbedded*:PdfFont.PredefinedCMaps*:PdfError.*:PdfInterpreter.Ceiling*:Scheduler.FailedTileNotCached:PdfDocument.UserUnit*` passes under ASan once `§15` to `§21` ship.
- `recto-tests.exe --gtest_filter=PdfStagedRevisions.*:Actor.PageTransition*:Actor.OcgEdit*:Scheduler.LineWeightMode*:Scheduler.TileKeyUsesCompiledGeneration` passes under ASan once `§11` to `§14` ship (`§12` and `§14` are phase 2).
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
