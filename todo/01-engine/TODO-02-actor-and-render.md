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

> **Current state:** Measured 2026-10-14. `src/core/recto-core.vcxproj` compiles the scaffold `recto::core::abi_version()` entry point. `D01 T01` provides `recto::pdf::Document`, decoded streams, resources, and page geometry.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 3.3 (`TileKey`, `DisplayList`, `CancelToken`), 4.1 items 4 to 6 (content-stream interpreter, font loading, incremental save), 4.2 to 4.8 (thread ownership, runtime map, actor, tile rendering, shutdown order).
- `design/SPEC.md` section 9.2 (page outlines immediately, tiles fill in, stretched zoom preview, dark-theme page colour modes).
- -> XREF: D01 T01 §1 -- consumes `recto::pdf::Document` (`§10`), `PdfResult<T>` (`§1`), and decoded page content and resource streams (`§7`, `§8`) from `src/pdf`.
- -> XREF: D01 T03 §6 -- the thread-safety verification battery in `D01 T03 §6` and `§7` stress-tests the actor, display-list, render-pool and cancellation machinery built here.
- -> XREF: D02 T01 §3 -- `DocumentTabSession` in `D02 T01 §3` owns a per-tab `DocumentHandle` and closes its `DocumentActor` (`§8`) on tab close; `OpenDocumentPipeline` in `D02 T01 §5` drives the actor's async open/authenticate flow, and `Save` in `D02 T01 §8` invokes `§5`'s incremental writer.
- -> XREF: D02 T02 §3 -- the viewer's presentation thread, `TileTextureCache` (`D02 T02 §3`), and viewport-to-scheduler driver (`D02 T02 §4`) consume the `TileKey`, BGRA premultiplied tile buffers, and `TileScheduler` (`§9`) built here.
- -> XREF: D02 T03 §7 -- Optional Content Groups (`D02 T03 §7`) filter `BDC`/`EMC` layer blocks during `DisplayList` compilation (`§3`, `§4`), and `FontInventory` (`D02 T03 §9`) inspects `§2`'s font descriptors.
- -> XREF: D04 T05 §5 -- the `PDF/A` font-subsetting compliance engine (`D04 T05 §5`) inspects `§2`'s font loader and embedded font streams.
- -> XREF: D05 T01 §8 -- `SignatureEngine` (`D05 T01 §8`) uses `§5`'s `IncrementalWriter` when counter-signing documents.

## Outcome

- `recto-pdf` interprets page content streams (vector paths, strokes, fills, clipping, `/ExtGState` alpha and blend modes, `/DeviceGray`/`RGB`/`CMYK`/`Indexed`/`ICCBased`/`Separation` colours, image and Form XObjects, and embedded/Standard-14 fonts via DirectWrite) into immutable, reference-counted `DisplayList`s, and writes non-destructive incremental PDF revisions (`src/pdf/incremental_writer.cpp`).
- Each open document runs on its own `DocumentActor` thread (`src/core/document_actor.cpp`) with a 32-page LRU cache of `DisplayList`s; the UI thread never blocks on PDF parsing or content-stream interpretation.
- `RenderPool` worker threads rasterize `512 x 512` premultiplied BGRA tiles from `std::shared_ptr<const DisplayList>` in parallel via per-worker Direct2D / WIC bitmap targets, honoring `CancelToken` every 64 commands and producing byte-identical output regardless of thread count.
- `Runtime::~Runtime()` tears down actors, jobs, workers and presentation hooks in the exact 6-step order of foundation plan section 4.8, verified by zeroed Debug live-object counters.

**Adjacency:** all=not-applicable (engine rendering, incremental writing, and concurrency runtime in `src/pdf` and `src/core`; UI presentation and controls live in domain 02)

