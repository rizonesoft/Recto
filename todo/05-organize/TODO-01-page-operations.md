---
schema_version: 1
id: page-operations
domain: 05-organize
status: active
title: "TODO-01 -- Page Operations, Stamping, Digital Signatures and True Redaction"
track: M4
---

# TODO-01 -- Page Operations, Stamping, Digital Signatures and True Redaction

> **Goal:** The engine-side page surgery, stamping, cryptographic signing, and true redaction behind Organize, the Viewer security bars, and the Convert cards: graft-based merge and split, reorder, rotate, delete, insert blank page, crop/resize page boxes, watermarks, headers/footers, Bates numbering, CNG `AES-256` (`R=6`) encrypt and decrypt, X.509 / PKCS#12 / Windows Certificate Store / PAdES / RFC 3161 digital signatures and verification (`src/pdf/signature.cpp`), and true physical content-stream redaction and document sanitization (`src/pdf/redactor.cpp`), all as `recto-pdf` and `recto-core` operations that never modify an open viewer document in place.

> **Current state:** Measured 2026-10-14. No code exists. The mechanisms are plan section 5's rows: `recto::pdf::PdfWriter` with one `recto::pdf::GraftMap` per source for merge and split, page-tree functions on a copy for reorder, rotate, delete, blank-page insertion and box cropping, `src/core/stamp_watermark.cpp` for watermarks, headers/footers and Bates numbering, `src/pdf/crypto.cpp` for `AES-256` (`R=6`) encryption, `src/pdf/signature.cpp` for PKCS#7 / PAdES digital signatures, and `src/pdf/redactor.cpp` for physical redaction and sanitization, all saved through `AtomicFileWriter` (`D04 T02 §1`).

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` decisions D9 and D12; sections 4.1 (`graft.cpp`, `crypto.cpp`, `signature.cpp`, `redactor.cpp`), 4.9 (job isolation), 5 (Merge, Split, Rotate/delete/reorder/crop, Watermarks/headers/footers/Bates, Encrypt/decrypt, Digital Signatures, True Redaction & Sanitization), and 9 (security rules).
- `design/SPEC.md` sections 8.2 (`Redaction & Sanitize Bar`, `Signature Status Banner & Sign Mode`), 8.3 (`Signatures & Standards` side panel), 9.5 (`Protected & Signed PDF` and `Unlocked PDF` cards), 9.6 (`Organize` tools), and 9.9 (`Watermark`, `Header/Footer & Bates`, `Crop & Resize`, `Sanitize Document`, and `Sign Document` dialogs).
- -> XREF: D01 T01 §9 -- the `AES-256` (`V=5, R=6`) encryption writer in §7 extends `src/pdf/crypto.cpp` built in `D01 T01 §9`.
- -> XREF: D01 T02 §5 -- `SignatureEngine` in §8 uses `D01 T02 §5`'s `IncrementalWriter` so existing signatures stay valid when counter-signing.
- -> XREF: D02 T03 §4 -- the viewer thumbnail context-menu page actions (`Insert Blank Page`, `Crop`, `Extract`, `Delete`) invoke §3 and §4 here.
- -> XREF: D02 T03 §9 -- the Info panel's security and permissions summary reads back `AES-256` and permission flags written by §7 here.
- -> XREF: D02 T04 §6 -- the `Verify signatures when opening` and `Default timestamp server URL` settings in `D02 T04 §6` are consumed by §8 and §9 here.
- -> XREF: D03 T01 §1 -- `Redactor` in §10 uses `PageText` character quads from `D03 T01 §1` to split and strip intersecting `Tj`/`TJ` runs.
- -> XREF: D04 T02 §1 -- `AtomicFileWriter` (`D04 T02 §1`) and `PdfWriter` (`D04 T02 §2`) are the shared writer plumbing this file consumes.
- -> XREF: D05 T02 §1 -- the Organize grid, dialogs, and viewer security bars in `D05 T02` compile down to this file's operation list.
- -> XREF: D06 T02 §2 -- the security audit in `D06 T02 §2` verifies physical glyph/pixel removal after redaction and metadata/hidden-data eradication after sanitization.
- -> XREF: D07 T01 §2 -- the print pipeline (`D07 T01 §2`) reuses the shared `parse_page_ranges` parser built in §1 here.
- -> XREF: D06 T03 §6 -- the raw-byte redaction completeness and audit suite gating §10's redactor, and the Unicode authoring suite (§7 there) over §5, §6 and §8.

## Outcome

- `GraftMap` (`§1`) and multi-document `merge` / `split` (`§2`) combine or partition PDFs with byte-level page content preserved by object grafting (no re-render) and shared resource deduplication per source.
- Copy-based page-tree edit lists (`reorder`, `/Rotate` delta, `delete`, `extract` in `§3`), blank-page insertion and `/MediaBox`/`/CropBox`/`/TrimBox`/`/BleedBox`/`/ArtBox` cropping + page rescaling (`§4`) round-trip through render comparison.
- Vector and image `Watermarks & Stamps` (`§5`) and 3-zone `Headers, Footers & Legal Bates Numbering` (`§6`) inject reusable Form XObjects with shrink-to-avoid-overlap protection.
- Encryption (`§7`) produces `AES-256` (`V=5, R=6`) files with the stated permissions that other readers honor; decryption with the right password produces a clean unprotected copy, and the wrong password is a typed refusal.
- `SignatureEngine` (`src/pdf/signature.cpp`, `§8`, `§9`) signs and verifies PDFs using PKCS#12 (`.pfx`/`.p12`) and the Windows Certificate Store (`crypt32.dll` / `bcrypt.dll`) with PKCS#7 detached and PAdES (`B-B`, `B-T`, `B-LT`), `/DSS` revocation data, RFC 3161 timestamps, `/DocMDP` certification, and incremental-save multi-signature support.
- `Redactor` (`src/pdf/redactor.cpp`, `§10`) permanently removes text runs, vector paths, and image pixels inside redaction rectangles and sanitizes hidden metadata, scripts, attachments, and layers.

**Adjacency:** all=not-applicable (engine operations inside the `D04 T01` job frame; cards, queue, notices and save settings are owned there and by `D02 T04 §5`)

**Adjacency rationale:** This file is the verb library the Convert cards, Organize UI, and Viewer security bars (`D05 T02`) call. Its surfaces live in `D04 T01` and `D05 T02`.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `GraftMap` object-graph cloner, resource deduplicator (`graft.cpp`) & page-range parser | D04 T02 §2 | [ ] |
| 2 | §2 | Multi-document graft `merge` and multi-method `split` (`recto-cli merge` & `split`) | §1 | [ ] |
| 3 | §3 | Copy-based page-tree edit list (`reorder`, `/Rotate` delta, `delete`, `extract`) | §2 | [ ] |
| 4 | §4 | `Insert Blank Page`, box cropping (`/MediaBox`..`/ArtBox`), margin detection & rescaling | §3 | [ ] |
| 5 | §5 | Vector and image `Watermark & Stamp` engine (`stamp_watermark.cpp`, `cli watermark`) | §3 | [ ] |
| 6 | §6 | 3-zone `Headers & Footers` and legal **Bates Numbering** (`stamp_watermark.cpp` & CLI) | §5 | [ ] |
| 7 | §7 | Windows CNG (`bcrypt.dll`) `AES-256` (`V=5, R=6`) encrypt/decrypt writer and CLI verbs | §1, D01 T01 §9 | [ ] |
| 8 | §8 | X.509 / PKCS#12 and Windows Certificate Store `PKCS#7` & `PAdES` signer (`cli sign`) | §7, D01 T02 §5 | [ ] |
| 9 | §9 | Cryptographic signature verifier (`/ByteRange`, CMS/CAdES, chain trust & tamper check) | §8 | [ ] |
| 10 | §10 | True physical content-stream redactor (glyph/path/image scrubbing) & `sanitize_document` | §3, D03 T01 §1 | [ ] |

