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
| 4 | §4 | Content-stream interpreter complete over the render corpus | §2, §3, §27, §28, §29, §30, §32, §33, §42, §43, §44, §45, §46, §47, D06 T05 §32, D06 T05 §33 | [ ] |
| 5 | §5 | Incremental PDF revision writer (`src/pdf/incremental_writer.cpp`) | D01 T01 §10, D06 T05 §24, §34, §35, §36 | [x] |
| 6 | §6 | Per-worker Direct2D / WIC `DisplayList` tile rasterizer (`src/pdf/rasterizer.cpp`) | §4 | [ ] |
| 7 | §7 | Cooperative `CancelToken` and sub-10 ms mid-replay tile cancellation | §6 | [ ] |
| 8 | §8 | `DocumentActor` thread, MPSC queue, 32-page `DisplayList` cache and password state | §4, §5 | [ ] |
| 9 | §9 | `RenderPool` worker threads and 4-tier priority `TileScheduler` with jump cancellation | §7, §8 | [ ] |
| 10 | §10 | `Runtime` coordinator, section 4.8 ordered shutdown sequence and live-object counters | §9 | [ ] |
| 11 | §11 | `RenderOptions::line_weight_mode` in the rasterizer and the `TileKey` identity | §9 | [ ] |
| 12 | §12 | `DocumentActor::get_page_transition` query for the parsed `/Trans` and `/Dur` | §8, D02 T02 §12 | [ ] |
| 13 | §13 | `ObjectChangeSet` and the in-memory `StagedRevisionChain` over `IncrementalWriter` | §5, §18 | [x] |
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
| 27 | §27 | Standard 14 font metrics from the Adobe Core14 AFM files, with system-face fallback | §2 | [x] |
| 28 | §28 | Simple-font encodings, the Adobe Glyph List, `/Differences` and `/ToUnicode` CMaps | §2 | [x] |
| 29 | §29 | Type 1 and CFF charstrings to glyph outlines (`eexec`, Type 2, `seac`, subroutines) | §2 | [x] |
| 30 | §30 | CID fonts: `/Type0`, `/DescendantFonts`, `/CIDToGIDMap`, `/W`, `/DW` and `/W2` | §28, §29 | [x] |
| 31 | §31 | Type 3 fonts: `/CharProcs` recorded as per-glyph sub-display-lists | §4, §19 | [ ] |
| 32 | §32 | Colour spaces: CalGray, CalRGB, Lab, ICCBased and calibrated CMYK through Windows colour | §3 | [x] |
| 33 | §33 | Indexed, Separation and DeviceN colour with PDF functions 0, 2, 3 and 4 | §32 | [x] |
| 34 | §34 | Native Flate compressor (`flate_compress`: LZ77, Huffman blocks, zlib wrapper) | D01 T01 §10 | [x] |
| 35 | §35 | Canonical `PdfValue` serializer for indirect objects and streams | D01 T01 §10 | [x] |
| 36 | §36 | Encrypting new objects with a document's `SecurityHandler` (RC4, AES-128, AES-256) | D01 T01 §10 | [x] |
| 37 | §37 | Faster, smaller `Default` Flate level, measured against zlib-ng | §34 | [x] |
| 38 | §38 | Cancellable Flate compression and incremental revisions (`CancelToken`) | §5, §7, §34 | [ ] |
| 39 | §39 | Flate compressor at zlib-ng parity: a medium-effort parser and SIMD paths | §37, D06 T01 §7 | [ ] |
| 40 | §40 | Staged revisions over shared bytes: no file copy per edit, and a 10,000-edit history | §13 | [ ] |
| 41 | §41 | Cubic interpolation for multi-input `/Order 3` sampled functions | §33 | [ ] |
| 42 | §42 | Text state, positioning and text-showing operators with ISO 32000-1 advances | §2, §3, §27, §28, §29, §30, §33 | [x] |
| 43 | §43 | Glyphs for Standard 14, simple, embedded Type 1, CFF, TrueType and Type 0 text | §42 | [ ] |
| 44 | §44 | Image XObjects with stencil masks, explicit and colour-key masks and soft masks | §3, §32, §33, D06 T05 §32, D06 T05 §33 | [ ] |
| 45 | §45 | Inline images decoded on the interpreter's verified framing | §44 | [ ] |
| 46 | §46 | Form XObjects with `/Matrix`, `/BBox` clip, resource fallback and a 32-level cap | §3 | [ ] |
| 47 | §47 | Optional-content marked content recorded for layer visibility | §3 | [ ] |
| 48 | §48 | One built CIDFont per shared `/Type0` descendant object | §42, D01 T01 §11 | [ ] |

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

## 4. Content-stream interpreter complete over the render corpus

> **Groomed 2026-10-23:** added `D06 T05 §32` (JPX per-component decode) and `D06 T05 §33` (DCT `/ColorTransform`, Adobe CMYK and YCCK) to `Depends On`: `/Do` image decoding renders wrong colours and sizes until those decoder repairs ship, and the corpus sweep below would pin the wrong pixels.

> **Corrected 2026-10-28:** (validation before the build) split for single responsibility (AGENTS.md, todo/README.md): this section held text operators, glyphs for four font kinds, image XObjects, inline images, Form XObjects, optional content and the corpus sweep, ten items each a section's work. They moved verbatim to `§42` (text operators), `§43` (glyphs), `§44` (image XObjects), `§45` (inline images), `§46` (Form XObjects) and `§47` (optional content). This section keeps the corpus sweep and depends on all six, so every section that depends on `§4` still waits for the whole interpreter.

The content-stream interpreter of `src/pdf/interpreter.cpp` interprets every page of the render corpus once text (`§42`, `§43`), images (`§44`, `§45`), Form XObjects (`§46`) and optional content (`§47`) are built (ISO 32000-1 sections 8.8, 8.9 and 9).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that decoded image bitmaps are stored as `std::shared_ptr<const DecodedBitmap>` in `DisplayList` and recursive `/Subtype /Form` interpretation enforces the 32-level depth cap without mutating shared `Document` dictionaries.

**Build order:**

1. Run the corpus sweep over `tests/corpus/render/` in `tests/recto-tests/pdf_interpreter_tests.cpp`. Done when: `PdfInterpreter.CorpusRenderSweep` passes under ASan.

- [ ] Interpret every page of `tests/corpus/render/` (`simple-1p.pdf`, `compressed-xref-2p.pdf`, `incremental-2rev.pdf`, `rotated-pages.pdf`, `images-and-vectors.pdf`, and unlocked `encrypted-*.pdf`) into `DisplayList`s with zero errors under ASan. Done when: `PdfInterpreter.CorpusRenderSweep` passes 100%.
- [ ] Commit: `pdf: interpret every page of the render corpus`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.*` passes under ASan across all corpus render and broken files.

## 5. Incremental PDF revision writer (`src/pdf/incremental_writer.cpp`)

> **Started:** 2026-10-25T10:12Z

> **Corrected 2026-10-24:** split before the build. The engine had no object serializer, no Flate encoder and no encryption path (only `decode_flate`, `SecurityHandler::decrypt_bytes`), so this section would have built three shared modules inside a revision writer. They are `§34` (the Flate compressor), `§35` (the `PdfValue` serializer) and `§36` (encrypting new objects); `D04 T02 §2`'s `PdfWriter` consumes the same three instead of building its own. This section is the revision writer on top of them.

> **Corrected 2026-10-25 (plan review, `docs/reviews/01-engine/D01-T02-s5.md`):** folded its six gaps: a signature placeholder with reported field offsets for `D05 T01 §8` (P1), `PdfResult` entry points with aggregate bounds and all-or-nothing output tested by injected failures (P1), strict reparsing so recovery cannot hide a bad offset (P1), one allocator for explicit, allocated and cross-reference stream numbers (P2), the free list linked onto the old chain (P2), and an identifier hashed before encryption (P2).

> **Corrected 2026-10-25 (plan validation):** the parser kept no record of the newest section (its `startxref`, its kind, its own trailer), so the writer could not chain `/Prev` or choose the section kind; step 1 adds it. Added what the section left undecided: which bases are refused (recovered, locked, at the reader's limits), how `/ID` changes and when one is added, the trailer entries carried forward (ISO 32000-1 section 7.5.6), when a stream is compressed, hybrid files, freeing an object, writing at a recorded number for `§13`'s squash, the offsets a signer needs, and AES-128 and RC4 bases beside AES-256. The writer does not judge `/P` permissions or a certification signature's `/DocMDP` level: the save commands (`D02 T01 §8`) and the signature validator (`D05 T01 §8`) own those.

Implement `src/pdf/incremental_writer.h` and `src/pdf/incremental_writer.cpp` (`recto::pdf::IncrementalWriter`, foundation plan section 4.1 item 6):
- Append a non-destructive PDF revision to the end of an existing PDF byte buffer (preserving the original bytes `0..original_size` untouched so existing cryptographic digital signatures `/ByteRange` remain valid when adding a counter-signature or filling allowed form/annotation fields).
- Allocate new object numbers (`>= prev_size`) and write modified indirect objects (`N G obj ... endobj` through `§35`'s serializer, with `FlateDecode` compression from `§34` for modified streams that carry no filter, and `§36`'s encryption matching the document's active `SecurityHandler` when encrypted).
- Write a new `xref` subsection (or `/Type /XRef` stream when the source file uses cross-reference streams) chained to the previous `startxref` via `/Prev`, updated `/ID [<id0> <new_id1>]`, and `startxref` / `%%EOF`.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `IncrementalWriter` produces a new buffer or writes to a destination stream without mutating the source buffer in place, and re-encrypts modified objects with the document's active encryption key when the source PDF is encrypted.

**Build order:**

1. Record the newest cross-reference section in `XrefTable` (`src/pdf/xref.h`, `src/pdf/xref.cpp`). Done when: the `PdfXref` tests below pass.
2. Author `src/pdf/incremental_writer.h` and `src/pdf/incremental_writer.cpp` supporting modified object replacement, new object allocation, freeing, classic `xref` and `/Type /XRef` incremental trailer chaining (`/Prev`), and encrypted-document object encryption. Done when: `incremental_writer.cpp` compiles cleanly on `x64` and `ARM64`.
3. Add `tests/recto-tests/pdf_incremental_writer_tests.cpp` testing single and multi-revision incremental updates on classic `xref`, compressed `/Type /XRef`, hybrid and encrypted corpus PDFs. Done when: `PdfIncrementalWriter.*` passes under ASan.

- [x] `XrefTable` keeps what the parse of the newest section saw: `startxref_offset()` (the value as written after the file's last `startxref`, relative to the header), `newest_section()` (`XrefSectionKind::Classic`, `Stream` or `Hybrid` for a classic table whose trailer names `/XRefStm`), `newest_trailer()` (that section's own trailer or stream dictionary, not the merged one) and `total_rows()` (the rows read across every revision, which the reader caps at `kMaxXrefTotalEntries`); a table rebuilt by recovery reports `XrefSectionKind::None`. Done when: `PdfXref.NewestSection` asserts all four on `render/simple-1p.pdf`, `render/compressed-xref-2p.pdf`, a hybrid file built in the test and a damaged file that recovers.
- [x] `IncrementalWriter::create(const Document& base)` returns `PdfResult<IncrementalWriter>` and refuses a base it cannot extend safely, before any work: a recovered cross-reference (`DamagedFormat`, so the save path falls back to a full rewrite through `D04 T02 §2`, chosen by `D02 T01 §8`), an encrypted document that is not authenticated (`NeedsPassword`), and a base already at `kMaxXrefRevisions` revisions (`ResourceLimit`). Every mutation and output call returns `PdfResult`; one that would take the file past the reader's limits is `ResourceLimit` at that call: the cumulative rows (`total_rows()` plus the new section's) past `kMaxXrefTotalEntries`, an object number past 8,388,607 (ISO 32000-1 Annex C), or a revision past `kMaxRevisionBytes` (1 GiB, all staged values and output counted, offsets computed with checked arithmetic). Done when: `PdfIncrementalWriter.RefusesUnsafeBases` gets each code and no bytes, the row and number limits tested through a base whose rows or `/Size` sit just under them.
- [x] Preserve bytes `0..original_eof` of the source PDF byte-for-byte when appending an incremental revision, beginning the revision on a new line when the source does not end with one. Done when: `std::memcmp(output.data(), source.data(), source.size()) == 0` in `PdfIncrementalWriter.BytePrefixPreserved`, on a source with and one without a final end-of-line. Cheaper substitute: full-file rewrite that invalidates existing digital signatures (`/ByteRange`).
- [x] Track replaced and new indirect objects through `set_object(IndirectRef, PdfValue)`, which takes an in-use object at its current generation (one stored in an object stream included, written uncompressed at generation 0) or a number at or past the base `/Size` (generation 0, so `D01 T02 §13`'s squash writes allocated objects at their recorded numbers), and `allocate_object(PdfValue)`, which takes the lowest number at or past the base `/Size` that no staged object holds; the cross-reference stream's number comes from the same allocator when the revision is written, and `/Size` is one past the highest number used. A free or unknown number below `/Size`, a wrong generation, or the `/Encrypt` dictionary itself is `InvalidArgument`; replacing a staged object with `set_object` is allowed and the last write wins. Done when: unmodified objects are not duplicated in the appended revision, and `PdfIncrementalWriter.ObjectRules` covers each refusal, an object-stream member replaced, and explicit numbers interleaved with allocations, every object reached through a reference after reopening.
- [x] `free_object(IndirectRef)` writes a free entry at the next generation (an in-use generation 65,534 or 65,535 becomes 65,535, never reused, ISO 32000-1 section 7.5.4) and links the newly freed numbers into the free list in ascending order: object `0` names the lowest, each names the next, and the last names the base's old head, so the existing chain is kept. Done when: `PdfIncrementalWriter.FreeList` frees three objects on a base that already has free entries and checks every link and generation in both the classic and the stream output, and each freed object resolves as null after reopening.
- [x] Write each object through `§35`'s `serialize_indirect`; a stream with neither `/Filter` nor `/DecodeParms` is compressed with `§34`'s `flate_compress` at `Default` and given `/Filter /FlateDecode` only when that makes it smaller, and an XMP `/Type /Metadata` stream stays uncompressed so tools that scan for XMP find it (ISO 32000-1 section 14.3.2); a stream that carries a filter is written as given. Done when: `PdfIncrementalWriter.StreamCompression` reads back a compressed content stream, an incompressible stream kept raw and an uncompressed XMP stream.
- [x] Emit sorted `xref` subsections covering only the written object numbers (plus object `0`'s free-list head) and a `trailer` carrying every entry of the newest trailer except `/Prev`, `/XRefStm` and the cross-reference stream's own keys (ISO 32000-1 section 7.5.6), with `/Prev <old startxref>`, the new `/Size` and `/ID`. Done when: `Document::open_memory` on the incremental output resolves both the modified objects and all untouched objects from the base revision, and a custom trailer key survives.
- [x] Keep `/ID[0]` (the value decryption used when encrypted) and set `/ID[1]` to the MD5 of the old `/ID`, the base size and the revision's objects serialized before encryption, so the same edit gives the same identifier and a different edit a different one even where AES draws fresh IVs (ISO 32000-1 section 14.4); a base without `/ID` gains one unless it is encrypted with `R` 2 to 4, whose key was derived without it. Done when: `PdfIncrementalWriter.FileIdentifier` checks the kept first element, a deterministic changed second, the same second for the same edit written twice on `render/encrypted-aes-256.pdf` whose ciphertexts differ, a new pair on an unencrypted base without one, and none added to an `R=3` base without one.
- [x] Emit a `/Type /XRef` cross-reference stream (instead of an ASCII `xref` table) when the newest section of the source is a `/Type /XRef` stream (required by ISO 32000-1 section 7.5.8.4 for pure XRef-stream files), with its own object number, `/Index` naming only the written numbers, the narrowest `/W` widths and `§34`'s compression; a hybrid file gets a classic section, whose `/Prev` reaches the older `/XRefStm`. Done when: `PdfIncrementalWriter.CompressedXrefFile` updates `render/compressed-xref-2p.pdf` and re-opens cleanly, and `PdfIncrementalWriter.HybridFile` updates a hybrid file and still resolves an object only its `/XRefStm` lists.
- [x] Encrypt modified strings and streams in the appended revision through `§36`'s `SecurityHandler::encrypt_object_values` (`RC4`, `AES-128`, or `AES-256` `R=6`) when the base document is encrypted, leaving the cross-reference stream and the trailer `/ID` unencrypted (ISO 32000-1 7.5.8.1, 7.6.1). Done when: `PdfIncrementalWriter.EncryptedDocument` incrementally updates `render/encrypted-aes-256.pdf`, `render/encrypted-aes-128.pdf` and `render/encrypted-rc4-128.pdf` and re-opens each with `"recto-test"`, reading the changed strings and streams back as written.
- [x] `revision()` returns the appended bytes, the base size and the offset of every object it wrote, and `write()` returns the base bytes followed by them; each is all or nothing (built in its own buffer, so a compression, encryption, serialization or allocation failure returns its error and leaves the staged edits and the source as they were, and a retry succeeds). Done when: `PdfIncrementalWriter.RevisionOffsets` finds `N G obj` at every reported offset, and `PdfIncrementalWriter.FailuresAreAtomic` injects each failure (`detail::ScopedFlateCompressAllocFailure`, `detail::ScopedSerializeAllocFailure`, `detail::ScopedEncryptionFailure`) mid-revision, gets the error, then writes the same revision successfully.
- [x] `add_signature(PdfDict sig, size_t contents_bytes)` stages one `/Type /Sig` or `/DocTimeStamp` dictionary per revision with a reserved `/ByteRange` of four fixed-width integers and a `/Contents` hex string of `contents_bytes` zero bytes, written by the writer itself after the rest of the dictionary is serialized (and encrypted, `/Contents` exempt as `§36` decides), and `revision()` reports the absolute offset and length of both fields, so the signer (`D05 T01 §8`) patches them in place without moving a byte. Done when: `PdfIncrementalWriter.SignaturePlaceholder` patches a computed `/ByteRange` and a `/Contents` value into the output of an unencrypted and an AES-256 base, and the patched file parses strictly with every cross-reference offset still pointing at its object and reads both fields back.
- [x] Support chaining 3 consecutive incremental revisions (`Rev 1 -> Rev 2 -> Rev 3`) and verify `Document::open_memory` sees the latest state from each revision. Done when: `PdfIncrementalWriter.MultiRevisionChain` passes under ASan.
- [x] Every test that reopens a written file first parses it with `XrefTable::parse(bytes, kMaxXrefTotalEntries, /*allow_recovery=*/false)` (`Document::open_memory` recovers a damaged cross-reference, which would hide a wrong offset) and asserts the revision count and newest section kind, on bases with a zero and a nonzero header offset. Done when: `PdfIncrementalWriter.StrictReparse` passes, and fails when one emitted offset, the `/Prev` value or a stream `/Index` entry is changed in the output.
- [x] Completion review (`docs/reviews/01-engine/D01-T02-s5.md`): every staged value counts against `kMaxRevisionBytes` (strings, names, containers, stream payloads and a signature's reservation), and allocation keeps a mark below which every number is staged, so it never rescans what it handed out. Done when: `PdfIncrementalWriter.StagingLimit` refuses at a lowered limit with earlier edits kept and room freed by replacing or dropping, and `PdfIncrementalWriter.AllocationScales` holds 10,000 and 20,000 allocations to two probes each. Cancellation and progress are `§38`'s; the large-base gate is `D06 T01 §10`'s. -> XREF: D01 T02 §38
- [x] Commit: `pdf: add IncrementalWriter for non-destructive PDF revisions`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfIncrementalWriter.*:PdfXref.NewestSection` passes under ASan.