**Adjacency rationale:** Tile rendering, caching and scheduling are internal engine mechanisms whose outputs feed `DocumentView` in `D02 T02`. No user-editable settings or persistent records are owned in this file (the texture cache budget is owned by `D02 T02 §3` and `D02 T04 §6`).

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Immutable `DisplayList` command buffer and `DisplayListBuilder` (`display_list.cpp`) | D01 T01 §10 | [ ] |
| 2 | §2 | DirectWrite in-memory font loader, Standard 14 metrics, Type 1/3 and `/ToUnicode` CMap | §1 | [ ] |
| 3 | §3 | Content-stream graphics state machine, path construction/clipping and colour spaces | §1 | [ ] |
| 4 | §4 | Content-stream text positioning, `/Subtype /Image`, inline images and `/Form` XObjects | §2, §3 | [ ] |
| 5 | §5 | Incremental PDF revision writer (`src/pdf/incremental_writer.cpp`) | D01 T01 §10 | [ ] |
| 6 | §6 | Per-worker Direct2D / WIC `DisplayList` tile rasterizer (`src/pdf/rasterizer.cpp`) | §4 | [ ] |
| 7 | §7 | Cooperative `CancelToken` and sub-10 ms mid-replay tile cancellation | §6 | [ ] |
| 8 | §8 | `DocumentActor` thread, MPSC queue, 32-page `DisplayList` cache and password state | §4, §5 | [ ] |
| 9 | §9 | `RenderPool` worker threads and 4-tier priority `TileScheduler` with jump cancellation | §7, §8 | [ ] |
| 10 | §10 | `Runtime` coordinator, section 4.8 ordered shutdown sequence and live-object counters | §9 | [ ] |

## 1. Immutable `DisplayList` command buffer and `DisplayListBuilder` (`display_list.cpp`)

Implement the thread-safe, immutable display-list representation in `src/pdf/display_list.h` and `src/pdf/display_list.cpp`:
- Value types for recorded commands: `SaveState`, `RestoreState`, `ConcatMatrix` (`PdfMatrix`), `SetStrokeStyle` (width, cap, join, miter limit, dash array/phase), `SetAlphaAndBlend` (`stroke_alpha`, `fill_alpha`, `BlendMode`), `PushClipPath` (path index, fill rule `NonZero` vs `EvenOdd`), `DrawPath` (path index, stroke colour, fill colour, stroke/fill flags, fill rule), `DrawGlyphRun` (font face handle/Type 3 sub-list, positioned glyph indices and `(x, y)` origins in text/user space, font size, text rendering mode `Tr`, fill/stroke colours, CTM/text matrix), and `DrawImage` (immutable `std::shared_ptr<const DecodedBitmap>`, destination transform, interpolation hint, soft-mask).
- `DisplayListBuilder`: records commands, interns path geometry segments (`MoveTo`, `LineTo`, `CubicBezierTo`, `CloseFigure`) and glyph arrays into contiguous flat vectors, computes the content bounding box (`content_bounds_pt` for `Auto-Detect White Margins` in `D05 T01 §4`), and seals the result into a `std::shared_ptr<const DisplayList>` via `finish()`.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that once `DisplayListBuilder::finish()` returns `std::shared_ptr<const DisplayList>`, the `DisplayList` exposes `const`-only accessors over contiguous immutable vectors and `std::shared_ptr<const DecodedBitmap>` buffers, with zero mutable members or lazy caches.

**Build order:**