## 1. `GraftMap` object-graph cloner, resource deduplicator (`graft.cpp`) & page-range parser

Implement `src/pdf/graft.h` and `src/pdf/graft.cpp` (`recto::pdf::GraftMap`) and the shared page-range specification parser in `src/core/page_ops.h/.cpp`:
- **`GraftMap`:** Clones a `/Page` dictionary and its reachable indirect object subgraph (`/Contents`, `/Resources` fonts, colour spaces, ICC profiles, `/XObject` images/forms, `/ExtGState`, `/Shading`, `/Pattern`, and `/Annots` excluding broken cross-page destination links) from a source `pdf::Document` into a target `PdfWriter` (`D04 T02 §2`), mapping each source `(obj_num, gen_num)` to a single allocated target `obj_num` so shared fonts and images across pages of the same source are written once and cycle-safe against circular indirect references.
- **Shared Page-Range Parser (`parse_page_range_spec`):** Parses 1-based user specifications (`"1-5, 8, 11-13"`, `"odd"`, `"even"`, `"1-end"`) against `total_pages`, refusing inverted ranges (`"5-2"`), zero (`"0"`), or beyond-end page numbers (`"99"` on a 10-page file) with a descriptive `PdfError` before any output file is created.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `GraftMap` in `src/pdf/graft.h/.cpp` with cycle-safe recursive object-graph cloning (visited map `(src_obj, src_gen) -> dst_obj` allocated before descending into children, plus inherited `/Pages` attribute materialization for `/Resources`, `/MediaBox`, `/CropBox`, `/Rotate`). Done when: `PageOps.GraftSingleAndSharedResources` grafts 10 pages sharing a single embedded font and image XObject into `PdfWriter` and verifies the font and image objects are emitted exactly once in the output PDF under ASan.
2. Implement `parse_page_range_spec(std::string_view spec, uint32_t total_pages)` in `src/core/page_ops.h/.cpp`. Done when: `PageOps.RangeParser` validates valid specs and refuses `"3-2"`, `"0"`, `"-5"`, and beyond-end ranges with exact fault descriptions.

- [ ] Implement `GraftMap` in `src/pdf/graft.h/.cpp` maintaining `std::unordered_map<uint64_t, uint32_t> mapped_objects_` per source `pdf::Document` and pre-allocating the destination object ID via `PdfWriter::alloc_object_id()` before recursing into dictionary/array/stream children so circular indirect object graphs never loop infinitely. Done when: `PageOps.GraftCyclicGraph` grafts a page containing a self-referential dictionary cycle without infinite recursion under ASan.
- [ ] Materialize inherited `/Pages` parent attributes (`/Resources`, `/MediaBox`, `/CropBox`, `/Rotate`) onto each grafted `/Page` dictionary and rewrite `/Parent` to the target `PdfWriter`'s `/Pages` tree node while stripping source-specific `/StructParents` when not grafting the entire structure tree. Done when: grafted pages retain their exact dimensions, rotation, and resources regardless of the source `/Pages` hierarchy depth.
- [ ] Preserve compressed stream payloads (`FlateDecode`, `DCTDecode`, `JPXDecode`, `CCITTFaxDecode`, `JBIG2Decode`) as raw compressed bytes during `GraftMap::graft_object` whenever the source document is unencrypted (or decrypt and `FlateDecode`-recompress when grafting from an unlocked encrypted source) so grafting never re-rasterizes vector content or re-encodes JPEGs. Done when: `PageOps.GraftPreservesStreams` confirms grafted page content and JPEG streams are preserved without re-rendering. Cheaper substitute: rendering pages to bitmaps and reassembling, which destroys selectable text and vector graphics.
- [ ] Sanitize intra-document `/Link` `/Dest` or `/GoTo` actions on grafted pages: if the target destination page is also grafted into the destination document, remap the page reference to the new target `/Page` object ID; if the target page was omitted (e.g. during split/extract), convert the broken internal link into a no-op so the output PDF never contains dangling indirect page references. Done when: `PageOps.GraftLinkRemap` verifies internal links work when both pages are kept and never dangle when split apart.
- [ ] Implement `parse_page_range_spec(std::string_view spec, uint32_t total_pages)` in `src/core/page_ops.cpp` supporting comma-separated 1-based numbers and ranges (`"1, 3-5, 10"`), `"all"`, `"odd"`, `"even"`, and `"N-end"`, returning `PdfErrorCode::InvalidArgument` naming the exact token on `"3-2"`, `"0"`, `"abc"`, or `page > total_pages`. Done when: `PageOps.RangeParser` passes all valid and refusal cases.
- [ ] Add `PageOps.Graft*` and `PageOps.RangeParser` tests in `tests/recto-tests/page_ops_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PageOps.Graft*:PageOps.RangeParser` passes under ASan.

- [ ] Commit: `GraftMap` object-graph deep cloner and per-source resource deduplicator (`src/pdf/graft.cpp`) plus shared page-range parser

**Test checkpoint:** `recto-tests.exe --gtest_filter=PageOps.Graft*:PageOps.RangeParser` passes under ASan.

## 2. Multi-document graft `merge` and multi-method `split` (`recto-cli merge` & `split`)