> **Verified:** 2026-10-25 | §5 | 9b40d0be97e16e21b3cbf40c24dc7a03b6a23a17
> **Review:** `docs/reviews/d01-t02-s5.json`

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
- [ ] Replay each glyph's `width_scale` (`§43`) by scaling its outline horizontally about its origin before the glyph-to-device transform, through DirectWrite and outline-only replay alike, without moving origins or advances; and draw nothing for a run whose font handle is null (`§43`'s advance-only glyphs), its positions kept. Done when: `PdfRasterizer.GlyphWidthScale` renders a `Times` line whose substitute `macron` and `summation` are fitted (scales below and above 1) and checks each glyph's painted ink width against its scaled outline and its origin unchanged, through both replay paths, and `PdfRasterizer.NullFontRun` renders a page holding an advance-only run without painting it. -> XREF: D01 T02 §43
- [ ] Replay glyph runs whose `LoadedFont` is `OutlineOnly` (`D01 T02 §2` `glyf` fallback, `§29` Type 1 and CFF) as filled paths from `LoadedFont::outline`, under the run's text matrix and CTM, with the same render-mode handling as DirectWrite runs. Done when: `PdfRasterizer.OutlineOnlyGlyphRuns` renders a `glyf`-fallback and a Type 1 run to golden images within the SSIM threshold of the DirectWrite rendering of the same glyphs. **Corrected 2026-10-24:** added from the `D01 T02 §2` plan review (gap 3).
- [ ] Render `§29`'s Type 1, name-keyed CFF and CID-keyed CFF fixtures as text at 8, 10 and 12 pt, at 100% and 200% scale, accents and thin strokes included, to reviewed golden images (filed by the `§29` completion review, `docs/reviews/01-engine/D01-T02-s29.md`). Done when: `PdfRasterizer.CharstringSmallText` matches its goldens within the SSIM threshold with no glyph missing or misplaced. -> XREF: D01 T02 §29
- [ ] Render `§30`'s Type 0 fonts from real PDFs, a CJK `/CIDFontType0` and an Office `/CIDFontType2` with Identity-H, and a vertical Identity-V line placed by `/W2` and `/DW2`, at 100% and 200% to reviewed golden images (filed by the `§30` completion review, `docs/reviews/01-engine/D01-T02-s30.md`). Done when: `PdfRasterizer.Type0Text` matches its goldens within the SSIM threshold with every glyph present and at its advance. -> XREF: D01 T02 §30
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
- [ ] Measure the longest single glyph call under `§29`'s bounds (a Type 2 glyph at 1,000,000 executed bytes and one at 100,000 path operators) inside a cancelled tile (filed by the `§29` completion review, `docs/reviews/01-engine/D01-T02-s29.md`). Done when: `PdfRasterizer.CancelDuringHeaviestGlyph` aborts in `< 10 ms`, or, if one call exceeds it, the glyph calls take the `CancelToken` after a dated correction to `font.h`'s frozen contract. -> XREF: D01 T02 §29
- [ ] Measure font loading inside a cancelled page compile at its bounds: `load_type0_font` with a 4 MiB embedded CMap of 100,000 mappings and a `/W` of 65,536 elements, and a 64 MiB CFF program (filed by the `§30` completion review, `docs/reviews/01-engine/D01-T02-s30.md`). Done when: `PdfInterpreter.CancelDuringFontLoad` aborts the compile in `< 10 ms` after the load, or, if one load exceeds it, the loaders take the `CancelToken` after a dated correction. -> XREF: D01 T02 §30
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

> **Started:** 2026-10-25T15:20Z

> **Groomed 2026-10-23:** the chain bound returned `ErrorCode::UnsupportedFeature`; it now returns `ErrorCode::ResourceLimit`, which `§18` adds (the code did not exist), and `§13` and `§14` depend on `§18`.

Acrobat Pro keeps an edit (a new layer, a renamed destination) unsaved until the user saves, and undoes it on request. Recto's rule is stricter: authoring always works on a copy and never modifies the open viewer document in place, and `recto::pdf::Document` is read-only. This section gives the engine the two pieces an actor needs for that: `ObjectChangeSet`, a plain value listing replaced and newly allocated indirect objects, and `StagedRevisionChain`, which appends each change set as one incremental revision (`§5`) to an in-memory copy of the opened bytes, reopens a working `Document` from that copy, undoes by truncating the last revision, and exports one squashed revision for the save path. It writes no file. It must not break `§5`'s byte-prefix rule (existing signatures stay valid), the `PdfResult<T>` contract, or the bounds on hostile input. The Recto improvement it keeps: the original file and the viewer's opened bytes are never touched until the save path (`D02 T01 §8`) writes a new file by temp-file-then-replace.

> **Corrected 2026-10-25 (plan validation against the stamped `§5`):** a reader follows at most `kMaxXrefRevisions` (256) revisions, so one revision per push would stop the chain after about 255 edits, far short of its 1,000-state bound and of a long markup or redaction session; the chain now compacts its oldest pushes into one revision when the next would pass that limit, and undo below the compacted point rebuilds instead of truncating. `IncrementalWriter::set_object` already writes numbers at or past `/Size` (`§5`), so the `set_new_object` extension is not needed. A change set records the revision it was built on, so one built before an undo cannot write over numbers the chain has since handed out. New numbers come from the writer (past the effective `/Size` and below rows ignored past it, `§5`).

> **Corrected 2026-10-25 (plan review, `docs/reviews/01-engine/D01-T02-s13.md`):** folded its six gaps: builders get the seeded change set from `begin_change()` (`§14` corrected to match), compaction runs after every transition so the next push and `begin_change()` always have room, counted from the base's own revisions, one retained-history budget covers working bytes, stored revisions and change sets, every transition is all or nothing, and the export and the encrypted and stream bases are checked for exactly the current state's values.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `ObjectChangeSet` holds only values (`IndirectRef` keys and `PdfValue`s) with no pointer into any `Document`, that a `StagedRevisionChain` and its working `Document` are owned and used by one thread only (the `DocumentActor` that creates it) and never shared, that it never mutates the base byte buffer or any other open `Document`, and that every buffer it exports is an immutable `std::shared_ptr<const std::vector<uint8_t>>`.

**Build order:**

1. Implement `src/pdf/staged_revisions.h` and `src/pdf/staged_revisions.cpp`: `ObjectChangeSet` (`replace(IndirectRef, PdfValue)`, `allocate(PdfValue) -> IndirectRef`, `merge(const ObjectChangeSet& later)`, `built_on() -> RevisionId`), `RevisionId` (a `uint64_t` value type), and `StagedRevisionChain` (`create(std::span<const uint8_t> base, std::string_view password, RevisionId base_revision, RevisionId first_new_revision)`, `begin_change() -> ObjectChangeSet`, `push(ObjectChangeSet)`, `undo()`, `redo()`, `working() -> const Document&`, `next_free_object()`, `depth()`, `current_revision() -> RevisionId`, `export_squashed() -> PdfResult<ExportedRevision { RevisionId revision; std::shared_ptr<const std::vector<uint8_t>> bytes; }>`), every fallible call returning `PdfResult`. Done when: `staged_revisions.cpp` compiles cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/pdf_staged_revisions_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfStagedRevisions.*` passes under ASan.

- [x] Implement `ObjectChangeSet` in `src/pdf/staged_revisions.h`, made by `begin_change()` with the current revision and `next_free_object()` (the first number `IncrementalWriter::create` on the working document would allocate, exposed as `IncrementalWriter::first_new_number()`), `allocate` numbering new objects from there in order, and `merge` keeping the later value for a reference set twice and every allocated reference. `push` refuses a set built on another revision (`InvalidArgument`), so a set built before an `undo` cannot reuse numbers handed out since. Done when: `PdfStagedRevisions.MergeOrder` shows the later value wins and no allocated reference is lost, and `PdfStagedRevisions.StaleChangeSet` gets `InvalidArgument` for a set built before an `undo` with the state unchanged.
- [x] Implement `StagedRevisionChain::push` writing the change set through `IncrementalWriter` (`§5`) on the current working document, appending the revision to the in-memory working bytes, and reopening the working `Document` from them with the retained password; a push that fails (the writer refuses a value or a limit, or the reopen fails) leaves the chain exactly as it was, and `create` returns the writer's refusal for a base it cannot extend (a recovered one is `DamagedFormat`, so the caller saves a full copy). Done when: `PdfStagedRevisions.PushPopRedo` sets page 1 `/Rotate 90` on `render/simple-1p.pdf` and the working document reports rotation `90`, and `PdfStagedRevisions.FailedPushKeepsState` pushes a set holding a non-finite real and finds the same bytes, revision and depth after.
- [x] Implement `undo` by truncating the working bytes to the length before the last revision and `redo` by re-appending the stored revision bytes when the buffer still ends where they were written (writing the stored change set again otherwise), with a new `push` after an `undo` clearing the redo stack. Done when: `PdfStagedRevisions.PushPopRedo` reads rotation `0` after `undo`, `90` after `redo`, and `redo` fails with a typed error after a fresh `push`.
- [x] Compact inside the push or redo that finds no room for one more revision (the room is `kMaxXrefRevisions` less the base's own revisions; `begin_change()` takes its number from `IncrementalWriter::first_new_number`, which needs no writer, so neither meets a refusing writer; **Corrected 2026-10-25:** was "after every transition that leaves no room", which would rewrite the bytes even when the user undoes next): the chain rewrites them as the base, one revision holding the oldest pushes merged, and the newest pushes as their own revisions (half the room left free; with room for one revision only, every state is the base plus its merged changes), keeping every state's change set, so `undo` to a state inside the compacted part rebuilds the base plus that state's merged changes instead of truncating; `create` refuses a base with no room (`ResourceLimit`). Done when: `PdfStagedRevisions.ManyEdits` pushes 600 edits on `render/simple-1p.pdf`, each allocating an object, each working buffer parsing strictly at no more than `kMaxXrefRevisions` revisions, and undoes all 600 one by one with spot checks of the edited value at every 50th state, ending byte for byte at the base; `PdfStagedRevisions.NearLimitBases` repeats 20 edits with allocations on bases already holding 253, 254 and 255 revisions, and gets `ResourceLimit` from `create` at 256.
- [x] Give every state of the chain a `RevisionId`: the base state carries `base_revision`, each `push` assigns the next unused id starting at `first_new_revision` (never reusing an id within the chain, including ids dropped from the redo stack), `undo` returns `current_revision()` to the previous state's id and `redo` restores the same id it had before the `undo`. Done when: `PdfStagedRevisions.RevisionIds` pushes A and B, undoes B, pushes C, and reads ids `base`, `a`, `b`, `a`, `c` with `c` greater than `b`.
- [x] Keep the base bytes as an untouched prefix of every working buffer. Done when: `PdfStagedRevisions.BasePrefixPreserved` finds `std::memcmp` of the base length equal to `0` after 3 pushes, 2 undos and 1 redo. Cheaper substitute: rewriting the whole file on each edit.
- [x] Implement `export_squashed` as the base bytes plus one revision holding the merged change sets of exactly the current state (never an undone edit or a discarded redo branch), writing allocated objects at their recorded numbers through `IncrementalWriter::set_object`, which takes numbers at or past the base `/Size` (`§5`); at the base state it is the base bytes unchanged. Done when: `PdfStagedRevisions.SquashEquivalent` reopens the squashed buffer and resolves every changed and allocated object equal to the working document, at the same object numbers and with every reference resolving, and the export carries `current_revision()`, after 3 pushes that each allocate one object, after a partial undo, after a full undo, after a redo, after a branch (undo then a new push), and after a compaction.
- [x] Support classic `xref`, `/Type /XRef` stream and encrypted bases through `IncrementalWriter`'s existing paths. Done when: `PdfStagedRevisions.CompressedAndEncrypted` pushes, undoes, redoes and squashes on `render/compressed-xref-2p.pdf` and on `render/encrypted-aes-256.pdf` with `recto-test`, reading every changed and allocated value back after each step (decrypted on the encrypted base, where the edited string is absent from the bytes in plain).
- [x] Bound the chain at 1,000 states, 100,000 objects per change set and 256 MiB of retained history (the working bytes beyond the base, every stored revision's bytes and every state's change set, counted as `IncrementalWriter` counts staged values, exposed as `staged_value_bytes`), checked before anything is copied, returning a typed `PdfError` with `ErrorCode::ResourceLimit` (`§18`) naming the limit, and zero the retained password with `SecureZeroMemory` on destruction. Done when: `PdfStagedRevisions.Bounds` hits each limit with a typed error and the chain unchanged, the history limit reached by replacing a large compressible stream again and again across a compaction and an undo.
- [x] Make every transition all or nothing: `push`, `undo`, `redo` and a compaction build the new bytes and reopen the new working document before changing anything, so a failure leaves the bytes, the working document, `current_revision()` and both history stacks as they were. Done when: `PdfStagedRevisions.TransitionsAreAtomic` injects a write failure (`detail::ScopedSerializeAllocFailure`) and a reopen failure (`detail::ScopedWorkingReopenFailure`) into a push, a truncating undo, a rebuilding undo across the compacted part, a redo that appends and a redo that writes again, and each time reads the same state, then succeeds without the injection.
- [x] Completion review (`docs/reviews/01-engine/D01-T02-s13.md`): an allocation failure anywhere in a transition is `OutOfMemory`, never a terminated process; the history's union is not held to the per-edit object limit, so every accepted state exports; undo and redo check the history budget before they publish, and merges check their bound before copying. Done when: `PdfStagedRevisions.AllocationFailureIsTyped`, `PdfStagedRevisions.HistoryPastOneEditsLimit` and `PdfStagedRevisions.UndoRedoRespectHistory` pass. Shared bytes, latency and a longer history are `§40`'s; cancellation is `§38`'s. -> XREF: D01 T02 §40
- [x] Commit: `pdf: add ObjectChangeSet and the in-memory StagedRevisionChain`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfStagedRevisions.*` passes under ASan, and making `undo` reopen the working document without truncating the bytes fails `PdfStagedRevisions.PushPopRedo`.

> **Verified:** 2026-10-25 | §13 | df40805611166b8afd978b9ca9240da9ba4d6d0c
> **Review:** `docs/reviews/d01-t02-s13.json`

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

- [ ] Define `OcgEditKind` (`CreateLayer`, `RenameLayer`, `DeleteLayer`, `SetLayerUsage`, `ReorderLayers`, `MergeLayers`, `ImportPageAsLayer`, `ImportImageAsLayer`, one per builder `build_create_layer`, `build_rename_layer`, `build_delete_layer`, `build_set_layer_usage`, `build_reorder_layers` and `build_merge_layers` of `D02 T03 §23` and `build_import_page_as_layer` and `build_import_image_as_layer` of `D02 T03 §24`), `recto::pdf::OcgBuildResult { ObjectChangeSet changes; std::optional<IndirectRef> created_layer; }` in `src/pdf/ocg_build.h`, `using OcgEditOutput = recto::pdf::OcgBuildResult;` and `OcgEdit { OcgEditKind kind; std::string label; std::function<PdfResult<std::optional<recto::pdf::IndirectRef>>(const recto::pdf::Document& working, recto::pdf::ObjectChangeSet& changes, const CancelToken&)> build; }` in `src/core/ocg_edit.h`, with `label` UTF-8 of at most 128 characters used as the undo label; the session hands each builder the change set from the chain's `begin_change()` (`§13`, seeded with the revision and the next free number) and pushes that same set, the builder returning the created layer, if any (**Corrected 2026-10-25** by the `§13` plan review: the builder took only the working document, which has no `next_free_object()`, and returned its own set). A builder never mutates `working`, allocates every new object through `ObjectChangeSet::allocate` (numbered from the chain's `next_free_object()`, `§13`) so `created_layer` names the same object in the working document and in the export, and takes any second file (the import source of `D02 T03 §24`) as a path and password captured by value, opening its own `Document` inside `build`. Done when: the header compiles in `recto-core` and `recto-tests`, and `src/pdf/ocg_build.h` compiles in `recto-pdf` with no include path to `src/core`.
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
- [ ] Generate `src/pdf/cmap_data.cpp` with `scripts/gen-cmap-data.py` from the pinned Adobe `cmap-resources` release (BSD-3-Clause, commit and SHA-256 recorded in `src/pdf/cmap-resources/SOURCE.md`, notice added to `NOTICE.md`) for `Identity-H/V`, `UniJIS-UCS2-H/V`, `UniJIS-UTF16-H/V`, `UniGB-UCS2-H/V`, `UniGB-UTF16-H/V`, `UniCNS-UCS2-H/V`, `UniCNS-UTF16-H/V`, `UniKS-UCS2-H/V`, `UniKS-UTF16-H/V` and the legacy `90ms-RKSJ`, `GBK-EUC`, `ETen-B5` and `KSCms-UHC` families, with `usecmap` resolved, and the reverse `Adobe-Japan1`, `Adobe-GB1`, `Adobe-CNS1` and `Adobe-Korea1` CID-to-Unicode maps. Done when: `python scripts/gen-cmap-data.py --check` regenerates the file byte for byte and the compiled data adds under 2 MB to `recto-pdf.lib`.

> **Corrected 2026-10-26:** (found by `D01 T02 §27`) the pinned `cmap-resources` record was planned under `third_party/cmap-resources/`, a tree the fixed repository layout (foundation plan section 3.2) does not have and `tests/scripts/test_engine_dependency_policy.py` refuses; it is `src/pdf/cmap-resources/`.
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
- [ ] Count colour conversion work against the page's budgets: each tint transform evaluation (`Function::evaluate`, `§33`) counts toward `max_operators` in proportion to its program's length, and an image row's colour conversion checks the wall-clock deadline, so a DeviceN image with a long calculator transform truncates the page instead of running for minutes (filed by the `§33` completion review, `docs/reviews/01-engine/D01-T02-s33.md`). Done when: `PdfInterpreter.CeilingColorConversion` truncates a page whose 10,000 x 10,000 DeviceN image uses a 60,000-operator calculator transform within the wall-clock budget, keeping the content before it.
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

> **Started:** 2026-10-25T23:16Z