1. Author `src/pdf/display_list.h` and `src/pdf/display_list.cpp` implementing `DisplayCommand`, `RecordedPath`, `RecordedGlyphRun`, `DecodedBitmap`, `DisplayList`, and `DisplayListBuilder` (with Debug atomic `live_display_lists` counter). Done when: `display_list.cpp` compiles cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/pdf_display_list_tests.cpp` verifying command recording, path/glyph contiguous storage, bounding-box computation, `live_display_lists` counter increment/decrement, and concurrent read access from 8 threads. Done when: `PdfDisplayList.*` passes under ASan.

- [ ] Define `DisplayCommand` and contiguous command/path/glyph storage in `DisplayList` so replaying a page walks flat arrays with minimal pointer chasing. Done when: `DisplayList` contains zero non-`const` methods or `mutable` fields.
- [ ] Implement `RecordedPath` storing `MoveTo`, `LineTo`, `CubicBezierTo`, `Rect`, and `CloseSubpath` verbs in a shared coordinate pool on `DisplayList`. Done when: `PdfDisplayList.PathRecording` records multi-subpath geometries and computes exact bounding boxes.
- [ ] Implement `DecodedBitmap` (`width`, `height`, `stride`, immutable `std::vector<uint8_t>` premultiplied BGRA pixels, `is_monochrome_mask`) wrapped in `std::shared_ptr<const DecodedBitmap>`. Done when: image bitmaps are shared zero-copy across commands and worker threads.
- [ ] Implement `DisplayListBuilder::finish()` sealing the builder and computing `page_box_pt` and `content_bounds_pt` (the union of all painted paths, glyph runs, and images clipped to the active clip stack). Done when: `PdfDisplayList.ContentBounds` returns the exact ink bounding box of a recorded page.
- [ ] Track active `DisplayList` instances via an atomic counter `recto::pdf::debug_live_display_lists()` so `§8` and `§10` can assert zero leaked display lists on shutdown. Done when: constructing and destroying `DisplayList` increments and decrements the counter to `0`.
- [ ] Verify concurrent lock-free reading of one `std::shared_ptr<const DisplayList>` across 8 threads in `PdfDisplayList.ConcurrentRead`. Done when: 8 threads iterating 10,000 commands simultaneously pass under ASan.
- [ ] Commit: `pdf: add immutable DisplayList command buffer and DisplayListBuilder`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfDisplayList.*` passes under ASan with 0 warnings.

## 2. DirectWrite in-memory font loader, Standard 14 metrics, Type 1/3 and `/ToUnicode` CMap

Implement font loading, glyph mapping, and Unicode CMap parsing in `src/pdf/font.h` and `src/pdf/font.cpp` (ISO 32000-1 section 9):
- Custom in-memory `IDWriteFontFileLoader` and `IDWriteFontFileStream` registering embedded `/FontFile2` (TrueType) and `/FontFile3` (OpenType / CFF) byte buffers directly with a process-wide thread-safe `IDWriteFactory` (`DWRITE_FACTORY_TYPE_SHARED`) to create thread-safe `IDWriteFontFace` instances without writing temp files to disk.
- Standard 14 PDF font table (`Times-Roman`/`Bold`/`Italic`/`BoldItalic`, `Helvetica` family, `Courier` family, `Symbol`, `ZapfDingbats`) with exact ISO 32000-1 AFM character widths and fallback mapping to Windows 11 system fonts (`Times New Roman`, `Arial`, `Courier New`, `Segoe UI Symbol`).
- Legacy `/Subtype /Type1` / bare `/FontFile` (`eexec` decrypted charstrings) and `/Subtype /Type3` (`/CharProcs` procedural glyph content streams) support, converting Type 1 charstrings to `RecordedPath` outlines and Type 3 charprocs into per-glyph sub-`DisplayList`s.
- Character code to glyph index and Unicode mapping: `/Encoding` (`WinAnsiEncoding`, `MacRomanEncoding`, `StandardEncoding`, `/Encoding << /BaseEncoding ... /Differences [...] >>`), TrueType `cmap` table (`(3,1)` Windows Unicode, `(1,0)` Mac, `(3,0)` Symbol), CIDFonts (`/Subtype /Type0` with `/DescendantFonts`, `/CIDToGIDMap`, `/W` and `/DW` widths), and `/ToUnicode` CMap parser (`begincodespacerange`, `beginbfchar`, `beginbfrange`).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `IDWriteFactory` is created with `DWRITE_FACTORY_TYPE_SHARED`, in-memory font byte buffers outlive their `IDWriteFontFace` via `std::shared_ptr<const std::vector<uint8_t>>`, and `LoadedFont` objects stored in a `DisplayList` are immutable and thread-safe for concurrent `ID2D1RenderTarget::DrawGlyphRun` calls across `RenderPool` workers.

**Build order:**

1. Implement `src/pdf/font.h` and `src/pdf/font.cpp` (in-memory `IDWriteFontFileLoader`, Standard 14 width/font resolver, `/Encoding` + `/Differences` tables, `/ToUnicode` CMap parser, CID `/W` width parser, Type 1 charstring path decoder, and Type 3 `/CharProcs` hook). Done when: `font.cpp` compiles cleanly on `x64` and `ARM64` linking `dwrite.lib`.
2. Add `tests/recto-tests/pdf_font_tests.cpp` testing Standard 14 metrics, in-memory TrueType/OpenType loading, `/Differences` encoding overrides, `/ToUnicode` `bfchar`/`bfrange` parsing (including UTF-16 surrogate pairs and multi-char ligature expansions), CID `/W` width arrays, and Type 1/Type 3 glyph outlines. Done when: `PdfFont.*` passes under ASan.