Implement `merge_pdfs` and `split_pdf` in `src/core/page_ops.h/.cpp` and their CLI commands `recto-cli merge` and `recto-cli split`:
- **Merge (`merge_pdfs`):** Takes an ordered list of `(source_path, password, page_selection)` inputs, creates one `GraftMap` per distinct source `pdf::Document`, grafts selected pages in the stated order into `PdfWriter`, builds top-level `/Outlines` bookmark entries naming each source file (wrapping each source's existing bookmarks underneath), and commits via `AtomicFileWriter` (`D04 T02 §1`).
- **Split (`split_pdf`):** Splits a source PDF into multiple output PDFs by explicit range list (`["1-3", "4-7", "8-10"]`), every `N` pages, or one file per page, committing each output file atomically.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `merge_pdfs` and `split_pdf` (`SplitByRanges`, `SplitEveryNPages`, `SplitSinglePages`) in `src/core/page_ops.cpp` over `GraftMap` (`§1`) and `AtomicFileWriter` (`D04 T02 §1`). Done when: merging three corpus PDFs and splitting the merged PDF back by the original page counts produces pages that pixel-match (`expect_rgba_equal`) the original sources at `72` and `150` DPI under ASan.
2. Implement `recto-cli merge <in1.pdf> <in2.pdf> [...] --out <merged.pdf>` and `recto-cli split <in.pdf> --out-dir <dir> [--ranges <spec> | --every <N> | --single-pages]` in `src/cli/cmd_page_ops.cpp`. Done when: `Cli.MergeAndSplit` passes under ASan.

- [ ] Define `MergeSourceSpec` (`path`, `password`, `page_indices_0based`), `SplitMethod` (`ByRanges`, `EveryNPages`, `OneFilePerPage`), and `SplitPartSpec` (`page_indices_0based`, `output_path`) in `src/core/page_ops.h`. Done when: `src/core/page_ops.h` compiles cleanly across `recto-core`, `recto-services`, and `recto-cli`.
- [ ] Implement `merge_pdfs(span<const MergeSourceSpec>, dst_path, CancelToken)` in `src/core/page_ops.cpp` maintaining one `GraftMap` per source document so interleaving pages from Source A and Source B (`A1, B1, A2`) still deduplicates Source A's shared resources across `A1` and `A2`. Done when: `PageOps.MergeInterleavedSources` verifies resource deduplication and pixel-exact rendering (`expect_rgba_equal`) of all merged pages.
- [ ] Build merged `/Outlines` bookmarks during `merge_pdfs`: create a top-level bookmark per source document pointing to that source's first grafted page, with the source's own `/Outlines` subtree remapped underneath where destination pages were included. Done when: `PageOps.MergeOutlines` verifies top-level source bookmarks in the merged output.
- [ ] Surface `PdfErrorCode::PasswordRequired` / `InvalidPassword` with the exact offending source file path when any input in `merge_pdfs` or `split_pdf` is encrypted without a valid password, so `D04 T01 §2` / `D05 T02 §3` can prompt for that file's password. Done when: `PageOps.MergeEncryptedRefusal` returns `PasswordRequired` naming the encrypted source and writes zero partial output files.
- [ ] Implement `split_pdf(src_path, password, span<const SplitPartSpec>, CancelToken)` validating all part page indices up front and writing each part via its own `PdfWriter` + `GraftMap` + `AtomicFileWriter`, cleaning up all already-written parts if `CancelToken` is tripped mid-batch. Done when: `PageOps.SplitRoundTripAndCancel` verifies pixel-exact merge->split round-tripping and clean multi-part rollback on cancel.
- [ ] Implement `recto-cli merge` and `recto-cli split` in `src/cli/cmd_page_ops.cpp`. Done when: `recto-tests.exe --gtest_filter=PageOps.Merge*:PageOps.Split*:Cli.MergeAndSplit` passes under ASan.
- [ ] Commit: `organize: graft-based merge and split with shared range parsing`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PageOps.Merge*:PageOps.Split*:Cli.MergeAndSplit` passes under ASan: merge-split round-trip pixel equality and range refusals.

## 3. Copy-based page-tree edit list (`reorder`, `/Rotate` delta, `delete`, `extract`)

Implement the Organize edit vocabulary in `src/core/page_ops.h/.cpp` (`apply_page_edits` and `extract_pages`): apply a declarative `PageEditPlan` (an ordered list of kept source page indices with per-page `/Rotate` deltas `0`, `90`, `180`, `270` and blank-page insertion slots, with deleted pages omitted) to a fresh copy of the source document via `GraftMap` (`§1`) and `PdfWriter` (`D04 T02 §2`), leaving the source file and any open viewer `DocumentActor` instance untouched until `AtomicFileWriter::commit()` (`D04 T02 §1`).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `PageEditEntry` (`source_page_index` or `BlankPageSpec`, `rotation_delta_deg` `0|90|180|270`) and implement `apply_page_edits(src_path, password, span<const PageEditEntry>, dst_path, CancelToken)` and `extract_pages(src_path, password, span<const uint32_t>, dst_path, CancelToken)` in `src/core/page_ops.cpp`. Done when: a scripted edit (move page 3 to front, rotate page 2 by `90°`, delete page 5) renders page-for-page identically to the reference renders and reports rotated page dimensions through `recto-cli info` under ASan.
2. Add `recto-cli pages <reorder|rotate|delete|extract>` in `src/cli/cmd_page_ops.cpp`. Done when: `Cli.PagesEditCommands` passes under ASan.

- [ ] Define `PageEditEntry` (`is_blank_page`, `source_page_index`, `rotation_delta_deg`, `blank_width_pt`, `blank_height_pt`) and `PageEditPlan` in `src/core/page_ops.h`. Done when: an identity `PageEditPlan` (`[0, 1, ..., N-1]` with `rotation_delta_deg = 0`) reproduces the source document's renders pixel-for-pixel (`expect_rgba_equal`).
- [ ] Implement `apply_page_edits` in `src/core/page_ops.cpp` opening its own isolated `pdf::Document` instance (never mutating a viewer `DocumentActor`'s document in place per `foundation plan section 5`), grafting kept pages in the new permutation order, and writing `new_rotate = (existing_rotate + rotation_delta_deg) % 360` to the grafted `/Page /Rotate` integer entry. Done when: `PageOps.ScriptedReorderRotateDelete` moves page 3 to index 0, rotates page 2 by `90°`, and deletes page 5, matching expected page count and rendered pixels. Cheaper substitute: mutating the open viewer document in place or rasterizing pages to rotate them.
- [ ] Verify that `/Rotate` updates persist as metadata on `/Page` (`0`, `90`, `180`, `270`) so `recto-cli info --json` reports the updated rotation and effective swapped display width/height without altering the underlying content stream coordinates. Done when: `PageOps.RotateMetadataReadback` confirms `rotation == 90` after a `+90°` edit and `rotation == 0` after four `+90°` edits.
- [ ] Refuse `apply_page_edits` with `PdfErrorCode::InvalidArgument` if the resulting document would have `0` pages (deleting all pages in a PDF is invalid per ISO 32000-1). Done when: attempting to delete every page returns a clean refusal before touching disk.
- [ ] Implement `extract_pages` as a single-output graft of the selected page indices (preserving per-page rotation deltas when supplied from Organize). Done when: `PageOps.ExtractPages` equals splitting to that exact range (`expect_rgba_equal`).
- [ ] Support `AtomicFileWriter` same-path replacement (`dst_path == src_path`) so when the user chooses `Save` (replace original in `D05 T02 §5`), the new file is written completely to a sibling `.tmp` file while reading from `src_path`, then atomically swapped once `src_path` is closed. Done when: `PageOps.ReplaceOriginalAtomic` proves `apply_page_edits` with `dst_path == src_path` succeeds atomically and leaves `src_path` byte-identical on mid-write cancel.
- [ ] Implement `recto-cli pages <reorder|rotate|delete|extract>` in `src/cli/cmd_page_ops.cpp`. Done when: `recto-tests.exe --gtest_filter=PageOps.Scripted*:PageOps.Rotate*:PageOps.Extract*:PageOps.Replace*:Cli.PagesEditCommands` passes under ASan.
- [ ] Commit: `organize: reorder, rotate, delete and extract as copy operations`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PageOps.*:Cli.PagesEditCommands` passes under ASan with the scripted-edit render comparisons quoted.

## 4. `Insert Blank Page`, box cropping (`/MediaBox`..`/ArtBox`), margin detection & rescaling

Extend `src/core/page_ops.h/.cpp` with:
1. **Insert Blank Page (`insert_blank_pages`):** Emits clean empty `/Page` dictionaries at any position in `PageEditPlan` with configurable paper dimensions (`Match adjacent page`, `A4` `595.28 x 841.89 pt`, `Letter` `612 x 792 pt`, `Legal` `612 x 1008 pt`, or custom `width_pt x height_pt`) and orientation (`Portrait` / `Landscape`).
2. **Crop & Resize Page Boxes (`crop_and_resize_pages` and `detect_white_margins`):** Updates `/CropBox`, `/MediaBox`, `/TrimBox`, `/BleedBox`, or `/ArtBox` across selected/all/even/odd pages, computes automatic tight content margins (`detect_white_margins` inspecting `DisplayList` path/text/image bounding boxes plus a configurable padding inset), and optionally **rescales page content** (`q sx 0 0 sy tx ty cm ... Q` + annotation `/Rect` and `/QuadPoints` affine scaling) to fit a new target paper size (`A4`, `Letter`, `Legal`, custom).
3. Expose `recto-cli crop <in.pdf> --out <out.pdf> [--margins <t,r,b,l> | --auto-margins [--padding <pt>]] [--box crop|media|trim|bleed|art] [--resize a4|letter|legal|<WxH>] [--pages <range>]`.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement blank-page synthesis in `apply_page_edits`, `detect_white_margins(const DisplayList&, PageBox)`, and `crop_and_resize_pages` in `src/core/page_ops.cpp`. Done when: `PageOps.InsertBlankPage`, `PageOps.AutoDetectMargins`, and `PageOps.CropAndRescale` pass under ASan.
2. Implement `recto-cli crop` in `src/cli/cmd_crop.cpp`. Done when: `Cli.CropCommand` crops and rescales corpus pages under ASan.

- [ ] Define `PageBoxKind` (`CropBox`, `MediaBox`, `TrimBox`, `BleedBox`, `ArtBox`), `PageScopeFilter` (`AllPages`, `EvenPagesOnly`, `OddPagesOnly`, `ExplicitPages`), and `CropResizeOptions` (`box_kind`, `margin_top_pt`, `margin_right_pt`, `margin_bottom_pt`, `margin_left_pt`, `auto_detect_white_margins`, `auto_margin_padding_pt`, `rescale_to_target_size`, `target_width_pt`, `target_height_pt`, `scope_filter`, `page_indices`) in `src/core/page_ops.h`. Done when: `src/core/page_ops.h` compiles cleanly.
- [ ] Emit blank `/Page` objects in `apply_page_edits` (`§3`) with `/Type /Page`, `/MediaBox [0 0 W H]`, empty `/Resources << >>`, and empty `/Contents` stream when a `PageEditEntry` has `is_blank_page == true`. Done when: `PageOps.InsertBlankPage` inserts an A4 portrait and a Letter landscape blank page into a corpus document and verifies page count and `/MediaBox` dimensions.
- [ ] Implement `detect_white_margins(const DisplayList& dl, RectF media_box, float padding_pt = 12.0f)` in `src/core/page_ops.cpp` computing the union bounding box of all non-white-background drawn paths, glyph runs, and images on the page (ignoring full-page white background fill rectangles) and clamping `[min_x - pad, min_y - pad, max_x + pad, max_y + pad]` to `media_box`. Done when: `PageOps.AutoDetectMargins` detects the exact content bounding box on a wide-margin corpus page.
- [ ] Implement page-box cropping in `crop_and_resize_pages`: update the requested `PageBoxKind` (`/CropBox`, `/MediaBox`, `/TrimBox`, `/BleedBox`, `/ArtBox`) and clamp `/CropBox` inside `/MediaBox` per ISO 32000-1 section 14.11.2, respecting `AllPages`, `EvenPagesOnly`, `OddPagesOnly`, or `ExplicitPages` filters. Done when: `PageOps.CropBoxes` verifies updated box arrays and rendered crop dimensions.
- [ ] Implement proportional page-content rescaling when `rescale_to_target_size == true`: compute uniform scale `s = std::min(target_w / src_w, target_h / src_h)` and centering offset `(tx, ty)`, wrap the page's `/Contents` stream in `q <s> 0 0 <s> <tx> <ty> cm ... Q`, set `/MediaBox` and `/CropBox` to `[0 0 target_w target_h]`, and transform `/Annots` `/Rect` and `/QuadPoints` by the same matrix. Done when: `PageOps.RescaleA4ToLetter` rescales an A4 page with annotations to Letter (`612 x 792 pt`) while keeping text, graphics, and annotation boxes aligned. Cheaper substitute: changing `/MediaBox` without scaling `/Contents` or `/Annots`, which clips or misaligns the page.
- [ ] Implement `recto-cli crop` in `src/cli/cmd_crop.cpp`. Done when: `recto-tests.exe --gtest_filter=PageOps.InsertBlank*:PageOps.AutoDetect*:PageOps.Crop*:PageOps.Rescale*:Cli.CropCommand` passes under ASan.

- [ ] Commit: `Insert Blank Page` and `/MediaBox`/`/CropBox`/`/TrimBox`/`/BleedBox`/`/ArtBox` cropping (`Auto-Detect White Margins` + page rescaling, `recto-cli crop`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=PageOps.InsertBlank*:PageOps.AutoDetect*:PageOps.Crop*:PageOps.Rescale*:Cli.CropCommand` passes under ASan.

## 5. Vector and image `Watermark & Stamp` engine (`stamp_watermark.cpp`, `cli watermark`)

Implement the vector and image watermark engine in `src/core/stamp_watermark.h` and `src/core/stamp_watermark.cpp`, plus `recto-cli watermark <in.pdf> --out <out.pdf>`: inject reusable `/Type /XObject /Subtype /Form` watermark streams either **behind** (prepended before existing `/Contents` wrapped in `q ... Q`) or **in front of** (appended after existing `/Contents`) page content with `/ExtGState` opacity (`/ca`, `/CA`), rotation angle (`-90°..+90°`), 9-point anchor grid (`TopLeft..BottomRight`, `Center`) or repeating `Tile across page` pattern, page-range and even/odd filters, supporting both formatted multi-line text watermarks and image / vector PDF logo watermarks.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `apply_watermark(src_path, password, WatermarkOptions, dst_path, CancelToken)` and `build_watermark_preview_display_list(page_dl, WatermarkOptions)` (for `D05 T02 §7`'s live dialog preview) in `src/core/stamp_watermark.cpp`, plus `recto-cli watermark`. Done when: `StampWatermark.TextAndImageWatermarks` stamps diagonal translucent text, a 9-point anchored PNG logo, and a tiled watermark across a multi-page PDF (sharing a single Form XObject resource across all stamped pages) under ASan.

- [ ] Define `WatermarkKind` (`Text`, `Image`, `PdfPageLogo`), `WatermarkLayer` (`BehindContent`, `InFrontOfContent`), `AnchorPosition` (`TopLeft`, `TopCenter`, `TopRight`, `MiddleLeft`, `Center`, `MiddleRight`, `BottomLeft`, `BottomCenter`, `BottomRight`, `TiledGrid`), and `WatermarkOptions` (`kind`, `text`, `font_family`, `font_size_pt`, `color_rgba`, `image_or_pdf_path`, `scale_percent`, `rotation_deg`, `opacity`, `layer`, `anchor`, `tile_spacing_x_pt`, `tile_spacing_y_pt`, `scope_filter`, `page_indices`) in `src/core/stamp_watermark.h`. Done when: `src/core/stamp_watermark.h` compiles cleanly across `recto-core`, `recto-services`, and `recto-cli`.
- [ ] Synthesize a single reusable `/Subtype /Form` XObject (`WatermarkFm`) and `/ExtGState` (`<< /Type /ExtGState /CA <alpha> /ca <alpha> /BM /Normal >>`) per document in `src/core/stamp_watermark.cpp` so stamping a 500-page PDF writes the watermark font/image stream only once in the output file. Done when: `StampWatermark.SharedFormXObject` confirms a 20-page watermarked PDF allocates only 1 `/Subtype /Form` watermark stream.
- [ ] Support **Behind page content** (`BehindContent`) by wrapping the existing page `/Contents` stream in `q /WatermarkFm Do Q q <original_contents> Q` (and making opaque full-page white background rects non-occluding or placing the watermark immediately after the page background rect), and **In front of page content** (`InFrontOfContent`) by appending `q <original_contents> Q q /WatermarkFm Do Q`. Done when: `StampWatermark.BehindVsInFront` verifies pixel layering order in both modes.
- [ ] Implement 9-point anchor positioning (`TopLeft` through `BottomRight` with margin insets), arbitrary rotation (`-90°..+90°` around the watermark center via `[cos sin -sin cos tx ty] cm`), and `TiledGrid` repeating placement across the page `/CropBox`. Done when: `StampWatermark.RotationAndTiling` renders diagonal `45°` centered and tiled watermarks accurately. Cheaper substitute: supporting only centered horizontal text with no rotation, opacity, or image/tiling support.
- [ ] Support image watermarks (`.png` with `/SMask` alpha, `.jpg` via `DCTDecode`) and vector PDF page watermarks (grafting page 0 of a logo `.pdf` as a Form XObject via `GraftMap` from `§1`). Done when: `StampWatermark.ImageAndVectorPdfWatermark` stamps both a translucent PNG logo and a vector PDF emblem.
- [ ] Expose `build_watermark_preview_display_list` so `D05 T02 §7`'s dialog can composite the live watermark preview over any page's `DisplayList` in `< 5 ms` without writing a temp file to disk on every slider tick. Done when: `StampWatermark.LivePreviewDisplayList` renders identical pixels to the saved watermarked page.
- [ ] Implement `recto-cli watermark <in.pdf> --out <out.pdf> [--text <str> | --image <path>] [--opacity <0..1>] [--rotate <deg>] [--layer behind|front] [--tile] [--pages <range>]` in `src/cli/cmd_watermark.cpp`. Done when: `recto-tests.exe --gtest_filter=StampWatermark.Text*:StampWatermark.Shared*:StampWatermark.Behind*:StampWatermark.Rotation*:StampWatermark.Image*:StampWatermark.Live*:Cli.WatermarkCommand` passes under ASan.

- [ ] Commit: vector and image `Watermark & Stamp` engine (`src/core/stamp_watermark.cpp`, `recto-cli watermark`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=StampWatermark.*:Cli.WatermarkCommand` passes under ASan.

## 6. 3-zone `Headers & Footers` and legal **Bates Numbering** (`stamp_watermark.cpp` & CLI)

Extend `src/core/stamp_watermark.h/.cpp` with **6-Zone Headers & Footers** (`Top-Left`, `Top-Center`, `Top-Right`, `Bottom-Left`, `Bottom-Center`, `Bottom-Right`) and legal **Bates Numbering** (`apply_headers_footers_and_bates` and `recto-cli bates`):
- Expand dynamic tokens per page (`{page}`, `{total}`, `{date}`, `{filename}`, `{author}`, and `{bates}` formatted with configurable `prefix`, `suffix`, `start_number`, and zero-padded `digits` width `4..10` e.g. `CASE-000101`).
- Support sequential Bates numbering across multi-document batches (returning `next_bates_number` after each document so document 2 continues seamlessly from where document 1 ended).
- Implement **`Shrink page content to avoid overlapping existing text`**, scaling the original page content and annotations inward into the safe area between the header and footer bands so Bates stamps and page numbers never collide with existing text near the top or bottom page edges.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `HeaderFooterBatesOptions`, token expansion (`{page}`, `{total}`, `{date}`, `{filename}`, `{author}`, `{bates}`), shrink-to-avoid-overlap content scaling, and `apply_headers_footers_and_bates` (single-document and multi-document sequential batch) in `src/core/stamp_watermark.cpp`. Done when: `StampWatermark.HeadersFootersAndBates` stamps a 10-page PDF with `CASE-000101`..`CASE-000110` and a 2-file batch with continuous `000101..000115` numbering, and `PageText` readback extracts the exact Bates string on every page under ASan.
2. Implement `recto-cli bates <in1.pdf> [in2.pdf...] --out <out_or_dir> --prefix <str> [--start <N>] [--digits <4..10>] [--shrink-to-fit]` in `src/cli/cmd_watermark.cpp`. Done when: `Cli.BatesCommand` passes under ASan.

- [ ] Define `HeaderFooterZoneText` (`top_left`, `top_center`, `top_right`, `bottom_left`, `bottom_center`, `bottom_right`), `BatesConfig` (`enabled`, `prefix`, `suffix`, `start_number`, `digit_count` `4..10`), and `HeaderFooterBatesOptions` (`zones`, `bates`, `font_family`, `font_size_pt`, `color_rgba`, `margin_top_pt`, `margin_bottom_pt`, `margin_left_pt`, `margin_right_pt`, `shrink_content_to_avoid_overlap`, `start_page_number`, `scope_filter`, `page_indices`) in `src/core/stamp_watermark.h`. Done when: `src/core/stamp_watermark.h` compiles cleanly.
- [ ] Implement per-page token expansion in `src/core/stamp_watermark.cpp` replacing `{page}` (`1-based page + start_page_number - 1`), `{total}`, `{date}` (`YYYY-MM-DD`), `{filename}` (stem of source file), `{author}` (from `/Info /Author`), and `{bates}` (`<prefix><zero_padded_N><suffix>`). Done when: `StampWatermark.TokenExpansion` expands `"Page {page} of {total} | {bates}"` into `"Page 1 of 10 | EXH-000101"`.
- [ ] Implement multi-document sequential Bates continuity (`apply_bates_batch`): pass the running `current_bates_counter` across an ordered list of PDFs so a 5-page file followed by a 7-page file receives `000001..000005` and `000006..000012` without gaps or resets. Done when: `StampWatermark.MultiFileBatesBatch` verifies continuous Bates numbering across 3 documents.
- [ ] Implement `shrink_content_to_avoid_overlap`: when enabled, scale and translate the existing page `/Contents` stream and `/Annots` rects into `[margin_left, margin_bottom + footer_height, width - margin_right, height - margin_top - header_height]` before drawing the 6 header/footer zones at full size in the top and bottom margin bands. Done when: `StampWatermark.ShrinkToAvoidOverlap` stamps a full-bleed page and verifies zero bounding-box intersection between the original content and the Bates footer. Cheaper substitute: drawing headers/footers directly over full-bleed page text and ignoring the SPEC 9.9 `Shrink page content to avoid overlapping` option.
- [ ] Emit header/footer text runs using standard `BT ... Tf ... Tm (...) Tj ET` content-stream operators with `/ToUnicode` mapping so stamped Bates numbers are immediately searchable via `Ctrl+F` (`D03 T01 §5`) and extractable via `PageText` (`D03 T01 §1`). Done when: `SearchEngine` finds `"CASE-000107"` on page 7 of the stamped output.
- [ ] Implement `recto-cli bates` in `src/cli/cmd_watermark.cpp`. Done when: `recto-tests.exe --gtest_filter=StampWatermark.*:Cli.BatesCommand` passes under ASan.
- [ ] Commit: `organize: add blank page insertion, crop/resize, watermarks, headers/footers and Bates numbering`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PageOps.Crop*:StampWatermark.*:Cli.WatermarkCommand:Cli.BatesCommand` passes under ASan.

## 7. Windows CNG (`bcrypt.dll`) `AES-256` (`V=5, R=6`) encrypt/decrypt writer and CLI verbs

Extend `src/pdf/crypto.h/.cpp` (`D01 T01 §9`), `src/pdf/writer.cpp` (`D04 T02 §2`), and `src/core/page_ops.cpp` with native **PDF 2.0 `AES-256` (`V=5, R=6`, ISO 32000-2 section 7.6.4) encryption** and **decryption**:
- **Encrypt (`encrypt_pdf`):** Generate a cryptographically random 32-byte file encryption key via `BCryptGenRandom`, compute `/U`, `/UE`, `/O`, `/OE` (Algorithm 2.B KDF with SHA-256/384/512 and AES-CBC rounds), and `/Perms` (16-byte AES-ECB encrypted permission block with `"adb"` marker), encrypt every indirect string and stream with per-object random 16-byte IV + AES-256-CBC via Windows CNG (`bcrypt.dll`), write the `/Encrypt` dictionary (`V=5, R=6, Length=256`, `/StmF /StdCF`, `/StrF /StdCF`, `/P` permissions bitmask for `print`, `high_res_print`, `modify`, `copy`, `annotate_and_forms`, `fill_forms`, `assemble`), and commit via `AtomicFileWriter` (`D04 T02 §1`).
- **Decrypt (`decrypt_pdf`):** Open an encrypted PDF (`R=2..6`) with the supplied password (`user` or `owner`), return a typed `PdfErrorCode::PasswordRequired` / `InvalidPassword` refusal on wrong password, and write a clean unencrypted copy via `GraftMap` + `PdfWriter` + `AtomicFileWriter`.
- Expose `recto-cli encrypt` and `recto-cli decrypt`.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `PdfEncryptionWriteOptions` (`user_password`, `owner_password`, `PdfPermissions` flags), Algorithm 8/9/10 (`V=5, R=6` `/U`, `/UE`, `/O`, `/OE`, `/Perms` key synthesis) in `src/pdf/crypto.cpp`, and per-object string/stream AES-256-CBC encryption in `src/pdf/writer.cpp`. Done when: `PageOps.EncryptAes256` encrypts a corpus PDF with user + owner passwords and restricted print/copy permissions, re-opens it via `D01 T01 §9`, verifies `D02 T03 §9`'s security summary reports `AES-256` and the exact permission flags, and renders pixel-identically to the source under ASan.
2. Implement `decrypt_pdf` in `src/core/page_ops.cpp` plus `recto-cli encrypt` and `recto-cli decrypt`. Done when: `PageOps.DecryptRoundTripAndRefusal` and `Cli.EncryptAndDecrypt` pass under ASan.

- [ ] Define `PdfPermissions` (`allow_print`, `allow_high_quality_print`, `allow_modify_contents`, `allow_copy_and_extract`, `allow_annotations_and_forms`, `allow_fill_forms`, `allow_assemble_pages`, `encrypt_metadata`) and `PdfEncryptionWriteOptions` in `src/pdf/crypto.h`. Done when: `PdfPermissions::to_p_bitmask()` encodes the exact ISO 32000-2 Table 22 32-bit signed `/P` integer.
- [ ] Implement ISO 32000-2 section 7.6.4.4 Algorithms 8, 9, and 10 in `src/pdf/crypto.cpp` using `BCryptGenRandom` (for the 32-byte file key, 8-byte validation salts, and 8-byte key salts), Algorithm 2.B (`D01 T01 §9`), AES-256-CBC (`/UE` and `/OE`), and AES-256-ECB (`/Perms` 16-byte block: `P_low32`, `0xFFFFFFFF`, `'T'|'F'` metadata flag, `'a','d','b'`, and 4 random bytes). Done when: `PdfCrypto.GenerateR6KeysRoundTrip` generates an `/Encrypt` dictionary that validates with both the user password and owner password and verifies `/Perms`. Cheaper substitute: falling back to legacy 128-bit RC4 or AES-128 (`R=4`) instead of `AES-256` (`V=5, R=6`).
- [ ] Integrate `PdfEncryptionWriteOptions` into `PdfWriter` (`src/pdf/writer.cpp`): encrypt all indirect string literals and stream bodies (after `FlateDecode` compression) with a fresh 16-byte random IV (`BCryptGenRandom`) and AES-256-CBC PKCS#7 padding, while leaving the `/Encrypt` dictionary itself and trailer `/ID` strings unencrypted per ISO 32000-2. Done when: `PageOps.EncryptAes256` writes an encrypted PDF whose raw file bytes contain zero plaintext page strings.
- [ ] Implement `encrypt_pdf(src_path, src_password, PdfEncryptionWriteOptions, dst_path, CancelToken)` and `decrypt_pdf(src_path, password, dst_path, CancelToken)` in `src/core/page_ops.cpp` using `AtomicFileWriter` (`D04 T02 §1`). Done when: encrypting a corpus file and decrypting the result reproduces pages that render pixel-identically (`expect_rgba_equal`) to the original, and an incorrect password returns `PdfErrorCode::InvalidPassword` without creating any file.
- [ ] Verify that `D02 T03 §9`'s `DocumentSecurityInfo` readback on the encrypted output reports `encryption_method = "AES-256"`, `is_encrypted = true`, and the exact boolean permission flags chosen at encryption time. Done when: `PageOps.PermissionFlagsReadback` verifies every permission flag combination.
- [ ] Implement `recto-cli encrypt <in.pdf> --out <out.pdf> [--user-password <pw>] [--owner-password <pw>] [--no-print] [--no-copy] [--no-edit]` and `recto-cli decrypt <in.pdf> --password <pw> --out <out.pdf>` in `src/cli/cmd_crypto.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfCrypto.*:PageOps.Encrypt*:PageOps.Decrypt*:PageOps.Permission*:Cli.EncryptAndDecrypt` passes under ASan.
- [ ] Commit: `organize: AES-256 encrypt and decrypt with permissions`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCrypto.*:PageOps.Encrypt*:PageOps.Decrypt*:PageOps.Permission*:Cli.EncryptAndDecrypt` passes under ASan.

## 8. X.509 / PKCS#12 and Windows Certificate Store `PKCS#7` & `PAdES` signer (`cli sign`)

Implement the cryptographic PDF signing engine in `src/pdf/signature.h` and `src/pdf/signature.cpp` (`recto::pdf::SignatureEngine::sign_document`) and `recto-cli sign`:
- Load X.509 certificates and private keys from **PKCS#12 (`.pfx` / `.p12`)** files (`PFXImportCertStore`), the **Windows Certificate Store** (`CertOpenSystemStoreW(0, L"MY")`, supporting hardware tokens/smartcards via NCrypt `CryptAcquireCertificatePrivateKey`), or generate a self-signed X.509 RSA-2048/ECDSA-P256 certificate via `CertCreateSelfSignCertificate` (`crypt32.dll` / `bcrypt.dll` / `ncrypt.dll`).
- Synthesize visible `/AP /N` signature appearance streams (handwritten ink paths, uploaded signature image, or standard text stamp + signer CN, date, reason, location) or invisible `/Rect [0 0 0 0]` signatures.
- Write the `/FT /Sig` widget annotation and `/Type /Sig` dictionary (`/Filter /Adobe.PPKLite`, `/SubFilter /adbe.pkcs7.detached` or `/ETSI.CAdES.detached` for **PAdES** `B-B`/`B-T`/`B-LT`, `/M`, `/Name`, `/Reason`, `/Location`, `/ContactInfo`, optional `/DocMDP` `/TransformMethod` certification `P=1|2|3`) via **`IncrementalWriter`** (`D01 T02 §5`) with a two-pass `/ByteRange [0 offset1 offset2 len2]` covering the entire file except the `<hex>` `/Contents` placeholder, sign the byte-range digest (`SHA-256`/`SHA-384`/`SHA-512`) via `CryptSignMessage` / `CMSG_SIGNED` (including the `id-aa-signingCertificateV2` ESS attribute for PAdES), optionally attach an RFC 3161 Time-Stamp Authority (`TSA`) token and `/DSS` (Document Security Store) revocation data, and commit via `AtomicFileWriter`.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement certificate loading (`.pfx`/`.p12`, Windows `"MY"` Cert Store, and CNG self-signed certificate generation), visible `/AP /N` signature appearance synthesis, `/ByteRange` two-pass incremental serialization (`D01 T02 §5`), and CMS/PAdES detached signing (`SHA-256`/`384`/`512`) in `src/pdf/signature.cpp`. Done when: `PdfSignature.SignSingleAndCounterSign` generates a CNG X.509 certificate, signs a PDF twice via incremental save (two independent counter-signatures), and produces valid `/ByteRange` and CMS structures under ASan.
2. Implement `/DocMDP` certification permissions (`P=1, 2, 3`), RFC 3161 timestamp token embedding (`id-aa-signatureTimeStampToken`), `/DSS` (`/Certs`, `/OCSPs`, `/CRLs`) embedding for `PAdES B-LT`, and `recto-cli sign`. Done when: `PdfSignature.DocMdpAndPades` and `Cli.SignCommand` pass under ASan.

- [ ] Define `SignatureSubFilter` (`Pkcs7Detached`, `PadesCadesDetached`), `SignatureHashAlgorithm` (`Sha256`, `Sha384`, `Sha512`), `DocMdpPermission` (`None`, `NoChangesAllowed_P1`, `FillFormsAndSign_P2`, `AnnotateFillFormsAndSign_P3`), `SignatureAppearanceConfig` (`is_visible`, `page_index`, `rect_pt`, `ink_strokes`, `image_bytes`, `show_signer_details`, `custom_title`), and `PdfSignOptions` in `src/pdf/signature.h`. Done when: `src/pdf/signature.h` compiles cleanly across `recto-pdf`, `recto-core`, `recto-services`, and `recto-cli`.
- [ ] Implement certificate enumeration (`list_windows_personal_certificates`), PKCS#12 `.pfx`/`.p12` import (`PFXImportCertStore` with `CRYPT_EXPORTABLE` / NCrypt key handle cleanup), and `create_self_signed_certificate(common_name, org, validity_days)` using Windows CryptoAPI / CNG (`crypt32.dll`, `ncrypt.dll`, `bcrypt.dll`). Done when: `PdfSignature.CertStoreAndPfx` generates a self-signed cert, exports it to an in-memory `.pfx` with a password, and re-imports it cleanly.
- [ ] Synthesize deterministic `/AP /N` Form XObject streams for visible signatures (`SignatureAppearanceConfig`): left/top graphic (handwritten Bezier ink strokes, embedded image XObject, or bold signer monogram) + right/bottom text block (`"Digitally signed by <CommonName>"`, `"Date: YYYY-MM-DD HH:MM:SS UTC"`, `"Reason: ..."`, `"Location: ..."`). Done when: visible signatures rasterize crisply through `DisplayList` (`D01 T02 §4`).
- [ ] Write the signature revision exclusively through `IncrementalWriter` (`D01 T02 §5`) with a fixed-width `/ByteRange` array and pre-sized `/Contents <0000...>` hex buffer (`32768` hex chars = `16 KB`), patch the exact `[0, off1, off2, len2]` integers in place, hash the two byte ranges (`[0..off1)` and `[off2..off2+len2)`) with CNG `BCryptHashData` (`SHA-256`/`384`/`512`), and write the DER-encoded CMS `SignedData` into `/Contents` padded with trailing `00` hex bytes. Done when: `off1 + contents_token_len == off2` and `off2 + len2 == total_file_size` to the exact byte. Cheaper substitute: drawing a visual signature stamp without a cryptographic `/ByteRange` CMS signature dictionary, or rewriting the whole file so prior signatures break.
- [ ] Support **PAdES (`ETSI.CAdES.detached`)** by including the `id-aa-signingCertificateV2` (`1.2.840.113549.1.9.16.2.47`) signed attribute containing the `ESSCertIDv2` SHA-256 hash of the signing certificate, plus optional `/DocMDP` `/Reference` dictionary (`P=1, 2, 3`), RFC 3161 TSA token (`WinHttp` or injectable TSA callback seeded from `D02 T04 §6`'s default timestamp URL), and `/DSS` dictionary. Done when: `PdfSignature.PadesAndDocMdp` verifies the `id-aa-signingCertificateV2` attribute and `/DocMDP` `/Perms` link.
- [ ] Implement `recto-cli sign <in.pdf> --out <out.pdf> [--pfx <file.pfx> --password <pw> | --cert-thumbprint <hex> | --self-signed <cn>] [--pades] [--hash sha256|sha384|sha512] [--reason <str>] [--location <str>] [--rect <page,x,y,w,h>]` in `src/cli/cmd_signature.cpp`. Done when: `Cli.SignCommand` signs a PDF from the command line under ASan.

- [ ] Commit: x.509 / PKCS#12 and Windows Certificate Store cryptographic PDF signer (`PKCS#7` & `PAdES`, `/ByteRange` incremental save, RFC 3161 TSA, `/DSS`, `/DocMDP`, `recto-cli sign`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfSignature.Cert*:PdfSignature.Sign*:PdfSignature.Pades*:Cli.SignCommand` passes under ASan.

## 9. Cryptographic signature verifier (`/ByteRange`, CMS/CAdES, chain trust & tamper check)

Implement `SignatureEngine::verify_signatures(const pdf::Document&)` in `src/pdf/signature.cpp` and `recto-cli verify <in.pdf> [--json]`:
- Enumerate all `/FT /Sig` fields across the document in `/ByteRange` chronological order.
- Recompute the cryptographic hash over each signature's `/ByteRange [0, off1, off2, len2]` directly from the raw file bytes and verify the CMS/PKCS#7 or PAdES/CAdES `SignedData` cryptographic signature via `CryptVerifyDetachedMessageSignature` (`crypt32.dll`).
- Build and validate the signer's X.509 certificate chain via `CertGetCertificateChain` and `CertVerifyCertificateChainPolicy`, checking embedded `/DSS` OCSP/CRL responses and RFC 3161 timestamp tokens.
- Inspect any bytes appended after `off2 + len2` (incremental updates after signing) to classify whether subsequent revisions are valid counter-signatures/allowed form fills or unauthorized document modifications (`Tampered`), and return an overall document `SignatureVerificationSummary` (`AllValid`, `ValidWithUntrustedCertOrAllowedChanges`, `InvalidOrTampered`).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `verify_signatures` in `src/pdf/signature.cpp` checking `/ByteRange` bounds, message digest match, CMS signature verification, X.509 chain trust (`CertGetCertificateChain`), RFC 3161 timestamp verification, and post-signature incremental revision analysis, plus `recto-cli verify <in.pdf> [--json]`. Done when: `PdfSignature.VerifyAndTamperDetection` verifies single-signed and counter-signed PDFs as valid, flags self-signed untrusted roots with `ValidSignatureUntrustedRoot`, and immediately flags `InvalidTampered` if a single byte in the signed range is altered or an unauthorized page edit is appended under ASan.

- [ ] Define `SignatureTrustStatus` (`ValidTrusted`, `ValidUntrustedRoot`, `ValidButUnsignedChangesAppended`, `InvalidDigestMismatch`, `InvalidCryptographicSignature`, `InvalidByteRange`), `VerifiedSignatureInfo` (`field_name`, `page_index`, `rect_pt`, `signer_common_name`, `signer_email`, `issuer_common_name`, `serial_number_hex`, `thumbprint_sha256`, `signing_time_utc`, `has_timestamp_token`, `timestamp_authority`, `sub_filter`, `hash_algorithm`, `reason`, `location`, `doc_mdp_permission`, `covers_entire_file`, `status`, `status_message`), and `SignatureVerificationSummary` in `src/pdf/signature.h`. Done when: `SignatureVerificationSummary` maps 1-to-1 to the 3 SPEC 8.2 banner states (`--success`, `--warning`, `--danger`).
- [ ] Validate `/ByteRange [b0, l0, b1, l1]` structure strictly against hostile inputs: require `b0 == 0`, `b0 + l0 < b1`, `b1 + l1 <= file_size`, and verify that `[b0 + l0, b1)` exactly spans the `<...>` `/Contents` hex token with zero unhashed gap bytes outside `/Contents`. Done when: `PdfSignature.HostileByteRange` rejects a signature whose `/ByteRange` skips a page stream outside `/Contents`.
- [ ] Verify the detached CMS `SignedData` over the two `/ByteRange` slices using `CryptVerifyDetachedMessageSignature` (`crypt32.dll`), extracting the signer `CERT_CONTEXT`, signing time (`szOID_RSA_signingTime`), and any RFC 3161 timestamp token (`1.2.840.113549.1.9.16.2.14`). Done when: flipping a single bit in page content or inside the `/Contents` CMS signature transitions status to `InvalidDigestMismatch` or `InvalidCryptographicSignature`. Cheaper substitute: parsing `/Name` and `/M` from the `/Sig` dictionary without running cryptographic digest and CMS signature verification.
- [ ] Build the X.509 certificate chain via `CertGetCertificateChain` (feeding any intermediate certificates embedded in the CMS bag and `/DSS /Certs` into an additional in-memory `HCERTSTORE` and supporting a test-root trust store override for unit tests). Done when: a signature chained to a trusted root reports `ValidTrusted`, while an unknown self-signed certificate reports `ValidUntrustedRoot`.
- [ ] Analyze post-signature incremental revisions when `b1 + l1 < file_size`: if the subsequent incremental update only adds another valid counter-signature (or fills form fields allowed by `/DocMDP P >= 2`), keep the earlier signature valid; if a subsequent update modifies page `/Contents` or violates `/DocMDP P=1`, flag the modification. Done when: `PdfSignature.CounterSignatureAndDocMdp` verifies a 2-signature incremental chain keeps Signature 1 `Valid` while appending an unauthorized page edit flags tampering.
- [ ] Wire `DocumentActor` (`D01 T02 §8`) to run `verify_signatures` automatically on document open when Settings `Verify signatures when opening` (`D02 T04 §6`) is enabled. Done when: opening a signed PDF populates `DocumentActor::signature_summary()`.
- [ ] Implement `recto-cli verify <in.pdf> [--json]` in `src/cli/cmd_signature.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfSignature.*:Cli.VerifyCommand` passes under ASan.

- [ ] Commit: cryptographic PDF signature verifier (`/ByteRange` digest check, CMS/CAdES validation, `CertGetCertificateChain` trust & revocation, tamper detection, `recto-cli verify`)

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfSignature.*:Cli.SignCommand:Cli.VerifyCommand` passes under ASan.

## 10. True physical content-stream redactor (glyph/path/image scrubbing) & `sanitize_document`

Implement `src/pdf/redactor.h` and `src/pdf/redactor.cpp` (`recto::pdf::Redactor::apply_redactions` and `sanitize_document`) and their CLI commands `recto-cli redact` and `recto-cli sanitize`:
- **True Physical Redaction (`apply_redactions`):** Given page redaction marks (`page_index`, `rect_pt`, fill colour, optional exemption overlay text e.g. `REDACTED` / `FOIA (b)(6)`), rewrite each affected page's `/Contents` stream and referenced Form XObjects:
  1. **Text runs (`Tj`, `TJ`, `'`, `"`):** Use `PageText` character quads (`D03 T01 §1`) to identify every character whose quad intersects a redaction box, split the `Tj`/`TJ` operator into pre-redaction and post-redaction positioned sub-runs (`Td`/`Tm`), and **physically delete** the intersecting character codes from the stream.
  2. **Vector paths (`m`, `l`, `c`, `re`):** Remove or clip vector subpaths that lie inside a redaction rectangle.
  3. **Embedded images (`/Subtype /Image`):** Transform each redaction rectangle through the inverse of the image's placement CTM (`cm^-1`) into image pixel space and overwrite the intersecting pixel region with solid fill in a newly written image XObject stream (so extracting the image XObject never reveals the redacted area).
  4. **Annotations, Structure & Outlines:** Remove intersecting `/Annots`, strip redacted text from `/StructTreeRoot` and `/Outlines`, paint the opaque burn-in rectangle and optional exemption label, and write a fresh full-rewrite PDF via `PdfWriter` (`D04 T02 §2`) so zero historical incremental revisions remain.
- **Document Sanitization (`sanitize_document`):** Strip `/Info`, `/Metadata` XMP streams, `/Names /JavaScript`, `/OpenAction`, `/AA`, `/Launch`, `/Names /EmbeddedFiles`, `/Annots` comments/markup, `/AcroForm` / hidden fields, `/OCProperties` hidden layers, `/PieceInfo`, and page `/Thumb` thumbnails, and compact the cross-reference table via full `PdfWriter` rewrite.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `Redactor::apply_redactions` (glyph-run splitting and deletion, vector path removal, image XObject pixel scrubbing, annotation/outline scrubbing, burn-in overlay drawing, and full `PdfWriter` rewrite) in `src/pdf/redactor.cpp`. Done when: `PdfRedactor.PhysicalTextAndImageRemoval` redacts a target word (`"CONFIDENTIAL-SECRET-42"`) inside a multi-word `Tj`/`TJ` run and a region of an embedded image, and proves that (a) neighboring unredacted words on the same line remain in place, (b) `PageText` extraction does not contain `"CONFIDENTIAL-SECRET-42"`, and (c) decompressing every stream in the output PDF and searching raw bytes finds zero occurrences of `"CONFIDENTIAL-SECRET-42"` under ASan.
2. Implement `Redactor::sanitize_document` in `src/pdf/redactor.cpp`, plus `recto-cli redact` and `recto-cli sanitize`. Done when: `PdfRedactor.SanitizeDocument` and `Cli.RedactAndSanitize` pass under ASan.

- [ ] Define `RedactionMark` (`page_index`, `rect_pt`, `fill_color_rgba`, `overlay_text`, `text_color_rgba`), `SanitizeOptions` (`strip_metadata_and_xmp`, `strip_javascript_and_actions`, `strip_embedded_files`, `strip_annotations_and_comments`, `flatten_or_strip_forms`, `strip_hidden_layers_ocg`, `strip_thumbnails_and_piece_info`), and `RedactionReport` (`redacted_char_count`, `redacted_path_count`, `scrubbed_image_count`, `removed_annot_count`) in `src/pdf/redactor.h`. Done when: `src/pdf/redactor.h` compiles cleanly.
- [ ] Implement physical glyph-run splitting in `src/pdf/redactor.cpp`: when a `Tj` or `TJ` operator spans `[char_0 .. char_k]` and a subset `[char_i .. char_j]` intersects a `RedactionMark::rect_pt`, emit the surviving prefix `[0 .. i-1]` at the original `Tm`, advance horizontal position by the exact width of the deleted characters `[i .. j]`, and emit the surviving suffix `[j+1 .. k]` so unredacted text to the right of the redaction box does not shift left into the redacted gap. Done when: `PdfRedactor.GlyphRunSplitAndAdvance` redacts the middle word of a 5-word `Tj` string, deletes the middle word's bytes completely, and keeps the 4th and 5th words at their exact original X coordinates. Cheaper substitute: drawing a black rectangle over text while leaving the underlying `Tj`/`TJ` string in the content stream (forbidden by `AGENTS.md` and `foundation plan section 9`).
- [ ] Implement vector path removal and embedded `/Subtype /Image` pixel scrubbing: map each `RedactionMark::rect_pt` through `CTM^-1` into each placed image XObject's `[0..Width, 0..Height]` pixel grid, paint the intersecting pixel box with solid `fill_color_rgba`, and write a new replacement `/Subtype /Image` XObject stream (so `ImageExtractor` in `D04 T02 §9` cannot recover the scrubbed pixels). Done when: `PdfRedactor.ImagePixelScrubbing` redacts the top-half of an embedded photo, extracts the image via `ImageExtractor`, and verifies the top-half pixels are solid black.
- [ ] Draw the burn-in redaction rectangle (`re f`) and centered `overlay_text` (e.g., `"REDACTED"` or `"FOIA (b)(6)"` scaled to fit the box) at the end of the page `/Contents` stream, and write the output via a full `PdfWriter` rewrite (never an incremental update, which would leave the unredacted original stream in earlier file bytes). Done when: searching raw file bytes and all decompressed streams in the output PDF finds `0` occurrences of the redacted string.
- [ ] Implement `Redactor::sanitize_document(const pdf::Document&, SanitizeOptions, dst_path, CancelToken)` stripping `/Info`, `/Root /Metadata`, `/Names /JavaScript`, `/OpenAction`, `/AA`, `/Launch`, `/Names /EmbeddedFiles`, `/Annots`, `/OCProperties`, `/PieceInfo`, and `/Thumb`, and rewriting a clean single-revision `xref` table via `PdfWriter` and `AtomicFileWriter`. Done when: `PdfRedactor.SanitizeDocument` takes a fixture containing XMP, `/Info`, `/JS`, an embedded attachment, comments, and 2 incremental revisions and produces a sanitized single-revision PDF with all selected categories eradicated.
- [ ] Implement `recto-cli redact <in.pdf> --out <out.pdf> [--rect <page,x,y,w,h>...] [--find <pattern> [--regex]] [--label <text>]` and `recto-cli sanitize <in.pdf> --out <out.pdf>` in `src/cli/cmd_redact.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfRedactor.*:Cli.RedactAndSanitize` passes under ASan.
- [ ] Commit: `pdf: add cryptographic digital signatures (PKCS#7/PAdES) and true physical redactor`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfSignature.*:PdfRedactor.*:Cli.SignCommand:Cli.VerifyCommand:Cli.RedactAndSanitize` passes under ASan.

## Verification

- M4's merge, split, reorder, rotate, delete, blank-page insert, crop/resize, watermarks, headers/footers, Bates numbering, encrypt/decrypt, digital signatures, and true redaction/sanitization each pass under ASan and round-trip in `D06 T01 §6`'s suite.
- No operation path modifies the open viewer's document instance in place.
- `python scripts/todo-graph.py validate` passes.