The fourteen standard fonts (ISO 32000-1 section 9.6.2.2) need their exact widths and font boxes even when a PDF does not embed them, and a face to draw with. Adobe's Core14 AFM files are the primary source; their licence allows use, copying and distribution with the notice kept, which is compatible with Recto's GPL-3.0-or-later.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the generated metrics are `constexpr` tables with no mutable state, and that a standard font resolves to an immutable `LoadedFont` shared by every thread.

**Build order:**

1. Vendor the 14 Adobe Core14 AFM files with their licence notice under `src/pdf/adobe-core14-afm/` and generate `src/pdf/standard14_metrics.inc` with `scripts/build-font-metrics.py`. Done when: `python scripts/build-font-metrics.py --check` regenerates the file byte for byte.
2. Expose the metrics and the system-face mapping from `src/pdf/font.h`. Done when: `PdfFont.Standard14Metrics` passes under ASan.

- [x] Vendor `Courier`, `Courier-Bold`, `Courier-Oblique`, `Courier-BoldOblique`, `Helvetica` (four), `Times-Roman`, `Times-Bold`, `Times-Italic`, `Times-BoldItalic`, `Symbol` and `ZapfDingbats` AFM files under `src/pdf/adobe-core14-afm/` with Adobe's licence text and a `README.md` naming the source and its SHA-256 sums. Done when: the files and sums are committed and `tests/scripts` checks the sums.
- [x] Generate `src/pdf/standard14_metrics.inc` (glyph name, code, width, glyph bounding box per glyph; `FontBBox`, `Ascender`, `Descender`, `CapHeight`, `ItalicAngle`, `IsFixedPitch` per font) with `scripts/build-font-metrics.py`, with a `--check` mode run by `tests/scripts`. Done when: `--check` passes and fails after a one-byte edit of an AFM.
- [x] Resolve a `/BaseFont` to its standard font, including the common aliases (`Arial`, `Arial,Bold`, `ArialMT`, `TimesNewRoman`, `TimesNewRomanPS-BoldMT`, `CourierNew` and their style suffixes) and subset prefixes (`ABCDEF+`). Done when: `PdfFont.Standard14Aliases` resolves each alias to its font and leaves unrelated names alone.
- [x] Map each standard font to a Windows 11 face for drawing (`Times New Roman`, `Arial`, `Courier New` with matching weight and style; `Symbol` to the Windows `Symbol` face, `ZapfDingbats` to `Segoe UI Symbol`), loaded once per process into an immutable `LoadedFont` shared by every thread (`load_standard14_face`), keeping the AFM widths for layout so text never reflows. Done when: `PdfFont.Standard14Faces` loads each mapped face and draws through it as the item below says.
- [x] Prove the metrics independently of the generator: `PdfFont.Standard14Metrics` parses each vendored AFM itself and compares every glyph record (name, code including unencoded `-1`, width, box) and every font-level field (`FontBBox`, `Ascender`, `Descender`, `CapHeight`, `ItalicAngle`, `IsFixedPitch`; an AFM without a field gives none, never zero) with the generated tables, and `tests/scripts` regenerates from a copy with one width, one box, one font metric and one glyph record each corrupted and requires `--check` to fail. Done when: both pass, and corrupting any one table entry in `standard14_metrics.inc` fails the C++ test.
- [x] Join metrics and face for the consumers (`§4`, `§17`): a glyph's layout advance is its AFM width, found by name (`standard14_glyph_by_name`) or built-in code (`standard14_glyph_by_code`); its face glyph is `standard14_face_glyph(font, code_point)`, the glyph and the face it is in for a Unicode scalar through the face's `cmap`, and for `Symbol` the built-in code through the face's symbol `cmap` (`0xF000 + code`), the Euro (0xA0, which the Windows `Symbol` face lacks) from `Segoe UI Symbol` at U+20AC; which name and Unicode a PDF code means is `§28`'s. Done when: `PdfFont.Standard14Faces` draws every ASCII glyph of the twelve Latin fonts, every `Symbol` code and the `ZapfDingbats` codes `0x21` to `0x7E` with a nonzero face glyph and a nonempty outline beside its AFM advance, and a code point the face lacks gives none.
- [x] Failure contract for `load_standard14_face`: a family or face the system collection lacks is `UnsupportedFeature` naming the family, never another font; a DirectWrite failure (collection, family, face, file stream) keeps its step and `HRESULT` as `load_embedded_font` does; allocation failure is `OutOfMemory`; a failure is not cached, so a later call can succeed; and a returned font stays usable after the collection, face and stream objects it was read through are released. Done when: `PdfFont.Standard14FaceFailures` injects each step's failure through `detail::ScopedFontLoaderFailure`, and a missing family through a test hook, and checks each result and the retry.
- [x] Completion review (`docs/reviews/01-engine/D01-T02-s27.md`): one caller at a time loads each face, so callers that ask at once share one read and decode, and a face DirectWrite matches at another weight, width or style is refused like a simulated one; every glyph of every font is drawn by name and its face advance matched to the AFM width (`kKnownWidthDifferences` lists the 15 reviewed design differences). Done when: `PdfFont.Standard14Faces` counts one file read under eight racing callers and none on a warm lookup (cold about 2 ms, warm a few microseconds on this host), `PdfFont.Standard14FaceFailures` refuses a partial family it finds in the system collection, and `PdfFont.Standard14Repertoire` passes. Drawing to the AFM advance is `§4`'s, drawable Unicode for private-use names `§28`'s, glyph-sheet goldens `D06 T01 §1`'s. -> XREF: D01 T02 §4 -> XREF: D01 T02 §28 -> XREF: D06 T01 §1
- [x] Commit: `pdf: Standard 14 font metrics from the Adobe Core14 AFM files`

> **Corrected 2026-10-26:** (the gates) the AFM files were planned under `third_party/adobe-core14-afm/`, a tree the fixed repository layout (foundation plan section 3.2) does not have and `tests/scripts/test_engine_dependency_policy.py` refuses (D00 T01 section 2: no `third_party` tree); they live in `src/pdf/adobe-core14-afm/`, beside the table generated from them.