- [ ] Implement a custom `IDWriteFontFileLoader` + `IDWriteFontFileStream` backed by `std::shared_ptr<const std::vector<uint8_t>>` so embedded `/FontFile2` and `/FontFile3` streams load into `IDWriteFontFace` in memory with zero disk I/O. Done when: `PdfFont.InMemoryDirectWriteLoader` loads an embedded TrueType font buffer and resolves glyph indices and design-unit metrics. Cheaper substitute: GDI `AddFontMemResourceEx` or ignoring embedded fonts and rendering everything in Arial.
- [ ] Build the complete Standard 14 font metric and encoding table (`Helvetica`, `Times-Roman`, `Courier`, `Symbol`, `ZapfDingbats` and their bold/oblique variants) with exact 1000-unit glyph widths and Windows 11 system `IDWriteFontFace` fallback. Done when: `PdfFont.Standard14Metrics` matches ISO AFM widths for ASCII and Latin-1 characters.
- [ ] Parse `/Encoding` (`/WinAnsiEncoding`, `/MacRomanEncoding`, `/MacExpertEncoding`, `/StandardEncoding`) and `/Differences [code /glyphname ...]` arrays, mapping Adobe Glyph List (`AGL`) names (`/fi`, `/bullet`, `/emdash`, `/Euro`, `/uniXXXX`) to Unicode codepoints and glyph indices. Done when: `PdfFont.EncodingAndDifferences` resolves custom `/Differences` remappings accurately.
- [ ] Implement the `/ToUnicode` CMap stream parser (`begincodespacerange` 1-byte and 2-byte ranges, `beginbfchar`, `beginbfrange` with both `<hex>` increment and `[<hex1> <hex2> ...]` array forms, and multi-character ligature targets like `<00660069>` -> `"fi"`). Done when: `PdfFont.ToUnicodeCMap` decodes 1-byte, 2-byte CID, and ligature CMap streams.
- [ ] Parse `/Page` font `/Widths` (`FirstChar`..`LastChar`) and Type 0 CIDFont `/DW` and `/W [c [w1 w2 ...] c_first c_last w]` arrays plus `/CIDToGIDMap` (`/Identity` or binary stream). Done when: `PdfFont.CidFontWidths` computes exact horizontal advances for 2-byte Identity-H CID strings.
- [ ] Implement Type 1 (`eexec` binary/hex decrypt + Type 1/Type 2 charstring `rmoveto`/`rlineto`/`rrcurveto`/`seac` path builder) and `/Subtype /Type3` (`/FontBBox`, `/FontMatrix`, `/CharProcs` sub-display-list) fallback for fonts DirectWrite cannot instantiate as an `IDWriteFontFace`. Done when: `PdfFont.Type1AndType3` converts Type 1 and Type 3 glyphs into vector `DisplayList` paths.
- [ ] Commit: `pdf: add DirectWrite in-memory font loader, Standard 14 metrics, Type 1/3 and ToUnicode parser`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFont.*` passes under ASan with 0 warnings.

## 3. Content-stream graphics state machine, path construction/clipping and colour spaces

Implement the graphics-state, colour-space, and vector-path half of the content-stream interpreter in `src/pdf/interpreter.h` and `src/pdf/interpreter.cpp` (ISO 32000-1 section 8):
- Graphics state stack (`q` push capped at depth 64, `Q` pop ignoring underflow, `cm` CTM concatenation, `w` line width, `J` line cap, `j` line join, `M` miter limit, `d` dash array and phase, `ri` rendering intent, `i` flatness, `gs` `/ExtGState` dictionary applying `/LW`, `/LC`, `/LJ`, `/ML`, `/D`, `/CA` stroke alpha, `/ca` fill alpha, `/BM` blend mode, `/SMask`).
- Path construction (`m` moveto, `l` lineto, `c` cubic bezier, `v` initial-point cubic, `y` final-point cubic, `h` closepath, `re` rectangle) and path painting/clipping (`S`, `s`, `f`, `F`, `f*`, `B`, `B*`, `b`, `b*`, `n` no-op paint, `W` non-zero clip, `W*` even-odd clip applied when the current path finishes).
- Colour spaces and operators (`g`/`G` `DeviceGray`, `rg`/`RG` `DeviceRGB`, `k`/`K` `DeviceCMYK`, `cs`/`CS`/`sc`/`SC`/`scn`/`SCN` supporting `/DeviceGray`, `/DeviceRGB`, `/DeviceCMYK`, `/CalGray`, `/CalRGB`, `/Lab`, `/ICCBased` with `/Alternate` fallback, `/Indexed` palette lookup up to 256 entries, `/Separation` and `/DeviceN` tint transform functions `/FunctionType 0, 2, 3, 4`, and `/Pattern` tiling/shading fills `sh`).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `Interpreter` mutates only its stack-allocated `GraphicsState` stack and `DisplayListBuilder`, never mutating shared resource dictionaries in `pdf::Document`.

**Build order:**

1. Implement `src/pdf/interpreter.h` and the graphics-state, colour-space/function evaluator, and path construction/painting/clipping operators in `src/pdf/interpreter.cpp`. Done when: `interpreter.cpp` compiles cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/pdf_interpreter_tests.cpp` testing `q`/`Q`/`cm`, path operators (`m`/`l`/`c`/`v`/`y`/`h`/`re`), painting/clipping (`S`/`f`/`f*`/`B`/`W`/`W*`/`n`), `/ExtGState` alpha, and colour spaces (`DeviceGray`/`RGB`/`CMYK`, `/Indexed`, `/Separation` with Type 2 exponential tint function). Done when: `PdfInterpreter.PathsAndColors` passes under ASan.

- [ ] Implement the PDF graphics state stack (`q` / `Q` capped at 64 saved states, ignoring unbalanced extra `Q` operators in sloppy PDFs without crashing) and `cm` (`[a b c d e f]`) affine matrix concatenation. Done when: `PdfInterpreter.GraphicsStateStack` verifies `q`/`Q` restoration and caps deep `q` stacks at 64.
- [ ] Implement stroke and `/ExtGState` (`gs`) operators (`w`, `J`, `j`, `M`, `d [array] phase`, `/CA` stroke opacity, `/ca` fill opacity, `/BM` blend mode). Done when: `PdfInterpreter.ExtGState` records stroke styles and alpha values into `DisplayList`.
- [ ] Implement all path construction operators (`m`, `l`, `c`, `v`, `y`, `h`, `re`) converting `v` and `y` shorthand Beziers into full cubic Beziers `(cp1, cp2, end)`. Done when: `PdfInterpreter.PathOperators` records exact control points for `m`/`l`/`c`/`v`/`y`/`h`/`re`.
- [ ] Implement all path painting and clipping combinations (`S`, `s`, `f`, `F`, `f*`, `B`, `B*`, `b`, `b*`, `n`, `W`, `W*`) applying pending `W`/`W*` clip paths before clearing the current path. Done when: `W n` pushes a clip path without stroking or filling it in `PdfInterpreter.ClippingPaths`.
- [ ] Implement colour operators (`g`/`G`, `rg`/`RG`, `k`/`K`, `cs`/`CS`, `sc`/`SC`, `scn`/`SCN`) and colour-space resolvers for `DeviceGray`, `DeviceRGB`, `DeviceCMYK` (calibrated CMYK-to-sRGB conversion), `CalGray`, `CalRGB`, `Lab`, and `ICCBased`. Done when: `PdfInterpreter.ColorSpaces` converts grayscale, RGB, CMYK, and Lab colours to expected sRGB values.
- [ ] Implement `/Indexed` (`[/Indexed base hival lookup]`) palette lookup and `/Separation` / `/DeviceN` tint transform evaluation (`/FunctionType 0` sampled, `2` exponential interpolation, `3` stitching, and bounded `4` PostScript calculator subset). Done when: `PdfInterpreter.IndexedAndSeparation` resolves indexed palette indices and spot-colour tints.
- [ ] Support axial (`/ShadingType 2`) and radial (`/ShadingType 3`) gradient shadings (`sh` operator and `/PatternType 2` shading patterns) plus `/PatternType 1` tiling patterns (capped at recursion depth 8). Done when: `PdfInterpreter.ShadingAndPatterns` records gradient/pattern fills into `DisplayList`.
- [ ] Commit: `pdf: implement content-stream state machine, path operators and colour spaces`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.Graphics*:PdfInterpreter.Path*:PdfInterpreter.Clip*:PdfInterpreter.Color*:PdfInterpreter.Indexed*:PdfInterpreter.Shading*` passes under ASan.

## 4. Content-stream text positioning, `/Subtype /Image`, inline images and `/Form` XObjects

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
- [ ] Implement inline image parsing (`BI ... ID <data> EI`) expanding abbreviated dictionary keys (`W`, `H`, `BPC`, `CS`, `F`, `DP`, `IM`, `I`) and locating the `EI` delimiter safely via expected decoded row size + filter length. Done when: `PdfInterpreter.InlineImages` decodes `A85`, `Fl`, and raw inline images without misidentifying `EI` bytes inside binary pixel data.
- [ ] Implement `/Do` for `/Subtype /Form` XObjects (pushing graphics state, concatenating `/Matrix`, clipping to `/BBox`, binding `/Resources` with parent fallback, and enforcing the **32-level recursion ceiling**). Done when: `PdfInterpreter.FormXObjectCycle` stops a self-referencing `F1 -> F1` Form XObject at depth 32 and returns cleanly under ASan. Cheaper substitute: unbounded recursive `interpret()` calls that overflow the stack on cyclic Form XObjects.
- [ ] Record optional-content marked-content blocks (`BDC` / `BMC` ... `EMC` with `/OC` property references) so `D02 T03 §7` can toggle OCG layer visibility without re-parsing the PDF file. Done when: hidden OCG groups are skipped when an OCG visibility predicate marks them off.
- [ ] Interpret every page of `tests/corpus/render/` (`simple-1p.pdf`, `compressed-xref-2p.pdf`, `incremental-2rev.pdf`, `rotated-pages.pdf`, `images-and-vectors.pdf`, and unlocked `encrypted-*.pdf`) into `DisplayList`s with zero errors under ASan. Done when: `PdfInterpreter.CorpusRenderSweep` passes 100%.
- [ ] Commit: `pdf: add content-stream text operators, image XObjects, inline images and Form XObjects`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfInterpreter.*` passes under ASan across all corpus render and broken files.