> **Corrected 2026-10-26:** (validation before the build, and the plan review in `docs/reviews/01-engine/D01-T02-s27.md`) Adobe's own downloads are gone: every Adobe URL for `Core14_AFMs.zip` and `Core14_AFMs.tar` answers 404 today. The files come from two independent mirrors of Adobe's set that agree line for line on all 14: `tecnickcom/tc-font-core14-afms`, vendored byte for byte with its CRLF line ends, and Apache PDFBox's bundled copy (its newlines doubled), both named with the date and sums in the README, with Adobe's `MustRead.html` notice; `.gitattributes` keeps the directory byte-exact (`-text`). `Symbol` maps to the Windows `Symbol` face (`symbol.ttf`, which carries Adobe Symbol's own encoding in its symbol `cmap`) rather than `Segoe UI Symbol`, which keeps only `ZapfDingbats`. The plan review found the plan silent on how AFM metrics and the face combine, on the face's failures, and on proving every metrics field; the last three items above add them. The API, in `src/pdf/font.h`: `enum class Standard14Font`; `standard14_from_base_font(std::string_view)` returning the font or none; `standard14_metrics(Standard14Font)` returning the `constexpr` table (font box, and ascender, descender, cap height, italic angle and fixed pitch where the AFM gives them, and per glyph its name, built-in code, width and box, in AFM units of 1/1000 em); `standard14_glyph_by_name` and `standard14_glyph_by_code`; `load_standard14_face(Standard14Font)` returning the shared `LoadedFont` of its Windows face (the face file's bytes read through DirectWrite once and loaded with `load_embedded_font`); and `standard14_face_glyph`.

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFont.Standard14*` passes under ASan with 0 warnings (`Standard14Metrics`, `Standard14Aliases`, `Standard14Faces`, `Standard14Repertoire`, `Standard14FaceFailures`), and `python scripts/build-font-metrics.py --check` exits 0.

> **Verified:** 2026-10-26 | §27 | 8cfc652c856be6a30a5eddbf0aaf64a55ee484c4
> **Review:** `docs/reviews/d01-t02-s27.json`

## 28. Simple-font encodings, the Adobe Glyph List, `/Differences` and `/ToUnicode` CMaps

> **Started:** 2026-10-26T01:13Z

Character codes in a simple font (`/Type1`, `/TrueType`, `/Type3`) map to glyphs through the font's encoding and to Unicode for search, copy and accessibility (ISO 32000-1 sections 9.6.6, 9.10). This section owns both maps.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the encoding, glyph-name and `/ToUnicode` maps are immutable once built and are read without locks by every thread that holds the font.

**Build order:**

1. Generate the base encodings (ISO 32000-1 Annex D) and the Adobe Glyph List into `src/pdf/glyph_names.inc` with `scripts/build-glyph-names.py`, and implement encoding and `/ToUnicode` parsing in `src/pdf/font_encoding.cpp`. Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below to `tests/recto-tests/pdf_font_tests.cpp`. Done when: they pass under ASan.

- [x] Base encodings `StandardEncoding`, `WinAnsiEncoding`, `MacRomanEncoding`, `MacExpertEncoding`, and the built-in encodings of `Symbol` and `ZapfDingbats`, generated from ISO 32000-1 Annex D tables, and the Adobe Glyph List (`glyphlist.txt`, `zapfdingbats.txt`, BSD-3-Clause, vendored under `src/pdf/adobe-glyph-list/` with sums). Done when: `python scripts/build-glyph-names.py --check` passes and `PdfFont.BaseEncodings` checks every code of each table.

> **Corrected 2026-10-26:** (found by `D01 T02 §27`) the vendored Glyph List files were planned under `third_party/`, a tree the fixed repository layout (foundation plan section 3.2) does not have and `tests/scripts/test_engine_dependency_policy.py` refuses; they live under `src/pdf/`.
- [x] `/Encoding` dictionaries: `/BaseEncoding` and `/Differences [code /name ...]`, names resolved through the AGL, `uniXXXX`, `uXXXX[XX]` and ligature names (`f_i`). Done when: `PdfFont.EncodingAndDifferences` resolves remapped codes, unknown names and runs of consecutive codes.
- [x] The effective encoding (ISO 32000-1 9.6.6.1 and 9.6.6.2): a named `/Encoding` is that base; a dictionary's `/BaseEncoding`, or when it has none, the font's built-in encoding, is the base its `/Differences` apply over; no `/Encoding` is the built-in encoding. The built-in encoding is the caller's to supply (`SimpleEncoding::build` takes it as 256 names): `StandardEncoding` for a non-symbolic standard Latin font, the AFM codes for `Symbol` and `ZapfDingbats` (`§27`), the font program's own for an embedded Type 1 (`D01 T02 §29` reads it), none for a TrueType font (its `cmap` decides, below). Done when: `PdfFont.EffectiveEncoding` covers a named encoding, a dictionary with and without `/BaseEncoding`, no `/Encoding`, and `/Differences` over a custom built-in encoding. -> XREF: D01 T02 §29
- [x] TrueType glyph selection for simple fonts (ISO 32000-1 9.6.6.4), with the font descriptor's flags and the encoding in the order the clause gives: a named `MacRomanEncoding` or `WinAnsiEncoding`, a `/Differences` encoding or the Nonsymbolic flag selects the name path (code to glyph name, then a `(3,1)` subtable through the name's Unicode, else a `(1,0)` subtable through the Mac OS standard Roman code of the name, else the `post` table's glyph names, formats 1 and 2), otherwise the code path (a `(3,0)` subtable at the code and at `0xF000`, `0xF100` and `0xF200` plus the code, else a `(1,0)` subtable at the code, else the code as a glyph index). Done when: `PdfFont.TrueTypeCmapSelection` asserts the exact glyph ID each branch selects with fixture fonts built in the test.
- [x] Hostile `cmap` and `post` tables: a truncated header or subtable, a subtable offset past the table, a format 4 `idRangeOffset` outside the glyph array, segment counts and format 12 group counts larger than their table, and reversed or overlapping format 12 ranges are each refused as `DamagedFormat` (the font then draws through the next path the clause allows), with every read bounds-checked and work linear in the table. Done when: `PdfFont.TrueTypeCmapMalformed` passes under ASan with each case's typed error.
- [x] `/ToUnicode` CMap streams: `begincodespacerange` with 1- to 4-byte ranges, `beginbfchar`, `beginbfrange` with the increment and array forms, UTF-16 surrogate pairs and multi-character targets (`<00660069>` is `fi`), `usecmap` refused with a typed error, and bounds on ranges, entries and target length. Done when: `PdfFont.ToUnicodeCMap` decodes each form and refuses malformed and oversized CMaps without a crash.
- [x] Unicode for each code in the order readers use: `/ToUnicode`, then the encoding's glyph name through the AGL, then the TrueType `cmap`, else none (never a guessed character). Done when: `PdfFont.UnicodeFallbackOrder` checks each step.
- [x] Glyph names the AGL maps to Adobe's private-use area get the Unicode character a Windows face can draw when a standard font is drawn through one (`commaaccent`, AGL U+F6C3, is U+0326; filed by the `§27` completion review, which found no Windows face with U+F6C3). Done when: `PdfFont.DrawableUnicode` maps every glyph name of the twelve Latin Core14 AFM files to a scalar `§27`'s `standard14_face_glyph` finds in that font's face, and every `ZapfDingbats` name through `zapfdingbats.txt` likewise (`Symbol` draws by its built-in code, and its `apple`, private-use U+F8FF in the AGL, maps to none); its expected scalars are a table of its own (independent of the AGL), and `commaaccent` draws below the baseline, a comma beneath, in each Latin face. -> XREF: D01 T02 §27
- [x] Completion review (`docs/reviews/01-engine/D01-T02-s28.md`): a range's increment never makes a surrogate; an array range gives exactly one string per code without wrapping; a glyph name target keeps the 512-byte budget; a damaged `cmap` subtable or `post` table is left out with its typed error (`TrueTypeTables::damage`) and the rest of the font stays usable, a short `post` header and a missing name string included; a lookup that cannot hold its characters is `OutOfMemory`, never a fallback; and every drawable name's glyph is shown to be its own by the metric-compatible faces' widths, `commaaccent` by its shape against the face's comma. Done when: `PdfFont.ToUnicodeCMap`, `PdfFont.TrueTypeCmapMalformed`, `PdfFont.ToUnicodeOutOfMemory` and `PdfFont.DrawableUnicode` pass. Drawing simple fonts is `§4`'s, the Type 1 built-in encoding `§29`'s, extraction `D03 T01 §1`'s, the comparison with Acrobat Pro `D06 T01 §7`'s. -> XREF: D01 T02 §4 -> XREF: D01 T02 §29 -> XREF: D03 T01 §1 -> XREF: D06 T01 §7
- [x] Commit: `pdf: simple-font encodings, glyph names and ToUnicode CMaps`

> **Corrected 2026-10-26:** (validation before the build) Sources. ISO 32000-1 Annex D is not machine-readable here, so its tables are transcribed into `src/pdf/pdf-encodings/annex-d.txt` (one `encoding code name` line each) with a README naming how each table was checked: `StandardEncoding`, `MacExpertEncoding` and `Symbol` agree in two independent transcriptions (pdf.js at `606bd6a`, Apache PDFBox at `84492c4`), and `StandardEncoding`, `Symbol` and `ZapfDingbats` equal the codes of Adobe's Core14 AFM files, which `scripts/build-glyph-names.py` checks on every run; where the transcriptions differ, Annex D's own notes decide (`WinAnsiEncoding` has `space` also at 0xA0, `hyphen` also at 0xAD and `bullet` at every unused code above 040; `MacRomanEncoding` has `space` also at 0xCA; PDFBox writes `nbspace` and `sfthyphen` there, and lacks ZapfDingbats 0x80 to 0x8D, which Adobe's AFM encodes). `MacRomanEncoding` (Annex D, 208 codes) and the Mac OS standard Roman character set 9.6.6.4 uses for a `(1,0)` subtable (those plus 15 Mac OS symbols, `notequal` 0xAD to `apple` 0xF0, and `Euro` for `currency` at 0xDB: Table 115, corrected after review round 1) are two tables. The API, in `src/pdf/font_encoding.h`: `enum class BaseEncoding` (Standard, MacRoman, WinAnsi, MacExpert, Symbol, ZapfDingbats) and `base_encoding_name(BaseEncoding, uint8_t)`; `SimpleEncoding`, built once from a base and a `/Differences` array and then immutable (`name(code)`); `glyph_name_unicode(name, bool zapf_dingbats)` returning the characters the AGL specification's algorithm gives (suffix after the first `.` dropped, `_` components, AGL, `uniXXXX` runs, `uXXXX` to `uXXXXXX`, surrogates refused); `TrueTypeCmap`, parsed from a font program's `cmap` (formats 0, 4, 6 and 12, bounded, every offset checked) with `simple_truetype_glyph`; `ToUnicodeCMap` (bounds: 100 codespace ranges, 65,536 mapped codes, 512 bytes of target per code, 16 MiB of CMap stream); `simple_font_unicode` for the fallback order; and `drawable_unicode`. `usecmap` is refused as `UnsupportedFeature`, which the fallback order treats as no `/ToUnicode`. The plan review (`docs/reviews/01-engine/D01-T02-s28.md`) found the TrueType clause incomplete (the `(1,0)` and `post` name paths, the `0xF100` and `0xF200` ranges, the flags' precedence), the effective encoding undefined when `/Encoding` or `/BaseEncoding` is absent, `DrawableUnicode` provable by any drawable character, and malformed `cmap` tables untested: the items above add them. The TrueType step of the fallback is the `(3,1)` subtable read backwards: the glyph a code selects, back to the Unicode scalar that maps to it.

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFont.*Encoding*:PdfFont.ToUnicode*:PdfFont.TrueTypeCmap*:PdfFont.UnicodeFallback*:PdfFont.DrawableUnicode` passes under ASan with 0 warnings, and `python scripts/build-glyph-names.py --check` exits 0.

> **Verified:** 2026-10-26 | §28 | 5eb86444c88fc93dbcc22ac6c05661dbbf2b7215
> **Review:** `docs/reviews/d01-t02-s28.json`

## 29. Type 1 and CFF charstrings to glyph outlines (`eexec`, Type 2, `seac`, subroutines)

> **Started:** 2026-10-26T03:12Z

Type 1 fonts (`/FontFile`) and bare CFF programs (`/FontFile3` with `/Type1C` or `/CIDFontType0C`) cannot be loaded by DirectWrite, so their glyphs are read into outlines here and drawn as paths (ISO 32000-1 sections 9.6.2 and 9.9; Adobe Type 1 Font Format; Adobe Technical Notes 5176 and 5177).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that a Type 1 or CFF program is parsed once (its decrypted charstrings and subroutines, `INDEX` offsets, charset, encoding, `FDSelect` and private dictionaries) into the immutable `LoadedFont` on the `DocumentActor` thread, and that each glyph's outline is computed per call from that immutable data with no lazy cache, as `font.h`'s contract has it, so `RenderPool` workers may call concurrently.

**Build order:**

1. Implement `src/pdf/font_charstrings.cpp` reading Type 1 and CFF programs into `LoadedFont` (kind `OutlineOnly`, drawn as paths like any outline-only font). Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below. Done when: they pass under ASan.

- [x] Type 1 programs: the cleartext and `eexec` portions by `/Length1`, `/Length2`, `/Length3` (binary and hexadecimal), `eexec` and charstring decryption with `lenIV`, `/Subrs`, `/CharStrings`, `/FontMatrix` and the built-in `/Encoding`. Done when: `PdfFont.Type1Outlines` reads a PFA-style and a PFB-style fixture to the same outlines.
- [x] Type 1 charstrings (`hsbw`, `sbw`, `rmoveto`, `rlineto`, `rrcurveto`, the short forms, `closepath`, `callsubr` and `return`, `div`, `pop`, `setcurrentpoint`, `seac` accented glyphs from `StandardEncoding`, the flex sequence through `callothersubr` 0 to 2 with its seven collected moves, hint replacement through `callothersubr` 3 and `pop`, hints and `dotsection` skipped), with a 10-level subroutine depth and a stack of 24. Done when: `PdfFont.Type1Outlines` matches reference outlines and advances for fixtures that use every one of those operators, including a complete flex and an `seac` glyph.
- [x] CFF programs: header, `INDEX` and `DICT` parsing, charsets (predefined and formats 0 to 2), encodings (predefined Standard and Expert, formats 0 and 1 with supplements), `FDArray` and `FDSelect` (formats 0 and 3) for CID-keyed fonts with each FD's private dictionary, local subroutines, widths and font matrix, and Type 2 charstrings (every path operator, `hintmask` and `cntrmask` skipped with their mask bytes, `callsubr` and `callgsubr` with the bias, `endchar` with `seac` arguments, the arithmetic, stack, storage and conditional operators `and` to `roll`), with a 10-level depth and a stack of 48. The width is present when the first stack-clearing operator has one argument more than it takes (`hstem`, `vstem` and their `hm` forms, `hintmask` and `cntrmask` with an odd count; `rmoveto` 3, `hmoveto` and `vmoveto` 2, `endchar` 1 or 5): that argument plus `nominalWidthX`, else `defaultWidthX`. Done when: `PdfFont.CffOutlines` matches reference outlines and advances for a name-keyed and a CID-keyed fixture whose charstrings use every operator, with explicit and default widths through each kind of first operator.
- [x] Glyph lookup and the CID machinery: `glyph_for_name` through a Type 1 program's `/CharStrings` and a CFF charset, `glyph_for_cid` through a CID-keyed CFF charset that is not the identity, and an `FDSelect` that puts glyphs in different FDs with different local subroutines, widths and matrices. Done when: `PdfFont.CffCidMapping` draws a glyph from each FD with that FD's subroutines, width and matrix, and finds glyphs by CID where CID and glyph ID differ.
- [x] Every outline through `LoadedFont`: `shape`, `outline`, `advance` and `ink_box` agree for both program kinds, through a non-default `/FontMatrix` (scaled and skewed), and for an empty glyph (an advance, no outline, no ink box). Done when: `PdfFont.CharstringShapes` passes.
- [x] Hostile programs refused with typed errors and bounded work: offsets outside the buffer, `INDEX` counts past the data, subroutine recursion, stack overflow and underflow, a charstring longer than 65,535 bytes, a glyph past 100,000 path operators, and a glyph past 1,000,000 executed charstring bytes shared across every subroutine call and both `seac` components (fan-out through subroutines that draws nothing), an `seac` whose component is itself an `seac`. Done when: `PdfFont.HostileCharstringsRefused` asserts each case's failed result and error code (`DamagedFormat` for malformed input, `ResourceLimit` for a bound passed) under ASan, beside the boundary cases that pass (depth 10, stack 24 and 48, 100,000 operators).
- [x] Hand a Type 1 or CFF program's built-in encoding (its `/Encoding` array, `StandardEncoding`, or the CFF encoding and charset) to `§28`'s `SimpleEncoding::build` as its 256 glyph names (filed by the `§28` completion review). Done when: `PdfFont.Type1BuiltInEncoding` builds a `SimpleEncoding` from an embedded program's custom encoding with `/Differences` over it. -> XREF: D01 T02 §28
- [x] Completion review (`docs/reviews/01-engine/D01-T02-s29.md`): no infinity or NaN reaches a point in text space, an advance, an accent code or an index; a glyph name past 255 bytes names nothing; the subroutine INDEXes of all FDs hold at most 1,048,576 offsets (`ResourceLimit`, `FontTableEntries`); every program ends by its terminator, `return` only ends a subroutine, Type 1 `pop` needs a value and each Type 2 path operator takes exactly its arguments; Type 2 `dotsection` is a no-op. Done when: `PdfFont.HostileCharstringsRefused` asserts each case, the boundaries that pass beside them.
- [x] Commit: `pdf: Type 1 and CFF charstrings to glyph outlines`

> **Corrected 2026-10-26:** (validation before the build) The Freeze check said outlines are "decoded once" with "no lazy decoding" on workers, which contradicts `font.h`'s frozen contract (outlines computed per call from immutable bytes, no lazy caches, as the stamped `glyf` path does) and would hold every glyph of a 20,000-glyph CID font in memory; it now asks for one parse into immutable data and per-call outlines. The section named no loader, no place in `LoadedFont`, no units and no source for CFF's predefined data; the contract is: `load_charstring_font(program, CharstringFormat, lengths)` in `src/pdf/font.h` (`CharstringFormat::Type1` with `/Length1` to `/Length3`, `Type1C`, `CIDFontType0C`), returning a `LoadedFont` of kind `OutlineOnly` (so the path rasterizer of `§6` draws it like any outline-only font; plan review gap 5 replaced the separate kinds first written here) with `glyph_for_name`, `glyph_for_cid` and `built_in_encoding()` (256 glyph names for `§28`) on `LoadedFont`, none for a font without them; outlines and advances in text space through the program's `/FontMatrix` (the CFF default 0.001, an FD's matrix composed with the top one for CID fonts); a Type 2 width from the charstring's first odd operand plus `nominalWidthX`, else `defaultWidthX`; hint operators skipped; `seac` and `endchar`'s accent form through `StandardEncoding` (`§28`). The plan review (`docs/reviews/01-engine/D01-T02-s29.md`) corrected the Type 2 width rule (the first stack-clearing operator's arity, not an odd count), added the operators real fonts use beyond the path ones, a shared per-glyph work budget, typed refusal assertions with passing boundaries, and fixtures for the mappings and per-FD dictionaries: the items above carry them. The 391 CFF standard strings, the Expert and Expert Subset charsets and the CFF Expert encoding (Adobe Technical Note 5176, Appendices A to C) are generated into `src/pdf/cff_data.inc` by `scripts/build-cff-data.py` from a transcription in `src/pdf/pdf-encodings/cff-standard-data.txt`, taken from two independent transcriptions (pdf.js and Apache PDFBox) that must agree, with `--check` run by `tests/scripts`. Bounds: a program of at most 64 MiB, a charstring of 65,535 bytes, 100,000 path operators per glyph, a subroutine depth of 10, a stack of 24 (Type 1) and 48 (Type 2), `INDEX` counts and offsets inside their data.

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFont.Type1*:PdfFont.Cff*:PdfFont.CharstringShapes:PdfFont.HostileCharstrings*` passes under ASan with 0 warnings, and `python scripts/build-cff-data.py --check` exits 0.

> **Verified:** 2026-10-26 | §29 | 3c2f13cd8919461d6fdfee557bb8ec675440acc3
> **Review:** `docs/reviews/d01-t02-s29.json`

## 30. CID fonts: `/Type0`, `/DescendantFonts`, `/CIDToGIDMap`, `/W`, `/DW` and `/W2`

> **Started:** 2026-10-26T06:24Z

Composite fonts (ISO 32000-1 section 9.7) carry most CJK and many Western PDFs from Office: a `/Type0` font with one `/DescendantFonts` entry, a CMap from codes to CIDs and widths per CID.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the CID maps and widths are immutable once built and shared read-only across threads with the font.

**Build order:**

1. Implement composite fonts in `src/pdf/font_cid.cpp` on top of `§2`, `§28` and `§29`. Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below. Done when: they pass under ASan.

- [x] Code-to-CID CMaps (ISO 32000-1 9.7.5 and 9.7.6.2; Adobe Technical Note 5014): `CidCMap::identity(vertical)` for `/Identity-H` and `/Identity-V`, and `CidCMap::parse` for an embedded CMap stream (`begincodespacerange`, `begincidrange`, `begincidchar`, `beginnotdefrange`, `beginnotdefchar`, `/WMode`, `usecmap` of `/Identity-H` or `/Identity-V`; another `usecmap` name is `UnsupportedFeature`, the predefined CJK CMaps staying with `§17`). `CidCMap::next(bytes)` splits a string by the codespace ranges into codes of 1 to 4 bytes, a code no range matches taking the length of the shortest range with the same first byte, else of the shortest range, and gives its CID: the mapped CID, else the notdef mapping, else 0; every call on a non-empty string consumes at least one byte and never more than remain, an incomplete code at the end consuming the rest with CID 0, and an empty string gives the end (length 0); an embedded CMap with no codespace range is `DamagedFormat`. Later mappings win where they overlap; ranges stay intervals, never expanded. Bounds: the stream at 4 MiB, 100 codespace ranges (as `§28`'s ToUnicode CMaps; **Corrected 2026-10-26:** 10,000 before, which let one hostile CMap cost 40,000 checks per code) and 100,000 mappings, codes of at most 4 bytes, CIDs at most 65,535 (Annex C). Done when: `PdfFont.Type0CodeToCid` maps a string through Identity-H, Identity-V and an embedded CMap with mixed 1-, 2-, 3- and 4-byte codespaces, ranges, single codes, notdef mappings, a later mapping overriding an earlier one and an identity `usecmap`, splits unmatched bytes by the rule above, decodes truncated Identity and mixed-length strings (an odd byte left, a partial 3-byte code) and the empty string by the end rule under ASan, refuses a foreign `usecmap`, a CMap with no codespace and each bound with its typed error, and reports `/WMode`.
- [x] CID-to-glyph mapping (9.7.4.2): `CidToGidMap::identity()` and `CidToGidMap::parse(stream)` (big-endian glyph indices; an odd length is `DamagedFormat`; entries past CID 65,535 are never reachable and not kept), a CID past the map giving glyph 0; for `/CIDFontType2` the map over a `§2` `LoadedFont`, for `/CIDFontType0` over a `§29` program `LoadedFont::glyph_for_cid` when it is CID-keyed, and the CID itself as the glyph index when it is name-keyed (the reading FreeType and pdf.js give; a default, cheap to change), through `cid_glyph(cid, map, font)`. A glyph at or past the font's glyph count is glyph 0 (`.notdef`). Done when: `PdfFont.CidToGidMap` resolves glyphs through the identity, a stream map (drawing `§2`'s TrueType glyphs), a CID past the map (glyph 0), `§29`'s CID-keyed fixture `cff-cid.cff` (non-identity CIDs) and its name-keyed `cff-outlines.cff` (the CID as the glyph index), a selected glyph past the font's glyph count (glyph 0), and refuses a map stream of odd length.
- [x] Widths (9.7.4.3): `CidWidths::build(dw, w, dw2, w2, resolver)` over the resolved descendant font values, indirect references inside `/W` and `/W2` resolved through an `IndirectObjectResolver`; `/DW` (default 1000) and `/W` in both forms (`c [w1 w2 ...]` and `c_first c_last w`), and vertical `/DW2` (default `[880 -1000]`) and `/W2` in both forms (`c [w1y v1x v1y ...]` and `c_first c_last w1y v1x v1y`), a CID without a vertical entry taking `v_x = w0 / 2` and `v_y` from `/DW2`; advances in text space (glyph space over 1,000); a malformed entry skipped as readers do, a later entry winning over an earlier one. Bounds: 65,536 array elements in `/W` and in `/W2`, a range's CIDs inside 0 to 65,535 (past it `ResourceLimit`). Done when: `PdfFont.CidFontWidths` computes exact horizontal advances, vertical advances and position vectors (each asserted apart) for an Identity-H string over both `/W` and both `/W2` forms, the defaults, a non-default `/DW2`, arrays reached through indirect references, a later entry overriding an earlier one and a skipped malformed entry, and refuses an oversized range and an over-long array.
- [x] Type 0 assembly (9.7.6, 9.7.4): `load_type0_font(type0, resolver)` resolves `/Encoding` (a name: `/Identity-H` or `/Identity-V`, another name `UnsupportedFeature` until `§17`; a stream: an embedded CMap, inheriting through its dictionary's `/UseCMap` (an Identity name, or another embedded stream) with its own mappings over the base's, at most 8 levels, a cycle `DamagedFormat`), `/DescendantFonts` (exactly one `/CIDFontType0` or `/CIDFontType2` font, else `DamagedFormat`), the descendant's `/W`, `/DW`, `/W2`, `/DW2` and `/CIDToGIDMap` (`/Identity` when absent; not read for `/CIDFontType0`), and its `/FontDescriptor`'s program (`/FontFile2` through `load_embedded_font`, `/FontFile3 /CIDFontType0C` through `load_charstring_font`, `/FontFile3 /OpenType` through `load_embedded_font`), every stream decoded by `decode_stream` with the resolver; no program gives a font without a `LoadedFont`, which `§17` substitutes, and a refused program its typed error. The immutable `Type0Font` gives, for each code of a string, its code, length, CID, glyph, horizontal advance and vertical metrics (planned from the plan review, `docs/reviews/01-engine/D01-T02-s30.md`). Done when: `PdfFont.Type0Assembly` loads in-memory object graphs, indirect references throughout, for a `/CIDFontType2` over `§2`'s TrueType fixture with a stream `/CIDToGIDMap` and for a `/CIDFontType0` over `cff-cid.cff` with an embedded CMap inheriting through `/UseCMap`, asserting each code's CID, glyph and advance exactly, gives a font without a program, and refuses no descendant, two descendants, a wrong subtype, a `/UseCMap` cycle and a chain of 9 levels.
- [x] Completion review (`docs/reviews/01-engine/D01-T02-s30.md`): one base per CMap, its codespace under the 100-range bound; `/DW2` members, `/UseCMap`, `/WMode` and `/Subtype` read through references; resolver errors other than a missing or damaged object passed on; `beginnotdefchar`, five-byte codes, indirect `/W2`, later vertical entries, `/W2` bounds, the OpenType branch and refused programs tested. Done when: `PdfFont.Type0CodeToCid`, `PdfFont.CidFontWidths` and `PdfFont.Type0Assembly` assert each case.
- [x] Commit: `pdf: CID fonts with CIDToGIDMap and W widths`

> **Corrected 2026-10-26:** (validation before the build) The section named no API, treated every code as two bytes, and left out how a CMap splits a string (ISO 32000-1 9.7.6.2: codespace ranges of 1 to 4 bytes, the rule for unmatched bytes, notdef mappings, `/WMode`), how `/W` reaches indirect arrays, which glyph a CID selects in a name-keyed CFF, and the defaults and units of the widths. The items now name `CidCMap`, `CidToGidMap`, `cid_glyph` and `CidWidths` in `src/pdf/font_cid.h`, the CMap tokenizer of `§28` shared with them, and bounds for the CMap stream, ranges, mappings, CIDs and width arrays, each tested with its typed error. Unchanged: the predefined CJK CMaps and `usecmap` of them stay with `§17`.

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFont.Type0*:PdfFont.CidTo*:PdfFont.CidFontWidths` passes under ASan with 0 warnings.

> **Verified:** 2026-10-26 | §30 | e1bb8c85839d03654e328d830df1a75f59040005
> **Review:** `docs/reviews/d01-t02-s30.json`

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

> **Started:** 2026-10-26T07:53Z

Calibrated and ICC-based colour (ISO 32000-1 sections 8.6.5 and 8.6.5.5) so colours from scanners, cameras and print workflows render as their authors saw them, through the Windows Color System (`mscms.dll`: `OpenColorProfileW`, `CreateMultiProfileTransform`, `TranslateColors`) rather than an approximation.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that colour transforms are built on the `DocumentActor` thread and cached per document only in immutable form, and that no colour state is shared across documents.

**Build order:**

1. Implement `src/pdf/color.h` and `src/pdf/color.cpp` (linking `mscms.lib`), the `ColorProfileCache` on `Document`, and wire them into `cs`/`CS`/`sc`/`SC`/`scn`/`SCN`, `g`/`rg`/`k` and their stroking forms in `src/pdf/interpreter.cpp`. Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below. Done when: they pass under ASan.

- [x] Calibrated spaces (ISO 32000-1 8.6.5.2 to 8.6.5.4): `ColorSpace` (immutable, `src/pdf/color.h`) for `CalGray` (`/WhitePoint`, `/BlackPoint`, `/Gamma`), `CalRGB` (`/WhitePoint`, `/BlackPoint`, `/Gamma`, `/Matrix`) and `Lab` (`/WhitePoint`, `/Range`, components clamped to it), each to sRGB through CIE XYZ: the CIE formulas, black point compensation from `/BlackPoint` (a linear scaling of each XYZ component from [black, white] to [0, white]; **Corrected 2026-10-26:** "as Adobe's and pdf.js's readers apply it" before, which named no method), Bradford adaptation from the space's white to D65, the sRGB matrix and transfer curve; the defaults of Tables 63 to 65; a space whose `/WhitePoint` is missing, not three positive numbers or not `Y = 1` read as the device space of its component count, as readers do. `cs`/`CS` and `sc`/`SC`/`scn`/`SCN` in `src/pdf/interpreter.cpp` take these spaces for fill and stroke apart, the space kept in the graphics state and restored by `Q`, and record the colour converted at paint time. Done when: `PdfInterpreter.CalibratedColors` interprets `cs`/`scn` fills and `CS`/`SCN` strokes in each space (painting right after selecting a space gives its initial colour), switches between calibrated and device spaces on one side without touching the other, restores both through `q`/`Q`, and matches reference sRGB values to within 1/255 per channel: Lab from LittleCMS 2.19 (Pillow's `ImageCms`, a Lab D50 to sRGB transform), CalGray and CalRGB from closed forms (a D65 white with the sRGB matrix and gamma 1, which reproduces sRGB's own encoding), a D50 white (adapted) and a non-zero `/BlackPoint`; and reads a malformed `/WhitePoint` as the device space.
- [x] `ICCBased` (8.6.5.5): `/N`, `/Alternate` and `/Range`; the profile stream at most `kMaxIccProfileBytes` (16 MiB) and its header checked (its declared size, `acsp`, its colour space against `/N`); a transform to sRGB through the Windows Color System (`OpenColorProfileW` on a `PROFILE_MEMBUFFER`, the sRGB profile from `GetStandardColorSpaceProfileW`, `CreateMultiProfileTransform` with the rendering intent in force when the colour is painted, named for each profile (with one intent Windows reads the source profile's perceptual table for every intent), `TranslateColors`), built on the `DocumentActor` thread once per profile stream and intent per document in the document's `ColorProfileCache` (a `mutable` member of `Document` like its object-stream cache, read and written only on the actor thread, holding immutable transforms) and released with the document; the profile stream decoded through `Document::decode_stream` with the 16 MiB limit, so a larger one is never allocated. The rendering intent comes from `ri` and from `/ExtGState /RI` (added to `apply_ext_gstate`), is saved and restored by `q`/`Q`, and applies to a colour set before it and painted after it. Failures: a profile that does not load, is refused or does not match `/N`, a destination sRGB profile Windows cannot give, a transform that cannot be built and a `TranslateColors` that fails all fall back to `/Alternate`, else the device space for `/N` (1, 3 or 4), with a diagnostic recorded, never a partly converted colour or row; allocation failure is `OutOfMemory`. Bounds: colour space resolution follows at most 8 nested spaces (`/Alternate` chains, resources naming resources) with a cycle read as the device space for its component count; the cache keeps at most 64 transforms and 64 MiB of profiles per document, a profile past them taking its fallback with a diagnostic. Done when: `PdfInterpreter.IccBasedColors` converts fills and strokes through generated profiles (an RGB matrix and curve profile, a Gray curve profile and a CMYK lookup-table profile whose perceptual and colorimetric tables differ, written by `scripts/build-corpus.py` into `tests/corpus/color/`) matched to LittleCMS 2.19 references to within 2/255 per channel (two colour engines); records different colours for the perceptual and relative colorimetric intents set by `ri` and by `/ExtGState /RI`, including an intent changed between `scn` and the paint and one restored by `Q`; builds one transform per profile and intent across pages of one document (the cache's count asserted); falls back on a damaged, an oversized (never decoded past 16 MiB), an `/N`-mismatched profile and on injected failures of the sRGB profile, the transform and `TranslateColors` (test hooks), each with its diagnostic; reads a cyclic `/Alternate` chain and mutually naming resources as the device space; and takes the fallback once the cache's budget is spent.
- [x] Calibrated `DeviceCMYK`: through the document's first `/OutputIntents` `/DestOutputProfile` with `/N 4`, else Windows' `RSWOP.icm` from the colour directory (`GetColorDirectoryW`; a recorded default, cheap to change), else the `§3` formula. Done when: `PdfInterpreter.CalibratedCmyk` checks `k` fills against the generated CMYK profile's LittleCMS result when it is the output intent, against `RSWOP.icm`'s own Windows transform when there is none and the file exists (asserting it differs from the formula), and the `§3` formula when no profile loads (the default profile switched off through a test hook).
- [x] Image samples for `§4`: `ColorSpace::convert_samples` turns a row of 1-, 2-, 4-, 8- or 16-bit components with `/Decode` into sRGB RGBA8 through the same conversions, one transform call per row for an ICC space; `ColorSpace::from_icc_profile` reads an image's own profile (a JPX `colr` box, `WicDecodedImage::icc_profile`) for an image dictionary that names no colour space. **Corrected 2026-10-24:** moved here from `§3` (it was filed there by the `D06 T05 §32` completion review). **Corrected 2026-10-26:** images are drawn by `§4`, which depends on this section, so this section builds and tests the conversion and `§4` draws through it. Row arithmetic is checked (width times components times bits, rounded up to bytes, in 64 bits), a row shorter than that is `DamagedFormat`, and a `/Decode` without two numbers per component is ignored as readers do. Done when: `PdfColor.ImageSamples` gives explicit expected pixels for packed 1-, 2- and 4-bit samples, big-endian 16-bit samples, a reversed `/Decode` range and an omitted one in the device spaces, converts rows in every calibrated and ICC space to the values the single-colour path gives, converts through an image ICC profile from `WicDecodedImage::icc_profile`, refuses a truncated row, ignores a malformed `/Decode`, and counts one `TranslateColors` call per ICC row through a test hook.
- [x] `/DefaultGray`, `/DefaultRGB` and `/DefaultCMYK` in the page or Form resources replace the device space they name (ISO 32000-1 8.6.5.6) for `g`, `rg`, `k`, `cs` and a device space named in `/ColorSpace`, except inside their own definitions (a default whose `/Alternate` is the device space it replaces does not recurse); `ColorSpaces::resolve` gives `§4` the same resolution for images. Done when: `PdfInterpreter.DefaultColorSpaces` checks `/DefaultGray`, `/DefaultRGB` and `/DefaultCMYK` `ICCBased` spaces applied to `g`/`G`, `rg`/`RG` and `k`/`K` and to `cs` naming the device space, the same resolution for an image's device space through `ColorSpaces::resolve`, a device space without a default left as it is, and a default naming itself read as the device space. **Corrected 2026-10-24:** added from the `D01 T02 §3` plan review (gap 5).
- [x] Completion review (`docs/reviews/01-engine/D01-T02-s32.md`): ICC initial colours of 0, Lab ICC profiles, ordered `/Range` pairs, failed transforms counted in the budget, `OutOfMemory` passed on, a profile read once per document and a space selected again reused; diagnostics, the byte budget, `G`/`K` defaults, every row space and two real pages tested. Done when: `PdfInterpreter.IccBasedColors`, `PdfInterpreter.DefaultColorSpaces` and `PdfColor.ImageSamples` assert each case.
- [x] Commit: `pdf: calibrated, Lab and ICC-based colour through Windows colour management`

> **Corrected 2026-10-26:** (validation before the build) Images are drawn by `§4`, which depends on this section, so the image items could not be proven through the interpreter here: this section builds the conversion (`convert_samples`, an image's own ICC profile, `ColorSpaces::resolve` for `/Default*`) and tests it directly, and `§4`'s image item draws through it. "Built once per profile per document" needed a home: a `ColorProfileCache` on `Document`, actor-thread only, as its object-stream cache is. The section named no reference values for the ICC paths and no test profiles: the profiles are generated by `scripts/build-corpus.py` and the references come from LittleCMS 2.19 through Pillow, to within 2/255 where two colour engines meet. The default CMYK profile is Windows' `RSWOP.icm`, recorded as a default. Bounds: a profile of at most 16 MiB with its header checked.

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.Calibrated*:PdfInterpreter.IccBased*:PdfInterpreter.DefaultColorSpaces:PdfColor.*` passes under ASan with 0 warnings. **Corrected 2026-10-26:** `PdfInterpreter.JpxIccProfile` became `PdfColor.ImageSamples` (images are `§4`'s).

> **Verified:** 2026-10-26 | §32 | e9ddfdd183d7d23990f99268b01ffd456e33e374
> **Review:** `docs/reviews/d01-t02-s32.json`

## 33. Indexed, Separation and DeviceN colour with PDF functions 0, 2, 3 and 4

> **Started:** 2026-10-27T20:19Z

Spot colours and palettes (ISO 32000-1 sections 7.10, 8.6.6 and 8.6.6.5): `Indexed`, `Separation` and `DeviceN` colour spaces with the four PDF function types their tint transforms use.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that parsed functions and colour spaces are immutable once built and evaluated without shared mutable state.

**Build order:**

1. Implement PDF functions in `src/pdf/function.h` and `src/pdf/function.cpp` (added to `src/pdf/recto-pdf.vcxproj`), the three colour spaces in `src/pdf/color.h` and `src/pdf/color.cpp`, and their colour operators in `src/pdf/interpreter.cpp`. Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below. Done when: they pass under ASan.

- [x] Colour components to 32: `kMaxColorComponents = 32` in `src/pdf/color.h` (ISO 32000-1 Annex C.2, the DeviceN limit), `ColorSpace::initial()` returning `std::array<double, kMaxColorComponents>` and the interpreter's `Color::components` the same size, so `sc`/`scn` take as many operands as the space has components. Done when: `PdfInterpreter.SeparationAndDeviceN` sets and paints a 6-colorant DeviceN colour, paints two successive DeviceN colours that differ only in their sixth component in different colours (the interpreter's last-conversion memory compares every component), restores one through `q`/`Q`, and the `§3` and `§32` colour tests (`PdfInterpreter.DeviceColors`, `PdfInterpreter.DefaultColorSpaces`) still pass, `DeviceColors`' Indexed case now painting its palette colour.
- [x] PDF functions (`Function`, immutable, `src/pdf/function.h`; `Function::parse(const Document&, const PdfValue&)` and `evaluate(std::span<const double> in, std::span<double> out)` with no allocation and no shared mutable state while evaluating): type 0 (sampled: `/BitsPerSample` 1, 2, 4, 8, 12, 16, 24 or 32 (ISO 32000-1 Table 39), `/Size`, `/Encode`, `/Decode`, multilinear interpolation for up to 8 inputs and simplex (Kuhn) interpolation past that, so one evaluation reads at most 256 corners, `/Order 3` drawn as a natural cubic spline through the samples for a one-input function of at least 4 samples (the common Separation tint transform; with fewer, Table 39 makes it Order 1) and linearly with a diagnostic for more inputs (a recorded default: 7.10.2 names cubic spline interpolation without a method, cheap to extend), the sample table at most `kMaxSampledFunctionBytes` (16 MiB) decoded through `Document::decode_stream` with that limit and checked against `/Size` in 64-bit arithmetic), type 2 (exponential, with the 7.10.3 domain rules for a non-integer or negative `/N`; `/C0` and `/C1` defaulting to `[0]` and `[1]`), type 3 (stitching: `/Functions`, `/Bounds` non-decreasing, `/Encode`, nested at most `kMaxFunctionDepth` (8) levels and at most `kMaxFunctionNodes` (256) functions per tree, so shared subfunctions cannot multiply), type 4 (PostScript calculator: the 7.10.5 operator set compiled once into an immutable program with `if`/`ifelse` jumps, at most `kMaxCalculatorOperators` (65,536) operators, a 100-entry stack, and an operation counter per evaluation; a stack overflow, underflow, type error, division or `idiv`/`mod` by zero, a domain error (`sqrt` or `ln`/`log` of a negative or zero) or a non-finite result ends the evaluation with the `/Range` low values and a diagnostic, never a partial result), each clipping inputs to `/Domain` and outputs to `/Range` where it has one (`/Range` is required for types 0 and 4 and optional for types 2 and 3, whose output count then comes from `/C0`/`/C1` or the first subfunction); at most 32 inputs and 32 outputs; a function array (n one-output functions) read as one function. Done when: `PdfInterpreter.Functions` checks each type against hand-computed values (type 0 at 1, 12, 24 and 32 bits with non-default `/Encode` and `/Decode` and with both defaulted, 2-input multilinear and 9-input simplex interpolation at grid points and at interior points where a nearest-sample answer differs, a one-input `/Order 3` interior point where the spline differs from the line, type 2 with fractional and negative `/N` and without `/Range`, a type 3 with three pieces, an `/Encode` reversal and no `/Range`, a type 4 covering every operator, inputs outside `/Domain` and outputs outside `/Range` clipped, a function array of three one-output functions), gives the `/Range` low values and records a diagnostic for a calculator overflow, underflow, type error, division by zero and `sqrt` of a negative (also through a Separation tint transform), and refuses with typed errors (`DamagedFormat` or `ResourceLimit`): a short sample table, an oversized one (never decoded past 16 MiB), unordered `/Bounds`, a type 3 nested 9 deep, a tree of 257 functions, an unbalanced or unknown calculator token and a program past 65,536 operators.
- [x] `Indexed` (8.6.6.3, `[/Indexed base hival lookup]`) over any base space except `Pattern` and `Indexed` (the device, `§32` and Separation and DeviceN spaces), `hival` an integer from 0 to 255 (a larger one read as 255 with a diagnostic), the lookup a string or a stream (decoded with a 1 MiB limit) of `components x (hival + 1)` bytes, each byte scaled to the base component's range; `sc`/`scn` round the index and clamp it to 0 to `hival`; a short lookup pads the missing entries with zero bytes and records a diagnostic (**Corrected 2026-10-27:** was "refuses a short lookup"; MuPDF pads it the same way, so the defined entries still draw), and a lookup that is neither string nor stream makes the space unusable (null, as an unknown space is). Done when: `PdfInterpreter.IndexedColors` resolves palette indices over DeviceRGB, ICCBased and Lab bases and over a Separation base, clamps out-of-range and fractional indices, pads a short lookup (its diagnostic asserted), reads a stream lookup, and rejects an `Indexed` base and a missing lookup.
- [x] `Separation` and `DeviceN` (8.6.6.4, 8.6.6.5): the alternate a device, CIE-based or ICCBased space, the tint transform a `Function` with one input (Separation) or one per colorant (DeviceN, at most 32 names) and at least as many outputs as the alternate has components (else the space is unusable, with a diagnostic), tints clipped to 0 to 1, initial tints of 1.0; `/Attributes` read with `/Subtype` (`DeviceN` or `NChannel`), `/Colorants` (each a Separation space) and `/Process` (`/ColorSpace` and `/Components`), kept on the space through `ColorSpace::colorants()`, `nchannel()`, `process()` and `colorant_spaces()` for `D04 T06`'s separations and preflight, while the colour itself is drawn through the tint transform; a Separation named `/None` or a DeviceN whose every name is `/None` paints nothing (`ColorSpace::paints()` false, and false for an Indexed space over such a base: fills and strokes are not drawn, and `§4` skips text, stencil masks and images in such a space the same way), and `/All` applies its tint to every colorant at once with its alternate space and tint transform ignored (8.6.6.4), so on a screen tint t is gray 1 - t, a registration mark black (**Corrected 2026-10-27:** was "draws through its tint transform like any other name", which 8.6.6.4 rules out; round 1 of the review). Done when: `PdfInterpreter.SeparationAndDeviceN` resolves spot tints through type 2 and type 4 tint transforms (to DeviceCMYK, DeviceRGB and ICCBased alternates), reads an NChannel `/Attributes` with `/Colorants` and `/Process` through the accessors, draws nothing for `/None` fill and stroke while the other side still paints, reports `paints()` false for an Indexed space over a `/None` base, draws `/All` as gray 1 - t whatever its transform, and treats a tint transform with too few outputs as unusable.
- [x] Image rows in these spaces for `§4` (which draws images through `ColorSpace::convert_samples`): an `Indexed` row's samples are palette indices (default `/Decode` `[0 2^bits-1]`, a given `/Decode` mapped and rounded), and a Separation or DeviceN row's samples are tints; a row converts at most `min(width, 2^bits)` distinct colours when the space has one component and `bits <= 8` (the palette or the tint table built once per row and looked up per pixel), with one `TranslateColors` call per row for an ICC base or alternate. Done when: `PdfColor.SpecialSpaceSamples` gives explicit pixels for 1-, 4- and 8-bit Indexed rows (a reversed `/Decode` included) and 8- and 16-bit Separation and DeviceN rows, counts at most 256 function evaluations for a 4,096-pixel 8-bit Separation row and one `TranslateColors` call for an Indexed row over an ICCBased base through test hooks.
- [x] Completion review (`docs/reviews/01-engine/D01-T02-s33.md`, both arms of the reviewer A/B experiment): the interpolation chosen by the input count, so a sampled function is continuous where an input crosses a sample; `/Order` other than 1 or 3 drawn linearly with a diagnostic; a one-input `/Order 3` table past `kMaxSplineSamples` (65,536) drawn linearly with its limit recorded (**Corrected 2026-10-27:** the spline had no bound); the full signed 32-bit integer range in calculators; at most `kMaxFunctionTreeBytes` (16 MiB) of tables per function tree, a subfunction an indirect reference names again parsed once and shared; at most `kMaxColorFunctionBytes` (64 MiB) of tint transform tables per interpretation, each function object parsed once and shared by every space naming it; the evaluation counter thread-local, so evaluation shares no state between threads. Done when: `PdfInterpreter.Functions` and `PdfInterpreter.SeparationAndDeviceN` assert each case.
- [x] Commit: `pdf: Indexed, Separation and DeviceN colour with PDF functions`

> **Deferred:** cubic interpolation for multi-input `/Order 3` sampled functions -> XREF: D01 T02 §41
> **Deferred:** tint transform work counted against the page's work and wall-clock budgets -> XREF: D01 T02 §19

> **Corrected 2026-10-27:** (validation before the build) The interpreter and `ColorSpace` held 4 components, so DeviceN (up to 32) had nowhere to live: widened. `§4` draws images through `convert_samples` and depends on this section, so Indexed, Separation and DeviceN rows are built and tested here as `§32` did for its spaces. "1 to 32 bits per sample" became the ISO Table 39 set. Indexed may also sit over Separation and DeviceN (8.6.6.3). The section named no bounds for multilinear interpolation (2^32 corners for a 32-input DeviceN), for stitching trees that share subfunctions, or for calculator programs: added. `/Attributes` needed somewhere to be read into: accessors for `D04 T06`. `/None` must also stop text and stencil masks, which `§4` draws: `ColorSpace::paints()` and a note in `§4`.

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.*:PdfColor.*` passes under ASan with 0 warnings. **Corrected 2026-10-27:** was the three new tests alone; the whole interpreter and colour suites, since the component widening touches the `§3` and `§32` paths (plan review).

> **Verified:** 2026-10-28 | §33 | 001c43e10f1cec9f8390eeb82e69b699cf996bc5
> **Review:** `docs/reviews/d01-t02-s33.json`

## 34. Native Flate compressor (`flate_compress`: LZ77, Huffman blocks, zlib wrapper)

> **Started:** 2026-10-24T18:16Z

> **Corrected 2026-10-24 (plan review, `docs/reviews/01-engine/D01-T02-s34.md`):** five gaps folded in. A round trip through `decode_flate` proves little because it accepts a missing Adler-32 trailer and recovers damaged headers, so the stream is checked strictly; memory, search work and allocation failure have stated bounds and tests; the ratio and speed fixtures are generated and identified, and speed has its own Release gate; `FlateLevel` has a defined contract; block selection is checked against the costs.

Recto writes PDFs (`§5`'s revisions, `D04 T02 §2`'s `PdfWriter`, the optimizer) and needs its own RFC 1950 and 1951 compressor: the engine takes no external runtime (D5, D7), and `filters.cpp` holds only the inflater (`decode_flate`, `inflate_raw_deflate`). -> XREF: D04 T02 §2 (its `PdfWriter` compresses through this function) -> XREF: D01 T02 §37 (a faster, smaller `Default`, measured against zlib-ng) -> XREF: D01 T02 §38 (cancellation)

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `flate_compress` is a pure function of its input and level, allocates against its stated bound, and shares no state between calls or threads.

**Build order:**

1. Generate the fixtures in `scripts/build-corpus.py`. Done when: `python scripts/build-corpus.py --check` passes with them.
2. Implement `FlateLevel`, `FlateCompressStats` and `flate_compress` in `src/pdf/filters.h` and `src/pdf/filters.cpp`. Done when: it compiles on `x64` and `ARM64`.
3. Add the tests below to `tests/recto-tests/pdf_filters_tests.cpp`. Done when: the checkpoint passes under ASan and the speed gate passes in Release.

- [x] Fixtures: `scripts/build-corpus.py` writes `tests/corpus/flate/content-stream.txt` (1 MiB of page content operators from a fixed-seed generator: paths, text, colours and numbers, as a producer writes them), `tests/corpus/flate/random.bin` (64 KiB from a fixed seed), `tests/corpus/flate/image-rows.bin` (a 256 by 256 RGB gradient with noise as PNG Up predictor rows) and `tests/corpus/flate/collisions.bin` (256 KiB over a three-symbol alphabet, the longest-chain worst case; these two added from the completion review), and `tests/corpus/flate/reference-sizes.json` listing, for those two and every `tests/corpus/fonts/*.ttf` and `*.otf`, the file's SHA-256 and its size under Python's `zlib.compress(data, 6)`. Done when: `build-corpus.py --check` regenerates them byte for byte, and `PdfFilters.FlateCompressRatio` fails when a listed file is missing or its SHA-256 differs.
- [x] `enum class FlateLevel : uint8_t { Fast, Default, Best }` with stated search budgets: hash-chain steps per position 8, 32 and 1,024 (a quarter once the held match is 8 or 32 bytes, for `Default` and `Best`, as zlib's `good_length`); lazy matching off, up to a 32-byte match and up to 258 bytes; stop on a match of 16, 128 and 258 bytes. Done when: `PdfFilters.FlateCompressLevels` round-trips every level and checks on `content-stream.txt` that `Best` is no larger than `Default`, `Default` no larger than `Fast`, and the chain steps counted in `FlateCompressStats` stay within the level's budget times the input length.
- [x] `flate_compress(std::span<const uint8_t> input, FlateLevel level = FlateLevel::Default, FlateCompressStats* stats = nullptr)` returning `PdfResult<std::vector<uint8_t>>`: a zlib stream (RFC 1950: `CMF` `0x78`, `FLG` with a valid `FCHECK` and `FLEVEL`, Adler-32 of the input in the trailer) of RFC 1951 blocks, LZ77 over a 32 KiB window through hash chains, each block written stored, fixed-Huffman or dynamic-Huffman (code lengths limited to 15 bits, the code-length alphabet to 7, RFC 1951 3.2.7), whichever costs fewest bits including the dynamic tree and the stored block's byte alignment. `FlateCompressStats` reports per block its type and the three costs. Done when: `PdfFilters.FlateCompressStrict` checks the header bytes and `FCHECK`, that the trailer is the input's Adler-32, that strict inflation consumes exactly the stream with the last block marked final (`detail::inflate_zlib_strict`, which refuses a missing or wrong trailer and trailing bytes), and that the output equals the input, for empty, one-byte, repetitive, random and `content-stream.txt` inputs; and `PdfFilters.FlateCompressBlocks` checks that each block's type is its cheapest by the reported costs, that all three types occur, and that inputs crossing the 32 KiB window, matching across a block boundary and needing several 65,535-byte stored blocks round-trip strictly.
- [x] Bounded output, memory and work: incompressible input grows by at most 5 bytes per 65,535 plus 6; input over `kMaxDecodedStreamBytes` is refused with `ResourceLimit`; working memory beside the output is at most 256 KiB (`2^14` heads of a one-slot 3-byte table, `2^14` heads of 4-byte hash chains and `2^15` chain links, as libdeflate's hash-chain finder splits them), reported in `FlateCompressStats`; **Corrected 2026-10-24:** was one 3-byte hash with `2^15` heads, whose chains filled with candidates sharing only three bytes; the split took predictor image rows from 1.249 to 1.000 of zlib's size and the content sample from 31 to 39 MB/s, measured by the completion review's broader samples; an allocation failure returns `OutOfMemory`. Done when: `PdfFilters.FlateCompressBounds` checks the growth bound on `random.bin`, the refusal, the working-memory figure on repetitive and mixed inputs, and `OutOfMemory` under `detail::ScopedFlateCompressAllocFailure`.
- [x] Ratio and speed: on every file `reference-sizes.json` lists, the `Default` level's output is at most 1.05 times the listed zlib size; and in Release x64, `Default` compresses `content-stream.txt` at 20 MB/s or more, the median of seven timed runs on `std::chrono::steady_clock` (stricter than the best of five first stated), with the median and slowest run of every level on the content, image and collision samples reported. Done when: `PdfFilters.FlateCompressRatio` passes in every configuration and `PdfFilters.FlateCompressSpeed` passes in Release x64 (it skips, saying why, in Debug and under ASan). **Corrected 2026-10-24:** the `Default` chain budget is 32, not 128: measured on `content-stream.txt` in Release x64, 128 links gave 0.99 of zlib's size at 20 to 22 MB/s (at the floor, 10 links per byte) and 32 give 1.017 at 31 MB/s, libdeflate's level-6 choice (search depth 35); the ratio and speed targets are unchanged. The speed test reports without asserting on GitHub Actions runners, whose throughput is not a measurement (D06 T01 §7 owns per-machine baselines); the gate is this checkpoint's Release command on the Windows host.
- [x] Commit: `pdf: add a native Flate compressor`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFilters.FlateCompress*` passes under ASan with 0 warnings, and `build/build.ps1 -Platform x64 -Configuration Release -TestFilter PdfFilters.FlateCompressSpeed` passes.

> **Verified:** 2026-10-24 | §34 | 5eed8f8bb444134d2c992849137ac837a9eadcbd
> **Review:** `docs/reviews/d01-t02-s34.json`

## 35. Canonical `PdfValue` serializer for indirect objects and streams

> **Started:** 2026-10-24T21:31Z

> **Corrected 2026-10-24 (plan review, `docs/reviews/01-engine/D01-T02-s35.md`):** three gaps folded in: typed results with an output budget, allocation failure and rollback of a failed call's partial output; streams written from their encoded `raw_bytes` with their filters, never the decoded cache `payload_bytes()` prefers; and stream framing, `/Length` and object headers asserted directly, since the parser's `endstream` scan would hide a wrong length.

Every PDF Recto writes goes through one serializer of the object model (ISO 32000-1 sections 7.3 and 7.5), shared by `§5` and `D04 T02 §2`, so a value written is always a value read back the same. -> XREF: D04 T02 §2 (its `PdfWriter` writes every object through it) -> XREF: D06 T03 §13 (a serializer stress sample in the cross-viewer check)

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that serialization reads the value without changing it, bounds its nesting and output, and leaves the caller's buffer as it was when it fails.

**Build order:**

1. Implement `src/pdf/serialize.h` and `src/pdf/serialize.cpp`. Done when: they compile on `x64` and `ARM64`.
2. Add `tests/recto-tests/pdf_serialize_tests.cpp`. Done when: its tests pass under ASan.

- [x] `serialize_value(const PdfValue&, std::string& out, size_t max_bytes = kMaxSerializedBytes)` returning `PdfResult<void>` and appending to `out`: null, booleans, integers, reals in the shortest form that reads back exactly, with no exponent (ISO 32000-1 7.3.3), names with `#XX` for delimiters, whitespace, `#` and bytes outside `!`..`~` (7.3.5), strings as literal strings with `(`, `)`, `\` and non-printing bytes escaped or as hex strings when most bytes are binary or the string was hex (7.3.4), arrays, dictionaries and references. Done when: `PdfSerialize.ValuesRoundTrip` parses every serialized value back equal (every kind, every corpus file's objects, compared by kind and content) and `PdfSerialize.RealsAndNames` checks exponent-free reals, non-finite reals refused and name escapes.
- [x] Failure is typed and leaves `out` as it was: nesting past 64 levels is `RecursionLimitExceeded` (as the parser reports nesting), output past `max_bytes` is `ResourceLimit` (`RefusalReason::WorkingBytes`), and an allocation failure is `OutOfMemory`; in each case `out` is truncated back to its length before the call. Done when: `PdfSerialize.Failures` checks depth 64 accepted and 65 refused, a string whose escaped form passes the budget refused, and an injected allocation failure after partial output, each with `out` and the input value unchanged.
- [x] `serialize_indirect(IndirectRef, const PdfValue&, std::string& out, size_t max_bytes = kMaxSerializedBytes)` writing `N G obj` and `endobj`; a stream is written from its encoded `raw_bytes` with its dictionary as given (`/Filter` and `/DecodeParms` kept) and `/Length` set to the bytes written, never from the decoded cache, with `stream`, a line feed, the bytes, a line feed and `endstream` (7.3.8.1), and the input value unchanged. Done when: `PdfSerialize.IndirectAndStreams` checks the header's number and generation, `/Length` against the emitted byte count, the delimiters for an empty stream, a binary payload holding `endstream` and one ending in CR LF, and a `FlateDecode` stream with its decoded cache filled, which reopens with its encoded bytes and decodes to the same content.
- [x] Commit: `pdf: add the canonical PdfValue serializer`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfSerialize.*` passes under ASan with 0 warnings.

> **Verified:** 2026-10-25 | §35 | c707b36e78b6a33c89ffe9fcc22257f037ec18e7
> **Review:** `docs/reviews/d01-t02-s35.json`

## 36. Encrypting new objects with a document's `SecurityHandler` (RC4, AES-128, AES-256)

> **Started:** 2026-10-24T23:13Z

> **Corrected 2026-10-24 (plan review, `docs/reviews/01-engine/D01-T02-s36.md`):** four gaps folded in: the crypt filter chosen as decryption chooses it (`/StrF`, `/StmF`, `/EFF` for embedded files, a stream's own `/Crypt` filter, a resolver for indirect parameters) through one shared selection, with unknown filters refused; all-or-nothing encryption proven by failure injection; fresh IVs asserted only for AES, RC4 checked against fixed ciphertext; and empty and block-boundary lengths with ciphertext computed independently.

The decrypting half exists (`SecurityHandler::decrypt_bytes`, `decrypt_object_values`, `D01 T01`); writing into an encrypted document (`§5`) and writing an encrypted one (`D05 T01`) need the encrypting half, with the same exemptions and the same crypt filter choice (ISO 32000-1 7.6, ISO 32000-2 7.6.3).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that encryption reads the authenticated handler's key without changing the handler, takes a fresh IV from `BCryptGenRandom` for every AES string and stream, and leaves the value unchanged on failure.

**Build order:**

1. Add the encrypting functions to `src/pdf/crypto.h` and `src/pdf/crypto.cpp`, with the crypt filter choice moved into one function both directions use. Done when: they compile on `x64` and `ARM64` and every existing `PdfCrypto` test passes unchanged.
2. Add the expected ciphertexts to `scripts/build-corpus.py` (its own RC4 and AES) and the tests below to `tests/recto-tests/pdf_crypto_tests.cpp`. Done when: they pass under ASan.

- [x] `SecurityHandler::encrypt_bytes(std::span<const uint8_t> plaintext, IndirectRef ref, CryptFilterMethod method)` returning `PdfResult<std::vector<uint8_t>>`: RC4 and AESV2 with the per-object key (ISO 32000-1 Algorithm 1, `sAlT` for AES), AESV3 with the file key, AES in CBC with PKCS#7 padding behind a fresh 16-byte IV from `BCryptGenRandom` (an empty plaintext is one IV and one padding block, never passed through), `None` returning the bytes unchanged; refused with `NeedsPassword` before authentication, as decryption is (**Corrected 2026-10-24:** was `InvalidArgument`; the build follows decryption's code). Done when: `PdfCrypto.EncryptBytes` checks, for each method and plaintext lengths 0, 1, 15, 16 and 17 with object 12 generation 3, the ciphertext length (AES: 16 plus the length rounded up to the next 16), that `decrypt_bytes` restores the plaintext, that two AES encryptions of one input differ while RC4's are equal, and the ciphertext against values `scripts/build-corpus.py` computes with its own RC4 and AES for a fixed key and, through `detail::ScopedEncryptionIv`, a fixed IV.
- [x] `SecurityHandler::encrypt_object_values(PdfValue&, IndirectRef, const IndirectObjectResolver* resolver = nullptr)` returning `PdfResult<void>`: every string with the `/StrF` method and every stream payload with `/StmF`, `/EFF` for `/Type /EmbeddedFile`, or the stream's own `/Crypt` filter, chosen by the same function `decrypt_object_values` uses, with `resolver` reading indirect `/Type`, `/Filter` and `/DecodeParms`; the same exemptions (the `/Encrypt` dictionary, cross-reference streams, `/Sig` and `/DocTimeStamp` `/Contents`, `/Metadata` when `/EncryptMetadata` is false, the `Identity` filter); an unknown crypt filter name is `UnsupportedEncryption` (**Corrected 2026-10-24:** was `DamagedFormat`; the shared choice keeps decryption's code); nesting past 64 levels is `RecursionLimitExceeded`. Done when: `PdfCrypto.EncryptObjects` checks each exemption, differing string, stream and embedded-file filters, a `/Crypt` override with indirect parameters, and that `decrypt_object_values` restores the original value.
- [x] All or nothing: the value is encrypted on a copy and replaced only when every string and stream succeeded, so a failure leaves the value and the handler as they were. Done when: `PdfCrypto.EncryptFailures` injects an allocation failure and a `BCryptGenRandom` failure after some strings are done (`detail::ScopedEncryptionFailure`), and refuses a value nested past the limit, each with a typed error, the value unchanged and a retry that succeeds.
- [x] Commit: `pdf: encrypt new objects with the document's security handler`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCrypto.Encrypt*` passes under ASan with 0 warnings.

> **Verified:** 2026-10-25 | §36 | eb1143c79ffaca75921b9160eb188c82a29e3030
> **Review:** `docs/reviews/d01-t02-s36.json`

## 37. Faster, smaller `Default` Flate level, measured against zlib-ng

> **Started:** 2026-10-25T02:10Z

> **Corrected 2026-10-25 (owner decision):** re-targeted to what was measured. The section was filed for zlib-ng parity on every sample; tuning reached smaller output than zlib-ng on content and image rows and 18% and 26% more speed than `§34` there, but not zlib-ng's speed, nor its size and speed on the collisions worst case, which need a new parser and SIMD paths. The owner chose to ship the measured gains and file the rest: full parity, `Best` on long chains and the `§34` size rule for every file move to `§39`. Was titled "Flate compressor at zlib-ng speed and size, with a fast `Best` on long chains".

> **Corrected 2026-10-24 (plan review, `docs/reviews/01-engine/D01-T02-s37.md`):** three gaps folded in: `Best` (and `Default`) held to the sizes `§34` stamped with on every sample, so a faster search cannot buy speed with size; the zlib-ng comparison run by the checkpoint itself against the fresh Release build, failing on any missing result; and the zlib-ng backend required and reported, never assumed. Measured on this host with Python's zlib-ng 2.2.4 at level 6 (median of seven): content 55.0 MB/s and 363,179 bytes, image rows 55.6 MB/s and 48,947 bytes, collisions 30.3 MB/s and 61,593 bytes; at level 9 collisions run at 1.3 MB/s.

Filed 2026-10-24 by the `D01 T02 §34` completion review (`docs/reviews/01-engine/D01-T02-s34.md`). -> XREF: D01 T02 §34

Its completion review filed the end-to-end comparison of save time and size with the competitors. -> XREF: D06 T01 §10

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `flate_compress` stays a pure function of its input and level with the same API, output format and bounds as `§34`.

**Build order:**

1. Tune `Default` in `src/pdf/flate_compress.cpp` behind the existing API. Done when: it compiles on `x64` and `ARM64` and every `PdfFilters.FlateCompress*` test passes.
2. Add `scripts/flate-bench.py` and its tests, and raise the gates in `tests/recto-tests/pdf_filters_tests.cpp`. Done when: the checkpoint passes on this host.

- [x] `Default` searches 64 links per position with a lazy window to 6 bytes and no 3-byte table (`LevelParams::three_byte`, kept for `Fast` and `Best`), with no allocation for a table a level does not use. Measured on this host with both versions interleaved in one binary (median of nine, Release x64): content 360,736 bytes at 1.18 times `§34`'s speed (`§34`: 364,038), image rows 48,758 at 1.26 times (48,942), collisions 62,161 at 0.98 times, level within noise (63,205); `Fast` and `Best` write the same bytes as `§34`. Done when: `PdfFilters.FlateCompressLevels` holds `Default` to those sizes and its budgets (64 links), and `Best` to `§34`'s sizes on every file `reference-sizes.json` lists, each read back strictly.
- [x] `scripts/flate-bench.py` requires Python's zlib to be zlib-ng 2.2 or later (`zlib.ZLIBNG_VERSION`), prints the version it measured against and refuses (exit 2) when the backend is absent, times zlib-ng at level 6 (median of seven) and runs `recto-tests.exe --gtest_filter=PdfFilters.FlateCompressSpeed` (which prints each level's median and size per sample) from the executable given on its command line, and exits 1 when a sample is missing, the test is skipped or fails, `Default` is larger than zlib-ng on the content or image sample, or `Default`'s speed falls under 0.62, 0.60 or 0.54 of zlib-ng's on content, image rows or collisions (about 10% under the lowest of five runs of this gate on this host: 0.69 to 0.76, 0.68 to 0.86 and 0.60 to 0.65; the test binary runs slower than a standalone build, and zlib-ng's own speed varied from 53 to 61 MB/s between runs). **Corrected 2026-10-25:** was 0.80, 0.90 and 0.60, taken from a scratch build rather than the gate. Done when: `python -m unittest tests.scripts.test_flate_bench` checks the refusals, the missing-result failure and the comparisons with a fake executable, and the script passes against the Release build.
- [x] Commit: `pdf: a faster, smaller Default Flate level, measured against zlib-ng`

**Test checkpoint:** `build/build.ps1 -Platform x64 -Configuration Release -TestFilter PdfFilters.FlateCompress*` passes, `python scripts/flate-bench.py --exe build/bin/x64/Release/recto-tests/recto-tests.exe` passes on this host, and `recto-tests.exe --gtest_filter=PdfFilters.FlateCompress*` passes under ASan.

> **Verified:** 2026-10-25 | §37 | 87fde24776518b9727fbe9d1ef197fc762b464b2
> **Review:** `docs/reviews/d01-t02-s37.json`

## 38. Cancellable Flate compression and incremental revisions (`CancelToken`)

Filed 2026-10-24 by the `D01 T02 §34` completion review: a caller can only discard a compression it no longer needs, so a large stream holds a save, an optimize job or shutdown until it ends. -> XREF: D01 T02 §34

> **Corrected 2026-10-25:** widened by the `§5` completion review (`docs/reviews/01-engine/D01-T02-s5.md`, excellence 2): a revision of large streams compresses, encrypts and serializes them in one call, so the incremental writer takes the same token and reports progress. Was titled "Cancellable Flate compression (`CancelToken` polled per block)". -> XREF: D01 T02 §5

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that a cancelled compression publishes no partial output and leaves no state behind.

**Build order:**

1. Add a `CancelToken` (`§7`) parameter to `flate_compress` in `src/pdf/filters.h` and `src/pdf/flate_compress.cpp`, polled before each block. Done when: it compiles and `§34`'s tests pass unchanged.
2. Add `PdfFilters.FlateCompressCancel`. Done when: it passes under ASan.

- [ ] A token cancelled before the call returns `Cancelled` without work; one cancelled mid-stream returns `Cancelled` within 10 ms in Release x64 on `flate/collisions.bin` at `Best` (the slowest case), with no output; the next call compresses normally. Done when: `PdfFilters.FlateCompressCancel` checks the three cases and the latency.
- [ ] `IncrementalWriter::revision` and `write` (`src/pdf/incremental_writer.h`) take the token and an optional progress callback (bytes written of the staged total, never decreasing), polled between objects and passed to each compression; a cancel returns `Cancelled` within 100 ms on a revision of 256 MiB of streams in Release x64, keeps the staged edits, and a retry writes the same revision. Done when: `PdfIncrementalWriter.Cancel` checks the latency, the kept edits, the retry and that progress never decreases and ends at the total.
- [ ] `StagedRevisionChain::push`, `undo`, `redo` and `export_squashed` (`§13`) take the token and pass it to the writer, so a compaction or a rebuild below it stops within 100 ms and leaves the chain as it was (filed by the `§13` completion review). Done when: `PdfStagedRevisions.Cancel` cancels a compaction and a rebuilding undo and reads the same state after. -> XREF: D01 T02 §13
- [ ] Commit: `pdf: cancellable Flate compression and incremental revisions`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFilters.FlateCompressCancel` passes under ASan with 0 warnings.

## 39. Flate compressor at zlib-ng parity: a medium-effort parser and SIMD paths

Filed 2026-10-25 from `§37` by owner decision: the parity `§37` was filed for, which tuning did not reach. On this host (zlib-ng 2.2.4, level 6, median of seven) zlib-ng compresses content at 55.0 MB/s to 363,179 bytes, image rows at 55.6 MB/s to 48,947 and collisions at 30.3 MB/s to 61,593; `§37`'s `Default` reaches 47, 53 and 20 MB/s at 360,736, 48,758 and 62,161 bytes. Its measurements showed lazy matching costs about 45% of `Default`'s time, and that even 8 links per position leave content near 52 MB/s, so the remaining gap is the parser and the per-position work, not the search depth. -> XREF: D01 T02 §37

Scheduled by the same decision after the v1 engine (Phase 6, M6 Ship), once `D06 T01 §7`'s benchmark harness can hold the gain per machine; nothing before it waits on parity, since the writers consume `flate_compress` through its unchanged API. -> XREF: D06 T01 §7

**Phase:** 6
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `flate_compress` stays a pure function of its input and level with the same API, output format and bounds as `§34`.

**Build order:**

1. Rework the `Default` parser and the per-position work in `src/pdf/flate_compress.cpp`. Done when: every `PdfFilters.FlateCompress*` test passes on `x64` and `ARM64`.
2. Raise `scripts/flate-bench.py`'s gates to parity. Done when: the checkpoint passes on this host.

- [ ] `Default` at zlib-ng level 6's speed or better on content, image rows and collisions at no larger output on any of them: candidates such as zlib-ng's medium-effort parser (one search per match, looking ahead from the match's end and moving the next match back into it), SIMD match comparison and hashing (SSE4.2 and AVX2 on `x64`, NEON on `ARM64`), each kept only when it measures. Done when: `scripts/flate-bench.py` holds `Default` to 1.00 of zlib-ng's speed and size on all three samples.
- [ ] No file larger than `§34` wrote it at the same level, the 1,339-byte `Default` output of `fonts/truetype-hostile-budgets.ttf` (`§34`: 1,328) included. Done when: `PdfFilters.FlateCompressLevels` holds every level to `§34`'s sizes on every listed file.
- [ ] `Best` at 10 MB/s or more on `flate/collisions.bin` without larger output on any sample (zlib-ng level 9 runs it at 1.3 MB/s, `Best` at 1.9): a search that stops when longer candidates stop appearing, or a binary-tree match finder as libdeflate's high levels use. Done when: `PdfFilters.FlateCompressSpeed` holds the floor.
- [ ] Commit: `pdf: Flate compressor at zlib-ng parity`

**Test checkpoint:** `python scripts/flate-bench.py --exe build/bin/x64/Release/recto-tests/recto-tests.exe --parity` passes on this host, and `recto-tests.exe --gtest_filter=PdfFilters.FlateCompress*` passes under ASan.

## 40. Staged revisions over shared bytes: no file copy per edit, and a 10,000-edit history

Filed 2026-10-25 by the `§13` completion review (`docs/reviews/01-engine/D01-T02-s13.md`, excellence 2 and 3): every push, undo and redo copies the whole working file into a new `Document` and reparses it, so a small edit on a large file costs time and memory in proportion to the file, and the history stops at 1,000 states where Foxit keeps 10,000 actions. -> XREF: D01 T02 §13

**Phase:** 6
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that a working `Document` still owns its bytes through an immutable shared buffer used by its one actor thread, and that the base stays an untouched prefix.

**Build order:**

1. Let `Document` open over a shared immutable byte buffer, and let `StagedRevisionChain` reuse the parsed base cross-reference across revisions. Done when: every `PdfStagedRevisions.*` and `PdfIncrementalWriter.*` test passes.
2. Measure and hold the gains. Done when: the checkpoint passes.

- [ ] `Document::open_shared(std::shared_ptr<const std::vector<uint8_t>>, password)` keeps the caller's buffer instead of copying it, and the chain builds each new buffer once (base prefix shared by reference where the platform allows, otherwise one copy per transition rather than two). Done when: `PdfStagedRevisions.SharedBytes` shows one buffer allocation per push on a 64 MiB base with `::recto::test::ScopedHeapMeter`, and undo allocating no copy of the base.
- [ ] Push, undo and redo of a one-object edit on a 100 MB base take under 100 ms at the 95th percentile in Release x64. Done when: `PdfStagedRevisions.LargeFileLatency` reports the percentiles and holds them on this host.
- [ ] The history keeps 10,000 states within `kMaxRetainedHistoryBytes` by storing change sets compactly (values shared between states instead of copied). Done when: `PdfStagedRevisions.TenThousandEdits` pushes and undoes 10,000 small edits within the budget.
- [ ] Commit: `pdf: staged revisions over shared bytes`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfStagedRevisions.*` passes in Release x64 with the latency gate held.

## 41. Cubic interpolation for multi-input `/Order 3` sampled functions

Filed 2026-10-27 by the `§33` completion review (`docs/reviews/01-engine/D01-T02-s33.md`, excellence 1 of both A/B arms): ISO 32000-1 7.10.2 gives `/Order 3` cubic spline interpolation for any number of inputs, and `§33` draws it as a natural cubic spline only for one input, linearly with a diagnostic for more, so a DeviceN tint transform sampled at `/Order 3` renders its in-between tints along straight lines where the author asked for curves.

**Phase:** 6
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the spline tables are built once at parse time, held in the immutable `Function`, and evaluated without allocation or shared mutable state.

**Build order:**

1. Build tensor-product natural cubic splines for sampled functions of 2 to 8 inputs in `src/pdf/function.cpp`, charged to `kMaxFunctionTreeBytes`. Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below. Done when: they pass under ASan.

- [ ] Tensor-product natural cubic spline for 2 to 8 inputs (the spline of `§33`'s one-input case along each input in turn), each evaluation reading at most `4^inputs` samples and the spline tables counted in `kMaxFunctionTreeBytes`, past which the function is drawn linearly with its limit recorded; more than 8 inputs stay on `§33`'s simplex interpolation with a diagnostic. Done when: `PdfInterpreter.CubicFunctions` matches independently computed tensor-spline values at interior points of a 2-input and a 4-input table where the linear result differs, and records the limit for a table whose splines pass the budget.
- [ ] Commit: `pdf: cubic interpolation for multi-input sampled functions`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.Functions:PdfInterpreter.CubicFunctions` passes under ASan with 0 warnings.

## 42. Text state, positioning and text-showing operators with ISO 32000-1 advances

> **Started:** 2026-10-28T01:33Z

Filed 2026-10-28 from `§4`'s split (validation before the build): text state (`BT`, `ET`, `Tc`, `Tw`, `Tz`, `TL`, `Tf`, `Tr`, `Ts`), positioning (`Td`, `TD`, `Tm`, `T*`) and showing (`Tj`, `TJ`, `'`, `"`) with ISO 32000-1 section 9.4.4 advances, recorded as glyph runs for `§43`'s glyphs (ISO 32000-1 sections 9.3 and 9.4). The `/None` rule for text moved here from `§44`'s item.

> **Corrected 2026-10-28:** (validation before the build) the section named no source for a code's width or for splitting a string into codes, so a `TextFont` in `src/pdf/text_font.h/.cpp` now resolves both for every font kind; horizontal scaling and rise needed a home in the recorded run; vertical writing (`/WMode 1`) was unmentioned; `Tr 3` was "skip drawing", which would lose the positions text extraction and searchable OCR layers need, so an invisible run is recorded and drawn by nothing; `Tr 4` to `7` record their mode for `§16`, which clips at `ET`. Glyph indices and outlines are `§43`'s: this section records each glyph's index as 0 until `§43` fills it.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the interpreter keeps every page's state on the stack of its one `interpret_page` call, records only into the page's `DisplayListBuilder`, that a `TextFont` is immutable once resolved, and that nothing mutates a shared `Document` dictionary.

**Build order:**

1. Implement `TextFont` in `src/pdf/text_font.h` and `src/pdf/text_font.cpp` (added to `src/pdf/recto-pdf.vcxproj`) and the text operators in `src/pdf/interpreter.cpp`. Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below to `tests/recto-tests/pdf_interpreter_tests.cpp`. Done when: they pass under ASan.

- [x] `TextFont` (immutable, `src/pdf/text_font.h`): `TextFont::resolve(const Document&, const PdfValue& font)` gives a font's codes and widths for positioning (ISO 32000-1 9.2.4, 9.6, 9.7): one byte per code for simple and Type 3 fonts, the CMap's codespace for `/Type0` through `§30`'s `Type0Font::next`; widths from `/FirstChar`, `/Widths` and the descriptor's `/MissingWidth` in thousandths of text space for simple fonts, `§27`'s AFM widths by `§28`'s effective encoding glyph names for a Standard 14 font with no `/Widths`, `/Widths` through `/FontMatrix` for Type 3, `§30`'s `/W` and `/DW` (and for `/WMode 1` its `/W2` and `/DW2` vertical metrics) for `/Type0`; `next(bytes)` returning each code with its length, its horizontal advance `w0`, for `/WMode 1` its vertical displacement `w1` and position vector `(vx, vy)` (`/W2`, else `/DW2` and half the horizontal advance), and whether it is the single-byte code 32 that word spacing applies to; the font's bounding box in text space (the descriptor's `/FontBBox`, a Standard 14 font's AFM `FontBBox`, a Type 3 `/FontBBox` through `/FontMatrix`, a `/Type0` descendant's descriptor), for runs' conservative bounds until `§43` gives each glyph its own; resolved once per interpretation by object key; a font that is missing, damaged (`DamagedFormat`), of an unsupported kind (`UnsupportedFeature`) or refused by a bound (`ResourceLimit`) positions its text with Helvetica's AFM widths and box (a recorded default, cheap to change), its diagnostic recorded, while `OutOfMemory` and any other resolver failure stop the page as every resolver failure in this interpreter does (**Corrected 2026-10-28:** plan review: the vertical position vector, the bounding box, and which failures recover). Done when: `PdfInterpreter.TextFonts` checks the widths and code lengths of a simple Type 1 with `/Widths` and `/MissingWidth`, a Standard 14 font without `/Widths` under `/Differences`, a Type 3 font with a non-default `/FontMatrix`, a `/Type0` over an Identity-H CMap with `/W` and a vertical `/Type0` with `/W2` and with only `/DW2` (both vectors asserted), each kind's bounding box, the Helvetica fallback for a missing `/Font` resource and for a damaged one with their diagnostics, and `OutOfMemory` passed on when allocation fails while resolving a font.
- [x] Implement text state operators (`BT`, `ET`, `Tc`, `Tw`, `Tz`, `TL`, `Tf`, `Tr`, `Ts`) and text matrix positioning (`Td`, `TD`, `Tm`, `T*`): the text matrix and text line matrix reset to identity by `BT` while the text state parameters persist, the text state parameters (`Tc`, `Tw`, `Tz`, `TL`, the font and size, `Tr`, `Ts`) saved and restored by `q` and `Q` and the matrices not (9.3.1, 9.4.1), `TD` setting `TL` to `-ty`, `Tm` replacing both matrices, the font and size also set by `gs` through `/ExtGState /Font [font size]` (Table 58) as `Tf` sets them, a show operator outside `BT ... ET` and a nested `BT` read as readers do (shown with the current matrices; a nested `BT` resets them) (**Corrected 2026-10-28:** plan review: the lifecycle each assertion must prove, and `/ExtGState /Font`). Done when: `PdfInterpreter.TextPositioning` computes exact user-space glyph positions across multi-line `Td`/`T*` sequences under a rotated and sheared CTM, and asserts `BT` resetting the matrices but not `Tc` or the font, `q`/`Q` inside a text object restoring the parameters but not the matrices, `TD`'s leading, `Tm` replacing both matrices, a nested `BT`, text shown outside a text object, and a font selected only through `gs` with its size and its restoration by `Q`.
- [x] Implement text-showing operators (`Tj`, `TJ`, `'`, `"`) computing per-glyph horizontal/vertical advances: `tx = ((w0 - TJ_num / 1000.0) * font_size + Tc + (char == 0x20 ? Tw : 0)) * (Tz / 100.0)`, and for vertical writing `ty = (w1 - TJ_num / 1000.0) * font_size + Tc + (char == 0x20 ? Tw : 0)` with each glyph shifted by its position vector `v` (9.4.4, 9.7.4.3); word spacing applies only to the single-byte code 32. One glyph run per shown string through `DisplayListBuilder::draw_glyph_run`: the run's text matrix is `[Tz/100 0 0 1 0 0] x Tm` at the string's start, each glyph's origin the advances before it in unscaled text space with the rise `Ts` as its y, the font size `Tf`'s, the colours the current fill and stroke converted as `§33` converts them, and the run's conservative bounds the `TextFont`'s box scaled by the font size over the run's advances (each glyph's own box is `§43`'s); a number in a `TJ` array before the first string, after the last or between strings moves the text matrix by `-num / 1000 * Tfs` (times `Tz / 100` horizontally) without character or word spacing (**Corrected 2026-10-28:** plan review: the run's bounds, which the builder needs for a visible run, and `TJ` numbers outside strings). Done when: `PdfInterpreter.TjKerningAndSpacing` places glyphs at exact ISO 32000-1 section 9.4.4 coordinates (with `Tc`, `Tw` on code 32 only, `Tz`, `Ts`, `TJ` adjustments, `'` and `"`, and a vertical `/Type0` line with its position vectors), advances the text matrix by the whole string, places the text after a `TJ` that starts with a number, ends with one and holds only numbers, and records a visible run whose bounds hold every glyph origin.
- [x] Text rendering modes (9.3.6): `Tr 0` to `2` record runs that fill, stroke or both; `Tr 3` records the run with `TextRenderMode::Invisible`, which nothing draws but text extraction reads; `Tr 4` to `7` record their mode for `§16`, which adds the glyphs to the clip at `ET`; a fill space whose `ColorSpace::paints()` is false (`§33`) drops only the fill from the mode (`Fill` becomes `Invisible`, `FillAndStroke` becomes `Stroke`, `FillAndClip` becomes `Clip`, `FillStrokeAndClip` becomes `StrokeAndClip`), a stroke space that paints nothing drops only the stroke likewise, and the run is still recorded and the text matrix still advanced, so strokes, clipping and positions survive (**Corrected 2026-10-28:** plan review: was "records no glyph run", which also lost strokes, clips and invisible positions). Done when: `PdfInterpreter.TextRenderModes` records each mode on its run, keeps an invisible run's glyph positions, and crosses every mode with a `/Separation /None` fill space and a `/Separation /None` stroke space, asserting each resulting mode, while the same text in `/Separation /Spot` spaces keeps its mode.
- [x] Commit: `pdf: text state, positioning and text-showing operators`

> **Resolved:** a shared font object parsed again for every font that names it (round 1; the per-page font budget bounds memory, not parse work) -> XREF: D01 T01 §11 (2026-10-28: closed by D01 T01 §11, whose resolved-object cache parses a shared object at most twice per document and hands it out without a copy; building a shared descendant once per font continues in D01 T02 §48)

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.TextFonts:PdfInterpreter.TextPositioning:PdfInterpreter.TjKerningAndSpacing:PdfInterpreter.TextRenderModes:PdfInterpreter.*` passes under ASan with 0 warnings.

> **Verified:** 2026-10-28 | §42 | 53044b5ef4b3cc673d17ae943fde14ec043db455
> **Review:** `docs/reviews/d01-t02-s42.json`

## 43. Glyphs for Standard 14, simple, embedded Type 1, CFF, TrueType and Type 0 text

> **Started:** 2026-10-28T08:53:34Z

Filed 2026-10-28 from `§4`'s split (validation before the build): each font kind's glyphs for `§42`'s glyph runs: non-embedded Standard 14 faces fitted to their AFM advances (`§27`), simple-font encodings and TrueType cmaps (`§28`), embedded Type 1, CFF and OpenType programs (`§29`) and composite `/Type0` fonts (`§30`).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the interpreter keeps every page's state on the stack of its one `interpret_page` call, records only into the page's `DisplayListBuilder`, and mutates no shared `Document` dictionary.

**Corrected 2026-10-28:** validation before the build: a glyph run carries one font handle and `PositionedGlyph` had no per-glyph horizontal scale, while a Standard 14 string can draw from two faces (`Symbol`'s Euro from `Segoe UI Symbol`) and each substitute glyph needs its own fitting scale, and `TextFont` (`§42`) gave codes and widths but no glyphs; so the section adds `PositionedGlyph::width_scale`, a per-code glyph table on `TextFont`, and one run per face within a shown string. Before, it named only the three drawing items.

**Build order:**

1. Add `PositionedGlyph::width_scale` to `src/pdf/display_list.h` and carry it through `DisplayListBuilder::draw_glyph_run`. Done when: a recorded glyph reads back its scale.
2. Give `TextFont` (`src/pdf/text_font.h`, `src/pdf/text_font.cpp`) each code's glyph: its face, glyph index and width scale. Done when: every font kind answers `TextFont::glyph` for the fixtures below.
3. Record glyph runs with real glyphs and faces in `src/pdf/interpreter.cpp`, one run per face within a string. Done when: the tests below pass under ASan.

- [ ] Add `double width_scale{1.0}` to `PositionedGlyph` (`src/pdf/display_list.h`): the horizontal glyph-space scale the rasterizer applies to the outline before placing it at `origin_pt` (its advance is already in `advance_pt`); `draw_glyph_run` refuses a scale that is not finite and positive as `DamagedFormat`. Done when: `PdfDisplayList.GlyphWidthScale` records glyphs with scales 1.0 and 0.6 and reads both back, and refuses 0, a negative and an infinite scale. Its replay is `§6`'s. -> XREF: D01 T02 §6
- [ ] Add `TextFont::glyph(const TextCode&) -> TextGlyph{face, glyph, width_scale, ink, blank}` and `TextFont::faces()` (`std::shared_ptr<const LoadedFont>` each, the run's font handle), built when the font resolves: a 256-entry table for one-byte kinds with each code's ink box (`LoadedFont::ink_box`, none for an empty outline, which is `blank`), and for a composite font the glyph `Type0Font::next` already decodes, kept in a new `TextCode::glyph` (**Corrected 2026-10-28:** plan review: `Type0Font`'s `CidToGidMap` is private, and `next` returns the glyph `TextCode` dropped), its ink box looked up once per face and glyph on the page (a memo on the interpreter's page state). The faces' memory counts toward `TextFont::memory_bytes`, which `§42`'s page font budget charges, a face shared with other fonts (a system face, one embedded program named by several fonts) counted once per font that holds it. Done when: `PdfInterpreter.TextFontGlyphs` checks the glyph, face, ink and blank flag of known codes for each kind below, a composite font over a non-identity `/CIDToGIDMap`, and a page of fifty fonts over one embedded program staying within the font budget while fifty distinct 2 MiB programs reach it with `WorkingBytes` recorded.
- [ ] Record each run's glyphs and font handle in `show()` (`src/pdf/interpreter.cpp`): a shown string is split into consecutive runs of codes drawn from the same face, each with its own run matrix offset, the text matrix advancing over the whole string as before; each glyph carries its `ink_bounds_pt` (its face's ink box scaled by the font size, the `width_scale` and `Tz`) and `is_blank` for an empty outline, so a run's recorded bounds are the union of its glyphs' ink, never the font-wide box (`§42` assigned each glyph's own box here). A code no face can draw (no face loads: the system face missing or its loader failing, the font's diagnostic recorded) is an advance-only glyph: `is_blank`, glyph 0, in a run whose font handle is null, so positions and the text after it stay where the file put them and nothing is painted. Done when: `PdfInterpreter.Standard14FittedWidths` shows a `Symbol` string holding the Euro and checks two runs with their faces and origins continuing across the split; `PdfInterpreter.GlyphInkBounds` checks a spaces-only string (every glyph blank, no bounds), a glyph with a negative left bearing and one overhanging its advance, and a split run's bounds each the union of its own glyphs; and `PdfInterpreter.MissingFaceText` injects a face-loader failure (`detail::ScopedFontLoaderFailure`) and checks the advance-only run with a null handle, the diagnostic, and the next run's origin.
- [ ] Draw a non-embedded standard font's glyph through its Windows face (`§27`'s `standard14_face_glyph`, by the code's effective glyph name through `drawable_unicode`, except `Symbol`, whose face is reached by the name's code in Symbol's built-in encoding (`standard14_glyph_by_name(Standard14Font::Symbol, name)->code`, `/Differences` included), as `standard14_face_glyph` expects; **Corrected 2026-10-28:** plan review: the Unicode route does not reach Symbol's symbol `cmap`) scaled horizontally to its AFM advance wherever the face's own advance differs, so a glyph never overlaps or gaps its neighbours (filed by the `§27` completion review, `docs/reviews/01-engine/D01-T02-s27.md`; the reviewed differences are `kKnownWidthDifferences` in `PdfFont.Standard14Repertoire`: `summation` 713 against 600, `macron` 552 against 333, and so on). Done when: `PdfInterpreter.Standard14FittedWidths` draws a Helvetica and a Times line holding each of those glyphs with every glyph origin at the sum of the AFM advances before it and each substitute glyph's `width_scale` its AFM width over its face advance (an overhang the AFM box allows, such as Times-Italic `macron`, stays), and a `Symbol` line of Greek letters, mathematical symbols (`summation`, `integral`) and the Euro, one of them reached through `/Differences`, with the exact glyph IDs and faces. Replaying `width_scale` is `§6`'s (its glyph replay item). -> XREF: D01 T02 §27
- [ ] Draw simple-font text through `§28`: each code's glyph by the font's effective encoding (`SimpleEncoding::build` with the font's built-in encoding: `§27`'s AFM codes for a standard font, `§29`'s for an embedded Type 1) and, for a TrueType program, `simple_truetype_glyph` over its `TrueTypeTables` (filed by the `§28` completion review, `docs/reviews/01-engine/D01-T02-s28.md`). Done when: `PdfInterpreter.SimpleFontGlyphs` draws real-PDF fixtures (a WinAnsi Type 1 with `/Differences`, a symbolic TrueType through `(3,0)`, a non-symbolic TrueType through `(1,0)`) with the exact glyph IDs asserted. -> XREF: D01 T02 §28
- [ ] Load each embedded font program by its font descriptor key (filed by the `§29` completion review, `docs/reviews/01-engine/D01-T02-s29.md`): `/FontFile2` and `/FontFile3 /Subtype /OpenType` through `load_embedded_font`, `/FontFile` through `load_charstring_font` with `CharstringFormat::Type1` and the stream's `/Length1` to `/Length3`, `/FontFile3 /Subtype /Type1C` and `/CIDFontType0C` through `load_charstring_font`; each code's glyph by `LoadedFont::glyph_for_name` over the effective encoding built on `built_in_encoding()`, except an OpenType program, which `load_embedded_font` loads without its CFF charstrings: an OpenType program draws through its `cmap` (`simple_truetype_glyph` over its `TrueTypeTables`) when it has one, else through its wrapped `CFF ` table's charset and encoding (`load_charstring_font` over that table with `CharstringFormat::Type1C`) (**Corrected 2026-10-28:** plan review: the route named before gave glyph 0 for every simple font embedding OpenType); the advance from `/Widths`, the program's own as fallback; a program refused with its typed error drawn in `§27`'s Standard 14 face matching its font descriptor's `/Flags` (fixed pitch, serif, symbolic) and `/FontWeight` and `/ItalicAngle`, with its diagnostic recorded (**Corrected 2026-10-28:** was "the substitute font of `§17`"; `§17` depends on `§4`, which now depends on this section, so `§17` widens this fallback later); `OutOfMemory` while loading stops the page as `§42` specifies. A `/Type0` font draws through `§30`'s `load_type0_font`, each code of a shown string by its CID, glyph and `/W` advance. Done when: `PdfInterpreter.EmbeddedType1AndCffText` draws a PDF holding an embedded Type 1 (a PFA stream with its lengths), a `/Type1C`, a simple font over an OpenType CFF program and one over an OpenType TrueType program, a `/Type0` over a `/CIDFontType0C` and a `/Type0` over a `/CIDFontType2` font with the expected glyph IDs and faces at the expected origins; a Type 1 with a custom built-in encoding and no `/BaseEncoding` maps by its program's encoding; a font with no `/Widths` advances by its program's widths (which differ from Helvetica's), and one with conflicting `/Widths` by the dictionary's; a PDF whose Type 1 program is damaged draws its text in the matching standard face with the damage recorded; and `PdfInterpreter.FontLoaderOutOfMemory` makes a program load run out of memory and checks that `interpret_page` returns `OutOfMemory` with no list. -> XREF: D01 T02 §29
- [ ] Commit: `pdf: glyphs for every font kind in text runs`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfDisplayList.GlyphWidthScale:PdfInterpreter.TextFontGlyphs:PdfInterpreter.Standard14FittedWidths:PdfInterpreter.GlyphInkBounds:PdfInterpreter.MissingFaceText:PdfInterpreter.SimpleFontGlyphs:PdfInterpreter.EmbeddedType1AndCffText:PdfInterpreter.FontLoaderOutOfMemory:PdfInterpreter.*` passes under ASan with 0 warnings (the allocation test in x64 Debug, where injection is available).

## 44. Image XObjects with stencil masks, explicit and colour-key masks and soft masks

Filed 2026-10-28 from `§4`'s split (validation before the build): `/Do` for `/Subtype /Image`: samples decoded through the stream filters and converted through `§32`'s and `§33`'s colour spaces, stencil masks, explicit and colour-key `/Mask`, `/SMask` alpha and `/Interpolate`, recorded as `DrawImage` in the unit square under the CTM (ISO 32000-1 sections 8.9.4 to 8.9.6, 11.6.5.3). The text half of the `/None` rule moved to `§42`.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that decoded image bitmaps are stored as `std::shared_ptr<const DecodedBitmap>` in `DisplayList` and that decoding mutates no shared `Document` dictionary.

**Build order:**

1. Implement it in `src/pdf/interpreter.cpp`. Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below to `tests/recto-tests/pdf_interpreter_tests.cpp`. Done when: they pass under ASan.

- [ ] Implement `/Do` for `/Subtype /Image` XObjects (`PdfInterpreter.ImageXObjects` covering each case), converting samples through `§32`'s `ColorSpaces::resolve` and `ColorSpace::convert_samples` (the image's own ICC profile when its dictionary names no colour space, `/Default*` spaces applied), supporting 1/2/4/8/16-bit components, `/Indexed` palettes, `/ImageMask true` (painting 1-bit stencil masks in the current non-stroking colour), explicit `/Mask`, and grayscale `/SMask` alpha compositing; stencil masks painted in a fill space whose `ColorSpace::paints()` is false (Separation or all-`/None` DeviceN, or Indexed over one, `§33`) and images whose own colour space paints nothing draw nothing (**Corrected 2026-10-27:** from `§33`'s validation and plan review). Done when: `render/images-and-vectors.pdf` decodes and records all embedded JPEG, Flate+SMask, and CCITT mask images, and `PdfInterpreter.NoneColorant` records no command for an `/ImageMask` image and a `/Separation /None` image drawn over a red rectangle, while the same content in a `/Separation /Spot` space records each.
- [ ] Commit: `pdf: image XObjects with masks and soft masks`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.ImageXObjects:PdfInterpreter.NoneColorant` passes under ASan with 0 warnings.

## 45. Inline images decoded on the interpreter's verified framing

Filed 2026-10-28 from `§4`'s split (validation before the build): inline images (`BI ... ID ... EI`) decoded through `§44`'s image path on the framing `§3` already verifies (ISO 32000-1 section 8.9.7).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the interpreter keeps every page's state on the stack of its one `interpret_page` call, records only into the page's `DisplayListBuilder`, and mutates no shared `Document` dictionary.

**Build order:**

1. Implement it in `src/pdf/interpreter.cpp`. Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below to `tests/recto-tests/pdf_interpreter_tests.cpp`. Done when: they pass under ASan.

- [ ] Implement inline image parsing (`BI ... ID <data> EI`) expanding abbreviated dictionary keys (`W`, `H`, `BPC`, `CS`, `F`, `DP`, `IM`, `I`) and locating the `EI` delimiter safely via expected decoded row size + filter length. Done when: `PdfInterpreter.InlineImages` decodes `A85`, `Fl`, and raw inline images without misidentifying `EI` bytes inside binary pixel data. **Corrected 2026-10-24:** `D01 T02 §3` already parses the dictionary and frames the data (by length unfiltered, by decoding to the full sample size filtered, a 64 MiB work budget, CR LF after `ID`) and refuses an image whose end it cannot verify; this item decodes the image on that framing and keeps the rule that no `EI` is taken on trust (the reviews of `§3` showed every printable-bytes heuristic runs pixels as operators).
- [ ] Commit: `pdf: inline images decoded into DrawImage`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.InlineImages` passes under ASan with 0 warnings.

## 46. Form XObjects with `/Matrix`, `/BBox` clip, resource fallback and a 32-level cap

Filed 2026-10-28 from `§4`'s split (validation before the build): `/Do` for `/Subtype /Form`: the form interpreted in a saved graphics state under its `/Matrix`, clipped to its `/BBox`, with its own `/Resources` or its parent's, at most 32 levels deep (ISO 32000-1 section 8.10).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that recursive `/Subtype /Form` interpretation enforces the 32-level depth cap without mutating shared `Document` dictionaries.

**Build order:**

1. Implement it in `src/pdf/interpreter.cpp`. Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below to `tests/recto-tests/pdf_interpreter_tests.cpp`. Done when: they pass under ASan.

- [ ] Implement `/Do` for `/Subtype /Form` XObjects (pushing graphics state, concatenating `/Matrix`, clipping to `/BBox`, binding `/Resources` with parent fallback, and enforcing the **32-level recursion ceiling**). Done when: `PdfInterpreter.FormXObjectCycle` stops a self-referencing `F1 -> F1` Form XObject at depth 32 and returns cleanly under ASan. Cheaper substitute: unbounded recursive `interpret()` calls that overflow the stack on cyclic Form XObjects.
- [ ] Commit: `pdf: Form XObjects with matrix, clip, resources and a depth cap`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.FormXObjectCycle` passes under ASan with 0 warnings.

## 47. Optional-content marked content recorded for layer visibility

Filed 2026-10-28 from `§4`'s split (validation before the build): marked content (`BMC`, `BDC`, `EMC`) with `/OC` property references recorded so `D02 T03 §7` toggles optional-content groups without re-parsing (ISO 32000-1 sections 8.11 and 14.6).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the interpreter keeps every page's state on the stack of its one `interpret_page` call, records only into the page's `DisplayListBuilder`, and mutates no shared `Document` dictionary.

**Build order:**

1. Implement it in `src/pdf/interpreter.cpp`. Done when: it compiles on `x64` and `ARM64`.
2. Add the tests below to `tests/recto-tests/pdf_interpreter_tests.cpp`. Done when: they pass under ASan.

- [ ] Record optional-content marked-content blocks (`BDC` / `BMC` ... `EMC` with `/OC` property references) so `D02 T03 §7` can toggle OCG layer visibility without re-parsing the PDF file. Done when: `PdfInterpreter.OptionalContentBlocks` skips the commands of a hidden group, keeps a visible one's, and nests `BMC`/`BDC` blocks within `EMC` balance (**Corrected 2026-10-28:** the item named no test).
- [ ] Commit: `pdf: optional-content marked content for layer visibility`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.OptionalContentBlocks` passes under ASan with 0 warnings.

## 48. One built CIDFont per shared `/Type0` descendant object

Filed 2026-10-28 from `D01 T01 §11`'s measurement: with the resolved-object cache, fifty `/Type0` fonts over one descendant whose `/W` holds 65,534 widths parse it twice in all (34 parses for the page) but still take 18.6 s in x64 Debug (75.7 s before the cache), because `load_type0_font` (`src/pdf/font_cid.cpp`) rebuilds the descendant's width runs, CIDToGIDMap and program for every font that names it, about 370 ms each. Acrobat and pdfium (`CPDF_CIDFont` per descendant dictionary) build a CIDFont once and share it. Recto shares the built descendant (its `CidWidths`, `CidToGidMap`, program and descriptor) across every `/Type0` font of a page that names the same descendant object, keyed by `object_key`, within the page's font budget (`§42`), while each font keeps its own encoding CMap. -> XREF: D01 T01 §11

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the shared descendants live on the stack of one `interpret_page` call (or an immutable value the `TextFont`s hold), are immutable once built, and that no `Document` dictionary is mutated.

**Build order:**

1. Split the descendant's built state out of `Type0Font` in `src/pdf/font_cid.h` and `src/pdf/font_cid.cpp`. Done when: two `Type0Font`s built over one descendant hold the same descendant pointer.
2. Share it through the interpreter's font resolution in `src/pdf/text_font.cpp` and `src/pdf/interpreter.cpp`. Done when: the fifty-font fixture builds its descendant once.
3. Test and measure. Done when: the tests below pass under ASan and the commit quotes the timing.

- [ ] Add `CidDescendant` (immutable: `CidWidths`, `CidToGidMap`, kind, program, its `memory_bytes()`) in `src/pdf/font_cid.h`, and let `load_type0_font` take an optional memo from descendant `object_key` to `std::shared_ptr<const CidDescendant>`, building on a miss and reusing on a hit. Done when: `PdfFont.SharedDescendant` loads two `/Type0` dictionaries with different encodings over one indirect descendant and checks one build (the same pointer), each font's own CMap, and widths equal to an unshared load.
- [ ] Charge a shared descendant once against the page's 64 MiB font budget (`TextFont::memory_bytes` counts it only for the font that built it) and keep the memo on the interpreter's page state. Done when: `PdfInterpreter.SharedDescendantBudget` shows fifty fonts over one 128 KiB-map descendant stay within the budget (no `WorkingBytes` refusal), while fifty distinct descendants of the same size still hit it as `PdfInterpreter.TextFontBudget` does.
- [ ] A descendant that fails to build is not memoized as a success: each font naming it takes the fallback with its diagnostic, as `§42` specifies. Done when: `PdfFont.SharedDescendant` resolves two fonts over a damaged descendant and both take the fallback, with the damage recorded.
- [ ] Share built encoding CMaps by stream object the same way, and read a shared stream's filter parameters through `SharedObjectResolver` (`decode_stream` in `src/pdf/filters.cpp` takes one alongside `IndirectObjectResolver`), so fonts naming one CMap stream decode it once and copy no `/DecodeParms` (found by `D01 T01 §11` round 1). Done when: `PdfFont.SharedDescendant` loads fifty `/Type0` fonts over one CMap stream whose `/DecodeParms` is indirect and checks one decode and no call to the copying resolver.
- [ ] Measure the fifty-font fixture of `PdfInterpreter.SharedObjectsParseOnce` in x64 Debug and Release. Done when: the Debug time falls at least tenfold from 18.6 s and the commit body quotes both timings.
- [ ] Commit: `pdf: one built CIDFont per shared Type 0 descendant`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFont.SharedDescendant:PdfInterpreter.SharedDescendantBudget:PdfInterpreter.SharedObjectsParseOnce:PdfInterpreter.*:PdfFont.*` passes under ASan with 0 warnings.

## Verification

- `recto-tests.exe --gtest_filter=PdfDisplayList.*:PdfFont.*:PdfInterpreter.*:PdfIncrementalWriter.*:PdfRasterizer.*:Actor.*:Scheduler.*:Runtime.*` passes in Debug, Release, and ASan builds.
- Debug live-object counters (`debug_live_documents()`, `debug_live_display_lists()`, `debug_live_workers()`) end at `0` after every test.
- `recto-tests.exe --gtest_filter=PdfBlend.*:PdfShading.*:PdfRasterizer.Transparency*:PdfRasterizer.Shading*:PdfRasterizer.TilingPattern*:PdfFont.NonEmbedded*:PdfFont.PredefinedCMaps*:PdfError.*:PdfInterpreter.Ceiling*:Scheduler.FailedTileNotCached:PdfDocument.UserUnit*` passes under ASan once `§15` to `§21` ship.
- `recto-tests.exe --gtest_filter=PdfStagedRevisions.*:Actor.PageTransition*:Actor.OcgEdit*:Scheduler.LineWeightMode*:Scheduler.TileKeyUsesCompiledGeneration` passes under ASan once `§11` to `§14` ship (`§12` and `§14` are phase 2).
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