## 5. Incremental PDF revision writer (`src/pdf/incremental_writer.cpp`)

Implement `src/pdf/incremental_writer.h` and `src/pdf/incremental_writer.cpp` (`recto::pdf::IncrementalWriter`, foundation plan section 4.1 item 6):
- Append a non-destructive PDF revision to the end of an existing PDF byte buffer (preserving the original bytes `0..original_size` untouched so existing cryptographic digital signatures `/ByteRange` remain valid when adding a counter-signature or filling allowed form/annotation fields).
- Allocate new object numbers (`>= prev_size`) and write modified indirect objects (`N G obj ... endobj` with `FlateDecode` compression for modified streams and CNG encryption matching the document's active `SecurityHandler` when encrypted).
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
- [ ] Encrypt modified strings and streams in the appended revision using the document's `SecurityHandler` (`RC4`, `AES-128`, or `AES-256` `R=6`) when the base document is encrypted. Done when: `PdfIncrementalWriter.EncryptedDocument` incrementally updates `render/encrypted-aes-256.pdf` and re-opens it with `"recto-test"`.
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

## Verification

- `recto-tests.exe --gtest_filter=PdfDisplayList.*:PdfFont.*:PdfInterpreter.*:PdfIncrementalWriter.*:PdfRasterizer.*:Actor.*:Scheduler.*:Runtime.*` passes in Debug, Release, and ASan builds.
- Debug live-object counters (`debug_live_documents()`, `debug_live_display_lists()`, `debug_live_workers()`) end at `0` after every test.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
